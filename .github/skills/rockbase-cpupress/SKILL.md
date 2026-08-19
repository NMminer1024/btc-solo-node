---
name: rockbase-cpupress
description: 'Measure Rockbase board CPU pressure distribution per process / per UI page. Use when: CPU压力, CPU占用, CPU分布, 进程CPU, 页面CPU, 哪个进程吃CPU, CPU热点, 刷新过快, 卡顿排查, cpu_page, 单核打满, load average高.'
---

# Rockbase CPU 压力分布测试流程

## 核心原则

- **板子是单核 RISC-V**：`nproc=1`，CPU% 即占该核百分比；load average > 1 即过载
- **对比不同 UI 页面时，重点看 `rockbase-lcdui` 一行的变化**；bitcoind/ckpool/内核态（kworker/kswapd/ksoftirqd）是各页共有的背景压力，页间波动（如 bitcoind 4%~9%）是背景噪声，不要归因到页面
- **采样前 UI 必须稳定停留在目标页面**：测的是该页持续刷新消耗，不是切页瞬间的尖峰
- **统一用 `/root/script/cpu_page.sh`**（已固化进镜像）：所有板子同一套脚本，保证横向对比有效
- **采样时长默认 60s**：太短会被瞬时波动干扰，太长浪费时间
- 脚本兼容 BusyBox（板子上无 `getconf`/`pgrep`/`ps --sort`，脚本内部只用 awk 解析 /proc）

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
- 背景压力：整机 sys 态 ~40-48% 偏高，与内存耗尽（free ~10M、无 swap、kswapd 持续回收）相关，属各页共有背景

> 详细基线数据、根因分析与排查经验见 `docs/rockbase-cpu-baseline.md`。

---

## 注意事项

- **不要停服务测**（与 netspeed 不同）：本 skill 测的就是真实运行态下各页的 CPU 分布，停服务会失去对比意义
- 采样期间**不要切页**，否则测到的是切页+新页混合消耗
- 板子内存紧张时（free < 50M），内核态 CPU 会整体偏高，页间对比时留意这一背景
- 脚本在板子路径 `/root/script/cpu_page.sh`，镜像源 `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/script/cpu_page.sh`
