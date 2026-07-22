#!/bin/sh
CONF=/root/bitcoind/bitcoin.conf
DATADIR=/root/block
btc_cli(){ bitcoin-cli -conf="$CONF" -datadir="$DATADIR" "$@" 2>/dev/null || echo "{}"; }

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

cpu_model=$(cat /proc/device-tree/model 2>/dev/null | tr '\0' ' ' | xargs)
[ -z "$cpu_model" ] && cpu_model=$(grep -m1 "uarch\|machine\|model name\|cpu model" /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs)
[ -z "$cpu_model" ] && cpu_model="K230 RISC-V"
uptime_sec=$(awk -F. '{print $1}' /proc/uptime)
uptime_fmt=$(printf '%dd %02dh:%02dm' $((uptime_sec/86400)) $((uptime_sec%86400/3600)) $((uptime_sec%3600/60)))
nproc=$(grep -c '^processor' /proc/cpuinfo 2>/dev/null || echo 1)
load1=$(awk '{print $1}' /proc/loadavg)
load_pct=$(awk -v l="$load1" -v n="$nproc" 'BEGIN{printf "%.0f", l/n*100}')
load_ring=$(awk -v p="$load_pct" 'BEGIN{if(p<0)p=0;if(p>100)p=100;printf "%.0f",p}')
load_display="${load1} / ${nproc} cores"
mem_total=$(awk '/^MemTotal/{printf "%.0f", $2/1024}' /proc/meminfo)
mem_free=$(awk '/^MemAvailable/{printf "%.0f", $2/1024}' /proc/meminfo)
mem_used=$((mem_total-mem_free))
mem_pct=$((mem_used*100/mem_total))
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
if [ -d "$DATADIR" ]; then
  block_disk_mb=$(du -sm "$DATADIR" 2>/dev/null | awk 'NR==1{print $1}')
  if [ -n "$block_disk_mb" ]; then
    block_disk_value=$(compact_size_mb "$block_disk_mb")
    block_disk_path="$DATADIR"
  else
    block_disk_value="-"
    block_disk_path="$DATADIR"
  fi
else
  block_disk_value="NOT_FOUND"
  block_disk_path="$DATADIR"
fi

temp="N/A"
for t in /sys/class/thermal/thermal_zone*/temp /sys/class/hwmon/hwmon*/temp1_input; do
  [ ! -r "$t" ] && continue
  raw=$(cat "$t" 2>/dev/null)
  case "$raw" in
    '' ) ;;
    * )
      if [ "$raw" -ge 100000 ] 2>/dev/null; then temp=$(awk -v r="$raw" 'BEGIN{printf "%.1fC", r/1000}'); break; fi
      if [ "$raw" -ge 1000 ] 2>/dev/null; then temp=$(awk -v r="$raw" 'BEGIN{printf "%.1fC", r/100}'); break; fi
      if echo "$raw" | grep -Eq '^[0-9]+$'; then temp=$(awk -v r="$raw" 'BEGIN{printf "%.1fC", r}'); break; fi
      ;;
  esac
done

btc_running=0
headers="-"; headers_fmt="-"
blocks="-"; blocks_fmt="-"
progress_pct="0.00"; progress_ring=0; ibd=""; chain="-"; difficulty="-"; difficulty_display="-"; connections="-"; subversion="-"; rss="N/A"; pruned="false"; best_block_date="-"; btc_context="-"
if pidof bitcoind >/dev/null 2>&1; then
  btc_running=1
  btc_info=$(btc_cli getblockchaininfo)
  btc_net=$(btc_cli getnetworkinfo)
  headers=$(echo "$btc_info" | grep -o '"headers": *[0-9]*' | grep -o '[0-9]*')
  blocks=$(echo "$btc_info" | grep -o '"blocks": *[0-9]*' | grep -o '[0-9]*')
  progress=$(echo "$btc_info" | grep -o '"verificationprogress": *[0-9.e+-]*' | cut -d: -f2)
  [ -n "$progress" ] && progress_pct=$(echo "$progress" | awk '{printf "%.2f", $1*100}')
  progress_ring=$(awk -v p="$progress_pct" 'BEGIN{if(p<0)p=0;if(p>100)p=100;printf "%.0f",p}')
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
    [ -n "$best_block_time" ] && best_block_date=$(date -u -d "@$best_block_time" '+%Y-%m-%d' 2>/dev/null || echo "-")
  fi
