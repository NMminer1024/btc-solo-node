#!/bin/sh

INTERVAL="${1:-10}"
CONSOLE=/dev/console
DIAG_LOG_DIR=/run/rockbase-log
DIAG_LOG_FILE=${DIAG_LOG_DIR}/serialhealth-diag.log
DIAG_LOG_MAX_KB=256
CONSOLE_OUTPUT=${ROCKBASE_SERIAL_HEALTH_CONSOLE:-1}
CONSOLE_SUMMARY_INTERVAL=${ROCKBASE_SERIAL_HEALTH_SUMMARY_INTERVAL:-10}
DIAG_COOLDOWN_SEC=${ROCKBASE_SERIAL_HEALTH_DIAG_COOLDOWN_SEC:-10}
DIAG_LAST_FILE=/tmp/rockbase-serial-healthd.diag
SYSRQ_LAST_FILE=/tmp/rockbase-serial-healthd.sysrq
SYSRQ_COOLDOWN_SEC=${ROCKBASE_SERIAL_HEALTH_SYSRQ_COOLDOWN_SEC:-120}
SYSRQ_KEYS=${ROCKBASE_SERIAL_HEALTH_SYSRQ_KEYS:-w}
# 软挂死兜底: 关键 I/O 进程持续 D 或 wbusy 持续超高 -> 主动 panic 自动重启
PANIC_AFTER_SEC=${ROCKBASE_SERIAL_HEALTH_PANIC_AFTER_SEC:-60}
PANIC_MIN_WBUSY=${ROCKBASE_SERIAL_HEALTH_PANIC_MIN_WBUSY:-800}
PANIC_COOLDOWN_SEC=${ROCKBASE_SERIAL_HEALTH_PANIC_COOLDOWN_SEC:-600}
PANIC_STARTUP_GRACE=${ROCKBASE_SERIAL_HEALTH_STARTUP_GRACE:-180}
PANIC_LAST_FILE=/tmp/rockbase-serial-healthd.panic
export TZ="${TZ:-CST-8}"

log_console() {
    [ "${CONSOLE_OUTPUT}" = "1" ] || return 0
    printf '[health] %s %s\n' "$(date '+%F %T')" "$*" >"${CONSOLE}" 2>/dev/null
}

rotate_diag_log_if_needed() {
    [ -f "${DIAG_LOG_FILE}" ] || return 0
    size="$(wc -c <"${DIAG_LOG_FILE}" 2>/dev/null)"
    size="$(num_or_zero "${size}")"
    max_bytes=$((DIAG_LOG_MAX_KB * 1024))
    [ "${size}" -lt "${max_bytes}" ] && return 0
    mv "${DIAG_LOG_FILE}" "${DIAG_LOG_FILE}.1" 2>/dev/null || true
}

log_diag() {
    line="[health] $(date '+%F %T') $*"
    if [ "${CONSOLE_OUTPUT}" = "1" ]; then
        printf '%s\n' "${line}" >"${CONSOLE}" 2>/dev/null
    fi
    mkdir -p "${DIAG_LOG_DIR}" 2>/dev/null || true
    rotate_diag_log_if_needed
    printf '%s\n' "${line}" >>"${DIAG_LOG_FILE}" 2>/dev/null || true
}

num_or_zero() {
    case "$1" in
        ''|*[!0-9]*) printf '0' ;;
        *) printf '%s' "$1" ;;
    esac
}

