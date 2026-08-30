#pragma once

#include "core/Types.h"

#include <cstring>
#include <string>

namespace exchange {

// ============================================================
// BINARY PROTOCOL: How clients talk to the exchange
// ============================================================
//
// Fixed-size packed messages, each led by a 1-byte type tag, so the
// socket layer reads a known number of bytes and casts rather than
// parsing. The equivalent text encoding of a new order is roughly
// 55 bytes of JSON plus a parse step; this is 23 bytes and none.
//
// PORTABILITY LIMIT: fields are written in the host's native byte
// order and native double representation. That is fine for a client
// and server on the same machine, which is how this is exercised, and
// would need explicit endian conversion to run across architectures.
//
// MESSAGE TYPES:
// 1. OrderMessage     , Client -> Exchange: "submit this order"
// 2. CancelMessage    , Client -> Exchange: "cancel order #X"
// 3. ExecutionReport  , Exchange -> Client: "here's what happened"
// ============================================================

// Every message starts with this byte so the receiver knows
// what type of message follows and how many bytes to read.
enum class MessageType : uint8_t {
    NewOrder   = 1,  // Submit a new order
    Cancel     = 2,  // Cancel an existing order
    ExecReport = 3   // Execution report (trade/ack/reject)
};

// ============================================================
// ORDER MESSAGE (Client -> Exchange)
// ============================================================
// "I want to buy/sell X shares of SYMBOL at PRICE"
//
// #pragma pack(push, 1) suppresses alignment padding, so the struct
// size is exactly the sum of its fields and the layout is the same on
// both ends of the socket. Sizes are verified by tools/print_sizes.cpp.
// ============================================================
#pragma pack(push, 1)

struct OrderMessage {
    MessageType msg_type = MessageType::NewOrder;  // 1 byte
    char        symbol[8];                         // 8 bytes (null-padded)
    Side        side;                              // 1 byte
    OrderType   order_type;                        // 1 byte
    Price       price;                             // 8 bytes
    Quantity    quantity;                           // 4 bytes
    // Total: 23 bytes (compact!)

    void set_symbol(const std::string& sym) {
        std::memset(symbol, 0, sizeof(symbol));
        std::memcpy(symbol, sym.c_str(),
                    std::min(sym.size(), sizeof(symbol) - 1));
    }

    std::string get_symbol() const {
        return std::string(symbol, strnlen(symbol, sizeof(symbol)));
    }
};

// ============================================================
// CANCEL MESSAGE (Client -> Exchange)
// ============================================================
struct CancelMessage {
    MessageType msg_type = MessageType::Cancel;  // 1 byte
    char        symbol[8];                       // 8 bytes
    OrderId     order_id;                        // 8 bytes
    // Total: 17 bytes

    void set_symbol(const std::string& sym) {
        std::memset(symbol, 0, sizeof(symbol));
        std::memcpy(symbol, sym.c_str(),
                    std::min(sym.size(), sizeof(symbol) - 1));
    }

    std::string get_symbol() const {
        return std::string(symbol, strnlen(symbol, sizeof(symbol)));
    }
};

// ============================================================
// EXECUTION REPORT (Exchange -> Client)
// ============================================================
// Reports acceptance, fill, or rejection of one submitted order.
//
// This is sent back to the client for every order and every trade.
// If an order generates 3 trades, the client receives 3 exec reports.
struct ExecutionReport {
    MessageType msg_type = MessageType::ExecReport;  // 1 byte
    OrderId     order_id;                            // 8 bytes
    OrderStatus status;                              // 1 byte
    Price       price;                               // 8 bytes (fill price, or 0)
    Quantity    quantity;                             // 4 bytes (fill qty, or 0)
    Quantity    remaining_qty;                        // 4 bytes (what's left)
    // Total: 26 bytes
};

#pragma pack(pop)  // Restore default alignment

} // namespace exchange
