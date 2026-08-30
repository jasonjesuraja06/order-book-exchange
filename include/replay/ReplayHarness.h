#pragma once

#include "core/Types.h"
#include "metrics/LatencyHistogram.h"

#include <cstdint>
#include <fstream>
#include <string>

namespace exchange {
class MatchingEngine;
namespace risk { class RiskChecker; }
}

namespace exchange::replay {

// ============================================================
// REPLAY HARNESS: Deterministic regression testing via tapes
// ============================================================
// In production exchanges, every release is regression-tested
// by replaying historical "tapes" (logs of every order and
// cancel from a representative trading day) through the new
// build. Divergent trade output is a regression.
//
// This is the technique used at NYSE, CME, Citadel, Jump, JS
// (and basically every serious trading firm) to certify code
// changes before they touch real money.
//
// Tape format (CSV, header row required):
//   timestamp_ns,symbol,side,type,price,quantity,action,order_id
//
//   action: 'N' = new order, 'C' = cancel
//   side:   'B' = buy, 'S' = sell  (ignored for cancels)
//   type:   'L' = limit, 'M' = market, 'I' = IOC
//
// Example:
//   1000000000,AAPL,B,L,150.00,100,N,0
//   1000001500,AAPL,S,L,150.05,80,N,0
//   1000002200,AAPL,B,L,150.05,50,N,0
//   1000005000,AAPL,,,,,C,2
// ============================================================

struct ReplayEvent {
    int64_t   timestamp_ns;
    char      symbol[16];
    Side      side;
    OrderType type;
    Price     price;
    Quantity  quantity;
    char      action;   // 'N' or 'C'
    OrderId   order_id; // for cancels
};

class TapeReader {
public:
    explicit TapeReader(const std::string& csv_path);
    bool next(ReplayEvent& out);
    bool eof() const { return file_.eof() || !file_.good(); }
    size_t events_read() const { return count_; }

private:
    std::ifstream file_;
    size_t        count_ = 0;
};

struct ReplayResult {
    size_t  events_processed   = 0;
    size_t  new_orders         = 0;
    size_t  cancels            = 0;
    size_t  risk_rejections    = 0;
    size_t  trades_generated   = 0;
    Quantity volume_traded     = 0;
    double   notional_traded   = 0.0;
    int64_t  total_duration_ns = 0;
    double   throughput_ops_per_sec = 0.0;

    int64_t  latency_min_ns    = 0;
    int64_t  latency_p50_ns    = 0;
    int64_t  latency_p99_ns    = 0;
    int64_t  latency_p999_ns   = 0;
    int64_t  latency_max_ns    = 0;
    double   latency_mean_ns   = 0.0;
};

class ReplayHarness {
public:
    ReplayHarness(MatchingEngine& engine, risk::RiskChecker* risk = nullptr);

    // Run the entire tape as fast as possible (timestamps ignored).
    ReplayResult replay(const std::string& csv_path);

    // Access the underlying latency histogram (e.g., to write CSV).
    const metrics::LatencyHistogram& latency() const { return latency_; }
    metrics::LatencyHistogram&       latency()       { return latency_; }

private:
    MatchingEngine&             engine_;
    risk::RiskChecker*          risk_;
    metrics::LatencyHistogram   latency_;
};

} // namespace exchange::replay
