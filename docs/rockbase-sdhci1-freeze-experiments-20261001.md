# K230 sdhci1@91581000 无 PHY 校准冻结根因验证实验（2026-10-01 立项）

> 状态：实验 1 泡机中（124.38，2026-10-01 09:35 UTC 起），实验 2/3 待执行。
> 关联文档：`docs/rockbase-freeze-milestones.md`（历史排查里程碑）、
> `docs/freeze-baselines/dmesg-e0cafa-cdd6b3-20261001-dstate-episode.txt`（本次活体证据）。

## 1. 背景与根因假设

### 1.1 现象

- 板群（Rockbase 1G/2G 及 01Studio 对比板均有，见 §1.2 硬件背景澄清）
  不定期整机卡死：web/SSH 超时、LCD 冻结，靠 60s 硬件 WDT
  或 healthd D 状态兜底 panic 恢复；历史最长卡死 3.5h（2026-09-21，124.9）。
- 2026-10-01 在 124.38（e0:ca:fa:cd:d6:b3，1G）开机 7.4h 内抓到 **93 次**
  healthd D 状态告警（约 12 次/小时），其中 9 次有完整任务转储：8 次受害者为
  bitcoind `b-msghand`（`filemap_fault → folio_wait_bit_common → io_schedule`，
  等 SD 卡 mmc1 数据页），1 次双受害者（叠加 WiFi `sdio_irq_work →
  __mmc_claim_host`）。全部为秒级微卡死、可自愈；load 常态 2.1~2.6。

### 1.2 根因假设（当前最可能，~70%）

**`sdhci1@91581000` 控制器实例没有 PHY 延迟线校准，而 Rockbase 板把 SD 卡
挂在它上面（01Studio 对比板把 WiFi 挂在它上面）——CMD/DATA 线电气时序余量
不足，偶发边沿错误导致：幽灵中断 / 命令完成丢失 / 传输挂起。** 多数时候 SD 协议栈超时
重试救回（微卡死）；小概率重试路径自身 wedged（host claim 持有分钟~小时）
→ 整机卡死。

> **硬件背景澄清（2026-10-01 补记，用户确认）**：
> - Rockbase 1G 与 2G 板**硬件相同**，仅内存大小有区别（U-Boot
>   `detect_ddr_size()` 运行时探测，DDR 训练参数一致），SD 与 WiFi 接口
>   接线**完全一致**：mmc0=SDIO WiFi（sdhci0@91580000，有 PHY 校准）、
>   mmc1=SD 卡（sdhci1@91581000，无校准）。
> - 历史排查文档（含 `rockbase-freeze-milestones.md`）中记录的所谓"2G 板"
>   （`124.23`、`124.29` 等）**实际是 01Studio 的 2G 板**——当时为对比
>   测试借用，profile 虽命名为 `k230_rockbase_2g_*`，硬件却是 01Studio，
>   DTS 也 include 自 `k230-canmv-01studio-lcd.dts`。
> - 因此"接线相反"的对比双方是 **Rockbase 板（SD 卡挂 91581000） vs
>   01Studio 板（WiFi 挂 91581000）**，不是 Rockbase 1G vs 2G。
>   `k230-canmv-01studio-lcd.dts` 的 `&mmc_sd1` 同样只有
>   `mshc_ctrl_r = <0x00>`、无 `rx/tx_delay_line`——01Studio 板 WiFi
>   恰好也走这个无校准实例。

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
3. 实例视角统一历史现象：Rockbase 板受害者=SD 卡（挂在 91581000）、
   01Studio 对比板受害者=WiFi（挂在 91581000）→ "两类板频率相近、
   受害者随机" = 同一坏实例、不同设备。（见 §1.2 硬件背景澄清：
   历史文档中的"2G 板"即 01Studio 板。）
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
| `linux/0036-rockbase-sdhci1-delay-line-exp1.patch` | 0e6e75e9b5d5a77563e802f44f86403a | ~~实验 1~~ **已判定 no-op（§5.5）**：驱动对 91581000 硬编码 have_phy=0，DTS 延迟线属性从不被读取/写入；板上 dtb（5840c32e）功能上等同 exp0。保留存档，不再使用 |
| `linux/0037-rockbase-sdhci1-phy-probe-exp1b.patch` | （见文件头） | **实验 1b（已部署 124.38，10/1 16:06）**：驱动诊断——①probe 只读 dump 两实例 PHY 块(0x300)寄存器；②延迟线写入+读回日志；③PHY_PWRGOOD 超时不再 fail probe（防变砖安全网）；④DTS 门控 `force_phy`（未启用）。内核 Image md5 8ca5f33e（旧版备份 /boot/Image.exp1a.bak，f8c37440）。诊断日志前缀 `rockbase-exp1b` |

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
- **判"无效"前必须先排除"参数没生效"**：某档 DTS 值若 M1/M2 无改善，
  不能直接下"假设错误"结论——先确认参数确实写进了硬件，再判时序假设。
  确认手段（按成本排序）：
  1. 值扫描中的**负对照**：故意配大幅过冲值（如 rx=0x1f/tx=0xff），
     若 M1/M2 反而变差 → 证明参数链路生效（免费，嵌在值扫描里）；
  2. **pr_info 读回**：`sdhci-of-kendryte.c` 写完延迟线后读回寄存器并
     打印，开机 dmesg 直接物证（需重编内核+换 Image+重启，归实验 3 增强
     一起做，避免重置泡机时钟）。
