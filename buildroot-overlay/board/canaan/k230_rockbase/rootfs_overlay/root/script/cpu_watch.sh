#!/bin/sh
#
# Rockbase 持续 CPU 压力观测脚本（长时版）
#
# 用法: sh /root/script/cpu_watch.sh [总时长秒=3600] [高频间隔秒=30] [进程分布间隔秒=900]
# 示例: sh /root/script/cpu_watch.sh 3600 30 900
#
# 功能:
#   - 高频(默认30s): CPU各态、调度、内存回收、SD I/O、内存、温度
#   - 低频(默认900s): 调用 cpu_page.sh 做一次60s窗口的逐进程CPU%分布
#   - 全程后台运行，不依赖 SSH 持续连接
#   - 兼容 BusyBox（只用 awk/cut/cat/date/sleep）
#
# 输出: /tmp/cpu_watch_<时间戳>.log
#   高频行: 每个采样周期的 CPU / 调度 / VM / SD 增量及当前内存、温度
#   低频块: cpu_page.sh 的标准输出（含整机CPU + 进程排行）
#

TOTAL="${1:-3600}"
FAST="${2:-30}"
PROC_INT="${3:-900}"
PROC_WIN=60
SD_DEV=mmcblk1
STAMP=$(date +%Y%m%d_%H%M%S)
LOG=/tmp/cpu_watch_${STAMP}.log
DIST_LOG=/tmp/cpu_watch_${STAMP}_process.log
LATEST=/tmp/cpu_watch_latest.log
LATEST_DIST=/tmp/cpu_watch_latest_process.log

ln -sf "$LOG" "$LATEST"
ln -sf "$DIST_LOG" "$LATEST_DIST"

snapshot() {
  awk '
    /^cpu / {user=$2+$3; syscpu=$4; idle=$5; iowait=$6; irq=$7+$8; steal=$9}
    /^intr / {intr=$2}
    /^ctxt / {ctxt=$2}
    /^processes / {forks=$2}
    /^procs_running / {running=$2}
    /^procs_blocked / {blocked=$2}
    END {print user,syscpu,idle,iowait,irq,steal,intr,ctxt,forks,running,blocked}
  ' /proc/stat
  awk '
    /^pgfault / {fault=$2}
    /^pgmajfault / {major=$2}
    /^pgscan_kswapd / {scan_k=$2}
    /^pgscan_direct / {scan_d=$2}
    /^pgsteal_kswapd / {steal_k=$2}
    /^pgsteal_direct / {steal_d=$2}
    /^allocstall_/ {stall+=$2}
    /^oom_kill / {oom=$2}
    END {print fault,major,scan_k,scan_d,steal_k,steal_d,stall,oom}
  ' /proc/vmstat
  awk -v dev="$SD_DEV" '
    $3==dev {print $4,$6,$7,$8,$10,$11,$12,$13,$14; found=1}
    END {if(!found) print "0 0 0 0 0 0 0 0 0"}
  ' /proc/diskstats
}

{
  echo "===== CPU WATCH START $(date) ====="
  echo "total=${TOTAL}s fast=${FAST}s proc_interval=${PROC_INT}s proc_window=${PROC_WIN}s"
  echo "nproc=$(nproc)"
  echo "sd_device=$SD_DEV"
  echo "cpu_model=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2 | sed 's/^ *//')"
  echo "log_file=$LOG"
  echo "process_log=$DIST_LOG"
} > "$LOG"
: > "$DIST_LOG"

set -- $(snapshot)
u1=$1; s1=$2; id1=$3; iw1=$4; irq1=$5; st1=$6; intr1=$7; ctxt1=$8; forks1=$9
shift 9
run1=$1; block1=$2; fault1=$3; major1=$4; scan_k1=$5; scan_d1=$6; steal_k1=$7; steal_d1=$8; stall1=$9
shift 9
oom1=$1; rd1=$2; rdsec1=$3; rdms1=$4; wr1=$5; wrsec1=$6; wrms1=$7; inflight1=$8; ioms1=$9
shift 9
weighted1=$1
start=$(date +%s)
last_proc=$start

