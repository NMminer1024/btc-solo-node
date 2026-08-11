# Rockbase 当前交接文档

更新时间：2026-08-11 18:40 CST。

本文档是新 agent 接手入口。更完整的背景见：

- `docs/rockbase-project-overview.md`
- `docs/rockbase-debug-playbook.md`
- `docs/rockbase-freeze-milestones.md`
- `docs/rockbase-block-seed-image-workflow.md`

## 当前仓库状态

当前工作分支：`dev`。

最新提交：

- `c3c7b123 rockbase: rename 1g defconfig and align references`
- `ab6efa9b rockbase: commit pending wifi and init script updates`
- `d2136efa rockbase: set rootfs 2G and keep blockdata default 24G`
- `52543ab5 tools: remove unused web_img helpers`
- `741394f7 seed: switch to device sync and remove legacy SD-card flow`
- `f548c8d0 docs: organize Rockbase handoff and design notes`

当前未提交改动：

- `buildroot-overlay/board/canaan/k230_rockbase/patches/rtl8189fs/0007-log-sdio-bus-queue-state-snapshots.patch`

这个 `0007` 已经编译进当前两台真机正在跑的最新镜像，但补丁文件本身还没有 git commit。

## 板子基线

`192.168.124.23`：

- 2G 真板。
- 当前根分区：`root=/dev/mmcblk0p2`。
- 拓扑：`mmc0=SD card`，`mmc1=SDIO WiFi`。
- `ramoops@7fc00000`。

`192.168.124.28`：

- 1G 真板。
- 当前根分区：`root=/dev/mmcblk1p2`。
- 拓扑：`mmc0=SDIO WiFi`，`mmc1=SD card`。
- `ramoops@3fc00000`。

注意：1G/2G 的 MMC host 编号相反，分析日志时不要把固定的 `mmc0/mmc1` 当作固定设备角色。

## 当前已烧录镜像

两台都已烧录并在线核对过，符合预期。

2G 镜像：

- 板子：`192.168.124.23`
- Windows 侧镜像：`/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-2g-20260811-160221/sysimage-sdcard.img`
- 镜像大小：`2.4G`
- 内核时间：`Tue Aug 11 10:48:37 CST 2026`

1G 镜像：

- 板子：`192.168.124.28`
- Windows 侧镜像：`/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-1g-20260811-161202/sysimage-sdcard.img`
- 镜像大小：`2.4G`
- 内核时间：`Tue Aug 11 10:43:22 CST 2026`

镜像规则：

- 这两份都是非 seed 最小镜像。
- 当前 rootfs 基线是 `2G`，不是之前的 `6G`。
- `K230_BLOCKDATA_SIZE=256M`。
- `swap` 已禁用，两台 `/proc/swaps` 均为空。
- 默认不要构建 seed 镜像，除非用户明确说“带 seed”。

## 已验证的运行特征

两台都具备以下能力：

- `/sys/fs/pstore` 已挂载。
- `S02pstore` 存在，会在上电早期把上一次 pstore 内容归档到 `/root/log/pstore-archive/latest`。
- `pstore/ramoops` 已注册。
- `hung_task_panic=1`、相关 panic/lockup 抓手已经开启。
- `8189fs.ko` 已包含 `0007` 新增字符串：
  - `state worker=...`
  - `oldest_wait_ms`
  - `submit wait`
  - `irq_pending`

当前核对时 dmesg 已能看到：

```text
[rb8189-bus] SDIO bus worker started
[rb8189-bus] state worker=...
```

124.28 当前刚启动时可能显示 `1970` 时间，这是 NTP/时间同步尚未完成，不影响镜像判断。

## 当前 Freeze 判断

最可信假设：

- 主嫌疑仍是 RTL8189FS SDIO 路径。
- 关注点是 TX/RX/IRQ work、`RTW_SDIO_BUS` bus worker、`RTW_XMIT_THREAD`、`RTW_CMD_THREAD` 与 MMC host claim/SD 卡写回之间的交互。
- SD 卡写回和 bitcoind 同步是重要放大器，但目前不像唯一根因。
- UI/LVGL、内存耗尽、swap、单纯 ext4 写回都不是当前最强主因。

需要特别谨慎的点：

- `RTW_XMIT_THREAD:S:_rtw_down_sema` 本身常见，不一定异常。
- `RTW_SDIO_BUS:D:_rtw_down_sema` 也可能是正常等待 semaphore，不能单凭这一行判定死锁。
- 真正需要看 `0007` 新日志来区分：
  - worker 正常空等
  - 队列有请求但 worker 没醒
  - IRQ pending 长时间未处理
  - active op 卡住
  - 某个 SDIO 调用绕过 bus worker
  - SD 卡侧写回把系统拖进不可调度状态

## 最新卡死现场归档

本地归档根目录：

- `output/pstore-after-reset-20260811/`

重要现场：

- `output/pstore-after-reset-20260811/124.23-074749-freeze/`
- `output/pstore-after-reset-20260811/124.28-161426-freeze/`

124.23 现场要点：

