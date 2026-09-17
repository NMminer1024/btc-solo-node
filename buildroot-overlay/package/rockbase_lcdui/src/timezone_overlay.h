#pragma once

#include <pthread.h>

#include <string>
#include <vector>

#include "confirm_overlay.h"
#include "lvgl/lvgl.h"
#include "modal_overlay.h"

class TimezoneOverlay {
public:
    void open();
    void update();
    bool active() const { return overlay_.visible() || confirm_.active(); }
    void set_status_refresh_request(void (*cb)()) { status_refresh_request_ = cb; }

private:
    struct Zone {
        const char* key;
        const char* label;
        const char* utc_offset;
    };

    enum class Phase {
        Hidden,
        List,
        Confirm,
        Applying,
        Result,
    };

    void set_phase(Phase phase);
    void rebuild();
    void build_list();
    void build_confirm();
    void build_applying();
    void build_result();
    void start_apply();
    void finish_worker_if_ready();
    void clear_body();
    void request_close();
    void close();
    void set_message(const char* text);

    static void* apply_thread_entry(void* arg);

    static constexpr Zone kZones[] = {
        {"UTC", "UTC", "+0.0"},
        {"Africa/Cairo", "Africa/Cairo", "+3.0"},
        {"Africa/Johannesburg", "Africa/Johannesburg", "+2.0"},
        {"America/Anchorage", "America/Anchorage", "-8.0"},
        {"America/Chicago", "America/Chicago", "-5.0"},
        {"America/Denver", "America/Denver", "-6.0"},
        {"America/Los_Angeles", "America/Los_Angeles", "-7.0"},
        {"America/Mexico_City", "America/Mexico_City", "-6.0"},
        {"America/New_York", "America/New_York", "-4.0"},
        {"America/Santiago", "America/Santiago", "-4.0"},
        {"America/Sao_Paulo", "America/Sao_Paulo", "-3.0"},
        {"Asia/Bangkok", "Asia/Bangkok", "+7.0"},
        {"Asia/Dubai", "Asia/Dubai", "+4.0"},
        {"Asia/Kolkata", "Asia/Kolkata", "+5.5"},
        {"Asia/Seoul", "Asia/Seoul", "+9.0"},
        {"Asia/Shanghai", "Asia/Shanghai", "+8.0"},
        {"Asia/Singapore", "Asia/Singapore", "+8.0"},
        {"Asia/Tokyo", "Asia/Tokyo", "+9.0"},
        {"Australia/Perth", "Australia/Perth", "+8.0"},
        {"Australia/Sydney", "Australia/Sydney", "+10.0"},
        {"Europe/Berlin", "Europe/Berlin", "+2.0"},
        {"Europe/Istanbul", "Europe/Istanbul", "+3.0"},
        {"Europe/London", "Europe/London", "+1.0"},
        {"Europe/Moscow", "Europe/Moscow", "+3.0"},
        {"Pacific/Auckland", "Pacific/Auckland", "+12.0"},
        {"Pacific/Honolulu", "Pacific/Honolulu", "-10.0"},
    };

    ModalOverlay overlay_;
    Phase phase_ = Phase::Hidden;
    Phase built_phase_ = Phase::Hidden;
    pthread_t worker_ {};
    pthread_mutex_t lock_ = PTHREAD_MUTEX_INITIALIZER;
    bool worker_running_ = false;
    bool worker_done_ = false;
    bool worker_ok_ = false;
    bool close_requested_ = false;
    const Zone* selected_zone_ = nullptr;
    std::string worker_message_;
    void (*status_refresh_request_)() = nullptr;
    ConfirmOverlay confirm_;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* message_label_ = nullptr;
};
