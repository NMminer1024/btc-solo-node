# Rockbase Freeze Debug Handoff - 2026-08-06

本文档用于交接 Rockbase K230 卡死问题的后续排查。当前核心目标不是通过 watchdog 重启规避问题，而是定位并降低系统在 SD 卡高写压下进入不可恢复卡死的概率，同时保证卡住前后能从串口拿到足够证据。

## 当前结论

- 主要怀疑方向：SD 卡写回/flush/jbd2/ext4 路径在高写压下把块层和 MMC 请求队列压满，导致多个任务进入 D 状态，系统响应显著下降，最终可能发展成整机卡死。
- 已确认 SD 卡和 WiFi 不是同一个 MMC host：日志显示 `mmc0` 是 SD 卡 `SD128`，`mmc1` 是 SDIO WiFi。因此目前更像是 SD 卡/块层/文件系统写回问题，不是 SD 与 WiFi 共用同一个 host 的硬件互斥。
- 压测已经能稳定制造与昨晚卡死现场相似的前兆：高 `disk_wbusy`/`block_wbusy`、`flush-179:0`、`kblockd`、`jbd2/mmcblk0p3-8`、多个 `dd` 进入 D 状态。
- 压测尚未稳定复现“屏幕时间停止、串口完全无后续有效输出”的最终卡死状态。当前压测阶段屏幕仍能更新，说明系统尚未完全死锁。
- 用户明确不倾向 watchdog 自动重启，因为 bitcoind 区块数据可能损坏；优先方向是让系统不要卡死，以及在卡死时保留内核级串口证据。

## 关键日志证据

2026-08-05 晚间真实卡死前日志特征：

- load 从约 `2.5` 升到 `4.9`。
- `mmcblk0p3` 写入持续偏高，经常出现 `+w=50MB` 到 `+w=90MB` 级别。
- 出现 `D` 状态任务，包括 `RTW_XMIT_THREAD:D:__mmc_claim_host`、`kworker/...+kblockd`。
- 但当时缺少更完整的内核栈、sysrq、pstore/ramoops 证据。

2026-08-06 压测日志特征：

- `dirty`/`writeback` 高，例如 `dirty=149928KB wb=48688KB`。
- `disk_wbusy`/`block_wbusy` 极高，例如 `41963%`、`60246%`、`85054%`、`92057%`。
- D 状态任务栈集中在：
  - `flush-179:0` -> `blk_mq_get_tag`、`__wait_on_buffer`、`do_get_write_access`
  - `kblockd` -> `mmc_blk_rw_wait`
  - `jbd2/mmcblk0p3-8` -> `folio_wait_writeback`、`jbd2_journal_finish_inode_data_buffers`
  - `dd` -> `balance_dirty_pages_ratelimited_flags`、`ext4_da_get_block_prep`
- MMC host 记录：
  - `mmc0 card=mmc0:5048 type=SD name=SD128`
  - `mmc1 card=mmc1:0001 type=SDIO`

## 已加入的诊断改动

当前工作区已有未提交改动，主要文件如下：

- `buildroot-overlay/board/canaan/k230_rockbase/fragment/linux.fragment`
  - 增加 `CONFIG_LOG_BUF_SHIFT=20`
  - 增加 `CONFIG_MAGIC_SYSRQ=y`
  - 增加 `CONFIG_PSTORE=y`
  - 增加 `CONFIG_PSTORE_CONSOLE=y`
  - 增加 `CONFIG_PSTORE_PMSG=y`
  - 增加 `CONFIG_PSTORE_RAM=y`
  - 增加 `CONFIG_DYNAMIC_DEBUG=y`
  - 增加 `CONFIG_MMC_DEBUG=y`
- `buildroot-overlay/boot/uboot/u-boot-2022.10-overlay/board/canaan/common/k230_img.c`
  - 给 bootargs 追加 `ignore_loglevel log_buf_len=4M keep_bootcon printk.always_kmsg_dump=1`
  - 去掉 NOR flash bootargs 里的 `quiet`
- `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/init.d/S04serialhealth`
  - hung task 超时从 60 秒调到 30 秒
  - 打开 hung/soft lockup all CPU backtrace
  - 打开 sysrq
- `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/usr/bin/rockbase_serial_healthd.sh`
  - 串口打印更细的 D 状态、VM、MMC host、进程栈诊断
  - 当出现 D 状态或极高 wbusy 时，触发 sysrq `w` 和 `t`，冷却时间 30 秒
  - 同步写 `/root/log/serialhealth-diag.log`，保留最近诊断文本
- `buildroot-overlay/board/canaan/k230_rockbase/patches/linux/0028-add-rockbase-linux-dts.patch`
  - 1G DTS 增加 `ramoops@3fc00000`，预留 4M
  - 2G LCD DTS 增加 `ramoops@7fc00000`，预留 4M
  - 1G LCD DTS 也已补 `ramoops@3fc00000`
- `AGENTS.md`
  - 已加入 seed 镜像规则：默认走不带 seed 的镜像，只有用户明确要求时才构建/导出带 seed 镜像。

## 当前镜像/构建状态

