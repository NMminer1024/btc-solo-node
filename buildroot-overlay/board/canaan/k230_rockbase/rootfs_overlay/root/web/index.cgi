#!/bin/sh
#
# NMminer K230 Dashboard — Shell CGI (8-language i18n)
# 用法: busybox httpd -h /root/web -p 80
#

CONF=/root/bitcoind/bitcoin.conf
DATADIR=/root/block

btc_cli() { bitcoin-cli -conf="$CONF" -datadir="$DATADIR" "$@" 2>/dev/null || echo "{}"; }

# ── 系统 ──
# 优先读设备树 model（如 "Canaan K230"），其次读 cpuinfo 的 uarch/march
cpu_model=$(cat /proc/device-tree/model 2>/dev/null | tr '\0' ' ' | xargs)
if [ -z "$cpu_model" ]; then
  cpu_model=$(grep -m1 "uarch\|machine\|model name\|cpu model" /proc/cpuinfo 2>/dev/null | cut -d: -f2 | xargs)
fi
[ -z "$cpu_model" ] && cpu_model="K230 RISC-V"
uptime_sec=$(awk -F. '{print $1}' /proc/uptime)
uptime_fmt=$(printf '%dd %02dh:%02dm' $((uptime_sec/86400)) $((uptime_sec%86400/3600)) $((uptime_sec%3600/60)))
nproc=$(grep -c '^processor' /proc/cpuinfo 2>/dev/null || echo 1)
load1=$(awk '{print $1}' /proc/loadavg)
load_pct=$(awk -v l="$load1" -v n="$nproc" 'BEGIN{printf "%.0f", l/n*100}')
load_bar_pct=$(awk -v p="$load_pct" 'BEGIN{if (p > 100) p = 100; print p}')
mem_total=$(awk '/^MemTotal/{printf "%.0f", $2/1024}' /proc/meminfo)
mem_free=$(awk '/^MemAvailable/{printf "%.0f", $2/1024}' /proc/meminfo)
mem_used=$((mem_total - mem_free)); mem_pct=$((mem_used * 100 / mem_total))
disk_info=$(df -h / | awk 'NR==2{print $3"/"$2" ("$5" used)"}')
if [ -d "$DATADIR" ]; then
  block_disk_info=$(df -h "$DATADIR" 2>/dev/null | awk 'NR==2{print $3"/"$2" ("$5" used)"}')
  [ -z "$block_disk_info" ] && block_disk_info="—"
else
  block_disk_info="NOT_FOUND"
