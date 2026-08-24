# Rockbase OTA 开发 Handoff（接手用）

> 写给接手 OTA 开发的 agent/人。读完这份 + `docs/design/rockbase-ota-plan.md` 即可无缝继续。
> 更新日期：2026-08-24　当前分支：`dev`（`ota` 分支已 ff 合并进 dev，见第 2 节）

---

## 0. 一句话现状

**A/B OTA 主框架全链路（E1~E4）已于 2026-08-24 在 124.30 真机验证通过，且
`tools/ota-push.sh` 一键推送（打包→本机 HTTP→板端下载验签→dd 写槽→切槽重启→确认）
已在 B→A 方向实测闭环**：双槽切换、配置/数据保留、写盘中途断电安全、bootcount
超限自动回滚全部工作。`ota` 分支已 ff 合并进 `dev`。
剩余：E5（2G 板适配，需先评估 2G defconfig）、E6 回归、阶段 C 的 GitHub 发布链路、
P2 的 Web 升级页（ota.cgi + 前端卡片 + LCD 进度展示）。

---

## 1. 硬性约束（用户拍板，不要推翻）

| 约束 | 值 |
|---|---|
| 目标板型 | **只针对 1G 板**（`k230_rockbase_1g_defconfig`）。**2G defconfig 不要动**（曾误改已还原） |
| 镜像源 | 客户海外；自有域名 `nmminer.com` → GitHub Releases（纯免费） |
| 签名 | **必须**（openssl dgst RSA/SHA256；私钥离线，公钥固化设备） |
| 根分区 | **定 2G**（实测 gzip 后 ~43MB；缩 FS 不省下载只省 dd 时间，维持 2G 改动最小） |
| 升级范围 | 只升系统，**不覆盖用户参数**（WiFi/ckpool/bitcoin 配置） |

## 2. 分支与回退基线

```
ota 分支（已全部 ff 合并进 dev @ 131a4fca，后续直接在 dev 上开发）：
  363c7f69  docs: OTA 方案初稿
  49f8508f  docs: 2G 定案 + 镜像源/签名 + 体积分析
  e2c07913  docs: 分阶段调试推进方案（方案第 7 章）
  56f929dd  feat: A2 libubootenv + S00 fw_env 修复
  0ef9fcb8  feat: A3 OTA agent + 打包 + 确认服务 + 工具链
  e954f64c  fix:  A3 板端兼容修复（pgrep→ps、Range 容错）
  dfc742ae  feat: A1 v2 配置独立分区（p3=rockcfg / p4=blockdata）
  a81d473f  feat: 阶段 B+D（双槽布局、uboot bootcount、槽位自包含 /boot）
  c87eaab5  fix:  blinux 内联 bootargs（uboot 单层展开，首烧 VFS panic 修复）
  eaee4386  feat: E2 加固（apply 服务 quiesce + S00 脏 FS 修复重试）

回退基线：dev 分支 = 363c7f69（OTA 之前的稳定点）
镜像回退：dev 对应旧布局镜像可随时重烧恢复
```

## 3. 设计文档（单一事实来源）

**`docs/design/rockbase-ota-plan.md`** —— 共 7 章：
1. 当前镜像逻辑（分区/启动链/用户参数落点）
2. 目标架构（A/B 分区、uboot 回滚、配置外置、OTA agent、镜像源与签名、体积分析）
3. 改动清单（文件级，11 项）
4. 出货与迁移策略
5. 验证计划
6. 分期实施（P1/P2/P3）
7. **调试与验证推进方案（A→E 阶段，含每步通过标准与回退）** ← 当前正按这章走

---

## 4. 各阶段进度

### 阶段 A：纯软件（现网单 rootfs 布局可测，不重烧）

