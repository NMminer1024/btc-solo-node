#!/bin/sh
#
# Rockbase async status cache daemon
# - refreshes status shards in the background
# - renders /tmp/status-api.json for the web UI
#

CONF=/root/bitcoind/bitcoin.conf
DATADIR=/root/block
DEBUG_LOG=/root/block/debug.log
CKPOOL_CONF=/root/ckpool/ckpool.conf
CKPOOL_LOG=/root/ckpool/logs/ckpool.log
STATE_DIR=/tmp/rockbase-status
FINAL_CACHE=/tmp/status-api.json
PIDFILE=/var/run/rockbase-statusd.pid
LOCK_DIR=/var/run/rockbase-statusd.lock
BTC_RPC_TIMEOUT=60
CONN_RPC_TIMEOUT=2
SYSTEM_INTERVAL=15
BITCOIN_CHAIN_INTERVAL=600
BITCOIN_CHAIN_IBD_INTERVAL=60
BITCOIN_CHAIN_RETRY_INTERVAL=20
BITCOIN_CONN_INTERVAL=45
BITCOIN_LOG_INTERVAL=20
STRATUM_INTERVAL=30
NETWORK_INTERVAL=20

SYSTEM_STATE=${STATE_DIR}/system.state
BITCOIN_CHAIN_STATE=${STATE_DIR}/bitcoin-chain.state
BITCOIN_CONN_STATE=${STATE_DIR}/bitcoin-conn.state
BITCOIN_LOG_STATE=${STATE_DIR}/bitcoin-log.state
STRATUM_STATE=${STATE_DIR}/stratum.state
NETWORK_STATE=${STATE_DIR}/network.state

btc_cli() {
    bitcoin-cli -rpcclienttimeout="${BTC_RPC_TIMEOUT}" -conf="${CONF}" -datadir="${DATADIR}" "$@" 2>/dev/null
}

btc_conn_cli() {
    bitcoin-cli -rpcclienttimeout="${CONN_RPC_TIMEOUT}" -conf="${CONF}" -datadir="${DATADIR}" "$@" 2>/dev/null
}

now_epoch() {
    date +%s 2>/dev/null || echo 0
}

ensure_state_dir() {
    mkdir -p "${STATE_DIR}"
}

shell_quote() {
    printf "'"
    printf '%s' "$1" | sed "s/'/'\\\\''/g"
    printf "'"
}

state_str() {
    printf "%s=%s\n" "$1" "$(shell_quote "$2")"
}

state_num() {
    v="$2"
    [ -n "$v" ] || v=0
    printf "%s=%s\n" "$1" "$v"
}

state_raw() {
    printf "%s=%s\n" "$1" "$2"
}

write_state_file() {
    target="$1"
    tmp="${target}.$$"
    cat > "${tmp}" || {
        rm -f "${tmp}"
        return 1
    }
    mv "${tmp}" "${target}"
}

json_str() {
    printf '"'
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g' | tr -d '\n'
    printf '"'
}

json_str_ml() {
    printf '"'
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g' | awk '{printf "%s\\n", $0}' | tr -d '\n' | sed 's/\\\\n$//'
    printf '"'
}

json_num() {
    v="$1"
    if [ -z "$v" ] || [ "$v" = "-" ] || [ "$v" = "null" ]; then
        printf 'null'
    else
        printf '%s' "$v"
    fi
}

compact_metric() {
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

compact_size_mb() {
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

fmt_int() {
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

height_progress_pct() {
    awk -v b="$1" -v h="$2" 'BEGIN {
      blocks = b + 0
      headers = h + 0
      if (headers <= 0 || blocks < 0) { print ""; exit }
      pct = blocks / headers * 100
      if (pct < 0) pct = 0
      if (pct > 100) pct = 100
      printf "%.6f", pct
    }'
}

verification_progress_pct() {
    awk -v p="$1" 'BEGIN {
      pct = (p + 0) * 100
      if (pct < 0) pct = 0
      if (pct > 100) pct = 100
      printf "%.6f", pct
    }'
}

ring_progress_pct() {
    awk -v p="$1" 'BEGIN {
      pct = p + 0
      if (pct < 0) pct = 0
      if (pct > 100) pct = 100
      printf "%.6f", pct
    }'
}

max_int() {
    max=""
    for v in "$@"; do
        case "${v}" in
            ''|'-'|'null') continue ;;
        esac
        if ! echo "${v}" | grep -Eq '^[0-9]+$'; then
            continue
        fi
        if [ -z "${max}" ] || [ "${v}" -gt "${max}" ]; then
            max="${v}"
        fi
    done
    printf '%s' "${max}"
}

pruned_from_conf() {
    prune_target=$(sed -n 's/^prune=\([0-9][0-9]*\).*/\1/p' "${CONF}" 2>/dev/null | head -n1)
    case "${prune_target}" in
        ''|0) echo false ;;
        *) echo true ;;
    esac
}

chain_from_conf() {
    chain_name=$(sed -n 's/^chain=\([A-Za-z0-9_-][A-Za-z0-9_-]*\).*/\1/p' "${CONF}" 2>/dev/null | head -n1)
    case "${chain_name}" in
        ''|main) echo main ;;
        *) echo "${chain_name}" ;;
    esac
}

latest_tip_date_from_log() {
    [ -r "${DEBUG_LOG}" ] || return 0
    tail -n 200 "${DEBUG_LOG}" 2>/dev/null | sed -n "s/.*date='\([^']*\)'.*/\1/p" | tail -n1 | sed 's/T/ /; s/Z$/ UTC/'
}

latest_update_tip_line() {
    [ -r "${DEBUG_LOG}" ] || return 0
    tail -n 400 "${DEBUG_LOG}" 2>/dev/null | awk '/UpdateTip: new best=/{line=$0} END{print line}'
}

latest_peer_blocks_from_log() {
    [ -r "${DEBUG_LOG}" ] || return 0
    tail -n 400 "${DEBUG_LOG}" 2>/dev/null | sed -n 's/.*blocks=\([0-9][0-9]*\).*/\1/p' | tail -n1
}

latest_block_tree_size_from_log() {
    [ -r "${DEBUG_LOG}" ] || return 0
    tail -n 400 "${DEBUG_LOG}" 2>/dev/null | sed -n 's/.*block tree size = \([0-9][0-9]*\).*/\1/p' | tail -n1
}

