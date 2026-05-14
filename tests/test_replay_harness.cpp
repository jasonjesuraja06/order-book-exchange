#include "core/MatchingEngine.h"
#include "replay/ReplayHarness.h"
#include "risk/RiskChecker.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

using namespace exchange;

namespace {

// Write a small tape to a tmp file for the test.
std::string write_tmp_tape() {
    const std::string path = "/tmp/exchange_test_tape.csv";
    std::ofstream f(path);
    f << "timestamp_ns,symbol,side,type,price,quantity,action,order_id\n"
      << "1000,AAPL,B,L,100.00,10,N,1\n"
      << "2000,AAPL,S,L,100.00,10,N,2\n"      // matches order 1 (trade)
      << "3000,AAPL,B,L,99.50,5,N,3\n"
      << "4000,AAPL,S,L,100.50,5,N,4\n"
      << "5000,AAPL,,,,,C,3\n"                  // cancel order 3
      << "6000,AAPL,S,L,99.40,10,N,5\n"         // crosses with implicit bid: none left
      << "7000,AAPL,B,L,99.40,10,N,6\n";        // matches order 5
    return path;
}

} // namespace

TEST(ReplayHarness, BasicTapeReplay) {
    const std::string tape = write_tmp_tape();
    MatchingEngine               engine;
    risk::RiskChecker            risk;
    replay::ReplayHarness        harness(engine, &risk);

    auto result = harness.replay(tape);

    EXPECT_EQ(result.events_processed, 7u);
    EXPECT_EQ(result.new_orders,       6u);
    EXPECT_EQ(result.cancels,          1u);
    EXPECT_GE(result.trades_generated, 2u);     // orders 1<->2 and 5<->6
    EXPECT_GE(result.volume_traded,    20u);
    EXPECT_GT(result.total_duration_ns, 0);
    std::remove(tape.c_str());
}

TEST(ReplayHarness, RiskBlocksOversizedOrder) {
    const std::string path = "/tmp/exchange_risk_tape.csv";
    {
        std::ofstream f(path);
        f << "timestamp_ns,symbol,side,type,price,quantity,action,order_id\n"
          << "1000,AAPL,B,L,100.00,1000000000,N,1\n";   // 1 billion shares: rejects
    }
    MatchingEngine        engine;
    risk::RiskLimits      tight;
    tight.max_order_quantity = 10'000;
    risk::RiskChecker     risk(tight);
    replay::ReplayHarness harness(engine, &risk);

    auto result = harness.replay(path);
    EXPECT_EQ(result.risk_rejections, 1u);
    EXPECT_EQ(result.trades_generated, 0u);
    std::remove(path.c_str());
}

TEST(ReplayHarness, LatencyHistogramPopulated) {
    const std::string tape = write_tmp_tape();
    MatchingEngine        engine;
    risk::RiskChecker     risk;
    replay::ReplayHarness harness(engine, &risk);

    auto result = harness.replay(tape);
    EXPECT_GT(harness.latency().total_count(), 0);
    EXPECT_GT(result.latency_p50_ns,           0);
    EXPECT_GE(result.latency_p99_ns,           result.latency_p50_ns);
    EXPECT_GE(result.latency_p999_ns,          result.latency_p99_ns);
    EXPECT_GE(result.latency_max_ns,           result.latency_p999_ns);
    std::remove(tape.c_str());
}

TEST(ReplayHarness, SampleTapeRuns) {
#ifdef SAMPLE_TAPE_PATH
    MatchingEngine        engine;
    risk::RiskChecker     risk;
    replay::ReplayHarness harness(engine, &risk);

    auto result = harness.replay(SAMPLE_TAPE_PATH);
    EXPECT_GE(result.events_processed, 40u);
    EXPECT_GT(result.trades_generated,  0u);
    EXPECT_GT(harness.latency().total_count(), 0);
#endif
}
