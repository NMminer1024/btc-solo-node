# K230 sdhci1@91581000 无 PHY 校准冻结根因验证实验（2026-10-01 立项）

> 状态：实验 1 泡机中（124.38，2026-10-01 09:35 UTC 起），实验 2/3 待执行。
> 关联文档：`docs/rockbase-freeze-milestones.md`（历史排查里程碑）、
> `docs/freeze-baselines/dmesg-e0cafa-cdd6b3-20261001-dstate-episode.txt`（本次活体证据）。

## 1. 背景与根因假设

### 1.1 现象

- 板群（1G/2G 均有）不定期整机卡死：web/SSH 超时、LCD 冻结，靠 60s 硬件 WDT
  或 healthd D 状态兜底 panic 恢复；历史最长卡死 3.5h（2026-09-21，124.9）。
- 2026-10-01 在 124.38（e0:ca:fa:cd:d6:b3，1G）开机 7.4h 内抓到 **93 次**
  healthd D 状态告警（约 12 次/小时），其中 9 次有完整任务转储：8 次受害者为
  bitcoind `b-msghand`（`filemap_fault → folio_wait_bit_common → io_schedule`，
  等 SD 卡 mmc1 数据页），1 次双受害者（叠加 WiFi `sdio_irq_work →
  __mmc_claim_host`）。全部为秒级微卡死、可自愈；load 常态 2.1~2.6。

### 1.2 根因假设（当前最可能，~70%）

**`sdhci1@91581000` 控制器实例没有 PHY 延迟线校准，而 1G 板把 SD 卡挂在它
上面（2G 板把 WiFi 挂在它上面）——CMD/DATA 线电气时序余量不足，偶发边沿
错误导致：幽灵中断 / 命令完成丢失 / 传输挂起。** 多数时候 SD 协议栈超时
重试救回（微卡死）；小概率重试路径自身 wedged（host claim 持有分钟~小时）
→ 整机卡死。

### 1.3 证据链

1. **3 台不同板子开机 1.86s（SD 卡 HS 初始化 CMD2 阶段）系统性出现**
   `mmc1: Got command interrupt 0x1 even though no command operation was in
   progress` + 全零寄存器转储（`Timeout: 0x0 | Int stat: 0x0`）：
   124.38 [1.864955]、124.15 [1.861656]、124.27 [1.861560]。
2. **DTS 实锤**（`arch/riscv/boot/dts/canaan/k230-rockbase-lcd.dts`，1G 实际
   加载）：`&mmc_sd0`（WiFi）有 `rx_delay_line=0x0d / tx_delay_line=0xc0`；
   `&mmc_sd1`（SD 卡）**只有 `mshc_ctrl_r=0x00`，无任何延迟线**。驱动
   `sdhci-of-kendryte.c` 对 91581000 `have_phy=0`，reset 时只写 mshc_ctrl_r，
   完全跳过 PHY 时序校准（源码注释 "sdio:(fpga board)"）。
3. 实例视角统一历史现象：1G 受害者=SD 卡（挂在 91581000）、2G 受害者=WiFi
   （挂在 91581000）→ "两板频率相近、受害者随机" = 同一坏实例、不同设备。
4. 已排除：SD 卡物理健康（sdhealth score 100 / io_errors 0）、WiFi 省电
   （09-24 实验）、WiFi 驱动软件并发（0005/0006 泡机无效）、ADMA 一致性
   （0034 PIO 泡机无效）、共享中断线（142/144 独立）。
5. 慢 claim 全部 `contended=0`：慢在操作本身（硬件层），非软件锁争抢。

次嫌疑（~20%）：91581000 的 SoC 级时钟/AXI 问题，或驱动对未校准路径的
状态机缺陷。两者与主假设共享同一组实验判别手段。

## 2. 源码补丁现状（2026-10-01 核实，md5 存证）

构建注入方式：`BR2_GLOBAL_PATCH_DIR=buildroot-overlay/board/canaan/k230_rockbase/patches`
（`.disabled` 后缀不生效）。

### 生效中

