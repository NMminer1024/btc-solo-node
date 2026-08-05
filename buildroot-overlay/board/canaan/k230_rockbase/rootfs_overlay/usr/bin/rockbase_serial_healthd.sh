#!/bin/sh

INTERVAL="${1:-10}"
CONSOLE=/dev/console
export TZ="${TZ:-CST-8}"

log_console() {
    printf '[health] %s %s\n' "$(date '+%F %T')" "$*" >"${CONSOLE}" 2>/dev/null
}

read_disk() {
    awk -v dev="$1" '$3 == dev { print $6, $10, $13, $14, $12 }' /proc/diskstats 2>/dev/null
}

disk_line() {
    dev="$1"
    prev_r="$2"
    prev_w="$3"
    prev_io="$4"
    prev_wio="$5"

    set -- $(read_disk "${dev}")
    rsec="${1:-0}"
    wsec="${2:-0}"
    io_ms="${3:-0}"
    wio_ms="${4:-0}"
    inflight="${5:-0}"

    dr=$((rsec - prev_r))
    dw=$((wsec - prev_w))
    dio=$((io_ms - prev_io))
    dwio=$((wio_ms - prev_wio))
    [ "${dr}" -lt 0 ] && dr=0
    [ "${dw}" -lt 0 ] && dw=0
    [ "${dio}" -lt 0 ] && dio=0
    [ "${dwio}" -lt 0 ] && dwio=0

    rkb=$((dr / 2))
    wkb=$((dw / 2))
    busy=$((dio / (INTERVAL * 10)))
    weighted=$((dwio / (INTERVAL * 10)))
    printf '%s:+r=%sKB +w=%sKB busy=%s%% wbusy=%s%% in_flight=%s' \
        "${dev}" "${rkb}" "${wkb}" "${busy}" "${weighted}" "${inflight}"
}

mem_line() {
    awk '
        /MemTotal:/ { mt=$2 }
        /MemAvailable:/ { ma=$2 }
        /SwapTotal:/ { st=$2 }
        /SwapFree:/ { sf=$2 }
        /Dirty:/ { dirty=$2 }
        /Writeback:/ { wb=$2 }
        END {
            mu=mt-ma
            su=st-sf
            printf "mem=%d/%dMB swap=%d/%dMB dirty=%dKB wb=%dKB",
                mu/1024, mt/1024, su/1024, st/1024, dirty, wb
        }' /proc/meminfo 2>/dev/null
}

vm_line() {
    awk '
        /pgmajfault / { maj=$2 }
        /pgpgin / { pgin=$2 }
        /pgpgout / { pgout=$2 }
        /pswpin / { sin=$2 }
        /pswpout / { sout=$2 }
        END { printf "pgin=%s pgout=%s majflt=%s swpin=%s swpout=%s", pgin, pgout, maj, sin, sout }' /proc/vmstat 2>/dev/null
}

tasks_line() {
    r=0
    s=0
    d=0
    z=0
    dlist=""
    for f in /proc/[0-9]*/stat; do
        [ -r "${f}" ] || continue
        pid="${f#/proc/}"
        pid="${pid%/stat}"
        line="$(cat "${f}" 2>/dev/null)" || continue
        rest="${line#*) }"
        state="${rest%% *}"
        case "${state}" in
            R) r=$((r + 1)) ;;
            S) s=$((s + 1)) ;;
            D)
                d=$((d + 1))
                if [ -z "${dlist}" ]; then
                    comm="$(cat "/proc/${pid}/comm" 2>/dev/null)"
                    dlist="${pid}:${comm:-?}"
                fi
                ;;
            Z) z=$((z + 1)) ;;
        esac
    done
    printf 'tasks=R%s/S%s/D%s/Z%s d_first=%s' "${r}" "${s}" "${d}" "${z}" "${dlist:--}"
}