CONSOLE_SUMMARY_INTERVAL="$(num_or_zero "${CONSOLE_SUMMARY_INTERVAL}")"
[ "${CONSOLE_SUMMARY_INTERVAL}" -gt 0 ] || CONSOLE_SUMMARY_INTERVAL=10
DIAG_COOLDOWN_SEC="$(num_or_zero "${DIAG_COOLDOWN_SEC}")"
[ "${DIAG_COOLDOWN_SEC}" -gt 0 ] || DIAG_COOLDOWN_SEC=10
SYSRQ_COOLDOWN_SEC="$(num_or_zero "${SYSRQ_COOLDOWN_SEC}")"
[ "${SYSRQ_COOLDOWN_SEC}" -gt 0 ] || SYSRQ_COOLDOWN_SEC=120
PANIC_AFTER_SEC="$(num_or_zero "${PANIC_AFTER_SEC}")"
[ "${PANIC_AFTER_SEC}" -gt 0 ] || PANIC_AFTER_SEC=60
PANIC_MIN_WBUSY="$(num_or_zero "${PANIC_MIN_WBUSY}")"
[ "${PANIC_MIN_WBUSY}" -gt 0 ] || PANIC_MIN_WBUSY=800
PANIC_COOLDOWN_SEC="$(num_or_zero "${PANIC_COOLDOWN_SEC}")"
[ "${PANIC_COOLDOWN_SEC}" -gt 0 ] || PANIC_COOLDOWN_SEC=600
PANIC_STARTUP_GRACE="$(num_or_zero "${PANIC_STARTUP_GRACE}")"
[ "${PANIC_STARTUP_GRACE}" -gt 0 ] || PANIC_STARTUP_GRACE=180

read_disk() {
    awk -v dev="$1" '$3 == dev { print $6, $10, $13, $14, $12 }' /proc/diskstats 2>/dev/null
}

disk_metrics() {
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
    printf '%s %s %s %s %s' "${rkb}" "${wkb}" "${busy}" "${weighted}" "${inflight}"
}

disk_line() {
    dev="$1"
    shift
    set -- $(disk_metrics "${dev}" "$@")
    printf '%s:+r=%sKB +w=%sKB busy=%s%% wbusy=%s%% in_flight=%s' \
        "${dev}" "$1" "$2" "$3" "$4" "$5"
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
    # single awk pass over all /proc/<pid>/stat (no per-pid fork);
    # output format identical to the old per-pid loop:
    #   tasks=R<n>/S<n>/D<n>/Z<n> d_first=<pid:comm|->
    awk '
        FNR == 1 {
            pid = FILENAME; sub("/proc/", "", pid); sub("/stat", "", pid)
            rest = $0
            sub(/^[^)]*\) /, "", rest)
            state = substr(rest, 1, 1)
            if (state == "R") r++
            else if (state == "S") s++
            else if (state == "D") {
                d++
                if (dlist == "") {
                    comm = "?"
                    f = "/proc/" pid "/comm"
                    if ((getline cline < f) > 0) comm = cline
                    close(f)
                    dlist = pid ":" comm
                }
            }
            else if (state == "Z") z++
        }
        END {
            printf "tasks=R%d/S%d/D%d/Z%d d_first=%s", r+0, s+0, d+0, z+0, (dlist == "" ? "-" : dlist)
        }' /proc/[0-9]*/stat 2>/dev/null
}