| 子阶段 | 内容 | 状态 |
|---|---|---|
| **A2** | libubootenv（fw_printenv/fw_setenv）上板 + 修 S00 fw_env 时机 | ✅ **完成并已验证**（commit 56f929dd） |
| **A3** | OTA agent + 打包脚本 + 确认服务 + 签名工具链 | ✅ **完成并已验证**（commit 0ef9fcb8 + e954f64c，2026-08-21 设备 dry-run 通过） |
| **A1** | 用户配置独立分区（p3=config 256M `rockcfg` / p4=blockdata） | ✅ **完成并已验证**（2026-08-21，124.30 重分区 + 热部署验证通过） |

### 阶段 B：uboot A/B 逻辑（串口命令行手测，不烧录）✅ 已完成（2026-08-24，124.30 串口）

- 串口链路：CH340 经 usbipd 透传进 WSL2，`/dev/ttyUSB0` 115200，uboot 提示符 `K230#`（bootdelay=1）。
- 正测：`ota_part=2` + `blinux_ab`（从 p2 `/boot/` 加载三件套 + env `bootargs` 覆盖 + bootm）→
  成功启动，`/proc/cmdline` 确认 `root=/dev/mmcblk1p2 ... rockbase_ab_test=1`——
  **方案 2.2 的关键假设成立：env `bootargs` 覆盖 DTS `bootargs`**（DTS 的
  `ignore_loglevel log_buf_len=4M keep_bootcon printk.always_kmsg_dump=1` 仍被追加）。
- 负测：`ota_part=3`（无 /boot）→ `Failed to load ...`，`&&` 链中断，不 bootm，安全回到 `K230#`。
- 回滚逻辑：槽位翻转 2→1 / 1→2 / 2→1 三次全对，`bootcount` 同步清零（未带 saveenv 的测试版）。
- `CONFIG_BOOTCOUNT_LIMIT=y` + `CONFIG_BOOTCOUNT_ENV=y` 已加进 uboot defconfig 并重编；
  **bootcount 自动 +1 / 超限触发 altbootcmd 待新镜像烧录后验证**（属阶段 E 的 E4）。

### 阶段 C：工具链 + 签名端到端（部分已并入 A3 完成）🔄 部分完成

### 阶段 D：分区改造（唯一需重烧）🔄 源码已改完，镜像构建中（2026-08-24）

- `genimage.cfg`：删 80M boot 分区；p1=slot_a / p2=slot_b（同一 `rootfs.ext4` 写两遍，
  slot_b 天然是 slot_a 出厂拷贝，无需 post-image 复制）；p3=config、p4=blockdata 不变。
- `post-build.sh`：新增 `gen_target_boot`，把 `Image` / `k.dtb`（实文件）/
  `fw_jump_add_uboot_head.bin` 装进 `${TARGET_DIR}/boot/`（每槽自包含）。
- `post-image.sh`：停调 `gen_boot_ext4`（不再生成 boot.ext4）。
- `default.env`：固化 A/B 版（`ota_part=1`、`bootlimit=3`、`upgrade_available=0`、
  `ota_bootargs`、新 `blinux` 按 `${ota_part}` 从 `/boot/` 加载并覆盖 `bootargs`、
  `altbootcmd` 翻槽 + `saveenv` + `run blinux`）——与串口验证过的命令逐字一致。
- uboot defconfig：`CONFIG_BOOTCOUNT_LIMIT=y` + `CONFIG_BOOTCOUNT_ENV=y`（bootcount 落 env）。
- 兼容性核查：`S00resizemmc` 只在 p4 存在时扩容 p4，不动 rootfs 分区（p1/p2 双槽安全）；
  rootfs_overlay 无 p1/boot 分区残留引用。

### 阶段 E：全链路真机验证 ✅ E1~E4 已通过（2026-08-24，124.30）

