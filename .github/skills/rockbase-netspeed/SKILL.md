---
name: rockbase-netspeed
description: 'Measure Rockbase board network throughput under idle conditions. Use when: 测速, 网络测速, netspeed, speed test, throughput, 吞吐, SSH吞吐, WiFi速度, ETH速度, 带宽, bandwidth, 网速.'
---

# Rockbase 网络测速流程

## 核心原则

- **必须停服务再测**：bitcoind + ckpool 占用大量 CPU/IO/网络，会严重干扰结果
- **测前验证 CPU idle ≥ 50%**：否则测出的是 CPU 瓶颈，不是网络瓶颈
- **所有板子用同一套命令**：保证横向对比有效
- **测速工具**：统一用 iperf3（无加密 TCP，隔离 SSH 加密在单核 RISC-V 上的额外开销）
- **测速方向**：双向都测 —— TX（板子→本地，上传）+ RX（本地→板子，下载，加 `-R`）
- **测速轮次**：每个方向测 3 轮，取中位数

---

## 步骤一：停止干扰服务（在板子上执行）

**顺序很重要**，必须先停 supervisord，否则它会把其他被停的服务拉起来：

```bash
# 先停 supervisor（防止它重启其他服务）
/etc/init.d/S45supervisord stop

# 停矿池和 Bitcoin 节点（最大 CPU/IO 来源）
/etc/init.d/S62ckpool stop
/etc/init.d/S61bitcoind stop   # 等 bitcoind 干净退出，约 5-30 秒

# 停 CPU 热点服务
/etc/init.d/S04serialhealth stop   # 17% CPU 的 shell 轮询
/etc/init.d/S03lcdui stop          # 8-9% CPU 的 LCD 渲染

# 停网络请求类服务
/etc/init.d/S44heartbeatd stop
/etc/init.d/S60ntpd stop

# 停状态/缓存采集
/etc/init.d/S43statusd stop
/etc/init.d/S63miners-cache stop
/etc/init.d/S64miners-history stop
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

## 步骤二：验证 CPU 空闲（在板子上执行）

停服务后必须确认 CPU idle ≥ 50% 再测，否则结果无效：

```bash
top -b -n2 -d2 | grep "^CPU" | tail -1
# 预期：CPU: X% usr  Y% sys   0% nic  ≥50% idle ...
```

如果 idle < 50%，检查哪个进程还在占 CPU：

```bash
top -b -n1 | head -15
```

---

## 步骤三：测速（iperf3，双向各 3 轮）

### 前置：本地 WSL 启动 iperf3 server

iperf3 已打进镜像，板子上自带 `iperf3` 命令。测速前先在本地 WSL 启动 iperf3 server：

```bash
# 本地 WSL 若无 iperf3，用 apt 下载免 sudo 解压（无需 root）：
cd /tmp
apt-get download iperf3 libiperf0 libsctp1 2>/dev/null
rm -rf iperf3-local && mkdir -p iperf3-local
for d in iperf3_*.deb libiperf0_*.deb libsctp1_*.deb; do dpkg -x "$d" iperf3-local/; done
LD_LIBRARY_PATH=/tmp/iperf3-local/usr/lib/x86_64-linux-gnu /tmp/iperf3-local/usr/bin/iperf3 -s
```

server 默认监听 **5201** 端口，保持前台运行。

### 标准测速命令（双向各 3 轮，所有板子统一用这个）

```bash
IP=192.168.124.XX      # 目标板子 IP（SDIO WiFi 用 wlan0，ETH 用 eth0）
SERVER=192.168.124.20  # WSL 本机 IP（iperf3 server 所在）

echo "########## TX 上传（板子 → 本地）##########"
for run in 1 2 3; do
  echo "===== TX Run $run ====="
  ssh -o StrictHostKeyChecking=no root@$IP "iperf3 -c $SERVER -t 15 2>&1 | grep -E 'sender|receiver' | tail -2"
done

echo "########## RX 下载（本地 → 板子）##########"
for run in 1 2 3; do
  echo "===== RX Run $run ====="
  ssh -o StrictHostKeyChecking=no root@$IP "iperf3 -c $SERVER -t 15 -R 2>&1 | grep -E 'sender|receiver' | tail -2"
