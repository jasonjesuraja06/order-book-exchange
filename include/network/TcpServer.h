#pragma once

#include "core/MatchingEngine.h"

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <netinet/in.h>

namespace exchange {
namespace risk { class RiskChecker; }

// ============================================================
// TCP SERVER: network front end for the matching engine
// ============================================================
//
// The main thread runs the accept loop; each accepted connection gets
// its own thread that reads fixed-size binary messages, applies the
// pre-trade risk check, forwards surviving orders to the matching
// engine, and writes an execution report back on the same socket.
//
// Thread-per-connection is chosen for clarity, not scale. An event
// loop over epoll/kqueue is what this would need past a few thousand
// concurrent clients.
//
// If a RiskChecker is supplied, every NewOrder message is checked
// before the engine sees it, and a failing check is answered with a
// Rejected execution report instead of reaching the book. Passing
// nullptr disables the pre-trade layer.
// ============================================================

class TcpServer {
public:
    TcpServer(MatchingEngine& engine, uint16_t port = 9876,
              risk::RiskChecker* risk = nullptr);
    ~TcpServer();

    // Start listening for connections (blocking, runs forever)
    void start();

    // Signal the server to stop
    void stop();

    // Is the server running?
    bool is_running() const { return running_.load(); }

    uint16_t port() const { return port_; }

private:
    MatchingEngine& engine_;
    uint16_t port_;
    risk::RiskChecker* risk_ = nullptr;  // optional pre-trade gate

    int server_fd_ = -1;              // The listening socket file descriptor
    std::atomic<bool> running_{false}; // Thread-safe flag for shutdown
    std::vector<std::thread> client_threads_;

    // Read messages from one connection until it closes.
    void handle_client(int client_fd);
};

} // namespace exchange
