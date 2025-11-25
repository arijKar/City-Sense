#pragma once
#include "../core/Detector.hpp"
#include "../core/Window.hpp"
#include "../model/SensorRecord.hpp"

#include <chrono>

namespace detectors {

class AirAlert : public core::Detector {
    double pm25_thr_;

public:
    explicit AirAlert(double pm25_thr) : pm25_thr_(pm25_thr) {}

    void detect(const core::Window& w) override {
        if (w.records.empty()) return;

        double sum_pm25 = 0.0;
        std::size_t count_pm25 = 0;

        for (const auto& rec : w.records) {
            if (rec.pm25.has_value()) {
                sum_pm25 += *rec.pm25;
                ++count_pm25;
            }
        }

        if (count_pm25 == 0) return;

        double mean = sum_pm25 / static_cast<double>(count_pm25);
        if (mean > pm25_thr_) {
            // Real system: record an event or trigger callback.
            // For now: just perform the computation without side effects.
        }
    }
};

} // namespace detectors
