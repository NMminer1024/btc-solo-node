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
<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><meta http-equiv="refresh" content="10"><title>NM full node monitor</title><link rel="stylesheet" href="/style.css"></head><body><div class="wrap"><div class="header"><div><h1>NM full node monitor</h1><div class="sub" data-i18n="subtitle">Bitcoin Full Node + Solo Mining Pool</div></div><div class="lang-selector"><label data-i18n="lang_label">Language</label><select id="langSelect" onchange="switchLang(this.value)"><option value="en">English</option><option value="zh">&#x4E2D;&#x6587;</option><option value="es">Espa&ntilde;ol</option><option value="pt">Portugu&ecirc;s</option><option value="ru">&#x420;&#x443;&#x441;&#x441;&#x43A;&#x438;&#x439;</option><option value="de">Deutsch</option><option value="ja">&#x65E5;&#x672C;&#x8A9E;</option><option value="ko">&#xD55C;&#xAD6D;&#xC5B4;</option></select></div></div><div class="grid">
<div class="card"><div class="card-head"><h2 data-i18n="card_sys">System</h2></div><div class="metrics sys-metrics"><div class="metric"><b data-i18n="load">CPU Load</b><div class="ring" style="--p:${load_ring};--c:#38bdf8"><span>${load_ring}%</span></div><strong>${load_display}</strong></div><div class="metric"><b data-i18n="memory">Memory</b><div class="ring" style="--p:${mem_pct};--c:#34d399"><span>${mem_pct}%</span></div><strong>${mem_used_display} / ${mem_total_display}</strong></div><div class="metric"><b data-i18n="disk">Disk</b><div class="ring" style="--p:${disk_pct};--c:#f59e0b"><span>${disk_pct}%</span></div><strong>${disk_size_info}</strong></div><div class="metric"><b data-i18n="swap_usage">SWAP</b><div class="ring" style="--p:${swap_pct};--c:#60a5fa"><span>${swap_pct}%</span></div><strong>${swap_used_display} / ${swap_total_display}</strong></div></div><div class="kv"><div class="row"><label data-i18n="cpu">CPU</label><div class="val">${cpu_model}</div></div><div class="row"><label data-i18n="uptime">Uptime</label><div class="val">${uptime_fmt}</div></div><div class="row"><label data-i18n="temp">Temp</label><div class="val">${temp}</div></div>
ENDOFHTML
if [ "$block_disk_value" = "NOT_FOUND" ]; then
  echo '<div class="row"><label data-i18n="block_disk">Block Data</label><div class="val err" data-i18n="not_detected">Not Detected</div></div>'
else
  echo '<div class="row"><label data-i18n="block_disk">Block Data</label><div class="val val-stack"><span>'"$block_disk_value"'</span><span class="subval">'"$block_disk_path"'</span></div></div>'
