#!/bin/sh
# NM-miner JSON Status API
# Outputs all dashboard data as JSON for AJAX consumption

CONF=/root/bitcoind/bitcoin.conf
DATADIR=/root/block
DEBUG_LOG=/root/bitcoind/debug.log
BTC_RPC_TIMEOUT=3
CACHE_FILE=/tmp/status-api.json
CACHE_STAMP=/tmp/status-api.stamp
CACHE_TTL=2
LOCK_DIR=/tmp/status-api.lock
LOCK_HELD=0

btc_cli(){
  bitcoin-cli -rpcclienttimeout="$BTC_RPC_TIMEOUT" -conf="$CONF" -datadir="$DATADIR" "$@" 2>/dev/null || echo "{}"
}

read_cache() {
  [ -r "$CACHE_FILE" ] && cat "$CACHE_FILE"
}

cache_is_fresh() {
  [ -r "$CACHE_FILE" ] || return 1
  [ -r "$CACHE_STAMP" ] || return 1
  now=$(date +%s 2>/dev/null || echo 0)
  stamp=$(cat "$CACHE_STAMP" 2>/dev/null || echo 0)
  [ -n "$stamp" ] || return 1
  age=$((now - stamp))
  [ "$age" -ge 0 ] 2>/dev/null || return 1
  [ "$age" -le "$CACHE_TTL" ] 2>/dev/null
}

acquire_render_lock() {
  if mkdir "$LOCK_DIR" 2>/dev/null; then
    echo $$ > "$LOCK_DIR/pid"
    LOCK_HELD=1
    return 0
  fi
  return 1
}

release_render_lock() {
  if [ "$LOCK_HELD" = "1" ]; then
    rm -rf "$LOCK_DIR" 2>/dev/null || true
  fi
}

if cache_is_fresh; then
  read_cache
  exit 0
fi

if ! acquire_render_lock; then
  tries=0
  while [ "$tries" -lt 6 ]; do
    if cache_is_fresh; then
      read_cache
      exit 0
    fi
    sleep 1
    tries=$((tries + 1))
  done
  if [ -r "$CACHE_FILE" ]; then
    read_cache
    exit 0
  fi
fi

trap 'release_render_lock' EXIT INT TERM

latest_tip_date_from_log() {
  [ -r "$DEBUG_LOG" ] || return 0
  tail -n 200 "$DEBUG_LOG" 2>/dev/null | sed -n "s/.*date='\([^']*\)'.*/\1/p" | tail -n1 | sed "s/T/ /; s/Z$/ UTC/"
}

latest_update_tip_line() {
  [ -r "$DEBUG_LOG" ] || return 0
  tail -n 400 "$DEBUG_LOG" 2>/dev/null | awk '/UpdateTip: new best=/{line=$0} END{print line}'
}

latest_peer_blocks_from_log() {
  [ -r "$DEBUG_LOG" ] || return 0
  tail -n 400 "$DEBUG_LOG" 2>/dev/null | sed -n 's/.*blocks=\([0-9][0-9]*\).*/\1/p' | tail -n1
}

latest_block_tree_size_from_log() {
  [ -r "$DEBUG_LOG" ] || return 0
  tail -n 400 "$DEBUG_LOG" 2>/dev/null | sed -n 's/.*block tree size = \([0-9][0-9]*\).*/\1/p' | tail -n1
}

pruned_from_conf() {
  prune_target=$(sed -n 's/^prune=\([0-9][0-9]*\).*/\1/p' "$CONF" 2>/dev/null | head -n1)
  case "$prune_target" in
    ''|0) echo false ;;
    *) echo true ;;
  esac
}

# --- JSON string escaping helper ---
json_str() {
  printf '"'
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr -d '\n'
  printf '"'
}

# Multiline variant: replaces real newlines with \n
json_str_ml() {
  printf '"'
  printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g' | awk '{printf "%s\\n", $0}' | tr -d '\n' | sed 's/\\\\n$//'
  printf '"'
}

# Numeric JSON value (no quotes)
json_num() {
  v="$1"
  if [ -z "$v" ] || [ "$v" = "-" ] || [ "$v" = "null" ]; then
    printf 'null'
  else
    printf '%s' "$v"
  fi
}

