#pragma once
// pragma once = "only include this file once per compilation unit"
// Without it, if two files both #include Types.h, you'd get duplicate
// definition errors. Every header file needs this.

#include <cstdint>
#include <chrono>
#include <string>

namespace exchange {

// ============================================================
// CORE TYPE ALIASES
// ============================================================
// We define our own type names instead of using raw int/double.
// Why? Two reasons:
// 1. Readability: "Price price" is clearer than "double price"
// 2. If we ever need to change the underlying type (e.g., Price
//    from double to fixed-point integer for real production use),
//    we change it in ONE place, not hundreds.
//
// Quant firms use integer prices (e.g., price * 10000) to avoid
// floating-point rounding errors. We use double for simplicity
// but the alias lets us swap later.
// ============================================================

using OrderId   = uint64_t;   // Unique ID for each order (0 to 18 quintillion)
using Price     = double;      // Price in dollars (e.g., 150.25)
using Quantity  = uint32_t;    // Number of shares (0 to ~4 billion)
using Timestamp = uint64_t;    // Nanoseconds since epoch — for latency measurement

// ============================================================
// ENUMS — the "vocabulary" of our exchange
// ============================================================

// Which side of the book? You're either buying or selling.
// "enum class" is a C++11 feature — it's a type-safe enum.
// You can't accidentally write Side::Buy == 0 (compile error).
// Regular enums allow that, which causes subtle bugs.
enum class Side : uint8_t {
    Buy,   // "Bid" in market terminology — wants to purchase shares
    Sell   // "Ask" / "Offer" — wants to sell shares
};

// What kind of order is this?
enum class OrderType : uint8_t {
    Limit,   // "I want to buy 100 shares at $150 or better"
             // Sits on the book until filled or cancelled.
             // This is 90%+ of real exchange traffic.

    Market,  // "I want to buy 100 shares RIGHT NOW at whatever price"
             // Matches immediately against the best available price.
             // If the book is empty, this order is rejected (no price).

    IOC      // "Immediate or Cancel" — fill what you can right now,
             // cancel the rest. Used by HFT firms who don't want
             // their unfilled orders sitting on the book (information leakage).
             // Example: IOC buy 100 shares at $150 — if only 60 are
             // available at $150, you get 60 and the remaining 40 are gone.
};

// What happened when we processed your order?
// The matching engine returns one of these for every order it handles.
enum class OrderStatus : uint8_t {
    Accepted,        // Order received and placed on the book (no immediate match)
    Filled,          // Order fully matched — you got all the shares you wanted
    PartiallyFilled, // Some shares matched, rest still on the book (Limit)
                     // or rest cancelled (IOC)
    Cancelled,       // Order was cancelled (either by user or IOC remainder)
    Rejected         // Invalid order — bad price, zero quantity, etc.
};

// ============================================================
// UTILITY: Get current time in nanoseconds
// ============================================================
// We measure everything in nanoseconds because at quant firms,
// the difference between 500ns and 800ns PER ORDER matters.
// std::chrono is C++'s time library — type-safe and precise.
inline Timestamp now_ns() {
    // steady_clock = monotonic clock (never jumps backward)
    // system_clock = wall clock (can jump due to NTP adjustments)
    // For latency measurement, ALWAYS use steady_clock.
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();
}

// Helper to convert enum to string (useful for printing/debugging)
inline const char* to_string(Side side) {
    return side == Side::Buy ? "BUY" : "SELL";
}

inline const char* to_string(OrderType type) {
    switch (type) {
        case OrderType::Limit:  return "LIMIT";
        case OrderType::Market: return "MARKET";
        case OrderType::IOC:    return "IOC";
    }
    return "UNKNOWN";
}

inline const char* to_string(OrderStatus status) {
    switch (status) {
        case OrderStatus::Accepted:        return "ACCEPTED";
        case OrderStatus::Filled:          return "FILLED";
        case OrderStatus::PartiallyFilled: return "PARTIAL";
        case OrderStatus::Cancelled:       return "CANCELLED";
        case OrderStatus::Rejected:        return "REJECTED";
    }
    return "UNKNOWN";
}

} // namespace exchange