fi
read_temp_c() {
  sensor_path="$1"
  sensor_name=""
  raw=$(cat "$sensor_path" 2>/dev/null)
  [ -z "$raw" ] && return 1

  case "$sensor_path" in
    /sys/class/thermal/thermal_zone*/temp)
      zone_dir=${sensor_path%/temp}
      sensor_name=$(cat "$zone_dir/type" 2>/dev/null)
      ;;
    /sys/class/hwmon/hwmon*/temp*_input)
      hwmon_dir=${sensor_path%/*}
      sensor_name=$(cat "$hwmon_dir/name" 2>/dev/null)
      ;;
  esac

  if [ "$sensor_name" = "canaan_thermal_zone" ]; then
    code=$((raw % 4096))
    temp_c=$(echo "scale=6; (0.000000000101472 * $code^4) - (0.00000110063 * $code^3) + (0.00436150 * $code^2) - (7.10128 * $code) + 3565.87" | bc -l 2>/dev/null)
    [ -n "$temp_c" ] && printf "%.1f" "$temp_c"
  elif echo "$raw" | grep -Eq '^[0-9]+$'; then
    if [ "$raw" -ge 100000 ]; then
      awk -v raw="$raw" 'BEGIN{printf "%.1f", raw/1000}'
    elif [ "$raw" -ge 1000 ]; then
      awk -v raw="$raw" 'BEGIN{printf "%.1f", raw/100}'
    else
      awk -v raw="$raw" 'BEGIN{printf "%.1f", raw}'
    fi
  else
    return 1
  fi
}

temp="N/A"
for t in /sys/class/thermal/thermal_zone*/temp /sys/class/hwmon/hwmon*/temp1_input; do
  if [ -r "$t" ]; then
    temp=$(read_temp_c "$t") && [ -n "$temp" ] && break
  fi
done

# ── Bitcoin ──
btc_running=0
best_block_date=""
if pidof bitcoind >/dev/null 2>&1; then
  btc_running=1
  btc_info=$(btc_cli getblockchaininfo); btc_net=$(btc_cli getnetworkinfo)
  headers=$(echo "$btc_info" | grep -o '"headers": *[0-9]*' | grep -o '[0-9]*')
  blocks=$(echo "$btc_info" | grep -o '"blocks": *[0-9]*' | grep -o '[0-9]*')
  progress=$(echo "$btc_info" | grep -o '"verificationprogress": *[0-9.e+-]*' | cut -d: -f2)
  ibd=$(echo "$btc_info" | grep -o '"initialblockdownload": *[a-z]*' | grep -o 'true\|false')
  chain=$(echo "$btc_info" | grep -o '"chain": *"[^"]*"' | cut -d'"' -f4)
  difficulty=$(echo "$btc_info" | grep -o '"difficulty": *[0-9.]*' | cut -d: -f2 | xargs)
  connections=$(echo "$btc_net" | grep -o '"connections": *[0-9]*' | grep -o '[0-9]*')
  subversion=$(echo "$btc_net" | grep -o '"subversion": *"[^"]*"' | cut -d'"' -f4)
  if [ -n "$blocks" ] && [ "$blocks" != "-" ]; then
    best_block_hash=$(btc_cli getblockhash "$blocks" | tr -d '"')
    if [ -n "$best_block_hash" ]; then
      best_block_time=$(btc_cli getblockheader "$best_block_hash" 2>/dev/null | grep -o '"time": *[0-9]*' | grep -o '[0-9]*' | head -n1)
      if [ -n "$best_block_time" ]; then
        best_block_date=$(date -u -d "@$best_block_time" '+%Y-%m-%d' 2>/dev/null || date -u -D '%s' -d "$best_block_time" '+%Y-%m-%d' 2>/dev/null || echo "")
      fi
    fi
  fi
  [ -n "$progress" ] && [ "$progress" != "null" ] && progress_pct=$(echo "$progress" | awk '{printf "%.2f", $1*100}') || progress_pct="0.00"
  rss=$(cat /proc/$(pidof bitcoind)/status 2>/dev/null | awk '/^VmRSS/{printf "%.0f", $2/1024}')
  [ -n "$rss" ] && rss="${rss}M" || rss="N/A"
else
  headers="-"; blocks="-"; progress_pct="0.00"
  ibd=""; chain="-"; difficulty="-"
  connections="-"; subversion=""
  rss="N/A"
fi
block_height_display="${headers} / ${blocks}"
[ -n "$best_block_date" ] && block_height_display="${block_height_display} (${best_block_date})"
ckpool_running=0; ckpool_tail=""
CKPOOL_CONF=/root/ckpool/ckpool.conf
btc_address=$(grep -m1 '"btcaddress"' "$CKPOOL_CONF" 2>/dev/null | cut -d'"' -f4)
[ -z "$btc_address" ] && btc_address="-"
ckpool_rpc_url=$(grep -m1 '"url"' "$CKPOOL_CONF" 2>/dev/null | cut -d'"' -f4)
case "$ckpool_rpc_url" in
  127.0.0.1:*|localhost:*|::1:*) stratum_mode="solo" ;;
  "") stratum_mode="-" ;;
  *) stratum_mode="pool" ;;
esac
if ps | grep -v grep | grep -q ckpool; then
  ckpool_running=1
  ckpool_tail=$(tail -5 /root/ckpool/logs/ckpool.log 2>/dev/null | sed 's/</\&lt;/g; s/>/\&gt;/g')
