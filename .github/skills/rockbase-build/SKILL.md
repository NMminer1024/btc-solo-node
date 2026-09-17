---
name: rockbase-build
description: 'Build Rockbase images and export to Windows. Use when: 构建镜像, 编译, 全量编译, 完整镜像, make buildroot, rebuild, export image, 导出镜像, 打进镜像, LVGL rebuild, 镜像构建, compile image, clean build, K230_NET, usb, sdio_wifi, 网络模式, 有线网卡, USB网卡, 网络接口, 切换网络, 网络变体.'
---

# Rockbase 镜像构建与导出

---

## ⚠️⚠️⚠️ 铁律：编译输出必须可观察 ⚠️⚠️⚠️

> **这是本 skill 最重要的规则，每次构建前请必读！**
>
> 执行 `make buildroot` 或任何编译命令时，**必须让编译输出可被观察**（直接显示在终端，或写入日志文件供 `tail` 查看），方便开发者观察进度和排查错误。
>
> **❌ 禁止做法（任何场景都禁止）：**
> - 不要加 `&> /dev/null`、`> /dev/null 2>&1`、`| tee /dev/null` 等重定向**丢弃**输出
> - 不要用 `| tail` 截断**实时**输出流
> - 不要用 `make -s`（silent 模式）
>
> **✅ 正确做法（按编译时长二选一）：**
> - **短编译 / 增量编译**（预计 < 10 分钟，输出量小）：直接在前台 `mode=sync` 运行 `make CONF=... buildroot ...`，让完整输出流显示在终端。
> - **长编译 / 全量编译**（30min+，输出几百 KB~MB）：必须用 `nohup` 后台 + 日志文件 + 轮询，**禁止用 `mode=sync`**。详见下方 **长编译运行方式（全量编译必读）** 章节。
>
> **为什么长编译不能用 `mode=sync`（关键教训，已固化）：**
> `run_in_terminal` 的 `mode=sync` 在**输出超过 ~20KB 时会截断并中断任务**。全量编译输出量远超此阈值，会被中途杀掉——症状：命令返回 "Large tool result written to file"，但 build 目录停止更新、无最终产物、`build-time.log` 停在某个中间阶段（如 boost install-staging）。因此长编译**必须** `nohup` 后台，输出重定向到日志文件（这不是"丢弃输出"，而是可 `tail` 观察的日志）。

---

## 并行编译（强制）

> **所有编译——无论全量还是增量——都必须使用最快的编译方式：多核 CPU 并行化。**

- 顶层 `make` 命令**必须显式追加 `-j$(nproc)`**，启用多核并行
- Buildroot 内部默认已是 `nproc+1` 并行（`BR2_JLEVEL=0` 时），但显式指定可避免歧义、保证跨环境行为一致
- 禁止使用 `-j1` 或任何串行编译方式
- 本规则适用于所有构建入口：`buildroot`、`clean`、`linux-dirclean`、`rockbase_lcdui-rebuild` 等

### 低内存机器（<8G RAM）：追加 `BR2_JLEVEL=3`

> **顶层 `-jN` 只控制 buildroot 包级并行，管不到包内部（如 bitcoin 的 cmake）的并行度。**
> 包内并行由 `BR2_JLEVEL` 控制（defconfig 默认 0 = nproc+1）。

- bitcoin 31.1 交叉编译单文件峰值 ~1.5G 内存，16 核机器 `BR2_JLEVEL=0` 会起 17 个 cc1plus → **OOM**（日志 `Killed signal terminated program cc1plus`）
- **低内存机器构建命令追加 `BR2_JLEVEL=3`**（顶层 Makefile 已支持透传，不传则用 .config 默认值，高内存机器无需任何改动）：

```bash
make CONF=k230_rockbase_defconfig buildroot -j$(nproc) BR2_JLEVEL=3 \
  K230_BLOCKDATA_SIZE=20G K230_IMAGE_GZIP=0
```

- 判断标准：`free -h` 可用内存 < 8G 就加；≥ 16G 不用加
- 2026-09-17 实测：7.4G 机器 `-j16`/`-j4` 均 OOM，`BR2_JLEVEL=3` 通过（峰值 ~4.5G）

---

