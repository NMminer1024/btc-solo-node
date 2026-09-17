#!/bin/sh

ZONEINFO_DIR=${ZONEINFO_DIR:-/usr/share/zoneinfo}
TZ_FILE=${TZ_FILE:-/etc/TZ}
LOCALTIME_FILE=${LOCALTIME_FILE:-/etc/localtime}

# 时区名归一化（仅兼容旧值 CST-8/UTC）。
# 新增时区无需在此添加（标准 IANA 名直接透传）；时区清单见下方 list 命令。
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
    # 时区清单：新增/删除时区时，必须与
    #   package/rockbase_lcdui/src/timezone_overlay.h 的 kZones[] 保持同步。
    list)
        printf '%s\n' \
            Pacific/Pago_Pago Pacific/Honolulu America/Anchorage America/Los_Angeles \
            America/Denver America/Chicago America/New_York America/Santiago \
            America/Sao_Paulo Atlantic/Azores UTC Europe/Berlin Africa/Cairo \
            Europe/Moscow Asia/Tehran Asia/Dubai Asia/Kabul Asia/Karachi \
            Asia/Kolkata Asia/Dhaka Asia/Yangon Asia/Bangkok Asia/Shanghai \
            Australia/Perth Asia/Tokyo Australia/Adelaide Australia/Sydney \
            Pacific/Noumea Pacific/Auckland
        ;;
    *)
        echo "Usage: $0 {status|get|apply <zone>|list}"
        exit 1
        ;;
esac
