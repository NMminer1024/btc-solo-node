# Rockbase NM Web Frontend Architecture

## 1. 文档目的

本文档用于定义 Rockbase 板卡上新一代 `NM 全节点监控` 网页的前端风格、信息架构、接口边界与设备侧减压方案。

目标不是直接进入实现，而是先统一以下设计原则：

- 页面风格参考 `readvid.ai/zh` 这类现代 AI SaaS 产品页，强调简洁、科技感、留白和结果导向。
- 数据交互参考 `AxeOS` 的思路，采用 `静态前端资源 + 独立 API` 的形式。
- 尽量降低 K230 嵌入式设备每次请求的 CPU 抖动、进程创建和 `bitcoin-cli` 重复调用成本。
- 保留当前页面中已经展示的核心监控数据，但用更清晰、更高级、更轻量的方式呈现。

## 2. 适用范围

当前目标板卡与网页路径：

- 板卡：`Rockbase`
- defconfig：`k230_rockbase_defconfig`
- 当前网页根目录：`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web`

当前页面已展示的数据主要来自：

- `system`
- `bitcoin`
- `mining`
- `network`

这些字段当前由 `root/web/api/status.cgi` 在每次请求时动态生成。

## 3. 现状问题

### 3.1 当前优点

当前网页已经具备基础的前后端分离雏形：

- 前端是静态 `index.html + style.css`
- 前端通过 `fetch('/api/status.cgi')` 拉取 JSON
- 页面自己完成渲染，而不是后端拼整页 HTML

这说明架构方向本身并没有走错。

### 3.2 当前主要问题

当前性能瓶颈不在“浏览器渲染”，而在“后端生成 JSON 的方式”：

- `status.cgi` 是 shell CGI
- 每次请求都会拉起大量外部命令
- 频繁调用 `grep`、`awk`、`sed`、`df`、`du`、`ip`、`iw`、`ps`
- 还会调用 `bitcoin-cli getblockchaininfo`、`getnetworkinfo`、`getblockhash`、`getblockheader`

这会带来几个直接问题：

- 单次请求延迟抖动大
- CPU 瞬时负载高
- 并发能力差
- 页面主观体验容易出现“卡顿”“慢一拍”“偶发空白”
- 后续功能越多，CGI 越难维护

### 3.3 当前页面风格问题

当前页面虽然信息完整，但视觉上更偏“功能页”，而不是“产品页”：

- 卡片层级不够鲜明
- 首屏缺少主叙事
- 状态重点不够聚焦
- 设计语言偏工程化，缺少产品感
- 页面缺少一种统一、克制、有辨识度的品牌视觉

## 4. 新方案总览

新网页采用以下总体框架：

`预压缩静态前端资源` + `轻量常驻 API 服务` + `浏览器定时拉取/按需拉取`

核心原则如下：

1. 前端资源在构建期产出为静态文件，不在请求时拼 HTML。
2. 后端 API 由常驻服务提供，不再使用 shell CGI 逐请求现算。
3. 设备侧维护一份内存态缓存，后台定时采样，HTTP 请求只读取缓存。
4. 接口按数据变化频率拆分，避免一个大接口把所有重逻辑绑在一起。
5. 前端保持轻量，不引入重型 SPA 运行时作为第一阶段前提。

## 5. 视觉风格定义

### 5.1 风格目标

视觉方向参考 `readvid.ai/zh` 一类网站的优点，但不直接模仿页面内容。

希望新网页呈现出以下气质：

- 极简科技感
- 高级留白
- 低噪音信息布局
- 重点状态一眼可见
- 像“产品面板”，不是“系统后台”

### 5.2 设计关键词

- `Clean`
- `Focused`
- `Editorial`
- `Calm Tech`
- `Premium Utility`

### 5.3 风格约束

- 避免传统路由器后台风格
- 避免大面积花哨渐变和强对比霓虹
- 避免密集表格主导页面
- 避免“所有数据同权重堆满首页”
- 避免紫色默认 AI 风

### 5.4 建议视觉语言

