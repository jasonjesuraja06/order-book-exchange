// Per-operation microbenchmarks for the matching engine.
//
// Each case measures the amortized cost of one class of operation in
// isolation. Sustained mixed-workload throughput and the latency
// distribution are measured separately by benchmarks/bench_throughput.cpp;
// the per-operation costs here are not additive into that rate.

#include <benchmark/benchmark.h>
#include "core/MatchingEngine.h"

using namespace exchange;

// Limit order insertion with no match: cost of placing a resting order.
// Dominated by the std::map price-level lookup, so O(log N) in levels.
static void BM_LimitOrderInsert(benchmark::State& state) {
    MatchingEngine engine;
    double price = 100.00;
    uint64_t i = 0;

    for (auto _ : state) {
        // Alternate sides at non-crossing prices so nothing matches.
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

// Guaranteed fill: submit + match + trade generation, the critical path.
// One iteration submits two orders, so items_per_second counts both.
static void BM_LimitOrderMatch(benchmark::State& state) {
    MatchingEngine engine;

    for (auto _ : state) {
        engine.submit_order("AAPL", Side::Sell, OrderType::Limit, 100.00, 100);
        engine.submit_order("AAPL", Side::Buy, OrderType::Limit, 100.00, 100);
    }

    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_LimitOrderMatch);

// Market order against a pre-filled book. One iteration is a market buy
// plus the limit order that replenishes the level it consumed, so the
// reported per-item cost covers both.
static void BM_MarketOrderFill(benchmark::State& state) {
    MatchingEngine engine;

    for (int i = 0; i < 1000; i++) {
        engine.submit_order("AAPL", Side::Sell, OrderType::Limit,
                            100.00 + i * 0.01, 100);
    }

    for (auto _ : state) {
        engine.submit_order("AAPL", Side::Buy, OrderType::Market, 0.0, 100);

        // Replenish the level just consumed.
        engine.submit_order("AAPL", Side::Sell, OrderType::Limit,
                            100.00, 100);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MarketOrderFill);

// Cancel by ID through the order-location map. Timing is paused while the
// book is refilled, so only the cancels are measured.
static void BM_CancelOrder(benchmark::State& state) {
    MatchingEngine engine;

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

// One aggressive order sweeping N resting price levels: the worst case for
// the matching loop, which walks one level per iteration.
//
// Engine construction and book setup are excluded from the timed region.
// Leaving them in would have dominated the result: each MatchingEngine
// preallocates its ObjectPool, which costs far more than the sweep itself.
static void BM_MultiLevelSweep(benchmark::State& state) {
    const int num_levels = static_cast<int>(state.range(0));

    for (auto _ : state) {
        state.PauseTiming();
        MatchingEngine engine;
        for (int i = 0; i < num_levels; i++) {
            engine.submit_order("AAPL", Side::Sell, OrderType::Limit,
                                100.00 + i * 0.01, 100);
        }
        state.ResumeTiming();

        // Sweep every resting level with a single marketable buy.
        engine.submit_order("AAPL", Side::Buy, OrderType::Limit,
                            100.00 + num_levels * 0.01,
                            static_cast<Quantity>(num_levels) * 100);
    }

    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MultiLevelSweep)->RangeMultiplier(10)->Range(1, 1000);

BENCHMARK_MAIN();
