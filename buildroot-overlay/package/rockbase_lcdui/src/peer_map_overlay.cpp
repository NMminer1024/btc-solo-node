#include "peer_map_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

namespace {

constexpr int32_t kWidth = 800;
constexpr int32_t kHeight = 480;
// The map renders below the 72px title bar (title + close button row).
constexpr int32_t kMapTop = 72;
constexpr int32_t kMapHeight = kHeight - kMapTop;  // 408
constexpr size_t kFrameBytes = static_cast<size_t>(kWidth) * kMapHeight * 4;
constexpr const char* kMapPath =
    "/usr/share/rockbase-lcdui/world_map_800x408.argb";
constexpr const char* kGeoipBin = "/usr/bin/rockbase-geoip";
constexpr double kLatMax = 85.0;
constexpr double kLatMin = -60.0;
constexpr double kLonCenter = 170.0;
constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kAnimPeriodMs = 200;  // 5 fps is smooth enough for dashed flow.

std::map<std::string, PeerMapOverlay::Geo> g_geo_cache;

uint32_t monotonic_ms() {
    timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

bool read_exact_file(const char* path, std::vector<uint8_t>& out) {
    out.assign(kFrameBytes, 0);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t remaining = out.size();
    uint8_t* dst = out.data();
    while (remaining > 0) {
        const ssize_t n = read(fd, dst, remaining);
        if (n <= 0) {
            close(fd);
            out.clear();
            return false;
        }
        dst += n;
        remaining -= static_cast<size_t>(n);
    }
    close(fd);
    return true;
}

bool safe_ip(const std::string& ip) {
    if (ip.empty() || ip.size() > 64) return false;
    return std::all_of(ip.begin(), ip.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || ch == '.' || ch == ':' ||
               (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    });
}

double json_number(const std::string& json, const char* key, double fallback) {
    const std::string token = std::string("\"") + key + "\":";
    size_t pos = json.find(token);
    if (pos == std::string::npos) return fallback;
    pos += token.size();
    return std::strtod(json.c_str() + pos, nullptr);
}

std::string json_string(const std::string& json, const char* key) {
    const std::string token = std::string("\"") + key + "\":\"";
    size_t pos = json.find(token);
    if (pos == std::string::npos) return {};
    pos += token.size();
    const size_t end = json.find('"', pos);
    return end == std::string::npos ? std::string() : json.substr(pos, end - pos);
}

PeerMapOverlay::Geo lookup_geo(const std::string& ip) {
    const auto cached = g_geo_cache.find(ip);
    if (cached != g_geo_cache.end()) return cached->second;

    PeerMapOverlay::Geo geo;
    if (!safe_ip(ip) || access(kGeoipBin, X_OK) != 0) {
        g_geo_cache[ip] = geo;
        return geo;
    }

    char command[192];
    std::snprintf(command, sizeof(command),
                  "printf '%%s\\n' '%s' | %s 2>/dev/null", ip.c_str(), kGeoipBin);
    FILE* pipe = popen(command, "r");
    if (pipe == nullptr) {
        g_geo_cache[ip] = geo;
        return geo;
    }
    char line[512] {};
    const bool got_line = std::fgets(line, sizeof(line), pipe) != nullptr;
    const int status = pclose(pipe);
    if (got_line && status == 0 && std::strstr(line, "\"error\"") == nullptr) {
        const std::string json(line);
        geo.lat = json_number(json, "lat", 999.0);
        geo.lon = json_number(json, "lon", 999.0);
        geo.city = json_string(json, "city");
        geo.country = json_string(json, "country");
        geo.known = geo.lat >= -90.0 && geo.lat <= 90.0 &&
                    geo.lon >= -180.0 && geo.lon <= 180.0;
    }
    g_geo_cache[ip] = geo;
    return geo;
}

void project(double lat, double lon, int32_t& x, int32_t& y) {
    // Coordinates are relative to the map buffer (800 x 408); the buffer is
    // blitted at (0, kMapTop) on screen.
    const double lo = kLonCenter - 180.0;
    double nlon = std::fmod(lon - lo, 360.0);
    if (nlon < 0.0) nlon += 360.0;
    nlon += lo;
    x = static_cast<int32_t>(std::lround((nlon - lo) / 360.0 * kWidth));
    y = static_cast<int32_t>(std::lround((kLatMax - lat) /
                                         (kLatMax - kLatMin) * kMapHeight));
    x = std::max(0, std::min(kWidth - 1, x));
    y = std::max(0, std::min(kMapHeight - 1, y));
}

void blend_pixel(std::vector<uint8_t>& buf, int32_t x, int32_t y,
                 uint32_t rgb, uint8_t alpha = 255) {
    if (x < 0 || x >= kWidth || y < 0 || y >= kMapHeight) return;
    const size_t off = (static_cast<size_t>(y) * kWidth + x) * 4;
    const uint8_t sr = static_cast<uint8_t>((rgb >> 16) & 0xff);
    const uint8_t sg = static_cast<uint8_t>((rgb >> 8) & 0xff);
    const uint8_t sb = static_cast<uint8_t>(rgb & 0xff);
    const uint16_t inv = static_cast<uint16_t>(255 - alpha);
    buf[off] = static_cast<uint8_t>((sb * alpha + buf[off] * inv) / 255);
    buf[off + 1] = static_cast<uint8_t>((sg * alpha + buf[off + 1] * inv) / 255);
    buf[off + 2] = static_cast<uint8_t>((sr * alpha + buf[off + 2] * inv) / 255);
    buf[off + 3] = 255;
}

void draw_disk(std::vector<uint8_t>& buf, int32_t cx, int32_t cy, int32_t radius,
               uint32_t color, uint8_t alpha = 255) {
    for (int32_t y = -radius; y <= radius; ++y) {
        for (int32_t x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= radius * radius) {
                blend_pixel(buf, cx + x, cy + y, color, alpha);
            }
        }
    }
}

void draw_line(std::vector<uint8_t>& buf, int32_t x0, int32_t y0,
               int32_t x1, int32_t y1, uint32_t color, uint8_t alpha = 255,
               int32_t thickness = 1) {
    const int32_t dx = std::abs(x1 - x0);
    const int32_t sx = x0 < x1 ? 1 : -1;
    const int32_t dy = -std::abs(y1 - y0);
    const int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        draw_disk(buf, x0, y0, std::max(0, thickness - 1), color, alpha);
        if (x0 == x1 && y0 == y1) break;
        const int32_t e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

}  // namespace

void PeerMapOverlay::open(const std::vector<std::string>& peer_ips, const Geo& self_geo) {
    close();
    close_requested_ = false;
    ips_ = peer_ips;
    self_geo_ = self_geo;

    map_ready_ = read_exact_file(kMapPath, backdrop_);
    if (!map_ready_) {
        backdrop_.assign(kFrameBytes, 0);
        for (size_t i = 0; i < kFrameBytes; i += 4) {
            backdrop_[i] = 0x13;
            backdrop_[i + 1] = 0x0a;
            backdrop_[i + 2] = 0x06;
            backdrop_[i + 3] = 0xff;
        }
    }
    buf_ = backdrop_;

    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x060a13), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);

    std::memset(&img_dsc_, 0, sizeof(img_dsc_));
    img_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    img_dsc_.header.cf = LV_COLOR_FORMAT_ARGB8888;
    img_dsc_.header.w = kWidth;
    img_dsc_.header.h = kMapHeight;
    img_dsc_.header.stride = kWidth * 4;
    img_dsc_.data_size = static_cast<uint32_t>(buf_.size());
    img_dsc_.data = buf_.data();

    map_img_ = lv_image_create(root_);
    lv_obj_remove_style_all(map_img_);
    lv_image_set_src(map_img_, &img_dsc_);
    lv_obj_set_pos(map_img_, 0, kMapTop);

    // Header readability comes from subtle solid strips, not a floating card.
    lv_obj_t* header = lv_obj_create(root_);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, kWidth, 72);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x060a13), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_70, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    title_label_ = lv_label_create(header);
    lv_label_set_text(title_label_, "Peer Map");
    lv_obj_set_pos(title_label_, 24, 20);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(0xf4efe2), 0);

    close_btn_ = lv_btn_create(header);
    lv_obj_set_size(close_btn_, 56, 56);
    lv_obj_set_pos(close_btn_, kWidth - 72, 8);
    lv_obj_set_style_radius(close_btn_, 28, 0);
    lv_obj_set_style_bg_opa(close_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(close_btn_, lv_color_hex(0x26343f), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(close_btn_, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(close_btn_, 0, 0);
    lv_obj_set_style_pad_all(close_btn_, 0, 0);
    lv_obj_t* close_label = lv_label_create(close_btn_);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xc3cdd3), 0);
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_btn_, on_close_btn, LV_EVENT_CLICKED, this);

    status_label_ = lv_label_create(root_);
    lv_obj_set_pos(status_label_, 24, kHeight - 38);
    lv_obj_set_width(status_label_, kWidth - 48);
    lv_obj_set_style_text_font(status_label_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_hex(0xc3cdd3), 0);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_LEFT, 0);
    set_status("Locating peers...");

    resolve_geo(ips_);
    anim_start_ms_ = monotonic_ms();
    draw_frame(0.0);
    anim_timer_ = lv_timer_create(anim_timer_cb, kAnimPeriodMs, this);
}

