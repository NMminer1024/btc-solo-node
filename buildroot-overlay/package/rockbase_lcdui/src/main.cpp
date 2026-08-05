#include "display.h"

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#include <thead.h>

#include "lvgl/lvgl.h"

namespace {

constexpr uint32_t kUiWidth = 800;
constexpr uint32_t kUiHeight = 480;
constexpr uint32_t kFramePixels = kUiWidth * kUiHeight;
constexpr uint32_t kTileCount = 4;
constexpr const char* kBacklightGpioChip = "/dev/gpiochip0";
constexpr uint32_t kBacklightGpioLine = 25;
constexpr uint32_t kBacklightFadeMs = 1000;
constexpr uint32_t kBootLogoHoldMs = 3000;
constexpr uint32_t kDashboardFadeMs = 500;
constexpr const char* kBootLogoRawPath = "/usr/share/rockbase-lcdui/boot-logo-800x480.argb";
constexpr const char* kDashboardRawPath = "/usr/share/rockbase-lcdui/bitcoin_dashboard_800x480.argb";
constexpr const char* kStatusCachePath = "/tmp/status-api.json";
volatile sig_atomic_t g_stop = 0;
display* g_display = nullptr;
display_plane* g_plane = nullptr;
display_buffer* g_buffer = nullptr;
int g_backlight_fd = -1;
void* g_lv_draw_buf = nullptr;

struct RawImage {
    std::vector<uint8_t> data;
    lv_image_dsc_t dsc {};
    bool ready = false;
};

struct TouchInput {
    int fd = -1;
    int raw_x = 0;
    int raw_y = 0;
    int min_x = 0;
    int max_x = 479;
    int min_y = 0;
    int max_y = 799;
    bool down = false;
};

struct TileRuntime {
    lv_obj_t* tileview = nullptr;
    lv_obj_t* active_label = nullptr;
    lv_obj_t* clock_time_label = nullptr;
    lv_obj_t* clock_date_label = nullptr;
    lv_obj_t* bitcoin_ip_label = nullptr;
    lv_obj_t* bitcoin_sync = nullptr;
    lv_obj_t* bitcoin_progress = nullptr;
    lv_obj_t* bitcoin_height = nullptr;
    lv_obj_t* bitcoin_peers = nullptr;
    lv_obj_t* bitcoin_difficulty = nullptr;
    lv_obj_t* bitcoin_rss = nullptr;
    lv_obj_t* bitcoin_block_date = nullptr;
    lv_obj_t* stratum_state = nullptr;
    lv_obj_t* stratum_url = nullptr;
    lv_obj_t* stratum_user = nullptr;
    lv_obj_t* stratum_mode = nullptr;
    lv_obj_t* system_load = nullptr;
    lv_obj_t* system_mem = nullptr;
    lv_obj_t* system_disk = nullptr;
    lv_obj_t* system_temp = nullptr;
    lv_obj_t* system_uptime = nullptr;
    lv_obj_t* system_swap = nullptr;
    lv_obj_t* network_ip = nullptr;
    lv_obj_t* network_ssid = nullptr;
    lv_obj_t* network_rssi = nullptr;
    lv_obj_t* network_ntp = nullptr;
    lv_obj_t* network_mac = nullptr;
    lv_obj_t* network_reach = nullptr;
};

struct TileSpec {
    const char* title;
    const char* subtitle;
    uint32_t bg;
    uint32_t accent;
    bool use_dashboard_bg;
};

struct DashboardData {
    std::string sync_key = "-";
    std::string sync_detail = "-";
    std::string progress_ring = "-";
    std::string blocks_fmt = "-";
    std::string headers_fmt = "-";
    std::string best_block_date = "-";
    std::string btc_context = "-";
    std::string connections = "-";
    std::string difficulty_display = "-";
    std::string rss = "-";
    std::string ckpool_state = "-";
    std::string ckpool_class = "-";
    std::string stratum_url = "-";
    std::string user_hint = "-";
    std::string recommended_mode = "-";
    std::string btc_address = "-";
    std::string load_display = "-";
    std::string load_avg_display = "-";
    std::string mem_used_display = "-";
    std::string mem_total_display = "-";
    std::string disk_size_info = "-";
    std::string swap_used_display = "-";
    std::string swap_total_display = "-";
    std::string temp = "-";
    std::string uptime = "-";
    std::string ip_addr = "-";
    std::string wifi_ssid = "-";
    std::string rssi_display = "-";
    std::string ntp_key = "-";
    std::string mac_addr = "-";
    std::string stratum_reach = "-";
};

constexpr TileSpec kTiles[kTileCount] = {
    {"Bitcoind", "Local Bitcoin node overview", 0x0a141b, 0xf0b44c, true},
    {"Stratum", "Pool and miner session placeholder", 0x101418, 0x51c7a3, false},
    {"System", "CPU, memory, disk and process placeholder", 0x13151e, 0x8fb7ff, false},
    {"Network", "WiFi, LAN and peer status placeholder", 0x101719, 0xff8c64, false},
};

TouchInput g_touch;
TileRuntime g_tiles;
RawImage g_boot_logo;
RawImage g_dashboard_bg;

uint32_t tick_ms() {
    timeval tv {};
    gettimeofday(&tv, nullptr);
    return static_cast<uint32_t>(tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL);
}

void handle_signal(int) {
    g_stop = 1;
}

bool set_backlight(int value) {
    if (g_backlight_fd < 0) {
        return false;
    }

    gpio_v2_line_values values {};
    values.mask = 1;
    values.bits = value ? 1 : 0;
    if (ioctl(g_backlight_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
        std::fprintf(stderr, "rockbase-lcdui: backlight gpiochip line set failed: %s\n", std::strerror(errno));
        return false;
    }

    return true;
}

bool init_backlight_gpio(int initial_value) {
    int chip_fd = open(kBacklightGpioChip, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0) {
        std::fprintf(stderr, "rockbase-lcdui: open %s failed: %s\n", kBacklightGpioChip, std::strerror(errno));
        return false;
    }

    gpio_v2_line_request request {};
    request.offsets[0] = kBacklightGpioLine;
    request.num_lines = 1;
    request.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    request.config.num_attrs = 1;
    request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    request.config.attrs[0].attr.values = initial_value ? 1 : 0;
    request.config.attrs[0].mask = 1;
    std::snprintf(request.consumer, sizeof(request.consumer), "rockbase-lcdui");

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request) < 0) {
        std::fprintf(stderr, "rockbase-lcdui: request %s line %u failed: %s\n",
                     kBacklightGpioChip, kBacklightGpioLine, std::strerror(errno));
        close(chip_fd);
        return false;
    }
    close(chip_fd);

