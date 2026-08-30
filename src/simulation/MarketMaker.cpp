#include "simulation/MarketMaker.h"

#include <cmath>
#include <algorithm>

namespace exchange {

MarketMaker::MarketMaker(MatchingEngine& engine, const std::string& symbol,
                         double fair_value, double starting_cash)
    : Trader("MarketMaker", engine, symbol, starting_cash)
    , fair_value_(fair_value)
    , half_spread_(0.05)       // $0.05 on each side = $0.10 total spread
    , quote_size_(100)         // 100 shares per quote (1 "lot")
    , max_position_(1000)      // Never hold more than 1000 shares
    , skew_factor_(0.001)      // Skew $0.001 per share of inventory
{}

// ============================================================
// ON TICK: The market maker's decision loop
// ============================================================
// Called every simulation tick. Here's what happens:
//
// 1. CANCEL old quotes (they're based on stale info)
// 2. CALCULATE new prices based on:
//    a. Fair value (where we think the stock should be)
//    b. Inventory skew (adjust if we're holding too much)
//    c. Random noise (simulate real market microstructure)
// 3. SUBMIT new bid and ask quotes
//
// INVENTORY SKEW (the clever part):
// If we're long 500 shares (too much inventory), we want to SELL.
// So we lower our ask price to attract buyers. We also lower our
// bid price so we're less likely to buy more.
//
// The formula: skew = -position * skew_factor
// Position = +500 → skew = -0.50 → both prices drop by $0.50
// Position = -300 → skew = +0.30 → both prices rise by $0.30
//
// This naturally keeps our position near zero, which is the goal
// of market making, profit from the spread, not from direction.
// ============================================================
void MarketMaker::on_tick(uint64_t tick_num) {
    // Step 1: Cancel stale quotes
    if (active_bid_id_ > 0) {
        send_cancel(active_bid_id_);
        active_bid_id_ = 0;
    }
    if (active_ask_id_ > 0) {
        send_cancel(active_ask_id_);
        active_ask_id_ = 0;
    }

    // Step 2: Calculate inventory skew
    // Negative position adjustment when long (want to sell)
    // Positive position adjustment when short (want to buy)
    double skew = -stats_.position * skew_factor_;

    // Add small random noise to prevent predictable patterns.
    // Real market makers add randomization so HFT predators
    // can't easily detect and exploit their quoting pattern.
    double noise = random_double(-0.02, 0.02);

    // Step 3: Calculate bid and ask prices
    // bid = fair_value - half_spread + skew + noise
    // ask = fair_value + half_spread + skew + noise
    double bid_price = fair_value_ - half_spread_ + skew + noise;
    double ask_price = fair_value_ + half_spread_ + skew + noise;

    // Round to nearest cent (exchanges have tick sizes)
    bid_price = std::round(bid_price * 100.0) / 100.0;
    ask_price = std::round(ask_price * 100.0) / 100.0;

    // Ensure ask > bid (crossed quotes are invalid)
    if (ask_price <= bid_price) {
        ask_price = bid_price + 0.01;
    }

    // Step 4: Submit new quotes
    // Only quote the buy side if we're not at max long position
    if (stats_.position < max_position_) {
        active_bid_id_ = send_order(Side::Buy, OrderType::Limit,
                                     bid_price, quote_size_);
    }

    // Only quote the sell side if we're not at max short position
    if (stats_.position > -max_position_) {
        active_ask_id_ = send_order(Side::Sell, OrderType::Limit,
                                     ask_price, quote_size_);
    }
}

} // namespace exchange
