#pragma once

#include <string>
#include <vector>

#include "core/dashboard_data.h"
#include "lvgl/lvgl.h"
#include "modal_overlay.h"

class MinersOverlay {
public:
    // Data types live in core/dashboard_data.h (LVGL-free, host-testable).
    // The aliases keep existing call sites (MinersOverlay::Snapshot, ...) valid.
    using Miner = lcdui::MinerInfo;
    using Snapshot = lcdui::MinersSnapshot;

    void set_snapshot(const Snapshot& snapshot);
    void open();
    void update();
    bool active() const { return overlay_.visible(); }

private:
    void close();
    void rebuild();

    Snapshot snapshot_;
    ModalOverlay overlay_;
};
