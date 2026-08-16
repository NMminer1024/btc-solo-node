# Rockbase Freeze 调试里程碑

本文档保存 Freeze 排查中最重要的阶段性结论。临时流水材料可放在本地 `output/freeze-log.md`，重要结论应及时整理到本文档。

## 2026-08-05：早期真实卡死

板子：

- `124.23` 2G。
- `124.28` 1G。

初始现象：

- LCD 时间停止或 UI 看起来卡住。
- 串口 health 有时仍继续输出。
- `RTW_XMIT_THREAD`、`flush`、`jbd2`、`kblockd` 都出现过可疑状态。

当时判断：

- 证据不足以单独归因到 LVGL 或 SD 卡。
- 需要增加 D 状态栈、MMC host 拓扑和持久化日志。

## 2026-08-06：SD 写回压力可复现部分堵塞链路

手工 SD 写压复现了明显的存储压力特征：

- `dirty/writeback` 高。
- `mmcblkXp3` 出现大写入突发。
- `flush-*`、`jbd2/*`、`kblockd` 卡在 ext4/MMC 路径。
- `dd` 在 `balance_dirty_pages_ratelimited_flags` 中被限速。

当时判断：

- SD 写回压力可以复现 freeze 链路的一部分。
- 后续证据证明它不是完整根因。

## 2026-08-06：写回参数调优降低突发

临时测试参数：

```text
vm.dirty_background_bytes=4194304
vm.dirty_bytes=16777216
vm.dirty_writeback_centisecs=100
vm.dirty_expire_centisecs=500
```

结果：

- `wbusy` 峰值明显降低。
- 瓶颈没有消失。

判断：

- 写回整形适合作为降低压力的手段。
- 不能解释后续 dirty 很低时仍出现的 WiFi/SDIO freeze。

## 2026-08-06 / 2026-08-07：禁用 WiFi 低功耗后仍复现

板子/日志：

- `COM14_2026_08_06.22.54.17.747.txt`
- `192.168.124.29`，约 2G 板。

关键点：

- 用户确认镜像已经禁用 RTL8189FS WiFi 低功耗。
- 仍然发生 freeze。

判断：

- WiFi 低功耗不是充分根因。
- 排查重点转向 RTL8189FS SDIO TX/RX/IRQ 路径和 MMC host claim 行为。

## 2026-08-07：124.28 硬复位后捞到证据

板子：

- `192.168.124.28`，1G。

复位后证据：

- `prev_shutdown=unclean`。
- 当时 `/sys/fs/pstore` 为空。
- 持久化日志显示 `RTW_XMIT_THREAD` 卡在 `__mmc_claim_host`。
- 多次出现 `sdio_irq_work` pending。

拓扑：

- `mmc0=SDIO WiFi`。
- `mmc1=SD 卡`。

判断：

- 虽然 1G/2G host 编号相反，但问题族一致。
- 不是单纯 SD 卡写回问题。

## 2026-08-08：124.28 出现 host claim 闭环

板子/日志：

- `COM5_2026_08_07.17.20.23.510.txt`
- `192.168.124.28`，1G。

关键证据：

- WiFi SDIO host 上出现 claim 闭环。
- 参与方包括 `RTW_XMIT_THREAD`、`RTW_CMD_THREAD` 和 `kworker/0:*`。
- 部分操作仍能以较大延迟完成，说明更像争抢/饥饿/活锁，不是某个线程简单永久睡死。

判断：

- RTL8189FS SDIO host claim 争抢成为最强嫌疑。
- `cmd=57` 已确认对应 `_Set_Drv_Extra`，频繁出现的 `cmd=57,11,1`、`cmd=57,5,0`、`cmd=57,2,0` 说明 `RTW_CMD_THREAD` 的后台 work 也参与 SDIO host 竞争。

## 2026-08-10：带 bus queue 镜像的 2G 板仍卡

板子/日志：

- `COM21_2026_08_10.12.40.41.959.txt`
- `192.168.124.23`，2G。

镜像背景：

