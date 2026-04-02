// ============================================================
// BENCHMARKS — Measure raw performance
// ============================================================
//
// Google Benchmark runs each function thousands of times and
// reports the average time per iteration. This is how you get
// the "X million orders/sec" number for your resume.
//
// HOW IT WORKS:
// The framework calls your function in a loop:
//   for (auto _ : state) {
//       // your code here — timed
//   }
// It automatically determines how many iterations are needed
// for a statistically reliable measurement (usually millions).
//
// IMPORTANT: The benchmark measures the AMORTIZED cost per
// operation. Individual operations may vary (cache misses, branch
// mispredictions), but the average is what matters for throughput.
// ============================================================

#include <benchmark/benchmark.h>
#include "core/MatchingEngine.h"

using namespace exchange;

// ============================================================
// BENCHMARK: Limit order insertion (no match)
// ============================================================
// Measures: How fast can we add orders to the book?
// Expected: O(log N) per order due to std::map insertion
// ============================================================
static void BM_LimitOrderInsert(benchmark::State& state) {
    MatchingEngine engine;
    double price = 100.00;
    uint64_t i = 0;

    for (auto _ : state) {
        // Alternate buy/sell at different prices so nothing matches.
        // We want to measure pure insertion speed, not matching.
        if (i % 2 == 0) {
            engine.submit_order("AAPL", Side::Buy, OrderType::Limit,
                                price - (i % 100) * 0.01, 100);
        } else {
            engine.submit_order("AAPL", Side::Sell, OrderType::Limit,
                                price + (i % 100) * 0.01, 100);
        }
        i++;
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LimitOrderInsert);

// ============================================================
// BENCHMARK: Limit order match (guaranteed fill)
// ============================================================
// Measures: How fast can we match two orders?
// This is the critical path — submit + match + trade generation.
// ============================================================
static void BM_LimitOrderMatch(benchmark::State& state) {
    MatchingEngine engine;

    for (auto _ : state) {
        // Submit a sell, then a matching buy — generates one trade
        engine.submit_order("AAPL", Side::Sell, OrderType::Limit, 100.00, 100);
        engine.submit_order("AAPL", Side::Buy, OrderType::Limit, 100.00, 100);
    }

    // Each iteration = 2 orders
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_LimitOrderMatch);

// ============================================================
// BENCHMARK: Market order fill
// ============================================================
// Measures: How fast is a market order when liquidity exists?
// ============================================================
static void BM_MarketOrderFill(benchmark::State& state) {
    MatchingEngine engine;

    // Pre-fill the book with sell orders (liquidity)
    for (int i = 0; i < 1000; i++) {
        engine.submit_order("AAPL", Side::Sell, OrderType::Limit,
                            100.00 + i * 0.01, 100);
    }

    for (auto _ : state) {
        // Market buy — matches against best ask
        engine.submit_order("AAPL", Side::Buy, OrderType::Market, 0.0, 100);

        // Replenish the level we just consumed
        engine.submit_order("AAPL", Side::Sell, OrderType::Limit,
                            100.00, 100);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MarketOrderFill);

// ============================================================
// BENCHMARK: Cancel order
// ============================================================
// Measures: O(1) cancel via the lookup map.
// HFT firms send 10-100x more cancels than orders, so this
// is arguably the most important benchmark.
// ============================================================
static void BM_CancelOrder(benchmark::State& state) {
    MatchingEngine engine;

    // Pre-fill with orders to cancel
    std::vector<OrderId> ids;
    for (int i = 0; i < 100000; i++) {
        OrderId id = engine.submit_order("AAPL", Side::Buy, OrderType::Limit,
                                          100.00 - (i % 100) * 0.01, 100);
        ids.push_back(id);
    }

    size_t idx = 0;
    for (auto _ : state) {
        if (idx < ids.size()) {
            engine.cancel_order("AAPL", ids[idx++]);
        } else {
            state.PauseTiming();
            // Refill orders
            ids.clear();
            for (int i = 0; i < 100000; i++) {
                OrderId id = engine.submit_order("AAPL", Side::Buy,
                    OrderType::Limit, 100.00 - (i % 100) * 0.01, 100);
                ids.push_back(id);
            }
            idx = 0;
            state.ResumeTiming();
        }
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CancelOrder);

// ============================================================
// BENCHMARK: Multi-level sweep
// ============================================================
// Measures: A large buy order that sweeps through multiple
// price levels — the worst case for matching.
// ============================================================
static void BM_MultiLevelSweep(benchmark::State& state) {
    int num_levels = state.range(0);  // Parameterized!

    for (auto _ : state) {
        MatchingEngine engine;

        // Create N ask levels
        for (int i = 0; i < num_levels; i++) {
            engine.submit_order("AAPL", Side::Sell, OrderType::Limit,
                                100.00 + i * 0.01, 100);
        }

        // Sweep all levels with one big buy
        engine.submit_order("AAPL", Side::Buy, OrderType::Limit,
                            100.00 + num_levels * 0.01,
                            num_levels * 100);
    }

    state.SetItemsProcessed(state.iterations());
}
// Test with 1, 10, 100, 1000 price levels
BENCHMARK(BM_MultiLevelSweep)->Range(1, 1000);

// Run all benchmarks
BENCHMARK_MAIN();
