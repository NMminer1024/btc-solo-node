#!/bin/sh
# Rockbase miners history cache.
# Reads /run/rockbase/miners.json and keeps a low-write JSONL history on /root/block.

MINERS_CACHE=${MINERS_CACHE:-/run/rockbase/miners.json}
DATA_DIR=${MINERS_HISTORY_DIR:-/root/block/rockbase-history/miners}
CACHE_DIR=${MINERS_HISTORY_CACHE_DIR:-/run/rockbase}
CACHE_FILE=${MINERS_HISTORY_CACHE:-${CACHE_DIR}/miners-history-node.json}
PID_FILE=${PID_FILE:-/var/run/rockbase-miners-history.pid}
MIN_SLEEP=${MINERS_HISTORY_MIN_SLEEP:-110}
MAX_SLEEP=${MINERS_HISTORY_MAX_SLEEP:-150}
RETENTION_DAYS=${MINERS_HISTORY_RETENTION_DAYS:-7}
MAX_POINTS=${MINERS_HISTORY_MAX_POINTS:-720}

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
}

write_sample() {
  [ -r "$MINERS_CACHE" ] || return 1
  mkdir -p "$DATA_DIR" "$CACHE_DIR" || return 1

  now=$(date +%s)
  ts=$(json_cache_num lastupdate)
  [ -n "$ts" ] || ts=$(json_cache_num generated_at)
  [ -n "$ts" ] || ts=$now
  case "$ts" in ''|*[!0-9]*) ts=$now ;; esac

  raw_file=$(raw_file_today)
  last_ts=$(tail -n 1 "$raw_file" 2>/dev/null | sed -n 's/.*"ts":[[:space:]]*\([0-9][0-9]*\).*/\1/p')
  [ "$last_ts" = "$ts" ] && return 0

  h1m=$(json_cache_str hashrate1m)
  h5m=$(json_cache_str hashrate5m)
  h15m=$(json_cache_str hashrate15m)
  h1h=$(json_cache_str hashrate1hr)
  h6h=$(json_cache_str hashrate6hr)
  h1d=$(json_cache_str hashrate1d)
  h7d=$(json_cache_str hashrate7d)
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

render_cache() {
  mkdir -p "$CACHE_DIR" || return 1
  tmp_all="${CACHE_FILE}.all.$$"
  tmp_out="${CACHE_FILE}.tmp.$$"
  : > "$tmp_all"

  if [ -d "$DATA_DIR" ]; then
    cat "$DATA_DIR"/node-*.jsonl >> "$tmp_all" 2>/dev/null || true
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
      total > 0 && (NR % step == 0 || NR == total) {
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
