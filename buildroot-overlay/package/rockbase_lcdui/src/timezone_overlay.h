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

    // ===== 时区数据唯一权威源 =====
    // 此处维护 UI 列表顺序 + 每项 UTC 偏移显示文本。
    // 新增/删除/改名时区时，必须同步以下位置（时区名需保持一致）：
    //   src/main.cpp                  timezone_posix_spec()     时区名 -> POSIX TZ（进程内生效）
    //   src/main.cpp                  timezone_offset_label()   时区名 -> offset 显示
    //   src/timezone_overlay.cpp      current_timezone_text()   当前时区回显（仅兼容 CST-8）
    //   etc/init.d/S03lcdui           case "${TZ}"              启动脚本 TZ 映射
    //   usr/bin/rockbase_timezone.sh  normalize_tz()/list       脚本侧归一化 + 清单
    //   usr/bin/rockbase_statusd.sh   timezone_from_system()    状态上报（仅兼容 CST-8）
    static constexpr Zone kZones[] = {
        {"Africa/Cairo", "Africa/Cairo", "+2.0"},
        {"America/Anchorage", "America/Anchorage", "-9.0"},
        {"America/Chicago", "America/Chicago", "-6.0"},
        {"America/Denver", "America/Denver", "-7.0"},
        {"America/Los_Angeles", "America/Los_Angeles", "-8.0"},
        {"America/New_York", "America/New_York", "-5.0"},
        {"America/Santiago", "America/Santiago", "-4.0"},
        {"America/Sao_Paulo", "America/Sao_Paulo", "-3.0"},
        {"Asia/Bangkok", "Asia/Bangkok", "+7.0"},
        {"Asia/Dhaka", "Asia/Dhaka", "+6.0"},
        {"Asia/Dubai", "Asia/Dubai", "+4.0"},
        {"Asia/Kabul", "Asia/Kabul", "+4.5"},
        {"Asia/Karachi", "Asia/Karachi", "+5.0"},
        {"Asia/Kolkata", "Asia/Kolkata", "+5.5"},
        {"Asia/Shanghai", "Asia/Shanghai", "+8.0"},
        {"Asia/Tehran", "Asia/Tehran", "+3.5"},
        {"Asia/Tokyo", "Asia/Tokyo", "+9.0"},
        {"Asia/Yangon", "Asia/Yangon", "+6.5"},
        {"Atlantic/Azores", "Atlantic/Azores", "-1.0"},
        {"Australia/Adelaide", "Australia/Adelaide", "+9.5"},
        {"Australia/Perth", "Australia/Perth", "+8.0"},
        {"Australia/Sydney", "Australia/Sydney", "+10.0"},
        {"Europe/Berlin", "Europe/Berlin", "+1.0"},
        {"Europe/Moscow", "Europe/Moscow", "+3.0"},
        {"Pacific/Auckland", "Pacific/Auckland", "+12.0"},
        {"Pacific/Honolulu", "Pacific/Honolulu", "-10.0"},
        {"Pacific/Noumea", "Pacific/Noumea", "+11.0"},
        {"Pacific/Pago_Pago", "Pacific/Pago_Pago", "-11.0"},
        {"UTC", "UTC", "+0.0"},
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
