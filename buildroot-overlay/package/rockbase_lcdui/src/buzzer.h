#pragma once

// PWM-driven passive buzzer on IO43 (alt2 = PWM1, channel 1 of the pwm0_2
// node, compatible "canaan,k230-pwm").
//
// Plan A (kernel PWM): the buzzer is driven through the PWM sysfs interface.
// The kernel PWM subsystem (CONFIG_PWM + CONFIG_PWM_K230) and the DTS
// (&pwm0_2 + buzzer_pwm1_pins iomux) are required. This module configures a
// 1kHz / 50% duty waveform once at init, then toggles the channel's
// enable node to beep. No userspace bit-banging or busy-wait delay.
//
// A short beep is emitted on user "clicks": any button inside a modal
// (bubbled up to the modal content) and any main-page config hotspot.
// Swipes and empty taps do not beep.

void buzzer_init();
void buzzer_beep();
void buzzer_update();
void buzzer_shutdown();

// Boot sound: ascending 4-tick "connect" sequence (2k→3k→4k→6k), ~500ms.
// Must be called from a separate thread (it blocks ~500ms via usleep).
// Restores the 1kHz/50% duty settings for normal click beeps on return.
void buzzer_boot_sound();