compact_metric(){
  awk -v n="$1" 'BEGIN {
    if (n == "" || n == "-" || n == "null") { print "-"; exit }
    val = n + 0
    u[1] = "K"; u[2] = "M"; u[3] = "G"; u[4] = "T"; u[5] = "P"; u[6] = "E"
    idx = 0
    while ((val >= 1000 || val <= -1000) && idx < 6) { val /= 1000; idx++ }
    abs = val < 0 ? -val : val
    if (idx == 0) printf "%.0f", val
    else if (abs >= 100) printf "%.0f%s", val, u[idx]
    else if (abs >= 10) printf "%.1f%s", val, u[idx]
    else printf "%.2f%s", val, u[idx]
  }'
}

compact_size_mb(){
  awk -v mb="$1" 'BEGIN {
    if (mb == "" || mb == "-" || mb == "null") { print "-"; exit }
    val = mb + 0
    u[1] = "M"; u[2] = "G"; u[3] = "T"; u[4] = "P"; u[5] = "E"
    idx = 1
    while (val >= 1024 && idx < 5) { val /= 1024; idx++ }
    if (val >= 100) printf "%.0f%s", val, u[idx]
    else if (val >= 10) printf "%.1f%s", val, u[idx]
    else printf "%.2f%s", val, u[idx]
  }'
}

fmt_int(){
  awk -v n="$1" 'BEGIN {
    if (n == "" || n == "-" || n == "null") { print "-"; exit }
    s = sprintf("%.0f", n + 0)
    out = ""
    while (length(s) > 3) {
      out = "," substr(s, length(s) - 2) out
      s = substr(s, 1, length(s) - 3)
    }
    print s out
  }'
}

# ============ System ============
cpu_model=$(cat /proc/device-tree/model 2>/dev/null | tr '\0' ' ' | xargs)
[ -z "$cpu_model" ] && cpu_model=$(grep -m1 "uarch\|machine\|model name\|cpu model" /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs)
[ -z "$cpu_model" ] && cpu_model="K230 RISC-V"
uptime_sec=$(awk -F. '{print $1}' /proc/uptime)
uptime_fmt=$(printf '%dd %02dh:%02dm' $((uptime_sec/86400)) $((uptime_sec%86400/3600)) $((uptime_sec%3600/60)))
cpu_stat_sample() {
  awk '/^cpu /{
    total = 0
    for (i = 2; i <= 8; i++) total += $i
    idle = $5 + $6
    print total, idle
    exit
  }' /proc/stat
}
set -- $(cpu_stat_sample)
cpu_total_1=$1
cpu_idle_1=$2
sleep 0.2
set -- $(cpu_stat_sample)
cpu_total_2=$1
cpu_idle_2=$2
load_pct=$(awk -v t1="$cpu_total_1" -v i1="$cpu_idle_1" -v t2="$cpu_total_2" -v i2="$cpu_idle_2" 'BEGIN{
  dt = t2 - t1
  di = i2 - i1
  if (dt <= 0) { print 0; exit }
  pct = (dt - di) * 100 / dt
  if (pct < 0) pct = 0
  if (pct > 100) pct = 100
  printf "%.0f", pct
}')
set -- $(awk '{print $1, $2, $3}' /proc/loadavg)
load_avg_1=$1
load_avg_5=$2
load_avg_15=$3
load_avg_1_display=$(awk -v v="$load_avg_1" 'BEGIN{if (v == "" || v == "-" || v == "null") print "-"; else printf "%.2f", v + 0}')
load_avg_5_display=$(awk -v v="$load_avg_5" 'BEGIN{if (v == "" || v == "-" || v == "null") print "-"; else printf "%.2f", v + 0}')
load_avg_15_display=$(awk -v v="$load_avg_15" 'BEGIN{if (v == "" || v == "-" || v == "null") print "-"; else printf "%.2f", v + 0}')
load_display="${load_pct}% busy"
load_avg_display="1m ${load_avg_1_display} · 5m ${load_avg_5_display}"

mem_total=$(awk '/^MemTotal/{printf "%.0f", $2/1024}' /proc/meminfo)
mem_free=$(awk '/^MemAvailable/{printf "%.0f", $2/1024}' /proc/meminfo)
mem_used=$((mem_total-mem_free))
if [ "$mem_total" -gt 0 ]; then mem_pct=$((mem_used*100/mem_total)); else mem_pct=0; fi

