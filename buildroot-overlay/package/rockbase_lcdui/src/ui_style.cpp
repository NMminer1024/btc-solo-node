#include "ui_style.h"

void ui_style_primary_button(lv_obj_t* btn) {
    if (btn == nullptr) return;
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
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

void ui_style_secondary_button(lv_obj_t* btn) {
    if (btn == nullptr) return;
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x18242e), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x2d4654), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1f2f3a), LV_STATE_PRESSED);
}

lv_obj_t* ui_button_label(lv_obj_t* btn, const char* text, uint32_t color) {
    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_center(label);
    return label;
}
