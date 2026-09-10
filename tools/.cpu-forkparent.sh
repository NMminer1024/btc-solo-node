#!/bin/sh
# 用 /proc/<pid>/stat 的 cminflt/cutime/cstime 定位 fork 源头
# cminflt = 已回收子进程的累计次缺页数；cutime/cstime = 已回收子进程累计 CPU ticks
# 用法: ssh root@IP 'sh -s -- 15' < tools/.cpu-forkparent.sh

W="${1:-15}"
A=/tmp/fp_a.$$
B=/tmp/fp_b.$$

snap() {
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
      # a[9]=cminflt  a[14]=cutime  a[15]=cstime
      printf "%s %s %s %s %s\n", pid, a[9], a[14], a[15], comm
    }' "$p"
  done
}

echo "=== fork 父进程定位 (窗口 ${W}s) ==="
P1=$(awk '/^processes/{print $2}' /proc/stat)
snap > "$A"
sleep "$W"
P2=$(awk '/^processes/{print $2}' /proc/stat)
snap > "$B"

echo "fork 总数: $((P2-P1)) 次 / ${W}s  => $(( (P2-P1)/W )) 次/秒"
echo
echo "--- 按「新增子进程数」(cminflt 增量) 排序 ---"
awk -v w="$W" '
  NR==FNR { c1[$1]=$2; u1[$1]=$3; s1[$1]=$4; name[$1]=$5; seen[$1]=1; next }
  { c2[$1]=$2; u2[$1]=$3; s2[$1]=$4; name[$1]=$5; seen[$1]=1 }
  END {
    for(pid in seen){
      a=(pid in c1)?c1[pid]:0; b=(pid in c2)?c2[pid]:0
      du=((pid in u2)?u2[pid]:0)-((pid in u1)?u1[pid]:0)
      ds=((pid in s2)?s2[pid]:0)-((pid in s1)?s1[pid]:0)
      dc=b-a; dct=du+ds
      if(dc<=0 && dct<=0) continue
      printf "children_reaped=%-8s childCPU=%.2f%% (u=%.2f%% s=%.2f%%)  pid=%-6s %s\n", dc, dct*100/(w*100), du*100/(w*100), ds*100/(w*100), pid, name[pid]
    }
  }
' "$A" "$B" | sort -rn -k1

echo
echo "--- 按「子进程 CPU 增量」排序 ---"
awk -v w="$W" '
  NR==FNR { c1[$1]=$2; u1[$1]=$3; s1[$1]=$4; name[$1]=$5; seen[$1]=1; next }
  { c2[$1]=$2; u2[$1]=$3; s2[$1]=$4; name[$1]=$5; seen[$1]=1 }
  END {
    for(pid in seen){
      a=(pid in c1)?c1[pid]:0; b=(pid in c2)?c2[pid]:0
      du=((pid in u2)?u2[pid]:0)-((pid in u1)?u1[pid]:0)
      ds=((pid in s2)?s2[pid]:0)-((pid in s1)?s1[pid]:0)
      dc=b-a; dct=du+ds
      if(dct<=0) continue
      printf "childCPU=%6.2f%% (u=%.2f%% s=%.2f%%)  children=%-8s pid=%-6s %s\n", dct*100/(w*100), du*100/(w*100), ds*100/(w*100), dc, pid, name[pid]
    }
  }
' "$A" "$B" | sort -rn -k1

echo
echo "--- 当前关键参数 ---"
for v in LOOP_DELAY HEARTBEAT_INTERVAL INTERVAL MIN_SLEEP MAX_SLEEP; do :; done
grep -hE "^(LOOP_DELAY|HEARTBEAT_INTERVAL|INTERVAL|SYSTEM_INTERVAL|BITCOIN_LOG_INTERVAL|NETWORK_INTERVAL|STRATUM_INTERVAL)=" \
  /usr/bin/rockbase_ledd.sh /usr/bin/rockbase_heartbeatd.sh /usr/bin/rockbase_serial_healthd.sh \
  /usr/bin/rockbase_statusd.sh /usr/bin/rockbase_supervisord.sh 2>/dev/null

echo
echo "--- 是否有 web 客户端高频轮询 (查看 httpd 子进程累积) ---"
if [ -f /var/log/httpd/access_log ]; then tail -20 /var/log/httpd/access_log; fi
ls -la /var/log 2>/dev/null | head
echo "--- status-api.json 读取次数 (mtime) ---"
ls -la /tmp/status-api.json /tmp/STATUS* /tmp/*.json 2>/dev/null | head -20

rm -f "$A" "$B"
