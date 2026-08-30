#pragma once

#include "core/Order.h"
#include "core/ObjectPool.h"

#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <functional>

namespace exchange {

// ============================================================
// ORDER BOOK — The central data structure of any exchange
// ============================================================
//
// Two sorted sides. Bids descend from the highest price a buyer will
// pay; asks ascend from the lowest price a seller will accept. The gap
// between the two best prices is the spread. An incoming buy at or
// above the best ask crosses and matches; likewise a sell at or below
// the best bid.
//
// DATA STRUCTURE CHOICE
// Three operations have to be cheap: reach the best price level,
// insert or remove a level, and remove one specific order by ID.
//
//   std::map<Price, std::list<Order*>>
//     - price levels kept sorted by the tree
//     - O(log N) in the number of levels to find or insert a level
//     - O(1) to reach the best level via begin()/rbegin()
//     - O(1) append within a level, which is the FIFO time priority
//
//   std::unordered_map<OrderId, OrderLocation>
//     - stores the side, price, and list iterator for each live order,
//       so cancel splices it out in O(1) without scanning the level
//
// A production book indexes an array directly by tick, since prices
// are discrete, and gets O(1) on the level lookup as well. The tree
// is used here for clarity and because the level count stays small.
// ============================================================

// A single price level — all orders at the same price, in FIFO order.
// "FIFO" = First In, First Out = time priority.
// If Alice and Bob both want to buy at $150, Alice gets filled first
// because she submitted her order first.
using PriceLevel = std::list<Order*>;

// Iterator pointing to a specific order within a price level.
// We store these in a lookup map so we can cancel any order in O(1).
using OrderIterator = PriceLevel::iterator;

class OrderBook {
public:
    // symbol = which stock this order book is for (e.g., "AAPL")
    explicit OrderBook(const std::string& symbol, size_t pool_size = 100000);

    // ---- Core Operations ----

    // Add a new order to the book. Returns a pointer to the order
    // stored in our object pool (NOT the caller's copy).
    // This is O(log N) where N = number of distinct price levels.
    Order* add_order(Side side, OrderType type, Price price, Quantity quantity);

    // Cancel an order by ID. Returns true if found and cancelled.
    // This is O(1) — we use the lookup map to jump directly to it.
    bool cancel_order(OrderId order_id);

    // Modify an existing order's quantity. In real exchanges, modifying
    // price = cancel + new order (you lose time priority). Modifying
    // quantity down keeps your priority. Modifying up = cancel + new.
    // We implement the simple case: reduce quantity, keep priority.
    bool reduce_order(OrderId order_id, Quantity new_quantity);

    // ---- Accessors ----

    // Best bid = highest price any buyer is willing to pay
    // Returns 0.0 if no bids exist (empty book)
    Price best_bid() const;

    // Best ask = lowest price any seller is willing to accept
    // Returns 0.0 if no asks exist
    Price best_ask() const;

    // Spread = best_ask - best_bid
    // This is the "cost" of crossing the spread (market orders pay this)
    Price spread() const;

    // Mid price = (best_bid + best_ask) / 2
    // Used as a "fair value" estimate
    Price mid_price() const;

    // Total volume sitting on each side of the book
    Quantity total_bid_quantity() const;
    Quantity total_ask_quantity() const;

    // Number of distinct price levels on each side
    size_t bid_depth() const { return bids_.size(); }
    size_t ask_depth() const { return asks_.size(); }

    // Total number of individual orders on the book
    size_t order_count() const { return order_lookup_.size(); }

    const std::string& symbol() const { return symbol_; }

    // ---- Matching Support ----
    // These are used by the MatchingEngine to walk through price levels
    // and match incoming orders against resting orders.

    // Get the best (top-of-book) price level for matching.
    // For bids: the HIGHEST price (rbegin of the map)
    // For asks: the LOWEST price (begin of the map)
    // Returns nullptr if that side is empty.
    PriceLevel* best_bid_level();
    PriceLevel* best_ask_level();

    // Remove an order from the book (after it's been fully filled).
    // Called by the matching engine, NOT by external users.
    void remove_order(Order* order);

    // Get the next order ID (monotonically increasing)
    OrderId next_order_id() { return next_order_id_++; }

private:
    std::string symbol_;

    // THE BOOK ITSELF:
    // bids_ is sorted by price DESCENDING (highest first).
    //   We achieve this with std::greater<Price> comparator.
    //   bids_.begin() = highest bid = best buyer.
    //
    // asks_ is sorted by price ASCENDING (lowest first).
    //   Default std::map behavior (std::less<Price>).
    //   asks_.begin() = lowest ask = best seller.
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel>                      asks_;

    // O(1) ORDER LOOKUP:
    // Maps OrderId -> {side, price, iterator_into_price_level}
    // This lets us cancel or modify any order without searching.
    //
    // Without this, cancelling order #12345 would require:
    //   1. Search all price levels (O(N price levels))
    //   2. Search within each level (O(M orders per level))
    //   = O(N*M) total — WAY too slow
    //
    // With this: O(1) lookup, O(1) erase from linked list.
    struct OrderLocation {
        Side side;
        Price price;
        OrderIterator iterator;
    };
    std::unordered_map<OrderId, OrderLocation> order_lookup_;

    // Object pool for orders — avoids heap allocation per order.
    // See ObjectPool.h for why this matters.
    ObjectPool<Order> order_pool_;

    // Monotonically increasing order IDs.
    // Using atomic<> would make this thread-safe, but we'll handle
    // thread safety at the MatchingEngine level instead.
    OrderId next_order_id_ = 1;

    // Helper: clean up empty price levels after orders are removed.
    // If all orders at $150.00 are filled/cancelled, remove that
    // price level from the map entirely. Keeps the book clean.
    void cleanup_price_level(Side side, Price price);
};

} // namespace exchange
