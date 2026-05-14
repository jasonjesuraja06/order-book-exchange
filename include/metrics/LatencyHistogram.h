#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

struct hdr_histogram;  // forward decl from HdrHistogram_c

namespace exchange::metrics {

// ============================================================
// LATENCY HISTOGRAM (HdrHistogram wrapper)
// ============================================================
// HdrHistogram (High Dynamic Range Histogram) is the standard
// data structure for recording latency distributions in
// high-performance systems. It maintains constant time and
// memory per record across a configurable dynamic range and
// resolution.
//
// Used in production by: LMAX Disruptor, Cassandra, Aeron,
// Kafka, Elasticsearch, and most major HFT firms.
//
// Why not std::vector + sort? Because real systems record
// billions of samples; sorting is O(N log N) and the vector
// can be GBs. HdrHistogram is O(1) per record and a few KB
// total.
//
// Resolution: significant_figures=3 means percentiles are
// accurate to 0.1% (the bucket your sample falls into is
// within 0.1% of its true value).
// ============================================================
class LatencyHistogram {
public:
    // Default range: 1 ns to 60 s; 3 significant figures (0.1% resolution).
    explicit LatencyHistogram(int64_t lowest_value       = 1,
                              int64_t highest_value      = 60'000'000'000LL,
                              int     significant_figures = 3);

    LatencyHistogram(const LatencyHistogram&)            = delete;
    LatencyHistogram& operator=(const LatencyHistogram&) = delete;
    LatencyHistogram(LatencyHistogram&&) noexcept;
    LatencyHistogram& operator=(LatencyHistogram&&) noexcept;
    ~LatencyHistogram();

    // Record a single value (e.g., nanoseconds).
    void record(int64_t value_ns);

    // Coordinated-omission correction: if you observed value_ns but the
    // expected gap between samples was expected_interval_ns, this back-fills
    // missed samples to account for measurement skew.
    void record_corrected(int64_t value_ns, int64_t expected_interval_ns);

    // Percentile queries (e.g., value_at_percentile(99.0) = p99)
    int64_t value_at_percentile(double percentile) const;

    int64_t min() const;
    int64_t max() const;
    double  mean() const;
    double  stddev() const;
    int64_t total_count() const;

    // Write a summary to a stream.
    //   Latency (ns):  count=1000000 min=41 mean=262.3 p50=251 p99=620 p99.9=2400 max=15000
    void print_summary(std::ostream& os, const std::string& label = "Latency (ns)") const;

    // Write the full distribution as CSV (value,percentile,total_count,...).
    void write_csv(const std::string& path) const;

    void reset();

private:
    hdr_histogram* h_ = nullptr;
};

} // namespace exchange::metrics
