#pragma once
#include "../core/Detector.hpp"
#include "../core/Window.hpp"
#include "../model/SensorRecord.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace detectors {

struct SpeedStats {
    double mean{0.0};
    double median{0.0};
    double p90{0.0};
    double p99{0.0};
};

inline SpeedStats compute_speed_stats(std::vector<double> values) {
    SpeedStats s{};
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

class TrafficCongestion : public core::Detector {
    double speed_thr_kmh_;
    int    consec_min_;
    std::vector<std::string>* alerts_;

public:
    TrafficCongestion(double speed_thr_kmh,
                      int consec_min,
                      std::vector<std::string>* alerts = nullptr)
        : speed_thr_kmh_(speed_thr_kmh),
          consec_min_(consec_min),
          alerts_(alerts) {}

    void detect(const core::Window& w) override {
        using namespace std::chrono;

        if (w.records.empty() || consec_min_ <= 0) return;

        std::unordered_map<long long, std::pair<double, std::size_t>> buckets;

        for (const auto& rec : w.records) {
            if (!rec.speed.has_value()) continue;
            auto mins_since_epoch =
                duration_cast<minutes>(rec.ts.time_since_epoch()).count();

            auto& bucket = buckets[mins_since_epoch];
            bucket.first  += *rec.speed; // assume km/h
            bucket.second += 1;
        }

        if (buckets.empty()) return;

        std::vector<long long> minutes;
        minutes.reserve(buckets.size());
        for (const auto& [m, _] : buckets) {
            minutes.push_back(m);
        }
        std::sort(minutes.begin(), minutes.end());

        std::vector<double> speeds_ms;
        speeds_ms.reserve(minutes.size());

        int  current_run = 0;
        bool congestion  = false;

        for (std::size_t i = 0; i < minutes.size(); ++i) {
            long long m = minutes[i];
            const auto& [sum_speed_kmh, count] = buckets[m];
            if (count == 0) continue;

            double avg_kmh = sum_speed_kmh / static_cast<double>(count);
            double avg_ms  = avg_kmh / 3.6;
            speeds_ms.push_back(avg_ms);

            bool below = avg_kmh < speed_thr_kmh_;

            if (i > 0 && minutes[i] == minutes[i - 1] + 1) {
                if (below) {
                    ++current_run;
                } else {
                    current_run = 0;
                }
            } else {
                current_run = below ? 1 : 0;
            }

            if (current_run >= consec_min_) {
                congestion = true;
            }
        }

        SpeedStats stats = compute_speed_stats(speeds_ms);

        if (congestion || stats.mean * 3.6 < speed_thr_kmh_) {
            if (alerts_) {
                std::ostringstream oss;
                oss << "[TrafficCongestion] Possible congestion. "
                    << "avg_speed=" << stats.mean * 3.6 << " km/h"
                    << ", median="  << stats.median * 3.6 << " km/h"
                    << ", p90="     << stats.p90 * 3.6 << " km/h"
                    << ", p99="     << stats.p99 * 3.6 << " km/h"
                    << " (threshold=" << speed_thr_kmh_
                    << " km/h, consec_min=" << consec_min_ << ")";
                alerts_->push_back(oss.str());
            }
        }
    }
};

} // namespace detectors
