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
                          const char* confirm_text, const char* cancel_text) {
    title_ = title != nullptr ? title : "";
    message_ = message != nullptr ? message : "";
    confirm_text_ = confirm_text != nullptr ? confirm_text : "OK";
    single_button_ = cancel_text == nullptr;
    cancel_text_ = cancel_text != nullptr ? cancel_text : "";
    result_ = Result::None;
    overlay_.show(title_.c_str(), kPanelW, kPanelH);
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
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, 0);
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
}
