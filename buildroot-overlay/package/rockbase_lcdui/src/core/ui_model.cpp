#include "ui_model.h"

#include <cmath>

#include "format_utils.h"
#include "json_parse.h"

namespace lcdui {

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

bool build_ui_model(UiModel& model, const char* status_path, const char* miners_path,
                    const char* history_path) {
    DashboardData data;
    if (!load_dashboard_data(data, status_path, miners_path, history_path)) {
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
    model.network.iface_icon = wired ? kSymbolUsb : kSymbolWifi;
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
        ? std::string(kSymbolBullet) + "  USB Ethernet"
        : std::string(kSymbolBullet) + "  " + data.wifi_ssid +
              "  " + std::string(kSymbolBullet) + "  " + data.rssi_display;
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

}  // namespace lcdui
