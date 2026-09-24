#!/bin/sh
#
# rockbase_boot_progress.sh - write boot progress for the LCD UI
#
# Usage: rockbase_boot_progress.sh <stage> <total> <pct> <color> <message>
#   stage: 0..N-1 (0=UI, 1=network, 2=statusd, 3=bitcoind, 4=ckpool, 5=ready)
#   total: total number of stages
#   pct:   0-100
#   color: #rrggbb (white=waiting, green=done, red=error)
#   message: short status text shown on the boot screen
#
# The LCD UI (rockbase-lcdui) polls /run/rockbase/boot-progress.json while
# showing the boot logo. Stage >= 2 (statusd up) means the dashboard has a
# live data source, so the UI exits the boot screen.
#
set -e

STAGE="${1:-0}"
TOTAL="${2:-6}"
PCT="${3:-0}"
COLOR="${4:-#ffffff}"
MSG="${5:-Booting...}"

DIR=/run/rockbase
mkdir -p "${DIR}"

TMP="${DIR}/boot-progress.json.tmp"
cat > "${TMP}" <<EOF
{"stage":${STAGE},"total_stages":${TOTAL},"progress":${PCT},"message":"${MSG}","color":"${COLOR}","timestamp":$(date +%s)}
EOF
mv "${TMP}" "${DIR}/boot-progress.json"
