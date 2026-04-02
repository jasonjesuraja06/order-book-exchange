#pragma once

#include "core/Types.h"

#include <cstring>
#include <string>

namespace exchange {

// ============================================================
// BINARY PROTOCOL — How clients talk to the exchange
// ============================================================
//
// WHY BINARY INSTEAD OF JSON/TEXT?
// JSON: {"type":"limit","side":"buy","price":150.00,"qty":100}
//       = ~55 bytes, requires parsing (slow)
//
// Binary: [1][0][0x4062C00000000000][0x00000064]
//         = 22 bytes, no parsing needed (fast)
//
// At quant firms, the wire protocol is ALWAYS binary. FIX protocol
// (used by most exchanges) is tag-value text, but even FIX is being
// replaced by binary protocols like SBE (Simple Binary Encoding)
// and ITCH (NASDAQ's native format).
//
// Our protocol is simple:
// - Fixed-size messages (no variable-length parsing needed)
// - Each message type has a 1-byte header identifying its type
// - Fields are in network byte order (big-endian) for portability
//   (though we skip endian conversion for simplicity in this project)
//
// MESSAGE TYPES:
// 1. OrderMessage      — Client -> Exchange: "submit this order"
// 2. CancelMessage     — Client -> Exchange: "cancel order #X"
// 3. ExecutionReport   — Exchange -> Client: "here's what happened"
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
// PACKED STRUCT:
// #pragma pack(push, 1) tells the compiler: "don't add any padding
// between fields." Normally the compiler adds padding bytes to align
// fields to their natural boundaries (e.g., doubles to 8-byte boundaries).
// Padding is good for CPU performance but bad for network protocols
// because the receiver doesn't know where your padding is.
//
// With packing, sizeof(OrderMessage) = exactly the sum of all field sizes.
// We can read/write it directly to/from a socket with one call.
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
// "Your order #X was accepted/filled/rejected. Here are the details."
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
