# Rockbase OTA 升级方案（保用户参数 · A/B 双槽 · 自动回滚）

- 版本：v1.0（设计定稿，待实施 P1）
- 日期：2026-08-21
- 适用范围：Rockbase 1G / 2G（K230，SD 卡启动）

## 0. 目标与结论

目标：设备全部到客户手上后，可远程 OTA **只升级系统，不覆盖用户参数**。

结论：**当前架构不支持**，存在 4 个硬性缺口；但缺口清晰、改造可控。推荐方案为
**「A/B 双槽 + 用户配置外置到 blockdata + uboot 回滚 + shell OTA agent」**。

硬性缺口：

1. 用户配置（WiFi / bitcoind / ckpool）混在 rootfs p2 内 —— 升 rootfs 必然覆盖。
2. 单 rootfs，无 A/B；blockdata 首启已扩到卡尾 —— 整卡 dd 会毁掉用户区块数据，
   单独 dd p2 断电即变砖、无回退。
3. uboot 无 `CONFIG_BOOTCOUNT_LIMIT` / `altbootcmd`，无回滚能力。
4. rootfs 缺 OTA 工具链：仅 busybox wget（无 TLS）、无 curl、无 openssl 二进制、无 `fw_setenv`。
   buildroot 2025.02.1 源码树内 `curl` / `libubootenv` / `openssl` / `ca-certificates` 均现成可用。

有利条件：

- rootfs 实际占用仅 ~170MB（2G 镜像 92% 空闲），A/B 双 512M 槽完全放得下。
- uboot `board_fdt_chosen_bootargs()`（k230_img.c）**优先读 env `bootargs`**，
  因此 A/B 切换 `root=` 无需改任何 C 代码。
- blockdata 保持 p3，现有扩容/挂载脚本基本零改动。

---

## 1. 当前镜像逻辑（基线事实）

### 1.1 分区布局（buildroot-overlay/board/canaan/k230_rockbase/genimage.cfg）

| 区域 | 偏移 | 大小 | 内容 |
|---|---|---|---|
| SPL ×2 | 1M / 0x180000 | raw | `fn_u-boot-spl.bin` |
| uboot env | 0x1e0000 | 64K 有效 | uboot 实际使用（`CONFIG_ENV_OFFSET=0x1e0000`） |
| uboot | 2M | raw | `fn_ug_u-boot.bin` |
| env（冗余拷贝） | 0x380000 | 128K | 静态写入，uboot 不读 |
| p1 boot | 30M | 80M ext4 | `Image` / `k.dtb` / `fw_jump_add_uboot_head.bin` |
| p2 rootfs | 128M | 2G ext4 | 完整系统（实际仅用 ~170MB） |
| p3 blockdata | ~2.2G | 构建时 256M/24G | 首启被 S00resizemmc 扩到卡尾，挂载 `/root/block` = bitcoind datadir |

### 1.2 启动链

```
SPL → uboot → run blinux
  ext4load mmc ${mmc_boot_dev_num}:1 fw_jump / Image / k.dtb → bootm
```

- `mmc_boot_dev_num` 由 board.c 按启动源运行时设置（1G=1，2G=0）。
- 内核 `root=`：`board_fdt_chosen_bootargs()`（board/canaan/common/k230_img.c）
  先读 env `bootargs`；为空则按 bootmode 硬编码 `root=/dev/mmcblk{0,1}p2 rw`。

### 1.3 用户参数落点（OTA 的核心约束）

| 用户参数 | 文件 | 所在分区 | 修改入口 |
|---|---|---|---|
| WiFi SSID/密码 | `/etc/rockbase-wifi.conf` | rootfs p2 | Web `/api/wifi.cgi` |
| bitcoind 配置 | `/root/bitcoind/bitcoin.conf` | rootfs p2 | SSH |
| ckpool 矿池/BTC 地址 | `/root/ckpool/ckpool.conf` | rootfs p2 | SSH |
| 区块数据 | `/root/block/{blocks,chainstate}` | blockdata p3 | 自动 |
| 初始默认 WiFi | uboot env `wlanssid/wlanpass` | raw env | 出厂 |
| LCD 配置 | `/etc/rockbase-lcdui.conf` | rootfs p2 | 编译期固定（随系统升级） |

---

## 2. 目标架构

### 2.1 新分区布局（出货前必须固化）

