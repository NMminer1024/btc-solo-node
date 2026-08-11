#!/bin/bash

set -e

SDK_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SEED_DIR_DEFAULT="${SDK_ROOT}/seed/root-block"
SEED_DIR="${1:-${SEED_DIR_DEFAULT}}"

mkdir -p "${SEED_DIR}"

cat <<EOF
Rockbase seed workspace initialized:

  seed: ${SEED_DIR}

Expected active seed path:

  ${SEED_DIR}

Suggested next step after mounting the source SD card in WSL:

  tools/copy-rockbase-seed-from-sd.sh

If the source data is not at the default X5 path, pass it explicitly:

  tools/copy-rockbase-seed-from-sd.sh /mnt/sdcard/root/block

One-build override example:

  BLOCK_SEED_DIR="${SEED_DIR}" make buildroot
EOF
