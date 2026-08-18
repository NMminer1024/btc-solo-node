---
name: rockbase-build
description: 'Build Rockbase images and export to Windows. Use when: 构建镜像, 编译, 全量编译, 完整镜像, make buildroot, rebuild, export image, 导出镜像, 打进镜像, LVGL rebuild, 镜像构建, compile image, clean build, K230_NET, usb, sdio_wifi, 网络模式, 有线网卡, USB网卡, 网络接口, 切换网络, 网络变体.'
---

# Rockbase 镜像构建与导出

---

## ⚠️⚠️⚠️ 铁律：编译输出必须可见 ⚠️⚠️⚠️

> **这是本 skill 最重要的规则，每次构建前请必读！**
>
> 执行 `make buildroot` 或任何编译命令时，**必须让编译输出直接显示在终端**，方便开发者观察进度和排查错误。
>
> **❌ 禁止做法：**
> - 不要加 `&> /dev/null`、`> /dev/null 2>&1`、`| tee /dev/null` 等重定向隐藏输出
> - 不要用 `| tail` 截断输出
> - 不要用 `make -s`（silent 模式）
> - 不要用后台模式（`mode=async`）执行编译，编译是一-shot 任务，应用 `mode=sync`
>
> **✅ 正确做法：**
> - 直接在前台运行 `make CONF=... buildroot ...`，让完整输出流显示在终端
> - 输出量较大时，Buildroot 自身会控制输出粒度，不需要额外压制

---

## 构建规则总览

| 规则 | 说明 |
|------|------|
| 默认非 seed | 除非用户明确说"带 seed 镜像"，否则永远构建非 seed 最小镜像 |
| 全量编译 | 清理所有编译产物（保留工具链和 `.config`），完全重新编译；详见下方 **全量编译** 章节 |
| 完整镜像 | 带 seed 的镜像（`K230_BLOCKDATA_SIZE=24G` + 真实 seed 数据）；详见 `rockbase-seed` skill |
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
| `sdio_wifi`（默认） | SDIO WiFi（RTL8189FS），USB 驱动不编入 | `-sdio` |
| `usb` | USB 全功能：有线网卡 + USB WiFi 驱动都编入 | `-usb` |

兼容旧值：`sdio_8189`/`sdio` → `sdio_wifi`；`usb_eth`/`usb_wifi` → `usb`。

### 设计原则

- **互斥粒度是接口族**：SDIO vs USB，不是 USB 内部再分有线/无线
- `usb` 模式同时编入 USB 有线（`usbnet`/`r8152`）和 USB WiFi（`mt7601u`/`rtw88_8821cu`）驱动，运行时由硬件决定实际接口
- 两种模式都包含 `wpa_supplicant` + `wireless_tools`（SDIO WiFi 和 USB WiFi 都需要）
- 唯一差异：`sdio_wifi` 模式编入 `8189fs.ko`，`usb` 模式不编入

### 用法

在 `make buildroot` 命令末尾追加 `K230_NET=<模式>`：

