#pragma once

#include <string>
#include <vector>

#include "lvgl/lvgl.h"
#include "modal_overlay.h"

class MinersOverlay {
public:
    struct Miner {
        std::string address = "-";
        bool online = false;
        int workers = 0;
        std::string hashrate_5m = "--";
        std::string best_share = "--";
        std::string last_share_ago = "-";
    };

    struct Snapshot {
        int total_miners = 0;
        int online_miners = 0;
        int total_workers = 0;
        std::string total_hashrate_5m = "--";
        std::vector<Miner> miners;
    };

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