- 镜像包含 `0005-serialize-sdio-host-and-dedupe-bg-cmds.patch`，也就是第一版 SDIO bus serialization/dedupe 补丁。

关键证据：

- 网络不可达。
- 尾部 `dirty/writeback` 很低。
- `RTW_XMIT_THREAD` 与 `sdio_irq_work`/kworker 仍在 SDIO host 上出现争抢。

判断：

- 第一版 bus serialization 没有完全解决 2G 真实 freeze。
- 可能还需要更完整的 SDIO 入口串行化或 WiFi bus 层恢复策略。
- 本次也证明诊断日志过密会干扰问题本身：`rb-mmc-diag claim wait begin` 大量打印 `waited_ms=0`，而真正有价值的是长等待、长持有和 claim/release 配对异常。

## 2026-08-10：诊断日志降噪

发现的问题：

- `rb-mmc-diag claim wait begin` 打印过密，很多是 `waited_ms=0`。
- 这会制造串口/printk 压力，在单核系统上可能放大调度问题。

决策：

- 保留高价值慢操作/阻塞记录。
- 降低正常路径和零等待日志。

## 2026-08-10：LVGL 滑动抖动架构改造

问题：

- 接入真实数据后，LVGL 页面左右滑动时页面内对象有抖动感。

架构改动：

- 数据刷新和滑动合成分离。
- 不再用 LVGL tileview 做实时滑动合成。
- 引入页面缓存合成器和 300ms 吸附动画。

结果：

- 用户确认滑动效果明显更顺滑。

详细设计见：

- `docs/design/rockbase-lvgl-ui-refactor-plan-20260810.md`

## 2026-08-11：pstore/ramoops 已加入并验证成功

镜像：

- 1G：`sdk:v1.2-20260811-104352-bitpony-DESKTOP-KC5S6JM-1f7ef1ae-nncase0.0.0`
- 2G：`sdk:v1.2-20260811-104909-bitpony-DESKTOP-KC5S6JM-1f7ef1ae-nncase0.0.0`

运行验证：

- `124.28` 使用 `ramoops@3fc00000`。
- `124.23` 使用 `ramoops@7fc00000`。
- 两台都挂载 `pstore on /sys/fs/pstore type pstore`。
- 两台都有 `S02pstore` 和 `S04serialhealth`。
- panic/hung-task sysctl 都已生效。

端到端验证：

- 在 `124.28` 上执行 `echo c > /proc/sysrq-trigger`。
- 板子 10 秒后自动重启。
- `/root/log/pstore-archive/latest/console-ramoops-0` 包含 panic、调用栈和 panic 前的 `rb8189-diag` 日志。

判断：

- pstore 已成为可靠抓手，能覆盖 panic/oops/lockup detector 路径。
- 如果是完全硬死锁，CPU 不再调度，pstore 仍可能没有新内容。

## 2026-08-11：124.23 硬复位后没有 panic 终点

板子：

- `192.168.124.23`，2G。

本地归档：

- `output/pstore-after-reset-20260811/124.23-074749-freeze/`

关键证据：

- `/root/log/pstore-archive/latest/console-ramoops-0` 被成功捞回，但没有 `Kernel panic`、`Oops`、`rb8189 SDIO bus op stuck` 或 watchdog panic。
- pstore 末尾是启动 3.8 秒后的 `reboot: Restarting system`，不是 freeze 最终现场。
- `heartbeat.log` 复位前最后一条为 `2026-08-11T07:30:21Z`，复位后下一条有效时间为 `2026-08-11T07:44:03Z`，中间约 13 分钟没有用户态日志。
- freeze 前 swap 为 0，dirty/writeback 不高，没有 OOM 迹象。
- `serialhealth-diag.log` 在 `2026-08-11 15:29:49` 看到 `RTW_SDIO_BUS:D:_rtw_down_sema`；此前多次出现 `RTW_XMIT_THREAD` 卡在 `_rtw_down_sema` 或 `rtw_sdio_bus_call`，但这些状态过去也能恢复。

