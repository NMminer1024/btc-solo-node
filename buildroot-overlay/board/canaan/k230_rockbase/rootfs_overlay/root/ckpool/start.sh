#!/bin/sh
#
# ckpool 手动启动脚本
# 用法: /root/ckpool/start.sh
#

CONF=/root/config/ckpool.conf
LOGDIR=/root/config/ckpool/logs

if ps | grep -v grep | grep -q ckpool; then
    echo "ckpool already running, showing log..."
    # 找最新的日志文件
    LOGFILE=$(ls -t "$LOGDIR"/ckpool*.log 2>/dev/null | head -1)
    [ -z "$LOGFILE" ] && LOGFILE="$LOGDIR/ckpool.log"
else
    mkdir -p "$LOGDIR"
    LOGFILE="$LOGDIR/ckpool.log"
    echo "Starting ckpool (solo mode)..."
    # MALLOC_ARENA_MAX: keep in sync with /etc/init.d/S62ckpool
    MALLOC_ARENA_MAX=2 ckpool -B -c "$CONF" > "$LOGFILE" 2>&1 &
    sleep 2
    if ps | grep -v grep | grep -q ckpool; then
        echo "ckpool started OK"
    else
        echo "ckpool FAILED to start, check $LOGFILE"
        exit 1
    fi
fi

echo "--- tail -f $LOGFILE (Ctrl+C to exit) ---"
tail -f "$LOGFILE"
