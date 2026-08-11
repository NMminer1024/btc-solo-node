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

## 静默硬 lockup 专项方案（可落地修复 / 调试顺序）

背景结论见 `docs/rockbase-freeze-milestones.md` 的“2026-08-11：静默卡死的结构性复盘”。要点：本问题很可能是**单核 + 无硬 lockup 检测**下的**硬 lockup（关中断/总线 stall，定时器都停）**，现有 panic 抓手结构性看不到；且 1G/2G 的 SDIO WiFi 走**不同控制器、不同 PHY 路径**，不保证同一根因。

### 关于看门狗的定位价值（重要，先读这段）

**看门狗的 reset 不帮助定位问题，只帮助恢复可用性。** 因此生产环境不应依赖看门狗复位，这里只把它当**临时诊断辅助**，且**只进 debug 镜像，生产必须关闭**。

必须诚实认识它的盲区：

- 现跑的 `0006` 驱动**已经内置了一个 60s 的“SDIO op 卡死就 `panic()`”看门狗**（`RB8189_BUS_STUCK_PANIC_MS=60000`）。但现场 pstore **从未出现 `rb8189 SDIO bus op stuck`**。
- 那个 panic 挂在**软件定时器**（`timer_list` / timer softirq）上。单核一旦进入关中断硬 lockup，softirq/定时器全停 → 该 panic 没机会执行。**它没生效，本身就是“定时器被打死”的强证据。**
- **WDT 的 pretimeout 也是普通中断，会被同样的关中断状态挡住**；同理，任何“基于软件定时器周期性检查再 `panic()` 的活性探针”（即上一版提的 A3）也有**完全相同的盲区**。所以对真硬 lockup，软件侧再加 panic 基本无效。

结论：**真正能定位硬 lockup 的是 JTAG（见轨道 A）**。看门狗剩下的价值只有两点，且都属于 debug-only：

1. **硬 vs 软判别器**：下次卡死后看“WDT 是否 reset 了、pretimeout panic 是否落盘了”。WDT reset 了但 pretimeout panic 没落盘 → 坐实关中断硬 lockup（软件无解，必须 JTAG）；pretimeout panic 落盘了 → 是软 hang，栈已到手。
2. **无人值守泡机的兜底恢复**：让泡机不用守着断电。

下面四条轨道有先后。每条都遵循本仓库规则：内核/驱动/DTS 改动无法热部署，必须重编内核并重烧；改前先 `git` 记录，改后按 freeze 记录清单验证。

### 轨道 A：JTAG 硬件取证（定位硬 lockup 的唯一确定手段，主力）

当证据指向关中断硬 lockup（软件 panic 全部失效）时，软件侧已无法自己抓现场，必须上硬件。这是本问题的主力定位手段，不是最后手段。

A1. 接 JTAG（K230 RISC-V 大核），卡死复现时 halt 目标 hart，读 PC、`ra`、调用栈，以及 SDHCI（`0x91580000`/`0x91581000`）与 PHY 寄存器快照。
A2. 重点确认卡死 PC 是否落在：
- SDHCI/PHY 寄存器忙等自旋。`sdhci-of-kendryte.c` 的 `dwcmshc_phy_init` 有一段最多 15000 次的 `PHY_PWRGOOD` 忙等 `while(1)`；若卡在这类忙等且条件永不满足，就是关中断硬死的典型现场。
- 总线访问指令上不返回（提示 AXI/时钟 stall，偏硬件/电气）。
A3. 若拿不到 JTAG，退而用 K230 第二个 RISC-V 小核 / OpenSBI 侧做“对端观察”：由不参与卡死的执行体周期性读大核状态或 SDHCI 寄存器，卡死时 dump。属进阶手段。

### 轨道 A'：看门狗（debug-only 判别器 + 泡机兜底，生产必关）

仅在需要“无人值守泡机”或“判别硬/软 lockup”时启用，**不得进生产镜像**。

A'1. 用**独立的 debug fragment**启用，避免混进生产 defconfig。新增 `board/canaan/k230_rockbase/fragment/linux_wdt_debug.fragment`：

  ```text
  CONFIG_DW_WDT=y
  CONFIG_WATCHDOG_PRETIMEOUT_GOV=y
  CONFIG_WATCHDOG_PRETIMEOUT_GOV_PANIC=y
  CONFIG_WATCHDOG_PRETIMEOUT_DEFAULT_GOV_PANIC=y
  ```

  只在 debug 构建的 `BR2_LINUX_KERNEL_CONFIG_FRAGMENT_FILES` 里追加这个 fragment；生产 defconfig 不引用它，`CONFIG_DW_WDT` 保持未编译。
- DTS 里 `wdt1@91106800`（`snps,dw-wdt`）已存在，确认 `status` 为 okay。
- 喂狗方：`CONFIG_WATCHDOG_HANDLE_BOOT_ENABLED=y` 已在；用 busybox `watchdog -t <喂狗间隔> -T <超时> /dev/watchdog`，超时给宽（如 60s）避免误触发。
- 判别用法：pretimeout 先触发 → 软 hang，`panic()` 落盘拿栈；pretimeout 不触发只发生 reset → 关中断硬 lockup，转轨道 A（JTAG）。

A'2. 复位后建立 reset-reason：读 `dmesg | grep -i "dw_wdt\|watchdog"` 与 SoC 复位原因寄存器（`sysctl_boot@91102000` 附近，对照 K230 手册确认位定义），在 `S02pstore`/`boot_audit` 记录“上次是否 WDT reset / 是否 pretimeout panic”，并入 freeze 记录清单。

