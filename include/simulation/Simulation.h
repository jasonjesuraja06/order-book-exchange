#pragma once

#include "core/MatchingEngine.h"
#include "simulation/MarketMaker.h"
#include "simulation/MomentumTrader.h"
#include "simulation/NoiseTrader.h"

#include <vector>
#include <memory>
#include <unordered_map>

namespace exchange {

// ============================================================
// SIMULATION — Drives the agents against one matching engine
// ============================================================
//
// Tick-based rather than event-based: fixed time steps, every agent
// acts on every tick. A tick is a simulation time step, not a price
// tick. The loop creates the engine and agents, random-walks a
// reference price, lets each agent act, and accumulates trade and
// latency statistics.
// ============================================================

struct SimulationConfig {
    std::string symbol = "AAPL";        // Which stock to simulate
    double initial_price = 150.00;       // Starting price
    uint64_t num_ticks = 10000;          // How many simulation steps
    double price_volatility = 0.001;     // Per-tick price change stddev
    // (0.001 = 0.1% per tick, annualizes to ~25% vol for daily trading)
};

struct SimulationResults {
    uint64_t total_ticks = 0;
    uint64_t total_orders = 0;
    uint64_t total_cancels = 0;
    uint64_t total_trades = 0;
    uint64_t total_volume = 0;
    double   total_notional = 0.0;

    // Wall-clock seconds spent inside the tick loop. Throughput is
    // (total_orders + total_cancels) / wall_clock_sec, measured over the
    // whole run; it is not derived from the latency figures below.
    double   wall_clock_sec = 0.0;

    double   avg_latency_ns = 0.0;
    double   min_latency_ns = 0.0;
    double   max_latency_ns = 0.0;
    double   final_price = 0.0;
    double   price_return_pct = 0.0;

    // Per-trader results
    struct TraderResult {
        std::string name;
        int64_t  final_position;
        double   pnl;
        uint64_t orders_sent;
        uint64_t fills_received;
    };
    std::vector<TraderResult> trader_results;
};

class Simulation {
public:
    explicit Simulation(const SimulationConfig& config = SimulationConfig{});

    // Run the full simulation. Returns results when done.
    SimulationResults run();

    // Print a formatted summary to stdout
    static void print_results(const SimulationResults& results);

private:
    SimulationConfig config_;
    MatchingEngine engine_;

    std::unique_ptr<MarketMaker> market_maker_;
    std::unique_ptr<MomentumTrader> momentum_trader_;
    std::vector<std::unique_ptr<NoiseTrader>> noise_traders_;

    // Reference price that random-walks each tick, standing in for
    // external information flow. The market maker quotes around it.
    double true_price_;

    // Trade log for analysis
    std::vector<Trade> trade_log_;

    // Order ID to submitting agent. A trade names two order IDs;
    // both are looked up here so on_fill() reaches the right agents.
    std::unordered_map<OrderId, Trader*> order_owner_;
};

} // namespace exchange
