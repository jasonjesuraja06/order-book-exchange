#include "risk/RiskChecker.h"

#include <cmath>
#include <cstdlib>

namespace exchange::risk {

const char* reject_reason_str(RejectReason r) {
    switch (r) {
        case RejectReason::None:                          return "None";
        case RejectReason::KillSwitchActive:              return "KillSwitchActive";
        case RejectReason::InvalidQuantity:               return "InvalidQuantity";
        case RejectReason::InvalidPrice:                  return "InvalidPrice";
        case RejectReason::MaxOrderQuantityExceeded:      return "MaxOrderQuantityExceeded";
        case RejectReason::MaxOrderNotionalExceeded:      return "MaxOrderNotionalExceeded";
        case RejectReason::MaxPositionExceeded:           return "MaxPositionExceeded";
        case RejectReason::MaxNotionalExposureExceeded:   return "MaxNotionalExposureExceeded";
    }
    return "Unknown";
}

RiskChecker::RiskChecker(RiskLimits limits) : limits_(limits) {}

RejectReason RiskChecker::check(const std::string& symbol,
                                Side               side,
                                OrderType          type,
                                Price              price,
                                Quantity           quantity) {
    // 1. Firm-wide kill switch
    if (kill_switch_.load(std::memory_order_acquire)) {
        stats_.record_reject(RejectReason::KillSwitchActive);
        return RejectReason::KillSwitchActive;
    }

    // 2. Quantity validity
    if (quantity == 0) {
        stats_.record_reject(RejectReason::InvalidQuantity);
        return RejectReason::InvalidQuantity;
    }
    if (quantity > limits_.max_order_quantity) {
        stats_.record_reject(RejectReason::MaxOrderQuantityExceeded);
        return RejectReason::MaxOrderQuantityExceeded;
    }

    // 3. Price validity (limit orders must have positive price)
    if (type == OrderType::Limit && price <= 0.0) {
        stats_.record_reject(RejectReason::InvalidPrice);
        return RejectReason::InvalidPrice;
    }

    // 4. Per-order notional cap (only meaningful for priced orders)
    if (type != OrderType::Market) {
        const double order_notional = price * static_cast<double>(quantity);
        if (order_notional > limits_.max_order_notional) {
            stats_.record_reject(RejectReason::MaxOrderNotionalExceeded);
            return RejectReason::MaxOrderNotionalExceeded;
        }
    }

    // 5. Per-symbol absolute position cap (assume full fill)
    long current_pos = position(symbol);
    long delta       = (side == Side::Buy ? +static_cast<long>(quantity)
                                          : -static_cast<long>(quantity));
    long projected   = current_pos + delta;
    if (std::abs(projected) > limits_.max_position_abs) {
        stats_.record_reject(RejectReason::MaxPositionExceeded);
        return RejectReason::MaxPositionExceeded;
    }

    // 6. Firm-wide notional exposure cap
    if (type != OrderType::Market) {
        const double order_notional      = price * static_cast<double>(quantity);
        const double projected_exposure  = current_notional_exposure_ + order_notional;
        if (projected_exposure > limits_.max_notional_exposure) {
            stats_.record_reject(RejectReason::MaxNotionalExposureExceeded);
            return RejectReason::MaxNotionalExposureExceeded;
        }
    }

    stats_.record_pass();
    return RejectReason::None;
}

void RiskChecker::on_fill(const std::string& symbol, Side side, Price price, Quantity qty) {
    const long delta = (side == Side::Buy ? +static_cast<long>(qty)
                                          : -static_cast<long>(qty));
    positions_[symbol] += delta;
    current_notional_exposure_ += price * static_cast<double>(qty);
}

long RiskChecker::position(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    return it == positions_.end() ? 0L : it->second;
}

} // namespace exchange::risk
