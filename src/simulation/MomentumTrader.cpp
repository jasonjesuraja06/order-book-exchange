#include "simulation/MomentumTrader.h"

#include <numeric>

namespace exchange {

MomentumTrader::MomentumTrader(MatchingEngine& engine,
                               const std::string& symbol,
                               double starting_cash)
    : Trader("MomentumTrader", engine, symbol, starting_cash)
    , window_size_(20)        // SMA lookback in prints
    , threshold_(0.10)        // Trade when price is $0.10 away from SMA
    , trade_size_(50)         // 50 shares per trade
    , max_position_(500)      // Never hold more than 500 shares
    , cooldown_ticks_(5)      // Wait at least 5 ticks between trades
{}

void MomentumTrader::update_price(double price) {
    price_history_.push_back(price);

    // Rolling window: drop the oldest print once the window is full.
    if (price_history_.size() > window_size_) {
        price_history_.pop_front();
    }
}

// ============================================================
// ON TICK — signal evaluation
// ============================================================
// Wait for a full window, average it, and compare the latest price
// against that average. Deviation beyond the threshold in either
// direction opens a position, subject to the position cap. Averaging
// is what suppresses single-print spikes; only sustained movement
// clears the threshold.
// ============================================================
void MomentumTrader::on_tick(uint64_t tick_num) {
    if (price_history_.size() < window_size_) return;

    // Cooldown between trades on the same signal.
    if (tick_num - last_trade_tick_ < static_cast<uint64_t>(cooldown_ticks_)) return;

    double sum = std::accumulate(price_history_.begin(),
                                  price_history_.end(), 0.0);
    double sma = sum / price_history_.size();
    double current_price = price_history_.back();

    double deviation = current_price - sma;

    if (deviation > threshold_ && stats_.position < max_position_) {
        // Market order rather than limit: the strategy trades the
        // continuation of a move, so it pays the spread for immediacy
        // instead of queuing and risking a miss.
        send_order(Side::Buy, OrderType::Market, 0.0, trade_size_);
        last_trade_tick_ = tick_num;

    } else if (deviation < -threshold_ && stats_.position > -max_position_) {
        send_order(Side::Sell, OrderType::Market, 0.0, trade_size_);
        last_trade_tick_ = tick_num;
    }
}

} // namespace exchange