| # | 用例 | 结果 |
|---|---|---|
| E1 | 新镜像烧录 → slot A 启动 | ✅ `root=/dev/mmcblk1p1`、p4 扩容到卡尾、p3 播种、服务全起 |
| E2 | 真 OTA apply 写对侧槽 | ✅ A→B：切 `ota_part=2` 从 B 起（版本标记 `99999999-e2test`）、p3/p4 完好、confirm 后 `upgrade_available=0 bootcount=0`。B→A：`ota-push.sh` 一键推送 `v20260824-112046-ota` 到 slot A、重启起 p1、修复 p4 后 confirm 成功。过程中暴露并修复三个坑（见下） |
| E3 | dd 写 A 槽中途断电（49% 处） | ✅ 上电照常用 slot B 启动，env 未污染，p4 journal replay 正常 |
| E4 | 破坏 A 槽 + 切 `ota_part=1` | ✅ 串口实测 bootcount 1→2→3 递增，超限自动 altbootcmd 回滚 slot B；回滚后 S98otaconfirm 自动确认 |
| E5 | 2G 板（mmcblk0） | ⬜ 未测（2G defconfig 按约束未动，需另行评估） |
| E6 | 回归：seed 构建、网络切换 | ⬜ 未测 |

**E2 暴露并修复的三个坑（前两个 commit eaee4386，均已板端验证）**：

1. **freeze 看门狗误杀**：dd 写 SD 期间 mmc1 host claim 长占用是预期行为，
   `rockbase_serial_healthd` 误判冻结 → sysrq crash（连续 3 次）；supervisord
   还会把 bitcoind 拉回来加剧争抢。修复：`rockbase-ota.sh` 真 apply 前
   quiesce（先停 supervisord 断复活 → ckpool/bitcoind/miners/statusd →
   暂停 serialhealthd；保留 lcdui/httpd/网络/sshd），失败逆序恢复。
2. **新槽首启 p4 挂不上**：崩溃弄脏 p4 → 新槽（无 `/resize_done`）S00 的
   resize2fs 被拒（需先 e2fsck）→ `/resize_done` 建不了 → S59 无限期推迟挂载
   → otaconfirm 等不到健康。修复：S00 `resize_fs()`（resize2fs 失败先
   `e2fsck -f -y` 再重试）。
3. **e2fsck 返回码误判**（B→A 实测暴露）：`e2fsck -f -y` 修复成功后返回 **1**
   （"errors corrected"），`resize_fs()` 原实现把非 0 全当失败直接放弃。
   修复：接受 rc<4（0/1/2 都算成功）再重试 resize2fs。**注意此修复是热部署
   验证后写回源码的，OTA 打进对侧槽的镜像仍是构建时点的脚本版本——本次就是
   靠热部署新 S00 重跑才把 A 槽 p4 修好**（运维教训：OTA 槽镜像只带构建时点
   的脚本，热修复不会随 OTA 传播）。

**另一个阶段 D 的首烧坑（commit c87eaab5）**：uboot 变量单层展开——
`ota_bootargs` 中间变量会让 `${mmc_boot_dev_num}`/`${ota_part}` 以字面量进
kernel cmdline 导致 VFS panic；`blinux` 必须内联 `setenv bootargs root=...p${ota_part} ...`。

---

## 5. 已写好的代码（都在 ota 分支，已提交）

