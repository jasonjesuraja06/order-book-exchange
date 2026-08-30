// Sustained-throughput and latency-distribution harness.
//
// Two phases run over the same synthetic workload:
//
//   Phase 1 (throughput) times the whole run with one wall clock and
//   divides the operation count by the elapsed seconds. No per-operation
//   instrumentation is active, so the result is the rate the engine
//   actually sustains rather than a reciprocal of a mean latency.
//
//   Phase 2 (latency) re-runs the workload with a timestamp around every
//   operation and feeds the samples to an HdrHistogram. Timestamping costs
//   two clock reads per operation, so this phase reports the distribution
//   and phase 1 reports the rate. The two are deliberately not derived
//   from each other.
//
// The workload keeps a live book: passive limit orders rest on both sides
// of a fixed mid, a fraction of operations cancel a resting order, and a
// fraction cross the spread to force matches. Resting depth is capped at
// kDepth by converting a passive add into a cancel once the book is full,
// so the engine runs against a realistically deep book (roughly 500 orders
// per price level) instead of one that grows without bound for the whole
// run. The RNG is seeded and the sequence is precomputed, so both phases
// execute an identical operation stream and runs are reproducible.

#include "core/MatchingEngine.h"
#include "metrics/LatencyHistogram.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace exchange;

