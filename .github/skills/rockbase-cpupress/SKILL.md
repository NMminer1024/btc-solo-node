---
name: rockbase-cpupress
description: 'Measure Rockbase board CPU, memory reclaim, scheduling and SD I/O pressure over short or long periods. Use when: CPU压力, CPU占用, CPU分布, 进程CPU, 页面CPU, 哪个进程吃CPU, CPU热点, 持续观测, 长期观测, 一小时压力, 压力趋势, SD压力, IO压力, 内存回收, 刷新过快, 卡顿排查, cpu_page, cpu_watch, 单核打满, load average高.'
---

# Rockbase CPU 压力分布测试流程

## 核心原则

- **板子是单核 RISC-V**：`nproc=1`，CPU% 即占该核百分比；load average > 1 即过载
- **对比不同 UI 页面时，重点看 `rockbase-lcdui` 一行的变化**；bitcoind/ckpool/内核态（kworker/kswapd/ksoftirqd）是各页共有的背景压力，页间波动（如 bitcoind 4%~9%）是背景噪声，不要归因到页面
- **采样前 UI 必须稳定停留在目标页面**：测的是该页持续刷新消耗，不是切页瞬间的尖峰
- **统一用 `/root/script/cpu_page.sh`**（已固化进镜像）：所有板子同一套脚本，保证横向对比有效
- **采样时长默认 60s**：太短会被瞬时波动干扰，太长浪费时间
- 脚本兼容 BusyBox（板子上无 `getconf`/`pgrep`/`ps --sort`，脚本内部只用 awk 解析 /proc）
- **长期观测统一用 `/root/script/cpu_watch.sh`**：主日志和进程日志写入 `/tmp` tmpfs，不向 SD 落盘
- `cpu_watch.sh` 默认观测 1 小时、每 30s 采样一次、每 15 分钟执行一个 60s 进程分布窗口

---

## 步骤一：快速摸底（可选，30 秒）

先看整机是否过载、大致谁在吃 CPU：

```bash
ssh -o StrictHostKeyChecking=no root@192.168.124.XX '
uptime; nproc; cat /proc/loadavg; top -bn1 | head -n 15'
```

判读：
- 单核下 load average **> 1 过载，> 3 严重过载**
- `top` 的 `CPU:` 行看 usr/sys/idle 分布；**sys 占比异常高（>40%）通常意味着内存回收/IO 压力**（kswapd 活跃），需结合 `free -m` 确认

---

## 步骤二：按页面测 CPU 压力分布（标准流程）

**每页操作**：UI 切到目标页 → 稳定停留 → 执行采样：

```bash
ssh -o StrictHostKeyChecking=no root@192.168.124.XX 'sh /root/script/cpu_page.sh <页面名> 60'
```

页面名用 UI 页签名：`Dashboard` / `Stratum` / `System` / `Network` 等。

输出包含：
- 整机 CPU 使用率（单核）
- 各进程 CPU% 排行（含 RSS），按消耗降序

**逐页测完做横向对比表**，格式：

| 页面 | 整机 CPU | rockbase-lcdui CPU | 判定 |
|------|---------|--------------------|------|
| Dashboard | xx% | xx% | 正常/异常 |

**判定标准**：
- UI 进程 CPU 与其他页一致（±3%）→ 该页正常
- UI 进程 CPU 显著高于其他页（>2 倍）→ 该页存在刷新/重绘热点，进 `main.cpp` 查该页的定时器/图表重绘路径

---

## 步骤三：定位热点（发现异常页后）

1. **看异常页的 UI 进程 CPU 增量**：如 Stratum 页 34.7% vs 其他页 ~7%，增量 ~27% 就是该页额外消耗
2. **进 `buildroot-overlay/package/rockbase_lcdui/src/main.cpp` 查该页**：
   - 该页的刷新定时器周期（`lv_timer` / 主循环里的刷新间隔）
   - 图表重绘成本（`lv_chart` 点数 × 序列数，如 Stratum 页 500 点 × 3 序列）
   - `present_display_buffer` / DRM commit 是否每帧全量
