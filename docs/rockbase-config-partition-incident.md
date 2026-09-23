# Rockbase /root/config 分区撑爆事故记录

> 日期：2026-09-23
> 触发板子：192.168.124.11（1G，OTA 网页端"manifest 下载失败"）
> 关联提交：`85162102`（slot 镜像清理 + ckpool.log truncate）、后续 `cleanup_workdir` 增强

## 现象

124.11 网页端点击"强制 OTA 升级"一直提示 **manifest 下载失败**，局域网其他设备正常。

## 根因

`/root/config`（mmcblk1p3，约 975MB）被写满（100%，可用 0）。OTA 的下载目录
`/root/config/ota/` 和 `ota.log` 都在该分区，磁盘满后 curl 报
`curl: (23) Failed writing body`，manifest 缓存写不进，网页端轮询
`/api/ota.cgi?action=manifest` 拿不到有效数据 → 提示下载失败。

**不是网络问题**：手动 `curl` 拉 manifest 一直通，DNS/WiFi/CA 证书均正常。
其他板子正常是因为它们还没累积到撑爆的量级。

### 占用明细（124.11 事发时）

| 路径 | 大小 | 说明 |
|------|------|------|
| `/root/config/ckpool/logs/ckpool.log` | **884.8M** | ckpool 单文件日志，无轮转 |
| `/root/config/ota/slot-v0.4.00-...img.gz` | 73.6M | 旧 slot 镜像残留 |

## 全面审计：/root/config 所有持续增长项

对 124.9 做全量 `find + du` 审计，逐项确认是否有清理机制：

| 增长项 | 写入方 | 增速 | 清理机制 | 状态 |
|--------|--------|------|----------|------|
| `ckpool/logs/ckpool.log` | ckpool stdout（每秒 1 行状态） | ~7MB/天 | **原无** → 已加启动时 truncate（>50MB） | ✅ 已修 |
| `ota/slot-*.img.gz` | rockbase-ota.sh apply 下载 | +74MB/次升级 | **原无** → 已加 flash 后删除 + check 时清旧 | ✅ 已修 |
| `ota/ota.log` | rockbase-ota.sh `log()`（tee -a） | 低（仅 OTA 操作时） | **原无** → 已加字节轮转（>1MB 留 512KB） | ✅ 已修 |
| `ota/apply-web-*.log` | ota.cgi 每次 web 触发 apply | 每次 +5~16KB | **原无** → 已加只保留最近 5 个 | ✅ 已修 |
| `rockbase-history/miners/*.jsonl` | rockbase_miners_historyd.sh | 低（~30KB/天/文件） | **已有**：`RETENTION_DAYS=7` 自动删 + 离线用户清理 | ✅ 安全 |
| `ckpool/logs/users/*`、`pool/pool.status` | ckpool | 极小（KB 级） | 固定大小 | ✅ 安全 |
| `bitcoin.conf`/`ckpool.conf`/`rockbase-wifi.conf` | 配置 | 固定 | — | ✅ 安全 |

**结论**：4 个无界增长项已全部加清理；`rockbase-history` 自带 7 天保留策略，安全。

## 修复内容

### 1. `rockbase-ota.sh` — `cleanup_workdir()`（核心）

新增函数，在 `check-async`（周期性）和 `apply` 前调用：

- **ota.log 轮转**：>1MB 时 `tail -c 512K` 保留尾部（按字节，不依赖换行符，
  BusyBox 兼容）。
- **apply-web-*.log**：只保留最近 5 个，旧的删除。
- **slot-*.img.gz/.sig**：删除与当前 manifest 版本不符的旧镜像（处理上次
  apply 中断残留）。
- **flash 成功后**：`cmd_apply` 里 dd 完成即删除所有 slot 镜像（主清理点）。

### 2. `S62ckpool` — `trim_log()`

启动 ckpool 前（直接 start + bitcoind-ready waiter 两条路径）检查
`ckpool.log`，>50MB 则 `: >` truncate。

## 踩到的坑

1. **板子 BusyBox 无 `stat` 命令**：取文件大小必须用 `wc -c < file` 或
   `du -b`，不能用 `stat -c%s`。
2. **`tail -n` 按行轮转不可靠**：若日志无换行符（或整段算一行），
   `tail -n 500` 会原样保留整个文件。改用 `tail -c` 按字节轮转。
3. **`mj_str` 参数顺序**：签名是 `mj_str <field> <file>`，不是
   `mj_str <file> <field>`。

## 验证（124.9 真机）

| 测试 | 结果 |
|------|------|
| 清理 7 个旧 slot 镜像 | `/root/config` 51% → 0% |
| ckpool.log truncate（60MB 模拟） | 正确截断为 0，ckpool restart 正常 |
| ota.log 字节轮转（3MB 模拟） | 3146522 → 525161 字节 |
| apply-web 日志清理（10 个模拟） | 保留最近 5 个 |
| 旧 slot 清理（v0.9.99 模拟） | 已删除，当前版本保留 |
| `rockbase-ota.sh status` | 正常，无副作用 |

## 运维建议

- 长期运行的板子，`/root/config` 应稳定在 <20%。若发现 >50%，优先查
  `ckpool.log` 和 `ota/` 目录。
- 开发期频繁 OTA 测试时，每次升级后 `ota/` 应只剩当前 manifest + 少量日志。
- 监控命令：
  ```sh
  df -h /root/config
  du -sh /root/config/* | sort -rh
  ```
