#!/bin/sh
# rockbase_sdhealthd.sh - Rockbase SD card lifetime/health sampler
#
# Zero-burden design: only reads kernel-maintained statistics and never
# touches the SD data area. Sources:
#   /proc/diskstats   (cumulative sectors, io_ms, weighted_io_ms, inflight)
#   /proc/meminfo     (Dirty/Writeback)
#   dmesg             (I/O error events, kernel ring buffer)
#   /sys/block/<dev>/device/*  (card identity)
#
# Persists a small cross-reboot baseline to /root/config/sdhealth.state
# (a few tens of bytes every INTERVAL) and renders /tmp/sdhealth.json for
# the web UI (merged into status-api.json by rockbase_statusd.sh).
#
# NOTE: diskstats resets on reboot, so cumulative totals are computed as
#       (previous cumulative) + (per-boot delta), detecting reboot via uptime.
#       SD cards have NO EXT_CSD lifetime register (verified on real HW), so
#       there is no vendor "remaining life %" — only objective wear evidence.

INTERVAL=${ROCKBASE_SDHEALTH_INTERVAL:-300}
STATE_FILE=${ROCKBASE_SDHEALTH_STATE:-/root/config/sdhealth.state}
JSON_FILE=${ROCKBASE_SDHEALTH_JSON:-/tmp/sdhealth.json}
LOG_FILE=${ROCKBASE_SDHEALTH_LOG:-/run/rockbase-log/sdhealth.log}
LOG_MAX_KB=${ROCKBASE_SDHEALTH_LOG_MAX_KB:-128}

# Boards have no RTC: wall clock starts at 1970 and jumps forward on the NTP
# one-shot sync. Wall-clock values recorded before that sync are garbage, so
# only trust the clock once it is past this sanity threshold (2020-01-01 UTC).
CLOCK_TRUST_EPOCH=1577836800

# I/O error patterns scanned from dmesg (incremental via kernel timestamp).
ERR_PATTERN='I/O error|buffer I/O error|blk_update_request.*error|EXT4-fs error|end_request.*I/O error'

# ---- A-scheme verdict inputs (override per-deployment via env) ----
# Lifetime (L1) thresholds on CUMULATIVE written bytes. These are the only
# wear-based signals: a card only degrades/criticals once it has genuinely
# absorbed a lot of writes. Conservative defaults for 32-64GB node cards.
TBW_DEGRADED_GB=${ROCKBASE_SD_TBW_DEGRADED_GB:-500}
TBW_CRITICAL_GB=${ROCKBASE_SD_TBW_CRITICAL_GB:-2000}
# Rolling window (powered-on seconds) used for BOTH the I/O-error recency
# window and the peak-wbusy decay. Errors/spikes older than this stop counting,
# so the card can recover to healthy (no permanent ratchet).
WINDOW_SEC=${ROCKBASE_SD_WINDOW_SEC:-86400}
# Observation period: below this powered-on runtime we only "monitor" and do
# not declare degraded/critical (except on hard I/O errors).
OBSERVATION_SEC=${ROCKBASE_SD_OBSERVATION_SEC:-$((7 * 86400))}

# ---------- helpers ----------

now_epoch() {
    date +%s 2>/dev/null || echo 0
}

num_or_zero() {
    case "$1" in
        ''|*[!0-9]*) printf '0' ;;
        *) printf '%s' "$1" ;;
    esac
}

json_str() {
    v=$1
    v=${v//\\/\\\\}
    v=${v//\"/\\\"}
    case "$v" in
        *"
"*) v=$(printf '%s' "$v" | tr -d '\n') ;;
    esac
    printf '"%s"' "$v"
}

json_num() {
    v="$1"
    if [ -z "$v" ] || [ "$v" = "-" ] || [ "$v" = "null" ]; then
        printf 'null'
    else
        printf '%s' "$v"
    fi
}

# Convert epoch seconds -> UTC YYYY-MM-DD (Howard Hinnant civil_from_days).
epoch_to_date() {
    awk -v e="$1" 'BEGIN{
        if (e == "" || e <= 0) { print "-"; exit }
        z = int(e/86400) + 719468
        era = int(z/146097)
        doe = z - era*146097
        yoe = int((doe - int(doe/1460) + int(doe/36524) - int(doe/146096)) / 365)
        y = yoe + era*400
        doy = doe - (365*yoe + int(yoe/4) - int(yoe/100))
        mp = int((5*doy + 2)/153)
        d = doy - int((153*mp + 2)/5) + 1
        m = mp + (mp < 10 ? 3 : -9)
        y += (m <= 2)
        printf "%04d-%02d-%02d", y, m, d
    }'
}

# Format bytes into human-readable units (B/KB/MB/GB/TB).
bytes_display() {
    awk -v b="$1" 'BEGIN{
        if (b == "" || b < 0) { print "-"; exit }
        val = b + 0
        u[1]="B"; u[2]="KB"; u[3]="MB"; u[4]="GB"; u[5]="TB"; u[6]="PB"
        idx = 1
        while (val >= 1024 && idx < 6) { val /= 1024; idx++ }
        if (val >= 100) printf "%.0f%s", val, u[idx]
        else if (val >= 10) printf "%.1f%s", val, u[idx]
        else printf "%.2f%s", val, u[idx]
    }'
}

