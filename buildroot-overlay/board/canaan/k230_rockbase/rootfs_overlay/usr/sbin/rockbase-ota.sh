#!/bin/sh
#
# rockbase-ota.sh - Rockbase A/B OTA upgrade agent (shell)
#
# Subcommands:
#   status                 Show current slot / pending state / last error
#   check [url]            Fetch manifest.json and report pending version
#   apply <manifest_url>   Download -> verify sig+sha256 -> dd to inactive slot -> switch
#   confirm                Mark current slot good (clear bootcount/upgrade_available)
#   rollback               Manually switch back to the previous slot and reboot
#
# Design notes:
# - Only ever writes the INACTIVE slot, so an interrupted apply never bricks the
#   running system. Slot switch happens via uboot env `ota_part`.
# - A/B root= switching relies on board_fdt_chosen_bootargs() honoring env
#   `bootargs`; the blinux bootcmd sets root=/dev/mmcblkXp$ota_part.
# - All downloaded artifacts live under the blockdata partition (large, persistent).
# - Default is SAFE: apply requires --force to actually dd+reboot; otherwise dry-run.
#
# This is the A3-stage skeleton: download + verify path is real; the dd/switch
# path is gated behind --force and will be exercised in stage D after the new
# A/B partition layout exists.

set -u

PROG=$(basename "$0")
PUBKEY=${OTA_PUBKEY:-/etc/rockbase-ota-pub.pem}
WORKDIR=${OTA_WORKDIR:-/root/block/ota}
MANIFEST_NAME=manifest.json
LOGFILE=${OTA_LOG:-$WORKDIR/ota.log}

# Live state file consumed by lcdui (full-screen OTA overlay) and web.
# Lives in /run (tmpfs): a reboot wipes it, so the overlay lifecycle is
# strictly tied to the apply run - no stale state after reboot.
STATE_FILE=${OTA_STATE_FILE:-/run/rockbase/ota-state.json}
STATE_ENABLED=0   # only a real apply (--force) drives the overlay
APPLY_LOCKDIR=${OTA_APPLY_LOCKDIR:-/run/rockbase/ota-apply.lock}

# set_state <phase> <progress 0-100|-1 indeterminate> <message> [speed_bps]
set_state() {
    [ "$STATE_ENABLED" = "1" ] || return 0
    local phase=$1 progress=$2 msg speed_bps=${4:--1} safe_msg
    # strip characters that would break JSON (busybox-friendly)
    safe_msg=$(printf '%s' "$3" | tr -d '"\\' | tr '\n' ' ')
    mkdir -p /run/rockbase 2>/dev/null
    printf '{"phase":"%s","progress":%s,"message":"%s","speed_bps":%s,"ts":%s}\n' \
        "$phase" "$progress" "$safe_msg" "$speed_bps" "$(date +%s)" > "$STATE_FILE.tmp" 2>/dev/null
    mv "$STATE_FILE.tmp" "$STATE_FILE" 2>/dev/null
}

# uboot env keys
ENV_SLOT=ota_part
ENV_UPGRADE=upgrade_available

log() {
    mkdir -p "$WORKDIR" 2>/dev/null
    local line
    line="[$(date '+%F %T')] $*"
    echo "$line" | tee -a "$LOGFILE" >&2
    [ -w /dev/console ] && echo "$line" > /dev/console
}

die() {
    log "ERROR: $*"
    set_state failed -1 "Upgrade failed: $*"
    ota_restore
    exit 1
}

have() { command -v "$1" >/dev/null 2>&1; }

require() {
    for c in "$@"; do
        have "$c" || die "missing required tool: $c"
    done
}

file_size_bytes() {
    local n
    n=$(wc -c < "$1" 2>/dev/null) || n=0
    echo "${n:-0}"
}

