#pragma once

#include "lvgl/lvgl.h"

class ModalOverlay {
public:
    ModalOverlay() = default;
    ~ModalOverlay();

    lv_obj_t* show(const char* title, int32_t width = 760, int32_t height = 456,
                   bool center_title = false);
    void hide();
    bool visible() const { return panel_ != nullptr; }
    bool close_requested() const { return close_requested_; }
    void clear_close_requested() { close_requested_ = false; }
    lv_obj_t* content() const { return content_; }
    void hide_close_button();

private:
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* close_btn_ = nullptr;
    bool close_requested_ = false;
};
