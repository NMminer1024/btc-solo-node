---
name: rockbase-netspeed
description: 'Measure Rockbase board network throughput under idle conditions. Use when: 测速, 网络测速, netspeed, speed test, throughput, 吞吐, SSH吞吐, WiFi速度, ETH速度, 带宽, bandwidth, 网速.'
---

# Rockbase 网络测速流程

## 核心原则

- **必须停服务再测**：bitcoind + ckpool 占用大量 CPU/IO/网络，会严重干扰结果
- **测前验证 CPU idle ≥ 50%**：否则测出的是 CPU 瓶颈，不是网络瓶颈
- **所有板子用同一套命令**：保证横向对比有效
- **测速方向**：从板子发数据到 WSL 本地（板子 TX 方向），用 SSH 做传输通道

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

## 步骤三：测速（从 WSL 执行）

### 标准测速命令（所有板子统一用这个）

```bash
IP=192.168.124.XX  # 替换为目标板子 IP（SDIO WiFi 用 wlan0 的 IP，ETH 用 eth0 的 IP）

echo "=== Run 1 ==="
{ time ssh -o StrictHostKeyChecking=no root@$IP \
    'dd if=/dev/zero bs=1M count=64 2>/dev/null' \
  | dd of=/dev/null bs=1M 2>&1; } 2>&1 | grep -E "bytes|real"

echo "=== Run 2 ==="
{ time ssh -o StrictHostKeyChecking=no root@$IP \
    'dd if=/dev/zero bs=1M count=64 2>/dev/null' \
  | dd of=/dev/null bs=1M 2>&1; } 2>&1 | grep -E "bytes|real"

echo "=== Run 3 ==="
{ time ssh -o StrictHostKeyChecking=no root@$IP \
    'dd if=/dev/zero bs=1M count=64 2>/dev/null' \
  | dd of=/dev/null bs=1M 2>&1; } 2>&1 | grep -E "bytes|real"
```

**参数固定**：`bs=1M count=64`（64 MB），不要随意改，保证跨板对比一致。

### 双板同测（SDIO WiFi vs USB ETH 对比）

```bash
IP_SDIO=192.168.124.21   # SDIO WiFi 板子
IP_ETH=192.168.124.30    # USB ETH 板子

for run in 1 2 3; do
  echo "=== Run $run SDIO ==="
  { time ssh -o StrictHostKeyChecking=no root@$IP_SDIO \
      'dd if=/dev/zero bs=1M count=64 2>/dev/null' \
    | dd of=/dev/null bs=1M 2>&1; } 2>&1 | grep -E "bytes|real"

  echo "=== Run $run ETH ==="
  { time ssh -o StrictHostKeyChecking=no root@$IP_ETH \
      'dd if=/dev/zero bs=1M count=64 2>/dev/null' \
    | dd of=/dev/null bs=1M 2>&1; } 2>&1 | grep -E "bytes|real"
  echo ""
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
| eth0 (USB ETH) | 97% | 12.0 MB/s | 12.1 MB/s | 12.1 MB/s | **~12.1 MB/s** |

**停服务后 SDIO WiFi 速度没有变化** —— 与服务运行中（1.0 MB/s）完全一致。

#### 根因结论（已验证）

SDIO WiFi 的 1 MB/s 瓶颈**不是** CPU、不是 SSH 加密、也不是我们的诊断补丁，而是 **RTL8189FS SDIO 驱动 TX 路径的硬件/驱动固有上限**：

- WiFi PHY 链路速率：72.2 Mb/s（信号 -56 dBm，质量 54/70，理论 ~9 MB/s）
- `/proc/net/dev` wlan0 TX 计数器实测：**1.4–1.6 MB/s**（接口层即如此，与 SSH 无关）
- chacha20-poly1305 cipher 替换 aes-128-ctr 后同样 ~955 kB/s（排除加密开销）
- 推断原因：RTL8189FS 驱动未实现有效的 A-MPDU 帧聚合，每帧都经历独立 SDIO CMD53 事务，SDIO 总线往返延迟累积导致吞吐受限

对比结论：USB ETH 在相同 CPU 空闲状态下稳定 12 MB/s，为 SDIO WiFi 的 **12 倍**。

### 判断依据

| 结果 | 解读 |
|------|------|
| SDIO 停服务后明显提升（>3 MB/s） | CPU 是主要瓶颈 |
| SDIO 停服务后无变化（仍 ~1 MB/s） | ✅ 已确认：驱动/硬件是瓶颈，与 CPU 无关 |
| ETH 停服务后明显提升（>20 MB/s） | 背景服务在抢网络带宽 |
| ETH 停服务后无明显变化（~12 MB/s） | ✅ 已确认：USB ETH dongle 自身限速约 12 MB/s |

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

把以下内容在 WSL 中整段执行，指定 IP 后自动完成"停服务 → 等待 → 验 CPU → 测速 × 3"：

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

echo ">>> Speed test (3 runs, 64 MB each):"
for run in 1 2 3; do
  echo "--- Run $run ---"
  { time ssh -o StrictHostKeyChecking=no root@$IP \
      'dd if=/dev/zero bs=1M count=64 2>/dev/null' \
    | dd of=/dev/null bs=1M 2>&1; } 2>&1 | grep -E "bytes|real"
done

echo ">>> Done. Run 'ssh root@$IP reboot' to restore services."
```

---

## 注意事项

- WiFi 板子通过 wlan0 IP 连接，ETH 板子通过 eth0 IP 连接，**不要混用**
- `dd count=64` 对 SDIO WiFi (~1 MB/s) 约需 60-70 秒/次，属正常
- 如果 SSH 连接不稳定导致 dd 中断，检查 WiFi 信号或换成有线测试
- bitcoind stop 可能需要 10-60 秒，等待足够时间再测
