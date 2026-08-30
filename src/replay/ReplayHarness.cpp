#include "replay/ReplayHarness.h"

#include "core/MatchingEngine.h"
#include "core/Order.h"
#include "core/Types.h"
#include "risk/RiskChecker.h"

#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>

namespace exchange::replay {

namespace {

Side parse_side(const std::string& s) {
    if (s.empty()) return Side::Buy; // for cancels
    char c = s[0];
    return (c == 'S' || c == 's') ? Side::Sell : Side::Buy;
}

OrderType parse_type(const std::string& s) {
    if (s.empty()) return OrderType::Limit;
    char c = s[0];
    if (c == 'M' || c == 'm') return OrderType::Market;
    if (c == 'I' || c == 'i') return OrderType::IOC;
    return OrderType::Limit;
}

bool split_csv(const std::string& line, std::vector<std::string>& out) {
    out.clear();
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        out.push_back(field);
    }
    return !out.empty();
}

} // namespace

// ---------------- TapeReader ----------------

TapeReader::TapeReader(const std::string& csv_path) : file_(csv_path) {
    if (!file_.is_open()) {
        throw std::runtime_error("TapeReader: cannot open " + csv_path);
    }
    std::string header;
    std::getline(file_, header); // discard header
}

bool TapeReader::next(ReplayEvent& out) {
    std::string line;
    while (std::getline(file_, line)) {
        if (line.empty()) continue;
        std::vector<std::string> fields;
        if (!split_csv(line, fields) || fields.size() < 8) continue;

        try {
            out.timestamp_ns = std::stoll(fields[0]);
            std::memset(out.symbol, 0, sizeof(out.symbol));
            std::strncpy(out.symbol, fields[1].c_str(), sizeof(out.symbol) - 1);
            out.side       = parse_side(fields[2]);
            out.type       = parse_type(fields[3]);
            out.price      = fields[4].empty() ? 0.0 : std::stod(fields[4]);
            out.quantity   = fields[5].empty() ? 0 : static_cast<Quantity>(std::stoull(fields[5]));
            out.action     = fields[6].empty() ? 'N' : fields[6][0];
            out.order_id   = fields[7].empty() ? 0 : std::stoull(fields[7]);
        } catch (const std::exception&) {
            continue; // skip malformed rows
        }

        ++count_;
        return true;
    }
    return false;
}

// ---------------- ReplayHarness ----------------

ReplayHarness::ReplayHarness(MatchingEngine& engine, risk::RiskChecker* risk)
    : engine_(engine), risk_(risk), latency_() {}

ReplayResult ReplayHarness::replay(const std::string& csv_path) {
    TapeReader   reader(csv_path);
    ReplayResult result;

    // Map tape order_id -> engine-assigned order_id (for cancel events)
    std::unordered_map<OrderId, OrderId> id_map;

    // Track trades via callback so we can count them per-event
    size_t  trades_count    = 0;
    Quantity volume_running = 0;
    double   notional_running = 0.0;
    engine_.set_trade_callback([&](const Trade& t) {
        ++trades_count;
        volume_running   += t.quantity;
        notional_running += t.price * static_cast<double>(t.quantity);
        if (risk_) {
            // record both legs (buy + sell), RiskChecker tracks net by symbol
            // We don't know symbol from Trade struct directly; harness ignores
            // for simplicity in this implementation.
            (void)risk_;
        }
    });

    const auto t_start = std::chrono::steady_clock::now();

    ReplayEvent ev;
    while (reader.next(ev)) {
        ++result.events_processed;

        if (ev.action == 'N' || ev.action == 'n') {
            ++result.new_orders;
            const std::string symbol(ev.symbol);

            // Pre-trade risk check
            if (risk_) {
                auto reason = risk_->check(symbol, ev.side, ev.type, ev.price, ev.quantity);
                if (reason != risk::RejectReason::None) {
                    ++result.risk_rejections;
                    continue;
                }
            }

            // Submit + measure
            const auto t0 = std::chrono::steady_clock::now();
            OrderId assigned_id = engine_.submit_order(symbol, ev.side, ev.type,
                                                       ev.price, ev.quantity);
            const auto t1 = std::chrono::steady_clock::now();

            int64_t latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     t1 - t0).count();
            if (latency_ns > 0) latency_.record(latency_ns);

            if (ev.order_id != 0) id_map[ev.order_id] = assigned_id;
        }
        else if (ev.action == 'C' || ev.action == 'c') {
            ++result.cancels;
            const std::string symbol(ev.symbol);
            // Map tape ID to engine ID if known, else attempt direct cancel.
            OrderId target = ev.order_id;
            auto it = id_map.find(ev.order_id);
            if (it != id_map.end()) target = it->second;

            const auto t0 = std::chrono::steady_clock::now();
            engine_.cancel_order(symbol, target);
            const auto t1 = std::chrono::steady_clock::now();
            int64_t latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     t1 - t0).count();
            if (latency_ns > 0) latency_.record(latency_ns);
        }
    }

    const auto t_end = std::chrono::steady_clock::now();
    result.total_duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   t_end - t_start).count();
    if (result.total_duration_ns > 0) {
        result.throughput_ops_per_sec =
            static_cast<double>(result.events_processed) * 1e9
            / static_cast<double>(result.total_duration_ns);
    }

    result.trades_generated = trades_count;
    result.volume_traded    = volume_running;
    result.notional_traded  = notional_running;

    if (latency_.total_count() > 0) {
        result.latency_min_ns   = latency_.min();
        result.latency_p50_ns   = latency_.value_at_percentile(50.0);
        result.latency_p99_ns   = latency_.value_at_percentile(99.0);
        result.latency_p999_ns  = latency_.value_at_percentile(99.9);
        result.latency_max_ns   = latency_.max();
        result.latency_mean_ns  = latency_.mean();
    }

    return result;
}

} // namespace exchange::replay