| 补丁 | md5 | 作用 |
|---|---|---|
| `linux/0032-rockbase-mmc-claim-diagnostics.patch` | acbb8a14a206821495091a7dabbcaca8 | mmc host claim 慢操作打点（rb-mmc-diag） |
| `linux/0033-rockbase-mmc-claim-lifecycle-diagnostics.patch` | 87e096d9febaf854a328e5952a1e526a | claim 生命周期（owner/releaser/contended） |
| `rtl8189fs/0002-add-sdio-freeze-diagnostics.patch` | de577145ee1745c425efe06102a8bfea | SDIO 卡死诊断（seq/限流） |
| `rtl8189fs/0003-refine-sdio-freeze-diagnostics.patch` | 6ba05289648e8b045a6e30fe96d10e41 | 诊断细化 |
| `rtl8189fs/0004-close-sdio-diagnostic-gaps.patch` | b5fbfe7be76f11a6533e3b43a95958e0 | 诊断补漏 |
| `rtl8189fs/0009-enable-dynamic-tx-power.patch` | 9e91d4ef817dcf7163ca4d34c81d3a07 | Realtek DTP 动态发射功率（降热） |
| `linux/0036-rockbase-sdhci1-delay-line-exp1.patch` | 0e6e75e9b5d5a77563e802f44f86403a | **实验 1（进行中）**：sdhci1 加 PHY 延迟线（rx=0x0d/tx=0xc0）；2026-10-01 已按本补丁内容就地改树并热部署 dtb（md5 5840c32e）到 124.38 |

### 已禁用（实验过/搁置的候选修复与判别）

| 补丁 | md5 | 实验结论 |
|---|---|---|
| `rtl8189fs/0005-serialize-sdio-host-and-dedupe-bg-cmds.patch.disabled` | bfe3007171ebfd442f826457aaa5a1eb | 8/13 随 K230_NET 基线切换禁用 |
| `rtl8189fs/0006-serialize-sdio-bulk-and-irq-via-bus-worker.patch.disabled` | e4fb6a771796336d812e0972dbb064d6 | 8/10 泡机仍卡 |
| `rtl8189fs/0007-log-sdio-bus-queue-state-snapshots.patch.disabled` | 5efc60d99d5430c72baeb02e48e2c498 | 快照诊断，实验 3 拟重开；随 0005-0008 一起被降噪禁用 |
| `rtl8189fs/0008-bound-sdio-bus-wait-and-self-recover.patch.disabled` | dd0a082fa6c83de8ca32de295e2d6f04 | 8/12 曾启用，8/13 回退；思路保留 |
| `linux/0034-rockbase-sdhci-force-pio-dma-diagnostic.patch.disabled` | d804aa6c0e5f40d32e31ba679754c452 | 8/12 PIO 泡机仍卡 → DMA 恢复（936ea15） |

**结论：当前镜像 SD 卡侧只有诊断、没有任何修复在跑；卡死预防仅有
WDT(60s) + healthd D 状态兜底 + pstore 取证。**

## 3. 基线（124.38，2026-10-01 09:11 UTC 采集）

环境：v0.1.00-ccee7b4 镜像（含 statusd 饿死修复 48223b4 热部署）、内核
6.6.36、SanDisk SK32G 32G、SD 卡 high-speed（50MHz）模式、开机
~7.8h（01:26 启动）。采集时用户 ASIC 掉线（pool Users:0 Disconnected:5，
09:10 起，用户侧事件，不影响卡死测试——事件与负载无强关联）。

| 指标 | 基线值 | 说明 |
|---|---|---|
| M1 D 状态事件率 | **95 次 sysrq / 7.77h ≈ 12.2 次/h** | `dmesg \| grep -c "sysrq: Show Blocked"` |
| M1b 完整任务转储 | 9 次（8× b-msghand filemap_fault + 1× sdio_irq_work） | 双受害者现场 1 次 |
| M2 慢 claim | **n=12 / 7.77h ≈ 1.5 次/h；held_ms min 568 / avg 1107 / max 2876；contended 全 0** | 全部在 mmc1 |
| M3 load | 1min 2.0~2.5 / 5min ~2.4 | 单核，含 D 任务堆积 |
| M4 读吞吐 | 32MB direct read **1~2s（≈16~32 MB/s）**，其中慢 claim 占 1.4~1.7s | `dd if=/dev/mmcblk1p4 bs=32M iflag=direct` x3 |
| M5 链/挖矿 | 块高 969433 持续推进；采集前 1h hashrate5m 10.5T | 掉线后 36.5G |
| M6 SD 健康 | score 100 / healthy / wbusy 1% / peak 20% / io_errors 0 / 写 0.93GB/天 | |

