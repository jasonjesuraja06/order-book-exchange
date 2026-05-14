#include "risk/RiskChecker.h"

#include <gtest/gtest.h>

using namespace exchange;
using namespace exchange::risk;

namespace {

RiskLimits tight_limits() {
    RiskLimits l;
    l.max_order_quantity    = 1000;
    l.max_order_notional    = 100'000.0;
    l.max_position_abs      = 5000;
    l.max_notional_exposure = 500'000.0;
    return l;
}

} // namespace

TEST(RiskChecker, AcceptsValidLimitOrder) {
    RiskChecker rc(tight_limits());
    auto r = rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 100);
    EXPECT_EQ(r, RejectReason::None);
    EXPECT_EQ(rc.stats().total_passes, 1u);
}

TEST(RiskChecker, RejectsZeroQuantity) {
    RiskChecker rc(tight_limits());
    auto r = rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 0);
    EXPECT_EQ(r, RejectReason::InvalidQuantity);
}

TEST(RiskChecker, RejectsQuantityAboveMax) {
    RiskChecker rc(tight_limits());
    auto r = rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 2000);
    EXPECT_EQ(r, RejectReason::MaxOrderQuantityExceeded);
}

TEST(RiskChecker, RejectsNegativeLimitPrice) {
    RiskChecker rc(tight_limits());
    auto r = rc.check("AAPL", Side::Sell, OrderType::Limit, -1.0, 100);
    EXPECT_EQ(r, RejectReason::InvalidPrice);
}

TEST(RiskChecker, AllowsMarketOrderWithoutPrice) {
    RiskChecker rc(tight_limits());
    auto r = rc.check("AAPL", Side::Buy, OrderType::Market, 0.0, 100);
    EXPECT_EQ(r, RejectReason::None);
}

TEST(RiskChecker, RejectsNotionalAboveMax) {
    RiskChecker rc(tight_limits());
    // 1000 shares * $200 = $200K > $100K cap
    auto r = rc.check("AAPL", Side::Buy, OrderType::Limit, 200.0, 1000);
    EXPECT_EQ(r, RejectReason::MaxOrderNotionalExceeded);
}

TEST(RiskChecker, RejectsPositionAboveMax) {
    RiskChecker rc(tight_limits());
    // Build up a position approaching the cap
    rc.on_fill("AAPL", Side::Buy, 150.0, 4500);
    EXPECT_EQ(rc.position("AAPL"), 4500);
    // Next order would push to 5100 > 5000 cap
    auto r = rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 600);
    EXPECT_EQ(r, RejectReason::MaxPositionExceeded);
}

TEST(RiskChecker, KillSwitchRejectsEverything) {
    RiskChecker rc(tight_limits());
    rc.engage_kill_switch();
    EXPECT_TRUE(rc.is_killed());
    auto r = rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 100);
    EXPECT_EQ(r, RejectReason::KillSwitchActive);
    rc.disengage_kill_switch();
    EXPECT_FALSE(rc.is_killed());
    r = rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 100);
    EXPECT_EQ(r, RejectReason::None);
}

TEST(RiskChecker, OnFillUpdatesPositionsCorrectly) {
    RiskChecker rc(tight_limits());
    rc.on_fill("AAPL", Side::Buy, 150.0, 100);
    rc.on_fill("AAPL", Side::Buy, 151.0,  50);
    rc.on_fill("AAPL", Side::Sell, 152.0, 30);
    EXPECT_EQ(rc.position("AAPL"), 120);  // 100 + 50 - 30
    EXPECT_EQ(rc.position("MSFT"), 0);    // never traded
}

TEST(RiskChecker, NotionalExposureAccumulates) {
    RiskChecker rc(tight_limits());
    rc.on_fill("AAPL", Side::Buy, 150.0, 100);
    EXPECT_DOUBLE_EQ(rc.notional_exposure(), 15'000.0);
    rc.on_fill("MSFT", Side::Buy, 420.0,  50);
    EXPECT_DOUBLE_EQ(rc.notional_exposure(), 15'000.0 + 21'000.0);
}

TEST(RiskChecker, StatsBreakdownByReason) {
    RiskChecker rc(tight_limits());
    rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 100);   // pass
    rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0,   0);   // InvalidQuantity
    rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 2000);  // MaxOrderQty
    rc.engage_kill_switch();
    rc.check("AAPL", Side::Buy, OrderType::Limit, 150.0, 100);   // KillSwitch

    EXPECT_EQ(rc.stats().total_checks,     4u);
    EXPECT_EQ(rc.stats().total_passes,     1u);
    EXPECT_EQ(rc.stats().total_rejections, 3u);
    EXPECT_EQ(rc.stats().by_reason[static_cast<size_t>(RejectReason::InvalidQuantity)],          1u);
    EXPECT_EQ(rc.stats().by_reason[static_cast<size_t>(RejectReason::MaxOrderQuantityExceeded)], 1u);
    EXPECT_EQ(rc.stats().by_reason[static_cast<size_t>(RejectReason::KillSwitchActive)],         1u);
}
