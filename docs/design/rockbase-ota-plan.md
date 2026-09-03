# Rockbase OTA 升级方案（保用户参数 · A/B 双槽 · 自动回滚）

> **⚠️ 2026-09 profile 合并说明**：1G/2G 已合并为单一 `k230_rockbase_defconfig`（内存由 U-Boot 运行时探测）。本文正文中的 defconfig 引用为当时历史记录，现已过时。详见 `AGENTS.md`「Profile 合并记录」。

- 版本：v1.0（设计定稿，待实施 P1）
- 日期：2026-08-21
- 适用范围：Rockbase 1G / 2G（K230，SD 卡启动）

## 0. 目标与结论

目标：设备全部到客户手上后，可远程 OTA **只升级系统，不覆盖用户参数**。

结论：**当前架构不支持**，存在 4 个硬性缺口；但缺口清晰、改造可控。推荐方案为
**「A/B 双槽 + 用户配置独立分区 + uboot 回滚 + shell OTA agent」**。

硬性缺口：

1. 用户配置（WiFi / bitcoind / ckpool）混在 rootfs p2 内 —— 升 rootfs 必然覆盖。
2. 单 rootfs，无 A/B；blockdata 首启已扩到卡尾 —— 整卡 dd 会毁掉用户区块数据，
   单独 dd p2 断电即变砖、无回退。
3. uboot 无 `CONFIG_BOOTCOUNT_LIMIT` / `altbootcmd`，无回滚能力。
4. rootfs 缺 OTA 工具链：仅 busybox wget（无 TLS）、无 curl、无 openssl 二进制、无 `fw_setenv`。
   buildroot 2025.02.1 源码树内 `curl` / `libubootenv` / `openssl` / `ca-certificates` 均现成可用。

有利条件：

- rootfs 实际占用仅 ~71MB（2G 镜像 96% 空闲），A/B 双 2G 槽在 SD 卡上轻松容纳。
- uboot `board_fdt_chosen_bootargs()`（k230_img.c）**优先读 env `bootargs`**，
  因此 A/B 切换 `root=` 无需改任何 C 代码。
- blockdata 仍由现有脚本扩容/挂载（改为按 LABEL 识别，分区号 p3→p4 见 2.3）。

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
| p3 config | 紧随 p2 | 256M ext4 `LABEL=rockcfg` | A1 v2 新增：用户配置分区，挂载 `/root/config` |
| p4 blockdata | 紧随 p3 | 构建时 256M/20G | 首启被 S00resizemmc 扩到卡尾，挂载 `/root/block` = bitcoind datadir |

> 注：A1 v2 之前的老镜像为「p1 boot / p2 rootfs / p3 blockdata（扩到卡尾）」，
> 无 config 分区。开发期不做兼容。

### 1.2 启动链

```
SPL → uboot → run blinux
  ext4load mmc ${mmc_boot_dev_num}:1 fw_jump / Image / k.dtb → bootm
```

- `mmc_boot_dev_num` 由 board.c 按启动源运行时设置（mmc 拓扑已统一：mmc0=SDIO WiFi、mmc1=SD，SD 启动为 1）。
- 内核 `root=`：`board_fdt_chosen_bootargs()`（board/canaan/common/k230_img.c）
  先读 env `bootargs`；为空则按 bootmode 硬编码 `root=/dev/mmcblk{0,1}p2 rw`。

### 1.3 用户参数落点（OTA 的核心约束）

| 用户参数 | 文件 | 所在分区 | 修改入口 |
|---|---|---|---|
| WiFi SSID/密码 | `/root/config/rockbase-wifi.conf` | config p3 | Web `/api/wifi.cgi` |
| bitcoind 配置 | `/root/config/bitcoin.conf` | config p3 | SSH |
| ckpool 矿池/BTC 地址 | `/root/config/ckpool.conf` | config p3 | SSH |
| 区块数据 | `/root/block/{blocks,chainstate}` | blockdata p4 | 自动 |
| 初始默认 WiFi | uboot env `wlanssid/wlanpass` | raw env | 出厂 |
| LCD 配置 | `/etc/rockbase-lcdui.conf` | rootfs p2 | 编译期固定（随系统升级） |

---

## 2. 目标架构

### 2.1 新分区布局（出货前必须固化）

