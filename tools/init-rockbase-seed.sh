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

Suggested next step to sync from your long-running seed device:

  tools/sync-rockbase-seed-from-device.sh

The sync script asks for remote host/user/password and remote block directory interactively.

You can also pre-set local destination for one run:

  DEST_DIR="${SEED_DIR}" tools/sync-rockbase-seed-from-device.sh

One-build override example:

  BLOCK_SEED_DIR="${SEED_DIR}" make buildroot
EOF
