#pragma once
#include <fstream>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>
#include <algorithm>

#include "../core/Aggregator.hpp"

namespace export_ {

// Simple JSON exporter for the final summary + alerts.
class JsonExporter {
    std::string path_;

public:
    explicit JsonExporter(std::string path)
        : path_(std::move(path)) {}

    // Old signature still works (no alerts -> empty list).
    void emit(const core::Summary& s) const {
        static const std::vector<std::string> empty_alerts;
        emit(s, empty_alerts);
    }

    // New version: summary + list of alert strings.
    void emit(const core::Summary& s,
              const std::vector<std::string>& alerts) const {
        std::ofstream out(path_, std::ios::trunc);
        if (!out) {
            throw std::runtime_error("JsonExporter: cannot open file '" + path_ + "'");
        }

        out << "{\n";
        out << "  \"total_count\": " << s.total_count;

        // --- by_zone ---
        out << ",\n  \"by_zone\": [";

        if (!s.by_zone.empty()) {
            std::vector<std::pair<int,int>> zones;
            zones.reserve(s.by_zone.size());
            for (const auto& [zone, count] : s.by_zone) {
                zones.emplace_back(zone, count);
            }
            std::sort(zones.begin(), zones.end(),
                      [](const auto& a, const auto& b) {
                          return a.first < b.first;
                      });

            bool first = true;
            for (const auto& [zone, count] : zones) {
                if (!first) {
                    out << ",";
                }
                out << "\n    { \"zone\": " << zone
                    << ", \"count\": " << count << " }";
                first = false;
            }
            if (!zones.empty()) {
                out << "\n  ";
            }
        }

        out << "],\n";

        // --- alerts ---
        out << "  \"alerts\": [";
        if (!alerts.empty()) {
            bool first = true;
            for (const auto& msg : alerts) {
                if (!first) {
                    out << ",";
                }
                // We assume no double-quotes in msg; otherwise we'd need escaping.
                out << "\n    \"" << msg << "\"";
                first = false;
            }
            out << "\n  ";
        }
        out << "]\n}\n";

        if (!out) {
            throw std::runtime_error("JsonExporter: write failed for '" + path_ + "'");
        }
    }
};

} // namespace export_
