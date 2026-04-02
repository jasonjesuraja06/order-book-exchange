// ============================================================
// EXCHANGE SERVER — Full TCP exchange with live client support
// ============================================================
// Starts the matching engine + TCP server, then also runs the
// simulation bots in a separate thread so there's activity
// on the book for any connecting clients to see.
//
// Usage: ./exchange [port]
// Default port: 9876
// ============================================================

#include "core/MatchingEngine.h"
#include "network/TcpServer.h"
#include "simulation/Simulation.h"

#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
    using namespace exchange;

    uint16_t port = 9876;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    MatchingEngine engine;

    // Run simulation in background thread so the book has activity
    std::thread sim_thread([&engine]() {
        SimulationConfig config;
        config.symbol = "AAPL";
        config.initial_price = 150.00;
        config.num_ticks = 50000;  // Longer run for the server
        config.price_volatility = 0.001;

        Simulation sim(config);
        auto results = sim.run();
        Simulation::print_results(results);
    });

    // Start TCP server (blocking — runs until killed)
    TcpServer server(engine, port);
    server.start();

    sim_thread.join();
    return 0;
}
