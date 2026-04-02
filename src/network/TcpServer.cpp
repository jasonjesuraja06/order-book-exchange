#include "network/TcpServer.h"
#include "network/Protocol.h"

#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>

namespace exchange {

TcpServer::TcpServer(MatchingEngine& engine, uint16_t port)
    : engine_(engine)
    , port_(port)
{}

TcpServer::~TcpServer() {
    stop();
}

// ============================================================
// START — Set up the listening socket and accept connections
// ============================================================
//
// SOCKET PROGRAMMING 101 (ELI5):
//
// A socket is like a phone line for programs. Here's the process:
//
// 1. socket()   — Buy a phone (create a socket file descriptor)
// 2. bind()     — Assign it a phone number (IP address + port)
// 3. listen()   — Turn it on and wait for calls
// 4. accept()   — Pick up when someone calls (blocks until a client connects)
//
// After accept(), you get a NEW socket for that specific conversation.
// The original socket keeps listening for more calls.
//
// File descriptors (the int values) are how Unix represents open
// connections. Everything in Unix is a file — sockets, pipes, files
// on disk — all identified by an integer "file descriptor."
// ============================================================
void TcpServer::start() {
    // Step 1: Create the socket
    // AF_INET = IPv4, SOCK_STREAM = TCP (reliable, ordered delivery)
    // SOCK_DGRAM would be UDP (unreliable, unordered, but faster)
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "Failed to create socket\n";
        return;
    }

    // SO_REUSEADDR: Allow reuse of the port immediately after the
    // server stops. Without this, you'd get "Address already in use"
    // errors for ~60 seconds after stopping the server (TIME_WAIT state).
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Step 2: Bind to address + port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;          // IPv4
    addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all network interfaces
    addr.sin_port = htons(port_);       // htons = "host to network short"
    // (converts port number to big-endian, which is the standard for networks)

    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port " << port_ << "\n";
        close(server_fd_);
        return;
    }

    // Step 3: Start listening (backlog = 10 pending connections max)
    if (listen(server_fd_, 10) < 0) {
        std::cerr << "Failed to listen\n";
        close(server_fd_);
        return;
    }

    running_ = true;
    std::cout << "Exchange server listening on port " << port_ << "\n";

    // Step 4: Accept loop — blocks waiting for clients
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (running_) std::cerr << "Accept failed\n";
            continue;
        }

        std::cout << "Client connected (fd=" << client_fd << ")\n";

        // Spawn a thread to handle this client.
        // Each client gets its own thread that reads messages in a loop.
        // std::thread takes a member function + object + arguments.
        client_threads_.emplace_back(&TcpServer::handle_client, this, client_fd);
    }
}

void TcpServer::stop() {
    running_ = false;
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }
    client_threads_.clear();
}

// ============================================================
// HANDLE CLIENT — Process messages from one connection
// ============================================================
// This runs in its own thread, reading messages in a loop.
//
// Protocol flow:
// 1. Read 1 byte (message type)
// 2. Based on type, read the rest of the message (fixed size)
// 3. Process the message (submit to matching engine)
// 4. Send an execution report back
// 5. Repeat until client disconnects
//
// recv() and send() are the socket equivalents of read() and write().
// recv() blocks until data arrives (or client disconnects).
// ============================================================
void TcpServer::handle_client(int client_fd) {
    // Buffer large enough for the biggest message type
    char buffer[64];

    while (running_) {
        // Read the message type (first byte)
        MessageType msg_type;
        ssize_t n = recv(client_fd, &msg_type, sizeof(msg_type), MSG_WAITALL);
        // MSG_WAITALL = "don't return until you've read ALL the bytes I asked for"
        // Without it, recv might return partial reads that we'd have to reassemble.

        if (n <= 0) break;  // Client disconnected or error

        if (msg_type == MessageType::NewOrder) {
            // Read the rest of the OrderMessage (we already read the first byte)
            OrderMessage msg;
            msg.msg_type = msg_type;
            n = recv(client_fd, reinterpret_cast<char*>(&msg) + 1,
                     sizeof(msg) - 1, MSG_WAITALL);
            if (n <= 0) break;

            // Submit to matching engine
            OrderId id = engine_.submit_order(
                msg.get_symbol(), msg.side, msg.order_type,
                msg.price, msg.quantity
            );

            // Send execution report back
            ExecutionReport report;
            report.order_id = id;
            report.status = (id > 0) ? OrderStatus::Accepted : OrderStatus::Rejected;
            report.price = msg.price;
            report.quantity = msg.quantity;
            report.remaining_qty = msg.quantity;

            send(client_fd, &report, sizeof(report), 0);

        } else if (msg_type == MessageType::Cancel) {
            CancelMessage msg;
            msg.msg_type = msg_type;
            n = recv(client_fd, reinterpret_cast<char*>(&msg) + 1,
                     sizeof(msg) - 1, MSG_WAITALL);
            if (n <= 0) break;

            bool cancelled = engine_.cancel_order(msg.get_symbol(), msg.order_id);

            ExecutionReport report;
            report.order_id = msg.order_id;
            report.status = cancelled ? OrderStatus::Cancelled : OrderStatus::Rejected;
            report.price = 0;
            report.quantity = 0;
            report.remaining_qty = 0;

            send(client_fd, &report, sizeof(report), 0);
        }
    }

    close(client_fd);
    std::cout << "Client disconnected (fd=" << client_fd << ")\n";
}

} // namespace exchange
