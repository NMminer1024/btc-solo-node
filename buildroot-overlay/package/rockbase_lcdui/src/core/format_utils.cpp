#include "format_utils.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace lcdui {

int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

// Parses strings like "47.3C" or "N/A" produced by rockbase_statusd.sh.
double parse_temp_celsius(const std::string& text, bool* ok) {
    if (ok != nullptr) {
        *ok = false;
    }
    if (text.empty() || text == "-" || text == "N/A") {
        return 0.0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
        return 0.0;
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return parsed;
}

// Green/amber/red staging shared by the System page gauges.
uint32_t threshold_color(double value, double warn, double danger, uint32_t normal_color) {
    if (value >= danger) {
        return 0xd65a5a;
    }
    if (value >= warn) {
        return 0xf0b44c;
    }
    return normal_color;
}

double parse_hashrate_ghs(const std::string& value) {
    if (value.empty() || value == "-") {
        return 0.0;
    }

    char* end = nullptr;
    const double magnitude = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        return 0.0;
    }

    while (*end == ' ') {
        ++end;
    }

    switch (*end) {
    case 'P':
    case 'p':
        return magnitude * 1000000.0;
    case 'T':
    case 't':
        return magnitude * 1000.0;
    case 'G':
    case 'g':
        return magnitude;
    case 'M':
    case 'm':
        return magnitude / 1000.0;
    case 'K':
    case 'k':
        return magnitude / 1000000.0;
    default:
        return magnitude;
    }
}

std::vector<int32_t> downsample_history_series(const std::vector<int32_t>& values, size_t max_points) {
    if (values.size() <= max_points || max_points < 2) {
        return values;
    }

    std::vector<int32_t> out;
    out.reserve(max_points);
    const double step = static_cast<double>(values.size() - 1) / static_cast<double>(max_points - 1);
    for (size_t index = 0; index < max_points; ++index) {
        size_t src = static_cast<size_t>(step * static_cast<double>(index) + 0.5);
        if (src >= values.size()) {
            src = values.size() - 1;
        }
        out.push_back(values[src]);
    }
    return out;
}

std::vector<int32_t> downsample_history_points(const std::vector<int32_t>& values, size_t max_points) {
    return downsample_history_series(values, max_points);
}

HashrateDisplay format_hashrate_display(double value_ghs) {
    static constexpr const char* kUnits[] = {"GH/s", "TH/s", "PH/s", "EH/s"};
    size_t unit_index = 0;
    double scaled = value_ghs < 0.0 ? 0.0 : value_ghs;
    while (scaled >= 1000.0 && unit_index + 1 < sizeof(kUnits) / sizeof(kUnits[0])) {
        scaled /= 1000.0;
        ++unit_index;
    }
    return {scaled, kUnits[unit_index]};
}

// 数值拆成整数/小数两部分：小数点由独立的金色圆点对象渲染
// （stratum_total_hashrate_dot），因为 DSEG7 原生小数点只有数字高度的
// ~1/4，24px 下几乎看不清。
HashrateValueParts format_hashrate_value_parts(double value_ghs) {
    const HashrateDisplay display = format_hashrate_display(value_ghs);
    char buf[32];
    if (display.scaled >= 100.0) {
        std::snprintf(buf, sizeof(buf), "%.0f", display.scaled);
    }
    else if (display.scaled >= 10.0) {
        std::snprintf(buf, sizeof(buf), "%.1f", display.scaled);
    }
    else {
        std::snprintf(buf, sizeof(buf), "%.2f", display.scaled);
    }
    HashrateValueParts parts;
    const std::string text = buf;
    const size_t dot = text.find('.');
    if (dot != std::string::npos) {
        parts.int_part = text.substr(0, dot);
        parts.frac_part = text.substr(dot + 1);
    } else {
        parts.int_part = text;
    }
    return parts;
}

std::string format_share_value(double value) {
    if (value <= 0.0) {
        return "--";
    }
    static constexpr struct {
        double divisor;
        const char* suffix;
    } kUnits[] = {
        {1e12, "T"},
        {1e9, "G"},
        {1e6, "M"},
        {1e3, "K"},
    };
    for (const auto& unit : kUnits) {
        if (value >= unit.divisor) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f%s", value / unit.divisor, unit.suffix);
            return buf;
        }
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f", value);
    return buf;
}

std::string format_chart_time_label(int32_t ts) {
    if (ts <= 0) {
        return "--";
    }
    char buf[24];
    time_t raw = static_cast<time_t>(ts);
    tm local {};
    localtime_r(&raw, &local);
    std::strftime(buf, sizeof(buf), "%m-%d %H:%M", &local);
    return buf;
}

std::string join_value(const std::string& left, const char* sep, const std::string& right, const char* fallback) {
    const bool has_left = !left.empty() && left != "-" && left != "N/A";
    const bool has_right = !right.empty() && right != "-" && right != "N/A";
    if (has_left && has_right) {
        return left + sep + right;
    }
    if (has_left) {
        return left;
    }
    if (has_right) {
        return right;
    }
    return fallback;
}

bool has_value(const std::string& value) {
    return !value.empty() && value != "-";
}

std::string strip_commas(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch != ',') {
            out += ch;
        }
    }
    return out;
}

