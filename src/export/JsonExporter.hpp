#pragma once
#include <fstream>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>
#include <algorithm>

#include "../core/Aggregator.hpp"

namespace export_ {

// Minimal JSON-like output without third-party libs.
class JsonExporter {
    std::string path_;

public:
    explicit JsonExporter(std::string path)
      : path_(std::move(path)) {}

    void emit(const core::Summary& s) const {
        std::ofstream out(path_, std::ios::trunc);
        if (!out) {
            throw std::runtime_error("JsonExporter: cannot open file '" + path_ + "'");
        }

        // Write a small JSON object:
        // {
        //   "total_count": 42,
        //   "by_zone": [
        //     { "zone": 1, "count": 10 },
        //     { "zone": 2, "count": 32 }
        //   ]
        // }
        out << "{\n";
        out << "  \"total_count\": " << s.total_count;

        out << ",\n  \"by_zone\": [";

        if (!s.by_zone.empty()) {
            std::vector<std::pair<int,int>> zones;
            zones.reserve(s.by_zone.size());
            for (const auto& [zone, count] : s.by_zone) {
                zones.emplace_back(zone, count);
            }
            std::sort(zones.begin(), zones.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            bool first = true;
            for (const auto& [zone, count] : zones) {
                if (!first) {
                    out << ",";
                }
                out << "\n    { \"zone\": " << zone << ", \"count\": " << count << " }";
                first = false;
            }
            if (!zones.empty()) {
                out << "\n  ";
            }
        }

        out << "]\n}\n";

        if (!out) {
            throw std::runtime_error("JsonExporter: write failed for '" + path_ + "'");
        }
    }
};

} // namespace export_
