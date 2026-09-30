// Host-side unit tests for the LVGL-free core layer (json_parse,
// format_utils, dashboard_data, ui_model). Built and run on the host only
// via `make test` in src/Makefile — never cross-compiled, never shipped.
//
// Fixture files in core/test/fixtures/ are real captures from the board, so
// the end-to-end cases double as a field-by-field regression net for the
// hand-rolled JSON scan helpers.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/dashboard_data.h"
#include "core/format_utils.h"
#include "core/json_parse.h"
#include "core/ui_model.h"

using namespace lcdui;

static int g_pass = 0;
static int g_fail = 0;

static void record(bool ok, const std::string& detail, const char* file, int line) {
    if (ok) {
        ++g_pass;
        return;
    }
    ++g_fail;
    std::printf("FAIL %s:%d: %s\n", file, line, detail.c_str());
}

#define CHECK(cond) record((cond), #cond, __FILE__, __LINE__)
#define CHECKS(expr, want)                                                    \
    record((expr) == (want),                                                  \
         std::string("expected <") + (want) + "> got <" + (expr) + ">",       \
         __FILE__, __LINE__)
#define CHECKD(expr, want) record(feq((expr), (want)), #expr, __FILE__, __LINE__)

static bool feq(double a, double b) { return std::fabs(a - b) <= 1e-9; }
static void set_tz(const char* tz) {
    setenv("TZ", tz, 1);
    tzset();
}

// ---------------------------------------------------------------------------
// json_parse
// ---------------------------------------------------------------------------

static void test_json_unescape() {
    CHECKS(json_unescape("plain"), "plain");
    CHECKS(json_unescape("a\\nb"), "a b");    // quirk: \n -> space
    CHECKS(json_unescape("a\\rb"), "ab");     // quirk: \r -> dropped
    CHECKS(json_unescape("a\\tb"), "a b");    // quirk: \t -> space
    CHECKS(json_unescape("a\\\"b"), "a\"b");
    CHECKS(json_unescape("a\\\\b"), "a\\b");
    CHECKS(json_unescape("a\\/b"), "a/b");
    CHECKS(json_unescape("a\\qb"), "aqb");    // unknown escape -> raw char
    CHECKS(json_unescape("trailing\\"), "trailing\\");
}

static void test_json_object_and_value() {
    const std::string json =
        "  { \"bitcoin\": { \"blocks\": \"969,304\", \"peers\": [\"1.2.3.4\"], "
        "\"self_geo\": { \"city\": \"Singapore\" } }, \"mining\": { \"state\": 1 } }";

    const std::string bitcoin = json_object(json, "bitcoin");
    CHECK(!bitcoin.empty());
    CHECKS(json_value(bitcoin, "blocks"), "969,304");
    CHECKS(json_value(bitcoin, "missing", "fb"), "fb");
    // Quirk: non-string values scan to the next ,/} — an array yields its raw text.
    CHECKS(json_value(bitcoin, "peers"), "[\"1.2.3.4\"]");

    // Nested object survives as a brace-balanced slice.
    const std::string geo = json_object(bitcoin, "self_geo");
    CHECKS(json_value(geo, "city"), "Singapore");
    CHECK(json_object(json, "nope").empty());
    CHECK(json_object(bitcoin, "peers").empty());  // array, not object

    // Scalars: numbers, booleans, null.
    const std::string obj =
        "{ \"n\": 42, \"d\": -1.5, \"flag\": true, \"nil\": null, \"s\": \"x y\" }";
    CHECKS(json_value(obj, "n"), "42");
    CHECKS(json_value(obj, "d"), "-1.5");
    CHECKS(json_value(obj, "flag"), "true");
    CHECKS(json_value(obj, "nil", "fb"), "fb");
    CHECKS(json_value(obj, "s"), "x y");
    CHECKD(json_double(obj, "d", 0.0), -1.5);
    CHECKD(json_double(obj, "nokey", 7.5), 7.5);
    CHECK(json_int_value(obj, "n") == 42);
    CHECK(json_int_value(obj, "s", -3) == -3);
}

