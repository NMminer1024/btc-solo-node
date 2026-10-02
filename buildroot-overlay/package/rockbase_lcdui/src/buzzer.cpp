#include "buzzer.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace {

// IO43 is muxed to PWM1 (alt2), which is channel 1 of the pwm0_2 node
// (compatible "canaan,k230-pwm", npwm=3). The kernel exposes each enabled
// pwm node as a chip under /sys/class/pwm (e.g. pwmchip0 for pwm0_2,
// pwmchip3 for pwm3_5). A channel's pwmN sysfs directory only appears after
// the channel is written to the chip's "export" file, so we must export
// channel 1 before touching period/duty_cycle/enable. We identify the right
// chip by its device path containing "pwm0_2" (IO43 = PWM1 lives there, not
// on pwm3_5 where channel 1 would be PWM4).
constexpr const char* kPwmClassDir = "/sys/class/pwm";
constexpr const char* kBuzzerPwmNode = "pwm0_2";  // IO43 = PWM1 = channel 1 of pwm0_2
constexpr const char* kBuzzerPwmChannel = "pwm1";  // channel 1 of pwm0_2
constexpr uint32_t kBuzzerFreqHz = 1000;  // passive buzzer resonant tone
constexpr uint32_t kBuzzerBeepMs = 80;    // single click duration
constexpr uint32_t kBuzzerMinIntervalMs = 60;  // throttle: avoid a buzz on rapid taps

char g_chip_dir[128] = {};
char g_export_path[128] = {};
char g_period_path[128] = {};
char g_duty_path[128] = {};
char g_enable_path[128] = {};
bool g_pwm_ready = false;
uint64_t g_last_beep_ms = 0;
uint64_t g_beep_until_ms = 0;

// Millisecond monotonic clock, matching the tick_ms() style used elsewhere.
uint64_t now_ms() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000U + static_cast<uint64_t>(ts.tv_nsec) / 1000000U;
}

// Write a string to a sysfs file, truncating. Returns true on success.
bool sysfs_write(const char* path, const char* value) {
    int fd = open(path, O_WRONLY | O_TRUNC);
    if (fd < 0) {
        std::fprintf(stderr, "rockbase-lcdui: open %s failed: %s\n", path, std::strerror(errno));
        return false;
    }
    const size_t len = std::strlen(value);
    const ssize_t n = write(fd, value, len);
    close(fd);
    if (n != static_cast<ssize_t>(len)) {
        std::fprintf(stderr, "rockbase-lcdui: write %s failed: %s\n", path, std::strerror(errno));
        return false;
    }
    return true;
}

// Locate the pwm chip whose device path contains the target node (pwm0_2) and
// fill in the export / period / duty_cycle / enable sysfs paths. The channel
// directory (pwm1) does not exist yet; it is created by writing to export.
// Returns true on success.
bool find_buzzer_pwm() {
    DIR* dir = opendir(kPwmClassDir);
    if (!dir) {
        std::fprintf(stderr, "rockbase-lcdui: open %s failed: %s\n", kPwmClassDir, std::strerror(errno));
        return false;
    }
    bool found = false;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::strncmp(ent->d_name, "pwmchip", 7) != 0) {
            continue;
        }
        char device_link[128];
        std::snprintf(device_link, sizeof(device_link), "%s/%s/device", kPwmClassDir, ent->d_name);
        char device_path[256] = {};
        const ssize_t n = readlink(device_link, device_path, sizeof(device_path) - 1);
        if (n <= 0) {
            continue;
        }
        device_path[n] = '\0';
        if (std::strstr(device_path, kBuzzerPwmNode) == nullptr) {
            continue;  // not the pwm0_2 chip (IO43 = PWM1 lives here)
        }
        std::snprintf(g_chip_dir, sizeof(g_chip_dir), "%s/%s", kPwmClassDir, ent->d_name);
        std::snprintf(g_export_path, sizeof(g_export_path), "%s/export", g_chip_dir);
        std::snprintf(g_period_path, sizeof(g_period_path), "%s/%s/period", g_chip_dir, kBuzzerPwmChannel);
        std::snprintf(g_duty_path, sizeof(g_duty_path), "%s/%s/duty_cycle", g_chip_dir, kBuzzerPwmChannel);
        std::snprintf(g_enable_path, sizeof(g_enable_path), "%s/%s/enable", g_chip_dir, kBuzzerPwmChannel);
        found = true;
        break;
    }
    closedir(dir);
    return found;
}

}  // namespace

