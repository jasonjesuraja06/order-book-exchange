#include "simulation/MomentumTrader.h"

#include <numeric>

namespace exchange {

MomentumTrader::MomentumTrader(MatchingEngine& engine,
                               const std::string& symbol,
                               double starting_cash)
    : Trader("MomentumTrader", engine, symbol, starting_cash)
    , window_size_(20)        // Look at last 20 prices
    , threshold_(0.10)        // Trade when price is $0.10 away from SMA
    , trade_size_(50)         // 50 shares per trade
    , max_position_(500)      // Never hold more than 500 shares
    , cooldown_ticks_(5)      // Wait at least 5 ticks between trades
{}

void MomentumTrader::update_price(double price) {
    price_history_.push_back(price);

    // Keep only the last window_size_ prices.
    // std::deque is a double-ended queue — efficient push/pop on both ends.
    // As new prices come in, old ones fall off the back.
    if (price_history_.size() > window_size_) {
        price_history_.pop_front();
    }
}

// ============================================================
// ON TICK — The momentum trader's decision loop
// ============================================================
// 1. Need enough history? If not, wait.
// 2. Calculate SMA (Simple Moving Average) = sum(prices) / count
// 3. Compare current price to SMA
// 4. If bullish signal + within risk limits → BUY
// 5. If bearish signal + within risk limits → SELL
//
// SMA EXPLAINED (ELI5):
// If the last 5 prices were: $100, $101, $102, $101, $103
// SMA = ($100 + $101 + $102 + $101 + $103) / 5 = $101.40
// Current price ($103) > SMA ($101.40) → trending UP → BUY signal
//
// The SMA smooths out noise. A single price spike doesn't trigger
// a trade, but sustained movement does.
// ============================================================
void MomentumTrader::on_tick(uint64_t tick_num) {
    // Need a full window of prices before we can calculate SMA
    if (price_history_.size() < window_size_) return;

    // Cooldown: don't spam orders every tick
    if (tick_num - last_trade_tick_ < static_cast<uint64_t>(cooldown_ticks_)) return;

    // Calculate Simple Moving Average
    // std::accumulate sums all elements: accumulate(begin, end, initial_value)
    double sum = std::accumulate(price_history_.begin(),
                                  price_history_.end(), 0.0);
    double sma = sum / price_history_.size();
    double current_price = price_history_.back();

    // Signal: how far is the current price from the average?
    double deviation = current_price - sma;

    if (deviation > threshold_ && stats_.position < max_position_) {
        // BULLISH: Price is above average → trending up → BUY
        // Use a market order — momentum traders want immediate execution,
        // they don't want to wait in a queue (they're paying the spread
        // because they believe the trend will continue in their favor)
        send_order(Side::Buy, OrderType::Market, 0.0, trade_size_);
        last_trade_tick_ = tick_num;

    } else if (deviation < -threshold_ && stats_.position > -max_position_) {
        // BEARISH: Price is below average → trending down → SELL
        send_order(Side::Sell, OrderType::Market, 0.0, trade_size_);
        last_trade_tick_ = tick_num;
    }
}

} // namespace exchange