## 长编译运行方式（全量编译必读）

> **本章节是长编译（全量编译 / 30min+ 构建）的唯一权威运行方式。执行长编译时以本 skill 为准，不要依赖会话记忆里的临时做法。**

### 为什么必须 nohup 后台

`run_in_terminal` 的 `mode=sync` 在输出超过 ~20KB 时会截断并杀掉任务。全量编译输出几百 KB~MB，用 sync 必被中途杀掉。因此长编译**必须** `nohup` 后台 + 日志文件 + 轮询。

### 标准流程

**1. 启动后台编译（输出写日志，不丢弃）：**

```bash
cd /home/bitpony/Desktop/mining/btc-solo-node
nohup make CONF=k230_rockbase_defconfig buildroot -j$(nproc) \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0 \
  > /tmp/rockbase-build.log 2>&1 &
echo "PID=$!"
```

**1b. 实时观察编译过程（让用户看见，必须执行）：**

启动后台编译后，**立即**用 `run_in_terminal` 的 `mode=async` 跑一次 `tail -f`，把编译输出实时流式显示给用户：

```bash
tail -f /tmp/rockbase-build.log
```

- 用 `mode=async`（**不是** `mode=sync`）：`tail -f` 永不退出，sync 会一直阻塞；async 会先返回一段实时输出快照，之后编译每产生新日志，终端就会持续刷新，用户能直接看见编译过程。
- 这一步**不替代**下方的轮询（步骤 2），二者并行：`tail -f` 负责"让用户看见"，轮询负责"判断进度/是否结束"。
- 编译结束后（步骤 2 检测到 `MAKE_EXITED`），用 `kill_terminal` 关掉这个 `tail -f` 终端，避免它一直挂着。
- 若用户主动 `Ctrl+C` 中断了 `tail -f`，不影响后台编译，无需重启。

**2. 轮询进度（每次观察三处）：**

```bash
BT=output/k230_rockbase_defconfig/build/build-time.log
pgrep -x make >/dev/null && echo "RUNNING" || echo "MAKE_EXITED"
tail -1 "$BT" 2>/dev/null          # 精确到当前包/阶段
tail -15 /tmp/rockbase-build.log   # 原始 make 输出末尾
```

**3. 判断是否被异常中断（而非正常结束）：**

正常结束应有最终产物；若 `pgrep -x make` 无进程但满足以下任一条，说明被杀/失败：
- `find output -newermt '-1 minute' | wc -l` 计数为 0（build 目录停止更新）
- 无 `output/<CONF>/images/sysimage-sdcard.img`
- `build-time.log` 停在中间阶段（如 boost install-staging）而非 `end` 收尾

### 轮询循环模板（可整段执行）

```bash
cd /home/bitpony/Desktop/mining/btc-solo-node
BT=output/k230_rockbase_defconfig/build/build-time.log
for i in $(seq 1 40); do
  if pgrep -x make >/dev/null; then
    echo "[$i] RUNNING | $(tail -1 "$BT" 2>/dev/null)"
    sleep 60
  else
    echo "[$i] MAKE_EXITED"; break
  fi
done
echo "=== 最终 build-time 末尾 ==="; tail -8 "$BT" 2>/dev/null
echo "=== build log 末尾 ==="; tail -15 /tmp/rockbase-build.log
```

> 40 × 60s ≈ 40 分钟覆盖一次全量编译；若预计更久，调大 `seq` 上限。

---

## 构建规则总览

