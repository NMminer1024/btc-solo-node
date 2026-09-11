#!/bin/sh
# NM-miner JSON Log API
# Supports incremental log fetch by byte cursor.

json_str_ml() {
  printf '"'
  printf '%s' "$1" \
    | tr '\r' '\n' \
    | tr -d '\000-\010\013\014\016-\037' \
    | sed 's/\[[0-9;?]*[A-Za-z]//g' \
    | sed 's/\\/\\\\/g; s/"/\\"/g' \
    | awk '{printf "%s\\n", $0}' \
    | tr -d '\n' \
    | sed 's/\\\\n$//'
  printf '"'
}

json_str() {
  printf '"'
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr -d '\n'
  printf '"'
}

get_qs_value() {
  key="$1"
  printf '%s' "${QUERY_STRING:-}" | tr '&' '\n' | sed -n "s/^${key}=//p" | head -n1
}

service=$(get_qs_value service)
cursor=$(get_qs_value cursor)
limit=$(get_qs_value limit)
max_initial_bytes=1048576
max_delta_bytes=65536

case "$service" in
  stratum)
    log_service="stratum"
    log_path="/root/config/ckpool/logs/ckpool.log"
    default_limit=200
  ;;
  bitcoind|*)
    log_service="bitcoind"
    log_path="/root/block/debug.log"
    default_limit=300
  ;;
esac

[ -z "$limit" ] && limit=$default_limit
limit=$(awk -v n="$limit" 'BEGIN{n=int(n); if (n < 50) n=50; if (n > 500) n=500; print n}')
initial_request="false"
reset="false"
ok="true"
text=""
size=0
mtime="0"
truncated="false"

if [ -z "$cursor" ]; then
  initial_request="true"
  cursor=0
fi

if [ -r "$log_path" ]; then
  size=$(wc -c < "$log_path" 2>/dev/null)
  [ -z "$size" ] && size=0
  mtime=$(stat -c %Y "$log_path" 2>/dev/null || echo 0)

  if [ "$cursor" -le 0 ] 2>/dev/null || [ "$cursor" -gt "$size" ] 2>/dev/null; then
    [ "$cursor" -gt "$size" ] 2>/dev/null && reset="true"
    [ "$size" -gt "$max_initial_bytes" ] 2>/dev/null && truncated="true"
    text=$(tail -c "$max_initial_bytes" "$log_path" 2>/dev/null | tr '\r' '\n' | tail -n "$limit" 2>/dev/null)
    cursor="$size"
  elif [ "$cursor" -lt "$size" ] 2>/dev/null; then
    delta=$((size - cursor))
    if [ "$delta" -gt "$max_delta_bytes" ] 2>/dev/null; then
      truncated="true"
      text=$(tail -c "$max_delta_bytes" "$log_path" 2>/dev/null | tr '\r' '\n' | tail -n "$limit" 2>/dev/null)
    else
      text=$(tail -c +$((cursor + 1)) "$log_path" 2>/dev/null | tr '\r' '\n' | tail -n "$limit" 2>/dev/null)
    fi
    cursor="$size"
  fi
else
  ok="false"
  text="Log file not available: $log_path"
fi

echo "Content-Type: application/json; charset=utf-8"
echo ""
printf '{\n'
printf '  "ok": %s,\n' "$ok"
printf '  "service": %s,\n' "$(json_str "$log_service")"
printf '  "path": %s,\n' "$(json_str "$log_path")"
printf '  "initial": %s,\n' "$initial_request"
printf '  "reset": %s,\n' "$reset"
printf '  "truncated": %s,\n' "$truncated"
printf '  "cursor": %s,\n' "$cursor"
printf '  "size": %s,\n' "$size"
printf '  "mtime": %s,\n' "$mtime"
printf '  "limit": %s,\n' "$limit"
printf '  "text": %s\n' "$(json_str_ml "$text")"
printf '}\n'
