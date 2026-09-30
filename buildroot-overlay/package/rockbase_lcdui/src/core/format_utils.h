#pragma once

// Value formatting / parsing helpers shared by the data model and the views.
// Pure standard C++ (no LVGL) so this layer can be unit-tested on the host.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lcdui {

int clamp_int(int value, int min_value, int max_value);
double parse_temp_celsius(const std::string& text, bool* ok = nullptr);
uint32_t threshold_color(double value, double warn, double danger, uint32_t normal_color);
double parse_hashrate_ghs(const std::string& value);
std::vector<int32_t> downsample_history_series(const std::vector<int32_t>& values, size_t max_points);
std::vector<int32_t> downsample_history_points(const std::vector<int32_t>& values, size_t max_points);

struct HashrateDisplay {
    double scaled = 0.0;
    const char* unit = "GH/s";
};

HashrateDisplay format_hashrate_display(double value_ghs);

struct HashrateValueParts {
    std::string int_part;
    std::string frac_part;
};

HashrateValueParts format_hashrate_value_parts(double value_ghs);
std::string format_share_value(double value);
std::string format_chart_time_label(int32_t ts);
std::string join_value(const std::string& left, const char* sep, const std::string& right, const char* fallback = "-");
bool has_value(const std::string& value);
std::string strip_commas(const std::string& value);
bool is_all_digits(const std::string& value);
bool is_rolling_number(const std::string& value);
std::string format_progress(const std::string& value);
void split_date_time(const std::string& value, std::string& date_part, std::string& time_part);
bool convert_utc_to_local(const std::string& value, std::string& date_part, std::string& time_part);
void split_pair_value(const std::string& value, std::string& left_part, std::string& right_part);
std::string ntp_label(const std::string& key);
std::string timezone_label(const std::string& key);
std::string timezone_offset_label(const std::string& key);
std::string current_time_text(const char* fmt);

}  // namespace lcdui
