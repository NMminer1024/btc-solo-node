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
              const char* cancel_text = "Cancel");
    void update();
    void close();

    bool active() const { return overlay_.visible(); }
    Result consume_result();

private:
    void build();

    ModalOverlay overlay_;
    Result result_ = Result::None;
    std::string title_;
    std::string message_;
    std::string confirm_text_;
    std::string cancel_text_;
};