swap_total=$(awk '/^SwapTotal/{printf "%.0f", $2/1024}' /proc/meminfo)
swap_free=$(awk '/^SwapFree/{printf "%.0f", $2/1024}' /proc/meminfo)
swap_used=$((swap_total-swap_free))
if [ "$swap_total" -gt 0 ]; then swap_pct=$((swap_used*100/swap_total)); else swap_pct=0; fi
mem_used_display=$(compact_size_mb "$mem_used")
mem_total_display=$(compact_size_mb "$mem_total")
swap_used_display=$(compact_size_mb "$swap_used")
swap_total_display=$(compact_size_mb "$swap_total")

disk_used_display=$(df -h / | awk 'NR==2{print $3}')
disk_total_display=$(df -h / | awk 'NR==2{print $2}')
[ -z "$disk_used_display" ] && disk_used_display="-"
[ -z "$disk_total_display" ] && disk_total_display="-"
disk_size_info="${disk_used_display} / ${disk_total_display}"
disk_pct=$(df -h / | awk 'NR==2{gsub("%","",$5);print $5}')
[ -z "$disk_pct" ] && disk_pct=0

block_disk_found="true"
block_disk_value="-"
block_disk_path="$DATADIR"
if [ ! -d "$DATADIR" ]; then
  block_disk_found="false"
fi

temp="N/A"
for t in /sys/class/thermal/thermal_zone*/temp /sys/class/hwmon/hwmon*/temp1_input; do
  [ ! -r "$t" ] && continue
  raw=$(cat "$t" 2>/dev/null)
  case "$raw" in
    '' ) ;;
    * )
      if echo "$raw" | grep -Eq '^[0-9]+$'; then
        zone_type_file="${t%/temp}/type"
        if [ -r "$zone_type_file" ] && grep -qx 'canaan_thermal_zone' "$zone_type_file" 2>/dev/null; then
          ts_code=$((raw & 4095))
          temp=$(awk -v code="$ts_code" 'BEGIN{
            c = code + 0
            v = (1.01472e-10 * c * c * c * c) - (1.10063e-6 * c * c * c) + (4.36150e-3 * c * c) - (7.10128 * c) + 3565.87
            printf "%.1fC", v
          }')
          break
        fi
      fi
      if [ "$raw" -ge 100000 ] 2>/dev/null; then temp=$(awk -v r="$raw" 'BEGIN{printf "%.1fC", r/1000}'); break; fi
      if [ "$raw" -ge 1000 ] 2>/dev/null; then temp=$(awk -v r="$raw" 'BEGIN{printf "%.1fC", r/100}'); break; fi
      if echo "$raw" | grep -Eq '^[0-9]+$'; then temp=$(awk -v r="$raw" 'BEGIN{printf "%.1fC", r}'); break; fi
      ;;
  esac
done

# ============ Bitcoin Node ============
btc_running=0
headers="-"; headers_fmt="-"
blocks="-"; blocks_fmt="-"
progress_pct="0.000"; progress_ring="0.000"; ibd=""; chain="-"; difficulty="-"
difficulty_display="-"; connections="-"; subversion="-"; rss="N/A"
pruned="false"; best_block_date="-"; btc_context="-"
ibd_stage_key="syncing"; ibd_stage_label="IBD Syncing"; ibd_stage_detail=""
ibd_stage_height="-"; ibd_stage_pct=""
stage_height_num=""; stage_pct_num=""
header_gap="0"