复测方法（实验后同口径）：
```sh
dmesg | grep -c "sysrq: Show Blocked"                 # M1（除以运行小时）
dmesg | grep rb-mmc-diag | grep -o "held_ms=[0-9]*" | cut -d= -f2 | sort -n | awk 'NR==1{min=$1}{max=$1;s+=$1;n++}END{print "n="n,"min="min,"max="max,"avg="int(s/n)}'  # M2
cat /proc/loadavg                                     # M3
dd if=/dev/mmcblk1p4 of=/dev/null bs=32M count=1 iflag=direct   # M4 x3
```

## 4. 实验计划（均在 124.38 执行，先方案后动手）

判别逻辑：若根因是 91581000 电气/PHY 时序裕量，则**改善该实例的时序或降低
其工作频率，M1/M2 应显著下降**。

### 实验 1：给 `&mmc_sd1` 加 PHY 延迟线校准（DTS）

- 改动：`&mmc_sd1 { rx_delay_line = <0x0d>; tx_delay_line = <0xc0>; }`
  （先复用 mmc_sd0 的实测值）。
- 需要内核 DTB 重编（DTS-only，无驱动逻辑改动），出 A/B 对比镜像。
- 若 0x0d/0xc0 无效或无改善：扫 rx∈{0,5,d,15,1f}、tx∈{0,40,c0,ff}
  小矩阵，每值泡机 ≥2h 取 M1/M2。
- 风险：延迟线值过冲可能更差 → 每档保留回滚镜像（A/B OTA 或 TF 双烧）。
- 验收：泡机 ≥4h，M1 ≤ 基线 50%（<6 次/h）且 M2 max ≤ 500ms → 判定有效。

### 实验 2：SD 卡降速对照（DTS）

- 改动：去掉 `&mmc_sd1` 的 `cap-sd-highspeed`（HS 50MHz → 25/20MHz）。
- 预期：若时序裕量问题成立，降速后 M1/M2 应大幅下降；M4 读吞吐下降是
  可接受的代价参照。
- 与实验 1 串行（不要同镜像叠加，避免归因混淆）。
- 验收同上；M4 记录对应吞吐损失。

### 实验 3：重开 0007 类快照诊断（rtl8189fs 侧）+ 增强 rb-mmc-diag

- 重开 `0007-log-sdio-bus-queue-state-snapshots.patch`（去 .disabled），
  在实验 1/2 泡机期间持续采样：claim 内部状态（wait/timeout/retry 计数、
  队列深度、IRQ pending），用于区分"卡在等卡"vs"卡在等控制器"。
- 可选增强（新补丁，实验 3 内开发）：mmc1 慢 claim 时 dump sdhci 寄存器
  （复用 `sdhci_dumpregs`），把"全零幽灵中断"类现场从开机扩展到运行时。
- 验收：拿到 ≥1 次事件内的 claim 内部快照（不要求修复效果）。

### 执行顺序

基线（已完成）→ 实验 1（主验证，值扫描兜底）→ 实验 2（对照，1 无效时必做）
→ 实验 3 与 1/2 泡机期并行（只加诊断，不干扰归因）→ 结论归档 +
（若实验 1/2 有效）修复方案与发版计划。

### 通用纪律

- 每个实验独立镜像/独立 tag，记录镜像 sha 与 dtb md5。
- 泡机期间每 2h 记一次 M1/M2 快照；事件发生时（sysrq 出现）立即
  `dmesg > /tmp/dmesg-<ts>.txt` 并 scp 回本机归档到 `docs/freeze-baselines/`。
- 任何一档实验若出现 >10min 长卡死（M2 max > 600000ms）：立即回滚该档，
  记录为反证。

## 5. 执行记录

### 5.1 实验 1 部署（2026-10-01，124.38）

- 补丁：`buildroot-overlay/board/canaan/k230_rockbase/patches/linux/0036-rockbase-sdhci1-delay-line-exp1.patch`
  （DTS-only，`k230-rockbase.dts` + `k230-rockbase-lcd.dts` 的 `&mmc_sd1` 各加
  `rx_delay_line = <0x0d>; tx_delay_line = <0xc0>;`，与 mmc_sd0 实测值一致）。
  `patch -p1 --dry-run` 对干净源树验证通过。
