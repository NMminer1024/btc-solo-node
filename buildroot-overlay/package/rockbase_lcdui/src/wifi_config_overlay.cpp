#include "wifi_config_overlay.h"

#include "ui_style.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unistd.h>

namespace {

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

std::string run_command(const std::string& command, int* rc_out = nullptr) {
    std::string output;
    FILE* fp = popen(command.c_str(), "r");
    if (fp == nullptr) {
        if (rc_out != nullptr) *rc_out = -1;
        return output;
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), fp) != nullptr) {
        output += buf;
    }
    const int rc = pclose(fp);
    if (rc_out != nullptr) *rc_out = rc;
    return output;
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

void style_button(lv_obj_t* btn, uint32_t color) {
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
}

}  // namespace

void WifiConfigOverlay::open() {
    overlay_.show("Wi-Fi Setup");
    phase_ = Phase::Scanning;
    built_phase_ = Phase::Hidden;
    pending_ssid_.clear();
    pending_password_.clear();
    worker_message_.clear();
    networks_.clear();
    close_requested_ = false;
    rebuild();
    start_scan();
}

void WifiConfigOverlay::update() {
    confirm_.update();
    if (confirm_.active()) {
        const ConfirmOverlay::Result result = confirm_.consume_result();
        if (result == ConfirmOverlay::Result::Confirmed) {
            if (phase_ == Phase::Confirm) {
                confirm_.close();
                set_phase(Phase::Applying);
                rebuild();
                start_apply();
            }
            else if (phase_ == Phase::Result) {
                confirm_.close();
                close();
                return;
            }
        }
        else if (result == ConfirmOverlay::Result::Cancelled) {
            if (phase_ == Phase::Confirm) {
                confirm_.close();
                overlay_.show("Wi-Fi Setup");
                set_phase(Phase::Password);
                rebuild();
            }
        }
    }
    if (close_requested_ || overlay_.close_requested()) {
        close();
        return;
    }
    if (!overlay_.visible()) {
        // 大覆膜已隐藏；小弹窗仍在时继续推进（Applying 进度 → Result）。
        if (!confirm_.active()) {
            cleanup_keyboard();
            phase_ = Phase::Hidden;
            built_phase_ = Phase::Hidden;
            body_ = nullptr;
            message_label_ = nullptr;
            password_ta_ = nullptr;
            return;
        }
        finish_worker_if_ready();
        if (phase_ != built_phase_) {
            rebuild();
        }
        return;
    }
    finish_worker_if_ready();
    if (phase_ != built_phase_) {
        rebuild();
    }
}

void WifiConfigOverlay::set_phase(Phase phase) {
    phase_ = phase;
}

void WifiConfigOverlay::clear_body() {
    cleanup_keyboard();
    lv_obj_t* content = overlay_.content();
    if (content == nullptr) return;
    if (body_ != nullptr) {
        lv_obj_del(body_);
        body_ = nullptr;
        message_label_ = nullptr;
        password_ta_ = nullptr;
    }
    body_ = lv_obj_create(content);
    lv_obj_remove_style_all(body_);
    // Start below the ghost close button (which spans y 16-72) so the first
    // list row is not covered by the close hit-target.
    lv_obj_set_pos(body_, 24, 80);
    lv_obj_set_size(body_, 712, 352);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
}

void WifiConfigOverlay::cleanup_keyboard() {
    if (keyboard_ != nullptr) {
        lv_obj_del(keyboard_);
        keyboard_ = nullptr;
    }
}

void WifiConfigOverlay::show_keyboard() {
    if (keyboard_ == nullptr) return;
    lv_obj_clear_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard_);
}

void WifiConfigOverlay::hide_keyboard() {
    if (keyboard_ == nullptr) return;
    lv_obj_add_flag(keyboard_, LV_OBJ_FLAG_HIDDEN);
}

void WifiConfigOverlay::request_close() {
    close_requested_ = true;
}

void WifiConfigOverlay::close() {
    confirm_.close();
    cleanup_keyboard();
    overlay_.hide();
    phase_ = Phase::Hidden;
    built_phase_ = Phase::Hidden;
    body_ = nullptr;
    message_label_ = nullptr;
    password_ta_ = nullptr;
    close_requested_ = false;
}

void WifiConfigOverlay::rebuild() {
    clear_body();
    built_phase_ = phase_;
    switch (phase_) {
        case Phase::Scanning: build_scanning(); break;
        case Phase::List: build_list(); break;
        case Phase::Password: build_password(); break;
        case Phase::Confirm: build_confirm(); break;
        case Phase::Applying: build_applying(); break;
        case Phase::Result: build_result(); break;
        case Phase::Hidden: break;
    }
}

