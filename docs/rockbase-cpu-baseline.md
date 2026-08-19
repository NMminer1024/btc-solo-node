# Rockbase CPU 压力基线

> 本文档记录 Rockbase 板子各 UI 页面的 CPU 压力分布基线，供后续 agent 对比参考。
> 测试方法见 skill: `rockbase-cpupress`（`.github/skills/rockbase-cpupress/SKILL.md`）。
> 测试脚本: 板子 `/root/script/cpu_page.sh`，镜像源 `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/script/cpu_page.sh`。

## 测试方法

- 命令: `ssh root@<IP> 'sh /root/script/cpu_page.sh <页面名> 60'`
- 原理: 首尾各拍一次全进程 CPU ticks 快照（/proc/[pid]/stat 的 utime+stime），除以 /proc/stat 总 ticks 增量，得到窗口内每个进程的 CPU%
- 条件: 服务全开（bitcoind + ckpool + statusd + lcdui 等），UI 稳定停留在目标页面 60s
- 板子为**单核 RISC-V**，CPU% 即占该核百分比

## 基线数据（2026-08-19 实测）

**测试板**: `192.168.124.15`（1G 版，mmc0=SDIO WiFi, mmc1=SD card）
**系统状态**: 开机 ~2h，bitcoind 运行中，内存紧张（free ~10M，无 swap，kswapd 持续回收）

### 修复前（呼吸动画启用，首次基线）

| 页面 | 整机 CPU | **rockbase-lcdui** | bitcoind | ckpool | statusd | 内核态合计 |
|------|---------|--------------------|----------|--------|---------|-----------|
| Dashboard | 77.4% | 7.3% | 4.2% | 2.2% | 1.4% | ~3.2% |
| **Stratum** | **97.5%** | **34.7%** | 4.0% | 2.4% | 1.3% | ~2.5% |
| System | 81.1% | 6.7% | 4.9% | 6.2% | 1.3% | ~3.5% |
| Network | 87.0% | 6.8% | 9.4% | 2.2% | 1.4% | ~3.2% |

### 修复后（呼吸动画禁用，2026-08-19 复测）

| 配置 | 整机 CPU | **rockbase-lcdui** | bitcoind | ckpool | 判定 |
|------|---------|--------------------|----------|--------|------|
| 触摸 10ms + 睡眠上限 10ms | **73.4%** | **3.6%** | 2.8% | 2.7% | ✅ 已修复 |
| 触摸 20ms + 睡眠上限 20ms | **71.3%** | **3.1%** | 2.5% | 1.5% | ✅ 已修复（最终采用） |

（RSS 参考: rockbase-lcdui ~26MB, bitcoind ~620-650MB, ckpool ~150-170MB）
（当前源码参数: `kTouchReadPeriodMs=20`（50Hz）、`kMaxLvSleepUs=20000`、`kMinLvSleepUs=500`）

## 关键结论

1. **Stratum 页 CPU 热点根因 = 状态点无限呼吸动画，非图表刷新**（2026-08-19 定位并修复）：
   - 初始假设是图表（`kStratumChartMaxPoints=500` 下采样）刷新过频，但实测图表仅 32 点 × 3 序列、5s 才刷新一次，不是热点
   - 真凶是 `start_status_dot_breathing()`：Stratum 在线时启动 `LV_ANIM_REPEAT_INFINITE` 无限动画（1s 周期，LVGL anim timer 每 10ms 一帧），每帧执行 `std::cos` 双精度浮点（RISC-V 软件实现）并修改 dot 透明度 → 持续 invalidate + 重绘，即使切到其他页动画仍在跑
   - 修复：`main.cpp` 中呼吸动画整体 `#if 0` 禁用，在线时显示**静态绿点**（`set_stratum_state_visual` 里 `state_class=="ok"` 分支）；离线仍为红色静态点，语义不变（绿=在线/红=离线）
   - 效果：Stratum 页 lcdui CPU **34.7% → 3.6%**（降 9.6 倍），整机 97.5% → 73.4%，与其余页面同量级
