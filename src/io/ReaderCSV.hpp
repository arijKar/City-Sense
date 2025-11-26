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

/*
returns a copy of a given string with whitespace removed -> using isspace
param: input string (copied by val)
return: trimmed copy of s or empty string if all whitespace 
*/ 
inline std::string trim_copy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    auto tmp1 = std::find_if_not(s.begin(), s.end(), is_space);
    auto tmp2 = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    if (tmp1 >= tmp2) return {};
    return std::string(tmp1, tmp2);
}

/*
return a lowercased copy of the input string -> using tolower 
param: s input string (copied by val)
return: new string where all chars are now lowercase
*/
inline std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

/*
splits a csv line into trimmed fields separated by commas 
-> ',' is separator, trims whitespace, returns all fields even empty ones 
param: line CSV line to split 
return: vector of trimmed cells in order of appearance 
*/
inline std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    std::stringstream ss(line);
    while (std::getline(ss, cell, ',')) {
        out.push_back(trim_copy(cell));
    }
    return out;
}

/*
parses a string into an optional double 
-> trims whitespace, returns nullopt if string empty, verifies entire string was consumed, 
   returns nullopt on any parse error 
param: string to parse 
return: parsed double or nullopt if parsing fails or string empty 
*/
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

/*
best-effort parsing of a timestamp from a string 
- trims whitespace 
- interprets the string as an integer number of minutes since epoch -> using stoll
- on success, returns std::chrono::system_clock::time_point from minute count 
- on failure or empty input, returns default time_point (caller treats as "missing") 
param: string representation of a timestamp 
return: parsed time point or default time point on failure 
*/
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
        // fall through to default
    }
    return {};
}

} // namespace detail

/*
Streaming reader over one or more CSV files of sensor data.
- Skips any files that can't be opened.
- Parses a header row to discover column positions by name.
- Validates that required columns are present: timestamp/ts sensor_id/sensor one_id/zone 
- Skips malformed data rows (e.g., bad numeric values).
column names: timestamp, ts, sensor_id, sensor, zone_id, zone,
  speed, flow, pm25, pm10, db, noise, noise_db
*/
class ReaderCSV {
public:
    /* 
    list of file paths to read, in the order they should be processed.
    */
    explicit ReaderCSV(std::vector<std::string> inputs)
      : inputs_(std::move(inputs)) {}

    /*
    reads next batch of sensor records.
    - Opens input files as needed.
    - Parses and validates the header of each file to discover column indices.
    - Reads data lines from the current file.
    - Skips empty lines and malformed rows.
    - When all input files are exhausted:
    param: max_rows max number of records to return in this batch. If 0, an empty vector is returned.
    return: vector of parsed SensorRecord objects, size in [0, max_rows].
    */
    std::vector<model::SensorRecord> next_batch(std::size_t max_rows = 1000) {
        std::vector<model::SensorRecord> result;
        if (max_rows == 0) return result;

        while (result.size() < max_rows) {
            if (!current_.is_open()) {
                if (!open_next_file()) {
                    if (!opened_any_file_) {
                        throw std::runtime_error(
                            "ReaderCSV: no input files could be opened");
                    } else {
                        break; 
                    }
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
            if (line.empty()) continue;

            auto rec = parse_record(line);
            if (rec) {
                result.push_back(std::move(*rec));
            }
            
        }

        return result;
    }

private:
    /*
    opens the next readable input file.

    - Closes any currently open file.
    - Iterates from the current file index onward until it finds a file
      that can be opened; files that fail to open are skipped.
    - On success, resets header_parsed_ and marks opened_any_file_.

    return: true if a file was opened, false if no remaining file could be opened.
    */
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

    /*
    closes the currently open file, if any, and resets header state.
    */
    void close_current_file() {
        if (current_.is_open()) {
            current_.close();
        }
        header_parsed_ = false;
    }

    /*
    parses CSV header line and records column indices for known fields.

    - Column names are trimmed, lowercased, then matched against a small set
      of known identifiers like timestamp, sensor_id, zone_id, etc.
    - After scanning, the function enforces that the following are present:
        * timestamp
        * sensor_id 
        * zone_id 
      Missing  causes std::runtime_error.

    param: raw header line
    throws: std::runtime_error if required columns are missing.
    */
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

        // enforce required columns
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

    /*
    parses a single CSV data line into a SensorRecord.
    param: csv data line to parse 
    return: fully populated SensorRecord on success, or std::nullopt if malformed. 
    */
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

}
