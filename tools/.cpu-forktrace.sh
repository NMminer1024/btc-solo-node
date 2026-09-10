#!/bin/sh
# 追踪 fork 源头：找出谁在疯狂创建短命进程
# 用法: ssh root@IP 'sh -s' < tools/.cpu-forktrace.sh

echo "=== fork 源头追踪 (10s) ==="
echo "--- 方法1: 反复采样 /proc 找出新生进程的 ppid ---"

prev=/tmp/ft_prev.$$
: > "$prev"

i=0
while [ $i -lt 60 ]; do
  for p in /proc/[0-9]*/stat; do
    [ -r "$p" ] || continue
    pid=${p#/proc/}; pid=${pid%/stat}
    if ! grep -q "^${pid}$" "$prev" 2>/dev/null; then
      awk -v pid="$pid" '{
        n=0; for(i=length($0); i>0; i--) if(substr($0,i,1)==")"){n=i; break}
        if(n==0) next
        m=0; for(i=1;i<=length($0);i++) if(substr($0,i,1)=="("){m=i;break}
        if(m==0) next
        comm=substr($0, m+1, n-m-1)
        rest=substr($0, n+2); split(rest, a, " ")
        printf "%s %s %s\n", a[2], comm, a[4]
      }' "$p"
      echo "$pid" >> "$prev"
    fi
  done
  i=$((i+1))
done
echo "(采样结束)"
echo "new pid 数: $(wc -l < "$prev")"

echo
echo "=== 方法2: 用 ps 看当前所有进程树 ==="
ps -ef 2>/dev/null | head -70

echo
echo "=== 方法3: httpd / statusd 相关脚本内容检查 ==="
echo "--- rockbase_statusd.sh 主循环 ---"
grep -nE "while|sleep|for |spawn|&" /usr/bin/rockbase_statusd.sh 2>/dev/null | head -40

echo
echo "--- S38wifi ---"
grep -nE "while|sleep|for " /etc/init.d/S38wifi 2>/dev/null | head -20

echo
echo "--- rockbase_miners / heartbeat / superv 脚本 ---"
for f in /usr/bin/rockbase_miners.sh /usr/bin/rockbase_heartbeat.sh /usr/bin/rockbase_supervisord.sh /usr/bin/rockbase_ledd.sh; do
  [ -f "$f" ] && { echo "### $f"; grep -nE "while|sleep|for |fork" "$f" | head -25; }
done

echo
echo "=== 方法4: 找出 fork 计数最高的父进程 (通过 /proc/*/task 无法直接看, 用 pid 消耗速率推算) ==="
for n in 1 2 3; do
  echo "t$n: $(awk '/^processes/{print $2}' /proc/stat)  maxpid=$(ls -d /proc/[0-9]* 2>/dev/null | sed 's#/proc/##' | sort -n | tail -1)"
  sleep 3
done

echo
echo "=== 方法5: 采样 5 次"谁在运行中的进程" ==="
for n in 1 2 3 4 5; do
  echo "--- sample $n ---"
  ps -eo pid,ppid,stat,comm 2>/dev/null | grep -vE "^\s*PID|sshd|watch|^ +[0-9]+ +2 " | head -30
  sleep 1
done

rm -f "$prev"
