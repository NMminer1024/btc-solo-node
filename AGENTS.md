# Project Memory — Rockbase

## 项目身份

- Primary board: `Rockbase` (K230-based Bitcoin pruned validating node + local Stratum/solo mining)
- Primary defconfig: `k230_rockbase_defconfig`（1G/2G 统一；内存大小由 U-Boot 运行时探测，见 [profile 合并记录](#profile合并记录)）
- Web root: `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web`
- Rootfs overlay: `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay`

## 板子基线

| IP | 型号 | MMC 拓扑 |
|----|------|----------|
| `192.168.124.15` | 1G | mmc0=SDIO WiFi, mmc1=SD card |
| `192.168.124.23` | 2G | mmc0=SDIO WiFi, mmc1=SD card（已统一接线） |
| `192.168.124.30` | 1G | mmc0=SDIO WiFi, mmc1=SD card；当前烧 usb 模式镜像 |
| `192.168.124.35` | 1G | mmc0=SDIO WiFi, mmc1=SD card (SD64G) |

## Seed 源设备

| IP | 型号 | 凭据 | bitcoind datadir |
|----|------|------|------------------|
| `192.168.124.12` | RDK X5 (Ubuntu 22.04) | `sunrise` / `sunrise` | `/home/sunrise/Desktop/bitcoind/data` |
| `192.168.124.32` | Raspberry Pi 4B Rev 1.4 | `bitpony` / `qawsed123` | `/home/bitpony/block` |

Seed 同步流程见 skill: `rockbase-seed`。

**注意：所有板子已统一接线为 mmc0=SDIO WiFi、mmc1=SD card（SD 卡走 SDIO1 启动）。** 若遇到早期 01studio 接线的旧板，仍需结合当次启动日志确认。

## 始终生效的核心规则

### 1. 调试工作流（热部署优先）
每次改功能/页面，**先 `scp`/`ssh` 热部署到真实板子验证**，验证通过后再写回源码。不要反过来。
→ 详细流程见 skill: `rockbase-hotfix`

### 2. 变更追踪
热调试期间必须追踪所有改动过的业务文件，固化时逐项勾销，不要靠猜。

### 3. "打进镜像"规则
当用户说"打进镜像"时，只固化已验证的热部署改动到源码。**不要编译或导出新镜像**，除非用户明确要求编译。

### 4. Seed 镜像默认
**默认构建非 seed 最小镜像。** 只有用户明确说"带 seed"才构建 seed 镜像。
→ 构建命令见 skill: `rockbase-build`，seed 管理见 skill: `rockbase-seed`

### 5. LVGL UI 重建规则
local 包（`rockbase_lcdui` 等）源码改动后，顶层 `buildroot` 构建**会自动** rsync 检测变化并重编（已通过改写 `buildroot-overlay/package/pkg-generic.mk` 根治，`.stamp_rsynced` 每次增量 rsync + 变化时删除 build/install stamp）。仍可手动 `<pkg>-rebuild` 强制全量重建。
→ 详细流程见 skill: `rockbase-lvgl-ui`

### 5b. Overlay 同步陷阱
`tools/sync.mk` 的 `.overlay_sync` 只对 `buildroot-overlay/` 文件**新增/删除**敏感，对**内容修改不敏感**（`BR_OVERLAY_FS` 只列文件名）。改了 `package/pkg-generic.mk`、`package/Config.in`、`support/` 等顶层/非包目录文件的**内容**后，必须先 `make sync` 再构建，否则 buildroot 源码里仍是旧版。

### 6. WSL `/mnt/d` 挂载判断
不要只根据沙箱报错判断 `/mnt/d` 只读。必须用 `findmnt` + `touch` 做真实写测试。以提权/非沙箱检查为准。

### 7. Windows 导出规则
导出到 `/mnt/d/code/pony/IoT/k230/img/`。如果沙箱写失败，不要停，请求提权重试。

### 8. CMA 基线
64M CMA 是当前工作基线。如果重新调优：先测 32M，再 16M；128M 仅在 64M 出现 DMA/WiFi 不稳定时使用。

## 当前诊断状态

- 最新关键提交: `936ea15b`（禁用 `0034`，源码默认恢复 DMA）
- `dev` 分支源码已不再应用 `0034-rockbase-sdhci-force-pio-dma-diagnostic.patch`，**重新构建的新镜像默认恢复 DMA**
- 注意：统一后仅单一 `output/k230_rockbase_defconfig/build/linux-*`；**共享 kernel patch 变更后，执行 `linux-dirclean` 再重编**，否则可能导出旧的 PIO kernel
- Rootfs 基线: 2G，非 6G
- Swap 已禁用

## Profile 合并记录

**2026-09（本次变更）：1G/2G profile 合并为单一 `k230_rockbase_defconfig`。**

- 原 `k230_rockbase_1g_defconfig` → 重命名为 `k230_rockbase_defconfig`；原 `k230_rockbase_2g_defconfig` 及全部 2G 专属文件已删除（含 Linux DTS `k230-rockbase-2g-lcd.dts`、U-Boot `k230_rockbase_2g_defconfig`/`k230_rockbase_2g.dts`、`0031-rockbase-2g-touchscreen-size.patch`、`linux_2g_nocma.fragment`）。
- 内存大小（1G/2G）由 U-Boot `detect_ddr_size()` **运行时探测**，DDR 训练参数两板一致（LPDDR4 2667），故无需按内存拆分 profile。
- 统一后固定接线：mmc0=SDIO WiFi、mmc1=SD card，U-Boot 从 SDIO1 启动。
- 导出镜像目录前缀统一为 `sysimage-sdcard-rockbase-<build_stamp>`（去掉了 `-1g`/`-2g` 后缀）。
- 若今后再出现需要按硬件区分的变体，恢复独立 defconfig 前先确认是否真的无法运行时探测。

## Skill 索引

| 场景 | 使用 Skill |
|------|-----------|
| 构建镜像、导出、切换网络模式（K230_NET） | `rockbase-build` |
| 热部署、固化 | `rockbase-hotfix` |
| 卡死/死锁调试 | `rockbase-freeze-debug` |
| Seed 镜像管理 | `rockbase-seed` |
| LVGL/LCD UI 开发 | `rockbase-lvgl-ui` |
| Web/API 开发 | `rockbase-web` |
| 网络测速（停服务→验CPU→SSH吞吐测试） | `rockbase-netspeed` |
| CPU 压力分布测试（按页面/进程，`/root/script/cpu_page.sh`） | `rockbase-cpupress` |
| OTA 发布到 GitHub Releases（打版本包/签名/发布到公开发布仓库） | `rockbase-ota-release` |
| OTA 升级真机（「ota升级124.30」→ 触发/监控/验证） | `rockbase-ota` |
| 产品可读版本号（VERSION 文件、vX.Y.Z 含义、升版流程） | `rockbase-version` |

> **OTA 完整链路顺序**：`rockbase-build`（编译镜像）→ `rockbase-ota-release`（打包签名并发布到
> `NMminer1024/btc-solo-node-release`）→ `rockbase-ota`（真机 apply，统一用生产域名
> `ota.nmminer.com` 或 GitHub 直链）。
> 三步之间必须核对**签名密钥一致性**（`~/.rockbase-ota-keys/ota-pub.pem` md5 需同时等于源码
> `buildroot-overlay/.../etc/rockbase-ota-pub.pem` 和目标设备当前 `/etc/rockbase-ota-pub.pem`），
> 详见 `docs/rockbase-ota-handoff.md` 第 13.4 节与 repo memory `ota-signing-key.md`。

## 参考文档

- `docs/rockbase-project-overview.md` — 完整项目背景
- `docs/rockbase-debug-playbook.md` — 卡死调试手册
- `docs/rockbase-freeze-milestones.md` — Freeze 排查里程碑
- `docs/rockbase-block-seed-image-workflow.md` — Seed 镜像详细流程
- `docs/rockbase-cpu1-linux-only-plan.md` — CPU1 实验方案
- `docs/design/` — 架构设计文档
