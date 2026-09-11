#!/bin/sh
# Rockbase ckpool miners cache.
# Reads small ckpool status JSON files and publishes a single cached JSON file.

LOGDIR=${CKPOOL_LOGDIR:-/root/config/ckpool/logs}
POOL_STATUS=${POOL_STATUS:-${LOGDIR}/pool/pool.status}
USERS_DIR=${USERS_DIR:-${LOGDIR}/users}
CACHE_DIR=${CACHE_DIR:-/run/rockbase}
CACHE_FILE=${CACHE_FILE:-${CACHE_DIR}/miners.json}
PID_FILE=${PID_FILE:-/var/run/rockbase-miners-cache.pid}
MIN_SLEEP=${MINERS_MIN_SLEEP:-60}
MAX_SLEEP=${MINERS_MAX_SLEEP:-120}
OFFLINE_SECONDS=${MINERS_OFFLINE_SECONDS:-1800}
STALE_SECONDS=${MINERS_STALE_SECONDS:-300}

json_str() {
  printf '"'
  printf '%s' "$1" \
    | tr '\r' '\n' \
    | tr -d '\000-\010\013\014\016-\037' \
    | sed 's/\\/\\\\/g; s/"/\\"/g' \
    | tr -d '\n'
  printf '"'
}

json_num() {
  case "$1" in
    ''|*[!0-9.-]*) printf '%s' "${2:-0}" ;;
    *) printf '%s' "$1" ;;
  esac
}

json_bool() {
  [ "$1" = "1" ] && printf true || printf false
}

json_file_str() {
  key=$1
  file=$2
  sed -n 's/.*"'"${key}"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$file" 2>/dev/null | head -n1
}

json_file_num() {
  key=$1
  file=$2
  sed -n 's/.*"'"${key}"'"[[:space:]]*:[[:space:]]*\([-0-9.][0-9.]*\).*/\1/p' "$file" 2>/dev/null | head -n1
}

json_text_str() {
  key=$1
  printf '%s' "$2" | sed -n 's/.*"'"${key}"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n1
}

json_text_num() {
  key=$1
  printf '%s' "$2" | sed -n 's/.*"'"${key}"'"[[:space:]]*:[[:space:]]*\([-0-9.][0-9.]*\).*/\1/p' | head -n1
}

pool_str() {
  key=$1
  sed -n 's/.*"'"${key}"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$POOL_STATUS" 2>/dev/null | head -n1
}

pool_num() {
  key=$1
  sed -n 's/.*"'"${key}"'"[[:space:]]*:[[:space:]]*\([-0-9.][0-9.]*\).*/\1/p' "$POOL_STATUS" 2>/dev/null | head -n1
}

format_time_ago() {
  ts=$1
  now=$2
  case "$ts" in ''|*[!0-9]*) printf '-' && return ;; esac
  diff=$((now - ts))
  [ "$diff" -lt 0 ] && diff=0
  if [ "$diff" -lt 60 ]; then
    printf '%ss ago' "$diff"
  elif [ "$diff" -lt 3600 ]; then
    printf '%sm ago' $((diff / 60))
  elif [ "$diff" -lt 86400 ]; then
    printf '%sh %sm ago' $((diff / 3600)) $(((diff % 3600) / 60))
  else
    printf '%sd %sh ago' $((diff / 86400)) $(((diff % 86400) / 3600))
  fi
}

extract_worker_objects() {
  awk '
    /"worker"/ && /\[/ { inarr=1; next }
    inarr {
      if (index($0, "{") > 0) { inobj=1; obj="" }
      if (inobj) obj=obj " " $0
      if (inobj && index($0, "}") > 0) { print obj; inobj=0; obj="" }
      if ($0 ~ /^[[:space:]]*\]/) inarr=0
    }
  ' "$1" 2>/dev/null
}

rand_sleep() {
  span=$((MAX_SLEEP - MIN_SLEEP + 1))
  [ "$span" -lt 1 ] && span=1
  seed=$(od -An -N2 -tu2 /dev/urandom 2>/dev/null | tr -d ' ')
  case "$seed" in ''|*[!0-9]*) seed=$(date +%s) ;; esac
  printf '%s' $((MIN_SLEEP + seed % span))
}

take_pidfile() {
  old_pid=$(cat "$PID_FILE" 2>/dev/null)
  if [ -n "$old_pid" ] && [ "$old_pid" != "$$" ] && kill -0 "$old_pid" 2>/dev/null; then
    exit 0
  fi
  echo "$$" > "$PID_FILE"
  trap 'rm -f "$PID_FILE"; exit 0' INT TERM
  trap 'rm -f "$PID_FILE"' EXIT
}