load_state_file() {
    [ -r "$1" ] && . "$1"
}

collect_system() {
    ensure_state_dir

    cpu_model=$(cat /proc/device-tree/model 2>/dev/null | tr '\0' ' ' | xargs)
    [ -z "${cpu_model}" ] && cpu_model=$(grep -m1 "uarch\|machine\|model name\|cpu model" /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs)
    [ -z "${cpu_model}" ] && cpu_model="K230 RISC-V"

    uptime_sec=$(awk -F. '{print $1}' /proc/uptime 2>/dev/null)
    [ -n "${uptime_sec}" ] || uptime_sec=0
    uptime_fmt=$(printf '%dd %02dh:%02dm' $((uptime_sec / 86400)) $((uptime_sec % 86400 / 3600)) $((uptime_sec % 3600 / 60)))

    set -- $(awk '/^cpu /{total=0; for(i=2;i<=8;i++) total+=$i; idle=$5+$6; print total, idle; exit}' /proc/stat 2>/dev/null)
    cpu_total_1=$1
    cpu_idle_1=$2
    [ -n "${cpu_total_1}" ] || cpu_total_1=0
    [ -n "${cpu_idle_1}" ] || cpu_idle_1=0
    sleep 0.2
    set -- $(awk '/^cpu /{total=0; for(i=2;i<=8;i++) total+=$i; idle=$5+$6; print total, idle; exit}' /proc/stat 2>/dev/null)
    cpu_total_2=$1
    cpu_idle_2=$2
    [ -n "${cpu_total_2}" ] || cpu_total_2=0
    [ -n "${cpu_idle_2}" ] || cpu_idle_2=0
    load_pct=$(awk -v t1="${cpu_total_1}" -v i1="${cpu_idle_1}" -v t2="${cpu_total_2}" -v i2="${cpu_idle_2}" 'BEGIN{
      dt = t2 - t1
      di = i2 - i1
      if (dt <= 0) { print 0; exit }
      pct = (dt - di) * 100 / dt
      if (pct < 0) pct = 0
      if (pct > 100) pct = 100
      printf "%.0f", pct
    }')
    set -- $(awk '{print $1, $2, $3}' /proc/loadavg 2>/dev/null)
    load_avg_1=$1
    load_avg_5=$2
    load_avg_15=$3
    [ -n "${load_avg_1}" ] || load_avg_1=0
    [ -n "${load_avg_5}" ] || load_avg_5=0
    [ -n "${load_avg_15}" ] || load_avg_15=0
    load_avg_1_display=$(awk -v v="${load_avg_1}" 'BEGIN{printf "%.2f", v + 0}')
    load_avg_5_display=$(awk -v v="${load_avg_5}" 'BEGIN{printf "%.2f", v + 0}')
    load_avg_15_display=$(awk -v v="${load_avg_15}" 'BEGIN{printf "%.2f", v + 0}')
    load_display="${load_pct}% busy"
    load_avg_display="1m ${load_avg_1_display} · 5m ${load_avg_5_display}"

    mem_total=$(awk '/^MemTotal/{printf "%.0f", $2/1024}' /proc/meminfo 2>/dev/null)
    mem_free=$(awk '/^MemAvailable/{printf "%.0f", $2/1024}' /proc/meminfo 2>/dev/null)
    [ -n "${mem_total}" ] || mem_total=0
    [ -n "${mem_free}" ] || mem_free=0
    mem_used=$((mem_total - mem_free))
    if [ "${mem_total}" -gt 0 ]; then mem_pct=$((mem_used * 100 / mem_total)); else mem_pct=0; fi

    swap_total=$(awk '/^SwapTotal/{printf "%.0f", $2/1024}' /proc/meminfo 2>/dev/null)
    swap_free=$(awk '/^SwapFree/{printf "%.0f", $2/1024}' /proc/meminfo 2>/dev/null)
    [ -n "${swap_total}" ] || swap_total=0
    [ -n "${swap_free}" ] || swap_free=0
    swap_used=$((swap_total - swap_free))
    if [ "${swap_total}" -gt 0 ]; then swap_pct=$((swap_used * 100 / swap_total)); else swap_pct=0; fi

    mem_used_display=$(compact_size_mb "${mem_used}")
    mem_total_display=$(compact_size_mb "${mem_total}")
    swap_used_display=$(compact_size_mb "${swap_used}")
    swap_total_display=$(compact_size_mb "${swap_total}")

    block_disk_found=true
    block_disk_path="${DATADIR}"
    [ -d "${DATADIR}" ] || block_disk_found=false

    rootfs_used_display=$(df -h / 2>/dev/null | awk 'NR==2{print $3}')
    rootfs_total_display=$(df -h / 2>/dev/null | awk 'NR==2{print $2}')
    [ -n "${rootfs_used_display}" ] || rootfs_used_display="-"
    [ -n "${rootfs_total_display}" ] || rootfs_total_display="-"
    rootfs_size_info="${rootfs_used_display} / ${rootfs_total_display}"
    rootfs_pct=$(df -h / 2>/dev/null | awk 'NR==2{gsub("%","",$5);print $5}')
    [ -n "${rootfs_pct}" ] || rootfs_pct=0

    disk_used_display=$(df -h "${DATADIR}" 2>/dev/null | awk 'NR==2{print $3}')
    disk_total_display=$(df -h "${DATADIR}" 2>/dev/null | awk 'NR==2{print $2}')
    disk_pct=$(df -h "${DATADIR}" 2>/dev/null | awk 'NR==2{gsub("%","",$5);print $5}')
    if [ -z "${disk_used_display}" ] || [ -z "${disk_total_display}" ]; then
        disk_used_display="${rootfs_used_display}"
        disk_total_display="${rootfs_total_display}"
        disk_pct="${rootfs_pct}"
    fi
    disk_size_info="${disk_used_display} / ${disk_total_display}"
    [ -n "${disk_pct}" ] || disk_pct=0

    temp="N/A"
    for t in /sys/class/thermal/thermal_zone*/temp /sys/class/hwmon/hwmon*/temp1_input; do
        [ -r "${t}" ] || continue
        raw=$(cat "${t}" 2>/dev/null)
        case "${raw}" in
            '')
                ;;
            *)
                if echo "${raw}" | grep -Eq '^[0-9]+$'; then
                    zone_type_file="${t%/temp}/type"
                    if [ -r "${zone_type_file}" ] && grep -qx 'canaan_thermal_zone' "${zone_type_file}" 2>/dev/null; then
                        ts_code=$((raw & 4095))
                        temp=$(awk -v code="${ts_code}" 'BEGIN{
                          c = code + 0
                          v = (1.01472e-10 * c * c * c * c) - (1.10063e-6 * c * c * c) + (4.36150e-3 * c * c) - (7.10128 * c) + 3565.87
                          printf "%.1fC", v
                        }')
                        break
                    fi
                fi
                if [ "${raw}" -ge 100000 ] 2>/dev/null; then temp=$(awk -v r="${raw}" 'BEGIN{printf "%.1fC", r/1000}'); break; fi
                if [ "${raw}" -ge 1000 ] 2>/dev/null; then temp=$(awk -v r="${raw}" 'BEGIN{printf "%.1fC", r/100}'); break; fi
                if echo "${raw}" | grep -Eq '^[0-9]+$'; then temp=$(awk -v r="${raw}" 'BEGIN{printf "%.1fC", r}'); break; fi
                ;;
        esac
    done

    {
        state_num updated_at "$(now_epoch)"
        state_str cpu_model "${cpu_model}"
        state_str uptime_fmt "${uptime_fmt}"
        state_str temp "${temp}"
        state_num load_pct "${load_pct}"
        state_str load_display "${load_display}"
        state_str load_avg_1_display "${load_avg_1_display}"
        state_str load_avg_5_display "${load_avg_5_display}"
        state_str load_avg_15_display "${load_avg_15_display}"
        state_str load_avg_display "${load_avg_display}"
        state_num mem_pct "${mem_pct}"
        state_str mem_used_display "${mem_used_display}"
        state_str mem_total_display "${mem_total_display}"
        state_num swap_pct "${swap_pct}"
        state_str swap_used_display "${swap_used_display}"
        state_str swap_total_display "${swap_total_display}"
        state_num disk_pct "${disk_pct}"
        state_str disk_size_info "${disk_size_info}"
        state_num rootfs_pct "${rootfs_pct}"
        state_str rootfs_size_info "${rootfs_size_info}"
        state_raw block_disk_found "${block_disk_found}"
        state_str block_disk_path "${block_disk_path}"
    } | write_state_file "${SYSTEM_STATE}"
}