void WifiConfigOverlay::set_message(const char* text) {
    if (message_label_ != nullptr) {
        lv_label_set_text(message_label_, text != nullptr ? text : "");
    }
}

void WifiConfigOverlay::build_scanning() {
    message_label_ = lv_label_create(body_);
    lv_label_set_text(message_label_, "Scanning nearby Wi-Fi networks...");
    lv_obj_set_style_text_font(message_label_, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(message_label_, lv_color_hex(0xc7d2d8), 0);
    lv_obj_center(message_label_);
}

void WifiConfigOverlay::build_list() {
    lv_obj_t* list = lv_obj_create(body_);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, 0);
    lv_obj_set_size(list, 712, 352);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    if (networks_.empty()) {
        message_label_ = lv_label_create(list);
        lv_obj_set_width(message_label_, 420);
        lv_obj_set_style_text_font(message_label_, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(message_label_, lv_color_hex(0x9aa6ac), 0);
        lv_label_set_text(message_label_, "No networks found.");
    }

    for (size_t i = 0; i < networks_.size(); ++i) {
        const Network& net = networks_[i];
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, LV_PCT(100), 58);
        style_button(btn, 0x14232c);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x2b4552), 0);
        lv_obj_set_style_pad_all(btn, 0, 0);

        lv_obj_t* ssid = lv_label_create(btn);
        lv_label_set_text(ssid, net.ssid.c_str());
        lv_obj_set_pos(ssid, 20, 8);
        lv_obj_set_width(ssid, 455);
        lv_obj_set_style_text_font(ssid, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(ssid, lv_color_hex(0xf4efe2), 0);
        lv_label_set_long_mode(ssid, LV_LABEL_LONG_MODE_DOTS);

        lv_obj_t* sec = lv_label_create(btn);
        lv_label_set_text(sec, net.security.empty() ? "open" : net.security.c_str());
        lv_obj_set_pos(sec, 22, 36);
        lv_obj_set_width(sec, 420);
        lv_obj_set_style_text_font(sec, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(sec, lv_color_hex(0x78848b), 0);
        lv_label_set_long_mode(sec, LV_LABEL_LONG_MODE_DOTS);

        lv_obj_t* rssi = lv_label_create(btn);
        char rssi_text[32];
        std::snprintf(rssi_text, sizeof(rssi_text), "%d dBm", net.signal);
        lv_label_set_text(rssi, rssi_text);
        lv_obj_set_pos(rssi, 584, 18);
        lv_obj_set_width(rssi, 90);
        lv_obj_set_style_text_font(rssi, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(rssi, lv_color_hex(net.signal >= -60 ? 0x59d79b : (net.signal >= -75 ? 0xf0b44c : 0xff8c64)), 0);
        lv_obj_set_style_text_align(rssi, LV_TEXT_ALIGN_RIGHT, 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<WifiConfigOverlay*>(lv_event_get_user_data(e));
            if (self == nullptr) return;
            lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
            lv_obj_t* label = lv_obj_get_child(btn, 0);
            self->pending_ssid_ = label != nullptr ? lv_label_get_text(label) : "";
            self->pending_password_.clear();
            self->set_phase(Phase::Password);
        }, LV_EVENT_CLICKED, this);
    }
}

