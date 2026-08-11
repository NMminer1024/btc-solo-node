# Rockbase NM Backend API Wiring Plan

## 1. 文档目的

本文档用于定义 Rockbase 上 `NM` 监控前端对应的后端真数据接线方案。

目标是把当前 `shell CGI 即时采样` 模式，演进为：

- `常驻 API 守护进程`
- `后台定时采样`
- `内存缓存`
- `前端按菜单按需读取`

本文档不直接实现代码，但要求设计足够具体，后续可以据此进入编码。

## 2. 产品术语与页面文案

### 2.1 为什么不建议继续叫“全节点”

当前产品实际形态是：

- `bitcoind`
- `pruned=true` 的剪枝模式
- 搭配本地 `ckpool / stratum` 独立挖矿服务

从 Bitcoin 技术语义上说：

- 剪枝节点仍然是 `validating node`
- 它会完整验证区块和交易
- 但它不是常见用户理解里的“保留全历史区块数据的 full archival node”

因此，纯用户界面上继续大写强调 `Full Node`，容易造成误导。

### 2.2 推荐产品定位名称

我建议把产品的用户可见定位改成下面这组术语。

#### 中文推荐

主标题建议：

- `NM 比特币验证节点`

副标题建议：

- `剪枝验证节点 + 独立挖矿服务`

如果你想更产品化一点，也可以用：

- `NM 独立挖矿节点`

但更准确的技术副标题要补上：

- `基于 Bitcoin 剪枝验证节点与本地 Stratum 服务`

#### 英文推荐

主标题建议：

- `NM Bitcoin Node Monitor`

副标题建议：

- `Pruned Bitcoin validating node + solo mining service`

不建议继续写：

- `Bitcoin Full Node + Solo Mining Pool`

因为这里的 `Full Node` 容易被理解成“全历史完整节点”。

### 2.3 页面级术语建议

首页顶部：

- 中文：`剪枝验证节点 + 独立挖矿服务`
- 英文：`Pruned validating node + solo mining service`

节点卡片标题：

- 中文：`Bitcoin 节点`
- 英文：`Bitcoin Node`

模式徽标：

- 中文：`剪枝验证节点`
- 英文：`Pruned Validating Node`

挖矿卡片标题：

- 中文：`独立挖矿服务`
- 英文：`Solo Mining Service`

如果想保留 `Stratum` 术语，也建议写成：

- 中文：`Stratum 独立挖矿服务`
- 英文：`Stratum Solo Mining Service`

### 2.4 推荐统一文案

建议后续前端统一采用以下一组：

- 产品标题：`NM Bitcoin Node Monitor`
- 中文副标题：`剪枝验证节点 + 独立挖矿服务`
- 英文副标题：`Pruned validating node + solo mining service`

这组最稳，兼顾准确性和用户理解。

## 3. 当前后端问题

当前数据来自：

- `buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/api/status.cgi`

问题不是字段不够，而是采样方式太重：

- 每次 HTTP 请求都实时采样
- 每次请求都创建大量 shell 进程
- 每次请求都解析 `bitcoin-cli` 输出
- 每次请求都做目录扫描、日志读取和网络命令调用

这会造成：

- 响应时间抖动
- CPU 峰值
- 前端轮询成本偏高
- 未来多菜单扩展后成本失控

## 4. 目标架构

### 4.1 总体结构

建议新增一个轻量守护进程，例如：

- `rockbase-webd`

职责如下：

1. 周期性采样系统状态
2. 周期性读取 bitcoind / ckpool / network / ntp 状态
3. 把结果存入内存缓存
4. 对外提供 HTTP JSON API
5. 可选同时提供静态文件

### 4.2 推荐实现语言

推荐：

- `C`

推荐原因：

- 运行时轻
- Buildroot 集成直接
- 更适合 K230 设备
- 后续可继续保留 BusyBox 体系

### 4.3 推荐依赖