| 规则 | 说明 |
|------|------|
| 默认非 seed | 除非用户明确说"带 seed 镜像"，否则永远构建非 seed 最小镜像 |
| 全量编译 | 清理所有编译产物（保留工具链和 `.config`），完全重新编译；详见下方 **全量编译** 章节 |
| 完整镜像 | 带 seed 的镜像（`K230_BLOCKDATA_SIZE=20G` + 真实 seed 数据）；详见 `rockbase-seed` skill |
| LVGL UI 重建 | local 包源码改动后顶层 `make buildroot` 会自动 rsync 检测并重编（已根治）；仍可手动 `<pkg>-rebuild` 强制重建 |
| WSL 导出 | 使用 `post-image.sh` 自动导出到 `/mnt/d/code/pony/IoT/k230/img/` |
| 构建前准备 | 非 seed 构建需要 `mkdir -p /tmp/rockbase-empty-seed` |
| 并行编译 | 所有编译（全量/增量）必须显式多核并行：make 命令追加 `-j$(nproc)`；详见下方 **并行编译（强制）** 章节 |
| Kernel patch 变更 | 只要改了 `buildroot-overlay/board/canaan/k230_rockbase/patches/linux/`，每个目标 defconfig 都必须先执行一次 `linux-dirclean` 再重编 |
| Overlay 顶层文件内容修改 | 只改 `buildroot-overlay/` 下**顶层/非包目录**文件（如 `package/pkg-generic.mk`、`package/Config.in`、`support/`）的**内容**（非新增/删除）时，`.overlay_sync` 不会自动重跑，必须先 `make sync` 再构建；详见下方 **Overlay 同步陷阱** 章节 |
| 网络接口 | 统一镜像（USB + SDIO 驱动都在，运行时 `rockbase_netsel.sh` 自动选择）；`K230_NET` 仅作测试开关；详见下方 **网络接口模式（统一镜像，K230_NET）** 章节 |

---

## 网络接口模式（统一镜像，K230_NET）

> 2026-08-20 起实施"单镜像 USB 优先 + SDIO 回退"方案，详见 `docs/rockbase-net-unified-image-plan.md`。

### 统一镜像（默认）

**单一镜像同时带 SDIO WiFi（RTL8189FS）与 USB 网络（有线 + WiFi）驱动**，板子上电后由 `/usr/bin/rockbase_netsel.sh`（init.d/S37netsel，早于 S38wifi）运行时自动探测：

1. USB 有线网卡（`r8152`/`ax8817x`/`ax88179_178a`/`cdc_ether`，内核内置）→ `usb_eth`
2. USB WiFi 加密狗（`mt7601u`/`rtw88_8821cu`，按 `/etc/rockbase-usb-wifi.map` 自动加载）→ `usb_wifi`
3. 都没有 → 回退板载 SDIO WiFi（`8189fs`）→ `sdio_wifi`

决策结果写入 `/tmp/rockbase-net-mode`，供 `S38wifi`/`rockbase_wifi.sh` 读取。USB 路径下运行时 unbind SDIO 控制器（freeze 缓解）；**8189fs 已加载时绝不运行时 rmmod**（实测会触发历史 freeze 路径）。

### K230_NET 变量（仅测试开关）

| `K230_NET` 值 | 说明 |
|---------------|------|
| `auto`（默认） | 统一镜像，不做任何构建期驱动增删 |
| `sdio_wifi` / `usb` | 旧值兼容，行为与 `auto` 相同（保留作强制模式测试开关） |

镜像目录后缀统一为 **`-net`**（不再有 `-sdio`/`-usb` 区分）。

### 用法