- 2G 板。
- pstore 没有 `Kernel panic`、`Oops`、`rb8189-bus stuck`。
- `heartbeat.log` 复位前最后有效时间：`2026-08-11T07:30:21Z`。
- 复位后下一条有效时间：`2026-08-11T07:44:03Z`。
- freeze 前可见：`RTW_SDIO_BUS:D:_rtw_down_sema`，同时 `RTW_XMIT_THREAD` 长期在 `_rtw_down_sema`。
- 这次促成了 `0007`，因为需要区分 bus worker 正常空等和异常等待。

124.28 现场要点：

- 1G 板。
- 当时跑的是上一轮镜像，不包含 `0007` 的 state/submit-wait 快照。
- pstore 同样没有 panic/oops 终点。
- freeze 前更偏向 SD 卡侧写回/MMC block path：
  - `kworker/...+kblockd:D:mmc_blk_rw_wait`
  - `__wait_on_buffer`
  - `__mmc_claim_host`
  - `disk_wbusy` 可到数千百分比
- 同时 `RTW_XMIT_THREAD` 长期在 `_rtw_down_sema`。
- 结论：和 124.23 同属“无 panic/oops 的硬卡/失联后人工复位”，但 124.28 这次更像 SD 卡写回放大或参与，不能直接等同于 WiFi bus worker 自身卡死。

## `0007` 诊断补丁说明

文件：

- `buildroot-overlay/board/canaan/k230_rockbase/patches/rtl8189fs/0007-log-sdio-bus-queue-state-snapshots.patch`

目的：

- 不改变 SDIO 读写/IRQ 的执行顺序。
- 只增加诊断信息。
- 用于下一次 hard freeze 前的最后日志分类。

新增状态字段：

- `worker`：bus worker 当前状态，常见值 `init`、`wait_sema`、`drain`。
- `state_ms`：worker 停留在当前状态的时间。
- `active`：当前正在执行的 SDIO op，如 `sd_read`、`sd_write`、`sdio_irq`。
- `active_ms`：active op 已执行时长。
- `irq_pending`：是否有未处理 SDIO IRQ。
- `queued`：bus request 队列深度。
- `oldest_wait_ms`：队列最老请求等待时间。
- `totals=q/done/irqq/irqrun`：普通请求、已完成请求、IRQ 入队和 IRQ 实际执行累计值。
- `maxq`：观察到的最大队列深度。
- `last`：最近完成的 op、提交者、返回值、耗时、地址和长度。

关键日志样例：

```text
[rb8189-bus] state worker=wait_sema state_ms=... active=- ... irq_pending=1 queued=0 ...
[rb8189-bus] submit wait op=sd_write seq=... waited_ms=... worker=... active=...
```

判断方法：

- `worker=wait_sema queued=0 irq_pending=0`：大概率只是正常空等。
- `worker=wait_sema queued>0` 且 `oldest_wait_ms` 持续增长：疑似队列入队后 worker 未醒或 semaphore 丢唤醒。
- `worker=wait_sema irq_pending=1` 持续增长但没有 `irqrun` 增加：疑似 IRQ pending 没被 drain。
- `active` 非空且 `active_ms` 持续增长：疑似某个 SDIO op 卡住。
- 出现 `submit wait`：提交线程已经等 bus completion 超过 10 秒，应优先分析该行。
- 如果 freeze 后仍没有这些关键行，说明卡死可能发生在更底层或日志刷新前。

## 下次卡死后的操作

用户复位后，第一时间拉取：

```bash
mkdir -p output/pstore-after-reset-20260811/<board>-<time>-freeze
scp -O -r root@192.168.124.23:/root/log/pstore-archive/latest output/pstore-after-reset-20260811/124.23-<time>-freeze/pstore-boot-latest
scp -O root@192.168.124.23:/root/log/messages root@192.168.124.23:/root/log/serialhealth-diag.log root@192.168.124.23:/root/log/heartbeat.log output/pstore-after-reset-20260811/124.23-<time>-freeze/
```

124.28 同理替换 IP。

随后在线补抓：

```bash
ssh root@192.168.124.23 'dmesg; cat /proc/swaps; strings /lib/modules/6.6.36/updates/8189fs.ko | grep -E "rb8189-bus|oldest_wait_ms|submit wait|irq_pending"'
```

重点搜索：

```bash
rg -n "Kernel panic|Oops|BUG:|hung_task|blocked for more than|rb8189-bus|RTW_SDIO_BUS|RTW_XMIT_THREAD|RTW_CMD_THREAD|RTWHALXT|mmc_blk_rw_wait|__mmc_claim_host|folio_wait_bit_common|__wait_on_buffer" <freeze-dir>
```

## 构建和导出规则

默认最小非 seed 构建：

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_defconfig rtl8189fs-dirclean
make CONF=k230_rockbase_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0 \
  K230_IMAGE_OUT_DIR=/mnt/d/code/pony/IoT/k230/img
```

2G 板使用：

```bash
make CONF=k230_rockbase_2g_defconfig rtl8189fs-dirclean
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0 \
  K230_IMAGE_OUT_DIR=/mnt/d/code/pony/IoT/k230/img
