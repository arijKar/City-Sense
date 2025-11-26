#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace io {

// Simple config struct for the CitySense pipeline.
//
// - inputs: list of CSV files to read
// - window_minutes: sliding window size for the aggregator (default 5)
struct Config {
    std::vector<std::string> inputs;
    int window_minutes{5};
};

namespace detail_json {

// Trim whitespace from both ends (copy version).
inline std::string trim_copy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    auto it1 = std::find_if_not(s.begin(), s.end(), is_space);
    auto it2 = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    if (it1 >= it2) return {};
    return std::string(it1, it2);
}

// Read the whole file into a single string.
inline std::string read_all(std::ifstream& in) {
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// parse an int after this key
//
// We assume something like:
//   "window_minutes": 5
//
// If anything goes wrong, we just return 5 as a default.
inline int parse_int_after(const std::string& text, const std::string& key) {
    auto pos = text.find(key);
    if (pos == std::string::npos) return 5;

    pos = text.find(':', pos);
    if (pos == std::string::npos) return 5;
    ++pos;

    while (pos < text.size() &&
           std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }

    int v = 5;
    try {
        std::size_t consumed = 0;
        v = std::stoi(text.substr(pos), &consumed);
    } catch (...) {
        // keep default 5
    }
    return v;
}

// Parse a JSON-like array of strings after a key.
//
// We handle imput like:
//   "inputs": ["air.csv", "traffic.csv", "noise.csv"]
//
inline std::vector<std::string> parse_string_array_after(const std::string& text,
                                                         const std::string& key) {
    std::vector<std::string> out;

    auto pos = text.find(key);
    if (pos == std::string::npos) return out;

    pos = text.find('[', pos);
    if (pos == std::string::npos) return out;

    auto end = text.find(']', pos);
    if (end == std::string::npos) return out;

    std::string inner = text.substr(pos + 1, end - pos - 1);
    std::string current;
    bool in_string = false;

    for (char c : inner) {
        if (c == '"') {
            in_string = !in_string;
            if (!in_string && !current.empty()) {
                out.push_back(trim_copy(current));
                current.clear();
            }
        } else if (in_string) {
            current.push_back(c);
        }
    }

    return out;
}

} // namespace detail_json

// Minimal JSON reader for the CitySense config file.
//
// It expects something like:
//
// {
//   "inputs": ["air.csv", "traffic.csv", "noise.csv"]
//    "window_minutes": 5,
// }
//
// If no input , we throw.
// If window_minutes is missing or broken, we just use 5 by default.
class ReaderJSON {
public:
    explicit ReaderJSON(std::string path)
      : path_(std::move(path)) {}

    Config read() const {
        std::ifstream in(path_);
        if (!in) {
            throw std::runtime_error(
                "ReaderJSON: cannot open file '" + path_ + "'");
        }

        auto text = detail_json::read_all(in);

        Config cfg;
        cfg.inputs = detail_json::parse_string_array_after(text, "\"inputs\"");
        cfg.window_minutes =
            detail_json::parse_int_after(text, "\"window_minutes\"");
        return cfg;
    }

private:
    std::string path_;
};

} // namespace io
