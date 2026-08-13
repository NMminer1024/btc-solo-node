#!/bin/sh

USB_WIFI_MAP=${USB_WIFI_MAP:-/etc/rockbase-usb-wifi.map}
USB_WIFI_LOG=${USB_WIFI_LOG:-/tmp/rockbase-usb-wifi.log}
USB_WIFI_WAIT_SEC=${USB_WIFI_WAIT_SEC:-8}

usb_wifi_log() {
  line="[rockbase_usb_wifi] $*"
  printf '%s\n' "$line" >>"$USB_WIFI_LOG" 2>/dev/null || true
  printf '%s\n' "$line" >/dev/console 2>/dev/null || true
}

usb_wifi_norm_vidpid() {
  printf '%s' "$1" | tr '[:lower:]' '[:upper:]'
}

usb_wifi_is_usb_net_iface() {
  iface=$1
  [ -n "$iface" ] || return 1
  [ -e "/sys/class/net/$iface" ] || return 1
  dev_path=$(readlink -f "/sys/class/net/$iface/device" 2>/dev/null || true)
  case "$dev_path" in
    *"/usb"*|*"/usb"/*) return 0 ;;
  esac
  return 1
}

usb_wifi_pick_iface() {
  want=$1
  if [ -n "$want" ] && [ "$want" != "auto" ] && usb_wifi_is_usb_net_iface "$want"; then
    printf '%s\n' "$want"
    return 0
  fi

  for p in /sys/class/net/wlan*; do
    [ -e "$p" ] || continue
    i=$(basename "$p")
    if usb_wifi_is_usb_net_iface "$i"; then
      printf '%s\n' "$i"
      return 0
    fi
  done

  return 1
}

usb_wifi_list_usb_vidpid() {
  for d in /sys/bus/usb/devices/*; do
    [ -f "$d/idVendor" ] || continue
    v=$(cat "$d/idVendor" 2>/dev/null)
    p=$(cat "$d/idProduct" 2>/dev/null)
    [ -n "$v" ] && [ -n "$p" ] || continue
    printf '%s:%s\n' "$(printf '%s' "$v" | tr '[:lower:]' '[:upper:]')" "$(printf '%s' "$p" | tr '[:lower:]' '[:upper:]')"
  done
}

usb_wifi_match_map() {
  [ -f "$USB_WIFI_MAP" ] || return 1

  usb_list="$(usb_wifi_list_usb_vidpid)"
  [ -n "$usb_list" ] || return 1

  while IFS='|' read -r vidpid chip modules ifname note; do
    case "$vidpid" in
      ''|'#'*) continue ;;
    esac

    key=$(usb_wifi_norm_vidpid "$vidpid")
    if printf '%s\n' "$usb_list" | grep -qx "$key"; then
      printf '%s|%s|%s|%s|%s\n' "$key" "$chip" "$modules" "$ifname" "$note"
      return 0
    fi
  done <"$USB_WIFI_MAP"

  return 1
}

usb_wifi_load_modules() {
  mods=$1
  for m in $mods; do
    modprobe "$m" 2>/dev/null || true
  done
}

usb_wifi_detect() {
  autoload=$1

  match=$(usb_wifi_match_map) || return 1
  vidpid=$(printf '%s' "$match" | cut -d'|' -f1)
  chip=$(printf '%s' "$match" | cut -d'|' -f2)
  mods=$(printf '%s' "$match" | cut -d'|' -f3)
  ifname=$(printf '%s' "$match" | cut -d'|' -f4)

  if [ "$autoload" = "--autoload" ] && [ -n "$mods" ]; then
    usb_wifi_log "match $vidpid chip=$chip modules=[$mods], loading"
    usb_wifi_load_modules "$mods"
  fi

  t=0
  while [ "$t" -lt "$USB_WIFI_WAIT_SEC" ]; do
    iface=$(usb_wifi_pick_iface "$ifname" 2>/dev/null || true)
    if [ -n "$iface" ]; then
      usb_wifi_log "ready iface=$iface chip=$chip vidpid=$vidpid"
      printf '%s\n' "$iface"
      return 0
    fi
    sleep 1
    t=$((t + 1))
  done

  usb_wifi_log "matched $vidpid but no usb wlan iface appeared"
  return 1
}

case "$1" in
  detect)
    usb_wifi_detect "$2"
    ;;
  list)
    usb_wifi_match_map
    ;;
  *)
    echo "Usage: $0 {detect [--autoload]|list}" >&2
    exit 1
    ;;
esac
