#pragma once

#include <unordered_map>
#include <vector>
#include <chrono>
#include <ranges>
#include <type_traits>
#include <iterator>

#include "Window.hpp"
#include "../model/SensorRecord.hpp"

namespace core {

// Small summary struct used by the rest of the code.
// - total_count: how many records are in the window
// - by_zone: count per zone_id
struct Summary {
    int total_count{0};
    std::unordered_map<int, int> by_zone;
};

// Aggregates sensor records inside a sliding time window.
// The window is defined in minutes. We keep all records whose
// timestamp is within [latest_ts - window_size, latest_ts].
class Aggregator {
public:
    explicit Aggregator(int window_minutes)
        : window_size_{std::chrono::minutes(window_minutes)} {}

    // Generic entry point to ingest a range.
    //
    // If the range holds SensorRecord, we:
    //   - push all records into the window
    //   - update the latest timestamp we have seen
    //   - rebuild the summary (drop old records and recompute counts)
    //
    // For any other type, we just bump total_count by the number of elements.
    template <typename Range>
    void consume(const Range& r) {
        using Value = std::ranges::range_value_t<Range>;

        if constexpr (std::is_same_v<Value, model::SensorRecord>) {
            if (std::ranges::empty(r)) {
                return;
            }

            for (const auto& rec : r) {
                window_.records.push_back(rec);

                if (!has_latest_ts_ || rec.ts > latest_ts_) {
                    latest_ts_ = rec.ts;
                    has_latest_ts_ = true;
                }
            }

            rebuild_summary();
        } else {
            const auto n =
                static_cast<int>(std::distance(std::begin(r), std::end(r)));
            summary_.total_count += n;
        }
    }

    // Read-only access to the current window.
    const Window& current_window_view() const {
        return window_;
    }

    // Returns a copy of the cached summary (total_count + counts per zone).
    Summary summary() const {
        return summary_;
    }

private:
    // Rebuilds the summary and keeps only the records that fall inside
    // the current time window.
    //
    // If we never saw a valid timestamp or there are no records, we just
    // leave the summary at 0.
    void rebuild_summary() {
        summary_.total_count = 0;
        summary_.by_zone.clear();

        if (window_.records.empty() || !has_latest_ts_) {
            return;
        }

        const auto threshold = latest_ts_ - window_size_;

        std::vector<model::SensorRecord> kept;
        kept.reserve(window_.records.size());

        for (const auto& rec : window_.records) {
            if (rec.ts >= threshold) {
                kept.push_back(rec);
                ++summary_.total_count;
                ++summary_.by_zone[rec.zone_id];
            }
        }

        // Drop old records that are outside the window.
        window_.records.swap(kept);
    }

    Window window_;                             // actual records we keep
    std::chrono::minutes window_size_;          // size of the time window

    Summary summary_;                           // cached summary for the window

    std::chrono::system_clock::time_point latest_ts_{};
    bool has_latest_ts_{false};
};

} // namespace core
