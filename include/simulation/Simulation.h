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
// SIMULATION — Orchestrates a full simulated trading day
// ============================================================
//
// This is the "main loop" that ties everything together:
// 1. Creates the matching engine and trading bots
// 2. Runs N ticks (each tick = one time step)
// 3. On each tick, every bot gets a chance to act
// 4. Tracks trades, updates prices, records stats
// 5. At the end, prints a summary (latency, P&L, volume)
//
// TICK-BASED vs EVENT-BASED SIMULATION:
// We use tick-based: fixed time steps, every bot acts each tick.
// Event-based would be: events trigger callbacks (more realistic
// but harder to implement). Tick-based is standard for backtesting
// and academic simulations.
//
// A "tick" here is NOT a price tick — it's a simulation time step.
// With 10,000 ticks representing a 6.5-hour trading day, each tick
// is about 2.3 seconds of market time.
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
    uint64_t total_trades = 0;
    uint64_t total_volume = 0;
    double   total_notional = 0.0;
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

    // Smart pointers manage bot lifetime automatically.
    // unique_ptr = "this object is owned by exactly one pointer"
    // When the Simulation is destroyed, all bots are destroyed too.
    std::unique_ptr<MarketMaker> market_maker_;
    std::unique_ptr<MomentumTrader> momentum_trader_;
    std::vector<std::unique_ptr<NoiseTrader>> noise_traders_;

    // Simulated "true" price that random-walks each tick.
    // This represents external information flow — earnings, news, etc.
    // The market maker tracks this; other bots react to market prices.
    double true_price_;

    // Trade log for analysis
    std::vector<Trade> trade_log_;

    // Maps order IDs to the trader that submitted them.
    // When a trade happens, we look up both order IDs to find
    // the buyer and seller, then call on_fill() on each.
    std::unordered_map<OrderId, Trader*> order_owner_;
};

} // namespace exchange
