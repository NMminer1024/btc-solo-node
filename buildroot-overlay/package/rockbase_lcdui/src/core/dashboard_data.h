#pragma once

// Status data layer: reads the JSON caches written by rockbase_statusd.sh
// and the stratum service into a flat DashboardData struct.
// Pure standard C++ (no LVGL) so this layer can be unit-tested on the host.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace lcdui {

// Shared data-only types. The LVGL overlays alias these
// (PeerMapOverlay::Geo / MinersOverlay::Miner / MinersOverlay::Snapshot)
// so UI code keeps using the familiar qualified names.
struct PeerGeo {
    double lat = 0.0;
    double lon = 0.0;
    std::string city;
    std::string country;
    bool known = false;

    bool operator==(const PeerGeo& o) const {
        return lat == o.lat && lon == o.lon && city == o.city &&
               country == o.country && known == o.known;
    }
};

struct MinerInfo {
    std::string address = "-";
    bool online = false;
    int workers = 0;
    std::string hashrate_5m = "--";
    std::string best_share = "--";
    std::string last_share_ago = "-";
};

struct MinersSnapshot {
    int total_miners = 0;
    int online_miners = 0;
    int total_workers = 0;
    std::string total_hashrate_5m = "--";
    std::vector<MinerInfo> miners;
};

constexpr const char* kStatusCachePath = "/tmp/status-api.json";
constexpr const char* kMinersCachePath = "/run/rockbase/miners.json";
constexpr const char* kMinersHistoryCachePath = "/run/rockbase/miners-history-node.json";
constexpr const char* kProductVersionPath = "/etc/version/product_version";
constexpr const char* kTimezoneFilePath = "/etc/TZ";
constexpr size_t kStratumChartMaxPoints = 500;

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
    PeerGeo self_geo;
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
    MinersSnapshot miners_snapshot;
};


bool read_text_file(const char* path, std::string& out, size_t max_size = 65536);
std::string read_product_version();
std::string read_timezone_key();
std::string timezone_posix_spec(const std::string& key);
// The cache paths default to the on-board locations; the host unit tests
// pass fixture files instead. On-board behavior is unchanged.
bool load_miners_chart_data(DashboardData& data,
                            const char* miners_path = kMinersCachePath,
                            const char* history_path = kMinersHistoryCachePath);
bool load_dashboard_data(DashboardData& data,
                         const char* status_path = kStatusCachePath,
                         const char* miners_path = kMinersCachePath,
                         const char* history_path = kMinersHistoryCachePath);

}  // namespace lcdui