最小建议：

- `cJSON`

可复用但非必须新增：

- `libevent`
- `zlib`

不建议首版引入：

- Node.js
- Python Web 框架
- Nginx + FastCGI

## 5. 服务边界

### 5.1 建议服务形式

后端服务建议独占一个本地端口，例如：

- `127.0.0.1:9080`

前端访问路径有两种设计：

#### 方案 A

- `busybox httpd` 继续发静态资源
- 新增轻量反向代理或路径转发

这个方案改造不够干净。

#### 方案 B

- `rockbase-webd` 直接接管 `80` 端口
- 自己同时发静态文件和 JSON API

这个方案更整洁，也更接近 AxeOS 的边界。

我建议后续实现以 `方案 B` 为目标。

## 6. API 路径设计

### 6.1 只读接口

建议首版先实现以下接口：

- `GET /api/summary`
- `GET /api/system`
- `GET /api/bitcoin`
- `GET /api/mining`
- `GET /api/network`
- `GET /api/meta`
- `GET /api/status`

其中：

- `status` 是兼容聚合接口
- 其它接口按菜单职责拆分

### 6.2 日志接口

建议首版先只做只读 tail 型日志：

- `GET /api/logs/bitcoind?tail=100`
- `GET /api/logs/ckpool?tail=100`
- `GET /api/logs/system?tail=100`

要求：

- 默认限制最大返回行数
- 不提供整文件全量下载
- 日志页按需访问，不在首页自动轮询

### 6.3 元信息接口

建议 `GET /api/meta` 返回：

- board model
- firmware version
- web version
- api version
- uptime
- sample timestamp
- active theme default

## 7. 缓存模型

### 7.1 核心原则

改成：

- `定时采样`
- `缓存`
- `请求只读缓存`

不要：

- `请求触发采样`

### 7.2 建议缓存结构

建议进程内维护以下结构体：

```c
typedef struct {
    char cpu_model[128];
    char uptime[32];
    char temp[16];
    int load_pct;
    char load_display[32];
    int mem_pct;
    char mem_used_display[16];
    char mem_total_display[16];
    int disk_pct;
    char disk_size_info[32];
    int swap_pct;
    char swap_used_display[16];
    char swap_total_display[16];
    int block_disk_found;
    char block_disk_value[16];
    char block_disk_path[64];
    uint64_t sampled_at_ms;
} nm_system_cache_t;

typedef struct {
    int running;
    char sync_key[24];
    char sync_class[16];
    char prune_key[24];
    char prune_class[16];
    char progress_pct[16];
    int progress_ring;
    char headers_fmt[24];
    char blocks_fmt[24];
    char best_block_date[16];
    char btc_context[64];
    char connections[16];
    char difficulty_display[24];
    char rss[16];
    int pruned;
    int initial_block_download;
    uint64_t sampled_at_ms;
} nm_bitcoin_cache_t;

typedef struct {
    int ckpool_running;
    char ckpool_class[16];
    char ckpool_state[24];
    char stratum_key[16];
    char btc_address[96];
    char ckpool_tail[1024];
    uint64_t sampled_at_ms;
} nm_mining_cache_t;

typedef struct {
    char wifi_ssid[64];
    char ip_addr[40];
    char iface[16];
    char mac_addr[24];
    char rssi_display[16];
    int rssi_pct;
    int signal_level;
    char ntp_class[16];
    char ntp_key[16];
    uint64_t sampled_at_ms;
} nm_network_cache_t;
```

## 8. 采样线程与周期

### 8.1 推荐采样周期

建议不要用单线程串行把所有数据一口气采完。

推荐按模块分周期：

- system：`2s`
- bitcoin：`5s`
- mining：`5s`
- network：`10s`
- block disk size：`30s`
- logs tail cache：`10s`
- meta/version：启动时一次，必要时 `60s`

### 8.2 锁策略

缓存读写建议使用：

