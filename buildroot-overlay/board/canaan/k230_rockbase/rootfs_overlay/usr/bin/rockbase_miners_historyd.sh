#!/bin/sh
# Rockbase miners history cache.
# Reads /run/rockbase/miners.json and keeps a low-write JSONL history on /root/block.

MINERS_CACHE=${MINERS_CACHE:-/run/rockbase/miners.json}
DATA_DIR=${MINERS_HISTORY_DIR:-/root/config/rockbase-history/miners}
USERS_DIR=${CKPOOL_USERS_DIR:-/root/config/ckpool/logs/users}
CACHE_DIR=${MINERS_HISTORY_CACHE_DIR:-/run/rockbase}
CACHE_FILE=${MINERS_HISTORY_CACHE:-${CACHE_DIR}/miners-history-node.json}
PID_FILE=${PID_FILE:-/var/run/rockbase-miners-history.pid}
MIN_SLEEP=${MINERS_HISTORY_MIN_SLEEP:-110}
MAX_SLEEP=${MINERS_HISTORY_MAX_SLEEP:-150}
RETENTION_DAYS=${MINERS_HISTORY_RETENTION_DAYS:-7}
MAX_POINTS=${MINERS_HISTORY_MAX_POINTS:-720}
OFFLINE_SECONDS=${MINERS_OFFLINE_SECONDS:-1800}
USER_OFFLINE_DROP_SECONDS=${MINERS_USER_OFFLINE_DROP_SECONDS:-604800}

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

json_cache_str() {
  key=$1
  sed -n 's/.*"'"${key}"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$MINERS_CACHE" 2>/dev/null | head -n1
}

json_cache_num() {
  key=$1
  sed -n 's/.*"'"${key}"'"[[:space:]]*:[[:space:]]*\([-0-9.][0-9.]*\).*/\1/p' "$MINERS_CACHE" 2>/dev/null | head -n1
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

valid_address() {
  case "$1" in
    ''|*[!A-Za-z0-9]*) return 1 ;;
    *) return 0 ;;
  esac
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

raw_file_today() {
  printf '%s/node-%s.jsonl' "$DATA_DIR" "$(date -u +%Y%m%d)"
}

cleanup_old_files() {
  [ -d "$DATA_DIR" ] || return 0
  find "$DATA_DIR" -type f -name 'node-*.jsonl' -mtime +"$RETENTION_DAYS" -delete 2>/dev/null || true
  find "$DATA_DIR" -type f -name 'user-*.jsonl' -mtime +"$RETENTION_DAYS" -delete 2>/dev/null || true
  cleanup_offline_users
}

# Drop per-wallet history once the wallet disappeared from ckpool or has
# been offline longer than USER_OFFLINE_DROP_SECONDS (default 7 days).
cleanup_offline_users() {
  # If ckpool has not started yet (e.g. right after boot while bitcoind is
  # still syncing), the users dir is absent and we cannot tell a deleted
  # wallet from a not-yet-started pool. Skip cleanup to avoid wiping history.
  [ -d "$USERS_DIR" ] || return 0
  now=$(date +%s)
  for hist in "$DATA_DIR"/user-*.jsonl; do
    [ -f "$hist" ] || continue
    base=$(basename "$hist" .jsonl)
    address=${base#user-}
    address=${address%-2[0-9][0-9][0-9][0-1][0-9][0-3][0-9]}
    valid_address "$address" || continue
    user_file="$USERS_DIR/$address"
    if [ ! -f "$user_file" ]; then
      rm -f "$DATA_DIR"/user-"$address"-*.jsonl
      continue
    fi
    lastshare=$(json_file_num lastshare "$user_file")
    case "$lastshare" in ''|*[!0-9]*) lastshare=0 ;; esac
    if [ "$lastshare" -gt 0 ] && [ $((now - lastshare)) -gt "$USER_OFFLINE_DROP_SECONDS" ]; then
      rm -f "$DATA_DIR"/user-"$address"-*.jsonl
    fi
  done
}

write_sample() {
  [ -r "$MINERS_CACHE" ] || return 1
  mkdir -p "$DATA_DIR" "$CACHE_DIR" || return 1

  now=$(date +%s)
  cache_ok=$(sed -n 's/.*"ok"[[:space:]]*:[[:space:]]*\(true\|false\).*/\1/p' "$MINERS_CACHE" 2>/dev/null | head -n1)
  [ "$cache_ok" = "false" ] && return 0

  ts=$(json_cache_num lastupdate)
  if [ -z "$ts" ] || [ "$ts" = "0" ]; then
    ts=$(json_cache_num generated_at)
  fi
  [ -n "$ts" ] || ts=$now
  case "$ts" in ''|*[!0-9]*) ts=$now ;; esac

  h1m=$(json_cache_str hashrate1m)
  h5m=$(json_cache_str hashrate5m)
  h15m=$(json_cache_str hashrate15m)
  h1h=$(json_cache_str hashrate1hr)
  h6h=$(json_cache_str hashrate6hr)
  h1d=$(json_cache_str hashrate1d)
  h7d=$(json_cache_str hashrate7d)
  if [ -z "$h1m$h5m$h15m$h1h$h6h$h1d$h7d" ]; then
    return 0
  fi

  raw_file=$(raw_file_today)
  last_ts=$(tail -n 1 "$raw_file" 2>/dev/null | sed -n 's/.*"ts":[[:space:]]*\([0-9][0-9]*\).*/\1/p')
  [ "$last_ts" = "$ts" ] && return 0

  users=$(json_cache_num users)
  workers=$(json_cache_num workers)
  bestshare=$(json_cache_num bestshare)

  {
    printf '{"ts":%s' "$(json_num "$ts" "$now")"
    printf ',"h1m":%s' "$(json_str "$h1m")"
    printf ',"h5m":%s' "$(json_str "$h5m")"
    printf ',"h15m":%s' "$(json_str "$h15m")"
    printf ',"h1h":%s' "$(json_str "$h1h")"
    printf ',"h6h":%s' "$(json_str "$h6h")"
    printf ',"h1d":%s' "$(json_str "$h1d")"
    printf ',"h7d":%s' "$(json_str "$h7d")"
    printf ',"users":%s' "$(json_num "$users" 0)"
    printf ',"workers":%s' "$(json_num "$workers" 0)"
    printf ',"bestshare":%s' "$(json_num "$bestshare" 0)"
    printf '}\n'
  } >> "$raw_file"
}

