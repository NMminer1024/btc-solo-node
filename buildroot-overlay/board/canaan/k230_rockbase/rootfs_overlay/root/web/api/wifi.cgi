#!/bin/sh

LIB=/usr/bin/rockbase_wifi.sh
[ -r "$LIB" ] && . "$LIB"

url_decode() {
  printf '%b' "$(printf '%s' "$1" | sed 's/+/ /g; s/%/\\x/g')"
}

get_params() {
  params="$QUERY_STRING"
  if [ "$REQUEST_METHOD" = "POST" ]; then
    body=$(cat)
    if [ -n "$params" ] && [ -n "$body" ]; then
      params="${params}&${body}"
    elif [ -n "$body" ]; then
      params="$body"
    fi
  fi
}

get_param() {
  key=$1
  old_ifs=$IFS
  IFS='&'
  set -- $params
  IFS=$old_ifs
  for pair in "$@"; do
    case "$pair" in
      *=*)
        name=${pair%%=*}
        value=${pair#*=}
        if [ "$name" = "$key" ]; then
          url_decode "$value"
          return 0
        fi
        ;;
    esac
  done
  return 1
}

json_escape() {
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\r//g'
}

json_str() {
  printf '"%s"' "$(json_escape "$1")"
}

read_request() {
  # `params` is already populated by the top-level get_params call; do not
  # re-read the POST body here (stdin is consumed after the first read).
  WIFI_SSID_PARAM=$(get_param ssid 2>/dev/null || true)
  [ -n "$WIFI_SSID_PARAM" ] || WIFI_SSID_PARAM=$(get_param wifi_ssid 2>/dev/null || true)
  WIFI_PASS_PARAM=$(get_param password 2>/dev/null || true)
  [ -n "$WIFI_PASS_PARAM" ] || WIFI_PASS_PARAM=$(get_param wifi_pass 2>/dev/null || true)
  WIFI_HIDDEN_PARAM=$(get_param hidden 2>/dev/null || true)
  [ -n "$WIFI_HIDDEN_PARAM" ] || WIFI_HIDDEN_PARAM=$(get_param wifi_hidden 2>/dev/null || true)
}

wifi_summary_json() {
  wifi_get_json
}

wifi_scan_endpoint() {
  wifi_scan_json
}

wifi_apply_endpoint() {
  read_request
  # No fallback to the saved SSID: an empty/missing ssid must be rejected, not
  # silently re-applied with a caller-supplied password (that can drop the link).
  next_ssid=${WIFI_SSID_PARAM:-}
  next_pass=${WIFI_PASS_PARAM:-}
  next_hidden=${WIFI_HIDDEN_PARAM:-0}

  # --- validation ---
  if [ -z "$next_ssid" ]; then
    printf '{"ok":false,"error":"ssid required"}'
    return 1
  fi
  ssid_len=${#next_ssid}
  if [ "$ssid_len" -lt 1 ] || [ "$ssid_len" -gt 32 ]; then
    printf '{"ok":false,"error":"ssid must be 1-32 characters"}'
    return 1
  fi
  if [ -n "$next_pass" ]; then
    pass_len=${#next_pass}
    if [ "$pass_len" -lt 8 ] || [ "$pass_len" -gt 63 ]; then
      printf '{"ok":false,"error":"password must be 8-63 characters (or empty for open network)"}'
      return 1
    fi
  fi

  # --- re-entry guard: atomic lock file (state file is written async by the
  # worker, so it cannot be trusted for concurrency control) ---
  apply_lock=${WIFI_APPLY_LOCK:-/tmp/rockbase-wifi-apply.lock}
  if [ -f "$apply_lock" ]; then
    lock_ts=$(cat "$apply_lock" 2>/dev/null || echo 0)
    now_ts=$(date +%s 2>/dev/null || echo 0)
    # stale lock (>180s) from a crashed worker: remove and proceed
    if [ $((now_ts - lock_ts)) -gt 180 ]; then
      rm -f "$apply_lock" 2>/dev/null || true
    fi
  fi
  if ! ( set -o noclobber; echo "$(date +%s)" >"$apply_lock" ) 2>/dev/null; then
    printf '{"ok":false,"error":"previous apply still in progress, try again in a few seconds"}'
    return 1
  fi

  apply_pid=$(wifi_apply_async "$next_ssid" "$next_pass" "$next_hidden")
  printf '{'
  printf '"ok":true,'
  printf '"apply_state":"applying",'
  printf '"apply_pid":%s,' "$apply_pid"
  printf '"saved_ssid":%s' "$(json_str "$next_ssid")"
  printf '}'
}

echo "Content-Type: application/json; charset=utf-8"
echo "Cache-Control: no-store, no-cache, must-revalidate"
echo "Pragma: no-cache"
echo ""

# Populate the global `params` (QUERY_STRING + POST body) before parsing the
# action. Without this, get_param sees an empty params and every request
# silently falls back to the "get" endpoint.
get_params
action=$(get_param action 2>/dev/null || true)
[ -n "$action" ] || action=get

case "$action" in
  get|status) wifi_summary_json ;;
  scan) wifi_scan_endpoint ;;
  apply) wifi_apply_endpoint ;;
  *)
    printf '{"ok":false,"error":"unknown action"}'
    ;;
esac
