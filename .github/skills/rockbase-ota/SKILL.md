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
  真 apply 已内置 **quiesce**（自动停 supervisord/bitcoind/ckpool 等、暂停 freeze 看门狗，
  保留 lcdui/httpd/网络/sshd），**不要手动再停服务**。
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

### 2. 监控 dd 进度（约 2~4 分钟）

```bash
ssh root@192.168.124.30 '
  awk "\$3==\"mmcblk1pX\" {print \$10\"/4194304\"}" /proc/diskstats   # X=非活动槽
  tail -3 /root/block/ota/ota.log'
```

日志出现 `rebooting into new slot` 或 SSH 断连 = 正常（正在切槽重启）。
SSH 中途 banner timeout 属正常（dd 占 SD 卡时 sshd 响应慢），重试即可。

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
- 连续 3 次启动未确认 → uboot 自动回滚旧槽（E4 已验证）。
- 手动回滚：板上 `rockbase-ota.sh rollback`。
- 完整排查手册：`docs/rockbase-ota-handoff.md` 第 4/6 节。

## 已知边界

- 只支持 1G 板（`k230_rockbase_1g_defconfig`）；2G 未适配（mmc 设备号相反）。
- 生产源（GitHub Releases + `ota.nmminer.com` 重定向）未上线，当前走 WSL 本地 HTTP。
- 镜像包约 43MB（2G 槽 gzip），usb有线版板端下载+验签 ~10s，dd 2G 约 2~4 分钟。
