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
| Kernel patch 变更 | 只要改了 `buildroot-overlay/board/canaan/k230_rockbase/patches/linux/`，每个目标 defconfig 都必须先执行一次 `linux-dirclean` 再重编 |

## Kernel Patch 变更规则（重要）

`1G` 和 `2G` 各自有独立的 `output/<CONF>/build/linux-*` 目录。

这意味着：

- 你在源码里禁用/新增/修改了 `patches/linux/*.patch`，**不会**自动让另一个 defconfig 的旧内核目录失效
- 如果不先 `linux-dirclean`，Buildroot 可能直接复用旧的已打 patch 内核产物
- 结果就是：源码已经变了，但新导出的镜像仍然带着旧 kernel patch 行为

### 什么时候必须 `linux-dirclean`

- 改了 `patches/linux/` 下任意 patch
- 预期 kernel 行为应变化，例如 `PIO ↔ DMA`、MMC/SDHCI/IRQ/DT 相关修改
- 需要给 `1G`、`2G` 都出镜像，且这些镜像必须口径一致

### 正确做法

1G：

```bash
make CONF=k230_rockbase_1g_defconfig linux-dirclean
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

2G：

```bash
make CONF=k230_rockbase_2g_defconfig linux-dirclean
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

### 本次踩坑案例

- 已在源码中禁用 `0034-rockbase-sdhci-force-pio-dma-diagnostic.patch`
- 1G 因为做过 `linux-dirclean`，重编后确实恢复 DMA
- 2G 第一次直接 `make buildroot` 很快完成，但实际复用了旧的 `output/k230_rockbase_2g_defconfig/build/linux-*`
- 因此第一次导出的 2G 镜像仍然是旧的 PIO kernel，不是预期中的 DMA 版本

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

## 构建后验证（重要）

如果本次目标是验证 kernel patch 是否真的进镜像，构建后至少做一项源码侧检查和一项实机侧检查。

### 源码侧检查

例如检查构建后的 `sdhci-of-kendryte.c`：

```bash
sed -n '292,310p' output/k230_rockbase_1g_defconfig/build/linux-*/drivers/mmc/host/sdhci-of-kendryte.c
sed -n '292,310p' output/k230_rockbase_2g_defconfig/build/linux-*/drivers/mmc/host/sdhci-of-kendryte.c
```

判读：

- 看到 `SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_BROKEN_DMA` → 还是 PIO 诊断态
- 只剩 `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN` → DMA 已恢复

### 实机侧检查

烧录后看启动日志：

```bash
ssh root@<IP> 'dmesg | grep -iE "mmc[01]: SDHCI controller|using PIO|using ADMA|using DMA"'
```

不要只看源码目录或 commit 信息就默认镜像行为已经变化。

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
- 共享 `patches/linux/` 改动后，如果没对对应 defconfig 先做 `linux-dirclean`，镜像可能静默复用旧 kernel
- LVGL 8.3.7 不支持 `lv_indev_set_gesture_limit`
- `rockbase_lcdui` 调用 `drmModeAtomicCommit` 前需要 `display_wait_vsync`
- rootfs 基线为 2G（非 6G）
- 默认导出后 WSL 侧大镜像会被自动删除（除非 `K230_KEEP_WSL_IMAGES=1`）

---

## 种子 (Seed) 镜像

Seed 镜像构建见 `rockbase-seed` skill。除非用户明确说"带 seed"，不触发 seed 流程。

## 板子信息

当前板子、MMC 拓扑等常驻信息见 `AGENTS.md`。
