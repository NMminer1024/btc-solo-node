#!/bin/sh

WLAN_DEV=${WLAN_DEV:-wlan0}
WIFI_CONFIG_FILE=${WIFI_CONFIG_FILE:-/root/config/rockbase-wifi.conf}
WIFI_WPA_CONF=${WIFI_WPA_CONF:-/tmp/rockbase-wpa_supplicant.conf}
WIFI_STATE_FILE=${WIFI_STATE_FILE:-/tmp/rockbase-wifi-state.conf}
WIFI_SCAN_CACHE=${WIFI_SCAN_CACHE:-/tmp/rockbase-wifi-scan.cache}
WIFI_SCAN_CACHE_TTL=${WIFI_SCAN_CACHE_TTL:-15}
WIFI_ASSOC_TIMEOUT=${WIFI_ASSOC_TIMEOUT:-25}
WIFI_DHCP_TRIES=${WIFI_DHCP_TRIES:-12}
WIFI_LATE_WATCH_TIMEOUT=${WIFI_LATE_WATCH_TIMEOUT:-90}
WIFI_DHCP_LOG=${WIFI_DHCP_LOG:-/tmp/rockbase-wifi-udhcpc.log}
USB_WIFI_HELPER=${USB_WIFI_HELPER:-/usr/bin/rockbase_usb_wifi.sh}
NETSEL_STATE_FILE=${NETSEL_STATE_FILE:-/tmp/rockbase-net-mode}
WIFI_WATCHDOG_ENABLE=${WIFI_WATCHDOG_ENABLE:-1}
WIFI_WATCHDOG_INTERVAL=${WIFI_WATCHDOG_INTERVAL:-20}
WIFI_WATCHDOG_FAIL_THRESHOLD=${WIFI_WATCHDOG_FAIL_THRESHOLD:-3}
WIFI_WATCHDOG_PIDFILE=${WIFI_WATCHDOG_PIDFILE:-/var/run/rockbase-wifi-watchdog.pid}
WIFI_APPLY_LOCK=${WIFI_APPLY_LOCK:-/tmp/rockbase-wifi-apply.lock}
WIFI_RTW_D_FILE=${WIFI_RTW_D_FILE:-/tmp/rockbase-wifi-rtw-d}

wifi_shell_quote() {
  printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

wifi_json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\r//g'
}

wifi_json_str() {
  printf '"%s"' "$(wifi_json_escape "$1")"
}

wifi_console_log() {
  line="[rockbase_wifi] $*"
  printf '%s\n' "$line" >>/tmp/rockbase-wifi.log 2>/dev/null || true
  printf '%s\n' "$line" >/dev/console 2>/dev/null || printf '%s\n' "$line"
}

# Read the unified network path decision written by rockbase_netsel.sh
# (init.d/S37netsel). Sets NET_MODE (usb_eth / usb_wifi / sdio_wifi) and,
# when available, NET_IFACE. Falls back to sdio_wifi when the state file is
# missing (e.g. netsel not yet deployed).
wifi_load_netsel() {
  NET_MODE="sdio_wifi"
  NET_IFACE=""
  if [ -f "$NETSEL_STATE_FILE" ]; then
    . "$NETSEL_STATE_FILE" 2>/dev/null || true
  fi
  case "$NET_MODE" in
    usb_eth|usb_wifi|sdio_wifi) ;;
    *) NET_MODE="sdio_wifi" ;;
  esac
}

