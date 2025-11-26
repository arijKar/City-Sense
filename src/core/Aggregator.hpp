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

/*
simple summary struct 
- total_count: total number of records in the current window
- by_zone: map from zone_id to how many records in that zone
*/
struct Summary {
    int total_count{0};
    std::unordered_map<int, int> by_zone; 
};

/*
aggregator for sensor records using a sliding time window
- keeps a window of recent SensorRecord objects based on timestamp
- rebuilds a cached summary after each consume of SensorRecord ranges
param: window_minutes size of the sliding window in minutes
*/
class Aggregator {
public:
    explicit Aggregator(int window_minutes)
        : window_size_{std::chrono::minutes(window_minutes)} {}

/*
generic  entry point 
- if value_type is SensorRecord:
  - appends all records into the window
  - updates latest timestamp seen
  - rebuilds the summary based on the sliding window
- if not:
  - just increases total_count by the number of elements
param: r range of values to consume
*/
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

            rebuild_summary_locked();
        } else {
            const auto n = static_cast<int>(std::distance(std::begin(r), std::end(r)));
            summary_.total_count += n;
        }
    }

/*
returns a const view of the current window
- the returned Window reflects the current in-memory state
return: reference to the internal Window 
*/
    const Window& current_window_view() const {
        return window_;
    }

/*
returns the current cached summary
- returns a copy
return: Summary with total_count and per-zone counts for the current window
*/
    Summary summary() const {
        return summary_;
    }

private:
/*
rebuilds the summary and gets rid of old records
- if window is empty or we never saw a valid timestamp, summary stays 0
- computes threshold = latest_ts_ - window_size_
- keeps only records with ts >= threshold
- updates total_count and by_zone based on kept records
- replaces window_.records with only the kept records 
*/
    void rebuild_summary_locked() {
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

        // bye bye old records outside the window
        window_.records.swap(kept);
    }

    Window window_;                            
    std::chrono::minutes window_size_;          

    Summary summary_;                           

    std::chrono::system_clock::time_point latest_ts_{};
    bool has_latest_ts_{false};
};

} // namespace core