A'3.（可选，同样有软件定时器盲区）若想多覆盖“软 hang 子集”，可在 `0006` 现有 `RB8189_BUS_STUCK_PANIC_MS` 基础上，补一个“`queued>0` 且 `oldest_wait_ms` 持续增长但 worker 在 `wait_sema`”的丢唤醒判据也 `panic()`。注意它只对定时器还活着的软 hang 有效，对硬 lockup 无效——不要指望它兜住主问题。

### 轨道 B：共因判别实验（两板同做，一次一个变量，比对 MTBF）

前提修正（2026-08-11 用户反馈）：1G/2G **卡死频率相近且随机**，因此根因是**两板共有**，不是 2G 专属的 PHY 差异。所有实验都要**两板同时做、同口径对比 MTBF**，不要只在 2G 上做。目标是快速判别“共用 DMA/缓存一致性路径” vs “共用软件并发” vs “共用电气/时钟裕度”。

B1. 强制关 DMA 回退 PIO（判别 `dma-noncoherent` ADMA 缓存竞态，最高判别价值）：

- 依据：两个控制器都 `dma-noncoherent` + ADMA，是货真价实的共用代码路径；随机、与负载弱关联，符合缓存一致性/竞态特征。
- 改法：在 `sdhci-of-kendryte.c` 的 `sdhci_dwcmshc_kendryte_pdata.quirks` 里加 `SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_BROKEN_DMA`（core 在 `sdhci_setup_host` 会据此回退 PIO；确切位名以本树 `drivers/mmc/host/sdhci.c` 4270~4295 行为准）。
- 判读：两板强制 PIO 泡机后**都不再卡** → 坐实 `dma-noncoherent` ADMA/缓存一致性 bug，进而查 DMA 映射/cache flush 或 `dma-noncoherent` 属性是否与硬件实际一致；**照卡** → 排除 DMA 一致性，转 B2/B3。
- 代价：PIO 慢且吃 CPU（单核），只作诊断泡机，不作产品形态。可先只加 `SDHCI_QUIRK_BROKEN_ADMA`（退 SDMA 单缓冲）区分是 ADMA 描述符路径还是整个 DMA。

B2. 降 SDIO 时钟上限（判别共用电气/时钟裕度）：

- 两板都给各自 WiFi 控制器节点显式设较低 `max-frequency`（如 50→25MHz，1G 是 `&mmc_sd0`、2G 是 `&mmc_sd1`）。
- 判读：两板卡死频率**同时显著下降** → 共性电气/时序裕度问题（含 clock/PHY 默认值）；**无变化** → 与时钟无关。

B3. 收敛传输模式：

- 两板 WiFi 节点只跑 `cap-sd-highspeed`，不开 UHS/DDR/tuning。必要时 `/delete-property/ cap-sd-highspeed;` 退到 default speed（25MHz、单沿）做“最保守能不能不死”的判据。

B4.（已降级）PHY 校准差异：

- 频率相近已基本排除“2G 无 PHY 校准”为主因。仅当 B1/B2 之后仍怀疑 2G 有额外第二故障模式，才回头看 `91581000` 的 `mshc_ctrl_r`/切到有 PHY 校准控制器。不作为首要方向。

### 轨道 D：驱动去风险（仅在证据指向 0006 时）

- 目前分析：`0006` bus worker 的提交方是不可中断 `wait_for_completion`，若 worker 卡住有队列时会被 hung_task 抓到；实际没抓到，所以 worker 大概率不是静默死主凶。
- 因此**先不要盲目回退 `0005`/`0006`/`0007`**。只有当轨道 A'3 的丢唤醒探针明确抓到“worker 在 `wait_sema` 而 `queued>0` 且 `oldest_wait_ms` 一直涨”的软 hang 现场（注意该探针对硬 lockup 无效），才针对性地：
  - 用 completion/无锁队列 + `smp_mb()` 复核 `sdio_bus_req_sema` 的 up/down 与队列可见性；
  - 或评估直接依赖 MMC 核心的 `sdio_claim_host()` 天然互斥、移除自研 bus worker 这一层，减少手写并发面。

### 建议的落地顺序（一句话版）

1. **轨道 B1（强制 PIO 关 DMA，两板同做）**先做：最高判别价值、纯驱动 quirk 可逆、无需硬件。两板都不再卡 → 坐实 `dma-noncoherent` ADMA 缓存竞态；照卡 → 排除 DMA，做 B2（两板降 SDIO 时钟）。
2. **轨道 A'（debug-only 看门狗）**并行：仅用于判别“硬 vs 软 lockup”和无人值守泡机兜底，**不进生产**。看它下次是 pretimeout panic 落盘（软 hang，栈到手）还是只 reset（硬 lockup）。
3. 若 A' 判定为硬 lockup 且 B 未消除，上**轨道 A（JTAG）**——这是定位硬 lockup 的唯一确定手段。
4. 只有 A'3 软 hang 探针抓到 `0006` 丢唤醒时，才上**轨道 D**改驱动。
5. 生产镜像发布前，务必移除 A' 的看门狗 fragment，`CONFIG_DW_WDT` 保持未编译。

## 重新烧录后的 SSH host key

重新烧录镜像后，SSH host key 会变化。一次性检查时可用：

```bash
ssh -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@192.168.124.28 'uptime'
```

除非必要，不要永久修改 `known_hosts`。
