#include "timezone_overlay.h"

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

void style_button(lv_obj_t* btn, uint32_t color) {
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
}

lv_obj_t* button_label(lv_obj_t* btn, const char* text) {
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
    lv_obj_center(label);
    return label;
}

std::string current_timezone_text() {
    int rc = 0;
    std::string text = run_command("sh -c 'cat /etc/TZ 2>/dev/null || readlink /etc/localtime 2>/dev/null || echo UTC'", &rc);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    if (text.empty()) {
        text = "UTC";
    }
    const std::string prefix = "/usr/share/zoneinfo/";
    if (text.rfind(prefix, 0) == 0) {
        text = text.substr(prefix.size());
    }
    if (text == "CST-8") {
        text = "Asia/Shanghai";
    }
    return text;
}

}  // namespace

void TimezoneOverlay::open() {
    overlay_.show("Timezone");
    phase_ = Phase::List;
    built_phase_ = Phase::Hidden;
    selected_zone_ = nullptr;
    worker_message_.clear();
    close_requested_ = false;
    rebuild();
}

void TimezoneOverlay::update() {
    confirm_.update();
    if (confirm_.active()) {
        const ConfirmOverlay::Result result = confirm_.consume_result();
        if (result == ConfirmOverlay::Result::Confirmed) {
            confirm_.close();
            set_phase(Phase::Applying);
            rebuild();
            start_apply();
        }
        else if (result == ConfirmOverlay::Result::Cancelled) {
            confirm_.close();
            set_phase(Phase::List);
        }
    }
    if (close_requested_ || overlay_.close_requested()) {
        close();
        return;
    }
    if (!overlay_.visible()) {
        phase_ = Phase::Hidden;
        built_phase_ = Phase::Hidden;
        body_ = nullptr;
        message_label_ = nullptr;
        return;
    }
    finish_worker_if_ready();
    if (phase_ != built_phase_) {
        rebuild();
    }
}

void TimezoneOverlay::set_phase(Phase phase) {
    phase_ = phase;
}

void TimezoneOverlay::clear_body() {
    lv_obj_t* content = overlay_.content();
    if (content == nullptr) return;
    if (body_ != nullptr) {
        lv_obj_del(body_);
        body_ = nullptr;
        message_label_ = nullptr;
    }
    body_ = lv_obj_create(content);
    lv_obj_remove_style_all(body_);
    lv_obj_set_pos(body_, 24, 64);
    lv_obj_set_size(body_, 712, 376);
    lv_obj_clear_flag(body_, LV_OBJ_FLAG_SCROLLABLE);
}

void TimezoneOverlay::request_close() {
    close_requested_ = true;
}

void TimezoneOverlay::close() {
    confirm_.close();
    overlay_.hide();
    phase_ = Phase::Hidden;
    built_phase_ = Phase::Hidden;
    body_ = nullptr;
    message_label_ = nullptr;
    close_requested_ = false;
}

void TimezoneOverlay::rebuild() {
    clear_body();
    built_phase_ = phase_;
    switch (phase_) {
        case Phase::List: build_list(); break;
        case Phase::Confirm: build_confirm(); break;
        case Phase::Applying: build_applying(); break;
        case Phase::Result: build_result(); break;
        case Phase::Hidden: break;
    }
}

void TimezoneOverlay::set_message(const char* text) {
    if (message_label_ != nullptr) {
        lv_label_set_text(message_label_, text != nullptr ? text : "");
    }
}

void TimezoneOverlay::build_list() {
    lv_obj_t* current = lv_label_create(body_);
    std::string text = std::string("Current: ") + current_timezone_text();
    lv_label_set_text(current, text.c_str());
    lv_obj_set_pos(current, 0, 0);
    lv_obj_set_width(current, 684);
    lv_obj_set_style_text_font(current, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(current, lv_color_hex(0x9aa6ac), 0);
    lv_label_set_long_mode(current, LV_LABEL_LONG_DOT);

    lv_obj_t* list = lv_obj_create(body_);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_size(list, 712, 340);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    for (const Zone& zone : kZones) {
        lv_obj_t* btn = lv_btn_create(list);
        lv_obj_set_size(btn, LV_PCT(100), 54);
        style_button(btn, 0x1d2d36);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_hex(0x304551), 0);
        lv_obj_t* label = lv_label_create(btn);
        lv_label_set_text(label, zone.label);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
        lv_obj_set_width(label, 500);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 18, 0);

        lv_obj_t* offset = lv_label_create(btn);
        lv_label_set_text(offset, zone.utc_offset);
        lv_obj_set_style_text_font(offset, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(offset, lv_color_hex(0xffb16d), 0);
        lv_obj_set_size(offset, 88, LV_SIZE_CONTENT);
        lv_obj_align(offset, LV_ALIGN_RIGHT_MID, -18, 0);
        lv_obj_set_style_text_align(offset, LV_TEXT_ALIGN_RIGHT, 0);

        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
            auto* self = static_cast<TimezoneOverlay*>(lv_event_get_user_data(e));
            if (self == nullptr) return;
            lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
            lv_obj_t* text_label = target != nullptr ? lv_obj_get_child(target, 0) : nullptr;
            const char* zone_key = text_label != nullptr ? lv_label_get_text(text_label) : nullptr;
            if (zone_key == nullptr || zone_key[0] == '\0') return;
            self->selected_zone_ = nullptr;
            for (const Zone& item : kZones) {
                if (std::strcmp(item.key, zone_key) == 0) {
                    self->selected_zone_ = &item;
                    break;
                }
            }
            if (self->selected_zone_ == nullptr) return;
            self->set_phase(Phase::Confirm);
            self->rebuild();
            std::string message = "Switch timezone to \"" + std::string(self->selected_zone_->key) + "\"?";
            self->confirm_.open("Confirm Timezone", message.c_str(), "OK", "Cancel");
        }, LV_EVENT_CLICKED, this);
    }
}

