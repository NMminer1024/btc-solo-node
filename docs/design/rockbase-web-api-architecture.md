# Rockbase Web/API 架构记录

## 文档目的

本文档合并原 `NM 后端 API 接线方案` 和 `NM 网页前端架构`，保留后续 Web/API 改造需要的关键设计。

当前重点是：降低设备侧采样压力，让网页监控从“请求触发重采样”演进为“后台采样 + 内存缓存 + 轻量 API”。

## 产品定位

Rockbase 的产品形态是：

- Bitcoin 剪枝验证节点。
- 本地 Stratum / solo mining 服务。
- Web 页面和 LCD 页面用于状态监控。

用户可见文案建议：

- 中文：`剪枝验证节点 + 独立挖矿服务`
- 英文：`Pruned validating node + solo mining service`

不建议在用户界面里继续突出 `Full Node`，因为设备运行的是 `pruned=true`，容易被理解为保存全历史区块的 archival node。

## 当前问题

当前 Web 数据主要来自：

- `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/api/status.cgi`

现有 CGI 的主要问题：

- 每次 HTTP 请求都会即时采样。
- 每次请求都会创建多个 shell 进程。
- 频繁调用 `grep`、`awk`、`sed`、`df`、`du`、`ip`、`iw`、`ps`。
- 还可能调用 `bitcoin-cli getblockchaininfo`、`getnetworkinfo`、`getblockhash`、`getblockheader`。

这会导致：

- 请求延迟抖动。
- CPU 瞬时峰值。
- 页面偶发卡顿或空白。
- 多页面扩展后维护成本失控。

## 目标架构

建议演进为：

```text
静态前端资源 + 常驻 rockbase-webd + 后台定时采样 + 内存缓存 + JSON API
```

核心原则：

- HTTP 请求只读缓存，不触发重采样。
- 高成本字段降频采样。
- 日志只按需读取，不在首页轮询。
- 前端保持轻量，不把重型 SPA 作为第一阶段前提。

## 后端服务建议

建议新增轻量常驻进程：

- `rockbase-webd`

推荐实现：

- 首选 C。
- 最小依赖可用 `cJSON`。
- 可选复用 `libevent`、`zlib`。
- 不建议首版引入 Node.js、Python Web 框架、Nginx + FastCGI。

服务边界：

- 第一阶段可保留 BusyBox httpd 发静态资源。
- 更干净的目标方案是 `rockbase-webd` 直接提供静态资源和 JSON API。

## API 设计

建议接口：

- `GET /api/summary`
- `GET /api/system`
- `GET /api/bitcoin`
- `GET /api/mining`
- `GET /api/network`
- `GET /api/meta`
- `GET /api/status`

其中：

- `summary` 给首页总览。
- `status` 作为兼容聚合接口。
- 其它接口按页面职责拆分。

日志接口：

- `GET /api/logs/bitcoind?tail=100`
- `GET /api/logs/ckpool?tail=100`
- `GET /api/logs/system?tail=100`

日志接口规则：

- 默认限制最大返回行数。
- 不提供整文件全量下载。
- 日志页按需访问，不在首页自动轮询。
- 日志文件不存在时返回 `200`、`exists=false`、`lines=[]`，不要直接 `500`。

## 采样周期建议

按模块分频，不要所有数据同频刷新：

- `summary`：5 秒。
- `system`：2 到 5 秒采样，页面可 10 秒请求。
- `bitcoin`：5 到 10 秒。
- `mining`：5 到 10 秒。
- `network`：10 到 15 秒。
- `block_disk_value`：30 到 60 秒。
- `logs tail cache`：10 秒或只在日志页触发。
- `meta/version`：启动时一次，必要时 60 秒。

高成本字段：

- `block_disk_value` 不应每次请求都 `du`。
- `best_block_date` 只在 blocks/header 高度变化时刷新。
- `ckpool_tail` 只截取最后 N 行并缓存。
- `bitcoin-cli` 不能放在 HTTP 请求路径里。

## 数据来源映射

system：

- CPU 型号：`/proc/device-tree/model`，回退 `/proc/cpuinfo`。
- uptime：`/proc/uptime`。
- 温度：`/sys/class/thermal` 或 `/sys/class/hwmon`。
- load/mem/swap：`/proc/loadavg`、`/proc/meminfo`。
- 根分区容量：`statvfs("/")`。
- block data size：低频目录遍历或低频 shell。

bitcoin：

- 配置：`/root/bitcoind/bitcoin.conf`。
- datadir：`/root/block`。
- RPC：`getblockchaininfo`、`getnetworkinfo`。
- `getblockhash/getblockheader` 只在高度变化时调用，用于刷新 best block time。

mining：

- `ckpool` 运行态：遍历 `/proc` 或 `pidof`。
- Stratum URL 和 BTC 地址：解析 `/root/ckpool/ckpool.conf`。
- ckpool 日志：低频 tail。

network：

- IP/interface：`getifaddrs()` 优先。
- MAC：`/sys/class/net/*/address`。
- RSSI：首版可保留低频 `iw` / `iwconfig`。
- NTP：不能只看 `ntpd` 进程，最好结合时间同步状态。

meta：

- 固件版本：`/etc/version/*`。
- 板卡型号：`/proc/device-tree/model`。
- API/Web version：构建写入或守护进程常量。

## 前端方向

第一阶段保持轻量：

- 静态 `index.html`。
- 模块化 `app.js`。
- 单独 `style.css`。
- 构建期可预压缩 `html/js/css/svg`。

页面信息架构：

- Dashboard：整体健康状态和关键摘要。
- Node：bitcoind 细节。
- Mining：ckpool/Stratum 状态。
- Network：联网和时间同步状态。
- Logs：日志查看，按需请求。
- Settings：后续配置入口。
- About：版本和构建信息。

Dashboard 边界：

- 只负责总览和异常发现。
- 不放大段日志。
- 不堆所有字段。
- 具体排查进入子页面。

## 性能目标

- 普通 API 请求不触发外部命令采样。
- 页面连续轮询不造成明显 CPU 峰值。
- 降低 shell 进程创建次数。
- 降低 `bitcoin-cli` 调用频率。
- 降低目录扫描和日志读取频率。

## 迁移顺序

建议顺序：

1. 新增 `rockbase-webd` 最小 HTTP 服务骨架。
2. 先实现兼容 `GET /api/status`，字段对齐当前页面。
3. 前端热部署切到新接口验证。
4. 再拆分 `summary/system/bitcoin/mining/network/meta`。
5. 最后接日志接口和设置接口。

## 调试与固化规则

Web/API 改动必须遵守项目规则：

- 先热部署到真板。
- 在线验证页面和 API 行为。
- 验证通过后再回写仓库源码。
- 只有用户明确要求编译/输出镜像时，才编译。