判断：

- 本次更像完全硬 freeze，没有进入 panic/oops/watchdog 路径。
- 单独看到 `RTW_SDIO_BUS:D:_rtw_down_sema` 不能直接判定异常，因为 bus worker 空等 semaphore 时也可能表现为 D。
- 需要区分“正常空等”“队列丢唤醒”“IRQ pending 未处理”和“遗漏的 SDIO 入口直接卡住”。

后续诊断改动：

- 新增 `0007-log-sdio-bus-queue-state-snapshots.patch`。
- 记录 bus worker 当前状态、状态持续时间、active op、queue depth、最老请求等待时间、IRQ pending、累计 queued/completed/irq 计数、最近完成 op。
- 当提交线程等待 bus completion 超过 10 秒时打印 `submit wait` 现场。
- 该改动只增加诊断日志，不改变 SDIO 读写/IRQ 的执行顺序。

## 2026-08-11：124.28 上一轮镜像再次 freeze

板子：

- `192.168.124.28`，1G。

本地归档：

- `output/pstore-after-reset-20260811/124.28-161426-freeze/`

镜像背景：

- 当时 124.28 还没有烧录最新 `0007` 镜像。
- 模块中有早期 bus worker/watchdog 日志，但没有 `state worker=...`、`oldest_wait_ms`、`submit wait` 这些最新快照字段。

关键证据：

- `/root/log/pstore-archive/latest/console-ramoops-0` 被成功捞回，但没有 `Kernel panic`、`Oops`、`BUG` 或 `rb8189-bus stuck`。
- pstore 末尾仍是启动阶段日志和 `reboot: Restarting system`，不是 freeze 终点。
- `messages` 中 freeze 前最后一批 WiFi SDIO IRQ/读操作仍能完成，例如 `sd_int_hdl after_dpc` 和 `irq done elapsed_ms=204`。
- `serialhealth-diag.log` 在 freeze 前显示更强的 SD 卡写回/MMC block path 特征：
  - `kworker/...+kblockd:D:mmc_blk_rw_wait`
  - `kworker/u2:0+flush-179:0:D:__wait_on_buffer`
  - `kworker/0:3+events:D:__mmc_claim_host`
  - `disk_wbusy`/`block_wbusy` 多次到数百至数千百分比
- 同时 `RTW_XMIT_THREAD` 长期在 `_rtw_down_sema`，但没有看到 124.23 那种终局 `RTW_SDIO_BUS:D:_rtw_down_sema`。

## 2026-08-12：PIO 诊断仍可卡死，0008 转向自恢复

新一轮实验表明：

- 仅关 ADMA/SDMA 的 PIO 镜像并没有彻底消掉 freeze。
- 1G 三台 `124.8`、`124.15`、`124.21` 和 2G `124.23` 都已刷入最新镜像并在线核对 `release_version`。
- 当前提交 `cc6410e5` 只引入 `0008-bound-sdio-bus-wait-and-self-recover.patch` 与 `rockbase_statusd.sh` 修正，**没有恢复 DMA**。
- 这次修正的重点不是恢复 DMA，而是把 SDIO bus 的单次等待改成有界等待，并让超时后可以自恢复，避免再次把整机拖进无限阻塞。

对后续 agent 的提醒：

- 不要把 `cc6410e5` 误解成 DMA 回归提交。
- 如果后续要恢复 DMA，仍需单独处理 `0034-rockbase-sdhci-force-pio-dma-diagnostic.patch`。
- 下一轮测试要重点看 `0008` 是否把原来的“整机静默卡死”降级成“可恢复的超时错误”。

判断：

- 与 124.23 同属“没有 panic/oops 终点的 hard freeze/人工复位”问题族。
- 但这次 124.28 现场更偏向 SD 卡写回/MMC block path 参与或放大。
- 不能把 124.28 这次直接等同于 WiFi bus worker 自身死锁。
- 因为缺少 `0007` 快照，仍无法区分队列丢唤醒、IRQ pending 未处理、active op 卡住或底层 MMC 写回拖死。