- 背景事实（2026-10-01 实测）：sdhci1 的 PHY 寄存器块（基址+0x300 起）
  用户态 devmem **完全不可读**（空闲和 SD I/O 进行中均 segfault/全 F，
  而同期主寄存器 0x91581000 可读且随状态变化）→ 读回验证只能走内核内
  路径（手段 2）。对照实例 sdhci0 的 RX 寄存器 0x91580320 读回 0x0D 与
  其 DTS `rx_delay_line=0x0d` 一致，证明该机制读回的确实是 DTS 编程值。

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
- **I/O 窗口复读（10:10 UTC，边跑 dd 边读 10 次，排除"空闲门控"解释）**：
  sdhci1 主寄存器 0x91581000 在传输中可读且随状态变化（0x2E1/0x1ED/0x3C8…），
  但 PHY 块 0x9158131E 仍 devmem segfault、0x91581320 仍全 F
  → **sdhci1 的 PHY 寄存器块（+0x300 起）对用户态完全不可访问**，与门控
  时机无关；读回验证只能走内核内路径（见通用纪律的 pr_info 方案）。
- **机制旁证**：对照实例 sdhci0 的 RX 寄存器 0x91580320 读回 0x0D，与其
  DTS `rx_delay_line=0x0d` 一致 → 该组寄存器读回的确实是 DTS 编程值，
  延迟线机制是真实落地的。（sdhci0 TX 读回 0x40 与期望 0x20 不符，
  疑似 CNFG 高位编码影响读回，待寄存器手册确认，不影响 RX 旁证。）

### 5.3 结果记录（每档泡机结束/中止时填写）

快照原始数据：板上 `/root/log/exp1-snapshots.log`（每 2h 一行，字段：
m1_sysrq / m1_dstate / m2_n / m2_max_ms / m2_avg_ms / load / block / peers）；
事件 dmesg 归档到 `docs/freeze-baselines/`。

