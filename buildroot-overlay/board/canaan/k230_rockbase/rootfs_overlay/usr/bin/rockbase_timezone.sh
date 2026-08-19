#!/bin/sh

ZONEINFO_DIR=${ZONEINFO_DIR:-/usr/share/zoneinfo}
TZ_FILE=${TZ_FILE:-/etc/TZ}
LOCALTIME_FILE=${LOCALTIME_FILE:-/etc/localtime}

normalize_tz() {
    case "$1" in
        CST-8|Asia/Shanghai) printf '%s' 'Asia/Shanghai' ;;
        UTC|UTC0|Etc/UTC) printf '%s' 'UTC' ;;
        *) printf '%s' "$1" ;;
    esac
}

current_tz() {
    if [ -L "${LOCALTIME_FILE}" ]; then
        target=$(readlink "${LOCALTIME_FILE}" 2>/dev/null || true)
        prefix="${ZONEINFO_DIR}/"
        case "${target}" in
            "${prefix}"*) normalize_tz "${target#${prefix}}" ; return 0 ;;
        esac
    fi

    if [ -r "${TZ_FILE}" ]; then
        tz=$(sed 's/[[:space:]]//g' <"${TZ_FILE}" 2>/dev/null || true)
        [ -n "${tz}" ] && { normalize_tz "${tz}"; return 0; }
    fi

    printf '%s' 'UTC'
}

apply_tz() {
    tz=$(normalize_tz "$1")
    [ -n "${tz}" ] || tz=UTC
    zone_path="${ZONEINFO_DIR}/${tz}"
    if [ -r "${zone_path}" ]; then
        mkdir -p "$(dirname "${LOCALTIME_FILE}")" 2>/dev/null || true
        ln -sf "${zone_path}" "${LOCALTIME_FILE}" 2>/dev/null || cp -f "${zone_path}" "${LOCALTIME_FILE}"
    fi
    printf '%s\n' "${tz}" >"${TZ_FILE}"
    export TZ="${tz}"
}

case "${1:-status}" in
    status|get)
        current_tz
        printf '\n'
        ;;
    apply)
        shift
        apply_tz "${1:-UTC}"
        ;;
    list)
        printf '%s\n' \
            UTC Asia/Shanghai Asia/Tokyo Asia/Seoul Asia/Bangkok Asia/Singapore \
            Europe/London Europe/Berlin Europe/Moscow Europe/Istanbul Africa/Cairo \
            Africa/Johannesburg America/New_York America/Chicago America/Denver \
            America/Los_Angeles America/Mexico_City America/Sao_Paulo America/Santiago \
            America/Anchorage Pacific/Honolulu Pacific/Auckland Australia/Perth Australia/Sydney
        ;;
    *)
        echo "Usage: $0 {status|get|apply <zone>|list}"
        exit 1
        ;;
esac
