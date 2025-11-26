#pragma once
#include <iostream>
#include <utility>
#include <vector>
#include <algorithm>

#include "../core/Aggregator.hpp"

namespace export_ {

/*
Detect Events
• Implement detectors for traffic congestion, air alerts, and noise spikes.
• Each detector reads the processed data and decides if something unusual is happening.
• Detectors must return findings (e.g., std::vector<Finding>) instead of printing.
*/

// ConsolePrinter: small helper to show a readable report on stdout.
// It prints:
//   - a summary of how many records we have per zone
//   - a list of detected events (one per line)
struct ConsolePrinter {
    // Old-style entry point: only summary, no events.
    void emit(const core::Summary& s) const {
        static const std::vector<std::string> no_events;
        emit(s, no_events);
    }

    // New version: summary + event messages (e.g., from detectors).
    void emit(const core::Summary& s,
              const std::vector<std::string>& events) const {
        std::cout << "        CitySense Report      \n";

        // ----- Summary -----
        std::cout << "Summary\n";
        std::cout << "-------\n";
        std::cout << "Total records in window: " << s.total_count << "\n";

        if (s.by_zone.empty()) {
            std::cout << "Per-zone counts: (none)\n\n";
        } else {
            std::cout << "Per-zone counts:\n";

            std::vector<std::pair<int,int>> zones;
            zones.reserve(s.by_zone.size());
            for (const auto& [zone, count] : s.by_zone) {
                zones.emplace_back(zone, count);
            }

            std::sort(zones.begin(), zones.end(),
                      [](const auto& a, const auto& b) {
                          return a.first < b.first;
                      });

            for (const auto& [zone, count] : zones) {
                std::cout << "  - zone " << zone << ": " << count << "\n";
            }
            std::cout << "\n";
        }

        // ----- Events -----
        std::cout << "Detected Events\n";
        std::cout << "---------------\n";
        if (events.empty()) {
            std::cout << "No unusual events detected in this window.\n";
        } else {
            for (const auto& msg : events) {
                std::cout << "  - " << msg << "\n";
            }
        }
        std::cout << "\n";
    }
};

} // namespace export_
