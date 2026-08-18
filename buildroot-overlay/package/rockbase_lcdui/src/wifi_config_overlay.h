#pragma once

#include <pthread.h>

#include <string>
#include <vector>

#include "lvgl/lvgl.h"
#include "modal_overlay.h"

class WifiConfigOverlay {
public:
    void open();
    void update();
    bool active() const { return overlay_.visible(); }

private:
    struct Network {
        std::string ssid;
        int signal = -100;
        std::string security;
    };

    enum class Phase {
        Hidden,
        Scanning,
        List,
        Password,
        Applying,
        Result,
    };

    void set_phase(Phase phase);
    void rebuild();
    void build_scanning();
    void build_list();
    void build_password();
    void build_applying();
    void build_result();
    void start_scan();
    void start_apply();
    void finish_worker_if_ready();
    void clear_body();
    void cleanup_keyboard();
    void show_keyboard();
    void hide_keyboard();
    void request_close();
    void close();
    void set_message(const char* text);

    static void* scan_thread_entry(void* arg);
    static void* apply_thread_entry(void* arg);
    static std::vector<Network> parse_scan_json(const std::string& json);

    ModalOverlay overlay_;
    Phase phase_ = Phase::Hidden;
    Phase built_phase_ = Phase::Hidden;
    pthread_t worker_ {};
    pthread_mutex_t lock_ = PTHREAD_MUTEX_INITIALIZER;
    bool worker_running_ = false;
    bool worker_done_ = false;
    bool worker_ok_ = false;
    bool apply_started_ = false;
    bool close_requested_ = false;
    std::vector<Network> networks_;
    std::string pending_ssid_;
    std::string pending_password_;
    std::string worker_message_;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* message_label_ = nullptr;
    lv_obj_t* password_ta_ = nullptr;
    lv_obj_t* keyboard_ = nullptr;
};