bool is_all_digits(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

bool is_rolling_number(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    bool has_decimal = false;
    for (char ch : value) {
        if (ch >= '0' && ch <= '9') {
            continue;
        }
        if (ch == '.' && !has_decimal) {
            has_decimal = true;
            continue;
        }
        return false;
    }
    return true;
}

// Keep the status API's millipercent precision while syncing. At completion,
// the compact integer "100" reads better and matches the finalized state.
std::string format_progress(const std::string& value) {
    if (!has_value(value)) {
        return "--";
    }

    char* end = nullptr;
    const double pct = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        return "--";
    }

    char buf[24];
    if (pct >= 100.0) {
        std::snprintf(buf, sizeof(buf), "100");
    }
    else {
        std::snprintf(buf, sizeof(buf), "%.3f", std::max(0.0, pct));
    }
    return buf;
}

void split_date_time(const std::string& value, std::string& date_part, std::string& time_part) {
    if (!has_value(value)) {
        date_part = "--";
        time_part.clear();
        return;
    }
    const std::size_t split = value.find(' ');
    if (split == std::string::npos) {
        date_part = value;
        time_part.clear();
        return;
    }
    date_part = value.substr(0, split);
    time_part = value.substr(split + 1);
}

// Convert a UTC timestamp string like "2026-09-15 00:09:36 UTC" into local
// date/time parts using the process timezone (set by sync_process_timezone).
// Returns false if the string cannot be parsed as a UTC datetime.
bool convert_utc_to_local(const std::string& value, std::string& date_part, std::string& time_part) {
    tm utc {};
    if (strptime(value.c_str(), "%Y-%m-%d %H:%M:%S", &utc) == nullptr) {
        return false;
    }
    const time_t epoch = timegm(&utc);
    if (epoch < 0) {
        return false;
    }
    tm local {};
    localtime_r(&epoch, &local);
    char date_buf[16];
    char time_buf[16];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &local);
    std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &local);
    date_part = date_buf;
    time_part = time_buf;
    return true;
}

void split_pair_value(const std::string& value, std::string& left_part, std::string& right_part) {
    if (!has_value(value)) {
        left_part = "--";
        right_part.clear();
        return;
    }
    const std::size_t split = value.find(" / ");
    if (split == std::string::npos) {
        left_part = value;
        right_part.clear();
        return;
    }
    left_part = value.substr(0, split);
    right_part = value.substr(split + 3);
}

std::string ntp_label(const std::string& key) {
    if (key == "running") {
        return "NTP Ready";
    }
    return "NTP Unsynced";
}

std::string timezone_label(const std::string& key) {
    if (key == "-" || key.empty()) {
        return "UTC";
    }
    if (key == "CST-8") {
        return "Asia/Shanghai";
    }
    return key;
}

// 时区名 -> UTC 偏移显示文本。
// 注意：新增/删除时区时，必须与 timezone_overlay.h 的 kZones[] 保持同步。
std::string timezone_offset_label(const std::string& key) {
    const std::string zone = timezone_label(key);
    if (zone == "UTC") return "+0.0";
    if (zone == "Pacific/Pago_Pago") return "-11.0";
    if (zone == "Pacific/Honolulu") return "-10.0";
    if (zone == "America/Anchorage") return "-9.0";
    if (zone == "America/Los_Angeles") return "-8.0";
    if (zone == "America/Denver") return "-7.0";
    if (zone == "America/Chicago") return "-6.0";
    if (zone == "America/Mexico_City") return "-6.0";
    if (zone == "America/New_York") return "-5.0";
    if (zone == "America/Santiago") return "-4.0";
    if (zone == "America/Sao_Paulo") return "-3.0";
    if (zone == "Atlantic/Azores") return "-1.0";
    if (zone == "Europe/Berlin") return "+1.0";
    if (zone == "Europe/London") return "+0.0";
    if (zone == "Africa/Cairo") return "+2.0";
    if (zone == "Africa/Johannesburg") return "+2.0";
    if (zone == "Europe/Moscow") return "+3.0";
    if (zone == "Europe/Istanbul") return "+3.0";
    if (zone == "Asia/Tehran") return "+3.5";
    if (zone == "Asia/Dubai") return "+4.0";
    if (zone == "Asia/Kabul") return "+4.5";
    if (zone == "Asia/Karachi") return "+5.0";
    if (zone == "Asia/Kolkata") return "+5.5";
    if (zone == "Asia/Dhaka") return "+6.0";
    if (zone == "Asia/Yangon") return "+6.5";
    if (zone == "Asia/Bangkok") return "+7.0";
    if (zone == "Asia/Shanghai") return "+8.0";
    if (zone == "Asia/Singapore") return "+8.0";
    if (zone == "Asia/Seoul") return "+9.0";
    if (zone == "Asia/Tokyo") return "+9.0";
    if (zone == "Australia/Adelaide") return "+9.5";
    if (zone == "Australia/Sydney") return "+10.0";
    if (zone == "Australia/Perth") return "+8.0";
    if (zone == "Pacific/Noumea") return "+11.0";
    if (zone == "Pacific/Auckland") return "+12.0";
    return "--";
}

std::string current_time_text(const char* fmt) {
    char buf[32];
    const time_t now = time(nullptr);
    tm local {};
    localtime_r(&now, &local);
    std::strftime(buf, sizeof(buf), fmt, &local);
    return buf;
}

}  // namespace lcdui