## 2026-08-11：1G/2G 最新 `0007` 镜像已烧录并验证

已导出并烧录：

- 2G：`/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-2g-20260811-160221/sysimage-sdcard.img`
- 1G：`/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-1g-20260811-161202/sysimage-sdcard.img`

共同特征：

- 都是非 seed 最小镜像。
- 镜像大小约 `2.4G`。
- rootfs 当前基线为 `2G`。
- `blockdata.ext4=256M`。
- `/proc/swaps` 为空，swap 已禁用。
- `S02pstore` 存在。
- `pstore/ramoops` 已启用。
- `8189fs.ko` 均包含 `0007` 字符串：
  - `state worker=...`
  - `oldest_wait_ms`
  - `submit wait`
  - `irq_pending`

实机核对：

- `124.23` dmesg 已看到多条 `[rb8189-bus] state ...`。
- `124.28` dmesg 已看到 `[rb8189-bus] state worker=drain ...`。

下一次 freeze 的判断重点：

- pstore 是否出现 panic/oops/hung task。
- freeze 前最后的 `[rb8189-bus] state` 是 `wait_sema`、`drain` 还是 active op。
- 是否有 `queued>0` 且 `oldest_wait_ms` 持续增长。
- 是否有 `irq_pending=1` 且 `irqrun` 不增长。
- 是否出现 `submit wait`，以及提交者是 `RTW_XMIT_THREAD`、`RTW_CMD_THREAD`、`wpa_supplicant` 还是 IRQ 路径。
- 是否仍然主要表现为 SD 卡写回/MMC block path，例如 `mmc_blk_rw_wait`、`__wait_on_buffer`、`folio_wait_bit_common`。

## 2026-08-11：静默卡死的结构性复盘（可捕获性 + 两板硬件路径不同）

这一节是对“为什么折腾很久还没解决”的系统性复盘，结论来自代码/内核配置/DTS/现场日志的交叉核对，纠正了之前几个隐含假设。

### 结论 1：现有 panic 抓手结构性地看不到本问题

- 板子是**单核 Linux**（现场 dmesg：`rcu: RCU restricting CPUs from NR_CPUS=64 to nr_cpu_ids=1`）。
- 内核 `CONFIG_HARDLOCKUP_DETECTOR is not set`。RISC-V 单核也没有 NMI 型硬 lockup 检测。
  - 含义：只要卡死是**硬 lockup**（某段内核代码关中断自旋，或 SoC/AXI 总线 stall），CPU 不再调度、定时器中断不再来，`hung_task`、`softlockup`、`RCU stall`、`pstore panic` 全部无法触发。这类现场必然“完全静默、无任何 panic 落盘”。
- `hung_task` 只扫描 `TASK_UNINTERRUPTIBLE`（D）。而 RTL8189FS 的线程等待走 `_rtw_down_sema`，其实现是 `down_interruptible()`（见驱动 `os_dep/osdep_service.c`），属于 `TASK_INTERRUPTIBLE`（S）。
  - 含义：RTW 信号量上的**丢唤醒/死锁**对 `hung_task_panic` 完全不可见。这解释了为什么现场反复看到 `RTW_XMIT_THREAD:S:_rtw_down_sema` / `RTW_SDIO_BUS:_rtw_down_sema` 却从来不转成 panic。
- 反过来推断：`0006` 的 bus worker 提交方用的是 `wait_for_completion()`（不可中断 D）。如果 worker 卡在“队列非空但不干活”，提交方会进 D，`hung_task_panic` 应在 120s 内触发。**而实际 13 分钟静默都没触发**，说明本次更像**硬 lockup**，而不是可调度的 D 等待，也说明 bus worker 自身大概率不是这次静默死的主凶。
- 更强的一条证据：`0006` **自身就带一个 60s 的“SDIO op 卡死就 `panic()`”看门狗**（`RB8189_BUS_STUCK_PANIC_MS=60000`），但现场 pstore **从未出现 `rb8189 SDIO bus op stuck`**。这个 panic 挂在**软件定时器**（`timer_list`/timer softirq）上，单核关中断硬 lockup 时定时器全停 → 它没机会执行。它没触发本身就坐实“定时器被打死”。**推论：WDT pretimeout（普通中断）和任何“软件定时器 + panic”的活性探针都有同一盲区，对真硬 lockup 无效——不要指望看门狗或软件探针兜住主问题，定位硬 lockup 只能靠 JTAG。**

