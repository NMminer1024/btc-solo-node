---
name: rockbase-build
description: 'Build Rockbase images and export to Windows. Use when: 构建镜像, 编译, make buildroot, rebuild, export image, 导出镜像, 打进镜像, LVGL rebuild, 镜像构建, compile image, K230_NET, usb_eth, usb_wifi, sdio_wifi, 网络模式, 有线网卡, USB网卡, 网络接口, 切换网络, 网络变体.'
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
| 网络模式切换 | 使用 `K230_NET` 变量控制；详见下方 **网络接口模式（K230_NET）** 章节 |

---

## 网络接口模式（K230_NET）

### 可用模式

| `K230_NET` 值 | 说明 | 镜像目录后缀 |
|---------------|------|-------------|
| `sdio_8189`（默认） | 仅启用 RTL8189FS SDIO WiFi（互斥） | `-sdio8189` |
| `sdio_bl616`（占位） | BL616 SDIO WiFi 预留模式（互斥，当前仅保留用户态） | `-sdiobl616` |
| `usb_eth` | USB 有线网卡互斥模式：不编译 WiFi 接口驱动 | `-usbeth` |
| `usb_wifi` | USB 无线互斥模式：启用 USB WiFi 驱动族，禁用 SDIO 驱动族 | `-usbwifi` |

兼容旧值：`sdio_wifi` 会自动映射到 `sdio_8189`。

### 用法

在 `make buildroot` 命令末尾追加 `K230_NET=<模式>`：

```bash
# sdio_8189（默认，不写也等价）
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  K230_NET=sdio_8189 \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0

# usb_eth — 仅 USB 有线，WiFi 接口驱动不进入镜像
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  K230_NET=usb_eth \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0

# 临时调试共存：usb_eth 里保留 8189 WiFi 用户态和驱动
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  K230_NET=usb_eth \
  K230_NET_DEBUG_COEXIST=1 \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

### 临时共存开关

`K230_NET_DEBUG_COEXIST=1` 是临时调试开关，只建议在 USB WiFi / USB ETH bring-up 期间使用。

当它与 `K230_NET=usb_eth` 同时设置时，构建会保留 `RTL8189FS`、`wpa_supplicant` 和 `iwconfig`，方便让 `8189` 与 `eth` 同时在线调试。

导出目录后缀会变成 `-usbethdbg`，便于和正式 `usb_eth` 镜像区分。

### 互斥原则（关键）

每个 `K230_NET` 模式只允许一个接口族：

- `sdio_8189`：仅 `rtl8189fs`
- `sdio_bl616`：仅 `bl616`（当前占位）
- `usb_eth`：仅 USB 有线
- `usb_wifi`：仅 USB 无线（如 AIC）

不要在一个模式里混入其他模式的驱动文件，否则测速与稳定性结论会被噪声污染。

### 模式切换时的内部行为

脚本 `tools/rockbase_net_mode.sh` 在 `buildroot` target 最前面自动执行：

1. 若 `K230_NET` 与上次构建相同 → 仅做幂等 sed overlay，直接构建。
2. 若 `K230_NET` 发生变化 → 先 re-apply defconfig（清洁基线），再 dirclean WiFi 包，再 overlay。

**不需要手动 `dirclean` WiFi 包**，切换模式时脚本自动处理。

### 镜像目录命名

导出目录格式：`sysimage-sdcard-rockbase-{1g,2g}-{net_suffix}-TIMESTAMP/`

示例：
- `sysimage-sdcard-rockbase-1g-sdio8189-20260813-102416/`
- `sysimage-sdcard-rockbase-1g-sdiobl616-20260813-104100/`
- `sysimage-sdcard-rockbase-1g-usbeth-20260813-113037/`

### 模式验收（互斥标准）

`usb_eth` 目标（必须全部满足）：
- 无 `8189fs.ko`
- `lsmod` 无 `8189fs`
- 无 `wlan0`
- 无 `wpa_supplicant` 与 `iwconfig`

`sdio_8189` 目标（必须全部满足）：
- 有 `8189fs.ko`
- `lsmod` 有 `8189fs`
- 有 `wlan0`
- 有 `wpa_supplicant` 与 `iwconfig`

`sdio_bl616` 目标（占位）：
- 不应出现 `8189fs.ko`
- 不应出现 USB WiFi 驱动模块
- 后续接入 BL616 驱动后，再补充对应验收项

`usb_wifi` 目标（必须全部满足）：
- 不应出现 `8189fs.ko`
- 应出现 USB WiFi 驱动模块（如 `aic_*`）
- 由 USB 网卡提供 `wlan*` 接口

如果任一条不满足，按“镜像与模式不一致”处理，不要继续做业务验证。

---

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

## 默认构建命令

### 1G 非 seed 最小镜像（默认 sdio_wifi）

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

### 2G 非 seed 最小镜像（默认 sdio_wifi）

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

## 编译输出必须可见

执行 `make buildroot` 或任何编译命令时，**必须让编译输出直接显示在终端**，方便开发者观察进度和排查错误。

**禁止做法**：
- 不要加 `&> /dev/null`、`> /dev/null 2>&1`、`| tee /dev/null` 等重定向隐藏输出
- 不要用 `make -s`（silent 模式）
- 不要用后台模式（`mode=async`）执行编译，编译是一-shot 任务，应用 `mode=sync`

**正确做法**：
- 直接在前台运行 `make CONF=... buildroot ...`，让完整输出流显示在终端
- 输出量较大时，Buildroot 自身会控制输出粒度，不需要额外压制

---

## 编译陷阱

### ⚠️ 从 target 手动删除包文件后，Buildroot 不会自动重装

**场景**：手动 `rm` 了 target 里某个包的二进制（如 `wpa_supplicant`、`8189fs.ko`），后续 `make buildroot` 仍不会重装——因为 Buildroot 依据 `.stamp_target_installed` 判断包已安装。

**后果举例**（本次亲历）：

1. 临时试验中手动删除了 `output/.../target/usr/sbin/wpa_supplicant`
2. 恢复 defconfig（加回 `BR2_PACKAGE_WPA_SUPPLICANT=y`）
3. `make buildroot` 很快跑完，看起来正常
4. 刷机后串口报 `wpa_supplicant: not found` — 二进制根本没进镜像

**正确修复**：对受影响的包跑 dirclean，迫使 Buildroot 重新编译并安装到 target：

```bash
make CONF=k230_rockbase_1g_defconfig wpa_supplicant-dirclean
make CONF=k230_rockbase_1g_defconfig wireless_tools-dirclean
# 然后正常 make buildroot
```

**一般规律**：只要手动改动过 `output/.../target/` 目录里的包文件，对应的包就需要 dirclean。

---

### ⚠️ Kernel patch / rtl8189fs patch 变更必须 dirclean

- `patches/linux/` 任意 patch 变更 → 每个目标 defconfig 都要 `linux-dirclean`
- `patches/rtl8189fs/` 任意 patch 变更 → 要 `rtl8189fs-dirclean`

---

## 构建后验证（重要）

如果本次目标是验证 kernel patch 是否真的进镜像，构建后至少做一项源码侧检查和一项实机侧检查。

### 源码侧检查

例如检查构建后的 `sdhci-of-kendryte.c`：

```bash
sed -n '292,310p' output/k230_rockbase_1g_defconfig/build/linux-*/drivers/mmc/host/sdhci-of-kendryte.c
```

判读：

- 看到 `SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_BROKEN_DMA` → 还是 PIO 诊断态
- 只剩 `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN` → DMA 已恢复

### 实机侧检查

烧录后看启动日志：

```bash
ssh root@<IP> 'dmesg | grep -iE "mmc[01]: SDHCI controller|using PIO|using ADMA|using DMA"'
```

### K230_NET 验证

```bash
# usb_eth 互斥验证（任一 FAIL 都视为镜像不合格）
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
echo "[iface]"; ip link show | awk -F: "/^[0-9]+:/ {print \$2}" | sed "s/^ //"
echo "[ko]"; find /lib/modules -name "8189fs.ko" | head -3
echo "[lsmod]"; lsmod | grep 8189 || true
command -v wpa_supplicant >/dev/null && echo "FAIL:wpa_supplicant present" || echo "OK:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "FAIL:iwconfig present" || echo "OK:no iwconfig"
[ -d /sys/class/net/wlan0 ] && echo "FAIL:wlan0 present" || echo "OK:no wlan0"
'