# Map SD manufacturer id (JEDEC manfid) to a vendor name.
vendor_name() {
    manfid="$1"
    dec=$(printf '%d' "0x${manfid#0x}" 2>/dev/null)
    case "$dec" in
        159) printf 'Kingston' ;;
        3)   printf 'SanDisk' ;;
        27)  printf 'Samsung' ;;
        40)  printf 'Lexar' ;;
        41)  printf 'Transcend' ;;
        0|'') printf 'Unknown' ;;
        *)   printf 'manfid:%s' "$manfid" ;;
    esac
}

log_msg() {
    mkdir -p "$(dirname "$LOG_FILE")" 2>/dev/null
    size_kb=$(du -k "$LOG_FILE" 2>/dev/null | awk 'NR==1{print $1}')
    [ -n "$size_kb" ] || size_kb=0
    if [ "$size_kb" -ge "$LOG_MAX_KB" ]; then
        mv "$LOG_FILE" "$LOG_FILE.1" 2>/dev/null || true
    fi
    printf '%s %s\n' "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" "$*" >> "$LOG_FILE" 2>/dev/null
}

# Detect the SD block device (mmcblk1 on Rockbase; type=SD).
detect_sd_dev() {
    for d in mmcblk1 mmcblk0; do
        t=$(cat "/sys/block/$d/device/type" 2>/dev/null | tr -d '[:space:]')
        [ -n "$t" ] && { SD_DEV="$d"; return 0; }
    done
    SD_DEV="mmcblk1"
}

# Read one diskstats line:
#   read_sectors($6) write_sectors($10) ms_writing($11) io_ms($13) inflight($12)
# NOTE: write-busy must use ms_writing ($11, write time only). weighted_io_ms
# ($14) counts BOTH reads and writes, so during IBD/read-heavy phases it spikes
# and would falsely look like "write busy".
read_disk() {
    awk -v dev="$SD_DEV" '$3 == dev { print $6, $10, $11, $13, $12 }' /proc/diskstats 2>/dev/null
}

# ---------- state ----------

load_state() {
    total_written_sectors=0
    total_read_sectors=0
    last_written_sectors=0
    last_read_sectors=0
    last_ms_writing=0
    last_sample_epoch=0
    last_uptime_sec=0
    total_uptime_sec=0
    io_err_count=0
    io_err_marker=0
    peak_wbusy=0
    peak_wbusy_uptime=0
    io_err_window_start_count=0
    io_err_window_start_uptime=0
    first_install_epoch=0
    [ -r "$STATE_FILE" ] && . "$STATE_FILE" 2>/dev/null || true
}

write_state() {
    mkdir -p "$(dirname "$STATE_FILE")" 2>/dev/null || return 0
    tmp="${STATE_FILE}.$$"
    {
        echo "total_written_sectors=$total_written_sectors"
        echo "total_read_sectors=$total_read_sectors"
        echo "last_written_sectors=$last_written_sectors"
        echo "last_read_sectors=$last_read_sectors"
        echo "last_ms_writing=$last_ms_writing"
        echo "last_sample_epoch=$last_sample_epoch"
        echo "last_uptime_sec=$last_uptime_sec"
        echo "total_uptime_sec=$total_uptime_sec"
        echo "io_err_count=$io_err_count"
        echo "io_err_marker=$io_err_marker"
        echo "peak_wbusy=$peak_wbusy"
        echo "peak_wbusy_uptime=$peak_wbusy_uptime"
        echo "io_err_window_start_count=$io_err_window_start_count"
        echo "io_err_window_start_uptime=$io_err_window_start_uptime"
        echo "first_install_epoch=$first_install_epoch"
    } > "$tmp" 2>/dev/null && mv "$tmp" "$STATE_FILE" 2>/dev/null
}