### 结论 2：1G 与 2G 的 SDIO WiFi 走的是不同控制器、不同 PHY 路径

- 驱动 `drivers/mmc/host/sdhci-of-kendryte.c` 按控制器物理地址区分行为：
  - `91580000`（sdhci0）：`have_phy=1`，执行完整 PHY 初始化与 `dwcmshc_phy_delay_config()` 延迟线校准（读 `rx_delay_line`/`tx_delay_line`，缺省 rx=0xd/tx=0x40）。
  - `91581000`（sdhci1）：`have_phy=0`，**完全不做 PHY 时序校准**，reset 时只写 `mshc_ctrl_r`。驱动注释写着 “sdio:(fpga board)”。
- 现场探测确认 WiFi 挂载控制器两板相反：
  - 1G `124.28`：`mmc0: new high speed SDIO card` = `91580000` = **有 PHY 校准**。
  - 2G `124.23`：`mmc1: new high speed SDIO card` = `91581000` = **无 PHY 校准**。
- DTS 侧印证：2G 用 `k230-rockbase-2g-lcd.dts` → include `k230-canmv-01studio-lcd.dts`，其中 2G WiFi 节点 `&mmc_sd1` 只有 `mshc_ctrl_r=0x00`，**没有 `rx_delay_line`/`tx_delay_line`**；而 SD 卡槽 `&mmc_sd0` 反而配了延迟线。
- 含义：**不能默认两台板子是同一个根因**。2G 的 WiFi 走的是无时序校准的弱电气路径，更容易出现高速 CMD53 边缘不稳→偶发 CRC/超时→（最坏）控制器级 stall。这类 stall 恰好落进“结论 1”的不可捕获盲区。

### 结论 3：SDIO 中断路径已澄清（推翻旧的轮询/`cap-sdio-irq` 猜测）

- 实际绑定的是 **SDHCI**（`sdhci-of-kendryte`，探测日志 `SDHCI controller ... using ADMA`），不是 dw_mmc。
- SDIO 中断是 SDHCI 硬件**带内中断**：`sd_sync_int_hdl` 跑在 `kworker`（`sdio_irq_work` 工作队列）里，不是 `ksdioirqd` 轮询线程。
- 所以 DTS 缺 `cap-sdio-irq` 不构成问题（SDHCI 核心自带 SDIO IRQ 能力）。之前任何“改成轮询/加 cap-sdio-irq”的方向作废。

### 诊断能力缺口清单（当前镜像）

- `CONFIG_DW_WDT` 未编译：WDT 硬件（`wdt1@91106800 snps,dw-wdt`）存在但**无驱动**，等于当前**没有任何硬件看门狗**，硬 lockup 只能靠人工断电，且无 reset-reason 可读。
- `CONFIG_WATCHDOG_PRETIMEOUT_GOV` 未开：没有 pretimeout→panic 兜底。
- `CONFIG_SOFTLOCKUP_DETECTOR=y`、`CONFIG_WQ_WATCHDOG=y`、`CONFIG_DEBUG_ATOMIC_SLEEP=y` 已在（且无 sleep-in-atomic splat，说明 `0006` worker 没在原子上下文睡觉）。

详细的落地修复/调试顺序见 `docs/rockbase-debug-playbook.md` 的“静默硬 lockup 专项方案”一节。

## 2026-08-11 补充：两板卡死频率相近且随机 → 根因是共因，PHY 差异不是主因

用户现场反馈（重要，修正上一节结论 2 的优先级）：

