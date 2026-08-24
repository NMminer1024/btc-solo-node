#include "ota_overlay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

namespace {

constexpr const char* kOtaStatePath = "/run/rockbase/ota-state.json";
constexpr uint32_t kPollIntervalMs = 1000;

constexpr const char* kStepNames[4] = {"Download", "Verify", "Write", "Reboot"};

uint32_t monotonic_ms() {
    timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

// Same minimal file/JSON helpers as wifi_config_overlay.cpp / main.cpp.
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

int json_int_at(const std::string& src, size_t pos, const char* key, int fallback) {
    const std::string needle = std::string("\"") + key + "\":";
    pos = src.find(needle, pos);
    if (pos == std::string::npos) return fallback;
    pos += needle.size();
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) ++pos;
    return std::atoi(src.c_str() + pos);
}

std::string format_speed(int bytes_per_second) {
    if (bytes_per_second <= 0) {
        return {};
    }

    char buf[32];
    if (bytes_per_second >= 1000000) {
        std::snprintf(buf, sizeof(buf), "%.1f MB/s", bytes_per_second / 1000000.0);
    }
    else if (bytes_per_second >= 1000) {
        std::snprintf(buf, sizeof(buf), "%.0f KB/s", bytes_per_second / 1000.0);
    }
    else {
        std::snprintf(buf, sizeof(buf), "%d B/s", bytes_per_second);
    }
    return buf;
}

void set_label_if_changed(lv_obj_t* label, const std::string& text) {
    if (label == nullptr) return;
    const char* current = lv_label_get_text(label);
    if (current == nullptr || text != current) {
        lv_label_set_text(label, text.c_str());
    }
}

void set_hidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) return;
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace

OtaOverlay::Phase OtaOverlay::parse_phase_name(const std::string& name) {
    if (name == "preparing") return Phase::Preparing;
    if (name == "downloading") return Phase::Downloading;
    if (name == "verifying") return Phase::Verifying;
    if (name == "flashing") return Phase::Flashing;
    if (name == "switching") return Phase::Switching;
    if (name == "failed") return Phase::Failed;
    return Phase::Hidden;
}

const char* OtaOverlay::default_message(Phase phase) {
    switch (phase) {
        case Phase::Preparing: return "Preparing for upgrade...";
        case Phase::Downloading: return "Downloading firmware...";
        case Phase::Verifying: return "Verifying package...";
        case Phase::Flashing: return "Writing system image to the device...";
        case Phase::Switching: return "Rebooting into the new system...";
        case Phase::Failed: return "The upgrade could not be completed.";
        case Phase::Hidden: break;
    }
    return "";
}

const char* OtaOverlay::short_stage_name(Phase phase) {
    switch (phase) {
        case Phase::Preparing: return "Preparing";
        case Phase::Downloading: return "Downloading";
        case Phase::Verifying: return "Verifying";
        case Phase::Flashing: return "Writing";
        case Phase::Switching: return "Rebooting";
        default: break;
    }
    return "";
}

void OtaOverlay::update() {
    const uint32_t now = monotonic_ms();
    if (last_poll_ms_ != 0 && now - last_poll_ms_ < kPollIntervalMs) {
        return;
    }
    last_poll_ms_ = now;

    poll_state_file();
    if (phase_ == Phase::Hidden) {
        close();
        return;
    }
    if (root_ == nullptr) {
        open();
    }
    apply_state();
}

int OtaOverlay::current_step() const {
    switch (phase_) {
        case Phase::Preparing:
        case Phase::Downloading: return 0;
        case Phase::Verifying: return 1;
        case Phase::Flashing: return 2;
        case Phase::Switching: return 3;
        case Phase::Failed:
        case Phase::Hidden: break;
    }
    return -1;
}

void OtaOverlay::poll_state_file() {
    // The OTA script writes this file atomically (tmp + rename) on tmpfs.
    const std::string json = read_file(kOtaStatePath);
    if (json.empty()) {
        phase_ = Phase::Hidden;
        progress_ = -1;
        speed_bps_ = -1;
        message_.clear();
        return;
    }
    phase_ = parse_phase_name(json_value_at(json, 0, "phase"));
    progress_ = json_int_at(json, 0, "progress", -1);
    speed_bps_ = json_int_at(json, 0, "speed_bps", -1);
    message_ = json_value_at(json, 0, "message");
}

