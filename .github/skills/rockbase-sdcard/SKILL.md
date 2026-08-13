---
name: rockbase-sdcard
description: 'Measure Rockbase board SD card sequential + 4K random IOPS read/write throughput under idle conditions. Use when: SD卡测速, sdcard, sd card, TF卡, TF卡测速, 读写速度, 4K随机IOPS, 随机读写, iops, fio, 磁盘测速, 磁盘IO, block device, mmc, mmcblk, 顺序读写, 吞吐, IO测速, storage benchmark, dd测速.'
---

# Rockbase SD 卡测速流程

## 核心原则

- **必须停服务再测**：bitcoind 的 blockchain 数据在 SD 卡 `/root/block`（p3 分区），是最大的 SD 读写源；ckpool / miners-history / statusd 也会读/写 SD 卡，会严重干扰结果
- **测前验证 CPU idle ≥ 50%**：K230 是弱 CPU，IO 测试本身也吃 CPU；否则测出的是 CPU 瓶颈，不是 SD 卡瓶颈
- **测速首选 fio**：板子镜像已带 fio（`BR2_PACKAGE_FIO=y`，1G/2G 都已启用）。顺序读写、4K 随机 IOPS 全部用 fio，`--direct=1` 绕过 page cache 测真实落盘
- **busybox dd 无速度输出**：本板 busybox 未编译 `FEATURE_DD_THIRD_STATUS_LINE`，dd 只输出 `100+0 records`，**没有 MB/s**。不要用 dd 测速；只有没装 fio 时才用 dd 循环 seek 粗估 IOPS
- **写测试必须落盘**：fio 用 `--end_fsync=1` 强制写完刷盘；dd 用 `conv=fsync`，否则只写进 page cache 测的是内存速度
- **测试走文件系统（fs），不直接测裸块设备**：测试文件落在挂载分区内（`/root/` 或 `/root/block/`），通过文件读写测速，反映应用真实可获得的 IO 性能
- **测试文件 ~100 MB**：`--size=100M`，不要改大，避免长时间磨损与等待
- **所有板子用同一套命令**：保证横向对比有效

---

## 步骤一：停止干扰服务（在板子上执行）

**顺序很重要**，必须先停 supervisord，否则它会把其他被停的服务拉起来：

```bash
# 先停 supervisor（防止它重启其他服务）
/etc/init.d/S45supervisord stop

# 停 Bitcoin 节点（最大 SD 读写源，datadir=/root/block）
/etc/init.d/S61bitcoind stop   # 等 bitcoind 干净退出，约 5-30 秒

# 停矿池（写日志到 SD 卡 p2 分区 /root/ckpool/logs）
/etc/init.d/S62ckpool stop

# 停会读/写 SD 卡的状态与缓存采集
/etc/init.d/S64miners-history stop   # 写 /root/block/rockbase-history（p3）
/etc/init.d/S63miners-cache stop     # 读 /root/ckpool/logs（p2）
/etc/init.d/S43statusd stop          # 读 /root/bitcoind/debug.log + ckpool.log

# 停 CPU 热点服务（K230 弱 CPU，dd 也吃 CPU）
/etc/init.d/S04serialhealth stop   # 17% CPU
/etc/init.d/S03lcdui stop          # 8-9% CPU

# 停网络请求类服务
/etc/init.d/S44heartbeatd stop
/etc/init.d/S60ntpd stop
```

**一键执行**（SSH 到板子后粘贴）：

```bash
for svc in S45supervisord S62ckpool S61bitcoind S04serialhealth S03lcdui \
            S44heartbeatd S60ntpd S43statusd S63miners-cache S64miners-history; do
    /etc/init.d/$svc stop 2>/dev/null; done
echo "waiting for bitcoind to exit..."; sleep 15
echo "done"
```

---

## 步骤二：确认 SD 卡识别与剩余空间（在板子上执行）

### 2.1 确认 SD 卡设备名与分区

