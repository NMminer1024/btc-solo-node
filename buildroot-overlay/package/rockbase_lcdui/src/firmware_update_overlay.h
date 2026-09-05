#pragma once

#include <cstdint>
#include <string>

#include "confirm_overlay.h"
#include "lvgl/lvgl.h"
#include "modal_overlay.h"

// "Firmware / Check Update" card on the System page. Kicks off
// `rockbase-ota.sh check-async` in the background and polls
// /run/rockbase/ota-check-state.json (same file-polling pattern as
// OtaOverlay) for the result. If an update is available the user is asked
// via ConfirmOverlay; confirming hands off to `rockbase-ota.sh apply
// --force`, whose progress is then shown by the existing OtaOverlay.
class FirmwareUpdateOverlay {
public:
    void open();   // start a check; safe to call repeatedly (no-op while busy)
    void update(); // call once per main-loop tick
    bool active() const { return overlay_.visible() || confirm_.active(); }

private:
    enum class Phase {
        Hidden,
        Checking,
        UpToDate,
        UpdateAvailable,
        Failed,
    };

    void poll_state_file();
    void show_message(const char* title, const std::string& message);
    void start_apply();

    ModalOverlay overlay_;
    ConfirmOverlay confirm_;
    Phase phase_ = Phase::Hidden;
    std::string current_version_;
    std::string latest_version_;
    std::string manifest_url_;
    std::string message_;
    uint32_t check_start_ms_ = 0;
    uint32_t last_poll_ms_ = 0;
};
