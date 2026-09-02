#!/bin/sh
#
# rockbase_netsel.sh — unified network path selector (phase 1: static boot probe)
#
# Part of the unified network image (docs/rockbase-net-unified-image-plan.md).
# The image ships BOTH SDIO WiFi (8189fs) and USB network drivers; this script
# decides at boot which path to use, in priority order:
#
#   1. usb_eth   — USB wired NIC (r8152/ax8817x/ax88179_178a/cdc_ether, built-in)
#   2. usb_wifi  — USB WiFi dongle (mt7601u/rtw88_8821cu, VID:PID map autoload)
#   3. sdio_wifi — on-board SDIO WiFi (8189fs) fallback
#
# The decision is written to $NETSEL_STATE_FILE (values: usb_eth / usb_wifi /
# sdio_wifi) for S38wifi / rockbase_wifi.sh to consume.
#
# Freeze mitigation (carried over from the old build-time behavior): when a USB
# path is selected, the SDIO WiFi controller is unbound at runtime so the
# 8189fs chip stops generating spurious SDIO interrupts. When sdio_wifi is
# selected the controller stays bound.
#
# Detection is EXACT (interface type + VID:PID whitelist), never "any USB
# device present" — a USB mass-storage stick must NOT trigger USB mode.
#
# Usage:
#   rockbase_netsel.sh            # run detection, write state, apply actions
#   rockbase_netsel.sh --dry-run  # detect + log only, no modprobe/unbind
#

NETSEL_STATE_FILE=${NETSEL_STATE_FILE:-/tmp/rockbase-net-mode}
NETSEL_LOG=${NETSEL_LOG:-/tmp/rockbase-netsel.log}
NETSEL_USB_WIFI_HELPER=${NETSEL_USB_WIFI_HELPER:-/usr/bin/rockbase_usb_wifi.sh}
# Short poll window: USB enumeration may lag behind userspace startup.
NETSEL_USB_WAIT_SEC=${NETSEL_USB_WAIT_SEC:-5}
# SDIO WiFi controller platform device. All Rockbase boards host the 8189fs
# on sdhci0 (91580000.sdhci0/mmc0); the device is still resolved dynamically
# by scanning for the SDIO WiFi function (vendor 0x024c = Realtek) so the
# script stays robust across board revisions. NETSEL_SDHCI_DEV can force a
# specific device for testing.
NETSEL_SDHCI_DEV=${NETSEL_SDHCI_DEV:-}
NETSEL_SDHCI_DRIVER=${NETSEL_SDHCI_DRIVER:-sdhci-dwcmshc-kendryte}
NETSEL_SDIO_WIFI_VENDOR=${NETSEL_SDIO_WIFI_VENDOR:-024c}

netsel_log() {
  line="[rockbase_netsel] $*"
  printf '%s\n' "$line" >>"$NETSEL_LOG" 2>/dev/null || true
  printf '%s\n' "$line" >/dev/console 2>/dev/null || true
}

netsel_write_state() {
  mode=$1
  iface=$2
  tmp=$(mktemp "${TMPDIR:-/tmp}/rockbase-net-mode.XXXXXX" 2>/dev/null) || return 1
  {
    echo "# Rockbase network path selection (rockbase_netsel.sh)"
    printf 'NET_MODE=%s\n' "$mode"
    printf 'NET_IFACE=%s\n' "$iface"
    printf 'NET_SEL_TS=%s\n' "$(date -u '+%Y-%m-%d %H:%M:%S UTC' 2>/dev/null)"
  } >"$tmp" || {
    rm -f "$tmp"
    return 1
  }
  chmod 644 "$tmp" 2>/dev/null || true
  mv -f "$tmp" "$NETSEL_STATE_FILE"
}