3. **优化方向**：降刷新频率、降采样点数、脏区重绘、数据无变化时跳过重绘
4. **改完按 `rockbase-lvgl-ui` skill 流程重建 + 热部署验证**，再用本 skill 复测对比

---

## 长期观测（CPU + 调度 + 内存回收 + SD I/O）

当用户要求持续观测、一小时压力、长期趋势或事后关联分析时，直接使用已固化脚本：

```bash
ssh -o StrictHostKeyChecking=no root@192.168.124.XX '
old=$(cat /tmp/cpu_watch.pid 2>/dev/null)
if [ -n "$old" ] && kill -0 "$old" 2>/dev/null; then
   echo "ALREADY_RUNNING pid=$old"
   exit 1
fi
rm -f /tmp/cpu_watch_latest.log /tmp/cpu_watch_latest_process.log /tmp/cpu_watch.pid
nohup sh /root/script/cpu_watch.sh 3600 30 900 >/tmp/cpu_watch_launcher.log 2>&1 &
pid=$!
echo "$pid" >/tmp/cpu_watch.pid
echo "STARTED pid=$pid"
'
```

参数依次为：

```text
cpu_watch.sh <总时长秒> <主采样间隔秒> <进程分布间隔秒>
```

默认值就是 `3600 30 900`。不传参数也可直接执行：

```bash
sh /root/script/cpu_watch.sh
```

### 日志位置

日志全部位于 `/tmp` tmpfs（RAM）中，swap 为 zram（RAM 内压缩，不落 SD），因此不会给 SD 增加写压力：

```text
/tmp/cpu_watch_latest.log          # 高频主日志软链接
/tmp/cpu_watch_latest_process.log  # 逐进程窗口日志软链接
/tmp/cpu_watch_<时间戳>.log
/tmp/cpu_watch_<时间戳>_process.log
```

重启会清空 `/tmp`，观测完成后及时拉回主机：

```bash
scp root@192.168.124.XX:/tmp/cpu_watch_latest.log ./
scp root@192.168.124.XX:/tmp/cpu_watch_latest_process.log ./
```

### 检查状态

不要轮询或持续 `tail`；长任务等待自然完成，需要人工确认时只检查一次：

```bash
ssh root@192.168.124.XX '
pid=$(cat /tmp/cpu_watch.pid 2>/dev/null)
if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
   echo "RUNNING pid=$pid"
else
   echo "FINISHED"
fi
tail -n 2 /tmp/cpu_watch_latest.log 2>/dev/null
'
```

主日志出现 `===== CPU WATCH END ... =====` 表示完整结束。脚本退出前会等待最后一个 60s 进程窗口完成，因此实际结束时间可能比总时长最多晚约 60s。

### 主日志字段

每个采样周期记录增量，便于按同一时间轴做关联分析：

- `cpu[]`：`busy/usr/sys/iowait/irq/steal/idle` 百分比
- `load[]`：1/5/15 分钟 load、运行队列/总任务数、最近 PID
- `sched[]`：context switch、中断、fork、当前 running/blocked 数
- `mem_kb`：`MemAvailable/MemTotal cache=PageCache swapfree=SwapFree`（KB）
- `mem_proc[]`：关键进程 RSS（KB）：`bitcoind/ckpool/rockbase-lcdui/sshd`，进程不存在记 0
- `vm[]`：fault、major fault、kswapd/direct scan 与 steal、allocstall、OOM
- `sd[]`：`mmcblk1` 的读写次数、KB、耗时、inflight、busy%、weighted I/O ms
- `temp_raw`：thermal zone 原始温度值

`/proc/diskstats` 和 `/sys` 的读取只访问内核统计，不会向 SD 发起数据读写。Rockbase 统一接线中 SD 为 `mmcblk1`；若未来硬件拓扑变化，先确认 `/proc/partitions` 再修改脚本中的 `SD_DEV`。

### 事后分析顺序

