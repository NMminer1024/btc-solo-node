#!/bin/bash
#
# ota-push.sh - One-shot OTA push: package current 1G build, serve it over
#               HTTP from this WSL host, and trigger a real apply on the board.
#
# Usage:
#   tools/ota-push.sh <board_ip> [version]
#
# Example:
#   tools/ota-push.sh 192.168.124.30
#   tools/ota-push.sh 192.168.124.30 v0.1.0-rc1
#
# Notes:
# - Slot image: output/k230_rockbase_1g_defconfig/images/rootfs.ext2
#   (build first with the rockbase-build skill if you need fresh code in it).
# - Signing key lives OUTSIDE the repo at ~/.rockbase-ota-keys/ (private key
#   must never be committed). Auto-generated on first use; the pubkey is then
#   auto-deployed to the board's /etc/rockbase-ota-pub.pem if it differs.
# - The board reboots into the new slot on success. SSH host key WILL change
#   (the other slot has its own /etc/ssh) — the script de-dups known_hosts.
# - apply runs under nohup on the board: a dying SSH session must never kill
#   a half-finished dd (E2 lesson, 2026-08-24).

set -euo pipefail

PROG=$(basename "$0")
die() { echo "ERROR: $*" >&2; exit 1; }

BOARD=${1:-}
VERSION=${2:-}
[ -n "$BOARD" ] || die "usage: $PROG <board_ip> [version]"

SDK_ROOT=$(cd "$(dirname "$0")/.." && pwd)
SLOT_IMG="${SLOT_IMG:-$SDK_ROOT/output/k230_rockbase_1g_defconfig/images/rootfs.ext2}"
KEY_DIR="$HOME/.rockbase-ota-keys"
PRIVKEY="$KEY_DIR/ota-private.pem"
PUBKEY="$KEY_DIR/ota-pub.pem"
DIST_ROOT="$HOME/ota-dist"
HTTP_PORT=8000
HTTP_PIDFILE="$DIST_ROOT/.http.pid"

[ -r "$SLOT_IMG" ] || die "slot image missing: $SLOT_IMG (build first: see rockbase-build skill)"

# version default: from the image's own release_version git hash + timestamp
if [ -z "$VERSION" ]; then
    build_id=$(debugfs -R 'cat /etc/version/release_version' "$SLOT_IMG" 2>/dev/null \
               | sed -n 's/^sdk:[^-]*-\([0-9a-f-]*\)-.*/\1/p' | head -1)
    VERSION="v${build_id:-$(date +%Y%m%d-%H%M%S)}-ota"
fi

SSH="ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 root@$BOARD"

echo "==> board   : $BOARD"
echo "==> version : $VERSION"
echo "==> slot img: $SLOT_IMG"

# --- 1. signing keypair (durable, outside repo) -------------------------------
if [ ! -f "$PRIVKEY" ]; then
    echo "==> generating OTA keypair in $KEY_DIR"
    mkdir -p "$KEY_DIR" && chmod 700 "$KEY_DIR"
    openssl genrsa -out "$PRIVKEY" 2048 2>/dev/null
    openssl rsa -in "$PRIVKEY" -pubout -out "$PUBKEY" 2>/dev/null
fi

# board pubkey must match our signing key
remote_md5=$($SSH 'md5sum /etc/rockbase-ota-pub.pem 2>/dev/null | cut -d" " -f1' || true)
local_md5=$(md5sum "$PUBKEY" | cut -d' ' -f1)
if [ "$remote_md5" != "$local_md5" ]; then
    echo "==> deploying OTA pubkey to board (/etc/rockbase-ota-pub.pem)"
    scp -o StrictHostKeyChecking=no "$PUBKEY" "root@$BOARD:/etc/rockbase-ota-pub.pem" >/dev/null
fi

# --- 2. WSL host IP reachable from the board ----------------------------------
HOST_IP=$(ip -4 route get "$BOARD" 2>/dev/null | sed -n 's/.*src \([0-9.]*\).*/\1/p' | head -1)
[ -n "$HOST_IP" ] || die "cannot determine host IP route to $BOARD"
BASE_URL="http://$HOST_IP:$HTTP_PORT/$VERSION"
echo "==> base url: $BASE_URL"

# --- 3. package ----------------------------------------------------------------
"$SDK_ROOT/tools/mk-ota-package.sh" \
    --version "$VERSION" \
    --key "$PRIVKEY" \
    --slot-img "$SLOT_IMG" \
    --base-url "$BASE_URL" \
    --out-dir "$DIST_ROOT/$VERSION"

# --- 4. HTTP server (idempotent, PID file; do NOT pkill -f, it kills your own shell) ---
mkdir -p "$DIST_ROOT"
server_alive() { [ -f "$HTTP_PIDFILE" ] && kill -0 "$(cat "$HTTP_PIDFILE")" 2>/dev/null; }
if ! server_alive; then
    echo "==> starting HTTP server on :$HTTP_PORT (root $DIST_ROOT)"
    ( cd "$DIST_ROOT" && nohup python3 -m http.server "$HTTP_PORT" --bind 0.0.0.0 \
        > "$DIST_ROOT/http.log" 2>&1 & echo $! > "$HTTP_PIDFILE" )
    sleep 1
fi
server_alive || die "HTTP server failed to start (see $DIST_ROOT/http.log)"
curl -sfI "$BASE_URL/manifest.json" >/dev/null || die "manifest not reachable at $BASE_URL"

# --- 5. trigger apply on the board ---------------------------------------------
ssh-keygen -R "$BOARD" >/dev/null 2>&1 || true
echo "==> board pre-state:"
$SSH 'rockbase-ota.sh status | sed -n "2,5p"'

echo "==> triggering apply --force (nohup, survives ssh drop)"
$SSH "nohup rockbase-ota.sh apply $BASE_URL/manifest.json --force \
      > /root/block/ota/apply-$VERSION.log 2>&1 & echo started pid \$!"

cat <<EOF

OTA push initiated. The board will:
  quiesce services -> download+verify -> dd to inactive slot -> switch -> reboot
Monitor with:
  ssh root@$BOARD 'tail -f /root/block/ota/ota.log'
After the reboot, verify with the rockbase-ota skill checklist.
EOF