```
raw 区不变：SPL / env@0x1e0000 / uboot / env@0x380000
p1 = slot_a  512M ext4   完整系统，内含 /boot/{Image, k.dtb, fw_jump_add_uboot_head.bin}
p2 = slot_b  512M ext4   出厂时为 slot_a 的拷贝
p3 = blockdata           剩余全部（分区号不变）
```

要点：

- 内核/dtb/opensbi 收进各槽 `/boot/`，每槽自包含、版本自洽；uboot 只是按槽号
  ext4load（机制与今天读 boot.ext4 完全相同）。取消 80M 共享 boot 分区。
- rootfs 由 2G 缩到 512M（占用 170M，约 3 倍余量）。
- blockdata 保持 p3：S00resizemmc 的 p3 扩容与 S59blockdata 挂载逻辑不动；
  OTA 永远只写 p1/p2，p3 上的区块与用户配置物理上不可能被碰。

### 2.2 启动与回滚（uboot 层：纯 env 脚本 + 1 个 defconfig 开关）

default.env 改为：

```bash
ota_part=1                      # 1=slot_a, 2=slot_b
bootlimit=3
upgrade_available=0
blinux=ext4load mmc ${mmc_boot_dev_num}:${ota_part} 0x3000000 /boot/fw_jump_add_uboot_head.bin \
    && ext4load mmc ${mmc_boot_dev_num}:${ota_part} 0x200000 /boot/Image \
    && ext4load mmc ${mmc_boot_dev_num}:${ota_part} 0x2200000 /boot/k.dtb \
    && setenv bootargs root=/dev/mmcblk${mmc_boot_dev_num}p${ota_part} rw rootwait rootfstype=ext4 console=ttyS0,115200 earlycon=sbi \
    && bootm 0x3000000 - 0x2200000
altbootcmd=if test "${ota_part}" = 1; then ota_part=2; else ota_part=1; fi; \
    setenv bootcount 0; saveenv; run blinux
```

- `board_fdt_chosen_bootargs()` 优先吃 env `bootargs`，`root=` 切换不改 C 代码。
- uboot defconfig `k230_rockbase_defconfig` 增加 `CONFIG_BOOTCOUNT_LIMIT=y`：
  每次启动 bootcount+1，超过 bootlimit 自动执行 altbootcmd 回旧槽。
  env 位于 MMC raw 区可写，bootcount 直接落 env。

### 2.3 用户配置外置到 blockdata（"保参数"的核心）

- blockdata 新建 `/root/block/config/`，集中存放：
  `rockbase-wifi.conf`、`bitcoin.conf`、`ckpool.conf`，以及未来 web 新增的所有用户设置。
- rootfs 内改为符号链接：
  `/etc/rockbase-wifi.conf`、`/root/bitcoind/bitcoin.conf`、`/root/ckpool/ckpool.conf`
  → `/root/block/config/…`
- blockdata 挂载提前到 S05 左右（挂载无需等 resize_done；S38wifi 在 S37 之后，时序满足）。
  挂载改用 `LABEL=blockdata`（mkfs 已打 label），比 p3 硬编码更稳。
- 首启播种：`/root/block/config/` 为空时从 `/etc/rockbase-factory-defaults/` 拷贝默认配置；
  blockdata 缺失时各服务回退内置默认，保证可启动。
- 效果：OTA、甚至整系统重刷（不动 p3）都不会丢用户参数。

### 2.4 OTA agent（全 shell，符合项目现有风格）

新文件 `/usr/sbin/rockbase-ota.sh`，子命令：`status / check / apply / confirm / rollback`。

包格式 `rockbase-ota-<ver>.tar.gz`：

```
manifest          # version、min_version、slot 镜像 sha256、目标大小
slot.img.gz       # 512M 槽镜像 gzip（实际约 150~200MB）
signature         # 可选（P2 阶段，openssl dgst 验签）
```

apply 流程：

1. `fw_printenv ota_part` 得当前槽 → 目标槽 = 另一个。
2. `curl --cacert /etc/ssl/certs/…` 从固定 HTTPS 地址下载到 `/root/block/ota/`（空间充足）。
3. `sha256sum -c` 校验 → `gunzip | dd of=/dev/mmcblkXp<目标槽> bs=4M conv=fsync`
   （只写非活动槽，中断零风险）。
4. 只读挂载目标槽，核对 `/etc/version/release_version` 与 manifest 一致。
5. `fw_setenv ota_part=<T> bootcount=0 upgrade_available=1` → `reboot`。

确认服务 新增 `S98otaconfirm`：启动后等待 web/ssh/bitcoind 健康（如 120s 内），
执行 `fw_setenv bootcount=0 upgrade_available=0`；连续 3 次未确认 → uboot 自动回旧槽。

