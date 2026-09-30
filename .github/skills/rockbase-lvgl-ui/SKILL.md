---
name: rockbase-lvgl-ui
description: 'Develop and debug Rockbase LVGL LCD UI. Covers tileview, swipe, DRM rendering, and UiModel architecture. Use when: LVGL, lcdui, LCD, tileview, 滑动, DRM, rockbase-lcdui, UI抖动, lvgl, ui refactor, screen, display.'
---

# Rockbase LVGL LCD UI 开发

完整设计见 `docs/design/rockbase-lvgl-ui-refactor-plan-20260810.md`。

---

## 产品需求

- 四个横向页面，每页有全屏底图 + 监测数据叠加
- 时间类信息 1 秒刷新
- Bitcoin/System/Network 状态 2-5 秒刷新
- 高频字段按需启用，不影响滑动
- 左右滑动必须顺滑，滑动中页面内容不应重排、闪烁或更新

---

## 当前架构

**核心原则：数据刷新和滑动合成分离。**

### UiModel

从 `/tmp/status-api.json` 和本地时间生成数据快照：

- 数据路径负责：文件 IO、JSON 解析、字段格式化、按页面判断 dirty
- 显示路径只消费已解析好的模型，不在关键显示路径做 IO 或 JSON 解析

### 页面渲染

每个页面视为独立 renderer，不再用 LVGL tileview 做实时滑动合成。

### 滑动合成

- 引入页面缓存合成器
- 300ms 吸附动画
- 触摸释放时立即 snap 到目标页面

### 开机视频（预渲染帧序列，A 方案）

开机 logo 支持播放短视频。**K230 rootfs 不装 ffmpeg/解码器**，所以采用离线预渲染：
host 端把 mp4 解码成 YUV420P 帧序列，板子开机逐帧读 → YUV→ARGB8888 → 软件旋转 →
直接写 DRM buffer commit，**零新增运行时依赖**，不碰 LVGL 主循环。

- 源视频：`/usr/share/rockbase-lcdui/boot-video-src.mp4`（用户放置）
- 预渲染产物：`/usr/share/rockbase-lcdui/boot-video/{frames.yuv,meta.txt}`
  （`frames.yuv` = 连续 YUV420P 帧，每帧 `W*H*1.5` 字节；`meta.txt` = `宽 高 帧数 fps`）
- 生成工具：`tools/gen_boot_video.py`（host 端跑，需 ffmpeg；缩放/letterbox 到 800×480，
  上限 15fps / 130 帧 ≈ 8s / ~75MB）
- 运行时：`src/boot_video.{h,cpp}`（`bootvideo::Player`）+ `main.cpp` 的 `play_boot_video()`
- **开机流程（当前）**：背光 GPIO（line 25）是 on/off 使能脚（**非 PWM 调光**）。
  背光由 `play_boot_video()` 内部管理，顺序：
  ① 先 commit 一帧**不透明黑帧**（擦掉 DSI framebuffer 里 U-Boot 残留画面，否则复位冷启动
  开背光瞬间会直接点亮残留画面而非从黑渐亮）→ ② `set_backlight(1)` 点亮（此时屏幕已黑）→
  ③ 播放循环**帧从第 0 帧就推进**，同时前 1 秒做**内容渐亮**（`kBootVideoFadeInMs=1000`，
  每帧按 `tick_ms()-start_ms` 算亮度因子 0→255 缩放逐像素 RGB，所以渐亮期间视频已经在动）→
  ④ 视频末尾 `fade_boot_video_to_black()` 渐变到黑（`kBootVideoFadeMs=600`）→
  ⑤ dashboard 从黑场淡入（`kDashboardFadeMs=500`）。
  无 `boot-video/` 时回退静态 logo 停留（此时 `main()` 才 `set_backlight(1)`）。
  注意：`fade_backlight_in()`（PWM 渐亮）代码保留但**不再调用**——硬件不支持 PWM 调光。
  渐亮用「内容亮度」而非「物理背光」实现，drmshot 抓 framebuffer 能量化验证（亮度递增 + 帧间差非零）。
- 安全上限 `kBootVideoMaxMs=10000`，防开机卡死
- **恢复静态 logo**：取消 `main()` 里 `build_boot_scene(screen)` 那行注释即可
  （`boot_layer` 非空时自动走 logo 淡出分支，视频分支被跳过）

---

## 开发规则

1. **LVGL 8.3.7** 不支持 `lv_indev_set_gesture_limit`
2. 可保持 `lv_indev_set_scroll_limit` 和 `lv_indev_set_scroll_throw`
3. `drmModeAtomicCommit` 调用前必须先 `display_wait_vsync`，否则返回 `-12`
4. 触摸释放时 snap：检测 pressed→released，立即 `lv_tileview_set_tile_by_index(...)` 基于 `lv_obj_get_scroll_x(tileview)`，同时清除 `LV_OBJ_FLAG_SCROLL_MOMENTUM`

---

## 调试注意事项

- 如果串口/SSH 正常但 LCD 停住 → 优先排查 `rockbase-lcdui` 是否卡在非正常 sleep
- 不要在显示路径里做文件 IO
- 触摸期间应延后部分状态刷新
- **直接写 DRM framebuffer 的像素字节序**：ARGB8888 内存字节序是 **B,G,R,A**（LSB 字节 = B），
  所以 `uint32_t` 值必须是 `0xAARRGGBB`（`(A<<24)|(R<<16)|(G<<8)|B`）。
  若误写成 `0xAABBGGRR`（R/B 互换），黄色会变青蓝、红色会变青色——典型症状是"颜色不对但画面正常"。
  参考 `tools/gen_world_map_image.py` 注释与 `boot_video.cpp` 的 YUV→ARGB 转换。

