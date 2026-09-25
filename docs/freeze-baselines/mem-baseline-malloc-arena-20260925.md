# 内存基线 — ckpool MALLOC_ARENA_MAX 修复实验（2026-09-25）

> **实验目的**：验证 `MALLOC_ARENA_MAX=2` 限制 glibc per-thread malloc arena 后，
> ckpool RSS 下降 → page cache 增大 → SD 卡随机读压力降低 → 卡死频率下降。
>
> **根因**：ckpool 27 线程 + glibc 默认 arena 上限（8×cores+8，未设 MALLOC_ARENA_MAX）
> → 每线程独立 arena 增长到 ~64MB 且不归还 OS → 8-10 个 arena 锁死 ~200-233MB。
>
> **修复**：`S62ckpool` 两处启动点注入 `MALLOC_ARENA_MAX=2`（源码已固化，commit 待提交）。
>
> **复查时间**：2026-09-26 ~13:00 UTC（部署后 24h），对比各板：
> - ckpool RSS / big_arenas
> - page cache（buff/cache）、available
> - zram swap 使用率
> - 卡死次数（pstore-archive 目录数增量 + uptime 是否异常重置）
> - serialhealth-diag.log 的 trigger=D / disk_wbusy 频率

## 部署前基线（2026-09-25 12:52 UTC 采集）

| MAC | IP | 采集时刻 | uptime | ckpool RSS | big_arenas | bitcoind RSS | buff/cache | available | zram swap |
|-----|-----|---------|--------|-----------|-----------|-------------|-----------|-----------|-----------|
| e0:95:bc:bb:62:bf | 124.22 | 12:44（修复前） | ~23h | **233 MB** | 9 | 516 MB | 119 MB | 121 MB | 100% (131/131) |
| e0:60:19:30:32:bc | 124.15 | 12:52 | 8:42 | **195 MB** | 10 | 495 MB | 203 MB | 210 MB | 100% (131/131) |
| e0:a2:01:6a:99:35 | 124.28 | 12:52 | 1d 3:28 | **194 MB** | 8 | 506 MB | 176 MB | 177 MB | 100% (131/131) |
| e0:eb:4e:2c:2d:70 | 124.34 | 12:52 | 1d 3:23 | **233 MB** | 9 | 486 MB | 129 MB | 133 MB | 100% (131/131) |
| e0:d9:1b:92:d8:56 | 124.31 | 12:52 | 6:32 | **203 MB** | 10 | 523 MB | 168 MB | 172 MB | 100% (131/131) |

共同特征：
- 全部 `MALLOC_ARENA_MAX=unset`（glibc 默认）
- 全部 27 线程
- 全部 zram 128MB **100% 打满**（anon 页全被挤出）
- page cache 仅 119-203MB，而 chainstate 10.9GB → 命中率 ~1-2%

## 124.22 修复后参考值（2026-09-25 12:48 UTC，部署后 3min）

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| ckpool RSS | 233 MB | **68 MB** |
| big_arenas | 9 | 1 |
| buff/cache | 119 MB | **323 MB** |
| available | 121 MB | **329 MB** |
| zram swap | 100% | 82% |
| ckpool 功能 | — | 正常（workbase 3705 tx，日志正常滚动） |

## 部署记录

| MAC | IP | 部署时间 (UTC) | 方式 | 部署后 ckpool RSS | 状态 |
|-----|-----|--------------|------|------------------|------|
| e0:95:bc:bb:62:bf | 124.22 | 2026-09-25 12:44 | kill + MALLOC_ARENA_MAX=2 nohup 重启 | 68 MB（3min 稳定） | ✅ 已验证 |
| e0:60:19:30:32:bc | 124.15 | 2026-09-25 ~13:05 | scp S62ckpool + kill + start | 37 MB（刚启动） | ✅ |
| e0:a2:01:6a:99:35 | 124.28 | 2026-09-25 ~13:05 | 同上 | 22 MB（刚启动） | ✅ |
| e0:eb:4e:2c:2d:70 | 124.34 | 2026-09-25 ~13:05 | 同上 | 26 MB（刚启动） | ✅ |
| e0:d9:1b:92:d8:56 | 124.31 | 2026-09-25 ~13:05 | 同上 | 37 MB（刚启动） | ✅ |

**部署踩坑**：第一版脚本注释里写了 `ckpool's`（撇号），在 `start_waiter` 的 `sh -c '...'` 单引号串内破坏引号结构 → 板上 `syntax error: unexpected "fi"`，且旧进程已被 kill 导致 ckpool 短暂停摆。修正注释（去撇号）+ 本地 `sh -n` 验证后重新部署成功。
**教训**：改 init 脚本（尤其含 `sh -c '...'` 嵌套引号的）部署前必须 `sh -n` 语法检查；备份在板上 `/etc/init.d/S62ckpool.bak-20260925`。