```
raw 区不变：SPL / env@0x1e0000 / uboot / env@0x380000
p1 = slot_a  2G ext4   完整系统，内含 /boot/{Image, k.dtb, fw_jump_add_uboot_head.bin}
p2 = slot_b  2G ext4   出厂时为 slot_a 的拷贝
p3 = config  256M ext4  用户配置分区（LABEL=rockcfg，挂载 /root/config）
p4 = blockdata          剩余全部（LABEL=blockdata，挂载 /root/block）
```

要点：

- 内核/dtb/opensbi 收进各槽 `/boot/`，每槽自包含、版本自洽；uboot 只是按槽号
  ext4load（机制与今天读 boot.ext4 完全相同）。取消 80M 共享 boot 分区。
- **根分区定为 2G**（`EXT2_SIZE` 维持 2G 不变）。选型理由见 2.6：由于 gzip 会把
  空闲块压掉，2G 与更小 FS 的 OTA 下载体积几乎相同，缩 FS 只能省 dd 刷写时间；
  保留 2G 换来充足的未来升级余量，且**改动最小、风险最低**（与当前 rootfs 等大，
  无需重调构建参数与挂载/扩容逻辑）。
- blockdata 为 p4（config 占据 p3）：S00resizemmc 的扩容对象改为 p4，
  S59blockdata 挂载改为按 LABEL 识别；OTA 永远只写 p1/p2，
  p3/p4 上的用户配置与区块数据物理上不可能被碰。
- 卡容量要求：A/B 双 2G 槽 + config + blockdata，建议 **≥ 8G SD 卡**（当前客户基线已满足）。

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

### 2.3 用户配置独立分区（"保参数"的核心，A1 v2 定稿）

- **独立配置分区 p3**（256M，ext4，`LABEL=rockcfg`，挂载 `/root/config`），
  与 blockdata（p4，`LABEL=blockdata`）并列。OTA 刷 rootfs 槽不碰它，
  **重灌区块 seed（mkfs blockdata）也不碰它**——用户参数与区块数据彻底解耦。
  256M 的余量也为后续自定义图片/动画等 web 资产预留空间。
- 集中存放：`rockbase-wifi.conf`、`bitcoin.conf`、`ckpool.conf`，
  以及未来 web 新增的所有用户设置/自定义资产。
- **不用符号链接**（开发初期无向后兼容包袱）：所有消费方直接引用 `/root/config/…`。
  涉及 `S61bitcoind`、`S62ckpool`、`bitcoind/start.sh|stop.sh`、`ckpool/start.sh`、
  `rockbase_statusd.sh`、`rockbase_bitcoind_ready.sh`、`rockbase_wifi.sh`（`WIFI_CONFIG_FILE` 默认值）。
- 配置分区挂载提前到 S05（新 `S05config`，按 LABEL 挂载，不等 resize_done；
  S38wifi 在 S37 之后，时序满足）。blockdata 仍由 S59 挂载（改为按 LABEL，回退 p4）。
- 首启播种：`/root/config/` 缺文件时从 `/etc/rockbase-factory-defaults/` 拷贝默认配置；
  配置分区缺失时 S05config 把出厂默认拷进 rootfs 的 `/root/config`（降级但可启动）。
- 分区布局（当前与阶段 D 一致）：

  ```
  现网过渡:  p1=boot  p2=rootfs    p3=config(256M)  p4=blockdata(扩到卡尾)
  阶段D:     p1=rootfsA(2G) p2=rootfsB(2G)  p3=config(256M)  p4=blockdata(扩到卡尾)
  ```

  S00resizemmc 的扩容对象相应从 p3 改为 p4。
- 效果：OTA、整系统重刷、blockdata 重灌，都不丢用户参数。

### 2.4 OTA agent（全 shell，符合项目现有风格）

新文件 `/usr/sbin/rockbase-ota.sh`，子命令：`status / check / apply / confirm / rollback`。

发布单元（不打包 tar，直接散放，便于断点续传与单独验签）：

```
manifest.json         # 见 2.5.3，版本/URL/sha256/大小/签名的索引
slot-<ver>.img.gz     # 2G 槽镜像 gzip（实测约 43MB，见 2.6）
slot-<ver>.img.gz.sig # 对 slot img.gz 的 RSA/SHA256 签名（openssl dgst）
```