void PeerMapOverlay::resolve_geo(const std::vector<std::string>& ips) {
    markers_.clear();
    size_t located = 0;
    for (const std::string& ip : ips) {
        const Geo geo = lookup_geo(ip);
        if (!geo.known) continue;
        Marker marker;
        project(geo.lat, geo.lon, marker.x, marker.y);
        markers_.push_back(marker);
        ++located;
    }

    self_x_ = -1;
    self_y_ = -1;
    if (self_geo_.known) {
        project(self_geo_.lat, self_geo_.lon, self_x_, self_y_);
    }

    char status[96];
    std::snprintf(status, sizeof(status), "%zu of %zu peers located",
                  located, ips.size());
    set_status(status);
}

void PeerMapOverlay::set_status(const std::string& text) {
    if (status_label_ != nullptr) lv_label_set_text(status_label_, text.c_str());
}

void PeerMapOverlay::draw_arc(int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                              double t, uint32_t color) {
    const double dx = static_cast<double>(x2 - x1);
    const double dy = static_cast<double>(y2 - y1);
    const double dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 20.0) return;

    const double mx = (x1 + x2) * 0.5;
    const double my = (y1 + y2) * 0.5;
    double nx = -dy / dist;
    double ny = dx / dist;
    if (ny > 0.0) { nx = -nx; ny = -ny; }
    const double cx = mx + nx * dist * 0.18;
    const double cy = my + ny * dist * 0.18;

    constexpr int kSteps = 120;
    // Dash pattern: 2 on / 4 off (~5px dashes at 120 steps, matching the
    // web map's 5/9 dash scaled to the 800px canvas). The phase subtracts
    // from the step index so the visible dashes travel from the start point
    // toward the destination (self -> peer), matching the web map's
    // negative lineDashOffset.
    const int dash_phase = static_cast<int>(t * 28.0) % 6;
    int32_t prev_x = x1;
    int32_t prev_y = y1;
    for (int i = 1; i <= kSteps; ++i) {
        const double u = static_cast<double>(i) / kSteps;
        const double one = 1.0 - u;
        const int32_t x = static_cast<int32_t>(std::lround(
            one * one * x1 + 2.0 * one * u * cx + u * u * x2));
        const int32_t y = static_cast<int32_t>(std::lround(
            one * one * y1 + 2.0 * one * u * cy + u * u * y2));
        const int pos = ((i - dash_phase) % 6 + 6) % 6;
        if (pos < 2) {
            draw_line(buf_, prev_x, prev_y, x, y, color, 220, 1);
        }
        prev_x = x;
        prev_y = y;
    }

    const double angle = std::atan2(y2 - cy, x2 - cx);
    const int32_t ax1 = x2 - static_cast<int32_t>(8 * std::cos(angle - 0.42));
    const int32_t ay1 = y2 - static_cast<int32_t>(8 * std::sin(angle - 0.42));
    const int32_t ax2 = x2 - static_cast<int32_t>(8 * std::cos(angle + 0.42));
    const int32_t ay2 = y2 - static_cast<int32_t>(8 * std::sin(angle + 0.42));
    draw_line(buf_, x2, y2, ax1, ay1, color, 235, 1);
    draw_line(buf_, x2, y2, ax2, ay2, color, 235, 1);
}

