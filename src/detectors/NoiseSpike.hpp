#pragma once
#include "../core/Detector.hpp"
#include "../core/Window.hpp"
#include "../model/SensorRecord.hpp"

#include <vector>
#include <algorithm>
#include <chrono>

namespace detectors {

class NoiseSpike : public core::Detector {
    double db_thr_;
    int count_thr_;
    int window_min_;

public:
    NoiseSpike(double db_thr, int count_thr, int window_min)
      : db_thr_(db_thr), count_thr_(count_thr), window_min_(window_min) {}

    void detect(const core::Window& w) override {
        using namespace std::chrono;

        if (w.records.empty() || count_thr_ <= 0 || window_min_ <= 0) return;

        // Collect timestamps for records exceeding db threshold
        std::vector<system_clock::time_point> times;
        times.reserve(w.records.size());
        for (const auto& rec : w.records) {
            if (rec.db.has_value() && *rec.db >= db_thr_) {
                times.push_back(rec.ts);
            }
        }
        if (times.empty()) return;

        std::sort(times.begin(), times.end());

        // Sliding window over times (two-pointer technique)
        const auto window_duration = minutes(window_min_);
        std::size_t left = 0;
        for (std::size_t right = 0; right < times.size(); ++right) {
            while (left <= right &&
                   times[right] - times[left] > window_duration) {
                ++left;
            }
            std::size_t count = right - left + 1;
            if (static_cast<int>(count) >= count_thr_) {
                // Spike detected; again, we could register an event; no side effects here.
                break;
            }
        }
    }
};

} // namespace detectors