apply 流程：

1. `fw_printenv ota_part` 得当前槽 → 目标槽 = 另一个。
2. `curl --cacert /etc/ssl/certs/ca-certificates.crt \
       --retry 5 -C - -fSL <manifest_url>` 先取 manifest 到 `/root/block/ota/`，
   再按其中 `url` 用 `-C -` **断点续传**下载 `slot.img.gz` 与 `.sig`。
3. **先验签**（2.5.2），再 `sha256sum -c` 完整性校验 →
   `gunzip | dd of=/dev/mmcblkXp<目标槽> bs=4M conv=fsync`
   （只写非活动槽，中断零风险）。
4. 只读挂载目标槽，核对 `/etc/version/release_version` 与 manifest 一致。
5. `fw_setenv ota_part=<T> bootcount=0 upgrade_available=1` → `reboot`。

确认服务 新增 `S98otaconfirm`：启动后等待 web/ssh/bitcoind 健康（如 120s 内），
执行 `fw_setenv bootcount=0 upgrade_available=0`；连续 3 次未确认 → uboot 自动回旧槽。

Web 集成：新增 `/root/web/api/ota.cgi`（status/check/apply + 进度日志轮询），
前端加「系统升级」卡片。

### 2.5 镜像源与安全（已定案）

已定决策：客户主要在**国外**、有自有域名 **nmminer.com**、倾向**纯免费 GitHub**、**上镜像签名**。

#### 2.5.1 源布局：自有域名分层 + GitHub Releases 后端

设备固件里**只写死自有域名**，不直接写 GitHub 地址。这样将来换/加后端
（Cloudflare R2、CDN、对象存储）都不需要改设备固件，只改 DNS。

```
设备请求  https://ota.nmminer.com/rockbase/manifest.json
              │  (DNS CNAME / 301 重定向)
              ▼
        GitHub Releases  <owner>/<repo> 的 release asset
```

- 主入口：`https://ota.nmminer.com/rockbase/manifest.json`
  - 做法 A（推荐，零运维）：`ota.nmminer.com` 做 **HTTP 301** 到
    `https://github.com/<owner>/<repo>/releases/latest/download/manifest.json`。
  - 做法 B：DNS CNAME 到一个能回源 GitHub 的静态托管（如 GitHub Pages / Cloudflare），
    由它代理 Release asset。
- 槽镜像等大文件：manifest 内的 `url` 字段直接指向 GitHub Releases 的
  `.../releases/download/<tag>/slot-<ver>.img.gz`（GitHub 支持 Range，curl `-C -` 可断点）。
- GitHub Releases 单文件上限 2GB，本方案 ~200MB 远低于上限，纯免费够用。

#### 2.5.2 签名校验（必须，P1 即上）

HTTPS 只保证「传输没被改」，不保证「源头是官方」。GitHub 账号/Token 一旦泄露，
攻击者可推送恶意固件。因此必须做**离线签名校验**：

- 发布端（构建机，私钥离线保存，**不入库**）：
  `openssl dgst -sha256 -sign ota-private.pem -out slot-<ver>.img.gz.sig slot-<ver>.img.gz`
- 设备端固化公钥 `/etc/rockbase-ota-pub.pem`（随 rootfs 打进镜像，只读）：
  ```sh
  openssl dgst -sha256 -verify /etc/rockbase-ota-pub.pem \
      -signature slot.img.gz.sig slot.img.gz
  ```
  验签失败 → 直接 abort，绝不写分区。
- 因此 defconfig 需加 `BR2_PACKAGE_OPENSSL_BIN`（提供 `openssl` 命令行用于 dgst 验签），
  curl 走 `BR2_PACKAGE_LIBCURL_OPENSSL`。

密钥管理：

- 私钥 `ota-private.pem` 离线冷备（U 盘/密码管理器），**绝不提交进 git**。
- 公钥 `ota-pub.pem` 提交进 rootfs overlay（`etc/rockbase-ota-pub.pem`）。
- 在 `.gitignore` 增加 `*-private.pem` 防误提交。

#### 2.5.3 manifest.json 结构

