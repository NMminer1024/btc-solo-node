#include "buzzer.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

namespace {

// IO43 lives on gpio1_ports (IO32-63), exposed as /dev/gpiochip1 with
// per-chip line numbering, so IO43 = line 11 (43 - 32). It is NOT on
// gpiochip0 (IO0-31), which is why a gpiochip0/line-43 request fails with
// EINVAL. (IO43 alt2 = PWM1, but Plan B bit-bangs it as a plain GPIO.)
constexpr const char* kBuzzerGpioChip = "/dev/gpiochip1";
constexpr uint32_t kBuzzerGpioLine = 11;  // IO43
constexpr uint32_t kBuzzerFreqHz = 1000;  // passive buzzer resonant tone
constexpr uint32_t kBuzzerBeepMs = 80;    // single click duration
constexpr uint32_t kBuzzerMinIntervalMs = 60;  // throttle: avoid a buzz on rapid taps

int g_buzzer_fd = -1;
uint64_t g_last_beep_ms = 0;

// Millisecond monotonic clock, matching the tick_ms() style used elsewhere.
uint64_t now_ms() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000U + static_cast<uint64_t>(ts.tv_nsec) / 1000000U;
}

// Microsecond-accurate delay. usleep() has ~1ms granularity on Linux, which is
// far too coarse for a ~185us half-period, so spin on CLOCK_MONOTONIC instead.
// A short beep only spins for a few tens of ms total, so the CPU cost is
// negligible.
void delay_us(uint32_t us) {
    struct timespec start {};
    clock_gettime(CLOCK_MONOTONIC, &start);
    const long target_ns = static_cast<long>(us) * 1000L;
    for (;;) {
        struct timespec now {};
        clock_gettime(CLOCK_MONOTONIC, &now);
        const long elapsed_ns =
            (now.tv_sec - start.tv_sec) * 1000000000L + (now.tv_nsec - start.tv_nsec);
        if (elapsed_ns >= target_ns) {
            break;
        }
    }
}

bool set_buzzer(int value) {
    if (g_buzzer_fd < 0) {
        return false;
    }
    gpio_v2_line_values values {};
    values.mask = 1;
    values.bits = value ? 1 : 0;
    if (ioctl(g_buzzer_fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &values) < 0) {
        std::fprintf(stderr, "rockbase-lcdui: buzzer gpio set failed: %s\n", std::strerror(errno));
        return false;
    }
    return true;
}

}  // namespace

void buzzer_init() {
    int chip_fd = open(kBuzzerGpioChip, O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0) {
        std::fprintf(stderr, "rockbase-lcdui: open %s failed: %s\n",
                     kBuzzerGpioChip, std::strerror(errno));
        return;
    }

    gpio_v2_line_request request {};
    request.offsets[0] = kBuzzerGpioLine;
    request.num_lines = 1;
    request.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
    request.config.num_attrs = 1;
    request.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    request.config.attrs[0].attr.values = 0;
    request.config.attrs[0].mask = 1;
    std::snprintf(request.consumer, sizeof(request.consumer), "rockbase-lcdui");

    if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &request) < 0) {
        std::fprintf(stderr, "rockbase-lcdui: request %s line %u failed: %s\n",
                     kBuzzerGpioChip, kBuzzerGpioLine, std::strerror(errno));
        close(chip_fd);
        return;
    }
    close(chip_fd);

    g_buzzer_fd = request.fd;
    std::printf("rockbase-lcdui: buzzer=%s line=%u freq=%uHz\n",
                kBuzzerGpioChip, kBuzzerGpioLine, kBuzzerFreqHz);
}

void buzzer_beep() {
    if (g_buzzer_fd < 0) {
        return;
    }
    const uint64_t now = now_ms();
    if (now - g_last_beep_ms < kBuzzerMinIntervalMs) {
        return;  // throttled
    }
    g_last_beep_ms = now;

    const uint32_t period_us = 1000000U / kBuzzerFreqHz;
    const uint32_t half_us = period_us / 2U;
    const uint32_t cycles = (kBuzzerBeepMs * 1000U) / period_us;
    for (uint32_t i = 0; i < cycles; ++i) {
        set_buzzer(1);
        delay_us(half_us);
        set_buzzer(0);
        delay_us(half_us);
    }
    set_buzzer(0);
}

void buzzer_shutdown() {
    if (g_buzzer_fd >= 0) {
        close(g_buzzer_fd);
        g_buzzer_fd = -1;
    }
}
