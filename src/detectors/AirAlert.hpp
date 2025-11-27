#pragma once
#include "../core/Detector.hpp"
#include "../core/Window.hpp"
#include "../model/SensorRecord.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

namespace detectors {

/*
basic stats for pm25
- mean: average
- median: middle value
- p90/p99: high percentiles to see spikes
*/
struct BasicStats {
    double mean{0.0};
    double median{0.0};
    double p90{0.0};
    double p99{0.0};
};

/*
compute stats for a list of values
- makes a copy, sorts it and fills BasicStats
*/
inline BasicStats compute_basic_stats(std::vector<double> values) {
    BasicStats s{};
    if (values.empty()) return s;

    std::sort(values.begin(), values.end());
    std::size_t n = values.size();

    double sum = 0.0;
    for (double v : values) sum += v;
    s.mean = sum / static_cast<double>(n);

    auto get_percentile = [&](double p) {
        if (n == 1) return values[0];
        double idx = p * (n - 1);
        std::size_t i = static_cast<std::size_t>(idx);
        std::size_t j = std::min(i + 1, n - 1);
        double frac = idx - static_cast<double>(i);
        return values[i] + (values[j] - values[i]) * frac;
    };

    s.median = get_percentile(0.5);
    s.p90    = get_percentile(0.9);
    s.p99    = get_percentile(0.99);

    return s;
}

/*
air quality detector based on pm25
- reads pm25 from the window
- computes stats
- if mean or p90 > pm25_thr_ it pushes a text alert
*/
class AirAlert : public core::Detector {
    double pm25_thr_;
    std::vector<std::string>* alerts_; // optional sink for messages

public:
    explicit AirAlert(double pm25_thr,
                      std::vector<std::string>* alerts = nullptr)
        : pm25_thr_(pm25_thr), alerts_(alerts) {}

    void detect(const core::Window& w) override {
        if (w.records.empty()) return;

        std::vector<double> pm_values;
        pm_values.reserve(w.records.size());

        for (const auto& rec : w.records) {
            if (rec.pm25.has_value()) {
                pm_values.push_back(*rec.pm25);
            }
        }
        if (pm_values.empty()) return;

        BasicStats stats = compute_basic_stats(pm_values);

        if (stats.mean > pm25_thr_ || stats.p90 > pm25_thr_) {
            if (alerts_) {
                std::ostringstream oss;
                oss << "[AirAlert] High PM2.5 detected. "
                    << "mean=" << stats.mean
                    << ", median=" << stats.median
                    << ", p90=" << stats.p90
                    << ", p99=" << stats.p99
                    << " (threshold=" << pm25_thr_ << ")";
                alerts_->push_back(oss.str());
            }
        }
    }
};

} // namespace detectors
