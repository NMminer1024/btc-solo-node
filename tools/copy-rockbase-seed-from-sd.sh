#!/bin/bash

set -euo pipefail

SDK_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SOURCE_DIR_DEFAULT="/mnt/sdcard/home/sunrise/Desktop/bitcoind/data"
DEST_DIR_DEFAULT="${SDK_ROOT}/seed/root-block"

SOURCE_DIR="${1:-${SOURCE_DIR_DEFAULT}}"
DEST_DIR="${2:-${DEST_DIR_DEFAULT}}"
TMP_DIR="${DEST_DIR}.tmp"
OLD_DIR="${DEST_DIR}.old"
RESET_COPY="${RESET_SEED_COPY:-0}"

validate_manifest()
{
	local db_dir="$1"
	local label="$2"
	local current_file="${db_dir}/CURRENT"
	local manifest_file

	if [ ! -f "${current_file}" ]; then
		echo "ERROR: ${label} missing CURRENT: ${current_file}" >&2
		exit 1
	fi

	manifest_file=$(tr -d '\r\n' < "${current_file}")
	if [ -z "${manifest_file}" ]; then
		echo "ERROR: ${label} CURRENT is empty: ${current_file}" >&2
		exit 1
	fi

	if [ ! -f "${db_dir}/${manifest_file}" ]; then
		echo "ERROR: ${label} manifest missing: ${db_dir}/${manifest_file}" >&2
		exit 1
	fi
}

if ! findmnt -n /mnt/sdcard >/dev/null 2>&1; then
	echo "ERROR: /mnt/sdcard is not mounted." >&2
	exit 1
fi

if [ ! -d "${SOURCE_DIR}/blocks" ] || [ ! -d "${SOURCE_DIR}/chainstate" ]; then
	echo "ERROR: source must contain blocks/ and chainstate/: ${SOURCE_DIR}" >&2
	exit 1
fi

if [ ! -d "${SOURCE_DIR}/blocks/index" ]; then
	echo "ERROR: source must contain blocks/index/: ${SOURCE_DIR}" >&2
	exit 1
fi

validate_manifest "${SOURCE_DIR}/chainstate" "source chainstate"
validate_manifest "${SOURCE_DIR}/blocks/index" "source blocks/index"

mkdir -p "$(dirname "${DEST_DIR}")"
rm -rf "${OLD_DIR}"
if [ "${RESET_COPY}" = "1" ]; then
	rm -rf "${TMP_DIR}"
fi
mkdir -p "${TMP_DIR}"

echo "Copying Rockbase block seed:"
echo "  from: ${SOURCE_DIR}"
echo "  to:   ${DEST_DIR}"
echo "  tmp:  ${TMP_DIR}"
if [ -d "${TMP_DIR}" ] && [ -n "$(find "${TMP_DIR}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
	echo "Resuming from existing temporary copy. Set RESET_SEED_COPY=1 to restart from scratch."
fi

rsync -aH --delete --partial --append-verify --info=progress2 \
	--exclude='.lock' \
	--exclude='.cookie' \
	--exclude='bitcoind.pid' \
	--exclude='LOCK' \
	"${SOURCE_DIR}/" "${TMP_DIR}/"

validate_manifest "${TMP_DIR}/chainstate" "copied chainstate"
validate_manifest "${TMP_DIR}/blocks/index" "copied blocks/index"

if [ -e "${DEST_DIR}" ]; then
	mv "${DEST_DIR}" "${OLD_DIR}"
fi
mv "${TMP_DIR}" "${DEST_DIR}"
rm -rf "${OLD_DIR}"

du -sh "${DEST_DIR}" "${DEST_DIR}/blocks" "${DEST_DIR}/blocks/index" "${DEST_DIR}/chainstate"
echo "Rockbase block seed is ready: ${DEST_DIR}"
