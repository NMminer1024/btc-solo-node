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