acquire_apply_lock() {
    local old_pid
    mkdir -p /run/rockbase 2>/dev/null
    if mkdir "$APPLY_LOCKDIR" 2>/dev/null; then
        echo $$ > "$APPLY_LOCKDIR/pid" 2>/dev/null || true
        trap 'rm -rf "$APPLY_LOCKDIR"' EXIT INT TERM
        return 0
    fi
    old_pid=$(cat "$APPLY_LOCKDIR/pid" 2>/dev/null || true)
    if [ -n "$old_pid" ] && kill -0 "$old_pid" 2>/dev/null; then
        die "another OTA apply is already running (pid=$old_pid)"
    fi
    rm -rf "$APPLY_LOCKDIR" 2>/dev/null || true
    mkdir "$APPLY_LOCKDIR" 2>/dev/null || die "cannot acquire OTA apply lock"
    echo $$ > "$APPLY_LOCKDIR/pid" 2>/dev/null || true
    trap 'rm -rf "$APPLY_LOCKDIR"' EXIT INT TERM
}

# --- service quiesce ---------------------------------------------------------
# Lesson from E2 (2026-08-24): OTA flashing is a long, heavy SD write.
#  1) supervisord resurrects bitcoind mid-flash (SD IO contention returns)
#  2) serial healthd sees the expected long MMC host claims during dd and
#     false-positives a freeze -> sysrq crash -> reboot mid-flash
# So before a real apply: stop the resurrector FIRST, then all SD/network
# heavy services, and pause software/hardware watchdog helpers for the duration.
# Kept running on purpose: lcdui (LCD shows OTA progress), httpd (web
# progress polling), network, sshd.
OTA_QUIESCE_SERVICES="S45supervisord S62ckpool S61bitcoind S64miners-history S63miners-cache S43statusd S04serialhealth S05watchdog"
OTA_QUIESCED=0

kill_cmdline_matches() {
    local pattern=$1 sig=${2:-} proc_dir pid cmdline
    for proc_dir in /proc/[0-9]*; do
        [ -r "$proc_dir/cmdline" ] || continue
        pid=${proc_dir##*/}
        [ "$pid" = "$$" ] && continue
        cmdline=$(tr '\000' ' ' <"$proc_dir/cmdline" 2>/dev/null || true)
        case "$cmdline" in
            *"$pattern"*) kill $sig "$pid" >/dev/null 2>&1 || true ;;
        esac
    done
}

stop_init_bounded() {
    local s=$1 pid waited=0 limit=8
    [ -x "/etc/init.d/$s" ] || return 0
    "/etc/init.d/$s" stop >/dev/null 2>&1 &
    pid=$!
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$waited" -ge "$limit" ]; then
            log "WARN: /etc/init.d/$s stop timed out; continuing OTA quiesce"
            kill "$pid" >/dev/null 2>&1 || true
            sleep 1
            kill -9 "$pid" >/dev/null 2>&1 || true
            break
        fi
        sleep 1
        waited=$((waited + 1))
    done
    wait "$pid" >/dev/null 2>&1 || true
}

ota_quiesce() {
    log "quiescing services for OTA (keeping lcdui/httpd/network/sshd up)"
    kill_cmdline_matches rockbase_supervisord.sh
    kill_cmdline_matches rockbase_statusd.sh
    kill_cmdline_matches rockbase_miners_cache.sh
    kill_cmdline_matches rockbase_miners_historyd.sh
    kill_cmdline_matches rockbase_serial_healthd.sh
    kill_cmdline_matches /etc/init.d/S43statusd
    kill_cmdline_matches /etc/init.d/S61bitcoind
    kill_cmdline_matches /root/bitcoind/stop.sh
    kill_cmdline_matches "watchdog -T"
    killall rockbase_serial_healthd.sh >/dev/null 2>&1 || true
    killall watchdog >/dev/null 2>&1 || true
    killall bitcoind >/dev/null 2>&1 || true
    killall ckpool >/dev/null 2>&1 || true
    sleep 1
    kill_cmdline_matches rockbase_supervisord.sh -9
    kill_cmdline_matches rockbase_statusd.sh -9
    kill_cmdline_matches rockbase_miners_cache.sh -9
    kill_cmdline_matches rockbase_miners_historyd.sh -9
    kill_cmdline_matches rockbase_serial_healthd.sh -9
    kill_cmdline_matches /etc/init.d/S43statusd -9
    kill_cmdline_matches /etc/init.d/S61bitcoind -9
    kill_cmdline_matches /root/bitcoind/stop.sh -9
    kill_cmdline_matches "watchdog -T" -9
    killall -9 bitcoind >/dev/null 2>&1 || true
    killall -9 ckpool >/dev/null 2>&1 || true
    OTA_QUIESCED=1
}

