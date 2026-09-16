#pragma once

// PWM-driven passive buzzer on GPIO43 (IO43, alt2 = PWM1).
//
// Plan B (userspace bit-bang): the buzzer is driven from userspace by
// toggling the GPIO with a gpio_v2 handle plus a microsecond-accurate delay,
// the same pattern the backlight fade already uses. This needs no kernel or
// DTS change, so it can be hot-deployed and validated immediately.
//
// Once the kernel PWM subsystem is enabled (Plan A), only this module's
// internals change to write the PWM sysfs nodes (period / duty_cycle /
// enable); the public API below stays identical, so the call sites in
// main.cpp and modal_overlay.cpp are untouched.
//
// A short beep is emitted on user "clicks": any button inside a modal
// (bubbled up to the modal content) and any main-page config hotspot.
// Swipes and empty taps do not beep.

void buzzer_init();
void buzzer_beep();
void buzzer_shutdown();
