#pragma once

#include <cstdint>
#include <chrono>
#include <string>

namespace exchange {

// ============================================================
// CORE TYPE ALIASES
// ============================================================
// Price is a double here. Production engines use scaled integers
// (price * 10^4) because binary floating point cannot represent
// decimal tick sizes exactly, so repeated arithmetic drifts. The
// alias confines that change to one line if it is ever made.
// ============================================================

using OrderId   = uint64_t;   // Unique ID for each order (0 to 18 quintillion)
using Price     = double;      // Price in dollars (e.g., 150.25)
using Quantity  = uint32_t;    // Number of shares (0 to ~4 billion)
using Timestamp = uint64_t;    // Nanoseconds since epoch — for latency measurement

// ============================================================
// ENUMS — the "vocabulary" of our exchange
// ============================================================

enum class Side : uint8_t {
    Buy,   // "Bid" in market terminology — wants to purchase shares
    Sell   // "Ask" / "Offer" — wants to sell shares
};

enum class OrderType : uint8_t {
    Limit,   // Rests on the book until filled or cancelled.
    Market,  // Takes the best available price; never rests.
    IOC      // Immediate or Cancel: fill what is available now,
             // cancel the remainder rather than resting it.
};

// Terminal or resting state reported for every handled order.
enum class OrderStatus : uint8_t {
    Accepted,        // Order received and placed on the book (no immediate match)
    Filled,          // Order fully matched — you got all the shares you wanted
    PartiallyFilled, // Some shares matched, rest still on the book (Limit)
                     // or rest cancelled (IOC)
    Cancelled,       // Order was cancelled (either by user or IOC remainder)
    Rejected         // Invalid order — bad price, zero quantity, etc.
};

// Monotonic nanosecond timestamp. steady_clock rather than
// system_clock: the wall clock can step backwards under NTP
// correction, which would produce negative latency samples.
inline Timestamp now_ns() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()
    ).count();
}

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