```json
{
  "product": "rockbase",
  "version": "v1.2.0",
  "min_version": "v1.0.0",
  "released": "2026-09-01",
  "slot": {
    "url": "https://github.com/<owner>/<repo>/releases/download/v1.2.0/slot-v1.2.0.img.gz",
    "sha256": "<hex>",
    "size": 204472320,
    "sig_url": "https://github.com/<owner>/<repo>/releases/download/v1.2.0/slot-v1.2.0.img.gz.sig"
  }
}
```

- `min_version` 用于跨大版本强制过渡（如必须先升中间版）。
- `size` 供下载前检查 blockdata 剩余空间、并在 dd 前核对解压后大小。

### 2.6 OTA 体积与刷写时间（实测，2G 根分区定案依据）

实测环境：`output/k230_rockbase_1g_defconfig`，`gzip -9`。

#### 2.6.1 三个易混的「大小」

| 概念 | 值 | 由谁决定 |
|---|---|---|
| **ext4 文件系统容量**（superblock block count） | 2.00 GB（524288 × 4096） | 构建时 `BR2_TARGET_ROOTFS_EXT2_SIZE="2G"` 写死 |
| **分区容量**（分区表 p2 物理大小） | ≥ FS 容量，否则截断损坏 | genimage.cfg 的 `size =` |
| **OTA 下载体积**（gzip 后） | ≈ FS 中实际数据量，与空闲无关 | 内容 + gzip |

关键：gzip 后的镜像**解压回 ext2 后，其声明的文件系统容量仍是 2G**——gzip 只是把
空闲 0 块压没，不改变 superblock。因此**槽分区 ≥ FS 容量**是硬约束，两者必须配套。

#### 2.6.2 实测体积

| 组成 | 未压缩 | gzip -9 后 |
|---|---|---|
| rootfs.ext2（2G 镜像） | 2.0 G | **33.5 MB** |
| rootfs 内容裸数据 | 71.4 MB | 31.7 MB |
| Image + dtb + fw_jump | 22.3 MB | 9.6 MB |
| **单槽 OTA 合计（.gz）** | — | **≈ 43 MB** |

三个反直觉结论：

1. **2G 镜像 gzip 后仅 33.5MB** —— 96% 空闲块全是 0，gzip 几乎免费压掉。
   ⇒ **FS 定 2G 还是更小，对 OTA 下载体积几乎无影响**（差 ~2MB）。
2. **整个 ext2 镜像 vs 裸内容只差 1.8MB**（33.5 vs 31.7）—— ext4 元数据+空闲开销极小。
   ⇒ 没必要搞「只传内容再重建 FS」的复杂方案，直接传整个 ext2 镜像 gzip 即可。
3. **内核 22M 压到 9.6M**，占比小，无法避免。

#### 2.6.3 升级速度拆解

瓶颈**不是下载，是 dd 刷写**：

| 阶段 | 2G FS | 说明 |
|---|---|---|
| 下载（43MB） | 10 Mbps ≈ 35s；100 Mbps ≈ 3.5s | 海外客户带宽下很快 |
| 验签 + sha256 | 数秒 | openssl/sha256sum，CPU 够用 |
| **dd 写 2G 分区** | SD 卡 ~20MB/s ≈ **100s** | **主要耗时**，写解压后全部 2G（含空闲） |

总升级时长 ≈ 2 分钟级，客户可接受。

#### 2.6.4 根分区为何定 2G（而非更小）

既然下载体积几乎不随 FS 大小变化，**缩 FS 唯一的好处是省 dd 时间**：

| FS 大小 | 下载 | dd 时间(20MB/s) | 升级余量 | 评价 |
|---|---|---|---|---|
| **2G（选定）** | 33MB | ~100s | 1.88G | 余量充足、改动最小、风险最低 |
| 512M | 33MB | ~25s | 440M | 省 dd 时间，但需改 EXT2_SIZE 并重调构建 |
| 256M | 33MB | ~13s | 184M | 余量偏紧，未来升级受限 |

**定案 2G** 的理由：

- 下载体积与更小 FS 几乎相同（gzip 压空闲块），缩 FS 不省下载。
- `EXT2_SIZE` 维持 2G **无需改动构建参数**，与现有 rootfs 等大，resize/挂载逻辑零调整，风险最低。
- 1.88G 空闲为未来多次升级留足余量，长期省心。
- dd 多花的 ~75s 在 2 分钟级总时长里可接受，不值得为它引入裁剪 FS 的复杂度。

