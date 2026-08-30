#include "core/OrderBook.h"

namespace exchange {

OrderBook::OrderBook(const std::string& symbol, size_t pool_size)
    : symbol_(symbol)
    , order_pool_(pool_size)
{}

// ============================================================
// ADD ORDER
// ============================================================
// Step by step:
// 1. Get an Order object from the pool (O(1), no heap alloc)
// 2. Fill in its fields
// 3. Look up (or create) the price level in the map (O(log N))
// 4. Append to the back of that level's list (O(1))
//    Back = lowest time priority = FIFO ordering
// 5. Store a lookup entry so we can cancel in O(1) later
//
// WHY we append to the BACK of the list:
// Price-time priority means earlier orders match first.
// list.front() = earliest order = matches first.
// list.back() = newest order = matches last.
// So new orders go to the back.
// ============================================================
Order* OrderBook::add_order(Side side, OrderType type, Price price, Quantity quantity) {
    // Step 1: Get a recycled Order from the pool
    Order* order = order_pool_.acquire();

    // Step 2: Initialize it (placement-style — we reuse the memory)
    *order = Order(next_order_id_++, side, type, price, quantity);

    // Step 3-4: Insert into the correct side of the book
    if (side == Side::Buy) {
        // operator[] on std::map creates the key if it doesn't exist.
        // So if there's no price level at $150, one is created automatically.
        // Then we push_back the order pointer into that level's list.
        bids_[price].push_back(order);

        // Step 5: Save the iterator for O(1) cancel later.
        // prev(end()) points to the element we just push_back'd.
        auto it = std::prev(bids_[price].end());
        order_lookup_[order->id] = {side, price, it};
    } else {
        asks_[price].push_back(order);
        auto it = std::prev(asks_[price].end());
        order_lookup_[order->id] = {side, price, it};
    }

    return order;
}

// ============================================================
// CANCEL ORDER — O(1) average case
// ============================================================
// This is where the order_lookup_ map pays off.
// Without it: search every price level, every order = O(N*M)
// With it: one hash lookup + one list erase = O(1)
//
// Real exchanges process millions of cancels per second.
// HFT firms submit 10-100x more cancels than actual trades.
// So cancel performance is arguably MORE important than add.
// ============================================================
bool OrderBook::cancel_order(OrderId order_id) {
    // Step 1: Look up the order's location
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        return false;  // Order not found (already filled or never existed)
    }

    OrderLocation& loc = it->second;

    // Step 2: Get the order pointer and mark it cancelled
    Order* order = *loc.iterator;
    order->status = OrderStatus::Cancelled;

    // Step 3: Remove from the price level's linked list
    // std::list::erase is O(1) when you have the iterator.
    // This is why we use std::list instead of std::vector —
    // vector erase is O(N) because it shifts all subsequent elements.
    if (loc.side == Side::Buy) {
        bids_[loc.price].erase(loc.iterator);
    } else {
        asks_[loc.price].erase(loc.iterator);
    }

    // Step 4: Clean up empty price levels
    cleanup_price_level(loc.side, loc.price);

    // Step 5: Remove from lookup map and return to pool
    order_lookup_.erase(it);
    order_pool_.release(order);

    return true;
}

// ============================================================
// REDUCE ORDER — shrink quantity while holding queue position
// ============================================================
// Exchange convention: reducing displayed quantity keeps the order's
// place in the FIFO queue at its level, because it takes liquidity
// away from the book and disadvantages nobody behind it. Increasing
// quantity or changing price does not, and is implemented as a cancel
// followed by a new order, which goes to the back of the queue.
// ============================================================
bool OrderBook::reduce_order(OrderId order_id, Quantity new_quantity) {
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) return false;

    Order* order = *it->second.iterator;

    // Can only reduce, not increase
    if (new_quantity >= order->remaining_qty) return false;

    if (new_quantity == 0) {
        return cancel_order(order_id);
    }

    order->remaining_qty = new_quantity;
    return true;
}

// ============================================================
// REMOVE ORDER — Called by MatchingEngine after a full fill
// ============================================================
void OrderBook::remove_order(Order* order) {
    auto it = order_lookup_.find(order->id);
    if (it == order_lookup_.end()) return;

    OrderLocation& loc = it->second;

    if (loc.side == Side::Buy) {
        bids_[loc.price].erase(loc.iterator);
    } else {
        asks_[loc.price].erase(loc.iterator);
    }

    cleanup_price_level(loc.side, loc.price);
    order_lookup_.erase(it);
    order_pool_.release(order);
}

// ============================================================
// ACCESSORS — Best prices, spread, depth
// ============================================================

Price OrderBook::best_bid() const {
    // bids_ is sorted descending (greatest first), so begin() = highest price
    if (bids_.empty()) return 0.0;
    return bids_.begin()->first;
}

Price OrderBook::best_ask() const {
    // asks_ is sorted ascending (least first), so begin() = lowest price
    if (asks_.empty()) return 0.0;
    return asks_.begin()->first;
}

Price OrderBook::spread() const {
    Price bid = best_bid();
    Price ask = best_ask();
    if (bid == 0.0 || ask == 0.0) return 0.0;
    return ask - bid;
}

Price OrderBook::mid_price() const {
    Price bid = best_bid();
    Price ask = best_ask();
    if (bid == 0.0 || ask == 0.0) return 0.0;
    return (bid + ask) / 2.0;
}

Quantity OrderBook::total_bid_quantity() const {
    Quantity total = 0;
    for (const auto& [price, level] : bids_) {
        for (const Order* order : level) {
            total += order->remaining_qty;
        }
    }
    return total;
}

Quantity OrderBook::total_ask_quantity() const {
    Quantity total = 0;
    for (const auto& [price, level] : asks_) {
        for (const Order* order : level) {
            total += order->remaining_qty;
        }
    }
    return total;
}

// ============================================================
// MATCHING SUPPORT — Used by MatchingEngine
// ============================================================

PriceLevel* OrderBook::best_bid_level() {
    if (bids_.empty()) return nullptr;
    return &bids_.begin()->second;
}

PriceLevel* OrderBook::best_ask_level() {
    if (asks_.empty()) return nullptr;
    return &asks_.begin()->second;
}

// ============================================================
// CLEANUP — Remove empty price levels
// ============================================================
// After all orders at a price are filled/cancelled, the price
// level is an empty list. We remove it from the map to keep
// the book clean and prevent stale entries from slowing lookups.
// ============================================================
void OrderBook::cleanup_price_level(Side side, Price price) {
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it != bids_.end() && it->second.empty()) {
            bids_.erase(it);
        }
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end() && it->second.empty()) {
            asks_.erase(it);
        }
    }
}

} // namespace exchange
