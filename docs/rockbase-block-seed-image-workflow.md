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
make CONF=k230_rockbase_1g_defconfig buildroot \
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
- 导出的 `sysimage-sdcard.img` 约 `2.4G ~ 2.6G`（rootfs 2G 基线）

## 带 seed 镜像

带 seed 构建示例：

```bash
make CONF=k230_rockbase_1g_defconfig buildroot \
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

## 更新 seed 的建议流程

推荐使用脚本，不要在构建过程中手工直接改 `seed/root-block`。

辅助脚本：

- `tools/init-rockbase-seed.sh`
- `tools/sync-rockbase-seed-from-device.sh`：在 WSL 中交互式拉取远端设备上的 block 数据并更新本地 `seed/root-block`。

### 方案 A：从长时间运行的设备同步 seed（推荐）

适用于你有一台长期运行的设备作为 seed 机器，每周定期更新本地 seed。

在 WSL 中执行：

```bash
bash tools/sync-rockbase-seed-from-device.sh
```

脚本会交互式询问：

- 远端设备 IP/主机名
- 远端登录用户名（默认 `root`）
- 远端登录密码
- SSH 登录成功后，再输入远端 block 数据目录（默认 `/root/block`）

本地目标目录默认就是：

```text
seed/root-block
```

脚本行为说明：

- 先验证 SSH 登录，再验证你输入的远端目录是否存在。
- 同步前会提示确认并清空本地 `seed/root-block`，然后再接收远端数据。
- 同步完成后会在本地校验 `chainstate/CURRENT` 和 `blocks/index/CURRENT` 对应 manifest，确保 seed 可用于后续构建。

每周更新时可按这个最短流程执行：

1. 确认 seed 机器上的 `bitcoind` 已优雅停止。
2. 在 WSL 运行 `bash tools/sync-rockbase-seed-from-device.sh`。
3. 按提示输入远端连接信息和远端数据目录。
4. 同步完成后，确认脚本输出的 `du -sh` 结果和本地校验通过。

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

