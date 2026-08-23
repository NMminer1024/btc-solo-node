# Rockbase OTA 开发 Handoff（接手用）

> 写给接手 OTA 开发的 agent/人。读完这份 + `docs/design/rockbase-ota-plan.md` 即可无缝继续。
> 更新日期：2026-08-21　当前分支：`ota`

---

## 0. 一句话现状

A/B OTA 的方案已定稿（`rockbase-ota-plan.md`），**阶段 A（A1/A2/A3）已全部完成并验证通过**，
下一步是 **阶段 B（uboot A/B 手测，需串口）**。

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
ota 分支（当前工作分支）：
  363c7f69  docs: OTA 方案初稿
  49f8508f  docs: 2G 定案 + 镜像源/签名 + 体积分析
  e2c07913  docs: 分阶段调试推进方案（方案第 7 章）
  56f929dd  feat: A2 libubootenv + S00 fw_env 修复
  0ef9fcb8  feat: A3 OTA agent + 打包 + 确认服务 + 工具链
  e954f64c  fix:  A3 板端兼容修复（pgrep→ps、Range 容错）

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

### 阶段 B：uboot A/B 逻辑（串口命令行手测，不烧录）⬜ 未开始

### 阶段 C：工具链 + 签名端到端（部分已并入 A3 完成）🔄 部分完成

### 阶段 D：分区改造（唯一需重烧，放最后）⬜ 未开始

### 阶段 E：全链路真机验证 ⬜ 未开始

---

## 5. 已写好的代码（都在 ota 分支，已提交）

| 文件 | 作用 | 验证状态 |
|---|---|---|
| `rootfs_overlay/usr/sbin/rockbase-ota.sh` | OTA agent：`status/check/apply/confirm/rollback`。下载+验签是真实逻辑；dd+切槽用 `--force` 门控（默认 dry-run 安全） | check/status ✅ 板端验证；apply dry-run 🔄 收尾中 |
| `tools/mk-ota-package.sh` | 构建端打包：gzip 槽镜像 + sha256 + openssl dgst 签名 + manifest.json | ✅ 本机全流程自测通过 |
| `rootfs_overlay/etc/init.d/S98otaconfirm` | 确认服务：升级启动后等 web+sshd+bitcoind 健康则 `ota confirm` | 已写，待阶段 D 后真验 |
| `rootfs_overlay/etc/rockbase-ota-pub.pem` | 验签公钥（测试密钥） | ✅ 已固化进 overlay |
| `k230_rockbase_1g_defconfig` | +`LIBUBOOTENV`、`LIBCURL(+CURL/_OPENSSL)`、`CA_CERTIFICATES`、`LIBOPENSSL_BIN` | ✅ 已编译进镜像 |
| `.gitignore` | +`*-private.pem` 防私钥误提交 | ✅ |
| `rootfs_overlay/etc/init.d/S05config` | A1 v2：早期挂载 `LABEL=rockcfg`（p3）到 `/root/config` + 出厂默认播种 + 降级回退（blkid 表解析，busybox 无 `blkid -L`） | ✅ 板端验证 |
| `rootfs_overlay/etc/rockbase-factory-defaults/` | A1：出厂默认 `bitcoin.conf`/`ckpool.conf`（原 rootfs 实体文件移入） | ✅ 板端 md5 与原文件一致 |
| 消费方路径直改 `/root/config/…`（无符号链接） | `S61bitcoind`/`S62ckpool`/`bitcoind start|stop.sh`/`ckpool/start.sh`/`rockbase_statusd.sh`/`rockbase_bitcoind_ready.sh`/`rockbase_wifi.sh`(`WIFI_CONFIG_FILE`) | ✅ 板端验证（重启后 bitcoind 以新路径启动） |
| `S59blockdata` / `S00resizemmc` | A1 v2：S59 按 LABEL=blockdata 挂载（回退 p4）；S00 扩容对象 p3→p4 | ✅ 板端验证（LABEL 挂载正常；S00 扩容逻辑待阶段 D 新卡验证） |
| `genimage.cfg` + `post-image.sh` | A1 v2：新增 p3 config 分区（空 256M `rockcfg.ext4`，`gen_rockcfg_ext4`） | 已写，待下次构建镜像时验证 |

> A1 曾有 v1（配置放 blockdata 子目录 + 符号链接），经讨论改为 v2 独立分区方案，
> v1 产物（`S05blockdata`、符号链接、wifi readlink 补丁）已全部移除。

## 6. 已验证的事实（不要重复验证）

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

~~A3 设备端 apply dry-run~~ **已于 2026-08-21 验证通过**（WiFi / USB 有线各一次）：下载 → 验签 →
sha256 校验 → 正确识别目标槽位 `mmcblk1p1`，日志 `DRY-RUN complete`。A3 收尾完成。
临时测试密钥 `/tmp/ota-keys/` 与测试包 `/tmp/ota-dist/` 仍在（`/tmp` 重启即清，无所谓）；
本机 HTTP 服务 `python3 -m http.server 8000`（起在 `/tmp/ota-dist/v0.0.1-test/`）当前仍在跑，不用了 `pkill -f http.server` 即可。

直接进入第 8 节的下一步即可（阶段 B，需串口）。

历史备注：dry-run 时板子 WiFi ~40-60KB/s，34MB 包下载约 10 分钟。

## 8. 下一步该做什么（按优先级）

### ~~选项 1：A1 配置外置~~ ✅ 已完成（2026-08-21）
把 WiFi/ckpool/bitcoin 配置从 rootfs 挪到 blockdata `/root/block/config/`，rootfs 改符号链接。涉及：
- 新建 `S05blockdata`（提前按 LABEL 挂载，不等 resize_done）
- `/etc/rockbase-factory-defaults/` 出厂默认配置 + 首启播种
- 3 个配置符号链接：`/etc/rockbase-wifi.conf`、`/root/bitcoind/bitcoin.conf`、`/root/ckpool/ckpool.conf`
- 详见方案 2.3 + 阶段 A1

### 阶段 B：uboot A/B 手测（需要串口，风险低）← 下一步
串口中断 bootdelay，手测新 `blinux`/`altbootcmd`/`bootcount`。需先改 `default.env` + uboot defconfig `CONFIG_BOOTCOUNT_LIMIT=y`，重编 uboot。详见方案 2.2 + 阶段 B。

**阶段 A 已全部完成**（纯软件部分收官）。B 需要人在板子旁接串口线，无法远程进行。

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

- [ ] `git checkout ota && git log --oneline -6` 确认在最新 ota 分支
- [ ] 读 `docs/design/rockbase-ota-plan.md` 第 2、7 章
- [ ] A2/A3 均已完成验证（第 4、6 节），无需复查
- [ ] 下一步：A1（推荐，见第 8 节）或 B
- [ ] 遵守第 9、10 节的规则与坑
