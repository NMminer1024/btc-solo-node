#include "modal_overlay.h"

ModalOverlay::~ModalOverlay() {
    hide();
}

lv_obj_t* ModalOverlay::show(const char* title, int32_t width, int32_t height) {
    hide();
    close_requested_ = false;

    panel_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel_);
    lv_obj_set_size(panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(panel_, 0, 0);
    lv_obj_set_style_bg_color(panel_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(panel_, LV_OPA_70, 0);
    lv_obj_clear_flag(panel_, LV_OBJ_FLAG_SCROLLABLE);

    content_ = lv_obj_create(panel_);
    lv_obj_remove_style_all(content_);
    lv_obj_set_size(content_, width, height);
    lv_obj_center(content_);
    lv_obj_set_style_radius(content_, 20, 0);
    lv_obj_set_style_bg_color(content_, lv_color_hex(0x0e161e), 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content_, 1, 0);
    lv_obj_set_style_border_color(content_, lv_color_hex(0x24343f), 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    // Soft drop shadow lifts the card off the dimmed backdrop.
    lv_obj_set_style_shadow_width(content_, 40, 0);
    lv_obj_set_style_shadow_color(content_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(content_, LV_OPA_60, 0);
    lv_obj_set_style_shadow_ofs_y(content_, 12, 0);
    lv_obj_set_style_shadow_spread(content_, -20, 0);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(content_);
    lv_label_set_text(title_label, title != nullptr ? title : "");
    lv_obj_set_pos(title_label, 32, 28);
    lv_obj_set_width(title_label, width - 120);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xf4efe2), 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);

    // Ghost close icon: transparent, unobtrusive (top-right), but with a
    // generous touch target so it is easy to hit.
    close_btn_ = lv_btn_create(content_);
    lv_obj_set_size(close_btn_, 56, 56);
    lv_obj_set_pos(close_btn_, width - 76, 16);
    lv_obj_set_style_radius(close_btn_, 28, 0);
    lv_obj_set_style_bg_color(close_btn_, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(close_btn_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(close_btn_, 0, 0);
    lv_obj_set_style_pad_all(close_btn_, 0, 0);
    // Subtle hover/pressed fill so the target is discoverable without being loud.
    lv_obj_set_style_bg_color(close_btn_, lv_color_hex(0x1a2630), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(close_btn_, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_t* close_label = lv_label_create(close_btn_);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0x8496a1), 0);
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_btn_, [](lv_event_t* e) {
        auto* self = static_cast<ModalOverlay*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->close_requested_ = true;
        }
        lv_event_stop_processing(e);
    }, LV_EVENT_CLICKED, this);

    return content_;
}

void ModalOverlay::hide_close_button() {
    if (close_btn_ != nullptr) {
        lv_obj_add_flag(close_btn_, LV_OBJ_FLAG_HIDDEN);
    }
}

void ModalOverlay::hide() {
    if (panel_ != nullptr) {
        lv_obj_del(panel_);
        panel_ = nullptr;
        content_ = nullptr;
    }
    close_requested_ = false;
}
