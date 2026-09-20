#!/bin/sh
# NM-miner Clean Block Data API
#   action=state  GET   current clean progress (state file)
#   action=clean  POST  stop bitcoind, wipe /root/block (keep debug.log), restart
#
# The clean work is long-running (graceful bitcoind stop can take up to ~120s),
# so it runs in a nohup background worker and this CGI returns immediately.
# Progress is exposed via the state file for the frontend to poll.

STATE_FILE=/run/rockbase/cleanblock-state.json
LOCK_FILE=/run/rockbase/cleanblock.lock
LOG_DIR=/root/config/cleanblock
DATADIR=/root/block
STOP_SH=/root/bitcoind/stop.sh
INIT_BITCOIND=/etc/init.d/S61bitcoind
STALE_LOCK_SECS=600

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
        if [ "${pair%%=*}" = "$key" ]; then
          printf '%b' "$(printf '%s' "${pair#*=}" | sed 's/+/ /g; s/%/\\x/g')"
          return 0
        fi
        ;;
    esac
  done
  return 1
}

emit_state() {
  if [ -f "$STATE_FILE" ]; then
    cat "$STATE_FILE"
  else
    printf '{"ok":true,"phase":"idle"}'
  fi
}

start_clean() {
  if [ "$REQUEST_METHOD" != "POST" ]; then
    printf '{"ok":false,"error":"POST required"}'
    return 1
  fi

  # --- re-entry guard: atomic lock file (state file is written async by the
  # worker, so it cannot be trusted for concurrency control) ---
  if [ -f "$LOCK_FILE" ]; then
    lock_ts=$(cat "$LOCK_FILE" 2>/dev/null || echo 0)
    now_ts=$(date +%s 2>/dev/null || echo 0)
    case "$lock_ts" in ''|*[!0-9]*) lock_ts=0 ;; esac
    # stale lock (>STALE_LOCK_SECS) from a crashed worker: remove and proceed
    if [ $((now_ts - lock_ts)) -gt "$STALE_LOCK_SECS" ]; then
      rm -f "$LOCK_FILE" 2>/dev/null || true
    fi
  fi
  if ! ( set -o noclobber; echo "$(date +%s)" >"$LOCK_FILE" ) 2>/dev/null; then
    printf '{"ok":false,"error":"clean already running"}'
    return 1
  fi

  mkdir -p /run/rockbase "$LOG_DIR" 2>/dev/null
  ts=$(date +%Y%m%d-%H%M%S)
  log="$LOG_DIR/cleanblock-$ts.log"

  nohup sh -c '
    STATE=/run/rockbase/cleanblock-state.json
    DATADIR=/root/block
    STOP_SH=/root/bitcoind/stop.sh
    INIT=/etc/init.d/S61bitcoind
    set_state() {
      printf "{\"phase\":\"%s\",\"ts\":%s}" "$1" "$(date +%s)" > "$STATE.tmp" 2>/dev/null && mv "$STATE.tmp" "$STATE" 2>/dev/null
    }
    set_state stopping
    "$STOP_SH" >/dev/null 2>&1
    set_state cleaning
    # wipe the whole datadir, keeping only debug.log for post-mortem
    for f in "$DATADIR"/* "$DATADIR"/.[!.]* "$DATADIR"/..?*; do
      [ -e "$f" ] || continue
      b=${f##*/}
      [ "$b" = "debug.log" ] && continue
      rm -rf "$f" 2>/dev/null
    done
    set_state restarting
    "$INIT" start >/dev/null 2>&1
    sleep 3
    if pidof bitcoind >/dev/null 2>&1; then
      set_state done
    else
      set_state error
    fi
  ' > "$log" 2>&1 &

  printf '{"ok":true,"started":true,"pid":%s}' "$!"
}

get_params
action=$(get_param action 2>/dev/null || true)
[ -n "$action" ] || action=state

echo "Content-Type: application/json; charset=utf-8"
echo "Cache-Control: no-store, no-cache, must-revalidate"
echo "Pragma: no-cache"
echo ""

case "$action" in
  state) emit_state ;;
  clean) start_clean ;;
  *)     printf '{"ok":false,"error":"unknown action"}' ;;
esac
