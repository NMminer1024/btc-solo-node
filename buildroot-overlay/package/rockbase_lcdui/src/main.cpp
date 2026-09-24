#include "display.h"
#include "miners_overlay.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
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
#include <map>
#include <vector>

#include <thead.h>

#include "lvgl/lvgl.h"
#include "lvgl/draw/lv_snapshot.h"
#include "buzzer.h"
#include "network_settings_overlay.h"
#include "peer_map_overlay.h"
#include "ui_style.h"

// DSEG7 digital-tube font (see font/README.md). Its glyph set has no
// comma/slash/percent, so those must stay in a regular font.
LV_FONT_DECLARE(dseg7c700_font_22);
LV_FONT_DECLARE(dseg7c700_font_28);
LV_FONT_DECLARE(dseg7c700_font_32);
#include "timezone_overlay.h"
#include "wifi_config_overlay.h"
#include "ota_overlay.h"
#include "firmware_update_overlay.h"

namespace {

constexpr uint32_t kUiWidth = 800;
constexpr uint32_t kUiHeight = 480;
constexpr uint32_t kRollingDigitAnimMs = 620;
constexpr uint32_t kRollingDigitStaggerMs = 38;
constexpr size_t kRollingDigitCapacity = 8;
constexpr uint32_t kSwipeDragStartThresholdPx = 40;
constexpr uint32_t kPageSwitchThresholdPx = kUiWidth / 10U;
constexpr uint32_t kFramePixels = kUiWidth * kUiHeight;
constexpr uint32_t kTileCount = 4;
constexpr uint32_t kBitcoinStageCount = 7;
constexpr const char* kBacklightGpioChip = "/dev/gpiochip0";
constexpr uint32_t kBacklightGpioLine = 25;
constexpr uint32_t kBacklightFadeMs = 1000;
constexpr uint32_t kBootLogoHoldMs = 3000;
constexpr uint32_t kDashboardFadeMs = 500;
constexpr uint32_t kTouchReadPeriodMs = 20;  // 50Hz 触摸采样（最终参数，见 docs/rockbase-cpu-baseline.md）
constexpr uint32_t kPagerSnapAnimMs = 300;
constexpr uint32_t kMaxLvSleepUs = 20000;    // 空闲时最多睡 20ms（最终参数）
constexpr uint32_t kMinLvSleepUs = 500;
constexpr uint32_t kDisplayRefrPeriodMs = 10;
constexpr uint32_t kPresentIntervalMs = 20;
constexpr size_t kStratumChartMaxPoints = 500;
constexpr const char* kBootLogoRawPath = "/usr/share/rockbase-lcdui/boot-logo-800x480.argb";
constexpr const char* kProductVersionPath = "/etc/version/product_version";
constexpr const char* kStatusCachePath = "/tmp/status-api.json";
constexpr const char* kBootProgressPath = "/run/rockbase/boot-progress.json";
constexpr uint32_t kBootMinHoldMs = 2000;      // logo 最短显示时间
constexpr uint32_t kBootMaxWaitMs = 60000;     // 等待系统就绪的最长时间（兜底）
constexpr uint32_t kBootStageStallMs = 20000;  // 进度停滞超时（某阶段卡住时兜底退出）
constexpr uint32_t kBootStageMinHoldMs = 500;  // 每条进度最短停留时间（保证可见）
constexpr const char* kMinersCachePath = "/run/rockbase/miners.json";
constexpr const char* kMinersHistoryCachePath = "/run/rockbase/miners-history-node.json";
constexpr const char* kTimezoneFilePath = "/etc/TZ";
volatile sig_atomic_t g_stop = 0;
display* g_display = nullptr;
display_plane* g_plane = nullptr;
display_buffer* g_buffer = nullptr;
display_buffer* g_display_buffers[3] {};
uint32_t g_render_buffer_index = 0;
uint32_t g_last_present_ms = 0;
int g_backlight_fd = -1;
void* g_lv_draw_buf = nullptr;
lv_obj_t* g_boot_bar = nullptr;
lv_obj_t* g_boot_pct_label = nullptr;
lv_obj_t* g_boot_msg_label = nullptr;

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
    lv_obj_t* bitcoin_stage_dot[kBitcoinStageCount] {};
    lv_obj_t* bitcoin_stage_label[kBitcoinStageCount] {};
    lv_obj_t* bitcoin_progress = nullptr;
    lv_obj_t* bitcoin_progress_unit = nullptr;
    lv_obj_t* bitcoin_height_title = nullptr;
    lv_obj_t* bitcoin_height_current = nullptr;
    lv_obj_t* bitcoin_height_sep = nullptr;
    lv_obj_t* bitcoin_height_total = nullptr;
    lv_obj_t* bitcoin_height = nullptr;
    lv_obj_t* bitcoin_peers = nullptr;
    lv_obj_t* bitcoin_block_date_title = nullptr;
    lv_obj_t* bitcoin_block_date = nullptr;
    lv_obj_t* bitcoin_block_time = nullptr;
    lv_obj_t* stratum_state_dot = nullptr;
    lv_obj_t* stratum_state_label = nullptr;
    lv_obj_t* stratum_url_label = nullptr;
    int32_t stratum_state_dot_anchor_x = 0;
    int32_t stratum_state_dot_anchor_y = 0;
    bool stratum_state_anim_running = false;
    lv_obj_t* stratum_chart = nullptr;
    lv_obj_t* stratum_chart_hint = nullptr;
    lv_obj_t* stratum_legend_5m = nullptr;
    lv_obj_t* stratum_legend_6h = nullptr;
    lv_obj_t* stratum_legend_1d = nullptr;
    lv_obj_t* stratum_total_hashrate = nullptr;
    lv_obj_t* stratum_total_hashrate_unit = nullptr;
    lv_obj_t* stratum_best_share_prefix = nullptr;
    lv_obj_t* stratum_best_share_value = nullptr;
    lv_obj_t* stratum_best_share_unit = nullptr;
    lv_obj_t* stratum_y_unit = nullptr;
    lv_obj_t* stratum_y_ticks[5] {};
    lv_obj_t* stratum_x_ticks[3] {};
    lv_chart_series_t* stratum_series_5m = nullptr;
    lv_chart_series_t* stratum_series_6h = nullptr;
    lv_chart_series_t* stratum_series_1d = nullptr;
    lv_obj_t* page_version_label[kTileCount] {};
    lv_obj_t* system_cpu_arc = nullptr;
    lv_obj_t* system_cpu_value = nullptr;
    lv_obj_t* system_cpu_sub = nullptr;
    lv_obj_t* system_mem_arc = nullptr;
    lv_obj_t* system_mem_value = nullptr;
    lv_obj_t* system_mem_sub = nullptr;
    lv_obj_t* system_temp_arc = nullptr;
    lv_obj_t* system_temp_value = nullptr;
    lv_obj_t* system_temp_sub = nullptr;
    lv_obj_t* system_disk_pct = nullptr;
    lv_obj_t* system_disk_fill = nullptr;
    lv_obj_t* system_disk_detail = nullptr;
    lv_obj_t* system_swap_pct = nullptr;
    lv_obj_t* system_swap_fill = nullptr;
    lv_obj_t* system_swap_detail = nullptr;
    lv_obj_t* system_uptime = nullptr;
    lv_obj_t* network_iface_icon = nullptr;
    lv_obj_t* network_settings_icon = nullptr;
    lv_obj_t* network_signal_arc = nullptr;
    lv_obj_t* network_signal_value = nullptr;
    lv_obj_t* network_signal_sub = nullptr;
    lv_obj_t* network_device_dot = nullptr;
    lv_obj_t* network_device_value = nullptr;
    lv_obj_t* network_device_mac = nullptr;
    lv_obj_t* network_gateway_dot = nullptr;
    lv_obj_t* network_gateway = nullptr;
    lv_obj_t* network_dns_dot = nullptr;
    lv_obj_t* network_dns = nullptr;
    lv_obj_t* network_down_arrow = nullptr;
    lv_obj_t* network_down_value = nullptr;
    lv_obj_t* network_up_arrow = nullptr;
    lv_obj_t* network_up_value = nullptr;
};

struct RollingDigitSlot {
    lv_obj_t* clip = nullptr;
    lv_obj_t* track = nullptr;
    lv_obj_t* old_label = nullptr;
    lv_obj_t* new_label = nullptr;
};

struct RollingNumber {
    lv_obj_t* root = nullptr;
    std::array<RollingDigitSlot, kRollingDigitCapacity> slots {};
    size_t capacity = 0;
    int32_t digit_width = 0;
    int32_t decimal_width = 0;
    int32_t line_height = 0;
    std::string value;
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
    std::vector<std::string> peer_ips;
    std::map<std::string, bool> peer_inbound;
    PeerMapOverlay::Geo self_geo;
    std::string difficulty_display = "-";
    std::string rss = "-";
    std::string ckpool_state = "-";
    std::string ckpool_class = "-";
    std::string stratum_url = "-";
    std::string user_hint = "-";
    std::string recommended_mode = "-";
    std::string btc_address = "-";
    std::string load_display = "-";
    std::string load_pct = "-";
    std::string mem_used_display = "-";
    std::string mem_total_display = "-";
    std::string disk_size_info = "-";
    std::string swap_used_display = "-";
    std::string swap_total_display = "-";
    std::string mem_pct = "-";
    std::string disk_pct = "-";
    std::string swap_pct = "-";
    std::string load_avg_1 = "-";
    std::string load_avg_5 = "-";
    std::string product_version = "-";
    std::string temp = "-";
    std::string uptime = "-";
    std::string ip_addr = "-";
    std::string iface = "-";
    std::string wifi_ssid = "-";
    std::string rssi_display = "-";
    std::string rssi_pct = "0";
    int signal_level = 0;
    std::string ntp_key = "-";
    std::string mac_addr = "-";
    std::string timezone_key = "-";
    std::string gateway_ip = "-";
    std::string dns_primary = "-";
    std::string dns_secondary = "-";
    std::string net_rx_display = "--";
    std::string net_tx_display = "--";
    std::string miners_hashrate_5m = "0";
    std::string miners_hashrate_6h = "0";
    std::string miners_hashrate_1d = "0";
    std::vector<int32_t> miners_history_5m;
    std::vector<int32_t> miners_history_6h;
    std::vector<int32_t> miners_history_1d;
    std::vector<int32_t> miners_history_ts;
    MinersOverlay::Snapshot miners_snapshot;
};

struct HeightCardModel {
    std::string title = "Blocks / Header";
    std::string value = "--";
};

struct BitcoinPageModel {
    uint32_t stage_index = 0;
    std::string progress = "--";
    HeightCardModel height;
    std::string peers = "--";
    std::vector<std::string> peer_ips;
    std::map<std::string, bool> peer_inbound;
    PeerMapOverlay::Geo self_geo;
    std::string block_date = "--";
    std::string block_time;
    std::string ip_addr = "--";
    std::string log_sync_key = "-";
    std::string log_blocks = "-";
    std::string log_headers = "-";
};

struct StratumPageModel {
    std::string state = "--";
    std::string state_class = "-";
    std::string url = "--";
    std::string user = "--";
    std::string mode = "--";
    std::string total_hashrate_5m = "0";
    std::string best_share = "--";
    std::vector<int32_t> history_5m;
    std::vector<int32_t> history_6h;
    std::vector<int32_t> history_1d;
    std::vector<int32_t> history_ts;
    MinersOverlay::Snapshot miners_snapshot;
};

struct SystemPageModel {
    int cpu_value = 0;
    std::string cpu_text = "--";
    std::string cpu_sub = "Load avg --";
    int mem_value = 0;
    std::string mem_text = "--";
    std::string mem_sub = "-- / --";
    int temp_value = 0;
    std::string temp_text = "--";
    std::string temp_sub = "--";
    int disk_value = 0;
    std::string disk_pct_text = "--";
    std::string disk_detail = "--";
    int swap_value = 0;
    std::string swap_pct_text = "--";
    std::string swap_detail = "--";
    std::string uptime = "--";
};