- 一个全局读写锁
  或
- 每个模块单独锁

建议首版先用模块级互斥锁，逻辑更清晰。

## 9. 真数据来源映射

### 9.1 system 数据来源

| 字段 | 数据来源 | 建议实现 |
| --- | --- | --- |
| `cpu_model` | `/proc/device-tree/model`，回退 `/proc/cpuinfo` | 直接文件读取 |
| `uptime` | `/proc/uptime` | 进程内格式化 |
| `temp` | `/sys/class/thermal/...` / `/sys/class/hwmon/...` | 顺序探测 |
| `load_pct` / `load_display` | `/proc/loadavg` + CPU 核数 | 进程内计算 |
| `mem_pct` / memory displays | `/proc/meminfo` | 进程内计算 |
| `swap_pct` / swap displays | `/proc/meminfo` | 进程内计算 |
| `disk_pct` / `disk_size_info` | `statvfs("/")` | 不再调用 `df` |
| `block_disk_value` | `du -sm /root/block` 替换为目录大小计算或保留低频 shell | 首版可低频 shell，后续内建遍历 |
| `block_disk_path` | 常量 `/root/block` | 常量 |

### 9.2 bitcoin 数据来源

主要来源：

- `bitcoin-cli -conf=/root/bitcoind/bitcoin.conf -datadir=/root/block`

建议调用：

- `getblockchaininfo`
- `getnetworkinfo`

仅在必要时调用：

- `getblockhash <height>`
- `getblockheader <hash>`

建议流程：

1. 先检测 `bitcoind` 是否在运行
2. 如果未运行，直接填默认状态，不做 RPC
3. 如果运行，拉 `getblockchaininfo`
4. 再拉 `getnetworkinfo`
5. 只有当 `blocks` 发生变化时，才刷新 `best_block_date`

这样可以避免每个 bitcoin 采样周期都多打一轮 `getblockhash + getblockheader`

### 9.3 mining 数据来源

| 字段 | 数据来源 | 建议实现 |
| --- | --- | --- |
| `ckpool_running` | `pidof ckpool` 或 `/proc` 扫描 | 进程内实现 |
| `ckpool_class` / `ckpool_state` | 运行态映射 | 进程内映射 |
| `stratum_key` | 解析 `/root/ckpool/ckpool.conf` 中 `url` | 启动时读入，必要时重载 |
| `btc_address` | 同上配置文件里的 `btcaddress` | 启动时读入 |
| `ckpool_tail` | `/root/ckpool/logs/ckpool.log` | 低频读取最后 N 行 |

### 9.4 network 数据来源

| 字段 | 数据来源 | 建议实现 |
| --- | --- | --- |
| `wifi_ssid` | `iw` / `iwconfig` | 首版可保留外部命令 |
| `ip_addr` / `iface` | `getifaddrs()` 优先 | 避免 shell `ip addr` |
| `mac_addr` | `/sys/class/net/*/address` | 文件读取 |
| `rssi_display` / `rssi_pct` / `signal_level` | `iw dev wlan0 link`，回退 `iwconfig` | 首版可外部命令 |
| `ntp_key` / `ntp_class` | 进程检测 + 时间同步判断 | 不能只看 `ntpd` 进程 |

### 9.5 meta 数据来源

| 字段 | 数据来源 |
| --- | --- |
| firmware version | `/etc/version/*` 或构建写入的版本文件 |
| board model | `/proc/device-tree/model` |
| api version | 守护进程内常量 |
| web version | 静态文件构建时写入 |
| sample timestamp | 各 cache 的 `sampled_at_ms` |

## 10. 应避免继续使用 shell 的字段

以下项建议优先去 shell 化：

- `df`
- `ip addr`
- `cat /proc/... | grep | awk`
- `ps | grep`

建议改成：

- `statvfs`
- `getifaddrs`
- 直接读 `/proc`
- 遍历 `/proc` 或 `pidof` 等轻路径

