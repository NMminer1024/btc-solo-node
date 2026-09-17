#include "confirm_overlay.h"

namespace {

// Compact confirm dialog geometry (8px grid).
constexpr int32_t kPanelW = 560;
constexpr int32_t kPanelH = 260;
constexpr int32_t kPadX = 32;
constexpr int32_t kBtnH = 56;
constexpr int32_t kBtnW = 240;
constexpr int32_t kBtnGap = 20;
constexpr int32_t kBtnY = 172;

// Primary (confirm) button: theme blue, capsule, subtle shadow + pressed state.
void style_primary_button(lv_obj_t* btn) {
    lv_obj_set_style_radius(btn, kBtnH / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3b82c4), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 16, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x3b82c4), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_shadow_ofs_y(btn, 6, 0);
    lv_obj_set_style_shadow_spread(btn, -8, 0);
    // Pressed: darken + drop the shadow for a tactile sink.
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2f6a9e), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_PRESSED);
}

// Secondary (cancel) button: quiet outline capsule.
void style_secondary_button(lv_obj_t* btn) {
    lv_obj_set_style_radius(btn, kBtnH / 2, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x18242e), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x2d4654), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1f2f3a), LV_STATE_PRESSED);
}

lv_obj_t* button_label(lv_obj_t* btn, const char* text, uint32_t color) {
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_center(label);
    return label;
}

}  // namespace

void ConfirmOverlay::open(const char* title, const char* message,
                          const char* confirm_text, const char* cancel_text,
                          const char* dont_ask_text) {
    title_ = title != nullptr ? title : "";
    message_ = message != nullptr ? message : "";
    confirm_text_ = confirm_text != nullptr ? confirm_text : "OK";
    single_button_ = cancel_text == nullptr;
    cancel_text_ = cancel_text != nullptr ? cancel_text : "";
    dont_ask_text_ = dont_ask_text != nullptr ? dont_ask_text : "";
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
    lv_obj_set_pos(msg, kPadX, 72);
    lv_obj_set_width(msg, kPanelW - 2 * kPadX);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0x9fb0ba), 0);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

    if (!single_button_) {
        lv_obj_t* cancel = lv_btn_create(content);
        lv_obj_set_size(cancel, kBtnW, kBtnH);
        lv_obj_set_pos(cancel, kPadX, kBtnY);
        style_secondary_button(cancel);
        button_label(cancel, cancel_text_.c_str(), 0xc7d2d8);
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
    style_primary_button(ok);
    button_label(ok, confirm_text_.c_str(), 0xffffff);
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
