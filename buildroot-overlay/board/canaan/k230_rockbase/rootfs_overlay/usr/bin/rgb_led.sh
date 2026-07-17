#!/bin/sh
#
# rgb_led.sh - RGB LED control utility for boot status indication
#
# Pin mapping (IO low = LED ON, GPIO_ACTIVE_LOW):
#   Red:   /sys/class/leds/rgb:red/
#   Green: /sys/class/leds/rgb:green/
#   Blue:  /sys/class/leds/rgb:blue/
#
# Usage:
#   rgb_led.sh color <red|green|blue|yellow|cyan|magenta|white|off>
#   rgb_led.sh blink <color> <on_ms> <off_ms>
#   rgb_led.sh heartbeat <green|red>
#   rgb_led.sh phase <boot|init|ready|panic>
#

LED_BASE="/sys/class/leds"
LED_RED="${LED_BASE}/rgb:red"
LED_GREEN="${LED_BASE}/rgb:green"
LED_BLUE="${LED_BASE}/rgb:blue"

led_exists() {
	[ -d "${LED_RED}" ] && [ -d "${LED_GREEN}" ] && [ -d "${LED_BLUE}" ]
}

led_off_all() {
	echo 0 > "${LED_RED}/brightness" 2>/dev/null
	echo 0 > "${LED_GREEN}/brightness" 2>/dev/null
	echo 0 > "${LED_BLUE}/brightness" 2>/dev/null
	# Clear triggers
	echo none > "${LED_RED}/trigger" 2>/dev/null
	echo none > "${LED_GREEN}/trigger" 2>/dev/null
	echo none > "${LED_BLUE}/trigger" 2>/dev/null
}

led_set_trigger() {
	local led="$1"
	local trigger="$2"
	echo "$trigger" > "${led}/trigger" 2>/dev/null
}

led_set_brightness() {
	local led="$1"
	local val="$2"
	echo "$val" > "${led}/brightness" 2>/dev/null
}

# Set solid color
# With GPIO_ACTIVE_LOW: brightness 255 = IO LOW = LED ON
led_color() {
	case "$1" in
	red)
		led_off_all
		led_set_brightness "$LED_RED" 255
		;;
	green)
		led_off_all
		led_set_brightness "$LED_GREEN" 255
		;;
	blue)
		led_off_all
		led_set_brightness "$LED_BLUE" 255
		;;
	yellow)
		led_off_all
		led_set_brightness "$LED_RED" 255
		led_set_brightness "$LED_GREEN" 255
		;;
	cyan)
		led_off_all
		led_set_brightness "$LED_GREEN" 255
		led_set_brightness "$LED_BLUE" 255
		;;
	magenta)
		led_off_all
		led_set_brightness "$LED_RED" 255
		led_set_brightness "$LED_BLUE" 255
		;;
	white)
		led_off_all
		led_set_brightness "$LED_RED" 255
		led_set_brightness "$LED_GREEN" 255
		led_set_brightness "$LED_BLUE" 255
		;;
	off)
		led_off_all
		;;
	*)
		echo "Unknown color: $1" >&2
		return 1
		;;
	esac
}

# Blink one color with specific on/off times (ms)
led_blink() {
	local color="$1"
	local on_ms="$2"
	local off_ms="$3"
	local led

	case "$color" in
	red)    led="$LED_RED" ;;
	green)  led="$LED_GREEN" ;;
	blue)   led="$LED_BLUE" ;;
	*)
		echo "Unknown color: $color" >&2
		return 1
		;;
	esac

	# Turn off all LEDs first
	led_off_all

	led_set_trigger "$led" timer
	echo "$on_ms"  > "${led}/delay_on"  2>/dev/null
	echo "$off_ms" > "${led}/delay_off" 2>/dev/null
}

# Blink two colors simultaneously (e.g. yellow = R+G)
led_blink_dual() {
	local color1="$1"
	local color2="$2"
	local on_ms="$3"
	local off_ms="$4"
	local led1 led2

	case "$color1" in
	red)   led1="$LED_RED" ;;
	green) led1="$LED_GREEN" ;;
	blue)  led1="$LED_BLUE" ;;
	*)     echo "Unknown color: $color1" >&2; return 1 ;;
	esac
	case "$color2" in
	red)   led2="$LED_RED" ;;
	green) led2="$LED_GREEN" ;;
	blue)  led2="$LED_BLUE" ;;
	*)     echo "Unknown color: $color2" >&2; return 1 ;;
	esac

	led_off_all

	led_set_trigger "$led1" timer
	echo "$on_ms"  > "${led1}/delay_on"  2>/dev/null
	echo "$off_ms" > "${led1}/delay_off" 2>/dev/null

	led_set_trigger "$led2" timer
	echo "$on_ms"  > "${led2}/delay_on"  2>/dev/null
	echo "$off_ms" > "${led2}/delay_off" 2>/dev/null
}

# Heartbeat mode (double-pulse like real heartbeat)
led_heartbeat() {
	local color="$1"
	led_off_all
	case "$color" in
	red)   led_set_trigger "$LED_RED" heartbeat ;;
	green) led_set_trigger "$LED_GREEN" heartbeat ;;
	*)     led_set_trigger "$LED_RED" heartbeat
	       led_set_trigger "$LED_GREEN" heartbeat ;;
	esac
}

# ============================================================
# Phase transitions (industry-standard Linux boot light sequence)
# ============================================================

# Phase 1: All off (DTS has no default-state, LEDs off until userspace takes over)

# Phase 2: Early userspace init -> Blue blinking 500ms
led_phase_boot() {
	if ! led_exists; then
		return 1
	fi
	led_off_all
	echo timer > "${LED_BLUE}/trigger" 2>/dev/null
	echo 500 > "${LED_BLUE}/delay_on" 2>/dev/null
	echo 500 > "${LED_BLUE}/delay_off" 2>/dev/null
}

# Phase 3: Driver/service loading -> Cyan (G+B) blinking 200ms
led_phase_init() {
	if ! led_exists; then
		return 1
	fi
	led_blink_dual green blue 200 200
}

# Phase 4: System fully ready -> Green heartbeat
led_phase_ready() {
	if ! led_exists; then
		return 1
	fi
	led_heartbeat green
}

# Fault: Kernel panic -> Blue rapid blinking (handled by ledtrig-panic in kernel)
# Userspace error -> Blue fast blink 100ms
led_phase_panic() {
	if ! led_exists; then
		return 1
	fi
	led_blink blue 100 100
}

# ============================================================
# CLI dispatch
# ============================================================
case "$1" in
color)
	led_color "$2"
	;;
blink)
	led_blink "$2" "$3" "$4"
	;;
heartbeat)
	led_heartbeat "$2"
	;;
phase)
	case "$2" in
	boot)   led_phase_boot ;;
	init)   led_phase_init ;;
	ready)  led_phase_ready ;;
	panic)  led_phase_panic ;;
	*)      echo "Unknown phase: $2" >&2; exit 1 ;;
	esac
	;;
*)
	echo "Usage: $0 {color <c>|blink <c> <on_ms> <off_ms>|heartbeat <c>|phase <boot|init|ready|panic>}" >&2
	echo "  Colors: red, green, blue, yellow, cyan, magenta, white, off" >&2
	exit 1
	;;
esac