# True if $1 is a USB-attached network interface (device path under /usb/).
netsel_is_usb_net() {
  iface=$1
  [ -n "$iface" ] || return 1
  [ -e "/sys/class/net/$iface" ] || return 1
  dev_path=$(readlink -f "/sys/class/net/$iface/device" 2>/dev/null || true)
  case "$dev_path" in
    *"/usb"*|*"/usb"/*) return 0 ;;
  esac
  return 1
}

# True if $1 is a wireless (wlan) interface.
netsel_is_wireless() {
  iface=$1
  [ -n "$iface" ] || return 1
  [ -e "/sys/class/net/$iface" ] || return 1
  if [ -e "/sys/class/net/$iface/wireless" ]; then
    return 0
  fi
  case "$iface" in
    wlan*) return 0 ;;
  esac
  return 1
}

# Detect a USB wired NIC: USB-attached, non-wireless interface.
# Prints the interface name.
netsel_detect_usb_eth() {
  for p in /sys/class/net/*; do
    [ -e "$p" ] || continue
    i=$(basename "$p")
    case "$i" in
      lo) continue ;;
    esac
    netsel_is_usb_net "$i" || continue
    netsel_is_wireless "$i" && continue
    printf '%s\n' "$i"
    return 0
  done
  return 1
}

# Detect a USB WiFi dongle via the VID:PID map helper (autoloads the matching
# module and polls for the wlan interface). Prints the interface name.
netsel_detect_usb_wifi() {
  [ -x "$NETSEL_USB_WIFI_HELPER" ] || return 1
  iface=$("$NETSEL_USB_WIFI_HELPER" detect --autoload 2>/dev/null | head -n1)
  if [ -n "$iface" ] && [ -e "/sys/class/net/$iface" ]; then
    printf '%s\n' "$iface"
    return 0
  fi
  return 1
}

# Poll for a USB network path (wired first, then WiFi dongle) so late USB
# enumeration is not misread as "no USB device". Prints "usb_eth <iface>" or
# "usb_wifi <iface>".
#
# Timing: the wired check is fast (built-in driver) and is polled every second
# for NETSEL_USB_WAIT_SEC. The WiFi helper (rockbase_usb_wifi.sh detect
# --autoload) has its OWN internal USB_WIFI_WAIT_SEC poll for the wlan iface,
# so it is invoked only ONCE (on the last iteration) to avoid multiplying that
# wait across the loop — worst case is ~NETSEL_USB_WAIT_SEC + USB_WIFI_WAIT_SEC
# instead of NETSEL_USB_WAIT_SEC * USB_WIFI_WAIT_SEC.
netsel_detect_usb() {
  t=0
  while [ "$t" -lt "$NETSEL_USB_WAIT_SEC" ]; do
    iface=$(netsel_detect_usb_eth 2>/dev/null || true)
    if [ -n "$iface" ]; then
      printf 'usb_eth %s\n' "$iface"
      return 0
    fi
    if [ "$t" -eq $((NETSEL_USB_WAIT_SEC - 1)) ]; then
      iface=$(netsel_detect_usb_wifi 2>/dev/null || true)
      if [ -n "$iface" ]; then
        printf 'usb_wifi %s\n' "$iface"
        return 0
      fi
    fi
    sleep 1
    t=$((t + 1))
  done
  return 1
}

# Find the sdhci platform device that hosts the SDIO WiFi card
# (Realtek, vendor 0x024c). Scans the SDIO card nodes (mmcX:0001) whose
# vendor attribute is available even before 8189fs is loaded. Prints the
# platform device name (e.g. 91580000.sdhci0). Honors NETSEL_SDHCI_DEV
# override.
netsel_find_sdio_wifi_sdhci() {
  if [ -n "$NETSEL_SDHCI_DEV" ]; then
    printf '%s\n' "$NETSEL_SDHCI_DEV"
    return 0
  fi
  for m in /sys/bus/mmc/devices/mmc*:0001; do
    [ -e "$m" ] || continue
    v=$(cat "$m/vendor" 2>/dev/null | sed 's/^0x//' | tr '[:upper:]' '[:lower:]' || true)
    case "$v" in
      "$NETSEL_SDIO_WIFI_VENDOR"|"$NETSEL_SDIO_WIFI_VENDOR"*)
        sdhci=$(readlink -f "$m" 2>/dev/null | sed 's|.*/platform/||;s|^soc/||;s|/mmc.*||')
        if [ -n "$sdhci" ]; then
          printf '%s\n' "$sdhci"
          return 0
        fi
        ;;
    esac
  done
  return 1
}