static void test_json_arrays() {
    const std::string json = "{ \"ips\": [\"1.1.1.1\", \"2.2.2.2\", \"3.3.3.3\"] }";
    const auto ips = json_string_array(json_array(json, "ips"));
    CHECK(ips.size() == 3);
    if (ips.size() == 3) {
        CHECKS(ips[0], "1.1.1.1");
        CHECKS(ips[2], "3.3.3.3");
    }
    CHECK(json_string_array("[]").empty());
    CHECK(json_string_array("").empty());

    const std::string rows_json =
        "[ {\"a\": 1, \"note\": \"} in string\"}, {\"b\": 2} , {\"c\": 3}]";
    const auto rows = json_array_objects(rows_json);
    CHECK(rows.size() == 3);
    if (rows.size() == 3) {
        CHECKS(rows[1], "{\"b\": 2}");
        CHECK(json_value(rows[0], "a") == "1");
        // The stray '}' inside the string must not terminate row 0.
        CHECKS(json_value(rows[0], "note"), "} in string");
    }
}

static void test_int_parsers() {
    CHECK(parse_int_or("14", 0) == 14);
    CHECK(parse_int_or("2.6", 0) == 3);      // lround
    CHECK(parse_int_or("-5.2", 0) == -5);
    CHECK(parse_int_or("-", 99) == 99);
    CHECK(parse_int_or("", 99) == 99);
    CHECK(parse_int_or("abc", 99) == 99);
    CHECK(parse_leading_int("969,304 x") == 969);
    CHECK(parse_leading_int("abc", -1) == -1);
    CHECK(parse_leading_int("") == -1);
}

// ---------------------------------------------------------------------------
// format_utils
// ---------------------------------------------------------------------------

static void test_clamp_and_temp() {
    CHECK(clamp_int(-5, 0, 100) == 0);
    CHECK(clamp_int(150, 0, 100) == 100);
    CHECK(clamp_int(42, 0, 100) == 42);

    bool ok = false;
    CHECKD(parse_temp_celsius("41.3C", &ok), 41.3);
    CHECK(ok);
    CHECK(!parse_temp_celsius("N/A", &ok) && !ok);
    CHECK(!ok);
    ok = true;
    CHECKD(parse_temp_celsius("-", &ok), 0.0);
    CHECK(!ok);

    const uint32_t normal = 0x112233;
    CHECK(threshold_color(60.0, 65.0, 80.0, normal) == normal);
    CHECK(threshold_color(65.0, 65.0, 80.0, normal) == 0xf0b44c);
    CHECK(threshold_color(79.9, 65.0, 80.0, normal) == 0xf0b44c);
    CHECK(threshold_color(80.0, 65.0, 80.0, normal) == 0xd65a5a);
}

static void test_hashrate_parse() {
    CHECKD(parse_hashrate_ghs("4.05T"), 4050.0);
    CHECKD(parse_hashrate_ghs("1.95t"), 1950.0);
    CHECKD(parse_hashrate_ghs("71.5G"), 71.5);
    CHECKD(parse_hashrate_ghs("300M"), 0.3);
    CHECKD(parse_hashrate_ghs("5K"), 5e-6);
    CHECKD(parse_hashrate_ghs("2P"), 2e6);
    CHECKD(parse_hashrate_ghs("12"), 12.0);
    CHECKD(parse_hashrate_ghs("-"), 0.0);
    CHECKD(parse_hashrate_ghs(""), 0.0);
    CHECKD(parse_hashrate_ghs("xyz"), 0.0);
}

static void check_vec(const std::vector<int32_t>& got, const std::vector<int32_t>& want,
                      const char* file, int line) {
    record(got == want,
           got == want ? "vec ok" : "vector mismatch",
           file, line);
}
#define CHECKV(got, want) check_vec((got), (want), __FILE__, __LINE__)

static void test_downsample() {
    std::vector<int32_t> ten;
    for (int i = 0; i < 10; ++i) ten.push_back(i * 10);  // 0..90
    CHECKV(downsample_history_series(ten, 4), (std::vector<int32_t>{0, 30, 60, 90}));
    CHECKV(downsample_history_points(ten, 4), (std::vector<int32_t>{0, 30, 60, 90}));
    CHECKV(downsample_history_series(ten, 10), ten);  // size == max -> as-is
    CHECKV(downsample_history_series(ten, 20), ten);  // size <  max -> as-is
    CHECKV(downsample_history_series(ten, 1), ten);   // max < 2    -> as-is
}