# sdio_wifi 验证（任一 FAIL 都视为镜像不合格）
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
echo "[iface]"; ip link show | awk -F: "/^[0-9]+:/ {print \$2}" | sed "s/^ //"
echo "[ko]"; find /lib/modules -name "8189fs.ko" | head -3
echo "[lsmod]"; lsmod | grep 8189 || true
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
[ -d /sys/class/net/wlan0 ] && echo "OK:wlan0 present" || echo "FAIL:no wlan0"
'
```

烧录时优先使用目录名带后缀的镜像：
- `-usbeth-` 对应 `K230_NET=usb_eth`
- `-usbwifi-` 对应 `K230_NET=usb_wifi`
- `-sdio8189-` 对应 `K230_NET=sdio_8189`
- `-sdiobl616-` 对应 `K230_NET=sdio_bl616`

若实机行为与后缀不一致，先排查误烧录或混用旧目录，再排查构建流程。

不要只看源码目录或 commit 信息就默认镜像行为已经变化。

---

## LVGL UI 重建规则（重要）

任何 `rockbase_lcdui`/LVGL UI 源码改动后，**必须**在构建镜像前单独重建：

```bash
make CONF=k230_rockbase_1g_defconfig rockbase_lcdui-rebuild
make CONF=k230_rockbase_2g_defconfig rockbase_lcdui-rebuild
```

---

## 镜像产物说明

| 产物 | 路径 |
|------|------|
| 构建中间文件 | `output/<CONF>/images/blockdata.ext4`、`sysimage-sdcard.img` |
| Windows 导出 | `/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-{1g,2g}-{net}-<timestamp>/sysimage-sdcard.img` |

非 seed 镜像预期大小：`~2.4G ~ 2.6G`（rootfs 2G + blockdata 256M + 分区开销）。

如果镜像约 32G，说明走到了 seed 路径或默认 24G blockdata，需要加 `K230_BLOCKDATA_SIZE=256M` 重新构建。

---

## 种子 (Seed) 镜像

Seed 镜像构建见 `rockbase-seed` skill。除非用户明确说"带 seed"，不触发 seed 流程。

## 板子信息

当前板子、MMC 拓扑等常驻信息见 `AGENTS.md`。
