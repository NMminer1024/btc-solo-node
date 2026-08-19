#!/bin/sh
# Rockbase lightweight service supervisor.
# Keeps core daemons (including bitcoind) alive. bitcoind is monitored so
# that an OOM kill or crash is recovered automatically instead of staying
# down until the next reboot.

LOG_DIR=${ROCKBASE_SUPERVISOR_LOG_DIR:-/run/rockbase-log}
LOG_FILE=${ROCKBASE_SUPERVISOR_LOG:-${LOG_DIR}/supervisor.log}
STATE_DIR=${ROCKBASE_SUPERVISOR_STATE:-/run/rockbase-supervisor}
PID_FILE=${PID_FILE:-/var/run/rockbase-supervisord.pid}
INTERVAL=${ROCKBASE_SUPERVISOR_INTERVAL:-60}
RESTART_COOLDOWN=${ROCKBASE_SUPERVISOR_RESTART_COOLDOWN:-120}
STARTUP_GRACE=${ROCKBASE_SUPERVISOR_STARTUP_GRACE:-180}
LOG_MAX_KB=${ROCKBASE_SUPERVISOR_LOG_MAX_KB:-512}
LOG_ROTATIONS=${ROCKBASE_SUPERVISOR_LOG_ROTATIONS:-5}

SERVICES="
bitcoind|bitcoind -conf|/etc/init.d/S61bitcoind
statusd|rockbase_statusd.sh daemon|/etc/init.d/S43statusd
miners-cache|rockbase_miners_cache.sh daemon|/etc/init.d/S63miners-cache
miners-history|rockbase_miners_historyd.sh daemon|/etc/init.d/S64miners-history
ledd|rockbase_ledd.sh run|/etc/init.d/S95ledd
"

now_epoch() {
    date +%s 2>/dev/null || echo 0
}

uptime_seconds() {
    awk -F. '{print $1}' /proc/uptime 2>/dev/null || echo 0
}

log_msg() {
    mkdir -p "$LOG_DIR"
    rotate_log
    printf '%s %s\n' "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" "$*" >> "$LOG_FILE"
}

rotate_log() {
    [ -f "$LOG_FILE" ] || return 0
    size_kb=$(du -k "$LOG_FILE" 2>/dev/null | awk 'NR==1 {print $1}')
    [ -n "$size_kb" ] || size_kb=0
    [ "$size_kb" -lt "$LOG_MAX_KB" ] && return 0

    i=$LOG_ROTATIONS
    while [ "$i" -gt 0 ]; do
        prev=$((i - 1))
        if [ "$prev" -eq 0 ]; then
            [ -f "$LOG_FILE" ] && mv "$LOG_FILE" "$LOG_FILE.$i"
        else
            [ -f "$LOG_FILE.$prev" ] && mv "$LOG_FILE.$prev" "$LOG_FILE.$i"
        fi
        i=$prev
    done
}

is_running_pattern() {
    pattern=$1
    ps | grep -F "$pattern" | grep -v grep >/dev/null 2>&1
}

can_restart() {
    name=$1
    mkdir -p "$STATE_DIR"
    stamp_file="${STATE_DIR}/${name}.last_restart"
    now=$(now_epoch)
    last=$(cat "$stamp_file" 2>/dev/null)
    case "$last" in ''|*[!0-9]*) last=0 ;; esac
    delta=$((now - last))
    [ "$delta" -ge "$RESTART_COOLDOWN" ] || return 1
    echo "$now" > "$stamp_file"
    return 0
}

check_service() {
    name=$1
    pattern=$2
    init_script=$3

    if is_running_pattern "$pattern"; then
        return 0
    fi

    if ! can_restart "$name"; then
        log_msg "skip ${name}: down but restart cooldown active"
        return 0
    fi

    if [ ! -x "$init_script" ]; then
        log_msg "skip ${name}: init script missing or not executable (${init_script})"
        return 0
    fi

    log_msg "restart ${name}: pattern not found (${pattern})"
    "$init_script" restart >> "$LOG_FILE" 2>&1 || log_msg "restart ${name}: failed"
}

check_all() {
    uptime=$(uptime_seconds)
    case "$uptime" in ''|*[!0-9]*) uptime=0 ;; esac
    if [ "$uptime" -lt "$STARTUP_GRACE" ]; then
        return 0
    fi

    printf '%s\n' "$SERVICES" | while IFS='|' read -r name pattern init_script; do
        [ -n "$name" ] || continue
        check_service "$name" "$pattern" "$init_script"
    done
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

case "$1" in
    daemon)
        take_pidfile
        log_msg "supervisor started interval=${INTERVAL}s cooldown=${RESTART_COOLDOWN}s startup_grace=${STARTUP_GRACE}s"
        while true; do
            check_all
            sleep "$INTERVAL"
        done
        ;;
    once)
        check_all
        ;;
    *)
        echo "Usage: $0 {daemon|once}" >&2
        exit 1
        ;;
esac

exit 0