    g_backlight_fd = request.fd;
    std::printf("rockbase-lcdui: backlight=%s line=%u initial=%d\n",
                kBacklightGpioChip, kBacklightGpioLine, initial_value);
    return true;
}

void fade_backlight_in() {
    constexpr int steps = 20;
    constexpr int period_us = 1000;
    const int step_ms = kBacklightFadeMs / steps;

    for (int step = 0; step <= steps && !g_stop; ++step) {
        const int cycles = (step_ms * 1000) / period_us;
        const int on_us = (period_us * step) / steps;
        const int off_us = period_us - on_us;
        for (int cycle = 0; cycle < cycles && !g_stop; ++cycle) {
            if (on_us > 0) {
                set_backlight(1);
                usleep(on_us);
            }
            if (off_us > 0) {
                set_backlight(0);
                usleep(off_us);
            }
        }
    }

    set_backlight(1);
}

void run_lv_for(uint32_t ms) {
    const uint32_t end = tick_ms() + ms;
    while (!g_stop && static_cast<int32_t>(end - tick_ms()) > 0) {
        lv_timer_handler();
        usleep(5000);
    }
}

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (g_buffer == nullptr || g_buffer->map == nullptr || area == nullptr || px_map == nullptr) {
        lv_display_flush_ready(disp);
        return;
    }

    const uint32_t area_w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    const auto* src = reinterpret_cast<const uint32_t*>(px_map);
    auto* dst = static_cast<uint32_t*>(g_buffer->map);
    const uint32_t dst_stride_px = g_buffer->stride / sizeof(uint32_t);

    for (int32_t sy = area->y1; sy <= area->y2; ++sy) {
        if (sy < 0 || sy >= static_cast<int32_t>(kUiHeight)) {
            continue;
        }
        for (int32_t sx = area->x1; sx <= area->x2; ++sx) {
            if (sx < 0 || sx >= static_cast<int32_t>(kUiWidth)) {
                continue;
            }

            const uint32_t src_x = static_cast<uint32_t>(sx - area->x1);
            const uint32_t src_y = static_cast<uint32_t>(sy - area->y1);
            const uint32_t pixel = src[src_y * area_w + src_x];

            /* 800x480 logical landscape -> 480x800 physical portrait. */
            const uint32_t dx = kUiHeight - 1U - static_cast<uint32_t>(sy);
            const uint32_t dy = static_cast<uint32_t>(sx);
            if (dx < g_buffer->width && dy < g_buffer->height) {
                dst[dy * dst_stride_px + dx] = pixel;
            }
        }
    }

    thead_csi_dcache_clean_invalid_range(g_buffer->map, g_buffer->size);
    display_wait_vsync(g_display);
    if (display_update_buffer(g_buffer, 0, 0) != 0 || display_commit(g_display) != 0) {
        std::fprintf(stderr, "rockbase-lcdui: display commit failed\n");
    }

    lv_display_flush_ready(disp);
}

