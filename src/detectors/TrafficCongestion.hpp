#pragma once
#include "../core/Detector.hpp"
#include "../core/Window.hpp"
#include "../model/SensorRecord.hpp"

#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>
#include <numeric>

namespace detectors {

class TrafficCongestion : public core::Detector {
    double speed_thr_;
    int consec_min_;

public:
    TrafficCongestion(double speed_thr, int consec_min)
      : speed_thr_(speed_thr), consec_min_(consec_min) {}

    void detect(const core::Window& w) override {
        using namespace std::chrono;

        if (w.records.empty() || consec_min_ <= 0) return;

        // Group records by minute since epoch and compute avg speed per minute
        std::unordered_map<long long, std::pair<double, std::size_t>> buckets;
        buckets.reserve(w.records.size());

        for (const auto& rec : w.records) {
            if (!rec.speed.has_value()) continue;
            auto mins = duration_cast<minutes>(rec.ts.time_since_epoch()).count();
            auto& [sum, cnt] = buckets[mins];
            sum += *rec.speed;
            ++cnt;
        }

        if (buckets.empty()) return;

        // Build sorted list of minutes
        std::vector<long long> minutes;
        minutes.reserve(buckets.size());
        for (const auto& [m, _] : buckets) {
            minutes.push_back(m);
        }
        std::sort(minutes.begin(), minutes.end());

        // Scan for consecutive mins where avg speed < threshold
        int current_run = 0;
        for (std::size_t i = 0; i < minutes.size(); ++i) {
            auto m = minutes[i];
            const auto& [sum, cnt] = buckets[m];
            if (cnt == 0) continue;
            double avg = sum / static_cast<double>(cnt);

            bool below = avg < speed_thr_;

            // Check continuity: if not the first, enforce m == prev + 1
            if (i > 0 && minutes[i] == minutes[i - 1] + 1) {
                if (below) {
                    ++current_run;
                } else {
                    current_run = 0;
                }
            } else {
                // gap -> restart run
                current_run = below ? 1 : 0;
            }

            if (current_run >= consec_min_) {
                // In a real system we’d record an event; for now, just “detected”.
                // (No-op to keep contract as void.)
                break;
            }
        }
    }
};

} // namespace detectors
