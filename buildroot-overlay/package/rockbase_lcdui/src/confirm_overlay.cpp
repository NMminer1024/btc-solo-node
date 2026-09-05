#include "confirm_overlay.h"

namespace {

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

}  // namespace

void ConfirmOverlay::open(const char* title, const char* message,
                          const char* confirm_text, const char* cancel_text) {
    title_ = title != nullptr ? title : "";
    message_ = message != nullptr ? message : "";
    confirm_text_ = confirm_text != nullptr ? confirm_text : "OK";
    single_button_ = cancel_text == nullptr;
    cancel_text_ = cancel_text != nullptr ? cancel_text : "";
    result_ = Result::None;
    overlay_.show(title_.c_str());
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
    lv_obj_set_pos(msg, 24, 86);
    lv_obj_set_width(msg, 712);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xc7d2d8), 0);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);

    if (!single_button_) {
        lv_obj_t* cancel = lv_btn_create(content);
        lv_obj_set_size(cancel, 220, 72);
        lv_obj_set_pos(cancel, 136, 360);
        style_button(cancel, 0x35414a);
        button_label(cancel, cancel_text_.c_str());
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
        lv_obj_set_size(ok, 480, 72);
        lv_obj_set_pos(ok, 140, 360);
    }
    else {
        lv_obj_set_size(ok, 220, 72);
        lv_obj_set_pos(ok, 404, 360);
    }
    style_button(ok, 0x2d8f5a);
    button_label(ok, confirm_text_.c_str());
    lv_obj_add_event_cb(ok, [](lv_event_t* e) {
        auto* self = static_cast<ConfirmOverlay*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->result_ = Result::Confirmed;
        }
    }, LV_EVENT_CLICKED, this);
}