if pidof bitcoind >/dev/null 2>&1; then
  btc_running=1
  btc_info=$(btc_cli getblockchaininfo)
  headers=$(echo "$btc_info" | grep -o '"headers": *[0-9]*' | grep -o '[0-9]*')
  blocks=$(echo "$btc_info" | grep -o '"blocks": *[0-9]*' | grep -o '[0-9]*')
  progress=$(echo "$btc_info" | grep -o '"verificationprogress": *[0-9.e+-]*' | cut -d: -f2)
  [ -n "$progress" ] && progress_pct=$(echo "$progress" | awk '{printf "%.3g", $1*100}')
  progress_ring=$(awk -v p="$progress_pct" 'BEGIN{if(p<0)p=0;if(p>100)p=100;printf "%.3f",p}')
  ibd=$(echo "$btc_info" | grep -o '"initialblockdownload": *[a-z]*' | grep -o 'true\|false')
  chain=$(echo "$btc_info" | grep -o '"chain": *"[^"]*"' | cut -d'"' -f4)
  difficulty=$(echo "$btc_info" | grep -o '"difficulty": *[0-9.]*' | cut -d: -f2 | xargs)
  block_disk_bytes=$(echo "$btc_info" | grep -o '"size_on_disk": *[0-9]*' | grep -o '[0-9]*')
  if [ -n "$block_disk_bytes" ]; then
    block_disk_mb=$(awk -v b="$block_disk_bytes" 'BEGIN{printf "%.0f", (b+0)/1048576}')
    block_disk_value=$(compact_size_mb "$block_disk_mb")
  fi
  latest_tip_date=$(latest_tip_date_from_log)
  [ -n "$latest_tip_date" ] && best_block_date="$latest_tip_date"

  connections="-"
  subversion="-"
  pruned=$(echo "$btc_info" | grep -o '"pruned": *[a-z]*' | grep -o 'true\|false')
  btc_pid=$(pidof bitcoind 2>/dev/null | awk '{print $1}')
  [ -n "$btc_pid" ] && rss=$(awk '/^VmRSS/{printf "%.0fM",$2/1024}' "/proc/$btc_pid/status" 2>/dev/null)

  update_tip_line=$(latest_update_tip_line)
  log_blocks=$(printf '%s\n' "$update_tip_line" | sed -n 's/.* height=\([0-9][0-9]*\).*/\1/p')
  log_progress=$(printf '%s\n' "$update_tip_line" | sed -n "s/.* progress=\([0-9.][0-9.]*\).*/\1/p")
  log_best_date=$(printf '%s\n' "$update_tip_line" | sed -n "s/.* date='\([^']*\)'.*/\1/p" | sed 's/T/ /; s/Z$/ UTC/')
  log_headers=$(latest_block_tree_size_from_log)
  [ -z "$log_headers" ] && log_headers=$(latest_peer_blocks_from_log)

  [ -z "$blocks" ] && blocks="$log_blocks"
  [ -z "$headers" ] && headers="$log_headers"
  if [ -z "$progress" ] && [ -n "$log_progress" ]; then
    progress="$log_progress"
    progress_pct=$(awk -v p="$progress" 'BEGIN{printf "%.3f", (p+0)*100}')
    progress_ring=$(awk -v p="$progress_pct" 'BEGIN{if(p<0)p=0;if(p>100)p=100;printf "%.3f",p}')
  fi
  if { [ -z "$best_block_date" ] || [ "$best_block_date" = "-" ]; } && [ -n "$log_best_date" ]; then
    best_block_date="$log_best_date"
  fi
  [ -z "$pruned" ] && pruned=$(pruned_from_conf)
  if [ -z "$ibd" ] && [ -n "$progress" ]; then
    if awk -v p="$progress" 'BEGIN{exit !((p + 0) < 0.99999)}'; then
      ibd="true"
    else
      ibd="false"
    fi
  fi

  if [ -n "$headers" ] && [ -n "$blocks" ] && [ "$headers" != "-" ] && [ "$blocks" != "-" ]; then
    header_gap=$(awk -v h="$headers" -v b="$blocks" 'BEGIN{
      gap = (h + 0) - (b + 0)
      if (gap < 0) gap = 0
      printf "%.0f", gap
    }')
  fi
  if [ "$ibd" = "true" ] && [ -r "$DEBUG_LOG" ]; then
    stage_scan=$(tail -n 500 "$DEBUG_LOG" 2>/dev/null | awk '
      /Loading block index/ { key="loading_index"; line=$0; stage_nr=NR }
      /Loading block files/ { key="loading_blocks"; line=$0; stage_nr=NR }
      /Pre-synchronizing blockheaders, height: [0-9]+ \(~[0-9.]+%\)/ { key="pre_headers"; line=$0; stage_nr=NR }
      /Synchronizing blockheaders, height: [0-9]+ \(~[0-9.]+%\)/ { key="sync_headers"; line=$0; stage_nr=NR }
      /Verifying blocks/ { key="verifying_blocks"; line=$0; stage_nr=NR }
      /Rewinding blocks/ { key="rewinding_blocks"; line=$0; stage_nr=NR }
      /Pruning block files/ { key="pruning"; line=$0; stage_nr=NR }
      END {
        if (key != "") {
          print key
          if (line != "") print line
        }
      }')
    if [ -n "$stage_scan" ]; then
      ibd_stage_key=$(printf '%s\n' "$stage_scan" | sed -n '1p')
      stage_line=$(printf '%s\n' "$stage_scan" | sed -n '2p')
      case "$ibd_stage_key" in
        pre_headers)
          ibd_stage_label="Pre-sync blockheaders"
          stage_height_num=$(printf '%s\n' "$stage_line" | sed -n 's/.*height: \([0-9][0-9]*\).*/\1/p')
          stage_pct_num=$(printf '%s\n' "$stage_line" | sed -n 's/.*(~\([0-9.][0-9.]*\)%).*/\1/p')
          ibd_stage_height="$stage_height_num"
          ibd_stage_pct="$stage_pct_num"
          [ -n "$ibd_stage_height" ] && ibd_stage_height=$(fmt_int "$ibd_stage_height")
          if [ -n "$ibd_stage_pct" ]; then
            progress_pct=$(awk -v p="$ibd_stage_pct" 'BEGIN{printf "%.3f", p+0}')
            progress_ring=$(awk -v p="$ibd_stage_pct" 'BEGIN{p=p+0;if(p<0)p=0;if(p>100)p=100;printf "%.3f",p}')
          fi
          if [ -n "$ibd_stage_height" ]; then
            ibd_stage_detail="height ${ibd_stage_height}"
          elif [ -n "$ibd_stage_pct" ]; then
            ibd_stage_detail="${ibd_stage_pct}%"
          fi
        ;;
        sync_headers)
          ibd_stage_label="Synchronizing blockheaders"
          stage_height_num=$(printf '%s\n' "$stage_line" | sed -n 's/.*height: \([0-9][0-9]*\).*/\1/p')
          stage_pct_num=$(printf '%s\n' "$stage_line" | sed -n 's/.*(~\([0-9.][0-9.]*\)%).*/\1/p')
          ibd_stage_height="$stage_height_num"
          ibd_stage_pct="$stage_pct_num"
          [ -n "$ibd_stage_height" ] && ibd_stage_height=$(fmt_int "$ibd_stage_height")
          if [ -n "$ibd_stage_pct" ]; then
            progress_pct=$(awk -v p="$ibd_stage_pct" 'BEGIN{printf "%.3f", p+0}')
            progress_ring=$(awk -v p="$ibd_stage_pct" 'BEGIN{p=p+0;if(p<0)p=0;if(p>100)p=100;printf "%.3f",p}')
          fi
          if [ -n "$ibd_stage_height" ]; then
            ibd_stage_detail="height ${ibd_stage_height}"
          fi
        ;;
        loading_index)
          ibd_stage_label="Loading block index"
          ibd_stage_detail="building block index"
        ;;
        loading_blocks)
          ibd_stage_label="Loading block files"
          ibd_stage_detail="loading block files"
        ;;
        verifying_blocks)
          ibd_stage_label="Verifying blocks"
          ibd_stage_detail="verifying chain state"
        ;;
        rewinding_blocks)
          ibd_stage_label="Rewinding blocks"
          ibd_stage_detail="rewinding chain state"
        ;;
        pruning)
          ibd_stage_label="Pruning blocks"
          ibd_stage_detail="pruning old block data"
        ;;
      esac
    fi

    if [ -n "$stage_height_num" ] && [ -n "$headers" ] && [ "$headers" != "-" ]; then
      stage_delta=$(awk -v h="$headers" -v s="$stage_height_num" 'BEGIN{
        d = (h + 0) - (s + 0)
        if (d < 0) d = -d
        printf "%.0f", d
      }')
      if [ "$stage_delta" -le 8192 ] 2>/dev/null; then
        header_stage_fresh="1"
      fi
    fi

    case "$ibd_stage_key" in
      pre_headers|sync_headers)
        if [ "$header_stage_fresh" = "1" ] && [ -n "$stage_pct_num" ]; then
          if awk -v p="$stage_pct_num" 'BEGIN{exit !((p + 0) < 99.9)}'; then
            header_stage_active="1"
          fi
        fi
      ;;
    esac

    block_sync_needed=""
    if [ "$header_gap" -gt 0 ] 2>/dev/null; then
      if [ -z "$stage_scan" ]; then
        block_sync_needed="1"
      else
        case "$ibd_stage_key" in
          pre_headers|sync_headers)
            if [ "$header_stage_active" != "1" ]; then
              block_sync_needed="1"
            fi
          ;;
        esac
      fi
    fi

    # Header-stage log lines become stale once header sync is effectively done.
    # At that point the live RPC counters should drive the UI back to block sync.
    if [ "$block_sync_needed" = "1" ]; then
      ibd_stage_key="sync_blocks"
      ibd_stage_label="Synchronizing blocks"
      ibd_stage_height=$(fmt_int "$blocks")
      ibd_stage_pct=""
      ibd_stage_detail="headers $(fmt_int "$headers")"
      [ -n "$progress" ] && progress_pct=$(echo "$progress" | awk '{printf "%.3f", $1*100}')
      progress_ring=$(awk -v p="$progress_pct" 'BEGIN{p=p+0;if(p<0)p=0;if(p>100)p=100;printf "%.3f",p}')
    fi
  fi

  if [ "$ibd" = "true" ] && [ "$ibd_stage_height" = "-" -o -z "$ibd_stage_height" ] && [ -n "$blocks" ] && [ "$blocks" != "-" ]; then
    ibd_stage_key="sync_blocks"
    ibd_stage_label="Synchronizing blocks"
    ibd_stage_height=$(fmt_int "$blocks")
    ibd_stage_detail="headers $(fmt_int "$headers")"
  fi
