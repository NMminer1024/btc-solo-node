#!/bin/sh

LOG_DIR=/root/log
HEARTBEAT_LOG=${LOG_DIR}/heartbeat.log
HEARTBEAT_MAX_KB=1024
HEARTBEAT_ROTATIONS=10
HEARTBEAT_INTERVAL=${ROCKBASE_HEARTBEAT_INTERVAL:-60}
STATUS_DIR=/tmp/rockbase-status
BLOCK_PATH=/root/block

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

vm_snapshot() {
    awk '
        /^(nr_dirty|nr_writeback|pswpin|pswpout|pgmajfault|oom_kill) / {
            printf "%s=%s ", $1, $2
        }
    ' /proc/vmstat 2>/dev/null
}

mmc_snapshot() {
    awk '
        $3 == "mmcblk1" {
            printf "mmc_reads=%s mmc_read_ms=%s mmc_writes=%s mmc_write_ms=%s mmc_inflight=%s mmc_io_ms=%s mmc_weighted_io_ms=%s", $4, $7, $8, $11, $12, $13, $14
            found=1
        }
        END {
            if (!found) printf "mmc=missing"
        }
    ' /proc/diskstats 2>/dev/null
}

block_fs_snapshot() {
    awk -v p="${BLOCK_PATH}" '$2 == p { printf "block_mount=rw fstype=%s", $3; found=1 } END { if (!found) printf "block_mount=missing" }' /proc/mounts 2>/dev/null
    df -k "${BLOCK_PATH}" 2>/dev/null | awk 'NR==2 { printf " block_used_mb=%d block_avail_mb=%d block_use_pct=%s", int($3/1024), int($4/1024), $5 }'
}

dstate_snapshot() {
    count=0
    sample=""
    for status in /proc/[0-9]*/status; do
        [ -r "${status}" ] || continue
        state=$(awk '/^State:/ {print $2; exit}' "${status}" 2>/dev/null)
        [ "${state}" = "D" ] || continue
        pid=${status#/proc/}
        pid=${pid%/status}
        name=$(awk '/^Name:/ {print $2; exit}' "${status}" 2>/dev/null)
        count=$((count + 1))
        if [ "${count}" -le 6 ]; then
            [ -n "${sample}" ] && sample="${sample},"
            sample="${sample}${pid}:${name:-?}"
        fi
    done
    [ -n "${sample}" ] || sample="-"
    printf "dstate_count=%s dstate_sample=%s" "${count}" "${sample}"
}

bitcoin_cache_snapshot() {
    chain_state=${STATUS_DIR}/bitcoin-chain.state
    conn_state=${STATUS_DIR}/bitcoin-conn.state
    log_state=${STATUS_DIR}/bitcoin-log.state

    blocks="-"
    headers="-"
    progress="-"
    ibd="-"
    connections="-"
    log_blocks="-"
    stage_key="-"

    if [ -r "${chain_state}" ]; then
        # shellcheck disable=SC1090
        . "${chain_state}" 2>/dev/null || true
        [ -n "${blocks}" ] || blocks="-"
        [ -n "${headers}" ] || headers="-"
        [ -n "${progress}" ] || progress="-"
        [ -n "${ibd}" ] || ibd="-"
    fi
    if [ -r "${conn_state}" ]; then
        # shellcheck disable=SC1090
        . "${conn_state}" 2>/dev/null || true
        [ -n "${connections}" ] || connections="-"
    fi
    if [ -r "${log_state}" ]; then
        # shellcheck disable=SC1090
        . "${log_state}" 2>/dev/null || true
        [ -n "${log_blocks}" ] || log_blocks="-"
        [ -n "${stage_key}" ] || stage_key="-"
    fi

    printf "btc_cache_blocks=%s btc_cache_headers=%s btc_cache_progress=%s btc_cache_ibd=%s btc_cache_conn=%s btc_log_blocks=%s btc_stage=%s" \
        "${blocks}" "${headers}" "${progress}" "${ibd}" "${connections}" "${log_blocks}" "${stage_key}"
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
    vm_info=$(vm_snapshot)
    mmc_info=$(mmc_snapshot)
    block_info=$(block_fs_snapshot)
    dstate_info=$(dstate_snapshot)
    btc_info=$(bitcoin_cache_snapshot)

    printf '%s uptime=%ss load1=%s load5=%s mem_avail=%sMB swap_used=%sMB wlan0_ip=%s rssi=%s bitcoind=%s ckpool=%s %s%s %s %s %s %s\n' \
        "${ts}" "${uptime_s:-0}" "${load1:-0}" "${load5:-0}" "${mem_avail:-0}" "${swap_used:-0}" "${wlan_ip}" "${rssi}" "${bitcoind_state}" "${ckpool_state}" \
        "${vm_info}" "${mmc_info}" "${block_info}" "${dstate_info}" "${btc_info}" \
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
