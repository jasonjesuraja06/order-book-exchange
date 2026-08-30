// ============================================================
// SIMULATION RUNNER: Run a full simulated trading day
// ============================================================
// This is the main entry point for running the simulation
// without the TCP server. It creates the matching engine,
// spawns trading bots, runs 10,000 ticks, and prints results.
//
// Usage: ./run_simulation
// ============================================================

#include "simulation/Simulation.h"

#include <iostream>

int main() {
    using namespace exchange;

    SimulationConfig config;
    config.symbol = "AAPL";
    config.initial_price = 150.00;
    config.num_ticks = 10000;
    config.price_volatility = 0.001;  // ~0.1% per tick

    Simulation sim(config);
    SimulationResults results = sim.run();
    Simulation::print_results(results);

    return 0;
}
