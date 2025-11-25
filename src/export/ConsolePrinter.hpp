#pragma once
#include <iostream>
#include <utility>
#include <vector>
#include <algorithm>

#include "../core/Aggregator.hpp"
namespace export_ {

struct ConsolePrinter {
    void emit(const core::Summary& s) const {
        std::cout << "[summary] total_count=" << s.total_count << "\n";

        if (s.by_zone.empty()) {
            std::cout << "  (no per-zone data)\n";
            return;
        }

        std::vector<std::pair<int,int>> zones;
        zones.reserve(s.by_zone.size());
        for (const auto& [zone, count] : s.by_zone) {
            zones.emplace_back(zone, count);
        }
        std::sort(zones.begin(), zones.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::cout << "  by_zone:\n";
        for (const auto& [zone, count] : zones) {
            std::cout << "    zone " << zone << " -> " << count << "\n";
        }
    }
};

} // namespace export_
