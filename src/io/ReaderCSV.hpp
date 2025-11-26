#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <chrono>

#include "../model/SensorRecord.hpp"

namespace io {

namespace detail {

// Trim leading/trailing whitespace from a copy of the string.
inline std::string trim_copy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    auto it1 = std::find_if_not(s.begin(), s.end(), is_space);
    auto it2 = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    if (it1 >= it2) return {};
    return std::string(it1, it2);
}

// Return a lower-cased copy of the string.
inline std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// Split a CSV line on commas and trim each field.
inline std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    std::stringstream ss(line);
    while (std::getline(ss, cell, ',')) {
        out.push_back(trim_copy(cell));
    }
    return out;
}

// Try to parse a double from a string.
// Empty/invalid strings return std::nullopt.
inline std::optional<double> parse_optional_double(const std::string& s) {
    auto t = trim_copy(s);
    if (t.empty()) return std::nullopt;

    try {
        std::size_t pos = 0;
        double v = std::stod(t, &pos);
        if (pos != t.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

// Very simple timestamp parser used for this project.
//
// We treat the input as an integer number of minutes since epoch.
// If it fails, or the string is empty, we just return the default time_point.
inline std::chrono::system_clock::time_point parse_timestamp_best_effort(
    const std::string& s)
{
    auto t = trim_copy(s);
    if (t.empty()) {
        return {};
    }

    try {
        std::size_t pos = 0;
        long long v = std::stoll(t, &pos);
        if (pos == t.size()) {
            using namespace std::chrono;
            return std::chrono::system_clock::time_point{minutes{v}};
        }
    } catch (...) {
        // ignore and fall through
    }
    return {};
}

} // namespace detail

// Reader over one or more CSV files of sensor data.
//
//  - tries to open each path from the list (skips ones that fail)
//  - reads a header line and discovers column indices by name
//  - requires timestamp/ts, sensor_id/sensor, and zone_id/zone
//  - parses each row into a SensorRecord
//  - skips rows with bad zone_id or other malformed values
class ReaderCSV {
public:
    // Constructor takes a list of file paths in the order we want to read them.
    explicit ReaderCSV(std::vector<std::string> inputs)
      : inputs_(std::move(inputs)) {}

    // Read up to max_rows SensorRecord objects.
    //
    // - Opens files on demand.
    // - Validates the header the first time we see it.
    // - Returns as soon as we either hit max_rows or run out of data.
    // - If no file could be opened at all, throws std::runtime_error.
    std::vector<model::SensorRecord> next_batch(std::size_t max_rows = 1000) {
        std::vector<model::SensorRecord> result;
        if (max_rows == 0) return result;

        while (result.size() < max_rows) {
            if (!current_.is_open()) {
                if (!open_next_file()) {
                    if (!opened_any_file_) {
                        throw std::runtime_error(
                            "ReaderCSV: no input files could be opened");
                    }
                    break; // nothing more to read
                }
            }

            if (!header_parsed_) {
                std::string header_line;
                if (!std::getline(current_, header_line)) {
                    close_current_file();
                    continue;
                }
                parse_header(header_line);
                header_parsed_ = true;
                continue;
            }

            std::string line;
            if (!std::getline(current_, line)) {
                close_current_file();
                continue;
            }
            if (line.empty()) {
                continue;
            }

            auto rec = parse_record(line);
            if (rec) {
                result.push_back(std::move(*rec));
            }
            // malformed rows are just ignored
        }

        return result;
    }

private:
    // Close the current file, if any, and reset header state.
    void close_current_file() {
        if (current_.is_open()) {
            current_.close();
        }
        header_parsed_ = false;
    }

    // Try to open the next file from inputs_.
    // Files that fail to open are skipped.
    bool open_next_file() {
        close_current_file();

        while (current_index_ < inputs_.size()) {
            current_.clear();
            current_.open(inputs_[current_index_]);
            ++current_index_;

            if (current_) {
                header_parsed_ = false;
                opened_any_file_ = true;
                return true;
            }
        }
        return false;
    }

    // Parse the header line and figure out which column index is which.
    //
    // We accept:
    //   timestamp or ts
    //   sensor_id or sensor
    //   zone_id or zone
    //
    // If any of those are missing, we throw.
    void parse_header(const std::string& line) {
        using namespace detail;

        auto cols = split_csv_line(line);
        col_ts_ = col_sensor_ = col_zone_ =
            col_speed_ = col_flow_ = col_pm25_ = col_pm10_ = col_db_ = -1;

        for (std::size_t i = 0; i < cols.size(); ++i) {
            auto name = to_lower_copy(cols[i]);
            if (name == "timestamp" || name == "ts") {
                col_ts_ = static_cast<int>(i);
            } else if (name == "sensor_id" || name == "sensor") {
                col_sensor_ = static_cast<int>(i);
            } else if (name == "zone_id" || name == "zone") {
                col_zone_ = static_cast<int>(i);
            } else if (name == "speed") {
                col_speed_ = static_cast<int>(i);
            } else if (name == "flow") {
                col_flow_ = static_cast<int>(i);
            } else if (name == "pm25" || name == "pm2_5") {
                col_pm25_ = static_cast<int>(i);
            } else if (name == "pm10") {
                col_pm10_ = static_cast<int>(i);
            } else if (name == "db" || name == "noise" || name == "noise_db") {
                col_db_ = static_cast<int>(i);
            }
        }

        if (col_ts_ < 0) {
            throw std::runtime_error(
                "ReaderCSV: required column 'timestamp' (or 'ts') is missing");
        }
        if (col_sensor_ < 0) {
            throw std::runtime_error(
                "ReaderCSV: required column 'sensor_id' (or 'sensor') is missing");
        }
        if (col_zone_ < 0) {
            throw std::runtime_error(
                "ReaderCSV: required column 'zone_id' (or 'zone') is missing");
        }
    }

    // Parse a single data row into a SensorRecord.
    // If we can't get a valid zone_id, we return std::nullopt.
    std::optional<model::SensorRecord> parse_record(const std::string& line) const {
        using namespace detail;

        auto fields = split_csv_line(line);

        auto get_field = [&](int idx) -> std::string {
            if (idx < 0 || static_cast<std::size_t>(idx) >= fields.size()) {
                return {};
            }
            return fields[static_cast<std::size_t>(idx)];
        };

        model::SensorRecord rec;

        rec.ts = parse_timestamp_best_effort(get_field(col_ts_));
        rec.sensor_id = get_field(col_sensor_);

        // zone_id must be present and parse as an int
        {
            auto z = get_field(col_zone_);
            if (z.empty()) {
                return std::nullopt;
            }
            try {
                rec.zone_id = std::stoi(z);
            } catch (...) {
                return std::nullopt;
            }
        }

        if (col_speed_ >= 0) {
            rec.speed = parse_optional_double(get_field(col_speed_));
        }
        if (col_flow_ >= 0) {
            rec.flow = parse_optional_double(get_field(col_flow_));
        }
        if (col_pm25_ >= 0) {
            rec.pm25 = parse_optional_double(get_field(col_pm25_));
        }
        if (col_pm10_ >= 0) {
            rec.pm10 = parse_optional_double(get_field(col_pm10_));
        }
        if (col_db_ >= 0) {
            rec.db = parse_optional_double(get_field(col_db_));
        }

        return rec;
    }

    std::vector<std::string> inputs_;
    std::size_t current_index_{0};
    std::ifstream current_;
    bool header_parsed_{false};
    bool opened_any_file_{false};

    int col_ts_{-1};
    int col_sensor_{-1};
    int col_zone_{-1};
    int col_speed_{-1};
    int col_flow_{-1};
    int col_pm25_{-1};
    int col_pm10_{-1};
    int col_db_{-1};
};

} // namespace io