done
```

**参数固定**：`-t 15`（每轮 15 秒），不要随意改，保证跨板对比一致。
**取值**：看汇总行 `sender` 的 `Mbits/sec`；TX 取 `sender`，RX 加 `-R` 后 `sender` 表示本地发送方向，同样取 `sender` 行。

### 双板同测（SDIO WiFi vs USB ETH 对比）

```bash
IP_SDIO=192.168.124.21   # SDIO WiFi 板子
IP_ETH=192.168.124.30    # USB ETH 板子
SERVER=192.168.124.20

for ip in $IP_SDIO $IP_ETH; do
  echo "========== $ip TX 上传 =========="
  for run in 1 2 3; do
    echo "--- TX Run $run ---"
    ssh -o StrictHostKeyChecking=no root@$ip "iperf3 -c $SERVER -t 15 2>&1 | grep -E 'sender|receiver' | tail -2"
  done
  echo "========== $ip RX 下载 =========="
  for run in 1 2 3; do
    echo "--- RX Run $run ---"
    ssh -o StrictHostKeyChecking=no root@$ip "iperf3 -c $SERVER -t 15 -R 2>&1 | grep -E 'sender|receiver' | tail -2"
  done
done
```

---

## 步骤四：结果判读

### 已验证基线（2026-08-13 实测）

#### 服务运行中（bitcoind + lcdui + serialhealth 等全部在线）

| 接口 | CPU idle | SSH 吞吐 | 说明 |
|------|---------|---------|------|
| wlan0 (SDIO WiFi) | **0%** | 1.0 MB/s | CPU 全满，WiFi 驱动 mmc0 中断 642k 次，sys% 75% |
| eth0 (USB ETH) | 54% | 11.3 MB/s | 无 SDIO WiFi 驱动开销 |

#### 服务停止后（本 skill 标准流程，CPU idle ≥ 95%）

| 接口 | CPU idle | Run 1 | Run 2 | Run 3 | 平均 |
|------|---------|-------|-------|-------|------|
| wlan0 (SDIO WiFi) | 95% | 1.2 MB/s | 1.0 MB/s | 0.9 MB/s | **~1.0 MB/s** |
| eth0 (USB ETH 百兆转换器) | 97% | 12.0 MB/s | 12.1 MB/s | 12.1 MB/s | **~12.1 MB/s** |

> 注：此 12.1 MB/s 基线用的是**百兆** USB-ETH 转换器（100 Mbps），12.1 MB/s × 8 ≈ 96 Mbps，已顶满百兆口。换千兆 RTL8153 后见下方「千兆口 + RTL8153」章节（~20.4 MB/s）。

**停服务后 SDIO WiFi 速度没有变化** —— 与服务运行中（1.0 MB/s）完全一致。

#### 根因结论（已验证）

SDIO WiFi 的 1 MB/s 瓶颈**不是** CPU、不是 SSH 加密、也不是我们的诊断补丁，而是 **RTL8189FS SDIO 驱动 TX 路径的硬件/驱动固有上限**：

- WiFi PHY 链路速率：72.2 Mb/s（信号 -56 dBm，质量 54/70，理论 ~9 MB/s）
- `/proc/net/dev` wlan0 TX 计数器实测：**1.4–1.6 MB/s**（接口层即如此，与 SSH 无关）
- chacha20-poly1305 cipher 替换 aes-128-ctr 后同样 ~955 kB/s（排除加密开销）
- 推断原因：RTL8189FS 驱动未实现有效的 A-MPDU 帧聚合，每帧都经历独立 SDIO CMD53 事务，SDIO 总线往返延迟累积导致吞吐受限

对比结论：USB ETH（**百兆**转换器）在相同 CPU 空闲状态下稳定 12 MB/s，为 SDIO WiFi 的 **12 倍**。此 12 MB/s 受**百兆网卡**（100 Mbps）限速，非 USB 2.0 上限。

### USB ETH 千兆口 + RTL8153 上限（2026-08-14 实测）

**测试条件**：交换机已换千兆口，网卡为 Realtek **RTL8153**（USB 3.0 千兆芯片），板子 USB 控制器为 `dwc2_hsotg`（High-Speed，仅 USB 2.0）。

**链路协商**：`eth0 = 1000/full`（网卡芯片支持千兆），但 USB 设备实际运行在 `speed=480`（USB 2.0 High-Speed）。

**实测吞吐**（停服务后，CPU idle 88%）：

| Run | 吞吐 |
|-----|------|
| 1 | 19.6 MB/s |
| 2 | 20.5 MB/s |
| 3 | 21.0 MB/s |
| 平均 | **~20.4 MB/s** |

**结论：已触碰 USB 2.0 物理天花板。**

- USB 2.0 High-Speed 理论 480 Mbps（60 MB/s），bulk transfer 有效 ~80%，纯 USB 层上限 ~48 MB/s
- 叠加 USB↔以太网 + TCP/IP + SSH 加密开销后，现实天花板 ~20–25 MB/s
- 瓶颈**不在**交换机（已千兆）、**不在**网卡芯片（RTL8153 支持千兆），而在板子 USB 控制器只有 USB 2.0
- K230 无 USB 3.0 主控，USB 有线无法突破 ~20 MB/s；SDIO WiFi 更慢（~1 MB/s）

**验证是否 USB 2.0 限速的命令**：

```bash
ssh root@<IP> '
cat /sys/class/net/eth0/speed          # 期望 1000
cat /sys/class/net/eth0/duplex         # 期望 full
lsusb | grep -i lan                    # 期望 RTL8153 (0bda:8153)
for d in /sys/bus/usb/devices/[0-9]*/; do
  [ -f "$d/product" ] && echo "$(basename $d): $(cat $d/product) [$(cat $d/speed)]"