```bash
# SD 卡块设备（SDIO WiFi 不是块设备，不会出现在这里，所以列出的 mmcblk 就是 SD 卡）
grep mmcblk /proc/partitions

# 确认 root 分区指向哪张卡（root=mmcblkXp2）
grep -o 'root=/dev/mmcblk[0-9]p[0-9]*' /proc/cmdline
```

分区布局（来自 `S59blockdata`）：
- `mmcblkXp2` = rootfs（测试默认目标）
- `mmcblkXp3` = blockdata（bitcoind 数据，挂载在 `/root/block`）

> 注意：1G 板子 mmc0=SDIO WiFi / mmc1=SD 卡；2G 板子反过来 mmc0=SD 卡 / mmc1=SDIO WiFi。**不要固定假设设备名**，以上面命令输出为准。

### 2.2 确认剩余空间（测试文件 ~100 MB）

```bash
df -h / /root/block 2>/dev/null
# 预期：/ 和 /root/block 各自剩余空间 ≥ 500 MB（写 100 MB 测试文件绰绰有余）
```

### 2.3（可选）确认 SD 卡总线模式与时序

```bash
# debugfs 默认未挂载，先挂载，否则 /sys/kernel/debug/mmc*/ios 读不到
mount | grep -q debugfs || mount -t debugfs none /sys/kernel/debug 2>/dev/null

# 查看识别到的卡速度等级、总线宽度、时钟（用于定位"卡慢"是卡本身还是总线限制）
for d in /sys/kernel/debug/mmc*/ios; do echo "== $d ="; cat "$d" 2>/dev/null; done

# 卡型号 + mmc 识别日志
cat /sys/block/mmcblk*/device/name 2>/dev/null
dmesg | grep -iE "mmc[01]: new|sdhci" | tail -20
```

判读要点：`bus width` 是 4-bit 还是 1-bit、`timing spec` 是 HS / DDR / SDR104、`clock` 是多少 MHz。若卡支持高速但运行在低速模式，说明总线/驱动限制了速度。

---

## 步骤三：测速（在板子上执行，SSH 方式）

### 标准测速命令（所有板子统一用这个，全部用 fio）

默认测 **rootfs 分区（p2）**，测试文件 `/root/sdtest.bin`：

```bash
IP=192.168.124.XX   # 替换为目标板子 IP

echo "=== 顺序写（100 MB, direct=1, end_fsync 强制落盘）==="
ssh -o StrictHostKeyChecking=no root@$IP \
  'fio --name=seqwrite --filename=/root/sdtest.bin --rw=write --bs=1M --size=100M \
       --direct=1 --iodepth=1 --end_fsync=1 --group_reporting 2>&1 | grep -iE "WRITE:|bw=|iops="'

echo ""
echo "=== 顺序读（100 MB, direct=1 绕过缓存）==="
ssh -o StrictHostKeyChecking=no root@$IP \
  'fio --name=seqread --filename=/root/sdtest.bin --rw=read --bs=1M --size=100M \
       --direct=1 --iodepth=1 --group_reporting 2>&1 | grep -iE "READ:|bw=|iops="'
```

**参数固定**：`--size=100M`（100 MB）、`--bs=1M`、`--direct=1`、`--iodepth=1`（单线程同步）、写加 `--end_fsync=1` 强制刷盘。不要随意改。

> 说明：`--direct=1` 直接绕过 page cache 读写，等价于 dd 的冷读 + fsync 写，所以**不再需要手动 drop_caches**。

### 可选：测 blockdata 分区（p3，bitcoind 数据分区）

把测试文件路径换成 `/root/block/sdtest.bin`（需 `/root/block` 仍挂载，S59blockdata 未停所以正常挂载）：

