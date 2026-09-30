#pragma once

// Minimal JSON extraction helpers for the status caches written by
// rockbase_statusd.sh and the stratum service. Pure standard C++ (no LVGL)
// so this layer can be built and unit-tested on the host.
//
// NOTE: these are intentionally small scan helpers, not a full JSON parser.
// Replacing them with a real parser (e.g. cJSON) is a tracked follow-up;
// keep the public signatures stable until then.

#include <cstddef>
#include <string>
#include <vector>

namespace lcdui {

size_t skip_ws(const std::string& s, size_t pos);
std::string json_unescape(const std::string& value);
std::string json_object(const std::string& json, const char* object_name);
std::string json_array(const std::string& json, const char* array_name);
std::string json_value(const std::string& object, const char* key, const char* fallback = "-");
std::vector<std::string> json_string_array(const std::string& array_json);
std::vector<std::string> json_array_objects(const std::string& array_json);
double json_double(const std::string& object, const char* key, double fallback);
int json_int_value(const std::string& object, const char* key, int fallback = 0);
int parse_int_or(const std::string& text, int fallback);
long parse_leading_int(const std::string& value, long fallback = -1);

}  // namespace lcdui
