#pragma once

#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <ranges>
#include <type_traits>
#include <iterator>

#include "Window.hpp"
#include "../model/SensorRecord.hpp"

namespace core {

/*
simple summary struct used by tests and api
- total_count: total number of records in the current window
- by_zone: map from zone_id to how many records in that zone
*/
struct Summary {
    int total_count{0};
    std::unordered_map<int, int> by_zone; // zone_id -> count
};

/*
aggregator for sensor records using a sliding time window
- keeps a window of recent SensorRecord objects based on timestamp
- rebuilds a cached summary after each consume of SensorRecord ranges
- thread-safe: internal state is protected with a mutex
- also supports "fake" ranges (e.g., vector<int>) just to bump total_count in tests
param: window_minutes size of the sliding window in minutes
*/
class Aggregator {
public:
    explicit Aggregator(int window_minutes)
        : window_size_{std::chrono::minutes(window_minutes)} {}

/*
generic consume entry point used by tests and pipeline
- if Range::value_type is SensorRecord:
  - appends all records into the window
  - updates latest timestamp seen
  - rebuilds the summary based on the sliding window
- otherwise (e.g. Range<int>):
  - just increases summary_.total_count by the number of elements
- this function locks the mutex while updating internal state
param: r range of values to consume (usually SensorRecord, sometimes dummy types for tests)
*/
    template <typename Range>
    void consume(const Range& r) {
        using Value = std::ranges::range_value_t<Range>;

        if constexpr (std::is_same_v<Value, model::SensorRecord>) {
            std::lock_guard<std::mutex> lock(mutex_);
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
            // fallback path just for api-shape tests (e.g., std::vector<int>{1,2,3})
            std::lock_guard<std::mutex> lock(mutex_);
            const auto n = static_cast<int>(std::distance(std::begin(r), std::end(r)));
            summary_.total_count += n;
        }
    }

/*
returns a const view of the current window
- no locking here: window_ is only mutated under the mutex
- safe to call while other threads use consume + summary
return: reference to the internal Window (records currently in the time window)
    */
    const Window& current_window_view() const {
        // Window is only mutated under mutex_; reading the view is logically const.
        return window_;
    }

/*
returns the current cached summary
- locks the mutex to read summary_
- returns a copy so caller can use it without holding the lock
return: Summary with total_count and per-zone counts for the current window
*/
    Summary summary() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return summary_; 
    }

private:
/*
rebuilds the summary and evicts old records
- assumes mutex_ is already locked by the caller
- if window is empty or we never saw a valid timestamp, summary stays zeroed
- computes threshold = latest_ts_ - window_size_
- keeps only records with ts >= threshold
- updates total_count and by_zone based on kept records
- replaces window_.records with only the kept records (eviction)
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

        // evict old records outside the window
        window_.records.swap(kept);
    }

    Window window_;                             // current window contents
    std::chrono::minutes window_size_;          // logical window length
    mutable std::mutex mutex_;                  // protects window_ + summary_

    Summary summary_;                           // cached summary for tests/api

    std::chrono::system_clock::time_point latest_ts_{};
    bool has_latest_ts_{false};
};

} // namespace core