```bash
# sdio_wifi（默认，不写也等价）
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  K230_NET=sdio_wifi \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0

# usb — USB 有线 + WiFi 全功能
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  K230_NET=usb \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

### USB WiFi 芯片支持（usb 模式）

`usb` 模式通过 `rockbase_usb_wifi.sh detect --autoload` 按 VID:PID 自动加载对应驱动：

| 芯片 | 内核模块 | 固件 | 配置项 |
|------|---------|------|--------|
| MT7601U | `mt7601u` | `lib/firmware/mt7601u.bin` | `CONFIG_MT7601U=m` |
| RTL8821CU | `rtw88_8821cu` | `lib/firmware/rtw88/rtw8821c_fw.bin` | `CONFIG_RTW88_8821CU=m` |

- 芯片区分在**运行时**完成（`etc/rockbase-usb-wifi.map`），不需要为不同芯片烧不同镜像
- 新增芯片：在 map 文件加一行 VID:PID 条目 + 确认内核模块已编入即可
- 改 `linux.fragment` 或 rtw88/mt7601u 相关配置后，必须 `linux-dirclean` 再重编
- 固件注意：`mt7601u.bin` 放 `lib/firmware/` 根目录（不带 `mediatek/` 前缀）；`rtw8821c_fw.bin` 放 `lib/firmware/rtw88/`

### 模式切换时的内部行为

脚本 `tools/rockbase_net_mode.sh` 在 `buildroot` target 最前面自动执行：

1. 若 `K230_NET` 与上次构建相同 → 仅做幂等 sed overlay，直接构建。
2. 若 `K230_NET` 发生变化 → 先 re-apply defconfig（清洁基线），再 dirclean WiFi 包，再 overlay。

**不需要手动 `dirclean` WiFi 包**，切换模式时脚本自动处理。

### 镜像目录命名

导出目录格式：`sysimage-sdcard-rockbase-{1g,2g}-{flavor}-{net_suffix}-TIMESTAMP/`

- `{flavor}`：镜像类型标志
  - `-min-`：最小镜像（非 seed，`K230_BLOCKDATA_SIZE=256M`）
  - `-full-`：完整镜像（带 seed，`K230_BLOCKDATA_SIZE=24G`）
- `{net_suffix}`：网络模式后缀（`-sdio` / `-usb`）

判定依据：脚本按 `K230_BLOCKDATA_SIZE` 判断，`>= 1G` 视为完整镜像（`-full-`），否则为最小镜像（`-min-`）。

示例：
- `sysimage-sdcard-rockbase-1g-min-sdio-20260817-102416/`（1G 最小镜像，SDIO WiFi）
- `sysimage-sdcard-rockbase-1g-min-usb-20260817-113037/`（1G 最小镜像，USB）
- `sysimage-sdcard-rockbase-2g-full-sdio-20260817-120000/`（2G 完整镜像，带 seed）

### 模式验收

`sdio_wifi` 目标（必须全部满足）：
- 有 `8189fs.ko`
- `lsmod` 有 `8189fs`
- 有 `wlan0`
- 有 `wpa_supplicant` 与 `iwconfig`

`usb` 目标（必须全部满足）：
- 无 `8189fs.ko`
- 有 `wpa_supplicant` 与 `iwconfig`
- 插入 USB 有线网卡后出现 `eth*` 接口
- 插入 USB WiFi 网卡后出现 `wlan*` 接口

如果任一条不满足，按"镜像与模式不一致"处理，不要继续做业务验证。

---

## Kernel Patch 变更规则（重要）

`1G` 和 `2G` 各自有独立的 `output/<CONF>/build/linux-*` 目录。

这意味着：

- 你在源码里禁用/新增/修改了 `patches/linux/*.patch`，**不会**自动让另一个 defconfig 的旧内核目录失效
- 如果不先 `linux-dirclean`，Buildroot 可能直接复用旧的已打 patch 内核产物
- 结果就是：源码已经变了，但新导出的镜像仍然带着旧 kernel patch 行为

### 什么时候必须 `linux-dirclean`

- 改了 `patches/linux/` 下任意 patch
- 改了 `fragment/linux.fragment`（内核配置 fragment，如 `CONFIG_MT7601U=m`）
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

## 全量编译

**定义**：清理所有编译产物（`build/`、`staging/`、`target/`、`images/`），保留工具链和 `.config`，完全重新编译。

**适用场景**：
- defconfig 大幅变更后需要干净构建
- 怀疑增量编译产物不一致
- 切换 Buildroot 版本或 overlay 结构变更后
- 排查"改了源码但镜像没变"类问题

**命令**：

```bash
# 1G 全量编译
make CONF=k230_rockbase_1g_defconfig clean
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0

# 2G 全量编译
make CONF=k230_rockbase_2g_defconfig clean
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

**说明**：
- `make clean` 会清理 `output/<CONF>/` 下的 `build/`、`staging/`、`target/`、`images/`，但保留 `.config`
- 工具链是外部工具链（`BR2_TOOLCHAIN_EXTERNAL`），不在 `output/` 目录内，天然不受影响
- 全量编译耗时较长（所有包重新编译），仅在必要时使用
- 如果连 `.config` 也要重置（重新从 defconfig 生成），直接 `rm -rf output/<CONF>/` 再构建

---

## 完整镜像（带 Seed）

**定义**：包含真实 Bitcoin 区块数据的镜像，`K230_BLOCKDATA_SIZE=24G`，seed 数据写入独立 `blockdata.ext4` 分区。

**与最小镜像的区别**：

| 维度 | 最小镜像（默认） | 完整镜像（带 seed） |
|------|-----------------|-------------------|
| Seed 数据 | 空目录 | 真实区块数据（`seed/root-block/`） |
| Blockdata 大小 | 256M | 24G |
| 镜像总大小 | ~2.4G–2.6G | ~26G+ |
| 用途 | 开发调试、功能验证 | 生产部署、同步验证 |

**构建命令**：见 `rockbase-seed` skill。

**触发条件**：用户明确说"带 seed"、"完整镜像"、"seed 镜像"时才构建完整镜像。

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
# usb 模式验证（任一 FAIL 都视为镜像不合格）
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
echo "[iface]"; ip link show | awk -F: "/^[0-9]+:/ {print \$2}" | sed "s/^ //"
echo "[ko]"; find /lib/modules -name "8189fs.ko" | head -3
[ -f /lib/modules/*/kernel/drivers/net/wireless/8189fs/8189fs.ko ] && echo "FAIL:8189fs.ko present" || echo "OK:no 8189fs.ko"
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
'

# sdio_wifi 模式验证（任一 FAIL 都视为镜像不合格）
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
echo "[iface]"; ip link show | awk -F: "/^[0-9]+:/ {print \$2}" | sed "s/^ //"
echo "[ko]"; find /lib/modules -name "8189fs.ko" | head -3
[ -f /lib/modules/*/kernel/drivers/net/wireless/8189fs/8189fs.ko ] && echo "OK:8189fs.ko present" || echo "FAIL:no 8189fs.ko"
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
[ -d /sys/class/net/wlan0 ] && echo "OK:wlan0 present" || echo "FAIL:no wlan0"
'
```

烧录时优先使用目录名带后缀的镜像：
- `-min-` 对应最小镜像（非 seed，`K230_BLOCKDATA_SIZE=256M`）
- `-full-` 对应完整镜像（带 seed，`K230_BLOCKDATA_SIZE=24G`）
- `-sdio-` 对应 `K230_NET=sdio_wifi`
- `-usb-` 对应 `K230_NET=usb`

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
| Windows 导出 | `/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-{1g,2g}-{min\|full}-{net}-<timestamp>/sysimage-sdcard.img` |

非 seed 镜像预期大小：`~2.4G ~ 2.6G`（rootfs 2G + blockdata 256M + 分区开销）。

如果镜像约 32G，说明走到了 seed 路径或默认 24G blockdata，需要加 `K230_BLOCKDATA_SIZE=256M` 重新构建。

---

## 完整镜像（Seed 镜像）

完整镜像 = 带 seed 的镜像。构建流程、seed 同步、容量规则等详见 `rockbase-seed` skill。除非用户明确说"带 seed"、"完整镜像"，不触发 seed 流程。

## 板子信息

当前板子、MMC 拓扑等常驻信息见 `AGENTS.md`。