void OtaOverlay::open() {
    // Opaque full-screen backdrop on the top layer; nothing below shows through.
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x05090c), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);  // swallow touch events

    lv_obj_t* panel = lv_obj_create(root_);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 640, 384);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 24, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0c141b), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x2d4654), 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    title_label_ = lv_label_create(panel);
    lv_label_set_text(title_label_, "System Upgrade in Progress");
    lv_obj_set_pos(title_label_, 20, 26);
    lv_obj_set_width(title_label_, 600);
    lv_obj_set_style_text_font(title_label_, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title_label_, lv_color_hex(0xf4efe2), 0);
    lv_obj_set_style_text_align(title_label_, LV_TEXT_ALIGN_CENTER, 0);

    subtitle_label_ = lv_label_create(panel);
    lv_label_set_text(subtitle_label_, "The whole upgrade takes about 5-15 minutes.");
    lv_obj_set_pos(subtitle_label_, 20, 60);
    lv_obj_set_width(subtitle_label_, 600);
    lv_obj_set_style_text_font(subtitle_label_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(subtitle_label_, lv_color_hex(0x9aa6ac), 0);
    lv_obj_set_style_text_align(subtitle_label_, LV_TEXT_ALIGN_CENTER, 0);

    // Step indicator: Download > Verify > Write > Reboot
    steps_row_ = lv_obj_create(panel);
    lv_obj_remove_style_all(steps_row_);
    lv_obj_set_size(steps_row_, 600, 34);
    lv_obj_set_pos(steps_row_, 20, 100);
    lv_obj_set_flex_flow(steps_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(steps_row_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(steps_row_, 12, 0);
    lv_obj_clear_flag(steps_row_, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; ++i) {
        if (i > 0) {
            lv_obj_t* sep = lv_label_create(steps_row_);
            lv_label_set_text(sep, ">");
            lv_obj_set_style_text_font(sep, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(sep, lv_color_hex(0x4a5a63), 0);
        }
        step_labels_[i] = lv_label_create(steps_row_);
        lv_label_set_text(step_labels_[i], kStepNames[i]);
        lv_obj_set_style_text_font(step_labels_[i], &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(step_labels_[i], lv_color_hex(0x78848b), 0);
    }

    bar_ = lv_bar_create(panel);
    lv_obj_set_size(bar_, 560, 18);
    lv_obj_set_pos(bar_, 40, 162);
    lv_bar_set_range(bar_, 0, 100);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(0x14232c), 0);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(0x2d8f5a), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_, 9, 0);
    lv_obj_set_style_radius(bar_, 9, LV_PART_INDICATOR);
    lv_bar_set_value(bar_, 0, LV_ANIM_OFF);

    progress_label_ = lv_label_create(panel);
    lv_label_set_text(progress_label_, "0%");
    lv_obj_set_pos(progress_label_, 20, 192);
    lv_obj_set_width(progress_label_, 600);
    lv_obj_set_style_text_font(progress_label_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(progress_label_, lv_color_hex(0xf4efe2), 0);
    lv_obj_set_style_text_align(progress_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_recolor(progress_label_, true);

    message_label_ = lv_label_create(panel);
    lv_label_set_text(message_label_, "");
    lv_obj_set_pos(message_label_, 40, 230);
    lv_obj_set_width(message_label_, 560);
    lv_obj_set_style_text_font(message_label_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(message_label_, lv_color_hex(0xc7d2d8), 0);
    lv_obj_set_style_text_align(message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(message_label_, LV_LABEL_LONG_WRAP);

    warn_line1_ = lv_label_create(panel);
    lv_label_set_text(warn_line1_, "Do not power off the device.");
    lv_obj_set_pos(warn_line1_, 20, 306);
    lv_obj_set_width(warn_line1_, 600);
    lv_obj_set_style_text_font(warn_line1_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(warn_line1_, lv_color_hex(0xf0b44c), 0);
    lv_obj_set_style_text_align(warn_line1_, LV_TEXT_ALIGN_CENTER, 0);

    warn_line2_ = lv_label_create(panel);
    lv_label_set_text(warn_line2_, "If power is lost, the previous system will be restored automatically.");
    lv_obj_set_pos(warn_line2_, 20, 332);
    lv_obj_set_width(warn_line2_, 600);
    lv_obj_set_style_text_font(warn_line2_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(warn_line2_, lv_color_hex(0xf0b44c), 0);
    lv_obj_set_style_text_align(warn_line2_, LV_TEXT_ALIGN_CENTER, 0);

    failed_hint_label_ = lv_label_create(panel);
    lv_label_set_text(failed_hint_label_, "Services have been restored. Reboot to retry.");
    lv_obj_set_pos(failed_hint_label_, 20, 300);
    lv_obj_set_width(failed_hint_label_, 600);
    lv_obj_set_style_text_font(failed_hint_label_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(failed_hint_label_, lv_color_hex(0x9aa6ac), 0);
    lv_obj_set_style_text_align(failed_hint_label_, LV_TEXT_ALIGN_CENTER, 0);
}

void OtaOverlay::close() {
    if (root_ == nullptr) return;
    lv_obj_del(root_);
    root_ = nullptr;
    title_label_ = nullptr;
    subtitle_label_ = nullptr;
    steps_row_ = nullptr;
    for (auto& label : step_labels_) label = nullptr;
    bar_ = nullptr;
    progress_label_ = nullptr;
    message_label_ = nullptr;
    warn_line1_ = nullptr;
    warn_line2_ = nullptr;
    failed_hint_label_ = nullptr;
}

void OtaOverlay::apply_state() {
    if (root_ == nullptr) return;

    const bool failed = phase_ == Phase::Failed;
    set_label_if_changed(title_label_, failed ? "Upgrade Failed" : "System Upgrade in Progress");
    lv_obj_set_style_text_color(title_label_, lv_color_hex(failed ? 0xff6b6b : 0xf4efe2), 0);

    // Failed panel: only title + message + hint, everything else hidden.
    set_hidden(subtitle_label_, failed);
    set_hidden(steps_row_, failed);
    set_hidden(bar_, failed);
    set_hidden(progress_label_, failed);
    set_hidden(warn_line1_, failed);
    set_hidden(warn_line2_, failed);
    set_hidden(failed_hint_label_, !failed);
    if (failed) {
        set_label_if_changed(message_label_, message_.empty() ? default_message(phase_) : message_);
        return;
    }

    // Step indicator: done steps dim with a check, current step highlighted.
    const int step = current_step();
    for (int i = 0; i < 4; ++i) {
        if (step_labels_[i] == nullptr) continue;
        if (i < step) {
            set_label_if_changed(step_labels_[i], std::string(LV_SYMBOL_OK " ") + kStepNames[i]);
            lv_obj_set_style_text_color(step_labels_[i], lv_color_hex(0x60707a), 0);
        }
        else {
            set_label_if_changed(step_labels_[i], kStepNames[i]);
            lv_obj_set_style_text_color(step_labels_[i],
                                        lv_color_hex(i == step ? 0xf0b44c : 0x78848b), 0);
        }
    }

    // Progress bar; -1 means indeterminate: full bar at reduced opacity,
    // stage name instead of a percentage.
    if (progress_ >= 0 && progress_ <= 100) {
        lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_bar_set_value(bar_, progress_, LV_ANIM_OFF);
        char pct[96];
        std::snprintf(pct, sizeof(pct), "%d%%", progress_);
        const std::string speed = format_speed(speed_bps_);
        if (!speed.empty()) {
            set_label_if_changed(progress_label_, std::string(pct) + "  |  #f0b44c " + speed + "#");
        }
        else {
            set_label_if_changed(progress_label_, pct);
        }
    }
    else {
        lv_obj_set_style_bg_opa(bar_, LV_OPA_40, LV_PART_INDICATOR);
        lv_bar_set_value(bar_, 100, LV_ANIM_OFF);
        set_label_if_changed(progress_label_, short_stage_name(phase_));
    }

    set_label_if_changed(message_label_, message_.empty() ? default_message(phase_) : message_);
}
