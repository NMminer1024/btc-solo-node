# Rockbase SD 卡寿命检查方案设计

> 状态：**设计定稿（待实现）**
> 日期：2026-09-24
> 目标真机：`e0:95:bc:bb:62:bf` @ 192.168.124.31（金士顿 U3 V3 SD64G）
> 关联：`/memories/repo/sd-health-baseline.md`（字段语义基线 + 验证结论）

---

## 1. 背景与目标

整机频繁卡死，需要区分两个根因：

- **SD 卡寿命到了**（物理磨损、坏块、写寿命耗尽）
- **代码/系统问题**（SD I/O 压力 + 单核 + 内存压力，见 `rockbase-freeze-milestones.md`）

本方案在 web **About 页**展示一套全面的 SD 卡寿命/健康数据，配合现有 freeze 取证，让卡死时能一眼定性。

## 2. 核心约束（不可违背）

**功能本身不能给 SD 卡带来额外负担。** 因此：

| 允许（零/近零负担） | 禁止（会给 SD 加负担） |
|--------------------|----------------------|
| 读 `/proc/diskstats`（内核内存计数器） | 在线 `e2fsck`、主动 `dd` 读写测试 |
| 读 `/proc/meminfo`、`/proc/vmstat`（内存） | 频繁 `mmc extcsd read` |
| `dmesg`、`/sys/fs/pstore`（内核环形缓冲） | 对 block 分区做数据 I/O |
| 每天 ≤1 次 `mmc extcsd read`（若将来换 eMMC） | 定期写压力测试 |

> 采样脚本只读内核统计，绝不触碰 SD 数据区。持久化基线每次仅写几十字节到 `/root/config`（config 分区）。

## 3. 真机验证结论（已实测，决定方案取舍）

| 验证项 | 结果 | 对方案的影响 |
|--------|------|-------------|
| `/proc/diskstats` | **20 列**，无 errors 字段 | I/O 错误只能从 dmesg 提取 |
| 字段标定（增量法） | `f10`=累计写扇区（写 50MB→+103008≈精确）；`f6`=累计读扇区；`f12`=inflight；`f13`/`f14`=io_ms/weighted_io_ms | L1/L3 指标地基可靠 |
| `/proc/meminfo` | Dirty/Writeback 可读 | L3 脏页积压可用 |
| `dmesg` | 有 `rb-mmc-diag`（held_ms）+ 可 grep I/O error | L4 错误事件可用 |
| SD 卡 EXT_CSD | **CMD61 超时(ETIMEDOUT)、sysfs 无缓存** | **L2 厂商健康度砍掉** |

### 3.1 L2 为什么砍掉（领域知识）

寿命字段 `PRE_EOL_INFO`(216-221) / `DEVICE_LIFE_TIME_EST_TYP_A/B`(245-248) 是 **eMMC** 的 JEDEC 标准化 EXT_CSD 字段。**SD 卡没有 EXT_CSD 寄存器**（CMD61=SD_SEND_EXT_CSD 是 SD 4.0 可选命令，金士顿 U3 及绝大多数 SD 卡都不实现）。

实测证据：交叉编译静态 `sdextcsd` 直发 MMC 命令，CMD17 读用户数据成功、CMD61 读 EXT_CSD 超时。因此 **SD 卡寿命无法靠厂商字段，只能靠客观 I/O 统计**。

> 未来若换 eMMC 板：复用 `MMC_IOC_CMD`，flags=`MMC_RSP_R1(0x15)|MMC_CMD_ADTC(0x20)=0x35`，CMD61 is_acmd=1、blksz=1、blocks=1、arg=字节索引，即可读回 L2 字段。

## 4. 指标体系（最终版）

### L1 累计写入量 —— 寿命消耗最直接指标

来源 `/proc/diskstats` `mmcblk1` 行：

| 字段 | 列 | 说明 |
|------|-----|------|
| 累计写扇区 | `f10` | ×512B → 累计写入 GB/TB，**核心指标** |
| 累计读扇区 | `f6` | ×512B → 累计读取 |
| 写/读速率 | `f10`/`f6` 增量 | 相邻采样差 ÷ 间隔 |

> ⚠️ diskstats 重启清零 → 必须**跨重启持久化基线**（见 §5），否则累计值失真。

### L3 运行时写压力 —— 判断"是否被写爆"

| 指标 | 来源 | 说明 |
|------|------|------|
| wbusy% | `f14` 增量 ÷ 间隔 | 写繁忙度（与 `rockbase_serial_healthd.sh` 口径一致） |
| busy% | `f13` 增量 ÷ 间隔 | I/O 总繁忙度 |
| inflight | `f12` | 在途 I/O（瞬时） |
| Dirty/Writeback | `/proc/meminfo` | 脏页积压 → 写回压力 |
| 历史峰值 wbusy | 持久化 | 长期趋势 |

### L4 错误事件 —— 直接关联"卡死是不是 SD 的锅"

