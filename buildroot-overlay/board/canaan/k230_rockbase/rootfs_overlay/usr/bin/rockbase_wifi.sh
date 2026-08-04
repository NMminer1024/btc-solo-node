#!/bin/sh

WLAN_DEV=${WLAN_DEV:-wlan0}
WIFI_CONFIG_FILE=${WIFI_CONFIG_FILE:-/etc/rockbase-wifi.conf}
WIFI_WPA_CONF=${WIFI_WPA_CONF:-/tmp/rockbase-wpa_supplicant.conf}
WIFI_STATE_FILE=${WIFI_STATE_FILE:-/tmp/rockbase-wifi-state.conf}
WIFI_SCAN_CACHE=${WIFI_SCAN_CACHE:-/tmp/rockbase-wifi-scan.cache}
WIFI_SCAN_CACHE_TTL=${WIFI_SCAN_CACHE_TTL:-15}

wifi_shell_quote() {
  printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

wifi_json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\r//g'
}

wifi_json_str() {
  printf '"%s"' "$(wifi_json_escape "$1")"
}

wifi_write_state() {
  state=${1:-idle}
  message=${2:-}
  tmp=$(mktemp "${TMPDIR:-/tmp}/rockbase-wifi-state.XXXXXX") || return 1
  {
    echo "# Rockbase Wi-Fi apply state"
    printf 'WIFI_APPLY_STATE=%s\n' "$(wifi_shell_quote "$state")"
    printf 'WIFI_APPLY_MESSAGE=%s\n' "$(wifi_shell_quote "$message")"
    printf 'WIFI_APPLY_TS=%s\n' "$(wifi_shell_quote "$(date -u '+%Y-%m-%d %H:%M:%S UTC' 2>/dev/null)")"
  } >"$tmp" || {
    rm -f "$tmp"
    return 1
  }
  chmod 600 "$tmp" 2>/dev/null || true
  mv -f "$tmp" "$WIFI_STATE_FILE"
}

wifi_load_state() {
  WIFI_APPLY_STATE="idle"
  WIFI_APPLY_MESSAGE=""
  WIFI_APPLY_TS=""
  [ -f "$WIFI_STATE_FILE" ] && . "$WIFI_STATE_FILE"
  [ -n "$WIFI_APPLY_STATE" ] || WIFI_APPLY_STATE="idle"
}

wifi_load_config() {
  WIFI_CONFIG_LOADED=0
  WIFI_CONFIG_SOURCE="default"
  WIFI_SSID=""
  WIFI_PASS=""
  WIFI_HIDDEN="0"

  if [ -f "$WIFI_CONFIG_FILE" ]; then
    . "$WIFI_CONFIG_FILE"
    WIFI_CONFIG_LOADED=1
    WIFI_CONFIG_SOURCE="$WIFI_CONFIG_FILE"
  fi

  if [ "$WIFI_CONFIG_LOADED" = "0" ]; then
    WIFI_SSID="NMTech-2.4G"
    WIFI_PASS="NMMiner2048"
    WIFI_HIDDEN="0"
  fi

  [ -n "$WIFI_SSID" ] || WIFI_SSID="NMTech-2.4G"
  [ -n "$WIFI_HIDDEN" ] || WIFI_HIDDEN="0"
}

wifi_save_config() {
  ssid=$1
  pass=$2
  hidden=${3:-0}
  tmp=$(mktemp "${TMPDIR:-/tmp}/rockbase-wifi.XXXXXX") || return 1
  {
    echo "# Rockbase Wi-Fi profile"
    printf 'WIFI_SSID=%s\n' "$(wifi_shell_quote "$ssid")"
    printf 'WIFI_PASS=%s\n' "$(wifi_shell_quote "$pass")"
    printf 'WIFI_HIDDEN=%s\n' "$(wifi_shell_quote "$hidden")"
  } >"$tmp" || {
    rm -f "$tmp"
    return 1
  }
  chmod 600 "$tmp" 2>/dev/null || true
  mkdir -p "$(dirname "$WIFI_CONFIG_FILE")" 2>/dev/null || true
  mv -f "$tmp" "$WIFI_CONFIG_FILE"
}

