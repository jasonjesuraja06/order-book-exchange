#pragma once

#include "core/MatchingEngine.h"

#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <functional>
#include <netinet/in.h>

namespace exchange {

// ============================================================
// TCP SERVER — Network layer for the exchange
// ============================================================
//
// This is what turns our matching engine from a library into a
// real exchange. Clients (trading bots, humans) connect over TCP,
// send order messages, and receive execution reports back.
//
// TCP vs UDP:
// Real exchanges use BOTH. TCP for order submission (guaranteed
// delivery — you can't lose an order). UDP multicast for market
// data broadcast (faster, one-to-many, but packets can be lost).
// We use TCP for everything since this is a simulation.
//
// ARCHITECTURE:
// 1. Main thread listens for new connections (accept loop)
// 2. Each client gets its own thread for reading messages
// 3. Messages are forwarded to the matching engine
// 4. Responses are sent back to the client on the same connection
//
// In a real exchange, you'd use epoll/kqueue (event-driven I/O)
// instead of thread-per-client. Thread-per-client is simpler to
// understand but doesn't scale past ~10K connections. For our
// simulation with 3-5 bots, it's perfectly fine.
// ============================================================

class TcpServer {
public:
    TcpServer(MatchingEngine& engine, uint16_t port = 9876);
    ~TcpServer();

    // Start listening for connections (blocking — runs forever)
    void start();

    // Signal the server to stop
    void stop();

    // Is the server running?
    bool is_running() const { return running_.load(); }

    uint16_t port() const { return port_; }

private:
    MatchingEngine& engine_;
    uint16_t port_;

    int server_fd_ = -1;              // The listening socket file descriptor
    std::atomic<bool> running_{false}; // Thread-safe flag for shutdown
    std::vector<std::thread> client_threads_;

    // Handle a single client connection.
    // Reads messages in a loop, forwards to engine, sends responses.
    void handle_client(int client_fd);
};

} // namespace exchange