- 背景：暖灰白或冷白带轻微层次，而不是纯白平面
- 主色：深石墨、墨绿、深青、金属灰
- 强调色：单一品牌强调色，建议在青绿或钴蓝中二选一
- 字体风格：偏现代、克制、窄一点的标题风格
- 圆角：中等圆角，避免过度拟物
- 阴影：轻阴影，重点靠层级而不是厚阴影
- 动效：仅保留淡入、卡片渐显、状态切换，不做炫技动画

## 6. 页面信息架构

### 6.1 页面目标

首页要回答四个问题：

1. 机器现在是否健康
2. Bitcoin 节点是否正常运行和同步
3. 挖矿服务是否在线
4. 当前网络和关键资源是否稳定

### 6.2 新页面结构

建议采用单页式结构，分为五个层级区块。

#### A. Hero 顶部总览区

用于一眼判断设备状态，不堆所有细节。

建议内容：

- 页面标题：`NM Full Node Monitor`
- 副标题：`Bitcoin Full Node + Solo Mining on Rockbase`
- 设备主状态徽标：`Healthy / Syncing / Warning / Offline`
- 3 到 4 个关键摘要指标
  - 节点同步状态
  - Bitcoind RSS
  - 磁盘占用
  - 网络 IP 或 NTP 状态

这一屏要更像产品首页，而不是运维表格。

#### B. Core Metrics 核心指标区

展示最关键信息，用大数字和轻图形表达：

- CPU Load
- Memory
- Disk
- Swap
- Bitcoin IBD 进度

建议用环形图或横向进度，但减少花哨装饰。

#### C. Bitcoin / Mining 双主卡区

左侧 `Bitcoin Node`，右侧 `Stratum Service`。

`Bitcoin Node` 卡：

- Sync 状态
- Full / Pruned
- Headers / Blocks
- Best Block Date
- Connections
- Difficulty
- RSS
- Chain / Subversion

`Stratum Service` 卡：

- Service 状态
- Solo / Pool 模式
- BTC 地址
- 最近日志摘要

日志只保留小摘要，不做大段滚动终端感。

#### D. System / Network 辅助信息区

用于展示稳定性相关信息：

- CPU 型号
- Uptime
- Temperature
- Block Data 路径与容量
- IP / Interface
- WiFi SSID
- MAC
- RSSI
- NTP 状态

#### E. Footer / Meta 区

只保留少量说明：

- 数据更新时间
- 固件版本
- API 延迟
- 当前网页版本

### 6.3 多菜单模式扩展

本架构不仅支持单页 Dashboard，也支持演进为类似 AxeOS 的多菜单模式。

推荐菜单树如下：

- `Dashboard`
- `Node`
- `Mining`
- `Network`
- `Logs`
- `Settings`
- `About`

第一阶段以 `Dashboard` 为主，但从架构设计上，菜单扩展应当被视为正式目标，而不是后续临时拼接。

### 6.4 菜单页职责定义

#### Dashboard

用途：

- 首页总览
- 一眼判断设备健康与关键状态
- 提供少量高价值摘要，不展示全部细节

建议内容：

- Hero 顶部状态
- 核心资源指标
- Bitcoin 节点摘要
- Mining 服务摘要
- Network 摘要
- 最近更新时间和系统版本

#### Node

用途：

- 专门展示 bitcoind 的运行细节
- 与 Dashboard 解耦，避免首页承载过多节点细节

建议内容：

- 运行状态
- Sync 状态
- Full / Pruned 模式
- Headers / Blocks
- Best Block Date
- Connections
- Difficulty
- RSS
- Chain / Subversion
- 后续可加入更详细的节点诊断字段

#### Mining

用途：

- 专门展示 ckpool / stratum 相关信息

建议内容：

- 服务状态
- Solo / Pool 模式
- BTC 地址
- 最近日志摘要
- 后续可扩展连接数、最近 share 活跃信息、启动耗时等

#### Network

用途：

- 展示联网状态和时间同步状态

建议内容：

- IP
- Interface
- WiFi SSID
- MAC
- RSSI
- NTP 状态
- 后续可扩展默认网关、DNS、链路状态、网络错误计数

#### Logs

用途：

