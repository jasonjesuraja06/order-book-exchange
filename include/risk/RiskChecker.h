#pragma once

#include "core/Types.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace exchange::risk {

// ============================================================
// PRE-TRADE RISK CHECKER
// ============================================================
// Every production exchange and every trading desk has a risk
// layer between the client and the matching engine. The job is
// to reject orders that violate firm-level limits BEFORE they
// hit the book and create exposure that can't be unwound.
//
// Real-world examples:
//   - Knight Capital lost $440M in 45 minutes in 2012 because
//     a risk check was bypassed during a deployment.
//   - Most exchanges enforce fat-finger protection (max qty
//     and max price-from-last) to stop obvious typos.
//
// Our checks (in evaluation order):
//   1. Kill switch (firm-wide halt)
//   2. Quantity validity (qty > 0, qty <= max_order_quantity)
//   3. Price validity (price > 0 for limit orders)
//   4. Order notional cap (price * qty <= max_order_notional)
//   5. Position cap (resulting |net position| <= max_position)
//   6. Firm-wide notional exposure cap
// ============================================================

enum class RejectReason : uint8_t {
    None = 0,
    KillSwitchActive,
    InvalidQuantity,
    InvalidPrice,
    MaxOrderQuantityExceeded,
    MaxOrderNotionalExceeded,
    MaxPositionExceeded,
    MaxNotionalExposureExceeded,
};

const char* reject_reason_str(RejectReason r);

struct RiskLimits {
    Quantity max_order_quantity      = 1'000'000;        // per-order qty cap
    double   max_order_notional      = 10'000'000.0;     // per-order $ cap
    long     max_position_abs        = 5'000'000;        // |net qty| per symbol
    double   max_notional_exposure   = 50'000'000.0;     // firm-wide open notional
};

struct RiskStats {
    uint64_t total_checks    = 0;
    uint64_t total_passes    = 0;
    uint64_t total_rejections = 0;
    uint64_t by_reason[8]    = {0};

    void record_pass()                  { ++total_checks; ++total_passes; }
    void record_reject(RejectReason r)  { ++total_checks; ++total_rejections;
                                          ++by_reason[static_cast<size_t>(r)]; }
};

class RiskChecker {
public:
    explicit RiskChecker(RiskLimits limits = {});

    // Pre-trade check. Returns RejectReason::None if the order is acceptable,
    // otherwise the specific reason for rejection.
    RejectReason check(const std::string& symbol,
                       Side               side,
                       OrderType          type,
                       Price              price,
                       Quantity           quantity);

    // Post-trade callback: update net position and notional exposure on fill.
    void on_fill(const std::string& symbol, Side side, Price price, Quantity qty);

    // Kill switch
    void engage_kill_switch()    { kill_switch_.store(true,  std::memory_order_release); }
    void disengage_kill_switch() { kill_switch_.store(false, std::memory_order_release); }
    bool is_killed() const       { return kill_switch_.load(std::memory_order_acquire); }

    // Inspection
    long   position(const std::string& symbol) const;
    double notional_exposure() const { return current_notional_exposure_; }

    const RiskStats& stats() const { return stats_; }
    void reset_stats()             { stats_ = RiskStats{}; }

    const RiskLimits& limits() const { return limits_; }
    void set_limits(const RiskLimits& l) { limits_ = l; }

private:
    RiskLimits                                  limits_;
    std::atomic<bool>                            kill_switch_{false};
    std::unordered_map<std::string, long>        positions_;
    double                                       current_notional_exposure_ = 0.0;
    RiskStats                                    stats_;
};

} // namespace exchange::risk