collect_once() {
  mkdir -p "$CACHE_DIR" || return 1
  now=$(date +%s)
  next_sleep=$(rand_sleep)
  tmp="${CACHE_FILE}.tmp.$$"
  pool_exists=0
  [ -r "$POOL_STATUS" ] && pool_exists=1

  pool_last=$(pool_num lastupdate)
  [ -z "$pool_last" ] && pool_last=0
  pool_age=$((now - pool_last))
  [ "$pool_age" -lt 0 ] && pool_age=0
  pool_stale=0
  [ "$pool_age" -gt "$STALE_SECONDS" ] && pool_stale=1

  {
    printf '{\n'
    printf '  "ok": %s,\n' "$(json_bool "$pool_exists")"
    printf '  "generated_at": %s,\n' "$now"
    printf '  "next_refresh_seconds": %s,\n' "$next_sleep"
    printf '  "offline_threshold_seconds": %s,\n' "$OFFLINE_SECONDS"
    printf '  "stale_threshold_seconds": %s,\n' "$STALE_SECONDS"
    printf '  "source": {\n'
    printf '    "pool_status": %s,\n' "$(json_str "$POOL_STATUS")"
    printf '    "users_dir": %s\n' "$(json_str "$USERS_DIR")"
    printf '  },\n'
    printf '  "pool": {\n'
    printf '    "available": %s,\n' "$(json_bool "$pool_exists")"
    printf '    "stale": %s,\n' "$(json_bool "$pool_stale")"
    printf '    "lastupdate": %s,\n' "$(json_num "$pool_last" 0)"
    printf '    "age_seconds": %s,\n' "$(json_num "$pool_age" 0)"
    printf '    "users": %s,\n' "$(json_num "$(pool_num Users)" 0)"
    printf '    "workers": %s,\n' "$(json_num "$(pool_num Workers)" 0)"
    printf '    "idle": %s,\n' "$(json_num "$(pool_num Idle)" 0)"
    printf '    "disconnected": %s,\n' "$(json_num "$(pool_num Disconnected)" 0)"
    printf '    "hashrate1m": %s,\n' "$(json_str "$(pool_str hashrate1m)")"
    printf '    "hashrate5m": %s,\n' "$(json_str "$(pool_str hashrate5m)")"
    printf '    "hashrate15m": %s,\n' "$(json_str "$(pool_str hashrate15m)")"
    printf '    "hashrate1hr": %s,\n' "$(json_str "$(pool_str hashrate1hr)")"
    printf '    "hashrate6hr": %s,\n' "$(json_str "$(pool_str hashrate6hr)")"
    printf '    "hashrate1d": %s,\n' "$(json_str "$(pool_str hashrate1d)")"
    printf '    "hashrate7d": %s,\n' "$(json_str "$(pool_str hashrate7d)")"
    printf '    "diff": %s,\n' "$(json_num "$(pool_num diff)" 0)"
    printf '    "accepted": %s,\n' "$(json_num "$(pool_num accepted)" 0)"
    printf '    "rejected": %s,\n' "$(json_num "$(pool_num rejected)" 0)"
    printf '    "bestshare": %s,\n' "$(json_num "$(pool_num bestshare)" 0)"
    printf '    "sps1m": %s,\n' "$(json_num "$(pool_num SPS1m)" 0)"
    printf '    "sps5m": %s\n' "$(json_num "$(pool_num SPS5m)" 0)"
    printf '  },\n'
    printf '  "miners": [\n'

    first_miner=1
    if [ -d "$USERS_DIR" ]; then
      for user_file in "$USERS_DIR"/*; do
        [ -f "$user_file" ] || continue
        address=$(basename "$user_file")
        lastshare=$(json_file_num lastshare "$user_file")
        [ -z "$lastshare" ] && lastshare=0
        last_age=$((now - lastshare))
        [ "$last_age" -lt 0 ] && last_age=0
        online=0
        [ "$lastshare" -gt 0 ] && [ "$last_age" -le "$OFFLINE_SECONDS" ] && online=1
        worker_objects=$(extract_worker_objects "$user_file")
        worker_detail_count=$(printf '%s\n' "$worker_objects" | sed '/^[[:space:]]*$/d' | wc -l)
        [ -z "$worker_detail_count" ] && worker_detail_count=0

        [ "$first_miner" = 1 ] || printf ',\n'
        first_miner=0
        printf '    {\n'
        printf '      "address": %s,\n' "$(json_str "$address")"
        printf '      "online": %s,\n' "$(json_bool "$online")"
        printf '      "lastshare": %s,\n' "$(json_num "$lastshare" 0)"
        printf '      "lastshare_age_seconds": %s,\n' "$(json_num "$last_age" 0)"
        printf '      "lastshare_ago": %s,\n' "$(json_str "$(format_time_ago "$lastshare" "$now")")"
        printf '      "workers": %s,\n' "$(json_num "$(json_file_num workers "$user_file")" "$worker_detail_count")"
        printf '      "worker_detail_count": %s,\n' "$(json_num "$worker_detail_count" 0)"
        printf '      "hashrate1m": %s,\n' "$(json_str "$(json_file_str hashrate1m "$user_file")")"
        printf '      "hashrate5m": %s,\n' "$(json_str "$(json_file_str hashrate5m "$user_file")")"
        printf '      "hashrate1hr": %s,\n' "$(json_str "$(json_file_str hashrate1hr "$user_file")")"
        printf '      "hashrate1d": %s,\n' "$(json_str "$(json_file_str hashrate1d "$user_file")")"
        printf '      "hashrate7d": %s,\n' "$(json_str "$(json_file_str hashrate7d "$user_file")")"
        printf '      "shares": %s,\n' "$(json_num "$(json_file_num shares "$user_file")" 0)"
        printf '      "bestshare": %s,\n' "$(json_num "$(json_file_num bestshare "$user_file")" 0)"
        printf '      "bestever": %s,\n' "$(json_num "$(json_file_num bestever "$user_file")" 0)"
        printf '      "authorised": %s,\n' "$(json_num "$(json_file_num authorised "$user_file")" 0)"
        printf '      "workers_detail": [\n'

        printf '%s\n' "$worker_objects" | sed '/^[[:space:]]*$/d' | while IFS= read -r obj; do
          workername=$(json_text_str workername "$obj")
          shortname=$workername
          case "$shortname" in *.*) shortname=${shortname#*.} ;; esac
          w_last=$(json_text_num lastshare "$obj")
          [ -z "$w_last" ] && w_last=0
          w_age=$((now - w_last))
          [ "$w_age" -lt 0 ] && w_age=0
          w_online=0
          [ "$w_last" -gt 0 ] && [ "$w_age" -le "$OFFLINE_SECONDS" ] && w_online=1
          [ "${worker_first:-1}" = 1 ] || printf ',\n'
          worker_first=0
          printf '        {\n'
          printf '          "name": %s,\n' "$(json_str "$shortname")"
          printf '          "full_name": %s,\n' "$(json_str "$workername")"
          printf '          "online": %s,\n' "$(json_bool "$w_online")"
          printf '          "lastshare": %s,\n' "$(json_num "$w_last" 0)"
          printf '          "lastshare_age_seconds": %s,\n' "$(json_num "$w_age" 0)"
          printf '          "lastshare_ago": %s,\n' "$(json_str "$(format_time_ago "$w_last" "$now")")"
          printf '          "hashrate1m": %s,\n' "$(json_str "$(json_text_str hashrate1m "$obj")")"
          printf '          "hashrate5m": %s,\n' "$(json_str "$(json_text_str hashrate5m "$obj")")"
          printf '          "hashrate1hr": %s,\n' "$(json_str "$(json_text_str hashrate1hr "$obj")")"
          printf '          "hashrate1d": %s,\n' "$(json_str "$(json_text_str hashrate1d "$obj")")"
          printf '          "hashrate7d": %s,\n' "$(json_str "$(json_text_str hashrate7d "$obj")")"
          printf '          "shares": %s,\n' "$(json_num "$(json_text_num shares "$obj")" 0)"
          printf '          "bestshare": %s,\n' "$(json_num "$(json_text_num bestshare "$obj")" 0)"
          printf '          "bestever": %s\n' "$(json_num "$(json_text_num bestever "$obj")" 0)"
          printf '        }'
        done

        printf '\n'
        printf '      ]\n'
        printf '    }'
      done
    fi

    printf '\n'
    printf '  ]\n'
    printf '}\n'
  } > "$tmp" && mv "$tmp" "$CACHE_FILE"
}

case "$1" in
  once)
    collect_once
    ;;
  daemon|'')
    take_pidfile
    while :; do
      collect_once
      sleep "$(rand_sleep)"
    done
    ;;
  *)
    echo "Usage: $0 [once|daemon]" >&2
    exit 1
    ;;
esac
