#!/bin/bash

set -e

SEED_ROOT_DEFAULT="/home/bitpony/rockbase-seed"
SEED_ROOT="${1:-${SEED_ROOT_DEFAULT}}"
CURRENT_DIR="${SEED_ROOT}/current"
RELEASES_DIR="${SEED_ROOT}/releases"
STAGING_DIR="${SEED_ROOT}/staging"

mkdir -p "${CURRENT_DIR}" "${RELEASES_DIR}" "${STAGING_DIR}"

cat <<EOF
Rockbase seed workspace initialized:

  root:     ${SEED_ROOT}
  current:  ${CURRENT_DIR}
  releases: ${RELEASES_DIR}
  staging:  ${STAGING_DIR}

Expected active seed path:

  ${CURRENT_DIR}/root-block

Suggested next step after mounting the source SD card in WSL:

  ts=\$(date +%Y%m%d-%H%M%S)
  mkdir -p "${RELEASES_DIR}/\${ts}/root-block"
  rsync -aH --info=progress2 /mnt/sdcard/root/block/ "${RELEASES_DIR}/\${ts}/root-block/"
  rm -rf "${CURRENT_DIR}/root-block"
  cp -a "${RELEASES_DIR}/\${ts}/root-block" "${CURRENT_DIR}/root-block"

One-build override example:

  BLOCK_SEED_DIR="${CURRENT_DIR}/root-block" make buildroot
EOF
