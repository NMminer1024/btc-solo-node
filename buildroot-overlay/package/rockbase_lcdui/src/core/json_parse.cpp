#include "json_parse.h"

#include <cmath>
#include <cstdlib>

namespace lcdui {

size_t skip_ws(const std::string& s, size_t pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) {
        ++pos;
    }
    return pos;
}

std::string json_unescape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) {
            out.push_back(value[i]);
            continue;
        }

        const char esc = value[++i];
        switch (esc) {
        case '"':
        case '\\':
        case '/':
            out.push_back(esc);
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back(' ');
            break;
        case 'r':
            break;
        case 't':
            out.push_back(' ');
            break;
        default:
            out.push_back(esc);
            break;
        }
    }
    return out;
}

std::string json_object(const std::string& json, const char* object_name) {
    const std::string token = std::string("\"") + object_name + "\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos) {
        return "";
    }

    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return "";
    }
    pos = skip_ws(json, pos + 1);
    if (pos >= json.size() || json[pos] != '{') {
        return "";
    }

    const size_t start = pos;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (in_string) {
            if (escaped) {
                escaped = false;
            }
            else if (ch == '\\') {
                escaped = true;
            }
            else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
        }
        else if (ch == '{') {
            ++depth;
        }
        else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(start, pos - start + 1);
            }
        }
    }

    return "";
}

std::string json_array(const std::string& json, const char* array_name) {
    const std::string token = std::string("\"") + array_name + "\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos) {
        return "";
    }

    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return "";
    }
    pos = skip_ws(json, pos + 1);
    if (pos >= json.size() || json[pos] != '[') {
        return "";
    }

    const size_t start = pos;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (in_string) {
            if (escaped) {
                escaped = false;
            }
            else if (ch == '\\') {
                escaped = true;
            }
            else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
        }
        else if (ch == '[') {
            ++depth;
        }
        else if (ch == ']') {
            --depth;
            if (depth == 0) {
                return json.substr(start, pos - start + 1);
            }
        }
    }

    return "";
}

std::string json_value(const std::string& object, const char* key, const char* fallback) {
    const std::string token = std::string("\"") + key + "\"";
    size_t pos = object.find(token);
    if (pos == std::string::npos) {
        return fallback;
    }

    pos = object.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = skip_ws(object, pos + 1);
    if (pos >= object.size()) {
        return fallback;
    }

    if (object[pos] == '"') {
        const size_t start = ++pos;
        bool escaped = false;
        for (; pos < object.size(); ++pos) {
            const char ch = object[pos];
            if (escaped) {
                escaped = false;
            }
            else if (ch == '\\') {
                escaped = true;
            }
            else if (ch == '"') {
                return json_unescape(object.substr(start, pos - start));
            }
        }
        return fallback;
    }

    const size_t start = pos;
    while (pos < object.size() && object[pos] != ',' && object[pos] != '}') {
        ++pos;
    }
    std::string value = object.substr(start, pos - start);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\n' || value.back() == '\r' || value.back() == '\t')) {
        value.pop_back();
    }
    if (value.empty() || value == "null") {
        return fallback;
    }
    return value;
}

std::vector<std::string> json_string_array(const std::string& array_json) {
    std::vector<std::string> values;
    if (array_json.size() < 2 || array_json.front() != '[') return values;
    size_t pos = 1;
    while (pos < array_json.size()) {
        pos = array_json.find('"', pos);
        if (pos == std::string::npos) break;
        const size_t start = ++pos;
        bool escaped = false;
        for (; pos < array_json.size(); ++pos) {
            const char ch = array_json[pos];
            if (escaped) {
                escaped = false;
            }
            else if (ch == '\\') {
                escaped = true;
            }
            else if (ch == '"') {
                values.push_back(json_unescape(array_json.substr(start, pos - start)));
                ++pos;
                break;
            }
        }
    }
    return values;
}

double json_double(const std::string& object, const char* key, double fallback) {
    const std::string value = json_value(object, key, "");
    if (value.empty()) return fallback;
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    return end != value.c_str() ? parsed : fallback;
}

int json_int_value(const std::string& object, const char* key, int fallback) {
    const std::string value = json_value(object, key, "");
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str()) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

int parse_int_or(const std::string& text, int fallback) {
    if (text.empty() || text == "-") {
        return fallback;
    }
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) {
        return fallback;
    }
    return static_cast<int>(std::lround(parsed));
}

std::vector<std::string> json_array_objects(const std::string& array_json) {
    std::vector<std::string> out;
    if (array_json.size() < 2 || array_json.front() != '[') {
        return out;
    }

    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t pos = 0; pos < array_json.size(); ++pos) {
        const char ch = array_json[pos];
        if (in_string) {
            if (escaped) {
                escaped = false;
            }
            else if (ch == '\\') {
                escaped = true;
            }
            else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            if (depth == 0) {
                start = pos;
            }
            ++depth;
            continue;
        }
        if (ch == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                out.push_back(array_json.substr(start, pos - start + 1));
                start = std::string::npos;
            }
        }
    }

    return out;
}

long parse_leading_int(const std::string& value, long fallback) {
    long out = 0;
    bool seen_digit = false;
    for (char ch : value) {
        if (ch >= '0' && ch <= '9') {
            seen_digit = true;
            out = out * 10 + (ch - '0');
        }
        else if (seen_digit) {
            break;
        }
    }
    return seen_digit ? out : fallback;
}

}  // namespace lcdui
