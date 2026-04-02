// ============================================================
// ORDER TESTS — Validate the basic building block
// ============================================================
// Google Test basics:
// - TEST(GroupName, TestName) defines a test case
// - EXPECT_EQ(a, b) checks a == b (test continues if it fails)
// - ASSERT_EQ(a, b) checks a == b (test STOPS if it fails)
// - Use EXPECT for non-critical checks, ASSERT for preconditions
// ============================================================

#include <gtest/gtest.h>
#include "core/Order.h"

using namespace exchange;

TEST(OrderTest, ConstructorSetsFields) {
    Order order(1, Side::Buy, OrderType::Limit, 150.00, 100);

    EXPECT_EQ(order.id, 1);
    EXPECT_EQ(order.side, Side::Buy);
    EXPECT_EQ(order.type, OrderType::Limit);
    EXPECT_DOUBLE_EQ(order.price, 150.00);
    EXPECT_EQ(order.quantity, 100);
    EXPECT_EQ(order.remaining_qty, 100);  // Starts fully unfilled
    EXPECT_EQ(order.status, OrderStatus::Accepted);
    EXPECT_GT(order.timestamp, 0u);  // Should be set to now
}

TEST(OrderTest, FilledQuantityTracking) {
    Order order(1, Side::Buy, OrderType::Limit, 150.00, 100);

    EXPECT_EQ(order.filled_qty(), 0);
    EXPECT_FALSE(order.is_filled());

    // Simulate a partial fill
    order.remaining_qty = 40;
    EXPECT_EQ(order.filled_qty(), 60);
    EXPECT_FALSE(order.is_filled());

    // Simulate a complete fill
    order.remaining_qty = 0;
    EXPECT_EQ(order.filled_qty(), 100);
    EXPECT_TRUE(order.is_filled());
}

TEST(OrderTest, TradeStruct) {
    Trade trade;
    trade.buy_order_id = 1;
    trade.sell_order_id = 2;
    trade.price = 150.00;
    trade.quantity = 50;
    trade.timestamp = now_ns();

    EXPECT_EQ(trade.buy_order_id, 1);
    EXPECT_EQ(trade.sell_order_id, 2);
    EXPECT_DOUBLE_EQ(trade.price, 150.00);
    EXPECT_EQ(trade.quantity, 50);
}

TEST(OrderTest, SizeIsCompact) {
    // Verify our Order struct is reasonably compact.
    // We want it small enough that two orders fit in a cache line (64 bytes).
    EXPECT_LE(sizeof(Order), 48);
}