watched_threads_line() {
    out=""
    count=0
    for f in /proc/[0-9]*/stat; do
        [ -r "${f}" ] || continue
        pid="${f#/proc/}"
        pid="${pid%/stat}"
        comm="$(cat "/proc/${pid}/comm" 2>/dev/null)" || continue
        case "${comm}" in
            bitcoind|ckpool|rockbase-lcdui|RTW_*|jbd2/*|kworker*|flush-*)
                ;;
            *)
                continue
                ;;
        esac

        line="$(cat "${f}" 2>/dev/null)" || continue
        rest="${line#*) }"
        state="${rest%% *}"
        wchan="$(cat "/proc/${pid}/wchan" 2>/dev/null)"
        [ -n "${wchan}" ] || wchan="-"

        if [ "${state}" = "D" ] || [ "${comm}" = "RTW_XMIT_THREAD" ] || [ "${comm}" = "bitcoind" ] || [ "${comm}" = "rockbase-lcdui" ]; then
            item="${pid}:${comm}:${state}:${wchan}"
            if [ -z "${out}" ]; then
                out="${item}"
            else
                out="${out},${item}"
            fi
            count=$((count + 1))
            [ "${count}" -ge 8 ] && break
        fi
    done
    printf 'watch=%s' "${out:--}"
}

procs_line() {
    printf 'procs='
    for name in bitcoind ckpool rockbase-lcdui; do
        if pidof "${name}" >/dev/null 2>&1; then
            printf '%s:up ' "${name}"
        else
            printf '%s:down ' "${name}"
        fi
    done
}

net_line() {
    ipaddr="$(ip -4 addr show wlan0 2>/dev/null | awk '/inet / { print $2; exit }')"
    oper="$(cat /sys/class/net/wlan0/operstate 2>/dev/null)"
    printf 'wlan0=%s ip=%s' "${oper:-unknown}" "${ipaddr:--}"
}

read_irq_groups() {
    awk '
        {
            line = tolower($0)
            sum = 0
            for (i = 2; i <= NF; i++) {
                if ($i ~ /^[0-9]+$/) {
                    sum += $i
                }
            }
            if (line ~ /mmc|sdhci|sdio/) mmc += sum
            if (line ~ /8189|rtl|rtw|wifi|wlan/) wifi += sum
            if (line ~ /gpio/) gpio += sum
            if (line ~ /i2c/) i2c += sum
        }
        END { printf "mmc %d\nwifi %d\ngpio %d\ni2c %d\n", mmc, wifi, gpio, i2c }' /proc/interrupts 2>/dev/null
}

read_softirqs() {
    awk '
        /^(NET_RX|NET_TX|BLOCK|TIMER|SCHED|RCU):/ {
            name = $1
            sub(":", "", name)
            sum = 0
            for (i = 2; i <= NF; i++) {
                if ($i ~ /^[0-9]+$/) {
                    sum += $i
                }
            }
            printf "%s %d\n", name, sum
        }' /proc/softirqs 2>/dev/null
}

metric_value() {
    key="$1"
    snapshot="$2"
    printf '%s\n' "${snapshot}" | awk -v k="${key}" '$1 == k { print $2; found=1; exit } END { if (!found) print 0 }'
}

delta_metric_line() {
    label="$1"
    keys="$2"
    prev="$3"
    curr="$4"

    printf '%s' "${label}"
    for key in ${keys}; do
        old="$(metric_value "${key}" "${prev}")"
        new="$(metric_value "${key}" "${curr}")"
        delta=$((new - old))
        [ "${delta}" -lt 0 ] && delta=0
        printf ' %s=+%s' "${key}" "${delta}"
    done
}

root_dev="$(sed -n 's/.*root=\/dev\/\([^ ]*\).*/\1/p' /proc/cmdline)"
[ -n "${root_dev}" ] || root_dev=mmcblk0p2
case "${root_dev}" in
    mmcblk*p*) disk_dev="${root_dev%p*}" ;;
    *) disk_dev=mmcblk0 ;;
esac
block_dev="${disk_dev}p3"

set -- $(read_disk "${disk_dev}")
prev_disk_r="${1:-0}"
prev_disk_w="${2:-0}"
prev_disk_io="${3:-0}"
prev_disk_wio="${4:-0}"
set -- $(read_disk "${root_dev}")
prev_root_r="${1:-0}"
prev_root_w="${2:-0}"
prev_root_io="${3:-0}"
prev_root_wio="${4:-0}"
set -- $(read_disk "${block_dev}")
prev_block_r="${1:-0}"
prev_block_w="${2:-0}"
prev_block_io="${3:-0}"
prev_block_wio="${4:-0}"
prev_irq="$(read_irq_groups)"
prev_softirq="$(read_softirqs)"

log_console "start interval=${INTERVAL}s root=${root_dev} disk=${disk_dev} block=${block_dev} sysrq=$(cat /proc/sys/kernel/sysrq 2>/dev/null)"

while :; do
    sleep "${INTERVAL}"

    disk_stats="$(disk_line "${disk_dev}" "${prev_disk_r}" "${prev_disk_w}" "${prev_disk_io}" "${prev_disk_wio}")"
    set -- $(read_disk "${disk_dev}")
    prev_disk_r="${1:-${prev_disk_r}}"
    prev_disk_w="${2:-${prev_disk_w}}"
    prev_disk_io="${3:-${prev_disk_io}}"
    prev_disk_wio="${4:-${prev_disk_wio}}"

    root_stats="$(disk_line "${root_dev}" "${prev_root_r}" "${prev_root_w}" "${prev_root_io}" "${prev_root_wio}")"
    set -- $(read_disk "${root_dev}")
    prev_root_r="${1:-${prev_root_r}}"
    prev_root_w="${2:-${prev_root_w}}"
    prev_root_io="${3:-${prev_root_io}}"
    prev_root_wio="${4:-${prev_root_wio}}"

    block_stats="$(disk_line "${block_dev}" "${prev_block_r}" "${prev_block_w}" "${prev_block_io}" "${prev_block_wio}")"
    set -- $(read_disk "${block_dev}")
    prev_block_r="${1:-${prev_block_r}}"
    prev_block_w="${2:-${prev_block_w}}"
    prev_block_io="${3:-${prev_block_io}}"
    prev_block_wio="${4:-${prev_block_wio}}"

    load="$(cut -d' ' -f1-3 /proc/loadavg)"
    up="$(cut -d' ' -f1 /proc/uptime | cut -d. -f1)"
    log_console "uptime=${up}s load=${load} $(mem_line) $(vm_line) $(tasks_line) $(procs_line) $(net_line)"
    log_console "io ${disk_stats} | ${root_stats} | ${block_stats}"

    curr_irq="$(read_irq_groups)"
    curr_softirq="$(read_softirqs)"
    log_console "$(delta_metric_line irq 'mmc wifi gpio i2c' "${prev_irq}" "${curr_irq}") $(delta_metric_line softirq 'NET_RX NET_TX BLOCK TIMER SCHED RCU' "${prev_softirq}" "${curr_softirq}")"
    log_console "$(watched_threads_line)"
    prev_irq="${curr_irq}"
    prev_softirq="${curr_softirq}"
done
