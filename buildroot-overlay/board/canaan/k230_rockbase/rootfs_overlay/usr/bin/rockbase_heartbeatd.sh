#!/bin/sh

LOG_DIR=/root/log
HEARTBEAT_LOG=${LOG_DIR}/heartbeat.log
HEARTBEAT_MAX_KB=512
HEARTBEAT_ROTATIONS=7
HEARTBEAT_INTERVAL=${ROCKBASE_HEARTBEAT_INTERVAL:-120}

rotate_logs() {
    [ -f "${HEARTBEAT_LOG}" ] || return 0

    size_kb=$(du -k "${HEARTBEAT_LOG}" 2>/dev/null | awk 'NR==1 {print $1}')
    [ -n "${size_kb}" ] || size_kb=0
    [ "${size_kb}" -lt "${HEARTBEAT_MAX_KB}" ] && return 0

    i=${HEARTBEAT_ROTATIONS}
    while [ "${i}" -gt 0 ]; do
        prev=$((i - 1))
        if [ "${prev}" -eq 0 ]; then
            [ -f "${HEARTBEAT_LOG}" ] && mv "${HEARTBEAT_LOG}" "${HEARTBEAT_LOG}.${i}"
        else
            [ -f "${HEARTBEAT_LOG}.${prev}" ] && mv "${HEARTBEAT_LOG}.${prev}" "${HEARTBEAT_LOG}.${i}"
        fi
        i=${prev}
    done
}

mem_available_mb() {
    awk '/MemAvailable:/ {print int($2/1024)}' /proc/meminfo 2>/dev/null
}

swap_used_mb() {
    awk '
        /^SwapTotal:/ {total=$2}
        /^SwapFree:/ {free=$2}
        END {
            used=total-free
            if (used < 0) used=0
            print int(used/1024)
        }
    ' /proc/meminfo 2>/dev/null
}

wifi_rssi() {
    if command -v iw >/dev/null 2>&1; then
        iw dev wlan0 link 2>/dev/null | awk '/signal:/ {print $2 "dBm"; exit}'
        return
    fi

    awk '
        $1 ~ /^wlan0:/ {
            gsub(/\./, "", $4)
            print $4 "dBm"
            found=1
            exit
        }
        END {
            if (!found) print "n/a"
        }
    ' /proc/net/wireless 2>/dev/null
}

wifi_ip() {
    ip -4 addr show dev wlan0 2>/dev/null | awk '/inet / {print $2; exit}'
}

service_state() {
    local service_name="$1"
    if pidof "${service_name}" >/dev/null 2>&1; then
        echo "up"
    else
        echo "down"
    fi
}

write_heartbeat() {
    mkdir -p "${LOG_DIR}"
    rotate_logs

    read -r load1 load5 _ < /proc/loadavg
    ts=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
    uptime_s=$(awk '{print int($1)}' /proc/uptime 2>/dev/null)
    mem_avail=$(mem_available_mb)
    swap_used=$(swap_used_mb)
    wlan_ip=$(wifi_ip)
    [ -n "${wlan_ip}" ] || wlan_ip="n/a"
    rssi=$(wifi_rssi)
    bitcoind_state=$(service_state "bitcoind")
    ckpool_state=$(service_state "ckpool")

    printf '%s uptime=%ss load1=%s load5=%s mem_avail=%sMB swap_used=%sMB wlan0_ip=%s rssi=%s bitcoind=%s ckpool=%s\n' \
        "${ts}" "${uptime_s:-0}" "${load1:-0}" "${load5:-0}" "${mem_avail:-0}" "${swap_used:-0}" "${wlan_ip}" "${rssi}" "${bitcoind_state}" "${ckpool_state}" \
        >> "${HEARTBEAT_LOG}"
    sync "${HEARTBEAT_LOG}" 2>/dev/null || true
}

case "$1" in
    daemon)
        while true; do
            write_heartbeat
            sleep "${HEARTBEAT_INTERVAL}"
        done
        ;;
    once)
        write_heartbeat
        ;;
    *)
        echo "Usage: $0 {daemon|once}"
        exit 1
        ;;
esac

exit 0