2. **其他页 UI 进程稳定在 6.7%~7.3%**，页间差异可忽略。
3. **背景进程页间基本一致**：bitcoind 4%~9%、ckpool 2%~6% 的波动是自身活动噪声，与页面无关。整机 CPU 的页间差异几乎全部来自 `rockbase-lcdui`。
4. **背景压力（各页共有）**：
   - 单核 load average 长期 2.5~5（严重过载）
   - sys 态 CPU ~40-48% 偏高，根因是内存耗尽（total 945M，free ~10M，无 swap）→ kswapd 持续页面回收（vmstat: pgsteal_kswapd ~1.1M）
   - bitcoind RSS ~640M + ckpool ~150M 占满 1G 内存的大头

## 排查经验（供后续参考）

- **UI 页 CPU 异常高 → 先查无限动画**：`grep -n "LV_ANIM_REPEAT_INFINITE" main.cpp`，LVGL 无限动画即使页面被裁剪/切走，exec_cb 仍每 10ms 执行
- **注意 RISC-V 无硬件浮点**：动画 exec_cb 里的 `std::cos`/`std::sin` 等双精度浮点非常昂贵，应避免在每帧回调里使用
- **busybox top 列对齐问题**：板子上 `top -bn1` 是 busybox 版，进程 CPU% 列需要按 `/proc/[pid]/stat` 快照差值精确计算（`cpu_page.sh` 已封装），不要直接解析 top 输出列
- **触摸轮询 1ms 空转**：`lv_timer_set_period(indev_read_timer, 1)` 会让 `lv_timer_handler()` 永远返回 ≤1ms，主循环 sleep 被锁死在 0.5~2ms，空闲空转烧 CPU；先改为 10ms（100Hz）再降到 **20ms（50Hz）**（`kTouchReadPeriodMs`），主循环睡眠上限相应放宽到 `kMaxLvSleepUs=20000`（20ms）。此项对 Stratum 热点无贡献，但对空闲 CPU 有益
- **触摸降频的边际收益**：20ms/20ms vs 10ms/10ms，Stratum 页 lcdui CPU 仅 3.6% → 3.1%（-0.5pp），整机 73.4% → 71.3%。**注意代价**：`kMaxLvSleepUs=20000` 把 LVGL 动画帧率上限压到 ~50fps（`kPagerSnapAnimMs=300` snap 动画约 15 帧），快速滑动手感略钝。收益小但无碍，已作为最终参数固化

## 复测指引（给后续 agent）

- 优化 Stratum 页后复测：`ssh root@192.168.124.15 'sh /root/script/cpu_page.sh Stratum 60'`
- **达标标准**：Stratum 页 `rockbase-lcdui` CPU 与其他页同量级（~7% 内），整机 CPU ~75-80%
- 注意：整机绝对值受内存压力/IBD 状态影响会漂移，**页间相对差异（UI 进程 CPU）才是有效对比指标**
- 若板子内存状况变化（如加了 swap 或降了 bitcoind 内存），内核态背景 CPU 会变化，整机绝对值需重新校准
- 若后续要恢复呼吸动画：采用"慢速呼吸"折中方案（duration 1000→4000ms 以上 + 仅 Stratum 页激活时运行 + 动画 exec_cb 去掉浮点三角函数改用整数查表）

## 历史

| 日期 | 事件 |
|------|------|
| 2026-08-19 | 首次基线：4 页测试，发现 Stratum 页 UI 进程 CPU 34.7%（×4.7 异常），初始怀疑图表刷新热点 |
| 2026-08-19 | 定位根因：Stratum 状态点无限呼吸动画（`LV_ANIM_REPEAT_INFINITE`），`#if 0` 禁用改静态绿点；复测 Stratum 页 lcdui CPU 降至 3.6%，整机 73.4% ✅ |
| 2026-08-19 | 触摸降频 20ms/20ms（`kTouchReadPeriodMs=20`, `kMaxLvSleepUs=20000`）：lcdui 3.6% → 3.1%，整机 73.4% → 71.3%；收益边际但无副作用，固化提交 |