## 部署后即时快照（2026-09-25 ~13:10 UTC，部署后 ~10min）

| MAC | IP | ckpool RSS | big_arenas | buff/cache | available | 对比修复前 |
|-----|-----|-----------|-----------|-----------|-----------|-----------|
| e0:95:bc:bb:62:bf | 124.22 | 121 MB | 1 | 264 MB | 264 MB | 233→121 / 119→264 |
| e0:60:19:30:32:bc | 124.15 | 57 MB | 0 | 366 MB | 366 MB | 195→57 / 203→366 |
| e0:a2:01:6a:99:35 | 124.28 | 35 MB | 1 | 340 MB | 340 MB | 194→35 / 176→340 |
| e0:eb:4e:2c:2d:70 | 124.34 | 38 MB | 0 | 332 MB | 332 MB | 233→38 / 129→332 |
| e0:d9:1b:92:d8:56 | 124.31 | 50 MB | 0 | 332 MB | 332 MB | 203→50 / 168→332 |

全部 5 台 `MALLOC_ARENA_MAX=2` 生效，big_arenas ≤1，page cache 普遍翻倍以上。

## 部署后 ~24h 中期快照（2026-09-25 13:15 UTC，部署后 ~1h+，实际为次日复查前采集）

> 采集时间 2026-09-25 13:15 UTC（部署后约 1 小时）。ckpool 已从刚启动的 22-68MB
> 涨到 140-163MB 平台期，**RSS ≈ HWM 且不再刷新**，属 glibc 正常行为
>（malloc 释放不归还 OS，运行数小时后达到分配模式稳态），非泄漏。

| MAC | IP | ckpool RSS | HWM | heap 段数 | 负载 | 整机 avail |
|-----|-----|-----------|-----|----------|------|-----------|
| e0:95:bc:bb:62:bf | 124.22 | 163 MB | 163 MB | 1 | 无矿机（1 user/1 worker 本地会话） | 239 MB |
| e0:60:19:30:32:bc | 124.15 | **153 MB** | 153 MB | 1 | **真实矿机：2 users / 9 workers，~48TH/s** | 263 MB |
| e0:a2:01:6a:99:35 | 124.28 | 141 MB | 141 MB | 1 | 无矿机 | 251 MB |
| e0:eb:4e:2c:2d:70 | 124.34 | 140 MB | 140 MB | 1 | 无矿机 | 231 MB |
| e0:d9:1b:92:d8:56 | 124.31 | 147 MB | 153 MB | 1 | 无矿机 | 232 MB |

**⚠️ 修正（部署后 ~5h，13:15→18:00 UTC 对比）**：ckpool 仍在缓慢爬升 +14~33MB（186/174/155/163/180MB），
3 台 HWM 被刷新，增速 ~5MB/h。之前"平台期"结论下早了——**判定标准修正为看增速是否收敛**，
不能只看 RSS≈HWM。若 24h 复查时增速仍 ≥5MB/h（RSS > 250MB），需查 ckpool 是否有
per-share/per-worker 累积结构（share 历史、worker 统计缓存）。即使持续 24h 也仅 ~260MB，
仍低于修复前 195-233MB 锁死值 + 无 arena 膨胀，暂不紧急。

### 爬升根因定论（源码级分析，2026-09-25 14:30 UTC）

**结论：非泄漏 = 60s 周期 GBT 大分配 × glibc 堆棘轮效应。无矿机板子同样爬升，因 GBT 循环与矿机会话完全解耦。**

源码证据链（`dl/ckpool/git/src/`）：
1. **周期性大分配**：generator 线程每 60s 无条件拉 GBT（`gen_loop` generator.c:394 → `gen_gbtbase` bitcoin.c:128）。
   每次 ~4000 tx 的 GBT JSON 产生 ~4-6MB 临时分配：yyjson_rpc_call 解析（~2MB）+ mut_doc 整份拷贝（~2MB）
   + imut_copy 存进 workbase->gbtdoc（~2MB）。板上日志实证：`Stored local workbase with 3960 transactions` 每秒滚动。
2. **存活数据有界**：workbase 最多 3 个（stratifier.c:1125 `HASH_COUNT < 3`），>10min 由 `clear_workbase`
   （stratifier.c:800）完整释放（txn_data/gbtdoc/merkle 全 free）；share 按 workbase 老化清理
   （purge/age_share_hashtable）；`ckyyalc`（yyjson_util.c:35）就是普通 malloc 包装，无自定义池。
   存活上限 ≈ 3 × 2MB = 6MB。**释放路径完整，无泄漏点。**