# restore in reverse stop order (supervisord last, after its daemons are up)
ota_restore() {
    [ "$OTA_QUIESCED" = "1" ] || return 0
    local s rev=""
    for s in $OTA_QUIESCE_SERVICES; do rev="$s $rev"; done
    log "restoring services after OTA failure"
    for s in $rev; do
        [ -x "/etc/init.d/$s" ] && "/etc/init.d/$s" start >/dev/null 2>&1 || true
    done
    OTA_QUIESCED=0
}

# --- environment / device helpers -------------------------------------------

# Root device, e.g. /dev/mmcblk1p2 -> boot base /dev/mmcblk1
root_dev() {
    sed -n 's#.*root=\(/dev/mmcblk[0-9]p[0-9]\+\).*#\1#p' /proc/cmdline
}

boot_dev() {
    local rd; rd=$(root_dev)
    [ -n "$rd" ] || return 1
    printf '%s\n' "${rd%p*}"
}

# Current running slot partition number (2 in the legacy single-rootfs layout,
# 1/2 once the A/B layout lands). Fall back to parsing root= if env missing.
current_slot() {
    local s
    s=$(fw_printenv -n "$ENV_SLOT" 2>/dev/null || true)
    if [ -n "$s" ]; then
        printf '%s\n' "$s"
    else
        root_dev | sed 's/.*p//'
    fi
}

inactive_slot() {
    local cur; cur=$(current_slot)
    case "$cur" in
        1) echo 2 ;;
        2) echo 1 ;;
        *) die "cannot determine inactive slot from current=$cur" ;;
    esac
}

disk_written_sectors() {
    local part=$1
    awk -v part="$part" '$3 == part { print $10; found = 1 } END { exit found ? 0 : 1 }' /proc/diskstats
}

# --- manifest parsing (busybox-friendly, no jq) ------------------------------
# Extract a top-level string field: "key": "value"
mj_str() {
    sed -n 's/.*"'"$1"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$2" | head -1
}
# Extract a numeric (or string) field possibly without quotes
mj_num() {
    sed -n 's/.*"'"$1"'"[[:space:]]*:[[:space:]]*\([0-9]\+\).*/\1/p' "$2" | head -1
}

