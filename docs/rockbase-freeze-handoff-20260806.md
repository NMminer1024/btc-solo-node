# Rockbase Freeze Debug Handoff - 2026-08-06

本文档用于交接 Rockbase K230 卡死问题的后续排查。当前核心目标不是通过 watchdog 重启规避问题，而是定位并降低系统在 SD 卡高写压下进入不可恢复卡死的概率，同时保证卡住前后能从串口拿到足够证据。

## 当前结论

- 主要怀疑方向：SD 卡写回/flush/jbd2/ext4 路径在高写压下把块层和 MMC 请求队列压满，导致多个任务进入 D 状态，系统响应显著下降，最终可能发展成整机卡死。
- 已确认 SD 卡和 WiFi 不是同一个 MMC host：日志显示 `mmc0` 是 SD 卡 `SD128`，`mmc1` 是 SDIO WiFi。因此目前更像是 SD 卡/块层/文件系统写回问题，不是 SD 与 WiFi 共用同一个 host 的硬件互斥。
- 压测已经能稳定制造与昨晚卡死现场相似的前兆：高 `disk_wbusy`/`block_wbusy`、`flush-179:0`、`kblockd`、`jbd2/mmcblk0p3-8`、多个 `dd` 进入 D 状态。
- 压测尚未稳定复现“屏幕时间停止、串口完全无后续有效输出”的最终卡死状态。当前压测阶段屏幕仍能更新，说明系统尚未完全死锁。
- 2026-08-07 新增 COM14 真实卡死证据：在已禁用 RTL8189FS WiFi 低功耗的镜像上仍复现卡死，因此“WiFi 低功耗”不是充分根因；当前需要把重点转向 `8189fs` 的 SDIO TX/RX 事务、`sdio_irq_work`、`RTW_XMIT_THREAD` 与 MMC core/workqueue 交互。
- COM14 这次和纯 SD 写压压测不是同一形态：最终阶段 `dirty`/`writeback` 很低，D 状态不集中在 ext4/jbd2/flush，而是出现 `RTW_XMIT_THREAD` 卡在 `sd_write -> sdio_write_port`，同时 `events` workqueue 中有 `sdio_irq_work` 执行或堆积。
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

2026-08-06 22:54 至 2026-08-07 02:22，COM14 真实卡死日志：

- 日志文件：`C:\Users\bitpony\Desktop\log\COM14_2026_08_06.22.54.17.747.txt`
- 设备：`wlan0=up ip=192.168.124.29/24`，约 2G 内存板，日志中显示 `mem=570/1951MB`。
- 用户确认该设备运行的是 2026-08-06 晚间重新编译的镜像，包含禁用 RTL8189FS WiFi 低功耗补丁。
- 最后 health 行停在 `2026-08-07 02:22:02`，之后串口无响应：
  - `uptime=13111s load=7.82 6.86 5.80 mem=570/1951MB swap=0/2047MB dirty=1680KB wb=0KB`
  - `tasks=R2/S49/D0/Z0`
  - `io mmcblk0:+r=0KB +w=9818KB busy=58% wbusy=228% in_flight=0`
  - `mmcblk0p3:+r=0KB +w=9482KB busy=57% wbusy=196% in_flight=0`
- 这不是“SD 卡写回爆掉”的尾部状态：`dirty` 和 `wb` 很低，`in_flight=0`，没有持续大量 ext4/jbd2/flush D 状态堆积。
- 关键 D 状态证据：
  - `RTW_XMIT_THREAD state=D wchan=__mmc_claim_host`
  - sysrq 栈进入 `8189fs` TX 路径：`sd_write -> sdio_write_port -> _rtw_write_port -> _dequeue_writeport -> rtl8188fs_xmit_buf_handler -> rtw_xmit_thread`
  - `events` workqueue 中出现 `sdio_irq_work` in-flight/pending
  - `sdio_irq_work` 栈进入 `8189fs` RX/中断路径：`_sd_read -> sdio_read_port -> sd_recv -> sd_int_dpc -> sd_int_hdl -> sd_sync_int_hdl -> sdio_irq_work`
  - `kblockd` 中仍可见 `blk_mq_timeout_work` pending，但本次尾部没有 SD 写回高压特征，不能把它当成单一 SD 卡写满队列问题。
- host 拓扑再次确认：
  - `mmc0 card=mmc0:5048 type=SD name=SD128`
  - `mmc1 card=mmc1:0001 type=SDIO`
- 本次结论：低功耗补丁不够，下一步应审查和增强 RTL8189FS SDIO 读写路径的超时、错误处理、host claim 保护、`sdio_irq_work` 调度隔离和 WiFi 自恢复能力。

2026-08-07，`192.168.124.28` 硬复位后 SSH 捞取到的 freeze log：

- 设备：`192.168.124.28`，1G 内存板，复位后 `MemTotal=967720 kB`。
- 运行镜像：内核 `Linux canaan 6.6.36 #2 SMP Thu Aug 6 13:57:09 CST 2026`，cmdline 已包含诊断参数 `ignore_loglevel log_buf_len=4M keep_bootcon printk.always_kmsg_dump=1`。
- `boot_audit.log` 显示 `prev_shutdown=unclean`，符合硬复位/非正常关机。
- `/sys/fs/pstore` 为空：本次没有 ramoops/pstore crash 记录，说明没有捕获到 panic/oops 尾巴，或者最终卡死时没有进入 pstore 可保存路径。
- 已复制现场日志到 WSL 本地：`/tmp/rockbase-12428-freeze-20260807/`
  - `serialhealth-diag.log`
  - `messages`
  - `messages.0`
- 复位前最后有效 `serialhealth-diag.log` 时间停在 `2026-08-06 19:13:43`，随后出现 NUL 填充，再进入新启动的 `1970-01-01/2026-08-07 09:08` 日志。
- 复位前最后阶段不是持续大脏页写回：
  - `2026-08-06 19:13:42 diag trigger=D d=1 disk_wbusy=224% block_wbusy=178%`
  - `nr_dirty=823 nr_writeback=0`
  - 最后 host 记录仍正常输出到 `2026-08-06 19:13:43`
- 关键栈与 COM14 高度一致：
  - `RTW_XMIT_THREAD state=D`
  - `__mmc_claim_host -> sdio_claim_host -> sd_write -> sdio_write_port -> _rtw_write_port -> _dequeue_writeport`
  - 模块为 `[8189fs]`
- syslog 中也有相同证据：
  - `Aug 6 11:09:50 task:RTW_XMIT_THREAD state:D`
  - `__mmc_claim_host -> sdio_claim_host -> sd_write -> sdio_write_port -> _rtw_write_port -> _dequeue_writeport -> rtl8188fs_xmit_buf_handler -> rtw_xmit_thread`
  - 多次出现 `workqueue events` pending `sdio_irq_work`
  - 多次出现 `kblockd` pending `blk_mq_timeout_work`
- 124.28 的 host 拓扑与 124.29/2G 板编号相反，但仍然是分 host：
  - `mmc0 card=mmc0:0001 type=SDIO`
  - `mmc1 card=mmc1:5048 type=SD name=SD64G`
- 本次结论：124.28 与 COM14/124.29 指向同一个问题族，不是单纯 SD 卡写回压死，也不是 WiFi 低功耗未关闭；核心嫌疑仍是 RTL8189FS SDIO host claim/read-write/irq work 路径在真实业务流量下卡住，进一步拖垮单核系统调度与 MMC/workqueue。

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
