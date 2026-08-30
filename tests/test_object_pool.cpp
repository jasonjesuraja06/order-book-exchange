#include <gtest/gtest.h>

#include "core/Order.h"
#include "core/ObjectPool.h"

#include <vector>

using namespace exchange;

TEST(ObjectPool, PreallocatesRequestedCapacity) {
    ObjectPool<Order> pool(16);
    EXPECT_EQ(pool.capacity(), 16u);
    EXPECT_EQ(pool.active_count(), 0u);
}

TEST(ObjectPool, AcquireAndReleaseTrackActiveCount) {
    ObjectPool<Order> pool(4);

    Order* a = pool.acquire();
    Order* b = pool.acquire();
    EXPECT_EQ(pool.active_count(), 2u);

    pool.release(a);
    EXPECT_EQ(pool.active_count(), 1u);

    pool.release(b);
    EXPECT_EQ(pool.active_count(), 0u);
}

TEST(ObjectPool, ReleasedObjectsAreRecycled) {
    ObjectPool<Order> pool(2);

    Order* a = pool.acquire();
    pool.release(a);
    Order* b = pool.acquire();

    EXPECT_EQ(a, b);                 // same slot handed back out
    EXPECT_EQ(pool.capacity(), 2u);  // no growth was needed
}

// Regression: growing the pool past its initial capacity must not move
// objects that are already checked out. The order book stores raw Order*
// in its price levels and its ID index, so any relocation on growth would
// leave every resting order dangling.
TEST(ObjectPool, GrowthDoesNotInvalidateOutstandingPointers) {
    constexpr size_t kInitial = 8;
    ObjectPool<Order> pool(kInitial);

    std::vector<Order*> held;
    for (size_t i = 0; i < kInitial; ++i) {
        Order* o = pool.acquire();
        o->id = static_cast<OrderId>(i + 1);
        held.push_back(o);
    }

    // Exhaust and then force the pool well past its initial capacity.
    for (size_t i = 0; i < kInitial * 4; ++i) {
        Order* o = pool.acquire();
        o->id = 9999;
        held.push_back(o);
    }

    // Every pointer handed out before growth must still address its order.
    for (size_t i = 0; i < kInitial; ++i) {
        EXPECT_EQ(held[i]->id, static_cast<OrderId>(i + 1))
            << "pointer handed out at index " << i << " was invalidated by growth";
    }

    EXPECT_EQ(pool.capacity(), kInitial * 5);
    EXPECT_EQ(pool.active_count(), kInitial * 5);
}

// The README quotes this size; tools/print_sizes.cpp prints it. Keep the
// three in agreement.
TEST(OrderLayout, SizeMatchesDocumentedValue) {
    EXPECT_EQ(sizeof(Order), 40u);
    EXPECT_EQ(alignof(Order), 8u);
    EXPECT_LT(sizeof(Order), 64u);  // a single order fits within one cache line
}