# Incremental I/O error counter driven by kernel timestamps.
scan_io_errors() {
    old_marker=$io_err_marker
    max_ts=$old_marker
    new_count=0
    for ts in $(dmesg 2>/dev/null | grep -E "$ERR_PATTERN" | sed -nE 's/^\[ *([0-9]+)\..*/\1/p'); do
        ts=$(num_or_zero "$ts")
        [ "$ts" -gt "$old_marker" ] && new_count=$((new_count + 1))
        [ "$ts" -gt "$max_ts" ] && max_ts=$ts
    done
    io_err_count=$((io_err_count + new_count))
    io_err_marker=$max_ts
}

# ---------- one sample ----------

sample() {
    detect_sd_dev
    load_state

    now=$(now_epoch)
    uptime=$(awk -F. '{print $1}' /proc/uptime 2>/dev/null)
    [ -n "$uptime" ] || uptime=0

    set -- $(read_disk)
    r_sectors=$(num_or_zero "${1:-0}")
    w_sectors=$(num_or_zero "${2:-0}")
    ms_writing=$(num_or_zero "${3:-0}")
    inflight=$(num_or_zero "${5:-0}")

    # Reboot detection: uptime went backwards -> diskstats reset.
    if [ "$uptime" -lt "$last_uptime_sec" ]; then
        last_written_sectors=0
        last_read_sectors=0
        last_ms_writing=0
        last_sample_epoch=0
        io_err_marker=0
    fi

    # First-ever sample (fresh install): seed the baseline to the CURRENT
    # diskstats values so we do NOT attribute the whole boot's prior I/O
    # (e.g. bitcoind IBD reading tens of GB) to our sampling. Cumulative
    # read/write then starts exactly from deployment time, consistent with
    # `baseline_since`.
    if [ "$first_install_epoch" -eq 0 ]; then
        last_written_sectors=$w_sectors
        last_read_sectors=$r_sectors
        last_ms_writing=$ms_writing
    fi

    # Cumulative written/read sectors (correct across reboot).
    dw=$((w_sectors - last_written_sectors))
    [ "$dw" -lt 0 ] && dw=$w_sectors
    total_written_sectors=$((total_written_sectors + dw))

    dr=$((r_sectors - last_read_sectors))
    [ "$dr" -lt 0 ] && dr=$r_sectors
    total_read_sectors=$((total_read_sectors + dr))

    # Cumulative card runtime.
    du=$((uptime - last_uptime_sec))
    [ "$du" -lt 0 ] && du=0
    total_uptime_sec=$((total_uptime_sec + du))

    # Write busy % / write rate need a valid prior sample. On first-ever
    # sample (or first sample after a reboot) there is no prior window, so
    # report 0 rather than attributing the whole boot's I/O to one window.
    if [ "$last_sample_epoch" -eq 0 ]; then
        wbusy=0
        write_rate_bps=0
    else
        # Sampling window from MONOTONIC uptime delta, never wall clock:
        # boards have no RTC, so the NTP one-shot sync jumps the wall clock
        # ~55 years forward at boot. A wall-clock window would clamp to
        # INTERVAL while dwrite_ms spans the real (much larger) interval,
        # faking wbusy=100 and ratcheting the persisted peak permanently.
        window=$((uptime - last_uptime_sec))
        if [ "$window" -le 0 ] || [ "$window" -gt $((INTERVAL * 4)) ]; then
            # Abnormal gap (daemon stall / missed samples): skip this window
            # instead of distorting the rate or polluting the peak.
            wbusy=0
            write_rate_bps=0
        else
            # Write busy % from ms_writing (f11, write time only) delta.
            dwrite_ms=$((ms_writing - last_ms_writing))
            [ "$dwrite_ms" -lt 0 ] && dwrite_ms=0
            wbusy=$(awk -v d="$dwrite_ms" -v w="$window" 'BEGIN{if(w>0){p=d/(w*1000)*100; if(p>100)p=100; printf "%.0f", p} else print 0}')

            # Write rate bytes/sec over the window.
            write_rate_bps=$(awk -v dw="$dw" -v w="$window" 'BEGIN{if(w>0)printf "%.0f", dw*512/w; else print 0}')
        fi
    fi

    # Peak wbusy is now a ROLLING 24h value (informational only — it no longer
    # drives status/score). Reset it once the recorded peak is older than the
    # window so a single transient spike can no longer lock the card
    # "degraded" forever (the old permanent ratchet was the main false-positive
    # source). total_uptime_sec is monotonic (immune to NTP jumps).
    if [ "$total_uptime_sec" -ge $((peak_wbusy_uptime + WINDOW_SEC)) ]; then
        peak_wbusy=0
        peak_wbusy_uptime=$total_uptime_sec
    fi
    if [ "$wbusy" -gt "$peak_wbusy" ]; then
        peak_wbusy=$wbusy
        peak_wbusy_uptime=$total_uptime_sec
    fi

    # Dirty / Writeback (write-back pressure).
    dirty_kb=$(awk '/^Dirty:/{print $2}' /proc/meminfo 2>/dev/null)
    [ -n "$dirty_kb" ] || dirty_kb=0

    # I/O error scan (incremental).
    scan_io_errors

    # Roll the 24h I/O-error recency window forward (powered-on seconds). The
    # verdict uses errors within this window, so a single long-ago transient
    # error stops counting after 24h and the card can recover to healthy.
    if [ "$total_uptime_sec" -ge $((io_err_window_start_uptime + WINDOW_SEC)) ]; then
        io_err_window_start_count=$io_err_count
        io_err_window_start_uptime=$total_uptime_sec
    fi

    # Persist current sample as the new "last".
    last_written_sectors=$w_sectors
    last_read_sectors=$r_sectors
    last_ms_writing=$ms_writing
    last_sample_epoch=$now
    last_uptime_sec=$uptime
    # Re-seed until the clock is trustworthy: a pre-NTP sample would otherwise
    # pin the baseline to a 1970 epoch and "since" would show 1970-01-01 forever.
    [ "$first_install_epoch" -lt "$CLOCK_TRUST_EPOCH" ] && first_install_epoch=$now

    write_state
}

