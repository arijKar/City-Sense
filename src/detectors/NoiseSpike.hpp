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
basic stats for noise (dB)
- mean / median: typical level
- p90 / p99: loud tail of the distribution
*/
struct NoiseStats {
    double mean{0.0};
    double median{0.0};
    double p90{0.0};
    double p99{0.0};
};

/*
compute stats for a list of dB values
- sorts values and fills NoiseStats
*/
inline NoiseStats compute_noise_stats(std::vector<double> values) {
    NoiseStats s{};
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
noise spike detector
- db_thr_: how loud is “loud”
- count_thr_: how many loud readings we need
- window_min_: time window in minutes for those loud readings
- if we see many loud points close in time, or p99 >= db_thr_,
  we push a simple alert string
*/
class NoiseSpike : public core::Detector {
    double db_thr_;
    int    count_thr_;
    int    window_min_;
    std::vector<std::string>* alerts_;

public:
    NoiseSpike(double db_thr,
               int count_thr,
               int window_min,
               std::vector<std::string>* alerts = nullptr)
        : db_thr_(db_thr),
          count_thr_(count_thr),
          window_min_(window_min),
          alerts_(alerts) {}

    void detect(const core::Window& w) override {
        using namespace std::chrono;

        if (w.records.empty() || count_thr_ <= 0 || window_min_ <= 0) return;

        std::vector<double> all_db;
        all_db.reserve(w.records.size());

        std::vector<system_clock::time_point> times;
        times.reserve(w.records.size());

        for (const auto& rec : w.records) {
            if (rec.db.has_value()) {
                double v = *rec.db;
                all_db.push_back(v);
                if (v >= db_thr_) {
                    times.push_back(rec.ts);
                }
            }
        }

        if (all_db.empty()) return;

        NoiseStats stats = compute_noise_stats(all_db);

        bool spike_detected = false;
        if (!times.empty()) {
            std::sort(times.begin(), times.end());
            const auto window_duration = minutes(window_min_);

            std::size_t left = 0;
            for (std::size_t right = 0; right < times.size(); ++right) {
                while (left <= right &&
                       times[right] - times[left] > window_duration) {
                    ++left;
                }
                std::size_t count = right - left + 1;
                if (static_cast<int>(count) >= count_thr_) {
                    spike_detected = true;
                    break;
                }
            }
        }

        if (spike_detected || stats.p99 >= db_thr_) {
            if (alerts_) {
                std::ostringstream oss;
                oss << "[NoiseSpike] Loud noise detected. "
                    << "mean=" << stats.mean
                    << ", median=" << stats.median
                    << ", p90=" << stats.p90
                    << ", p99=" << stats.p99
                    << " (spike_thr=" << db_thr_ << " dB)";
                alerts_->push_back(oss.str());
            }
        }
    }
};

} // namespace detectors
