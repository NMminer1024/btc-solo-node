#!/bin/bash
# Rockbase K230_NET build-mode manager.
# Usage: rockbase_net_mode.sh <K230_NET> <BRW_BUILD_DIR> <BR_SRC_DIR> <CONF>
#
# Canonical K230_NET values (mutually exclusive):
#   sdio_8189  — RTL8189FS SDIO WiFi only
#   sdio_bl616 — BL616 SDIO WiFi placeholder (driver not wired yet)
#   usb_eth    — USB wired ethernet only
#   usb_wifi   — USB WiFi only (AIC path)
set -e

K230_NET_RAW="${1:-sdio_8189}"
BRW_BUILD_DIR="$2"
BR_SRC_DIR="$3"
CONF="$4"
NET_STAMP="${BRW_BUILD_DIR}/.k230_net_mode"

canon_mode() {
  case "$1" in
    sdio_wifi|sdio_8189) echo "sdio_8189" ;;
    sdio_bl616|sdio-bl616) echo "sdio_bl616" ;;
    usb_eth|usb-eth) echo "usb_eth" ;;
    usb_wifi|usb-wifi) echo "usb_wifi" ;;
    *) echo "sdio_8189" ;;
  esac
}

set_pkg_y() {
  local pkg="$1"
  if grep -q "^BR2_PACKAGE_${pkg}=y" "${CONFIG}"; then
    return 0
  fi
  sed -i "s/^# BR2_PACKAGE_${pkg} is not set/BR2_PACKAGE_${pkg}=y/" "${CONFIG}" || true
  if ! grep -q "^BR2_PACKAGE_${pkg}=y" "${CONFIG}"; then
    printf 'BR2_PACKAGE_%s=y\n' "${pkg}" >> "${CONFIG}"
  fi
}

unset_pkg() {
  local pkg="$1"
  sed -i "s/^BR2_PACKAGE_${pkg}=y/# BR2_PACKAGE_${pkg} is not set/" "${CONFIG}" || true
}

K230_NET="$(canon_mode "${K230_NET_RAW}")"

CONFIG="${BRW_BUILD_DIR}/.config"
prev="$(canon_mode "$(cat "${NET_STAMP}" 2>/dev/null || echo "sdio_8189")")"

# Family package switches (only these are toggled by K230_NET)
PKG_WIFI_USER="WPA_SUPPLICANT WPA_SUPPLICANT_WEXT WPA_SUPPLICANT_CLI WPA_SUPPLICANT_PASSPHRASE WIRELESS_TOOLS WIRELESS_TOOLS_LIB"
PKG_SDIO_8189="RTL8189FS"
PKG_USB_WIFI="AIC8800"
PKG_DISABLED_ALWAYS="AIC8800_SDIO RTL8733BS"
PKG_SDIO_BL616=""

if [ "${prev}" != "${K230_NET}" ]; then
    echo "[K230_NET] mode switch: ${prev} -> ${K230_NET}"
    # Re-apply defconfig to guarantee a clean baseline before overlaying.
    echo "[K230_NET] re-applying defconfig ${CONF}..."
    make -C "${BR_SRC_DIR}" "${CONF}" O="${BRW_BUILD_DIR}" >/dev/null
    # Clean WiFi package build dirs so they are rebuilt (or skipped) correctly.
        echo "[K230_NET] dirclean net-related packages from previous mode..."
        make -C "${BRW_BUILD_DIR}" rtl8189fs-dirclean wpa_supplicant-dirclean wireless_tools-dirclean \
          aic8800-dirclean aic8800_sdio-dirclean 2>/dev/null || true
    # dirclean only clears build dirs, not already-installed target files. Remove
    # stale WiFi artifacts from target so disabled packages don't leak into the image.
    echo "[K230_NET] removing stale WiFi artifacts from target..."
    TARGET="${BRW_BUILD_DIR}/target"
    find "${TARGET}/lib/modules" -name '8189fs.ko' -delete 2>/dev/null || true
    find "${TARGET}/lib/modules" -name 'aic*.ko' -delete 2>/dev/null || true
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
            rm -rf "${TARGET}/lib/firmware/aic8800D80" 2>/dev/null || true
    # Refresh module dependency files so the removed module is not referenced.
    KVER=$(ls "${TARGET}/lib/modules" 2>/dev/null | head -1)
    if [ -n "${KVER}" ] && [ -x "${BRW_BUILD_DIR}/host/sbin/depmod" ]; then
        "${BRW_BUILD_DIR}/host/sbin/depmod" -b "${TARGET}" "${KVER}" 2>/dev/null || true
    fi
fi

# Start from strict disabled baseline for all managed families.
for pkg in ${PKG_WIFI_USER} ${PKG_SDIO_8189} ${PKG_USB_WIFI} ${PKG_DISABLED_ALWAYS}; do
    unset_pkg "${pkg}"
done

case "${K230_NET}" in
  sdio_8189)
    echo "[K230_NET] overlay: exclusive mode sdio_8189"
    for pkg in ${PKG_WIFI_USER} ${PKG_SDIO_8189}; do
        set_pkg_y "${pkg}"
    done
    ;;
  sdio_bl616)
    echo "[K230_NET] overlay: exclusive mode sdio_bl616 (placeholder, no driver package yet)"
    for pkg in ${PKG_WIFI_USER}; do
        set_pkg_y "${pkg}"
    done
    ;;
  usb_eth)
    echo "[K230_NET] overlay: exclusive mode usb_eth"
    ;;
  usb_wifi)
    echo "[K230_NET] overlay: exclusive mode usb_wifi"
    for pkg in ${PKG_WIFI_USER} ${PKG_USB_WIFI}; do
        set_pkg_y "${pkg}"
    done
    ;;
esac

echo "${K230_NET}" > "${NET_STAMP}"