fi
headers_fmt=$(fmt_int "$headers")
blocks_fmt=$(fmt_int "$blocks")
difficulty_display=$(compact_metric "$difficulty")
[ -z "$connections" ] && connections="-"
[ -z "$difficulty_display" ] && difficulty_display="-"
btc_context=$(printf '%s %s' "$chain" "$subversion" | xargs)
[ -z "$btc_context" ] && btc_context="-"
if [ "$btc_running" = "0" ]; then sync_key=btc_not_running; sync_class=err; elif [ "$ibd" = "true" ]; then sync_key=syncing; sync_class=warn; else sync_key=synced; sync_class=ok; fi
if [ "$pruned" = "true" ]; then prune_key=pruned_mode; prune_class=soft; else prune_key=full_mode; prune_class=ok; fi
ckpool_running=0; ckpool_tail=""
CKPOOL_CONF=/root/ckpool/ckpool.conf
btc_address=$(grep -m1 '"btcaddress"' "$CKPOOL_CONF" 2>/dev/null | cut -d'"' -f4)
[ -z "$btc_address" ] && btc_address="-"
ckpool_rpc_url=$(grep -m1 '"url"' "$CKPOOL_CONF" 2>/dev/null | cut -d'"' -f4)
case "$ckpool_rpc_url" in 127.0.0.1:*|localhost:*|::1:*) stratum_key=solo ;; "") stratum_key=unknown ;; *) stratum_key=pool ;; esac
if ps | grep -v grep | grep -q ckpool; then ckpool_running=1; ckpool_tail=$(tail -8 /root/ckpool/logs/ckpool.log 2>/dev/null | sed 's/\&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g'); fi
if [ "$ckpool_running" = "1" ]; then ckpool_class=ok; ckpool_state=running; else ckpool_class=err; ckpool_state=stopped; fi
[ -z "$ckpool_tail" ] && ckpool_tail='-'
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
if [ "$rssi_pct" -ge 76 ]; then signal_level=4; elif [ "$rssi_pct" -ge 51 ]; then signal_level=3; elif [ "$rssi_pct" -ge 26 ]; then signal_level=2; elif [ "$rssi_pct" -ge 1 ]; then signal_level=1; else signal_level=0; fi
ntp_running=0; ps | grep -v grep | grep -q ntpd && ntp_running=1
if [ "$ntp_running" = "1" ]; then ntp_key=running; ntp_class=ok; else ntp_key=not_synced; ntp_class=err; fi

echo "Content-Type: text/html; charset=utf-8"
echo ""
cat <<ENDOFHTML
<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><meta http-equiv="refresh" content="10"><title>NM full node monitor</title><link rel="stylesheet" href="/style.css?v=20260721d"></head><body><div class="wrap"><div class="header"><div><h1 data-i18n="title">NM full node monitor</h1><div class="sub" data-i18n="subtitle">Bitcoin Full Node + Solo Mining Pool</div></div><div class="lang-selector"><label data-i18n="lang_label">Language</label><select id="langSelect" onchange="switchLang(this.value)"><option value="en">English</option><option value="zh">&#x4E2D;&#x6587;</option><option value="es">Espa&ntilde;ol</option><option value="pt">Portugu&ecirc;s</option><option value="ru">&#x420;&#x443;&#x441;&#x441;&#x43A;&#x438;&#x439;</option><option value="de">Deutsch</option><option value="ja">&#x65E5;&#x672C;&#x8A9E;</option><option value="ko">&#xD55C;&#xAD6D;&#xC5B4;</option></select></div></div><div class="grid">
<div class="card"><div class="card-head"><h2 data-i18n="card_sys">System</h2></div><div class="metrics sys-metrics"><div class="metric"><b data-i18n="load">CPU Load</b><div class="ring" style="--p:${load_ring};--c:#38bdf8"><span>${load_ring}%</span></div><strong>${load_display}</strong></div><div class="metric"><b data-i18n="memory">Memory</b><div class="ring" style="--p:${mem_pct};--c:#34d399"><span>${mem_pct}%</span></div><strong>${mem_used_display} / ${mem_total_display}</strong></div><div class="metric"><b data-i18n="disk">Disk</b><div class="ring" style="--p:${disk_pct};--c:#f59e0b"><span>${disk_pct}%</span></div><strong>${disk_size_info}</strong></div><div class="metric"><b data-i18n="swap_usage">SWAP</b><div class="ring" style="--p:${swap_pct};--c:#60a5fa"><span>${swap_pct}%</span></div><strong>${swap_used_display} / ${swap_total_display}</strong></div></div><div class="kv"><div class="row"><label data-i18n="cpu">CPU</label><div class="val">${cpu_model}</div></div><div class="row"><label data-i18n="uptime">Uptime</label><div class="val">${uptime_fmt}</div></div><div class="row"><label data-i18n="temp">Temp</label><div class="val">${temp}</div></div>
ENDOFHTML
if [ "$block_disk_value" = "NOT_FOUND" ]; then
  echo '<div class="row"><label data-i18n="block_disk">Block Data</label><div class="val err" data-i18n="not_detected">Not Detected</div></div>'
