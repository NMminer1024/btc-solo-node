#include "network_settings_overlay.h"

namespace {

void style_setting_button(lv_obj_t* button) {
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x14222a), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x304551), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x20343e), LV_STATE_PRESSED);
}

}  // namespace

void NetworkSettingsOverlay::open(const std::string& wifi_summary,
                                  const std::string& timezone_summary,
                                  bool wifi_enabled) {
    lv_obj_t* panel = overlay_.show("Network Settings", 680, 360);
    if (panel == nullptr) {
        return;
    }

    pending_action_ = Action::None;
    lv_obj_t* intro = lv_label_create(panel);
    lv_label_set_text(intro, "Connectivity and local time configuration");
    lv_obj_set_pos(intro, 32, 72);
    lv_obj_set_style_text_font(intro, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(intro, lv_color_hex(0x8fa0aa), 0);

    add_setting_row(112, LV_SYMBOL_WIFI, "WiFi", wifi_summary, wifi_clicked, wifi_enabled);
    add_setting_row(220, LV_SYMBOL_GPS, "Timezone", timezone_summary, timezone_clicked);
}

void NetworkSettingsOverlay::update() {
    if (overlay_.visible() && overlay_.close_requested()) {
        overlay_.hide();
    }
}

NetworkSettingsOverlay::Action NetworkSettingsOverlay::take_action() {
    const Action action = pending_action_;
    pending_action_ = Action::None;
    return action;
}

void NetworkSettingsOverlay::add_setting_row(int32_t y, const char* icon, const char* title,
                                             const std::string& summary,
                                             lv_event_cb_t callback,
                                             bool enabled) {
    lv_obj_t* panel = overlay_.content();
    if (panel == nullptr) {
        return;
    }

    lv_obj_t* button = lv_btn_create(panel);
    lv_obj_set_pos(button, 32, y);
    lv_obj_set_size(button, 616, 88);
    style_setting_button(button);
    if (enabled) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, this);
    }
    else {
        lv_obj_add_state(button, LV_STATE_DISABLED);
        lv_obj_clear_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x101a20), LV_STATE_DISABLED);
        lv_obj_set_style_border_color(button, lv_color_hex(0x24323a), LV_STATE_DISABLED);
    }

    lv_obj_t* icon_label = lv_label_create(button);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(icon_label,
                                lv_color_hex(enabled ? 0xff9a72 : 0x526269), 0);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t* title_label = lv_label_create(button);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 72, 11);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(title_label,
                                lv_color_hex(enabled ? 0xf4efe2 : 0x6f7d85), 0);

    lv_obj_t* summary_label = lv_label_create(button);
    lv_label_set_text(summary_label, summary.c_str());
    lv_obj_set_pos(summary_label, 72, 46);
    lv_obj_set_width(summary_label, 450);
    lv_label_set_long_mode(summary_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(summary_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(summary_label,
                                lv_color_hex(enabled ? 0x9aa6ac : 0x526269), 0);

    if (enabled) {
        lv_obj_t* arrow = lv_label_create(button);
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_font(arrow, &lv_font_montserrat_22, 0);
        lv_obj_set_style_text_color(arrow, lv_color_hex(0x78848b), 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -12, 0);
    }
}

void NetworkSettingsOverlay::wifi_clicked(lv_event_t* event) {
    auto* self = static_cast<NetworkSettingsOverlay*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }
    self->pending_action_ = Action::Wifi;
    self->overlay_.hide();
    lv_event_stop_processing(event);
}

void NetworkSettingsOverlay::timezone_clicked(lv_event_t* event) {
    auto* self = static_cast<NetworkSettingsOverlay*>(lv_event_get_user_data(event));
    if (self == nullptr) {
        return;
    }
    self->pending_action_ = Action::Timezone;
    self->overlay_.hide();
    lv_event_stop_processing(event);
}
