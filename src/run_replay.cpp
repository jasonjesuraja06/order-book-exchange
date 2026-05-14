// ============================================================
// run_replay — Deterministic regression testing via tape replay
// ============================================================
// Reads a CSV tape of historical orders + cancels, replays them
// through the matching engine with pre-trade risk checks,
// reports trade output + latency percentiles (p50/p99/p99.9).
//
// Usage:
//   ./run_replay <path/to/tape.csv>
//   ./run_replay data/sample_tape.csv
// ============================================================

#include "core/MatchingEngine.h"
#include "metrics/LatencyHistogram.h"
#include "replay/ReplayHarness.h"
#include "risk/RiskChecker.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string tape_path = (argc > 1) ? argv[1] : "data/sample_tape.csv";

    exchange::MatchingEngine     engine;
    exchange::risk::RiskLimits   limits;        // sensible defaults
    exchange::risk::RiskChecker  risk(limits);
    exchange::replay::ReplayHarness harness(engine, &risk);

    std::cout << "============================================================\n"
              << " Replaying tape: " << tape_path << "\n"
              << "============================================================\n";

    auto result = harness.replay(tape_path);

    std::cout << "\n--- Replay Results ---\n"
              << "  Events processed: " << result.events_processed   << "\n"
              << "  New orders:       " << result.new_orders         << "\n"
              << "  Cancels:          " << result.cancels            << "\n"
              << "  Risk rejections:  " << result.risk_rejections    << "\n"
              << "  Trades generated: " << result.trades_generated   << "\n"
              << "  Volume traded:    " << result.volume_traded      << " shares\n"
              << "  Notional traded:  $" << result.notional_traded   << "\n"
              << "  Duration:         " << (result.total_duration_ns / 1000) << " µs\n"
              << "  Throughput:       " << static_cast<int64_t>(result.throughput_ops_per_sec)
                                         << " ops/sec\n";

    std::cout << "\n--- Latency Distribution (nanoseconds) ---\n";
    harness.latency().print_summary(std::cout, "  Per-op latency");

    std::cout << "\n--- Risk Stats ---\n"
              << "  Total checks:     " << risk.stats().total_checks     << "\n"
              << "  Passes:           " << risk.stats().total_passes     << "\n"
              << "  Rejections:       " << risk.stats().total_rejections << "\n";

    return 0;
}
