# Rockbase 当前交接文档

更新时间：2026-08-11。

## 当前状态

仓库当前在 `main`，已经从 `dev` 合并。

相关提交：

- `cc30835f Enable Rockbase pstore capture`
- `1f7ef1ae Merge dev into main`

`origin/dev` 和 `origin/main` 都已经推送到远端。

## 当前测试镜像

两份都是非 seed 最小镜像，`K230_BLOCKDATA_SIZE=256M`。

1G 镜像：

- 板子：`192.168.124.28`
- 版本：`sdk:v1.2-20260811-104352-bitpony-DESKTOP-KC5S6JM-1f7ef1ae-nncase0.0.0`
- 预期大小：约 `6.4G`
- pstore 地址：`ramoops@3fc00000`

2G 镜像：

- 板子：`192.168.124.23`
- 版本：`sdk:v1.2-20260811-104909-bitpony-DESKTOP-KC5S6JM-1f7ef1ae-nncase0.0.0`
- 预期大小：约 `6.4G`
- pstore 地址：`ramoops@7fc00000`

## 已经证明的事实

- 两台板子都已注册 `pstore/ramoops`。
- 两台板子都已挂载 `/sys/fs/pstore`。
- `S02pstore` 会把上一次 pstore 记录归档到 `/root/log/pstore-archive/latest`。
- panic 和 lockup 相关 sysctl 已开启。
- `124.28` 已通过端到端 panic 测试：
  - 执行 `echo c > /proc/sysrq-trigger`
  - 10 秒后自动重启
  - 从 `console-ramoops-0` 恢复到 panic 和调用栈

## 当前 Freeze 假设

当前最可信模型：

- 主嫌疑：RTL8189FS SDIO 路径，尤其是 TX/RX/IRQ work 与 `mmc_claim_host()` 争抢。
- 放大器：SD 卡写回突发、bitcoind 磁盘同步、过密内核/串口诊断日志。
- 不太像主因：LVGL 应用死锁、内存耗尽、纯 ext4 写回、WiFi 低功耗单独导致。

关键注意点：

- SD 卡和 WiFi 在不同 MMC host 上，但 1G/2G host 编号不同。
- 反复出现的尾部证据跟随 WiFi SDIO host，而不是固定跟随 `mmc0`。

## 如果下一次再次卡死

不要丢失第一次重启后的证据。

硬复位或自动重启后：

```bash
ssh root@192.168.124.28 'cat /root/log/pstore-archive/latest/*' > pstore-124.28.txt
ssh root@192.168.124.23 'cat /root/log/pstore-archive/latest/*' > pstore-124.23.txt
```

随后保存：

- 完整串口日志。
- 如果能登录，保存 `/root/log/serialhealth-diag.log`。
- 如果能登录，保存 `/var/log/messages` 和 rotated logs。
- `/etc/version/release_version`。
- `dmesg | grep -Ei 'ramoops|pstore|8189|rtw|mmc|sdio'`。

具体流程见：

- `docs/rockbase-debug-playbook.md`

## 下一步工程方向

不要盲目增加高频日志。

建议下一步：

1. 让两台 pstore 镜像在真实负载下继续跑。
2. 如果再 freeze，先分类 pstore 捕获到了什么：
   - panic/oops
   - hung task panic
   - softlockup panic
   - 没有记录，说明更像完全硬锁死
3. 如果 pstore 捕获到 RTW/SDIO 争抢，继续推进更完整的 SDIO 访问串行化或 WiFi bus 层恢复方案。
4. 如果硬 freeze 后 pstore 为空，再评估更低层的独立 watchdog/NMI 类抓手，但必须先考虑 bitcoind 数据安全。

## 不要忘记

- 默认构建不带 seed。
- 非 seed 镜像约 `6.4G`。
- 使用 `K230_BLOCKDATA_SIZE=256M`。
- 不要只凭沙箱里的 `/mnt/d` 只读现象下结论，必要时提权做真实写测试。
- UI/业务改动先热部署验证，再固化进镜像。