```bash
ssh -o StrictHostKeyChecking=no root@$IP \
  'fio --name=seqwrite --filename=/root/block/sdtest.bin --rw=write --bs=1M --size=100M \
       --direct=1 --iodepth=1 --end_fsync=1 --group_reporting 2>&1 | grep -iE "WRITE:|bw=|iops="'
ssh -o StrictHostKeyChecking=no root@$IP \
  'fio --name=seqread --filename=/root/block/sdtest.bin --rw=read --bs=1M --size=100M \
       --direct=1 --iodepth=1 --group_reporting 2>&1 | grep -iE "READ:|bw=|iops="'
```

### 4K 随机 IOPS 测试

随机 IOPS 是 SD 卡「小文件 / 数据库随机访问」能力的核心指标，bitcoind 的 UTXO 数据库（LevelDB）就是大量随机 4K 读。

#### 方案 A（首选）：fio

板子镜像已带 fio（`BR2_PACKAGE_FIO=y`，1G/2G 都已启用）。用下面命令（`--direct=1` 绕过 page cache，`iodepth=1` 模拟单线程同步随机访问）：

```bash
IP=192.168.124.XX   # 替换为目标板子 IP
TF=/root/sdtest.bin

# 随机 4K 写（100M 文件，end_fsync 刷盘）
ssh -o StrictHostKeyChecking=no root@$IP \
  "fio --name=randwrite --filename=$TF --rw=randwrite --bs=4k --size=100M \
       --direct=1 --iodepth=1 --end_fsync=1 --group_reporting 2>&1 | grep -iE 'write:|iops='"

# 随机 4K 读
ssh -o StrictHostKeyChecking=no root@$IP \
  "fio --name=randread --filename=$TF --rw=randread --bs=4k --size=100M \
       --direct=1 --iodepth=1 --group_reporting 2>&1 | grep -iE 'read:|iops='"
```

结果取 `write: IOPS=...` / `read: IOPS=...` 的值。

> ⚠️ 抓 IOPS 必须用 `grep -iE`（不区分大小写）：fio 的 IOPS 在**小写**行 `write: IOPS=...`，带宽在**大写**行 `WRITE: bw=...`，两个都要看。

#### 方案 B（无 fio 时）：busybox dd 循环 seek 粗估

**仅在没有 fio 时使用**。每次 dd 都要 fork 一个进程，进程开销会拉低 IOPS，且随机读会命中 page cache，所以这是**粗估**，只用于横向相对比较：

```bash
TF=/root/sdtest.bin
dd if=/dev/zero of=$TF bs=1M count=100 conv=fsync 2>/dev/null   # 预分配 100MB

N=500
echo "=== rand 4K write (busybox 粗估) ==="
start=$(date +%s); i=0
while [ $i -lt $N ]; do
  off=$((RANDOM % 25600))
  dd if=/dev/zero of=$TF bs=4k count=1 seek=$off conv=notrunc 2>/dev/null
  i=$((i+1))
done
end=$(date +%s); el=$((end-start)); [ $el -lt 1 ] && el=1
echo "rand 4K write IOPS ~= $((N/el))"

echo "=== rand 4K read (busybox 粗估, 受 page cache 影响) ==="
start=$(date +%s); i=0
while [ $i -lt $N ]; do
  off=$((RANDOM % 25600))
  dd if=$TF of=/dev/null bs=4k count=1 skip=$off 2>/dev/null
  i=$((i+1))
done
end=$(date +%s); el=$((end-start)); [ $el -lt 1 ] && el=1
echo "rand 4K read IOPS ~= $((N/el))"
```

> 说明：`off=$((RANDOM % 25600))` —— 100MB / 4KB = 25600 个块。busybox `dd` 的 `seek`/`skip` 以块为单位（`bs=4k`），`conv=notrunc` 保证只覆盖目标块、不截断文件。

---

## 步骤四：结果判读

### 读法

fio 输出形如：

```
  WRITE: bw=18.7MiB/s (19.6MB/s), ... io=100MiB (105MB), run=5359-5359msec
  write: IOPS=1545, BW=6182KiB/s (6330kB/s)(100MiB/16564msec)
```

