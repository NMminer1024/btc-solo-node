---
name: rockbase-ota
description: 'Rockbase A/B OTA 升级真机流程。Use when: ota升级, OTA 升级, ota push, 推送固件, 触发 OTA, ota升级124.30, 升级板子, rockbase-ota.sh, mk-ota-package, OTA 测试, 槽位切换, 回滚验证.'
---

# Rockbase OTA 升级（真机）

> 典型触发：「ota升级124.30」「OTA 升级 192.168.124.30」。
> 设计文档：`docs/design/rockbase-ota-plan.md`；进度/坑：`docs/rockbase-ota-handoff.md`。

---

## 前置认知（不要重新踩坑）

- 布局：`p1=slot_a / p2=slot_b / p3=config(rockcfg) / p4=blockdata`，每槽 `/boot/` 自包含。
- 板端 agent：`/usr/sbin/rockbase-ota.sh`（`status/check/apply/confirm/rollback`）。
  真 apply 已内置 **direct quiesce**：不依赖容易卡住的 init `stop`，而是直接按
  cmdline/killall 清理 `bitcoind`/`ckpool`/`statusd`/miners/supervisord/serialhealth/
  用户态 watchdog，保留 lcdui/httpd/网络/sshd。**不要手动再停服务**。
- OTA agent 必须有 `/run/rockbase/ota-apply.lock` 防并发 apply；重复触发应被拒绝，
  不能出现多个 `rockbase-ota.sh apply`/`gunzip` 同时写槽。
- 写盘进度以目标分区 `/proc/diskstats` 写扇区增量为准，并写入
  `/run/rockbase/ota-state.json`（LCD 覆膜）和 `/dev/console`（串口日志）。
  不要再用 pipeline `$!` + `/proc/<pid>/io` 判断 dd 进度。
- **必须 nohup 跑 apply**：SSH 会话死掉会杀远程 dd（E2 踩过）。`tools/ota-push.sh` 已处理。
- 切槽重启后 **SSH host key 会变**（另一个槽有独立 `/etc/ssh`）：
  先 `ssh-keygen -R <ip>` 再连。脚本已处理前置，升级后若再变需手动清一次。
- 升级后 `S98otaconfirm` 等健康（httpd+sshd+bitcoind，≤120s）自动 confirm；
  不健康则不确认，靠 uboot `bootcount>3` 自动回滚旧槽。
- uboot 变量**单层展开**：env 里的 bootargs 必须内联，不要加中间变量（c87eaab5 的坑）。

## 标准流程

### 1. 推送（WSL 侧一条命令）

```bash
tools/ota-push.sh 192.168.124.30            # 版本号自动取镜像 release_version
tools/ota-push.sh 192.168.124.30 v0.1.0     # 或显式指定
```

脚本自动完成：密钥检查（`~/.rockbase-ota-keys/`，不在仓库内）→ 板端公钥比对/部署 →
`mk-ota-package.sh` 打包签名 → HTTP 服务（`~/ota-dist`，PID 文件管理，幂等）→
板端 `nohup apply --force`。

**镜像来源**：`output/k230_rockbase_1g_defconfig/images/rootfs.ext2`。
用户说「重新编译再升级」时才先构建（见 `rockbase-build` skill）；否则直接用现有构建产物。

**重要**：热部署到板上的修复不会自动进入 OTA 包。若刚修了 `rockbase-ota.sh`、
`S00resizemmc` 或 `rockbase-lcdui`，必须重新构建 1G 镜像，并用 `debugfs` 从新的
`rootfs.ext2` 抽文件验证后再 OTA。

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

- 只支持 1G 板（`k230_rockbase_1g_defconfig`）；2G 未适配（mmc 设备号相反）。
- 生产源（GitHub Releases + `ota.nmminer.com` 重定向）未上线，当前走 WSL 本地 HTTP。
- 镜像包约 43MB（2G 槽 gzip），usb有线版板端下载+验签 ~10s，dd 2G 约 2~4 分钟。