wifi_select_iface() {
  wifi_load_netsel

  # usb_eth: no WiFi backend at all; S38wifi skips startup.
  if [ "$NET_MODE" = "usb_eth" ]; then
    return 1
  fi

  if [ -e "/sys/class/net/${WLAN_DEV}" ]; then
    return 0
  fi

  # usb_wifi: use the interface chosen by netsel (or re-detect via helper).
  if [ "$NET_MODE" = "usb_wifi" ]; then
    if [ -n "$NET_IFACE" ] && [ -e "/sys/class/net/${NET_IFACE}" ]; then
      WLAN_DEV="$NET_IFACE"
      wifi_console_log "netsel: using usb wifi iface=${WLAN_DEV}"
      return 0
    fi
    if [ -x "$USB_WIFI_HELPER" ]; then
      usb_iface=$($USB_WIFI_HELPER detect 2>/dev/null | head -n1)
      if [ -n "$usb_iface" ] && [ -e "/sys/class/net/${usb_iface}" ]; then
        wifi_console_log "usb wifi detected: iface=${usb_iface}"
        WLAN_DEV="$usb_iface"
        return 0
      fi
    fi
    return 1
  fi

  # sdio_wifi: default path (wlan0 via 8189fs).
  return 1
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
    WIFI_SSID="RockBase_2.4G"
    WIFI_PASS="NMMiner2048"
    WIFI_HIDDEN="0"
  fi

  [ -n "$WIFI_SSID" ] || WIFI_SSID="RockBase_2.4G"
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

  if [ "$WIFI_CONNECTED_IP" = "-" ]; then
    WIFI_CONNECTED_IP=$(wifi_detect_ip)
  fi
  if [ "$WIFI_CONNECTED_STATE" = "disconnected" ] && [ "$WIFI_CONNECTED_SSID" != "-" ] && [ "$WIFI_CONNECTED_IP" != "-" ]; then
    WIFI_CONNECTED_STATE="COMPLETED"
  fi
}

wifi_scan_cache_valid() {
  [ -f "$WIFI_SCAN_CACHE" ] || return 1
  now=$(date +%s 2>/dev/null || echo 0)
  mtime=$(stat -c %Y "$WIFI_SCAN_CACHE" 2>/dev/null || echo 0)
  [ $((now - mtime)) -lt "$WIFI_SCAN_CACHE_TTL" ]
}

wifi_scan_result_count() {
  printf '%s\n' "$1" | awk 'NR > 1 && NF >= 5 { c++ } END { print c + 0 }'
}

wifi_scan_raw() {
  if wifi_scan_cache_valid; then
    cached=$(cat "$WIFI_SCAN_CACHE" 2>/dev/null || true)
    cached_count=$(wifi_scan_result_count "$cached")
    if [ "$cached_count" -gt 1 ]; then
      printf '%s\n' "$cached"
      return 0
    fi
  fi

  command -v wpa_cli >/dev/null 2>&1 || return 1
  wpa_cli -i "$WLAN_DEV" scan >/dev/null 2>&1 || return 1

  results=""
  best_results=""
  best_count=0
  i=0
  while [ "$i" -lt 8 ]; do
    sleep 1
    results=$(wpa_cli -i "$WLAN_DEV" scan_results 2>/dev/null || true)
    count=$(wifi_scan_result_count "$results")
    if [ "$count" -gt "$best_count" ]; then
      best_results="$results"
      best_count="$count"
    fi
    if [ "$best_count" -gt 1 ] && [ "$count" -eq "$best_count" ]; then
      break
    fi
    i=$((i + 1))
  done

  [ -n "$best_results" ] || best_results="$results"
  [ -n "$best_results" ] || return 1
  printf '%s\n' "$best_results" >"$WIFI_SCAN_CACHE" 2>/dev/null || true
  printf '%s\n' "$best_results"
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
  wifi_load_netsel

  # usb_eth: no WiFi backend by design (USB wired NIC is the network path).
  if [ "$NET_MODE" = "usb_eth" ]; then
    return 2
  fi

  wifi_select_iface && return 0

  # sdio_wifi: load 8189fs (netsel already did this at boot; this is a
  # safety net for manual restarts after rmmod).
  if [ "$NET_MODE" = "sdio_wifi" ]; then
    if ! lsmod 2>/dev/null | grep -q '^8189fs'; then
      modprobe 8189fs 2>/dev/null || true
      sleep 2
    fi
    wifi_select_iface && return 0
  fi

  # sdio_wifi probe on this DMA baseline can take >12s from modprobe to
  # netdev registration; widen the wait so first boot doesn't skip start.
  i=0
  while [ "$i" -lt 30 ]; do
    [ -e "/sys/class/net/${WLAN_DEV}" ] && return 0
    # usb_wifi: re-detect via helper (modules already autoloaded by netsel).
    if [ "$NET_MODE" = "usb_wifi" ] && [ -x "$USB_WIFI_HELPER" ]; then
      usb_iface=$($USB_WIFI_HELPER detect 2>/dev/null | head -n1)
      if [ -n "$usb_iface" ] && [ -e "/sys/class/net/${usb_iface}" ]; then
        WLAN_DEV="$usb_iface"
        return 0
      fi
    fi
    sleep 1
    i=$((i + 1))
  done

  return 2
}