- 顺序带宽取**大写** `WRITE:` / `READ:` 行的 `bw=` 值（MB/s）
- IOPS 取**小写** `write:` / `read:` 行的 `IOPS=` 值（注意大小写在不同行）
- 写/读各 3 次取平均

### 通用参考（SD 卡速度等级，非板子实测基线）

| 卡等级 | 最低顺序写 | 说明 |
|--------|-----------|------|
| Class 4 | ≥ 4 MB/s | 老卡 |
| Class 6 | ≥ 6 MB/s | 老卡 |
| Class 10 / U1 | ≥ 10 MB/s | 常见 |
| U3 / V30 | ≥ 30 MB/s | 视频/高速卡 |
| V60 / V90 | ≥ 60 / 90 MB/s | 高端卡 |

### 判断依据

| 结果 | 解读 |
|------|------|
| 写 ~10 MB/s 或更低 | Class 10 卡在 HS 总线下的正常水平 |
| 写 > 30 MB/s | 高速卡 + DDR/SDR104 总线 |
| 写远低于卡标称（如标 U3 却只有 5 MB/s） | 总线降速：查 `ios` 里的 bus width/clock，怀疑 1-bit 或低速 timing |
| 读速度明显高于写（常见） | 正常，SD 卡写慢读快 |
| 停服务前后结果差异明显 | 有后台服务在抢 SD IO（bitcoind 最可疑） |
| 停服务前后结果无差异 | SD 卡/总线是瓶颈，与 CPU/服务无关 |

### 4K 随机 IOPS 判读

| 指标 | 典型范围 | 说明 |
|------|---------|------|
| SD 卡 4K 随机读 | 数百 ~ 数千 IOPS | 消费级 SD 卡普遍较低 |
| SD 卡 4K 随机写 | 数十 ~ 数百 IOPS | 写远慢于读 |

- bitcoind 的 UTXO 数据库（LevelDB）是大量随机读，**随机读 IOPS 比顺序速度更能反映节点同步/启动快慢**
- 若随机读 IOPS 极低（<100），即使顺序速度尚可，节点数据库访问也会成为瓶颈
- fio（`--direct=1`）测的是真实落盘 IOPS；busybox dd 循环测的是 buffered 粗估，会偏高且受 fork 开销影响

> **基线实测**（2026-08-13，`192.168.124.23` 2G 板子，卡 SD64G SDXC，HS 模式 4-bit 50MHz，CPU idle 81%）：

| 板子 | 分区 | 顺序写 | 顺序读 | 4K 随机读 IOPS | 4K 随机写 IOPS | 卡型号/等级 |
|------|------|--------|--------|---------------|---------------|------------|
| 124.23 (2G) | p2 rootfs | 19.6 MB/s | 21.5 MB/s | 2065 | 1545 | SD64G (SDXC) |

其它板子/分区（1G、p3 blockdata）待实测，跑完后回填上表。

> 关键结论：顺序读写 ~20 MB/s 已顶到 HS 模式（4-bit × 50MHz ≈ 25MB/s）天花板，板子未启用 UHS-I（1.8V），这是总线/驱动限制而非卡慢。4K 随机读 2065 IOPS 对 SD 卡偏优，能支撑 bitcoind UTXO 库（LevelDB）随机读。

---

## 步骤五：测速完毕后处理

测速是破坏性操作（停了 bitcoind），测完后**重启板子**是最简单的恢复方式：

```bash
ssh root@<IP> 'rm -f /root/sdtest.bin /root/block/sdtest.bin; reboot'
```

如果不想重启，先删测试文件，再按反序手动重新启动服务：

```bash
ssh root@<IP> 'rm -f /root/sdtest.bin /root/block/sdtest.bin'

/etc/init.d/S61bitcoind start
/etc/init.d/S62ckpool start
/etc/init.d/S45supervisord start
/etc/init.d/S03lcdui start
/etc/init.d/S04serialhealth start
/etc/init.d/S60ntpd start
/etc/init.d/S44heartbeatd start
/etc/init.d/S63miners-cache start
/etc/init.d/S64miners-history start
/etc/init.d/S43statusd start
```