# Per-wallet samples, mirroring the pi5 ck-monitor user_history table:
# online wallets record live window hashrates; offline wallets record zeros
# so the chart drops to 0 instead of a flat stale line.
write_user_samples() {
  [ -d "$USERS_DIR" ] || return 0
  now=$(date +%s)
  for user_file in "$USERS_DIR"/*; do
    [ -f "$user_file" ] || continue
    address=$(basename "$user_file")
    valid_address "$address" || continue

    lastshare=$(json_file_num lastshare "$user_file")
    case "$lastshare" in ''|*[!0-9]*) lastshare=0 ;; esac
    online=0
    [ "$lastshare" -gt 0 ] && [ $((now - lastshare)) -le "$OFFLINE_SECONDS" ] && online=1

    if [ "$online" = "1" ]; then
      h1m=$(json_file_str hashrate1m "$user_file")
      h5m=$(json_file_str hashrate5m "$user_file")
      h1h=$(json_file_str hashrate1hr "$user_file")
      h1d=$(json_file_str hashrate1d "$user_file")
      h7d=$(json_file_str hashrate7d "$user_file")
      workers=$(json_file_num workers "$user_file")
    else
      h1m=0; h5m=0; h1h=0; h1d=0; h7d=0; workers=0
    fi
    bestshare=$(json_file_num bestshare "$user_file")
    bestever=$(json_file_num bestever "$user_file")

    raw_file="$DATA_DIR/user-${address}-$(date -u +%Y%m%d).jsonl"
    last_ts=$(tail -n 1 "$raw_file" 2>/dev/null | sed -n 's/.*"ts":[[:space:]]*\([0-9][0-9]*\).*/\1/p')
    [ "$last_ts" = "$now" ] && continue

    {
      printf '{"ts":%s' "$now"
      printf ',"h1m":%s' "$(json_str "$h1m")"
      printf ',"h5m":%s' "$(json_str "$h5m")"
      printf ',"h1h":%s' "$(json_str "$h1h")"
      printf ',"h1d":%s' "$(json_str "$h1d")"
      printf ',"h7d":%s' "$(json_str "$h7d")"
      printf ',"workers":%s' "$(json_num "$workers" 0)"
      printf ',"bestshare":%s' "$(json_num "$bestshare" 0)"
      printf ',"bestever":%s' "$(json_num "$bestever" 0)"
      printf '}\n'
    } >> "$raw_file"
  done
}

render_cache() {
  mkdir -p "$CACHE_DIR" || return 1
  tmp_all="${CACHE_FILE}.all.$$"
  tmp_out="${CACHE_FILE}.tmp.$$"
  : > "$tmp_all"

  if [ -d "$DATA_DIR" ]; then
    grep -h -v '^[[:space:]]*$' "$DATA_DIR"/node-*.jsonl >> "$tmp_all" 2>/dev/null || true
  fi

  total=$(wc -l < "$tmp_all" 2>/dev/null | tr -d ' ')
  case "$total" in ''|*[!0-9]*) total=0 ;; esac
  step=1
  if [ "$total" -gt "$MAX_POINTS" ] 2>/dev/null; then
    step=$(((total + MAX_POINTS - 1) / MAX_POINTS))
  fi

  {
    printf '{\n'
    printf '  "ok": true,\n'
    printf '  "generated_at": %s,\n' "$(date +%s)"
    printf '  "retention_days": %s,\n' "$(json_num "$RETENTION_DAYS" 7)"
    printf '  "max_points": %s,\n' "$(json_num "$MAX_POINTS" 720)"
    printf '  "raw_points": %s,\n' "$total"
    printf '  "sample_step": %s,\n' "$step"
    printf '  "source": %s,\n' "$(json_str "$DATA_DIR")"
    printf '  "series": ["1m","5m","15m","1h","6h","1d","7d"],\n'
    printf '  "points": [\n'
    awk -v step="$step" -v total="$total" '
      $0 != "" && total > 0 && (NR % step == 0 || NR == total) {
        if (seen++) printf ",\n"
        printf "    %s", $0
      }
      END {
        if (seen) printf "\n"
      }
    ' "$tmp_all"
    printf '  ]\n'
    printf '}\n'
  } > "$tmp_out" && mv "$tmp_out" "$CACHE_FILE"

  rm -f "$tmp_all" "$tmp_out"
}

collect_once() {
  cleanup_old_files
  write_sample || true
  write_user_samples || true
  render_cache
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