| 文件 | 作用 | 验证状态 |
|---|---|---|
| `tools/ota-push.sh` | WSL 侧一键推送：mk-ota-package 打包 → 起/复用本机 HTTP(:8000) → 触发板端 check/apply（nohup）→ 轮询进度 → 重启后等板回来 → otaconfirm。密钥 `~/.rockbase-ota-keys/`，产物 `~/ota-dist/` | ✅ B→A 全流程实测（skill「ota升级124.30」入口） |
| `.github/skills/rockbase-ota/SKILL.md` | skill：提示词「ota升级124.30」触发上述全流程 | ✅ 实测 |
| `rootfs_overlay/usr/sbin/rockbase-ota.sh` | OTA agent：`status/check/apply/confirm/rollback`；真 apply 前 quiesce 服务（停 supervisord→ckpool/bitcoind/miners/statusd→暂停 serialhealthd，留 lcdui/httpd/网络/sshd），失败逆序恢复 | ✅ E2 真切槽验证（含 quiesce） |
| `tools/mk-ota-package.sh` | 构建端打包：gzip 槽镜像 + sha256 + openssl dgst 签名 + manifest.json | ✅ E2/E3 真包验证 |
| `rootfs_overlay/etc/init.d/S98otaconfirm` | 确认服务：升级启动后等 web+sshd+bitcoind 健康则 `ota confirm` | ✅ E4 回滚后自动确认 |
| `rootfs_overlay/etc/rockbase-ota-pub.pem` | 验签公钥（测试密钥） | ✅ 已固化进 overlay |
| `k230_rockbase_1g_defconfig` | +`LIBUBOOTENV`、`LIBCURL(+CURL/_OPENSSL)`、`CA_CERTIFICATES`、`LIBOPENSSL_BIN` | ✅ 已编译进镜像 |
| `.gitignore` | +`*-private.pem` 防私钥误提交 | ✅ |
| `rootfs_overlay/etc/init.d/S05config` | A1 v2：早期挂载 `LABEL=rockcfg`（p3）到 `/root/config` + 出厂默认播种 + 降级回退（blkid 表解析，busybox 无 `blkid -L`） | ✅ 板端验证 |
| `rootfs_overlay/etc/rockbase-factory-defaults/` | A1：出厂默认 `bitcoin.conf`/`ckpool.conf`（原 rootfs 实体文件移入） | ✅ 板端 md5 与原文件一致 |
| 消费方路径直改 `/root/config/…`（无符号链接） | `S61bitcoind`/`S62ckpool`/`bitcoind start|stop.sh`/`ckpool/start.sh`/`rockbase_statusd.sh`/`rockbase_bitcoind_ready.sh`/`rockbase_wifi.sh`(`WIFI_CONFIG_FILE`) | ✅ 板端验证（重启后 bitcoind 以新路径启动） |
| `S59blockdata` / `S00resizemmc` | A1 v2：S59 按 LABEL=blockdata 挂载（回退 p4）；S00 扩容对象 p3→p4；S00 `resize_fs()` 脏 FS 先 e2fsck 再 resize（E2 修复） | ✅ E1/E3 验证（扩容、脏挂载恢复均正常） |
| `genimage.cfg` + `post-image.sh` | A1 v2：新增 p3 config 分区（空 256M `rockcfg.ext4`，`gen_rockcfg_ext4`） | ✅ 阶段 D 镜像已验证 |
| `genimage.cfg`（阶段 D） | 删 80M boot 分区；p1=slot_a / p2=slot_b（同一 `rootfs.ext4` 写两遍）；p3=config、p4=blockdata | ✅ 镜像分区表 fdisk 验证（30M 起，2G/2G/256M/256M） |
| `post-build.sh`（阶段 D） | `gen_target_boot`：`Image`/`k.dtb`(实文件)/`fw_jump_add_uboot_head.bin` 装进 `${TARGET_DIR}/boot/` | ✅ rootfs.ext4 debugfs 验证 |
| `default.env`（阶段 D） | A/B 版：`ota_part=1`/`bootlimit=3`/`upgrade_available=0`/新 `blinux`（bootargs 内联，uboot 单层展开）/`altbootcmd` | ✅ E1~E4 全链路验证 |
| uboot `k230_rockbase_defconfig` | `CONFIG_BOOTCOUNT_LIMIT=y` + `CONFIG_BOOTCOUNT_ENV=y` | ✅ E4 验证：bootcount 逐次 +1、超限触发 altbootcmd 回滚 |

> A1 曾有 v1（配置放 blockdata 子目录 + 符号链接），经讨论改为 v2 独立分区方案，
> v1 产物（`S05blockdata`、符号链接、wifi readlink 补丁）已全部移除。

## 6. 已验证的事实（不要重复验证）

- **阶段 B（2026-08-24，124.30 串口 `/dev/ttyUSB0`）**：uboot 按 `ota_part` 选槽加载
  `/boot/{fw_jump_add_uboot_head.bin,Image,k.dtb}` 正测/负测全对；env `bootargs`
  能覆盖 DTS `bootargs`；`altbootcmd` 翻槽逻辑三向全对。env 备份在 `/tmp/uboot-env-backup.txt`。