collect_bitcoin_chain() {
    ensure_state_dir
    pidof bitcoind >/dev/null 2>&1 || return 1

    btc_info=$(btc_cli getblockchaininfo) || return 1
    chain=$(echo "${btc_info}" | grep -o '"chain": *"[^"]*"' | cut -d'"' -f4)
    blocks=$(echo "${btc_info}" | grep -o '"blocks": *[0-9]*' | cut -d: -f2 | xargs)
    headers=$(echo "${btc_info}" | grep -o '"headers": *[0-9]*' | cut -d: -f2 | xargs)
    progress=$(echo "${btc_info}" | grep -o '"verificationprogress": *[0-9.eE+-]*' | cut -d: -f2 | xargs)
    ibd=$(echo "${btc_info}" | grep -o '"initialblockdownload": *[a-z]*' | grep -o 'true\|false')
    difficulty=$(echo "${btc_info}" | grep -o '"difficulty": *[0-9.]*' | cut -d: -f2 | xargs)
    block_disk_bytes=$(echo "${btc_info}" | grep -o '"size_on_disk": *[0-9]*' | grep -o '[0-9]*')
    if [ -n "${block_disk_bytes}" ]; then
        block_disk_mb=$(awk -v b="${block_disk_bytes}" 'BEGIN{printf "%.0f", (b+0)/1048576}')
        block_disk_value=$(compact_size_mb "${block_disk_mb}")
    else
        block_disk_value="-"
    fi
    pruned=$(echo "${btc_info}" | grep -o '"pruned": *[a-z]*' | grep -o 'true\|false')
    [ -n "${chain}" ] || chain=""
    [ -n "${blocks}" ] || blocks="-"
    [ -n "${headers}" ] || headers="-"
    [ -n "${progress}" ] || progress=""
    [ -n "${ibd}" ] || ibd=""
    [ -n "${difficulty}" ] || difficulty="-"
    [ -n "${pruned}" ] || pruned=$(pruned_from_conf)

    {
        state_num updated_at "$(now_epoch)"
        state_str chain "${chain}"
        state_str blocks "${blocks}"
        state_str headers "${headers}"
        state_str progress "${progress}"
        state_str ibd "${ibd}"
        state_str difficulty "${difficulty}"
        state_str pruned "${pruned}"
        state_str block_disk_value "${block_disk_value}"
    } | write_state_file "${BITCOIN_CHAIN_STATE}"
}

collect_bitcoin_conn() {
    ensure_state_dir

    if pidof bitcoind >/dev/null 2>&1; then
        connections=$(btc_conn_cli getconnectioncount | tr -dc '0-9')
    else
        connections=""
    fi
    if [ -z "${connections}" ]; then
        connections=$(netstat -tn 2>/dev/null | awk '/ESTABLISHED/ && ($4 ~ /:8333$/ || $5 ~ /:8333$/) { c++ } END { print c + 0 }')
    fi
    [ -n "${connections}" ] || connections=0

    {
        state_num updated_at "$(now_epoch)"
        state_str connections "${connections}"
    } | write_state_file "${BITCOIN_CONN_STATE}"
}