wifi_detect_ip() {
  ip_addr="-"
  if command -v ip >/dev/null 2>&1; then
    ip_addr=$(ip addr show "$WLAN_DEV" 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1 | head -n1)
  fi
  [ -n "$ip_addr" ] || ip_addr="-"
  printf '%s' "$ip_addr"
}

wifi_current_status() {
  WIFI_CONNECTED_STATE="disconnected"
  WIFI_CONNECTED_SSID="-"
  WIFI_CONNECTED_IP="-"
  WIFI_CONNECTED_BSSID="-"

  if command -v wpa_cli >/dev/null 2>&1; then
    status=$(wpa_cli -i "$WLAN_DEV" status 2>/dev/null)
    [ -n "$status" ] || status=""
    value=$(printf '%s\n' "$status" | awk -F= '/^wpa_state=/{print $2; exit}')
    [ -n "$value" ] && WIFI_CONNECTED_STATE="$value"
    value=$(printf '%s\n' "$status" | awk -F= '/^ssid=/{print $2; exit}')
    [ -n "$value" ] && WIFI_CONNECTED_SSID="$value"
    value=$(printf '%s\n' "$status" | awk -F= '/^ip_address=/{print $2; exit}')
    [ -n "$value" ] && WIFI_CONNECTED_IP="$value"
    value=$(printf '%s\n' "$status" | awk -F= '/^bssid=/{print $2; exit}')
    [ -n "$value" ] && WIFI_CONNECTED_BSSID="$value"
  fi

  if [ "$WIFI_CONNECTED_SSID" = "-" ] && command -v iwconfig >/dev/null 2>&1; then
    value=$(iwconfig "$WLAN_DEV" 2>/dev/null | grep -o 'ESSID:"[^"]*"' | cut -d'"' -f2 | head -n1)
    [ -n "$value" ] && WIFI_CONNECTED_SSID="$value"
  fi

  [ -n "$WIFI_CONNECTED_IP" ] || WIFI_CONNECTED_IP=$(wifi_detect_ip)
}

wifi_scan_cache_valid() {
  [ -f "$WIFI_SCAN_CACHE" ] || return 1
  now=$(date +%s 2>/dev/null || echo 0)
  mtime=$(stat -c %Y "$WIFI_SCAN_CACHE" 2>/dev/null || echo 0)
  [ $((now - mtime)) -lt "$WIFI_SCAN_CACHE_TTL" ]
}

wifi_scan_raw() {
  if wifi_scan_cache_valid; then
    cat "$WIFI_SCAN_CACHE"
    return 0
  fi

  command -v wpa_cli >/dev/null 2>&1 || return 1
  wpa_cli -i "$WLAN_DEV" scan >/dev/null 2>&1 || return 1

  results=""
  i=0
  while [ "$i" -lt 6 ]; do
    sleep 1
    results=$(wpa_cli -i "$WLAN_DEV" scan_results 2>/dev/null || true)
    printf '%s\n' "$results" | grep -q '^bssid' && break
    i=$((i + 1))
  done

  [ -n "$results" ] || return 1
  printf '%s\n' "$results" >"$WIFI_SCAN_CACHE" 2>/dev/null || true
  printf '%s\n' "$results"
}

