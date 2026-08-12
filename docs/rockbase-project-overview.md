# Rockbase 项目基础信息

## 产品形态

Rockbase 是基于 K230 的 Bitcoin 剪枝验证节点与本地 Stratum/solo mining 设备。

主要运行组件：

- `bitcoind`
- `ckpool`
- `rockbase-lcdui`
- `rockbase_status`
- `rockbase_serial_healthd.sh`
- `8189fs` RTL8189FS SDIO WiFi 驱动

## 板子基线

当前 4 台真实板：

- `192.168.124.8`：1G Rockbase 真板。
- `192.168.124.15`：1G Rockbase 真板。
- `192.168.124.21`：1G Rockbase 真板。
- `192.168.124.23`：2G Rockbase 真板。

重要 MMC host 拓扑：

- 1G / `124.8`、`124.15`、`124.21`：`mmc0=SDIO WiFi`，`mmc1=SD 卡`。
- 2G / `124.23`：`mmc0=SD 卡`，`mmc1=SDIO WiFi`。

这个编号差异非常重要。分析日志时不要固定假设 `mmc0` 或 `mmc1`，必须结合当次启动日志确认哪个 host 是 WiFi SDIO。

## 构建基线

主要 defconfig：

- 1G：`k230_rockbase_1g_defconfig`
- 2G：`k230_rockbase_2g_defconfig`

rootfs 容量基线：

- `BR2_TARGET_ROOTFS_EXT2_SIZE="2G"`（1G/2G 两个 Rockbase defconfig 已统一）

当前诊断状态：

- 最新提交 `cc6410e5` 只让 RTL8189FS SDIO bus 自恢复。
- 当前镜像仍处于 **PIO 诊断态**，`0034-rockbase-sdhci-force-pio-dma-diagnostic.patch` 仍在生效，**DMA 没有恢复**。

主要源码路径：

- Rootfs overlay：`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay`
- Web 根目录：`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web`
- Rockbase post-build：`buildroot-overlay/board/canaan/k230_rockbase/post-build.sh`
- Rockbase post-image：`buildroot-overlay/board/canaan/k230_rockbase/post-image.sh`
- Linux fragment：`buildroot-overlay/board/canaan/k230_rockbase/fragment/linux.fragment`
- Linux DTS patch：`buildroot-overlay/board/canaan/k230_rockbase/patches/linux/0028-add-rockbase-linux-dts.patch`

## 镜像规则

默认构建不带 seed 的最小镜像。

只有用户明确说“带 seed 镜像”时，才允许构建/导出 seed-backed 镜像。带 seed 镜像很大、构建和导出都很慢，当前卡死排查默认不走这条路径。

默认非 seed 参数：

```bash
BLOCK_SEED_DIR=/tmp/rockbase-empty-seed
K230_BLOCKDATA_SIZE=256M
K230_IMAGE_GZIP=0
```

非 seed 镜像预期：

- 总镜像约 `2.4G ~ 2.6G`（rootfs 2G + blockdata 256M + boot/分区开销）。
- `blockdata.ext4` 应为 `256M`。

如果镜像约 `32G`，通常说明走到了默认 `24G` blockdata 或类似 seed 路径，需要用 `K230_BLOCKDATA_SIZE=256M` 重新构建。

默认 1G 构建命令：

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

默认 2G 构建命令：

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

## 镜像导出方法

Rockbase 的 `post-image.sh` 会在镜像生成后自动导出：

- 默认导出根目录：`/mnt/d/code/pony/IoT/k230/img`
- 1G 导出目录前缀：`sysimage-sdcard-rockbase-1g-<build_stamp>`
- 2G 导出目录前缀：`sysimage-sdcard-rockbase-2g-<build_stamp>`
- 最终文件名：`sysimage-sdcard.img`

示例输出：

```text
/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-1g-20260811-104352/sysimage-sdcard.img
/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-2g-20260811-104909/sysimage-sdcard.img
```

如果要指定导出目录：

```bash
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0 \
  K230_IMAGE_OUT_DIR=/mnt/d/code/pony/IoT/k230/img
```