wifi_write_wpa_conf() {
  wifi_load_config
  mkdir -p "$(dirname "$WIFI_WPA_CONF")" 2>/dev/null || true

  if [ -n "$WIFI_PASS" ]; then
    {
      echo "ctrl_interface=/var/run/wpa_supplicant"
      echo "ap_scan=1"
      echo
      wpa_passphrase "$WIFI_SSID" "$WIFI_PASS" 2>/dev/null | sed 's/#psk.*/scan_ssid=1/g'
    } >"$WIFI_WPA_CONF" || return 1
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

wifi_run_dhcp() {
  command -v udhcpc >/dev/null 2>&1 || return 0
  killall udhcpc 2>/dev/null || true
  udhcpc -i "$WLAN_DEV" -q -n -t "$WIFI_DHCP_TRIES" >"$WIFI_DHCP_LOG" 2>&1
}

wifi_start_late_watcher() {
  expected_ssid=$1
  (
    elapsed=0
    last_state=""
    wifi_console_log "connect pending: watching SSID \"$expected_ssid\" on $WLAN_DEV for ${WIFI_LATE_WATCH_TIMEOUT}s"

    while [ "$elapsed" -lt "$WIFI_LATE_WATCH_TIMEOUT" ]; do
      sleep 2
      elapsed=$((elapsed + 2))
      wifi_current_status

      if [ "$WIFI_CONNECTED_STATE" != "$last_state" ]; then
        wifi_console_log "late state=${WIFI_CONNECTED_STATE} ssid=\"${WIFI_CONNECTED_SSID}\" ip=${WIFI_CONNECTED_IP} elapsed=${elapsed}s"
        last_state="$WIFI_CONNECTED_STATE"
      fi

      if [ "$WIFI_CONNECTED_STATE" = "COMPLETED" ] && [ "$WIFI_CONNECTED_IP" = "-" ]; then
        wifi_console_log "late associated SSID \"${WIFI_CONNECTED_SSID}\", retrying DHCP"
        wifi_run_dhcp || true
        wifi_current_status
      fi

      if [ "$WIFI_CONNECTED_STATE" = "COMPLETED" ] || [ "$WIFI_CONNECTED_IP" != "-" ]; then
        wifi_console_log "late connected SSID \"${WIFI_CONNECTED_SSID}\", IP ${WIFI_CONNECTED_IP}, elapsed=${elapsed}s"
        exit 0
      fi
    done

    wifi_current_status
    wifi_console_log "late connect timeout: state=${WIFI_CONNECTED_STATE} ssid=\"${WIFI_CONNECTED_SSID}\" ip=${WIFI_CONNECTED_IP} elapsed=${WIFI_LATE_WATCH_TIMEOUT}s"
  ) >/dev/null 2>&1 &
}

wifi_start_service() {
  wifi_ensure_device
  rc=$?
  if [ "$rc" = "2" ]; then
    wifi_console_log "no wifi backend found, skip start"
    return 0
  fi
  [ "$rc" = "0" ] || return 1

  iw dev "$WLAN_DEV" set power_save off 2>/dev/null || true
  iwconfig "$WLAN_DEV" power off 2>/dev/null || true
  wifi_watchdog_start

  wifi_load_config
  wifi_console_log "boot wifi config: source=${WIFI_CONFIG_SOURCE} target_ssid=\"${WIFI_SSID}\" hidden=${WIFI_HIDDEN} pass_len=${#WIFI_PASS}"
  wifi_write_wpa_conf || return 1
  wifi_console_log "trying SSID \"${WIFI_SSID}\" on ${WLAN_DEV} (source=${WIFI_CONFIG_SOURCE})"

  killall wpa_supplicant 2>/dev/null || true
  sleep 1
  wpa_start_log=/tmp/rockbase-wpa_supplicant.start.log
  start_ok=0
  for drv in nl80211 wext; do
    wpa_supplicant -D "$drv" -i "$WLAN_DEV" -c "$WIFI_WPA_CONF" -B >"$wpa_start_log" 2>&1 && {
      wifi_console_log "wpa_supplicant started driver=${drv}"
      start_ok=1
      break
    }
    err_msg=$(tr '\n' ';' <"$wpa_start_log" 2>/dev/null | cut -c1-220)
    wifi_console_log "wpa_supplicant start failed driver=${drv} err=${err_msg}"
  done
  [ "$start_ok" = "1" ] || return 1

  i=0
  last_assoc_state=""
  while [ "$i" -lt "$WIFI_ASSOC_TIMEOUT" ]; do
    wifi_current_status
    if [ "$WIFI_CONNECTED_STATE" != "$last_assoc_state" ]; then
      wifi_console_log "assoc state=${WIFI_CONNECTED_STATE} ssid=\"${WIFI_CONNECTED_SSID}\" bssid=${WIFI_CONNECTED_BSSID} ip=${WIFI_CONNECTED_IP}"
      last_assoc_state="$WIFI_CONNECTED_STATE"
    fi
    [ "$WIFI_CONNECTED_STATE" = "COMPLETED" ] && break
    sleep 1
    i=$((i + 1))
  done

  wifi_run_dhcp || true

  wifi_current_status
  if [ "$WIFI_CONNECTED_STATE" = "COMPLETED" ] || [ "$WIFI_CONNECTED_IP" != "-" ]; then
    wifi_console_log "connected ssid=\"${WIFI_CONNECTED_SSID}\" bssid=${WIFI_CONNECTED_BSSID} ip=${WIFI_CONNECTED_IP}"
    return 0
  fi

  wifi_console_log "connect pending: state=${WIFI_CONNECTED_STATE} ssid=\"${WIFI_CONNECTED_SSID}\" bssid=${WIFI_CONNECTED_BSSID} ip=${WIFI_CONNECTED_IP} assoc_timeout=${WIFI_ASSOC_TIMEOUT}s dhcp_tries=${WIFI_DHCP_TRIES} late_watch=${WIFI_LATE_WATCH_TIMEOUT}s"
  printf '[rockbase_wifi] connect pending: state=%s ssid="%s" ip=%s assoc_timeout=%ss dhcp_tries=%s late_watch=%ss\n' \
    "$WIFI_CONNECTED_STATE" "$WIFI_CONNECTED_SSID" "$WIFI_CONNECTED_IP" \
    "$WIFI_ASSOC_TIMEOUT" "$WIFI_DHCP_TRIES" "$WIFI_LATE_WATCH_TIMEOUT"
  wifi_start_late_watcher "$WIFI_SSID"
  return 2
}

# 单次 awk 遍历检测是否有 RTW 线程卡 D（驱动挂死信号，避免 per-pid fork）。
# 结果写入 $WIFI_RTW_D_FILE（1=有 RTW D，0=无）。检测失败默认 0（不误触发重载）。
wifi_rtw_d_state() {
  printf '0' >"$WIFI_RTW_D_FILE" 2>/dev/null || true
  awk '
    FNR == 1 {
      pid = FILENAME; sub("/proc/", "", pid); sub("/stat", "", pid)
      rest = $0; sub(/^[^)]*\) /, "", rest)
      state = substr(rest, 1, 1)
      if (state != "D") next
      f = "/proc/" pid "/comm"
      if ((getline cline < f) > 0) { close(f); if (cline ~ /^RTW_/) { found = 1; exit } }
    }
    END { print (found ? 1 : 0) }' /proc/[0-9]*/stat 2>/dev/null >"$WIFI_RTW_D_FILE" 2>/dev/null || true
}

