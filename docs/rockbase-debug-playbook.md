# Rockbase 调试方法手册

## 目标

当前 Freeze 调试目标是定位真实的内核/驱动卡死路径，而不是优先用 watchdog 自动重启掩盖问题。

突然重启可能影响 `bitcoind` 数据一致性，因此 watchdog 自动恢复不是第一优先级。

## 如果某台板子再次卡死

按顺序操作：

1. 如果串口还在打印，不要立刻断电。
2. 保存串口日志文件，记录板子 IP、板子类型和大概卡死时间。
3. 如果串口还在输出，继续录 2 到 5 分钟。
4. 如果串口完全无输出，SSH/网页都无响应，硬复位。
5. 复位启动后，不要先跑测试，第一时间捞 pstore。

1G / `124.28`：

```bash
ssh root@192.168.124.28 'ls -lah /root/log/pstore-archive/latest; cat /root/log/pstore-archive/latest/* 2>/dev/null' > pstore-124.28.txt
```

2G / `124.23`：

```bash
ssh root@192.168.124.23 'ls -lah /root/log/pstore-archive/latest; cat /root/log/pstore-archive/latest/* 2>/dev/null' > pstore-124.23.txt
```

同时保存完整串口日志，例如 `COM5_*.txt` 或 `COM21_*.txt`。

## Pstore/Ramoops 状态

当前 pstore 实现：

- 内核启用 `CONFIG_PSTORE=y`。
- 内核启用 `CONFIG_PSTORE_CONSOLE=y`。
- 内核启用 `CONFIG_PSTORE_PMSG=y`。
- 内核启用 `CONFIG_PSTORE_RAM=y`。
- 1G DTS 预留 `ramoops@3fc00000`。
- 2G DTS 预留 `ramoops@7fc00000`。
- `S02pstore` 开机挂载 `/sys/fs/pstore`，把上一次记录归档到 `/root/log/pstore-archive/<timestamp>/`，更新 `/root/log/pstore-archive/latest`，然后清空 `/sys/fs/pstore`，避免重复读取旧记录。

常用验证命令：

```bash
mount | grep pstore
dmesg | grep -Ei 'ramoops|pstore'
ls -lah /root/log/pstore-archive/latest
```

预期启动证据：

```text
pstore: Registered ramoops as persistent store backend
printk: console [ramoops-1] enabled
ramoops: using 0x400000@...
```

## Pstore 端到端测试结果

2026-08-11，`124.28` 做过一次受控 panic 测试：

```bash
sync
echo c > /proc/sysrq-trigger
```

结果：

- 板子进入 kernel panic。
- 因为 `kernel.panic=10`，10 秒后自动重启。
- 重启后生成 `/root/log/pstore-archive/latest/console-ramoops-0`。
- 文件中包含 `sysrq: Trigger a crash`、`Kernel panic - not syncing: sysrq triggered crash`、调用栈和 panic 前的 `rb8189-diag` 日志。

这证明链路成立：

```text
panic -> ramoops -> reboot -> S02pstore 归档 -> /root/log/pstore-archive/latest
```

边界：

- 如果系统硬死锁到 CPU 不再调度，且没有 panic/oops/lockup detector 触发，pstore 仍可能为空。
- 当前启用 panic 设置的目的，是尽量把可检测的 hung task、softlockup、oops 转成可保存的 pstore 现场。

## 当前 panic 和 lockup 设置

预期 sysctl 值：

```text
kernel.panic = 10
kernel.panic_on_oops = 1
kernel.hung_task_panic = 1
kernel.softlockup_panic = 1
kernel.panic_on_warn = 1
kernel.sysrq = 1
```

验证命令：

```bash
sysctl kernel.panic kernel.panic_on_oops kernel.hung_task_panic kernel.softlockup_panic kernel.panic_on_warn kernel.sysrq
```

## Freeze 记录清单

每次卡死至少记录：

- 板子 IP。
- 1G 或 2G。
- `/etc/version/release_version` 镜像版本。
- 串口日志文件名。
- 最后一条串口时间。
- SSH 是否响应。
- Web 是否响应。
- 屏幕时间是否还在更新。
- 重启后 pstore 是否有记录。
- 最后的 `[health]` 行。
- 最后的 `rb8189-diag` 行。
- 最后的 `rb-mmc-diag` 行。
- 当次启动的 MMC host 拓扑。

## 日志判断重点

指向 WiFi/SDIO 的信号：

- `RTW_XMIT_THREAD` 进入 `D`。
- `RTW_CMD_THREAD` 进入 `D`。
- `sdio_irq_work` pending 或 in-flight。
- 栈里出现 `sd_write`、`sd_read`、`sdio_write_port`、`sdio_read_port`。
- WiFi SDIO host 上出现 `__mmc_claim_host`。
- RTW 线程和 kworker 之间反复出现 host claim waiter/owner 闭环。
- `RTW_CMD_THREAD` 频繁处理 `cmd=57`。这里 `cmd=57` 对应 `_Set_Drv_Extra`，常见子项包括 `cmd=57,11,1`、`cmd=57,5,0`、`cmd=57,2,0`。

指向 SD 卡写回的信号：

- `dirty` 和 `writeback` 高。
- `mmcblkXp3` 有大写入突发。
- `flush-*`、`jbd2/*`、`kblockd` 进入 `D`。
- 栈里出现 `mmc_blk_rw_wait`。
- 栈里出现 `balance_dirty_pages_ratelimited_flags`。

指向 UI 的信号：

- `rockbase-lcdui` 卡在非正常 sleep 路径。
- 存储和 RTW 路径都正常。
- 串口/SSH 健康，但只有 LCD 渲染停住。

## 当前工作假设

目前最强的反复证据指向 `RTL8189FS + SDIO host claim/IRQ/XMIT` 路径。

SD 卡写回可能是触发器或放大器，但多次尾部现场显示 dirty/writeback 很低，仍然有 RTW/SDIO host 争抢。

## 后续修复优先级

当前不建议盲目加 watchdog 或继续加压复现。bitcoind 正在维护区块数据，突然自动重启可能放大数据一致性风险。

建议优先级：

1. 继续观察 pstore 镜像在真实业务负载下的结果，先判断下一次 freeze 是否能被 panic/oops/hung-task/softlockup 捕获。
2. 如果 pstore 捕获到 RTW/SDIO 争抢，优先补齐 RTL8189FS 的 SDIO 顶层入口串行化，覆盖 TX、RX/IRQ、需要 `sdio_claim_host()` 的 local read/write 入口。
3. 如果第一版串行化仍不足，再评估更彻底的 dedicated bus worker，把所有 SDIO 访问汇入单 worker 顺序执行。
4. 如果 pstore 为空且串口也彻底停住，再评估独立 watchdog/NMI 类抓手，但这应作为定位手段，不应先作为产品恢复策略。

当前不建议作为第一刀：

- 不建议优先调大 MMC/SDIO timeout 掩盖问题。
- 不建议把 `bitcoind`、ext4 或 VM dirty 参数当成主修复方向。
- 不建议继续把 WiFi 低功耗当成唯一主因，禁用低功耗后已经复现过。
- 不建议恢复高频 `rb-mmc-diag claim wait begin waited_ms=0` 打印，这类日志会放大单核系统调度压力。

## 重新烧录后的 SSH host key

重新烧录镜像后，SSH host key 会变化。一次性检查时可用：

```bash
ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@192.168.124.28 'uptime'
```

除非必要，不要永久修改 `known_hosts`。
