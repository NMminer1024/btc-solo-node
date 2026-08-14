---
name: rockbase-seed
description: 'Build Rockbase images with Bitcoin block seed data, or sync seed data from devices. Use when: seed镜像, 带seed, block seed, sync seed, 同步seed, 种子数据, seed data, BLOCK_SEED_DIR, K230_BLOCKDATA_SIZE.'
---

# Rockbase Seed 镜像管理

完整背景见 `docs/rockbase-block-seed-image-workflow.md`。

**默认不构建 seed 镜像。** 只有用户明确说"带 seed 镜像"才使用本 skill。

---

## Seed 源设备

| IP | 型号 | 角色 | 凭据 | bitcoind datadir |
|----|------|------|------|------------------|
| `192.168.124.12` | RDK X5 (Ubuntu 22.04 ARM64) | Seed 源 | `sunrise` / `sunrise` | `/home/sunrise/Desktop/bitcoind/data` |

注意：
- RDK X5 上 bitcoind 通过 snap 安装（`/snap/bitcoin-core/222/bin/bitcoind`）。
- 同步 seed 前必须先优雅停止 bitcoind：`sshpass -p 'sunrise' ssh sunrise@192.168.124.12 '/home/sunrise/Desktop/bitcoind/stop.sh'`。
- 同步完成后重启 bitcoind：`sshpass -p 'sunrise' ssh sunrise@192.168.124.12 'nohup /snap/bin/bitcoin-core.daemon -conf=/home/sunrise/Desktop/bitcoind/bitcoin.conf -datadir=/home/sunrise/Desktop/bitcoind/data &'`。

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

> **注意**：seed 镜像通常配合默认 `sdio_wifi` 模式；若当前实验是 `usb_eth`，需追加 `K230_NET=usb_eth`。
> 完整的 `K230_NET` 说明见 `rockbase-build` skill。

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

### 网络模式互斥验收（烧录后必须执行）

若本次带 seed 构建使用了 `K230_NET=usb_eth`，烧录后必须确认镜像不含 SDIO WiFi 运行要素：

```bash
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
find /lib/modules -name "8189fs.ko" | head -3
lsmod | grep 8189 || true
command -v wpa_supplicant >/dev/null && echo "FAIL:wpa_supplicant present" || echo "OK:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "FAIL:iwconfig present" || echo "OK:no iwconfig"
[ -d /sys/class/net/wlan0 ] && echo "FAIL:wlan0 present" || echo "OK:no wlan0"
'
```

若使用默认 `sdio_wifi`，则必须反向确认 SDIO WiFi 要素存在：

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

任一 `FAIL` 都按镜像与模式不一致处理，优先排查是否误烧录了错误后缀目录（`-usbeth-` 或 `-sdio-`）。

---

## Seed 路径优先级

```
BLOCK_SEED_DIR > ROCKBASE_BLOCK_SEED_DIR > seed/root-block
```

---

## 更新 Seed 流程（推荐）

使用 `tools/sync-rockbase-seed-from-device.sh`，不要在构建中手工直接改 `seed/root-block`。

### 从 Seed 源设备 (RDK X5) 同步

**快速同步命令（一键）**：

```bash
# 先停止远端 bitcoind
sshpass -p 'sunrise' ssh sunrise@192.168.124.12 '/home/sunrise/Desktop/bitcoind/stop.sh'
sleep 5

# ⚠️ 重要：前台运行，禁止重定向输出（不要用 `> /tmp/xxx.log` 或 `| tail`），以便看到 rsync 进度
REMOTE_HOST=192.168.124.12 \
REMOTE_USER=sunrise \
REMOTE_PASSWORD=sunrise \
REMOTE_DIR=/home/sunrise/Desktop/bitcoind/data \
bash tools/sync-rockbase-seed-from-device.sh

# 同步完成后重启远端 bitcoind
sshpass -p 'sunrise' ssh sunrise@192.168.124.12 'nohup /snap/bin/bitcoin-core.daemon -conf=/home/sunrise/Desktop/bitcoind/bitcoin.conf -datadir=/home/sunrise/Desktop/bitcoind/data > /dev/null 2>&1 &'
```

### 交互式同步（通用）

```bash
bash tools/sync-rockbase-seed-from-device.sh
```

交互式输入：

1. 远端设备 IP（默认用 `192.168.124.12`）
2. 远端用户名（RDK X5 上用 `sunrise`，Rockbase 板子上用 `root`）
3. 远端密码
4. 远端 block 数据目录（RDK X5 上为 `/home/sunrise/Desktop/bitcoind/data`）

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