- 1G 与 2G **都会不定期卡死**，两板**频率相近**。
- 触发**看不出明显规律，接近随机**，与 WiFi/SD 负载没有强绑定。

由此推翻/降级的判断：

- **“2G 缺 PHY 校准”不可能是主根因**：1G 的 WiFi 走的是 `91580000`（`have_phy=1`，有 PHY 校准）却同样卡，且频率与 2G 相近。若 PHY 校准差异是主因，1G 应明显更稳。既然频率相近，PHY 差异对卡死率**没有可测影响**，只可能是极次要因素，不再作为首要方向。
- 主根因必须是**两板共有**的东西。

强化“共因”的一条结构性线索：

- 两次归档现场的“受害者”不同——1G 偏 **SD 卡写回**（`mmc_blk_rw_wait`/`__mmc_claim_host`），2G 偏 **WiFi/SDIO**（`RTW_SDIO_BUS:D`）。
- 但 SD 卡与 WiFi 这两条路，在两块板上**都由同一个 `sdhci-of-kendryte` 控制器驱动**承载，且两个控制器**都是 `dma-noncoherent` + ADMA**（`k230.dtsi` 中 `mmc_sd0`/`mmc_sd1` 均带 `dma-noncoherent`）。
- 一个**共用控制器驱动 / `dma-noncoherent` ADMA 缓存一致性层**的偶发 hang，正好能同时解释“两板都卡”和“受害者随当时谁在忙而不同”。随机、与负载无强关联，也符合竞态/缓存一致性类 bug 的特征。

修正后的共同嫌疑排序（从强到弱）：

1. **共用的 K230 SDHCI 驱动 + `dma-noncoherent`/ADMA 缓存一致性路径**（同时服务两板的 SD 卡与 WiFi）——最能解释“两板 + 两种受害者 + 随机”。
2. **共用的 RTL8189FS SDIO 软件路径 / `0006` 并发**（两板同一份补丁）。
3. 共用的 SoC 级因素（时钟/DVFS/AXI）。
4. ~~2G 无 PHY 校准弱路径~~ —— 频率相近已基本排除其为主因，降为可忽略项。

判别手段（关键）：SDHCI 支持用 quirk 强制回退 PIO（`SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_BROKEN_DMA`，`sdhci-of-kendryte.c` 里加一行）。强制 PIO 泡机：两板都不再卡 → 坐实 `dma-noncoherent` ADMA 缓存竞态；照卡 → 排除 DMA 一致性，转查软件并发/电气。落地顺序见 playbook。

## 2026-08-16：看门狗确实能让卡死恢复 —— 但它是误烧进镜像的，不是源码功能

本节结论来自对 4 台在线 1G 板（`124.15`/`124.21`/`124.27`/`124.30`）uptime 异常的排查，以及对 8/14 构建产物的交叉核对。**本次只做分析，未改动任何文件。**

### 现象

- 4 台 1G 板都是 8/14 下午约 16:00 上电，但 8/16 检查时 uptime 各不相同（部分只剩约 2 小时），说明**中途发生过非人为重启**。
- `boot_audit.log` 显示 `prev_shutdown=unclean`，`/sys/fs/pstore` 为空 → 不是 panic 路径，更像硬复位。
- 各板 `/etc/init.d/S05watchdog` 存在，且 `watchdog -T 60 -t 10 /dev/watchdog` 正在运行（busybox applet，pid ~164），加上内核线程 `[watchdogd]`。
- 60 秒硬件看门狗超时 → 硬复位。这解释了 uptime 各异 + unclean 复位。

### 关键事实：看门狗确实能让卡死恢复

- 这是**首次用真机实证**：带 `S05watchdog` 的 1G 板，卡死后会由 60s 硬件看门狗复位自动恢复，而不是像过去那样永久静默卡死（需人工断电）。
- 但要明确：**watchdog 是"兜底恢复"手段，不是"根因修复"**。它让板子能自愈，但卡死根因（SDHCI/ADMA 一致性 / RTL8189FS SDIO 争抢，见上文）仍未解决，且硬复位可能影响 bitcoind 数据一致性。**卡死根因仍需继续深挖。**