bool commit_display_buffer() {
    thead_csi_dcache_clean_invalid_range(g_buffer->map, g_buffer->size);
    display_wait_vsync(g_display);
    if (display_update_buffer(g_buffer, 0, 0) != 0 || display_commit(g_display) != 0) {
        std::fprintf(stderr, "rockbase-lcdui: display commit failed\n");
        return false;
    }
    return true;
}

bool load_argb_image(const char* path, RawImage& image) {
    image.data.assign(kFramePixels * sizeof(uint32_t), 0);
    image.ready = false;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "rockbase-lcdui: open %s failed\n", path);
        return false;
    }

    uint8_t* dst = image.data.data();
    size_t remaining = image.data.size();
    while (remaining > 0) {
        const ssize_t n = read(fd, dst, remaining);
        if (n <= 0) {
            close(fd);
            std::fprintf(stderr, "rockbase-lcdui: read %s failed\n", path);
            return false;
        }
        dst += n;
        remaining -= static_cast<size_t>(n);
    }

    close(fd);
    std::memset(&image.dsc, 0, sizeof(image.dsc));
    image.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    image.dsc.header.w = kUiWidth;
    image.dsc.header.h = kUiHeight;
    image.dsc.header.stride = kUiWidth * sizeof(uint32_t);
    image.dsc.data_size = static_cast<uint32_t>(image.data.size());
    image.dsc.data = image.data.data();
    image.ready = true;
    return true;
}

int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

bool read_text_file(const char* path, std::string& out, size_t max_size = 65536) {
    out.clear();
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    char buf[4096];
    while (out.size() < max_size) {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            close(fd);
            return false;
        }
        if (n == 0) {
            break;
        }
        const size_t room = max_size - out.size();
        out.append(buf, static_cast<size_t>(n) > room ? room : static_cast<size_t>(n));
    }

    close(fd);
    return !out.empty();
}

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