---

## 附：快速单板一键测速脚本

把以下内容在 WSL 中整段执行，指定 IP 后自动完成"停服务 → 等待 → 验 CPU → 写×3 → 读×3 → 清理提示"：

```bash
IP=192.168.124.XX

echo ">>> Stopping services on $IP..."
ssh -o StrictHostKeyChecking=no root@$IP '
for svc in S45supervisord S62ckpool S61bitcoind S04serialhealth S03lcdui \
            S44heartbeatd S60ntpd S43statusd S63miners-cache S64miners-history; do
    /etc/init.d/$svc stop 2>/dev/null
done'

echo ">>> Waiting 20s for bitcoind to exit..."
sleep 20

echo ">>> CPU idle check:"
ssh -o StrictHostKeyChecking=no root@$IP 'top -b -n2 -d2 | grep "^CPU" | tail -1'

echo ">>> SD card device / free space:"
ssh -o StrictHostKeyChecking=no root@$IP 'grep mmcblk /proc/partitions; df -h /'

echo ">>> Seq write (fio, 100M, direct=1, end_fsync):"
ssh -o StrictHostKeyChecking=no root@$IP \
  'fio --name=seqwrite --filename=/root/sdtest.bin --rw=write --bs=1M --size=100M \
       --direct=1 --iodepth=1 --end_fsync=1 --group_reporting 2>&1 | grep -iE "WRITE:|bw=|iops="'

echo ">>> Seq read (fio, 100M, direct=1):"
ssh -o StrictHostKeyChecking=no root@$IP \
  'fio --name=seqread --filename=/root/sdtest.bin --rw=read --bs=1M --size=100M \
       --direct=1 --iodepth=1 --group_reporting 2>&1 | grep -iE "READ:|bw=|iops="'

echo ">>> 4K rand write IOPS (fio, direct=1):"
ssh -o StrictHostKeyChecking=no root@$IP \
  'fio --name=randwrite --filename=/root/sdtest.bin --rw=randwrite --bs=4k --size=100M \
       --direct=1 --iodepth=1 --end_fsync=1 --group_reporting 2>&1 | grep -iE "write:|iops="'

echo ">>> 4K rand read IOPS (fio, direct=1):"
ssh -o StrictHostKeyChecking=no root@$IP \
  'fio --name=randread --filename=/root/sdtest.bin --rw=randread --bs=4k --size=100M \
       --direct=1 --iodepth=1 --group_reporting 2>&1 | grep -iE "read:|iops="'

echo ">>> Done. Clean up & restore:"
echo "    ssh root@$IP 'rm -f /root/sdtest.bin; reboot'"
```

---

## 注意事项

- 测试文件必须落在真实 SD 卡分区（`/root/` 或 `/root/block/`），**不要放 `/tmp`**（那是 tmpfs 内存盘，测的是内存不是卡）
- 测速统一用 fio `--direct=1`（绕过 page cache），写加 `--end_fsync=1` 强制刷盘；这样无需手动 drop_caches
- busybox dd 无速度输出（未编译 `FEATURE_DD_THIRD_STATUS_LINE`），只显示 `100+0 records`，**不要用它测速度**
- fio 的 IOPS 在**小写**行 `write: IOPS=...`，带宽在**大写**行 `WRITE: bw=...`，抓取用 `grep -iE`
- debugfs 默认未挂载，读 `/sys/kernel/debug/mmc*/ios` 前先 `mount -t debugfs none /sys/kernel/debug`
- bitcoind stop 可能需要 10-60 秒，等待足够时间再测
- 测完务必删掉测试文件（`/root/sdtest.bin`、`/root/block/sdtest.bin`），避免占用 SD 空间
- blockdata 分区（p3）是 bitcoind 数据分区，测它时不要动里面已有的文件，只新建/删除自己的测试文件
