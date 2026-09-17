#include "confirm_overlay.h"

#include "ui_style.h"

namespace {

// Compact confirm dialog geometry (8px grid).
constexpr int32_t kPanelW = 560;
constexpr int32_t kPanelH = 260;
constexpr int32_t kPadX = 32;
constexpr int32_t kBtnH = 56;
constexpr int32_t kBtnW = 240;
constexpr int32_t kBtnGap = 20;
constexpr int32_t kBtnY = 172;

}  // namespace

void ConfirmOverlay::open(const char* title, const char* message,
                          const char* confirm_text, const char* cancel_text,
                          const char* dont_ask_text, uint32_t message_color) {
    title_ = title != nullptr ? title : "";
    message_ = message != nullptr ? message : "";
    confirm_text_ = confirm_text != nullptr ? confirm_text : "OK";
    no_button_ = confirm_text == nullptr;
    single_button_ = cancel_text == nullptr;
    cancel_text_ = cancel_text != nullptr ? cancel_text : "";
    dont_ask_text_ = dont_ask_text != nullptr ? dont_ask_text : "";
    message_color_ = message_color;
    dont_ask_checked_ = false;
    result_ = Result::None;
    // Grow the panel to fit the optional "Don't ask me" checkbox row.
    const int32_t panel_h = dont_ask_text_.empty() ? kPanelH : kPanelH + 44;
    overlay_.show(title_.c_str(), kPanelW, panel_h, true);
    // The confirm/cancel buttons already cover dismissal; hide the ghost X.
    overlay_.hide_close_button();
    build();
}

void ConfirmOverlay::update() {
    if (overlay_.visible() && overlay_.close_requested()) {
        overlay_.clear_close_requested();
        result_ = Result::Cancelled;
        return;
    }
    if (!overlay_.visible()) {
        result_ = Result::None;
    }
}

void ConfirmOverlay::close() {
    overlay_.hide();
    result_ = Result::None;
}

ConfirmOverlay::Result ConfirmOverlay::consume_result() {
    const Result out = result_;
    result_ = Result::None;
    return out;
}

void ConfirmOverlay::build() {
    lv_obj_t* content = overlay_.content();
    if (content == nullptr) return;

    lv_obj_t* msg = lv_label_create(content);
    lv_label_set_text(msg, message_.c_str());
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(message_color_), 0);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, kPanelW - 2 * kPadX);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, 0);
    if (no_button_) {
        // Bare progress panel: center the single message line.
        lv_obj_center(msg);
        return;
    }
    lv_obj_set_pos(msg, kPadX, 72);

    if (!single_button_) {
        lv_obj_t* cancel = lv_btn_create(content);
        lv_obj_set_size(cancel, kBtnW, kBtnH);
        lv_obj_set_pos(cancel, kPadX, kBtnY);
        ui_style_secondary_button(cancel);
        ui_button_label(cancel, cancel_text_.c_str(), 0xc7d2d8);
        lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
            auto* self = static_cast<ConfirmOverlay*>(lv_event_get_user_data(e));
            if (self != nullptr) {
                self->result_ = Result::Cancelled;
            }
        }, LV_EVENT_CLICKED, this);
    }

    lv_obj_t* ok = lv_btn_create(content);
    if (single_button_) {
        // Wide and centered - a single button doesn't need to share the row.
        lv_obj_set_size(ok, kPanelW - 2 * kPadX, kBtnH);
        lv_obj_set_pos(ok, kPadX, kBtnY);
    }
    else {
        lv_obj_set_size(ok, kBtnW, kBtnH);
        lv_obj_set_pos(ok, kPanelW - kPadX - kBtnW, kBtnY);
    }
    ui_style_primary_button(ok);
    ui_button_label(ok, confirm_text_.c_str(), 0xffffff);
    lv_obj_add_event_cb(ok, [](lv_event_t* e) {
        auto* self = static_cast<ConfirmOverlay*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->result_ = Result::Confirmed;
        }
    }, LV_EVENT_CLICKED, this);

    if (!dont_ask_text_.empty()) {
        dont_ask_cb_ = lv_checkbox_create(content);
        lv_obj_set_pos(dont_ask_cb_, kPadX, 244);
        // In LVGL 8 the checkbox text is stored in cb->txt (default "Check box"),
        // not a child label. Set it directly and style the checkbox object itself.
        lv_checkbox_set_text(dont_ask_cb_, dont_ask_text_.c_str());
        lv_obj_set_style_text_font(dont_ask_cb_, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(dont_ask_cb_, lv_color_hex(0x6b7d8a), 0);
        lv_obj_set_style_bg_color(dont_ask_cb_, lv_color_hex(0x18242e), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(dont_ask_cb_, lv_color_hex(0x3b82c4), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_radius(dont_ask_cb_, 6, LV_PART_INDICATOR);
        lv_obj_add_event_cb(dont_ask_cb_, [](lv_event_t* e) {
            auto* self = static_cast<ConfirmOverlay*>(lv_event_get_user_data(e));
            if (self != nullptr) {
                self->dont_ask_checked_ =
                    lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(e)), LV_STATE_CHECKED);
            }
        }, LV_EVENT_VALUE_CHANGED, this);
    }
}
