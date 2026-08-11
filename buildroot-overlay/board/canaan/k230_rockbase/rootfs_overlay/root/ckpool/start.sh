#!/bin/sh
#
# ckpool 手动启动脚本
# 用法: /root/ckpool/start.sh
#

CONF=/root/ckpool/ckpool.conf
LOGDIR=/root/ckpool/logs

if ps | grep -v grep | grep -q ckpool; then
    echo "ckpool already running, showing log..."
    # 找最新的日志文件
    LOGFILE=$(ls -t "$LOGDIR"/ckpool*.log 2>/dev/null | head -1)
    [ -z "$LOGFILE" ] && LOGFILE="$LOGDIR/ckpool.log"
else
    mkdir -p "$LOGDIR"
    LOGFILE="$LOGDIR/ckpool.log"
    echo "Starting ckpool (solo mode)..."
    ckpool -c "$CONF" > "$LOGFILE" 2>&1 &
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
