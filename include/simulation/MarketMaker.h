#pragma once

#include "simulation/Trader.h"

namespace exchange {

// ============================================================
// MARKET MAKER — Provides liquidity by quoting both sides
// ============================================================
//
// WHAT MARKET MAKERS DO (ELI5):
// Imagine you're at a currency exchange booth at an airport.
// The booth says: "We buy euros at $1.08, we sell euros at $1.12"
// That $0.04 gap is the "spread" — the booth's profit.
//
// Market makers do this for stocks. They simultaneously offer to:
//   BUY at $99.95 (the bid)  — slightly below fair value
//   SELL at $100.05 (the ask) — slightly above fair value
//
// If someone buys from them at $100.05 and someone else sells to
// them at $99.95, they make $0.10 per share without taking any
// directional risk. This is called "capturing the spread."
//
// THE RISK:
// If the stock price moves against you before the other side fills,
// you lose money. Example: you buy at $99.95, then the price drops
// to $98.00 — you're down $1.95/share. Market makers manage this
// by:
// 1. Keeping positions small (cancel and re-quote frequently)
// 2. Widening the spread when volatility increases
// 3. Skewing quotes when they have too much inventory
//
// THIS IS LITERALLY WHAT JANE STREET AND CITADEL SECURITIES DO.
// If an interviewer asks "do you understand market making?", you
// can walk them through this code.
//
// STRATEGY:
// Every tick, the market maker:
// 1. Cancels all existing quotes (stale prices = risk)
// 2. Calculates a fair value estimate
// 3. Places new bid and ask quotes around fair value
// 4. Adjusts the spread based on inventory risk
//    (if holding too many shares, lower the ask to sell faster)
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
