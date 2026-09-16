#pragma once

#include <cstdint>
#include <string>

#include "confirm_overlay.h"
#include "lvgl/lvgl.h"

// Passive OTA update checker. Periodically (every 60 s, first run 60 s
// after boot) launches `rockbase-ota.sh check-async` in the background and
// polls /run/rockbase/ota-check-state.json for the result. When a newer
// version is available the user is prompted via ConfirmOverlay; confirming
// hands off to `rockbase-ota.sh apply --force`, whose progress is then
// shown by the existing OtaOverlay. No prompt is shown when the board is
// already on the latest version or when the check fails, and a dismissed
// prompt is not repeated for the same version.
class FirmwareUpdateOverlay {
public:
    void update();  // call once per main-loop tick
    bool active() const { return confirm_.active(); }

private:
    enum class Phase {
        Idle,
        Checking,
    };

    void start_check();
    void poll_state_file();
    void start_apply();

    ConfirmOverlay confirm_;
    Phase phase_ = Phase::Idle;
    std::string current_version_;
    std::string latest_version_;
    std::string manifest_url_;
    std::string prompted_version_;  // last version prompted for (suppress re-prompts)
    uint32_t check_start_ms_ = 0;
    uint32_t last_poll_ms_ = 0;
    uint32_t last_check_ms_ = 0;
};