fi
wifi_ssid=$(iwconfig 2>/dev/null | grep -o 'ESSID:"[^"]*"' | cut -d'"' -f2); [ -z "$wifi_ssid" ] && wifi_ssid="—"
ip_addr=$(ip addr show wlan0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
[ -z "$ip_addr" ] && ip_addr=$(ip addr show eth0 2>/dev/null | grep "inet " | awk '{print $2}' | cut -d/ -f1)
[ -z "$ip_addr" ] && ip_addr="N/A"
ntp_running=0; ps | grep -v grep | grep -q ntpd && ntp_running=1

echo "Content-Type: text/html; charset=utf-8"
echo ""

cat <<ENDOFHTML
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="refresh" content="10">
<title>NM full node monitor</title>
<link rel="stylesheet" href="/style.css">
</head>
<body>
<div class="header">
<div class="header-left">
<h1>&#x26CF;&#xFE0F; NM full node monitor</h1>
<div class="sub" data-i18n="subtitle">Bitcoin Full Node + Solo Mining Pool</div>
</div>
<div class="lang-selector">
<label data-i18n="lang_label">Language</label>
<select id="langSelect" onchange="switchLang(this.value)">
<option value="en">English</option>
<option value="zh">&#x4E2D;&#x6587;</option>
<option value="es">Espa&ntilde;ol</option>
<option value="pt">Portugu&ecirc;s</option>
<option value="ru">&#x420;&#x443;&#x441;&#x441;&#x43A;&#x438;&#x439;</option>
<option value="de">Deutsch</option>
<option value="ja">&#x65E5;&#x672C;&#x8A9E;</option>
<option value="ko">&#xD55C;&#xAD6D;&#xC5B4;</option>
</select>
</div>
</div>
<div class="grid">

<!-- System -->
<div class="card">
<h2 data-i18n="card_sys">System</h2>
<table>
<tr><td data-i18n="cpu">CPU</td><td>${cpu_model}</td></tr>
<tr><td data-i18n="load">CPU Load</td><td>${load_pct}% (${nproc} cores)<div class="bar-bg"><div class="bar-fill" style="width:${load_bar_pct}%"></div></div></td></tr>
<tr><td data-i18n="uptime">Uptime</td><td>${uptime_fmt}</td></tr>
<tr><td data-i18n="temp">Temperature</td><td>${temp}°C</td></tr>
<tr><td data-i18n="memory">Memory</td><td>${mem_used}M / ${mem_total}M (${mem_pct}%)<div class="bar-bg"><div class="bar-fill" style="width:${mem_pct}%"></div></div></td></tr>
<tr><td data-i18n="disk">Disk</td><td>${disk_info}</td></tr>
ENDOFHTML

if [ "$block_disk_info" = "NOT_FOUND" ]; then
  echo '<tr><td data-i18n="block_disk">Block Data</td><td><span class="badge-err">&#x274C; <span data-i18n="not_detected">Not Detected</span></span></td></tr>'
else
  echo "<tr><td data-i18n=\"block_disk\">Block Data</td><td>${block_disk_info}</td></tr>"
fi

cat <<ENDOFHTML
</table>
</div>

<!-- Bitcoin -->
<div class="card">
<h2 data-i18n="card_btc">Bitcoin Node</h2>
<table>
<tr><td data-i18n="chain">Chain</td><td>${chain} (${subversion})</td></tr>
ENDOFHTML

if [ "$btc_running" = "0" ]; then
  echo '<tr><td data-i18n="sync_status">Sync</td><td class="badge-err">&#x274C; <span data-i18n="btc_not_running">Not Running</span></td></tr>'
elif [ "$ibd" = "true" ]; then
  echo '<tr><td data-i18n="sync_status">Sync</td><td class="badge-ok">&#x23F3; <span data-i18n="syncing">IBD Syncing</span></td></tr>'
else
  echo '<tr><td data-i18n="sync_status">Sync</td><td class="badge-ok">&#x2705; <span data-i18n="synced">Synced</span></td></tr>'
fi

cat <<ENDOFHTML
<tr><td data-i18n="block_height">Height</td><td>${block_height_display}</td></tr>
<tr><td data-i18n="ibd_progress">IBD Progress</td><td>${progress_pct}%<div class="bar-bg"><div class="bar-fill" style="width:${progress_pct}%"></div></div></td></tr>
<tr><td data-i18n="connections">Conn</td><td>${connections}</td></tr>
<tr><td data-i18n="difficulty">Diff</td><td>${difficulty}</td></tr>
<tr><td data-i18n="rss_memory">RSS</td><td>${rss}</td></tr>
</table>
</div>

<!-- Mining -->
<div class="card">
<h2 data-i18n="card_mining">Stratum Service</h2>
<table>
ENDOFHTML

if [ "$ckpool_running" = "1" ]; then
  echo '<tr><td data-i18n="service_status">Service</td><td class="badge-ok">ckpool | <span data-i18n="running">Running</span></td></tr>'
else
  echo '<tr><td data-i18n="service_status">Service</td><td class="badge-err">ckpool | <span data-i18n="stopped">Stopped</span></td></tr>'
fi

cat <<ENDOFHTML
<tr><td data-i18n="stratum_mode">Stratum Mode</td><td>${stratum_mode}</td></tr>
<tr><td data-i18n="btc_address">BTC Address</td><td><input type="text" readonly value="${btc_address}" onclick="this.select();" onfocus="this.select();" spellcheck="false" style="width:100%;box-sizing:border-box;background:#0d1117;border:1px solid #30363d;color:#e6edf3;border-radius:8px;padding:6px 8px;font:inherit"></td></tr>
</table>
ENDOFHTML
if [ -n "$ckpool_tail" ]; then
  echo '<div style="margin-top:10px;color:#8b949e;font-size:0.8rem"><span data-i18n="recent_logs">Recent Logs</span></div><div class="log-box">'"$ckpool_tail"'</div>'
fi

cat <<ENDOFHTML
</div>

<!-- Network -->
<div class="card">
<h2 data-i18n="card_network">Network</h2>
<table>
<tr><td data-i18n="ip_address">IP</td><td>${ip_addr}</td></tr>
<tr><td data-i18n="wifi_ssid">WiFi</td><td>${wifi_ssid}</td></tr>
ENDOFHTML

if [ "$ntp_running" = "1" ]; then
  echo '<tr><td data-i18n="ntp_status">NTP</td><td class="badge-ok">&#x2705; <span data-i18n="running">Running</span></td></tr>'
else
  echo '<tr><td data-i18n="ntp_status">NTP</td><td class="badge-ok">&#x274C; <span data-i18n="not_synced">Not Synced</span></td></tr>'
fi

cat <<'ENDOFHTML'
</table>
</div>

</div>
<div class="refresh" data-i18n="footer">Auto-refresh 10s | Busybox httpd + Shell CGI</div>

<script>
var L={
subtitle:{en:'Bitcoin Full Node + Solo Mining Pool',zh:'Bitcoin\u5168\u8282\u70B9 + \u72EC\u7ACB\u6316\u77FF\u6C60',es:'Nodo Completo Bitcoin + Pool de Miner\u00EDa Solo',pt:'N\u00F3 Completo Bitcoin + Pool de Minera\u00E7\u00E3o Solo',ru:'\u041F\u043E\u043B\u043D\u0430\u044F \u043D\u043E\u0434\u0430 Bitcoin + \u0421\u043E\u043B\u043E-\u043C\u0430\u0439\u043D\u0438\u043D\u0433 \u043F\u0443\u043B',de:'Bitcoin Full Node + Solo Mining Pool',ja:'Bitcoin\u30D5\u30EB\u30CE\u30FC\u30C9 + \u30BD\u30ED\u30DE\u30A4\u30CB\u30F3\u30B0\u30D7\u30FC\u30EB',ko:'\uBE44\uD2B8\uCF54\uC778 \uD480\uB178\uB4DC + \uC194\uB85C \uB9C8\uC774\uB2DD \uD480'},
lang_label:{en:'Language',zh:'\u8BED\u8A00',es:'Idioma',pt:'Idioma',ru:'\u042F\u0437\u044B\u043A',de:'Sprache',ja:'\u8A00\u8A9E',ko:'\uC5B8\uC5B4'},
card_sys:{en:'System',zh:'\u7CFB\u7EDF\u4FE1\u606F',es:'Sistema',pt:'Sistema',ru:'\u0421\u0438\u0441\u0442\u0435\u043C\u0430',de:'System',ja:'\u30B7\u30B9\u30C6\u30E0',ko:'\uC2DC\uC2A4\uD15C'},
cpu:{en:'CPU',zh:'CPU',es:'CPU',pt:'CPU',ru:'\u0426\u041F',de:'CPU',ja:'CPU',ko:'CPU'},
load:{en:'CPU Load',zh:'CPU\u8D1F\u8F7D',es:'Carga CPU',pt:'Carga CPU',ru:'\u041D\u0430\u0433\u0440\u0443\u0437\u043A\u0430 CPU',de:'CPU-Last',ja:'CPU\u8CA0\u8377',ko:'CPU \uBD80\uD558'},
uptime:{en:'Uptime',zh:'\u8FD0\u884C\u65F6\u95F4',es:'Actividad',pt:'Tempo Ativo',ru:'\u0410\u043F\u0442\u0430\u0439\u043C',de:'Laufzeit',ja:'\u7A3C\u50CD\u6642\u9593',ko:'\uAC00\uB3D9 \uC2DC\uAC04'},
temp:{en:'Temp',zh:'\u6E29\u5EA6',es:'Temp',pt:'Temp',ru:'\u0422\u0435\u043C\u043F',de:'Temp',ja:'\u6E29\u5EA6',ko:'\uC628\uB3C4'},
memory:{en:'Memory',zh:'\u5185\u5B58',es:'Memoria',pt:'Mem\u00F3ria',ru:'\u041F\u0430\u043C\u044F\u0442\u044C',de:'Speicher',ja:'\u30E1\u30E2\u30EA',ko:'\uBA54\uBAA8\uB9AC'},
disk:{en:'Disk',zh:'\u78C1\u76D8',es:'Disco',pt:'Disco',ru:'\u0414\u0438\u0441\u043A',de:'Platte',ja:'\u30C7\u30A3\u30B9\u30AF',ko:'\uB514\uC2A4\uD06C'},
block_disk:{en:'Block Data',zh:'\u533A\u5757\u6570\u636E',es:'Datos Block',pt:'Dados Block',ru:'\u0414\u0430\u043D\u043D\u044B\u0435 \u0431\u043B\u043E\u043A\u043E\u0432',de:'Block-Daten',ja:'\u30D6\u30ED\u30C3\u30AF\u30C7\u30FC\u30BF',ko:'\uBE14\uB85D \uB370\uC774\uD130'},
not_detected:{en:'Not Detected',zh:'\u672A\u68C0\u6D4B\u5230',es:'No detectado',pt:'N\u00E3o detectado',ru:'\u041D\u0435 \u043E\u0431\u043D\u0430\u0440\u0443\u0436\u0435\u043D',de:'Nicht erkannt',ja:'\u672A\u691C\u51FA',ko:'\uAC10\uC9C0 \uC548 \uB428'},
card_btc:{en:'Bitcoin Node',zh:'Bitcoin \u8282\u70B9',es:'Nodo Bitcoin',pt:'N\u00F3 Bitcoin',ru:'\u041D\u043E\u0434\u0430 Bitcoin',de:'Bitcoin-Knoten',ja:'Bitcoin\u30CE\u30FC\u30C9',ko:'\uBE44\uD2B8\uCF54\uC778 \uB178\uB4DC'},
chain:{en:'Chain',zh:'\u94FE',es:'Cadena',pt:'Rede',ru:'\u0421\u0435\u0442\u044C',de:'Kette',ja:'\u30C1\u30A7\u30FC\u30F3',ko:'\uCCB4\uC778'},
sync_status:{en:'Sync',zh:'\u72B6\u6001',es:'Sincro',pt:'Sincro',ru:'\u0421\u0438\u043D\u0445',de:'Sync',ja:'\u540C\u671F',ko:'\uB3D9\uAE30\uD654'},
syncing:{en:'IBD Syncing',zh:'IBD\u540C\u6B65\u4E2D',es:'Sincronizando',pt:'Sincronizando',ru:'\u0421\u0438\u043D\u0445\u0440\u043E\u043D\u0438\u0437\u0430\u0446\u0438\u044F',de:'IBD Sync',ja:'IBD\u540C\u671F\u4E2D',ko:'IBD \uB3D9\uAE30\uD654 \uC911'},
synced:{en:'Synced',zh:'\u5DF2\u540C\u6B65',es:'Sincronizado',pt:'Sincronizado',ru:'\u0421\u0438\u043D\u0445\u0440.',de:'Sync',ja:'\u540C\u671F\u6E08',ko:'\uB3D9\uAE30\uD654 \uC644\uB8CC'},
btc_not_running:{en:'Not Running',zh:'\u672A\u8FD0\u884C',es:'No ejec.',pt:'N\u00E3o exec.',ru:'\u041D\u0435 \u0437\u0430\u043F\u0443\u0449.',de:'Nicht gest.',ja:'\u672A\u8D77\u52D5',ko:'\uC2E4\uD589 \uC548 \uB428'},
block_height:{en:'Height',zh:'\u533A\u5757\u9AD8\u5EA6',es:'Altura',pt:'Altura',ru:'\u0412\u044B\u0441\u043E\u0442\u0430',de:'H\u00F6he',ja:'\u30D6\u30ED\u30C3\u30AF\u9AD8',ko:'\uBE14\uB85D \uB192\uC774'},
ibd_progress:{en:'IBD Progress',zh:'IBD\u8FDB\u5EA6',es:'Progreso IBD',pt:'Progresso IBD',ru:'\u041F\u0440\u043E\u0433\u0440\u0435\u0441\u0441 IBD',de:'IBD Fortschritt',ja:'IBD\u9032\u6357',ko:'IBD \uC9C4\uD589\uB960'},
connections:{en:'Conn',zh:'\u8FDE\u63A5\u6570',es:'Conexiones',pt:'Conex\u00F5es',ru:'\u0421\u043E\u0435\u0434.',de:'Verb.',ja:'\u63A5\u7D9A\u6570',ko:'\uC5F0\uACB0'},
difficulty:{en:'Diff',zh:'\u96BE\u5EA6',es:'Dificultad',pt:'Dificuldade',ru:'\u0421\u043B\u043E\u0436\u043D\u043E\u0441\u0442\u044C',de:'Schwierigkeit',ja:'\u96E3\u6613\u5EA6',ko:'\uB09C\uC774\uB3C4'},
rss_memory:{en:'RSS',zh:'RSS\u5185\u5B58',es:'RSS',pt:'RSS',ru:'RSS',de:'RSS',ja:'RSS',ko:'RSS'},
card_mining:{en:'Stratum Service',zh:'Stratum\u670D\u52A1',es:'Servicio Stratum',pt:'Servi\u00E7o Stratum',ru:'\u0421\u0435\u0440\u0432\u0438\u0441 Stratum',de:'Stratum-Dienst',ja:'Stratum\u30B5\u30FC\u30D3\u30B9',ko:'Stratum \uC11C\uBE44\uC2A4'},
service_status:{en:'Service',zh:'\u670D\u52A1',es:'Servicio',pt:'Servi\u00E7o',ru:'\u0421\u0435\u0440\u0432\u0438\u0441',de:'Dienst',ja:'\u30B5\u30FC\u30D3\u30B9',ko:'\uC11C\uBE44\uC2A4'},
running:{en:'Running',zh:'\u8FD0\u884C\u4E2D',es:'En ejec.',pt:'Executando',ru:'\u0417\u0430\u043F\u0443\u0449\u0435\u043D',de:'L\u00E4uft',ja:'\u5B9F\u884C\u4E2D',ko:'\uC2E4\uD589 \uC911'},
stopped:{en:'Stopped',zh:'\u672A\u8FD0\u884C',es:'Detenido',pt:'Parado',ru:'\u041E\u0441\u0442\u0430\u043D\u043E\u0432\u043B\u0435\u043D',de:'Gestoppt',ja:'\u505C\u6B62\u4E2D',ko:'\uC911\uC9C0\uB428'},
not_synced:{en:'Not Sync',zh:'\u672A\u540C\u6B65',es:'No Sinc.',pt:'N\u00E3o Sinc.',ru:'\u041D\u0435 \u0441\u0438\u043D\u0445.',de:'Nicht sync.',ja:'\u672A\u540C\u671F',ko:'\uBBF8\uB3D9\uAE30\uD654'},
ntp_time:{en:'NTP Time',zh:'NTP\u65F6\u95F4',es:'Hora NTP',pt:'Hora NTP',ru:'\u0412\u0440\u0435\u043C\u044F NTP',de:'NTP-Zeit',ja:'NTP\u6642\u523B',ko:'NTP \uC2DC\uAC04'},
stratum_mode:{en:'Stratum Mode',zh:'Stratum\u6A21\u5F0F',es:'Modo Stratum',pt:'Modo Stratum',ru:'\u0420\u0435\u0436\u0438\u043C Stratum',de:'Stratum-Modus',ja:'Stratum\u30E2\u30FC\u30C9',ko:'Stratum \uBAA8\uB4DC'},
btc_address:{en:'BTC Address',zh:'BTC\u5730\u5740',es:'Direcci\u00F3n BTC',pt:'Endere\u00E7o BTC',ru:'BTC \u0430\u0434\u0440\u0435\u0441',de:'BTC-Adresse',ja:'BTC\u30A2\u30C9\u30EC\u30B9',ko:'BTC \uC8FC\uC18C'},
recent_logs:{en:'Recent Logs',zh:'\u6700\u8FD1\u65E5\u5FD7',es:'Registros',pt:'Logs Recentes',ru:'\u041B\u043E\u0433\u0438',de:'Letzte Logs',ja:'\u6700\u8FD1\u306E\u30ED\u30B0',ko:'\uCD5C\uADFC \uB85C\uADF8'},
card_network:{en:'Network',zh:'\u7F51\u7EDC',es:'Red',pt:'Rede',ru:'\u0421\u0435\u0442\u044C',de:'Netzwerk',ja:'\u30CD\u30C3\u30C8\u30EF\u30FC\u30AF',ko:'\uB124\uD2B8\uC6CC\uD06C'},
ip_address:{en:'IP',zh:'IP\u5730\u5740',es:'IP',pt:'IP',ru:'IP',de:'IP',ja:'IP',ko:'IP'},
wifi_ssid:{en:'WiFi',zh:'WiFi',es:'WiFi',pt:'WiFi',ru:'WiFi',de:'WLAN',ja:'WiFi',ko:'WiFi'},
ntp_status:{en:'NTP',zh:'NTP',es:'NTP',pt:'NTP',ru:'NTP',de:'NTP',ja:'NTP',ko:'NTP'},
footer:{en:'Auto-refresh 10s | Busybox httpd + Shell CGI',zh:'\u81EA\u52A8\u5237\u65B010\u79D2 | Busybox httpd + Shell CGI',es:'Auto-refresh 10s | Busybox httpd + Shell CGI',pt:'Auto-refresh 10s | Busybox httpd + Shell CGI',ru:'\u0410\u0432\u0442\u043E\u043E\u0431\u043D\u043E\u0432\u043B\u0435\u043D\u0438\u0435 10\u0441 | Busybox httpd + Shell CGI',de:'Auto-refresh 10s | Busybox httpd + Shell CGI',ja:'10\u79D2\u81EA\u52D5\u66F4\u65B0 | Busybox httpd + Shell CGI',ko:'10\uCD08 \uC790\uB3D9 \uAC31\uC2E0 | Busybox httpd + Shell CGI'}
};
function switchLang(l){
  localStorage.setItem('nmm_lang',l);
  document.querySelectorAll('[data-i18n]').forEach(function(e){
    var k=e.getAttribute('data-i18n');
    if(L[k]&&L[k][l]) e.textContent=L[k][l];
  });
  document.getElementById('langSelect').value=l;
}
(function(){
  var s=localStorage.getItem('nmm_lang')||'zh';
  document.getElementById('langSelect').value=s;
  switchLang(s);
})();
</script>
</body>
</html>
ENDOFHTML
