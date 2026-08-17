#!/bin/sh
#
# bitcoind 优雅停止脚本
# 用法: /root/bitcoind/stop.sh
#

CONF=/root/bitcoind/bitcoin.conf
DATADIR=/root/block
LOGFILE=/root/block/debug.log

if ! pidof bitcoind > /dev/null 2>&1; then
    echo "bitcoind is not running"
    exit 0
fi

echo "Stopping bitcoind gracefully..."
bitcoin-cli -conf="$CONF" -datadir="$DATADIR" stop

# 等待 debug.log 中出现 "Shutdown: done"，最多等 120 秒
echo "Waiting for 'Shutdown: done' in $LOGFILE ..."
SECONDS=0
MAX_WAIT=120
while [ $SECONDS -lt $MAX_WAIT ]; do
    if tail -5 "$LOGFILE" 2>/dev/null | grep -q "Shutdown: done"; then
        echo "bitcoind stopped OK"
        exit 0
    fi
    if ! pidof bitcoind > /dev/null 2>&1; then
        # 进程已退出但可能没写日志（配置文件切换等情况）
        echo "bitcoind process exited (no shutdown log)"
        exit 0
    fi
    sleep 1
done

echo "bitcoind didn't stop in ${MAX_WAIT}s, force killing..."
killall -9 bitcoind 2>/dev/null
echo "bitcoind force killed"