# Unbind the SDIO WiFi controller (freeze mitigation when a USB path is used).
#
# SAFETY (hard-won, see docs/rockbase-freeze-milestones.md):
# - NEVER rmmod 8189fs at runtime. Unloading the driver while its RTW
#   threads / SDIO host claim are active is the historical freeze suspect
#   path, and on a board reached over that very WiFi it also kills the
#   management link.
# - Only unbind the controller when 8189fs is NOT loaded. In the unified
#   image this is the normal boot case: S37netsel runs before S38wifi and
#   8189fs is no longer modprobed unconditionally at boot, so on the USB
#   path the controller is unbound with the driver never loaded — exactly
#   the safe state the old build-time unbind produced.
# - If 8189fs IS loaded (e.g. hot-plug scenario, phase 2), skip the unbind
#   and log a warning instead of risking a freeze.
netsel_unbind_sdio() {
  if lsmod 2>/dev/null | grep -q '^8189fs'; then
    netsel_log "sdio unbind: 8189fs already loaded, SKIP (runtime rmmod is unsafe; hot-plug reselection is phase 2)"
    return 0
  fi
  sdhci=$(netsel_find_sdio_wifi_sdhci 2>/dev/null || true)
  if [ -z "$sdhci" ]; then
    netsel_log "sdio unbind: no SDIO WiFi controller found, skip"
    return 0
  fi
  drv_dir="/sys/bus/platform/drivers/$NETSEL_SDHCI_DRIVER"
  if [ ! -d "$drv_dir" ]; then
    netsel_log "sdio unbind: driver dir $drv_dir not present, skip"
    return 0
  fi
  if [ ! -e "$drv_dir/$sdhci" ]; then
    netsel_log "sdio unbind: $sdhci not bound to $NETSEL_SDHCI_DRIVER, skip"
    return 0
  fi
  if echo "$sdhci" > "$drv_dir/unbind" 2>/dev/null; then
    netsel_log "sdio unbind: $sdhci unbound (8189fs not loaded)"
  else
    netsel_log "sdio unbind: FAILED to unbind $sdhci"
  fi
  return 0
}

netsel_main() {
  dry_run=$1
  mode=""
  iface=""

  usb=$(netsel_detect_usb 2>/dev/null || true)
  if [ -n "$usb" ]; then
    mode=$(printf '%s' "$usb" | cut -d' ' -f1)
    iface=$(printf '%s' "$usb" | cut -d' ' -f2)
  else
    mode="sdio_wifi"
    iface="wlan0"
  fi

  netsel_log "decision: mode=$mode iface=$iface dry_run=$dry_run"
  netsel_write_state "$mode" "$iface"

  if [ "$dry_run" = "--dry-run" ]; then
    netsel_log "dry-run: no modprobe/unbind actions taken"
    return 0
  fi

  case "$mode" in
    usb_eth|usb_wifi)
      # USB path wins: keep 8189fs unloaded and silence the SDIO controller.
      netsel_unbind_sdio
      ;;
    sdio_wifi)
      # SDIO fallback: make sure the controller is bound, then load 8189fs.
      sdhci=$(netsel_find_sdio_wifi_sdhci 2>/dev/null || true)
      drv_dir="/sys/bus/platform/drivers/$NETSEL_SDHCI_DRIVER"
      if [ -n "$sdhci" ] && [ -d "$drv_dir" ] && [ ! -e "$drv_dir/$sdhci" ]; then
        netsel_log "sdio bind: re-binding $sdhci"
        echo "$sdhci" > "$drv_dir/bind" 2>/dev/null || true
        sleep 1
      fi
      if ! lsmod 2>/dev/null | grep -q '^8189fs'; then
        netsel_log "loading 8189fs (sdio_wifi)"
        modprobe 8189fs 2>/dev/null || true
        sleep 2
      fi
      ;;
  esac

  return 0
}

case "${1:-}" in
  --dry-run)
    netsel_main --dry-run
    ;;
  *)
    netsel_main
    ;;
esac
