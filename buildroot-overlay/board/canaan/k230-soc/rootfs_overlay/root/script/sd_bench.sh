#!/bin/sh
# ============================================================
# SD 卡 / eMMC 综合测速脚本
# 用法: ./sd_bench.sh [测试目录]
#   默认在 /root/block 下测试 (若不存在则用 /tmp)
#
# 测试项目:
#   1. 顺序写入 256MB (1M 块)
#   2. 顺序读取 256MB (1M 块)
#   3. 随机 4K 写入 (200MB total, 5000 files)
#   4. 随机 4K 读取 (200MB total, 5000 files)
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

# 辅助函数: 将 dd 输出中的速度提取为 MB/s
parse_speed() {
    # dd 输出类似: 268435456 bytes (256.0 MB) copied, 3.5 s, 76.8 MB/s
    sed -n 's/.*, \([0-9.]*\) MB\/s$/\1/p'
}

# 辅助函数: 将 time 输出中的 real 时间提取为秒
parse_seconds() {
    # real  0m3.50s
    grep real | awk '{print $2}' | sed 's/0m//;s/s$//'
}

echo "===== 1/6 顺序写入: 256MB (bs=1M, conv=fsync) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
SEQ_WRITE_OUT=$(dd if=/dev/zero of=seq.bin bs=1M count=256 conv=fsync 2>&1)
echo "$SEQ_WRITE_OUT"
SEQ_WRITE_SPEED=$(echo "$SEQ_WRITE_OUT" | parse_speed)
echo "  => 顺序写入速度: ${SEQ_WRITE_SPEED:-N/A} MB/s"
echo ""

echo "===== 2/6 顺序读取: 256MB (bs=1M) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
SEQ_READ_OUT=$(dd if=seq.bin of=/dev/null bs=1M count=256 2>&1)
echo "$SEQ_READ_OUT"
SEQ_READ_SPEED=$(echo "$SEQ_READ_OUT" | parse_speed)
echo "  => 顺序读取速度: ${SEQ_READ_SPEED:-N/A} MB/s"
rm -f seq.bin
echo ""

echo "===== 3/6 4K 小文件写入: 200MB total (5000 files × 40K) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
START=$(date +%s%N)
for i in $(seq 1 5000); do
    dd if=/dev/zero of="f$i" bs=4K count=10 2>/dev/null
done
sync
END=$(date +%s%N)
ELAPSED=$(echo "scale=2; ($END - $START) / 1000000000" | bc 2>/dev/null || echo "0")
FILE_COUNT=$(ls | wc -l)
TOTAL_SIZE=$(du -sh . | awk '{print $1}')
echo "  文件数: $FILE_COUNT, 总大小: $TOTAL_SIZE, 耗时: ${ELAPSED}s"
if [ "$ELAPSED" != "0" ] && [ "$ELAPSED" != "" ]; then
    SPEED=$(echo "scale=1; 200 / $ELAPSED" | bc 2>/dev/null || echo "N/A")
    echo "  => 4K 小文件写入速度: ${SPEED} MB/s"
else
    echo "  => 4K 小文件写入速度: N/A MB/s"
fi
echo ""

echo "===== 4/6 4K 小文件读取: 200MB total (5000 files) ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
START=$(date +%s%N)
for i in $(seq 1 5000); do
    dd if="f$i" of=/dev/null bs=4K 2>/dev/null
done
END=$(date +%s%N)
ELAPSED=$(echo "scale=2; ($END - $START) / 1000000000" | bc 2>/dev/null || echo "0")
echo "  耗时: ${ELAPSED}s"
if [ "$ELAPSED" != "0" ] && [ "$ELAPSED" != "" ]; then
    SPEED=$(echo "scale=1; 200 / $ELAPSED" | bc 2>/dev/null || echo "N/A")
    echo "  => 4K 小文件读取速度: ${SPEED} MB/s"
else
    echo "  => 4K 小文件读取速度: N/A MB/s"
fi
rm -f f*
echo ""

echo "===== 5/6 单文件随机 4K 写入: 10000 次 ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
START=$(date +%s%N)
for i in $(seq 1 10000); do
    dd if=/dev/zero of=rand.bin bs=4K count=1 seek=$((i * 100 % 65536)) conv=notrunc 2>/dev/null
done
sync
END=$(date +%s%N)
ELAPSED=$(echo "scale=2; ($END - $START) / 1000000000" | bc 2>/dev/null || echo "0")
echo "  10000 次写入, 耗时: ${ELAPSED}s"
if [ "$ELAPSED" != "0" ] && [ "$ELAPSED" != "" ]; then
    IOPS=$(echo "scale=0; 10000 / $ELAPSED" | bc 2>/dev/null || echo "N/A")
    SPEED=$(echo "scale=1; 10000 * 4 / 1024 / $ELAPSED" | bc 2>/dev/null || echo "N/A")
    echo "  => 随机 4K 写入: ${IOPS} IOPS, ${SPEED} MB/s"
else
    echo "  => 随机 4K 写入: N/A"
fi
echo ""

echo "===== 6/6 单文件随机 4K 读取: 10000 次 ====="
sync && echo 3 > /proc/sys/vm/drop_caches 2>/dev/null
START=$(date +%s%N)
for i in $(seq 1 10000); do
    dd if=rand.bin of=/dev/null bs=4K skip=$((i * 100 % 65536)) count=1 2>/dev/null
done
END=$(date +%s%N)
ELAPSED=$(echo "scale=2; ($END - $START) / 1000000000" | bc 2>/dev/null || echo "0")
echo "  10000 次读取, 耗时: ${ELAPSED}s"
if [ "$ELAPSED" != "0" ] && [ "$ELAPSED" != "" ]; then
    IOPS=$(echo "scale=0; 10000 / $ELAPSED" | bc 2>/dev/null || echo "N/A")
    SPEED=$(echo "scale=1; 10000 * 4 / 1024 / $ELAPSED" | bc 2>/dev/null || echo "N/A")
    echo "  => 随机 4K 读取: ${IOPS} IOPS, ${SPEED} MB/s"
else
    echo "  => 随机 4K 读取: N/A"
fi
echo ""

# 清理
rm -f rand.bin
cd /
rm -rf "$WORK_DIR"

echo "============================================"
echo "  测试完成!"
echo "============================================"
echo ""
echo "  ╔══════════════════════════════════════════╗"
echo "  ║           Summary                       ║"
echo "  ╠══════════════════════════════════════════╣"
printf "  ║ 顺序写入 (1M):   %7s MB/s        ║\n" "${SEQ_WRITE_SPEED:-N/A}"
printf "  ║ 顺序读取 (1M):   %7s MB/s        ║\n" "${SEQ_READ_SPEED:-N/A}"
echo "  ╚══════════════════════════════════════════╝"
echo ""
