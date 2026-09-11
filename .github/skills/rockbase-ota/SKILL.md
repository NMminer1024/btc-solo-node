---
name: rockbase-ota
description: 'Rockbase A/B OTA 升级真机流程。Use when: ota升级, OTA 升级, ota push, 推送固件, 触发 OTA, ota升级124.30, 升级板子, rockbase-ota.sh, mk-ota-package, OTA 测试, 槽位切换, 回滚验证.'
---

# Rockbase OTA 升级（真机）

> 典型触发：「ota升级124.30」「OTA 升级 192.168.124.30」。
> 设计文档：`docs/design/rockbase-ota-plan.md`；进度/坑：`docs/rockbase-ota-handoff.md`。

---

## 前置认知（不要重新踩坑）

- 布局：`p1=slot_a / p2=slot_b / p3=config(1G,LABEL=config) / p4=blockdata`，每槽 `/boot/` 自包含。
- 板端 agent：`/usr/sbin/rockbase-ota.sh`（`status/check/apply/confirm/rollback`）。
  真 apply 已内置 **direct quiesce**：不依赖容易卡住的 init `stop`，而是直接按
  cmdline/killall 清理 `bitcoind`/`ckpool`/`statusd`/miners/supervisord/serialhealth/
  用户态 watchdog，保留 lcdui/httpd/网络/sshd。**不要手动再停服务**。
- OTA agent 必须有 `/run/rockbase/ota-apply.lock` 防并发 apply；重复触发应被拒绝，
  不能出现多个 `rockbase-ota.sh apply`/`gunzip` 同时写槽。
- 写盘进度以目标分区 `/proc/diskstats` 写扇区增量为准，并写入
  `/run/rockbase/ota-state.json`（LCD 覆膜）和 `/dev/console`（串口日志）。
  不要再用 pipeline `$!` + `/proc/<pid>/io` 判断 dd 进度。
- **必须 nohup 跑 apply**：SSH 会话死掉会杀远程 dd（E2 踩过），见下方标准流程的 `nohup ... &` 用法。
- **OTA payload 只能让设备自己下载**：不要把 `manifest.json`、`slot-*.img.gz`、
  `slot-*.img.gz.sig` 用 `scp` 传到目标机再 `file://` apply。`scp` 包到板上是早期调试手段，
  不能作为真机 OTA 验证路径；如果设备拉不到 HTTP/GitHub 源，先修路由/HTTP 服务/发布源，
  或停止并报告网络问题，不要绕过 OTA 下载链路。
- 切槽重启后 **SSH host key 会变**（另一个槽有独立 `/etc/ssh`）：
  先 `ssh-keygen -R <ip>` 再连。脚本已处理前置，升级后若再变需手动清一次。
- 升级后 `S98otaconfirm` 等健康（httpd+sshd+bitcoind，≤120s）自动 confirm；
  不健康则不确认，靠 uboot `bootcount>3` 自动回滚旧槽。
- uboot 变量**单层展开**：env 里的 bootargs 必须内联，不要加中间变量（c87eaab5 的坑）。

## 标准流程

### 0. 发布前置检查（每次真机 apply 前必做）

真实 OTA 切槽会把目标槽整个替换成镜像自带内容，包括 `/etc/rockbase-ota-pub.pem`。
apply 前先核对三处签名公钥一致，避免验签失败（2026-09-03 124.40 真机测试时踩过）：

```bash
md5sum ~/.rockbase-ota-keys/ota-pub.pem \
  buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/rockbase-ota-pub.pem
ssh root@<IP> 'md5sum /etc/rockbase-ota-pub.pem'
```

三个 md5 必须一致。不一致就先 `scp ~/.rockbase-ota-keys/ota-pub.pem root@<IP>:/etc/rockbase-ota-pub.pem`
把板子当前槽同步到最新公钥，再继续。详见 `docs/rockbase-ota-handoff.md` 第 13.4 节。

完整链路顺序：`rockbase-build`（编译）→ `rockbase-ota-release`（打包签名发布）→
本 skill（真机 apply）。三步之间共享同一把签名密钥。

### 1. 真机升级（生产域名或 GitHub 直链）

手动跑板端 agent，验证的就是设备实际会走的真实链路：

```bash
MANIFEST_URL="https://ota.nmminer.com/rockbase/manifest.json"
# 或直接用 GitHub 直链：
# MANIFEST_URL="https://github.com/NMminer1024/btc-solo-node-release/releases/latest/download/manifest.json"

ssh root@<IP> "rockbase-ota.sh check '$MANIFEST_URL'"      # 先确认能解析出正确版本/sha256
ssh root@<IP> "nohup rockbase-ota.sh apply '$MANIFEST_URL' --force \
  > /root/block/ota/apply-manual.log 2>&1 & echo started pid \$!"   # nohup 防 SSH 断连杀 dd
```

前提：该版本已经用 `rockbase-ota-release` skill 发布到
`NMminer1024/btc-solo-node-release`，且 0 节的密钥核对已通过。

