#!/bin/sh
# 临时诊断脚本：定位 CPU 未归属到常驻进程的部分
# 用法: ssh root@IP 'sh -s' < tools/.cpu-diagnose.sh

echo "=== /proc/stat cpu 行采样 (10s) ==="
awk 'NR==1{print "before: "$0}' /proc/stat
awk 'NR==1{print "irq-before:", $7, "softirq-before:", $8, "iowait-before:", $6}' /proc/stat
F1=$(awk '{s=0; for(i=1;i<=NF;i++) s+=$i; print s}' /proc/stat 2>/dev/null)
echo "--- 10s window ---"
sleep 10
awk 'NR==1{print "after : "$0}' /proc/stat
awk 'NR==1{print "irq-after :", $7, "softirq-after :", $8, "iowait-after :", $6}' /proc/stat

echo
echo "=== 进程 fork 速率 (10s) ==="
P1=$(awk -F' ' '{print $1" "$2" "$3}' /proc/loadavg)
echo "loadavg before: $P1"
N1=$(ls -d /proc/[0-9]* 2>/dev/null | wc -l)
echo "proc count before: $N1"
sleep 10
N2=$(ls -d /proc/[0-9]* 2>/dev/null | wc -l)
echo "proc count after : $N2"

echo
echo "=== 中断统计 (/proc/interrupts 前 20 行) ==="
head -20 /proc/interrupts

echo
echo "=== softirq 统计 ==="
head -5 /proc/softirqs

echo
echo "=== 全部进程列表 (pid ppid stat comm) ==="
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
    printf "pid=%-7s ppid=%-7s state=%s utime=%s stime=%s  %s\n", pid, a[2], a[1], a[12], a[13], comm
  }' "$p"
done

echo
echo "=== ckpool 进程状态 ==="
ls -l /proc/2245/fd 2>/dev/null | head -20

echo
echo "=== 最近的 fork/exec 痕迹: /proc/sys/kernel/pid_max 与当前 pid ==="
cat /proc/sys/kernel/pid_max
cat /proc/loadavg

echo
echo "=== dmesg 尾部 ==="
dmesg 2>/dev/null | tail -30
