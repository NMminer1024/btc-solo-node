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
  get_params
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
  wifi_load_config
  next_ssid=${WIFI_SSID_PARAM:-$WIFI_SSID}
  next_pass=${WIFI_PASS_PARAM:-}
  next_hidden=${WIFI_HIDDEN_PARAM:-0}
  if [ -z "$next_ssid" ]; then
    printf '{"ok":false,"error":"ssid required"}'
    return 1
  fi
  apply_pid=$(wifi_apply_async "$next_ssid" "$next_pass" "$next_hidden")
  printf '{'
  printf '"ok":true,'
  printf '"apply_state":"applying",'
  printf '"apply_pid":%s,' "$(json_str "$apply_pid")"
  printf '"saved_ssid":%s' "$(json_str "$next_ssid")"
  printf '}'
}

echo "Content-Type: application/json; charset=utf-8"
echo ""

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
