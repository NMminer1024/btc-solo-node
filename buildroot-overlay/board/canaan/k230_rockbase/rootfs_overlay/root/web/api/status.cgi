#!/bin/sh
# NM-miner JSON Status API
# Outputs all dashboard data as JSON for AJAX consumption

CONF=/root/bitcoind/bitcoin.conf
DATADIR=/root/block
btc_cli(){ bitcoin-cli -conf="$CONF" -datadir="$DATADIR" "$@" 2>/dev/null || echo "{}"; }

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
nproc=$(grep -c '^processor' /proc/cpuinfo 2>/dev/null || echo 1)
load1=$(awk '{print $1}' /proc/loadavg)
load_pct=$(awk -v l="$load1" -v n="$nproc" 'BEGIN{printf "%.0f", l/n*100}')
if [ "$load_pct" -lt 0 ] 2>/dev/null; then load_pct=0; elif [ "$load_pct" -gt 100 ] 2>/dev/null; then load_pct=100; fi
load_display="${load1} / ${nproc} cores"

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
if [ -d "$DATADIR" ]; then
  block_disk_mb=$(du -sm "$DATADIR" 2>/dev/null | awk 'NR==1{print $1}')
  if [ -n "$block_disk_mb" ]; then
    block_disk_value=$(compact_size_mb "$block_disk_mb")
  fi
else
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

if pidof bitcoind >/dev/null 2>&1; then
  btc_running=1
  btc_info=$(btc_cli getblockchaininfo)
  btc_net=$(btc_cli getnetworkinfo)
  headers=$(echo "$btc_info" | grep -o '"headers": *[0-9]*' | grep -o '[0-9]*')
  blocks=$(echo "$btc_info" | grep -o '"blocks": *[0-9]*' | grep -o '[0-9]*')
  progress=$(echo "$btc_info" | grep -o '"verificationprogress": *[0-9.e+-]*' | cut -d: -f2)
  [ -n "$progress" ] && progress_pct=$(echo "$progress" | awk '{printf "%.3f", $1*100}')
  progress_ring=$(awk -v p="$progress_pct" 'BEGIN{if(p<0)p=0;if(p>100)p=100;printf "%.3f",p}')
  ibd=$(echo "$btc_info" | grep -o '"initialblockdownload": *[a-z]*' | grep -o 'true\|false')
  chain=$(echo "$btc_info" | grep -o '"chain": *"[^"]*"' | cut -d'"' -f4)
  difficulty=$(echo "$btc_info" | grep -o '"difficulty": *[0-9.]*' | cut -d: -f2 | xargs)
  connections=$(echo "$btc_net" | grep -o '"connections": *[0-9]*' | grep -o '[0-9]*')
  subversion=$(echo "$btc_net" | grep -o '"subversion": *"[^"]*"' | cut -d'"' -f4)
  pruned=$(echo "$btc_info" | grep -o '"pruned": *[a-z]*' | grep -o 'true\|false')
  btc_pid=$(pidof bitcoind 2>/dev/null | awk '{print $1}')
  [ -n "$btc_pid" ] && rss=$(awk '/^VmRSS/{printf "%.0fM",$2/1024}' "/proc/$btc_pid/status" 2>/dev/null)
  if [ -n "$blocks" ] && [ "$blocks" != "-" ]; then
    best_block_hash=$(btc_cli getblockhash "$blocks" | tr -d '"')
    best_block_time=$(btc_cli getblockheader "$best_block_hash" 2>/dev/null | grep -o '"time": *[0-9]*' | grep -o '[0-9]*' | head -n1)
    [ -n "$best_block_time" ] && best_block_date=$(date -u -d "@$best_block_time" '+%Y-%m-%d %H:%M:%S UTC' 2>/dev/null || echo "-")
  fi
fi
headers_fmt=$(fmt_int "$headers")
blocks_fmt=$(fmt_int "$blocks")
difficulty_display=$(compact_metric "$difficulty")
[ -z "$connections" ] && connections="-"
[ -z "$difficulty_display" ] && difficulty_display="-"
btc_context=$(printf '%s %s' "$chain" "$subversion" | xargs)
[ -z "$btc_context" ] && btc_context="-"

if [ "$btc_running" = "0" ]; then sync_key=btc_not_running; sync_class=err
elif [ "$ibd" = "true" ]; then sync_key=syncing; sync_class=warn
else sync_key=synced; sync_class=ok; fi

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
printf '    "prune_key": %s,\n' "$(json_str "$prune_key")"
printf '    "prune_class": %s,\n' "$(json_str "$prune_class")"
printf '    "progress_pct": %s,\n' "$(json_str "$progress_pct")"
printf '    "progress_ring": %s,\n' "$(json_num "$progress_ring")"
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