static void test_hashrate_display() {
    HashrateDisplay d;

    d = format_hashrate_display(999.9);
    CHECKD(d.scaled, 999.9);
    CHECKS(std::string(d.unit), "GH/s");

    d = format_hashrate_display(1500.0);
    CHECKD(d.scaled, 1.5);
    CHECKS(std::string(d.unit), "TH/s");

    d = format_hashrate_display(2e6);
    CHECKD(d.scaled, 2.0);
    CHECKS(std::string(d.unit), "PH/s");

    // Rolls all the way up to the top unit.
    d = format_hashrate_display(1e9);
    CHECKD(d.scaled, 1.0);
    CHECKS(std::string(d.unit), "EH/s");

    d = format_hashrate_display(-5.0);
    CHECKD(d.scaled, 0.0);
    CHECKS(std::string(d.unit), "GH/s");
}

static void test_hashrate_value_parts() {
    HashrateValueParts p;

    p = format_hashrate_value_parts(6.6);
    CHECKS(p.int_part, "6");
    CHECKS(p.frac_part, "60");

    p = format_hashrate_value_parts(99.97);
    CHECKS(p.int_part, "100");   // "%.1f" rounds 99.97 up to 100.0
    CHECKS(p.frac_part, "0");

    p = format_hashrate_value_parts(1234.0);
    CHECKS(p.int_part, "1");     // rolled to 1.23 TH/s
    CHECKS(p.frac_part, "23");

    p = format_hashrate_value_parts(999.97);
    CHECKS(p.int_part, "1000");  // quirk: %.0f rounds but unit does not roll
    CHECKS(p.frac_part, "");

    p = format_hashrate_value_parts(0.0);
    CHECKS(p.int_part, "0");
    CHECKS(p.frac_part, "00");
}

static void test_share_and_misc_format() {
    CHECKS(format_share_value(0.0), "--");
    CHECKS(format_share_value(-3.0), "--");
    CHECKS(format_share_value(999.0), "999");
    CHECKS(format_share_value(1500.0), "1.50K");
    CHECKS(format_share_value(45463369.34774818), "45.46M");
    CHECKS(format_share_value(183631500.29927883), "183.63M");
    CHECKS(format_share_value(1935989115.258747), "1.94G");
    CHECKS(format_share_value(2.5e12), "2.50T");

    CHECKS(join_value("a", " / ", "b"), "a / b");
    CHECKS(join_value("-", " / ", "b"), "b");
    CHECKS(join_value("a", " / ", "N/A"), "a");
    CHECKS(join_value("-", " / ", "-"), "-");
    CHECKS(join_value("", " / ", "", "fb"), "fb");

    CHECK(!has_value(""));
    CHECK(!has_value("-"));
    CHECK(has_value("0"));

    CHECKS(strip_commas("969,304"), "969304");
    CHECK(is_all_digits("123"));
    CHECK(!is_all_digits("12a"));
    CHECK(!is_all_digits(""));
    CHECK(is_rolling_number("12.5"));
    CHECK(is_rolling_number("0"));
    CHECK(!is_rolling_number("12.5.6"));
    CHECK(!is_rolling_number("12x"));
    CHECK(!is_rolling_number(""));

    CHECKS(format_progress("100.000000"), "100");
    CHECKS(format_progress("100"), "100");
    CHECKS(format_progress("42.123456"), "42.123");
    CHECKS(format_progress("0.5"), "0.500");
    CHECKS(format_progress("-"), "--");
    CHECKS(format_progress("abc"), "--");

    std::string date, time;
    split_date_time("2026-09-30 12:53:25 UTC", date, time);
    CHECKS(date, "2026-09-30");
    CHECKS(time, "12:53:25 UTC");
    split_date_time("onlydate", date, time);
    CHECKS(date, "onlydate");
    CHECKS(time, "");
    split_date_time("-", date, time);
    CHECKS(date, "--");
    CHECKS(time, "");

    std::string left, right;
    split_pair_value("607M / 945M", left, right);
    CHECKS(left, "607M");
    CHECKS(right, "945M");
    split_pair_value("single", left, right);
    CHECKS(left, "single");
    CHECKS(right, "");
    split_pair_value("-", left, right);
    CHECKS(left, "--");
    CHECKS(right, "");
}

static void test_timezone_helpers() {
    CHECKS(ntp_label("running"), "NTP Ready");
    CHECKS(ntp_label("stale"), "NTP Unsynced");

    CHECKS(timezone_label(""), "UTC");
    CHECKS(timezone_label("-"), "UTC");
    CHECKS(timezone_label("CST-8"), "Asia/Shanghai");
    CHECKS(timezone_label("Asia/Tokyo"), "Asia/Tokyo");

    CHECKS(timezone_offset_label("CST-8"), "+8.0");
    CHECKS(timezone_offset_label("UTC"), "+0.0");
    CHECKS(timezone_offset_label("Asia/Tokyo"), "+9.0");
    CHECKS(timezone_offset_label("America/New_York"), "-5.0");
    CHECKS(timezone_offset_label("Foo/Bar"), "--");

    CHECKS(timezone_posix_spec("UTC"), "UTC0");
    CHECKS(timezone_posix_spec("Asia/Shanghai"), "CST-8");
    CHECKS(timezone_posix_spec("Asia/Tehran"), "<+03:30>-3:30");
    CHECKS(timezone_posix_spec("Nope/Nowhere"), "UTC0");
}

