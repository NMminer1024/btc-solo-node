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

---

## 镜像构建

`rockbase_lcdui` 是 `SITE_METHOD=local` 的本地源码包，buildroot 增量构建**不会**自动重编它——改了源码后只跑顶层 `make buildroot` 会打包旧二进制。**构建镜像前必须单独重建：**

```bash
make CONF=k230_rockbase_defconfig rockbase_lcdui-rebuild -j$(nproc)
```

然后再构建完整镜像。详见 `rockbase-build` skill。

构建后校验 build 目录拷贝是否等于最新源码（md5 不同 = 未重编，需 rebuild）：

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
