#!/bin/bash

set -euo pipefail

MOUNT_POINT="${1:-/mnt/sdcard}"

if [ "$(id -u)" -ne 0 ]; then
	echo "Please run as root: sudo $0 [mount-point]" >&2
	exit 1
fi

if ! mountpoint -q "${MOUNT_POINT}"; then
	echo "Not mounted: ${MOUNT_POINT}" >&2
	exit 0
fi

umount "${MOUNT_POINT}"
echo "Unmounted ${MOUNT_POINT}"