collect_bitcoin_log() {
    ensure_state_dir

    best_block_date=$(latest_tip_date_from_log)
    update_tip_line=$(latest_update_tip_line)
    log_blocks=$(printf '%s\n' "${update_tip_line}" | sed -n 's/.* height=\([0-9][0-9]*\).*/\1/p')
    log_progress=$(printf '%s\n' "${update_tip_line}" | sed -n "s/.* progress=\([0-9.][0-9.]*\).*/\1/p")
    log_best_date=$(printf '%s\n' "${update_tip_line}" | sed -n "s/.* date='\([^']*\)'.*/\1/p" | sed 's/T/ /; s/Z$/ UTC/')
    [ -n "${log_best_date}" ] && best_block_date="${log_best_date}"
    stage_scan=$(tail -n 500 "${DEBUG_LOG}" 2>/dev/null | awk '
      /Loading block index/ { key="loading_index"; line=$0 }
      /Loading block files/ { key="loading_blocks"; line=$0 }
      /Pre-synchronizing blockheaders, height: [0-9]+ \(~[0-9.]+%\)/ { key="pre_headers"; line=$0 }
      /Synchronizing blockheaders, height: [0-9]+ \(~[0-9.]+%\)/ { key="sync_headers"; line=$0 }
      /Verifying blocks/ { key="verifying_blocks"; line=$0 }
      /Rewinding blocks/ { key="rewinding_blocks"; line=$0 }
      /Pruning block files/ { key="pruning"; line=$0 }
      END {
        if (key != "") {
          print key
          if (line != "") print line
        }
      }')

    stage_key=$(printf '%s\n' "${stage_scan}" | sed -n '1p')
    stage_line=$(printf '%s\n' "${stage_scan}" | sed -n '2p')
    stage_height_num=""
    stage_pct_num=""
    case "${stage_key}" in
        pre_headers|sync_headers)
            stage_height_num=$(printf '%s\n' "${stage_line}" | sed -n 's/.*height: \([0-9][0-9]*\).*/\1/p')
            stage_pct_num=$(printf '%s\n' "${stage_line}" | sed -n 's/.*(~\([0-9.][0-9.]*\)%).*/\1/p')
            ;;
    esac

    tree_headers=$(latest_block_tree_size_from_log)
    peer_headers=$(latest_peer_blocks_from_log)
    log_headers=$(max_int "${tree_headers}" "${peer_headers}" "${stage_height_num}")

    {
        state_num updated_at "$(now_epoch)"
        state_str best_block_date "${best_block_date}"
        state_str log_blocks "${log_blocks}"
        state_str log_progress "${log_progress}"
        state_str log_headers "${log_headers}"
        state_str stage_key "${stage_key}"
        state_str stage_line "${stage_line}"
        state_str stage_height_num "${stage_height_num}"
        state_str stage_pct_num "${stage_pct_num}"
    } | write_state_file "${BITCOIN_LOG_STATE}"
}

collect_stratum() {
    ensure_state_dir

    ckpool_running=0
    btc_address=$(grep -m1 '"btcaddress"' "${CKPOOL_CONF}" 2>/dev/null | cut -d'"' -f4)
    [ -n "${btc_address}" ] || btc_address="-"
    ckpool_rpc_url=$(grep -m1 '"url"' "${CKPOOL_CONF}" 2>/dev/null | cut -d'"' -f4)
    stratum_listen=$(grep -A2 '"serverurl"' "${CKPOOL_CONF}" 2>/dev/null | grep -m1 '"[^"]*:[0-9][0-9]*"' | cut -d'"' -f2)
    stratum_port=$(printf '%s\n' "${stratum_listen}" | awk -F: 'NF{print $NF}' | head -n1)
    [ -n "${stratum_port}" ] || stratum_port=3333

    case "${ckpool_rpc_url}" in
        127.0.0.1:*|localhost:*|::1:*) stratum_key=solo ;;
        "") stratum_key=unknown ;;
        *) stratum_key=pool ;;
    esac

    if pidof ckpool >/dev/null 2>&1; then
        ckpool_running=1
    fi
    if [ "${ckpool_running}" = "1" ]; then
        ckpool_class=ok
        ckpool_state=running
    else
        ckpool_class=err
        ckpool_state=stopped
    fi

    {
        state_num updated_at "$(now_epoch)"
        state_num ckpool_running "${ckpool_running}"
        state_str ckpool_class "${ckpool_class}"
        state_str ckpool_state "${ckpool_state}"
        state_str stratum_key "${stratum_key}"
        state_str stratum_port "${stratum_port}"
        state_str btc_address "${btc_address}"
    } | write_state_file "${STRATUM_STATE}"
}

collect_network() {
    ensure_state_dir

    wifi_ssid=$(iwconfig 2>/dev/null | grep -o 'ESSID:"[^"]*"' | cut -d'"' -f2 | head -n1)
    [ -n "${wifi_ssid}" ] || wifi_ssid="-"
    ip_addr=$(ip addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1 | head -n1)
    iface=wlan0
    if [ -z "${ip_addr}" ]; then
        iface=eth0
        ip_addr=$(ip addr show eth0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1 | head -n1)
    fi
    [ -n "${ip_addr}" ] || ip_addr="N/A"
    mac_addr=$(cat /sys/class/net/wlan0/address 2>/dev/null | tr 'a-z' 'A-Z')
    [ -n "${mac_addr}" ] || mac_addr=$(cat /sys/class/net/eth0/address 2>/dev/null | tr 'a-z' 'A-Z')
    [ -n "${mac_addr}" ] || mac_addr="N/A"

    rssi_dbm=$(iw dev wlan0 link 2>/dev/null | awk '/signal:/{print int($2); exit}')
    if [ -z "${rssi_dbm}" ]; then
        rssi_dbm=$(iwconfig wlan0 2>/dev/null | awk -F'[ =]+' '/Signal level=/{print int($7); exit}')
    fi
    if echo "${rssi_dbm}" | grep -Eq '^-?[0-9]+$'; then
        rssi_display="${rssi_dbm} dBm"
        rssi_pct=$(awk -v s="${rssi_dbm}" 'BEGIN{p=(s+100)*2;if(p<0)p=0;if(p>100)p=100;printf "%.0f",p}')
    else
        rssi_display="N/A"
        rssi_pct=0
    fi
    if [ "${rssi_pct}" -ge 76 ]; then signal_level=4
    elif [ "${rssi_pct}" -ge 51 ]; then signal_level=3
    elif [ "${rssi_pct}" -ge 26 ]; then signal_level=2
    elif [ "${rssi_pct}" -ge 1 ]; then signal_level=1
    else signal_level=0
    fi

    ntp_running=0
    ps | grep -v grep | grep -q ntpd && ntp_running=1
    if [ "${ntp_running}" = "1" ]; then
        ntp_key=running
        ntp_class=ok
    else
        ntp_key=not_synced
        ntp_class=err
    fi

    {
        state_num updated_at "$(now_epoch)"
        state_str wifi_ssid "${wifi_ssid}"
        state_str ip_addr "${ip_addr}"
        state_str iface "${iface}"
        state_str mac_addr "${mac_addr}"
        state_str rssi_display "${rssi_display}"
        state_num rssi_pct "${rssi_pct}"
        state_num signal_level "${signal_level}"
        state_str ntp_key "${ntp_key}"
        state_str ntp_class "${ntp_class}"
    } | write_state_file "${NETWORK_STATE}"
}