- 专门承载日志读取，不污染 Dashboard
- 避免首页加载日志内容造成无谓压力

建议内容：

- `bitcoind` 日志
- `ckpool` 日志
- `system` 关键事件日志

交互原则：

- 日志页单独请求
- 默认只读取最后 N 行
- 提供手动刷新
- 不建议首页自动轮询大日志

#### Settings

用途：

- 放置后续所有“配置型操作”
- 让 Dashboard 保持只读监控角色

建议内容：

- 网络相关设置
- 时间/NTP相关设置
- 页面语言与显示偏好
- 服务行为类开关

说明：

- 第一阶段可以只保留只读框架或占位入口
- 第二阶段再逐步增加写接口

#### About

用途：

- 展示版本、构建信息与页面自身元数据

建议内容：

- 固件版本
- 网页版本
- API 版本
- 板卡型号
- 最近启动时间
- 数据采样时间

### 6.5 菜单形态建议

推荐采用以下两种布局之一：

- 桌面端左侧窄边栏 + 右侧内容区
- 顶部轻量导航 + 移动端抽屉菜单

视觉上建议接近现代产品后台，而不是传统工业后台：

- 导航层级少
- 每个菜单只解决一类问题
- 当前菜单高亮明确
- 内容区保留大留白

### 6.6 Dashboard 与子页面的边界

必须避免 Dashboard 失控膨胀。

建议边界如下：

- Dashboard 只负责“总览”和“异常发现”
- 具体排查去子页面
- 配置写操作去 `Settings`
- 大段日志只放在 `Logs`

这样可以保证首页始终简洁，也更符合你希望的产品化风格。

## 7. 数据范围定义

### 7.1 第一阶段必须覆盖的数据

与当前网页保持功能对齐，全部来自现有页面已使用的数据。

#### system

- `cpu_model`
- `uptime`
- `temp`
- `load_pct`
- `load_display`
- `mem_pct`
- `mem_used_display`
- `mem_total_display`
- `disk_pct`
- `disk_size_info`
- `swap_pct`
- `swap_used_display`
- `swap_total_display`
- `block_disk_found`
- `block_disk_value`
- `block_disk_path`

#### bitcoin

- `running`
- `sync_key`
- `sync_class`
- `prune_key`
- `prune_class`
- `progress_pct`
- `progress_ring`
- `headers_fmt`
- `blocks_fmt`
- `best_block_date`
- `btc_context`
- `connections`
- `difficulty_display`
- `rss`

#### mining

- `ckpool_running`
- `ckpool_class`
- `ckpool_state`
- `stratum_key`
- `btc_address`
- `ckpool_tail`

#### network

- `wifi_ssid`
- `ip_addr`
- `iface`
- `mac_addr`
- `rssi_display`
- `rssi_pct`
- `signal_level`
- `ntp_class`
- `ntp_key`

### 7.2 第二阶段可扩展的数据

如果后端缓存模型建立完成，可以继续扩展：

- API 响应时间
- bitcoind RPC 最近成功时间
- ckpool 最后活跃时间
- 上次区块更新时间
- 页面数据采样时间戳
- 设备固件版本
- 内存趋势和负载趋势短历史

## 8. 接口架构设计

### 8.1 设计目标

接口层要从“单个重 CGI”改成“按频率拆分的轻 API”。

### 8.2 建议接口分组

不再把所有数据塞进一个每次现算的大 CGI。

建议拆分为以下接口：

- `GET /api/summary`
  - 顶部总览区使用
  - 返回整体状态和几个关键摘要

- `GET /api/system`
  - 返回 system 区块数据

- `GET /api/bitcoin`
  - 返回 bitcoin 区块数据

- `GET /api/mining`
  - 返回 ckpool 状态和地址、日志摘要

- `GET /api/network`
  - 返回 network 区块数据

- `GET /api/meta`
  - 返回页面版本、固件版本、采样时间、API 版本

如果采用多菜单模式，建议从一开始就按页面职责映射接口，而不是继续维护一个超大杂糅接口。

第一阶段也可以保留一个兼容接口：