- 驱动链路核实：`sdhci-of-kendryte.c` 读 DTB 两个属性，probe 时写 PHY 寄存器
  `DWC_MSHC_SDCLKDL_DC`(0x300+0x1E=0x31E) 与 `DWC_MSHC_SMPLDL_CNFG`(0x300+0x20=0x320)；
  无成功日志，值 >256 才告警。
- 构建：整树 make buildroot 不会重编 dtb（linux 包增量跳过 dtbs，syncconfig
  环境不全无法直接 `make dtbs`）；按 `.k230-rockbase-lcd.dtb.cmd` 记录的原始命令
  （gcc -E + scripts/dtc/dtc）手动重编了两个 dtb。
  反编译确认 `sdhci1@91581000` 节点含新属性（exp0 反编译无）。
- 产物存证：
  - 新 dtb（k230-rockbase-lcd）md5 = `5840c32e70f1afef9da4e069d7e06adc`
  - 原 dtb md5 = `27a3fcc51d7a3f5fe6c95175c637ccb3`（与基线板上一致）
- 部署方式：热部署 `/boot/k.dtb`（板上 `/boot` 在 A/B slot 根文件系统内，
  非独立 boot 分区）。原 dtb 备份 `/boot/k.dtb.exp0.bak`。重启生效。
- **回滚方式**：`cp /boot/k.dtb.exp0.bak /boot/k.dtb && reboot`
  （或 `cp` 回 27a3fcc5 版本后重启）。
- OTA 注意：板上 `rockbase-ota.sh` 仅 check 不自动 apply，泡机期不会自动刷
  v0.1.01；**实验期间禁止在 web/LCD 上 apply 升级**（apply 会整槽替换，
  实验 dtb 与热部署的 statusd 修复都会丢失，泡机作废）。
- 泡机起点：2026-10-01 09:35 UTC（新 boot 确认，peers=9 @09:43，
  block 969436，load ~2.3）。

### 5.2 实验 1 启动期观察（09:35 boot，待泡机结论）

- **启动期伪中断依旧出现**：`[1.808] mmc1: Got command interrupt 0x00000001
  even though no command operation was in progress` + 全零 Int stat 寄存器 dump，
  与 exp0（1.86s）签名一致 → 延迟线未消除该事件。注意：该事件不是判据
  （判据是 M1/M2），但说明它可能不完全由 CMD/DATA 时序裕量导致
  （或该事件发生在延迟线配置之后仍无法避免，需实验 3 的寄存器快照进一步判别）。
- 运行时 PHY 寄存器读回：devmem 读 sdhci1 主寄存器 0x91581000=0x00、
  PHY 块 0x9158131E/0x320=0xFFFFFFFF；对照 sdhci0 0x91580000=0x01、
  0x9158031C=0x00400101 可读 → 空闲时 sdhci1 总线被门控，devmem 读回不可靠，
  不能用作"延迟线已写入"的证据；验证以 device-tree 属性 + 驱动代码路径为准
  （/proc/device-tree/soc/sdhci1@91581000/{rx,tx}_delay_line = 0x0d/0xc0 已确认）。

### 5.3 结果记录（每档泡机结束/中止时填写）

快照原始数据：板上 `/root/log/exp1-snapshots.log`（每 2h 一行，字段：
m1_sysrq / m1_dstate / m2_n / m2_max_ms / m2_avg_ms / load / block / peers）；
事件 dmesg 归档到 `docs/freeze-baselines/`。

| 档 | DTB md5 | 泡机窗口 (UTC) | 时长 | M1 次/h | M2 max/avg ms | 结论 | 备注 |
|---|---|---|---|---|---|---|---|
| 基线 exp0 | 27a3fcc5 | 01:26–09:11 (9/30–10/1) | 7.77h | 12.2 | 2876/1107 | —（参照） | 见 §3 |
| exp1-A rx=0x0d/tx=0xc0 | 5840c32e | 09:35–13:35 (10/1) | 4h 计划 | 待填 | 待填 | 待填 | 泡机中 |

> 每档泡机结束：① 从上表所在板取终值填本表；② 完整 dmesg 存
> `docs/freeze-baselines/dmesg-<board>-<date>-expN.txt`；③ 若中止/回滚，
> 记录触发原因（红线事件）。所有源码改动（补丁）在实验结论出来前保持
> 工作区未提交状态，结论为“无效”则直接删除补丁文件回基线。