默认 `K230_KEEP_WSL_IMAGES` 不是 `1` 时，导出完成后会删除 WSL 侧的 `sysimage-sdcard.img` 和 `blockdata.ext4`，只保留 Windows 侧导出文件和必要中间产物。

如果想保留 WSL 侧镜像用于调试：

```bash
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0 \
  K230_KEEP_WSL_IMAGES=1
```

也可以在已有 WSL 侧镜像时使用 `copy-img` 手动复制：

```bash
make CONF=k230_rockbase_1g_defconfig copy-img IMG_OUTPUT_DIR=/mnt/d/code/pony/IoT/k230/img
```

但常规路径优先使用 `post-image.sh` 自动导出。

## Seed 镜像方法

默认不要构建 seed 镜像。

只有用户明确要求“带 seed 镜像”时，才使用 `seed/root-block` 或显式 `BLOCK_SEED_DIR=/path/to/seed`。

seed 目录最低要求：

```text
seed/root-block/
  blocks/
  blocks/index/
  chainstate/
```

构建时：

- `post-build.sh` 负责校验 seed 目录和 LevelDB manifest。
- `post-image.sh` 用 seed 目录生成 `blockdata.ext4`。
- 运行时 `S59blockdata` 将 blockdata 分区挂载到 `/root/block`。
- `bitcoind` 的 datadir 是 `/root/block`。

带 seed 构建时必须让 `K230_BLOCKDATA_SIZE` 足够容纳 seed，例如 `24G` 级别；否则 `mkfs.ext4 -d` 会失败或镜像空间不足。

seed 镜像示例：

```bash
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=seed/root-block \
  K230_BLOCKDATA_SIZE=24G \
  K230_IMAGE_GZIP=0
```

seed 维护细节见：

- `docs/rockbase-block-seed-image-workflow.md`

## Windows 导出规则

不要只根据沙箱里的报错就判断 `/mnt/d` 是只读。

历史上遇到过一种误判：Codex 沙箱内访问 `/mnt/d` 可能表现为只读或写失败，但真实 WSL 会话里的 `D:` 挂载实际是 `rw`。因此判断导出是否真的被 Windows 侧只读挂载阻塞前，必须用提权/非沙箱方式做真实检查：

```bash
findmnt -no SOURCE,TARGET,FSTYPE,OPTIONS /mnt/d
touch /mnt/d/__wsl_write_test && rm /mnt/d/__wsl_write_test
```

判断规则：

- `findmnt` 显示 `rw` 且 `touch` 成功：不是 Windows 挂载只读问题，后续导出失败应继续查路径、权限、文件占用或命令上下文。
- 只有 `findmnt` 明确显示 `ro`，或者真实 `touch` 失败，才认为 `/mnt/d` 当前不可写。
- 如果普通 Codex 命令和真实 WSL 检查结论冲突，以提权/非沙箱检查为准。

处理方法：

- 首选继续导出到 `/mnt/d/code/pony/IoT/k230/img`，必要时让 Codex 命令提权执行。
- 如果真实 `/mnt/d` 也不可写，先修复 Windows/WSL 挂载，再导出。
- 如果急需保留产物，可以临时设置 `K230_IMAGE_OUT_DIR=/tmp/k230-img-export` 输出到 WSL 侧，等 `/mnt/d` 恢复可写后再复制。

## 热调试规则

UI、页面、业务逻辑改动：

- 先热部署到真板。
- 在真板上验证。
- 验证通过后，再固化到镜像源码。
- 只有用户明确要求编译/输出镜像时，才编译导出。

内核、rootfs 启动路径改动：

- 能热部署就先热部署。
- 不能热部署时，才固化后编译测试镜像。

## 当前已验证镜像版本

2026-08-12 的非 seed 镜像已经在真板验证：

- 1G / `124.8`、`124.15`、`124.21`：`sdk:v0.1.0-20260812-143609-bitpony-DESKTOP-KC5S6JM-57fc4982-nncase0.0.0`
- 2G / `124.23`：`sdk:v0.1.0-20260812-143716-bitpony-DESKTOP-KC5S6JM-57fc4982-nncase0.0.0`

两者都包含 `pstore/ramoops`、`S02pstore` 和 panic/hung-task 相关 sysctl 设置；当前仍是 PIO 诊断态，DMA 未恢复。
