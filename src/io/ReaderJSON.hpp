#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace io {

/*
configuration for citySense pipeline
*/
struct Config {
    // paths to input CSV files to be ingested by the pipeline
    std::vector<std::string> inputs;

    // size of the sliding time window (in minutes) used by the aggregator.
    int window_minutes{5};
};

namespace detail_json {

/*
returns a copy of the given string with whitespace removed 
param: input string 
return: trimmed copy of s or empty
*/
inline std::string trim_copy(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c); };
    auto it1 = std::find_if_not(s.begin(), s.end(), is_space);
    auto it2 = std::find_if_not(s.rbegin(), s.rend(), is_space).base();
    if (it1 >= it2) return {};
    return std::string(it1, it2);
}

/*
reads the entire contents of an input stream into a string 
param: open input file stream
return: string containing the full file contents 
*/
inline std::string read_all(std::ifstream& in) {
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

/*
parses an integer value appearing after a given key in a simple json text
Searches for the first occurrence of key.
Then finds the next ':' and parses the integer that follows, skipping whitespace.
On any failure to locate or parse, returns a default of 5.
param: config, json key to search window minutes 
return: parsed int value or 5 if key not found or parsinf fails 
*/
inline int parse_int_after(const std::string& text, const std::string& key) {
    auto pos = text.find(key);
    if (pos == std::string::npos) return 5;
    pos = text.find(':', pos);
    if (pos == std::string::npos) return 5;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    std::size_t consumed = 0;
    int v = std::stoi(text.substr(pos), &consumed);
    return v;
}

/*
parses an array of string appearing after a given key in a simple json-like text 
This function:
  - Finds the first occurrence of @p key.
  - Locates the first '[' after that key and the matching ']'.
  - Extracts substrings inside quotes and treats them as individual strings.
  - Trims whitespace around each extracted string.
  - Ignores any non-quoted content.
param: config text, json key to search inputs 
return: vector parsed string or empty if can not 
*/
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

} 

/*
simple json reader for citySense configuration 
readerJson loads small json-like config file extracts imputs and window_minutes 
*/

class ReaderJSON {
public:
    /*
    constructor readerJson 
    param: path to the JSON configuration file on disk.
    */
    explicit ReaderJSON(std::string path)
      : path_(std::move(path)) {}

    /*
    reads and parses the config file 
    this function: attempts to open path; throws runtime_error if file cannot open 
                    reads full content into memory extracts inputs and window_minutes int -> using detail_json 
    return: a populated config struct
    throws: runtime_error if file can n ot open 
    */

    Config read() const {
        std::ifstream in(path_);
        if (!in) {
            throw std::runtime_error("ReaderJSON: cannot open file '" + path_ + "'");
        }
        auto text = detail_json::read_all(in);

        Config cfg;
        cfg.inputs = detail_json::parse_string_array_after(text, "\"inputs\"");
        cfg.window_minutes = detail_json::parse_int_after(text, "\"window_minutes\"");
        return cfg;
    }

private:
    /// Path to the JSON configuration file.
    std::string path_;
};

} // namespace io
