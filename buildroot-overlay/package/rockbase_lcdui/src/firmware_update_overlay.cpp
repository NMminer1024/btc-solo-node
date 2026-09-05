#include "firmware_update_overlay.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

namespace {

constexpr const char* kCheckStatePath = "/run/rockbase/ota-check-state.json";
constexpr uint32_t kPollIntervalMs = 1000;
constexpr uint32_t kCheckTimeoutMs = 25000;

uint32_t monotonic_ms() {
    timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

// Same minimal file/JSON helpers as ota_overlay.cpp / wifi_config_overlay.cpp.
std::string read_file(const char* path) {
    std::string out;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return out;
    }
    char buf[1024];
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        out.append(buf, static_cast<size_t>(n));
        if (out.size() > 16384) {
            break;
        }
    }
    close(fd);
    return out;
}

std::string json_value_at(const std::string& src, size_t pos, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    pos = src.find(needle, pos);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) ++pos;
    if (pos >= src.size() || src[pos] != '"') return {};
    ++pos;
    std::string out;
    bool esc = false;
    for (; pos < src.size(); ++pos) {
        const char c = src[pos];
        if (esc) {
            out += c;
            esc = false;
        }
        else if (c == '\\') {
            esc = true;
        }
        else if (c == '"') {
            break;
        }
        else {
            out += c;
        }
    }
    return out;
}

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        }
        else {
            out += c;
        }
    }
    out += "'";
    return out;
}

}  // namespace

void FirmwareUpdateOverlay::open() {
    if (phase_ == Phase::Checking || confirm_.active()) {
        return;  // a check or confirm prompt is already in progress
    }
    current_version_.clear();
    latest_version_.clear();
    manifest_url_.clear();
    message_.clear();
    phase_ = Phase::Checking;
    check_start_ms_ = monotonic_ms();
    // Delay (not 0) so the first poll doesn't race the not-yet-started
    // background process and misread a stale leftover state file.
    last_poll_ms_ = check_start_ms_;
    // Drop any leftover result from a previous run before spawning a new one -
    // otherwise a poll that lands before check-async writes "checking" could
    // misread stale state from an earlier check.
    unlink(kCheckStatePath);
    show_message("Check for Updates", "Checking for the latest firmware...");
    // rockbase-ota.sh backgrounds itself (trailing &), so this returns almost
    // immediately - safe to call from the LVGL loop without blocking it.
    if (std::system("nohup rockbase-ota.sh check-async >/tmp/rockbase-ota-check.log 2>&1 &") != 0) {
        std::fprintf(stderr, "rockbase-lcdui: failed to launch rockbase-ota.sh check-async\n");
    }
}

void FirmwareUpdateOverlay::update() {
    confirm_.update();
    if (confirm_.active()) {
        const ConfirmOverlay::Result result = confirm_.consume_result();
        if (result == ConfirmOverlay::Result::Confirmed) {
            start_apply();
            confirm_.close();
            phase_ = Phase::Hidden;
        }
        else if (result == ConfirmOverlay::Result::Cancelled) {
            confirm_.close();
            phase_ = Phase::Hidden;
        }
        return;
    }

    if (overlay_.visible() && overlay_.close_requested()) {
        overlay_.clear_close_requested();
        overlay_.hide();
        phase_ = Phase::Hidden;
        return;
    }

    if (phase_ != Phase::Checking) {
        return;
    }

    const uint32_t now = monotonic_ms();
    if (now - check_start_ms_ > kCheckTimeoutMs) {
        phase_ = Phase::Failed;
        show_message("Check for Updates",
                     "Timed out waiting for a response. Check the network and try again.");
        return;
    }
    if (last_poll_ms_ != 0 && now - last_poll_ms_ < kPollIntervalMs) {
        return;
    }
    last_poll_ms_ = now;
    poll_state_file();
}

void FirmwareUpdateOverlay::poll_state_file() {
    // rockbase-ota.sh check-async writes this file atomically (tmp + rename).
    const std::string json = read_file(kCheckStatePath);
    if (json.empty()) {
        return;  // the background check hasn't written anything yet
    }
    const std::string phase_name = json_value_at(json, 0, "phase");
    if (phase_name.empty() || phase_name == "checking") {
        return;  // still in progress
    }

    current_version_ = json_value_at(json, 0, "current_version");
    latest_version_ = json_value_at(json, 0, "latest_version");
    manifest_url_ = json_value_at(json, 0, "manifest_url");
    message_ = json_value_at(json, 0, "message");

    if (phase_name == "up_to_date") {
        phase_ = Phase::UpToDate;
        overlay_.hide();  // swap the plain message modal for a yes/no prompt
        // Debug-stage escape hatch: re-flash the same version without having
        // to bump VERSION for every test iteration. Safe to leave enabled -
        // apply always writes the inactive slot, never the running one.
        const std::string msg = std::string("You're already on the latest version (") +
                                 (current_version_.empty() ? "--" : current_version_) +
                                 "). Update anyway? (debug)";
        confirm_.open("Already Up To Date", msg.c_str(), "Update Anyway", nullptr);
    }
    else if (phase_name == "update_available") {
        phase_ = Phase::UpdateAvailable;
        overlay_.hide();  // swap the plain message modal for a yes/no prompt
        const std::string msg = std::string("A new version ") + latest_version_ +
                                 " is available (current: " + current_version_ + "). Update now?";
        confirm_.open("Update Available", msg.c_str(), "Update", "Not Now");
    }
    else {
        phase_ = Phase::Failed;
        show_message("Check for Updates",
                     message_.empty() ? "Could not check for updates." : message_);
    }
}

void FirmwareUpdateOverlay::show_message(const char* title, const std::string& message) {
    lv_obj_t* content = overlay_.show(title);
    if (content == nullptr) {
        return;
    }
    lv_obj_t* msg = lv_label_create(content);
    lv_label_set_text(msg, message.c_str());
    lv_obj_set_pos(msg, 24, 86);
    lv_obj_set_width(msg, 712);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xc7d2d8), 0);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
}

void FirmwareUpdateOverlay::start_apply() {
    if (manifest_url_.empty()) {
        return;
    }
    const std::string cmd = "nohup rockbase-ota.sh apply " + shell_quote(manifest_url_) +
                            " --force >/tmp/rockbase-ota-apply-lcdui.log 2>&1 &";
    if (std::system(cmd.c_str()) != 0) {
        std::fprintf(stderr, "rockbase-lcdui: failed to launch rockbase-ota.sh apply\n");
    }
}