## UI 目视验证（截屏闭环）

改 UI（字体/布局/颜色）后，用 **`rockbase-screenshot` skill** 抓屏目视验证，
形成闭环：**改代码 → 交叉编译 → scp 部署 → 截屏 → 目视验证 → 满意后固化**。

- 工具：`drmshot` / `touchswipe`（已随 `rockbase_lcdui` 包内置镜像 `/usr/bin/`，
  源码在 `buildroot-overlay/package/rockbase_lcdui/src/`，新镜像无需交叉编译/scp）
- framebuffer 480x800 竖屏 → 本地 PIL 旋转成 800x480 横屏
- 完整命令与陷阱见 skill: `rockbase-screenshot`
- 要验证**非首页**（Stratum/System/Network）或点按钮关弹窗 → 用
  **`rockbase-swipe` skill** 注入触摸事件切页，再截屏。

---

## 开机视频更新流程

换开机视频只需两步（host 端）：

```bash
# 1. 放源视频（覆盖即可，任意分辨率/编码，工具会缩放+letterbox 到 800x480）
cp your.mp4 buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/usr/share/rockbase-lcdui/boot-video-src.mp4

# 2. 预渲染成帧序列
python3 tools/gen_boot_video.py
```

产物 `boot-video/{frames.yuv,meta.txt}` 随 rootfs overlay 打进镜像。
热部署验证时：scp `rockbase-lcdui` 二进制 + `boot-video/` 目录到板子，
`S03lcdui stop` → 替换二进制 → `S03lcdui start`，再用 `drmshot` 在开机 8s 窗口内连抓几帧确认播放与颜色。

> 注意：`boot-video-src.mp4` 是源素材，**不必**打进镜像（镜像只需 `boot-video/` 帧序列）。
> 若不想让源视频进镜像，可在 `.gitignore` 或构建前移除它。

---

## 镜像构建

`rockbase_lcdui` 是 `SITE_METHOD=local` 的本地源码包。**已根治自动重编问题**：改写了 buildroot 的 `pkg-generic.mk`，local 包源码改动后顶层 `make buildroot` 会自动 rsync 检测变化并重编，无需手动 rebuild。

```bash
make CONF=k230_rockbase_defconfig buildroot -j$(nproc)
```

（如确需强制全量重建，仍可 `make CONF=k230_rockbase_defconfig rockbase_lcdui-rebuild -j$(nproc)`。详见 `rockbase-build` skill。）

### ⚠️ 热部署后增量构建报 `cannot stat calibd` → 先 `rockbase_lcdui-rebuild`

- **症状**：热部署时用手工交叉编译只产出了 `rockbase-lcdui` 主二进制（没走包内 Makefile 的
  `all` 目标），随后 `make buildroot` 在 `rockbase_lcdui Installing to target` 阶段报
  `install: cannot stat '.../build/rockbase_lcdui/calibd'`（`drmshot`/`touchswipe` 同样缺失）。
  日志特征是：rsync 报了 `>>> ROCKBASE_LCDUI: source changed, forcing rebuild`，但**中间没有
  `>>> rockbase_lcdui Building` 阶段**就直接跳到 Installing。
- **根因**：`rockbase_lcdui` 的 install 要装 4 个二进制，手工编译只编了主二进制，build 目录
  缺 debug 工具；增量构建的 rsync 钩子删 stamp 后 "Building" 没真正重跑，形成「stamp 与产物
  不一致」的脏增量状态，install 一装 `calibd` 就炸。
- **修复**：先 `make CONF=k230_rockbase_defconfig rockbase_lcdui-rebuild -j$(nproc)` 强制重建
  完整 `all` 目标（含 `calibd`/`drmshot`/`touchswipe`），再跑 `make buildroot`。
- **预防**：热部署 lcdui 二进制时，要么在独立临时目录编译、不要污染 `output/.../build/rockbase_lcdui`，
  要么固化时统一走 `rockbase_lcdui-rebuild`，别依赖「只编主二进制 + 增量 buildroot」。

构建后校验 build 目录拷贝是否等于最新源码（md5 相同 = 已同步）：

```bash
md5sum buildroot-overlay/package/rockbase_lcdui/src/main.cpp \
        output/k230_rockbase_defconfig/build/rockbase_lcdui/main.cpp
```

注意：若当前使用非默认网络模式（如 `usb`），构建时须同时传入 `K230_NET=usb`，否则模式会重置为 `sdio_wifi`。

完整镜像烧录后，必须做网络模式互斥验收：

- `usb` 目标：`8189fs.ko` 不存在、有 `wpa_supplicant/iwconfig`
- `sdio_wifi` 目标：`8189fs.ko` 存在、`lsmod` 有 `8189fs`、有 `wlan0`、有 `wpa_supplicant/iwconfig`

`usb` 验收命令：

```bash
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
find /lib/modules -name "8189fs.ko" | head -3
[ -f /lib/modules/*/kernel/drivers/net/wireless/8189fs/8189fs.ko ] && echo "FAIL:8189fs.ko present" || echo "OK:no 8189fs.ko"
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
'
```

`sdio_wifi` 验收命令：

```bash
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
find /lib/modules -name "8189fs.ko" | head -3
lsmod | grep 8189 || true
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
[ -d /sys/class/net/wlan0 ] && echo "OK:wlan0 present" || echo "FAIL:no wlan0"
'
```

任一 `FAIL` 都先排查烧录目录后缀与目标模式是否一致（`-usb-` / `-sdio-`），再继续 UI 功能验证。