**镜像来源**：`output/k230_rockbase_defconfig/images/rootfs.ext2`。
用户说「重新编译再升级」时才先构建（见 `rockbase-build` skill）；否则直接用现有构建产物。
但每次触发 OTA 前都必须确认待打包文件来自最新编译结果：检查 `rootfs.ext2` 的 mtime、
`/etc/version/release_version`，以及刚改过的关键文件是否已进入 `rootfs.ext2`。
如果无法证明当前 OTA 包对应最新编译产物，先重编或停止并报告，不要拿旧包升级。

**重要**：热部署到板上的修复不会自动进入 OTA 包。若刚修了 `rockbase-ota.sh`、
`S00resizemmc` 或 `rockbase-lcdui`，必须重新构建镜像，并用 `debugfs` 从新的
`rootfs.ext2` 抽文件验证后再 OTA。

**多台设备连续 OTA**：复用同一个已发布到公开仓库的包即可，依次对每台设备重复上面的
`check`/`apply` 命令。触发前必须确认目标机能从自身网络访问生产域名或 GitHub，例如：

```bash
ssh root@<IP> 'curl --cacert /etc/ssl/certs/ca-certificates.crt --retry 2 -sfI https://ota.nmminer.com/rockbase/manifest.json'
```

如果这里失败，先修域名/DNS/路由/防火墙问题；未修通前不执行 OTA。

### 2. 监控 dd 进度（约 2~4 分钟）

```bash
ssh root@192.168.124.30 '
  cat /run/rockbase/ota-state.json 2>/dev/null || true
  grep "flashing progress\|flash done\|switching boot\|rebooting" /root/block/ota/ota.log | tail -20
  ps | grep -v grep | grep -E "rockbase-ota|gunzip|dd of=" || true'
```

日志出现 `rebooting into new slot` 或 SSH 断连 = 正常（正在切槽重启）。
SSH 中途 banner timeout 属正常（dd 占 SD 卡时 sshd 响应慢），重试即可。

写盘正常日志示例（124.30，2026-08-24 已验证）：

```text
flashing progress: 42% (.../2048 MiB)
flashing progress: 49% (.../2048 MiB)   # 已越过旧故障点
...
flashing progress: 94% (.../2048 MiB)
flash done
switching boot slot -> <new-slot> (upgrade_available=1, bootcount=0)
rebooting into new slot...
```

### 3. 升级后验证清单

```bash
ssh-keygen -R 192.168.124.30        # host key 已变
ssh root@192.168.124.30 '
  cat /proc/cmdline | cut -d" " -f1            # root= 应切到另一个分区
  grep sdk: /etc/version/release_version       # 版本应是新推的
  fw_printenv ota_part upgrade_available bootcount
  mount | grep mmcblk                          # p3→/root/config p4→/root/block
  md5sum /root/config/bitcoin.conf             # 与升级前一致（配置保留）
  rockbase-ota.sh status | head -6'
```

通过标准：

- `root=` 指向新槽分区，`ota_part` 已翻转
- 版本号为新推版本
- `upgrade_available=0`（S98otaconfirm 自动确认；若还是 1，等 2 分钟或手动
  `rockbase-ota.sh confirm`）
- p3/p4 挂载正常、配置 md5 不变、服务（bitcoind 等）自动恢复

### 4. 失败处置

- apply 中途断电/崩溃：**无所谓**，运行槽不受影响，重跑一次推送即可（A/B 的意义）。
- 如果日志停在 `flashing progress: 36~43%` 后重启，且重启后仍是旧槽、
  `upgrade_available=0`，说明写槽中途被重启，未执行到 `fw_setenv`，A/B 保护正常。
  优先检查当前镜像里的 OTA agent 是否包含 direct quiesce、`S05watchdog`/`S04serialhealth`
  是否在写盘期被清理。
- 如果日志停在 `quiescing services for OTA`，检查是否有卡住的
  `S61bitcoind stop`、`S43statusd stop`、`/root/bitcoind/stop.sh` 或重复 apply。
  当前正确实现应直接清理进程，并用 apply lock 防重复触发。
- 如果重启后 `/root/block` 没挂载、`upgrade_available=1` 未确认，检查 `S00resizemmc`
  是否包含 `resize_fs()` 中 `e2fsck -f -y` 后接受 `rc < 4` 再 retry `resize2fs` 的修复。
- 连续 3 次启动未确认 → uboot 自动回滚旧槽（E4 已验证）。
- 手动回滚：板上 `rockbase-ota.sh rollback`。
- 完整排查手册：`docs/rockbase-ota-handoff.md` 第 4/6 节。

## 已知边界

- 镜像来自统一 `k230_rockbase_defconfig`（1G/2G 已合并，mmc 拓扑统一为 mmc0=SDIO WiFi / mmc1=SD）。
- 生产源（公开 GitHub Releases `NMminer1024/btc-solo-node-release` + `ota.nmminer.com`
  Cloudflare 重定向入口层）已上线并在真机验证通过，详见
  `docs/rockbase-ota-handoff.md` 第 13 节。真机测试统一直接用
  `https://ota.nmminer.com/rockbase/manifest.json` 或 GitHub Releases 直链跑
  `rockbase-ota.sh check/apply`。
- 镜像包约 43MB（2G 槽 gzip），usb有线版板端下载+验签 ~10s，dd 2G 约 2~4 分钟。