# download with resume; if the server lacks Range support (curl err 33),
# fall back to a clean full download. GitHub Releases supports Range; simple
# test servers (python http.server) do not.
# Optional 3rd arg: expected total bytes -> report download progress to STATE_FILE.
fetch() {
    local out=$1 url=$2 total=${3:-0}
    [ "$total" -gt 0 ] 2>/dev/null && set_state downloading 0 "Downloading firmware"
    curl --cacert /etc/ssl/certs/ca-certificates.crt \
         --retry 5 -C - -fSL -o "$out" "$url" &
    local pid=$! cur now last_time last_bytes speed_bps elapsed pct next_report mib_cur mib_total
    last_time=$(date +%s)
    last_bytes=$(file_size_bytes "$out")
    next_report=5
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$total" -gt 0 ] 2>/dev/null; then
            cur=$(file_size_bytes "$out")
            now=$(date +%s)
            elapsed=$((now - last_time))
            speed_bps=-1
            if [ "$elapsed" -gt 0 ] && [ "$cur" -ge "$last_bytes" ] 2>/dev/null; then
                speed_bps=$(((cur - last_bytes) / elapsed))
            fi
            last_time=$now
            last_bytes=$cur
            if [ "$cur" -gt 0 ] 2>/dev/null; then
                pct=$((cur * 100 / total))
                [ "$pct" -gt 100 ] && pct=100
                set_state downloading "$pct" "Downloading firmware" "$speed_bps"
                if [ "$pct" -ge "$next_report" ]; then
                    mib_cur=$((cur / 1048576))
                    mib_total=$((total / 1048576))
                    log "download progress: ${pct}% (${mib_cur}/${mib_total} MiB)"
                    next_report=$((pct + 5))
                fi
            fi
        fi
        sleep 2
    done
    if wait "$pid"; then
        [ "$total" -gt 0 ] 2>/dev/null && set_state downloading 100 "Download complete" 0
        return 0
    fi
    log "resume failed (server may lack Range); retrying full download"
    rm -f "$out"
    curl --cacert /etc/ssl/certs/ca-certificates.crt \
         --retry 5 -fSL -o "$out" "$url" &
    pid=$!
    last_time=$(date +%s)
    last_bytes=0
    next_report=5
    while kill -0 "$pid" 2>/dev/null; do
        if [ "$total" -gt 0 ] 2>/dev/null; then
            cur=$(file_size_bytes "$out")
            now=$(date +%s)
            elapsed=$((now - last_time))
            speed_bps=-1
            if [ "$elapsed" -gt 0 ] && [ "$cur" -ge "$last_bytes" ] 2>/dev/null; then
                speed_bps=$(((cur - last_bytes) / elapsed))
            fi
            last_time=$now
            last_bytes=$cur
            if [ "$cur" -gt 0 ] 2>/dev/null; then
                pct=$((cur * 100 / total))
                [ "$pct" -gt 100 ] && pct=100
                set_state downloading "$pct" "Downloading firmware" "$speed_bps"
                if [ "$pct" -ge "$next_report" ]; then
                    mib_cur=$((cur / 1048576))
                    mib_total=$((total / 1048576))
                    log "download progress: ${pct}% (${mib_cur}/${mib_total} MiB)"
                    next_report=$((pct + 5))
                fi
            fi
        fi
        sleep 2
    done
    wait "$pid"
}

# --- subcommands --------------------------------------------------------------

cmd_status() {
    local cur inc up
    cur=$(current_slot)
    inc=$(inactive_slot 2>/dev/null || echo "?")
    up=$(fw_printenv -n "$ENV_UPGRADE" 2>/dev/null || echo "?")
    echo "product        : rockbase"
    echo "current slot   : $cur"
    echo "inactive slot  : $inc"
    echo "upgrade_pending: $up"
    echo "bootdev        : $(boot_dev)"
    echo "rootdev        : $(root_dev)"
    echo "workdir        : $WORKDIR"
    echo "pubkey         : $PUBKEY $([ -r "$PUBKEY" ] && echo '(present)' || echo '(MISSING)')"
    echo "logfile        : $LOGFILE"
}

# check <manifest_url>: fetch manifest and report
cmd_check() {
    local url=${1:-}
    [ -n "$url" ] || die "usage: $PROG check <manifest_url>"
    require curl
    mkdir -p "$WORKDIR"
    local mf="$WORKDIR/$MANIFEST_NAME"
    log "fetching manifest: $url"
    curl --cacert /etc/ssl/certs/ca-certificates.crt \
         --retry 3 -fSL -o "$mf" "$url" || die "manifest download failed"
    echo "version     : $(mj_str version "$mf")"
    echo "min_version : $(mj_str min_version "$mf")"
    echo "released    : $(mj_str released "$mf")"
    echo "hw          : $(sed -n 's/.*"hw"[[:space:]]*:[^"]*"\([^"]*\)".*/\1/p' "$mf" | head -1)"
    echo "slot.url    : $(mj_str url "$mf")"
    echo "slot.size   : $(mj_num size "$mf") bytes"
    echo "slot.sha256 : $(mj_str sha256 "$mf")"
}

