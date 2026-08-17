#!/bin/bash
# Rockbase K230_NET build-mode manager.
# Usage: rockbase_net_mode.sh <K230_NET> <BRW_BUILD_DIR> <BR_SRC_DIR> <CONF>
#
# Canonical K230_NET values (mutually exclusive):
#   sdio_wifi  — SDIO WiFi (RTL8189FS) only [default]
#   usb        — USB network (wired + WiFi) only
set -e

K230_NET_RAW="${1:-sdio_wifi}"
BRW_BUILD_DIR="$2"
BR_SRC_DIR="$3"
CONF="$4"
NET_STAMP="${BRW_BUILD_DIR}/.k230_net_mode"

canon_mode() {
  case "$1" in
    sdio_wifi|sdio_8189|sdio) echo "sdio_wifi" ;;
    usb|usb_eth|usb-eth|usb_wifi|usb-wifi) echo "usb" ;;
    *) echo "sdio_wifi" ;;
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
prev="$(cat "${NET_STAMP}" 2>/dev/null || echo "sdio_wifi")"
prev="$(canon_mode "${prev}")"

# Family package switches (only these are toggled by K230_NET)
PKG_WIFI_USER="WPA_SUPPLICANT WPA_SUPPLICANT_WEXT WPA_SUPPLICANT_CLI WPA_SUPPLICANT_PASSPHRASE WIRELESS_TOOLS WIRELESS_TOOLS_LIB"
PKG_SDIO_8189="RTL8189FS"
PKG_DISABLED_ALWAYS="RTL8733BS"

if [ "${prev}" != "${K230_NET}" ]; then
  echo "[K230_NET] mode switch: ${prev} -> ${K230_NET}"
  # Re-apply defconfig to guarantee a clean baseline before overlaying.
  echo "[K230_NET] re-applying defconfig ${CONF}..."
  make -C "${BR_SRC_DIR}" "${CONF}" O="${BRW_BUILD_DIR}" >/dev/null
  # Clean WiFi package build dirs so they are rebuilt (or skipped) correctly.
  echo "[K230_NET] dirclean net-related packages from previous mode..."
  make -C "${BRW_BUILD_DIR}" rtl8189fs-dirclean wpa_supplicant-dirclean wireless_tools-dirclean 2>/dev/null || true
  # Remove stale artifacts from target so disabled packages don't leak into the image.
  echo "[K230_NET] removing stale artifacts from target..."
  TARGET="${BRW_BUILD_DIR}/target"
  if [ "${K230_NET}" = "usb" ]; then
    # USB mode: remove SDIO WiFi driver (not needed)
    find "${TARGET}/lib/modules" -name '8189fs.ko' -delete 2>/dev/null || true
  fi
  # Note: wpa_supplicant + wireless_tools are needed in BOTH modes (SDIO WiFi
  # and USB WiFi both require them). No removal needed when switching to sdio_wifi.
  # Clean up BL616 leftovers (legacy)
  find "${TARGET}/lib/modules" -name 'mr_sdio.ko' -delete 2>/dev/null || true
  rm -f "${TARGET}/usr/sbin/bflbwifid" \
    "${TARGET}/usr/bin/bflbwifictrl" \
    "${TARGET}/etc/init.d/S38bl616wifi" 2>/dev/null || true
  # Refresh module dependency files
  KVER=$(ls "${TARGET}/lib/modules" 2>/dev/null | head -1)
  if [ -n "${KVER}" ] && [ -x "${BRW_BUILD_DIR}/host/sbin/depmod" ]; then
    "${BRW_BUILD_DIR}/host/sbin/depmod" -b "${TARGET}" "${KVER}" 2>/dev/null || true
  fi
fi

# Start from strict disabled baseline for all managed families.
for pkg in ${PKG_WIFI_USER} ${PKG_SDIO_8189} ${PKG_DISABLED_ALWAYS}; do
    unset_pkg "${pkg}"
done

case "${K230_NET}" in
  sdio_wifi)
    echo "[K230_NET] overlay: sdio_wifi (RTL8189FS)"
    for pkg in ${PKG_WIFI_USER} ${PKG_SDIO_8189}; do
        set_pkg_y "${pkg}"
    done
    ;;
  usb)
    echo "[K230_NET] overlay: usb (wired + WiFi)"
    for pkg in ${PKG_WIFI_USER}; do
        set_pkg_y "${pkg}"
    done
    ;;
esac

echo "${K230_NET}" > "${NET_STAMP}"