wifi_scan_json() {
  raw=$(wifi_scan_raw) || {
    printf '{"ok":false,"error":"scan unavailable","scan":[]}'
    return 1
  }

  printf '%s\n' "$raw" | awk -F '\t' '
    function esc(s) {
      gsub(/\\/,"\\\\",s)
      gsub(/"/,"\\\"",s)
      return s
    }
    NR > 1 {
      ssid = $5
      if (ssid == "") next
      signal = $3 + 0
      if (!(ssid in seen) || signal > best[ssid]) {
        seen[ssid] = 1
        best[ssid] = signal
        freq[ssid] = $2 + 0
        bssid[ssid] = $1
        flags[ssid] = $4
      }
    }
    END {
      n = 0
      for (s in seen) {
        n++
        keys[n] = s
      }
      for (i = 1; i <= n; i++) {
        for (j = i + 1; j <= n; j++) {
          if (best[keys[j]] > best[keys[i]]) {
            tmp = keys[i]; keys[i] = keys[j]; keys[j] = tmp
          }
        }
      }
      printf "{\"ok\":true,\"scan\":["
      sep = ""
      for (i = 1; i <= n; i++) {
        s = keys[i]
        printf "%s{\"ssid\":\"%s\",\"bssid\":\"%s\",\"frequency\":%s,\"signal\":%s,\"security\":\"%s\"}", sep, esc(s), esc(bssid[s]), freq[s], best[s], esc(flags[s])
        sep = ","
      }
      printf "]}"
    }'
}

wifi_get_json() {
  wifi_load_config
  wifi_load_state
  wifi_current_status

  printf '{'
  printf '"ok":true,'
  printf '"config_file":%s,' "$(wifi_json_str "$WIFI_CONFIG_FILE")"
  printf '"saved_ssid":%s,' "$(wifi_json_str "$WIFI_SSID")"
  printf '"saved_hidden":%s,' "$(wifi_json_str "$WIFI_HIDDEN")"
  printf '"current_state":%s,' "$(wifi_json_str "$WIFI_CONNECTED_STATE")"
  printf '"current_ssid":%s,' "$(wifi_json_str "$WIFI_CONNECTED_SSID")"
  printf '"current_ip":%s,' "$(wifi_json_str "$WIFI_CONNECTED_IP")"
  printf '"current_bssid":%s,' "$(wifi_json_str "$WIFI_CONNECTED_BSSID")"
  printf '"iface":%s,' "$(wifi_json_str "$WLAN_DEV")"
  printf '"apply_state":%s,' "$(wifi_json_str "$WIFI_APPLY_STATE")"
  printf '"apply_message":%s,' "$(wifi_json_str "$WIFI_APPLY_MESSAGE")"
  printf '"apply_ts":%s,' "$(wifi_json_str "$WIFI_APPLY_TS")"
  printf '"scan_supported":%s' "$(command -v wpa_cli >/dev/null 2>&1 && printf true || printf false)"
  printf '}'
}

wifi_ensure_device() {
  if ! lsmod 2>/dev/null | grep -q '^8189fs'; then
    modprobe 8189fs 2>/dev/null || true
    sleep 2
  fi

  i=0
  while [ "$i" -lt 10 ]; do
    [ -e "/sys/class/net/${WLAN_DEV}" ] && return 0
    sleep 1
    i=$((i + 1))
  done

  return 1
}

wifi_write_wpa_conf() {
  wifi_load_config
  mkdir -p "$(dirname "$WIFI_WPA_CONF")" 2>/dev/null || true

  if [ -n "$WIFI_PASS" ]; then
    wpa_passphrase "$WIFI_SSID" "$WIFI_PASS" 2>/dev/null | sed 's/#psk.*/scan_ssid=1/g' >"$WIFI_WPA_CONF" || return 1
  else
    cat >"$WIFI_WPA_CONF" <<EOF
ctrl_interface=/var/run/wpa_supplicant
ap_scan=1

network={
        ssid="$WIFI_SSID"
        key_mgmt=NONE
        scan_ssid=1
}
EOF
  fi

  chmod 600 "$WIFI_WPA_CONF" 2>/dev/null || true
}

