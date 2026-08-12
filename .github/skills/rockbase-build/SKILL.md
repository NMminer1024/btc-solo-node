---
name: rockbase-build
description: 'Build Rockbase images and export to Windows. Use when: 构建镜像, 编译, make buildroot, rebuild, export image, 导出镜像, 打进镜像, LVGL rebuild, 镜像构建, compile image.'
---

# Rockbase 镜像构建与导出

## 构建规则总览

| 规则 | 说明 |
|------|------|
| 默认非 seed | 除非用户明确说"带 seed 镜像"，否则永远构建非 seed 最小镜像 |
| LVGL UI 重建 | 任何 LVGL/`rockbase_lcdui` 改动后，必须用 `rockbase_lcdui-rebuild` 单独重建 |
| WSL 导出 | 使用 `post-image.sh` 自动导出到 `/mnt/d/code/pony/IoT/k230/img/` |
| 构建前准备 | 非 seed 构建需要 `mkdir -p /tmp/rockbase-empty-seed` |

## 默认构建命令

### 1G 非 seed 最小镜像（默认）

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

### 2G 非 seed 最小镜像（默认）

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

### 指定导出目录

在以上命令末尾追加 `K230_IMAGE_OUT_DIR=/your/path`。

### 保留 WSL 侧镜像

追加 `K230_KEEP_WSL_IMAGES=1`。

### 手动复制已有镜像到 Windows

```bash
make CONF=k230_rockbase_1g_defconfig copy-img IMG_OUTPUT_DIR=/mnt/d/code/pony/IoT/k230/img
```

---

## LVGL UI 重建规则（重要）

任何 `rockbase_lcdui`/LVGL UI 源码改动后，**必须**在构建镜像前单独重建：

```bash
make CONF=k230_rockbase_1g_defconfig rockbase_lcdui-rebuild
make CONF=k230_rockbase_2g_defconfig rockbase_lcdui-rebuild
```

**不要**依赖后续的顶层 `buildroot` 命令去自动发现本地包 UI 改动。

---

## 镜像产物说明

| 产物 | 路径 |
|------|------|
| 构建中间文件 | `output/<CONF>/images/blockdata.ext4`、`sysimage-sdcard.img` |
| Windows 导出 | `/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-{1g,2g}-<timestamp>/sysimage-sdcard.img` |

非 seed 镜像预期大小：`~2.4G ~ 2.6G`（rootfs 2G + blockdata 256M + 分区开销）。

如果镜像约 32G，说明走到了 seed 路径或默认 24G blockdata，需要加 `K230_BLOCKDATA_SIZE=256M` 重新构建。

---

## 编译陷阱

参考 `docs/rockbase-project-overview.md` 和 `/memories/repo/build-notes.md`：

- WSL 下 Buildroot 可能拒绝含 Windows 路径的 PATH，需要清理 PATH
- WiFi 驱动 `8189fs.ko` 可能残留旧模块，必要时 `rtl8189fs-dirclean rtl8189fs`
- LVGL 8.3.7 不支持 `lv_indev_set_gesture_limit`
- `rockbase_lcdui` 调用 `drmModeAtomicCommit` 前需要 `display_wait_vsync`
- rootfs 基线为 2G（非 6G）
- 默认导出后 WSL 侧大镜像会被自动删除（除非 `K230_KEEP_WSL_IMAGES=1`）

---

## 种子 (Seed) 镜像

Seed 镜像构建见 `rockbase-seed` skill。除非用户明确说"带 seed"，不触发 seed 流程。

## 板子信息

当前板子、MMC 拓扑等常驻信息见 `AGENTS.md`。
