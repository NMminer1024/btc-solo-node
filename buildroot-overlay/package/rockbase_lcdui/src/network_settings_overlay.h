#pragma once

#include <string>

#include "lvgl/lvgl.h"
#include "modal_overlay.h"

class NetworkSettingsOverlay {
public:
    enum class Action {
        None,
        Wifi,
        Timezone,
    };

    void open(const std::string& wifi_summary, const std::string& timezone_summary,
              bool wifi_enabled);
    void update();
    bool active() const { return overlay_.visible(); }
    Action take_action();

private:
    static void wifi_clicked(lv_event_t* event);
    static void timezone_clicked(lv_event_t* event);
    void add_setting_row(int32_t y, const char* icon, const char* title,
                         const std::string& summary, lv_event_cb_t callback,
                         bool enabled = true);

    ModalOverlay overlay_;
    Action pending_action_ = Action::None;
};