## 11. 可接受暂时保留 shell 的字段

首版为了快速稳定落地，可以接受暂时保留 shell 的少数高复杂度字段，但必须降频：

- `bitcoin-cli`
- `iw` / `iwconfig`
- `ckpool log tail`
- `block data size`

原则：

- 可以保留
- 但不能放在 HTTP 请求路径里
- 必须转移到后台采样线程

## 12. 状态语义与错误语义

### 12.1 HTTP 状态

首版建议：

- 接口存在且缓存可读：`200`
- 接口不存在：`404`
- 仅在守护进程内部异常且无法返回 JSON 时：`500`

即使某个模块数据异常，也尽量返回：

- `200`
- JSON 内带字段级状态

### 12.2 字段级降级策略

例如：

- bitcoind 未运行
  - `running=false`
  - `sync_key=btc_not_running`
  - 其它字段填 `-` 或 `null`

- ckpool 未运行
  - `ckpool_running=false`
  - `ckpool_state=stopped`

- network 无 WiFi
  - `wifi_ssid="-"` 或 `"N/A"`

### 12.3 日志接口降级

如果日志文件不存在：

- 返回 `200`
- `lines=[]`
- `exists=false`

不要因为日志文件缺失直接返回 `500`。

## 13. 首版接口 JSON 建议

### 13.1 `/api/summary`

建议只返回首页最重要字段：

```json
{
  "product_title": "NM Bitcoin Node Monitor",
  "subtitle": "Pruned validating node + solo mining service",
  "device_status": "healthy",
  "device_status_text": "Healthy",
  "sync_progress": "99.82%",
  "bitcoind_rss": "2.3G",
  "block_data_size": "318G",
  "network_status": "-58 dBm",
  "sampled_at": 1784701800000
}
```

### 13.2 `/api/bitcoin`

建议延续当前字段命名，减少前端改造成本。

### 13.3 `/api/status`

建议直接按现有四大块输出：

- `system`
- `bitcoin`
- `mining`
- `network`

这样旧页面和新页面都容易兼容。

## 14. 建议落地文件结构

建议新增 Buildroot package：

```text
buildroot-overlay/package/rockbase_webd/
  Config.in
  rockbase_webd.mk
  src/
    main.c
    http_server.c
    http_server.h
    cache.h
    cache.c
    sample_system.c
    sample_bitcoin.c
    sample_mining.c
    sample_network.c
    sample_logs.c
    json_render.c
    util_proc.c
    util_fmt.c
```

同时补 rootfs 启动脚本：

```text
buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/init.d/S43rockbase_webd
```

启动顺序建议：

- 在 network 后
- 在 bitcoind / ckpool 前也可以启动
- 但内部采样允许等待服务上线

## 15. 实施顺序

建议按下面顺序推进：

1. 先实现 `rockbase-webd` 的最小 HTTP 服务骨架
2. 先接 `GET /api/status`，字段对齐当前页面
3. 把当前前端 mock 切到真数据
4. 再拆分 `summary/system/bitcoin/mining/network/meta`
5. 最后再接日志接口和设置接口

这样风险最小。

## 16. 调试与固化规则

必须继续遵守项目既定流程：

1. 先 `scp/ssh` 热部署到真机
2. 在线验证页面和 API 行为
3. 通过后再回写仓库源码
4. 最后再编译镜像

后端守护进程改造也必须遵守这条流程。

## 17. 结论

后端真正需要解决的不是“把 CGI 改成另一个 CGI”，而是把系统改成：

- `轻量守护进程`
- `按模块采样`
- `内存缓存`
- `按菜单按需输出`

而在产品命名上，最稳妥的定位不是“全节点”，而是：

- 中文：`剪枝验证节点 + 独立挖矿服务`
- 英文：`Pruned validating node + solo mining service`

这既准确，也更适合后续页面和接口文案统一。
