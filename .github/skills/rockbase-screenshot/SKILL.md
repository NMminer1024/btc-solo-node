---
name: rockbase-screenshot
description: 'Capture Rockbase board LCD screen via DRM framebuffer for closed-loop lcdui UI debugging. Use when: 截屏, 截图, screenshot, 抓屏, 看屏幕, 屏幕效果, UI验证, 目视验证, 看UI, 页面效果, 字体效果, 布局验证, drmshot, framebuffer, 闭环调试.'
---

# Rockbase LCD 截屏（lcdui 闭环调试）

从板子 DRM framebuffer 抓屏，本地旋转后目视验证 UI 效果。
配合 `rockbase-hotfix`（编译部署）形成闭环：**改代码 → 编译 → 部署 → 截屏 → 目视验证**。

## 工具

- 源码：`buildroot-overlay/package/rockbase_lcdui/src/drmshot.c`
- **已内置镜像**：随 `rockbase_lcdui` 包编译安装到板子 `/usr/bin/drmshot`，
  新镜像直接 `ssh root@$BOARD '/usr/bin/drmshot /tmp/screen.ppm'` 即可，无需交叉编译/scp。
- 依赖：板子已有 libdrm（lcdui 本身就链它），无需额外部署库
- 旧镜像（无 /usr/bin/drmshot）回退方案：交叉编译 + scp（见文末附录）

## 标准流程

### Step 1: 截屏（新镜像，工具已内置）

```bash
BOARD=192.168.124.9   # 目标板
ssh -o ConnectTimeout=5 root@$BOARD '/usr/bin/drmshot /tmp/screen.ppm'
```

若报 `not found`（旧镜像），用文末附录的回退流程。

输出 `fb: 480x800 stride=1920` 即成功（~1.1MB PPM）。

### Step 2: 拉回本地 + 旋转成横屏

framebuffer 是 **480x800 竖屏**，UI 是 **800x480 横屏**（显示时旋转）。
正方向 = `rotate(-90, expand=True)` 再 `rotate(180)`：

```bash
scp -o StrictHostKeyChecking=no root@$BOARD:/tmp/screen.ppm /tmp/screen_raw.ppm
python3 -c "
from PIL import Image
im = Image.open('/tmp/screen_raw.ppm')
im.rotate(-90, expand=True).rotate(180).save('/tmp/screen_final.png')
print('OK', im.size)
"
```

然后用 `view_image` 查看 `/tmp/screen_final.png`。

### Step 3: 目视验证

对照改动点检查：字体/颜色/布局/裁切/闪烁痕迹。
发现问题 → 回 `rockbase-hotfix` 流程改代码重部署，再截屏，直到满意。

## 一键脚本（可整段复制）

```bash
BOARD=192.168.124.9
ssh -o ConnectTimeout=5 root@$BOARD '/usr/bin/drmshot /tmp/screen.ppm' \
  && scp -o StrictHostKeyChecking=no root@$BOARD:/tmp/screen.ppm /tmp/screen_raw.ppm \
  && python3 -c "
from PIL import Image
Image.open('/tmp/screen_raw.ppm').rotate(-90, expand=True).rotate(180).save('/tmp/screen_final.png')
print('OK -> /tmp/screen_final.png')
"
```

## 附录：旧镜像回退（无 /usr/bin/drmshot 时）

```bash
cd /home/bitpony/k230_linux_sdk
CC=output/k230_rockbase_defconfig/host/bin/riscv64-unknown-linux-gnu-gcc
SYSROOT=output/k230_rockbase_defconfig/host/riscv64-buildroot-linux-gnu/sysroot
$CC -mcpu=c908v -mabi=lp64d -O2 -I$SYSROOT/usr/include/drm \
    buildroot-overlay/package/rockbase_lcdui/src/drmshot.c -o /tmp/drmshot -ldrm \
  && scp -o StrictHostKeyChecking=no /tmp/drmshot root@$BOARD:/tmp/drmshot \
  && ssh -o ConnectTimeout=5 root@$BOARD 'chmod +x /tmp/drmshot'
```

⚠️ 必须加 `-I$SYSROOT/usr/include/drm`：sysroot 顶层的 `xf86drm.h`
`#include <drm.h>`，而 `drm.h` 在 `drm/` 子目录里。

## ⚠️ 陷阱（踩坑记录）

### ⚠️ mmap 直接对 card0 fd 用 GEM handle 偏移会 EINVAL
- **症状**：`mmap: Invalid argument`
- **根因**：GEM 对象的 mmap 偏移必须经内核 ioctl 换取；且本 sysroot 的
  `drm.h` **没有标准 `drm_gem_mmap` 结构体**（`DRM_IOCTL_GEM_MMAP` 不可用）。
- **修复**：走 `DRM_IOCTL_PRIME_HANDLE_TO_FD` 把 handle 转成 prime fd，
  直接 `mmap(prime_fd, 0)`。display 库（`package/display/src/display.c`）
  也是 prime fd 路线。

### ⚠️ drmModeFB2 字段是数组
- **症状**：`error: 'drmModeFB2' has no member named 'pitch'`
- **根因**：新版 libdrm 的 `drmModeFB2` 用 `pitches[]`/`offsets[]`/`handles[]` 数组。
- **修复**：`fb->pitches[0]`、`fb->offsets[0]`、`fb->handles[0]`。

### ⚠️ 旋转方向
- framebuffer 竖屏 480x800 → 横屏 800x480 需要 90° 旋转，但**方向取决于
  面板安装方向**。Rockbase 实测：`rotate(-90, expand=True)` 得到 180° 颠倒的图，
  再 `rotate(180)` 才是正方向。
- 若某块板子方向不对，试 `rotate(90, expand=True)`（不加 180）。

### ⚠️ 截屏时机
- 截到的是**当前显示帧**。lcdui 有页面缓存合成器，滑动动画中途截屏可能抓到
  中间帧；等页面静止（~300ms 吸附动画结束）再截。
- 截屏不干扰显示（只读 mmap），可反复抓。

## 已知事实

- DRM 设备：`/dev/dri/card0`，驱动 `canaan-drm`，connector `card0-DSI-1`
- framebuffer：480x800 ARGB8888（stride=1920），UI 逻辑分辨率 800x480
- 板子无 python3/gcc/fbgrab，drmshot 必须本地交叉编译后 scp
- 本地需要 PIL（`python3 -c "import PIL"` 验证）

## 相关 skill

- 需要**切页/点按钮**再截屏（如验证 Stratum/System 页）→ 用 `rockbase-swipe`
  注入触摸事件，再回到本 skill 截屏。
