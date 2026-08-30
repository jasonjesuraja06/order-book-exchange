#pragma once

#include "simulation/Trader.h"
#include <deque>

namespace exchange {

// ============================================================
// MOMENTUM TRADER: Follows price trends
// ============================================================
//
// Trades a simple moving average crossover: track the last N executed
// prices, and when the latest price sits more than a threshold above
// the average, buy; more than a threshold below, sell. A cooldown
// keeps it from re-firing on every tick of the same signal.
//
// Its role in the simulation is to be the aggressor. It sends market
// orders that cross the spread, which is what fills the market maker's
// quotes; without a liquidity taker the maker would quote into an
// otherwise inert book.
// ============================================================

class MomentumTrader : public Trader {
public:
    MomentumTrader(MatchingEngine& engine, const std::string& symbol,
                   double starting_cash = 100000.0);

    void on_tick(uint64_t tick_num) override;

    // Feed in the latest price (from trades or mid-price)
    void update_price(double price);

private:
    std::deque<double> price_history_;  // Rolling window of recent prices
    size_t window_size_;                // How many prices to track
    double threshold_;                  // How far from SMA to trigger a trade
    int trade_size_;                    // Shares per trade
    int max_position_;                  // Risk limit
    uint64_t last_trade_tick_ = 0;     // Cooldown: don't trade every tick
    int cooldown_ticks_;               // Minimum ticks between trades
};

} // namespace exchange