| 指标 | 来源 | 说明 |
|------|------|------|
| I/O 错误计数 | `dmesg` 增量 grep `I/O error`/`buffer I/O`/`blk_update_request` | 累计 + 最近 N 条 |
| 文件系统错误 | `dmesg` grep `ext4.*error` | 累计 |
| 卡死取证 | `/sys/fs/pstore` + freeze 日志 | 与 `rockbase-freeze-debug` 打通 |

### 综合健康评分

将 L1/L3/L4 归一化为 **0–100 分 + 状态标签**（Healthy/Degraded/Critical/Unknown），并给主因提示（§6）。

## 5. 采集架构

新增低频守护脚本 **`rockbase_sdhealthd.sh`**（独立于 statusd，不污染 15s 的 system shard）。

| 采样项 | 频率 | 说明 |
|--------|------|------|
| diskstats 累计 + 增量 | 5 min | 算速率、更新累计基线 |
| dmesg SD 错误扫描 | 10 min | 增量提取新事件 |
| 健康分计算 + 渲染 JSON | 5 min | 写 `/tmp/sdhealth.json` |

**持久化**：
- 实时快照 → `/tmp/sdhealth.json`（tmpfs，web 读，重启自清）
- 跨重启基线 → `/root/config/sdhealth.state`（config 分区）：历史累计写扇区、首次安装时间、累计运行时长、历史峰值 wbusy、累计 I/O 错误数。**每次只写几十字节。**

**Web 接入（推荐 A）**：`rockbase_statusd.sh` 渲染 `/tmp/status-api.json` 时，把 `/tmp/sdhealth.json` 合并进 `system.sd_health` 对象，前端零新增请求。

## 6. 判定逻辑（区分卡寿命 vs 代码问题）

**证据加权**，输出 `sd_health.verdict`：

**倾向 SD 卡寿命**（满足越多越确定）：
- 累计 I/O 错误持续增长 / 非零
- 累计写入量异常大（相对卡容量/使用时长）
- wbusy 长期接近 100% 且 inflight 持续堆积
- dmesg 出现 `I/O error` / mmc 超时
- 卡死时 pstore 阻塞在 `jbd2`/`flush-*`/`blk` 栈

**倾向代码问题**：
- 上述 SD 指标全部正常
- 卡死时用户态进程 D 状态、wchan 卡在锁/`cs_vNodes`
- CPU 单核打满、load 高但 I/O 空闲

**输出**：`sd_card_suspect` / `code_suspect` / `healthy` / `insufficient_data` + 人类可读主因说明。verdict 只是**提示**，不自动做任何动作（不重启/不停机），最终结合 pstore 人工确认。

## 7. Web 展示设计（About 页新增卡片）

在 Storage 卡片下方新增 **「SD Card Health」** 卡片：

```
SD Card Health          [状态徽章: Healthy/Degraded/Critical]
─────────────────────────────────────────────
Health Score        82 / 100
Verdict             Healthy
─────────────────────────────────────────────
Total Written       1.2 TB        (累计写入)
Write Rate (24h)    3.1 GB/day
I/O Errors          0             (累计)
Peak Write Busy     96%           (历史峰值)
Uptime on Card      412 days
─────────────────────────────────────────────
Recent I/O Events   (最近 3 条 dmesg SD 事件，无则"无")
```

- 状态徽章复用 `status-ok / status-warm / status-danger`
- 卡身份（型号/序列号）一并展示，便于换卡对比（序列号来自 sysfs `serial`）

## 8. 落地文件清单（实现阶段逐项勾销）

| # | 文件 | 改动 |
|---|------|------|
| 1 | `usr/bin/rockbase_sdhealthd.sh`（新增） | 采样 + 健康分 + 渲染 `/tmp/sdhealth.json` + 持久化基线 |
| 2 | `etc/init.d/S44sdhealthd`（新增） | 启动脚本（仿 `S43statusd`） |
| 3 | `usr/bin/rockbase_supervisord.sh` | 注册 sdhealthd 受管服务 |
| 4 | `usr/bin/rockbase_statusd.sh` | 渲染时合并 `sd_health` 进 `system` |
| 5 | `root/web/index.html` | About 页新增 SD Health 卡片 DOM |
| 6 | `root/web/app.js` | `normalizeDashboardData` + `renderDashboard` + **10 语言 i18n** |
| 7 | `root/web/style.css` | 卡片样式（复用 storage-card） |

## 9. 风险与降级

1. **累计基线丢失**（config 分区损坏）→ 重置基线并标注"基线已重置"，避免误报。
2. **判定误报** → verdict 仅提示，配合 freeze 日志人工确认。
3. **换卡/换内核后字段语义漂移** → 按 `sd-health-baseline.md`「标定方法」重跑增量标定，追加新基线。

## 10. 实施顺序（遵循项目规则）

1. 先 `scp`/`ssh` 热部署到真机验证
2. 验证通过后写回源码
3. i18n 10 语言同步（`rockbase-web-i18n`）
4. "打进镜像"时只固化已验证改动，不编译/导出（除非明确要求）