```bash
# 统一镜像（默认，不写 K230_NET 也等价）
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_defconfig buildroot -j$(nproc) \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

### USB WiFi 芯片支持

统一镜像通过 `rockbase_usb_wifi.sh detect --autoload` 按 VID:PID 自动加载对应驱动：

| 芯片 | 内核模块 | 固件 | 配置项 |
|------|---------|------|--------|
| MT7601U | `mt7601u` | `lib/firmware/mt7601u.bin` | `CONFIG_MT7601U=m` |
| RTL8821CU | `rtw88_8821cu` | `lib/firmware/rtw88/rtw8821c_fw.bin` | `CONFIG_RTW88_8821CU=m` |

- 芯片区分在**运行时**完成（`etc/rockbase-usb-wifi.map`），不需要为不同芯片烧不同镜像
- 新增芯片：在 map 文件加一行 VID:PID 条目 + 确认内核模块已编入即可
- 改 `linux.fragment` 或 rtw88/mt7601u 相关配置后，必须 `linux-dirclean` 再重编
- 固件注意：`mt7601u.bin` 放 `lib/firmware/` 根目录（不带 `mediatek/` 前缀）；`rtw8821c_fw.bin` 放 `lib/firmware/rtw88/`

### 构建时的内部行为

脚本 `tools/rockbase_net_mode.sh` 在 `buildroot` target 最前面自动执行：

1. 若 `K230_NET` 与上次构建相同 → 仅做幂等 sed overlay，直接构建。
2. 若 `K230_NET` 发生变化 → 先 re-apply defconfig（清洁基线），再 dirclean WiFi 包，再 overlay。
3. 统一镜像下**不再删除 `8189fs.ko`**（所有模式都保留）。

**不需要手动 `dirclean` WiFi 包**，切换模式时脚本自动处理。

### 镜像目录命名

导出目录格式：`sysimage-sdcard-rockbase-{flavor}-net-TIMESTAMP/`

- `{flavor}`：镜像类型标志
  - `-min-`：最小镜像（非 seed，`K230_BLOCKDATA_SIZE=256M`）
  - `-full-`：完整镜像（带 seed，`K230_BLOCKDATA_SIZE=20G`）
- 网络后缀固定为 `-net`（统一镜像，USB + SDIO 驱动都在）

判定依据：脚本按 `K230_BLOCKDATA_SIZE` 判断，`>= 1G` 视为完整镜像（`-full-`），否则为最小镜像（`-min-`）。

示例：
- `sysimage-sdcard-rockbase-min-net-20260820-102416/`（最小统一镜像）
- `sysimage-sdcard-rockbase-full-net-20260820-120000/`（完整镜像，带 seed）

### 镜像验收（统一镜像）

构建后镜像必须同时满足：
- 有 `8189fs.ko`（SDIO WiFi 驱动）
- 有 `wpa_supplicant` 与 `iwconfig`
- 内核内置 USB 有线驱动（`r8152`/`ax8817x` 等）+ `mt7601u.ko`/`rtw88_8821cu.ko` 模块存在

烧录后按硬件组合验收（`/tmp/rockbase-net-mode` 的 `NET_MODE` 值）：
- 插 USB 有线网卡 → `NET_MODE=usb_eth`，`eth*` up，`lsmod` 无 `8189fs`
- 插 USB WiFi 狗（无有线）→ `NET_MODE=usb_wifi`，对应 `wlan*` 正常连接，`lsmod` 无 `8189fs`
- 什么都不插 → `NET_MODE=sdio_wifi`，`lsmod` 有 `8189fs`，`wlan0` 走 SDIO 路径

如果任一条不满足，按"镜像与模式不一致"处理，不要继续做业务验证。

---

## Kernel Patch 变更规则（重要）

现在只有单一 `output/k230_rockbase_defconfig/build/linux-*` 目录（1G/2G 已合并为单一 profile）。

这意味着：

- 你在源码里禁用/新增/修改了 `patches/linux/*.patch` 后，如果不先 `linux-dirclean`，Buildroot 可能直接复用旧的已打 patch 内核产物
- 结果就是：源码已经变了，但新导出的镜像仍然带着旧 kernel patch 行为

### 什么时候必须 `linux-dirclean`

- 改了 `patches/linux/` 下任意 patch
- 改了 `fragment/linux.fragment`（内核配置 fragment，如 `CONFIG_MT7601U=m`）
- 预期 kernel 行为应变化，例如 `PIO ↔ DMA`、MMC/SDHCI/IRQ/DT 相关修改

### 正确做法

```bash
make CONF=k230_rockbase_defconfig linux-dirclean
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_defconfig buildroot -j$(nproc) \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0
```

## 默认构建命令

### 非 seed 最小镜像（默认 sdio_wifi）

```bash
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_defconfig buildroot -j$(nproc) \
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
make CONF=k230_rockbase_defconfig copy-img IMG_OUTPUT_DIR=/mnt/d/code/pony/IoT/k230/img
```

---

## 全量编译

**定义**：清理所有编译产物（`build/`、`staging/`、`target/`、`images/`），保留工具链和 `.config`，完全重新编译。

**适用场景**：
- defconfig 大幅变更后需要干净构建
- 怀疑增量编译产物不一致
- 切换 Buildroot 版本或 overlay 结构变更后
- 排查"改了源码但镜像没变"类问题

**命令**（全量编译是长编译，**必须**按上方 **长编译运行方式** 章节用 `nohup` 后台 + 轮询，禁止 `mode=sync`）：

```bash
# 1) 清理（短任务，可前台 sync）
make CONF=k230_rockbase_defconfig clean -j$(nproc)
mkdir -p /tmp/rockbase-empty-seed

# 2) 全量编译（长任务，nohup 后台 + 日志 + 轮询）
cd /home/bitpony/Desktop/mining/btc-solo-node
nohup make CONF=k230_rockbase_defconfig buildroot -j$(nproc) \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_GZIP=0 \
  > /tmp/rockbase-build.log 2>&1 &
echo "PID=$!"
# 之后按"长编译运行方式"章节：
#   步骤 1b 用 mode=async 跑 tail -f /tmp/rockbase-build.log 让用户实时看见编译过程
#   步骤 2  用轮询模板观察进度 / 判断是否结束
```

**说明**：
- `make clean` 会清理 `output/<CONF>/` 下的 `build/`、`staging/`、`target/`、`images/`，但保留 `.config`
- 工具链是外部工具链（`BR2_TOOLCHAIN_EXTERNAL`），不在 `output/` 目录内，天然不受影响
- 全量编译耗时较长（所有包重新编译），仅在必要时使用
- 如果连 `.config` 也要重置（重新从 defconfig 生成），直接 `rm -rf output/<CONF>/` 再构建
- **运行方式以本 skill 为准**：长编译的 nohup/轮询做法已固化在本 skill，执行时直接遵循本 skill，不要依赖会话记忆里的临时做法

---

## 完整镜像（带 Seed）

**定义**：包含真实 Bitcoin 区块数据的镜像，`K230_BLOCKDATA_SIZE=20G`，seed 数据写入独立 `blockdata.ext4` 分区。

**与最小镜像的区别**：

| 维度 | 最小镜像（默认） | 完整镜像（带 seed） |
|------|-----------------|-------------------|
| Seed 数据 | 空目录 | 真实区块数据（`seed/root-block/`） |
| Blockdata 大小 | 256M | 20G |
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
make CONF=k230_rockbase_defconfig wpa_supplicant-dirclean
make CONF=k230_rockbase_defconfig wireless_tools-dirclean
# 然后正常 make buildroot
```

**一般规律**：只要手动改动过 `output/.../target/` 目录里的包文件，对应的包就需要 dirclean。

---

### ⚠️ Kernel patch / rtl8189fs patch 变更必须 dirclean

- `patches/linux/` 任意 patch 变更 → 每个目标 defconfig 都要 `linux-dirclean`
- `patches/rtl8189fs/` 任意 patch 变更 → 要 `rtl8189fs-dirclean`

---

### ⚠️ 内核 autoconf.h 丢失 → depmod 报 WSL 主机版本

**症状**：构建在 `target-finalize` 阶段报错，且版本号是 WSL 宿主机的内核版本：

```
depmod: ERROR: could not open directory .../target/lib/modules/6.6.87.2-microsoft-standard-WSL2: No such file or directory
depmod: FATAL: could not search modules: No such file or directory
```

**根因**：内核构建目录 `output/<CONF>/build/linux-*/include/generated/autoconf.h`
（连带 `include/config/kernel.release`）丢失了，但 `.stamp_configured` / `.stamp_built`
还在。Buildroot 依据 stamp 判断「内核已配置完成」，增量构建会**跳过 `make prepare`**，
于是 `make kernelrelease` 拿不到目标内核版本（`6.6.36`），回退成宿主机版本
`6.6.87.2-microsoft-standard-WSL2`，`target-finalize` 的 `depmod -a <ver>` 就去
`target/lib/modules/<hostver>` 找一个不存在的目录而失败。

典型诱因：在内核目录里手动 `make clean` / `rm include/generated/*`，或做过
`linux-dirclean` 后没走完整重编，造成「stamp 与文件实际状态不一致」的脏增量状态。

**标准修复（推荐，走 buildroot 机制）**：

```bash
# 让 buildroot 清掉内核 stamp 并重新 prepare / 重编，别手动补文件
make CONF=k230_rockbase_defconfig linux-rebuild
# 若仍异常，退回更彻底的重建
make CONF=k230_rockbase_defconfig linux-dirclean
```

**轻量修复（仅补回丢失文件，跳过 stamp 机制，应急用）**：在内核目录里执行

```bash
cd output/k230_rockbase_defconfig/build/linux-*
make ARCH=riscv syncconfig
```

> ⚠️ 必须带 `ARCH=riscv`。不带架构时 `make syncconfig` 会报
> `drivers/gpu/drm/Kconfig:8: recursive dependency detected`（DRM → DRM_CANAAN → OF →
> X86_INTEL_CE → ... → DRM 的环），这是 Kconfig 在未指定架构时把 **x86 的 Kconfig 也
> 拉进来一起解析**产生的假象，不是真实依赖问题。加 `ARCH=riscv` 后 `syncconfig` 干净通过。

---

## 构建后验证（重要）

如果本次目标是验证 kernel patch 是否真的进镜像，构建后至少做一项源码侧检查和一项实机侧检查。

### 源码侧检查

例如检查构建后的 `sdhci-of-kendryte.c`：

```bash
sed -n '292,310p' output/k230_rockbase_defconfig/build/linux-*/drivers/mmc/host/sdhci-of-kendryte.c
```

判读：

- 看到 `SDHCI_QUIRK_BROKEN_ADMA | SDHCI_QUIRK_BROKEN_DMA` → 还是 PIO 诊断态
- 只剩 `SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN` → DMA 已恢复

### 实机侧检查

烧录后看启动日志：

```bash
ssh root@<IP> 'dmesg | grep -iE "mmc[01]: SDHCI controller|using PIO|using ADMA|using DMA"'
```

### 统一镜像验证

```bash
# 镜像层验证（任一 FAIL 都视为镜像不合格）
ssh root@<IP> '
echo "[release]"; cat /etc/version/release_version
echo "[netsel]"; cat /tmp/rockbase-net-mode 2>/dev/null
echo "[iface]"; ip link show | awk -F: "/^[0-9]+:/ {print \$2}" | sed "s/^ //"
echo "[ko]"; find /lib/modules -name "8189fs.ko" | head -3
[ -f /lib/modules/*/kernel/drivers/net/wireless/8189fs/8189fs.ko ] && echo "OK:8189fs.ko present" || echo "FAIL:no 8189fs.ko"
command -v wpa_supplicant >/dev/null && echo "OK:wpa_supplicant present" || echo "FAIL:no wpa_supplicant"
command -v iwconfig >/dev/null && echo "OK:iwconfig present" || echo "FAIL:no iwconfig"
[ -x /usr/bin/rockbase_netsel.sh ] && echo "OK:netsel present" || echo "FAIL:no netsel"
'

# 运行时按硬件组合验证（NET_MODE 必须与所插硬件匹配）
# - 插 USB 有线网卡: NET_MODE=usb_eth, eth* up, lsmod 无 8189fs
# - 插 USB WiFi 狗:  NET_MODE=usb_wifi, wlan* 连接, lsmod 无 8189fs
# - 什么都不插:      NET_MODE=sdio_wifi, lsmod 有 8189fs, wlan0 走 SDIO
ssh root@<IP> '
echo "[mode]"; grep NET_MODE /tmp/rockbase-net-mode
echo "[8189fs]"; lsmod | grep 8189fs || echo "(not loaded)"
echo "[netsel log]"; tail -5 /tmp/rockbase-netsel.log
'
```

烧录时优先使用目录名带后缀的镜像：
- `-min-` 对应最小镜像（非 seed，`K230_BLOCKDATA_SIZE=256M`）
- `-full-` 对应完整镜像（带 seed，`K230_BLOCKDATA_SIZE=20G`）
- `-net-` 对应统一网络镜像（2026-08-20 起；旧镜像为 `-sdio-`/`-usb-`）

若实机行为与后缀不一致，先排查误烧录或混用旧目录，再排查构建流程。

不要只看源码目录或 commit 信息就默认镜像行为已经变化。

---

## LVGL UI 重建规则

`rockbase_lcdui` 是 `SITE_METHOD=local` 的本地源码包，buildroot 用 rsync 把源码拷贝进 build 目录后原地编译。

**已根治自动重编：** 改写了 buildroot 的 `pkg-generic.mk`（经 overlay 覆盖到 `package/pkg-generic.mk`），让 local 包的 `.stamp_rsynced` 步骤**每次构建都执行增量 rsync**，并用 `rsync -i --out-format='%n'` 检测源码变化——一旦有文件变化就自动删除 `.stamp_built` / install stamp，触发重编 + 重装。

所以现在**改了 local 包源码后，直接跑顶层 `make buildroot` 就会自动重编**，无需手动 `<pkg>-rebuild`。

- 实现要点：`.stamp_rsynced` 加 `FORCE` 伪依赖每次重跑；rsync 变化列表写到 `/tmp`（不放 build 目录，避免目录 mtime 污染）；configure 步骤因 order-only 依赖不会被级联重跑。
- 覆盖范围：对所有 `SITE_METHOD=local` 包（`rockbase_lcdui` / `display` / `aic8800` / `aic8800_sdio` / `bcmdhd` / `vg_lite` / `yolo` / `vvcam` / `mvx_player` 等 26 个）统一生效。

**仍可用 `<pkg>-rebuild` 强制全量重建**（会删除全部 stamp）：

```bash
make CONF=k230_rockbase_defconfig rockbase_lcdui-rebuild -j$(nproc)
```

**构建后校验产物是否同步最新源码（md5 相同 = build 目录已是最新）：**

```bash
md5sum buildroot-overlay/package/rockbase_lcdui/src/main.cpp \
        output/k230_rockbase_defconfig/build/rockbase_lcdui/main.cpp
```

---

## Overlay 同步陷阱（重要）

`tools/sync.mk` 的 `.overlay_sync` stamp 只对 `buildroot-overlay/` 文件的**新增/删除**敏感，对**内容修改不敏感**：

```make
BR_OVERLAY_FS=$(shell find $(BR_OVERLAY_DIR) -type f ...)
$(BR_SRC_DIR)/.overlay_sync: $(BR_SRC_DIR)/.download_extract $(BR_OVERLAY_FS)
	rsync -a $(BR_OVERLAY_DIR)/ $(BR_SRC_DIR)/ ...
```

`BR_OVERLAY_FS` 只列**文件名**（不含 mtime/内容哈希），所以：

- **新增/删除** overlay 文件 → 列表变化 → `.overlay_sync` 重跑 rsync ✅
- **只改内容**（文件名不变）→ 列表不变 → `.overlay_sync` **不重跑**，buildroot 源码里仍是旧版 ❌

**影响范围**：`buildroot-overlay/` 下所有顶层/非包目录文件，典型如 `package/pkg-generic.mk`、`package/Config.in`、`support/dependencies/*.mk`。包目录（`package/<pkg>/`）的 `.mk` 改动同理——但包 `.mk` 改动通常伴随源码改动，且 local 包源码改动会触发上面 LVGL 章节的 rsync 重编，所以最容易被忽略的是**纯构建系统文件**的内容修改。

**正确做法**：改了这类文件内容后，先手动 sync 再构建：

```bash
make sync
make CONF=k230_rockbase_defconfig buildroot -j$(nproc)
```

> 本次 `pkg-generic.mk` 是**新增文件**，所以首次构建会自动 sync 生效；但**以后若只改它的内容**，必须手动 `make sync`。

## 镜像产物说明

| 产物 | 路径 |
|------|------|
| 构建中间文件 | `output/<CONF>/images/blockdata.ext4`、`sysimage-sdcard.img` |
| Windows 导出 | `/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-{min\|full}-{net}-<timestamp>/sysimage-sdcard.img` |

非 seed 镜像预期大小：`~5.2G ~ 5.4G`（slot_a 2G + slot_b 2G + config 1G + blockdata 256M + 分区开销）。

如果镜像约 32G，说明走到了 seed 路径或默认 20G blockdata，需要加 `K230_BLOCKDATA_SIZE=256M` 重新构建。

---

## 完整镜像（Seed 镜像）

完整镜像 = 带 seed 的镜像。构建流程、seed 同步、容量规则等详见 `rockbase-seed` skill。除非用户明确说"带 seed"、"完整镜像"，不触发 seed 流程。

## 板子信息

当前板子、MMC 拓扑等常驻信息见 `AGENTS.md`。