void PeerMapOverlay::draw_star(int32_t cx, int32_t cy, int32_t r,
                               uint32_t fill, uint32_t stroke) {
    int32_t xs[10] {};
    int32_t ys[10] {};
    for (int i = 0; i < 10; ++i) {
        const double radius = (i % 2 == 0) ? r : r * 0.45;
        const double angle = -kPi / 2.0 + i * kPi / 5.0;
        xs[i] = cx + static_cast<int32_t>(std::lround(radius * std::cos(angle)));
        ys[i] = cy + static_cast<int32_t>(std::lround(radius * std::sin(angle)));
    }
    // Small star: fill by radial lines from the center, then crisp outline.
    for (int i = 0; i < 10; ++i) {
        draw_line(buf_, cx, cy, xs[i], ys[i], fill, 255, 1);
        draw_line(buf_, xs[i], ys[i], xs[(i + 1) % 10], ys[(i + 1) % 10],
                  stroke, 235, 1);
    }
    draw_disk(buf_, cx, cy, 2, fill);
}

void PeerMapOverlay::draw_frame(double t) {
    if (map_img_ == nullptr || backdrop_.size() != kFrameBytes) return;
    std::memcpy(buf_.data(), backdrop_.data(), kFrameBytes);

    if (self_x_ >= 0 && self_y_ >= 0) {
        for (const Marker& marker : markers_) {
            draw_arc(self_x_, self_y_, marker.x, marker.y, t, 0xf7931a);
        }
    }

    for (const Marker& marker : markers_) {
        draw_disk(buf_, marker.x, marker.y, 7, 0xffffff, 215);
        draw_disk(buf_, marker.x, marker.y, 5, 0xf7931a);
    }
    if (self_x_ >= 0 && self_y_ >= 0) {
        draw_star(self_x_, self_y_, 11, 0xff3b30, 0xffffff);
    }

    lv_obj_invalidate(map_img_);
}

void PeerMapOverlay::update() {
    if (root_ == nullptr) return;
    if (close_requested_) close();
}

void PeerMapOverlay::close() {
    if (anim_timer_ != nullptr) {
        lv_timer_delete(anim_timer_);
        anim_timer_ = nullptr;
    }
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
    map_img_ = nullptr;
    close_btn_ = nullptr;
    title_label_ = nullptr;
    status_label_ = nullptr;
    close_requested_ = false;
    markers_.clear();
    ips_.clear();
    backdrop_.clear();
    buf_.clear();
    std::memset(&img_dsc_, 0, sizeof(img_dsc_));
}

void PeerMapOverlay::on_close_btn(lv_event_t* e) {
    auto* self = static_cast<PeerMapOverlay*>(lv_event_get_user_data(e));
    if (self != nullptr) self->close_requested_ = true;
    lv_event_stop_processing(e);
}

void PeerMapOverlay::anim_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<PeerMapOverlay*>(lv_timer_get_user_data(timer));
    if (self == nullptr || self->root_ == nullptr) return;
    const double elapsed = (monotonic_ms() - self->anim_start_ms_) / 1000.0;
    self->draw_frame(elapsed);
}
