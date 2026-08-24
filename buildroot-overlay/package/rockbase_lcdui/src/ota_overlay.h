#pragma once

#include <cstdint>
#include <string>

#include "lvgl/lvgl.h"

// Full-screen OTA upgrade overlay. Purely driven by the state file
// /run/rockbase/ota-state.json (written by the OTA script): there is no
// trigger button and no close interaction. "failed" stays on screen until
// reboot clears /run.
class OtaOverlay {
public:
    void update();  // call from the main loop; polls the state file at ~1 Hz
    bool active() const { return root_ != nullptr; }

private:
    enum class Phase {
        Hidden,
        Preparing,
        Downloading,
        Verifying,
        Flashing,
        Switching,
        Failed,
    };

    void poll_state_file();
    void open();
    void close();
    void apply_state();
    int current_step() const;
    static Phase parse_phase_name(const std::string& name);
    static const char* default_message(Phase phase);
    static const char* short_stage_name(Phase phase);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* subtitle_label_ = nullptr;
    lv_obj_t* steps_row_ = nullptr;
    lv_obj_t* step_labels_[4] {};
    lv_obj_t* bar_ = nullptr;
    lv_obj_t* progress_label_ = nullptr;
    lv_obj_t* message_label_ = nullptr;
    lv_obj_t* warn_line1_ = nullptr;
    lv_obj_t* warn_line2_ = nullptr;
    lv_obj_t* failed_hint_label_ = nullptr;
    Phase phase_ = Phase::Hidden;
    int progress_ = -1;
    int speed_bps_ = -1;
    std::string message_;
    uint32_t last_poll_ms_ = 0;
};