std::string json_value(const std::string& object, const char* key, const char* fallback = "-") {
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

std::string join_value(const std::string& left, const char* sep, const std::string& right, const char* fallback = "-") {
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

std::string sync_label(const std::string& key) {
    if (key == "pre_headers") return "Pre-sync blockheaders";
    if (key == "sync_headers") return "Synchronizing blockheaders";
    if (key == "sync_blocks") return "Synchronizing blocks";
    if (key == "loading_index") return "Loading block index";
    if (key == "loading_blocks") return "Loading block files";
    if (key == "verifying_blocks") return "Verifying blocks";
    if (key == "rewinding_blocks") return "Rewinding blocks";
    if (key == "pruning") return "Pruning blocks";
    if (key == "synced") return "Synced";
    if (key == "btc_not_running") return "Not Running";
    return "IBD Syncing";
}

std::string ntp_label(const std::string& key) {
    if (key == "running") {
        return "NTP Ready";
    }
    return "NTP Unsynced";
}

std::string current_time_text(const char* fmt) {
    char buf[32];
    const time_t now = time(nullptr);
    tm local {};
    localtime_r(&now, &local);
    std::strftime(buf, sizeof(buf), fmt, &local);
    return buf;
}

bool load_dashboard_data(DashboardData& data) {
    std::string json;
    if (!read_text_file(kStatusCachePath, json)) {
        return false;
    }

    const std::string bitcoin = json_object(json, "bitcoin");
    const std::string mining = json_object(json, "mining");
    const std::string system = json_object(json, "system");
    const std::string network = json_object(json, "network");

    data.sync_key = json_value(bitcoin, "sync_key");
    data.sync_detail = json_value(bitcoin, "sync_detail");
    data.progress_ring = json_value(bitcoin, "progress_ring");
    data.blocks_fmt = json_value(bitcoin, "blocks_fmt");
    data.headers_fmt = json_value(bitcoin, "headers_fmt");
    data.best_block_date = json_value(bitcoin, "best_block_date");
    data.btc_context = json_value(bitcoin, "btc_context");
    data.connections = json_value(bitcoin, "connections");
    data.difficulty_display = json_value(bitcoin, "difficulty_display");
    data.rss = json_value(bitcoin, "rss");

    data.ckpool_state = json_value(mining, "ckpool_state");
    data.ckpool_class = json_value(mining, "ckpool_class");
    data.stratum_url = json_value(mining, "stratum_url");
    data.user_hint = json_value(mining, "user_hint");
    data.recommended_mode = json_value(mining, "recommended_mode");
    data.btc_address = json_value(mining, "btc_address");

    data.load_display = json_value(system, "load_display");
    data.load_avg_display = json_value(system, "load_avg_display");
    data.mem_used_display = json_value(system, "mem_used_display");
    data.mem_total_display = json_value(system, "mem_total_display");
    data.disk_size_info = json_value(system, "disk_size_info");
    data.swap_used_display = json_value(system, "swap_used_display");
    data.swap_total_display = json_value(system, "swap_total_display");
    data.temp = json_value(system, "temp");
    data.uptime = json_value(system, "uptime");

    data.ip_addr = json_value(network, "ip_addr");
    data.wifi_ssid = json_value(network, "wifi_ssid");
    data.rssi_display = json_value(network, "rssi_display");
    data.ntp_key = json_value(network, "ntp_key");
    data.mac_addr = json_value(network, "mac_addr");
    data.stratum_reach = json_value(network, "stratum_reach");
    return true;
}

bool init_display() {
    g_display = display_init(0);
    if (g_display == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: display_init failed\n");
        return false;
    }

    if (g_display->conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
        std::fprintf(stderr, "rockbase-lcdui: connector is not DSI\n");
        return false;
    }

    g_display->drm_rotation = rotation_0;
    g_plane = display_get_plane(g_display, DRM_FORMAT_ARGB8888);
    if (g_plane == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: ARGB8888 plane unavailable\n");
        return false;
    }

    g_plane->drm_rotation = rotation_0;
    g_buffer = display_allocate_buffer(g_plane, g_display->width, g_display->height);
    if (g_buffer == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: display buffer allocation failed\n");
        return false;
    }

    auto* pixels = static_cast<uint32_t*>(g_buffer->map);
    const uint32_t pixel_count = g_buffer->size / sizeof(uint32_t);
    for (uint32_t i = 0; i < pixel_count; ++i) {
        pixels[i] = 0xff000000;
    }
    std::printf("rockbase-lcdui: panel=%ux%u buffer=%ux%u stride=%u size=%u fourcc=ARGB8888 rotation=software-ccw\n",
                g_display->width, g_display->height, g_buffer->width, g_buffer->height,
                g_buffer->stride, g_buffer->size);
    return true;
}

void cleanup_display() {
    if (g_touch.fd >= 0) {
        close(g_touch.fd);
        g_touch.fd = -1;
    }
    if (g_lv_draw_buf != nullptr) {
        std::free(g_lv_draw_buf);
        g_lv_draw_buf = nullptr;
    }
    if (g_backlight_fd >= 0) {
        close(g_backlight_fd);
        g_backlight_fd = -1;
    }
    if (g_buffer != nullptr) {
        display_free_buffer(g_buffer);
        g_buffer = nullptr;
    }
    if (g_plane != nullptr) {
        display_free_plane(g_plane);
        g_plane = nullptr;
    }
    if (g_display != nullptr) {
        display_exit(g_display);
        g_display = nullptr;
    }
}

bool init_lv_display(lv_display_t** out_disp) {
    lv_init();
    lv_tick_set_cb(tick_ms);

    lv_display_t* disp = lv_display_create(kUiWidth, kUiHeight);
    if (disp == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: lv_display_create failed\n");
        return false;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_flush_cb(disp, flush_cb);

    const size_t draw_buf_size = kUiWidth * kUiHeight * sizeof(uint32_t);
    g_lv_draw_buf = std::malloc(draw_buf_size);
    if (g_lv_draw_buf == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: draw buffer allocation failed\n");
        return false;
    }
    lv_display_set_buffers(disp, g_lv_draw_buf, nullptr, draw_buf_size, LV_DISPLAY_RENDER_MODE_FULL);
    *out_disp = disp;
    return true;
}

bool touch_abs_info(int fd, int code, int* min_value, int* max_value) {
    input_absinfo abs {};
    if (ioctl(fd, EVIOCGABS(code), &abs) < 0) {
        return false;
    }
    *min_value = abs.minimum;
    *max_value = abs.maximum;
    return abs.maximum > abs.minimum;
}

int open_touch_device() {
    DIR* dir = opendir("/dev/input");
    if (dir == nullptr) {
        return -1;
    }

    int result = -1;
    dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::strncmp(ent->d_name, "event", 5) != 0) {
            continue;
        }

        char path[64];
        std::snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }

        char name[128] {};
        ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name);
        int min_x = 0;
        int max_x = 0;
        int min_y = 0;
        int max_y = 0;
        const bool has_mt = touch_abs_info(fd, ABS_MT_POSITION_X, &min_x, &max_x) &&
                            touch_abs_info(fd, ABS_MT_POSITION_Y, &min_y, &max_y);
        const bool has_abs = touch_abs_info(fd, ABS_X, &min_x, &max_x) &&
                             touch_abs_info(fd, ABS_Y, &min_y, &max_y);
        if (has_mt || has_abs) {
            g_touch.min_x = min_x;
            g_touch.max_x = max_x;
            g_touch.min_y = min_y;
            g_touch.max_y = max_y;
            result = fd;
            std::printf("rockbase-lcdui: touch=%s name=\"%s\" x=%d..%d y=%d..%d\n",
                        path, name, min_x, max_x, min_y, max_y);
            break;
        }

        close(fd);
    }

    closedir(dir);
    return result;
}

