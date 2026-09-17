#pragma once

#include <string>

#include "lvgl/lvgl.h"
#include "modal_overlay.h"

class ConfirmOverlay {
public:
    enum class Result {
        None,
        Confirmed,
        Cancelled,
    };

    void open(const char* title, const char* message,
              const char* confirm_text = "OK",
              const char* cancel_text = "Cancel",
              const char* dont_ask_text = nullptr,
              uint32_t message_color = 0x9fb0ba);
    void update();
    void close();

    bool active() const { return overlay_.visible(); }
    Result consume_result();
    // True if the optional "Don't ask me" checkbox was ticked when the
    // dialog was dismissed. Only meaningful when open() was given a
    // non-null dont_ask_text.
    bool dont_ask_checked() const { return dont_ask_checked_; }

private:
    void build();

    ModalOverlay overlay_;
    Result result_ = Result::None;
    std::string title_;
    std::string message_;
    std::string confirm_text_;
    std::string cancel_text_;
    std::string dont_ask_text_;
    uint32_t message_color_ = 0x9fb0ba;
    // confirm_text == nullptr in open() means "no buttons": a bare progress
    // panel (used for "Applying..." while a worker runs in the background).
    bool no_button_ = false;
    // cancel_text == nullptr in open() means "single button": no Cancel
    // button at all, just a wide centered confirm button (see build()).
    bool single_button_ = false;
    bool dont_ask_checked_ = false;
    lv_obj_t* dont_ask_cb_ = nullptr;
};