```

注意：

- `k230_rockbase_defconfig` 是 1G 板。
- `k230_rockbase_2g_defconfig` 是 2G 板。
- 如果 Codex 沙箱内看到 `/mnt/d` 只读，不要直接下结论。必须用提权/非沙箱 `findmnt` 加真实 `touch` 写测试确认。
- 本轮 1G 构建时，post-image 自动导出曾在沙箱内报 `Read-only file system`，但非沙箱写测试确认 `/mnt/d` 是 `rw`，随后手动复制成功。

## UI/业务状态

近期 UI 已完成较大改造：

- LVGL 页面滑动从旧 tileview 思路改为页面缓存合成器。
- 数据刷新和滑动合成分离。
- 吸附动画约 300ms。
- 用户确认滑动效果明显改善。

这些不是当前 freeze 主线，除非用户明确要求 UI，否则新 agent 应优先关注 kernel/driver freeze。

## 接手建议

短期不要继续盲改 WiFi 逻辑。当前最有价值的是等待 `0007` 镜像下一次真实 freeze：

1. 判断 pstore 是否捕获 panic/oops/hung task。
2. 判断 `rb8189-bus state` 最后状态。
3. 判断是否出现 `submit wait`。
4. 判断是 WiFi bus worker 活锁、IRQ pending、队列丢唤醒，还是 SD 卡写回/MMC block path 主导。
5. 只有拿到新的 `0007` 现场后，再决定是否推进更彻底的 bus worker recovery 或 SDIO 入口收敛。

## 2026-08-11 补充：静默卡死结构性复盘与落地计划

新一轮代码/内核配置/DTS/现场交叉核对得到三条关键结论，纠正了之前的部分隐含假设。完整版见 `docs/rockbase-freeze-milestones.md`（“2026-08-11：静默卡死的结构性复盘”）与 `docs/rockbase-debug-playbook.md`（“静默硬 lockup 专项方案”）。

要点：

- **现有 panic 抓手看不到本问题**：单核 + `CONFIG_HARDLOCKUP_DETECTOR` 未开 → 硬 lockup 无法捕获；`hung_task` 只看 D 态，而 RTW 线程 `_rtw_down_sema` 是 `down_interruptible`（S 态），信号量丢唤醒/死锁对 `hung_task_panic` 天生不可见。13 分钟静默无 D 态 hung_task panic，更像**硬 lockup**。
- **1G 与 2G 的 SDIO WiFi 走不同控制器**：`sdhci-of-kendryte.c` 按地址区分，`91580000` 有 PHY 延迟线校准、`91581000`（注释 “fpga board”）无校准。1G WiFi 在 `91580000`（有校准），2G WiFi 在 `91581000`（**无校准弱路径**）。不能默认两板同一根因。
- **SDIO 中断路径已澄清**：实际是 SDHCI 硬件带内中断（`sd_sync_int_hdl` 在 kworker/`sdio_irq_work`），不是轮询，`cap-sdio-irq` 方向作废。

落地顺序（一次只动一个变量，改内核/DTS 后必须重编重烧，按 freeze 记录清单验证）：

1. **轨道 B1（先做，两板同做，驱动 quirk 可逆）**：`sdhci-of-kendryte.c` 加 `SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_BROKEN_DMA` 强制 PIO 泡机。两板都不再卡 → 坐实 `dma-noncoherent` ADMA 缓存竞态（两板共用路径）；照卡 → 排除 DMA，做 B2（两板降 SDIO 时钟 50→25MHz）。
2. **轨道 A'（debug-only 看门狗，生产必关）**：仅作“硬 vs 软 lockup”判别器 + 无人值守泡机兜底。**reset 不帮定位只帮恢复**，用独立 fragment 隔离，生产 `CONFIG_DW_WDT` 保持未编译。
3. **轨道 A（JTAG，硬 lockup 唯一确定定位手段）**：A' 判定为关中断硬 lockup 时上 JTAG，halt 读 PC/栈。
4. **轨道 D（驱动去风险）**：仅当软 hang 探针抓到 `0006` 丢唤醒才动，先别盲目回退 `0005/0006/0007`。

关键修正（2026-08-11 用户反馈）：**1G/2G 卡死频率相近且随机 → 根因是两板共因，不是 2G 专属的 PHY 校准差异**。1G 的 WiFi 走有 PHY 校准的控制器照样卡。最强共同嫌疑是**共用的 `sdhci-of-kendryte` 驱动 + `dma-noncoherent` ADMA 缓存一致性路径**（同时服务两板的 SD 卡与 WiFi，能解释“两板都卡、受害者随谁在忙而不同、随机无规律”），其次是共用的 `0006` SDIO 软件并发。

关键提醒：**不要指望看门狗或任何“软件定时器 + panic”探针兜住主问题**。现跑的 `0006` 已内置 60s“SDIO op 卡死 panic”，现场从未触发，证明卡死时定时器已停（关中断硬 lockup）；WDT pretimeout 与软件探针有同一盲区，只对“软 hang”子集有效。

当前尚未动手改代码/配置，等用户确认后再按轨道 B1 →（可选）A' → A 推进。