- **A2**：124.15 上 `fw_printenv` 读 33 行 env、`fw_setenv` 写/删正常、删 fw_env.config 重启后新 S00 自动重建。
- **打包/签名/验签**：本机用 `mk-ota-package.sh` 造出 35MB 测试包，验签正例通过、篡改反例正确拒绝。
- **板端 status**：正确识别 `current slot=2 / inactive=1 / bootdev=/dev/mmcblk1`（1G 板）。
- **板端 check**：能从本机 HTTP 拉 manifest 并解析出 version/url/sha256/size。
- **板端 dry-run（WiFi）**：下载+验签+sha256+槽位识别全对，`DRY-RUN complete`。
- **板端 dry-run（USB 有线）**：34.2MB 下载 ~3s（约 11MB/s 跑满百兆），全流程 10.66s，同样通过。
- **板端文件与源码一致性（2026-08-21 已核对）**：`S00resizemmc`/pub key/netsel 全套 md5 与 ota 分支一致；
  `rockbase-ota.sh`/`S98otaconfirm` 曾落后源码（缺 e954f64c 修复），已重新 scp 同步。
- **rootfs 为 ext4 rw**：热部署文件重启后保留（只有重烧镜像才会被源码版本覆盖）。
- **A1 v2（2026-08-21，124.30）**：板端已重分区为 `p1=boot / p2=rootfs / p3=config(256M,LABEL=rockcfg) / p4=blockdata(55.6G,LABEL=blockdata)`；
  重启后 S05config/S59 按 LABEL 正常挂载、出厂默认播种正确（md5 一致）、bitcoind 以 `-conf=/root/config/bitcoin.conf` 启动、
  WiFi 保存直写 p3。原 p3 区块数据已按既定决策销毁（重灌 seed 或网络重同步；bitcoind 已在 p4 重新同步）。
  `S00resizemmc` 的 p4 扩容逻辑只改了源码，本板 `/resize_done` 已存在不会触发，待阶段 D 新卡验证。

## 7. 正在进行的事（接手第一件事）

~~A3 设备端 apply dry-run~~ 2026-08-21 完成。~~阶段 B 串口手测~~ 2026-08-24 完成。
~~阶段 D 分区改造~~ 2026-08-24 完成（镜像 `sysimage-sdcard-rockbase-1g-min-net-20260824-112046`）。
~~阶段 E1~E4 全链路~~ 2026-08-24 完成（见第 4 节阶段 E 表格与三个修复坑）。
~~`ota-push.sh` + skill 一键推送（B→A）~~ 2026-08-24 实测闭环；`ota` 分支已 ff 合并进 `dev`。

**接手第一件事：E6 回归 + 阶段 C 剩余项（GitHub Releases + 域名重定向），
或直接进 P2（Web 升级页 + LCD OTA 进度）。后续开发直接在 `dev` 分支进行。**

## 8. 下一步该做什么（按优先级）

1. **E6 回归**：seed 镜像构建不受影响、USB/SDIO 网络切换不受影响。
2. **阶段 C 剩余**：GitHub Releases 真实发布 + `ota.nmminer.com` 301 重定向链路（本机 HTTP 已验证）。
3. **P2 Web 集成**：`/root/web/api/ota.cgi`（status/check/apply + 进度轮询）+
   前端「系统升级」卡片 + LCD OTA 进度展示。
4. **E5（需用户拍板）**：2G 板适配——2G defconfig 按约束一直没动，
   要做的话需同步阶段 B/C/D 的全部改动并注意 mmc 设备号相反（mmcblk0）。

回退预案：新布局有问题就重烧 dev 分支旧布局镜像。

---

## 9. 项目特定规则（必须遵守，摘自 AGENTS.md）

1. **热部署优先**：改功能先 scp/ssh 到真板（现为有线 `124.30`，见第 12 节）验证，通过后再写回源码，不要反过来。
2. **变更追踪**：热调试期间追踪所有改过的业务文件，固化时逐项勾销。
3. **"打进镜像"≠编译**：用户说"打进镜像"只固化已验证改动，不编译除非明确要求。
4. **1G/2G MMC 拓扑相反**：1G `mmc0=SDIO WiFi, mmc1=SD`（root=`mmcblk1p2`）；2G 相反。别固定假设。
5. **共享 patch 变更**：改 kernel/uboot 共享 patch 后，每个 defconfig 要单独 `linux-dirclean`/`uboot-dirclean` 再重编。

