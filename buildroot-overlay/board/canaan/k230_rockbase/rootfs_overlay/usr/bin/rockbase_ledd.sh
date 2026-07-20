#!/bin/sh
#
# rockbase_ledd.sh - Event-driven LED controller for Rockbase
#

PIDFILE=/run/rockbase-ledd.pid
RGB_LED=/usr/bin/rgb_led.sh
ERROR_GRACE=180
LOOP_DELAY=1
SD_EVENT_HOLD=1
SD_COOLDOWN=2
CURRENT_MODE=boot
NET_TIER=idle

is_running() {
        [ -f "${PIDFILE}" ] || return 1

        pid=$(cat "${PIDFILE}" 2>/dev/null)
        [ -n "${pid}" ] || return 1
        kill -0 "${pid}" 2>/dev/null
}

write_pidfile() {
        echo "$1" > "${PIDFILE}"
}

clear_pidfile() {
        rm -f "${PIDFILE}"
}

uptime_seconds() {
        awk -F. '{print $1}' /proc/uptime 2>/dev/null || echo 0
}

pick_net_dev() {
        for dev in wlan0 eth0; do
                if [ -d "/sys/class/net/${dev}" ]; then
                        echo "${dev}"
                        return 0
                fi
        done

        return 1
}

pick_sd_stat() {
        for stat_file in /sys/block/mmcblk1/stat /sys/block/mmcblk0/stat; do
                if [ -f "${stat_file}" ]; then
                        echo "${stat_file}"
                        return 0
                fi
        done

        return 1
}

read_net_bytes_total() {
        dev="$1"
        rx=$(cat "/sys/class/net/${dev}/statistics/rx_bytes" 2>/dev/null || echo 0)
        tx=$(cat "/sys/class/net/${dev}/statistics/tx_bytes" 2>/dev/null || echo 0)
        echo $((rx + tx))
}

read_net_packets_total() {
        dev="$1"
        rx=$(cat "/sys/class/net/${dev}/statistics/rx_packets" 2>/dev/null || echo 0)
        tx=$(cat "/sys/class/net/${dev}/statistics/tx_packets" 2>/dev/null || echo 0)
        echo $((rx + tx))
}

read_sd_total() {
        stat_file="$1"
        set -- $(cat "${stat_file}" 2>/dev/null)
        write_ios="${5:-0}"
        echo $((write_ios))
}

fault_reason() {
        if [ "$(uptime_seconds)" -lt "${ERROR_GRACE}" ]; then
                return 1
        fi

        if ! pidof bitcoind >/dev/null 2>&1; then
                echo "bitcoind"
                return 0
        fi

        if ! pidof ckpool >/dev/null 2>&1; then
                echo "ckpool"
                return 0
        fi

        return 1
}

apply_fault() {
        reason="$1"
        if [ "${CURRENT_MODE}" != "fault:${reason}" ]; then
                "${RGB_LED}" blink red 200 200 >/dev/null 2>&1 || true
                CURRENT_MODE="fault:${reason}"
        fi
}

compute_net_score() {
        delta_bytes="$1"
        delta_packets="$2"
        kbps=$((delta_bytes / 1024))
        echo $((delta_packets + (kbps / 16)))
}

choose_net_tier() {
        score_x10="$1"

        case "${NET_TIER}" in
                idle)
                        if [ "${score_x10}" -ge 200 ]; then
                                echo light
                        else
                                echo idle
                        fi
                        ;;
                light)
                        if [ "${score_x10}" -lt 140 ]; then
                                echo idle
                        elif [ "${score_x10}" -ge 1200 ]; then
                                echo normal
                        else
                                echo light
                        fi
                        ;;
                normal)
                        if [ "${score_x10}" -lt 840 ]; then
                                echo light
                        elif [ "${score_x10}" -ge 2600 ]; then
                                echo busy
                        else
                                echo normal
                        fi
                        ;;
                busy)
                        if [ "${score_x10}" -lt 1820 ]; then
                                echo normal
                        elif [ "${score_x10}" -ge 4200 ]; then
                                echo heavy
                        else
                                echo busy
                        fi
                        ;;
                heavy)
                        if [ "${score_x10}" -lt 2940 ]; then
                                echo busy
                        else
                                echo heavy
                        fi
                        ;;
                *)
                        if [ "${score_x10}" -ge 4200 ]; then
                                echo heavy
                        elif [ "${score_x10}" -ge 2600 ]; then
                                echo busy
                        elif [ "${score_x10}" -ge 1200 ]; then
                                echo normal
                        elif [ "${score_x10}" -ge 200 ]; then
                                echo light
                        else
                                echo idle
                        fi
                        ;;
        esac
}

apply_net_tier() {
        tier="$1"

        case "${tier}" in
                idle)
                        mode="net:idle"
                        on_ms=70
                        off_ms=1500
                        ;;
                light)
                        mode="net:light"
                        on_ms=50
                        off_ms=600
                        ;;
                normal)
                        mode="net:normal"
                        on_ms=40
                        off_ms=250
                        ;;
                busy)
                        mode="net:busy"
                        on_ms=35
                        off_ms=90
                        ;;
                heavy)
                        mode="net:heavy"
                        on_ms=30
                        off_ms=45
                        ;;
                *)
                        mode="net:idle"
                        on_ms=70
                        off_ms=1500
                        tier=idle
                        ;;
        esac

        NET_TIER="${tier}"
        if [ "${CURRENT_MODE}" != "${mode}" ]; then
                "${RGB_LED}" blink green "${on_ms}" "${off_ms}" >/dev/null 2>&1 || true
                CURRENT_MODE="${mode}"
        fi
}