void touch_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    input_event ev {};
    while (g_touch.fd >= 0) {
        const ssize_t n = read(g_touch.fd, &ev, sizeof(ev));
        if (n != static_cast<ssize_t>(sizeof(ev))) {
            break;
        }

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X || ev.code == ABS_MT_POSITION_X) {
                g_touch.raw_x = ev.value;
            }
            else if (ev.code == ABS_Y || ev.code == ABS_MT_POSITION_Y) {
                g_touch.raw_y = ev.value;
            }
            else if (ev.code == ABS_MT_TRACKING_ID) {
                g_touch.down = ev.value >= 0;
            }
        }
        else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            g_touch.down = ev.value != 0;
        }
    }

    const int range_x = g_touch.max_x - g_touch.min_x;
    const int range_y = g_touch.max_y - g_touch.min_y;
    const int phys_x = range_x > 0 ? ((g_touch.raw_x - g_touch.min_x) * static_cast<int>(kUiHeight - 1U)) / range_x : g_touch.raw_x;
    const int phys_y = range_y > 0 ? ((g_touch.raw_y - g_touch.min_y) * static_cast<int>(kUiWidth - 1U)) / range_y : g_touch.raw_y;

    data->point.x = clamp_int(phys_y, 0, static_cast<int>(kUiWidth - 1U));
    data->point.y = clamp_int(static_cast<int>(kUiHeight - 1U) - phys_x, 0, static_cast<int>(kUiHeight - 1U));
    data->state = g_touch.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void init_touch_input(lv_display_t* disp) {
    g_touch.fd = open_touch_device();
    if (g_touch.fd < 0) {
        std::fprintf(stderr, "rockbase-lcdui: no touch input device found; tile swipe disabled\n");
        return;
    }

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, disp);
    lv_indev_set_read_cb(indev, touch_read_cb);
}

