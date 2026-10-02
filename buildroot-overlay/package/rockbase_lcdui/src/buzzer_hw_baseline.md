# Buzzer HW Baseline — Rockbase K230

## Device

- **Type**: Passive buzzer (无源蜂鸣器)
- **GPIO**: IO43 (alt2 → PWM1, channel 1 of `pwm0_2` node)
- **Driver**: Kernel PWM sysfs (`/sys/class/pwm/pwmchip0/pwm1/`)
- **Resonant frequency**: ~2 kHz (loudest/clearest)

## Frequency Response (measured 2026-10-02, 50% duty, 5s per point)

| Frequency | Audible? | Note |
|-----------|----------|------|
| 500 Hz    | ✗        | 听不清 |
| 1 kHz     | ✗        | 听不清 |
| 1.5 kHz   | ✗        | 听不清 |
| 1.8 kHz   | △        | 有声音，但不清晰 |
| **2 kHz** | ✓        | **最清晰，共振点** |
| 3 kHz     | ✓        | 清晰 |
| 4 kHz     | ✓        | 清晰 |
| 5 kHz     | ✓        | 有 |
| 6 kHz     | ✓        | 有 |

## Usable Band

**2 kHz – 6 kHz**

- Below 2 kHz: inaudible or very weak (below buzzer mechanical resonance)
- 2 kHz: resonant peak — maximum volume and clarity
- 2–6 kHz: usable range for rhythm/sweep patterns
- Above 6 kHz: untested, likely dropping off

## Design Constraints

- Single-tone device: pitch changes are subtle (volume varies, not pitch)
- Best use: **rhythm patterns** at fixed 2 kHz + **frequency sweeps** for
  dynamic "power-up" feel (volume naturally rises near 2 kHz resonance)
- Boot sound: `buzzer_boot_hologram()` — 1k→2.8k sweep up + 2.8k→2k sweep down (~310ms)
- Click feedback: fixed 1 kHz / 50% duty, 80ms beep (buzzer_init default)
