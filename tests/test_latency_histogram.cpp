#include "metrics/LatencyHistogram.h"

#include <gtest/gtest.h>
#include <sstream>

using namespace exchange::metrics;

TEST(LatencyHistogram, RecordsAndCounts) {
    LatencyHistogram h;
    for (int i = 0; i < 1000; ++i) h.record(100);
    EXPECT_EQ(h.total_count(), 1000);
    EXPECT_EQ(h.value_at_percentile(50.0), 100);
}

TEST(LatencyHistogram, PercentilesOnUniformDistribution) {
    LatencyHistogram h;
    for (int i = 1; i <= 1000; ++i) h.record(i);
    // p50 should be ~500, p99 ~990
    EXPECT_NEAR(h.value_at_percentile(50.0), 500, 10);
    EXPECT_NEAR(h.value_at_percentile(99.0), 990, 10);
}

TEST(LatencyHistogram, MinAndMax) {
    LatencyHistogram h;
    h.record(50);
    h.record(100);
    h.record(150);
    EXPECT_EQ(h.min(), 50);
    EXPECT_EQ(h.max(), 150);
}

TEST(LatencyHistogram, ResetClearsState) {
    LatencyHistogram h;
    for (int i = 0; i < 100; ++i) h.record(42);
    EXPECT_EQ(h.total_count(), 100);
    h.reset();
    EXPECT_EQ(h.total_count(), 0);
}

TEST(LatencyHistogram, PrintSummaryFormat) {
    LatencyHistogram h;
    for (int i = 1; i <= 100; ++i) h.record(i);
    std::ostringstream os;
    h.print_summary(os, "test");
    const auto s = os.str();
    EXPECT_NE(s.find("test"),  std::string::npos);
    EXPECT_NE(s.find("count="), std::string::npos);
    EXPECT_NE(s.find("p99="),   std::string::npos);
    EXPECT_NE(s.find("p99.9="), std::string::npos);
}

TEST(LatencyHistogram, HandlesWideDynamicRange) {
    LatencyHistogram h;
    h.record(1);                  // 1 ns
    h.record(1'000);              // 1 µs
    h.record(1'000'000);          // 1 ms
    h.record(1'000'000'000);      // 1 s
    EXPECT_EQ(h.total_count(), 4);
    EXPECT_EQ(h.min(), 1);
    // HdrHistogram approximates within significant_figures resolution;
    // for the highest bucket the recorded value is close to but not exactly 1e9.
    EXPECT_GE(h.max(), 999'000'000);
}
