// ============================================================
// ORDER BOOK TESTS — Validate the core data structure
// ============================================================

#include <gtest/gtest.h>
#include "core/OrderBook.h"

using namespace exchange;

class OrderBookTest : public ::testing::Test {
protected:
    // This runs before EACH test — gives us a fresh empty book.
    // "fixtures" in Google Test let you share setup code.
    OrderBook book{"AAPL"};
};

// ---- Basic Operations ----

TEST_F(OrderBookTest, EmptyBookHasZeroPrices) {
    EXPECT_DOUBLE_EQ(book.best_bid(), 0.0);
    EXPECT_DOUBLE_EQ(book.best_ask(), 0.0);
    EXPECT_DOUBLE_EQ(book.spread(), 0.0);
    EXPECT_EQ(book.order_count(), 0);
}

TEST_F(OrderBookTest, AddSingleBid) {
    book.add_order(Side::Buy, OrderType::Limit, 100.00, 50);

    EXPECT_DOUBLE_EQ(book.best_bid(), 100.00);
    EXPECT_DOUBLE_EQ(book.best_ask(), 0.0);  // No asks yet
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.total_bid_quantity(), 50);
}

TEST_F(OrderBookTest, AddSingleAsk) {
    book.add_order(Side::Sell, OrderType::Limit, 101.00, 30);

    EXPECT_DOUBLE_EQ(book.best_ask(), 101.00);
    EXPECT_DOUBLE_EQ(book.best_bid(), 0.0);
    EXPECT_EQ(book.order_count(), 1);
    EXPECT_EQ(book.total_ask_quantity(), 30);
}

TEST_F(OrderBookTest, BestBidIsHighest) {
    // Add bids at different prices — best bid should be highest
    book.add_order(Side::Buy, OrderType::Limit, 99.00, 100);
    book.add_order(Side::Buy, OrderType::Limit, 101.00, 100);
    book.add_order(Side::Buy, OrderType::Limit, 100.00, 100);

    EXPECT_DOUBLE_EQ(book.best_bid(), 101.00);
    EXPECT_EQ(book.bid_depth(), 3);  // 3 distinct price levels
}

TEST_F(OrderBookTest, BestAskIsLowest) {
    // Add asks at different prices — best ask should be lowest
    book.add_order(Side::Sell, OrderType::Limit, 102.00, 100);
    book.add_order(Side::Sell, OrderType::Limit, 100.00, 100);
    book.add_order(Side::Sell, OrderType::Limit, 101.00, 100);

    EXPECT_DOUBLE_EQ(book.best_ask(), 100.00);
    EXPECT_EQ(book.ask_depth(), 3);
}

TEST_F(OrderBookTest, SpreadCalculation) {
    book.add_order(Side::Buy, OrderType::Limit, 99.95, 100);
    book.add_order(Side::Sell, OrderType::Limit, 100.05, 100);

    EXPECT_NEAR(book.spread(), 0.10, 0.001);
    EXPECT_NEAR(book.mid_price(), 100.00, 0.001);
}

// ---- Cancellation ----

TEST_F(OrderBookTest, CancelOrder) {
    Order* order = book.add_order(Side::Buy, OrderType::Limit, 100.00, 50);
    OrderId id = order->id;

    EXPECT_EQ(book.order_count(), 1);
    EXPECT_TRUE(book.cancel_order(id));
    EXPECT_EQ(book.order_count(), 0);
    EXPECT_DOUBLE_EQ(book.best_bid(), 0.0);  // No more bids
}

TEST_F(OrderBookTest, CancelNonExistentOrderReturnsFalse) {
    EXPECT_FALSE(book.cancel_order(99999));
}

TEST_F(OrderBookTest, CancelOnlyRemovesTargetOrder) {
    Order* o1 = book.add_order(Side::Buy, OrderType::Limit, 100.00, 50);
    book.add_order(Side::Buy, OrderType::Limit, 100.00, 30);

    EXPECT_EQ(book.total_bid_quantity(), 80);

    book.cancel_order(o1->id);

    EXPECT_EQ(book.total_bid_quantity(), 30);
    EXPECT_EQ(book.order_count(), 1);
}

// ---- Price Level Cleanup ----

TEST_F(OrderBookTest, EmptyPriceLevelIsRemoved) {
    Order* order = book.add_order(Side::Sell, OrderType::Limit, 100.00, 50);

    EXPECT_EQ(book.ask_depth(), 1);

    book.cancel_order(order->id);

    EXPECT_EQ(book.ask_depth(), 0);  // Price level should be gone
}

// ---- Time Priority ----

TEST_F(OrderBookTest, TimePriorityPreserved) {
    // Add two orders at the same price. First one should be at front.
    Order* first = book.add_order(Side::Buy, OrderType::Limit, 100.00, 50);
    Order* second = book.add_order(Side::Buy, OrderType::Limit, 100.00, 30);

    // The best bid level should have first order at front (FIFO)
    PriceLevel* level = book.best_bid_level();
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->front()->id, first->id);
    EXPECT_EQ(level->back()->id, second->id);
}

// ---- Reduce Order ----

TEST_F(OrderBookTest, ReduceOrderQuantity) {
    Order* order = book.add_order(Side::Buy, OrderType::Limit, 100.00, 100);

    EXPECT_TRUE(book.reduce_order(order->id, 50));
    EXPECT_EQ(book.total_bid_quantity(), 50);
}

TEST_F(OrderBookTest, ReduceToZeroCancels) {
    Order* order = book.add_order(Side::Buy, OrderType::Limit, 100.00, 100);

    EXPECT_TRUE(book.reduce_order(order->id, 0));
    EXPECT_EQ(book.order_count(), 0);
}

TEST_F(OrderBookTest, CannotIncreaseQuantity) {
    Order* order = book.add_order(Side::Buy, OrderType::Limit, 100.00, 100);

    // Increasing quantity should fail (would need cancel + new order)
    EXPECT_FALSE(book.reduce_order(order->id, 200));
    EXPECT_EQ(book.total_bid_quantity(), 100);
}