wifi_start_service() {
  wifi_ensure_device || return 1

  iw dev "$WLAN_DEV" set power_save off 2>/dev/null || true
  iwconfig "$WLAN_DEV" power off 2>/dev/null || true

  wifi_write_wpa_conf || return 1
  printf '[rockbase_wifi] trying SSID "%s" on %s (source=%s)\n' "$WIFI_SSID" "$WLAN_DEV" "$WIFI_CONFIG_SOURCE"

  killall wpa_supplicant 2>/dev/null || true
  sleep 1
  wpa_supplicant -D nl80211 -i "$WLAN_DEV" -c "$WIFI_WPA_CONF" -B >/dev/null 2>&1 || return 1
  sleep 2

  if command -v udhcpc >/dev/null 2>&1; then
    killall udhcpc 2>/dev/null || true
    udhcpc -i "$WLAN_DEV" -q -n -t 8 >/dev/null 2>&1 || true
  fi

  wifi_current_status
  if [ "$WIFI_CONNECTED_STATE" = "COMPLETED" ] || [ "$WIFI_CONNECTED_IP" != "-" ]; then
    printf '[rockbase_wifi] connected SSID "%s", IP %s\n' "$WIFI_CONNECTED_SSID" "$WIFI_CONNECTED_IP"
    return 0
  fi

  printf '[rockbase_wifi] connect failed: state=%s ssid="%s" ip=%s\n' "$WIFI_CONNECTED_STATE" "$WIFI_CONNECTED_SSID" "$WIFI_CONNECTED_IP"
  return 1
}

wifi_stop_service() {
  killall udhcpc 2>/dev/null || true
  killall wpa_supplicant 2>/dev/null || true
  ifconfig "$WLAN_DEV" down 2>/dev/null || true
  return 0
}

wifi_restart_service() {
  wifi_stop_service
  sleep 1
  wifi_start_service
}

wifi_apply_worker() {
  next_ssid=$1
  next_pass=$2
  next_hidden=$3
  backup=$(mktemp "${TMPDIR:-/tmp}/rockbase-wifi-backup.XXXXXX") || return 1

  if [ -f "$WIFI_CONFIG_FILE" ]; then
    cp -f "$WIFI_CONFIG_FILE" "$backup" || {
      rm -f "$backup"
      return 1
    }
  else
    : >"$backup"
  fi

  wifi_write_state "applying" "Applying Wi-Fi profile"

  WIFI_SSID=$next_ssid
  WIFI_PASS=$next_pass
  WIFI_HIDDEN=$next_hidden
  if ! wifi_save_config "$WIFI_SSID" "$WIFI_PASS" "$WIFI_HIDDEN"; then
    wifi_write_state "error" "Failed to save Wi-Fi profile"
    rm -f "$backup"
    return 1
  fi

  if wifi_restart_service; then
    wifi_write_state "ready" "Connected to $WIFI_SSID"
    rm -f "$backup"
    return 0
  fi

  if [ -s "$backup" ]; then
    . "$backup"
    wifi_save_config "$WIFI_SSID" "$WIFI_PASS" "$WIFI_HIDDEN" || true
    wifi_restart_service >/dev/null 2>&1 || true
  fi
  wifi_write_state "error" "Unable to connect to $next_ssid"
  rm -f "$backup"
  return 1
}

wifi_apply_async() {
  next_ssid=$1
  next_pass=$2
  next_hidden=${3:-0}
  (
    wifi_apply_worker "$next_ssid" "$next_pass" "$next_hidden"
  ) >/dev/null 2>&1 &
  printf '%s\n' $!
}

case "$(basename "$0")" in
  rockbase_wifi.sh)
    case "$1" in
      start) wifi_start_service ;;
      stop) wifi_stop_service ;;
      restart|reload) wifi_restart_service ;;
      scan) wifi_scan_json ;;
      status|get) wifi_get_json ;;
      apply)
        shift
        wifi_apply_async "$@"
        ;;
    esac
    ;;
esac
