#!/bin/sh
# 精确 CPU 归属：覆盖采样窗口内存在过的所有进程（含短命进程）
# 用法: ssh root@IP 'sh -s' < tools/.cpu-attr.sh

SNAP1=/tmp/attr1.$$
SNAP2=/tmp/attr2.$$
W="${1:-15}"

snap() {
  # 输出: pid utime stime comm
  for p in /proc/[0-9]*/stat; do
    [ -r "$p" ] || continue
    pid=${p#/proc/}; pid=${pid%/stat}
    awk -v pid="$pid" '{
      n=0; for(i=length($0); i>0; i--) if(substr($0,i,1)==")"){n=i; break}
      if(n==0) next
      m=0; for(i=1;i<=length($0);i++) if(substr($0,i,1)=="("){m=i;break}
      if(m==0) next
      comm=substr($0, m+1, n-m-1)
      rest=substr($0, n+2); split(rest, a, " ")
      printf "%s %s %s %s\n", pid, a[12], a[13], comm
    }' "$p"
  done
}

readcpu() { awk 'NR==1{printf "%s %s %s %s %s %s %s\n", $2,$3,$4,$5,$6,$7,$8}' /proc/stat; }
readprocs() { awk '/^processes/{print $2}' /proc/stat; }
readnet() { awk -F'[: ]+' '/eth0|wlan0/{printf "%s rx=%s tx=%s\n", $2, $3, $11}' /proc/net/dev; }

echo "=== CPU 归属分析 (窗口 ${W}s) ==="
set -- $(readcpu); u1=$1 n1=$2 s1=$3 i1=$4 w1=$5 q1=$6 q2_1=$7
F1=$(readprocs)
snap > "$SNAP1"
sleep "$W"
set -- $(readcpu); u2=$1 n2=$2 s2=$3 i2=$4 w2=$5 q2=$6 q2_2=$7
F2=$(readprocs)
snap > "$SNAP2"

# 整机分解
awk -v u1=$u1 -v u2=$u2 -v s1=$s1 -v s2=$s2 -v i1=$i1 -v i2=$i2 -v w1=$w1 -v w2=$w2 -v q1=$q2_1 -v q2=$q2_2 'BEGIN{
  du=u2-u1; ds=s2-s1; di=i2-i1; dw=w2-w1; dq=q2-q1;
  t=du+ds+di+dw+dq;
  if(t<=0){print "n/a"; exit}
  printf "总 ticks=%d\n", t;
  printf "  user     %6.1f%%\n", du*100/t;
  printf "  system   %6.1f%%\n", ds*100/t;
  printf "  idle     %6.1f%%\n", di*100/t;
  printf "  iowait   %6.1f%%\n", dw*100/t;
  printf "  softirq  %6.1f%%\n", dq*100/t;
  printf "  BUSY     %6.1f%%\n", (t-di-dw)*100/t;
}'

echo
echo "fork 速率: $((F2-F1)) 次 / ${W}s"
echo

echo "--- 各进程 CPU% (含死亡进程, 按消耗排序) ---"
awk -v w="$W" '
  NR==FNR { if(!(($1) in seen)){ u1[$1]=$2; s1[$1]=$3; }; name[$1]=$4; seen[$1]=1; next }
  { u2[$1]=$2; s2[$1]=$3; name[$1]=$4; seen[$1]=1 }
  END {
    tot=0
    for(pid in seen){
      a=(pid in u1)?u1[pid]:0; b=(pid in u2)?u2[pid]:0
      c=(pid in s1)?s1[pid]:0; d=(pid in s2)?s2[pid]:0
      du=b-a; ds=d-c; dt=du+ds
      if(dt<=0) continue
      tot+=dt
      printf "%8.2f%%  (u=%.2f%% s=%.2f%%)  pid=%-6s %s\n", dt*100/(w*100), du*100/(w*100), ds*100/(w*100), pid, name[pid]
    }
    printf "---- 合计 %.2f%% ----\n", tot*100/(w*100)
  }
' "$SNAP1" "$SNAP2" | sort -rn -k1

echo
echo "=== 网络计数 ==="
readnet
echo
echo "=== loadavg / mem ==="
cat /proc/loadavg
free -m
echo
echo "=== 磁盘 IO (mmc1 = SD卡) ==="
grep -E "mmcblk1 " /proc/diskstats 2>/dev/null || grep mmcblk1 /proc/diskstats

echo
echo "=== 内核线程 system 时间 top10 (绝对值 ticks) ==="
for p in /proc/[0-9]*/stat; do
  [ -r "$p" ] || continue
  pid=${p#/proc/}; pid=${pid%/stat}
  awk -v pid="$pid" '{
    n=0; for(i=length($0); i>0; i--) if(substr($0,i,1)==")"){n=i; break}
    if(n==0) next
    m=0; for(i=1;i<=length($0);i++) if(substr($0,i,1)=="("){m=i;break}
    if(m==0) next
    comm=substr($0, m+1, n-m-1)
    rest=substr($0, n+2); split(rest, a, " ")
    printf "%s %s %s %s\n", a[13]+0, pid, a[12]+0, comm
  }' "$p"
done | sort -rn | head -12

rm -f "$SNAP1" "$SNAP2"
