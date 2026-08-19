#!/bin/sh
#
# Rockbase CPU 压力分布测试脚本
#
# 用法: sh /root/script/cpu_page.sh <页面标签> [采样秒数, 默认60]
# 示例: sh /root/script/cpu_page.sh Dashboard 60
#
# 功能:
#   - 首尾各拍一次全进程 CPU ticks 快照，计算采样窗口内每个进程的 CPU%
#   - 输出整机 CPU 使用率（单核）+ 各进程 CPU% 排行（含 RSS）
#   - 用于对比不同 UI 页面/场景下的 CPU 压力分布
#
# 注意:
#   - 板子为单核 RISC-V，CPU% 即占该核百分比
#   - 对比不同页面时，重点看 rockbase-lcdui 一行的变化；
#     bitcoind/ckpool/内核态是各页共有的背景压力
#   - 采样前 UI 需稳定停留在目标页面（测的是该页持续刷新消耗，非切页瞬间）
#   - 兼容 BusyBox ash/awk（无 getconf/pgrep/ps --sort）
#

PAGE="${1:-unknown}"
DUR="${2:-60}"
SNAP=/tmp/cpu_snap.$$

# 拍快照: 每行 "pid cpu_ticks rss comm"
snap() {
  for p in /proc/[0-9]*/stat; do
    [ -r "$p" ] || continue
    pid=${p#/proc/}; pid=${pid%/stat}
    rss=$(awk "/^VmRSS/{print \$2}" /proc/$pid/status 2>/dev/null)
    awk -v rss="${rss:-0}" -v pid="$pid" '
    {
      n=0
      for(i=length($0); i>0; i--) if(substr($0,i,1)==")"){n=i; break}
      if(n==0) next
      m=0
      for(i=1;i<=length($0);i++) if(substr($0,i,1)=="("){m=i;break}
      if(m==0) next
      comm=substr($0, m+1, n-m-1)
      rest=substr($0, n+2)
      split(rest, a, " ")
      cpu=a[12]+a[13]
      printf "%s %d %d %s\n", pid, cpu, rss, comm
    }' "$p"
  done
}

echo "===== PAGE=$PAGE  采样 ${DUR}s ====="
echo "start: $(date '+%H:%M:%S')  loadavg: $(cut -d' ' -f1-3 /proc/loadavg)"

# 读 /proc/stat 第一行: user nice system idle iowait irq softirq
readstat() { awk 'NR==1{print $2+$3+$4+$5+$6+$7+$8, $5+$6}' /proc/stat; }
set -- $(readstat); sys1=$1; idle1=$2
snap > "$SNAP"
sleep "$DUR"
set -- $(readstat); sys2=$1; idle2=$2
snap > "$SNAP.after"

echo "end:   $(date '+%H:%M:%S')  loadavg: $(cut -d' ' -f1-3 /proc/loadavg)"
echo

# 整机 CPU 使用率 = 1 - idle增量/总增量
awk -v s1=$sys1 -v s2=$sys2 -v i1=$idle1 -v i2=$idle2 'BEGIN{
  if(s2-s1<=0){print "整机CPU: n/a"}
  else printf "整机CPU使用率: %.1f%% (单核, idle增量=%d/总增量=%d)\n", (1-(i2-i1)/(s2-s1))*100, i2-i1, s2-s1
}'
echo
echo "--- 各进程 CPU% (占单核, 按消耗排序) ---"
awk -v s1=$sys1 -v s2=$sys2 '
  NR==FNR { cpu[$1]=$2; rss[$1]=$3; name[$1]=$4; next }
  {
    d=$2-cpu[$1]
    if(d<=0) next
    pct=d*100/(s2-s1)
    if(pct<0.3) next
    printf "%6.1f%%  rss=%6dKB  pid=%s  %s\n", pct, rss[$1], $1, $4
  }
' "$SNAP" "$SNAP.after" | sort -rn
echo
echo "===== PAGE=$PAGE 结束 ====="
rm -f "$SNAP" "$SNAP.after"
