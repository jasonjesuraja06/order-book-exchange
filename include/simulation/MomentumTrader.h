#pragma once

#include "simulation/Trader.h"
#include <deque>

namespace exchange {

// ============================================================
// MOMENTUM TRADER — Follows price trends
// ============================================================
//
// STRATEGY (ELI5):
// "If the price has been going up, buy. If it's been going down, sell."
//
// This is one of the oldest trading strategies in existence.
// Momentum traders believe that stocks in motion tend to stay in
// motion (Newton's first law, but for prices).
//
// HOW IT WORKS:
// 1. Track the last N prices (a "moving window")
// 2. Calculate a simple moving average (SMA)
// 3. If current price > SMA by a threshold → price is trending UP → BUY
// 4. If current price < SMA by a threshold → price is trending DOWN → SELL
//
// WHY THIS BOT EXISTS IN THE SIMULATION:
// Momentum traders are the "aggressors" — they cross the spread
// (pay the market maker's price) because they believe the trend
// will make up for it. They generate trades. Without them, the
// market maker would just sit there quoting with no fills.
//
// In real markets, momentum strategies can be very profitable
// but are also the most crowded — many hedge funds and HFT firms
// run momentum strategies, so the edge has diminished over time.
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