1. 汇总 `busy` 平均值、峰值以及超过 80%/90% 的样本比例。
2. 对照 `load`、`running`、`blocked` 判断 CPU 排队还是不可中断等待。
3. 用 `sys/iowait`、`sd busy_pct/weighted_ms` 区分内核 CPU 与 SD I/O 阻塞。
4. 用 `MemAvailable`、`scan_d`、`allocstall`、major fault 判断内存回收压力。
5. 对齐进程窗口，确认 `bitcoind`、`ckpool`、`rockbase-lcdui`、内核线程的 CPU 与 RSS；低频窗口的 `--- MEM TOP ---` 块给出 top10 RSS 进程分布 + 整机 meminfo 快照，用于发现内存膨胀（如 glibc arena 泄漏、缓存无上限增长）。
6. 用 `mem_proc[]` 趋势判断内存泄漏：某进程 RSS 随时间单调上涨且不回落 → 泄漏嫌疑；对照 `cache=` 与 `swapfree=` 判断 page cache 是否被 anon 页挤压（freeze 排查关键指标）。
7. 检查 `oom>0`、持续 `blocked>0`、高温等严重异常。

---

## 已验证基线（2026-08-19 实测，192.168.124.15，1G 版）

**测试条件**：服务全开（bitcoind + ckpool + statusd 等），单核，`sh /root/script/cpu_page.sh <页> 60`

### 修复前（Stratum 状态点呼吸动画启用）

| 页面 | 整机 CPU | **rockbase-lcdui** | bitcoind | ckpool | 判定 |
|------|---------|--------------------|----------|--------|------|
| Dashboard | 77.4% | 7.3% | 4.2% | 2.2% | ✅ 正常 |
| **Stratum** | **97.5%** | **34.7%** | 4.0% | 2.4% | ⚠️ **异常（×4.7）** |
| System | 81.1% | 6.7% | 4.9% | 6.2% | ✅ 正常 |
| Network | 87.0% | 6.8% | 9.4% | 2.2% | ✅ 正常 |

### 修复后（呼吸动画 `#if 0` 禁用，静态绿点）

| 配置 | 整机 CPU | **rockbase-lcdui** | bitcoind | ckpool | 判定 |
|------|---------|--------------------|----------|--------|------|
| 触摸 10ms + 睡眠上限 10ms | **73.4%** | **3.6%** | 2.8% | 2.7% | ✅ 已修复 |
| 触摸 20ms + 睡眠上限 20ms | **71.3%** | **3.1%** | 2.5% | 1.5% | ✅ 已修复（当前源码参数） |

**结论**：
- 除 Stratum 外各页 UI 进程稳定在 **6.7%~7.3%**
- **Stratum 页热点根因 = 状态点无限呼吸动画**（`start_status_dot_breathing` 的 `LV_ANIM_REPEAT_INFINITE`，每 10ms 一帧 `std::cos` 浮点 + 改透明度触发重绘），**不是图表刷新**（图表仅 32 点 × 3 序列、5s 刷一次）
- 修复：`#if 0` 禁用呼吸动画，在线显示静态绿点；Stratum 页 lcdui CPU **34.7% → 3.6%**，整机 97.5% → 73.4%
- 触摸参数最终定为 **20ms（50Hz）+ 睡眠上限 20ms**（`kTouchReadPeriodMs=20`, `kMaxLvSleepUs=20000`）：lcdui 3.6% → 3.1%、整机 73.4% → 71.3%，收益边际但无副作用；注意动画帧率上限相应降到 ~50fps，快速滑动略钝
- 背景压力：整机 sys 态 ~40-48% 偏高，与内存耗尽（free ~10M、kswapd 持续回收）相关，属各页共有背景。注：该基线测于 2026-08-19（当时 swap 已禁用）；2026-09-23 起 zram 128M lz4 已启用，但 1G 板 zram 很快打满，打满后压力特征与此基线基本一致

> 详细基线数据、根因分析与排查经验见 `docs/rockbase-cpu-baseline.md`。

---

## 注意事项

- **不要停服务测**（与 netspeed 不同）：本 skill 测的就是真实运行态下各页的 CPU 分布，停服务会失去对比意义
- 采样期间**不要切页**，否则测到的是切页+新页混合消耗
- 板子内存紧张时（free < 50M），内核态 CPU 会整体偏高，页间对比时留意这一背景
- 脚本在板子路径 `/root/script/cpu_page.sh`，镜像源 `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/script/cpu_page.sh`