- 1G 目标 defconfig：`k230_rockbase_defconfig`
- 2G 目标 defconfig：`k230_rockbase_2g_defconfig`
- 当前默认调试目标板：`192.168.124.23`，2G 内存板
- 可对比板：`192.168.124.28`，1G 内存板
- 本轮已经成功编译过 1G 镜像，说明上述诊断改动可编过。
- WSL 侧大镜像文件已按用户要求清理：
  - `/tmp/k230-rockbase-img` 已删除
  - `output/k230_rockbase_defconfig/images/sysimage-sdcard.img` 已删除
  - `output/k230_rockbase_defconfig/images/rootfs.ext2` 已删除
  - `output/k230_rockbase_defconfig/images/blockdata.ext4` 已删除
  - `output/k230_rockbase_defconfig/images/boot.ext4` 已删除
- `output/k230_rockbase_defconfig/images` 目前只剩较小的内核/uboot/dtb 等中间产物。

## 镜像规则

默认不要构建带 seed 的镜像。带 seed 镜像会生成约 30G 级别的完整 `sysimage-sdcard.img`，非常耗时，且当前排查重点是卡死问题，不是区块数据预置。

默认 1G 不带 seed 构建命令：

```bash
make CONF=k230_rockbase_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_OUT_DIR=/tmp/k230-rockbase-img \
  K230_KEEP_WSL_IMAGES=1
```

默认 2G 不带 seed 构建命令：

```bash
make CONF=k230_rockbase_2g_defconfig buildroot \
  BLOCK_SEED_DIR=/tmp/rockbase-empty-seed \
  K230_BLOCKDATA_SIZE=256M \
  K230_IMAGE_OUT_DIR=/tmp/k230-rockbase-img \
  K230_KEEP_WSL_IMAGES=1
```

只有用户明确说“带 seed”时，才允许使用默认 `seed/root-block` 或显式指定真实 seed 目录，并接受 30G 级别产物和长时间拷贝。

## Windows 导出问题

当前 `/mnt/d` 多次变成只读挂载，表现为：

```text
D:\ /mnt/d 9p ro,...
```

这会导致 `post-image.sh` 最后导出到 `/mnt/d/code/pony/IoT/k230/img` 失败。构建本身可在 WSL 文件系统内完成，问题只影响最后导出。

建议后续 agent 执行导出前先检查：

```bash
findmnt -no SOURCE,TARGET,FSTYPE,OPTIONS /mnt/d
test -w /mnt/d/code/pony/IoT/k230/img && echo writable || echo not_writable
```

如果仍是 `ro`，不要强行导出到 Windows 侧。先导出到 `/tmp/k230-rockbase-img`，等用户在 Windows 侧修复 `D:` 盘状态后再拷。

用户已询问过彻底方案，建议方向：

- Windows 管理员 PowerShell 执行 `powercfg /h off`
- Windows 管理员 PowerShell 执行 `chkdsk D: /f`
- 完整重启 Windows
- 进 WSL 后手动重新挂载：

```bash
sudo umount /mnt/d
sudo mount -t drvfs D: /mnt/d -o rw,metadata,uid=$(id -u),gid=$(id -g),umask=022,fmask=111
```

## 推荐后续验证目标

1. 刷入不带 seed 的 1G 或 2G 镜像，确认串口启动日志里有：
   - `ignore_loglevel`
   - `log_buf_len=4M`
   - `pstore`/`ramoops` 初始化信息
   - `serial health` 启动日志
2. 在真实业务环境中运行，不主动压测，等待是否再次卡住。
3. 如果卡住前串口仍能输出，优先收集：
   - `[health] uptime=...`
   - `[health] io ...`
   - `[health] watch=...`
   - `[health] diag trigger=...`
   - `[health] diag vm ...`
   - `[health] diag stack ...`
   - kernel hung task/sysrq `w`/`t` 输出
4. 硬复位后检查 pstore/ramoops：

```bash
ls -l /sys/fs/pstore
cat /sys/fs/pstore/* 2>/dev/null | tail -n 200
```

5. 如果真实环境不再卡，但压测仍能制造 D 状态，下一步不是继续加压，而是开始尝试降低写回尖峰。

## 可能的修复方向

这些还没有最终验证，作为后续候选：

- 限制 bitcoind/dbcache 或业务写入峰值，避免把 SD 卡写回队列打满。
- 调整 VM dirty 参数，降低脏页积累：
  - `vm.dirty_background_bytes`
  - `vm.dirty_bytes`
  - `vm.dirty_expire_centisecs`
  - `vm.dirty_writeback_centisecs`
- 检查 ext4 mount 选项，评估 `commit=`、`data=`、barrier 相关影响，但不能牺牲区块数据一致性。
- 验证 SD 卡质量、写入延迟长尾和供电稳定性。
- 如果 sysrq/pstore 能捕获最终卡死现场，再决定是否需要内核级 MMC 超时、blk-mq、ext4/jbd2 方向的补丁。

## 接手注意事项

- 当前代码工作区是 dirty 状态，不要随意 `git checkout --` 或 `git reset --hard`。
- 不要默认热部署；用户此前明确说设备已经挂过，重启/热部署需要他确认现场状态。
- 默认镜像构建必须不带 seed，除非用户明确要求带 seed。
- 当前优先级是解决卡死，不是优化镜像导出速度或 seed 数据流程。
- 每次构建前建议先确认 `/mnt/d` 是否可写；不可写就输出到 `/tmp`。
