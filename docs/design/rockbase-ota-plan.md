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

- rootfs 实际占用仅 ~71MB（2G 镜像 96% 空闲），A/B 双 2G 槽在 SD 卡上轻松容纳。
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
p1 = slot_a  2G ext4   完整系统，内含 /boot/{Image, k.dtb, fw_jump_add_uboot_head.bin}
p2 = slot_b  2G ext4   出厂时为 slot_a 的拷贝
p3 = blockdata         剩余全部（分区号不变）
```

要点：

- 内核/dtb/opensbi 收进各槽 `/boot/`，每槽自包含、版本自洽；uboot 只是按槽号
  ext4load（机制与今天读 boot.ext4 完全相同）。取消 80M 共享 boot 分区。
- **根分区定为 2G**（`EXT2_SIZE` 维持 2G 不变）。选型理由见 2.6：由于 gzip 会把
  空闲块压掉，2G 与更小 FS 的 OTA 下载体积几乎相同，缩 FS 只能省 dd 刷写时间；
  保留 2G 换来充足的未来升级余量，且**改动最小、风险最低**（与当前 rootfs 等大，
  无需重调构建参数与挂载/扩容逻辑）。
- blockdata 保持 p3：S00resizemmc 的 p3 扩容与 S59blockdata 挂载逻辑不动；
  OTA 永远只写 p1/p2，p3 上的区块与用户配置物理上不可能被碰。
- 卡容量要求：A/B 双 2G 槽 + blockdata，建议 **≥ 8G SD 卡**（当前客户基线已满足）。

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
  "hw": ["1g", "2g"],
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

- `hw` 区分 1g/2g，设备按自身型号匹配，防止刷错镜像。
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
| 1 | buildroot-overlay/board/canaan/k230_rockbase/genimage.cfg | 删 boot 分区；p1/p2 两个 2G 槽；blockdata 跟随 |
| 2 | buildroot-overlay/configs/k230_rockbase_{1g,2g}_defconfig | EXT2_SIZE 维持 2G 不变；新增 `BR2_PACKAGE_LIBUBOOTENV`、`BR2_PACKAGE_LIBCURL(+_CURL/_OPENSSL)`、`BR2_PACKAGE_CA_CERTIFICATES`、`BR2_PACKAGE_OPENSSL_BIN`（dgst 验签） |
| 3 | buildroot-overlay/board/canaan/k230_rockbase/default.env | A/B bootcmd / altbootcmd / ota_part / bootlimit |
| 4 | buildroot-overlay/boot/uboot/u-boot-2022.10-overlay/configs/k230_rockbase_defconfig | `CONFIG_BOOTCOUNT_LIMIT=y` |
| 5 | post-build.sh | 生成 `fw_jump_add_uboot_head.bin` 并把 Image/k.dtb 装入 `${TARGET_DIR}/boot/`；安装 factory-defaults |
| 6 | post-image.sh | slot_b 由 slot_a 复制；导出逻辑保留；新增 OTA 包产物输出 |
| 7 | rootfs_overlay/etc/init.d/S00resizemmc | `fw_env.config` 生成移到 done 检查之前（当前在 `/resize_done` 后直接退出，dd 出来的新槽会缺此文件导致 `fw_setenv` 失败） |
| 8 | rootfs_overlay/etc/init.d/S59blockdata | 拆出早期挂载（新 `S05blockdata`，按 LABEL 挂载，不等 resize_done） |
| 9 | overlay 新增 | `/usr/sbin/rockbase-ota.sh`、`/etc/init.d/S98otaconfirm`、`/root/web/api/ota.cgi`、`/etc/rockbase-factory-defaults/`、`/etc/rockbase-ota-pub.pem`（验签公钥）、3 个配置符号链接 |
| 10 | tools/ 新增 | `mk-ota-package.sh`（构建 OTA 包 + manifest + dgst 签名） |
| 11 | 仓库根 `.gitignore` | 增加 `*-private.pem`，防私钥误提交 |

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

- P1（出货前必须）：分区布局 + uboot A/B/回滚 + 配置外置 + OTA 脚本
  （manifest 拉取 + 断点续传 + **dgst 验签**）+ 确认服务 + 验签公钥入库。
- P2（可后置，OTA 自举升级）：Web 升级页（ota.cgi + 前端卡片）、下载进度展示、
  `ota.nmminer.com` 域名层上线（先用 GitHub 直连跑通再切域名）。
- P3（可选）：小 recovery 分区兜底；swupdate 替换自研脚本（buildroot 已有该包，
  但项目惯例为 shell，P1 用脚本更顺）。

建议实施顺序（每步先热部署到 192.168.124.15 验证再固化）：
配置外置 → 分区/uboot → OTA agent。