wifi_watchdog_health() {
  [ -e "/sys/class/net/${WLAN_DEV}" ] || return 1
  # 驱动挂死检测：任一 RTW 线程卡 D 即视为不健康（覆盖 wpa 卡住盲区）
  wifi_rtw_d_state
  [ "$(cat "$WIFI_RTW_D_FILE" 2>/dev/null)" = "1" ] && return 1
  wifi_current_status
  [ "$WIFI_CONNECTED_STATE" = "COMPLETED" ] && [ "$WIFI_CONNECTED_IP" != "-" ]
}

wifi_watchdog_rmmod_sdio() {
  # 后台卸载 8189fs，防 RTW 线程 D 导致 rmmod 阻塞恢复脚本自身。
  # 轮询 lsmod 确认卸载完成，最多 20s；超时清理子进程并返回 1（交 WDT/healthd 兜底）。
  ( rmmod 8189fs 2>/dev/null ) &
  rmmod_pid=$!
  i=0
  while [ "$i" -lt 20 ]; do
    lsmod 2>/dev/null | grep -q '^8189fs' || return 0
    sleep 1
    i=$((i + 1))
  done
  kill "$rmmod_pid" 2>/dev/null || true
  return 1
}

wifi_watchdog_reload() {
  wifi_load_netsel
  wifi_console_log "watchdog: link down (mode=${NET_MODE} state=${WIFI_CONNECTED_STATE} ssid=\"${WIFI_CONNECTED_SSID}\" ip=${WIFI_CONNECTED_IP}), reloading driver"
  wifi_stop_service
  if [ "$NET_MODE" = "sdio_wifi" ]; then
    # SDIO 8189fs 驱动挂死：卸载重载。后台 rmmod 防 RTW 线程 D 阻塞。
    if ! wifi_watchdog_rmmod_sdio; then
      wifi_console_log "watchdog: sdio rmmod timed out, skip reload (WDT/healthd fallback)"
      return 1
    fi
    sleep 3
    modprobe 8189fs 2>/dev/null || true
    sleep 2
  else
    rmmod rtw88_8821cu 2>/dev/null || true
    rmmod rtw88_8821c 2>/dev/null || true
    rmmod rtw88_usb 2>/dev/null || true
    rmmod rtw88_core 2>/dev/null || true
    sleep 2
  fi
  wifi_start_service
}