- `GET /api/status`
  - 由后端从缓存聚合以上数据
  - 用于兼容旧页面或调试阶段

### 8.3 菜单与接口映射

建议菜单与接口关系如下：

- `Dashboard`
  - `GET /api/summary`
  - `GET /api/meta`
  - 可选聚合接口 `GET /api/status`

- `Node`
  - `GET /api/bitcoin`

- `Mining`
  - `GET /api/mining`

- `Network`
  - `GET /api/network`

- `Logs`
  - `GET /api/logs/bitcoind?tail=100`
  - `GET /api/logs/ckpool?tail=100`
  - `GET /api/logs/system?tail=100`

- `Settings`
  - `GET /api/settings`
  - 后续可扩展 `PATCH /api/settings/...`

- `About`
  - `GET /api/meta`

### 8.4 数据刷新频率建议

不同数据不应该同频刷新。

- `summary`：5 秒
- `system`：10 秒
- `bitcoin`：10 秒
- `mining`：10 秒
- `network`：15 秒
- `meta`：页面初始化时加载一次，或 60 秒刷新一次

这样可以显著减少无意义请求。

多菜单模式下应进一步遵守一个原则：

- 只有当前打开的菜单页才主动拉取其数据

例如：

- 用户停留在 `Dashboard` 时，不主动请求 `Logs`
- 用户未进入 `Settings` 时，不主动加载配置详情

这对嵌入式设备非常重要。

### 8.5 后端缓存策略

设备上增加一个常驻服务，内部维护缓存结构。

建议采样频率：

- system：2 到 5 秒采样一次
- bitcoin：5 到 10 秒采样一次
- mining：5 到 10 秒采样一次
- network：10 到 15 秒采样一次

HTTP 请求只读取内存缓存，不触发即时 shell 采集。

## 9. 设备侧减压策略

### 9.1 核心原则

从“请求驱动采样”改成“后台驱动采样”。

### 9.2 必须避免的做法

- 每个 HTTP 请求都调用 `bitcoin-cli`
- 每个 HTTP 请求都 `du -sm /root/block`
- 每个 HTTP 请求都运行多条 `ps | grep`
- 每个 HTTP 请求都通过 shell 管道拼 JSON

### 9.3 推荐做法

- 常驻服务启动后定时采样
- 高成本字段单独降频
- 块目录大小这类重字段使用更长间隔
- 日志摘要只截取最后 N 行并缓存
- JSON 序列化在进程内完成，不依赖 shell 拼接

### 9.4 高成本字段特殊策略

以下字段应被视为重采样字段：

- `block_disk_value`
- `best_block_date`
- `difficulty_display`
- `ckpool_tail`

建议：

- `block_disk_value` 30 到 60 秒更新一次
- `best_block_date` 跟随区块高度变化更新
- `difficulty_display` 跟随 `getblockchaininfo` 更新
- `ckpool_tail` 只缓存最近几行，不做整文件扫描

## 10. 前端技术方案

### 10.1 第一阶段推荐

前端仍然保持轻量，不使用重型 SPA 框架作为首版前提。

推荐方案：

- 静态 `index.html`
- 模块化 `app.js`
- 单独 `style.css`
- 可选小型辅助脚本

原因：

- 设备端目标是低成本高响应
- 当前页面信息量不需要 Angular/React 级别复杂度
- 首屏加载更轻
- 浏览器运行内存更可控
- 调试路径更短

### 10.2 资源交付方式

参考 AxeOS 的“构建期压缩”思路：

- 构建阶段产出静态资源
- 对 `html/js/css/svg` 做 gzip
- HTTP 返回正确的 `Content-Encoding`
- 尽量让浏览器复用静态资源缓存

### 10.3 文件建议

建议新前端目录形态如下：

```text
root/web/
  index.html
  style.css
  app.js
  assets/
    logo.svg
    icons.svg
```

如果进入第二阶段构建流程，可演进为：

```text
web-src/
  index.html
  styles/
  scripts/
  assets/

root/web/
  index.html.gz
  app.js.gz
  style.css.gz
  assets/...
```

## 11. 后端技术方案

### 11.1 推荐方向

