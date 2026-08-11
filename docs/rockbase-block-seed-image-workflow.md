# Rockbase Block Seed 镜像流程

## 文档目的

本文档说明如何把外部 Bitcoin 剪枝数据 seed 打进 Rockbase 最终 `sysimage-sdcard.img`。

默认构建不使用 seed。只有用户明确要求“带 seed 镜像”时才使用本文档。

## 默认 seed 路径

默认路径：

```text
seed/root-block
```

也可以在单次构建时覆盖：

```bash
BLOCK_SEED_DIR=/your/path
ROCKBASE_BLOCK_SEED_DIR=/your/path
```

优先级：

- `BLOCK_SEED_DIR` 最高。
- 其次是 `ROCKBASE_BLOCK_SEED_DIR`。
- 最后才是默认 `seed/root-block`。

## seed 目录要求

seed 目录至少包含：

```text
seed/root-block/
  blocks/
  blocks/index/
  chainstate/
```

构建脚本会校验：

- `chainstate/CURRENT`
- `chainstate/<MANIFEST>`
- `blocks/index/CURRENT`
- `blocks/index/<MANIFEST>`

这些文件缺失时，构建会失败，避免把损坏的 LevelDB seed 打进镜像。

运行时垃圾文件不应该保留在 seed 中，例如：

- `.lock`
- `.cookie`
- `bitcoind.pid`
- `LOCK`

## 当前构建行为

当前代码路径：

- `post-build.sh`：检查 seed 是否存在、是否为空、是否包含必须目录和 LevelDB manifest。
- `post-image.sh`：用 seed 目录生成 `blockdata.ext4`。
- `genimage.cfg`：把 `blockdata.ext4` 放入最终 SD 卡镜像的 blockdata 分区。
- `S59blockdata`：设备启动后把 blockdata 分区挂载到 `/root/block`。
- `bitcoind`：使用 `/root/block` 作为 datadir。

重要说明：

- seed 数据不是直接塞进 rootfs。
- seed 数据进入独立的 `blockdata.ext4` 分区。
- 设备运行时通过挂载 blockdata 分区得到 `/root/block`。

## 非 seed 最小镜像

调试卡死问题时默认使用非 seed 最小镜像：

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

2G：

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

预期结果：

- `block seed: skip (directory is empty: /tmp/rockbase-empty-seed)`
- `blockdata: generating empty ... (256M)`
- 导出的 `sysimage-sdcard.img` 约 `6.4G`

## 带 seed 镜像

带 seed 构建示例：

```bash
make CONF=k230_rockbase_defconfig buildroot \
  BLOCK_SEED_DIR=seed/root-block \
  K230_BLOCKDATA_SIZE=24G \
  K230_IMAGE_GZIP=0
```

2G：

```bash
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=seed/root-block \
  K230_BLOCKDATA_SIZE=24G \
  K230_IMAGE_GZIP=0
```

容量规则：

- `K230_BLOCKDATA_SIZE` 必须大于 seed 数据实际大小。
- 对 `~17G` seed，建议从 `24G` 开始，不要卡得太紧。
- seed 镜像会显著变大，导出和烧录时间都会增加。

## 从 SD 卡更新 seed 的建议流程

推荐使用脚本，不要在构建过程中手工直接改 `seed/root-block`。

辅助脚本：

- `tools/init-rockbase-seed.sh`
- `tools/copy-rockbase-seed-from-sd.sh`
- `tools/attach-sd-to-wsl.ps1`
- `tools/mount-sdcard-in-wsl.sh`
- `tools/umount-sdcard-in-wsl.sh`
- `tools/detach-sd-from-wsl.ps1`

通用流程：

1. 在源设备上优雅停止 `bitcoind`。
2. 取下源 SD 卡并接入 Windows。
3. 在 Windows PowerShell 中 attach 到 WSL。
4. 在 WSL 中只读挂载源 SD 卡。
5. 使用 `tools/copy-rockbase-seed-from-sd.sh` 复制并校验 seed。
6. 卸载并 detach SD 卡。
7. 构建带 seed 镜像。

RDK X5 默认源路径：

```text
/mnt/sdcard/home/sunrise/Desktop/bitcoind/data
```

K230 源路径可显式传入：

```bash
tools/copy-rockbase-seed-from-sd.sh /mnt/sdcard/root/block
```

## Windows/WSL SD 卡挂载

Windows PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\attach-sd-to-wsl.ps1
```

WSL：

```bash
sudo tools/mount-sdcard-in-wsl.sh
```

结束后：

```bash
sudo tools/umount-sdcard-in-wsl.sh
```

Windows PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\detach-sd-from-wsl.ps1
```

注意：

- `mount-sdcard-in-wsl.sh` 默认只读挂载源卡，这是为了保护 seed 来源。
- 如果有多个 USB 存储设备，给 `attach-sd-to-wsl.ps1` 传明确 `-BusId`。

## 输出位置

中间文件：

```text
output/<CONF>/images/blockdata.ext4
output/<CONF>/images/sysimage-sdcard.img
```

默认导出：

```text
/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-1g-<build_stamp>/sysimage-sdcard.img
/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-2g-<build_stamp>/sysimage-sdcard.img
```

默认导出完成后，WSL 侧大镜像会被删除，除非设置：

```bash
K230_KEEP_WSL_IMAGES=1
```

