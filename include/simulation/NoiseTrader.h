#pragma once

#include "simulation/Trader.h"

namespace exchange {

// ============================================================
// NOISE TRADER — Random order flow (the "background noise")
// ============================================================
//
// WHY NOISE MATTERS (ELI5):
// In a real market, there are millions of participants with
// different time horizons, strategies, and reasons to trade.
// A retiree selling to fund their vacation. A mutual fund
// rebalancing quarterly. An employee exercising stock options.
//
// None of these people are trying to "beat the market" — they're
// just trading for their own reasons. This creates random-looking
// order flow that provides LIQUIDITY (something for market makers
// and momentum traders to trade against).
//
// Without noise traders in our simulation, the market maker and
// momentum trader would only trade with each other, creating
// unrealistic, predictable patterns.
//
// STRATEGY:
// Every tick, with some probability:
// 1. Flip a coin → buy or sell
// 2. Pick a random quantity
// 3. Pick a random order type (70% limit near mid, 30% market)
// 4. Submit the order
//
// This simulates the "retail flow" that real exchanges see.
// ============================================================

class NoiseTrader : public Trader {
public:
    NoiseTrader(MatchingEngine& engine, const std::string& symbol,
                double starting_cash = 100000.0);

    void on_tick(uint64_t tick_num) override;

    // Update the reference price (so limit orders are near the market)
    void set_reference_price(double price) { reference_price_ = price; }

private:
    double reference_price_;   // Current approximate market price
    double trade_probability_; // Probability of trading on any given tick
    int    max_size_;          // Maximum order size
    double price_range_;       // How far from reference price to place limits
};

} // namespace exchange