| 档 | DTB md5 | 泡机窗口 (UTC) | 时长 | M1 次/h | M2 max/avg ms | 结论 | 备注 |
|---|---|---|---|---|---|---|---|
| 基线 exp0 | 27a3fcc5 | 01:26–09:11 (9/30–10/1) | 7.77h | 12.2 | 2876/1107 | —（参照） | 见 §3 |
| exp1-A rx=0x0d/tx=0xc0 | 5840c32e | 09:35–11:00 (10/1) | 1.43h（用户判定提前终止） | **13.3**（19 次/1.43h，基线 12.2） | 1604/1153（含 2 次自测 dd 制造） | **无正向收益 → 事后重定性为 no-op（§5.5）** | 1.43h 速率与基线重合；§5.5 查明驱动对 91581000 硬编码 have_phy=0，延迟线从未写入 → **该档实为基线复测**（13.3/h 与基线 12.2 一致，数据有效，结论改为"复测确认基线速率"）；启动期伪中断未消失（§5.2） |
| exp0b 进程归因（停 bitcoind 1h） | 5840c32e | 11:37–12:42 (10/1，第 2 次尝试） | 1.08h | **9.2**（10 次/1.08h；同期运行段 13.3，基线 12.2） | 1604/1153（n=4 累计，与 exp1-A 持平，无恶化） | **未归零、降幅不显著**（Poisson p≈0.15–0.22） | 判据结果=后半支：**停了 bitcoind D 事件仍 ~9 次/h** → bitcoind 读不是主触发源；12:31:16 栈捕获受害者 = **jbd2/mmcblk1p3（ext4 日志线程，SD 卡）**，12:34:10 出现 d=2；触发时 disk_wbusy=0%（卡响应迟滞，非队列积压）。**第 1 次尝试（11:00:14 起）作废**：仅 `bitcoin-cli stop` 被 supervisord 22s 后拉起；修正=先落 `/run/rockbase/bitcoind-user-stopped` 再停（"skip bitcoind: user-stopped flag present" 确认）。12:44 删标志恢复 bitcoind：block 969442、peers 爬坡、ckpool up，运行正常。事件时间戳/采集器全量：`freeze-baselines/exp0b-obs-log-20261001.txt`；dmesg：`freeze-baselines/dmesg-e0cafa-cdd6b3-20261001-exp0b.txt` |
| exp1b PHY 探测（驱动诊断） | dtb 5840c32e / **Image 8ca5f33e** | 10/1 16:06 部署+重启 | 探测型（无泡机指标） | — | — | **延迟线假设判死**（§5.5） | sdhci1@91581000 PHY 块(0x300)整块读回全 0xFF = 寄存器文件不存在；sdhci0 同法读回全吻合（写 tx=0xc0/rx=0x0d → 回读 cnfg=0x01/dc=0x40/smpl=0x0d），方法有效。force_phy 阶段 2 不做（写空寄存器+未知 0x540 AT 位，风险>收益）。板上保留诊断内核（日志前缀 rockbase-exp1b，含 PWRGOOD 超时安全网） |
| exp1d 热写 AT 位（userspace） | 同上（无内核/dtb 变更） | 10/2 02:41–03:07（提前收工，25min） | 0.42h | **14.3**（6 次/0.42h；对照同 uptime 前段 12.1） | — | **无变化 → 判定事件驱动** | AT 位 25min 零 HW 活动 + 健康实例指纹位 10.5h 静态 = 一次性戳记 → 热写永不被消费（调谐仅在卡初始化跑）→ 转 exp1d'（§5.7）。数据：`freeze-baselines/exp1d-hot-at-reg-log-20261002.txt` |
| exp1d' probe 前置 AT 位（内核版） | dtb edbd68a5 / **Image e705189e** | 10/2 03:30–04:07 本地（用户判定 35min 提前收工） | 0.61h | **16.4**（10 次/0.61h；基线 12.2，复测 13.3，热写档 14.3） | 无红线事件（load ≤2.9，无 >10min stall） | **无效 → AT 机制对该实例判死** | 调谐**确已执行**：probe 写 0x011b0004（读回吻合，早于卡初始化）→ 卡初始化（t≈2.0s HS SDHC）后 0x540 回到 0x01000004（使能位被 HW 消费），与热写档"无初始化事件则永驻"对照闭环。但 M1 不降（16.4/h ≥ 各档基线）→ **auto-tuning 跑了也救不了** → 转 exp-1c（mshc_ctrl_r 0x00→0x01，§5.8）。数据：`freeze-baselines/exp1de-obs-log-20261002.txt`、`freeze-baselines/dmesg-e705189e-exp1d-exp1c-prep-20261002.txt` |
| exp1c mshc_ctrl_r=0x01 | dtb e55c56f1 / **Image 700d3506** | 10/2 04:19–04:46 本地（提前收工） | 0.46h | **19.2**（8 次/0.46h；基线 12.2） | 无红线事件 | **无效** | devmem 实测 0x508 bit0 写入后 **2s 内被硬件自动清除** → 此前"健康 0x01/病 0x00"静态对照疑为流量副产物（bit0 疑为每命令 strobe，sdhci1 持续流量恒被消耗）；DTS 0x01 只在每次 RESET_ALL 后短暂窗口有效（§5.8） |
| exp2 降频 50→25MHz（no_hs） | dtb c5bbb189 / **Image 5c71dbe8** | 10/2 05:20–06:25 本地 | 0.92h | **15.2**（14 次/0.92h；基线 12.2，复测 13.3，p≈0.26 无显著差异） | 无红线事件（load ≤3.4） | **无效 → 频率轴判死，软件域时序旋钮试尽** | 部署：DTS 去 cap-sd-highspeed 无效（HW CAPS 自通告 HS；本树 SDHCI_CAN_DO_HISPD=0x00200000=bit21 非标准 bit19）；sdhci-caps-mask 也无效（core 读时 dt_mask=0）；终方案=sdhci.c HS 授予点加 DTS 门控 `no_hs` 一票否决（`no_hs veto HS` 日志确认，卡 "new SDHC card" 无 high speed）。M5 探针（4K-direct/2s，>100ms 记 SLOW）2094 样本仅 1 次（开机首分钟 150ms）= 单读微卡顿罕见；**M1 不降 = 降频救不了 D 态冻结**。受害者模式不变（b-msghand filemap_fault 页读 SD 卡 >10s；同 tick ckpool 同 D）。M4：25MHz direct 读 32MB≈3s（~11MB/s vs 50MHz ~16MB/s）。数据：`freeze-baselines/exp2-50mhz-soak-dmesg-20261002.txt` |

> 每档泡机结束：① 从上表所在板取终值填本表；② 完整 dmesg 存
> `docs/freeze-baselines/dmesg-<board>-<date>-expN.txt`；③ 若中止/回滚，
> 记录触发原因（红线事件）。所有源码改动（补丁）在实验结论出来前保持
> 工作区未提交状态，结论为“无效”则直接删除补丁文件回基线。

### 5.4 exp1 + exp0b 阶段性解读（2026-10-01 12:50 UTC）

- **exp1（延迟线 rx=0x0d/tx=0xc0 镜像 sdhci0）**：1.43h 内 M1 13.3 次/h，
  与基线 12.2 无差异 → 镜像"已知良好"值不构成修复。
- **exp0b（停 bitcoind）**：M1 9.2 次/h，未归零、降幅不显著；受害者含
  **内核 jbd2 日志线程**（与 bitcoind 无关）。两点含义：
  1. D 事件由 SD 卡上的**任意读者**即可触发（jbd2、WiFi RTW 线程、
     statusd、web、日志轮转都在读），"bitcoind 读放大主导"假设**不成立**；
  2. 矛头继续指向 sdhci1 控制器/时序本身（或卡-控制器组合），与 §1.3
     证据链一致。
- **对假设的影响（2026-10-01 16:08 修订，§5.5 推翻本条旧结论）**：~~"缺
  PHY 延迟线校准"仍存活~~ → **延迟线假设对 sdhci1 判死**（该实例 PHY
  延迟线寄存器文件不存在，见 §5.5）。
- 板上状态（16:08 更新）：exp1 DTB（5840c32e，功能上等同 exp0）+ **诊断
  内核 Image（8ca5f33e，exp-1b，含只读 PHY 探测/写读回日志/PWRGOOD 超时
  安全网/未启用的 force_phy 门控）**；bitcoind/ckpool 16:07 重启后正常；
  旧内核备份 /boot/Image.exp1a.bak（f8c37440）；采集器 cron 保留。

### 5.5 exp-1b PHY 探测：延迟线假设判死（2026-10-01 16:06 部署，16:07 重启验证）

**代码事实**（`sdhci-of-kendryte.c` probe）：
```c
if (memcmp(host->hw_name, "91581000", 8) == 0) {
    priv->have_phy = 0;   /* SD 卡实例：硬编码无 PHY */
} else {
    priv->have_phy = 1;   /* 仅 91580000（WiFi）走 PHY 延迟线路径 */
}
...
if (priv->have_phy) { /* 只有此处才读 DTS rx/tx_delay_line 并写 0x300 块 */ }
```
→ exp1（0036 补丁，DTS 延迟线）对 sdhci1 **从未生效**（属性根本不读），
exp1-A 档实为基线复测。0036 补丁存档弃用。

**内核 probe 实测**（`rockbase-exp1b` 日志，10/1 16:07 boot）：
```
91580000.sdhci0: have_phy=1 dl:tx=0xc0 rx=0xd phy:cnfg=0x00890003 sdclkdl_dc=0x40 smpldl=0x0d
91580000.sdhci0: delay write tx=0xc0 rx=0xd rb:sdclkdl_cnfg=0x01 sdclkdl_dc=0x40 smpldl=0x0d   ← 写入读回全吻合
91581000.sdhci1: have_phy=0 phy:cnfg=0xffffffff cmd=0xff sdclkdl_cnfg=0xff sdclkdl_dc=0xff smpldl=0xff atdl=0xff
```
- sdhci1 的 0x300 PHY 寄存器组**整块读回全 0xFF**（与 userspace devmem
  全 F 互证）= 寄存器文件未实现/不存在 → **该硬件上没有延迟线旋钮**，
  与厂商硬编码 have_phy=0 的动机自洽
- sdhci0 写入-读回链路 100% 吻合（含 tx>128 的 CNFG=1/DC=tx-128 编码），
  排除"读回方法有问题"的备择解释
- **阶段 2（force_phy）不做**：强制走 PHY 路径只是写空寄存器（no-op）+
  附带 0x540 主块未知 AT 位（机制不明），风险>收益

**修订后的实验阶梯**：
1. ~~延迟线扫描~~ → 判死（硬件不存在）
2. **exp-1c：`mshc_ctrl_r` 扫描**（MSHC_CTRL_R=0x508，非 PHY 路径专用，
   内核注释"Launches CMD/DATA with respect to positive edge of cclk_tx"；
   当前 DTS=0x00）。第一候选 **0x01**（健康实例 0x508 低字节硬件默认值）
3. **exp-1d（新，§5.6 发现）：给 sdhci1 使能 auto-tuning 位**
   （AT_CTRL=0x540 |= 0x001F0000，健康实例实测已使能）
4. **exp2：&mmc_sd1 去掉 `cap-sd-highspeed`**（50MHz→25MHz，频率轴）
5. 硬件侧：换卡/示波器/1.8V-信号完整性（出软件域）

### 5.6 手册/上游代码查证 + exp-1c/1d 实测对照（2026-10-01 16:30）

**查证来源与结论**（用户要求"搜芯片手册核实不确定点"）：
- **Kendryte 官方 K230 datasheet**（github.com/kendryte/k230_docs，
  `en/00_hardware/K230_datasheet.md`）：2 个 SDxC 控制器（SD3.01/eMMC5.0），
  明确列出 **"Supports auto-tuning"** → 0x540/0x544 那组 AT 寄存器即
  采样相位自动调谐机制
- **K230 Hardware Design Guide**（同库）：**"The MMC1 interface's
  performance and pin count cannot meet the requirements for controlling
  eMMC circuits, so it can only be used to control SD/TF card circuits"**
  → MMC1（91581000，我们的 SD 卡）是**有意降配**的实例（MMC0 全配
  eMMC5.0/HS200/8-bit）；与 PHY 延迟线寄存器组缺失、厂商硬编码
  have_phy=0 完全自洽 → **延迟线缺失是硬件设计，不是缺陷/漏配**
- **上游驱动**（ruyisdk/linux-xuantie-kernel @7d4e1f44，commit
  9621f145 "k230 sd driver" 2024-06）：与板上驱动**逐字节一致**（无本地
  魔改）；提交历史仅 2 条，未解释 have_phy 硬编码动机
- **Kendryte 官方 U-Boot**（k230_linux_sdk → u-boot overlay
  `drivers/mmc/snps_sdhci.c`）：同寄存器映射；默认延迟 0x40/0x0d（与
  内核默认一致）；**完全不碰 mshc_ctrl_r**；PWRGOOD=BIT(1) 检查在
  全 0xFF 死块上恒真 → U-Boot 对死块无感，与"U-Boot 能正常启动 SD"
  无矛盾
- **`mshc_ctrl_r`(0x508) 位语义**：公开资料（官方 docs/datasheet/HDG/
  U-Boot/NuttX/第三方 fork）**均无定义**，仅内核一行注释 → 保持黑盒
  扫描性质，不因"查不到"而放弃，也不做位级预测

**实测对照（devmem，vendor 块 userspace 可读写）**：
| 寄存器 | sdhci0（健康） | sdhci1（SD卡） | 解读 |
|---|---|---|---|
| 0x508 低字节 | **0x01**（硬件默认，驱动不写） | **0x00**（DTS 钉死，驱动每 RESET_ALL 重写） | exp-1c 首候选 0x01 的依据 |
| 0x540 AT_CTRL | **0x1F1B0001**（AT 位 0x001F0000 全置位） | **0x01000004**（AT 位置 0） | **exp-1d 依据：auto-tuning 健康实例开着、病实例关着** |
| 0x544 AT_STAT | 0x00000006 | 0x00000006 | 一致 |

**热测试可行性（重大风险改善）**：vendor 块(0x500+)userspace 可
devmem 读写 → exp-1c/1d **可免重启热实验**：devmem 写候选值 → 观察
M1/M2 → devmem 写回。逃逸链：①ssh 活着则手动写回；②ssh 死则板上
`rockbase_serial_healthd.sh` 软挂死兜底（持续 D/wbusy 超 PANIC_AFTER_SEC
→ panic，kernel.panic=10 自动重启，重启即清 AT 位）；③mshc_ctrl_r
在下次 RESET_ALL 时被驱动按 DTS 值重写（双重自愈）。**红线不变**：
观察期出现 >10min 卡死立即回滚。

### 5.7 exp-1d 热测与 exp-1d'（内核版）（2026-10-02 02:41 起）

- **exp-1d（热写，02:41–03:07 本地，提前收工）**：devmem 将 sdhci1
  AT_CTRL(0x91581540) `0x01000004 → 0x011F0004`（置位内核 PHY 路径所用
  的 BIT16/17/19/20），AT_STAT 写 0（读回恒 0x06，疑只读状态位）。
  60s×25 采样：两实例 AT 寄存器**全程零变动**；M1 14.3 次/h 不变。
  **判定：auto-tuning 为事件驱动（卡初始化/HS 切换时跑一次），热写
  置位后无触发事件，位不被消费**。
- **exp-1d'（内核版，10/2 03:28 重启部署，03:30–04:07 本地，
  用户判定 35min 提前收工）**：补丁 0038（未跟踪）在 probe 非 PHY
  分支、卡初始化**之前**置位 AT 位（DTS 门控 `force_at_tune`，仅
  `&mmc_sd1` 添加）+ pr_info 读回日志（前缀 `rockbase-exp1d`）。
  **调谐真实执行的证据链闭环**：
  - probe 日志（t=1.777s，早于卡初始化 t=2.0s）：
    `force_at_tune AT_CTRL 0x01000004 -> 0x011b0004 AT_STAT=0x00000006`
    （写+读回吻合，注意内核 PHY 路径掩码实为 BIT16|17|19|20=0x001B0000）
  - 卡初始化（"new high speed SDHC card"）后 0x540 回到 **0x01000004**
    = 使能位被 HW 消费；对照热写档"无初始化事件 → 位永驻 25min 不动"
    → 消费模型成立，调谐在卡初始化时确实跑了一遍
  - atmon 20min（03:30–03:50）：两实例 AT 寄存器全程静态，无后续
    再调谐事件
  - **结果：M1 10 次/0.61h ≈ 16.4/h，不降反噪高（基线 12.2 / 复测
    13.3 / 热写档 14.3）** → **auto-tuning 跑了也救不了，AT 机制对
    该实例判死**。0038 存档弃用（不固化）。
- 重启部署=热写值的天然回滚。
- **下一步 exp-1c**（§5.8）：mshc_ctrl_r 0x00→0x01。

### 5.8 exp-1c：mshc_ctrl_r=0x01（2026-10-02 04:10 起）

**依据**（§5.6 实测对照 + 本次 04:07 复测）：两实例 MSHC_CTRL_R(0x508)
活值唯一差异在低字节——sdhci0（健康）=**0x01**（硬件自置，驱动写 0x00
后仍保持 0x01），sdhci1（病）=**0x00**（DTS 钉死 0x00，驱动每次
RESET_ALL 用 DTS 值重写，若硬件曾置位也会被抹掉）。高 16 位 0x0F0000
两实例相同（无关变量）。
**改动**：DTS `&mmc_sd1` 的 `mshc_ctrl_r = <0x00>` → `<0x01>`，使驱动
每次 RESET_ALL 都替硬件维持 bit0（镜像健康实例的稳态）。同一构建中
移除 `force_at_tune` 属性（AT 已判死，保持单变量；0038 驱动代码
留树内但失去 DTS 门控，行为等同基线）。
**位语义仍黑盒**（§5.6：公开资料无定义），本档性质=镜像对照实验，
非机制修复声明。
**部署**：重建 dtb → /boot/k.dtb（备份 k.dtb.exp1d.bak2）→ 重启 →
devmem 0x91581508 低字节应为 0x01 验证 → 1h 泡机（判据同 exp1d'：
M1 显著低于 12–14/h 才有效；红线 >10min stall 即回滚）。

**部署实测与重大发现（04:13 重启，Image 700d3506 / dtb e55c56f1，
旧版备份 Image.exp1de.bak=e705189e / k.dtb.exp1de.bak=edbd68a5）**：
- 重启验证：uptime 起新、`force_at_tune` probe 行消失 ✓、
  /proc/device-tree/soc/sdhci1@91581000/mshc_ctrl_r = 0x00000001 ✓、
  卡 HS 50MHz 正常挂载 ✓
- **但 devmem 0x91581508 低字节仍读 0x00** → devmem 写读测试查明：
  写 0x000F0001 立即读回 0x01 ✓（寄存器可写），**2s 后自动变回
  0x000F0000** = **bit0 会被硬件自行清除**
- 含义：此前"健康实例 0x01 / 病实例 0x00"的静态对照**可能只是
  流量副产物而非原因**——bit0 疑为"下一次 CMD/DATA 按 cclk_tx 上升
  沿发射"的一次性 strobe（内核唯一注释），每条命令发射后消耗归零；
  sdhci0（WiFi SDIO，基本无流量）保有 0x01，sdhci1（SD 卡持续
  流量）恒被消耗 → exp-1c 的 DTS 值只在每次 RESET_ALL 后的短暂
  窗口内有效，**预期收益下调**
- 决策：配置已部署，泡机成本≈0，继续 1h 观察（04:19:36 本地起，
  13×5min tick → /root/log/exp1c-obs.log；0x508 双实例 60s×20 采样
  → /root/log/exp1c-mshcmon.log）。M1 不降 → exp-1c 判死 +
  bit0=流量副产物假说升为主解释 → 转 exp2
- **M4 吞吐基线（50MHz，本启动窗口实测，exp2 对照用）**：
  direct 顺序读 32MB ≈ 2s（~16 MB/s）、direct 写 32MB ≈ 2s
  （~16 MB/s，ext4 根分区；/tmp 为 tmpfs 不计）。若 exp2 降到
  25MHz 后 M4 显著减半则验证"频率轴有效"，也给出对 bitcoind
  读放大影响的量化参照（用户 9/30 提问）

### 5.9 阶段性总判（2026-10-02 06:30）：软件域时序旋钮试尽，转硬件域

**全灭清单**（每个都有"参数确实生效"的闭环证据，排除无效操作假阳）：
1. ~~PHY 延迟线~~：硬件寄存器文件不存在（exp1b，0x300 块全 0xFF）
2. ~~auto-tuning~~：确认已执行（使能位被卡初始化消费）但无效（exp1d'）；
   与 1 同根因——调谐的执行机构（延迟线）在降配实例上不存在
3. ~~mshc_ctrl_r bit0~~：写入 2s 内被硬件清除（exp1c），静态对照为流量副产物
4. ~~50→25MHz 降频~~：M1 15.2/h 不降（exp2，no_hs 一票否决确认生效）

**统一结论**：sdhci1（降配实例）上的 D 态冻结**不与总线时钟频率成比例**，
控制器侧软件可达的时序自由度为零（无延迟线、无有效 AT、ctrl_r 位瞬态）。
受害模式恒定：页缓存读 SD 卡的进程（bitcoind b-msghand / jbd2 / 甚至
ckpool）在 10s+ 的卡级无响应中排队；dmesg 无控制器错误日志（重试在
控制器/ADMA 内消化）；WiFi 实例（满配）全程零错误。

**指向硬件域的三个候选**（优先级降序）：
1. **SD 卡本身**（弱卡/坏块区/电容老化）——最便宜的裁决实验：
   **换一张卡**。换卡后同样 ~12/h → 卡无关（控制器/PCB/电源）；
   换卡后显著下降/归零 → 原卡物理衰退，直接换卡即"修复"
2. **电源完整性**：SD VCC/VQCC 轨噪声或 LDO 余量不足，瞬态负载
   （WiFi TX 峰值）拉低卡供电 → 示波器/万用表动态量
3. **PCB 走线/SI**：降配实例走线长度/阻抗失配（板厂设计）

**留档的运行配置**：当前板跑 exp2 配置（25MHz，Image 5c71dbe8 +
dtb c5bbb189）。注意 25MHz 吞吐低 30%，若换卡实验需要 50MHz 基线
对照，用 /boot/k.dtb.exp2pre.bak（3ded397e 带 caps-mask 无效档）回
不到——应回到 k.dtb.exp1c.bak（e55c56f1，50MHz 等效基线）+ 对应
Image 700d3506（Image.exp2pre.bak 同为 700d3506 系）。**回基线命令**：
`cp /boot/Image.exp2pre.bak /boot/Image && cp /boot/k.dtb.exp1c.bak /boot/k.dtb && reboot`
（M5 探针 rockbase-m5-probe.sh 持续运行中，换卡后自动继续记录，
无需重部署——它是换卡实验的现成对照组指标。）