done                                   # 期望网卡 speed=480 → USB 2.0
'
```

判读：若网卡 `speed=480` 而 `eth0 speed=1000`，说明瓶颈是 USB 2.0 物理限速，非网络/网卡问题。

### 判断依据

| 结果 | 解读 |
|------|------|
| SDIO 停服务后明显提升（>3 MB/s） | CPU 是主要瓶颈 |
| SDIO 停服务后无变化（仍 ~1 MB/s） | ✅ 已确认：驱动/硬件是瓶颈，与 CPU 无关 |
| ETH 停服务后明显提升（>20 MB/s） | 背景服务在抢网络带宽 |
| ETH 停服务后无明显变化（~12 MB/s） | ✅ 已确认：百兆 USB ETH 转换器（100 Mbps）限速 |
| ETH 千兆口 + RTL8153（~20 MB/s） | ✅ 已确认：USB 2.0 控制器物理上限（见上方千兆口章节） |

### UWB-WiFi RTL8821CU 2.4G 基线（2026-08-17 实测）

**测试设备**：`192.168.124.35`（UWB-WiFi 模组，芯片 **RTL8821CU**，`0bda:c820`，走 USB，内核模块 `rtw88_8821cu`）

**接口**：`wlan0`（USB WiFi），2.4G 频段（freq=2472 MHz，SSID `NMTech-2.4G`）

**测试条件**：服务已停，CPU idle 85%

| Run | 传输量 | 耗时 | 吞吐 |
|-----|--------|------|------|
| 1 | 64 MiB | 77.7s | 864 kB/s |
| 2 | 64 MiB | 113.2s | 593 kB/s |
| 3 | 42 MiB ⚠️ | 48.9s | 906 kB/s |

**平均 ≈ 0.79 MB/s**

**结论**：
- RTL8821CU 走 USB 2.0（`dwc2_hsotg`），2.4G 模式下 SSH 吞吐 ~0.8 MB/s，与 SDIO 8189FS（~1.0 MB/s）同量级
- 波动明显（593~906 kB/s），ping 抖动 12~127 ms，说明 2.4G 信号偏弱/干扰大

### UWB-WiFi RTL8821CU 5G 基线（2026-08-17 实测）

**测试条件**：SSID `RockBase_5G`（freq=5785 MHz，信号 -38 dBm），服务已停，CPU idle 87%

| Run | 传输量 | 耗时 | 吞吐 |
|-----|--------|------|------|
| 1 | 64 MiB | 22.7s | 3.0 MB/s |
| 2 | 64 MiB | 20.9s | 3.2 MB/s |
| 3 | 64 MiB | 22.0s | 3.0 MB/s |

**平均 ≈ 3.1 MB/s**

**结论**：RTL8821CU 从 2.4G（~0.8 MB/s）切到 5G（~3.1 MB/s），吞吐提升约 **4 倍**；信号从 -56 提升到 -38 dBm，且 5G 频段干扰少、波动稳定（3.0~3.2 MB/s）。仍低于 USB 有线（百兆 ~12 MB/s / 千兆 RTL8153 ~20 MB/s），受 USB 2.0 + 802.11ac 单流实际开销限制。

### UWB-WiFi RTL8821CU 5G iperf3 实测（2026-08-17）

**方法**：板子部署 iperf3 3.18（RISC-V，复用 `k230_canmv_01studio_defconfig` 已编译产物 + `libiperf.so.0`），本地 WSL 用 `apt download iperf3` 解压跑 server。PHY 协商 433.3 Mb/s（5G 80MHz 单流），信号 -40 dBm，USB speed=480。

| 方向 | 吞吐 | Retr | 说明 |
|------|------|------|------|
| **TX（板子→本地，上传）** | 19~20 Mbits/sec ≈ **2.4 MB/s** | 116 次 | 大量重传 |
| **RX（本地→板子，下载）** | 53~56 Mbits/sec ≈ **6.7 MB/s** | 1 次 | 几乎零重传 |

**关键结论**：

1. **RX ≈ 2.8 × TX**，方向严重不对称，瓶颈在 **rtw88 驱动的 USB TX（上行）路径**，与 SDIO 8189FS 的 TX 瓶颈同源（每帧独立 USB bulk OUT 事务，无有效聚合）。
2. **无加密 iperf3 时 CPU idle 60~90%（未打满）** —— 说明之前 SSH dd 测速时 CPU 打满（usr+sys≈100%）是 **SSH 加密在单核 RISC-V 上极慢**导致的，加密是 SSH 测速的额外瓶颈，不是链路瓶颈。
3. 真实 TCP 上限：TX ~2.4 MB/s，RX ~6.7 MB/s。SSH dd 测的 3.1 MB/s（TX）略高于 iperf3 TX，因 `/dev/zero` 全零数据在 SSH 压缩后吞吐虚高。

**结论修正（重要）**：之前"单核 CPU 是瓶颈"的判断不准确。正确结论是 —— **SSH 加密拖累 CPU，但即便去掉加密，RTL8821CU 走 USB 2.0 的 TX 上限也只有 ~2.4 MB/s，RX 上限 ~6.7 MB/s**。二者都是 rtw88 USB 传输路径的固有瓶颈，非 CPU 能力问题。

---

## 步骤五：测速完毕后处理

测速是破坏性操作（停了 bitcoind），测完后**重启板子**是最简单的恢复方式：

```bash
ssh root@<IP> 'reboot'
```

如果不想重启，按反序手动重新启动服务：

```bash
/etc/init.d/S61bitcoind start
/etc/init.d/S62ckpool start
/etc/init.d/S45supervisord start
/etc/init.d/S03lcdui start
/etc/init.d/S04serialhealth start
/etc/init.d/S60ntpd start
/etc/init.d/S44heartbeatd start
```

---

## 附：快速单板一键测速脚本

把以下内容在 WSL 中整段执行，指定 IP 后自动完成"停服务 → 等待 → 验 CPU → iperf3 双向测速 × 3"：

```bash
IP=192.168.124.XX
SERVER=192.168.124.20   # WSL 本机 iperf3 server

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

echo ">>> TX 上传 (板子→本地, 3 rounds):"
for run in 1 2 3; do
  echo "--- TX Run $run ---"
  ssh -o StrictHostKeyChecking=no root@$IP "iperf3 -c $SERVER -t 15 2>&1 | grep -E 'sender|receiver' | tail -2"
done

echo ">>> RX 下载 (本地→板子, 3 rounds):"
for run in 1 2 3; do
  echo "--- RX Run $run ---"
  ssh -o StrictHostKeyChecking=no root@$IP "iperf3 -c $SERVER -t 15 -R 2>&1 | grep -E 'sender|receiver' | tail -2"
done

echo ">>> Done. Run 'ssh root@$IP reboot' to restore services."
```

---

## 注意事项

- WiFi 板子通过 wlan0 IP 连接，ETH 板子通过 eth0 IP 连接，**不要混用**
- iperf3 每轮 `-t 15` 约 15 秒，双向 3 轮合计约 90 秒 + SSH 建连时间
- 本地 WSL iperf3 server 若未启动，板子端会报 `connect failed`，先确认 server 前台运行中
- 如果 WiFi 信号差导致 iperf3 波动大，检查信号或换成有线测试
- bitcoind stop 可能需要 10-60 秒，等待足够时间再测