### 看门狗是怎么进镜像的（重要坑，避免重犯）

溯源结论：

- `S05watchdog` 启动脚本**只存在于构建产物** `output/k230_rockbase_1g_defconfig/target/etc/init.d/S05watchdog`（时间戳 `Aug 13 16:33`），**源码里从来没有**。
- 源码 `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/init.d/` 中无 `S05watchdog`。
- `git log --all` 全历史从未提交过任何 watchdog 相关文件。
- 时间线还原：
  1. 8/13 15:51 热部署改 WiFi（`S38wifi`，rootfs_overlay 能对上）。
  2. 8/13 16:33 热部署加看门狗，但把 `S05watchdog` **误写进 `output/.../target/`（构建产物目录）**，而非源码 `rootfs_overlay/`。
  3. 8/14 下午重编：buildroot 增量重编**不清空 target 目录**，rootfs_overlay 只做覆盖拷贝，不会删除 target 里 overlay 没有的残留文件 → 残留 `S05watchdog` 被原样打包进新镜像并烧录到 4 台板子。

- 内核侧：`CONFIG_DW_WATCHDOG=y` 是 `arch/riscv/configs/k230_defconfig` 默认开启的，**从来没人禁用过**，`/dev/watchdog` 一直存在。但没有用户态脚本打开它时，DesignWare 硬件看门狗不会激活，不会复位。所以内核驱动本身无害，关键是脚本。

### 1G vs 2G 对比

| 项 | 1G 镜像 | 2G 镜像 |
|----|---------|---------|
| `S05watchdog` 启动脚本 | **残留存在**（8/13 热部署误写进 target） | 无 |
| `CONFIG_DW_WATCHDOG` 内核驱动 | 开启 | 开启 |
| `/dev/watchdog` | 存在 | 存在 |
| 用户态看门狗进程 | **有** `watchdog -T 60 -t 10` | 无 |
| 卡死后是否自动复位 | **会**（60s） | 不会 |

（2G 板 `124.23` 的 `output/k230_rockbase_2g_defconfig/target/` 里没有 `S05watchdog`，所以 2G 不受影响。这也解释了为何复位现象集中在 1G 板。）

### 正确的去看门狗方法（供后续参考）

若要从镜像彻底去掉看门狗：

1. 删残留脚本：`rm -f output/k230_rockbase_1g_defconfig/target/etc/init.d/S05watchdog`（2G 若有残留也一并删）。
2. 更彻底：`make clean` 或删除 `output/<conf>/` 后全量重编（增量 `make` 不清 target，残留会被再次打包）。
3. 若要连内核驱动也关：在 `linux.fragment` 加 `# CONFIG_DW_WATCHDOG is not set` 并 `linux-dirclean` 重编内核，则 `/dev/watchdog` 也不存在。

### 热部署铁律（写入 agent 记忆）

- **热部署永远不要直接往 `output/<conf>/target/` 写文件**。要改镜像，必须写源码 `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/`，否则增量重编时残留会污染镜像，且 git 无法追踪（output 被 gitignore）。
- 判断"某文件是否真的来自源码"：先查 `rootfs_overlay/` 有没有、`git log --all -- <file>` 有没有，再看 `output/<conf>/target/` 的时间戳是否早于最后一次 build。

### 附带观察：MMC 慢路径仍是 SD 卡（mmc1）

- 4 台板 `rb-mmc-diag` 慢操作统计：mmc0（WiFi）干净（0~3 次），mmc1（SD 卡）281~4553 次、平均 754~804ms、最大 2496~5748ms。
- 结论：慢路径集中在 SD 卡 mmc1，WiFi mmc0 干净。与上文"SD 卡写回参与/放大 freeze"的方向一致。
- DMA 状态：当前镜像已恢复 ADMA（`mmc0/mmc1 ... using ADMA`），quirk 仅 `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN`，无 BROKEN_ADMA/BROKEN_DMA（`0034` 已 `.disabled`）。