static void test_convert_utc_to_local() {
    std::string date, time;
    bool ok;

    set_tz("UTC0");
    ok = convert_utc_to_local("2026-09-30 12:53:25 UTC", date, time);
    CHECK(ok);
    CHECKS(date, "2026-09-30");
    CHECKS(time, "12:53:25");

    set_tz("CST-8");
    ok = convert_utc_to_local("2026-09-30 12:53:25 UTC", date, time);
    CHECK(ok);
    CHECKS(date, "2026-09-30");
    CHECKS(time, "20:53:25");

    // UTC-8 date rolls back a day.
    set_tz("PST8PDT,M3.2.0/2,M11.1.0/2");
    ok = convert_utc_to_local("2026-09-30 01:00:00 UTC", date, time);
    CHECK(ok);
    CHECKS(date, "2026-09-29");
    CHECKS(time, "18:00:00");

    set_tz("UTC0");
    CHECK(!convert_utc_to_local("not a date", date, time));
    // timegm() < 0 (pre-1970) is rejected.
    CHECK(!convert_utc_to_local("1960-01-01 00:00:00", date, time));
}

// ---------------------------------------------------------------------------
// Fixture-driven end-to-end (real board captures)
// ---------------------------------------------------------------------------

static void test_load_dashboard_data(const std::string& fixdir) {
    const std::string status = fixdir + "/status-api.sample.json";
    const std::string miners = fixdir + "/miners.sample.json";
    const std::string history = fixdir + "/miners-history.sample.json";

    DashboardData data;
    CHECK(load_dashboard_data(data, status.c_str(), miners.c_str(), history.c_str()));

    // bitcoin section
    CHECKS(data.sync_key, "synced");
    CHECKS(data.progress_ring, "100.000000");
    CHECKS(data.blocks_fmt, "969,304");
    CHECKS(data.headers_fmt, "969,304");
    CHECKS(data.best_block_date, "2026-09-30 12:53:25 UTC");
    CHECKS(data.connections, "10");
    CHECK(data.peer_ips.size() == 10);
    CHECK(data.peer_inbound.size() == 10);
    for (const auto& kv : data.peer_inbound) {
        CHECK(!kv.second);  // fixture: all peers outbound
    }
    CHECK(data.self_geo.known);
    CHECKD(data.self_geo.lat, 1.2872);
    CHECKD(data.self_geo.lon, 103.8507);
    CHECKS(data.self_geo.city, "Singapore");
    CHECKS(data.self_geo.country, "Singapore");

    // mining section
    CHECKS(data.ckpool_state, "running");
    CHECKS(data.ckpool_class, "ok");
    CHECKS(data.stratum_url, "stratum+tcp://192.168.124.38:3333");
    CHECKS(data.btc_address, "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1");

    // system section
    CHECKS(data.load_pct, "14");
    CHECKS(data.temp, "41.3C");
    CHECKS(data.mem_pct, "64");
    CHECKS(data.disk_pct, "61");
    CHECKS(data.swap_pct, "91");
    CHECKS(data.swap_used_display, "117M");
    CHECKS(data.uptime, "0d 00h:29m");

    // network section
    CHECKS(data.ip_addr, "192.168.124.38");
    CHECKS(data.iface, "wlan0");
    CHECKS(data.wifi_ssid, "NMTech-2.4G");
    CHECKS(data.rssi_display, "-49 dBm");
    CHECKS(data.rssi_pct, "100");
    CHECK(data.signal_level == 4);
    CHECKS(data.timezone_key, "Asia/Shanghai");
    CHECKS(data.gateway_ip, "192.168.124.1");
    CHECKS(data.dns_primary, "192.168.124.1");
    CHECKS(data.dns_secondary, "-");
    CHECKS(data.net_rx_display, "26.5K/s");

    // miners chart section (miners.json + history)
    CHECKS(data.miners_hashrate_5m, "1.05T");
    CHECKS(data.miners_hashrate_6h, "957G");
    CHECKS(data.miners_hashrate_1d, "1.22T");
    CHECK(data.miners_snapshot.total_workers == 1);
    CHECKS(data.miners_snapshot.total_hashrate_5m, "1.05T");
    CHECK(data.miners_snapshot.total_miners == 3);
    CHECK(data.miners_snapshot.online_miners == 1);
    CHECK(data.miners_snapshot.miners.size() == 3);
    if (data.miners_snapshot.miners.size() == 3) {
        const MinerInfo& first = data.miners_snapshot.miners[0];
        CHECK(first.online);
        CHECKS(first.address, "bc1qslx7k8zu6awkfh5t4qzja2j5v98wez2fnekxe0");
        CHECK(first.workers == 1);
        CHECKS(first.hashrate_5m, "1.25T");
        CHECKS(first.best_share, "45.46M");
        CHECKS(first.last_share_ago, "29s ago");

        const MinerInfo& second = data.miners_snapshot.miners[1];
        CHECK(!second.online);
        CHECKS(second.address, "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1");
        CHECK(second.workers == 9);  // max(worker_detail_count, workers)
        CHECKS(second.best_share, "1.94G");

        const MinerInfo& third = data.miners_snapshot.miners[2];
        CHECK(!third.online);
        CHECKS(third.address, "bc1qk4fzsucmy8k5cu8a30tekzf3n9s58awuq5n2f5");
        CHECK(third.workers == 1);  // max(1 detail, 0 workers)
        CHECKS(third.best_share, "183.63M");
    }
    // 675 history points downsampled to the 500-point chart cap.
    CHECK(data.miners_history_5m.size() == kStratumChartMaxPoints);
    if (!data.miners_history_5m.empty()) {
        CHECK(data.miners_history_5m[0] == 26000);  // "26T"
        CHECK(data.miners_history_ts[0] == 1790219835);
    }
}

