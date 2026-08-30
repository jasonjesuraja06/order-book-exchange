// Exchange server entry point.
//
// Brings up a matching engine behind a pre-trade RiskChecker and a TCP
// front end, and runs the agent simulation on a background thread so a
// connecting client finds a live book rather than an empty one.
//
// Usage: ./exchange [port]     (default 9876)

#include "core/MatchingEngine.h"
#include "network/TcpServer.h"
#include "risk/RiskChecker.h"
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

    // Pre-trade limits applied to every order arriving over the socket.
    risk::RiskLimits limits;
    risk::RiskChecker risk(limits);

    // Keep the risk layer's positions and notional exposure current by
    // feeding it both legs of every execution.
    engine.set_trade_callback([&risk](const Trade& trade) {
        risk.on_fill("AAPL", Side::Buy,  trade.price, trade.quantity);
        risk.on_fill("AAPL", Side::Sell, trade.price, trade.quantity);
    });

    // Background activity so the book is not empty for connecting clients.
    // The simulation drives its own engine instance.
    std::thread sim_thread([]() {
        SimulationConfig config;
        config.symbol = "AAPL";
        config.initial_price = 150.00;
        config.num_ticks = 50000;
        config.price_volatility = 0.001;

        Simulation sim(config);
        auto results = sim.run();
        Simulation::print_results(results);
    });
    sim_thread.detach();

    // Blocks until the process is signalled.
    TcpServer server(engine, port, &risk);
    server.start();

    return 0;
}