lv_obj_t* add_label(lv_obj_t* parent, const char* text, int32_t x, int32_t y,
                    const lv_font_t* font, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

void make_static(lv_obj_t* obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t* create_card(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                      const char* title, const char* value, lv_color_t accent) {
    lv_obj_t* card = lv_obj_create(parent);
    make_static(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_80, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    lv_obj_set_style_pad_all(card, 16, 0);

    add_label(card, title, 0, 0, &lv_font_montserrat_16, lv_color_hex(0x9aa6ac));
    lv_obj_t* value_label = add_label(card, value, 0, 34, &lv_font_montserrat_22, lv_color_hex(0xf4efe2));
    lv_obj_set_width(value_label, w - 32);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_DOTS);
    return value_label;
}

void set_label(lv_obj_t* label, const std::string& value) {
    if (label != nullptr) {
        lv_label_set_text(label, value.c_str());
    }
}

void set_label_fmt(lv_obj_t* label, const char* fmt, const std::string& value) {
    if (label != nullptr) {
        lv_label_set_text_fmt(label, fmt, value.c_str());
    }
}

void style_tile(lv_obj_t* tile, lv_color_t bg, lv_color_t accent) {
    /* Do not remove all styles from a tileview tile: LVGL stores tile size/pos as local style props. */
    lv_obj_set_style_bg_color(tile, bg, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t* band = lv_obj_create(tile);
    make_static(band);
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, 800, 480);
    lv_obj_set_style_bg_color(band, accent, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_20, 0);
    lv_obj_set_style_border_width(band, 0, 0);
}

void add_page_header(lv_obj_t* tile, const char* title, const char* subtitle, lv_color_t accent) {
    add_label(tile, title, 36, 30, &lv_font_montserrat_36, accent);
    add_label(tile, subtitle, 38, 82, &lv_font_montserrat_18, lv_color_hex(0xb7c2c8));
}

void add_page_dots(lv_obj_t* tile, uint32_t active) {
    for (uint32_t i = 0; i < kTileCount; ++i) {
        lv_obj_t* dot = lv_obj_create(tile);
        make_static(dot);
        lv_obj_set_size(dot, i == active ? 30 : 10, 10);
        lv_obj_set_pos(dot, 348 + static_cast<int32_t>(i) * 28, 438);
        lv_obj_set_style_radius(dot, 5, 0);
        lv_obj_set_style_bg_color(dot, i == active ? lv_color_hex(0xf0b44c) : lv_color_hex(0x60707a), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
    }
}

lv_dir_t tile_dir_for(uint32_t col) {
    if (col == 0) {
        return LV_DIR_RIGHT;
    }
    if (col + 1U == kTileCount) {
        return LV_DIR_LEFT;
    }
    return static_cast<lv_dir_t>(LV_DIR_LEFT | LV_DIR_RIGHT);
}

void add_dashboard_background(lv_obj_t* tile) {
    if (!g_dashboard_bg.ready) {
        return;
    }

    lv_obj_t* img = lv_image_create(tile);
    lv_obj_remove_style_all(img);
    lv_image_set_src(img, &g_dashboard_bg.dsc);
    lv_obj_set_pos(img, 0, 0);
    lv_obj_set_size(img, kUiWidth, kUiHeight);
    lv_obj_move_to_index(img, 0);
}

lv_obj_t* create_tile_base(lv_obj_t* tv, uint8_t col, const TileSpec& spec) {
    const lv_color_t bg = lv_color_hex(spec.bg);
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = lv_tileview_add_tile(tv, col, 0, tile_dir_for(col));
    lv_obj_set_size(tile, kUiWidth, kUiHeight);
    lv_obj_set_pos(tile, static_cast<int32_t>(col) * static_cast<int32_t>(kUiWidth), 0);
    style_tile(tile, bg, accent);
    if (spec.use_dashboard_bg) {
        add_dashboard_background(tile);
    }
    add_page_header(tile, spec.title, spec.subtitle, accent);
    add_page_dots(tile, col);
    return tile;
}

void build_bitcoin_tile(lv_obj_t* tv) {
    const TileSpec& spec = kTiles[0];
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = create_tile_base(tv, 0, spec);

    g_tiles.clock_time_label = add_label(tile, "--:--:--", 538, 26, &lv_font_montserrat_36, lv_color_hex(0xf4efe2));
    lv_obj_set_width(g_tiles.clock_time_label, 220);
    lv_obj_set_style_text_align(g_tiles.clock_time_label, LV_TEXT_ALIGN_LEFT, 0);
    g_tiles.clock_date_label = add_label(tile, "----/--/--", 540, 72, &lv_font_montserrat_16, lv_color_hex(0x9aa6ac));
    lv_obj_set_width(g_tiles.clock_date_label, 180);
    lv_obj_set_style_text_align(g_tiles.clock_date_label, LV_TEXT_ALIGN_LEFT, 0);
    g_tiles.bitcoin_ip_label = add_label(tile, "IP --", 538, 112, &lv_font_montserrat_16, lv_color_hex(0xb7c2c8));
    lv_obj_set_width(g_tiles.bitcoin_ip_label, 220);
    lv_label_set_long_mode(g_tiles.bitcoin_ip_label, LV_LABEL_LONG_MODE_DOTS);
    g_tiles.bitcoin_sync = create_card(tile, 42, 142, 210, 102, "Sync", "--", accent);
    g_tiles.bitcoin_progress = create_card(tile, 294, 142, 180, 102, "Progress", "--", accent);
    g_tiles.bitcoin_height = create_card(tile, 516, 142, 240, 102, "Blocks / Headers", "--", accent);
    g_tiles.bitcoin_peers = create_card(tile, 42, 276, 104, 100, "Peers", "--", accent);
    g_tiles.bitcoin_difficulty = create_card(tile, 164, 276, 106, 100, "Difficulty", "--", accent);
    g_tiles.bitcoin_rss = create_card(tile, 288, 276, 130, 100, "RSS", "--", accent);
    g_tiles.bitcoin_block_date = create_card(tile, 436, 276, 320, 100, "Block Time", "--", accent);
}

void build_stratum_tile(lv_obj_t* tv) {
    const TileSpec& spec = kTiles[1];
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = create_tile_base(tv, 1, spec);

    g_tiles.stratum_state = create_card(tile, 42, 148, 210, 112, "ckpool", "--", accent);
    g_tiles.stratum_url = create_card(tile, 294, 148, 462, 112, "Stratum URL", "--", accent);
    g_tiles.stratum_user = create_card(tile, 42, 294, 336, 104, "Worker / Wallet", "--", accent);
    g_tiles.stratum_mode = create_card(tile, 422, 294, 334, 104, "Mode", "--", accent);
}

void build_system_tile(lv_obj_t* tv) {
    const TileSpec& spec = kTiles[2];
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = create_tile_base(tv, 2, spec);

    g_tiles.system_load = create_card(tile, 42, 148, 210, 112, "Load", "--", accent);
    g_tiles.system_mem = create_card(tile, 294, 148, 210, 112, "Memory", "--", accent);
    g_tiles.system_disk = create_card(tile, 546, 148, 210, 112, "Block Disk", "--", accent);
    g_tiles.system_temp = create_card(tile, 42, 294, 154, 104, "Temp", "--", accent);
    g_tiles.system_uptime = create_card(tile, 218, 294, 286, 104, "Uptime", "--", accent);
    g_tiles.system_swap = create_card(tile, 526, 294, 230, 104, "Swap", "--", accent);
}

void build_network_tile(lv_obj_t* tv) {
    const TileSpec& spec = kTiles[3];
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = create_tile_base(tv, 3, spec);

    g_tiles.network_ip = create_card(tile, 42, 148, 210, 112, "IP", "--", accent);
    g_tiles.network_ssid = create_card(tile, 294, 148, 210, 112, "WiFi SSID", "--", accent);
    g_tiles.network_rssi = create_card(tile, 546, 148, 210, 112, "Signal", "--", accent);
    g_tiles.network_ntp = create_card(tile, 42, 294, 154, 104, "NTP", "--", accent);
    g_tiles.network_mac = create_card(tile, 218, 294, 286, 104, "MAC", "--", accent);
    g_tiles.network_reach = create_card(tile, 526, 294, 230, 104, "Stratum Reach", "--", accent);
}

void tile_event_cb(lv_event_t* e) {
    lv_obj_t* tv = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_obj_t* active = lv_tileview_get_tile_active(tv);
    if (active == nullptr || g_tiles.active_label == nullptr) {
        return;
    }

    const int32_t x = lv_obj_get_x(active);
    uint32_t page = static_cast<uint32_t>((x + static_cast<int32_t>(kUiWidth / 2U)) / static_cast<int32_t>(kUiWidth));
    if (page >= kTileCount) {
        page = kTileCount - 1U;
    }
    lv_label_set_text_fmt(g_tiles.active_label, "%s  %u/%u",
                          kTiles[page].title, page + 1U, kTileCount);
}

void refresh_clock_cb(lv_timer_t*) {
    set_label(g_tiles.clock_time_label, current_time_text("%H:%M:%S"));
    set_label(g_tiles.clock_date_label, current_time_text("%Y-%m-%d"));
}

void refresh_status_cb(lv_timer_t*) {
    static bool logged_first_status = false;
    DashboardData data;
    if (!load_dashboard_data(data)) {
        set_label(g_tiles.bitcoin_sync, "status cache missing");
        return;
    }

    const std::string blocks = join_value(data.blocks_fmt, " / ", data.headers_fmt);
    const std::string progress = data.progress_ring != "-" ? data.progress_ring + "%" : "-";
    const std::string mem = join_value(data.mem_used_display, " / ", data.mem_total_display);
    const std::string swap = join_value(data.swap_used_display, " / ", data.swap_total_display);
    const std::string load = join_value(data.load_display, " | ", data.load_avg_display);

    set_label(g_tiles.bitcoin_sync, sync_label(data.sync_key));
    set_label(g_tiles.bitcoin_progress, progress);
    set_label(g_tiles.bitcoin_height, blocks);
    set_label(g_tiles.bitcoin_peers, data.connections);
    set_label(g_tiles.bitcoin_difficulty, data.difficulty_display);
    set_label(g_tiles.bitcoin_rss, data.rss);
    set_label(g_tiles.bitcoin_block_date, data.best_block_date);
    set_label(g_tiles.bitcoin_ip_label, "IP " + join_value(data.ip_addr, " | ", data.btc_context));

    set_label(g_tiles.stratum_state, data.ckpool_state);
    set_label(g_tiles.stratum_url, data.stratum_url);
    set_label(g_tiles.stratum_user, data.btc_address != "-" ? data.btc_address : data.user_hint);
    set_label(g_tiles.stratum_mode, data.recommended_mode);

    set_label(g_tiles.system_load, load);
    set_label(g_tiles.system_mem, mem);
    set_label(g_tiles.system_disk, data.disk_size_info);
    set_label(g_tiles.system_temp, data.temp);
    set_label(g_tiles.system_uptime, data.uptime);
    set_label(g_tiles.system_swap, swap);

    set_label(g_tiles.network_ip, data.ip_addr);
    set_label(g_tiles.network_ssid, data.wifi_ssid);
    set_label(g_tiles.network_rssi, data.rssi_display);
    set_label(g_tiles.network_ntp, ntp_label(data.ntp_key));
    set_label(g_tiles.network_mac, data.mac_addr);
    set_label(g_tiles.network_reach, data.stratum_reach);

    if (!logged_first_status) {
        std::printf("rockbase-lcdui: status loaded ip=%s sync=%s blocks=%s headers=%s\n",
                    data.ip_addr.c_str(), data.sync_key.c_str(), data.blocks_fmt.c_str(), data.headers_fmt.c_str());
        std::fflush(stdout);
        logged_first_status = true;
    }
}

void build_tile_stage(lv_obj_t* screen) {
    lv_obj_t* tv = lv_tileview_create(screen);
    lv_obj_set_size(tv, kUiWidth, kUiHeight);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x070b10), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(tv, tile_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    g_tiles.tileview = tv;

    build_bitcoin_tile(tv);
    build_stratum_tile(tv);
    build_system_tile(tv);
    build_network_tile(tv);

    g_tiles.active_label = add_label(screen, "Bitcoind  1/4", 636, 438, &lv_font_montserrat_16, lv_color_hex(0xe9d6a6));
    lv_tileview_set_tile_by_index(tv, 0, 0, LV_ANIM_OFF);
    refresh_clock_cb(nullptr);
    refresh_status_cb(nullptr);
    lv_timer_create(refresh_clock_cb, 1000, nullptr);
    lv_timer_create(refresh_status_cb, 5000, nullptr);
}

lv_obj_t* build_boot_scene(lv_obj_t* screen) {
    if (g_boot_logo.ready) {
        lv_obj_t* img = lv_image_create(screen);
        lv_obj_remove_style_all(img);
        lv_image_set_src(img, &g_boot_logo.dsc);
        lv_obj_set_pos(img, 0, 0);
        lv_obj_set_size(img, kUiWidth, kUiHeight);
        make_static(img);
        return img;
    }

    lv_obj_t* layer = lv_obj_create(screen);
    lv_obj_remove_style_all(layer);
    make_static(layer);
    lv_obj_set_size(layer, kUiWidth, kUiHeight);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_style_bg_color(layer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, 0);
    add_label(layer, "NM", 344, 172, &lv_font_montserrat_36, lv_color_hex(0xf0b44c));
    add_label(layer, "Rockbase", 318, 226, &lv_font_montserrat_22, lv_color_hex(0xf4efe2));
    return layer;
}

}  // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const bool backlight_ready = init_backlight_gpio(0);

    if (!init_display()) {
        cleanup_display();
        return 1;
    }

    load_argb_image(kBootLogoRawPath, g_boot_logo);
    load_argb_image(kDashboardRawPath, g_dashboard_bg);

    lv_display_t* disp = nullptr;
    if (!init_lv_display(&disp)) {
        cleanup_display();
        return 1;
    }
    init_touch_input(disp);

    lv_obj_t* screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_t* boot_layer = build_boot_scene(screen);
    lv_refr_now(disp);
    if (backlight_ready) {
        fade_backlight_in();
    }
    run_lv_for(kBootLogoHoldMs);
    build_tile_stage(screen);
    lv_obj_move_to_index(boot_layer, lv_obj_get_child_count(screen) - 1);
    lv_obj_fade_out(boot_layer, kDashboardFadeMs, 0);
    run_lv_for(kDashboardFadeMs + 80);
    lv_obj_delete(boot_layer);
    lv_refr_now(disp);

    std::printf("rockbase-lcdui: running lvgl tile UI\n");
    std::fflush(stdout);

    while (!g_stop) {
        lv_timer_handler();
        usleep(20000);
    }

    cleanup_display();
    return 0;
}
