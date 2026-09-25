---
name: rockbase-freeze-debug
description: 'Debug Rockbase board freeze/lockup/hang issues. Collect logs, pstore data, and analyze freeze evidence. Use when: freeze, 卡死, 死锁, lockup, hang, pstore, ramoops, panic, 抓日志, crash, hung task, softlockup.'
---

# Rockbase 卡死调试

完整背景见 `docs/rockbase-debug-playbook.md` 和 `docs/rockbase-freeze-milestones.md`。

---

## 如果某台板子再次卡死

按顺序操作：

### 1. 保存串口日志

- 如果串口还在打印，**不要立刻断电**
- 保存串口日志文件，记录板子 IP、板子内存型号（1G/2G，运行时检测）和大概卡死时间
- 如果串口还在输出，继续录 2~5 分钟
- 如果串口完全无输出，SSH/网页都无响应 → 硬复位

### 2. 硬复位后第一时间捞 pstore

**所有板子（2026-09-24 起局域网仅 6 台 1G 真机，MAC 见 AGENTS.md 板子基线表）**：

```bash
ssh root@<IP> 'ls -lah /root/log/pstore-archive/latest; cat /root/log/pstore-archive/latest/* 2>/dev/null' > pstore-<IP>.txt
```

**注意**：`latest` 软链指向**当前这次启动**的 archive；上一次卡死的证据在**倒数第二个** `pstore-boot-*` 目录（按目录内 console-ramoops-0 大小判断，几百字节的是硬 lockup 空记录）。

### 3. 验证 pstore 是否正常

```bash
ssh root@<IP> 'mount | grep pstore; dmesg | grep -Ei "ramoops|pstore"; ls -lah /root/log/pstore-archive/latest'
```

---

## Freeze 记录清单

每次卡死至少记录：

- 板子 IP
- 内存 1G 还是 2G
- `/etc/version/release_version` 镜像版本
- 串口日志文件名
- 最后一条串口时间
- SSH 是否响应
- Web 是否响应
- 屏幕时间是否还在更新
- 重启后 pstore 是否有记录
- 最后的 `[health]` 行
- 最后的 `rb8189-diag` 行
- 最后的 `rb-mmc-diag` 行
- 当次启动的 MMC host 拓扑

---

## 日志判断重点

### 指向 WiFi/SDIO

- `RTW_XMIT_THREAD` 进入 `D` 状态
- `RTW_CMD_THREAD` 进入 `D` 状态
- `sdio_irq_work` pending 或 in-flight
- 栈里出现 `sd_write`、`sd_read`、`sdio_write_port`、`sdio_read_port`
- WiFi SDIO host 上出现 `__mmc_claim_host`
- RTW 线程和 kworker 之间出现 host claim waiter/owner 闭环
- `cmd=57`（`_Set_Drv_Extra`）频繁出现，常见子项 `cmd=57,11,1`、`cmd=57,5,0`、`cmd=57,2,0`

### 指向 SD 卡写回

- `dirty` 和 `writeback` 高
- `mmcblkXp3` 有大写入突发
- `flush-*`、`jbd2/*`、`kblockd` 进入 `D` 状态
- 栈里出现 `mmc_blk_rw_wait`
- 栈里出现 `balance_dirty_pages_ratelimited_flags`

### 指向 UI

- `rockbase-lcdui` 卡在非正常 sleep 路径
- 存储和 RTW 都正常，但只有 LCD 渲染停住

---

## 当前诊断状态

- **当前镜像已恢复 DMA**。`0034-rockbase-sdhci-force-pio-dma-diagnostic.patch` 已被 `936ea15b` 重命名为 `.disabled`（`buildroot-overlay/board/canaan/k230_rockbase/patches/linux/0034-rockbase-sdhci-force-pio-dma-diagnostic.patch.disabled`），源码默认恢复 DMA
- **关键结论：PIO 也会卡死。** 仅关 ADMA/SDMA 的 PIO 镜像并没有彻底消掉 freeze（见 `docs/rockbase-freeze-milestones.md`「2026-08-12：PIO 诊断仍可卡死」），因此 0034 不是解药，根因是单核硬 lockup，与 PIO/DMA 模式无关
- 提交脉络：`57fc4982`(fix 0034 hunk) → `cc6410e5`(rtl8189fs SDIO bus 自恢复，仍 PIO) → `88f1eff3`(sync freeze status) → `936ea15b`(disable 0034, restore DMA)
- 最强证据指向 `RTL8189FS + SDIO host claim/IRQ/XMIT` 路径
- pstore/ramoops 已加入并验证成功：`echo c > /proc/sysrq-trigger` 触发 panic → 10s 后重启 → 生成 `console-ramoops-0`
- **软挂死兜底已实战验证（2026-09-21，124.9）**：`rockbase_serial_healthd` 检测关键 I/O 进程持续 D > 60s → 写 `c` 触发 panic 自动重启，卡死 3.5h 后自愈。pstore 抓到完整证据链：mmc1（SD 卡）host claim 被 kworker 持有最长 8.5min + bitcoind 线程 D 在 `filemap_fault→folio_wait_bit_common` + OOM 双杀 bitcoind（当时 1G 无 swap——zram 2026-09-23 才启用，anon ~619MB）。详见 `docs/rockbase-freeze-milestones.md`「2026-09-21」节
- **区分 sysrq 类型**：`sysrq: Show Blocked State`（w 键）只是诊断 dump，不是重启原因；真正的重启是 `sysrq: Trigger a crash`（c 键）+ `Rebooting in 10 seconds`
- **healthd 触发时间戳**（内核秒数）：`/tmp/rockbase-serial-healthd.{sysrq,diag,panic}`；diag 明细在 `/run/rockbase-log/serialhealth-diag.log`

### Panic 设置

```text
kernel.panic = 10
kernel.panic_on_oops = 1
kernel.hung_task_panic = 1
kernel.softlockup_panic = 1
kernel.panic_on_warn = 1
kernel.sysrq = 1
```

### 关于硬 lockup

- 驱动已内置 `RB8189_BUS_STUCK_PANIC_MS=60000`，但软定时器在关中断硬 lockup 下不触发
- 真正能定位硬 lockup 的是 JTAG，不是软件看门狗
- WDT 的价值仅限于 debug：区分"硬 lockup"和"软 hang"
- 当前 pstore 仍可能为空（如果系统硬死锁到 CPU 不再调度）

---

## 修复优先级

1. 继续用 pstore 观察真实业务负载下的结果
2. 如果 pstore 捕获到 RTW/SDIO 争抢，优先补齐 SDIO 入口串行化
3. 如果串行化仍不足，评估 dedicated bus worker
4. 如果 pstore 为空且串口彻底停住，评估 JTAG/NMI
5. **不要**盲目加 watchdog 掩盖问题
6. **不要**恢复高频 rb-mmc-diag 打印（放大单核调度压力）
