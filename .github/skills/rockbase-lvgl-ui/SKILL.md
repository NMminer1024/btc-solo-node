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
