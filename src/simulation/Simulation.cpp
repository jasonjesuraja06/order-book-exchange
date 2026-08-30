#include "simulation/Simulation.h"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <random>
#include <chrono>

namespace exchange {

Simulation::Simulation(const SimulationConfig& config)
    : config_(config)
    , true_price_(config.initial_price)
{
    // All agents share one matching engine; that shared book is the
    // only channel through which they interact.
    market_maker_ = std::make_unique<MarketMaker>(
        engine_, config_.symbol, config_.initial_price);

    momentum_trader_ = std::make_unique<MomentumTrader>(
        engine_, config_.symbol);

    // Create 3 noise traders for diverse order flow
    for (int i = 0; i < 3; i++) {
        noise_traders_.push_back(
            std::make_unique<NoiseTrader>(engine_, config_.symbol));
    }

    // Record which agent submitted each order so fills can be routed
    // back to both sides of a trade.
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
// RUN: main simulation loop
// ============================================================
// Per tick: random-walk the reference price, push it to the agents,
// let each agent act (orders are matched synchronously), and record
// the resulting trades.
//
// The loop is bracketed by a steady_clock reading so throughput can be
// reported as operations divided by elapsed wall-clock seconds.
// ============================================================
SimulationResults Simulation::run() {
    // Random number generator for the price random walk
    // Seeds only the price walk. Each agent seeds its own RNG from
    // std::random_device, so a run is not reproducible end to end.
    std::mt19937 rng(42);
    // Normal distribution with mean=0, stddev=volatility
    // This means each tick, the price moves ~0.1% on average
    std::normal_distribution<double> price_dist(0.0, config_.price_volatility);

    // Log every trade and route the fill to both counterparties.
    engine_.set_trade_callback([this](const Trade& trade) {
        trade_log_.push_back(trade);

        auto buy_it = order_owner_.find(trade.buy_order_id);
        if (buy_it != order_owner_.end()) {
            buy_it->second->on_fill(Side::Buy, trade.price, trade.quantity);
        }

        auto sell_it = order_owner_.find(trade.sell_order_id);
        if (sell_it != order_owner_.end()) {
            sell_it->second->on_fill(Side::Sell, trade.price, trade.quantity);
        }

        // The momentum trader's SMA is fed from executed prices.
        momentum_trader_->update_price(trade.price);
    });

    std::cout << "Starting simulation: " << config_.symbol
              << " @ $" << std::fixed << std::setprecision(2)
              << config_.initial_price
              << " for " << config_.num_ticks << " ticks\n";
    std::cout << std::string(60, '-') << "\n";

    // ---- MAIN LOOP ----
    const auto wall_start = std::chrono::steady_clock::now();

    for (uint64_t tick = 0; tick < config_.num_ticks; tick++) {
        // Geometric Brownian Motion with zero drift: dS/S = sigma dW.
        double return_pct = price_dist(rng);
        true_price_ *= (1.0 + return_pct);

        true_price_ = std::max(true_price_, 0.01);

        market_maker_->set_fair_value(true_price_);
        for (auto& noise : noise_traders_) {
            noise->set_reference_price(true_price_);
        }

        if (tick == 0) {
            momentum_trader_->update_price(true_price_);
        }

        market_maker_->on_tick(tick);
        momentum_trader_->on_tick(tick);
        for (auto& noise : noise_traders_) {
            noise->on_tick(tick);
        }

        if (tick > 0 && tick % (config_.num_ticks / 10) == 0) {
            double pct = 100.0 * tick / config_.num_ticks;
            std::cout << "  " << std::setw(3) << static_cast<int>(pct) << "% complete | "
                      << "Price: $" << std::fixed << std::setprecision(2) << true_price_
                      << " | Trades: " << engine_.stats().total_trades
                      << " | Orders: " << engine_.stats().total_orders << "\n";
        }
    }

    const auto wall_end = std::chrono::steady_clock::now();
    const double wall_sec =
        std::chrono::duration<double>(wall_end - wall_start).count();

    std::cout << "  100% complete\n";
    std::cout << std::string(60, '-') << "\n";

    // ---- BUILD RESULTS ----
    const auto& stats = engine_.stats();

    SimulationResults results;
    results.total_ticks = config_.num_ticks;
    results.total_orders = stats.total_orders;
    results.total_cancels = stats.total_cancels;
    results.wall_clock_sec = wall_sec;
    results.total_trades = stats.total_trades;
    results.total_volume = stats.total_volume;
    results.total_notional = stats.total_notional;
    results.avg_latency_ns = stats.avg_latency_ns();
    results.min_latency_ns = static_cast<double>(stats.min_latency_ns);
    results.max_latency_ns = static_cast<double>(stats.max_latency_ns);
    results.final_price = true_price_;
    results.price_return_pct = ((true_price_ - config_.initial_price)
                                 / config_.initial_price) * 100.0;

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
// PRINT RESULTS: Formatted simulation summary
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

    // Sustained throughput: operations actually executed divided by the
    // wall-clock time the tick loop took. Not derived from avg latency.
    if (results.wall_clock_sec > 0.0) {
        const uint64_t ops = results.total_orders + results.total_cancels;
        const double ops_per_sec = ops / results.wall_clock_sec;
        std::cout << "\nWall clock:  " << std::fixed << std::setprecision(3)
                  << results.wall_clock_sec << " s\n";
        std::cout << "Operations:  " << ops << " ("
                  << results.total_orders << " orders + "
                  << results.total_cancels << " cancels)\n";
        std::cout << "Throughput:  " << std::fixed << std::setprecision(0)
                  << ops_per_sec << " ops/sec ("
                  << std::setprecision(2) << ops_per_sec / 1e6
                  << "M ops/sec), measured over the whole run\n";
    }
}

} // namespace exchange
