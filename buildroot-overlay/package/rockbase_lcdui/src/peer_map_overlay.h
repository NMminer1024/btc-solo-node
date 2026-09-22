#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl/lvgl.h"

// Full-screen peer map overlay (distinct from the card-style ModalOverlay
// used by Wi-Fi / timezone): an opaque 800x480 backdrop showing the world
// map with peer dots, a red "self" star and animated data-flow arcs, plus a
// ghost close (X) button in the top-right corner.
//
// Data flow:
//   - peer IPs + self_geo come from the UiModel snapshot (main.cpp parses
//     /tmp/status-api.json, same source as the web dashboard);
//   - geolocation is resolved offline by exec'ing /usr/bin/rockbase-geoip
//     (GeoLite2-City.mmdb), with a process-lifetime cache;
//   - the static map backdrop is a pre-rendered ARGB image
//     (tools/gen_world_map_image.py, same projection as the web map).
class PeerMapOverlay {
public:
    struct Geo {
        double lat = 0.0;
        double lon = 0.0;
        std::string city;
        std::string country;
        bool known = false;
    };

    void open(const std::vector<std::string>& peer_ips, const Geo& self_geo);
    void update();  // call from the main loop (drives the arc animation)
    bool active() const { return root_ != nullptr; }

private:
    struct Marker {
        int32_t x = 0;
        int32_t y = 0;
        bool inbound = false;
    };

    void close();
    void draw_frame(double t);
    void draw_arc(int32_t x1, int32_t y1, int32_t x2, int32_t y2, double t,
                  uint32_t color);
    void draw_star(int32_t cx, int32_t cy, int32_t r, uint32_t fill,
                   uint32_t stroke);
    void resolve_geo(const std::vector<std::string>& ips);
    void set_status(const std::string& text);
    static void on_close_btn(lv_event_t* e);
    static void anim_timer_cb(lv_timer_t* timer);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* map_img_ = nullptr;
    lv_obj_t* close_btn_ = nullptr;
    lv_obj_t* title_label_ = nullptr;
    lv_obj_t* status_label_ = nullptr;
    lv_timer_t* anim_timer_ = nullptr;

    std::vector<uint8_t> backdrop_;  // immutable pre-rendered map
    std::vector<uint8_t> buf_;       // composited 800x480 ARGB8888 frame
    lv_image_dsc_t img_dsc_ {};
    bool map_ready_ = false;
    bool close_requested_ = false;

    std::vector<std::string> ips_;
    std::vector<Marker> markers_;
    Geo self_geo_ {};
    int32_t self_x_ = -1;
    int32_t self_y_ = -1;
    uint32_t anim_start_ms_ = 0;
};
