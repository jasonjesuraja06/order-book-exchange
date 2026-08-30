#pragma once

#include "simulation/Trader.h"

namespace exchange {

// ============================================================
// MARKET MAKER: Provides liquidity by quoting both sides
// ============================================================
//
// Quotes both sides around a fair value estimate, earning the spread
// when both quotes fill and carrying directional risk whenever they
// do not. Inventory is the exposure that has to be managed: a filled
// bid leaves the maker long, and a subsequent adverse price move is a
// loss against that position.
//
// Per tick the agent cancels its outstanding quotes, since a stale
// quote is an option written to the rest of the market, then re-quotes
// a bid and an ask around fair value with the spread skewed against
// its current inventory so the side that reduces the position is more
// likely to fill.
// ============================================================

class MarketMaker : public Trader {
public:
    MarketMaker(MatchingEngine& engine, const std::string& symbol,
                double fair_value, double starting_cash = 100000.0);

    void on_tick(uint64_t tick_num) override;

    // Set the fair value externally (e.g., from a price feed)
    void set_fair_value(double fv) { fair_value_ = fv; }

private:
    double fair_value_;        // Our estimate of the stock's true value
    double half_spread_;       // Half the bid-ask spread (e.g., $0.05)
    int    quote_size_;        // How many shares per quote
    int    max_position_;      // Max shares we're willing to hold
    double skew_factor_;       // How aggressively to skew on inventory

    // Track our active orders so we can cancel them next tick
    OrderId active_bid_id_ = 0;
    OrderId active_ask_id_ = 0;
};

} // namespace exchange
