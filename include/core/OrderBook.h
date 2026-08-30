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
// ORDER BOOK: The central data structure of any exchange
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

// A single price level: all orders at one price, in arrival order.
// The earlier order at a given price is matched first, which is what
// time priority means.
using PriceLevel = std::list<Order*>;

// Iterator pointing to a specific order within a price level.
// The lookup map stores one per order so cancel is O(1).
using OrderIterator = PriceLevel::iterator;

class OrderBook {
public:
    // symbol = which stock this order book is for (e.g., "AAPL")
    explicit OrderBook(const std::string& symbol, size_t pool_size = 100000);

    // ---- Core Operations ----

    // Add a new order to the book. Returns a pointer to the order
    // stored in the object pool, not to the caller's copy.
    // This is O(log N) where N = number of distinct price levels.
    Order* add_order(Side side, OrderType type, Price price, Quantity quantity);

    // Cancel an order by ID. Returns true if found and cancelled.
    // O(1): the lookup map jumps straight to the order.
    bool cancel_order(OrderId order_id);

    // Modify an existing order's quantity. On real exchanges a price
    // change is a cancel plus a new order and forfeits time priority,
    // a quantity decrease keeps it, and a quantity increase does not.
    // Only the decrease case is implemented here.
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
    //   The std::greater<Price> comparator gives that ordering.
    //   bids_.begin() = highest bid = best buyer.
    //
    // asks_ is sorted by price ASCENDING (lowest first).
    //   Default std::map behavior (std::less<Price>).
    //   asks_.begin() = lowest ask = best seller.
    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel>                      asks_;

    // O(1) ORDER LOOKUP:
    // Maps OrderId -> {side, price, iterator_into_price_level}
    // Cancel and modify therefore do not search the book.
    //
    // Without it, cancelling one order would scan every price level
    // and every order within a level, O(N*M) for N levels of M orders.
    // With it: O(1) lookup and O(1) erase from the linked list.
    struct OrderLocation {
        Side side;
        Price price;
        OrderIterator iterator;
    };
    std::unordered_map<OrderId, OrderLocation> order_lookup_;

    // Object pool for orders, which avoids a heap allocation per order.
    // ObjectPool.h documents the pointer-stability requirement.
    ObjectPool<Order> order_pool_;

    // Monotonically increasing order IDs.
    // Not atomic: the MatchingEngine mutex already serialises access.
    OrderId next_order_id_ = 1;

    // Helper: clean up empty price levels after orders are removed.
    // If all orders at $150.00 are filled/cancelled, remove that
    // price level from the map entirely. Keeps the book clean.
    void cleanup_price_level(Side side, Price price);
};

} // namespace exchange