namespace {

constexpr const char* kSymbol = "AAPL";
constexpr double      kMid    = 100.00;
constexpr double      kTick   = 0.01;
constexpr int         kBand   = 50;      // price levels quoted per side
constexpr size_t      kDepth  = 50'000;  // resting-order cap, ~500 per level

// One unit of work: what the driver should do next.
enum class Op : uint8_t { PassiveBuy, PassiveSell, Cancel, Aggress };

// Build the operation sequence once so both phases execute identical work.
std::vector<Op> build_workload(uint64_t n_ops, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> pick(0, 99);

    std::vector<Op> ops;
    ops.reserve(n_ops);
    for (uint64_t i = 0; i < n_ops; ++i) {
        int r = pick(rng);
        if (r < 35)      ops.push_back(Op::PassiveBuy);
        else if (r < 70) ops.push_back(Op::PassiveSell);
        else if (r < 90) ops.push_back(Op::Cancel);
        else             ops.push_back(Op::Aggress);
    }
    return ops;
}

// Price offsets, also precomputed, so neither phase pays for RNG while timed.
std::vector<int> build_offsets(uint64_t n_ops, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> level(1, kBand);

    std::vector<int> offsets;
    offsets.reserve(n_ops);
    for (uint64_t i = 0; i < n_ops; ++i) offsets.push_back(level(rng));
    return offsets;
}

struct RunResult {
    uint64_t ops_executed = 0;
    double   elapsed_sec  = 0.0;
    EngineStats stats;
};

// Phase 1: no per-operation timing.
RunResult run_throughput(const std::vector<Op>& ops, const std::vector<int>& offsets) {
    MatchingEngine engine;
    std::deque<OrderId> resting;

    auto t0 = std::chrono::steady_clock::now();

    for (size_t i = 0; i < ops.size(); ++i) {
        const int off = offsets[i];
        switch (ops[i]) {
            case Op::PassiveBuy:
            case Op::PassiveSell: {
                if (resting.size() >= kDepth) {
                    // Book is at target depth: retire the oldest order instead.
                    engine.cancel_order(kSymbol, resting.front());
                    resting.pop_front();
                    break;
                }
                const bool buy = (ops[i] == Op::PassiveBuy);
                OrderId id = engine.submit_order(
                    kSymbol, buy ? Side::Buy : Side::Sell, OrderType::Limit,
                    buy ? kMid - off * kTick : kMid + off * kTick, 100);
                if (id) resting.push_back(id);
                break;
            }
            case Op::Cancel: {
                if (!resting.empty()) {
                    engine.cancel_order(kSymbol, resting.front());
                    resting.pop_front();
                }
                break;
            }
            case Op::Aggress: {
                // Cross the spread with an IOC so the remainder never rests.
                Side s = (off % 2 == 0) ? Side::Buy : Side::Sell;
                Price p = (s == Side::Buy) ? kMid + kBand * kTick
                                           : kMid - kBand * kTick;
                engine.submit_order(kSymbol, s, OrderType::IOC, p, 300);
                break;
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();

    RunResult r;
    r.ops_executed = ops.size();
    r.elapsed_sec  = std::chrono::duration<double>(t1 - t0).count();
    r.stats        = engine.stats();
    return r;
}

// Phase 2: timestamp each operation into the histogram.
RunResult run_latency(const std::vector<Op>& ops, const std::vector<int>& offsets,
                      metrics::LatencyHistogram& hist) {
    MatchingEngine engine;
    std::deque<OrderId> resting;

    auto t0 = std::chrono::steady_clock::now();

    for (size_t i = 0; i < ops.size(); ++i) {
        const int off = offsets[i];
        auto op_start = std::chrono::steady_clock::now();

        switch (ops[i]) {
            case Op::PassiveBuy:
            case Op::PassiveSell: {
                if (resting.size() >= kDepth) {
                    // Book is at target depth: retire the oldest order instead.
                    engine.cancel_order(kSymbol, resting.front());
                    resting.pop_front();
                    break;
                }
                const bool buy = (ops[i] == Op::PassiveBuy);
                OrderId id = engine.submit_order(
                    kSymbol, buy ? Side::Buy : Side::Sell, OrderType::Limit,
                    buy ? kMid - off * kTick : kMid + off * kTick, 100);
                if (id) resting.push_back(id);
                break;
            }
            case Op::Cancel: {
                if (!resting.empty()) {
                    engine.cancel_order(kSymbol, resting.front());
                    resting.pop_front();
                }
                break;
            }
            case Op::Aggress: {
                Side s = (off % 2 == 0) ? Side::Buy : Side::Sell;
                Price p = (s == Side::Buy) ? kMid + kBand * kTick
                                           : kMid - kBand * kTick;
                engine.submit_order(kSymbol, s, OrderType::IOC, p, 300);
                break;
            }
        }

        auto op_end = std::chrono::steady_clock::now();
        hist.record(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        op_end - op_start).count());
    }

    auto t1 = std::chrono::steady_clock::now();

    RunResult r;
    r.ops_executed = ops.size();
    r.elapsed_sec  = std::chrono::duration<double>(t1 - t0).count();
    r.stats        = engine.stats();
    return r;
}

} // namespace

int main(int argc, char* argv[]) {
    uint64_t n_ops   = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 5'000'000ULL;
    std::string csv  = (argc > 2) ? argv[2] : "";

    const std::vector<Op>  ops     = build_workload(n_ops, 12345u);
    const std::vector<int> offsets = build_offsets(n_ops, 6789u);

    std::printf("workload            : %llu operations "
                "(35%% passive buy, 35%% passive sell, 20%% cancel, 10%% IOC cross)\n",
                static_cast<unsigned long long>(n_ops));

    // ---- Phase 1: sustained throughput, wall clock over the whole run ----
    RunResult tp = run_throughput(ops, offsets);
    double ops_per_sec = tp.ops_executed / tp.elapsed_sec;

    std::printf("\n[phase 1] sustained throughput (no per-op instrumentation)\n");
    std::printf("  wall clock        : %.3f s\n", tp.elapsed_sec);
    std::printf("  operations        : %llu\n",
                static_cast<unsigned long long>(tp.ops_executed));
    std::printf("  throughput        : %.0f ops/sec (%.2fM ops/sec)\n",
                ops_per_sec, ops_per_sec / 1e6);
    std::printf("  orders submitted  : %llu\n",
                static_cast<unsigned long long>(tp.stats.total_orders));
    std::printf("  cancels           : %llu\n",
                static_cast<unsigned long long>(tp.stats.total_cancels));
    std::printf("  trades            : %llu\n",
                static_cast<unsigned long long>(tp.stats.total_trades));
    std::printf("  shares traded     : %llu\n",
                static_cast<unsigned long long>(tp.stats.total_volume));

    // ---- Phase 2: latency distribution, per-op timestamps ----
    metrics::LatencyHistogram hist;
    RunResult lat = run_latency(ops, offsets, hist);

    std::printf("\n[phase 2] latency distribution (two clock reads per op)\n");
    std::printf("  samples           : %lld\n",
                static_cast<long long>(hist.total_count()));
    std::printf("  min               : %lld ns\n", static_cast<long long>(hist.min()));
    std::printf("  p50               : %lld ns\n",
                static_cast<long long>(hist.value_at_percentile(50.0)));
    std::printf("  p90               : %lld ns\n",
                static_cast<long long>(hist.value_at_percentile(90.0)));
    std::printf("  p99               : %lld ns\n",
                static_cast<long long>(hist.value_at_percentile(99.0)));
    std::printf("  p99.9             : %lld ns\n",
                static_cast<long long>(hist.value_at_percentile(99.9)));
    std::printf("  max               : %lld ns\n", static_cast<long long>(hist.max()));
    std::printf("  mean              : %.1f ns\n", hist.mean());
    std::printf("  instrumented rate : %.2fM ops/sec "
                "(lower than phase 1 by the cost of timestamping)\n",
                (lat.ops_executed / lat.elapsed_sec) / 1e6);

    if (!csv.empty()) {
        hist.write_csv(csv);
        std::printf("\nfull distribution written to %s\n", csv.c_str());
    }

    return 0;
}