struct NetworkPageModel {
    std::string iface_icon = LV_SYMBOL_WIFI;
    int signal_value = 0;
    std::string signal_text = "--";
    std::string signal_sub = "--";
    std::string ip = "--";
    std::string mac = "--";
    std::string ssid = "--";
    std::string rssi = "--";
    int signal_level = 0;
    std::string timezone = "--";
    std::string timezone_offset = "--";
    std::string gateway = "--";
    std::string dns = "--";
    std::string net_rx_display = "--";
    std::string net_tx_display = "--";
    bool device_up = false;
    bool gateway_up = false;
    bool dns_up = false;
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
    {"Stratum", "", 0x101418, 0x51c7a3, true},
    {"System", "Core vitals - CPU / Memory / Thermal", 0x13151e, 0x8fb7ff, true},
    {"Network", "Active interface and upstream path", 0x101719, 0xff8c64, true},
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
RollingNumber g_bitcoin_progress_roll;
RollingNumber g_bitcoin_peers_roll;
RollingNumber g_bitcoin_height_current_roll;
RollingNumber g_bitcoin_height_total_roll;
uint32_t g_bitcoin_roll_pending = 0;
bool g_bitcoin_roll_active = false;
RawImage g_boot_logo;
RawImage g_page_bg[kTileCount];
static uint32_t* g_tile_cache[kTileCount] = {};  // pre-rendered physical frames per tile
bool g_custom_frame_presenting = false;
uint32_t g_active_page = 0;
uint32_t g_swipe_start_page = 0;
uint32_t g_release_page = 0;
int32_t g_swipe_start_x = 0;
int32_t g_swipe_start_y = 0;
int32_t g_swipe_current_x = 0;
int32_t g_swipe_current_y = 0;
int32_t g_visual_scroll_x = 0;
bool g_pager_anim_active = false;
uint32_t g_pager_anim_start_ms = 0;
int32_t g_pager_anim_from_x = 0;
int32_t g_pager_anim_to_x = 0;
bool g_swipe_drag_started = false;
bool g_page_cache_dirty[kTileCount] {};
uint32_t g_last_page_cache_refresh_ms = 0;
uint32_t g_fps_window_start_ms = 0;
uint32_t g_fps_frame_count = 0;
bool g_clock_refresh_pending = false;
bool g_status_refresh_pending = false;
WifiConfigOverlay g_wifi_config_overlay;
bool g_wifi_config_overlay_open_pending = false;
bool g_wifi_config_overlay_wait_release = false;
TimezoneOverlay g_timezone_overlay;
bool g_timezone_overlay_open_pending = false;
NetworkSettingsOverlay g_network_settings_overlay;
bool g_network_settings_overlay_open_pending = false;
bool g_network_settings_overlay_wait_release = false;
NetworkSettingsOverlay::Action g_network_settings_child = NetworkSettingsOverlay::Action::None;
std::string g_network_settings_wifi_summary = "Not connected";
std::string g_network_settings_timezone_summary = "--";
bool g_network_settings_wifi_enabled = false;
MinersOverlay g_miners_overlay;
bool g_miners_overlay_open_pending = false;
PeerMapOverlay g_peer_map_overlay;
bool g_peer_map_overlay_open_pending = false;
std::vector<std::string> g_peer_map_ips;
std::map<std::string, bool> g_peer_map_inbound;
PeerMapOverlay::Geo g_peer_map_self_geo;
OtaOverlay g_ota_overlay;
FirmwareUpdateOverlay g_firmware_update_overlay;
int g_pressed_hotspot_index = -1;
bool g_show_page_indicator = false;

void set_active_page(uint32_t page);
void refresh_clock_cb(lv_timer_t*);
void refresh_status_cb(lv_timer_t*);
int present_display_buffer();
void request_status_refresh();
void set_page_source_visible(uint32_t index, bool visible);

uint32_t tick_ms() {
    timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

bool ui_interacting(uint32_t now_ms = tick_ms()) {
    (void)now_ms;
        return g_touch.down || g_pager_anim_active || g_network_settings_overlay.active() ||
            g_wifi_config_overlay.active() ||
           g_timezone_overlay.active() || g_miners_overlay.active() || g_ota_overlay.active() ||
           g_firmware_update_overlay.active();
}

struct ConfigHotspot {
    uint32_t page = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;
    void (*open)() = nullptr;
};

void open_wifi_config_overlay() {
    g_wifi_config_overlay_open_pending = true;
}

void open_timezone_overlay() {
    g_timezone_overlay_open_pending = true;
}

void open_network_settings_overlay() {
    g_network_settings_overlay_open_pending = true;
}

void open_miners_overlay() {
    g_miners_overlay_open_pending = true;
}

void open_peer_map_overlay() {
    g_peer_map_overlay_open_pending = true;
}

constexpr ConfigHotspot kConfigHotspots[] = {
    {0, 540, 142, 216, 96, open_peer_map_overlay},
    {1, 42, 118, 716, 308, open_miners_overlay},
    {3, 704, 20, 64, 64, open_network_settings_overlay},
};

int find_config_hotspot(uint32_t page, int32_t x, int32_t y) {
    for (size_t i = 0; i < sizeof(kConfigHotspots) / sizeof(kConfigHotspots[0]); ++i) {
        const ConfigHotspot& hotspot = kConfigHotspots[i];
        if (hotspot.page != page || hotspot.open == nullptr) {
            continue;
        }
        if (x >= hotspot.x && x < hotspot.x + hotspot.w &&
            y >= hotspot.y && y < hotspot.y + hotspot.h) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool trigger_config_hotspot(int hotspot_index) {
    if (hotspot_index < 0 ||
        hotspot_index >= static_cast<int>(sizeof(kConfigHotspots) / sizeof(kConfigHotspots[0]))) {
        return false;
    }
    const ConfigHotspot& hotspot = kConfigHotspots[hotspot_index];
    if (hotspot.open == nullptr) {
        return false;
    }
    buzzer_beep();
    hotspot.open();
    return true;
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
    if (!g_show_page_indicator) {
        return;
    }
    // The dots are blitted straight into the framebuffer on top of LVGL
    // content, so they would punch through full-screen overlays (peer map,
    // OTA, settings, ...). Hide them while any overlay is active.
    if (g_network_settings_overlay.active() || g_wifi_config_overlay.active() ||
        g_timezone_overlay.active() || g_miners_overlay.active() ||
        g_peer_map_overlay.active() || g_ota_overlay.active() ||
        g_firmware_update_overlay.active()) {
        return;
    }
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
    if (!g_swipe_drag_started) {
        if (std::abs(dx) < static_cast<int32_t>(kSwipeDragStartThresholdPx)) {
            return base;
        }
        g_swipe_drag_started = true;
    }
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

bool is_live_page_overlay(uint32_t index, lv_obj_t* child) {
    return index == 1 && child != nullptr && child == g_tiles.stratum_state_dot;
}

void set_page_source_visible(uint32_t index, bool visible) {
    if (index >= kTileCount || g_tiles.page[index] == nullptr ||
        g_tiles.page_image[index] == nullptr) {
        return;
    }
    lv_obj_t* page = g_tiles.page[index];
    lv_obj_t* image = g_tiles.page_image[index];
    const uint32_t child_count = lv_obj_get_child_count(page);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
        lv_obj_t* child = lv_obj_get_child(page, child_index);
        if (child == image || is_live_page_overlay(index, child)) {
            continue;
        }
        if (visible) {
            lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (visible) {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(image, LV_OBJ_FLAG_HIDDEN);
    }
}

void set_live_page_overlays_hidden(uint32_t index, bool hidden) {
    if (index >= kTileCount) {
        return;
    }
    lv_obj_t* page = g_tiles.page[index];
    if (page == nullptr) {
        return;
    }
    const uint32_t child_count = lv_obj_get_child_count(page);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
        lv_obj_t* child = lv_obj_get_child(page, child_index);
        if (!is_live_page_overlay(index, child)) {
            continue;
        }
        if (hidden) {
            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(child);
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
    set_live_page_overlays_hidden(index, true);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
        lv_obj_t* child = lv_obj_get_child(page, child_index);
        if (child != image && !is_live_page_overlay(index, child)) {
            lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_draw_buf_t* snapshot = lv_snapshot_take(page, LV_COLOR_FORMAT_ARGB8888);
    for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
        lv_obj_t* child = lv_obj_get_child(page, child_index);
        if (child != image && !is_live_page_overlay(index, child)) {
            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (snapshot == nullptr) {
        set_live_page_overlays_hidden(index, false);
        lv_obj_clear_flag(image, LV_OBJ_FLAG_HIDDEN);
        return false;
    }

    lv_draw_buf_t* old_snapshot = g_tiles.page_snapshot[index];
    g_tiles.page_snapshot[index] = snapshot;
    lv_image_set_src(image, snapshot);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_HIDDEN);
    set_live_page_overlays_hidden(index, false);
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
    if (page == 0 && g_bitcoin_roll_active) {
        return;
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

void request_status_refresh() {
    g_status_refresh_pending = true;
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

std::string read_product_version() {
    std::string ver;
    if (!read_text_file(kProductVersionPath, ver, 64)) {
        return "";
    }
    while (!ver.empty() && (ver.back() == '\n' || ver.back() == '\r' || ver.back() == ' ' || ver.back() == '\t')) {
        ver.pop_back();
    }
    while (!ver.empty() && (ver.front() == ' ' || ver.front() == '\t')) {
        ver.erase(ver.begin());
    }
    return ver;
}

std::string read_timezone_key() {
    std::string tz;
    if (!read_text_file(kTimezoneFilePath, tz, 256)) {
        return "";
    }
    while (!tz.empty() && (tz.back() == '\n' || tz.back() == '\r' || tz.back() == ' ' || tz.back() == '\t')) {
        tz.pop_back();
    }
    while (!tz.empty() && (tz.front() == ' ' || tz.front() == '\t')) {
        tz.erase(tz.begin());
    }
    if (tz == "CST-8") {
        return "Asia/Shanghai";
    }
    if (tz == "UTC0" || tz == "Etc/UTC") {
        return "UTC";
    }
    return tz;
}

// 时区名 -> POSIX TZ 字符串（经 sync_process_timezone 的 setenv/tzset 在进程内生效）。
// 注意：新增/删除时区时，必须与 timezone_overlay.h 的 kZones[]、
//       timezone_offset_label()、以及 etc/init.d/S03lcdui 的 case "${TZ}" 保持同步。
std::string timezone_posix_spec(const std::string& key) {
    if (key.empty() || key == "UTC") return "UTC0";
    if (key == "Asia/Shanghai") return "CST-8";
    if (key == "Asia/Tokyo") return "JST-9";
    if (key == "Asia/Seoul") return "KST-9";
    if (key == "Asia/Bangkok") return "<+07>-7";
    if (key == "Asia/Singapore") return "<+08>-8";
    if (key == "Asia/Dubai") return "<+04>-4";
    if (key == "Asia/Kolkata") return "IST-5:30";
    if (key == "Asia/Tehran") return "<+03:30>-3:30";
    if (key == "Asia/Kabul") return "<+04:30>-4:30";
    if (key == "Asia/Karachi") return "PKST-5";
    if (key == "Asia/Dhaka") return "<+06>-6";
    if (key == "Asia/Yangon") return "<+06:30>-6:30";
    if (key == "Europe/London") return "GMT0BST,M3.5.0/1,M10.5.0/2";
    if (key == "Europe/Berlin") return "CET-1CEST,M3.5.0/2,M10.5.0/3";
    if (key == "Europe/Moscow") return "MSK-3";
    if (key == "Europe/Istanbul") return "<+03>-3";
    if (key == "Africa/Cairo") return "EET-2";
    if (key == "Africa/Johannesburg") return "SAST-2";
    if (key == "Atlantic/Azores") return "<-01>1<-00>,M3.5.0/0,M10.5.0/1";
    if (key == "America/New_York") return "EST5EDT,M3.2.0/2,M11.1.0/2";
    if (key == "America/Chicago") return "CST6CDT,M3.2.0/2,M11.1.0/2";
    if (key == "America/Denver") return "MST7MDT,M3.2.0/2,M11.1.0/2";
    if (key == "America/Los_Angeles") return "PST8PDT,M3.2.0/2,M11.1.0/2";
    if (key == "America/Mexico_City") return "CST6";
    if (key == "America/Sao_Paulo") return "<+03>3";
    if (key == "America/Santiago") return "<+04>4<+03>,M9.1.6/24,M4.1.6/24";
    if (key == "America/Anchorage") return "AKST9AKDT,M3.2.0/2,M11.1.0/2";
    if (key == "Pacific/Honolulu") return "HST10";
    if (key == "Pacific/Pago_Pago") return "SST11";
    if (key == "Pacific/Auckland") return "NZST-12NZDT,M9.5.0/2,M4.1.0/3";
    if (key == "Pacific/Noumea") return "NCT-11";
    if (key == "Australia/Perth") return "AWST-8";
    if (key == "Australia/Sydney") return "AEST-10AEDT,M10.1.0/2,M4.1.0/3";
    if (key == "Australia/Adelaide") return "ACST-9:30ACDT,M10.1.0/2,M4.1.0/3";
    return "UTC0";
}

void sync_process_timezone() {
    static std::string applied_tz;
    std::string tz = read_timezone_key();
    if (tz.empty()) {
        tz = "UTC";
    }
    const std::string posix_tz = timezone_posix_spec(tz);
    if (posix_tz == applied_tz) {
        return;
    }
    setenv("TZ", posix_tz.c_str(), 1);
    tzset();
    applied_tz = posix_tz;
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

int json_int_value(const std::string& object, const char* key, int fallback = 0) {
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

// Parses strings like "47.3C" or "N/A" produced by rockbase_statusd.sh.
double parse_temp_celsius(const std::string& text, bool* ok = nullptr) {
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

struct HashrateDisplay {
    double scaled = 0.0;
    const char* unit = "GH/s";
};

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

std::string format_hashrate_value(double value_ghs) {
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
    return buf;
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

bool load_miners_chart_data(DashboardData& data) {
    std::string miners_json;
    if (!read_text_file(kMinersCachePath, miners_json, 262144)) {
        return false;
    }

    const std::string pool = json_object(miners_json, "pool");
    data.miners_hashrate_5m = json_value(pool, "hashrate5m", "0");
    data.miners_hashrate_6h = json_value(pool, "hashrate6hr", "0");
    data.miners_hashrate_1d = json_value(pool, "hashrate1d", "0");
    data.miners_snapshot.total_workers = json_int_value(pool, "workers", 0);
    data.miners_snapshot.total_hashrate_5m = data.miners_hashrate_5m;

    const std::string miners = json_array(miners_json, "miners");
    const std::vector<std::string> miner_rows = json_array_objects(miners);
    data.miners_snapshot.miners.clear();
    data.miners_snapshot.miners.reserve(miner_rows.size());
    for (const std::string& miner_row : miner_rows) {
        MinersOverlay::Miner miner;
        miner.address = json_value(miner_row, "address");
        miner.online = json_value(miner_row, "online", "false") == "true";
        miner.workers = std::max(json_int_value(miner_row, "worker_detail_count", 0),
                                 json_int_value(miner_row, "workers", 0));
        miner.hashrate_5m = json_value(miner_row, "hashrate5m", "--");
        miner.last_share_ago = json_value(miner_row, "lastshare_ago", "-");
        miner.best_share = format_share_value(std::strtod(json_value(miner_row, "bestshare", "0").c_str(), nullptr));
        data.miners_snapshot.miners.push_back(miner);
        if (miner.online) {
            ++data.miners_snapshot.online_miners;
        }
    }
    std::sort(data.miners_snapshot.miners.begin(), data.miners_snapshot.miners.end(),
              [](const MinersOverlay::Miner& left, const MinersOverlay::Miner& right) {
                  if (left.online != right.online) {
                      return left.online > right.online;
                  }
                  const double left_hash = parse_hashrate_ghs(left.hashrate_5m);
                  const double right_hash = parse_hashrate_ghs(right.hashrate_5m);
                  if (left_hash != right_hash) {
                      return left_hash > right_hash;
                  }
                  return left.address < right.address;
              });
    data.miners_snapshot.total_miners = static_cast<int>(data.miners_snapshot.miners.size());

    std::string history_json;
    if (!read_text_file(kMinersHistoryCachePath, history_json, 262144)) {
        return true;
    }

    const std::string points = json_array(history_json, "points");
    const std::vector<std::string> rows = json_array_objects(points);
    if (rows.empty()) {
        return true;
    }

    std::vector<int32_t> h5m;
    std::vector<int32_t> h6h;
    std::vector<int32_t> h1d;
    std::vector<int32_t> ts_values;
    h5m.reserve(rows.size());
    h6h.reserve(rows.size());
    h1d.reserve(rows.size());
    ts_values.reserve(rows.size());
    for (const std::string& row : rows) {
        ts_values.push_back(std::atoi(json_value(row, "ts", "0").c_str()));
        h5m.push_back(static_cast<int32_t>(parse_hashrate_ghs(json_value(row, "h5m", "0"))));
        h6h.push_back(static_cast<int32_t>(parse_hashrate_ghs(json_value(row, "h6h", "0"))));
        h1d.push_back(static_cast<int32_t>(parse_hashrate_ghs(json_value(row, "h1d", "0"))));
    }

    data.miners_history_5m = downsample_history_points(h5m, kStratumChartMaxPoints);
    data.miners_history_6h = downsample_history_points(h6h, kStratumChartMaxPoints);
    data.miners_history_1d = downsample_history_points(h1d, kStratumChartMaxPoints);
    data.miners_history_ts = downsample_history_points(ts_values, kStratumChartMaxPoints);
    return true;
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
    data.peer_ips = json_string_array(json_array(bitcoin, "peers"));
    // peers_detail carries the inbound flag per peer so the map can draw
    // flow arcs in the right direction (outbound: us -> peer, inbound: peer -> us).
    {
        const std::string peers_detail = json_array(bitcoin, "peers_detail");
        size_t pos = 0;
        while (pos < peers_detail.size()) {
            const size_t ob = peers_detail.find('{', pos);
            if (ob == std::string::npos) break;
            const size_t cb = peers_detail.find('}', ob);
            if (cb == std::string::npos) break;
            const std::string obj = peers_detail.substr(ob, cb - ob + 1);
            const std::string ip = json_value(obj, "ip", "");
            if (!ip.empty()) {
                // statusd emits inbound as 0/1 (integer), not true/false
                data.peer_inbound[ip] = (json_value(obj, "inbound", "0") == "1");
            }
            pos = cb + 1;
        }
    }
    const std::string self_geo = json_object(bitcoin, "self_geo");
    data.self_geo.lat = json_double(self_geo, "lat", 999.0);
    data.self_geo.lon = json_double(self_geo, "lon", 999.0);
    data.self_geo.city = json_value(self_geo, "city", "");
    data.self_geo.country = json_value(self_geo, "country", "");
    data.self_geo.known = data.self_geo.lat >= -90.0 && data.self_geo.lat <= 90.0 &&
                          data.self_geo.lon >= -180.0 && data.self_geo.lon <= 180.0;
    data.difficulty_display = json_value(bitcoin, "difficulty_display");
    data.rss = json_value(bitcoin, "rss");

    data.ckpool_state = json_value(mining, "ckpool_state");
    data.ckpool_class = json_value(mining, "ckpool_class");
    data.stratum_url = json_value(mining, "stratum_url");
    data.user_hint = json_value(mining, "user_hint");
    data.recommended_mode = json_value(mining, "recommended_mode");
    data.btc_address = json_value(mining, "btc_address");

    data.load_display = json_value(system, "load_display");
    data.load_pct = json_value(system, "load_pct");
    data.mem_used_display = json_value(system, "mem_used_display");
    data.mem_total_display = json_value(system, "mem_total_display");
    data.disk_size_info = json_value(system, "disk_size_info");
    data.swap_used_display = json_value(system, "swap_used_display");
    data.swap_total_display = json_value(system, "swap_total_display");
    data.mem_pct = json_value(system, "mem_pct");
    data.disk_pct = json_value(system, "disk_pct");
    data.swap_pct = json_value(system, "swap_pct");
    data.load_avg_1 = json_value(system, "load_avg_1");
    data.load_avg_5 = json_value(system, "load_avg_5");
    data.product_version = json_value(system, "product_version");
    data.temp = json_value(system, "temp");
    data.uptime = json_value(system, "uptime");

    data.ip_addr = json_value(network, "ip_addr");
    data.iface = json_value(network, "iface");
    data.wifi_ssid = json_value(network, "wifi_ssid");
    data.rssi_display = json_value(network, "rssi_display");
    data.rssi_pct = json_value(network, "rssi_pct");
    data.signal_level = json_int_value(network, "signal_level", 0);
    data.ntp_key = json_value(network, "ntp_key");
    data.mac_addr = json_value(network, "mac_addr");
    data.timezone_key = json_value(network, "timezone_key");
    data.gateway_ip = json_value(network, "gateway_ip");
    data.dns_primary = json_value(network, "dns_primary");
    data.dns_secondary = json_value(network, "dns_secondary");
    data.net_rx_display = json_value(network, "net_rx_display");
    data.net_tx_display = json_value(network, "net_tx_display");
    load_miners_chart_data(data);
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
    buzzer_shutdown();
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
    data->state = LV_INDEV_STATE_RELEASED;
    g_swipe_current_x = data->point.x;
    g_swipe_current_y = data->point.y;

    static bool was_down = false;
    if (g_network_settings_overlay.active() || g_wifi_config_overlay.active() ||
        g_timezone_overlay.active() || g_miners_overlay.active() ||
        g_peer_map_overlay.active() ||
        g_ota_overlay.active() || g_firmware_update_overlay.active()) {
        if (g_network_settings_overlay.active() && g_network_settings_overlay_wait_release) {
            if (!g_touch.down) {
                g_network_settings_overlay_wait_release = false;
            }
            was_down = g_touch.down;
            return;
        }
        if (g_wifi_config_overlay.active() && g_wifi_config_overlay_wait_release) {
            if (!g_touch.down) {
                g_wifi_config_overlay_wait_release = false;
            }
            was_down = g_touch.down;
            return;
        }
        data->state = g_touch.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        was_down = g_touch.down;
        return;
    }

    if (!was_down && g_touch.down) {
        g_swipe_start_x = data->point.x;
        g_swipe_start_y = data->point.y;
        g_swipe_start_page = g_active_page;
        g_visual_scroll_x = static_cast<int32_t>(g_swipe_start_page * kUiWidth);
        g_swipe_drag_started = false;
        g_pressed_hotspot_index = find_config_hotspot(g_swipe_start_page, data->point.x, data->point.y);
    }
    if (was_down && !g_touch.down) {
        g_release_page = g_swipe_start_page;
        const int32_t swipe_dx = data->point.x - g_swipe_start_x;
        const int32_t release_scroll_x = visual_scroll_x_from_touch();
        const bool drag_started = g_swipe_drag_started;
        if (swipe_dx <= -static_cast<int32_t>(kPageSwitchThresholdPx) &&
            g_release_page + 1U < kTileCount) {
            ++g_release_page;
        }
        else if (swipe_dx >= static_cast<int32_t>(kPageSwitchThresholdPx) && g_release_page > 0) {
            --g_release_page;
        }
        const int32_t tap_dx = data->point.x - g_swipe_start_x;
        const int32_t tap_dy = data->point.y - g_swipe_start_y;
        const bool is_tap = !drag_started &&
                            std::abs(tap_dx) < static_cast<int32_t>(kSwipeDragStartThresholdPx) &&
                            std::abs(tap_dy) < static_cast<int32_t>(kSwipeDragStartThresholdPx);
        const int released_hotspot_index =
            find_config_hotspot(g_swipe_start_page, data->point.x, data->point.y);
        if (is_tap && g_pressed_hotspot_index >= 0 &&
            g_pressed_hotspot_index == released_hotspot_index &&
            trigger_config_hotspot(g_pressed_hotspot_index)) {
            g_swipe_drag_started = false;
            g_pressed_hotspot_index = -1;
            was_down = g_touch.down;
            return;
        }
        set_active_page(g_release_page);
        if (drag_started) {
            start_pager_snap_anim(release_scroll_x, g_release_page);
        }
        g_swipe_drag_started = false;
        g_pressed_hotspot_index = -1;
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

    // Global click feedback: LV_EVENT_CLICKED is first dispatched to the indev
    // itself (before the target object), and its param is the clicked object.
    // This single handler covers every LVGL control (buttons, checkboxes,
    // keyboard keys) across all overlays, without touching each call site.
    lv_indev_add_event_cb(indev, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
            return;
        }
        if (lv_event_get_param(e) != nullptr) {
            buzzer_beep();
        }
    }, LV_EVENT_CLICKED, nullptr);
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

void rolling_track_set_y(void* obj, int32_t value) {
    lv_obj_set_y(static_cast<lv_obj_t*>(obj), value);
}

void finish_bitcoin_roll_if_idle() {
    if (g_bitcoin_roll_pending != 0) {
        return;
    }
    g_bitcoin_roll_active = false;
    g_page_cache_dirty[0] = true;
}

void rolling_digit_completed(lv_anim_t* anim) {
    auto* slot = static_cast<RollingDigitSlot*>(lv_anim_get_user_data(anim));
    if (slot != nullptr) {
        lv_label_set_text(slot->old_label, lv_label_get_text(slot->new_label));
        lv_label_set_text(slot->new_label, "");
        lv_obj_set_y(slot->track, 0);
    }
    if (g_bitcoin_roll_pending > 0) {
        --g_bitcoin_roll_pending;
    }
    finish_bitcoin_roll_if_idle();
}

RollingNumber create_rolling_number(lv_obj_t* parent, int32_t x, int32_t y,
                                    int32_t width, size_t capacity,
                                    const lv_font_t* font, lv_color_t color) {
    RollingNumber number;
    number.capacity = std::min(capacity, kRollingDigitCapacity);
    number.digit_width = static_cast<int32_t>(lv_font_get_glyph_width(font, '0', 0));
    number.decimal_width = std::max<int32_t>(8, static_cast<int32_t>(lv_font_get_glyph_width(font, '.', 0)));
    number.line_height = lv_font_get_line_height(font);
    number.root = lv_obj_create(parent);
    make_static(number.root);
    lv_obj_remove_style_all(number.root);
    lv_obj_set_pos(number.root, x, y);
    lv_obj_set_size(number.root, width, number.line_height);

    for (size_t index = 0; index < number.capacity; ++index) {
        RollingDigitSlot& slot = number.slots[index];
        slot.clip = lv_obj_create(number.root);
        make_static(slot.clip);
        lv_obj_remove_style_all(slot.clip);
        lv_obj_set_pos(slot.clip, static_cast<int32_t>(index) * number.digit_width, 0);
        lv_obj_set_size(slot.clip, number.digit_width, number.line_height);

        slot.track = lv_obj_create(slot.clip);
        make_static(slot.track);
        lv_obj_remove_style_all(slot.track);
        lv_obj_set_pos(slot.track, 0, 0);
        lv_obj_set_size(slot.track, number.digit_width, number.line_height * 2);

        slot.old_label = add_label(slot.track, "", 0, 0, font, color);
        slot.new_label = add_label(slot.track, "", 0, number.line_height, font, color);
        lv_obj_set_width(slot.old_label, number.digit_width);
        lv_obj_set_width(slot.new_label, number.digit_width);
        lv_obj_set_style_text_align(slot.old_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_align(slot.new_label, LV_TEXT_ALIGN_CENTER, 0);
    }
    return number;
}

void set_rolling_number_immediate(RollingNumber& number, const std::string& value) {
    int32_t x = 0;
    for (size_t index = 0; index < number.capacity; ++index) {
        RollingDigitSlot& slot = number.slots[index];
        const char text[2] = {index < value.size() ? value[index] : '\0', '\0'};
        const int32_t width = index < value.size() && value[index] == '.'
                                  ? number.decimal_width
                                  : number.digit_width;
        lv_obj_set_x(slot.clip, x);
        lv_obj_set_width(slot.clip, width);
        lv_obj_set_width(slot.track, width);
        lv_obj_set_width(slot.old_label, width);
        lv_obj_set_width(slot.new_label, width);
        lv_label_set_text(slot.old_label, text);
        lv_label_set_text(slot.new_label, "");
        lv_obj_set_y(slot.track, 0);
        x += index < value.size() ? width : 0;
    }
    number.value = value;
}

int32_t rolling_number_width(const RollingNumber& number, const std::string& value) {
    int32_t width = 0;
    for (char ch : value) {
        width += ch == '.' ? number.decimal_width : number.digit_width;
    }
    return width;
}

bool set_rolling_number(RollingNumber& number, const std::string& value) {
    if (number.root == nullptr || number.value == value) {
        return false;
    }
    const bool can_animate = g_active_page == 0 && !ui_interacting() &&
                             is_rolling_number(number.value) && is_rolling_number(value) &&
                             number.value.size() == value.size() &&
                             value.size() <= number.capacity;
    if (!can_animate) {
        set_rolling_number_immediate(number, value);
        return true;
    }

    if (!g_bitcoin_roll_active) {
        g_bitcoin_roll_active = true;
        set_page_source_visible(0, true);
    }
    for (size_t index = 0; index < value.size(); ++index) {
        RollingDigitSlot& slot = number.slots[index];
        if (number.value[index] == value[index]) {
            continue;
        }
        if (number.value[index] == '.' || value[index] == '.') {
            set_rolling_number_immediate(number, value);
            return true;
        }
        const char old_text[2] = {number.value[index], '\0'};
        const char new_text[2] = {value[index], '\0'};
        lv_label_set_text(slot.old_label, old_text);
        lv_label_set_text(slot.new_label, new_text);
        lv_obj_set_y(slot.track, 0);

        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, slot.track);
        lv_anim_set_exec_cb(&anim, rolling_track_set_y);
        lv_anim_set_values(&anim, 0, -number.line_height);
        lv_anim_set_duration(&anim, kRollingDigitAnimMs);
        lv_anim_set_delay(&anim, static_cast<uint32_t>(index) * kRollingDigitStaggerMs);
        lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
        lv_anim_set_user_data(&anim, &slot);
        lv_anim_set_completed_cb(&anim, rolling_digit_completed);
        ++g_bitcoin_roll_pending;
        lv_anim_start(&anim);
    }
    number.value = value;
    finish_bitcoin_roll_if_idle();
    return true;
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

lv_obj_t* create_status_dot(lv_obj_t* parent, int32_t x, int32_t y, int32_t size) {
    lv_obj_t* dot = lv_obj_create(parent);
    make_static(dot);
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_size(dot, size, size);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 2, 0);
    return dot;
}

void populate_stratum_placeholder_chart(lv_obj_t* chart) {
    if (chart == nullptr) {
        return;
    }

    static constexpr int32_t kHashrateSample[] = {
        18, 22, 25, 23, 30, 28, 34, 38, 36, 44, 41, 49, 46, 54, 58, 56, 63, 67
    };

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, sizeof(kHashrateSample) / sizeof(kHashrateSample[0]));
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 4, 6);
    lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 80);

    lv_chart_series_t* series = lv_chart_add_series(chart, lv_color_hex(0x51c7a3), LV_CHART_AXIS_PRIMARY_Y);
    for (size_t index = 0; index < sizeof(kHashrateSample) / sizeof(kHashrateSample[0]); ++index) {
        lv_chart_set_next_value(chart, series, kHashrateSample[index]);
    }
    lv_chart_refresh(chart);
}

bool update_stratum_chart(const StratumPageModel& model) {
    if (g_tiles.stratum_chart == nullptr ||
        g_tiles.stratum_series_5m == nullptr ||
        g_tiles.stratum_series_6h == nullptr ||
        g_tiles.stratum_series_1d == nullptr) {
        return false;
    }

    const size_t count = std::max(model.history_5m.size(),
                         std::max(model.history_6h.size(), model.history_1d.size()));
    if (count < 2) {
        bool dirty = false;
        if (!lv_obj_has_flag(g_tiles.stratum_chart, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(g_tiles.stratum_chart, LV_OBJ_FLAG_HIDDEN);
            dirty = true;
        }
        if (g_tiles.stratum_chart_hint != nullptr) {
            if (lv_obj_has_flag(g_tiles.stratum_chart_hint, LV_OBJ_FLAG_HIDDEN)) {
                lv_obj_clear_flag(g_tiles.stratum_chart_hint, LV_OBJ_FLAG_HIDDEN);
                dirty = true;
            }
            lv_label_set_text(g_tiles.stratum_chart_hint, "Waiting for hashrate history...");
        }
        return dirty;
    }

    std::vector<int32_t> h5m = model.history_5m.empty() ? std::vector<int32_t>(count, 0) : model.history_5m;
    std::vector<int32_t> h6h = model.history_6h.empty() ? std::vector<int32_t>(count, 0) : model.history_6h;
    std::vector<int32_t> h1d = model.history_1d.empty() ? std::vector<int32_t>(count, 0) : model.history_1d;
    if (h5m.size() != count) h5m.resize(count, h5m.empty() ? 0 : h5m.back());
    if (h6h.size() != count) h6h.resize(count, h6h.empty() ? 0 : h6h.back());
    if (h1d.size() != count) h1d.resize(count, h1d.empty() ? 0 : h1d.back());

    int32_t max_value = 0;
    for (size_t i = 0; i < count; ++i) {
        max_value = std::max(max_value, h5m[i]);
        max_value = std::max(max_value, h6h[i]);
        max_value = std::max(max_value, h1d[i]);
    }
    if (max_value <= 0) {
        max_value = 1000;
    }
    max_value = ((max_value + 499) / 500) * 500;

    const HashrateDisplay axis_display = format_hashrate_display(static_cast<double>(max_value));
    if (g_tiles.stratum_total_hashrate != nullptr) {
        const double total_ghs = parse_hashrate_ghs(model.total_hashrate_5m);
        if (total_ghs <= 0.0) {
            lv_label_set_text(g_tiles.stratum_total_hashrate, "--");
            lv_label_set_text(g_tiles.stratum_total_hashrate_unit, "");
        } else {
            lv_label_set_text(g_tiles.stratum_total_hashrate, format_hashrate_value(total_ghs).c_str());
            lv_label_set_text(g_tiles.stratum_total_hashrate_unit, format_hashrate_display(total_ghs).unit);
        }
        lv_point_t unit_size {};
        lv_text_get_size(&unit_size, lv_label_get_text(g_tiles.stratum_total_hashrate_unit),
                         &lv_font_montserrat_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        lv_obj_align_to(g_tiles.stratum_total_hashrate, g_tiles.stratum_total_hashrate_unit,
                        LV_ALIGN_OUT_LEFT_TOP, 72 - unit_size.x - 4, -13);
    }
    if (g_tiles.stratum_y_unit != nullptr) {
        lv_label_set_text(g_tiles.stratum_y_unit, axis_display.unit);
    }
    for (size_t i = 0; i < 5; ++i) {
        if (g_tiles.stratum_y_ticks[i] == nullptr) continue;
        const double value = (axis_display.scaled * static_cast<double>(4 - static_cast<int32_t>(i))) / 4.0;
        char buf[16];
        if (value >= 100.0) {
            std::snprintf(buf, sizeof(buf), "%.0f", value);
        }
        else if (value >= 10.0) {
            std::snprintf(buf, sizeof(buf), "%.1f", value);
        }
        else {
            std::snprintf(buf, sizeof(buf), "%.2f", value);
        }
        lv_label_set_text(g_tiles.stratum_y_ticks[i], buf);
    }
    const size_t ts_count = model.history_ts.size();
    const size_t ts_index[3] = {
        0,
        ts_count > 0 ? ts_count / 2 : 0,
        ts_count > 1 ? ts_count - 1 : 0
    };
    for (size_t i = 0; i < 3; ++i) {
        if (g_tiles.stratum_x_ticks[i] == nullptr) continue;
        const int32_t ts = ts_count > ts_index[i] ? model.history_ts[ts_index[i]] : 0;
        lv_label_set_text(g_tiles.stratum_x_ticks[i], format_chart_time_label(ts).c_str());
    }

    bool dirty = false;
    if (lv_obj_has_flag(g_tiles.stratum_chart, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(g_tiles.stratum_chart, LV_OBJ_FLAG_HIDDEN);
        dirty = true;
    }
    if (g_tiles.stratum_chart_hint != nullptr) {
        if (!lv_obj_has_flag(g_tiles.stratum_chart_hint, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(g_tiles.stratum_chart_hint, LV_OBJ_FLAG_HIDDEN);
            dirty = true;
        }
    }

    lv_chart_set_point_count(g_tiles.stratum_chart, count);
    lv_chart_set_axis_range(g_tiles.stratum_chart, LV_CHART_AXIS_PRIMARY_Y, 0, max_value);
    lv_chart_set_series_values(g_tiles.stratum_chart, g_tiles.stratum_series_5m, h5m.data(), h5m.size());
    lv_chart_set_series_values(g_tiles.stratum_chart, g_tiles.stratum_series_6h, h6h.data(), h6h.size());
    lv_chart_set_series_values(g_tiles.stratum_chart, g_tiles.stratum_series_1d, h1d.data(), h1d.size());
    lv_chart_refresh(g_tiles.stratum_chart);
    return true;
}

lv_obj_t* create_card_ex(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                         const char* title, const char* value, lv_color_t accent,
                         const lv_font_t* value_font,
                         int32_t value_y,
                         lv_obj_t** card_obj,
                         lv_obj_t** title_label) {
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
        lv_obj_t* value_label = add_label(card, value, 0, value_y, value_font, lv_color_hex(0xf4efe2));
        lv_obj_set_width(value_label, w - 32);
        lv_label_set_long_mode(value_label, LV_LABEL_LONG_MODE_DOTS);
        if (card_obj != nullptr) {
            *card_obj = card;
        }
        return value_label;
    }
    if (card_obj != nullptr) {
        *card_obj = card;
    }
    return nullptr;
}

lv_obj_t* create_card(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h,
                      const char* title, const char* value, lv_color_t accent,
                      lv_obj_t** title_label = nullptr) {
    return create_card_ex(parent, x, y, w, h, title, value, accent,
                          &lv_font_montserrat_22, 34, nullptr, title_label);
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

struct GaugeWidgets {
    lv_obj_t* arc = nullptr;
    lv_obj_t* value_label = nullptr;
    lv_obj_t* sub_label = nullptr;
};

// Instrument-cluster style ring gauge: 270-degree arc (bottom gap) with a big
// centered value, a caption baked into the gap, and a detail line underneath.
GaugeWidgets create_gauge(lv_obj_t* parent, int32_t x, int32_t y, int32_t size,
                          const char* caption, lv_color_t accent) {
    GaugeWidgets gw;

    lv_obj_t* arc = lv_arc_create(parent);
    make_static(arc);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_set_pos(arc, x, y);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_bg_angles(arc, 135, 45);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x1c2430), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    gw.arc = arc;

    gw.value_label = add_label(parent, "--", x, y + size / 2 - 34, &lv_font_montserrat_36, lv_color_hex(0xf4efe2));
    lv_obj_set_width(gw.value_label, size);
    lv_obj_set_style_text_align(gw.value_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* caption_label = add_label(parent, caption, x, y + size / 2 + 8, &lv_font_montserrat_14, lv_color_hex(0x78848b));
    lv_obj_set_width(caption_label, size);
    lv_obj_set_style_text_align(caption_label, LV_TEXT_ALIGN_CENTER, 0);

    gw.sub_label = add_label(parent, "--", x - 30, y + size + 8, &lv_font_montserrat_18, lv_color_hex(0x9aa6ac));
    lv_obj_set_width(gw.sub_label, size + 60);
    lv_obj_set_style_text_align(gw.sub_label, LV_TEXT_ALIGN_CENTER, 0);

    return gw;
}

constexpr int32_t kSystemBarWidth = 330;

struct BarWidgets {
    lv_obj_t* pct_label = nullptr;
    lv_obj_t* fill = nullptr;
    lv_obj_t* detail_label = nullptr;
};

// Thin horizontal usage bar (title + pct on one row, track below, detail underneath).
BarWidgets create_bar(lv_obj_t* parent, int32_t x, int32_t y, const char* title, lv_color_t accent) {
    BarWidgets bw;
    add_label(parent, title, x, y, &lv_font_montserrat_14, lv_color_hex(0x78848b));
    bw.pct_label = add_label(parent, "--", x, y, &lv_font_montserrat_14, accent);
    lv_obj_set_width(bw.pct_label, kSystemBarWidth);
    lv_obj_set_style_text_align(bw.pct_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t* track = lv_obj_create(parent);
    make_static(track);
    lv_obj_remove_style_all(track);
    lv_obj_set_pos(track, x, y + 22);
    lv_obj_set_size(track, kSystemBarWidth, 8);
    lv_obj_set_style_radius(track, 4, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(0x1c2430), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);

    lv_obj_t* fill = lv_obj_create(parent);
    make_static(fill);
    lv_obj_remove_style_all(fill);
    lv_obj_set_pos(fill, x, y + 22);
    lv_obj_set_size(fill, 0, 8);
    lv_obj_set_style_radius(fill, 4, 0);
    lv_obj_set_style_bg_color(fill, accent, 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    bw.fill = fill;

    bw.detail_label = add_label(parent, "--", x, y + 36, &lv_font_montserrat_14, lv_color_hex(0x9aa6ac));
    lv_obj_set_width(bw.detail_label, kSystemBarWidth);
    return bw;
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

bool set_text_color(lv_obj_t* obj, uint32_t color) {
    if (obj == nullptr) {
        return false;
    }
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return true;
}

bool set_network_status_dot(lv_obj_t* dot, bool online) {
    if (dot == nullptr) {
        return false;
    }
    const lv_color_t bg = lv_color_hex(online ? 0x37e88b : 0x526269);
    const lv_color_t border = lv_color_hex(online ? 0xcaf9d6 : 0x78848b);
    const bool changed = !lv_color_eq(lv_obj_get_style_bg_color(dot, LV_PART_MAIN), bg) ||
                         !lv_color_eq(lv_obj_get_style_border_color(dot, LV_PART_MAIN), border);
    lv_obj_set_style_bg_color(dot, bg, 0);
    lv_obj_set_style_border_color(dot, border, 0);
    return changed;
}

// Updates a System-page ring gauge: arc sweep + centered value + color staging.
bool set_gauge(lv_obj_t* arc, lv_obj_t* value_label, int value, const std::string& text, uint32_t color) {
    bool dirty = false;
    if (arc != nullptr && lv_arc_get_value(arc) != value) {
        lv_arc_set_value(arc, value);
        lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
        dirty = true;
    }
    dirty |= set_label(value_label, text);
    dirty |= set_text_color(value_label, color);
    return dirty;
}

// Updates a System-page horizontal usage bar: fill width + pct label.
bool set_bar(lv_obj_t* fill, lv_obj_t* pct_label, int pct, const std::string& pct_text) {
    bool dirty = false;
    if (fill != nullptr) {
        const int32_t width = kSystemBarWidth * clamp_int(pct, 0, 100) / 100;
        if (lv_obj_get_width(fill) != width) {
            lv_obj_set_width(fill, width);
            dirty = true;
        }
    }
    dirty |= set_label(pct_label, pct_text);
    return dirty;
}

#if 0  // 2026-08-19: 禁用 Stratum 状态点呼吸动画（无限循环动画每 10ms 触发重绘，单核 CPU 高占用热点）
void set_obj_opa_anim(void* obj, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), static_cast<lv_opa_t>(value), 0);
}

void set_status_dot_green_anim(void* obj, int32_t value) {
    lv_obj_t* dot = static_cast<lv_obj_t*>(obj);
    if (dot == nullptr) {
        return;
    }
    constexpr double kPi = 3.14159265358979323846;
    const double phase = static_cast<double>(clamp_int(value, 0, 360)) * kPi / 180.0;
    const double wave = (1.0 - std::cos(phase)) * 0.5;
    const uint32_t opa = static_cast<uint32_t>(LV_OPA_30 + (LV_OPA_COVER - LV_OPA_30) * wave);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x37e88b), 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(0xcaf9d6), 0);
    lv_obj_set_style_opa(dot, static_cast<lv_opa_t>(opa), 0);
}
#endif  // #if 0 禁用呼吸动画

void set_status_dot_anchor(int32_t x, int32_t y) {
    if (g_tiles.stratum_state_dot == nullptr) {
        return;
    }
    g_tiles.stratum_state_dot_anchor_x = x;
    g_tiles.stratum_state_dot_anchor_y = y;
    const int32_t size = lv_obj_get_width(g_tiles.stratum_state_dot);
    lv_obj_set_pos(g_tiles.stratum_state_dot, x - (size / 2), y - (size / 2));
}

void start_status_dot_breathing(lv_obj_t* dot) {
    if (dot == nullptr) {
        return;
    }
    if (g_tiles.stratum_state_anim_running) {
        return;
    }
    g_tiles.stratum_state_anim_running = true;
    // 静态绿点（呼吸动画已被 #if 0 禁用）
    lv_obj_set_style_bg_color(dot, lv_color_hex(0x37e88b), 0);
    lv_obj_set_style_border_color(dot, lv_color_hex(0xcaf9d6), 0);
    lv_obj_set_style_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
#if 0  // 呼吸动画禁用，避免每 10ms 无限重绘
    lv_anim_delete(dot, set_obj_opa_anim);
    lv_anim_delete(dot, set_status_dot_green_anim);
    lv_anim_t color_anim;
    lv_anim_init(&color_anim);
    lv_anim_set_var(&color_anim, dot);
    lv_anim_set_exec_cb(&color_anim, set_status_dot_green_anim);
    lv_anim_set_values(&color_anim, 0, 360);
    lv_anim_set_duration(&color_anim, 1000);
    lv_anim_set_playback_duration(&color_anim, 0);
    lv_anim_set_repeat_count(&color_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&color_anim);
#endif  // #if 0 禁用呼吸动画
}

void stop_status_dot_breathing(lv_obj_t* dot, lv_opa_t opa = LV_OPA_COVER) {
    if (dot == nullptr) {
        return;
    }
#if 0  // 呼吸动画禁用
    lv_anim_delete(dot, set_obj_opa_anim);
    lv_anim_delete(dot, set_status_dot_green_anim);
#endif  // #if 0 禁用呼吸动画
    g_tiles.stratum_state_anim_running = false;
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_style_opa(dot, opa, 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    set_status_dot_anchor(g_tiles.stratum_state_dot_anchor_x, g_tiles.stratum_state_dot_anchor_y);
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

void set_stratum_state_visual(const std::string& state_class, const std::string& state_text) {
    (void)state_text;
    const bool online = (state_class == "ok");

    if (g_tiles.stratum_state_dot != nullptr) {
        lv_obj_set_style_bg_opa(g_tiles.stratum_state_dot, LV_OPA_COVER, 0);
        if (online) {
            lv_obj_set_style_border_color(g_tiles.stratum_state_dot, lv_color_hex(0xcaf9d6), 0);
            start_status_dot_breathing(g_tiles.stratum_state_dot);
        }
        else {
            lv_obj_set_style_bg_color(g_tiles.stratum_state_dot, lv_color_hex(0xd65a5a), 0);
            lv_obj_set_style_border_color(g_tiles.stratum_state_dot, lv_color_hex(0xffc1c1), 0);
            stop_status_dot_breathing(g_tiles.stratum_state_dot, LV_OPA_COVER);
        }
    }
    if (g_tiles.stratum_url_label != nullptr) {
        lv_obj_set_style_text_color(g_tiles.stratum_url_label, lv_color_hex(online ? 0xf4efe2 : 0x7f8990), 0);
        if (g_tiles.stratum_state_dot != nullptr) {
            lv_point_t text_size {};
            lv_text_get_size(&text_size,
                             lv_label_get_text(g_tiles.stratum_url_label),
                             &lv_font_montserrat_18,
                             0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            const int32_t dot_x = lv_obj_get_x(g_tiles.stratum_url_label) + text_size.x + 15;
            const int32_t dot_y = lv_obj_get_y(g_tiles.stratum_url_label) + (text_size.y / 2);
            set_status_dot_anchor(dot_x, dot_y);
        }
    }
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

    if (data.ibd_stage_key == "pre_headers" || data.ibd_stage_key == "sync_headers") {
        card.title = "Header";
        const std::string height = has_value(data.ibd_stage_height) ? data.ibd_stage_height : data.headers_fmt;
        card.value = join_value(height, " / ", data.headers_fmt);
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
    if (!convert_utc_to_local(data.best_block_date, block_date, block_time)) {
        split_date_time(data.best_block_date, block_date, block_time);
    }

    model.status_available = true;
    model.bitcoin.stage_index = bitcoin_stage_index(data);
    model.bitcoin.progress = format_progress(data.progress_ring);
    model.bitcoin.height = make_height_card_model(data, blocks);
    model.bitcoin.peers = data.connections;
    model.bitcoin.peer_ips = data.peer_ips;
    model.bitcoin.peer_inbound = data.peer_inbound;
    model.bitcoin.self_geo = data.self_geo;
    model.bitcoin.block_date = block_date;
    model.bitcoin.block_time = block_time;
    model.bitcoin.ip_addr = data.ip_addr;
    model.bitcoin.log_sync_key = data.sync_key;
    model.bitcoin.log_blocks = data.blocks_fmt;
    model.bitcoin.log_headers = data.headers_fmt;

    model.stratum.state = data.ckpool_state;
    model.stratum.state_class = data.ckpool_class;
    model.stratum.url = data.stratum_url;
    model.stratum.user = data.btc_address != "-" ? data.btc_address : data.user_hint;
    model.stratum.mode = data.recommended_mode;
    model.stratum.total_hashrate_5m = data.miners_hashrate_5m;
    if (!data.miners_snapshot.miners.empty()) {
        model.stratum.best_share = data.miners_snapshot.miners[0].best_share;
    }
    model.stratum.history_5m = data.miners_history_5m;
    model.stratum.history_6h = data.miners_history_6h;
    model.stratum.history_1d = data.miners_history_1d;
    model.stratum.history_ts = data.miners_history_ts;
    model.stratum.miners_snapshot = data.miners_snapshot;

    model.system.cpu_value = clamp_int(parse_int_or(data.load_pct, 0), 0, 100);
    model.system.cpu_text = data.load_pct != "-" ? data.load_pct + "%" : "--";
    model.system.cpu_sub = "1m " + data.load_avg_1 + "  5m " + data.load_avg_5;

    model.system.mem_value = clamp_int(parse_int_or(data.mem_pct, 0), 0, 100);
    model.system.mem_text = data.mem_pct != "-" ? data.mem_pct + "%" : "--";
    model.system.mem_sub = join_value(data.mem_used_display, " / ", data.mem_total_display);

    bool temp_ok = false;
    const double temp_c = parse_temp_celsius(data.temp, &temp_ok);
    model.system.temp_value = temp_ok
        ? clamp_int(static_cast<int>(std::lround((temp_c - 20.0) * 100.0 / 70.0)), 0, 100)
        : 0;
    model.system.temp_text = temp_ok ? std::to_string(static_cast<int>(std::lround(temp_c))) + "C" : "--";
    model.system.temp_sub = !temp_ok ? "--" : (temp_c >= 80.0 ? "Hot" : (temp_c >= 65.0 ? "Warm" : "Normal"));

    model.system.disk_value = clamp_int(parse_int_or(data.disk_pct, 0), 0, 100);
    model.system.disk_pct_text = data.disk_pct != "-" ? data.disk_pct + "%" : "--";
    model.system.disk_detail = data.disk_size_info;

    model.system.swap_value = clamp_int(parse_int_or(data.swap_pct, 0), 0, 100);
    model.system.swap_pct_text = data.swap_pct != "-" ? data.swap_pct + "%" : "--";
    model.system.swap_detail = join_value(data.swap_used_display, " / ", data.swap_total_display);

    model.system.uptime = data.uptime;

    const bool wired = data.iface.rfind("eth", 0) == 0;
    model.network.iface_icon = wired ? LV_SYMBOL_USB : LV_SYMBOL_WIFI;
    model.network.ip = data.ip_addr;
    model.network.mac = data.mac_addr;
    model.network.device_up = data.ip_addr != "-" && !data.ip_addr.empty();
    model.network.gateway_up = data.gateway_ip != "-" && !data.gateway_ip.empty();
    model.network.dns_up = data.dns_primary != "-" && !data.dns_primary.empty();
    model.network.signal_value = wired ? (model.network.device_up ? 100 : 0)
                                       : clamp_int(parse_int_or(data.rssi_pct, 0), 0, 100);
    model.network.signal_text = wired ? model.network.iface_icon
                                      : std::to_string(model.network.signal_value) + "%";
    model.network.signal_sub = wired
        ? std::string(LV_SYMBOL_BULLET "  USB Ethernet")
        : std::string(LV_SYMBOL_BULLET "  ") + data.wifi_ssid +
              "  " LV_SYMBOL_BULLET "  " + data.rssi_display;
    model.network.ssid = wired ? "Not active (tap to configure)" : data.wifi_ssid;
    model.network.rssi = wired ? "WIRED" : data.rssi_display;
    model.network.signal_level = wired ? (model.network.device_up ? 4 : 0) : data.signal_level;
    model.network.timezone = timezone_label(data.timezone_key);
    model.network.timezone_offset = timezone_offset_label(data.timezone_key);
    model.network.gateway = data.gateway_ip;
    model.network.dns = join_value(data.dns_primary, " / ", data.dns_secondary);
    model.network.net_rx_display = data.net_rx_display;
    model.network.net_tx_display = data.net_tx_display;
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
    if (subtitle != nullptr && subtitle[0] != '\0') {
        add_label(tile, subtitle, 38, 82, &lv_font_montserrat_18, lv_color_hex(0xb7c2c8));
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

    // Fixed product-version tag at the bottom-right of every page. Baseline is
    // aligned with the page-indicator dots: dot center y = 438 + 10/2 = 443,
    // montserrat_16 line_height 18 / base_line 3 -> text baseline sits 15px
    // below the label top, so label top y = 443 - 15 = 428.
    std::string ver = read_product_version();
    lv_obj_t* ver_label = add_label(tile, ver.empty() ? "--" : ver.c_str(), 620, 428,
                                    &lv_font_montserrat_16, lv_color_hex(0xb7c2c8));
    lv_obj_set_width(ver_label, 138);
    lv_obj_set_style_text_align(ver_label, LV_TEXT_ALIGN_RIGHT, 0);
    g_tiles.page_version_label[col] = ver_label;

    g_tiles.page[col] = tile;
    return tile;
}

void build_bitcoin_tile(lv_obj_t* tv) {
    const TileSpec& spec = kTiles[0];
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = create_tile_base(tv, 0, spec);

    // Bottom-left live clock, same baseline as the page-indicator dots
    // (center y=443) and the bottom-right version tag. montserrat_18:
    // line_height 21 / base_line 4 -> baseline 17px below top -> y = 443 - 17
    // = 426. Left-aligned so the left edge stays pinned at x=42.
    g_tiles.clock_time_label = add_label(tile, "--:--:--", 42, 426,
                                         &lv_font_montserrat_18, lv_color_hex(0xf4efe2));
    lv_obj_set_width(g_tiles.clock_time_label, 120);
    lv_obj_set_style_text_align(g_tiles.clock_time_label, LV_TEXT_ALIGN_LEFT, 0);

    add_label(tile, "Sync Flow", 42, 130, &lv_font_montserrat_18, lv_color_hex(0xf0b44c));
    for (uint32_t index = 0; index < kBitcoinStageCount; ++index) {
        const int32_t row_y = 164 + static_cast<int32_t>(index) * 33;
        g_tiles.bitcoin_stage_dot[index] = create_stage_dot(tile, 50, row_y + 7);
        g_tiles.bitcoin_stage_label[index] = add_label(tile, kBitcoinStageLabels[index], 76, row_y,
                                                       &lv_font_montserrat_18, lv_color_hex(0x78848b));
        lv_obj_set_width(g_tiles.bitcoin_stage_label[index], 190);
    }
    update_bitcoin_stage_list(0);

    lv_obj_t* progress_card = create_card_frame(tile, 300, 142, 216, 96, "Progress", accent);
    lv_obj_t* peers_card = create_card_frame(tile, 540, 142, 216, 96, "Peers", accent);
    g_tiles.bitcoin_height = create_card_frame(tile, 300, 256, 456, 88, "Blocks / Header", accent,
                                               &g_tiles.bitcoin_height_title);
    // DSEG7 digital-tube font for the Bitcoind value cards. Its glyph set
    // has no comma/slash/percent, so those are drawn in montserrat instead,
    // and font_32 (not _36) is used so the digits fit inside the card
    // without clipping/wrapping at the bottom.
    g_bitcoin_progress_roll = create_rolling_number(progress_card, 0, 30, 184, 8,
                                                    &dseg7c700_font_32, lv_color_hex(0xf4efe2));
    g_bitcoin_peers_roll = create_rolling_number(peers_card, 0, 30, 184, 7,
                                                 &dseg7c700_font_32, lv_color_hex(0xf4efe2));
    set_rolling_number_immediate(g_bitcoin_progress_roll, "--");
    set_rolling_number_immediate(g_bitcoin_peers_roll, "--");

    lv_point_t progress_num_size {};
    lv_text_get_size(&progress_num_size, "100", &dseg7c700_font_32, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    // Bottom-align the "%" with the digits: shift it down by the height
    // difference so both share the same baseline at the bottom of the card.
    lv_point_t progress_pct_size {};
    lv_text_get_size(&progress_pct_size, "%", &lv_font_montserrat_32, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    g_tiles.bitcoin_progress_unit = add_label(progress_card, "%",
                                              progress_num_size.x + 4,
                                              34 + (progress_num_size.y - progress_pct_size.y),
                                              &lv_font_montserrat_32, lv_color_hex(0x9aa6ac));

    g_bitcoin_height_current_roll = create_rolling_number(g_tiles.bitcoin_height, 0, 24, 156, 6,
                                                          &dseg7c700_font_32, lv_color_hex(0x78848b));
    set_rolling_number_immediate(g_bitcoin_height_current_roll, "--");
    g_tiles.bitcoin_height_sep = add_label(g_tiles.bitcoin_height, "/", 161, 24,
                                           &lv_font_montserrat_32, lv_color_hex(0x9aa6ac));
    g_bitcoin_height_total_roll = create_rolling_number(g_tiles.bitcoin_height, 184, 24, 190, 7,
                                                        &dseg7c700_font_32, lv_color_hex(0xf4efe2));
    set_rolling_number_immediate(g_bitcoin_height_total_roll, "");
    lv_obj_add_flag(g_tiles.bitcoin_height_sep, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_bitcoin_height_total_roll.root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* block_time_card = create_card_frame(tile, 300, 362, 456, 58, "Block Time", accent,
                                                  &g_tiles.bitcoin_block_date_title);
    lv_obj_align(g_tiles.bitcoin_block_date_title, LV_ALIGN_LEFT_MID, 0, 0);
    g_tiles.bitcoin_block_date = add_label(block_time_card, "--", 0, 0, &lv_font_montserrat_18,
                                           lv_color_hex(0xf4efe2));
    lv_obj_set_width(g_tiles.bitcoin_block_date, 112);
    lv_label_set_long_mode(g_tiles.bitcoin_block_date, LV_LABEL_LONG_MODE_DOTS);
    g_tiles.bitcoin_block_time = add_label(block_time_card, "", 0, 0, &lv_font_montserrat_18,
                                           lv_color_hex(0x78848b));
    lv_obj_set_width(g_tiles.bitcoin_block_time, 140);
    lv_label_set_long_mode(g_tiles.bitcoin_block_time, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align_to(g_tiles.bitcoin_block_date, g_tiles.bitcoin_block_date_title, LV_ALIGN_OUT_RIGHT_MID, 25, 0);
    lv_obj_align_to(g_tiles.bitcoin_block_time, g_tiles.bitcoin_block_date, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
}

void build_stratum_tile(lv_obj_t* tv) {
    const TileSpec& spec = kTiles[1];
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = create_tile_base(tv, 1, spec);

    g_tiles.stratum_url_label = add_label(tile, "stratum+tcp://-", 38, 82, &lv_font_montserrat_18, lv_color_hex(0xf4efe2));
    lv_obj_set_width(g_tiles.stratum_url_label, 500);
    lv_label_set_long_mode(g_tiles.stratum_url_label, LV_LABEL_LONG_MODE_DOTS);

    g_tiles.stratum_state_dot = create_status_dot(tile, 548, 85, 14);
    g_tiles.stratum_state_label = nullptr;

    // Panel bottom must stay above the page-indicator dots (fixed at y=438).
    // (42,114) + 312 = 426, leaving a safe gap to the dots.
    lv_obj_t* chart_panel = lv_obj_create(tile);
    make_static(chart_panel);
    lv_obj_set_pos(chart_panel, 42, 114);
    lv_obj_set_size(chart_panel, 716, 312);
    lv_obj_set_style_radius(chart_panel, 20, 0);
    lv_obj_set_style_bg_color(chart_panel, lv_color_hex(0x0f171d), 0);
    lv_obj_set_style_bg_opa(chart_panel, LV_OPA_70, 0);
    lv_obj_set_style_border_width(chart_panel, 1, 0);
    lv_obj_set_style_border_color(chart_panel, lv_color_hex(0x2f4250), 0);
    lv_obj_set_style_pad_all(chart_panel, 16, 0);

    g_tiles.stratum_total_hashrate_unit = add_label(tile, "", 0, 0, &lv_font_montserrat_22, lv_color_hex(0x9aa6ac));
    lv_obj_set_width(g_tiles.stratum_total_hashrate_unit, 72);
    lv_obj_set_style_text_align(g_tiles.stratum_total_hashrate_unit, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align_to(g_tiles.stratum_total_hashrate_unit, chart_panel, LV_ALIGN_OUT_TOP_RIGHT, 0, -4);
    g_tiles.stratum_total_hashrate = add_label(tile, "--", 0, 0, &dseg7c700_font_32, lv_color_hex(0xf0b44c));
    lv_obj_set_width(g_tiles.stratum_total_hashrate, 120);
    lv_obj_set_style_text_align(g_tiles.stratum_total_hashrate, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(g_tiles.stratum_total_hashrate, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_align_to(g_tiles.stratum_total_hashrate, g_tiles.stratum_total_hashrate_unit, LV_ALIGN_OUT_LEFT_TOP, -4, -13);

    g_tiles.stratum_legend_5m = add_label(chart_panel, "5m", 0, 0, &lv_font_montserrat_16, lv_color_hex(0x51c7a3));
    g_tiles.stratum_legend_6h = add_label(chart_panel, "6h", 0, 0, &lv_font_montserrat_16, lv_color_hex(0x7fa16c));
    g_tiles.stratum_legend_1d = add_label(chart_panel, "1d", 0, 0, &lv_font_montserrat_16, lv_color_hex(0xc66f4f));
    // Legend row centered horizontally at the top of the chart area.
    lv_obj_align(g_tiles.stratum_legend_6h, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_align_to(g_tiles.stratum_legend_5m, g_tiles.stratum_legend_6h, LV_ALIGN_OUT_LEFT_MID, -18, 0);
    lv_obj_align_to(g_tiles.stratum_legend_1d, g_tiles.stratum_legend_6h, LV_ALIGN_OUT_RIGHT_MID, 18, 0);
    g_tiles.stratum_chart_hint = add_label(chart_panel, "Waiting for hashrate history...", 0, 145,
                                           &lv_font_montserrat_18, lv_color_hex(0x78848b));
    lv_obj_set_width(g_tiles.stratum_chart_hint, 656);
    lv_obj_set_style_text_align(g_tiles.stratum_chart_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(g_tiles.stratum_chart_hint, LV_OBJ_FLAG_HIDDEN);

    // Chart height 240 (y 26..266): leaves room for the x-axis tick labels
    // (y 272..286) inside the panel content area (312 - 2*16 pad = 280).
    // Width 632: right edge at x=52+632=684, leaving a 16px margin inside the
    // panel content right edge (716 - 16 pad = 700) so the rightmost x-tick is
    // never clipped by the panel border.
    g_tiles.stratum_chart = lv_chart_create(chart_panel);
    make_static(g_tiles.stratum_chart);
    lv_obj_set_pos(g_tiles.stratum_chart, 52, 26);
    lv_obj_set_size(g_tiles.stratum_chart, 632, 240);
    lv_obj_set_style_radius(g_tiles.stratum_chart, 0, 0);
    lv_obj_set_style_bg_color(g_tiles.stratum_chart, lv_color_hex(0x0f171d), 0);
    lv_obj_set_style_bg_opa(g_tiles.stratum_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_tiles.stratum_chart, 0, 0);
    // Zero padding so content area = full 240px; grid lines land exactly at
    // y = 26 + 240*i/4, matching the Y-tick label positions.
    lv_obj_set_style_pad_all(g_tiles.stratum_chart, 0, 0);
    lv_obj_set_style_size(g_tiles.stratum_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(g_tiles.stratum_chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_color(g_tiles.stratum_chart, lv_color_hex(0x26333b), LV_PART_MAIN);
    lv_chart_set_type(g_tiles.stratum_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(g_tiles.stratum_chart, LV_CHART_UPDATE_MODE_SHIFT);
    // 5 horizontal divisions -> 5 grid lines at 60px spacing (26/86/146/206/266),
    // matching the 5 Y-axis tick values exactly.
    lv_chart_set_div_line_count(g_tiles.stratum_chart, 5, 4);
    lv_chart_set_point_count(g_tiles.stratum_chart, 32);
    lv_chart_set_axis_range(g_tiles.stratum_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 20000);
    g_tiles.stratum_series_5m = lv_chart_add_series(g_tiles.stratum_chart, lv_color_hex(0x51c7a3), LV_CHART_AXIS_PRIMARY_Y);
    g_tiles.stratum_series_6h = lv_chart_add_series(g_tiles.stratum_chart, lv_color_hex(0x7fa16c), LV_CHART_AXIS_PRIMARY_Y);
    g_tiles.stratum_series_1d = lv_chart_add_series(g_tiles.stratum_chart, lv_color_hex(0xc66f4f), LV_CHART_AXIS_PRIMARY_Y);
    std::vector<int32_t> zeros(32, 0);
    lv_chart_set_series_values(g_tiles.stratum_chart, g_tiles.stratum_series_5m, zeros.data(), zeros.size());
    lv_chart_set_series_values(g_tiles.stratum_chart, g_tiles.stratum_series_6h, zeros.data(), zeros.size());
    lv_chart_set_series_values(g_tiles.stratum_chart, g_tiles.stratum_series_1d, zeros.data(), zeros.size());
    lv_chart_refresh(g_tiles.stratum_chart);

    // Unit sits above the topmost Y tick (1.00 at y=18) so it never overlaps the
    // value. Brighter than the tick values (0x78848b) so the unit reads as a
    // distinct label.
    g_tiles.stratum_y_unit = add_label(chart_panel, "GH/s", 13, 0, &lv_font_montserrat_14, lv_color_hex(0xb7c2c8));
    lv_obj_set_width(g_tiles.stratum_y_unit, 44);
    lv_obj_set_style_text_align(g_tiles.stratum_y_unit, LV_TEXT_ALIGN_LEFT, 0);
    for (size_t i = 0; i < 5; ++i) {
        g_tiles.stratum_y_ticks[i] = add_label(chart_panel, "0", 0, 0, &lv_font_montserrat_14, lv_color_hex(0x78848b));
        lv_obj_set_width(g_tiles.stratum_y_ticks[i], 44);
        lv_obj_set_style_text_align(g_tiles.stratum_y_ticks[i], LV_TEXT_ALIGN_RIGHT, 0);
    }
    // Y ticks: grid lines at y = 26, 86, 146, 206, 266 (5 lines, 60px spacing).
    // montserrat_14: line_height 16, cap_height 10, base_line 3 -> digit visual
    // center sits 8px below the label top. Offset -8 so the digit center aligns
    // with the grid line.
    lv_obj_set_pos(g_tiles.stratum_y_ticks[0], 0, 18);
    lv_obj_set_pos(g_tiles.stratum_y_ticks[1], 0, 78);
    lv_obj_set_pos(g_tiles.stratum_y_ticks[2], 0, 138);
    lv_obj_set_pos(g_tiles.stratum_y_ticks[3], 0, 198);
    lv_obj_set_pos(g_tiles.stratum_y_ticks[4], 0, 258);

    g_tiles.stratum_x_ticks[0] = add_label(chart_panel, "--", 0, 0, &lv_font_montserrat_14, lv_color_hex(0x78848b));
    g_tiles.stratum_x_ticks[1] = add_label(chart_panel, "--", 0, 0, &lv_font_montserrat_14, lv_color_hex(0x78848b));
    g_tiles.stratum_x_ticks[2] = add_label(chart_panel, "--", 0, 0, &lv_font_montserrat_14, lv_color_hex(0x78848b));
    lv_obj_set_width(g_tiles.stratum_x_ticks[0], 120);
    lv_obj_set_width(g_tiles.stratum_x_ticks[1], 120);
    lv_obj_set_width(g_tiles.stratum_x_ticks[2], 120);
    lv_obj_set_style_text_align(g_tiles.stratum_x_ticks[0], LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_align(g_tiles.stratum_x_ticks[1], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(g_tiles.stratum_x_ticks[2], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align_to(g_tiles.stratum_x_ticks[0], g_tiles.stratum_chart, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_align_to(g_tiles.stratum_x_ticks[1], g_tiles.stratum_chart, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
    lv_obj_align_to(g_tiles.stratum_x_ticks[2], g_tiles.stratum_chart, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 6);
}

void build_system_tile(lv_obj_t* tv) {
    const TileSpec& spec = kTiles[2];
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = create_tile_base(tv, 2, spec);

    // Three instrument-cluster gauges: CPU load, memory, thermal.
    constexpr int32_t kGaugeSize = 168;
    constexpr int32_t kGaugeY = 116;
    constexpr int32_t kGaugeX[3] = {46, 316, 586};

    GaugeWidgets cpu = create_gauge(tile, kGaugeX[0], kGaugeY, kGaugeSize, "CPU LOAD", accent);
    g_tiles.system_cpu_arc = cpu.arc;
    g_tiles.system_cpu_value = cpu.value_label;
    g_tiles.system_cpu_sub = cpu.sub_label;

    GaugeWidgets mem = create_gauge(tile, kGaugeX[1], kGaugeY, kGaugeSize, "MEMORY", accent);
    g_tiles.system_mem_arc = mem.arc;
    g_tiles.system_mem_value = mem.value_label;
    g_tiles.system_mem_sub = mem.sub_label;

    GaugeWidgets temp = create_gauge(tile, kGaugeX[2], kGaugeY, kGaugeSize, "THERMAL", accent);
    g_tiles.system_temp_arc = temp.arc;
    g_tiles.system_temp_value = temp.value_label;
    g_tiles.system_temp_sub = temp.sub_label;

    // Storage/swap usage bars below the gauges.
    constexpr int32_t kBarY = 350;
    BarWidgets disk_bar = create_bar(tile, 46, kBarY, "BLOCK DISK", accent);
    g_tiles.system_disk_pct = disk_bar.pct_label;
    g_tiles.system_disk_fill = disk_bar.fill;
    g_tiles.system_disk_detail = disk_bar.detail_label;

    BarWidgets swap_bar = create_bar(tile, 424, kBarY, "SWAP", lv_color_hex(0x6f8fae));
    g_tiles.system_swap_pct = swap_bar.pct_label;
    g_tiles.system_swap_fill = swap_bar.fill;
    g_tiles.system_swap_detail = swap_bar.detail_label;

    // Full-page dimming scrim: sits just above the background image (index 0) so the
    // whole page reads darker and the coin art no longer competes with the data.
    lv_obj_t* page_scrim = lv_obj_create(tile);
    make_static(page_scrim);
    lv_obj_remove_style_all(page_scrim);
    lv_obj_set_pos(page_scrim, 0, 0);
    lv_obj_set_size(page_scrim, kUiWidth, kUiHeight);
    lv_obj_set_style_bg_color(page_scrim, lv_color_hex(0x0a141b), 0);
    lv_obj_set_style_bg_opa(page_scrim, LV_OPA_50, 0);
    lv_obj_move_to_index(page_scrim, 1);

    // Bottom-left uptime, styled identically to the bottom-right version tag
    // (montserrat_16, 0xb7c2c8, baseline y=428) so the footer reads as a
    // symmetric status strip: uptime (left) / dots (center) / version (right).
    g_tiles.system_uptime = add_label(tile, "--", 42, 428, &lv_font_montserrat_16, lv_color_hex(0xb7c2c8));
    lv_obj_set_width(g_tiles.system_uptime, 360);
}

void build_network_tile(lv_obj_t* tv) {
    const TileSpec& spec = kTiles[3];
    const lv_color_t accent = lv_color_hex(spec.accent);
    lv_obj_t* tile = create_tile_base(tv, 3, spec);

    lv_obj_t* page_scrim = lv_obj_create(tile);
    make_static(page_scrim);
    lv_obj_remove_style_all(page_scrim);
    lv_obj_set_pos(page_scrim, 0, 0);
    lv_obj_set_size(page_scrim, kUiWidth, kUiHeight);
    lv_obj_set_style_bg_color(page_scrim, lv_color_hex(0x081416), 0);
    lv_obj_set_style_bg_opa(page_scrim, LV_OPA_50, 0);
    lv_obj_move_to_index(page_scrim, 1);

    lv_obj_t* settings_button = lv_btn_create(tile);
    lv_obj_clear_flag(settings_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(settings_button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(settings_button, 708, 24);
    lv_obj_set_size(settings_button, 52, 52);
    ui_style_primary_button(settings_button);
    lv_obj_set_style_bg_color(settings_button, lv_color_hex(0xe8784f), 0);
    lv_obj_set_style_shadow_color(settings_button, lv_color_hex(0xe8784f), 0);
    lv_obj_set_style_bg_color(settings_button, lv_color_hex(0xb95737), LV_STATE_PRESSED);
    g_tiles.network_settings_icon = ui_button_label(settings_button, LV_SYMBOL_SETTINGS, 0xffffff);

    GaugeWidgets signal = create_gauge(tile, 322, 128, 156, "CONNECTION", accent);
    g_tiles.network_signal_arc = signal.arc;
    g_tiles.network_signal_value = signal.value_label;
    lv_obj_set_style_text_font(g_tiles.network_signal_value, &lv_font_montserrat_22, 0);
    lv_obj_set_pos(g_tiles.network_signal_value, 322, 174);
    lv_obj_del(signal.sub_label);

    lv_obj_t* connection_line = lv_obj_create(tile);
    make_static(connection_line);
    lv_obj_remove_style_all(connection_line);
    lv_obj_set_pos(connection_line, 0, 288);
    lv_obj_set_size(connection_line, kUiWidth, 34);
    lv_obj_set_style_pad_column(connection_line, 8, 0);
    lv_obj_set_flex_flow(connection_line, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(connection_line, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    g_tiles.network_iface_icon = add_label(connection_line, LV_SYMBOL_WIFI, 0, 0,
                                           &lv_font_montserrat_22, lv_color_hex(0xff9a72));
    g_tiles.network_signal_sub = add_label(connection_line, "--", 0, 0,
                                            &lv_font_montserrat_18, lv_color_hex(0x9aa6ac));
    lv_label_set_long_mode(g_tiles.network_signal_sub, LV_LABEL_LONG_MODE_DOTS);

    constexpr int32_t kNodeY = 366;
    constexpr int32_t kNodeCenterX[3] = {144, 400, 656};
    constexpr int32_t kNodeColumnWidth = 220;
    for (int32_t index = 0; index < 2; ++index) {
        lv_obj_t* link = lv_obj_create(tile);
        make_static(link);
        lv_obj_remove_style_all(link);
        lv_obj_set_pos(link, kNodeCenterX[index] + 7, kNodeY + 6);
        lv_obj_set_size(link, kNodeCenterX[index + 1] - kNodeCenterX[index] - 14, 2);
        lv_obj_set_style_bg_color(link, lv_color_hex(0x526269), 0);
        lv_obj_set_style_bg_opa(link, LV_OPA_COVER, 0);
    }

    g_tiles.network_device_dot = create_status_dot(tile, kNodeCenterX[0] - 7, kNodeY, 14);
    g_tiles.network_gateway_dot = create_status_dot(tile, kNodeCenterX[1] - 7, kNodeY, 14);
    g_tiles.network_dns_dot = create_status_dot(tile, kNodeCenterX[2] - 7, kNodeY, 14);
    lv_obj_t* device_title = add_label(tile, "DEVICE", kNodeCenterX[0] - 110, 336,
                                       &lv_font_montserrat_14, lv_color_hex(0x78848b));
    lv_obj_t* gateway_title = add_label(tile, "GATEWAY", kNodeCenterX[1] - 110, 336,
                                        &lv_font_montserrat_14, lv_color_hex(0x78848b));
    lv_obj_t* dns_title = add_label(tile, "DNS", kNodeCenterX[2] - 110, 336,
                                    &lv_font_montserrat_14, lv_color_hex(0x78848b));
    lv_obj_t* node_titles[] = {device_title, gateway_title, dns_title};
    for (lv_obj_t* title : node_titles) {
        lv_obj_set_width(title, kNodeColumnWidth);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    }
    g_tiles.network_device_value = add_label(tile, "--", kNodeCenterX[0] - 110, 387,
                                              &lv_font_montserrat_18,
                                              lv_color_hex(0xf4efe2));
    g_tiles.network_device_mac = add_label(tile, "--", kNodeCenterX[0] - 110, 416,
                                            &lv_font_montserrat_14,
                                            lv_color_hex(0x9aa6ac));
    g_tiles.network_gateway = add_label(tile, "--", kNodeCenterX[1] - 110, 387,
                                         &lv_font_montserrat_18,
                                         lv_color_hex(0xf4efe2));
    g_tiles.network_dns = add_label(tile, "--", kNodeCenterX[2] - 110, 387,
                                     &lv_font_montserrat_18,
                                     lv_color_hex(0xf4efe2));
    lv_obj_t* node_values[] = {
        g_tiles.network_device_value,
        g_tiles.network_device_mac,
        g_tiles.network_gateway,
        g_tiles.network_dns,
    };
    for (lv_obj_t* value : node_values) {
        lv_obj_set_width(value, kNodeColumnWidth);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_label_set_long_mode(g_tiles.network_dns, LV_LABEL_LONG_MODE_DOTS);

    // --- Throughput block: vertical DOWN/UP rates in the left blank area ---
    constexpr int32_t kTrafficX = 100;
    constexpr int32_t kDownY = 168;
    constexpr int32_t kUpY = 205;
    g_tiles.network_down_arrow = add_label(tile, LV_SYMBOL_DOWN, kTrafficX, kDownY,
                                           &lv_font_montserrat_22, lv_color_hex(0x59d79b));
    g_tiles.network_down_value = add_label(tile, "--", kTrafficX + 30, kDownY,
                                           &lv_font_montserrat_22, lv_color_hex(0xf4efe2));
    g_tiles.network_up_arrow = add_label(tile, LV_SYMBOL_UP, kTrafficX, kUpY,
                                         &lv_font_montserrat_22, lv_color_hex(0xe8784f));
    g_tiles.network_up_value = add_label(tile, "--", kTrafficX + 30, kUpY,
                                         &lv_font_montserrat_22, lv_color_hex(0xf4efe2));
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

    sync_process_timezone();
    bool dirty = false;
    dirty |= set_label(g_tiles.clock_time_label, current_time_text("%H:%M:%S"));
    if (dirty) {
        g_page_cache_dirty[0] = true;
    }
}

bool update_height_card(const HeightCardModel& card) {
    bool dirty = false;

    dirty |= set_label(g_tiles.bitcoin_height_title, card.title);

    std::string left_value;
    std::string right_value;
    split_pair_value(card.value, left_value, right_value);
    // DSEG7 has no ',' or '/' glyph: numbers are stripped of thousands
    // separators and the separator is drawn in a regular font instead.
    left_value = strip_commas(left_value);
    right_value = strip_commas(right_value);
    const bool show_total = !right_value.empty();

    dirty |= set_rolling_number(g_bitcoin_height_current_roll, left_value);
    dirty |= set_label(g_tiles.bitcoin_height_sep, show_total ? "/" : "");
    dirty |= set_rolling_number(g_bitcoin_height_total_roll, right_value);
    dirty |= set_hidden(g_tiles.bitcoin_height_sep, !show_total);
    dirty |= set_hidden(g_bitcoin_height_total_roll.root, !show_total);
    return dirty;
}

uint32_t apply_status_unavailable_model() {
    bool dirty = false;
    dirty |= set_rolling_number(g_bitcoin_progress_roll, "--");
    dirty |= set_hidden(g_tiles.bitcoin_progress_unit, true);
    dirty |= set_label(g_tiles.bitcoin_height_title, "Blocks / Header");
    dirty |= set_rolling_number(g_bitcoin_height_current_roll, "--");
    dirty |= set_hidden(g_tiles.bitcoin_height_sep, true);
    dirty |= set_hidden(g_bitcoin_height_total_roll.root, true);
    dirty |= set_label(g_tiles.bitcoin_block_date_title, "Block Time");
    dirty |= set_label(g_tiles.bitcoin_block_date, "--");
    dirty |= set_label(g_tiles.bitcoin_block_time, "");
    return dirty ? page_dirty_bit(0) : 0;
}

uint32_t apply_ui_model(const UiModel& model) {
    uint32_t dirty_mask = 0;
    bool dirty = false;

    dirty |= update_bitcoin_stage_list(model.bitcoin.stage_index);
    dirty |= set_rolling_number(g_bitcoin_progress_roll, model.bitcoin.progress);
    lv_obj_set_x(g_tiles.bitcoin_progress_unit,
                 rolling_number_width(g_bitcoin_progress_roll, model.bitcoin.progress) + 4);
    dirty |= set_hidden(g_tiles.bitcoin_progress_unit, !is_rolling_number(model.bitcoin.progress));
    dirty |= update_height_card(model.bitcoin.height);
    dirty |= set_rolling_number(g_bitcoin_peers_roll, model.bitcoin.peers);
    dirty |= set_label(g_tiles.bitcoin_block_date_title, "Block Time");
    dirty |= set_label(g_tiles.bitcoin_block_date, model.bitcoin.block_date);
    dirty |= set_label(g_tiles.bitcoin_block_time, model.bitcoin.block_time);
    if (dirty) {
        dirty_mask |= page_dirty_bit(0);
    }

    dirty = false;
    dirty |= update_stratum_chart(model.stratum);
    dirty |= set_label(g_tiles.stratum_url_label, model.stratum.url.empty() ? "-" : model.stratum.url);
    set_stratum_state_visual(model.stratum.state_class, model.stratum.state);
    g_miners_overlay.set_snapshot(model.stratum.miners_snapshot);
    if (dirty) {
        dirty_mask |= page_dirty_bit(1);
    }

    dirty = false;
    dirty |= set_gauge(g_tiles.system_cpu_arc, g_tiles.system_cpu_value, model.system.cpu_value,
                       model.system.cpu_text, threshold_color(model.system.cpu_value, 70, 90, 0x8fb7ff));
    dirty |= set_label(g_tiles.system_cpu_sub, model.system.cpu_sub);
    dirty |= set_gauge(g_tiles.system_mem_arc, g_tiles.system_mem_value, model.system.mem_value,
                       model.system.mem_text, threshold_color(model.system.mem_value, 75, 90, 0x8fb7ff));
    dirty |= set_label(g_tiles.system_mem_sub, model.system.mem_sub);
    dirty |= set_gauge(g_tiles.system_temp_arc, g_tiles.system_temp_value, model.system.temp_value,
                       model.system.temp_text, threshold_color(model.system.temp_value, 64, 86, 0x8fb7ff));
    dirty |= set_label(g_tiles.system_temp_sub, model.system.temp_sub);
    dirty |= set_bar(g_tiles.system_disk_fill, g_tiles.system_disk_pct, model.system.disk_value,
                     model.system.disk_pct_text);
    dirty |= set_label(g_tiles.system_disk_detail, model.system.disk_detail);
    dirty |= set_bar(g_tiles.system_swap_fill, g_tiles.system_swap_pct, model.system.swap_value,
                     model.system.swap_pct_text);
    dirty |= set_label(g_tiles.system_swap_detail, model.system.swap_detail);
    dirty |= set_label(g_tiles.system_uptime, model.system.uptime);
    if (dirty) {
        dirty_mask |= page_dirty_bit(2);
    }

    dirty = false;
    const uint32_t signal_color = model.network.signal_level >= 4 ? 0x59d79b
                                : model.network.signal_level >= 2 ? 0xf0b44c
                                                                 : 0xd65a5a;
    dirty |= set_label(g_tiles.network_iface_icon, model.network.iface_icon);
    dirty |= set_text_color(g_tiles.network_iface_icon,
                            model.network.device_up ? 0xff9a72 : 0x526269);
    dirty |= set_gauge(g_tiles.network_signal_arc, g_tiles.network_signal_value,
                       model.network.signal_value, model.network.signal_text, signal_color);
    dirty |= set_label(g_tiles.network_signal_sub, model.network.signal_sub);
    g_network_settings_wifi_summary = model.network.device_up
        ? model.network.ssid + "  " + model.network.rssi
        : "Not connected";
    g_network_settings_wifi_enabled = model.network.iface_icon == LV_SYMBOL_WIFI;
    g_network_settings_timezone_summary = model.network.timezone + "  " +
                                          model.network.timezone_offset;
    dirty |= set_label(g_tiles.network_device_value, model.network.ip);
    dirty |= set_label(g_tiles.network_device_mac, model.network.mac);
    dirty |= set_label(g_tiles.network_gateway, model.network.gateway);
    dirty |= set_label(g_tiles.network_dns, model.network.dns);
    lv_obj_t* network_dots[] = {
        g_tiles.network_device_dot,
        g_tiles.network_gateway_dot,
        g_tiles.network_dns_dot,
    };
    const bool network_states[] = {
        model.network.device_up,
        model.network.gateway_up,
        model.network.dns_up,
    };
    for (size_t index = 0; index < 3; ++index) {
        dirty |= set_network_status_dot(network_dots[index], network_states[index]);
    }
    // TRAFFIC: down/up throughput
    const bool has_rx = model.network.net_rx_display != "--" && !model.network.net_rx_display.empty();
    const bool has_tx = model.network.net_tx_display != "--" && !model.network.net_tx_display.empty();
    dirty |= set_label(g_tiles.network_down_value, has_rx ? model.network.net_rx_display.c_str() : "--");
    dirty |= set_label(g_tiles.network_up_value, has_tx ? model.network.net_tx_display.c_str() : "--");
    dirty |= set_text_color(g_tiles.network_down_arrow, has_rx ? 0x59d79b : 0x526269);
    dirty |= set_text_color(g_tiles.network_up_arrow, has_tx ? 0xe8784f : 0x526269);
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
    g_peer_map_ips = model.bitcoin.peer_ips;
    g_peer_map_inbound = model.bitcoin.peer_inbound;
    g_peer_map_self_geo = model.bitcoin.self_geo;

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

        set_live_page_overlays_hidden(index, true);
        lv_draw_buf_t* snapshot = lv_snapshot_take(page, LV_COLOR_FORMAT_ARGB8888);
        if (snapshot == nullptr) {
            std::fprintf(stderr, "rockbase-lcdui: snapshot failed for page %u\n", index);
            set_live_page_overlays_hidden(index, false);
            continue;
        }

        const uint32_t child_count = lv_obj_get_child_count(page);
        for (uint32_t child_index = 0; child_index < child_count; ++child_index) {
            lv_obj_t* child = lv_obj_get_child(page, child_index);
            if (!is_live_page_overlay(index, child)) {
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
            }
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
        set_live_page_overlays_hidden(index, false);
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
    g_timezone_overlay.set_status_refresh_request(request_status_refresh);
    refresh_clock_cb(nullptr);
    refresh_status_cb(nullptr);
    cache_tile_pages();
    lv_timer_create(refresh_clock_cb, 1000, nullptr);
    lv_timer_create(refresh_status_cb, 5000, nullptr);
}

// Boot progress: driven by /run/rockbase/boot-progress.json, written by
// init scripts (S03/S40/S43/S61/S62/S99) via rockbase_boot_progress.sh.
struct BootStageEntry {
    int stage = 0;
    int pct = 0;
    std::string message;
    uint32_t color = 0xffffff;
};

struct BootProgressState {
    int target_progress = 0;
    int stage = -1;
    int total_stages = 6;
    float display_progress = 0.0f;
    std::string message = "Booting...";
    uint32_t color = 0xffffff;
    bool ready = false;  // all init stages done (S99) -> enter dashboard

    // Replay buffer: every stage that has arrived, in arrival order. The UI
    // plays them back one at a time (each held for kBootStageMinHoldMs) so
    // that fast init scripts (S43->S61->S62->S99 finish in <1s) don't skip
    // past a stage on screen.
    std::vector<BootStageEntry> stages;
    size_t play_index = 0;
    uint32_t play_start_ms = 0;
};

BootProgressState g_boot_progress {};

void poll_boot_progress() {
    std::string json;
    if (!read_text_file(kBootProgressPath, json, 4096)) {
        return;
    }
    const std::string pct = json_value(json, "progress", "0");
    int value = atoi(pct.c_str());
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    g_boot_progress.target_progress = value;
    const std::string msg = json_value(json, "message", "");
    if (!msg.empty()) {
        g_boot_progress.message = msg;
    }
    const std::string color = json_value(json, "color", "");
    if (color.size() == 7 && color[0] == '#') {
        g_boot_progress.color = static_cast<uint32_t>(std::stoul(color.substr(1), nullptr, 16));
    }
    // S99showversion writes stage 5 -> all init scripts have run.
    const int stage = atoi(json_value(json, "stage", "0").c_str());
    g_boot_progress.stage = stage;
    const int total = atoi(json_value(json, "total_stages", "6").c_str());
    if (total > 0) {
        g_boot_progress.total_stages = total;
    }
    if (stage >= g_boot_progress.total_stages - 1) {
        g_boot_progress.ready = true;
    }
    // Append to the replay buffer when a new entry arrives. Allow multiple
    // entries per stage (e.g. "Starting..." then "Done") so the UI shows
    // both the start and completion of each phase.
    if (stage >= 0) {
        const bool is_new = g_boot_progress.stages.empty() ||
            g_boot_progress.stages.back().stage != stage ||
            g_boot_progress.stages.back().message != g_boot_progress.message;
        if (is_new) {
            BootStageEntry e;
            e.stage = stage;
            e.pct = value;
            e.message = g_boot_progress.message;
            e.color = g_boot_progress.color;
            g_boot_progress.stages.push_back(e);
        }
    }
}

// Advance the replay playback: hold each stage for kBootStageMinHoldMs before
// moving to the next, so every stage is visible even when init is fast.
void advance_boot_playback(uint32_t now) {
    BootProgressState& bp = g_boot_progress;
    if (bp.stages.empty()) {
        return;
    }
    if (bp.play_index >= bp.stages.size()) {
        bp.play_index = bp.stages.size() - 1;
        return;
    }
    if (bp.play_start_ms == 0) {
        bp.play_start_ms = now;
    }
    if (now - bp.play_start_ms >= kBootStageMinHoldMs &&
        bp.play_index + 1 < bp.stages.size()) {
        bp.play_index++;
        bp.play_start_ms = now;
    }
}

const BootStageEntry& current_boot_stage(const BootProgressState& bp) {
    static const BootStageEntry kEmpty {};
    if (bp.stages.empty()) {
        return kEmpty;
    }
    size_t idx = bp.play_index;
    if (idx >= bp.stages.size()) {
        idx = bp.stages.size() - 1;
    }
    return bp.stages[idx];
}

void update_boot_progress_ui(lv_obj_t* bar, lv_obj_t* pct_label, lv_obj_t* msg_label) {
    BootProgressState& bp = g_boot_progress;
    const BootStageEntry& cur = current_boot_stage(bp);
    const bool has_stage = !bp.stages.empty();
    const int target_pct = has_stage ? cur.pct : bp.target_progress;
    // Exponential ease toward the currently-playing stage's pct.
    const float delta = static_cast<float>(target_pct) - bp.display_progress;
    if (delta > 0.5f) {
        bp.display_progress += delta * 0.4f;
    }
    else {
        bp.display_progress = static_cast<float>(target_pct);
    }
    if (bp.display_progress > 100.0f) {
        bp.display_progress = 100.0f;
    }
    lv_bar_set_value(bar, static_cast<int32_t>(bp.display_progress + 0.5f), LV_ANIM_OFF);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(bp.display_progress + 0.5f));
    lv_label_set_text(pct_label, buf);
    // Position the % label at the tip of the bar (clamped to bar bounds).
    const int32_t bar_x = lv_obj_get_x(bar);
    const int32_t bar_y = lv_obj_get_y(bar);
    const int32_t bar_w = lv_obj_get_width(bar);
    const int32_t label_w = lv_obj_get_width(pct_label);
    int32_t label_x = bar_x + static_cast<int32_t>(bar_w * bp.display_progress / 100.0f) - label_w / 2;
    if (label_x < bar_x) {
        label_x = bar_x;
    }
    if (label_x > bar_x + bar_w - label_w) {
        label_x = bar_x + bar_w - label_w;
    }
    lv_obj_set_pos(pct_label, label_x, bar_y + lv_obj_get_height(bar) + 6);
    // Details with stage counter: "[2/6] Status cache ready"
    const int shown_stage = has_stage ? cur.stage : (bp.stage < 0 ? 0 : bp.stage);
    const std::string& shown_msg = has_stage ? cur.message : bp.message;
    const uint32_t shown_color = has_stage ? cur.color : bp.color;
    char full[160];
    std::snprintf(full, sizeof(full), "[%d/%d] %s",
                  shown_stage + 1, bp.total_stages, shown_msg.c_str());
    lv_label_set_text(msg_label, full);
    lv_obj_set_style_text_color(msg_label, lv_color_hex(shown_color), 0);
}

lv_obj_t* build_boot_scene(lv_obj_t* screen) {
    if (g_boot_logo.ready) {
        lv_obj_t* img = lv_image_create(screen);
        lv_obj_remove_style_all(img);
        lv_image_set_src(img, &g_boot_logo.dsc);
        lv_obj_set_pos(img, 0, 0);
        lv_obj_set_size(img, kUiWidth, kUiHeight);
        make_static(img);

        // Progress bar centered on screen (NMAxe style).
        constexpr int32_t kBarW = 480;
        constexpr int32_t kBarH = 8;
        constexpr int32_t kBarX = (kUiWidth - kBarW) / 2;
        constexpr int32_t kBarY = kUiHeight / 2;  // vertical center

        lv_obj_t* bar = lv_bar_create(screen);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_obj_set_size(bar, kBarW, kBarH);
        lv_obj_set_pos(bar, kBarX, kBarY);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x3a4148), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_60, 0);
        lv_obj_set_style_radius(bar, 4, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0xf0b44c), LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
        g_boot_bar = bar;

        // Percentage label follows the bar tip (NMAxe style).
        lv_obj_t* pct_label = lv_label_create(screen);
        lv_label_set_text(pct_label, "0%");
        lv_obj_set_style_text_font(pct_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(pct_label, lv_color_hex(0xf4efe2), 0);
        lv_obj_clear_flag(pct_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(pct_label, kBarX, kBarY + kBarH + 6);
        g_boot_pct_label = pct_label;

        // Details text: bottom-left aligned, larger font (NMAxe style).
        lv_obj_t* msg_label = lv_label_create(screen);
        lv_label_set_text(msg_label, g_boot_progress.message.c_str());
        lv_obj_set_style_text_font(msg_label, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(msg_label, lv_color_hex(g_boot_progress.color), 0);
        lv_obj_clear_flag(msg_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(msg_label, LV_ALIGN_BOTTOM_LEFT, 24, -16);
        g_boot_msg_label = msg_label;

        // Overlay product version at bottom-right, read from /etc/version/product_version
        std::string ver = read_product_version();
        if (!ver.empty()) {
            lv_obj_t* ver_label = lv_label_create(screen);
            lv_label_set_text(ver_label, ver.c_str());
            lv_obj_set_style_text_font(ver_label, &lv_font_montserrat_32, 0);
            lv_obj_set_style_text_color(ver_label, lv_color_hex(0xffffff), 0);
            lv_obj_clear_flag(ver_label, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_move_to_index(ver_label, lv_obj_get_child_count(screen) - 1);
            lv_obj_align(ver_label, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
        }
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

bool calib_ok() {
    int fd = open("/sys/bus/iio/devices/iio:device0/in_voltage3_raw", O_RDONLY);
    if (fd < 0) return false;
    int v[3] = {-1, -1, -1};
    for (int i = 0; i < 3; ++i) {
        char buf[16] = {};
        lseek(fd, 0, SEEK_SET);
        if (read(fd, buf, 15) > 0) v[i] = atoi(buf);
        usleep(20000);
    }
    close(fd);
    std::sort(v, v + 3);
    return v[1] >= 683 && v[1] <= 910;
}

void run_hold_screen() {
    init_backlight_gpio(1);
    if (!init_display()) {
        cleanup_display();
        for (;;) sleep(3600);
    }
    lv_display_t* disp = nullptr;
    if (!init_lv_display(&disp)) {
        cleanup_display();
        for (;;) sleep(3600);
    }
    lv_obj_t* screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_t* label = lv_label_create(screen);
    lv_label_set_text(label, "Cloned Hardware");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xf0b44c), 0);
    lv_obj_center(label);
    lv_refr_now(disp);
    set_backlight(1);
    while (!g_stop) {
        uint32_t next_ms = lv_timer_handler();
        uint32_t sleep_us = next_ms * 1000U;
        if (sleep_us < kMinLvSleepUs) sleep_us = kMinLvSleepUs;
        if (sleep_us > kMaxLvSleepUs) sleep_us = kMaxLvSleepUs;
        usleep(sleep_us);
    }
    cleanup_display();
}

}  // namespace

int main() {
    if (!calib_ok()) {
        run_hold_screen();
        return 0;
    }
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    sync_process_timezone();

    const bool backlight_ready = init_backlight_gpio(0);
    buzzer_init();

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
    // Wait for system readiness (boot-progress.json, final stage = S99),
    // with a minimum logo hold, a per-stage minimum hold (so every stage is
    // visible), a stall timeout (a stage that never advances), and a maximum
    // wait as final fallback.
    {
        const uint32_t boot_start = tick_ms();
        int last_seen_stage = -1;
        uint32_t last_stage_change_ms = boot_start;
        while (!g_stop) {
            const uint32_t now = tick_ms();
            const uint32_t elapsed = now - boot_start;
            poll_boot_progress();
            // Detect stage stalls: if the stage value stops advancing for
            // kBootStageStallMs (e.g. bitcoind deferred waiting for resize),
            // fall through to the dashboard instead of blocking forever.
            if (g_boot_progress.stage != last_seen_stage) {
                last_seen_stage = g_boot_progress.stage;
                last_stage_change_ms = now;
            }
            const bool stalled = (now - last_stage_change_ms) >= kBootStageStallMs;
            // Advance the replay playback (hold each stage for the min hold).
            advance_boot_playback(now);
            const bool playback_done =
                (g_boot_progress.play_index + 1 >= g_boot_progress.stages.size());
            const bool current_held =
                (g_boot_progress.play_start_ms != 0 &&
                 now - g_boot_progress.play_start_ms >= kBootStageMinHoldMs);
            if (elapsed >= kBootMinHoldMs && playback_done && current_held &&
                (g_boot_progress.ready || stalled || elapsed >= kBootMaxWaitMs)) {
                break;
            }
            if (g_boot_bar != nullptr) {
                update_boot_progress_ui(g_boot_bar, g_boot_pct_label, g_boot_msg_label);
            }
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
        std::printf("rockbase-lcdui: boot wait done after %ums (ready=%d, stages=%zu)\n",
                    static_cast<unsigned>(tick_ms() - boot_start),
                    g_boot_progress.ready ? 1 : 0,
                    g_boot_progress.stages.size());
        std::fflush(stdout);
    }
    build_tile_stage(screen);
    build_tile_caches_from_snapshots();
    lv_obj_move_to_index(boot_layer, lv_obj_get_child_count(screen) - 1);
    lv_obj_fade_out(boot_layer, kDashboardFadeMs, 0);
    run_lv_for(kDashboardFadeMs + 80);
    lv_obj_delete(boot_layer);
    g_show_page_indicator = true;
    lv_refr_now(disp);

    std::printf("rockbase-lcdui: running lvgl tile UI\n");
    std::fflush(stdout);

    while (!g_stop) {
        const uint32_t now_ms = tick_ms();
        const bool interacting = ui_interacting(now_ms);
        if (g_network_settings_overlay_open_pending && !g_network_settings_overlay.active()) {
            g_network_settings_overlay.open(g_network_settings_wifi_summary,
                                            g_network_settings_timezone_summary,
                                            g_network_settings_wifi_enabled);
            g_network_settings_overlay_open_pending = false;
            g_network_settings_overlay_wait_release = true;
        }
        if (g_wifi_config_overlay_open_pending && !g_wifi_config_overlay.active()) {
            g_wifi_config_overlay.open();
            g_wifi_config_overlay_open_pending = false;
            g_wifi_config_overlay_wait_release = true;
        }
        if (g_timezone_overlay_open_pending && !g_timezone_overlay.active()) {
            g_timezone_overlay.open();
            g_timezone_overlay_open_pending = false;
        }
        if (g_miners_overlay_open_pending && !g_miners_overlay.active()) {
            g_miners_overlay.open();
            g_miners_overlay_open_pending = false;
        }
        if (g_peer_map_overlay_open_pending && !g_peer_map_overlay.active()) {
            g_peer_map_overlay.open(g_peer_map_ips, g_peer_map_self_geo, g_peer_map_inbound);
            g_peer_map_overlay_open_pending = false;
        }
        g_network_settings_overlay.update();
        const NetworkSettingsOverlay::Action settings_action =
            g_network_settings_overlay.take_action();
        if (settings_action == NetworkSettingsOverlay::Action::Wifi) {
            g_network_settings_child = settings_action;
            g_wifi_config_overlay_open_pending = true;
        }
        else if (settings_action == NetworkSettingsOverlay::Action::Timezone) {
            g_network_settings_child = settings_action;
            g_timezone_overlay_open_pending = true;
        }
        g_wifi_config_overlay.update();
        g_timezone_overlay.update();
        if (g_network_settings_child == NetworkSettingsOverlay::Action::Wifi &&
            !g_wifi_config_overlay.active() && !g_wifi_config_overlay_open_pending) {
            g_network_settings_child = NetworkSettingsOverlay::Action::None;
            g_network_settings_overlay_open_pending = true;
        }
        else if (g_network_settings_child == NetworkSettingsOverlay::Action::Timezone &&
                 !g_timezone_overlay.active() && !g_timezone_overlay_open_pending) {
            g_network_settings_child = NetworkSettingsOverlay::Action::None;
            g_network_settings_overlay_open_pending = true;
        }
        g_miners_overlay.update();
        g_peer_map_overlay.update();
        g_ota_overlay.update();  // self-manages open/close from /run/rockbase/ota-state.json
        // Passive OTA check: periodically runs check-async in the background
        // and pops a prompt only when a genuinely newer version is available.
        // Skipped while the OTA overlay is active (upgrade in progress).
        if (!g_ota_overlay.active()) {
            g_firmware_update_overlay.update();
        }

        process_deferred_refreshes_if_idle();
        refresh_one_dirty_page_if_idle();

        bool custom_present = false;
        if (!g_network_settings_overlay.active() && !g_wifi_config_overlay.active() &&
            !g_timezone_overlay.active() && !g_miners_overlay.active() &&
            !g_peer_map_overlay.active() &&
            !g_ota_overlay.active() && !g_firmware_update_overlay.active() && g_touch.down) {
            const int32_t scroll_x = visual_scroll_x_from_touch();
            if (g_swipe_drag_started) {
                custom_present = present_pager_frame(scroll_x);
            }
        }
        else if (!g_network_settings_overlay.active() && !g_wifi_config_overlay.active() &&
             !g_timezone_overlay.active() && !g_miners_overlay.active() &&
                 !g_peer_map_overlay.active() &&
                 !g_ota_overlay.active() && !g_firmware_update_overlay.active() && g_pager_anim_active) {
            custom_present = present_pager_snap_anim(now_ms);
        }

        g_custom_frame_presenting = !g_network_settings_overlay.active() &&
                        !g_wifi_config_overlay.active() && !g_timezone_overlay.active() &&
                                    !g_miners_overlay.active() && !g_peer_map_overlay.active() &&
                                    !g_ota_overlay.active() &&
                                    !g_firmware_update_overlay.active() &&
                                    (g_touch.down || custom_present);
        uint32_t next_ms = lv_timer_handler();
        g_custom_frame_presenting = false;
        // The OTA overlay has no touch interaction and can stay up for many
        // minutes; it must not force the no-sleep high-framerate mode.
        if (interacting && !g_ota_overlay.active()) {
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