3. **RSS 棘轮机制（板上实测 124.22）**：VmData 421MB 但 RSS 仅 174MB——`/proc/maps` 显示
   [heap] 主段 26MB + 1 个 65MB 全驻留段 + **24 个 8MB 段 Rss=0kB（纯虚拟预留）**。
   glibc 每轮 GBT 大分配向内核 mmap 新 8MB subheap 段；释放时只 munmap 堆顶连续空闲区，
   中间空洞留着复用 → RSS 棘轮上移 + 虚拟地址持续预留；下一轮复用上一轮 chunk →
   净效果 157-188MB 锯齿震荡（实测轨迹 140→186→173→157），非单调上涨。
4. **⚠️ 指标修正**：`grep -c heap /proc/maps` 只数 `[heap]` 主段（恒=1），**测不到匿名 arena 段**，
   之前"heap 段数=1"指标无效。真正的健康指标：**RSS 是否持续刷新 HWM 且突破 200MB**
   （当前 HWM 187 冻结，RSS 157-188MB 震荡 = 正常）。

**关键结论**：
- 修复效果稳定：稳态 140-163MB（修复前 195-233MB）
- **内存不随 worker 数线性增长**：124.15 带 9 个真实矿机 worker 仅 153MB，比无矿机的 124.22（163MB）还略低；stratum 单会话开销 KB 级
- 泄漏判定标准（24h 复查用）：RSS 单调逼近并**刷新 HWM** 才是泄漏特征；锯齿震荡 + HWM 冻结 = 正常
- 若 24h 复查时 HWM 被刷新到 200MB+ 才需重新排查

### 同期 bitcoind 内存分析（2026-09-25 13:10 UTC）

| 指标 | 值 | 说明 |
|------|-----|------|
| 当前 RSS | 524-571 MB（5 台一致） | smaps_rollup: Pss_Anon 398MB + Pss_File 142MB |
| HWM 峰值 | 807-840 MB（5 台高度一致） | 块下载/验证期瞬态峰值，非泄漏（峰值后回落且 24h 不涨） |
| UTXO 集 | 118 MiB | debug.log 确认，符合 dbcache=128 |
| leveldb cache | ~128 MB | dbcache=128MiB |
| mempool | ≤48 MB | maxmempool=50 |
| heap 段数 | 3 | 无 glibc arena 膨胀（15 线程未触发） |
| Pss_File 142MB | mmap .ldb 页 | 属 page cache，可回收，非真实压力 |

**bitcoind 判定：正常，无泄漏**。构成与配置参数一一对应（UTXO 118 + leveldb 128 + mempool ≤48 + 块处理 ~100 ≈ 398MB anon）。
**风险提示**：峰值叠加场景 `bitcoind 831 + ckpool 163 + lcdui 3 + sshd 3 + 内核 ~50 ≈ 1050MB > 967MB 总量`，
avail 压到 ~0、zram 打满，是 freeze 链的内存侧背景。可选缓解（收益有限）：`dbcache=96`（省 ~32MB）、
`maxmempool=20`（solo 挖矿不需要 50MB，省 ~28MB 峰值）；治本仍是减少 SD 随机读延迟。

## 部署后 24h 复查结果（2026-09-26 填写）

> 判定标准：ckpool RSS 锯齿震荡在 200MB 内且 HWM 未被刷新 = 正常（爬升根因见上节定论）。

| MAC | IP | ckpool RSS/HWM | buff/cache | available | zram | 卡死次数 | trigger=D 频率 | 结论 |
|-----|-----|-----------|-----------|-----------|-----------|------|---------|---------------|------|
| e0:95:bc:bb:62:bf | 124.22 | | | | | | | | |
| e0:60:19:30:32:bc | 124.15 | | | | | | | | |
| e0:a2:01:6a:99:35 | 124.28 | | | | | | | | |
| e0:eb:4e:2c:2d:70 | 124.34 | | | | | | | | |
| e0:d9:1b:92:d8:56 | 124.31 | | | | | | | | |

### 复查命令（复用）

```bash
for ip in 22 15 28 34 31; do
  echo "=== 192.168.124.$ip ==="
  timeout 30 ssh -o ConnectTimeout=8 -o StrictHostKeyChecking=no -o BatchMode=yes root@192.168.124.$ip '
    cat /sys/class/net/wlan0/address; uptime
    CKPID=$(pidof ckpool)
    # 健康指标 = VmRSS/VmHWM（HWM 持续刷新且 >200MB 才异常）；
    # 注意：grep -c heap /proc/maps 只数 [heap] 主段，测不到匿名 arena 段，勿用
    grep -E "VmRSS|VmHWM|Threads" /proc/$CKPID/status
    cat /proc/$CKPID/environ | tr "\0" "\n" | grep MALLOC
    free
    grep zram /proc/swaps
    ls /root/log/pstore-archive/ | wc -l
    grep -c "trigger=D" /run/rockbase-log/serialhealth-diag.log 2>/dev/null
  ' 2>/dev/null
done
```
