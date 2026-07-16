#!/bin/sh
# ============================================================
# SD 卡 / eMMC 综合测速脚本 (兼容 BusyBox)
# 用法: ./sd_bench.sh [测试目录]
#   默认在 /root/block 下测试 (若不存在则用 /tmp)
#
# 测试项目:
#   1. 顺序写入 256MB (1M 块)
#   2. 顺序读取 256MB (1M 块)
#   3. 4K 小文件写入 (200MB total, 5000 files)
#   4. 4K 小文件读取 (200MB total, 5000 files)
#   5. 单文件随机 4K 写入 (10000 次)
#   6. 单文件随机 4K 读取 (10000 次)
# ============================================================

TEST_DIR="${1:-/root/block}"
if [ ! -d "$TEST_DIR" ]; then
    TEST_DIR="/tmp"
    echo "[INFO] /root/block 不存在，使用 /tmp"
fi

WORK_DIR="$TEST_DIR/sd_bench_$$"
mkdir -p "$WORK_DIR" || { echo "[FAIL] 无法创建 $WORK_DIR"; exit 1; }
cd "$WORK_DIR" || exit 1

echo "============================================"
echo "  SD Card / eMMC Speed Benchmark"
echo "  测试目录: $TEST_DIR"
echo "  时间: $(date '+%Y-%m-%d %H:%M:%S')"
echo "============================================"
echo ""

# ---- 计时: 读取 /proc/uptime (精确到 0.01s) ----
now() {
    awk '{print $1}' /proc/uptime
}

# ---- 计算速度: $1=MB数, $2=开始秒, $3=结束秒 ----
calc_mbps() {
    _mb="$1"
    _t1="$2"
    _t2="$3"
    _elapsed=$(echo "$_t2 - $_t1" | bc 2>/dev/null)
    if [ -n "$_elapsed" ] && [ "$_elapsed" != "0" ]; then
        echo "scale=1; $_mb / $_elapsed" | bc 2>/dev/null
    else
        echo "N/A"
    fi
}

# ---- 计算 IOPS: $1=次数, $2=开始秒, $3=结束秒 ----
calc_iops() {
    _n="$1"
    _t1="$2"
    _t2="$3"
    _elapsed=$(echo "$_t2 - $_t1" | bc 2>/dev/null)
    if [ -n "$_elapsed" ] && [ "$_elapsed" != "0" ]; then
        echo "scale=0; $_n / $_elapsed" | bc 2>/dev/null
    else
        echo "N/A"
    fi
}

# ---- 测试 1: 顺序写入 256MB ----
echo "===== 1/6 顺序写入: 256MB (bs=1M, conv=fsync) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
T1=$(now)
dd if=/dev/zero of=seq.bin bs=1M count=256 conv=fsync 2>/dev/null
T2=$(now)
SEQ_WRITE_SPEED=$(calc_mbps 256 "$T1" "$T2")
echo "  => 顺序写入速度: ${SEQ_WRITE_SPEED} MB/s"
echo ""

# ---- 测试 2: 顺序读取 256MB ----
echo "===== 2/6 顺序读取: 256MB (bs=1M) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
T1=$(now)
dd if=seq.bin of=/dev/null bs=1M count=256 2>/dev/null
T2=$(now)
SEQ_READ_SPEED=$(calc_mbps 256 "$T1" "$T2")
echo "  => 顺序读取速度: ${SEQ_READ_SPEED} MB/s"
rm -f seq.bin
echo ""

# ---- 测试 3: 4K 小文件写入 ----
echo "===== 3/6 4K 小文件写入: 200MB total (5000 files x 40K) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
T1=$(now)
for i in $(seq 1 5000); do
    dd if=/dev/zero of="f$i" bs=4K count=10 2>/dev/null
done
sync
T2=$(now)
FILE_COUNT=$(ls | wc -l)
TOTAL_SIZE=$(du -sh . | awk '{print $1}')
SMALL_WRITE_SPEED=$(calc_mbps 200 "$T1" "$T2")
echo "  文件数: $FILE_COUNT, 总大小: $TOTAL_SIZE"
echo "  => 4K 小文件写入速度: ${SMALL_WRITE_SPEED} MB/s"
echo ""

# ---- 测试 4: 4K 小文件读取 ----
echo "===== 4/6 4K 小文件读取: 200MB total (5000 files) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
T1=$(now)
for i in $(seq 1 5000); do
    dd if="f$i" of=/dev/null bs=4K 2>/dev/null
done
T2=$(now)
SMALL_READ_SPEED=$(calc_mbps 200 "$T1" "$T2")
echo "  => 4K 小文件读取速度: ${SMALL_READ_SPEED} MB/s"
rm -f f*
echo ""

# ---- 测试 5: 单文件随机 4K 写入 10000 次 ----
echo "===== 5/6 随机 4K 写入: 2000 次 (单文件) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
T1=$(now)
for i in $(seq 1 2000); do
    dd if=/dev/zero of=rand.bin bs=4K count=1 seek=$((i * 100 % 65536)) conv=notrunc 2>/dev/null
done
sync
T2=$(now)
RND_WRITE_IOPS=$(calc_iops 2000 "$T1" "$T2")
if [ "$RND_WRITE_IOPS" != "N/A" ]; then
    RND_WRITE_SPEED=$(echo "scale=1; $RND_WRITE_IOPS * 4 / 1024" | bc 2>/dev/null)
else
    RND_WRITE_SPEED="N/A"
fi
echo "  => 随机 4K 写入: ${RND_WRITE_IOPS} IOPS, ${RND_WRITE_SPEED} MB/s"
echo ""

# ---- 测试 6: 单文件随机 4K 读取 2000 次 ----
echo "===== 6/6 随机 4K 读取: 2000 次 (单文件) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
T1=$(now)
for i in $(seq 1 2000); do
    dd if=rand.bin of=/dev/null bs=4K skip=$((i * 100 % 65536)) count=1 2>/dev/null
done
T2=$(now)
RND_READ_IOPS=$(calc_iops 2000 "$T1" "$T2")
if [ "$RND_READ_IOPS" != "N/A" ]; then
    RND_READ_SPEED=$(echo "scale=1; $RND_READ_IOPS * 4 / 1024" | bc 2>/dev/null)
else
    RND_READ_SPEED="N/A"
fi
echo "  => 随机 4K 读取: ${RND_READ_IOPS} IOPS, ${RND_READ_SPEED} MB/s"
echo ""

# ---- 清理 ----
rm -f rand.bin
cd /
rm -rf "$WORK_DIR"

echo "============================================"
echo "  测试完成!"
echo "============================================"
echo ""
echo "  ------- Summary -------"
echo "  顺序写入 (1M)  : ${SEQ_WRITE_SPEED} MB/s"
echo "  顺序读取 (1M)  : ${SEQ_READ_SPEED} MB/s"
echo "  小文件写入 (4K): ${SMALL_WRITE_SPEED} MB/s"
echo "  小文件读取 (4K): ${SMALL_READ_SPEED} MB/s"
echo "  随机写入 (4K)  : ${RND_WRITE_IOPS} IOPS (${RND_WRITE_SPEED} MB/s)"
echo "  随机读取 (4K)  : ${RND_READ_IOPS} IOPS (${RND_READ_SPEED} MB/s)"
echo ""