void buzzer_init() {
    if (!find_buzzer_pwm()) {
        std::fprintf(stderr,
                     "rockbase-lcdui: buzzer pwm %s (%s) not found (is CONFIG_PWM_K230 built in?)\n",
                     kBuzzerPwmChannel, kBuzzerPwmNode);
        return;
    }

    // Export the channel so its pwm1 sysfs directory (period / duty_cycle /
    // enable) is created. If it is already exported this write fails harmlessly.
    sysfs_write(g_export_path, "1");

    // Configure a 1kHz, 50% duty waveform once. The k230 pwm driver keeps the
    // period/duty in the hardware; we only toggle enable to beep.
    const uint32_t period_ns = 1000000000U / kBuzzerFreqHz;  // 1,000,000 ns
    const uint32_t duty_ns = period_ns / 2U;                 // 500,000 ns
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u", period_ns);
    if (!sysfs_write(g_period_path, buf)) {
        return;
    }
    std::snprintf(buf, sizeof(buf), "%u", duty_ns);
    if (!sysfs_write(g_duty_path, buf)) {
        return;
    }
    // Start disabled.
    sysfs_write(g_enable_path, "0");
    g_pwm_ready = true;
    std::printf("rockbase-lcdui: buzzer=pwm %s (%s) freq=%uHz\n",
                kBuzzerPwmChannel, kBuzzerPwmNode, kBuzzerFreqHz);
}

void buzzer_beep() {
    if (!g_pwm_ready) {
        return;
    }
    const uint64_t now = now_ms();
    if (now - g_last_beep_ms < kBuzzerMinIntervalMs) {
        return;  // throttled
    }
    g_last_beep_ms = now;

    if (sysfs_write(g_enable_path, "1")) {
        g_beep_until_ms = now + kBuzzerBeepMs;
    }
}

void buzzer_update() {
    if (!g_pwm_ready || g_beep_until_ms == 0) {
        return;
    }
    if (now_ms() >= g_beep_until_ms) {
        sysfs_write(g_enable_path, "0");
        g_beep_until_ms = 0;
    }
}

void buzzer_shutdown() {
    if (g_pwm_ready) {
        sysfs_write(g_enable_path, "0");
        g_beep_until_ms = 0;
        g_pwm_ready = false;
    }
}

// Helper: sweep PWM frequency from f1 to f2 over ms milliseconds.
// The buzzer stays enabled throughout; volume naturally rises/falls with
// proximity to the 2kHz resonance point.
static void pwm_sweep(int f1, int f2, int ms) {
    sysfs_write(g_enable_path, "1");
    const int steps = ms / 5;
    for (int i = 0; i <= steps; i++) {
        const int f = f1 + (f2 - f1) * i / steps;
        const uint32_t period_ns = 1000000000U / static_cast<uint32_t>(f);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%u", period_ns);
        sysfs_write(g_period_path, buf);
        std::snprintf(buf, sizeof(buf), "%u", period_ns / 2);
        sysfs_write(g_duty_path, buf);
        usleep(5000);
    }
    sysfs_write(g_enable_path, "0");
}

void buzzer_boot_hologram() {
    if (!g_pwm_ready) return;
    // Hologram: sweep up 1k→2.8k (180ms), gap 30ms, sweep down 2.8k→2k (100ms).
    pwm_sweep(1000, 2800, 180);
    usleep(30000);
    pwm_sweep(2800, 2000, 100);
    // Restore 1kHz / 50% duty for normal click beeps.
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u", 1000000U);
    sysfs_write(g_period_path, buf);
    std::snprintf(buf, sizeof(buf), "%u", 500000U);
    sysfs_write(g_duty_path, buf);
}