fi
cat <<ENDOFHTML
</div></div>
<div class="card"><div class="card-head"><h2 data-i18n="card_btc">Bitcoin Node</h2><div class="badges status-badges"><div class="badge ${sync_class}"><span data-i18n="sync_status">Sync</span><span data-i18n="${sync_key}">state</span></div><div class="badge ${prune_class}"><span data-i18n="mode_label">Mode</span><span data-i18n="${prune_key}">mode</span></div></div></div><div class="metrics btc-metrics"><div class="metric metric-ibd"><b data-i18n="ibd_progress">IBD Progress</b><div class="ibd-hero"><div class="ring" style="--p:${progress_ring};--c:#14b8a6"><span>${progress_ring}%</span></div><div class="ibd-copy"><strong>${headers_fmt} / ${blocks_fmt}</strong><span class="stamp">${best_block_date}</span><small class="metric-compact">${btc_context}</small></div></div></div></div><div class="kv"><div class="row"><label data-i18n="connections">Conn</label><div class="val">${connections}</div></div><div class="row"><label data-i18n="difficulty">Diff</label><div class="val">${difficulty_display}</div></div><div class="row"><label data-i18n="node_ram">Node RAM (RSS)</label><div class="val">${rss}</div></div></div></div>
<div class="card card-mining"><div class="card-head"><h2 data-i18n="card_mining">Stratum Service</h2><div class="badges status-badges"><div class="badge ${ckpool_class}"><span data-i18n="service_status">Service</span><span data-i18n="${ckpool_state}">state</span></div><div class="badge soft"><span data-i18n="stratum_mode">Stratum Mode</span><span data-i18n="${stratum_key}">mode</span></div></div></div><div class="kv"><div class="row address-row"><label data-i18n="btc_address">BTC Address</label><div class="val address-wrap"><div class="copy-wrap"><input class="copy" type="text" readonly value="${btc_address}" onclick="this.select();" onfocus="this.select();" spellcheck="false"><button class="copy-btn" type="button" onclick="copyField(this)" data-i18n-title="copy_address" title="Copy address" aria-label="Copy address">&#x2398;</button></div></div></div></div><div class="log-box">${ckpool_tail}</div></div>
<div class="card"><div class="card-head"><h2 data-i18n="card_network">Network</h2><div class="badges"><div class="badge ${ntp_class}"><span data-i18n="ntp_status">NTP</span><span data-i18n="${ntp_key}">state</span></div></div></div><div class="kv"><div class="row"><label data-i18n="ip_address">IP</label><div class="val">${ip_addr}<span class="subval">${iface}</span></div></div><div class="row"><label data-i18n="wifi_ssid">WiFi</label><div class="val">${wifi_ssid}</div></div><div class="row"><label data-i18n="mac_address">MAC</label><div class="val">${mac_addr}</div></div><div class="row"><label data-i18n="rssi">RSSI</label><div class="val">${rssi_display}<div class="signal"><div class="signal-bars" data-level="${signal_level}"><span></span><span></span><span></span><span></span></div></div></div></div></div></div></div><div class="footer" data-i18n="footer">Auto-refresh 10s | Busybox httpd + Shell CGI</div><script>
var L={
subtitle:{en:'Bitcoin Full Node + Solo Mining Pool',zh:'Bitcoin全节点 + 独立挖矿池',es:'Nodo Completo Bitcoin + Pool Solo',pt:'Nó Completo Bitcoin + Pool Solo',ru:'Полная нода Bitcoin + соло-пул',de:'Bitcoin Full Node + Solo-Pool',ja:'Bitcoinフルノード + ソロプール',ko:'Bitcoin 풀노드 + 솔로 풀'},
lang_label:{en:'Language',zh:'语言',es:'Idioma',pt:'Idioma',ru:'Язык',de:'Sprache',ja:'言語',ko:'언어'},
card_sys:{en:'System',zh:'系统信息',es:'Sistema',pt:'Sistema',ru:'Система',de:'System',ja:'システム',ko:'시스템'},
cpu:{en:'CPU',zh:'CPU',es:'CPU',pt:'CPU',ru:'CPU',de:'CPU',ja:'CPU',ko:'CPU'},
load:{en:'CPU Load',zh:'CPU负载',es:'Carga CPU',pt:'Carga CPU',ru:'Нагрузка CPU',de:'CPU-Last',ja:'CPU負荷',ko:'CPU 부하'},
uptime:{en:'Uptime',zh:'运行时间',es:'Actividad',pt:'Tempo Ativo',ru:'Аптайм',de:'Laufzeit',ja:'稼働時間',ko:'가동 시간'},
temp:{en:'Temp',zh:'温度',es:'Temp',pt:'Temp',ru:'Темп',de:'Temp',ja:'温度',ko:'온도'},
memory:{en:'Memory',zh:'内存',es:'Memoria',pt:'Memória',ru:'Память',de:'Speicher',ja:'メモリ',ko:'메모리'},
swap_usage:{en:'SWAP',zh:'SWAP',es:'SWAP',pt:'SWAP',ru:'SWAP',de:'SWAP',ja:'SWAP',ko:'SWAP'},
disk:{en:'Disk',zh:'磁盘',es:'Disco',pt:'Disco',ru:'Диск',de:'Platte',ja:'ディスク',ko:'디스크'},
block_disk:{en:'Block Data',zh:'区块数据',es:'Datos Block',pt:'Dados Block',ru:'Данные блоков',de:'Block-Daten',ja:'ブロックデータ',ko:'블록 데이터'},
not_detected:{en:'Not Detected',zh:'未检测到',es:'No detectado',pt:'Não detectado',ru:'Не обнаружен',de:'Nicht erkannt',ja:'未検出',ko:'감지 안 됨'},
card_btc:{en:'Bitcoin Node',zh:'Bitcoin 节点',es:'Nodo Bitcoin',pt:'Nó Bitcoin',ru:'Нода Bitcoin',de:'Bitcoin-Knoten',ja:'Bitcoinノード',ko:'Bitcoin 노드'},
sync_status:{en:'Sync',zh:'状态',es:'Sincro',pt:'Sincro',ru:'Синх',de:'Sync',ja:'同期',ko:'동기화'},
syncing:{en:'IBD Syncing',zh:'IBD同步中',es:'Sincronizando',pt:'Sincronizando',ru:'Синхронизация',de:'IBD Sync',ja:'IBD同期中',ko:'IBD 동기화 중'},
synced:{en:'Synced',zh:'已同步',es:'Sincronizado',pt:'Sincronizado',ru:'Синхр.',de:'Sync',ja:'同期済',ko:'동기화 완료'},
btc_not_running:{en:'Not Running',zh:'未运行',es:'No ejec.',pt:'Não exec.',ru:'Не запущ.',de:'Nicht gest.',ja:'未起動',ko:'실행 안 됨'},
block_height:{en:'Height',zh:'区块高度',es:'Altura',pt:'Altura',ru:'Высота',de:'Höhe',ja:'ブロック高',ko:'블록 높이'},
ibd_progress:{en:'IBD Progress',zh:'IBD进度',es:'Progreso IBD',pt:'Progresso IBD',ru:'Прогресс IBD',de:'IBD Fortschritt',ja:'IBD進捗',ko:'IBD 진행률'},
connections:{en:'Conn',zh:'连接数',es:'Conexiones',pt:'Conexões',ru:'Соед.',de:'Verb.',ja:'接続数',ko:'연결'},
difficulty:{en:'Diff',zh:'难度',es:'Dificultad',pt:'Dificuldade',ru:'Сложность',de:'Schwierigkeit',ja:'難易度',ko:'난이도'},
node_ram:{en:'Node RAM (RSS)',zh:'节点内存 (RSS)',es:'RAM Nodo (RSS)',pt:'RAM do Nó (RSS)',ru:'Память узла (RSS)',de:'Node-RAM (RSS)',ja:'ノードメモリ (RSS)',ko:'노드 RAM (RSS)'},
mode_label:{en:'Mode',zh:'模式',es:'Modo',pt:'Modo',ru:'Режим',de:'Modus',ja:'モード',ko:'모드'},
pruned_mode:{en:'Pruned',zh:'剪枝',es:'Podado',pt:'Podado',ru:'Pruned',de:'Pruned',ja:'剪枝',ko:'잘라내기'},
full_mode:{en:'Full Node',zh:'全节点',es:'Nodo completo',pt:'Nó completo',ru:'Полный узел',de:'Full Node',ja:'フルノード',ko:'풀 노드'},
card_mining:{en:'Stratum Service',zh:'Stratum服务',es:'Servicio Stratum',pt:'Serviço Stratum',ru:'Сервис Stratum',de:'Stratum-Dienst',ja:'Stratumサービス',ko:'Stratum 서비스'},
service_status:{en:'Service',zh:'服务',es:'Servicio',pt:'Serviço',ru:'Сервис',de:'Dienst',ja:'サービス',ko:'서비스'},
running:{en:'Running',zh:'运行中',es:'En ejec.',pt:'Executando',ru:'Запущен',de:'Läuft',ja:'実行中',ko:'실행 중'},
stopped:{en:'Stopped',zh:'未运行',es:'Detenido',pt:'Parado',ru:'Остановлен',de:'Gestoppt',ja:'停止中',ko:'중지됨'},
stratum_mode:{en:'Stratum Mode',zh:'Stratum模式',es:'Modo Stratum',pt:'Modo Stratum',ru:'Режим Stratum',de:'Stratum-Modus',ja:'Stratumモード',ko:'Stratum 모드'},
solo:{en:'Solo',zh:'Solo',es:'Solo',pt:'Solo',ru:'Solo',de:'Solo',ja:'Solo',ko:'Solo'},
pool:{en:'Pool',zh:'Pool',es:'Pool',pt:'Pool',ru:'Pool',de:'Pool',ja:'Pool',ko:'Pool'},
unknown:{en:'Unknown',zh:'未知',es:'Desconocido',pt:'Desconhecido',ru:'Неизвестно',de:'Unbekannt',ja:'不明',ko:'알 수 없음'},
btc_address:{en:'BTC Address',zh:'BTC地址',es:'Dirección BTC',pt:'Endereço BTC',ru:'BTC адрес',de:'BTC-Adresse',ja:'BTCアドレス',ko:'BTC 주소'},
copy_address:{en:'Copy address',zh:'复制地址',es:'Copiar dirección',pt:'Copiar endereço',ru:'Скопировать адрес',de:'Adresse kopieren',ja:'アドレスをコピー',ko:'주소 복사'},
card_network:{en:'Network',zh:'网络',es:'Red',pt:'Rede',ru:'Сеть',de:'Netzwerk',ja:'ネットワーク',ko:'네트워크'},
ip_address:{en:'IP',zh:'IP地址',es:'IP',pt:'IP',ru:'IP',de:'IP',ja:'IP',ko:'IP'},
wifi_ssid:{en:'WiFi',zh:'WiFi',es:'WiFi',pt:'WiFi',ru:'WiFi',de:'WLAN',ja:'WiFi',ko:'WiFi'},
mac_address:{en:'MAC',zh:'MAC地址',es:'MAC',pt:'MAC',ru:'MAC',de:'MAC',ja:'MAC',ko:'MAC'},
rssi:{en:'RSSI',zh:'RSSI信号',es:'RSSI',pt:'RSSI',ru:'RSSI',de:'RSSI',ja:'RSSI',ko:'RSSI'},
ntp_status:{en:'NTP',zh:'NTP',es:'NTP',pt:'NTP',ru:'NTP',de:'NTP',ja:'NTP',ko:'NTP'},
not_synced:{en:'Not Sync',zh:'未同步',es:'No Sinc.',pt:'Não Sinc.',ru:'Не синх.',de:'Nicht sync.',ja:'未同期',ko:'미동기화'},
footer:{en:'Auto-refresh 10s | Busybox httpd + Shell CGI',zh:'自动刷新10秒 | Busybox httpd + Shell CGI',es:'Auto-refresh 10s | Busybox httpd + Shell CGI',pt:'Auto-refresh 10s | Busybox httpd + Shell CGI',ru:'Автообновление 10с | Busybox httpd + Shell CGI',de:'Auto-refresh 10s | Busybox httpd + Shell CGI',ja:'10秒自動更新 | Busybox httpd + Shell CGI',ko:'10초 자동 갱신 | Busybox httpd + Shell CGI'}
};
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
  document.getElementById('langSelect').value=l;
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
(function(){var s=localStorage.getItem('nmm_lang')||'zh';document.getElementById('langSelect').value=s;switchLang(s);})();
</script></body></html>
ENDOFHTML
