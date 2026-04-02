#include "simulation/NoiseTrader.h"

#include <cmath>

namespace exchange {

NoiseTrader::NoiseTrader(MatchingEngine& engine, const std::string& symbol,
                         double starting_cash)
    : Trader("NoiseTrader", engine, symbol, starting_cash)
    , reference_price_(100.0)
    , trade_probability_(0.3)   // 30% chance of trading each tick
    , max_size_(200)            // Up to 200 shares per order
    , price_range_(0.50)        // Limit orders within $0.50 of reference
{}

void NoiseTrader::on_tick(uint64_t tick_num) {
    // Roll the dice — do we trade this tick?
    if (random_double(0.0, 1.0) > trade_probability_) return;

    // Random side: 50/50 buy or sell
    Side side = (random_int(0, 1) == 0) ? Side::Buy : Side::Sell;

    // Random quantity: 10 to max_size_, in multiples of 10 (round lots)
    Quantity qty = static_cast<Quantity>(random_int(1, max_size_ / 10) * 10);

    // 70% limit orders, 30% market orders
    // This ratio roughly mirrors real exchange statistics
    if (random_double(0.0, 1.0) < 0.7) {
        // Limit order at a random price near the reference
        double offset = random_double(-price_range_, price_range_);
        double price = reference_price_ + offset;
        price = std::round(price * 100.0) / 100.0;  // Round to cent
        if (price <= 0.0) price = 0.01;

        send_order(side, OrderType::Limit, price, qty);
    } else {
        // Market order — immediate execution at best available price
        send_order(side, OrderType::Market, 0.0, qty);
    }
}

} // namespace exchange
