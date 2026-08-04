#!/bin/sh

LOG_DIR=/root/log
AUDIT_LOG=${LOG_DIR}/boot_audit.log
BOOT_STATE=${LOG_DIR}/boot.current
CLEAN_FLAG=${LOG_DIR}/boot.clean

timestamp_utc() {
    date -u +"%Y-%m-%dT%H:%M:%SZ"
}

log_line() {
    mkdir -p "${LOG_DIR}"
    printf '%s %s\n' "$(timestamp_utc)" "$*" >> "${AUDIT_LOG}"
    sync "${AUDIT_LOG}" 2>/dev/null || sync 2>/dev/null || true
}

write_current_boot_state() {
    local boot_id="$1"
    local boot_epoch="$2"

    cat > "${BOOT_STATE}" <<EOF
BOOT_ID=${boot_id}
BOOT_EPOCH=${boot_epoch}
EOF
    sync "${BOOT_STATE}" 2>/dev/null || sync 2>/dev/null || true
}

read_boot_state() {
    BOOT_ID_PREV=
    BOOT_EPOCH_PREV=

    [ -r "${BOOT_STATE}" ] || return 0

    # shellcheck disable=SC1090
    . "${BOOT_STATE}" 2>/dev/null || true
    BOOT_ID_PREV=${BOOT_ID:-}
    BOOT_EPOCH_PREV=${BOOT_EPOCH:-}
}

current_uptime_seconds() {
    awk '{print int($1)}' /proc/uptime 2>/dev/null
}

case "$1" in
    start)
        mkdir -p "${LOG_DIR}"

        boot_id=$(timestamp_utc)
        boot_epoch=$(date +%s 2>/dev/null || echo 0)
        prev_shutdown=clean

        read_boot_state

        if [ ! -f "${CLEAN_FLAG}" ]; then
            prev_shutdown=unclean
        fi

        log_line "event=boot_start boot_id=${boot_id} prev_shutdown=${prev_shutdown} prev_boot_id=${BOOT_ID_PREV:-none} prev_boot_epoch=${BOOT_EPOCH_PREV:-0} uptime_now=$(current_uptime_seconds)"

        rm -f "${CLEAN_FLAG}"
        write_current_boot_state "${boot_id}" "${boot_epoch}"
        ;;
    stop)
        read_boot_state
        uptime_now=$(current_uptime_seconds)
        log_line "event=shutdown boot_id=${BOOT_ID_PREV:-unknown} uptime_now=${uptime_now:-0}"
        printf 'BOOT_ID=%s\nSHUTDOWN_AT=%s\n' "${BOOT_ID_PREV:-unknown}" "$(timestamp_utc)" > "${CLEAN_FLAG}"
        sync "${CLEAN_FLAG}" 2>/dev/null || sync 2>/dev/null || true
        ;;
    *)
        echo "Usage: $0 {start|stop}"
        exit 1
        ;;
esac

exit 0
