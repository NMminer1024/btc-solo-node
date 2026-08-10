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
#include <time.h>
#include <unistd.h>
#include <vector>

#include <thead.h>

#include "lvgl/lvgl.h"
#include "lvgl/draw/lv_snapshot.h"

namespace {

constexpr uint32_t kUiWidth = 800;
constexpr uint32_t kUiHeight = 480;
constexpr uint32_t kPageSwitchThresholdPx = kUiWidth / 10U;
constexpr uint32_t kFramePixels = kUiWidth * kUiHeight;
constexpr uint32_t kTileCount = 4;
constexpr uint32_t kBitcoinStageCount = 7;
constexpr const char* kBacklightGpioChip = "/dev/gpiochip0";
constexpr uint32_t kBacklightGpioLine = 25;
constexpr uint32_t kBacklightFadeMs = 1000;
constexpr uint32_t kBootLogoHoldMs = 3000;
constexpr uint32_t kDashboardFadeMs = 500;
constexpr uint32_t kTouchReadPeriodMs = 1;
constexpr uint32_t kPagerSnapAnimMs = 300;
constexpr uint32_t kMaxLvSleepUs = 2000;
constexpr uint32_t kMinLvSleepUs = 500;
constexpr uint32_t kDisplayRefrPeriodMs = 10;
constexpr uint32_t kPresentIntervalMs = 20;
constexpr const char* kBootLogoRawPath = "/usr/share/rockbase-lcdui/boot-logo-800x480.argb";
constexpr const char* kStatusCachePath = "/tmp/status-api.json";
volatile sig_atomic_t g_stop = 0;
display* g_display = nullptr;
display_plane* g_plane = nullptr;
display_buffer* g_buffer = nullptr;
display_buffer* g_display_buffers[3] {};
uint32_t g_render_buffer_index = 0;
uint32_t g_last_present_ms = 0;
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
    lv_obj_t* page[kTileCount] {};
    lv_draw_buf_t* page_snapshot[kTileCount] {};
    lv_obj_t* page_image[kTileCount] {};
    lv_obj_t* clock_time_label = nullptr;
    lv_obj_t* clock_date_label = nullptr;
    lv_obj_t* bitcoin_ip_label = nullptr;
    lv_obj_t* bitcoin_stage_dot[kBitcoinStageCount] {};
    lv_obj_t* bitcoin_stage_label[kBitcoinStageCount] {};
    lv_obj_t* bitcoin_progress = nullptr;
    lv_obj_t* bitcoin_height_title = nullptr;
    lv_obj_t* bitcoin_height_current = nullptr;
    lv_obj_t* bitcoin_height_sep = nullptr;
    lv_obj_t* bitcoin_height_total = nullptr;
    lv_obj_t* bitcoin_height = nullptr;
    lv_obj_t* bitcoin_peers = nullptr;
    lv_obj_t* bitcoin_block_date_title = nullptr;
    lv_obj_t* bitcoin_block_date = nullptr;
    lv_obj_t* bitcoin_block_time = nullptr;
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
    std::string ibd_stage_key = "-";
    std::string ibd_stage_label = "-";
    std::string ibd_stage_detail = "-";
    std::string ibd_stage_height = "-";
    std::string ibd_stage_pct = "";
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

struct HeightCardModel {
    std::string title = "Blocks / Header";
    std::string value = "--";
    bool split_pair = false;
};

struct BitcoinPageModel {
    uint32_t stage_index = 0;
    std::string progress = "--";
    HeightCardModel height;
    std::string peers = "--";
    std::string block_date = "--";
    std::string block_time;
    std::string ip_addr = "--";
    std::string log_sync_key = "-";
    std::string log_blocks = "-";
    std::string log_headers = "-";
};

struct StratumPageModel {
    std::string state = "--";
    std::string url = "--";
    std::string user = "--";
    std::string mode = "--";
};

struct SystemPageModel {
    std::string load = "--";
    std::string mem = "--";
    std::string disk = "--";
    std::string temp = "--";
    std::string uptime = "--";
    std::string swap = "--";
};

struct NetworkPageModel {
    std::string ip = "--";
    std::string ssid = "--";
    std::string rssi = "--";
    std::string ntp = "--";
    std::string mac = "--";
    std::string reach = "--";
};

struct UiModel {
    bool status_available = false;
    BitcoinPageModel bitcoin;
    StratumPageModel stratum;
    SystemPageModel system;
    NetworkPageModel network;
};

constexpr uint32_t page_dirty_bit(uint32_t page) {
    return 1U << page;
}

constexpr TileSpec kTiles[kTileCount] = {
    {"Bitcoind", "Local Bitcoin solo node overview", 0x0a141b, 0xf0b44c, true},
    {"Stratum", "Pool and miner session placeholder", 0x101418, 0x51c7a3, true},
    {"System", "CPU, memory, disk and process placeholder", 0x13151e, 0x8fb7ff, true},
    {"Network", "WiFi, LAN and peer status placeholder", 0x101719, 0xff8c64, true},
};

constexpr const char* kPageBgRawPaths[kTileCount] = {
    "/usr/share/rockbase-lcdui/dashboard_bg_800x480.argb",
    "/usr/share/rockbase-lcdui/stratum_bg_800x480.argb",
    "/usr/share/rockbase-lcdui/system_bg_800x480.argb",
    "/usr/share/rockbase-lcdui/network_bg_800x480.argb",
};

constexpr const char* kBitcoinStageLabels[kBitcoinStageCount] = {
    "Starting",
    "Load Index",
    "Chainstate",
    "Header Pre-sync",
    "Header Sync",
    "Block Sync",
    "Synced",
};

TouchInput g_touch;
TileRuntime g_tiles;
RawImage g_boot_logo;
RawImage g_page_bg[kTileCount];
static uint32_t* g_tile_cache[kTileCount] = {};  // pre-rendered physical frames per tile
bool g_custom_frame_presenting = false;
uint32_t g_active_page = 0;
uint32_t g_swipe_start_page = 0;
uint32_t g_release_page = 0;
int32_t g_swipe_start_x = 0;
int32_t g_swipe_current_x = 0;
int32_t g_visual_scroll_x = 0;
bool g_pager_anim_active = false;
uint32_t g_pager_anim_start_ms = 0;
int32_t g_pager_anim_from_x = 0;
int32_t g_pager_anim_to_x = 0;
bool g_page_cache_dirty[kTileCount] {};
uint32_t g_last_page_cache_refresh_ms = 0;
uint32_t g_fps_window_start_ms = 0;
uint32_t g_fps_frame_count = 0;
bool g_clock_refresh_pending = false;
bool g_status_refresh_pending = false;

void set_active_page(uint32_t page);
void refresh_clock_cb(lv_timer_t*);
void refresh_status_cb(lv_timer_t*);
int present_display_buffer();

uint32_t tick_ms() {
    timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

bool ui_interacting(uint32_t now_ms = tick_ms()) {
    (void)now_ms;
    return g_touch.down || g_pager_anim_active;
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
        uint32_t next_ms = lv_timer_handler();
        const uint32_t now_ms = tick_ms();
        if (ui_interacting(now_ms)) {
            next_ms = 0;
        }
        uint32_t sleep_us = next_ms * 1000U;
        if (sleep_us < kMinLvSleepUs) {
            sleep_us = kMinLvSleepUs;
        }
        if (sleep_us > kMaxLvSleepUs) {
            sleep_us = kMaxLvSleepUs;
        }
        usleep(sleep_us);
    }
}

void draw_cached_indicator(uint32_t* dst, uint32_t stride_px) {
    constexpr int32_t kDotSize = 10;
    constexpr int32_t kDotStartX = 359;
    constexpr int32_t kDotStepX = 24;
    constexpr int32_t kDotY = 438;
    constexpr int32_t kDotRadiusSquared = 25;

    for (uint32_t index = 0; index < kTileCount; ++index) {
        const uint32_t color = index == g_active_page ? 0xfff0b44c : 0xff60707a;
        const int32_t dot_x = kDotStartX + static_cast<int32_t>(index) * kDotStepX;
        for (int32_t y = 0; y < kDotSize; ++y) {
            for (int32_t x = 0; x < kDotSize; ++x) {
                const int32_t dx = x - kDotSize / 2;
                const int32_t dy = y - kDotSize / 2;
                if (dx * dx + dy * dy >= kDotRadiusSquared) {
                    continue;
                }
                const uint32_t physical_x = kUiHeight - 1U - static_cast<uint32_t>(kDotY + y);
                const uint32_t physical_y = static_cast<uint32_t>(dot_x + x);
                dst[physical_y * stride_px + physical_x] = color;
            }
        }
    }
}

bool compose_page_caches(int32_t scroll_x) {
    if (g_buffer == nullptr) return false;
    if (scroll_x < 0) scroll_x = 0;
    const int32_t a = scroll_x / static_cast<int32_t>(kUiWidth);
    const int32_t off = scroll_x % static_cast<int32_t>(kUiWidth);
    if (off == 0 || a + 1 >= static_cast<int32_t>(kTileCount)) return false;
    if (g_tile_cache[a] == nullptr || g_tile_cache[a + 1] == nullptr) return false;
    auto* dst = static_cast<uint32_t*>(g_buffer->map);
    const uint32_t row_px = kUiHeight;  // physical pixels per row = 480
    const uint32_t n_a = static_cast<uint32_t>(kUiWidth - off);
    std::memcpy(dst,
                g_tile_cache[a] + static_cast<uint32_t>(off) * row_px,
                n_a * row_px * sizeof(uint32_t));
    std::memcpy(dst + n_a * row_px,
                g_tile_cache[a + 1],
                static_cast<uint32_t>(off) * row_px * sizeof(uint32_t));
    draw_cached_indicator(dst, g_buffer->stride / sizeof(uint32_t));
    return true;
}

int32_t clamp_scroll_x(int32_t scroll_x) {
    const int32_t max_scroll = static_cast<int32_t>((kTileCount - 1U) * kUiWidth);
    if (scroll_x < 0) {
        return 0;
    }
    if (scroll_x > max_scroll) {
        return max_scroll;
    }
    return scroll_x;
}

int32_t visual_scroll_x_from_touch() {
    const int32_t base = static_cast<int32_t>(g_swipe_start_page * kUiWidth);
    const int32_t dx = g_swipe_current_x - g_swipe_start_x;
    const int32_t target = clamp_scroll_x(base - dx);
    if (std::abs(target - g_visual_scroll_x) <= 1) {
        return g_visual_scroll_x;
    }
    g_visual_scroll_x = target;
    return g_visual_scroll_x;
}

bool present_cached_page(uint32_t page) {
    if (page >= kTileCount || g_buffer == nullptr || g_buffer->map == nullptr ||
        g_tile_cache[page] == nullptr) {
        return false;
    }

    auto* dst = static_cast<uint32_t*>(g_buffer->map);
    std::memcpy(dst, g_tile_cache[page], kUiHeight * kUiWidth * sizeof(uint32_t));
    draw_cached_indicator(dst, g_buffer->stride / sizeof(uint32_t));
    return present_display_buffer() == 0;
}

bool present_pager_frame(int32_t scroll_x) {
    scroll_x = clamp_scroll_x(scroll_x);
    const uint32_t page = static_cast<uint32_t>(scroll_x) / kUiWidth;
    const uint32_t off = static_cast<uint32_t>(scroll_x) % kUiWidth;
    if (off == 0) {
        return present_cached_page(page);
    }
    if (compose_page_caches(scroll_x)) {
        return present_display_buffer() == 0;
    }
    return false;
}

uint32_t ease_out_cubic_permille(uint32_t t_permille) {
    if (t_permille >= 1000U) {
        return 1000U;
    }
    const uint32_t inv = 1000U - t_permille;
    return 1000U - (inv * inv * inv) / 1000000U;
}

void start_pager_snap_anim(int32_t from_x, uint32_t target_page) {
    g_pager_anim_active = true;
    g_pager_anim_start_ms = tick_ms();
    g_pager_anim_from_x = clamp_scroll_x(from_x);
    g_pager_anim_to_x = static_cast<int32_t>(target_page * kUiWidth);
    g_visual_scroll_x = g_pager_anim_from_x;
}

bool present_pager_snap_anim(uint32_t now_ms) {
    if (!g_pager_anim_active) {
        return false;
    }

    const uint32_t elapsed = now_ms - g_pager_anim_start_ms;
    const uint32_t t = elapsed >= kPagerSnapAnimMs ? 1000U : (elapsed * 1000U) / kPagerSnapAnimMs;
    const uint32_t eased = ease_out_cubic_permille(t);
    const int32_t delta = g_pager_anim_to_x - g_pager_anim_from_x;
    const int32_t sx = g_pager_anim_from_x + static_cast<int32_t>((static_cast<int64_t>(delta) * eased) / 1000);
    g_visual_scroll_x = sx;

    const bool presented = present_pager_frame(sx);
    if (t >= 1000U) {
        if (g_tiles.tileview != nullptr) {
            lv_tileview_set_tile_by_index(g_tiles.tileview, static_cast<uint8_t>(g_release_page), 0, LV_ANIM_OFF);
            present_cached_page(g_release_page);
        }
        g_pager_anim_active = false;
        g_visual_scroll_x = g_pager_anim_to_x;
    }
    return presented;
}

bool build_tile_cache_from_snapshot(uint32_t index) {
    if (index >= kTileCount || g_tiles.page_snapshot[index] == nullptr || g_buffer == nullptr) {
        return false;
    }

    lv_draw_buf_t* snapshot = g_tiles.page_snapshot[index];
    if (snapshot->header.w != kUiWidth || snapshot->header.h != kUiHeight ||
        snapshot->header.stride < kUiWidth * sizeof(uint32_t)) {
        return false;
    }

    std::free(g_tile_cache[index]);
    g_tile_cache[index] = static_cast<uint32_t*>(std::malloc(kUiHeight * kUiWidth * sizeof(uint32_t)));
    if (g_tile_cache[index] == nullptr) {
        return false;
    }

    const auto* src = reinterpret_cast<const uint32_t*>(snapshot->data);
    auto* dst = g_tile_cache[index];
    const uint32_t src_stride_px = snapshot->header.stride / sizeof(uint32_t);
    constexpr uint32_t kRotateBlock = 16;
    for (uint32_t sy0 = 0; sy0 < kUiHeight; sy0 += kRotateBlock) {
        const uint32_t sy_end = sy0 + kRotateBlock < kUiHeight ? sy0 + kRotateBlock : kUiHeight;
        for (uint32_t sx0 = 0; sx0 < kUiWidth; sx0 += kRotateBlock) {
            const uint32_t sx_end = sx0 + kRotateBlock < kUiWidth ? sx0 + kRotateBlock : kUiWidth;
            for (uint32_t sy = sy0; sy < sy_end; ++sy) {
                const uint32_t* src_row = src + sy * src_stride_px + sx0;
                for (uint32_t sx = sx0; sx < sx_end; ++sx) {
                    dst[sx * kUiHeight + (kUiHeight - 1U - sy)] = *src_row++;
                }
            }
        }
    }
    return true;
}

void build_tile_caches_from_snapshots() {
    for (uint32_t index = 0; index < kTileCount; ++index) {
        if (!build_tile_cache_from_snapshot(index)) {
            std::fprintf(stderr, "rockbase-lcdui: cache build failed for page %u\n", index);
        }
    }
}

bool refresh_page_cache(uint32_t index) {
    if (index >= kTileCount || g_tiles.page[index] == nullptr ||
        g_tiles.page_image[index] == nullptr || g_tiles.page_snapshot[index] == nullptr) {
        return false;
    }

    lv_obj_t* page = g_tiles.page[index];
    lv_obj_t* image = g_tiles.page_image[index];
    const uint32_t child_count = lv_obj_get_child_count(page);
    lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
        lv_obj_t* child = lv_obj_get_child(page, child_index);
        if (child != image) {
            lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_draw_buf_t* snapshot = lv_snapshot_take(page, LV_COLOR_FORMAT_ARGB8888);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
        lv_obj_t* child = lv_obj_get_child(page, child_index);
        if (child != image) {
            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (snapshot == nullptr) {
        lv_obj_clear_flag(image, LV_OBJ_FLAG_HIDDEN);
        return false;
    }

    lv_draw_buf_t* old_snapshot = g_tiles.page_snapshot[index];
    g_tiles.page_snapshot[index] = snapshot;
    lv_image_set_src(image, snapshot);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_HIDDEN);
    lv_draw_buf_destroy(old_snapshot);
    if (!build_tile_cache_from_snapshot(index)) {
        return false;
    }

    if (index == g_active_page) {
        lv_obj_invalidate(page);
    }
    return true;
}

void refresh_one_dirty_page_if_idle() {
    const uint32_t now_ms = tick_ms();
    if (ui_interacting(now_ms) || now_ms - g_last_page_cache_refresh_ms < 150U) {
        return;
    }

    uint32_t page = g_active_page;
    if (!g_page_cache_dirty[page]) {
        for (uint32_t index = 0; index < kTileCount; ++index) {
            if (g_page_cache_dirty[index]) {
                page = index;
                break;
            }
        }
    }
    if (!g_page_cache_dirty[page]) {
        return;
    }

    if (refresh_page_cache(page)) {
        g_page_cache_dirty[page] = false;
        g_last_page_cache_refresh_ms = now_ms;
    }
}

void process_deferred_refreshes_if_idle() {
    if (ui_interacting()) {
        return;
    }
    if (g_clock_refresh_pending) {
        g_clock_refresh_pending = false;
        refresh_clock_cb(nullptr);
    }
    if (g_status_refresh_pending) {
        g_status_refresh_pending = false;
        refresh_status_cb(nullptr);
    }
}

int present_display_buffer() {
    const uint32_t now_ms = tick_ms();
    if (g_last_present_ms != 0 && now_ms - g_last_present_ms < kPresentIntervalMs) {
        return 0;
    }

    thead_csi_dcache_clean_invalid_range(g_buffer->map, g_buffer->size);
    if (display_commit_buffer_noblock(g_buffer, 0, 0) != 0) {
        return -1;
    }

    g_last_present_ms = now_ms;
    g_render_buffer_index = (g_render_buffer_index + 1U) % 3U;
    g_buffer = g_display_buffers[g_render_buffer_index];
    return 0;
}

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (g_buffer == nullptr || g_buffer->map == nullptr || area == nullptr || px_map == nullptr) {
        lv_display_flush_ready(disp);
        return;
    }

    if (g_custom_frame_presenting) {
        lv_display_flush_ready(disp);
        return;
    }

    const uint32_t area_w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    const auto* src = reinterpret_cast<const uint32_t*>(px_map);
    auto* dst = static_cast<uint32_t*>(g_buffer->map);
    const uint32_t dst_stride_px = g_buffer->stride / sizeof(uint32_t);

    const bool full_screen = area->x1 == 0 && area->y1 == 0 &&
                             area->x2 == static_cast<int32_t>(kUiWidth - 1U) &&
                             area->y2 == static_cast<int32_t>(kUiHeight - 1U);
    const uint32_t t_rotate_start = tick_ms();
    if (full_screen && g_buffer->width == kUiHeight && g_buffer->height == kUiWidth) {
        constexpr uint32_t kRotateBlock = 16;
        for (uint32_t sy0 = 0; sy0 < kUiHeight; sy0 += kRotateBlock) {
            const uint32_t sy_end = sy0 + kRotateBlock < kUiHeight ? sy0 + kRotateBlock : kUiHeight;
            for (uint32_t sx0 = 0; sx0 < kUiWidth; sx0 += kRotateBlock) {
                const uint32_t sx_end = sx0 + kRotateBlock < kUiWidth ? sx0 + kRotateBlock : kUiWidth;
                for (uint32_t sy = sy0; sy < sy_end; ++sy) {
                    const uint32_t* src_row = src + sy * kUiWidth + sx0;
                    for (uint32_t sx = sx0; sx < sx_end; ++sx) {
                        dst[sx * dst_stride_px + (kUiHeight - 1U - sy)] = *src_row++;
                    }
                }
            }
        }
    }
    else {
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
    }
    const uint32_t t_rotate_end = tick_ms();
    draw_cached_indicator(dst, dst_stride_px);

    const uint32_t t_cache_start = tick_ms();
    const uint32_t t_commit_start = tick_ms();
    if (present_display_buffer() != 0) {
        std::fprintf(stderr, "rockbase-lcdui: display commit failed\n");
    }
    const uint32_t t_commit_end = tick_ms();

    static uint32_t flush_log_n = 0;
    static uint32_t last_flush_end = 0;
    const uint32_t between_ms = last_flush_end ? (t_cache_start - last_flush_end) : 0;
    last_flush_end = t_commit_end;
    const uint32_t cache_ms = t_commit_start - t_cache_start;
    const uint32_t commit_ms = t_commit_end - t_commit_start;
    const uint32_t rotate_ms = t_rotate_end - t_rotate_start;
    if (flush_log_n < 8 || rotate_ms > 10 || cache_ms > 3 || commit_ms > 18 || between_ms > 80) {
        ++flush_log_n;
        std::printf("rockbase-lcdui: flush between=%u rotate=%u cache=%u commit=%u\n",
                    between_ms, rotate_ms, cache_ms, commit_ms);
        std::fflush(stdout);
    }

    const uint32_t now_ms = tick_ms();
    if (g_fps_window_start_ms == 0) {
        g_fps_window_start_ms = now_ms;
    }
    ++g_fps_frame_count;
    const uint32_t elapsed = now_ms - g_fps_window_start_ms;
    if (elapsed >= 1000U) {
        const uint32_t fps = (g_fps_frame_count * 1000U) / elapsed;
        const bool in_interaction = ui_interacting(now_ms);
        std::printf("rockbase-lcdui: fps=%u window_ms=%u interaction=%u\n",
                    fps, elapsed, in_interaction ? 1U : 0U);
        std::fflush(stdout);
        g_fps_window_start_ms = now_ms;
        g_fps_frame_count = 0;
    }

    lv_display_flush_ready(disp);
}

bool commit_display_buffer() {
    if (present_display_buffer() != 0) {
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

long parse_leading_int(const std::string& value, long fallback = -1) {
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

bool has_value(const std::string& value) {
    return !value.empty() && value != "-";
}

std::string format_progress_percent(const std::string& value, int32_t max_width) {
    if (!has_value(value)) {
        return "--";
    }

    char* end = nullptr;
    const double pct = std::strtod(value.c_str(), &end);
    if (end == value.c_str()) {
        return "--";
    }

    char buf[16];
    for (int precision = 6; precision >= 0; --precision) {
        std::snprintf(buf, sizeof(buf), "%.*f%%", precision, pct);

        lv_point_t size {};
        lv_text_get_size(&size, buf, &lv_font_montserrat_36, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= max_width) {
            return buf;
        }
    }

    std::snprintf(buf, sizeof(buf), "%.0f%%", pct);
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
    data.ibd_stage_key = json_value(bitcoin, "ibd_stage_key");
    data.ibd_stage_label = json_value(bitcoin, "ibd_stage_label");
    data.ibd_stage_detail = json_value(bitcoin, "ibd_stage_detail");
    data.ibd_stage_height = json_value(bitcoin, "ibd_stage_height");
    data.ibd_stage_pct = json_value(bitcoin, "ibd_stage_pct");
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
    g_display_buffers[0] = display_allocate_buffer(g_plane, g_display->width, g_display->height);
    g_display_buffers[1] = display_allocate_buffer(g_plane, g_display->width, g_display->height);
    g_display_buffers[2] = display_allocate_buffer(g_plane, g_display->width, g_display->height);
    g_buffer = g_display_buffers[0];
    if (g_buffer == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: display buffer allocation failed\n");
        return false;
    }

    for (display_buffer* buffer : g_display_buffers) {
        if (buffer == nullptr) {
            std::fprintf(stderr, "rockbase-lcdui: display buffer allocation failed\n");
            return false;
        }
        auto* pixels = static_cast<uint32_t*>(buffer->map);
        const uint32_t pixel_count = buffer->size / sizeof(uint32_t);
        for (uint32_t i = 0; i < pixel_count; ++i) {
            pixels[i] = 0xff000000;
        }
    }
    std::printf("rockbase-lcdui: panel=%ux%u buffer=%ux%u stride=%u size=%u fourcc=ARGB8888 rotation=software-ccw\n",
                g_display->width, g_display->height, g_buffer->width, g_buffer->height,
                g_buffer->stride, g_buffer->size);
    return true;
}

void cleanup_display() {
    for (uint32_t i = 0; i < kTileCount; ++i) {
        std::free(g_tile_cache[i]);
        g_tile_cache[i] = nullptr;
    }
    for (uint32_t index = 0; index < kTileCount; ++index) {
        if (g_tiles.page_snapshot[index] != nullptr) {
            lv_draw_buf_destroy(g_tiles.page_snapshot[index]);
            g_tiles.page_snapshot[index] = nullptr;
        }
    }
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
    for (display_buffer*& buffer : g_display_buffers) {
        if (buffer != nullptr) {
            display_free_buffer(buffer);
            buffer = nullptr;
        }
    }
    g_buffer = nullptr;
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
    lv_timer_t* refr_timer = lv_display_get_refr_timer(disp);
    if (refr_timer != nullptr) {
        lv_timer_set_period(refr_timer, kDisplayRefrPeriodMs);
    }
    lv_timer_t* anim_timer = lv_anim_get_timer();
    if (anim_timer != nullptr) {
        lv_timer_set_period(anim_timer, kDisplayRefrPeriodMs);
    }

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
    // Phase 2 pager owns swiping. Keep LVGL from applying its own tileview drag.
    data->state = LV_INDEV_STATE_RELEASED;
    g_swipe_current_x = data->point.x;

    static bool was_down = false;
    if (!was_down && g_touch.down) {
        g_swipe_start_x = data->point.x;
        g_swipe_start_page = g_active_page;
        g_visual_scroll_x = static_cast<int32_t>(g_swipe_start_page * kUiWidth);
    }
    if (was_down && !g_touch.down) {
        g_release_page = g_swipe_start_page;
        const int32_t swipe_dx = data->point.x - g_swipe_start_x;
        const int32_t release_scroll_x = visual_scroll_x_from_touch();
        if (swipe_dx <= -static_cast<int32_t>(kPageSwitchThresholdPx) &&
            g_release_page + 1U < kTileCount) {
            ++g_release_page;
        }
        else if (swipe_dx >= static_cast<int32_t>(kPageSwitchThresholdPx) && g_release_page > 0) {
            --g_release_page;
        }
        set_active_page(g_release_page);
        start_pager_snap_anim(release_scroll_x, g_release_page);
    }
    was_down = g_touch.down;
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
    lv_timer_set_period(lv_indev_get_read_timer(indev), kTouchReadPeriodMs);
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

lv_obj_t* create_stage_dot(lv_obj_t* parent, int32_t x, int32_t y) {
    lv_obj_t* dot = lv_obj_create(parent);
    make_static(dot);
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_size(dot, 13, 13);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    return dot;
}

lv_obj_t* create_card(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                      const char* title, const char* value, lv_color_t accent,
                      lv_obj_t** title_label = nullptr) {
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

    lv_obj_t* title_obj = add_label(card, title, 0, 0, &lv_font_montserrat_16, lv_color_hex(0x9aa6ac));
    if (title_label != nullptr) {
        *title_label = title_obj;
    }
    if (value != nullptr && value[0] != '\0') {
        lv_obj_t* value_label = add_label(card, value, 0, 34, &lv_font_montserrat_22, lv_color_hex(0xf4efe2));
        lv_obj_set_width(value_label, w - 32);
        lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_DOTS);
        return value_label;
    }
    return nullptr;
}

lv_obj_t* create_card_frame(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                            const char* title, lv_color_t accent, lv_obj_t** title_label = nullptr) {
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

    lv_obj_t* title_obj = add_label(card, title, 0, 0, &lv_font_montserrat_16, lv_color_hex(0x9aa6ac));
    if (title_label != nullptr) {
        *title_label = title_obj;
    }
    return card;
}

bool set_label(lv_obj_t* label, const std::string& value) {
    if (label == nullptr) {
        return false;
    }
    const char* current = lv_label_get_text(label);
    if (current != nullptr && value == current) {
        return false;
    }
    lv_label_set_text(label, value.c_str());
    return true;
}

bool set_hidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) {
        return false;
    }
    const bool was_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    if (was_hidden == hidden) {
        return false;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    return true;
}

uint32_t bitcoin_stage_index(const DashboardData& data) {
    const std::string key = data.ibd_stage_key != "-" ? data.ibd_stage_key : data.sync_key;
    if (data.sync_key == "btc_not_running") {
        return 0;
    }
    if (data.sync_key == "synced" || key == "synced") {
        return 6;
    }
    if (key == "loading_index") {
        return 1;
    }
    if (key == "loading_blocks" || key == "verifying_blocks" ||
        key == "rewinding_blocks" || key == "pruning") {
        return 2;
    }
    if (key == "pre_headers") {
        return 3;
    }
    if (key == "sync_headers") {
        return 4;
    }
    if (key == "sync_blocks" || key == "syncing") {
        return 5;
    }
    return 0;
}

HeightCardModel make_height_card_model(const DashboardData& data, const std::string& blocks) {
    HeightCardModel card;
    card.title = "Blocks / Header";
    card.value = blocks;
    card.split_pair = false;

    if (data.ibd_stage_key == "pre_headers" || data.ibd_stage_key == "sync_headers") {
        card.title = "Header";
        const std::string height = has_value(data.ibd_stage_height) ? data.ibd_stage_height : data.headers_fmt;
        card.value = join_value(height, " / ", data.headers_fmt);
        card.split_pair = true;
    }
    else if (data.ibd_stage_key == "sync_blocks") {
        card.title = "Block Sync";
    }
    else if (data.ibd_stage_key == "loading_index") {
        card.title = "Block Index";
        card.value = "Preparing chain";
    }
    else if (data.ibd_stage_key == "loading_blocks") {
        card.title = "Block Files";
        card.value = "Loading blocks";
    }
    else if (data.ibd_stage_key == "verifying_blocks") {
        card.title = "Verifying";
        card.value = "Chain state";
    }
    else if (data.ibd_stage_key == "rewinding_blocks") {
        card.title = "Rewinding";
        card.value = "Chain state";
    }
    else if (data.ibd_stage_key == "pruning") {
        card.title = "Pruning";
        card.value = "Block files";
    }

    return card;
}

bool build_ui_model(UiModel& model) {
    DashboardData data;
    if (!load_dashboard_data(data)) {
        model.status_available = false;
        return false;
    }

    const std::string blocks = join_value(data.blocks_fmt, " / ", data.headers_fmt);
    std::string block_date;
    std::string block_time;
    split_date_time(data.best_block_date, block_date, block_time);

    model.status_available = true;
    model.bitcoin.stage_index = bitcoin_stage_index(data);
    model.bitcoin.progress = format_progress_percent(data.progress_ring, lv_obj_get_width(g_tiles.bitcoin_progress));
    model.bitcoin.height = make_height_card_model(data, blocks);
    model.bitcoin.peers = data.connections;
    model.bitcoin.block_date = block_date;
    model.bitcoin.block_time = block_time;
    model.bitcoin.ip_addr = data.ip_addr;
    model.bitcoin.log_sync_key = data.sync_key;
    model.bitcoin.log_blocks = data.blocks_fmt;
    model.bitcoin.log_headers = data.headers_fmt;

    model.stratum.state = data.ckpool_state;
    model.stratum.url = data.stratum_url;
    model.stratum.user = data.btc_address != "-" ? data.btc_address : data.user_hint;
    model.stratum.mode = data.recommended_mode;

    model.system.load = join_value(data.load_display, " | ", data.load_avg_display);
    model.system.mem = join_value(data.mem_used_display, " / ", data.mem_total_display);
    model.system.disk = data.disk_size_info;
    model.system.temp = data.temp;
    model.system.uptime = data.uptime;
    model.system.swap = join_value(data.swap_used_display, " / ", data.swap_total_display);

    model.network.ip = data.ip_addr;
    model.network.ssid = data.wifi_ssid;
    model.network.rssi = data.rssi_display;
    model.network.ntp = ntp_label(data.ntp_key);
    model.network.mac = data.mac_addr;
    model.network.reach = data.stratum_reach;
    return true;
}

bool update_bitcoin_stage_list(uint32_t active_stage) {
    constexpr uint32_t kDoneDot = 0x2f7d5d;
    constexpr uint32_t kActiveDot = 0x37e88b;
    constexpr uint32_t kPendingDot = 0x46515a;
    constexpr uint32_t kDoneText = 0x8ab79f;
    constexpr uint32_t kActiveText = 0xf4efe2;
    constexpr uint32_t kPendingText = 0x78848b;

    if (active_stage >= kBitcoinStageCount) {
        active_stage = 0;
    }

    static uint32_t last_active_stage = UINT32_MAX;
    if (last_active_stage == active_stage) {
        return false;
    }
    last_active_stage = active_stage;

    for (uint32_t index = 0; index < kBitcoinStageCount; ++index) {
        const bool done = index < active_stage;
        const bool active = index == active_stage;
        const uint32_t dot_color = active ? kActiveDot : (done ? kDoneDot : kPendingDot);
        const uint32_t text_color = active ? kActiveText : (done ? kDoneText : kPendingText);
        lv_obj_t* dot = g_tiles.bitcoin_stage_dot[index];
        if (dot != nullptr) {
            lv_obj_set_style_bg_color(dot, lv_color_hex(dot_color), 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(active ? 0xd9ffea : dot_color), 0);
            lv_obj_set_style_border_width(dot, active ? 2 : 0, 0);
        }
        if (g_tiles.bitcoin_stage_label[index] != nullptr) {
            lv_obj_set_style_text_color(g_tiles.bitcoin_stage_label[index], lv_color_hex(text_color), 0);
        }
    }
    return true;
}

void style_tile(lv_obj_t* tile, lv_color_t bg, lv_color_t /*accent*/) {
    /* Do not remove all styles from a tileview tile: LVGL stores tile size/pos as local style props. */
    lv_obj_set_style_bg_color(tile, bg, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
}

void add_page_header(lv_obj_t* tile, const char* title, const char* subtitle, lv_color_t accent) {
    add_label(tile, title, 36, 30, &lv_font_montserrat_36, accent);
    add_label(tile, subtitle, 38, 82, &lv_font_montserrat_18, lv_color_hex(0xb7c2c8));
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

void add_dashboard_background(lv_obj_t* tile, uint32_t page) {
    if (page >= kTileCount || !g_page_bg[page].ready) {
        return;
    }

    lv_obj_t* img = lv_image_create(tile);
    lv_obj_remove_style_all(img);
    lv_image_set_src(img, &g_page_bg[page].dsc);
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
        add_dashboard_background(tile, col);
    }
    add_page_header(tile, spec.title, spec.subtitle, accent);
    g_tiles.page[col] = tile;
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
    g_tiles.bitcoin_ip_label = add_label(tile, "--", 42, 438, &lv_font_montserrat_16, lv_color_hex(0xb7c2c8));
    lv_obj_set_width(g_tiles.bitcoin_ip_label, 260);
    lv_label_set_long_mode(g_tiles.bitcoin_ip_label, LV_LABEL_LONG_MODE_DOTS);

    add_label(tile, "Sync Flow", 42, 130, &lv_font_montserrat_18, lv_color_hex(0xf0b44c));
    for (uint32_t index = 0; index < kBitcoinStageCount; ++index) {
        const int32_t row_y = 164 + static_cast<int32_t>(index) * 33;
        g_tiles.bitcoin_stage_dot[index] = create_stage_dot(tile, 50, row_y + 7);
        g_tiles.bitcoin_stage_label[index] = add_label(tile, kBitcoinStageLabels[index], 76, row_y,
                                                       &lv_font_montserrat_18, lv_color_hex(0x78848b));
        lv_obj_set_width(g_tiles.bitcoin_stage_label[index], 190);
    }
    update_bitcoin_stage_list(0);

    g_tiles.bitcoin_progress = create_card(tile, 300, 142, 216, 96, "Progress", "--", accent);
    g_tiles.bitcoin_peers = create_card(tile, 540, 142, 216, 96, "Peers", "--", accent);
    g_tiles.bitcoin_height = create_card(tile, 300, 262, 456, 88, "Blocks / Header", "--", accent,
                                         &g_tiles.bitcoin_height_title);
    lv_obj_set_style_text_font(g_tiles.bitcoin_progress, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_font(g_tiles.bitcoin_peers, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_font(g_tiles.bitcoin_height, &lv_font_montserrat_36, 0);
    lv_obj_set_y(g_tiles.bitcoin_height, 24);
    g_tiles.bitcoin_height_current = add_label(lv_obj_get_parent(g_tiles.bitcoin_height), "--", 0, 24,
                                               &lv_font_montserrat_36, lv_color_hex(0x78848b));
    lv_obj_set_width(g_tiles.bitcoin_height_current, 152);
    lv_label_set_long_mode(g_tiles.bitcoin_height_current, LV_LABEL_LONG_MODE_DOTS);
    g_tiles.bitcoin_height_sep = add_label(lv_obj_get_parent(g_tiles.bitcoin_height), "/", 156, 24,
                                           &lv_font_montserrat_36, lv_color_hex(0x9aa6ac));
    g_tiles.bitcoin_height_total = add_label(lv_obj_get_parent(g_tiles.bitcoin_height), "", 184, 24,
                                             &lv_font_montserrat_36, lv_color_hex(0xf4efe2));
    lv_obj_set_width(g_tiles.bitcoin_height_total, 190);
    lv_label_set_long_mode(g_tiles.bitcoin_height_total, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_add_flag(g_tiles.bitcoin_height_current, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_tiles.bitcoin_height_sep, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_tiles.bitcoin_height_total, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* block_time_card = create_card_frame(tile, 300, 372, 456, 58, "Block Time", accent,
                                                  &g_tiles.bitcoin_block_date_title);
    lv_obj_set_width(g_tiles.bitcoin_block_date_title, 112);
    lv_obj_set_y(g_tiles.bitcoin_block_date_title, 10);
    lv_label_set_long_mode(g_tiles.bitcoin_block_date_title, LV_LABEL_LONG_MODE_CLIP);
    g_tiles.bitcoin_block_date = add_label(block_time_card, "--", 118, 10, &lv_font_montserrat_16,
                                           lv_color_hex(0xf4efe2));
    lv_obj_set_width(g_tiles.bitcoin_block_date, 84);
    lv_label_set_long_mode(g_tiles.bitcoin_block_date, LV_LABEL_LONG_MODE_DOTS);
    g_tiles.bitcoin_block_time = add_label(block_time_card, "", 208, 10, &lv_font_montserrat_16,
                                           lv_color_hex(0x78848b));
    lv_obj_set_width(g_tiles.bitcoin_block_time, 128);
    lv_label_set_long_mode(g_tiles.bitcoin_block_time, LV_LABEL_LONG_MODE_DOTS);
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

void set_active_page(uint32_t page) {
    if (page >= kTileCount) {
        page = kTileCount - 1U;
    }
    g_active_page = page;
}

void refresh_clock_cb(lv_timer_t*) {
    if (ui_interacting()) {
        g_clock_refresh_pending = true;
        return;
    }

    const std::string time_text = current_time_text("%H:%M:%S");
    const std::string date_text = current_time_text("%Y-%m-%d");
    bool dirty = false;
    dirty |= set_label(g_tiles.clock_time_label, time_text);
    dirty |= set_label(g_tiles.clock_date_label, date_text);
    if (dirty) {
        g_page_cache_dirty[0] = true;
    }
}

bool update_height_card(const HeightCardModel& card) {
    bool dirty = false;

    dirty |= set_label(g_tiles.bitcoin_height_title, card.title);
    if (card.split_pair) {
        std::string left_value;
        std::string right_value;
        split_pair_value(card.value, left_value, right_value);
        dirty |= set_label(g_tiles.bitcoin_height_current, left_value);
        dirty |= set_label(g_tiles.bitcoin_height_sep, right_value.empty() ? "" : "/");
        dirty |= set_label(g_tiles.bitcoin_height_total, right_value);
        dirty |= set_hidden(g_tiles.bitcoin_height, true);
        dirty |= set_hidden(g_tiles.bitcoin_height_current, false);
        dirty |= set_hidden(g_tiles.bitcoin_height_sep, false);
        dirty |= set_hidden(g_tiles.bitcoin_height_total, false);
    }
    else {
        dirty |= set_hidden(g_tiles.bitcoin_height, false);
        dirty |= set_hidden(g_tiles.bitcoin_height_current, true);
        dirty |= set_hidden(g_tiles.bitcoin_height_sep, true);
        dirty |= set_hidden(g_tiles.bitcoin_height_total, true);
        dirty |= set_label(g_tiles.bitcoin_height, card.value);
    }
    return dirty;
}

uint32_t apply_status_unavailable_model() {
    bool dirty = false;
    dirty |= set_label(g_tiles.bitcoin_progress, "No status");
    dirty |= set_label(g_tiles.bitcoin_height_title, "Blocks / Header");
    dirty |= set_label(g_tiles.bitcoin_height, "--");
    dirty |= set_hidden(g_tiles.bitcoin_height, false);
    dirty |= set_hidden(g_tiles.bitcoin_height_current, true);
    dirty |= set_hidden(g_tiles.bitcoin_height_sep, true);
    dirty |= set_hidden(g_tiles.bitcoin_height_total, true);
    dirty |= set_label(g_tiles.bitcoin_block_date_title, "Block Time");
    dirty |= set_label(g_tiles.bitcoin_block_date, "--");
    dirty |= set_label(g_tiles.bitcoin_block_time, "");
    return dirty ? page_dirty_bit(0) : 0;
}

uint32_t apply_ui_model(const UiModel& model) {
    uint32_t dirty_mask = 0;
    bool dirty = false;

    dirty |= update_bitcoin_stage_list(model.bitcoin.stage_index);
    dirty |= set_label(g_tiles.bitcoin_progress, model.bitcoin.progress);
    dirty |= update_height_card(model.bitcoin.height);
    dirty |= set_label(g_tiles.bitcoin_peers, model.bitcoin.peers);
    dirty |= set_label(g_tiles.bitcoin_block_date_title, "Block Time");
    dirty |= set_label(g_tiles.bitcoin_block_date, model.bitcoin.block_date);
    dirty |= set_label(g_tiles.bitcoin_block_time, model.bitcoin.block_time);
    dirty |= set_label(g_tiles.bitcoin_ip_label, model.bitcoin.ip_addr);
    if (dirty) {
        dirty_mask |= page_dirty_bit(0);
    }

    dirty = false;
    dirty |= set_label(g_tiles.stratum_state, model.stratum.state);
    dirty |= set_label(g_tiles.stratum_url, model.stratum.url);
    dirty |= set_label(g_tiles.stratum_user, model.stratum.user);
    dirty |= set_label(g_tiles.stratum_mode, model.stratum.mode);
    if (dirty) {
        dirty_mask |= page_dirty_bit(1);
    }

    dirty = false;
    dirty |= set_label(g_tiles.system_load, model.system.load);
    dirty |= set_label(g_tiles.system_mem, model.system.mem);
    dirty |= set_label(g_tiles.system_disk, model.system.disk);
    dirty |= set_label(g_tiles.system_temp, model.system.temp);
    dirty |= set_label(g_tiles.system_uptime, model.system.uptime);
    dirty |= set_label(g_tiles.system_swap, model.system.swap);
    if (dirty) {
        dirty_mask |= page_dirty_bit(2);
    }

    dirty = false;
    dirty |= set_label(g_tiles.network_ip, model.network.ip);
    dirty |= set_label(g_tiles.network_ssid, model.network.ssid);
    dirty |= set_label(g_tiles.network_rssi, model.network.rssi);
    dirty |= set_label(g_tiles.network_ntp, model.network.ntp);
    dirty |= set_label(g_tiles.network_mac, model.network.mac);
    dirty |= set_label(g_tiles.network_reach, model.network.reach);
    if (dirty) {
        dirty_mask |= page_dirty_bit(3);
    }

    return dirty_mask;
}

void mark_dirty_pages(uint32_t dirty_mask) {
    for (uint32_t index = 0; index < kTileCount; ++index) {
        if ((dirty_mask & page_dirty_bit(index)) != 0) {
            g_page_cache_dirty[index] = true;
        }
    }
}

void refresh_status_cb(lv_timer_t*) {
    if (ui_interacting()) {
        g_status_refresh_pending = true;
        return;
    }

    static bool logged_first_status = false;
    UiModel model;
    if (!build_ui_model(model)) {
        mark_dirty_pages(apply_status_unavailable_model());
        return;
    }

    mark_dirty_pages(apply_ui_model(model));

    if (!logged_first_status) {
        std::printf("rockbase-lcdui: status loaded ip=%s sync=%s blocks=%s headers=%s\n",
                    model.bitcoin.ip_addr.c_str(), model.bitcoin.log_sync_key.c_str(),
                    model.bitcoin.log_blocks.c_str(), model.bitcoin.log_headers.c_str());
        std::fflush(stdout);
        logged_first_status = true;
    }
}

void cache_tile_pages() {
    for (uint32_t index = 0; index < kTileCount; ++index) {
        lv_obj_t* page = g_tiles.page[index];
        if (page == nullptr) {
            continue;
        }

        lv_draw_buf_t* snapshot = lv_snapshot_take(page, LV_COLOR_FORMAT_ARGB8888);
        if (snapshot == nullptr) {
            std::fprintf(stderr, "rockbase-lcdui: snapshot failed for page %u\n", index);
            continue;
        }

        const uint32_t child_count = lv_obj_get_child_count(page);
        for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
            lv_obj_add_flag(lv_obj_get_child(page, child_index), LV_OBJ_FLAG_HIDDEN);
        }

        lv_obj_t* image = lv_image_create(page);
        lv_obj_remove_style_all(image);
        lv_image_set_src(image, snapshot);
        lv_obj_set_pos(image, 0, 0);
        lv_obj_set_size(image, kUiWidth, kUiHeight);
        make_static(image);
        g_tiles.page_snapshot[index] = snapshot;
        g_tiles.page_image[index] = image;
        g_page_cache_dirty[index] = false;
    }
}

void build_tile_stage(lv_obj_t* screen) {
    lv_obj_t* tv = lv_tileview_create(screen);
    lv_obj_set_size(tv, kUiWidth, kUiHeight);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0x070b10), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    g_tiles.tileview = tv;

    build_bitcoin_tile(tv);
    build_stratum_tile(tv);
    build_system_tile(tv);
    build_network_tile(tv);

    lv_tileview_set_tile_by_index(tv, 0, 0, LV_ANIM_OFF);
    refresh_clock_cb(nullptr);
    refresh_status_cb(nullptr);
    cache_tile_pages();
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
    for (uint32_t index = 0; index < kTileCount; ++index) {
        load_argb_image(kPageBgRawPaths[index], g_page_bg[index]);
    }

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
    lv_obj_t* fade_mask = lv_obj_create(screen);
    lv_obj_remove_style_all(fade_mask);
    make_static(fade_mask);
    lv_obj_set_size(fade_mask, kUiWidth, kUiHeight);
    lv_obj_set_pos(fade_mask, 0, 0);
    lv_obj_set_style_bg_color(fade_mask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(fade_mask, LV_OPA_COVER, 0);
    lv_refr_now(disp);
    if (backlight_ready) {
        set_backlight(1);
        lv_obj_fade_out(fade_mask, kBacklightFadeMs, 0);
        run_lv_for(kBacklightFadeMs + 20);
        lv_obj_delete(fade_mask);
        fade_mask = nullptr;
    }
    else {
        lv_obj_delete(fade_mask);
        fade_mask = nullptr;
    }
    run_lv_for(kBootLogoHoldMs);
    build_tile_stage(screen);
    build_tile_caches_from_snapshots();
    lv_obj_move_to_index(boot_layer, lv_obj_get_child_count(screen) - 1);
    lv_obj_fade_out(boot_layer, kDashboardFadeMs, 0);
    run_lv_for(kDashboardFadeMs + 80);
    lv_obj_delete(boot_layer);
    lv_refr_now(disp);

    std::printf("rockbase-lcdui: running lvgl tile UI\n");
    std::fflush(stdout);

    while (!g_stop) {
        const uint32_t now_ms = tick_ms();
        const bool interacting = ui_interacting(now_ms);
        process_deferred_refreshes_if_idle();
        refresh_one_dirty_page_if_idle();

        bool custom_present = false;
        if (g_touch.down) {
            custom_present = present_pager_frame(visual_scroll_x_from_touch());
        }
        else if (g_pager_anim_active) {
            custom_present = present_pager_snap_anim(now_ms);
        }

        g_custom_frame_presenting = g_touch.down || custom_present;
        uint32_t next_ms = lv_timer_handler();
        g_custom_frame_presenting = false;
        if (interacting) {
            next_ms = 0;
        }
        uint32_t sleep_us = next_ms * 1000U;
        if (sleep_us < kMinLvSleepUs) {
            sleep_us = kMinLvSleepUs;
        }
        if (sleep_us > kMaxLvSleepUs) {
            sleep_us = kMaxLvSleepUs;
        }
        usleep(sleep_us);
    }

    cleanup_display();
    return 0;
}
