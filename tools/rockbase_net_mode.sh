#!/bin/bash
# Rockbase K230_NET build-mode manager.
# Usage: rockbase_net_mode.sh <K230_NET> <BRW_BUILD_DIR> <BR_SRC_DIR> <CONF>
#
# K230_NET values:
#   sdio_wifi  — RTL8189FS SDIO WiFi (default; matches defconfig baseline)
#   usb_eth    — USB wired ethernet only; WiFi packages excluded
#   usb_wifi   — USB wireless (placeholder; WiFi packages excluded until driver ready)
set -e

K230_NET="${1:-sdio_wifi}"
BRW_BUILD_DIR="$2"
BR_SRC_DIR="$3"
CONF="$4"
NET_STAMP="${BRW_BUILD_DIR}/.k230_net_mode"

# All WiFi-related Buildroot package config keys (parent + sub-options)
WIFI_PKGS="RTL8189FS WPA_SUPPLICANT WPA_SUPPLICANT_WEXT WPA_SUPPLICANT_CLI \
           WPA_SUPPLICANT_PASSPHRASE WIRELESS_TOOLS WIRELESS_TOOLS_LIB"

CONFIG="${BRW_BUILD_DIR}/.config"
prev=$(cat "${NET_STAMP}" 2>/dev/null || echo "sdio_wifi")

if [ "${prev}" != "${K230_NET}" ]; then
    echo "[K230_NET] mode switch: ${prev} -> ${K230_NET}"
    # Re-apply defconfig to guarantee a clean baseline before overlaying.
    echo "[K230_NET] re-applying defconfig ${CONF}..."
    make -C "${BR_SRC_DIR}" "${CONF}" O="${BRW_BUILD_DIR}" >/dev/null
    # Clean WiFi package build dirs so they are rebuilt (or skipped) correctly.
    echo "[K230_NET] dirclean WiFi packages from previous mode..."
    make -C "${BRW_BUILD_DIR}" rtl8189fs-dirclean wpa_supplicant-dirclean wireless_tools-dirclean \
         2>/dev/null || true
    # dirclean only clears build dirs, not already-installed target files. Remove
    # stale WiFi artifacts from target so disabled packages don't leak into the image.
    echo "[K230_NET] removing stale WiFi artifacts from target..."
    TARGET="${BRW_BUILD_DIR}/target"
    find "${TARGET}/lib/modules" -name '8189fs.ko' -delete 2>/dev/null || true
    rm -f "${TARGET}/usr/sbin/wpa_supplicant" \
          "${TARGET}/usr/sbin/wpa_cli" \
          "${TARGET}/usr/sbin/wpa_passphrase" \
          "${TARGET}/etc/wpa_supplicant.conf" \
          "${TARGET}/sbin/iwconfig" \
          "${TARGET}/sbin/iwlist" \
          "${TARGET}/sbin/iwpriv" \
          "${TARGET}/sbin/iwspy" \
          "${TARGET}/sbin/iwgetid" \
          "${TARGET}/lib/libiw.so" 2>/dev/null || true
    rm -f "${TARGET}"/lib/libiw.so.* 2>/dev/null || true
    # Refresh module dependency files so the removed module is not referenced.
    KVER=$(ls "${TARGET}/lib/modules" 2>/dev/null | head -1)
    if [ -n "${KVER}" ] && [ -x "${BRW_BUILD_DIR}/host/sbin/depmod" ]; then
        "${BRW_BUILD_DIR}/host/sbin/depmod" -b "${TARGET}" "${KVER}" 2>/dev/null || true
    fi
fi

echo "${K230_NET}" > "${NET_STAMP}"

case "${K230_NET}" in
  usb_eth|usb_wifi)
    echo "[K230_NET] overlay: disabling WiFi packages for ${K230_NET}"
    for pkg in ${WIFI_PKGS}; do
        sed -i "s/^BR2_PACKAGE_${pkg}=y/# BR2_PACKAGE_${pkg} is not set/" "${CONFIG}"
    done
    ;;
  sdio_wifi)
    # sdio_wifi is the defconfig default — overlay not needed.
    ;;
  *)
    echo "[K230_NET] WARNING: unknown mode '${K230_NET}', falling back to sdio_wifi" >&2
    ;;
esac