void TimezoneOverlay::build_confirm() {
    message_label_ = lv_label_create(body_);
    lv_label_set_text(message_label_, "Waiting for confirmation...");
    lv_obj_set_style_text_font(message_label_, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(message_label_, lv_color_hex(0xc7d2d8), 0);
    lv_obj_center(message_label_);
}

void TimezoneOverlay::build_applying() {
    message_label_ = lv_label_create(body_);
    std::string text = std::string("Applying ") + (selected_zone_ != nullptr ? selected_zone_->key : "UTC") + "...";
    lv_label_set_text(message_label_, text.c_str());
    lv_obj_set_style_text_font(message_label_, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(message_label_, lv_color_hex(0xc7d2d8), 0);
    lv_obj_center(message_label_);
}

void TimezoneOverlay::build_result() {
    message_label_ = lv_label_create(body_);
    std::string text = worker_message_.empty()
        ? "Timezone updated.\nTakes effect immediately."
        : worker_message_;
    lv_label_set_text(message_label_, text.c_str());
    lv_obj_set_width(message_label_, 680);
    lv_obj_set_style_text_font(message_label_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(message_label_, lv_color_hex(0xc7d2d8), 0);
    lv_obj_set_style_text_align(message_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(message_label_, LV_LABEL_LONG_WRAP);
    lv_obj_align(message_label_, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t* done_btn = lv_btn_create(body_);
    lv_obj_set_size(done_btn, 220, 72);
    lv_obj_align(done_btn, LV_ALIGN_CENTER, 0, 86);
    style_button(done_btn, 0x2b6b57);
    button_label(done_btn, "Done");
    lv_obj_add_event_cb(done_btn, [](lv_event_t* e) {
        auto* self = static_cast<TimezoneOverlay*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->request_close();
        }
    }, LV_EVENT_CLICKED, this);
}

void TimezoneOverlay::start_apply() {
    pthread_mutex_lock(&lock_);
    if (worker_running_) {
        pthread_mutex_unlock(&lock_);
        return;
    }
    worker_running_ = true;
    worker_done_ = false;
    worker_ok_ = false;
    pthread_mutex_unlock(&lock_);
    pthread_create(&worker_, nullptr, apply_thread_entry, this);
}

void TimezoneOverlay::finish_worker_if_ready() {
    pthread_mutex_lock(&lock_);
    const bool done = worker_done_;
    const bool running = worker_running_;
    const bool ok = worker_ok_;
    pthread_mutex_unlock(&lock_);
    if (!done || !running) {
        return;
    }
    pthread_join(worker_, nullptr);
    pthread_mutex_lock(&lock_);
    worker_running_ = false;
    worker_done_ = false;
    pthread_mutex_unlock(&lock_);
    if (phase_ == Phase::Applying) {
        phase_ = Phase::Result;
    }
    if (ok && status_refresh_request_ != nullptr) {
        status_refresh_request_();
    }
    if (worker_message_.empty()) {
        worker_message_ = ok ? "Timezone updated." : "Timezone update failed.";
    }
}

void* TimezoneOverlay::apply_thread_entry(void* arg) {
    auto* self = static_cast<TimezoneOverlay*>(arg);
    if (self == nullptr) return nullptr;
    const std::string zone = self->selected_zone_ != nullptr ? self->selected_zone_->key : "UTC";
    const std::string cmd = "sh /usr/bin/rockbase_timezone.sh apply " + shell_quote(zone) +
                            " >/dev/null 2>&1 && sh /usr/bin/rockbase_statusd.sh refresh-now >/dev/null 2>&1";
    int rc = 0;
    const std::string output = run_command(cmd, &rc);
    self->worker_message_ = output.empty() ? (rc == 0 ? "Timezone updated." : "Timezone update failed.") : output;
    self->worker_ok_ = (rc == 0);
    pthread_mutex_lock(&self->lock_);
    self->worker_done_ = true;
    pthread_mutex_unlock(&self->lock_);
    return nullptr;
}
