#include "metrics/LatencyHistogram.h"

#include <hdr/hdr_histogram.h>

#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace exchange::metrics {

LatencyHistogram::LatencyHistogram(int64_t lowest_value,
                                   int64_t highest_value,
                                   int     significant_figures) {
    if (hdr_init(lowest_value, highest_value, significant_figures, &h_) != 0) {
        throw std::runtime_error("LatencyHistogram: hdr_init failed");
    }
}

LatencyHistogram::LatencyHistogram(LatencyHistogram&& other) noexcept
    : h_(other.h_) {
    other.h_ = nullptr;
}

LatencyHistogram& LatencyHistogram::operator=(LatencyHistogram&& other) noexcept {
    if (this != &other) {
        if (h_) hdr_close(h_);
        h_       = other.h_;
        other.h_ = nullptr;
    }
    return *this;
}

LatencyHistogram::~LatencyHistogram() {
    if (h_) hdr_close(h_);
}

void LatencyHistogram::record(int64_t value_ns) {
    hdr_record_value(h_, value_ns);
}

void LatencyHistogram::record_corrected(int64_t value_ns, int64_t expected_interval_ns) {
    hdr_record_corrected_value(h_, value_ns, expected_interval_ns);
}

int64_t LatencyHistogram::value_at_percentile(double percentile) const {
    return hdr_value_at_percentile(h_, percentile);
}

int64_t LatencyHistogram::min()         const { return hdr_min(h_); }
int64_t LatencyHistogram::max()         const { return hdr_max(h_); }
double  LatencyHistogram::mean()        const { return hdr_mean(h_); }
double  LatencyHistogram::stddev()      const { return hdr_stddev(h_); }
int64_t LatencyHistogram::total_count() const { return h_->total_count; }

void LatencyHistogram::print_summary(std::ostream& os, const std::string& label) const {
    os << label
       << ":  count=" << total_count()
       << "  min="    << min()
       << "  mean="   << mean()
       << "  p50="    << value_at_percentile(50.0)
       << "  p90="    << value_at_percentile(90.0)
       << "  p99="    << value_at_percentile(99.0)
       << "  p99.9="  << value_at_percentile(99.9)
       << "  max="    << max()
       << "\n";
}

void LatencyHistogram::write_csv(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) throw std::runtime_error("LatencyHistogram: cannot open " + path);
    // Format: 1.0 (highest equivalent value), 5.0, 5 ticks per half-distance,
    //         output_value_unit_scaling_ratio = 1.0
    hdr_percentiles_print(h_, f, 5, 1.0, CSV);
    std::fclose(f);
}

void LatencyHistogram::reset() {
    hdr_reset(h_);
}

} // namespace exchange::metrics