void WifiConfigOverlay::build_password() {
    lv_obj_t* ssid = lv_label_create(body_);
    std::string title = "SSID: " + pending_ssid_;
    lv_label_set_text(ssid, title.c_str());
    lv_obj_set_pos(ssid, 0, 0);
    lv_obj_set_width(ssid, 712);
    lv_obj_set_style_text_font(ssid, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(ssid, lv_color_hex(0xff8c64), 0);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_MODE_DOTS);

    password_ta_ = lv_textarea_create(body_);
    lv_obj_set_size(password_ta_, 712, 44);
    lv_obj_set_pos(password_ta_, 0, 36);
    lv_textarea_set_one_line(password_ta_, true);
    lv_textarea_set_password_mode(password_ta_, true);
    lv_textarea_set_max_length(password_ta_, 64);
    lv_textarea_set_placeholder_text(password_ta_, "Enter password");
    lv_textarea_set_text(password_ta_, pending_password_.c_str());
    lv_obj_set_style_text_font(password_ta_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(password_ta_, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_color(password_ta_, lv_color_hex(0x14232c), 0);
    lv_obj_set_style_border_color(password_ta_, lv_color_hex(0xff8c64), 0);
    lv_obj_set_style_pad_right(password_ta_, 34, 0);
    lv_obj_add_event_cb(password_ta_, [](lv_event_t* e) {
        auto* self = static_cast<WifiConfigOverlay*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->show_keyboard();
        }
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* reveal_btn = lv_btn_create(body_);
    lv_obj_set_size(reveal_btn, 30, 30);
    lv_obj_set_pos(reveal_btn, 678, 43);
    lv_obj_set_style_radius(reveal_btn, 8, 0);
    lv_obj_set_style_bg_opa(reveal_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(reveal_btn, 0, 0);
    lv_obj_t* reveal_label = lv_label_create(reveal_btn);
    lv_label_set_text(reveal_label, LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_color(reveal_label, lv_color_hex(0xc7d2d8), 0);
    lv_obj_set_style_text_font(reveal_label, &lv_font_montserrat_18, 0);
    lv_obj_center(reveal_label);
    lv_obj_add_event_cb(reveal_btn, [](lv_event_t* e) {
        auto* self = static_cast<WifiConfigOverlay*>(lv_event_get_user_data(e));
        if (self == nullptr || self->password_ta_ == nullptr) return;
        const bool hidden = lv_textarea_get_password_mode(self->password_ta_);
        lv_textarea_set_password_mode(self->password_ta_, !hidden);
        lv_obj_t* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_obj_t* label = lv_obj_get_child(btn, 0);
        if (label != nullptr) {
            lv_label_set_text(label, hidden ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE);
        }
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* cancel_btn = lv_btn_create(body_);
    lv_obj_set_size(cancel_btn, 198, 60);
    lv_obj_set_pos(cancel_btn, 0, 102);
    ui_style_secondary_button(cancel_btn);
    ui_button_label(cancel_btn, "Back", 0xc7d2d8);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* self = static_cast<WifiConfigOverlay*>(lv_event_get_user_data(e));
        if (self == nullptr) return;
        self->cleanup_keyboard();
        self->set_phase(Phase::List);
    }, LV_EVENT_CLICKED, this);

    lv_obj_t* apply_btn = lv_btn_create(body_);
    lv_obj_set_size(apply_btn, 198, 60);
    lv_obj_set_pos(apply_btn, 514, 102);
    ui_style_primary_button(apply_btn);
    ui_button_label(apply_btn, "Apply", 0xffffff);
    lv_obj_add_event_cb(apply_btn, [](lv_event_t* e) {
        auto* self = static_cast<WifiConfigOverlay*>(lv_event_get_user_data(e));
        if (self == nullptr) return;
        if (self->password_ta_ != nullptr) {
            self->pending_password_ = lv_textarea_get_text(self->password_ta_);
        }
        self->cleanup_keyboard();
        self->overlay_.hide();
        self->body_ = nullptr;
        self->message_label_ = nullptr;
        self->password_ta_ = nullptr;
        self->set_phase(Phase::Confirm);
        self->rebuild();
    }, LV_EVENT_CLICKED, this);

    keyboard_ = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(keyboard_, 760, 224);
    lv_obj_align(keyboard_, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_keyboard_set_textarea(keyboard_, password_ta_);
    lv_keyboard_set_popovers(keyboard_, true);
    lv_obj_set_style_bg_color(keyboard_, lv_color_hex(0x0f1b22), 0);
    lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(keyboard_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_all(keyboard_, 4, 0);
    lv_obj_set_style_pad_row(keyboard_, 4, 0);
    lv_obj_set_style_pad_column(keyboard_, 4, 0);
    lv_obj_move_foreground(keyboard_);
    lv_obj_add_event_cb(keyboard_, [](lv_event_t* e) {
        auto* self = static_cast<WifiConfigOverlay*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->hide_keyboard();
        }
    }, LV_EVENT_CANCEL, this);
}

void WifiConfigOverlay::build_confirm() {
    std::string message = "Connect to \"" + pending_ssid_ + "\" now?";
    confirm_.open("Confirm Wi-Fi", message.c_str(), "OK", "Cancel");
}

void WifiConfigOverlay::build_applying() {
    confirm_.open("Wi-Fi Setup", "Applying Wi-Fi profile...", nullptr, nullptr);
}

void WifiConfigOverlay::build_result() {
    std::string text = worker_message_.empty() ? "Wi-Fi operation finished." : worker_message_;
    confirm_.open("Wi-Fi Setup", text.c_str(), "Done", nullptr, nullptr,
                  worker_ok_ ? 0x59d79b : 0xff8c64);
}

void WifiConfigOverlay::start_scan() {
    if (worker_running_) return;
    worker_done_ = false;
    worker_ok_ = false;
    worker_message_.clear();
    networks_.clear();
    worker_running_ = pthread_create(&worker_, nullptr, scan_thread_entry, this) == 0;
    if (!worker_running_) {
        worker_done_ = true;
        worker_ok_ = false;
        worker_message_ = "Unable to start Wi-Fi scan.";
    }
}

void WifiConfigOverlay::start_apply() {
    if (worker_running_) return;
    worker_done_ = false;
    worker_ok_ = false;
    apply_started_ = false;
    worker_message_.clear();
    worker_running_ = pthread_create(&worker_, nullptr, apply_thread_entry, this) == 0;
    if (!worker_running_) {
        worker_done_ = true;
        worker_ok_ = false;
        worker_message_ = "Unable to start Wi-Fi apply.";
    }
}

void WifiConfigOverlay::finish_worker_if_ready() {
    if (!worker_done_) return;
    if (worker_running_) {
        pthread_join(worker_, nullptr);
        worker_running_ = false;
    }
    if (phase_ == Phase::Scanning) {
        phase_ = Phase::List;
    }
    else if (phase_ == Phase::Applying) {
        phase_ = Phase::Result;
    }
}

void* WifiConfigOverlay::scan_thread_entry(void* arg) {
    auto* self = static_cast<WifiConfigOverlay*>(arg);
    int rc = 0;
    const std::string json = run_command("sh /usr/bin/rockbase_wifi.sh scan 2>/dev/null", &rc);
    std::vector<Network> parsed = parse_scan_json(json);
    pthread_mutex_lock(&self->lock_);
    self->networks_ = parsed;
    self->worker_ok_ = rc == 0;
    self->worker_message_ = self->worker_ok_ ? "" : "Wi-Fi scan unavailable.";
    self->worker_done_ = true;
    pthread_mutex_unlock(&self->lock_);
    return nullptr;
}

void* WifiConfigOverlay::apply_thread_entry(void* arg) {
    auto* self = static_cast<WifiConfigOverlay*>(arg);
    const std::string ssid = self->pending_ssid_;
    const std::string password = self->pending_password_;
    const std::string cmd = "sh /usr/bin/rockbase_wifi.sh apply " + shell_quote(ssid) + " " +
                            shell_quote(password) + " 0 >/dev/null 2>&1";
    int rc = 0;
    run_command(cmd, &rc);
    self->apply_started_ = true;

    bool ok = false;
    std::string message = "Unable to connect. Previous Wi-Fi profile was restored.";
    for (int i = 0; i < 38; ++i) {
        sleep(2);
        const std::string status = run_command("sh /usr/bin/rockbase_wifi.sh status 2>/dev/null");
        const std::string state = json_value_at(status, 0, "apply_state");
        const std::string current_ssid = json_value_at(status, 0, "current_ssid");
        const std::string ip = json_value_at(status, 0, "current_ip");
        const std::string apply_message = json_value_at(status, 0, "apply_message");
        if (state == "ready" && current_ssid == ssid && !ip.empty() && ip != "-") {
            ok = true;
            message = "Connected to " + ssid + "\nIP: " + ip;
            break;
        }
        if (state == "error") {
            message = apply_message.empty() ? message : apply_message + "\nPrevious Wi-Fi profile was restored.";
            break;
        }
    }

    pthread_mutex_lock(&self->lock_);
    self->worker_ok_ = ok;
    self->worker_message_ = message;
    self->worker_done_ = true;
    pthread_mutex_unlock(&self->lock_);
    return nullptr;
}

std::vector<WifiConfigOverlay::Network> WifiConfigOverlay::parse_scan_json(const std::string& json) {
    std::vector<Network> out;
    size_t pos = 0;
    while ((pos = json.find("{\"ssid\":", pos)) != std::string::npos) {
        Network net;
        net.ssid = json_value_at(json, pos, "ssid");
        net.signal = json_int_at(json, pos, "signal", -100);
        net.security = json_value_at(json, pos, "security");
        if (!net.ssid.empty()) {
            out.push_back(net);
        }
        ++pos;
    }
    std::sort(out.begin(), out.end(), [](const Network& a, const Network& b) {
        return a.signal > b.signal;
    });
    return out;
}