#### 2.6.5 进一步压缩的可选项（默认不上，P3 视需要）

| 优化 | 收益 | 代价 |
|---|---|---|
| xz/zstd 替代 gzip | 43MB → ~30MB（省 30%） | 解压慢、设备需 xz；下载已够快，收益边际 |
| 裁剪 rootfs 内容（去 opencv/ffmpeg 等） | 71.4MB 内容 → 可能 ~50MB | 逐个 package 评估，工作量在项目侧 |
| 增量更新（bsdiff/xdelta） | 43MB → 可能 5~10MB | 设备需 bspatch、基线版本管理复杂 |

P1 采用「整槽 ext2 + gzip」，43MB 已足够快，不折腾裁剪与增量。

---

## 3. 改动清单（文件级）

| # | 文件 | 改动 |
|---|---|---|
| 1 | buildroot-overlay/board/canaan/k230_rockbase/genimage.cfg | 删 boot 分区；p1/p2 两个 2G 槽；p3 config（256M `rockcfg.ext4`）；blockdata 跟随为 p4 |
| 2 | buildroot-overlay/configs/k230_rockbase_{1g,2g}_defconfig | EXT2_SIZE 维持 2G 不变；新增 `BR2_PACKAGE_LIBUBOOTENV`、`BR2_PACKAGE_LIBCURL(+_CURL/_OPENSSL)`、`BR2_PACKAGE_CA_CERTIFICATES`、`BR2_PACKAGE_OPENSSL_BIN`（dgst 验签） |
| 3 | buildroot-overlay/board/canaan/k230_rockbase/default.env | A/B bootcmd / altbootcmd / ota_part / bootlimit |
| 4 | buildroot-overlay/boot/uboot/u-boot-2022.10-overlay/configs/k230_rockbase_defconfig | `CONFIG_BOOTCOUNT_LIMIT=y` |
| 5 | post-build.sh | 生成 `fw_jump_add_uboot_head.bin` 并把 Image/k.dtb 装入 `${TARGET_DIR}/boot/`；安装 factory-defaults |
| 6 | post-image.sh | slot_b 由 slot_a 复制；导出逻辑保留；新增 OTA 包产物输出；新增 `gen_rockcfg_ext4`（空 256M，`LABEL=rockcfg`） |
| 7 | rootfs_overlay/etc/init.d/S00resizemmc | `fw_env.config` 生成移到 done 检查之前（当前在 `/resize_done` 后直接退出，dd 出来的新槽会缺此文件导致 `fw_setenv` 失败）；blockdata 扩容对象 p3→p4 |
| 8 | rootfs_overlay/etc/init.d/S59blockdata + 新增 S05config | S59 改按 LABEL=blockdata 挂载（回退 p4）；新 `S05config` 早期挂载 LABEL=rockcfg 到 `/root/config` 并播种出厂默认 |
| 9 | overlay 新增 | `/usr/sbin/rockbase-ota.sh`、`/etc/init.d/S98otaconfirm`、`/root/web/api/ota.cgi`、`/etc/rockbase-factory-defaults/`、`/etc/rockbase-ota-pub.pem`（验签公钥）；消费方路径直改 `/root/config/…`（不用符号链接） |
| 10 | tools/ 新增 | `mk-ota-package.sh`（构建 OTA 包 + manifest + dgst 签名） |
| 11 | 仓库根 `.gitignore` | 增加 `*-private.pem`，防私钥误提交 |

注意：共享 kernel patch / uboot overlay 变更后，单一 `k230_rockbase_defconfig` 要
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

1. 新镜像烧录 → 确认 slot A 启动、blockdata 扩容/挂载正常、WiFi 配置落在 config 分区（p3）。
2. 构造 v2 假版本 OTA 包 → apply → 确认切槽、版本号变化、
   WiFi / ckpool / bitcoin 配置原样保留。
3. dd 写槽中途断电 → 设备仍从旧槽正常启动。
4. 切槽后人为破坏新槽 → 3 次启动后自动回滚旧槽。
5. ~~1G（mmcblk1）与 2G（mmcblk0）各跑一遍~~（已取消：内存统一自动识别、mmc 拓扑统一）。
6. 回归：seed 镜像构建、USB / SDIO 网络模式切换不受影响。

---

## 6. 分期实施

