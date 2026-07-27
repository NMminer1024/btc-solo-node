#!/bin/bash

set -euo pipefail

DEVICE="${1:-}"
MOUNT_POINT="${2:-/mnt/sdcard}"
MOUNT_OPTS="${MOUNT_OPTS:-ro}"

if [ "$(id -u)" -ne 0 ]; then
	echo "Please run as root: sudo $0 [device] [mount-point]" >&2
	exit 1
fi

pick_device() {
	local candidates

	candidates=$(lsblk -prno NAME,FSTYPE,MOUNTPOINT,SIZE | awk '
		$1 ~ /^\/dev\/sd[a-z][0-9]+$/ &&
		$2 == "ext4" &&
		$3 != "/" &&
		$3 !~ /^\/mnt\/wsl/ &&
		$3 !~ /^\/home\// &&
		$3 !~ /^\/tmp$/ {
			print $1 "|" $4 "|" $3
		}
	')

	if [ -z "${candidates}" ]; then
		echo "No ext4 SD-card candidate found. Attach the reader to WSL first." >&2
		exit 1
	fi

	if [ "$(printf '%s\n' "${candidates}" | wc -l)" -gt 1 ]; then
		echo "Multiple ext4 block devices found. Pass the device explicitly." >&2
		printf '%s\n' "${candidates}" | sed 's/|/\t/g' >&2
		exit 1
	fi

	printf '%s\n' "${candidates%%|*}"
}

if [ -z "${DEVICE}" ]; then
	if mountpoint -q "${MOUNT_POINT}"; then
		findmnt -no SOURCE,TARGET,FSTYPE,OPTIONS "${MOUNT_POINT}"
		exit 0
	fi
	DEVICE="$(pick_device)"
fi

if [ ! -b "${DEVICE}" ]; then
	echo "Block device not found: ${DEVICE}" >&2
	exit 1
fi

mkdir -p "${MOUNT_POINT}"

if findmnt -rn -S "${DEVICE}" >/dev/null 2>&1; then
	findmnt -rn -S "${DEVICE}" -o SOURCE,TARGET,FSTYPE,OPTIONS
	exit 0
fi

mount -o "${MOUNT_OPTS}" "${DEVICE}" "${MOUNT_POINT}"
findmnt -no SOURCE,TARGET,FSTYPE,OPTIONS "${MOUNT_POINT}"
