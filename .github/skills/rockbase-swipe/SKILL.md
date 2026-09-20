---
name: rockbase-swipe
description: 'Inject touch swipe gestures into Rockbase board to navigate the LVGL tileview UI for closed-loop debugging. Use when: 滑屏, 滑动, swipe, 翻页, 切页, 切到Stratum, 切到System, 切到Network, 切到Bitcoind, 触摸, touch, touchswipe, 点按钮, 点Not Now, 关弹窗, 页面切换, tileview, 导航, 闭环调试.'
---

# Rockbase 触摸滑屏（lcdui 闭环调试）

向板子 `/dev/input/event0` 注入触摸事件，驱动 LVGL tileview 翻页 / 点按钮，
配合 `rockbase-screenshot`（截屏）形成闭环：**改代码 → 编译 → 部署 → 滑屏切页 → 截屏 → 目视验证**。

## 工具

- 源码：`buildroot-overlay/package/rockbase_lcdui/src/touchswipe.c`
- **已内置镜像**：随 `rockbase_lcdui` 包编译安装到板子 `/usr/bin/touchswipe`，
  新镜像直接 `ssh root@$BOARD '/usr/bin/touchswipe ...'` 即可，无需交叉编译/scp。
  旧镜像回退：交叉编译 + scp（见 Step 1 附录）。
- 用法：`touchswipe <x0> <y0> <x1> <y1> [steps]`
  - 坐标是 **ft5x06 物理面板坐标**（480x800 竖屏），不是 UI 横屏坐标
  - `steps` 是移动步数，步数越少速度越快（默认 10）
  - 点按 = x0==x1 且 y0==y1（单步）

## 关键坑（务必先读）

### 坑 1: 必须用 `O_RDWR` 打开 input 设备
`open("/dev/input/event0", O_WRONLY)` **写不进去**（input 子系统要求读写权限），
事件静默丢失，滑动完全无效但工具不报错。必须 `O_RDWR`。

### 坑 2: 物理坐标 ≠ UI 坐标（旋转映射）
framebuffer 是 480x800 竖屏，UI 是 800x480 横屏。lcdui 的 `touch_read_cb` 做旋转映射：
- **UI X 轴（横屏左右翻页）= 物理 Y 轴**
- **UI Y 轴（横屏上下）= 物理 X 轴（反向）**

所以 **tileview 左右翻页要沿物理 Y 方向滑动**（x 固定，y 变化）：
- 物理 Y **减小**（如 700→100）= UI 向左滑 = **下一页**（Bitcoind→Stratum→System→Network）
- 物理 Y **增大**（如 100→700）= UI 向右滑 = **上一页**

### 坑 3: 页面顺序（col 索引）
`build_tile_stage` 里 tile 顺序固定：
| col | 页面 |
|-----|------|
| 0 | Bitcoind |
| 1 | Stratum |
| 2 | System |
| 3 | Network |

重启后默认停在 col 0（Bitcoind）。

### 坑 4: 重启后 UI 有启动画面，滑动无效
lcdui 启动后先显示 Bitcoin 硬币 + 版本号的启动画面（~8-10s），
此时 tileview 还没建好，滑动会被忽略。**重启后至少 sleep 12s 再滑。**

### 坑 5: OTA 更新弹窗会挡住页面
lcdui 启动 60s 后（`kFirstCheckDelayMs`）若检测到新版本会弹 "Update Available" 模态框。
规避方法：
- 重启前 `rm -f /run/rockbase/ota-check-state.json`（删掉 check-async 写的状态文件）
- 在 60s 内完成截屏
- 或已弹出时点 "Not Now" 关掉（见下方点按钮）

## 标准流程

### Step 1: 确认工具可用（新镜像已内置 /usr/bin/touchswipe）

```bash
BOARD=192.168.124.9
ssh -o ConnectTimeout=5 root@$BOARD 'command -v touchswipe || ls /usr/bin/touchswipe'
```

旧镜像没有时回退：

```bash
cd /home/bitpony/k230_linux_sdk
CC=output/k230_rockbase_defconfig/host/bin/riscv64-unknown-linux-gnu-gcc
$CC -mcpu=c908v -mabi=lp64d -O2 \
    buildroot-overlay/package/rockbase_lcdui/src/touchswipe.c -o /tmp/touchswipe
scp -o StrictHostKeyChecking=no /tmp/touchswipe root@$BOARD:/tmp/touchswipe
ssh -o ConnectTimeout=5 root@$BOARD 'chmod +x /tmp/touchswipe'
```

### Step 2: 滑动切页 + 截屏（配合 rockbase-screenshot）

从 Bitcoind(col0) 切到 Stratum(col1)：物理 Y 减小（下一页）

```bash
BOARD=192.168.124.9
ssh -o ConnectTimeout=5 root@$BOARD '/usr/bin/touchswipe 240 700 240 100 10; sleep 2; /usr/bin/drmshot /tmp/screen.ppm'
scp -o StrictHostKeyChecking=no root@$BOARD:/tmp/screen.ppm /tmp/screen.ppm
python3 -c "
from PIL import Image
Image.open('/tmp/screen.ppm').rotate(-90, expand=True).rotate(180).save('/tmp/screen.png')
print('OK')
"
```

切到 System(col2)：从 Bitcoind 连续两次「下一页」，或从 Stratum 再滑一次。
切回上一页：物理 Y 增大（如 `240 100 240 700 10`）。

### Step 3: 点按钮（如关弹窗）

点按 = 起点终点相同。"Not Now" 按钮在 UI 约 (184, 280) 物理坐标：

```bash
ssh -o ConnectTimeout=5 root@$BOARD '/usr/bin/touchswipe 184 280 184 280 1; sleep 1'
```

> 按钮物理坐标需先用截屏 + 目视估算，点不准就微调 x/y 再试。

## 一键：重启 + 切到 Stratum + 截屏

```bash
cd /home/bitpony/k230_linux_sdk
BOARD=192.168.124.9
PID=$(ssh -o ConnectTimeout=5 root@$BOARD 'for d in /proc/[0-9]*; do exe=$(readlink $d/exe 2>/dev/null); case "$exe" in *lcdui*) basename $d;; esac; done')
ssh -o ConnectTimeout=5 root@$BOARD "kill $PID; sleep 1; rm -f /run/rockbase/ota-check-state.json; /usr/bin/rockbase-lcdui >/tmp/lcdui.log 2>&1 &"
sleep 12
ssh -o ConnectTimeout=5 root@$BOARD '/usr/bin/touchswipe 240 700 240 100 10; sleep 2; /usr/bin/drmshot /tmp/screen.ppm'
scp -o StrictHostKeyChecking=no root@$BOARD:/tmp/screen.ppm /tmp/screen.ppm
python3 -c "
from PIL import Image
Image.open('/tmp/screen.ppm').rotate(-90, expand=True).rotate(180).save('/tmp/screen.png')
print('OK')
"
```

> 板子无 `pkill`/`pgrep`，用 `/proc/*/exe` readlink 匹配 `*lcdui*` 找 PID。
