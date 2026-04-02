#include "core/MatchingEngine.h"

#include <algorithm>

namespace exchange {

MatchingEngine::MatchingEngine() = default;

// ============================================================
// GET OR CREATE ORDER BOOK
// ============================================================
// Each stock symbol gets its own order book. If this is the first
// order for "AAPL", we create a new empty OrderBook for it.
// The try_emplace is like Python's dict.setdefault() — it only
// creates the value if the key doesn't already exist.
// ============================================================
OrderBook& MatchingEngine::get_order_book(const std::string& symbol) {
    auto [it, _] = books_.try_emplace(symbol, symbol);
    return it->second;
}

void MatchingEngine::set_trade_callback(TradeCallback callback) {
    trade_callback_ = std::move(callback);
}

// ============================================================
// SUBMIT ORDER — Main entry point
// ============================================================
// This is where all the magic happens. Let's walk through it:
//
// 1. VALIDATE: Reject garbage orders (zero quantity, negative price)
// 2. MATCH: Try to fill the order against existing orders
// 3. PLACE: If there's leftover quantity, put it on the book
//
// The mutex lock ensures thread safety — only one order at a time.
// In production, you'd use a single-threaded event loop instead.
// ============================================================
OrderId MatchingEngine::submit_order(const std::string& symbol, Side side,
                                      OrderType type, Price price,
                                      Quantity quantity) {
    Timestamp start = now_ns();  // Start the latency timer

    std::lock_guard<std::mutex> lock(mutex_);
    // lock_guard is RAII — it acquires the lock in the constructor
    // and releases it when it goes out of scope (end of function).
    // This means the lock is ALWAYS released, even if we throw an
    // exception. Manual lock/unlock is error-prone — always use RAII.

    OrderBook& book = get_order_book(symbol);
    stats_.total_orders++;

    // ---- VALIDATION ----
    // Reject orders that don't make sense.
    // In a real exchange, there are dozens more checks (self-trade
    // prevention, price bands, lot sizes, etc.)
    if (quantity == 0) {
        stats_.total_rejects++;
        return 0;  // 0 = invalid order ID
    }

    // Market orders don't specify a price (they take whatever's available).
    // Limit orders MUST have a positive price.
    if (type == OrderType::Limit && price <= 0.0) {
        stats_.total_rejects++;
        return 0;
    }

    // ---- CREATE THE ORDER ----
    // Add it to the book (this gets an object from the pool).
    // For market orders, we use a special price:
    //   Market BUY = max possible price (will match any ask)
    //   Market SELL = 0 (will match any bid)
    Price effective_price = price;
    if (type == OrderType::Market) {
        effective_price = (side == Side::Buy)
            ? std::numeric_limits<Price>::max()
            : 0.0;
    }

    Order* order = book.add_order(side, type, effective_price, quantity);
    OrderId id = order->id;

    // ---- MATCHING ----
    // Try to fill this order against the opposite side of the book.
    // match_order() returns all the trades that happened.
    std::vector<Trade> trades = match_order(book, order);

    // Notify external systems about each trade
    for (const Trade& trade : trades) {
        stats_.total_trades++;
        stats_.total_volume += trade.quantity;
        stats_.total_notional += trade.price * trade.quantity;

        if (trade_callback_) {
            trade_callback_(trade);
        }
    }

    // ---- POST-MATCH HANDLING ----
    // What happens to the leftover quantity depends on order type:
    //
    // LIMIT: Sits on the book until filled or cancelled.
    //        Already on the book from add_order(), so nothing to do.
    //
    // MARKET: Should NEVER rest on the book. If there weren't enough
    //         sellers/buyers, the remainder is cancelled.
    //         (In real life, some exchanges reject unfilled market
    //         orders; others fill partially. We cancel the remainder.)
    //
    // IOC: "Immediate or Cancel" — fill what you can, cancel the rest.
    //      Same behavior as market for the unfilled portion.
    if (order->is_filled()) {
        // Fully filled — remove from the book (it was added before matching)
        book.remove_order(order);
    } else if (type == OrderType::Market || type == OrderType::IOC) {
        // Market/IOC orders should never rest on the book.
        // Cancel whatever didn't fill.
        order->status = (order->filled_qty() > 0)
            ? OrderStatus::PartiallyFilled
            : OrderStatus::Cancelled;
        book.remove_order(order);
    }
    // Limit orders with remaining quantity stay on the book (already there)

    // ---- RECORD LATENCY ----
    Timestamp end = now_ns();
    uint64_t latency = end - start;
    stats_.total_latency_ns += latency;
    stats_.min_latency_ns = std::min(stats_.min_latency_ns, latency);
    stats_.max_latency_ns = std::max(stats_.max_latency_ns, latency);

    return id;
}

// ============================================================
// CANCEL ORDER
// ============================================================
bool MatchingEngine::cancel_order(const std::string& symbol, OrderId order_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = books_.find(symbol);
    if (it == books_.end()) return false;

    bool cancelled = it->second.cancel_order(order_id);
    if (cancelled) stats_.total_cancels++;
    return cancelled;
}

// ============================================================
// MATCH ORDER — The core matching loop
// ============================================================
// This is the HOTTEST code path in the entire exchange.
// Every nanosecond matters here. Let's trace through it:
//
// For a BUY order:
//   1. Look at the best (lowest) ask price level
//   2. Can we match? (buy price >= ask price)
//   3. Walk through orders at that price level (time priority)
//   4. Execute matches, reducing quantities
//   5. Move to the next price level if this one is exhausted
//   6. Stop when: order is filled, or no more matchable prices
//
// For a SELL order: same but reversed (match against bids).
//
// IMPORTANT: The trade price is ALWAYS the resting order's price,
// not the incoming order's price. This is how all exchanges work.
// If you submit a buy at $101 and the best ask is $100, you get
// filled at $100 (better for you!). This is called "price improvement."
// ============================================================
std::vector<Trade> MatchingEngine::match_order(OrderBook& book, Order* incoming) {
    std::vector<Trade> trades;

    // Determine which side of the book to match against.
    // Buy orders match against asks (sellers).
    // Sell orders match against bids (buyers).
    bool is_buy = (incoming->side == Side::Buy);

    while (incoming->remaining_qty > 0) {
        // Get the best price level on the opposite side
        PriceLevel* opposite_level = is_buy
            ? book.best_ask_level()
            : book.best_bid_level();

        // No more orders on the opposite side? Stop matching.
        if (!opposite_level || opposite_level->empty()) break;

        // Get the best resting order at this price level (front = earliest = FIFO)
        Order* resting = opposite_level->front();

        // ---- PRICE CHECK ----
        // Can these two orders trade?
        // BUY: incoming price must be >= resting ask price
        //      (buyer willing to pay at least what seller wants)
        // SELL: incoming price must be <= resting bid price
        //       (seller willing to accept at most what buyer offers)
        bool price_match = is_buy
            ? (incoming->price >= resting->price)
            : (incoming->price <= resting->price);

        if (!price_match) break;  // No more matchable prices — done

        // ---- EXECUTE THE MATCH ----
        Trade trade = execute_match(incoming, resting);
        trades.push_back(trade);

        // If the resting order is fully filled, remove it from the book
        if (resting->is_filled()) {
            resting->status = OrderStatus::Filled;
            book.remove_order(resting);
        }
    }

    // Update incoming order's status
    if (incoming->is_filled()) {
        incoming->status = OrderStatus::Filled;
    } else if (incoming->filled_qty() > 0) {
        incoming->status = OrderStatus::PartiallyFilled;
    }

    return trades;
}

// ============================================================
// EXECUTE MATCH — Single trade between two orders
// ============================================================
// This is the atomic unit of matching: one incoming order meets
// one resting order. We figure out:
//   1. How many shares trade (the minimum of what each wants)
//   2. At what price (the resting order's price)
//   3. Generate the Trade record
//   4. Update both orders' remaining quantities
//
// EXAMPLE:
//   Incoming BUY: 100 shares remaining
//   Resting SELL: 60 shares remaining, price $150.00
//
//   Trade: 60 shares @ $150.00
//   Incoming now has 40 remaining
//   Resting now has 0 remaining (fully filled)
// ============================================================
Trade MatchingEngine::execute_match(Order* incoming, Order* resting) {
    // Trade quantity = smaller of the two remaining quantities
    Quantity fill_qty = std::min(incoming->remaining_qty, resting->remaining_qty);

    // Trade price = resting order's price (price improvement for incoming)
    Price fill_price = resting->price;

    // Deduct the filled quantity from both orders
    incoming->remaining_qty -= fill_qty;
    resting->remaining_qty -= fill_qty;

    // Build the trade record
    Trade trade;
    trade.timestamp = now_ns();
    trade.price = fill_price;
    trade.quantity = fill_qty;

    // The buy_order_id and sell_order_id fields tell both parties
    // what happened — "your order #X traded against order #Y"
    if (incoming->side == Side::Buy) {
        trade.buy_order_id = incoming->id;
        trade.sell_order_id = resting->id;
    } else {
        trade.buy_order_id = resting->id;
        trade.sell_order_id = incoming->id;
    }

    return trade;
}

} // namespace exchange