while :; do
  now=$(date +%s)
  [ $((now - start)) -ge $TOTAL ] && break
  sleep "$FAST"
  now=$(date +%s)
  elapsed=$((now - start))
  set -- $(snapshot)
  u2=$1; s2=$2; id2=$3; iw2=$4; irq2=$5; st2=$6; intr2=$7; ctxt2=$8; forks2=$9
  shift 9
  run2=$1; block2=$2; fault2=$3; major2=$4; scan_k2=$5; scan_d2=$6; steal_k2=$7; steal_d2=$8; stall2=$9
  shift 9
  oom2=$1; rd2=$2; rdsec2=$3; rdms2=$4; wr2=$5; wrsec2=$6; wrms2=$7; inflight2=$8; ioms2=$9
  shift 9
  weighted2=$1
  cpu=$(awk -v u=$((u2-u1)) -v s=$((s2-s1)) -v id=$((id2-id1)) -v iw=$((iw2-iw1)) -v irq=$((irq2-irq1)) -v st=$((st2-st1)) 'BEGIN{
    total=u+s+id+iw+irq+st
    if(total<=0){print "busy=na usr=na sys=na iowait=na irq=na steal=na idle=na"; exit}
    printf "busy=%.1f usr=%.1f sys=%.1f iowait=%.1f irq=%.1f steal=%.1f idle=%.1f", (u+s+irq+st)*100/total,u*100/total,s*100/total,iw*100/total,irq*100/total,st*100/total,id*100/total
  }')
  load=$(cat /proc/loadavg)
  mem=$(awk '/^MemAvailable/{a=$2} /^MemTotal/{t=$2} END{printf "%d/%d",a,t}' /proc/meminfo)
  temp=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null)
  sd_busy=$(awk -v ms=$((ioms2-ioms1)) -v dt=$FAST 'BEGIN{printf "%.1f",ms*100/(dt*1000)}')
  sd="rd_ios=$((rd2-rd1)) rd_kb=$(((rdsec2-rdsec1)/2)) rd_ms=$((rdms2-rdms1)) wr_ios=$((wr2-wr1)) wr_kb=$(((wrsec2-wrsec1)/2)) wr_ms=$((wrms2-wrms1)) inflight=$inflight2 busy_pct=$sd_busy weighted_ms=$((weighted2-weighted1))"
  echo "$(date '+%H:%M:%S') t=${elapsed}s cpu[$cpu] load[$load] sched[ctxt=$((ctxt2-ctxt1)) intr=$((intr2-intr1)) forks=$((forks2-forks1)) running=$run2 blocked=$block2] mem_kb=$mem vm[fault=$((fault2-fault1)) major=$((major2-major1)) scan_k=$((scan_k2-scan_k1)) scan_d=$((scan_d2-scan_d1)) steal_k=$((steal_k2-steal_k1)) steal_d=$((steal_d2-steal_d1)) allocstall=$((stall2-stall1)) oom=$((oom2-oom1))] sd[$sd] temp_raw=$temp" >> "$LOG"
  u1=$u2; s1=$s2; id1=$id2; iw1=$iw2; irq1=$irq2; st1=$st2; intr1=$intr2; ctxt1=$ctxt2; forks1=$forks2
  run1=$run2; block1=$block2; fault1=$fault2; major1=$major2; scan_k1=$scan_k2; scan_d1=$scan_d2; steal_k1=$steal_k2; steal_d1=$steal_d2; stall1=$stall2
  oom1=$oom2; rd1=$rd2; rdsec1=$rdsec2; rdms1=$rdms2; wr1=$wr2; wrsec1=$wrsec2; wrms1=$wrms2; inflight1=$inflight2; ioms1=$ioms2; weighted1=$weighted2
  if [ $((now - last_proc)) -ge $PROC_INT ]; then
    last_proc=$now
    sh /root/script/cpu_page.sh watch_dist $PROC_WIN >> "$DIST_LOG" 2>&1 &
  fi
done

wait
echo "" >> "$LOG"
echo "===== CPU WATCH END $(date) =====" >> "$LOG"
echo "$LOG"