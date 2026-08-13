# Project Memory — Rockbase

## 项目身份

- Primary board: `Rockbase` (K230-based Bitcoin pruned validating node + local Stratum/solo mining)
- Primary defconfig: `k230_rockbase_1g_defconfig` (1G), `k230_rockbase_2g_defconfig` (2G)
- Web root: `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web`
- Rootfs overlay: `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay`

## 板子基线

| IP | 型号 | MMC 拓扑 |
|----|------|----------|
| `192.168.124.8` | 1G | mmc0=SDIO WiFi, mmc1=SD card |
| `192.168.124.15` | 1G | mmc0=SDIO WiFi, mmc1=SD card |
| `192.168.124.21` | 1G | mmc0=SDIO WiFi, mmc1=SD card |
| `192.168.124.23` | 2G | mmc0=SD card, mmc1=SDIO WiFi |

## Seed 源设备

| IP | 型号 | 凭据 | bitcoind datadir |
|----|------|------|------------------|
| `192.168.124.12` | RDK X5 (Ubuntu 22.04) | `sunrise` / `sunrise` | `/home/sunrise/Desktop/bitcoind/data` |

Seed 同步流程见 skill: `rockbase-seed`。

**注意：1G/2G 的 MMC host 编号相反。** 分析日志时不要固定假设 mmc0/mmc1 角色，必须结合当次启动日志确认。

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
任何 `rockbase_lcdui`/LVGL UI 改动后，**必须**用 `rockbase_lcdui-rebuild` 单独重建，不要依赖顶层 `buildroot` 自动发现。
→ 详细流程见 skill: `rockbase-lvgl-ui`

### 6. WSL `/mnt/d` 挂载判断
不要只根据沙箱报错判断 `/mnt/d` 只读。必须用 `findmnt` + `touch` 做真实写测试。以提权/非沙箱检查为准。

### 7. Windows 导出规则
导出到 `/mnt/d/code/pony/IoT/k230/img/`。如果沙箱写失败，不要停，请求提权重试。

### 8. CMA 基线
64M CMA 是 1G/2G 当前工作基线。如果重新调优：先测 32M，再 16M；128M 仅在 64M 出现 DMA/WiFi 不稳定时使用。

## 当前诊断状态

- 最新关键提交: `936ea15b`（禁用 `0034`，源码默认恢复 DMA）
- `dev` 分支源码已不再应用 `0034-rockbase-sdhci-force-pio-dma-diagnostic.patch`，**重新构建的新镜像默认恢复 DMA**
- 注意：`1G` 和 `2G` 各自复用独立的 `output/<CONF>/build/linux-*`；**共享 kernel patch 变更后，必须对每个目标 defconfig 单独执行 `linux-dirclean` 再重编**，否则可能导出旧的 PIO kernel
- Rootfs 基线: 2G，非 6G
- Swap 已禁用

## Skill 索引

| 场景 | 使用 Skill |
|------|-----------|
| 构建镜像、导出、切换网络模式（K230_NET） | `rockbase-build` |
| 热部署、固化 | `rockbase-hotfix` |
| 卡死/死锁调试 | `rockbase-freeze-debug` |
| Seed 镜像管理 | `rockbase-seed` |
| LVGL/LCD UI 开发 | `rockbase-lvgl-ui` |
| Web/API 开发 | `rockbase-web` |

## 参考文档

- `docs/rockbase-project-overview.md` — 完整项目背景
- `docs/rockbase-debug-playbook.md` — 卡死调试手册
- `docs/rockbase-freeze-milestones.md` — Freeze 排查里程碑
- `docs/rockbase-block-seed-image-workflow.md` — Seed 镜像详细流程
- `docs/rockbase-cpu1-linux-only-plan.md` — CPU1 实验方案
- `docs/design/` — 架构设计文档
