#pragma once

#include "lvgl/lvgl.h"

// Shared Rockbase button styling. The visual source of truth is the OTA
// "Update Available" confirm dialog; every action button (OK/Cancel/Apply/
// Back/Done) must go through these helpers so the whole UI stays consistent.

// Primary action button: theme-blue capsule with a soft glow shadow and a
// darker pressed state (used for OK / Apply / Done).
void ui_style_primary_button(lv_obj_t* btn);

// Secondary action button: quiet outline capsule (used for Cancel / Back).
void ui_style_secondary_button(lv_obj_t* btn);

// Centered button label using the standard button text size.
lv_obj_t* ui_button_label(lv_obj_t* btn, const char* text, uint32_t color);
