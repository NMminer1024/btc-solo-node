#include "dashboard_data.h"

#include <algorithm>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

#include "format_utils.h"
#include "json_parse.h"

namespace lcdui {

bool read_text_file(const char* path, std::string& out, size_t max_size) {
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

bool load_miners_chart_data(DashboardData& data, const char* miners_path,
                            const char* history_path) {
    std::string miners_json;
    if (!read_text_file(miners_path, miners_json, 262144)) {
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
        MinerInfo miner;
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
              [](const MinerInfo& left, const MinerInfo& right) {
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
    if (!read_text_file(history_path, history_json, 262144)) {
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

bool load_dashboard_data(DashboardData& data, const char* status_path,
                         const char* miners_path, const char* history_path) {
    std::string json;
    if (!read_text_file(status_path, json)) {
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
    load_miners_chart_data(data, miners_path, history_path);
    return true;
}

}  // namespace lcdui