tasks_d_count() {
    # pure-shell extract of the D count from a tasks_line string
    local t=$1
    t=${t#*tasks=R}
    t=${t#*/S}
    t=${t#*/D}
    t=${t%%/*}
    case "$t" in ''|*[!0-9]*) t=0 ;; esac
    printf '%s' "$t"
}

watched_threads_line() {
    # single awk pass (no per-pid fork); same selection and output as the old
    # per-pid loop: watch=<pid:comm:state:wchan[,...]> (max 8, "-" when empty)
    awk '
        FNR == 1 {
            pid = FILENAME; sub("/proc/", "", pid); sub("/stat", "", pid)
            f = "/proc/" pid "/comm"
            if ((getline comm < f) <= 0) { close(f); next }
            close(f)
            if (comm !~ /^(bitcoind|ckpool|rockbase-lcdui)$/ &&
                comm !~ /^RTW_/ && comm !~ /^jbd2\// &&
                comm !~ /^kworker/ && comm !~ /^flush-/) next
            rest = $0
            sub(/^[^)]*\) /, "", rest)
            state = substr(rest, 1, 1)
            wchan = "-"
            wf = "/proc/" pid "/wchan"
            if ((getline wl < wf) > 0 && wl != "") wchan = wl
            close(wf)
            if (state == "D" || comm == "RTW_XMIT_THREAD" ||
                comm == "bitcoind" || comm == "rockbase-lcdui") {
                item = pid ":" comm ":" state ":" wchan
                out = (out == "" ? item : out "," item)
                count++
                if (count >= 8) exit
            }
        }
        END { printf "watch=%s", (out == "" ? "-" : out) }' /proc/[0-9]*/stat 2>/dev/null
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

proc_stack_line() {
    pid="$1"
    if [ -r "/proc/${pid}/stack" ]; then
        awk 'NR <= 6 {
                gsub(/^[ \t]+|[ \t]+$/, "", $0)
                if (out == "") out = $0; else out = out ";" $0
             }
             END { print out }' "/proc/${pid}/stack" 2>/dev/null
    fi
}

dump_d_stacks() {
    count=0
    for f in /proc/[0-9]*/stat; do
        [ -r "${f}" ] || continue
        pid="${f#/proc/}"
        pid="${pid%/stat}"
        line="$(cat "${f}" 2>/dev/null)" || continue
        rest="${line#*) }"
        state="${rest%% *}"
        [ "${state}" = "D" ] || continue

        comm="$(cat "/proc/${pid}/comm" 2>/dev/null)"
        wchan="$(cat "/proc/${pid}/wchan" 2>/dev/null)"
        stack="$(proc_stack_line "${pid}")"
        log_diag "diag stack pid=${pid} comm=${comm:-?} state=D wchan=${wchan:--} stack=${stack:--}"

        count=$((count + 1))
        [ "${count}" -ge 6 ] && break
    done
}

# 是否存在卡在 D 状态的关键 I/O 进程 (SD 卡 / WiFi SDIO / 文件系统)
critical_d_process() {
    for f in /proc/[0-9]*/stat; do
        [ -r "${f}" ] || continue
        pid="${f#/proc/}"
        pid="${pid%/stat}"
        line="$(cat "${f}" 2>/dev/null)" || continue
        rest="${line#*) }"
        state="${rest%% *}"
        [ "${state}" = "D" ] || continue
        comm="$(cat "/proc/${pid}/comm" 2>/dev/null)"
        case "${comm}" in
            # WiFi 线程（RTW_*/8189*）挂死由 rockbase_wifi.sh 看门狗自恢复，
            # 不在此触发整机 panic；仅保留 SD 卡 I/O 线程。
            *kblockd*|jbd2/*|flush-*|mmcqd*|*sdhci*)
                return 0
                ;;
        esac
    done
    return 1
}

dump_mmc_sysfs() {
    count=0
    for host in /sys/class/mmc_host/mmc*; do
        [ -d "${host}" ] || continue
        for card in "${host}"/mmc*:*; do
            [ -d "${card}" ] || continue
            name="$(cat "${card}/name" 2>/dev/null)"
            type="$(cat "${card}/type" 2>/dev/null)"
            state="$(cat "${card}/state" 2>/dev/null)"
            ocr="$(cat "${card}/ocr" 2>/dev/null)"
            rca="$(cat "${card}/rca" 2>/dev/null)"
            log_diag "diag mmc host=$(basename "${host}") card=$(basename "${card}") type=${type:--} name=${name:--} state=${state:--} rca=${rca:--} ocr=${ocr:--}"
            count=$((count + 1))
            [ "${count}" -ge 6 ] && return
        done
    done
}

dump_io_diag() {
    reason="$1"
    d_count="$2"
    disk_wbusy="$3"
    block_wbusy="$4"

    log_diag "diag trigger=${reason} d=${d_count} disk_wbusy=${disk_wbusy}% block_wbusy=${block_wbusy}%"
    log_diag "$(watched_threads_line)"

    if [ -r /proc/pressure/io ]; then
        psi="$(awk '{ if (out == "") out = $0; else out = out ";" $0 } END { print out }' /proc/pressure/io 2>/dev/null)"
        log_diag "diag psi_io ${psi:--}"
    fi

    vm="$(awk '
        /^(nr_dirty|nr_writeback|nr_unstable|nr_writeback_temp|nr_dirtied|nr_written|pgpgout|pswpout|allocstall|compact_stall) / {
            printf "%s=%s ", $1, $2
        }' /proc/vmstat 2>/dev/null)"
    log_diag "diag vm ${vm:--}"
    dump_d_stacks
    dump_mmc_sysfs
}

should_emit_diag() {
    now="$1"
    last=""

    [ -r "${DIAG_LAST_FILE}" ] && last="$(cat "${DIAG_LAST_FILE}" 2>/dev/null)"
    last="$(num_or_zero "${last}")"
    if [ -n "${last}" ] && [ "${last}" -gt 0 ] && [ $((now - last)) -lt "${DIAG_COOLDOWN_SEC}" ]; then
        return 1
    fi

    printf '%s\n' "${now}" >"${DIAG_LAST_FILE}" 2>/dev/null || true
    return 0
}

sysrq_key_list() {
    printf '%s\n' "${SYSRQ_KEYS}" | tr '+,' '  '
}

maybe_trigger_sysrq() {
    reason="$1"
    now="$2"
    last=0

    [ -w /proc/sysrq-trigger ] || return 0
    [ -r "${SYSRQ_LAST_FILE}" ] && last="$(cat "${SYSRQ_LAST_FILE}" 2>/dev/null)"
    last="$(num_or_zero "${last}")"
    if [ $((now - last)) -lt "${SYSRQ_COOLDOWN_SEC}" ]; then
        return 0
    fi

    printf '%s\n' "${now}" >"${SYSRQ_LAST_FILE}" 2>/dev/null || true
    log_diag "diag sysrq reason=${reason} keys=${SYSRQ_KEYS}"
    for key in $(sysrq_key_list); do
        case "${key}" in
            w|m|p|l|t)
                printf '%s' "${key}" >/proc/sysrq-trigger 2>/dev/null || true
                ;;
        esac
    done
}

# 软挂死兜底: 持续异常达到 PANIC_AFTER_SEC 后主动 panic, 借 kernel.panic=10 自动重启
maybe_panic_soft_hang() {
    reason="$1"
    now="$2"
    last=0

    [ -w /proc/sysrq-trigger ] || return 0
    [ "${now}" -lt "${PANIC_STARTUP_GRACE}" ] && return 0
    [ -r "${PANIC_LAST_FILE}" ] && last="$(cat "${PANIC_LAST_FILE}" 2>/dev/null)"
    last="$(num_or_zero "${last}")"
    if [ $((now - last)) -lt "${PANIC_COOLDOWN_SEC}" ]; then
        return 0
    fi

    printf '%s\n' "${now}" >"${PANIC_LAST_FILE}" 2>/dev/null || true
    log_console "FATAL soft-hang (${reason}) sustained ${PANIC_AFTER_SEC}s -> triggering panic for auto-recovery"
    log_diag "diag fatal reason=${reason} sustained=${PANIC_AFTER_SEC}s action=panic"
    d="$(tasks_d_count "$(tasks_line)")"
    dump_io_diag "panic-${reason}" "${d}" "${disk_wbusy}" "${block_wbusy}"
    printf 'c' >/proc/sysrq-trigger 2>/dev/null || true
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

log_console "start interval=${INTERVAL}s summary_interval=${CONSOLE_SUMMARY_INTERVAL}s root=${root_dev} disk=${disk_dev} block=${block_dev} sysrq=$(cat /proc/sys/kernel/sysrq 2>/dev/null) sysrq_keys=${SYSRQ_KEYS} panic_after=${PANIC_AFTER_SEC}s panic_min_wbusy=${PANIC_MIN_WBUSY}%"

summary_last=0
PANIC_STUCK_SINCE=""

while :; do
    sleep "${INTERVAL}"

    set -- $(disk_metrics "${disk_dev}" "${prev_disk_r}" "${prev_disk_w}" "${prev_disk_io}" "${prev_disk_wio}")
    disk_wbusy="$(num_or_zero "$4")"
    disk_stats="$(printf '%s:+r=%sKB +w=%sKB busy=%s%% wbusy=%s%% in_flight=%s' "${disk_dev}" "$1" "$2" "$3" "$4" "$5")"
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

    set -- $(disk_metrics "${block_dev}" "${prev_block_r}" "${prev_block_w}" "${prev_block_io}" "${prev_block_wio}")
    block_wbusy="$(num_or_zero "$4")"
    block_stats="$(printf '%s:+r=%sKB +w=%sKB busy=%s%% wbusy=%s%% in_flight=%s' "${block_dev}" "$1" "$2" "$3" "$4" "$5")"
    set -- $(read_disk "${block_dev}")
    prev_block_r="${1:-${prev_block_r}}"
    prev_block_w="${2:-${prev_block_w}}"
    prev_block_io="${3:-${prev_block_io}}"
    prev_block_wio="${4:-${prev_block_wio}}"

    read -r la1 la2 la3 _ < /proc/loadavg
    load="${la1} ${la2} ${la3}"
    read -r up_raw _ < /proc/uptime
    up="${up_raw%.*}"
    task_stats="$(tasks_line)"
    d_count="$(num_or_zero "$(tasks_d_count "${task_stats}")")"

    curr_irq="$(read_irq_groups)"
    curr_softirq="$(read_softirqs)"
    if [ $((up - summary_last)) -ge "${CONSOLE_SUMMARY_INTERVAL}" ]; then
        log_console "uptime=${up}s load=${load} $(mem_line) $(vm_line) ${task_stats} $(procs_line) $(net_line)"
        log_console "io ${disk_stats} | ${root_stats} | ${block_stats}"
        log_console "$(delta_metric_line irq 'mmc wifi gpio i2c' "${prev_irq}" "${curr_irq}") $(delta_metric_line softirq 'NET_RX NET_TX BLOCK TIMER SCHED RCU' "${prev_softirq}" "${curr_softirq}")"
        log_console "$(watched_threads_line)"
        summary_last="${up}"
    fi

    if [ "${d_count}" -gt 0 ]; then
        if should_emit_diag "${up}"; then
            dump_io_diag "D" "${d_count}" "${disk_wbusy}" "${block_wbusy}"
            maybe_trigger_sysrq "D" "${up}"
        fi
    elif [ "${disk_wbusy}" -gt 200 ] || [ "${block_wbusy}" -gt 200 ]; then
        if should_emit_diag "${up}"; then
            dump_io_diag "wbusy" "${d_count}" "${disk_wbusy}" "${block_wbusy}"
            maybe_trigger_sysrq "wbusy" "${up}"
        fi
    fi

    # 软挂死兜底: 关键 I/O 进程持续 D 或 wbusy 持续超高 -> 主动 panic
    if [ "${d_count}" -gt 0 ] && critical_d_process; then
        stuck_now=1
    elif [ "${disk_wbusy}" -gt "${PANIC_MIN_WBUSY}" ] || [ "${block_wbusy}" -gt "${PANIC_MIN_WBUSY}" ]; then
        stuck_now=1
    else
        stuck_now=0
    fi
    if [ "${stuck_now}" = "1" ]; then
        if [ -z "${PANIC_STUCK_SINCE}" ]; then
            PANIC_STUCK_SINCE="${up}"
        elif [ $((up - PANIC_STUCK_SINCE)) -ge "${PANIC_AFTER_SEC}" ]; then
            maybe_panic_soft_hang "stuck" "${up}"
            PANIC_STUCK_SINCE=""
        fi
    else
        PANIC_STUCK_SINCE=""
    fi
    prev_irq="${curr_irq}"
    prev_softirq="${curr_softirq}"
done