render_status_json() {
    cpu_model="K230 RISC-V"
    uptime_fmt="-"
    temp="N/A"
    load_pct=0
    load_display="0% busy"
    load_avg_1_display="-"
    load_avg_5_display="-"
    load_avg_15_display="-"
    load_avg_display="1m - · 5m -"
    mem_pct=0
    mem_used_display="-"
    mem_total_display="-"
    disk_pct=0
    disk_size_info="-"
    rootfs_pct=0
    rootfs_size_info="-"
    swap_pct=0
    swap_used_display="-"
    swap_total_display="-"
    block_disk_found=true
    block_disk_path="${DATADIR}"

    headers="-"
    blocks="-"
    progress=""
    ibd=""
    chain=""
    difficulty="-"
    pruned=""
    block_disk_value="-"
    connections="-"
    best_block_date="-"
    log_blocks=""
    log_progress=""
    log_headers=""
    stage_key=""
    stage_line=""
    stage_height_num=""
    stage_pct_num=""

    ckpool_running=0
    ckpool_class=err
    ckpool_state=stopped
    stratum_key=unknown
    stratum_port=3333
    btc_address="-"

    wifi_ssid="-"
    ip_addr="N/A"
    iface="wlan0"
    mac_addr="N/A"
    rssi_display="N/A"
    rssi_pct=0
    signal_level=0
    ntp_key=not_synced
    ntp_class=err

    load_state_file "${SYSTEM_STATE}"
    load_state_file "${BITCOIN_CHAIN_STATE}"
    load_state_file "${BITCOIN_CONN_STATE}"
    load_state_file "${BITCOIN_LOG_STATE}"
    load_state_file "${STRATUM_STATE}"
    load_state_file "${NETWORK_STATE}"

    btc_running=0
    pidof bitcoind >/dev/null 2>&1 && btc_running=1
    if [ "${btc_running}" = "1" ]; then
        btc_pid=$(pidof bitcoind 2>/dev/null | awk '{print $1}')
        rss=$(awk '/^VmRSS/{printf "%.0fM",$2/1024}' "/proc/${btc_pid}/status" 2>/dev/null)
        [ -n "${rss}" ] || rss="N/A"
    else
        rss="N/A"
    fi

    if [ -z "${blocks}" ] || [ "${blocks}" = "-" ]; then
        blocks="${log_blocks}"
    fi
    if [ -n "${blocks}" ] && [ -n "${log_blocks}" ] &&
       echo "${blocks}" | grep -Eq '^[0-9]+$' &&
       echo "${log_blocks}" | grep -Eq '^[0-9]+$' &&
       [ "${log_blocks}" -gt "${blocks}" ] 2>/dev/null; then
        blocks="${log_blocks}"
        [ -n "${log_progress}" ] && progress="${log_progress}"
    fi
    if [ -z "${headers}" ] || [ "${headers}" = "-" ]; then
        headers="${log_headers}"
    fi
    if [ -n "${headers}" ] && [ -n "${log_headers}" ] &&
       [ "${headers}" != "-" ] && [ "${log_headers}" != "-" ]; then
        headers=$(max_int "${headers}" "${log_headers}")
    fi
    if [ -z "${progress}" ] || [ "${progress}" = "-" ]; then
        progress="${log_progress}"
    fi
    if [ -n "${headers}" ] && [ -n "${blocks}" ] && [ "${headers}" != "-" ] && [ "${blocks}" != "-" ]; then
        if awk -v h="${headers}" -v b="${blocks}" 'BEGIN{exit !((h + 0) < (b + 0))}'; then
            headers="${blocks}"
        fi
    fi
    [ -n "${best_block_date}" ] || best_block_date="-"
    [ -n "${pruned}" ] || pruned=$(pruned_from_conf)
    [ -n "${chain}" ] || chain=$(chain_from_conf)

    progress_pct="0.000"
    progress_ring="0.000"
    if [ -n "${progress}" ]; then
        progress_pct=$(verification_progress_pct "${progress}")
        progress_ring=$(ring_progress_pct "${progress_pct}")
    fi

    if [ -z "${ibd}" ] && [ -n "${progress}" ]; then
        if awk -v p="${progress}" 'BEGIN{exit !((p + 0) < 0.99999)}'; then
            ibd=true
        else
            ibd=false
        fi
    fi

    header_gap=0
    height_based_progress=""
    if [ -n "${headers}" ] && [ -n "${blocks}" ] && [ "${headers}" != "-" ] && [ "${blocks}" != "-" ]; then
        header_gap=$(awk -v h="${headers}" -v b="${blocks}" 'BEGIN{
          gap = (h + 0) - (b + 0)
          if (gap < 0) gap = 0
          printf "%.0f", gap
        }')
        height_based_progress=$(height_progress_pct "${blocks}" "${headers}")
    fi

    ibd_stage_key="syncing"
    ibd_stage_label="IBD Syncing"
    ibd_stage_detail=""
    ibd_stage_height="-"
    ibd_stage_pct=""

    if [ "${ibd}" = "true" ]; then
        case "${stage_key}" in
            pre_headers)
                ibd_stage_key="pre_headers"
                ibd_stage_label="Pre-sync blockheaders"
                ibd_stage_height="${stage_height_num}"
                ibd_stage_pct="${stage_pct_num}"
                [ -n "${ibd_stage_height}" ] && ibd_stage_height=$(fmt_int "${ibd_stage_height}")
                if [ -n "${ibd_stage_pct}" ]; then
                    progress_pct=$(awk -v p="${ibd_stage_pct}" 'BEGIN{printf "%.3f", p+0}')
                    progress_ring=$(awk -v p="${ibd_stage_pct}" 'BEGIN{p=p+0;if(p<0)p=0;if(p>100)p=100;printf "%.3f",p}')
                fi
                if [ -n "${ibd_stage_height}" ] && [ "${ibd_stage_height}" != "-" ]; then
                    ibd_stage_detail="height ${ibd_stage_height}"
                elif [ -n "${ibd_stage_pct}" ]; then
                    ibd_stage_detail="${ibd_stage_pct}%"
                fi
                ;;
            sync_headers)
                ibd_stage_key="sync_headers"
                ibd_stage_label="Synchronizing blockheaders"
                ibd_stage_height="${stage_height_num}"
                ibd_stage_pct="${stage_pct_num}"
                [ -n "${ibd_stage_height}" ] && ibd_stage_height=$(fmt_int "${ibd_stage_height}")
                if [ -n "${ibd_stage_pct}" ]; then
                    progress_pct=$(awk -v p="${ibd_stage_pct}" 'BEGIN{printf "%.3f", p+0}')
                    progress_ring=$(awk -v p="${ibd_stage_pct}" 'BEGIN{p=p+0;if(p<0)p=0;if(p>100)p=100;printf "%.3f",p}')
                fi
                if [ -n "${ibd_stage_height}" ] && [ "${ibd_stage_height}" != "-" ]; then
                    ibd_stage_detail="height ${ibd_stage_height}"
                fi
                ;;
            loading_index)
                ibd_stage_key="loading_index"
                ibd_stage_label="Loading block index"
                ibd_stage_detail="building block index"
                ;;
            loading_blocks)
                ibd_stage_key="loading_blocks"
                ibd_stage_label="Loading block files"
                ibd_stage_detail="loading block files"
                ;;
            verifying_blocks)
                ibd_stage_key="verifying_blocks"
                ibd_stage_label="Verifying blocks"
                ibd_stage_detail="verifying chain state"
                ;;
            rewinding_blocks)
                ibd_stage_key="rewinding_blocks"
                ibd_stage_label="Rewinding blocks"
                ibd_stage_detail="rewinding chain state"
                ;;
            pruning)
                ibd_stage_key="pruning"
                ibd_stage_label="Pruning blocks"
                ibd_stage_detail="pruning old block data"
                ;;
        esac

        header_stage_fresh=0
        header_stage_active=0
        if [ -n "${stage_height_num}" ] && [ -n "${headers}" ] && [ "${headers}" != "-" ]; then
            stage_delta=$(awk -v h="${headers}" -v s="${stage_height_num}" 'BEGIN{
              d = (h + 0) - (s + 0)
              if (d < 0) d = -d
              printf "%.0f", d
            }')
            if [ "${stage_delta}" -le 8192 ] 2>/dev/null; then
                header_stage_fresh=1
            fi
        fi

        case "${ibd_stage_key}" in
            pre_headers|sync_headers)
                if [ "${blocks}" = "0" ] && [ -n "${stage_pct_num}" ]; then
                    if awk -v p="${stage_pct_num}" 'BEGIN{exit !((p + 0) < 99.9)}'; then
                        header_stage_active=1
                    fi
                fi
                ;;
        esac

        block_sync_needed=""
        if [ "${header_gap}" -gt 0 ] 2>/dev/null; then
            if [ -z "${stage_key}" ]; then
                block_sync_needed=1
            else
                case "${ibd_stage_key}" in
                    pre_headers|sync_headers)
                        if [ "${header_stage_active}" != "1" ]; then
                            block_sync_needed=1
                        fi
                        ;;
                esac
            fi
        fi

        if [ "${block_sync_needed}" = "1" ]; then
            ibd_stage_key="sync_blocks"
            ibd_stage_label="Synchronizing blocks"
            ibd_stage_height=$(fmt_int "${blocks}")
            ibd_stage_pct=""
            ibd_stage_detail="headers $(fmt_int "${headers}")"
            if [ -n "${height_based_progress}" ]; then
                progress_pct="${height_based_progress}"
                progress_ring="${height_based_progress}"
            elif [ -n "${progress}" ]; then
                progress_pct=$(awk -v p="${progress}" 'BEGIN{printf "%.6f", (p+0)*100}')
                progress_ring=$(awk -v p="${progress_pct}" 'BEGIN{p=p+0;if(p<0)p=0;if(p>100)p=100;printf "%.6f",p}')
            fi
        fi

        if { [ -z "${ibd_stage_height}" ] || [ "${ibd_stage_height}" = "-" ]; } && [ -n "${blocks}" ] && [ "${blocks}" != "-" ]; then
            ibd_stage_key="sync_blocks"
            ibd_stage_label="Synchronizing blocks"
            ibd_stage_height=$(fmt_int "${blocks}")
            ibd_stage_detail="headers $(fmt_int "${headers}")"
        fi
    fi

    if [ "${ibd_stage_key}" = "sync_blocks" ] && [ "${header_gap}" -gt 0 ] 2>/dev/null && [ -n "${height_based_progress}" ]; then
        progress_pct="${height_based_progress}"
        progress_ring="${height_based_progress}"
    fi

     if [ "${btc_running}" != "0" ] && [ "${block_sync_needed}" = "1" ] &&
         [ "${header_gap}" -gt 0 ] 2>/dev/null && [ -n "${height_based_progress}" ]; then
        ibd_stage_key="sync_blocks"
        ibd_stage_label="Synchronizing blocks"
        ibd_stage_height=$(fmt_int "${blocks}")
        ibd_stage_pct=""
        ibd_stage_detail="headers $(fmt_int "${headers}")"
        progress_pct="${height_based_progress}"
        progress_ring="${height_based_progress}"
    fi

    headers_fmt=$(fmt_int "${headers}")
    blocks_fmt=$(fmt_int "${blocks}")
    difficulty_display=$(compact_metric "${difficulty}")
    [ -n "${connections}" ] || connections="-"
    [ -n "${difficulty_display}" ] || difficulty_display="-"

    case "${chain}" in
        main) btc_context="mainnet" ;;
        test) btc_context="testnet" ;;
        signet) btc_context="signet" ;;
        regtest) btc_context="regtest" ;;
        *) btc_context="${chain}" ;;
    esac
    [ -n "${btc_context}" ] || btc_context="-"

    if [ "${btc_running}" = "0" ]; then
        sync_key=btc_not_running
        sync_class=err
        sync_detail="bitcoind not running"
    elif [ "${ibd}" = "true" ] || [ "${ibd_stage_key}" = "sync_blocks" ]; then
        sync_key="${ibd_stage_key}"
        sync_class=warn
        sync_detail="${ibd_stage_label}"
    else
        sync_key=synced
        sync_class=ok
        sync_detail="initial sync complete"
    fi

    if [ "${pruned}" = "true" ]; then
        prune_key=pruned_mode
        prune_class=soft
    else
        prune_key=full_mode
        prune_class=ok
    fi

    stratum_host="${ip_addr}"
    case "${stratum_host}" in
        ""|N/A) stratum_host="<device-ip>" ;;
    esac
    stratum_url="stratum+tcp://${stratum_host}:${stratum_port}"
    stratum_reach="${stratum_host}:${stratum_port}"
    stratum_user_hint="wallet.workername"
    stratum_pass_hint="(leave blank)"
    stratum_recommended="LAN miners -> local Stratum"
    ckpool_tail="-"

    printf '{\n'
    printf '  "system": {\n'
    printf '    "cpu_model": %s,\n' "$(json_str "${cpu_model}")"
    printf '    "uptime": %s,\n' "$(json_str "${uptime_fmt}")"
    printf '    "temp": %s,\n' "$(json_str "${temp}")"
    printf '    "load_pct": %s,\n' "$(json_num "${load_pct}")"
    printf '    "load_display": %s,\n' "$(json_str "${load_display}")"
    printf '    "load_avg_1": %s,\n' "$(json_str "${load_avg_1_display}")"
    printf '    "load_avg_5": %s,\n' "$(json_str "${load_avg_5_display}")"
    printf '    "load_avg_15": %s,\n' "$(json_str "${load_avg_15_display}")"
    printf '    "load_avg_display": %s,\n' "$(json_str "${load_avg_display}")"
    printf '    "mem_pct": %s,\n' "$(json_num "${mem_pct}")"
    printf '    "mem_used_display": %s,\n' "$(json_str "${mem_used_display}")"
    printf '    "mem_total_display": %s,\n' "$(json_str "${mem_total_display}")"
    printf '    "disk_pct": %s,\n' "$(json_num "${disk_pct}")"
    printf '    "disk_size_info": %s,\n' "$(json_str "${disk_size_info}")"
    printf '    "rootfs_pct": %s,\n' "$(json_num "${rootfs_pct}")"
    printf '    "rootfs_size_info": %s,\n' "$(json_str "${rootfs_size_info}")"
    printf '    "swap_pct": %s,\n' "$(json_num "${swap_pct}")"
    printf '    "swap_used_display": %s,\n' "$(json_str "${swap_used_display}")"
    printf '    "swap_total_display": %s,\n' "$(json_str "${swap_total_display}")"
    printf '    "block_disk_found": %s,\n' "${block_disk_found}"
    printf '    "block_disk_value": %s,\n' "$(json_str "${block_disk_value}")"
    printf '    "block_disk_path": %s\n' "$(json_str "${block_disk_path}")"
    printf '  },\n'

    printf '  "bitcoin": {\n'
    printf '    "running": %s,\n' "${btc_running}"
    printf '    "sync_key": %s,\n' "$(json_str "${sync_key}")"
    printf '    "sync_class": %s,\n' "$(json_str "${sync_class}")"
    printf '    "sync_detail": %s,\n' "$(json_str "${sync_detail}")"
    printf '    "prune_key": %s,\n' "$(json_str "${prune_key}")"
    printf '    "prune_class": %s,\n' "$(json_str "${prune_class}")"
    printf '    "progress_pct": %s,\n' "$(json_str "${progress_pct}")"
    printf '    "progress_ring": %s,\n' "$(json_num "${progress_ring}")"
    printf '    "ibd_stage_key": %s,\n' "$(json_str "${ibd_stage_key}")"
    printf '    "ibd_stage_label": %s,\n' "$(json_str "${ibd_stage_label}")"
    printf '    "ibd_stage_detail": %s,\n' "$(json_str "${ibd_stage_detail}")"
    printf '    "ibd_stage_height": %s,\n' "$(json_str "${ibd_stage_height}")"
    printf '    "ibd_stage_pct": %s,\n' "$(json_str "${ibd_stage_pct}")"
    printf '    "headers_fmt": %s,\n' "$(json_str "${headers_fmt}")"
    printf '    "blocks_fmt": %s,\n' "$(json_str "${blocks_fmt}")"
    printf '    "best_block_date": %s,\n' "$(json_str "${best_block_date}")"
    printf '    "btc_context": %s,\n' "$(json_str "${btc_context}")"
    printf '    "connections": %s,\n' "$(json_str "${connections}")"
    printf '    "difficulty_display": %s,\n' "$(json_str "${difficulty_display}")"
    printf '    "rss": %s\n' "$(json_str "${rss}")"
    printf '  },\n'

    printf '  "mining": {\n'
    printf '    "ckpool_running": %s,\n' "${ckpool_running}"
    printf '    "ckpool_class": %s,\n' "$(json_str "${ckpool_class}")"
    printf '    "ckpool_state": %s,\n' "$(json_str "${ckpool_state}")"
    printf '    "stratum_key": %s,\n' "$(json_str "${stratum_key}")"
    printf '    "stratum_port": %s,\n' "$(json_num "${stratum_port}")"
    printf '    "stratum_url": %s,\n' "$(json_str "${stratum_url}")"
    printf '    "user_hint": %s,\n' "$(json_str "${stratum_user_hint}")"
    printf '    "pass_hint": %s,\n' "$(json_str "${stratum_pass_hint}")"
    printf '    "recommended_mode": %s,\n' "$(json_str "${stratum_recommended}")"
    printf '    "btc_address": %s,\n' "$(json_str "${btc_address}")"
    printf '    "ckpool_tail": %s\n' "$(json_str_ml "${ckpool_tail}")"
    printf '  },\n'

    printf '  "network": {\n'
    printf '    "wifi_ssid": %s,\n' "$(json_str "${wifi_ssid}")"
    printf '    "ip_addr": %s,\n' "$(json_str "${ip_addr}")"
    printf '    "iface": %s,\n' "$(json_str "${iface}")"
    printf '    "mac_addr": %s,\n' "$(json_str "${mac_addr}")"
    printf '    "rssi_display": %s,\n' "$(json_str "${rssi_display}")"
    printf '    "rssi_pct": %s,\n' "$(json_num "${rssi_pct}")"
    printf '    "signal_level": %s,\n' "$(json_num "${signal_level}")"
    printf '    "stratum_reach": %s,\n' "$(json_str "${stratum_reach}")"
    printf '    "ntp_class": %s,\n' "$(json_str "${ntp_class}")"
    printf '    "ntp_key": %s\n' "$(json_str "${ntp_key}")"
    printf '  }\n'
    printf '}\n'
}