static void test_load_edge_cases(const std::string& fixdir) {
    const std::string status = fixdir + "/status-api.sample.json";
    const std::string miners = fixdir + "/miners.sample.json";

    DashboardData data;
    // Missing status file -> false.
    CHECK(!load_dashboard_data(data, "/nonexistent/status.json",
                               miners.c_str(), "/nonexistent/history.json"));
    // Missing miners file -> load_miners_chart_data false.
    CHECK(!load_miners_chart_data(data, "/nonexistent/miners.json",
                                  "/nonexistent/history.json"));
    // Missing history file is not fatal: miners still load.
    DashboardData ok_data;
    CHECK(load_miners_chart_data(ok_data, miners.c_str(), "/nonexistent/history.json"));
    CHECK(ok_data.miners_snapshot.total_miners == 3);
    CHECK(ok_data.miners_history_5m.empty());

    // Missing status through the UI model entry point.
    UiModel model;
    model.status_available = true;
    CHECK(!build_ui_model(model, "/nonexistent/status.json",
                          miners.c_str(), "/nonexistent/history.json"));
    CHECK(!model.status_available);
}

static void test_build_ui_model(const std::string& fixdir) {
    set_tz("UTC0");
    const std::string status = fixdir + "/status-api.sample.json";
    const std::string miners = fixdir + "/miners.sample.json";
    const std::string history = fixdir + "/miners-history.sample.json";

    UiModel model;
    CHECK(build_ui_model(model, status.c_str(), miners.c_str(), history.c_str()));
    CHECK(model.status_available);

    // Bitcoin page
    CHECK(model.bitcoin.stage_index == 6);  // synced
    CHECKS(model.bitcoin.progress, "100");
    CHECKS(model.bitcoin.height.title, "Blocks / Header");
    CHECKS(model.bitcoin.height.value, "969,304 / 969,304");
    CHECKS(model.bitcoin.peers, "10");
    CHECK(model.bitcoin.peer_ips.size() == 10);
    CHECK(model.bitcoin.self_geo.known);
    CHECKS(model.bitcoin.block_date, "2026-09-30");
    CHECKS(model.bitcoin.block_time, "12:53:25");
    CHECKS(model.bitcoin.ip_addr, "192.168.124.38");
    CHECKS(model.bitcoin.log_sync_key, "synced");

    // Stratum page
    CHECKS(model.stratum.state, "running");
    CHECKS(model.stratum.state_class, "ok");
    CHECKS(model.stratum.url, "stratum+tcp://192.168.124.38:3333");
    CHECKS(model.stratum.user, "18dK8EfyepKuS74fs27iuDJWoGUT4rPto1");
    CHECKS(model.stratum.total_hashrate_5m, "1.05T");
    CHECKS(model.stratum.best_share, "45.46M");  // top miner after sort
    CHECK(model.stratum.history_5m.size() == kStratumChartMaxPoints);
    CHECK(model.stratum.miners_snapshot.total_miners == 3);

    // System page
    CHECK(model.system.cpu_value == 14);
    CHECKS(model.system.cpu_text, "14%");
    CHECKS(model.system.cpu_sub, "1m 1.91  5m 1.72");
    CHECK(model.system.mem_value == 64);
    CHECKS(model.system.mem_sub, "607M / 945M");
    CHECKS(model.system.temp_text, "41C");
    CHECK(model.system.temp_value == 30);  // (41.3-20)*100/70 rounded
    CHECKS(model.system.temp_sub, "Normal");
    CHECK(model.system.disk_value == 61);
    CHECKS(model.system.disk_detail, "14.8G / 24.1G");
    CHECK(model.system.swap_value == 91);
    CHECKS(model.system.swap_detail, "117M / 128M");
    CHECKS(model.system.uptime, "0d 00h:29m");

    // Network page (wlan0 -> wifi icon)
    CHECK((model.network.iface_icon == kSymbolWifi));
    CHECK(model.network.device_up);
    CHECK(model.network.gateway_up);
    CHECK(model.network.dns_up);
    CHECK(model.network.signal_value == 100);
    CHECKS(model.network.signal_text, "100%");
    CHECKS(model.network.ssid, "NMTech-2.4G");
    CHECKS(model.network.rssi, "-49 dBm");
    CHECK(model.network.signal_level == 4);
    CHECKS(model.network.timezone, "Asia/Shanghai");
    CHECKS(model.network.timezone_offset, "+8.0");
    CHECKS(model.network.gateway, "192.168.124.1");
    CHECKS(model.network.dns, "192.168.124.1");
    CHECKS(model.network.net_rx_display, "26.5K/s");
    CHECKS(model.network.net_tx_display, "21.6K/s");

    // Stage-index mapping table (synthetic data, not fixtures).
    DashboardData d;
    d.sync_key = "synced";
    CHECK(bitcoin_stage_index(d) == 6);
    d.sync_key = "btc_not_running";
    CHECK(bitcoin_stage_index(d) == 0);
    d = DashboardData();
    d.sync_key = "-";
    d.ibd_stage_key = "loading_index";
    CHECK(bitcoin_stage_index(d) == 1);
    d.ibd_stage_key = "loading_blocks";
    CHECK(bitcoin_stage_index(d) == 2);
    d.ibd_stage_key = "pre_headers";
    CHECK(bitcoin_stage_index(d) == 3);
    d.ibd_stage_key = "sync_headers";
    CHECK(bitcoin_stage_index(d) == 4);
    d.ibd_stage_key = "sync_blocks";
    CHECK(bitcoin_stage_index(d) == 5);
    d.ibd_stage_key = "syncing";
    CHECK(bitcoin_stage_index(d) == 5);

    // Height card variants.
    DashboardData card_d;
    card_d.blocks_fmt = "969,304";
    card_d.headers_fmt = "969,312";
    HeightCardModel card = make_height_card_model(card_d, "969,304 / 969,312");
    CHECKS(card.title, "Blocks / Header");
    CHECKS(card.value, "969,304 / 969,312");
    card_d.ibd_stage_key = "pre_headers";
    card_d.ibd_stage_height = "969,312";
    card = make_height_card_model(card_d, "969,304 / 969,312");
    CHECKS(card.title, "Header");
    card_d.ibd_stage_key = "loading_index";
    card = make_height_card_model(card_d, "x");
    CHECKS(card.title, "Block Index");
    CHECKS(card.value, "Preparing chain");
}

int main(int argc, char** argv) {
    const std::string fixdir = (argc > 1) ? argv[1] : "core/test/fixtures";

    test_json_unescape();
    test_json_object_and_value();
    test_json_arrays();
    test_int_parsers();
    test_clamp_and_temp();
    test_hashrate_parse();
    test_downsample();
    test_hashrate_display();
    test_hashrate_value_parts();
    test_share_and_misc_format();
    test_timezone_helpers();
    test_convert_utc_to_local();
    test_load_dashboard_data(fixdir);
    test_load_edge_cases(fixdir);
    test_build_ui_model(fixdir);

    std::printf("core tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
