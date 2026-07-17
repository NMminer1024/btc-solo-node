#!/bin/sh
#
# bitcoind 手动启动脚本
# 用法: /root/bitcoind/start.sh
#

CONF=/root/bitcoind/bitcoin.conf
DATADIR=/root/block

# debug.log 可能在 datadir 或 conf 所在目录
find_log() {
    for d in "$DATADIR" "${CONF%/*}"; do
        [ -f "$d/debug.log" ] && echo "$d/debug.log" && return
    done
    echo "$DATADIR/debug.log"
}

if ps | grep -v grep | grep -q bitcoind; then
    echo "bitcoind already running, showing log..."
else
    echo "Starting bitcoind..."
    bitcoind -conf="$CONF" -datadir="$DATADIR" -daemon
    sleep 2
    if ps | grep -v grep | grep -q bitcoind; then
        echo "bitcoind started OK"
    else
        echo "bitcoind FAILED to start"
        exit 1
    fi
fi

LOGFILE=$(find_log)
echo "--- tail -f $LOGFILE (Ctrl+C to exit) ---"
tail -f "$LOGFILE"
