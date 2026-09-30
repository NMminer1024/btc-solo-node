#pragma once

// Presentation model: converts raw DashboardData into per-page display
// models. Pure standard C++ (no LVGL) so this layer can be unit-tested on
// the host.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "dashboard_data.h"

namespace lcdui {

// Glyph constants mirroring LVGL's lv_symbol_def.h (staging LVGL v9 build:
// LV_SYMBOL_BULLET / LV_SYMBOL_WIFI / LV_SYMBOL_USB). Kept as raw UTF-8 so
// the model layer stays buildable without LVGL. lv_conf.h does not override
// these; if that ever changes, keep the values in sync.
constexpr const char kSymbolBullet[] = "\xE2\x80\xA2";
constexpr const char kSymbolWifi[] = "\xEF\x87\xAB";
constexpr const char kSymbolUsb[] = "\xEF\x8a\x87";

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
    PeerGeo self_geo;
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
    MinersSnapshot miners_snapshot;
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
    std::string iface_icon = kSymbolWifi;
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


uint32_t bitcoin_stage_index(const DashboardData& data);
HeightCardModel make_height_card_model(const DashboardData& data, const std::string& blocks);
bool build_ui_model(UiModel& model);

}  // namespace lcdui