# ---------- verdict / score ----------

compute_health() {
    total_written_bytes=$((total_written_sectors * 512))
    # Cumulative powered-on days; used only for the insufficient_data verdict.
    uptime_days=$((total_uptime_sec / 86400))
    # Display value: wall-clock span since tracking started, so it matches the
    # "since <date>" shown next to it. A board powered off for days still shows
    # the real elapsed monitoring time instead of a misleading low uptime figure.
    # If the baseline was never seeded with a trustworthy clock, fall back to
    # cumulative powered-on time instead of a bogus multi-decade span.
    if [ "$first_install_epoch" -ge "$CLOCK_TRUST_EPOCH" ]; then
        _now=$(now_epoch)
        _span=$(( _now - first_install_epoch ))
        [ "$_span" -lt 0 ] && _span=0
        uptime_days_display=$(awk -v s="$_span" 'BEGIN{printf "%.1f", s/86400}')
    else
        uptime_days_display=$(awk -v s="$total_uptime_sec" 'BEGIN{printf "%.1f", s/86400}')
    fi

    tbw_degraded_bytes=$((TBW_DEGRADED_GB * 1000000000))
    tbw_critical_bytes=$((TBW_CRITICAL_GB * 1000000000))

    # I/O errors occurring within the rolling window (ongoing failure signal).
    io_err_recent=$((io_err_count - io_err_window_start_count))
    [ "$io_err_recent" -lt 0 ] && io_err_recent=0

    status="healthy"
    verdict="healthy"

    # Hard evidence 1: I/O errors happening RECENTLY (within the window).
    # A single long-ago transient error no longer pins the card critical.
    if [ "$io_err_recent" -gt 0 ]; then
        status="critical"
        verdict="sd_card_suspect"
    # Hard evidence 2: cumulative wear (L1) crossing lifetime thresholds.
    # This is the only true lifetime signal and only trips on real, large
    # accumulated writes — never on transient I/O pressure.
    elif [ "$total_written_bytes" -ge "$tbw_critical_bytes" ]; then
        status="critical"
        verdict="wear_out"
    elif [ "$total_written_bytes" -ge "$tbw_degraded_bytes" ]; then
        status="degraded"
        verdict="wear_warning"
    fi

    # Observation period: not enough powered-on runtime to judge -> keep
    # healthy and mark monitoring (frontend renders status=healthy +
    # insufficient_data as "Monitoring"). Never overrides a hard
    # critical/degraded decided above.
    if [ "$status" = "healthy" ] && [ "$total_uptime_sec" -lt "$OBSERVATION_SEC" ]; then
        verdict="insufficient_data"
    fi

    score=100
    case "$status" in
        critical) score=40 ;;
        degraded) score=70 ;;
    esac
}

# ---------- render ----------

