#include "simulation/Simulation.h"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <random>

namespace exchange {

Simulation::Simulation(const SimulationConfig& config)
    : config_(config)
    , true_price_(config.initial_price)
{
    // Create the trading bots.
    // Each bot gets a reference to the same matching engine.
    // This is how they interact — all orders go through one engine.
    market_maker_ = std::make_unique<MarketMaker>(
        engine_, config_.symbol, config_.initial_price);

    momentum_trader_ = std::make_unique<MomentumTrader>(
        engine_, config_.symbol);

    // Create 3 noise traders for diverse order flow
    for (int i = 0; i < 3; i++) {
        noise_traders_.push_back(
            std::make_unique<NoiseTrader>(engine_, config_.symbol));
    }

    // Wire up order ownership tracking.
    // Every time a bot submits an order, we record which bot owns it.
    // When a trade fires, we look up both sides and call on_fill().
    auto register_order = [this](OrderId id, Trader* trader) {
        order_owner_[id] = trader;
    };

    market_maker_->set_order_callback(register_order);
    momentum_trader_->set_order_callback(register_order);
    for (auto& noise : noise_traders_) {
        noise->set_order_callback(register_order);
    }
}

// ============================================================
// RUN — The main simulation loop
// ============================================================
// Here's what happens every tick:
//
// 1. RANDOM WALK the true price (simulates real market movement)
//    Each tick, the price changes by a small random amount.
//    This is the "Geometric Brownian Motion" model — the same
//    math behind the Black-Scholes options pricing formula.
//    Simple version: new_price = old_price * (1 + random_normal)
//
// 2. UPDATE each bot's view of the market
//    - Market maker gets the new "fair value"
//    - Momentum trader gets the latest trade price
//    - Noise traders get an approximate reference price
//
// 3. LET each bot act (call on_tick())
//    Each bot looks at the market and submits/cancels orders.
//    The matching engine processes them immediately.
//
// 4. RECORD any trades that happened this tick
// ============================================================
SimulationResults Simulation::run() {
    // Random number generator for the price random walk
    std::mt19937 rng(42);  // Fixed seed = reproducible results
    // Normal distribution with mean=0, stddev=volatility
    // This means each tick, the price moves ~0.1% on average
    std::normal_distribution<double> price_dist(0.0, config_.price_volatility);

    // Register a trade callback so we can log every trade
    // and notify the bots about their fills.
    //
    // Lambda capture explained:
    // [this] means "this lambda can access the Simulation's member variables"
    // (const Trade& trade) is the parameter — the trade that just happened
    engine_.set_trade_callback([this](const Trade& trade) {
        trade_log_.push_back(trade);

        // Route fills to the correct traders using the ownership map.
        // Each trade has a buyer and seller — notify both.
        auto buy_it = order_owner_.find(trade.buy_order_id);
        if (buy_it != order_owner_.end()) {
            buy_it->second->on_fill(Side::Buy, trade.price, trade.quantity);
        }

        auto sell_it = order_owner_.find(trade.sell_order_id);
        if (sell_it != order_owner_.end()) {
            sell_it->second->on_fill(Side::Sell, trade.price, trade.quantity);
        }

        // Momentum trader also tracks the last trade price for SMA
        momentum_trader_->update_price(trade.price);
    });

    std::cout << "Starting simulation: " << config_.symbol
              << " @ $" << std::fixed << std::setprecision(2)
              << config_.initial_price
              << " for " << config_.num_ticks << " ticks\n";
    std::cout << std::string(60, '-') << "\n";

    // ---- MAIN LOOP ----
    for (uint64_t tick = 0; tick < config_.num_ticks; tick++) {
        // Step 1: Random walk the true price
        // Geometric Brownian Motion: dS/S = μdt + σdW
        // We set μ=0 (no drift) so the price is a pure random walk.
        // The price_dist gives us σdW (random shock).
        double return_pct = price_dist(rng);
        true_price_ *= (1.0 + return_pct);

        // Ensure price stays positive (can't have negative stock prices)
        true_price_ = std::max(true_price_, 0.01);

        // Step 2: Update bot price views
        market_maker_->set_fair_value(true_price_);
        for (auto& noise : noise_traders_) {
            noise->set_reference_price(true_price_);
        }

        // Initialize momentum trader's price history
        if (tick == 0) {
            momentum_trader_->update_price(true_price_);
        }

        // Step 3: Let each bot act
        market_maker_->on_tick(tick);
        momentum_trader_->on_tick(tick);
        for (auto& noise : noise_traders_) {
            noise->on_tick(tick);
        }

        // Progress update every 10% of simulation
        if (tick > 0 && tick % (config_.num_ticks / 10) == 0) {
            double pct = 100.0 * tick / config_.num_ticks;
            std::cout << "  " << std::setw(3) << static_cast<int>(pct) << "% complete | "
                      << "Price: $" << std::fixed << std::setprecision(2) << true_price_
                      << " | Trades: " << engine_.stats().total_trades
                      << " | Orders: " << engine_.stats().total_orders << "\n";
        }
    }

    std::cout << "  100% complete\n";
    std::cout << std::string(60, '-') << "\n";

    // ---- BUILD RESULTS ----
    const auto& stats = engine_.stats();

    SimulationResults results;
    results.total_ticks = config_.num_ticks;
    results.total_trades = stats.total_trades;
    results.total_volume = stats.total_volume;
    results.total_notional = stats.total_notional;
    results.avg_latency_ns = stats.avg_latency_ns();
    results.min_latency_ns = static_cast<double>(stats.min_latency_ns);
    results.max_latency_ns = static_cast<double>(stats.max_latency_ns);
    results.final_price = true_price_;
    results.price_return_pct = ((true_price_ - config_.initial_price)
                                 / config_.initial_price) * 100.0;

    // Collect per-trader stats
    auto add_trader_result = [&](const Trader& trader) {
        SimulationResults::TraderResult tr;
        tr.name = trader.name();
        tr.final_position = trader.stats().position;
        tr.pnl = trader.stats().calculate_pnl(true_price_);
        tr.orders_sent = trader.stats().orders_sent;
        tr.fills_received = trader.stats().fills_received;
        results.trader_results.push_back(tr);
    };

    add_trader_result(*market_maker_);
    add_trader_result(*momentum_trader_);
    for (const auto& noise : noise_traders_) {
        add_trader_result(*noise);
    }

    return results;
}

// ============================================================
// PRINT RESULTS — Formatted simulation summary
// ============================================================
void Simulation::print_results(const SimulationResults& results) {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║          SIMULATION RESULTS                 ║\n";
    std::cout << "╠══════════════════════════════════════════════╣\n";

    std::cout << std::fixed << std::setprecision(2);

    std::cout << "║ Total Ticks:     " << std::setw(10) << results.total_ticks
              << "                 ║\n";
    std::cout << "║ Total Trades:    " << std::setw(10) << results.total_trades
              << "                 ║\n";
    std::cout << "║ Total Volume:    " << std::setw(10) << results.total_volume
              << " shares          ║\n";
    std::cout << "║ Total Notional:  $" << std::setw(12) << results.total_notional
              << "               ║\n";
    std::cout << "║ Final Price:     $" << std::setw(9) << results.final_price
              << "                ║\n";
    std::cout << "║ Price Return:    " << std::setw(9) << results.price_return_pct
              << "%               ║\n";

    std::cout << "╠══════════════════════════════════════════════╣\n";
    std::cout << "║          LATENCY (nanoseconds)              ║\n";
    std::cout << "╠══════════════════════════════════════════════╣\n";

    std::cout << std::fixed << std::setprecision(0);
    std::cout << "║ Average:         " << std::setw(10) << results.avg_latency_ns
              << " ns              ║\n";
    std::cout << "║ Minimum:         " << std::setw(10) << results.min_latency_ns
              << " ns              ║\n";
    std::cout << "║ Maximum:         " << std::setw(10) << results.max_latency_ns
              << " ns              ║\n";

    std::cout << "╠══════════════════════════════════════════════╣\n";
    std::cout << "║          TRADER P&L                         ║\n";
    std::cout << "╠══════════════════════════════════════════════╣\n";

    std::cout << std::fixed << std::setprecision(2);
    for (const auto& tr : results.trader_results) {
        std::cout << "║ " << std::setw(16) << std::left << tr.name
                  << " Pos:" << std::setw(6) << std::right << tr.final_position
                  << " P&L: $" << std::setw(10) << tr.pnl << " ║\n";
    }

    std::cout << "╚══════════════════════════════════════════════╝\n";

    // Throughput calculation
    if (results.avg_latency_ns > 0) {
        double orders_per_sec = 1e9 / results.avg_latency_ns;
        std::cout << "\nThroughput: ~" << std::fixed << std::setprecision(0)
                  << orders_per_sec << " orders/sec ("
                  << std::setprecision(2) << orders_per_sec / 1e6
                  << "M orders/sec)\n";
    }
}

} // namespace exchange