render_final_cache() {
    ensure_state_dir
    tmp="${FINAL_CACHE}.$$"
    render_status_json > "${tmp}" || {
        rm -f "${tmp}"
        return 1
    }
    mv "${tmp}" "${FINAL_CACHE}"
}

next_bitcoin_chain_interval() {
    [ -r "${BITCOIN_CHAIN_STATE}" ] || {
        echo "${BITCOIN_CHAIN_INTERVAL}"
        return
    }

    blocks=""
    headers=""
    ibd=""
    # shellcheck disable=SC1090
    . "${BITCOIN_CHAIN_STATE}" 2>/dev/null || true

    if [ "${ibd}" = "true" ] || { [ "${blocks}" -lt "${headers}" ] 2>/dev/null; }; then
        echo "${BITCOIN_CHAIN_IBD_INTERVAL}"
    else
        echo "${BITCOIN_CHAIN_INTERVAL}"
    fi
}

refresh_initial() {
    collect_system >/dev/null 2>&1 || true
    render_final_cache >/dev/null 2>&1 || true
    collect_network >/dev/null 2>&1 || true
    render_final_cache >/dev/null 2>&1 || true
    collect_stratum >/dev/null 2>&1 || true
    render_final_cache >/dev/null 2>&1 || true
    collect_bitcoin_log >/dev/null 2>&1 || true
    render_final_cache >/dev/null 2>&1 || true
    sleep 1
    collect_bitcoin_conn >/dev/null 2>&1 || true
    render_final_cache >/dev/null 2>&1 || true
    sleep 1
    collect_bitcoin_chain >/dev/null 2>&1 || true
    render_final_cache >/dev/null 2>&1 || true
}

