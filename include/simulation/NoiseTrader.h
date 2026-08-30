#pragma once

#include "simulation/Trader.h"

namespace exchange {

// ============================================================
// NOISE TRADER: Random order flow (the "background noise")
// ============================================================
//
// Uncorrelated order flow standing in for participants who trade for
// reasons unrelated to price prediction. Its purpose is to keep the
// other two agents from trading only against each other, which would
// produce a degenerate and highly predictable book.
//
// Per tick, with a fixed probability: pick a side at random, a random
// quantity, and a random type (limit near the reference price, or
// market), then submit.
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