- P1（出货前必须）：分区布局 + uboot A/B/回滚 + 配置外置 + OTA 脚本
  （manifest 拉取 + 断点续传 + **dgst 验签**）+ 确认服务 + 验签公钥入库。
- P2（可后置，OTA 自举升级）：Web 升级页（ota.cgi + 前端卡片）、下载进度展示、
  `ota.nmminer.com` 域名层上线（先用 GitHub 直连跑通再切域名）。
- P3（可选）：小 recovery 分区兜底；swupdate 替换自研脚本（buildroot 已有该包，
  但项目惯例为 shell，P1 用脚本更顺）。

建议实施顺序（每步先热部署到 192.168.124.15 验证再固化）：
配置外置 → 分区/uboot → OTA agent。

---

## 7. 调试与验证推进方案（分阶段 · 可回退）

核心原则：**从安全到危险，每步可独立验证、随时可回退**。先用现网单 rootfs 布局把所有
「软件逻辑」调通，**最后一步才动分区表**（动分区是唯一不可逆、必须重烧的操作）。

关键洞察：A/B 的 uboot 逻辑、配置外置、OTA 下载/验签/dd/确认，都可以在
**当前单 rootfs 布局**上先调通——它们只是「读写 env、读写文件、dd 一个空闲分区」。
只有最后把 p2 拆成两槽、真正把系统切过去，才需要新分区表。这样把风险压到最低。

```
阶段A  纯软件，现网布局可测，零风险
  A1 配置外置 → A2 fw_env 修复 → A3 OTA agent 骨架
阶段B  uboot A/B 逻辑（先在 u-boot 命令行手测，不烧录）
阶段C  工具链 + 签名端到端
阶段D  分区改造（唯一需重烧，放最后）
阶段E  全链路真机验证
```

### 阶段 A：纯软件，现网布局可测（不重烧）

#### A1 · 用户配置独立分区（config 分区）

- 改：新 `S05config`（按 LABEL=rockcfg 早期挂载 `/root/config`，不等 resize_done）、
  消费方路径直改 `/root/config/…`（无符号链接）、首启播种逻辑、
  `/etc/rockbase-factory-defaults/`、`S59blockdata` 改 LABEL 挂载、`S00resizemmc` 扩容对象 p3→p4。
- 热部署验证（124.30，USB 有线）：
  1. 板端重分区：p3=config(256M) / p4=blockdata，mkfs 打 LABEL。
  2. 改 WiFi（web）→ 确认落到 `/root/config/rockbase-wifi.conf`（p3）。
  3. 改 ckpool 配置 → 重启服务仍生效。
  4. 重启 → S05config/S59 按 LABEL 挂载、配置播种、bitcoind 以 `-conf=/root/config/bitcoin.conf` 启动。
- 通过标准：OTA 换槽、blockdata 重灌 seed 后，WiFi/bitcoind/ckpool 配置原样保留。
- 回退：恢复旧分区表 + 旧脚本（开发期不兼容老布局，重烧即可）。
- ✅ 2026-08-21 已完成并验证（见 handoff 文档）。

#### A2 · 修复 fw_env.config 时机 + libubootenv 上板（**建议的第一步**）

- 背景：[S00resizemmc](buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/init.d/S00resizemmc)
  在 `/resize_done` 后提前 return，dd 出来的新槽会缺 `fw_env.config`，导致 `fw_setenv` 失败。
- 改：`fw_env.config` 生成移到 done 检查之前；defconfig 加 `BR2_PACKAGE_LIBUBOOTENV`，
  让 `fw_printenv/fw_setenv` 上板。
- 验证：板上 `fw_printenv ota_part`、`fw_setenv test=1 && fw_printenv test` 能读写 env。
  这是 A/B 的地基，必须先通。
- 通过标准：用户态能可靠读写 uboot env。

#### A3 · OTA agent 骨架 + 确认服务

- 改：`/usr/sbin/rockbase-ota.sh`（status/check/apply/confirm/rollback）、`S98otaconfirm`。
- 此阶段 apply 只到「下载+验签+校验」，不真的 dd（dry-run 模式）。
- 验证：本地起 HTTP 服务放 manifest+假包，`ota.sh check` 能解析、
  `ota.sh apply --dry-run` 走完验签流程。

**阶段 A 出口**：配置外置 + env 读写 + OTA 下载验签，全部在现网布局跑通，系统仍从 p2 正常启动。