flash_event() {
        tag="$1"
        color="$2"
        on_ms="$3"
        off_ms="$4"
        hold_secs="$5"

        "${RGB_LED}" blink "${color}" "${on_ms}" "${off_ms}" >/dev/null 2>&1 || true
        CURRENT_MODE="${tag}"
        sleep "${hold_secs}"
}

run_loop() {
        net_dev=$(pick_net_dev 2>/dev/null || true)
        sd_stat=$(pick_sd_stat 2>/dev/null || true)
        last_net_bytes=0
        last_net_packets=0
        last_sd_total=0
        last_sd_event=0
        smooth_score_x10=0

        if [ -n "${net_dev}" ]; then
                last_net_bytes=$(read_net_bytes_total "${net_dev}")
                last_net_packets=$(read_net_packets_total "${net_dev}")
        fi

        if [ -n "${sd_stat}" ]; then
                last_sd_total=$(read_sd_total "${sd_stat}")
        fi

        apply_net_tier idle

        while true; do
                reason=$(fault_reason 2>/dev/null || true)
                if [ -n "${reason}" ]; then
                        apply_fault "${reason}"
                        sleep "${LOOP_DELAY}"
                        continue
                fi

                if [ -z "${net_dev}" ] || [ ! -d "/sys/class/net/${net_dev}" ]; then
                        net_dev=$(pick_net_dev 2>/dev/null || true)
                        if [ -n "${net_dev}" ]; then
                                last_net_bytes=$(read_net_bytes_total "${net_dev}")
                                last_net_packets=$(read_net_packets_total "${net_dev}")
                        fi
                        smooth_score_x10=0
                        NET_TIER=idle
                fi

                if [ -z "${sd_stat}" ] || [ ! -f "${sd_stat}" ]; then
                        sd_stat=$(pick_sd_stat 2>/dev/null || true)
                        if [ -n "${sd_stat}" ]; then
                                last_sd_total=$(read_sd_total "${sd_stat}")
                        fi
                fi

                now=$(uptime_seconds)
                sd_changed=0

                if [ -n "${sd_stat}" ]; then
                        sd_total=$(read_sd_total "${sd_stat}")
                        if [ "${sd_total}" -gt "${last_sd_total}" ]; then
                                sd_changed=1
                        fi
                        last_sd_total="${sd_total}"
                fi

                if [ -n "${net_dev}" ]; then
                        net_bytes=$(read_net_bytes_total "${net_dev}")
                        net_packets=$(read_net_packets_total "${net_dev}")

                        if [ "${net_bytes}" -lt "${last_net_bytes}" ]; then
                                delta_bytes=0
                        else
                                delta_bytes=$((net_bytes - last_net_bytes))
                        fi

                        if [ "${net_packets}" -lt "${last_net_packets}" ]; then
                                delta_packets=0
                        else
                                delta_packets=$((net_packets - last_net_packets))
                        fi

                        raw_score=$(compute_net_score "${delta_bytes}" "${delta_packets}")
                        smooth_score_x10=$(((smooth_score_x10 * 65 + raw_score * 350 + 50) / 100))
                        last_net_bytes="${net_bytes}"
                        last_net_packets="${net_packets}"
                else
                        smooth_score_x10=0
                        NET_TIER=idle
                fi

                if [ "${sd_changed}" -eq 1 ] && [ $((now - last_sd_event)) -ge "${SD_COOLDOWN}" ]; then
                        last_sd_event="${now}"
                        flash_event sd blue 35 45 "${SD_EVENT_HOLD}"
                        continue
                fi

                apply_net_tier "$(choose_net_tier "${smooth_score_x10}")"
                sleep "${LOOP_DELAY}"
        done
}

start_daemon() {
        if is_running; then
                return 0
        fi

        "$0" run >/dev/null 2>&1 &
        daemon_pid=$!
        write_pidfile "${daemon_pid}"
        sleep 1
        kill -0 "${daemon_pid}" 2>/dev/null
}

stop_daemon() {
        if ! is_running; then
                clear_pidfile
                "${RGB_LED}" color off >/dev/null 2>&1 || true
                return 0
        fi

        pid=$(cat "${PIDFILE}" 2>/dev/null)
        kill "${pid}" 2>/dev/null || true

        timeout=5
        while kill -0 "${pid}" 2>/dev/null && [ "${timeout}" -gt 0 ]; do
                sleep 1
                timeout=$((timeout - 1))
        done

        if kill -0 "${pid}" 2>/dev/null; then
                kill -9 "${pid}" 2>/dev/null || true
        fi

        clear_pidfile
        "${RGB_LED}" color off >/dev/null 2>&1 || true
        return 0
}

case "$1" in
  start)
        start_daemon
        ;;
  stop)
        stop_daemon
        ;;
  restart|reload)
        stop_daemon
        sleep 1
        start_daemon
        ;;
  run)
        trap 'clear_pidfile; "${RGB_LED}" color off >/dev/null 2>&1 || true; exit 0' INT TERM EXIT
        run_loop
        ;;
  *)
        echo "Usage: $0 {start|stop|restart|run}" >&2
        exit 1
        ;;
esac

exit $?