else
  echo '<div class="row"><label data-i18n="block_disk">Block Data</label><div class="val val-stack"><span>'"$block_disk_value"'</span><span class="subval">'"$block_disk_path"'</span></div></div>'
fi
cat <<ENDOFHTML
</div></div>
<div class="card"><div class="card-head"><h2 data-i18n="card_btc">Bitcoin Node</h2><div class="badges status-badges"><div class="badge ${sync_class}"><span data-i18n="sync_status">Sync</span><span data-i18n="${sync_key}">state</span></div><div class="badge ${prune_class}"><span data-i18n="mode_label">Mode</span><span data-i18n="${prune_key}">mode</span></div></div></div><div class="metrics btc-metrics"><div class="metric metric-ibd"><b data-i18n="ibd_progress">IBD Progress</b><div class="ibd-hero"><div class="ring" style="--p:${progress_ring};--c:#14b8a6"><span>${progress_pct}%</span></div><div class="ibd-copy"><div class="height-pair"><span class="height-live">${headers_fmt}</span><span class="height-sep">/</span><span class="height-local">${blocks_fmt}</span></div><span class="stamp">${best_block_date}</span><small class="metric-compact">${btc_context}</small></div></div></div></div><div class="kv"><div class="row"><label data-i18n="connections">Conn</label><div class="val">${connections}</div></div><div class="row"><label data-i18n="difficulty">Diff</label><div class="val">${difficulty_display}</div></div><div class="row"><label data-i18n="node_ram">Node RAM (RSS)</label><div class="val">${rss}</div></div></div></div>
<div class="card card-mining"><div class="card-head"><h2 data-i18n="card_mining">Stratum Service</h2><div class="badges status-badges"><div class="badge ${ckpool_class}"><span data-i18n="service_status">Service</span><span data-i18n="${ckpool_state}">state</span></div><div class="badge soft"><span data-i18n="stratum_mode">Stratum Mode</span><span data-i18n="${stratum_key}">mode</span></div></div></div><div class="address-line"><label data-i18n="btc_address">BTC Address</label><div class="copy-wrap"><input class="copy" type="text" readonly value="${btc_address}" onclick="this.select();" onfocus="this.select();" spellcheck="false"><button class="copy-btn" type="button" onclick="copyField(this)" data-i18n-title="copy_address" title="Copy address" aria-label="Copy address">&#x2398;</button></div></div><div class="log-box">${ckpool_tail}</div></div>
<div class="card"><div class="card-head"><h2 data-i18n="card_network">Network</h2><div class="badges"><div class="badge ${ntp_class}"><span data-i18n="ntp_status">NTP</span><span data-i18n="${ntp_key}">state</span></div></div></div><div class="kv"><div class="row"><label data-i18n="ip_address">IP</label><div class="val">${ip_addr}<span class="subval">${iface}</span></div></div><div class="row"><label data-i18n="wifi_ssid">WiFi</label><div class="val">${wifi_ssid}</div></div><div class="row"><label data-i18n="mac_address">MAC</label><div class="val">${mac_addr}</div></div><div class="row"><label data-i18n="rssi">RSSI</label><div class="val">${rssi_display}<div class="signal"><div class="signal-bars" data-level="${signal_level}"><span></span><span></span><span></span><span></span></div></div></div></div></div></div></div><div class="footer" data-i18n="footer">Auto-refresh 10s | Busybox httpd + Shell CGI</div><script>
var L={"title":{"en":"NM full node monitor","zh":"NM \u5168\u8282\u70b9\u76d1\u63a7","es":"Monitor de nodo completo NM","pt":"Monitor de n\u00f3 completo NM","ru":"\u041c\u043e\u043d\u0438\u0442\u043e\u0440 \u043f\u043e\u043b\u043d\u043e\u0433\u043e \u0443\u0437\u043b\u0430 NM","de":"NM Full-Node-Monitor","ja":"NM \u30d5\u30eb\u30ce\u30fc\u30c9\u30e2\u30cb\u30bf\u30fc","ko":"NM \ud480\ub178\ub4dc \ubaa8\ub2c8\ud130"},"subtitle":{"en":"Bitcoin Full Node + Solo Mining Pool","zh":"Bitcoin \u5168\u8282\u70b9 + Solo \u77ff\u6c60","es":"Nodo completo Bitcoin + pool solo","pt":"N\u00f3 completo Bitcoin + pool solo","ru":"\u041f\u043e\u043b\u043d\u044b\u0439 \u0443\u0437\u0435\u043b Bitcoin + solo-\u043f\u0443\u043b","de":"Bitcoin Full Node + Solo-Pool","ja":"Bitcoin \u30d5\u30eb\u30ce\u30fc\u30c9 + Solo \u30d7\u30fc\u30eb","ko":"Bitcoin \ud480\ub178\ub4dc + Solo \ud480"},"lang_label":{"en":"Language","zh":"\u8bed\u8a00","es":"Idioma","pt":"Idioma","ru":"\u042f\u0437\u044b\u043a","de":"Sprache","ja":"\u8a00\u8a9e","ko":"\uc5b8\uc5b4"},"card_sys":{"en":"System","zh":"\u7cfb\u7edf\u4fe1\u606f","es":"Sistema","pt":"Sistema","ru":"\u0421\u0438\u0441\u0442\u0435\u043c\u0430","de":"System","ja":"\u30b7\u30b9\u30c6\u30e0","ko":"\uc2dc\uc2a4\ud15c"},"cpu":{"en":"CPU","zh":"CPU","es":"CPU","pt":"CPU","ru":"CPU","de":"CPU","ja":"CPU","ko":"CPU"},"load":{"en":"CPU Load","zh":"CPU \u8d1f\u8f7d","es":"Carga CPU","pt":"Carga CPU","ru":"\u041d\u0430\u0433\u0440\u0443\u0437\u043a\u0430 CPU","de":"CPU-Last","ja":"CPU \u8ca0\u8377","ko":"CPU \ubd80\ud558"},"uptime":{"en":"Uptime","zh":"\u8fd0\u884c\u65f6\u95f4","es":"Actividad","pt":"Tempo ativo","ru":"\u0410\u043f\u0442\u0430\u0439\u043c","de":"Laufzeit","ja":"\u7a3c\u50cd\u6642\u9593","ko":"\uac00\ub3d9 \uc2dc\uac04"},"temp":{"en":"Temp","zh":"\u6e29\u5ea6","es":"Temp","pt":"Temp","ru":"\u0422\u0435\u043c\u043f.","de":"Temp","ja":"\u6e29\u5ea6","ko":"\uc628\ub3c4"},"memory":{"en":"Memory","zh":"\u5185\u5b58","es":"Memoria","pt":"Mem\u00f3ria","ru":"\u041f\u0430\u043c\u044f\u0442\u044c","de":"Speicher","ja":"\u30e1\u30e2\u30ea","ko":"\uba54\ubaa8\ub9ac"},"swap_usage":{"en":"SWAP","zh":"SWAP","es":"SWAP","pt":"SWAP","ru":"SWAP","de":"SWAP","ja":"SWAP","ko":"SWAP"},"disk":{"en":"Disk","zh":"\u78c1\u76d8","es":"Disco","pt":"Disco","ru":"\u0414\u0438\u0441\u043a","de":"Platte","ja":"\u30c7\u30a3\u30b9\u30af","ko":"\ub514\uc2a4\ud06c"},"block_disk":{"en":"Block Data","zh":"\u533a\u5757\u6570\u636e","es":"Datos de bloques","pt":"Dados de blocos","ru":"\u0414\u0430\u043d\u043d\u044b\u0435 \u0431\u043b\u043e\u043a\u043e\u0432","de":"Blockdaten","ja":"\u30d6\u30ed\u30c3\u30af\u30c7\u30fc\u30bf","ko":"\ube14\ub85d \ub370\uc774\ud130"},"not_detected":{"en":"Not Detected","zh":"\u672a\u68c0\u6d4b\u5230","es":"No detectado","pt":"N\u00e3o detectado","ru":"\u041d\u0435 \u043e\u0431\u043d\u0430\u0440\u0443\u0436\u0435\u043d\u043e","de":"Nicht erkannt","ja":"\u672a\u691c\u51fa","ko":"\uac10\uc9c0 \uc548 \ub428"},"card_btc":{"en":"Bitcoin Node","zh":"Bitcoin \u8282\u70b9","es":"Nodo Bitcoin","pt":"N\u00f3 Bitcoin","ru":"\u0423\u0437\u0435\u043b Bitcoin","de":"Bitcoin-Knoten","ja":"Bitcoin \u30ce\u30fc\u30c9","ko":"Bitcoin \ub178\ub4dc"},"sync_status":{"en":"Sync","zh":"\u540c\u6b65","es":"Sincro","pt":"Sincro","ru":"\u0421\u0438\u043d\u0445\u0440.","de":"Sync","ja":"\u540c\u671f","ko":"\ub3d9\uae30\ud654"},"syncing":{"en":"IBD Syncing","zh":"IBD \u540c\u6b65\u4e2d","es":"Sincronizando","pt":"Sincronizando","ru":"\u0421\u0438\u043d\u0445\u0440\u043e\u043d\u0438\u0437\u0430\u0446\u0438\u044f","de":"IBD Sync","ja":"IBD \u540c\u671f\u4e2d","ko":"IBD \ub3d9\uae30\ud654 \uc911"},"synced":{"en":"Synced","zh":"\u5df2\u540c\u6b65","es":"Sincronizado","pt":"Sincronizado","ru":"\u0421\u0438\u043d\u0445\u0440.","de":"Synchron","ja":"\u540c\u671f\u6e08\u307f","ko":"\ub3d9\uae30\ud654 \uc644\ub8cc"},"btc_not_running":{"en":"Not Running","zh":"\u672a\u8fd0\u884c","es":"No ejecuta","pt":"N\u00e3o exec.","ru":"\u041d\u0435 \u0437\u0430\u043f\u0443\u0449\u0435\u043d","de":"Nicht aktiv","ja":"\u672a\u8d77\u52d5","ko":"\uc2e4\ud589 \uc548 \ub428"},"block_height":{"en":"Height","zh":"\u533a\u5757\u9ad8\u5ea6","es":"Altura","pt":"Altura","ru":"\u0412\u044b\u0441\u043e\u0442\u0430","de":"H\u00f6he","ja":"\u30d6\u30ed\u30c3\u30af\u9ad8","ko":"\ube14\ub85d \ub192\uc774"},"ibd_progress":{"en":"IBD Progress","zh":"IBD \u8fdb\u5ea6","es":"Progreso IBD","pt":"Progresso IBD","ru":"\u041f\u0440\u043e\u0433\u0440\u0435\u0441\u0441 IBD","de":"IBD Fortschritt","ja":"IBD \u9032\u6357","ko":"IBD \uc9c4\ud589\ub960"},"connections":{"en":"Conn","zh":"\u8fde\u63a5\u6570","es":"Conexiones","pt":"Conex\u00f5es","ru":"\u0421\u043e\u0435\u0434.","de":"Verb.","ja":"\u63a5\u7d9a\u6570","ko":"\uc5f0\uacb0"},"difficulty":{"en":"Diff","zh":"\u96be\u5ea6","es":"Dificultad","pt":"Dificuldade","ru":"\u0421\u043b\u043e\u0436\u043d\u043e\u0441\u0442\u044c","de":"Schwierigkeit","ja":"\u96e3\u6613\u5ea6","ko":"\ub09c\uc774\ub3c4"},"node_ram":{"en":"Node RAM (RSS)","zh":"\u8282\u70b9\u5185\u5b58 (RSS)","es":"RAM nodo (RSS)","pt":"RAM do n\u00f3 (RSS)","ru":"\u041f\u0430\u043c\u044f\u0442\u044c \u0443\u0437\u043b\u0430 (RSS)","de":"Node-RAM (RSS)","ja":"\u30ce\u30fc\u30c9\u30e1\u30e2\u30ea (RSS)","ko":"\ub178\ub4dc RAM (RSS)"},"mode_label":{"en":"Mode","zh":"\u6a21\u5f0f","es":"Modo","pt":"Modo","ru":"\u0420\u0435\u0436\u0438\u043c","de":"Modus","ja":"\u30e2\u30fc\u30c9","ko":"\ubaa8\ub4dc"},"pruned_mode":{"en":"Pruned","zh":"\u526a\u679d","es":"Podado","pt":"Podado","ru":"\u0421 \u043e\u0431\u0440\u0435\u0437\u043a\u043e\u0439","de":"Pruned","ja":"\u526a\u5b9a","ko":"\ud504\ub8e8\ub2dd"},"full_mode":{"en":"Full Node","zh":"\u5168\u8282\u70b9","es":"Nodo completo","pt":"N\u00f3 completo","ru":"\u041f\u043e\u043b\u043d\u044b\u0439 \u0443\u0437\u0435\u043b","de":"Full Node","ja":"\u30d5\u30eb\u30ce\u30fc\u30c9","ko":"\ud480\ub178\ub4dc"},"card_mining":{"en":"Stratum Service","zh":"Stratum \u670d\u52a1","es":"Servicio Stratum","pt":"Servi\u00e7o Stratum","ru":"\u0421\u0435\u0440\u0432\u0438\u0441 Stratum","de":"Stratum-Dienst","ja":"Stratum \u30b5\u30fc\u30d3\u30b9","ko":"Stratum \uc11c\ube44\uc2a4"},"service_status":{"en":"Service","zh":"\u670d\u52a1","es":"Servicio","pt":"Servi\u00e7o","ru":"\u0421\u0435\u0440\u0432\u0438\u0441","de":"Dienst","ja":"\u30b5\u30fc\u30d3\u30b9","ko":"\uc11c\ube44\uc2a4"},"running":{"en":"Running","zh":"\u8fd0\u884c\u4e2d","es":"En ejec.","pt":"Executando","ru":"\u0417\u0430\u043f\u0443\u0449\u0435\u043d","de":"L\u00e4uft","ja":"\u5b9f\u884c\u4e2d","ko":"\uc2e4\ud589 \uc911"},"stopped":{"en":"Stopped","zh":"\u672a\u8fd0\u884c","es":"Detenido","pt":"Parado","ru":"\u041e\u0441\u0442\u0430\u043d\u043e\u0432\u043b\u0435\u043d","de":"Gestoppt","ja":"\u505c\u6b62\u4e2d","ko":"\uc911\uc9c0\ub428"},"stratum_mode":{"en":"Stratum Mode","zh":"Stratum \u6a21\u5f0f","es":"Modo Stratum","pt":"Modo Stratum","ru":"\u0420\u0435\u0436\u0438\u043c Stratum","de":"Stratum-Modus","ja":"Stratum \u30e2\u30fc\u30c9","ko":"Stratum \ubaa8\ub4dc"},"solo":{"en":"Solo","zh":"Solo","es":"Solo","pt":"Solo","ru":"Solo","de":"Solo","ja":"Solo","ko":"Solo"},"pool":{"en":"Pool","zh":"Pool","es":"Pool","pt":"Pool","ru":"Pool","de":"Pool","ja":"Pool","ko":"Pool"},"unknown":{"en":"Unknown","zh":"\u672a\u77e5","es":"Desconocido","pt":"Desconhecido","ru":"\u041d\u0435\u0438\u0437\u0432\u0435\u0441\u0442\u043d\u043e","de":"Unbekannt","ja":"\u4e0d\u660e","ko":"\uc54c \uc218 \uc5c6\uc74c"},"btc_address":{"en":"BTC Address","zh":"BTC \u5730\u5740","es":"Direcci\u00f3n BTC","pt":"Endere\u00e7o BTC","ru":"BTC \u0430\u0434\u0440\u0435\u0441","de":"BTC-Adresse","ja":"BTC \u30a2\u30c9\u30ec\u30b9","ko":"BTC \uc8fc\uc18c"},"copy_address":{"en":"Copy address","zh":"\u590d\u5236\u5730\u5740","es":"Copiar direcci\u00f3n","pt":"Copiar endere\u00e7o","ru":"\u0421\u043a\u043e\u043f\u0438\u0440\u043e\u0432\u0430\u0442\u044c \u0430\u0434\u0440\u0435\u0441","de":"Adresse kopieren","ja":"\u30a2\u30c9\u30ec\u30b9\u3092\u30b3\u30d4\u30fc","ko":"\uc8fc\uc18c \ubcf5\uc0ac"},"card_network":{"en":"Network","zh":"\u7f51\u7edc","es":"Red","pt":"Rede","ru":"\u0421\u0435\u0442\u044c","de":"Netzwerk","ja":"\u30cd\u30c3\u30c8\u30ef\u30fc\u30af","ko":"\ub124\ud2b8\uc6cc\ud06c"},"ip_address":{"en":"IP","zh":"IP \u5730\u5740","es":"IP","pt":"IP","ru":"IP","de":"IP","ja":"IP","ko":"IP"},"wifi_ssid":{"en":"WiFi","zh":"WiFi","es":"WiFi","pt":"WiFi","ru":"WiFi","de":"WLAN","ja":"WiFi","ko":"WiFi"},"mac_address":{"en":"MAC","zh":"MAC \u5730\u5740","es":"MAC","pt":"MAC","ru":"MAC","de":"MAC","ja":"MAC","ko":"MAC"},"rssi":{"en":"RSSI","zh":"RSSI \u4fe1\u53f7","es":"RSSI","pt":"RSSI","ru":"RSSI","de":"RSSI","ja":"RSSI","ko":"RSSI"},"ntp_status":{"en":"NTP","zh":"NTP","es":"NTP","pt":"NTP","ru":"NTP","de":"NTP","ja":"NTP","ko":"NTP"},"not_synced":{"en":"Not Sync","zh":"\u672a\u540c\u6b65","es":"No sinc.","pt":"N\u00e3o sinc.","ru":"\u041d\u0435 \u0441\u0438\u043d\u0445\u0440.","de":"Nicht sync.","ja":"\u672a\u540c\u671f","ko":"\ubbf8\ub3d9\uae30\ud654"},"footer":{"en":"Auto-refresh 10s | Busybox httpd + Shell CGI","zh":"\u81ea\u52a8\u5237\u65b0 10 \u79d2 | Busybox httpd + Shell CGI","es":"Auto-refresh 10s | Busybox httpd + Shell CGI","pt":"Auto-refresh 10s | Busybox httpd + Shell CGI","ru":"\u0410\u0432\u0442\u043e\u043e\u0431\u043d\u043e\u0432\u043b\u0435\u043d\u0438\u0435 10\u0441 | Busybox httpd + Shell CGI","de":"Auto-refresh 10s | Busybox httpd + Shell CGI","ja":"10\u79d2\u81ea\u52d5\u66f4\u65b0 | Busybox httpd + Shell CGI","ko":"10\ucd08 \uc790\ub3d9 \uc0c8\ub85c\uace0\uce68 | Busybox httpd + Shell CGI"}};
function switchLang(l){
  localStorage.setItem('nmm_lang',l);
  document.querySelectorAll('[data-i18n]').forEach(function(e){
    var k=e.getAttribute('data-i18n');
    if(L[k]&&L[k][l]) e.textContent=L[k][l];
  });
  document.querySelectorAll('[data-i18n-title]').forEach(function(e){
    var k=e.getAttribute('data-i18n-title');
    if(L[k]&&L[k][l]){
      e.title=L[k][l];
      e.setAttribute('aria-label',L[k][l]);
    }
  });
  var sel=document.getElementById('langSelect');
  if(sel) sel.value=l;
}
function flashCopied(btn){
  var prev=btn.innerHTML;
  btn.classList.add('copied');
  btn.textContent='OK';
  setTimeout(function(){btn.classList.remove('copied');btn.innerHTML=prev;},1200);
}
function copyField(btn){
  var input=btn.parentNode.querySelector('input');
  if(!input) return;
  input.focus();
  input.select();
  var ok=false;
  try{ok=document.execCommand('copy');}catch(e){}
  if(ok){flashCopied(btn);return;}
  if(navigator.clipboard&&navigator.clipboard.writeText){
    navigator.clipboard.writeText(input.value).then(function(){flashCopied(btn);});
  }
}
document.addEventListener('DOMContentLoaded',function(){var sel=document.getElementById('langSelect');if(!sel)return;sel.onchange=function(){switchLang(this.value);};switchLang(localStorage.getItem('nmm_lang')||sel.value||'zh');});
</script></body></html>
ENDOFHTML