推荐新增轻量常驻服务，例如：

- `rockbase-webd`

职责：

- 定时采样系统状态
- 定时调用 bitcoind RPC
- 缓存结果
- 提供 JSON API
- 可选直接提供静态文件

### 11.2 推荐语言

第一优先级建议：

- `C`

原因：

- 运行时占用小
- 与 Buildroot 集成直接
- 可控性高
- 更适合 K230 这类嵌入式 Linux 板卡

### 11.3 推荐依赖

目标是依赖最小化。

建议依赖：

- `cJSON`

可复用但非强制新增：

- `zlib`
- `libevent`

不建议首版引入：

- Node.js
- Python Web 框架
- Nginx + FastCGI 的额外复杂链路

## 12. 页面交互原则

### 12.1 交互目标

交互只服务于“更快理解状态”，不做花哨仪表盘。

### 12.2 建议交互

- 首屏加载骨架屏
- 指标更新时轻微淡入
- 状态徽标颜色切换
- BTC 地址一键复制
- 数据异常时卡片出现轻警示态

### 12.3 不建议交互

- 全屏图表动画
- 高频闪烁刷新
- 模拟终端大日志滚动
- 装饰性 3D 效果

## 13. 状态表达规范

统一所有状态的表达方式。

### 13.1 状态等级

- `ok`
- `warn`
- `err`
- `soft`

### 13.2 视觉对应

- `ok`：稳定绿色
- `warn`：琥珀色
- `err`：低饱和红色
- `soft`：中性灰蓝

### 13.3 文案原则

- 用短词，不用长句
- 优先表达结果，不表达内部机制
- 例如：
  - `Synced`
  - `IBD Syncing`
  - `Stopped`
  - `Not Synced`
  - `Full Node`
  - `Pruned`

## 14. 兼容与迁移策略

### 14.1 迁移顺序

推荐分四步演进：

1. 保留现有页面功能不变，先设计新 API 边界
2. 增加常驻 API 服务，并让它输出与当前字段兼容的数据
3. 新前端接入新 API，在线热部署验证
4. 验证通过后再固化到镜像并重新编译

### 14.2 调试原则

必须遵守当前项目的调试规则：

- 每个功能先 `scp/ssh` 在线热调
- 真机验证通过后，再固化到仓库源码
- 最后再编译镜像输出

这条规则对新网页和新 API 同样适用。

### 14.3 兼容接口建议

迁移期建议保留：

- 旧入口 `/`
- 兼容接口 `/api/status`

直到新前端和新服务稳定后，再移除旧 CGI。

## 15. 性能目标

### 15.1 前端目标

- 首屏静态资源尽量控制在轻量级
- 页面初始可见内容快速出现
- 正常刷新时不出现明显重排卡顿

### 15.2 后端目标

- 普通 API 请求不触发外部采样流程
- 常规请求返回延迟稳定
- 页面连续轮询时不引起明显 CPU 峰值

### 15.3 设备目标

- 降低 shell 进程创建次数
- 降低 `bitcoin-cli` 调用频率
- 降低目录扫描频率
- 降低日志读取频率

## 16. 第一阶段交付定义

第一阶段不是一次性做到所有高级能力，而是先完成下面这些：

- 一个新的 UI 视觉框架
- 与当前字段功能等价的数据展示
- 前端静态资源化
- 后端从 CGI 转向常驻缓存式 API
- 明显降低每次请求压力

第一阶段暂不要求：

- 复杂历史图表
- 多页面路由
- 登录权限系统
- WebSocket 实时流
- 重型前端框架

## 17. 结论

这次改造的关键不是“把页面画得更像某个网站”，而是同时完成两件事：

- 用更现代、更克制、更有产品感的方式重做 `NM 全节点监控` 页面
- 用更轻的 `静态前端 + 常驻 API + 后台缓存` 架构替换当前重 CGI 模式

最终目标是：

- 页面看起来更高级
- 信息更聚焦
- 设备压力更低
- 架构更可维护
- 后续扩展更容易

后续实现应以本文档为准，并继续遵守项目当前的真机热调优先流程。
