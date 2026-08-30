#pragma once

#include "core/OrderBook.h"

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace exchange {

// ============================================================
// MATCHING ENGINE: order intake, matching, and resting
// ============================================================
//
// The matching engine is the central component that:
// 1. Receives incoming orders
// 2. Tries to match them against resting orders in the book
// 3. Generates trades when matches occur
// 4. Places unmatched (or partially matched) limit orders on the book
//
// MATCHING ALGORITHM: Price-Time Priority (FIFO)
// -------------------------------------------------
// This is the algorithm used by NYSE, NASDAQ, CME, and most
// major exchanges worldwide. It works like this:
//
// 1. PRICE PRIORITY: Better prices match first.
//    - For a new buy order: match against the LOWEST ask first
//    - For a new sell order: match against the HIGHEST bid first
//
// 2. TIME PRIORITY: At the same price, earlier orders match first.
//    - If two sellers both want $100, the one who submitted first
//      gets filled first (FIFO = First In, First Out)
//
// EXAMPLE:
//   Book state:
//     Ask: $100 x 50 (Alice, arrived 9:30), $100 x 30 (Bob, arrived 9:31)
//     Bid: $99 x 100 (Charlie)
//
//   New order: BUY 70 shares at $100 (LIMIT)
//
//   Best ask is $100 and the buy price is $100, so the order crosses.
//   Alice is first at $100 by time priority, so 50 fill from Alice and
//   she leaves the book. Bob is next at $100, so 20 fill from Bob and
//   his remaining 10 stay resting. The incoming order is fully filled.
//
//   Trades generated:
//     Trade 1: BUY matched ALICE's SELL, 50 shares @ $100
//     Trade 2: BUY matched BOB's SELL, 20 shares @ $100
//
// THREAD SAFETY:
// A single mutex serialises order processing, so the engine is safe
// to call from the server's per-connection threads but does not scale
// with them. The standard alternative is a lock-free queue feeding a
// single-threaded matching loop, where no lock is needed because only
// one thread ever touches the book.
// ============================================================

// Invoked synchronously on the submitting thread for every trade, so
// logging, P&L, and risk position updates all observe it in order.
using TradeCallback = std::function<void(const Trade&)>;

// Stats that the matching engine tracks for benchmarking.
struct EngineStats {
    uint64_t total_orders = 0;      // How many orders have been submitted
    uint64_t total_trades = 0;      // How many trades (matches) occurred
    uint64_t total_cancels = 0;     // How many orders were cancelled
    uint64_t total_rejects = 0;     // How many orders were rejected
    uint64_t total_volume = 0;      // Total shares traded
    double   total_notional = 0.0;  // Total dollar value traded (price * qty)
    uint64_t total_latency_ns = 0;  // Cumulative processing time (nanoseconds)
    uint64_t min_latency_ns = UINT64_MAX;  // Fastest order processed
    uint64_t max_latency_ns = 0;           // Slowest order processed

    // Average latency per order in nanoseconds
    double avg_latency_ns() const {
        return total_orders > 0
            ? static_cast<double>(total_latency_ns) / total_orders
            : 0.0;
    }
};

class MatchingEngine {
public:
    MatchingEngine();

    // Submit an order to the exchange.
    // Entry point for limit, market, and IOC orders.
    // Returns the order ID assigned to this order.
    //
    // The engine will:
    // 1. Validate the order (reject if invalid)
    // 2. Try to match against the opposite side of the book
    // 3. Place any unmatched remainder on the book (limit orders)
    //    or cancel it (market/IOC orders)
    OrderId submit_order(const std::string& symbol, Side side,
                         OrderType type, Price price, Quantity quantity);

    // Cancel an existing order by ID.
    bool cancel_order(const std::string& symbol, OrderId order_id);

    // Register a callback that fires on every trade.
    void set_trade_callback(TradeCallback callback);

    // Get the order book for a specific symbol.
    // Creates one if it doesn't exist (lazy initialization).
    OrderBook& get_order_book(const std::string& symbol);

    // Performance statistics
    const EngineStats& stats() const { return stats_; }
    void reset_stats() { stats_ = EngineStats{}; }

private:
    std::unordered_map<std::string, OrderBook> books_;

    std::mutex mutex_;  // serialises all order processing

    // Called on every trade
    TradeCallback trade_callback_;

    // Running statistics
    EngineStats stats_;

    // ---- Internal matching logic ----

    // Try to match an incoming order against the opposite side.
    // The core matching loop, and the hottest code path.
    // Returns a vector of trades generated.
    std::vector<Trade> match_order(OrderBook& book, Order* order);

    // Execute a single match between an incoming order and a resting order.
    // Adjusts quantities on both orders and generates a Trade.
    Trade execute_match(Order* incoming, Order* resting);
};

} // namespace exchange