daemon_loop() {
    ensure_state_dir
    if ! mkdir "${LOCK_DIR}" 2>/dev/null; then
        if [ -r "${LOCK_DIR}/pid" ]; then
            old_pid=$(cat "${LOCK_DIR}/pid" 2>/dev/null)
            if [ -n "${old_pid}" ] && kill -0 "${old_pid}" 2>/dev/null; then
                exit 0
            fi
        fi
        rm -rf "${LOCK_DIR}" 2>/dev/null || true
        mkdir "${LOCK_DIR}" 2>/dev/null || exit 1
    fi
    echo $$ > "${LOCK_DIR}/pid"
    echo $$ > "${PIDFILE}"
    trap 'rm -rf "${LOCK_DIR}" 2>/dev/null; rm -f "${PIDFILE}"; exit 0' INT TERM EXIT

    refresh_initial

    now=$(now_epoch)
    next_system=$((now + 5 + ($$ % 3)))
    next_bitcoin_log=$((now + 10 + ($$ % 5)))
    next_network=$((now + 15 + ($$ % 3)))
    next_stratum=$((now + 20 + ($$ % 5)))
    next_bitcoin_conn=$((now + 30 + ($$ % 7)))
    next_bitcoin_chain=$((now + 45 + ($$ % 11)))

    while :; do
        now=$(now_epoch)

        if [ "${now}" -ge "${next_system}" ]; then
            collect_system >/dev/null 2>&1 || true
            render_final_cache >/dev/null 2>&1 || true
            next_system=$((now + SYSTEM_INTERVAL))
            sleep 1
            continue
        fi
        if [ "${now}" -ge "${next_bitcoin_log}" ]; then
            collect_bitcoin_log >/dev/null 2>&1 || true
            render_final_cache >/dev/null 2>&1 || true
            next_bitcoin_log=$((now + BITCOIN_LOG_INTERVAL))
            sleep 1
            continue
        fi
        if [ "${now}" -ge "${next_network}" ]; then
            collect_network >/dev/null 2>&1 || true
            render_final_cache >/dev/null 2>&1 || true
            next_network=$((now + NETWORK_INTERVAL))
            sleep 1
            continue
        fi
        if [ "${now}" -ge "${next_stratum}" ]; then
            collect_stratum >/dev/null 2>&1 || true
            render_final_cache >/dev/null 2>&1 || true
            next_stratum=$((now + STRATUM_INTERVAL))
            sleep 1
            continue
        fi
        if [ "${now}" -ge "${next_bitcoin_conn}" ]; then
            collect_bitcoin_conn >/dev/null 2>&1 || true
            render_final_cache >/dev/null 2>&1 || true
            next_bitcoin_conn=$((now + BITCOIN_CONN_INTERVAL))
            sleep 1
            continue
        fi
        if [ "${now}" -ge "${next_bitcoin_chain}" ]; then
            if collect_bitcoin_chain >/dev/null 2>&1; then
                next_bitcoin_chain=$((now + $(next_bitcoin_chain_interval)))
            else
                next_bitcoin_chain=$((now + BITCOIN_CHAIN_RETRY_INTERVAL))
            fi
            render_final_cache >/dev/null 2>&1 || true
            sleep 1
            continue
        fi

        sleep 1
    done
}

case "$1" in
    daemon|start)
        daemon_loop
        ;;
    refresh-now)
        refresh_initial
        ;;
    render-stdout)
        render_status_json
        ;;
    render-cache)
        render_final_cache
        ;;
    *)
        echo "Usage: $0 {daemon|start|refresh-now|render-stdout|render-cache}"
        exit 1
        ;;
esac