## 10. 板端/构建的坑（已踩过，记入 repo memory `build-notes.md`）

- **`BR2_PACKAGE_LIBOPENSSL_BIN`** 才是 openssl 命令行开关（不是 `OPENSSL_BIN`）；curl 命令行是 `LIBCURL_CURL`（`LIBCURL` 只是库）。
- **libubootenv** 的 `fw_printenv` 依赖 `libubootenv.so.0`+`libyaml-0.so.2`+`libz.so.1`，热部署要连库一起拷；`fw_setenv` 是它的符号链接。
- **libopenssl 改 config 后**：要 `libopenssl-dirclean` 再重编才会装 `apps/openssl`；且 WSL PATH 含空格会报 "PATH contains spaces"，用干净 PATH `env PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"`。
- **busybox 无 pgrep**：板上进程检测用 `ps | grep -v grep | grep <pat>`（参考 S42web）。
- **busybox ash 不支持 `$SECONDS`**：展开为空导致 `[ $SECONDS -lt N ]` 报 "unknown operand" 恒为假。
  `root/bitcoind/stop.sh` 的 120s 等待循环因此形同虚设（bitcoin-cli stop 失败时会直接跳到 force kill；
  且 conf 路径变更期间 bitcoin-cli 缺 conf 会导致 stop 流程卡死）。后续修 stop.sh 时改用 `date +%s` 计时。
- **busybox fdisk 新建分区的默认值取"第一个空闲区域"**：卡上前部有 uboot raw 间隙时，
  空回车会把新分区建到那个间隙里（曾误建 p4=16-61439 盖住 SPL/env 区，幸好只改表没写数据）。
  建分区必须**显式给首尾扇区**，w 之前用 `p` 打印确认。
- **简单 HTTP 服务器不支持 Range**：curl `-C -` 报 `(33)`，脚本已加 `fetch()` 全量重下容错。GitHub Releases 支持 Range，生产无碍。
- **顶层 `make CONF=... buildroot`** 会经 `sync` 把 `.config` 重置回 defconfig；单独重编某包用 `make -C output/buildroot-2025.02.1 O=$PWD/output/<conf> <pkg>` 绕过。

## 11. 构建命令（1G，非 seed 最小镜像）

```bash
cd /home/bitpony/k230_linux_sdk
mkdir -p /tmp/rockbase-empty-seed
make CONF=k230_rockbase_1g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M K230_IMAGE_GZIP=0 K230_KEEP_WSL_IMAGES=1
```
导出到 `/mnt/d/code/pony/IoT/k230/img/sysimage-sdcard-rockbase-1g-min-net-<ts>/`。

## 12. 连接板子

**2026-08-21 起：板子已改走 USB 有线网卡**（`NET_MODE=usb_eth`，netsel 自动选择，
SDIO WiFi 被解绑）。USB 网卡在路由器端按 MAC 绑定固定 IP **`192.168.124.30`**（与插哪块板子无关）。
WiFi 侧旧 IP `192.168.124.15` 已失效。

```bash
ssh -o StrictHostKeyChecking=no root@192.168.124.30   # 无密码（key 已信任）
# 若报 HOST IDENTIFICATION HAS CHANGED：先 ssh-keygen -R 192.168.124.30
```

---

## 13. 接手检查清单（5 分钟上手）

- [ ] `git checkout dev && git log --oneline -6` 确认在最新 dev（ota 已合并）
- [ ] 读 `docs/design/rockbase-ota-plan.md` 第 2、7 章
- [ ] A2/A3 均已完成验证（第 4、6 节），无需复查
- [ ] 下一步：A1（推荐，见第 8 节）或 B
- [ ] 遵守第 9、10 节的规则与坑
