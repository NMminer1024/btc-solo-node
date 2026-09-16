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
constexpr uint32_t kCheckIntervalMs = 6 * 3600 * 1000;  // periodic check-async cadence (6 h)
constexpr uint32_t kFirstCheckDelayMs = 60000;  // first check after boot

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

void FirmwareUpdateOverlay::start_check() {
    last_check_ms_ = monotonic_ms();
    current_version_.clear();
    latest_version_.clear();
    manifest_url_.clear();
    phase_ = Phase::Checking;
    check_start_ms_ = last_check_ms_;
    // Delay (not 0) so the first poll doesn't race the not-yet-started
    // background process and misread a stale leftover state file.
    last_poll_ms_ = check_start_ms_;
    // Drop any leftover result from a previous run before spawning a new one -
    // otherwise a poll that lands before check-async writes "checking" could
    // misread stale state from an earlier check.
    unlink(kCheckStatePath);
    // rockbase-ota.sh backgrounds itself (trailing &), so this returns almost
    // immediately - safe to call from the LVGL loop without blocking it.
    if (std::system("nohup rockbase-ota.sh check-async >/tmp/rockbase-ota-check.log 2>&1 &") != 0) {
        std::fprintf(stderr, "rockbase-lcdui: failed to launch rockbase-ota.sh check-async\n");
        phase_ = Phase::Idle;
    }
}

void FirmwareUpdateOverlay::update() {
    confirm_.update();
    if (confirm_.active()) {
        const ConfirmOverlay::Result result = confirm_.consume_result();
        if (result == ConfirmOverlay::Result::Confirmed) {
            start_apply();
            confirm_.close();
        }
        else if (result == ConfirmOverlay::Result::Cancelled) {
            // "Not Now": if the user ticked "Don't ask me", suppress this
            // version for the rest of the boot cycle; otherwise the next
            // periodic poll will prompt again.
            if (confirm_.dont_ask_checked() && !latest_version_.empty()) {
                suppressed_version_ = latest_version_;
            }
            confirm_.close();
        }
        return;
    }

    const uint32_t now = monotonic_ms();

    if (phase_ == Phase::Checking) {
        if (now - check_start_ms_ > kCheckTimeoutMs) {
            phase_ = Phase::Idle;  // timed out; stay silent
            return;
        }
        if (last_poll_ms_ != 0 && now - last_poll_ms_ < kPollIntervalMs) {
            return;
        }
        last_poll_ms_ = now;
        poll_state_file();
        return;
    }

    // Phase::Idle: kick off the next periodic background check.
    if (now < kFirstCheckDelayMs) {
        return;
    }
    if (last_check_ms_ != 0 && now - last_check_ms_ < kCheckIntervalMs) {
        return;
    }
    start_check();
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

    // Prompt when a genuinely newer version is available, unless the user
    // chose "Don't ask me" for this exact version this boot. A plain
    // "Not Now" does NOT suppress, so the next periodic poll prompts again.
    if (phase_name == "update_available" && !latest_version_.empty() &&
        latest_version_ != suppressed_version_) {
        const std::string msg = std::string("A new version ") + latest_version_ +
                                 " is available (current: " + current_version_ + "). Update now?";
        confirm_.open("Update Available", msg.c_str(), "Update", "Not Now",
                      "Don't ask me again");
    }
    // up_to_date / failed / suppressed version: stay silent.
    phase_ = Phase::Idle;
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