fi
headers_fmt=$(fmt_int "$headers")
blocks_fmt=$(fmt_int "$blocks")
difficulty_display=$(compact_metric "$difficulty")
[ -z "$connections" ] && connections="-"
[ -z "$difficulty_display" ] && difficulty_display="-"
btc_context=$(printf '%s %s' "$chain" "$subversion" | xargs)
[ -z "$btc_context" ] && btc_context="-"

if [ "$btc_running" = "0" ]; then
  sync_key=btc_not_running; sync_class=err; sync_detail="bitcoind not running"
elif [ "$ibd" = "true" ]; then
  sync_key="$ibd_stage_key"; sync_class=warn
  sync_detail="$ibd_stage_label"
else
  sync_key=synced; sync_class=ok; sync_detail="initial sync complete"
fi

if [ "$pruned" = "true" ]; then prune_key=pruned_mode; prune_class=soft
else prune_key=full_mode; prune_class=ok; fi

# ============ Mining / Stratum ============
ckpool_running=0; ckpool_tail=""
CKPOOL_CONF=/root/ckpool/ckpool.conf
btc_address=$(grep -m1 '"btcaddress"' "$CKPOOL_CONF" 2>/dev/null | cut -d'"' -f4)
[ -z "$btc_address" ] && btc_address="-"
ckpool_rpc_url=$(grep -m1 '"url"' "$CKPOOL_CONF" 2>/dev/null | cut -d'"' -f4)
stratum_listen=$(grep -A2 '"serverurl"' "$CKPOOL_CONF" 2>/dev/null | grep -m1 '"[^"]*:[0-9][0-9]*"' | cut -d'"' -f2)
stratum_port=$(printf '%s\n' "$stratum_listen" | awk -F: 'NF{print $NF}' | head -n1)
[ -z "$stratum_port" ] && stratum_port=3333
case "$ckpool_rpc_url" in
  127.0.0.1:*|localhost:*|::1:*) stratum_key=solo ;;
  "") stratum_key=unknown ;;
  *)  stratum_key=pool ;;