Web 集成：新增 `/root/web/api/ota.cgi`（status/check/apply + 进度日志轮询），
前端加「系统升级」卡片。发布侧 = 任意静态 HTTPS（GitHub Releases 即可）：
`manifest.json` + 包文件。

---

## 3. 改动清单（文件级）

| # | 文件 | 改动 |
|---|---|---|
| 1 | buildroot-overlay/board/canaan/k230_rockbase/genimage.cfg | 删 boot 分区；p1/p2 两个 512M 槽；blockdata 跟随 |
| 2 | buildroot-overlay/configs/k230_rockbase_{1g,2g}_defconfig | EXT2_SIZE 2G→512M；新增 `BR2_PACKAGE_LIBUBOOTENV`、`BR2_PACKAGE_LIBCURL(+_CURL/_OPENSSL)`、`BR2_PACKAGE_CA_CERTIFICATES`（可选 `BR2_PACKAGE_OPENSSL_BIN`） |
| 3 | buildroot-overlay/board/canaan/k230_rockbase/default.env | A/B bootcmd / altbootcmd / ota_part / bootlimit |
| 4 | buildroot-overlay/boot/uboot/u-boot-2022.10-overlay/configs/k230_rockbase_defconfig | `CONFIG_BOOTCOUNT_LIMIT=y` |
| 5 | post-build.sh | 生成 `fw_jump_add_uboot_head.bin` 并把 Image/k.dtb 装入 `${TARGET_DIR}/boot/`；安装 factory-defaults |
| 6 | post-image.sh | slot_b 由 slot_a 复制；导出逻辑保留；新增 OTA 包产物输出 |
| 7 | rootfs_overlay/etc/init.d/S00resizemmc | `fw_env.config` 生成移到 done 检查之前（当前在 `/resize_done` 后直接退出，dd 出来的新槽会缺此文件导致 `fw_setenv` 失败） |
| 8 | rootfs_overlay/etc/init.d/S59blockdata | 拆出早期挂载（新 `S05blockdata`，按 LABEL 挂载，不等 resize_done） |
| 9 | overlay 新增 | `/usr/sbin/rockbase-ota.sh`、`/etc/init.d/S98otaconfirm`、`/root/web/api/ota.cgi`、`/etc/rockbase-factory-defaults/`、3 个配置符号链接 |
| 10 | tools/ 新增 | `mk-ota-package.sh`（构建 OTA 包 + manifest） |

注意：共享 kernel patch / uboot overlay 变更后，1G/2G 两个 defconfig 要各自
`linux-dirclean`（uboot 同理）再重编，避免导出旧产物（项目已有教训）。

---

## 4. 出货与迁移策略

- 新分区布局必须在量产前固化。旧布局（boot + 单 rootfs + blockdata 已扩卡尾）的设备
  无法无损迁移到新布局（分区表变化必然动 blockdata）。属「发货前必做」项。
- 现有 4 台开发板（192.168.124.15 / 23 / 30 / 35）重烧新镜像即可，
  blockdata 数据重同步或重灌 seed。
- uboot / SPL 不纳入常规 OTA（raw 区刷写风险大）；确有需求走单独特殊包。

---

## 5. 验证计划（真实板）

1. 新镜像烧录 → 确认 slot A 启动、blockdata 扩容/挂载正常、WiFi 配置落在 p3。
2. 构造 v2 假版本 OTA 包 → apply → 确认切槽、版本号变化、
   WiFi / ckpool / bitcoin 配置原样保留。
3. dd 写槽中途断电 → 设备仍从旧槽正常启动。
4. 切槽后人为破坏新槽 → 3 次启动后自动回滚旧槽。
5. 1G（mmcblk1）与 2G（mmcblk0）各跑一遍，验证设备号自适应。
6. 回归：seed 镜像构建、USB / SDIO 网络模式切换不受影响。

---

## 6. 分期实施

- P1（出货前必须）：分区布局 + uboot A/B/回滚 + 配置外置 + OTA 脚本（URL 拉取）+ 确认服务。
- P2（可后置，OTA 自举升级）：Web 升级页、HTTPS 验签、断点续传。
- P3（可选）：小 recovery 分区兜底；swupdate 替换自研脚本（buildroot 已有该包，
  但项目惯例为 shell，P1 用脚本更顺）。

建议实施顺序（每步先热部署到 192.168.124.15 验证再固化）：
配置外置 → 分区/uboot → OTA agent。