### 阶段 B：uboot A/B 逻辑（先命令行手测，不烧录）

> 不重烧、不改分区，只验证「uboot 能否按 env 的 ota_part 选对分区、能否回滚」。

1. 串口中断 bootdelay，手动测新 `blinux`：
   `setenv ota_part 2; run blinux` → 当前只有一个系统，应 load 失败。
2. 验证 `altbootcmd` 回滚：`setenv ota_part 2; run altbootcmd` → 应自动 `ota_part=1`。
3. 验证 `CONFIG_BOOTCOUNT_LIMIT`：加上后看 bootcount 是否每次启动 +1、超限是否触发 altbootcmd。
4. 关键：验证 `board_fdt_chosen_bootargs()` 吃 env `bootargs` 的行为——
   `setenv bootargs root=...p1 ...; run blinux`，看 root= 是否被 env 覆盖。

通过标准：uboot 层「选槽 + 回滚 + bootcount」三板斧在命令行全部符合预期。
全程串口手动做，失败就 reset，不碰任何持久数据。

### 阶段 C：工具链 + 签名端到端

1. defconfig 加 `LIBCURL(_OPENSSL)` / `CA_CERTIFICATES` / `OPENSSL_BIN`。
2. 构建一对测试密钥：公钥入 overlay `/etc/rockbase-ota-pub.pem`，私钥离线。
3. `tools/mk-ota-package.sh`：构建 slot.img.gz + 签名 + manifest.json。
4. 真机端到端：
   - `curl -C -` 断点续传（**人为断网再续，验证续传**）；
   - dgst 验签（**故意改一个字节，验证拒绝**）；
   - sha256 校验。
5. 用 GitHub Releases 发测试 release，`ota.nmminer.com` 301 指向它，
   验证重定向链路（`curl -fSL` 跟两次跳转）。

通过标准：GitHub 下载 → 验签 → 校验全通；篡改包被拒；断点续传可用。

### 阶段 D：分区改造（唯一需重烧，放最后）

> 前面全部验证通过后，才动分区。这是唯一不可逆步骤。

1. 改 `genimage.cfg`（删 boot 分区、p1/p2 双 2G 槽、p3 config、p4 blockdata——
   config 分区已在 A1 v2 先行加入）、`post-build.sh`（Image/dtb/fw_jump 装进 `/boot/`）、
   `post-image.sh`（slot_b 复制 slot_a）。
2. 构建新镜像 → 烧录到 124.30（第一台吃新布局的板子，USB 有线）。
3. 验证：从 slot A 启动、blockdata 扩容/挂载正常、配置从 config 分区（p3）读出、WiFi 连通。
4. 手动做一次真 OTA：`ota.sh apply` 写 slot B → 切 `ota_part=2` → reboot →
   确认从 B 起来 → `S98otaconfirm` 确认。

回退预案：新布局有问题，重烧回旧镜像。
注意：阶段 D 会重分区，blockdata 需重灌 seed 或重同步；用户配置在 p3 不受影响。

### 阶段 E：全链路真机验证（对应第 5 节）

| # | 用例 | 预期 |
|---|---|---|
| E1 | 新镜像烧录 slot A | 正常启动、blockdata 扩容、配置在 p3 |
| E2 | OTA 到 slot B | 切槽成功、版本变化、WiFi/ckpool/bitcoin 配置保留 |
| E3 | dd 写 B 中途断电 | 仍从 A 正常启动（A/B 价值所在） |
| E4 | 切槽后破坏 B（如 dd 冲掉 B 的 kernel） | 3 次启动后自动回滚 A |
| E5 | ~~1G(mmcblk1) + 2G(mmcblk0) 各跑一遍~~ | 已取消：内存统一自动识别、mmc 拓扑统一 |
| E6 | 回归：seed 镜像构建、USB/SDIO 网络切换 | 不受影响 |

### 回退与分支基线

- 文档基线：`dev` 分支停留在 commit `363c7f69`（OTA 方案初稿），作为可靠回退点。
- OTA 实验全部在 `ota` 分支进行；每个阶段通过后 commit 一次，出问题 `git checkout dev` 回退。
- 镜像回退：保留一份 dev 分支构建的旧布局镜像，随时可重烧恢复。
