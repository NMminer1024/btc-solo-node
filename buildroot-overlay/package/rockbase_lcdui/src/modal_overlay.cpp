#include "modal_overlay.h"

ModalOverlay::~ModalOverlay() {
    hide();
}

lv_obj_t* ModalOverlay::show(const char* title) {
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
    lv_obj_set_size(content_, 760, 456);
    lv_obj_center(content_);
    lv_obj_set_style_radius(content_, 24, 0);
    lv_obj_set_style_bg_color(content_, lv_color_hex(0x0c141b), 0);
    lv_obj_set_style_bg_opa(content_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content_, 1, 0);
    lv_obj_set_style_border_color(content_, lv_color_hex(0x2d4654), 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_clear_flag(content_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(content_);
    lv_label_set_text(title_label, title != nullptr ? title : "");
    lv_obj_set_pos(title_label, 24, 18);
    lv_obj_set_width(title_label, 560);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xf4efe2), 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);

    lv_obj_t* close_btn = lv_btn_create(content_);
    lv_obj_set_size(close_btn, 108, 52);
    lv_obj_set_pos(close_btn, 628, 10);
    lv_obj_set_style_radius(close_btn, 18, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x22313a), 0);
    lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(close_btn, 0, 0);
    lv_obj_t* close_label = lv_label_create(close_btn);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(close_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(close_label, lv_color_hex(0xc7d2d8), 0);
    lv_obj_center(close_label);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* self = static_cast<ModalOverlay*>(lv_event_get_user_data(e));
        if (self != nullptr) {
            self->close_requested_ = true;
        }
        lv_event_stop_processing(e);
    }, LV_EVENT_CLICKED, this);

    return content_;
}

void ModalOverlay::hide() {
    if (panel_ != nullptr) {
        lv_obj_del(panel_);
        panel_ = nullptr;
        content_ = nullptr;
    }
    close_requested_ = false;
}
