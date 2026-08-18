#pragma once

#include "lvgl/lvgl.h"

class ModalOverlay {
public:
    ModalOverlay() = default;
    ~ModalOverlay();

    lv_obj_t* show(const char* title);
    void hide();
    bool visible() const { return panel_ != nullptr; }
    bool close_requested() const { return close_requested_; }
    void clear_close_requested() { close_requested_ = false; }
    lv_obj_t* content() const { return content_; }

private:
    lv_obj_t* panel_ = nullptr;
    lv_obj_t* content_ = nullptr;
    bool close_requested_ = false;
};