# apply <manifest_url> [--force]
cmd_apply() {
    local url="" force=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --force) force=1 ;;
            -*) die "unknown flag: $1" ;;
            *) url=$1 ;;
        esac
        shift
    done
    [ -n "$url" ] || die "usage: $PROG apply <manifest_url> [--force]"
    require curl openssl sha256sum gunzip dd
    acquire_apply_lock

    mkdir -p "$WORKDIR"
    local mf="$WORKDIR/$MANIFEST_NAME"
    log "=== OTA apply start (force=$force) ==="
    log "fetching manifest: $url"
    curl --cacert /etc/ssl/certs/ca-certificates.crt \
         --retry 3 -fSL -o "$mf" "$url" || die "manifest download failed"

    local ver imgurl sigurl sha size
    ver=$(mj_str version "$mf");      [ -n "$ver" ]   || die "manifest: no version"
    imgurl=$(mj_str url "$mf");       [ -n "$imgurl" ]|| die "manifest: no slot.url"
    sigurl=$(mj_str sig_url "$mf");   [ -n "$sigurl" ]|| die "manifest: no slot.sig_url"
    sha=$(mj_str sha256 "$mf");       [ -n "$sha" ]   || die "manifest: no slot.sha256"
    size=$(mj_num size "$mf")

    local img="$WORKDIR/slot-$ver.img.gz"
    local sig="$img.sig"

    # real apply: free the SD card / network from other services before the
    # heavy download+dd (dry-run keeps everything running on purpose).
    # STATE_ENABLED stays 0 for dry-run so the LCD overlay never pops up.
    if [ "$force" = "1" ]; then
        ota_quiesce
        STATE_ENABLED=1
        set_state preparing -1 "Preparing for upgrade"
    fi

    # download image + signature with resume (auto full-retry if no Range)
    log "downloading image: $imgurl"
    fetch "$img" "$imgurl" "${size:-0}" || die "image download failed"
    # signature must always be fetched fresh: a stale cached .sig from a
    # previous run would silently pass the -C - resume check and fail verify
    rm -f "$sig"
    log "downloading signature: $sigurl"
    fetch "$sig" "$sigurl" || die "signature download failed"

    # 1) signature verify (authenticity) - hard requirement
    [ -r "$PUBKEY" ] || die "missing OTA public key: $PUBKEY"
    set_state verifying -1 "Verifying signature"
    log "verifying signature"
    openssl dgst -sha256 -verify "$PUBKEY" -signature "$sig" "$img" >/dev/null 2>&1 \
        || die "signature verification FAILED - refusing to flash"

    # 2) integrity check (sha256)
    set_state verifying -1 "Verifying checksum"
    log "verifying sha256"
    local actual
    actual=$(sha256sum "$img" | awk '{print $1}')
    [ "$actual" = "$sha" ] || die "sha256 mismatch: manifest=$sha actual=$actual"
    log "verified OK: version=$ver size=${size:-unknown} sha256=$sha"

    local tgt_slot tgt_dev bd
    tgt_slot=$(inactive_slot)
    bd=$(boot_dev); [ -n "$bd" ] || die "cannot resolve boot device"
    tgt_dev="${bd}p${tgt_slot}"
    log "target inactive slot: p$tgt_slot ($tgt_dev)"

    if [ "$force" != "1" ]; then
        log "DRY-RUN: would run: gunzip -c $img | dd of=$tgt_dev bs=4M conv=fsync"
        log "DRY-RUN: would setenv $ENV_SLOT=$tgt_slot $ENV_UPGRADE=1 bootcount=0 and reboot"
        log "DRY-RUN complete. Re-run with --force to actually flash."
        return 0
    fi

    # 3) flash inactive slot (progress via target partition diskstats)
    [ -b "$tgt_dev" ] || die "target device not found: $tgt_dev (A/B layout not present yet?)"
    set_state flashing 0 "Writing system image"
    log "flashing $tgt_dev ..."
    local flash_total="" tgt_base start_sectors cur_sectors done_bytes pct next_report mib_done mib_total
    local now last_time last_bytes speed_bps elapsed
    flash_total=$(cat "/sys/block/$(basename "$bd")/$(basename "$tgt_dev")/size" 2>/dev/null || true)
    [ -n "$flash_total" ] && flash_total=$((flash_total * 512)) || flash_total=0
    tgt_base=$(basename "$tgt_dev")
    start_sectors=$(disk_written_sectors "$tgt_base" 2>/dev/null || echo 0)
    next_report=0
    last_time=$(date +%s)
    last_bytes=0
    gunzip -c "$img" | dd of="$tgt_dev" bs=4M conv=fsync status=none &
    local dd_pid=$!
    while kill -0 "$dd_pid" 2>/dev/null; do
        if [ "$flash_total" -gt 0 ]; then
            cur_sectors=$(disk_written_sectors "$tgt_base" 2>/dev/null || echo "$start_sectors")
            done_bytes=$(((cur_sectors - start_sectors) * 512))
            [ "$done_bytes" -lt 0 ] 2>/dev/null && done_bytes=0
            pct=$((done_bytes * 100 / flash_total))
            [ "$pct" -gt 100 ] && pct=100
            now=$(date +%s)
            elapsed=$((now - last_time))
            speed_bps=-1
            if [ "$elapsed" -gt 0 ] && [ "$done_bytes" -ge "$last_bytes" ] 2>/dev/null; then
                speed_bps=$(((done_bytes - last_bytes) / elapsed))
            fi
            last_time=$now
            last_bytes=$done_bytes
            set_state flashing "$pct" "Writing system image" "$speed_bps"
            if [ "$pct" -ge "$next_report" ]; then
                mib_done=$((done_bytes / 1048576))
                mib_total=$((flash_total / 1048576))
                log "flashing progress: ${pct}% (${mib_done}/${mib_total} MiB)"
                next_report=$((pct + 5))
            fi
        fi
        sleep 2
    done
    wait "$dd_pid" || die "dd flash failed"
    set_state flashing 100 "Write complete"
    sync
    log "flash done"

    # 4) sanity: mount read-only and check version marker
    local mnt=/tmp/ota_verify_mnt
    mkdir -p "$mnt"
    if mount -o ro -t ext4 "$tgt_dev" "$mnt" 2>/dev/null; then
        local newver
        newver=$(sed -n 's/^sdk:[^-]*-\([0-9a-f]*\).*/\1/p' "$mnt/etc/version/release_version" 2>/dev/null | head -1)
        log "flashed slot version marker: ${newver:-unknown}"
        umount "$mnt" 2>/dev/null
    else
        log "WARN: could not mount $tgt_dev read-only for sanity check"
    fi

    # 5) switch slot + arm rollback counter
    set_state switching -1 "Switching to new system, rebooting"
    log "switching boot slot -> $tgt_slot (upgrade_available=1, bootcount=0)"
    fw_setenv "$ENV_SLOT" "$tgt_slot" \
              "$ENV_UPGRADE" "1" \
              bootcount "0" || die "fw_setenv failed"

    log "rebooting into new slot..."
    sync
    # unmount blockdata before the forced reboot so the new slot does not
    # inherit a dirty p4 (resize2fs refuses a dirty fs on first boot)
    umount /root/block 2>/dev/null || true
    reboot -f
}

# confirm: called after the new slot proves healthy
cmd_confirm() {
    require fw_setenv
    log "confirming current slot $(current_slot) as good"
    fw_setenv bootcount "0" "$ENV_UPGRADE" "0" || die "fw_setenv failed"
    log "confirmed"
}

# rollback: switch back to the other slot and reboot
cmd_rollback() {
    require fw_setenv
    local tgt
    tgt=$(inactive_slot)
    log "manual rollback -> slot $tgt"
    fw_setenv "$ENV_SLOT" "$tgt" bootcount "0" "$ENV_UPGRADE" "0" || die "fw_setenv failed"
    log "rebooting into previous slot..."
    sync
    reboot -f
}

# --- entry --------------------------------------------------------------------

cmd=${1:-status}
[ $# -gt 0 ] && shift
case "$cmd" in
    status)   cmd_status "$@" ;;
    check)    cmd_check "$@" ;;
    apply)    cmd_apply "$@" ;;
    confirm)  cmd_confirm "$@" ;;
    rollback) cmd_rollback "$@" ;;
    -h|--help|help)
        sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
        ;;
    *) die "unknown subcommand: $cmd (try: status|check|apply|confirm|rollback)" ;;
esac
