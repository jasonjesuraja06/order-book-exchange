#pragma once

#include "core/Types.h"

namespace exchange {

// ============================================================
// ORDER — a request to buy or sell
// ============================================================
//
// MEMORY LAYOUT
// Fields are ordered widest-first and the enums are uint8_t, which
// packs the 35 bytes of payload into 40 bytes with 8-byte alignment
// and no interior padding. Measured with tools/print_sizes.cpp:
//
//     sizeof(Order) == 40, alignof(Order) == 8
//
// One order therefore fits inside a 64-byte cache line, but two do
// not (80 > 64). Shrinking further would mean narrowing Price from
// double to a fixed-point integer, which is the change a production
// engine would make anyway, for rounding reasons rather than size.
//
// tests/test_object_pool.cpp asserts this size so the figure quoted
// here and in the README cannot drift from the code.
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

    // status, remaining_qty, and timestamp are derived rather than
    // passed: a new order is always Accepted, starts fully unfilled,
    // and is stamped on construction.
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
