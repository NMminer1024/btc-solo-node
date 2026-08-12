---
name: rockbase-seed
description: 'Build Rockbase images with Bitcoin block seed data, or sync seed data from devices. Use when: seed镜像, 带seed, block seed, sync seed, 同步seed, 种子数据, seed data, BLOCK_SEED_DIR, K230_BLOCKDATA_SIZE.'
---

# Rockbase Seed 镜像管理

完整背景见 `docs/rockbase-block-seed-image-workflow.md`。

**默认不构建 seed 镜像。** 只有用户明确说"带 seed 镜像"才使用本 skill。

---

## Seed 目录要求

最少包含：

```text
seed/root-block/
  blocks/
  blocks/index/
  chainstate/
```

构建脚本会校验 LevelDB manifest：

- `chainstate/CURRENT` + `<MANIFEST>`
- `blocks/index/CURRENT` + `<MANIFEST>`

不要在 seed 中保留运行时文件：`.lock`、`.cookie`、`bitcoind.pid`、`LOCK`。

---

## 带 Seed 构建命令

### 1G 带 seed

```bash
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=seed/root-block \
  K230_BLOCKDATA_SIZE=24G \
  K230_IMAGE_GZIP=0
```

### 2G 带 seed

```bash
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=seed/root-block \
  K230_BLOCKDATA_SIZE=24G \
  K230_IMAGE_GZIP=0
```

### 容量规则

- `K230_BLOCKDATA_SIZE` 必须 > seed 实际大小
- `~17G` seed 建议从 `24G` 开始
- seed 镜像会显著变大，导出和烧录时间都会增加

---

## Seed 路径优先级

```
BLOCK_SEED_DIR > ROCKBASE_BLOCK_SEED_DIR > seed/root-block
```

---

## 更新 Seed 流程（推荐）

使用 `tools/sync-rockbase-seed-from-device.sh`，不要在构建中手工直接改 `seed/root-block`。

### 从运行中的设备同步

```bash
bash tools/sync-rockbase-seed-from-device.sh
```

交互式输入：

1. 远端设备 IP
2. 远端用户名（默认 `root`）
3. 远端密码
4. 远端 block 数据目录（默认 `/root/block`）

脚本行为：

- 验证 SSH 登录
- 验证远端目录存在
- 提示确认后清空本地 `seed/root-block`
- rsync 远端数据到本地
- 校验 `chainstate/CURRENT` 和 `blocks/index/CURRENT` manifest

### 每周更新流程

1. 确认 seed 机器上 `bitcoind` 已优雅停止（`bitcoin-cli stop`）
2. 运行 `bash tools/sync-rockbase-seed-from-device.sh`
3. 按提示输入远端信息
4. 确认脚本输出 `du -sh` 结果和本地校验通过

---

## 运行时行为

- seed 数据进入独立 `blockdata.ext4` 分区（不是 rootfs）
- `S59blockdata` 启动时挂载 blockdata 分区到 `/root/block`
- `bitcoind` datadir 为 `/root/block`

---

## 非 seed 构建

非 seed 构建使用空seed目录并设置 256M blockdata，见 `rockbase-build` skill。
