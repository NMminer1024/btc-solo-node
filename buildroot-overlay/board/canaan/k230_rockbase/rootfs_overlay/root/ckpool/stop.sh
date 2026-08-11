#!/bin/sh
#
# ckpool 优雅停止脚本
# 用法: /root/ckpool/stop.sh
#

if ! ps | grep -v grep | grep -q ckpool; then
    echo "ckpool is not running"
    exit 0
fi

echo "Stopping ckpool..."

# 先发 SIGTERM 优雅退出
killall ckpool 2>/dev/null

# 等最多 10 秒
for i in $(seq 1 10); do
    if ! ps | grep -v grep | grep -q ckpool; then
        echo "ckpool stopped OK"
        exit 0
    fi
    sleep 1
done

# 超时则强制杀
echo "ckpool didn't stop, force killing..."
killall -9 ckpool 2>/dev/null
echo "ckpool force killed"