esac
if ps | grep -v grep | grep -q ckpool; then
  ckpool_running=1
  ckpool_tail=$(tail -8 /root/ckpool/logs/ckpool.log 2>/dev/null)
fi
if [ "$ckpool_running" = "1" ]; then ckpool_class=ok; ckpool_state=running
else ckpool_class=err; ckpool_state=stopped; fi
[ -z "$ckpool_tail" ] && ckpool_tail='-'

# ============ Network ============
wifi_ssid=$(iwconfig 2>/dev/null | grep -o 'ESSID:"[^"]*"' | cut -d'"' -f2 | head -n1)
[ -z "$wifi_ssid" ] && wifi_ssid="-"
ip_addr=$(ip addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1 | head -n1)
iface=wlan0
if [ -z "$ip_addr" ]; then iface=eth0; ip_addr=$(ip addr show eth0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1 | head -n1); fi
[ -z "$ip_addr" ] && ip_addr="N/A"
mac_addr=$(cat /sys/class/net/wlan0/address 2>/dev/null | tr 'a-z' 'A-Z')
[ -z "$mac_addr" ] && mac_addr=$(cat /sys/class/net/eth0/address 2>/dev/null | tr 'a-z' 'A-Z')
[ -z "$mac_addr" ] && mac_addr="N/A"
rssi_dbm=$(iw dev wlan0 link 2>/dev/null | awk '/signal:/{print int($2); exit}')
if [ -z "$rssi_dbm" ]; then rssi_dbm=$(iwconfig wlan0 2>/dev/null | awk -F'[ =]+' '/Signal level=/{print int($7); exit}'); fi
if echo "$rssi_dbm" | grep -Eq '^-?[0-9]+$'; then
  rssi_display="${rssi_dbm} dBm"
  rssi_pct=$(awk -v s="$rssi_dbm" 'BEGIN{p=(s+100)*2;if(p<0)p=0;if(p>100)p=100;printf "%.0f",p}')