render_json() {
    compute_health

    card_name=$(cat "/sys/block/$SD_DEV/device/name" 2>/dev/null | tr -d '\0[:space:]')
    card_manfid=$(cat "/sys/block/$SD_DEV/device/manfid" 2>/dev/null | tr -d '[:space:]')
    card_serial=$(cat "/sys/block/$SD_DEV/device/serial" 2>/dev/null | tr -d '[:space:]')
    vendor=$(vendor_name "$card_manfid")
    [ -n "$card_name" ] || card_name="-"
    # Card manufacture date (sysfs `date` is "MM/YYYY") -> ISO "YYYY-MM".
    card_date=$(cat "/sys/block/$SD_DEV/device/date" 2>/dev/null | tr -d '[:space:]')
    card_date=$(awk -v d="$card_date" 'BEGIN{
        split(d, a, "/")
        if (length(a[1])==2 && length(a[2])==4) printf "%s-%s", a[2], a[1]
        else print d
    }')
    [ -n "$card_date" ] || card_date="-"
    # Baseline start: when this sampler first installed / started recording.
    # Only shown once seeded with a trustworthy (post-NTP) clock.
    baseline_since="-"
    [ "$first_install_epoch" -ge "$CLOCK_TRUST_EPOCH" ] && baseline_since=$(epoch_to_date "$first_install_epoch")

    total_written_display=$(bytes_display "$total_written_bytes")
    total_read_display=$(bytes_display "$((total_read_sectors * 512))")

    write_rate_day_gb=$(awk -v b="$write_rate_bps" 'BEGIN{
        gb = b * 86400 / 1000000000
        if (gb >= 100) printf "%.0f", gb
        else if (gb >= 10) printf "%.1f", gb
        else printf "%.2f", gb
    }')
    write_rate_day_display="${write_rate_day_gb} GB/day"

    # Recent I/O error lines (max 3) for display.
    recent_json="[]"
    recent=$(dmesg 2>/dev/null | grep -E "$ERR_PATTERN" | tail -3)
    if [ -n "$recent" ]; then
        recent_json="["
        first=1
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            [ "$first" -eq 1 ] && first=0 || recent_json="${recent_json},"
            recent_json="${recent_json}$(json_str "$line")"
        done <<EOF
$recent
EOF
        recent_json="${recent_json}]"
    fi

    tmp="${JSON_FILE}.$$"
    {
        printf '{\n'
        printf '  "available": true,\n'
        printf '  "updated_at": %s,\n' "$(json_num "$now")"
        printf '  "card": {"name": %s, "vendor": %s, "serial": %s, "date": %s},\n' \
            "$(json_str "$card_name")" "$(json_str "$vendor")" "$(json_str "$card_serial")" "$(json_str "$card_date")"
        printf '  "baseline_since": %s,\n' "$(json_str "$baseline_since")"
        printf '  "score": %s,\n' "$(json_num "$score")"
        printf '  "status": %s,\n' "$(json_str "$status")"
        printf '  "verdict": %s,\n' "$(json_str "$verdict")"
        printf '  "total_written_bytes": %s,\n' "$(json_num "$total_written_bytes")"
        printf '  "total_written_display": %s,\n' "$(json_str "$total_written_display")"
        printf '  "total_read_display": %s,\n' "$(json_str "$total_read_display")"
        printf '  "write_rate_day_gb": %s,\n' "$(json_str "$write_rate_day_gb")"
        printf '  "write_rate_day_display": %s,\n' "$(json_str "$write_rate_day_display")"
        printf '  "wbusy_pct": %s,\n' "$(json_num "$wbusy")"
        printf '  "peak_wbusy_pct": %s,\n' "$(json_num "$peak_wbusy")"
        printf '  "dirty_kb": %s,\n' "$(json_num "$dirty_kb")"
        printf '  "io_errors": %s,\n' "$(json_num "$io_err_count")"
        printf '  "io_errors_recent": %s,\n' "$(json_num "$io_err_recent")"
        printf '  "uptime_days": %s,\n' "$(json_num "$uptime_days")"
        printf '  "uptime_days_display": %s,\n' "$(json_str "$uptime_days_display")"
        printf '  "recent_events": %s\n' "$recent_json"
        printf '}\n'
    } > "$tmp" 2>/dev/null && mv "$tmp" "$JSON_FILE" 2>/dev/null

    log_msg "dev=$SD_DEV written=$total_written_display err=$io_err_count wbusy=$wbusy% peak=$peak_wbusy% status=$status verdict=$verdict"
}

# ---------- entry ----------

case "$1" in
    daemon|start)
        while :; do
            sample
            render_json
            sleep "$INTERVAL"
        done
        ;;
    oneshot)
        sample
        render_json
        ;;
    *)
        echo "Usage: $0 {daemon|start|oneshot}"
        exit 1
        ;;
esac
