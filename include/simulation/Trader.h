#pragma once

#include "core/MatchingEngine.h"

#include <string>
#include <vector>
#include <random>

namespace exchange {

// ============================================================
// TRADER: Base class for all simulated trading agents
// ============================================================
//
// Each bot type (market maker, momentum, noise) inherits from this
// and implements its own strategy via the on_tick() method.
//
// on_tick() runs once per simulation clock advance. Each agent reads
// the market and decides whether to submit orders, cancel resting
// ones, or do nothing.
//
// PORTFOLIO TRACKING:
// Each bot tracks its own P&L (profit and loss):
//   - position: net shares held (positive long, negative short)
//   - cash: cash balance
//   - P&L = cash + (position * current_price) - starting_cash
//
// P&L is the comparison metric across strategies.
// ============================================================

struct TraderStats {
    int64_t  position = 0;         // Net shares held (+ = long, - = short)
    double   cash = 0.0;           // Cash balance
    double   starting_cash = 0.0;  // Initial cash (for P&L calculation)
    uint64_t orders_sent = 0;      // Total orders submitted
    uint64_t fills_received = 0;   // Total fills (trades) received
    uint64_t cancels_sent = 0;     // Total cancels submitted
    double   total_pnl = 0.0;      // Realized + unrealized P&L

    // Calculate total P&L given the current market price.
    // Realized P&L = cash - starting_cash (profit from closed trades)
    // Unrealized P&L = position * current_price (value of open position)
    // Total P&L = realized + unrealized
    double calculate_pnl(double current_price) const {
        return (cash - starting_cash) + (position * current_price);
    }
};

class Trader {
public:
    Trader(const std::string& name, MatchingEngine& engine,
           const std::string& symbol, double starting_cash = 100000.0)
        : name_(name)
        , engine_(engine)
        , symbol_(symbol)
        , rng_(std::random_device{}())
    {
        stats_.cash = starting_cash;
        stats_.starting_cash = starting_cash;
    }

    virtual ~Trader() = default;

    // Called every tick, the bot decides what to do.
    // tick_num is the current simulation step (0, 1, 2, ...)
    // This is pure virtual, each bot type MUST implement it.
    virtual void on_tick(uint64_t tick_num) = 0;

    // Called when one of this bot's orders is filled.
    // Updates position and cash automatically.
    void on_fill(Side side, Price price, Quantity quantity) {
        stats_.fills_received++;
        if (side == Side::Buy) {
            stats_.position += quantity;
            stats_.cash -= price * quantity;
        } else {
            stats_.position -= quantity;
            stats_.cash += price * quantity;
        }
    }

    const std::string& name() const { return name_; }
    const TraderStats& stats() const { return stats_; }

    // Callback for the simulation to track order ownership.
    // Called every time this trader submits an order.
    using OrderCallback = std::function<void(OrderId, Trader*)>;
    void set_order_callback(OrderCallback cb) { order_callback_ = std::move(cb); }

protected:
    std::string name_;
    MatchingEngine& engine_;
    std::string symbol_;
    TraderStats stats_;
    std::mt19937 rng_;     // Mersenne Twister random number generator
                           // (same one used in quantitative finance)

    OrderCallback order_callback_;

    // Helper: submit an order and track it
    OrderId send_order(Side side, OrderType type, Price price, Quantity quantity) {
        stats_.orders_sent++;
        OrderId id = engine_.submit_order(symbol_, side, type, price, quantity);
        if (id > 0 && order_callback_) {
            order_callback_(id, this);
        }
        return id;
    }

    // Helper: cancel an order and track it
    bool send_cancel(OrderId id) {
        stats_.cancels_sent++;
        return engine_.cancel_order(symbol_, id);
    }

    // Helper: generate a random double in [min, max]
    double random_double(double min, double max) {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(rng_);
    }

    // Helper: generate a random int in [min, max]
    int random_int(int min, int max) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(rng_);
    }
};

} // namespace exchange