else
  rssi_display="N/A"; rssi_pct=0
fi
if [ "$rssi_pct" -ge 76 ]; then signal_level=4
elif [ "$rssi_pct" -ge 51 ]; then signal_level=3
elif [ "$rssi_pct" -ge 26 ]; then signal_level=2
elif [ "$rssi_pct" -ge 1 ]; then signal_level=1
else signal_level=0; fi

stratum_host="$ip_addr"
case "$stratum_host" in
  ""|N/A) stratum_host="<device-ip>" ;;
esac
stratum_url="stratum+tcp://${stratum_host}:${stratum_port}"
stratum_reach="${stratum_host}:${stratum_port}"
stratum_user_hint="wallet.workername"
stratum_pass_hint="(leave blank)"
stratum_recommended="LAN miners -> local Stratum"

ntp_running=0; ps | grep -v grep | grep -q ntpd && ntp_running=1
if [ "$ntp_running" = "1" ]; then ntp_key=running; ntp_class=ok
else ntp_key=not_synced; ntp_class=err; fi

# ============ Output JSON ============
OUTPUT_FILE="/tmp/status-api.$$"
{
echo "Content-Type: application/json; charset=utf-8"
echo ""

printf '{\n'

# system
printf '  "system": {\n'
printf '    "cpu_model": %s,\n' "$(json_str "$cpu_model")"
printf '    "uptime": %s,\n' "$(json_str "$uptime_fmt")"
printf '    "temp": %s,\n' "$(json_str "$temp")"
printf '    "load_pct": %s,\n' "$(json_num "$load_pct")"
printf '    "load_display": %s,\n' "$(json_str "$load_display")"
printf '    "load_avg_1": %s,\n' "$(json_str "$load_avg_1_display")"
printf '    "load_avg_5": %s,\n' "$(json_str "$load_avg_5_display")"
printf '    "load_avg_15": %s,\n' "$(json_str "$load_avg_15_display")"
printf '    "load_avg_display": %s,\n' "$(json_str "$load_avg_display")"
printf '    "mem_pct": %s,\n' "$(json_num "$mem_pct")"
printf '    "mem_used_display": %s,\n' "$(json_str "$mem_used_display")"
printf '    "mem_total_display": %s,\n' "$(json_str "$mem_total_display")"
printf '    "disk_pct": %s,\n' "$(json_num "$disk_pct")"
printf '    "disk_size_info": %s,\n' "$(json_str "$disk_size_info")"
printf '    "swap_pct": %s,\n' "$(json_num "$swap_pct")"
printf '    "swap_used_display": %s,\n' "$(json_str "$swap_used_display")"
printf '    "swap_total_display": %s,\n' "$(json_str "$swap_total_display")"
printf '    "block_disk_found": %s,\n' "$block_disk_found"
printf '    "block_disk_value": %s,\n' "$(json_str "$block_disk_value")"
printf '    "block_disk_path": %s\n' "$(json_str "$block_disk_path")"
printf '  },\n'

# bitcoin
printf '  "bitcoin": {\n'
printf '    "running": %s,\n' "$btc_running"
printf '    "sync_key": %s,\n' "$(json_str "$sync_key")"
printf '    "sync_class": %s,\n' "$(json_str "$sync_class")"
printf '    "sync_detail": %s,\n' "$(json_str "$sync_detail")"
printf '    "prune_key": %s,\n' "$(json_str "$prune_key")"
printf '    "prune_class": %s,\n' "$(json_str "$prune_class")"
printf '    "progress_pct": %s,\n' "$(json_str "$progress_pct")"
printf '    "progress_ring": %s,\n' "$(json_num "$progress_ring")"
printf '    "ibd_stage_key": %s,\n' "$(json_str "$ibd_stage_key")"
printf '    "ibd_stage_label": %s,\n' "$(json_str "$ibd_stage_label")"
printf '    "ibd_stage_detail": %s,\n' "$(json_str "$ibd_stage_detail")"
printf '    "ibd_stage_height": %s,\n' "$(json_str "$ibd_stage_height")"
printf '    "ibd_stage_pct": %s,\n' "$(json_str "$ibd_stage_pct")"
printf '    "headers_fmt": %s,\n' "$(json_str "$headers_fmt")"
printf '    "blocks_fmt": %s,\n' "$(json_str "$blocks_fmt")"
printf '    "best_block_date": %s,\n' "$(json_str "$best_block_date")"
printf '    "btc_context": %s,\n' "$(json_str "$btc_context")"
printf '    "connections": %s,\n' "$(json_str "$connections")"
printf '    "difficulty_display": %s,\n' "$(json_str "$difficulty_display")"
printf '    "rss": %s\n' "$(json_str "$rss")"
printf '  },\n'

# mining
printf '  "mining": {\n'
printf '    "ckpool_running": %s,\n' "$ckpool_running"
printf '    "ckpool_class": %s,\n' "$(json_str "$ckpool_class")"
printf '    "ckpool_state": %s,\n' "$(json_str "$ckpool_state")"
printf '    "stratum_key": %s,\n' "$(json_str "$stratum_key")"
printf '    "stratum_port": %s,\n' "$(json_num "$stratum_port")"
printf '    "stratum_url": %s,\n' "$(json_str "$stratum_url")"
printf '    "user_hint": %s,\n' "$(json_str "$stratum_user_hint")"
printf '    "pass_hint": %s,\n' "$(json_str "$stratum_pass_hint")"
printf '    "recommended_mode": %s,\n' "$(json_str "$stratum_recommended")"
printf '    "btc_address": %s,\n' "$(json_str "$btc_address")"
printf '    "ckpool_tail": %s\n' "$(json_str_ml "$ckpool_tail")"
printf '  },\n'

# network
printf '  "network": {\n'
printf '    "wifi_ssid": %s,\n' "$(json_str "$wifi_ssid")"
printf '    "ip_addr": %s,\n' "$(json_str "$ip_addr")"
printf '    "iface": %s,\n' "$(json_str "$iface")"
printf '    "mac_addr": %s,\n' "$(json_str "$mac_addr")"
printf '    "rssi_display": %s,\n' "$(json_str "$rssi_display")"
printf '    "rssi_pct": %s,\n' "$(json_num "$rssi_pct")"
printf '    "signal_level": %s,\n' "$signal_level"
printf '    "stratum_reach": %s,\n' "$(json_str "$stratum_reach")"
printf '    "ntp_class": %s,\n' "$(json_str "$ntp_class")"
printf '    "ntp_key": %s\n' "$(json_str "$ntp_key")"
printf '  }\n'

printf '}\n'
} > "$OUTPUT_FILE"

cp "$OUTPUT_FILE" "$CACHE_FILE"
date +%s > "$CACHE_STAMP"
cat "$OUTPUT_FILE"
rm -f "$OUTPUT_FILE"
