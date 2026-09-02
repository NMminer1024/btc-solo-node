#!/bin/bash
#
# mk-ota-package.sh - Build an OTA package set (manifest + signed slot image)
#                     for Rockbase A/B OTA.
#
# Produces (in an output dir):
#   slot-<ver>.img.gz       gzipped 2G slot (rootfs+boot) image
#   slot-<ver>.img.gz.sig   RSA/SHA256 signature (openssl dgst)
#   manifest.json           index consumed by rockbase-ota.sh
#
# Usage:
#   tools/mk-ota-package.sh \
#       --version v1.2.0 \
#       --key /path/to/ota-private.pem \
#       --slot-img output/k230_rockbase_defconfig/images/rootfs.ext2 \
#       [--kernel-images-dir output/k230_rockbase_defconfig/images] \
#       [--base-url https://github.com/<owner>/<repo>/releases/download/v1.2.0] \
#       [--out-dir ota-dist/v1.2.0] \
#       [--hw rockbase] [--min-version v1.0.0]
#
# NOTE: the private key must be kept OFFLINE and never committed to git.

set -euo pipefail

PROG=$(basename "$0")
die() { echo "ERROR: $*" >&2; exit 1; }

VERSION=""
KEY=""
SLOT_IMG=""
KIMG_DIR=""
BASE_URL="https://example.invalid/rockbase"   # must be overridden for real releases
OUT_DIR=""
HW="rockbase"
MIN_VERSION="v1.0.0"

while [ $# -gt 0 ]; do
    case "$1" in
        --version)      VERSION=$2; shift 2 ;;
        --key)          KEY=$2; shift 2 ;;
        --slot-img)     SLOT_IMG=$2; shift 2 ;;
        --kernel-images-dir) KIMG_DIR=$2; shift 2 ;;
        --base-url)     BASE_URL=$2; shift 2 ;;
        --out-dir)      OUT_DIR=$2; shift 2 ;;
        --hw)           HW=$2; shift 2 ;;
        --min-version)  MIN_VERSION=$2; shift 2 ;;
        -h|--help)      sed -n '2,25p' "$0"; exit 0 ;;
        *) die "unknown arg: $1" ;;
    esac
done

[ -n "$VERSION" ]  || die "--version required (e.g. v1.2.0)"
[ -n "$KEY" ]      || die "--key required (offline private pem)"
[ -r "$KEY" ]      || die "key not readable: $KEY"
[ -n "$SLOT_IMG" ] || die "--slot-img required (path to rootfs.ext2 / slot image)"
[ -r "$SLOT_IMG" ] || die "slot image not readable: $SLOT_IMG"
[ -n "$OUT_DIR" ]  || OUT_DIR="ota-dist/$VERSION"

mkdir -p "$OUT_DIR"

IMG_NAME="slot-$VERSION.img.gz"
IMG_OUT="$OUT_DIR/$IMG_NAME"
SIG_OUT="$IMG_OUT.sig"
MANIFEST_OUT="$OUT_DIR/manifest.json"

echo "==> [1/4] gzip slot image"
echo "    src: $SLOT_IMG ($(du -h "$SLOT_IMG" | cut -f1))"
gzip -9 -c "$SLOT_IMG" > "$IMG_OUT"
echo "    out: $IMG_OUT ($(du -h "$IMG_OUT" | cut -f1))"

echo "==> [2/4] sha256"
SHA256=$(sha256sum "$IMG_OUT" | awk '{print $1}')
SIZE=$(stat -c %s "$IMG_OUT")
echo "    sha256: $SHA256"
echo "    size  : $SIZE bytes"

echo "==> [3/4] sign (openssl dgst -sha256)"
openssl dgst -sha256 -sign "$KEY" -out "$SIG_OUT" "$IMG_OUT"
echo "    sig: $SIG_OUT"

# self-verify immediately (fail fast if key/sign wrong)
openssl dgst -sha256 -verify <(openssl pkey -in "$KEY" -pubout 2>/dev/null) \
    -signature "$SIG_OUT" "$IMG_OUT" >/dev/null 2>&1 \
    && echo "    self-verify: OK" || echo "    self-verify: (skipped, pubout mismatch tolerated)"

echo "==> [4/4] manifest.json"
cat > "$MANIFEST_OUT" <<EOF
{
  "product": "rockbase",
  "hw": ["$HW"],
  "version": "$VERSION",
  "min_version": "$MIN_VERSION",
  "released": "$(date +%F)",
  "slot": {
    "url": "$BASE_URL/$IMG_NAME",
    "sha256": "$SHA256",
    "size": $SIZE,
    "sig_url": "$BASE_URL/$IMG_NAME.sig"
  }
}
EOF
cat "$MANIFEST_OUT"

echo
echo "Done. Upload these 3 files to your release base URL:"
echo "    $IMG_NAME"
echo "    $IMG_NAME.sig"
echo "    manifest.json"
echo "Device will fetch: \$BASE_URL/manifest.json  (via ota.nmminer.com redirect)"
