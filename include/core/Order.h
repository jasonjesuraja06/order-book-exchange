#pragma once

#include "core/Types.h"

namespace exchange {

// ============================================================
// ORDER — A request to buy or sell shares
// ============================================================
//
// This is the fundamental "message" in any exchange. Every single
// thing that happens starts with an Order.
//
// MEMORY LAYOUT MATTERS:
// At quant firms, they obsess over struct size because:
// 1. Smaller structs = more fit in CPU cache = faster
// 2. Cache line = 64 bytes on modern CPUs
// 3. If your Order is 65 bytes, it spans 2 cache lines = 2x slower to read
//
// Our Order is ~42 bytes. Two orders fit in one cache line.
// We achieve this by:
// - Using uint8_t enums (1 byte each instead of 4)
// - Ordering fields to minimize padding (alignment gaps)
//
// STRUCT vs CLASS:
// In C++, struct and class are identical except struct defaults to public.
// In finance/systems code, convention is:
//   struct = plain data (like this)
//   class  = has behavior/invariants (like OrderBook)
// ============================================================

struct Order {
    OrderId   id;              // 8 bytes — unique identifier
    Price     price;           // 8 bytes — limit price (0 for market orders)
    Quantity  quantity;         // 4 bytes — how many shares
    Quantity  remaining_qty;   // 4 bytes — how many shares still unfilled
    Timestamp timestamp;       // 8 bytes — when the order arrived (nanoseconds)
    Side      side;            // 1 byte  — buy or sell
    OrderType type;            // 1 byte  — limit, market, or IOC
    OrderStatus status;        // 1 byte  — current state of the order

    // Default constructor — creates an empty/invalid order
    Order() = default;

    // The constructor you'll actually use.
    // We don't take status because new orders are always Accepted.
    // We don't take remaining_qty because it starts equal to quantity.
    // We don't take timestamp because it's set to NOW automatically.
    Order(OrderId id, Side side, OrderType type, Price price, Quantity quantity)
        : id(id)
        , price(price)
        , quantity(quantity)
        , remaining_qty(quantity)  // starts fully unfilled
        , timestamp(now_ns())     // stamped on creation
        , side(side)
        , type(type)
        , status(OrderStatus::Accepted)
    {}

    // How many shares have been filled so far?
    Quantity filled_qty() const { return quantity - remaining_qty; }

    // Is this order completely done?
    bool is_filled() const { return remaining_qty == 0; }
};

// ============================================================
// TRADE — A record of a match between two orders
// ============================================================
// When a buy order's price >= a sell order's price, a Trade happens.
// This is the "execution report" that both parties receive.
//
// Example: Alice has a sell limit at $100 for 50 shares.
//          Bob submits a buy limit at $101 for 30 shares.
//          Trade: price=$100 (seller's price), qty=30 (smaller of the two)
//          Alice still has 20 shares on the book.
//          Bob is fully filled.

struct Trade {
    OrderId   buy_order_id;    // The aggressive or resting buy order
    OrderId   sell_order_id;   // The aggressive or resting sell order
    Price     price;           // Execution price (always the resting order's price)
    Quantity  quantity;         // How many shares changed hands
    Timestamp timestamp;       // When the trade happened
};

} // namespace exchange