wifi_watchdog_loop() {
  fail_count=0
  while true; do
    sleep "$WIFI_WATCHDOG_INTERVAL"

    if [ -e "/sys/class/net/${WLAN_DEV}" ]; then
      iw dev "$WLAN_DEV" set power_save off 2>/dev/null || true
      iwconfig "$WLAN_DEV" power off 2>/dev/null || true
    fi

    if wifi_watchdog_health; then
      fail_count=0
      continue
    fi

    fail_count=$((fail_count + 1))
    wifi_console_log "watchdog: unhealthy (state=${WIFI_CONNECTED_STATE} ip=${WIFI_CONNECTED_IP}) fail=${fail_count}/${WIFI_WATCHDOG_FAIL_THRESHOLD}"
    if [ "$fail_count" -ge "$WIFI_WATCHDOG_FAIL_THRESHOLD" ]; then
      wifi_watchdog_reload
      fail_count=0
    fi
  done
}

wifi_watchdog_start() {
  [ "$WIFI_WATCHDOG_ENABLE" = "1" ] || return 0
  if [ -f "$WIFI_WATCHDOG_PIDFILE" ]; then
    pid=$(cat "$WIFI_WATCHDOG_PIDFILE" 2>/dev/null)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      return 0
    fi
  fi
  rm -f "$WIFI_WATCHDOG_PIDFILE"
  mkdir -p "$(dirname "$WIFI_WATCHDOG_PIDFILE")" 2>/dev/null || true
  ( wifi_watchdog_loop ) >/dev/null 2>&1 &
  echo $! >"$WIFI_WATCHDOG_PIDFILE" 2>/dev/null || true
  wifi_console_log "watchdog: started interval=${WIFI_WATCHDOG_INTERVAL}s threshold=${WIFI_WATCHDOG_FAIL_THRESHOLD}"
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
    # Remove the apply lock on any exit path (success, failure, or signal).
    trap 'rm -f "$WIFI_APPLY_LOCK" 2>/dev/null' EXIT
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
