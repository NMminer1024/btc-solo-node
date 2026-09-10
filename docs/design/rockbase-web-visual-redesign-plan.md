# Rockbase Web 视觉重构方案（迁移 ck-monitor 设计语言）

> 创建：2026-09-10
> 状态：**方案设计完成，待实施**
> 范围：`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/`
> （`index.html` / `style.css` / `app.js`，共 3 个前端文件）

---

## 1. 目标与边界

### 目标

将树莓派独立节点项目（`/home/bitpony/Desktop/mining/_work/btc/ck-monitor`）的视觉体系——
**GitHub Dark (Primer) 风格的工程感深色监控台**——迁移到 K230 Rockbase 的 Web 控制台，
解决当前"藏青 + 暗金 + 大圆角 + 毛玻璃"工业风长时间观看易疲劳的问题。

### 核心主题（信息优先级重排的依据）

Rockbase 是**消费级独立挖矿节点**，Web 首屏必须突出三个核心状态：

1. **Bitcoind 核心状态**（同步进度、块高、连接数、运行时长）
2. **Stratum 服务（ckpool）状态**（运行/停止、监听地址、连接信息）
3. **局域网矿机状态**（矿机数、总算力、Best Share、worker 明细）

### 硬边界（不可触碰）

| 边界 | 说明 |
|------|------|
| ❌ 不动 API 契约 | 5 个 CGI（status/miners/miners-history/log/wifi）的请求/响应格式、轮询节奏、cursor 机制全部保持 |
| ❌ 不动交互逻辑 | 视图切换、轮询、排序、`<details>` 折叠、WiFi 表单流程、主题/语言控件行为 |
| ❌ 不动 JS 数据流 | `normalizeDashboardData()`、key→label 映射、DOM id 命名保持不变 |
| ✅ 可重排版 | DOM 结构调整、卡片合并/拆分、栅格重排（但 id 保留，否则 app.js 找不到节点） |
| ✅ 可精简页面 | 删除纯装饰元素、合并冗余信息、Settings 页硬编码假数据区块可移除或收敛 |
| ✅ 只改视觉的 JS 例外 | `app.js:882-888` 算力图序列色、`app.js:1378-1384` RSSI 信号色是硬编码 hex，属配色范畴，可改值不改逻辑 |

---

## 2. 现状问题诊断

| 问题 | 现状 | 影响 |
|------|------|------|
| 配色疲劳 | 藏青 `#1c3d63` + 暗金 `#c19549` 高饱和对比，light 主题米白底 + 双径向渐变 | 长时间盯监控容易累 |
| 圆角/阴影过重 | 圆角 28px/22px/16px、大阴影 `0 20px 42px`、`backdrop-filter: blur(14px)` | 消费级"家电感"，不像监控工具，层级靠阴影而非信息结构 |
| 信息层级平铺 | Dashboard 四张卡权重相同，Stratum 卡只有连接信息没有"服务健康"感 | 三大核心状态不突出 |
| 矿机状态藏太深 | 矿机列表在 Miners 视图，Dashboard 完全看不到矿机摘要 | 核心信息 3 需要切视图 |
| 装饰性元素偏多 | 数字翻滚动画（roll-char）、148px/88px conic 圆环、渐变品牌块 | 视觉噪音，且 roll-char 与 `--pct` 圆环是 JS 驱动的，重构时需保留钩子 |
| 样式瑕疵 | `--shadow-soft` 未定义（style.css:1081 引用，tooltip 实际无阴影） | 顺手修复 |

---

## 3. 新设计语言（照搬 ck-monitor，精确取值）

### 3.1 配色板（CSS 变量全量替换）

新 `:root` 变量块——**直接采用 GitHub Dark 色板**，语义映射如下：

```css
:root {
  /* ── 背景三层（层级全靠背景分层 + 1px 边框，不用阴影）── */
  --bg:        #0d1117;   /* 页面底色 */
  --bg2:       #161b22;   /* 卡片 / 表格行 / header 底 */
  --bg3:       #21262d;   /* 表头 / 输入框 / hover / 进度条轨道 */
  --border:    #30363d;   /* 所有 1px 边框与分隔线 */

  /* ── 文字两级 ── */
  --text:      #c9d1d9;   /* 主文字 */
  --muted:     #8b949e;   /* 次要文字 / label / 表头 */

  /* ── 强调与状态 ── */
  --accent:    #58a6ff;   /* 链接 / 强调数字 / 激活态 */
  --green:     #3fb950;   /* 在线 / 正常 / 挖到块 */
  --yellow:    #d29922;   /* 警告 / best share / 概率 */
  --red:       #f85149;   /* 错误 / 离线 / 高温 */

  /* ── 突出用亮色（区块高度等对，刻意偏离主色板）── */
  --bright-green: #4ade80;
  --bright-amber: #fbbf24;

  --radius:    8px;       /* 全局卡片基准圆角 */
  --font:      'Segoe UI', system-ui, -apple-system, sans-serif;
  --mono:      'SFMono-Regular', Consolas, 'Liberation Mono', Menlo, monospace;
}
```

**双主题策略**：ck-monitor 是纯暗色站。Rockbase 现有主题切换控件是真实现（`data-theme` + localStorage），
交互不能删——处理方案：
- **dark 主题 = 上表**（新默认，也是设计主战场）
- **light 主题 = 同一色板的 GitHub Light 对应值**（`#ffffff / #f6f8fa / #eaeef2 / #d0d7de / #24292f / #57606a / #0969da / #1a7f37 / #9a6700 / #cf222e`），
  保证切换控件仍然有意义，但设计验收以 dark 为准

**着色徽章配方**（状态 pill 通用模式，全站统一）：

```
文字 = 实色状态色
背景 = rgba(状态色, 0.12)
边框 = rgba(状态色, 0.35)
```

即 online=`rgba(63,185,80,0.12)` 底，warn=`rgba(210,153,34,0.12)` 底，error=`rgba(248,81,73,0.12)` 底。

### 3.2 字体体系

| 项 | 值 |
|----|-----|
| 主字体 | `'Segoe UI', system-ui, -apple-system, sans-serif`（保留 `Noto Sans SC` 兜底中文，放在 system-ui 之后） |
| 等宽 | 地址 / worker 名 / 日志：`SFMono-Regular, Consolas, Menlo, monospace` |
| 基础 | 14px / 行高 1.6 |
| 字重 | 只用 400 / 600 / 700 三档 |
| 数字 | `font-variant-numeric: tabular-nums`（块高等对齐） |

字号阶梯（桌面 / 移动 ≤600px）：

| 角色 | 桌面 | 移动 | 字重 | 其他 |
|------|------|------|------|------|
| 品牌 logo | 18px | 17px | 700 | |
| section 标题 | 17px | 15px | 600 | 底部 1px 通栏细线 |
| stat 大数字 | 22px | 18px | 700 | 可用状态色着色 |
| hero 数字（块高/算力） | 26px | 20px | 700 | |
| 表格正文 | 13px | 12px | 400 | |
| 表头 / stat-label | 11px | 10px | 600 | **全大写 + letter-spacing 0.07~0.08em**，muted |
| badge/pill | 12px | 11px | 400~600 | |
| footer | 12px | — | 400 | |

### 3.3 组件改造映射（旧 → 新）

| 现有组件 | 改为 |
|----------|------|
| 大圆角毛玻璃面板（`--radius-xl 28px` + blur + 大阴影） | 8px 圆角、`--bg2` 实底、1px `--border`、**无阴影** |
| conic-gradient 大圆环（IBD 148px / metric orb 88px） | 保留圆环骨架但改细线风格：轨道 `--bg3`、进度弧 `--accent`/`--green`，弱化尺寸（IBD 环 ~96px，metric 环可降级为 6px 水平进度条） |
| pill 徽章四态（status-ok/soft/warm/danger） | 统一"着色徽章配方"（§3.1），前缀 `●` 圆点 |
| 渐变品牌方块 "NM" | 扁平 `--accent` 色块或直接文字 logo |
| stat 卡 hover 浮起 | hover 仅 `border-color: var(--accent)`，`transition: .2s` |
| 进度条 | 轨道 6px 高 `--bg3` 底、3px 圆角，填充随阈值变色：`transition: width .5s ease` |
| 左侧栏 260px 固定导航 | 保留结构（改交互不允许），视觉改为 `--bg` 底 + 右侧 1px 边框；nav-item 激活态 = `--bg3` 底 + 左侧 3px accent 竖条（替代 pill 填充） |
| 表格 | `.table-wrapper` 圆角边框容器、表头 `--bg3` 大写小字、行 hover `--bg3`、行间 1px 分隔、离线行 `opacity:.55` |
| 日志视图 | 终端式：`--bg` 底、等宽 12px、按关键字着色（error 红 / warn 黄 / info 蓝） |
| 主题/语言控件 | ghost 按钮风格：透明底 + 1px `--border` + 6px 圆角 + muted 文字，hover 提亮 |

### 3.4 需要同步改的 JS 硬编码颜色（仅改值，不改逻辑）

| 位置 | 内容 | 新值 |
|------|------|------|
| `app.js:882-888` | 算力历史 7 条序列色 | 参照 ck-monitor 映射：5m=`#58a6ff`、15m=`#3fb950`、1hr=`#2dd4bf`、6hr=`#fbbf24`、1d=`#d29922`、7d=`#f85149`、1m=`rgba(139,148,158,0.6)` |
| `app.js:1378-1384` | RSSI 信号条 5 档色 | `#3fb950 / #3fb950 / #d29922 / #f85149 / #555`（去绿屏风） |
| `style.css:1081` | `--shadow-soft` 未定义 | 直接改为 `0 4px 12px rgba(0,0,0,0.4)` 内联值（ck-monitor 纪律：几乎不用阴影） |

---

## 4. 信息架构重排（重排版，不改数据流）

### 4.1 新 Dashboard 首屏（核心改动）

**设计原则：一屏回答三个问题——"节点正常吗？矿池开着吗？矿机在挖吗？"**

```
┌─────────────────────────────────────────────────────────┐
│ ① 状态总览条（新增，三格横排，一眼三色）                     │
│  [● Bitcoind  Synced] [● Stratum  Running] [● Miners  3 online] │
├─────────────────────────────────────────────────────────┤
│ ② KPI 指标墙（stat-card 网格，auto-fit minmax(145px,1fr)）  │
│  [Block Height] [Connections] [Difficulty] [Uptime]       │
│  [Pool Hashrate 5m] [Best Share] [Workers] [Temperature]  │
├──────────────────────────┬──────────────────────────────┤
│ ③ Bitcoind 卡             │ ④ Stratum 卡                  │
│  同步徽章 + IBD 进度条      │  Running 徽章                 │
│  current/total 块高        │  endpoint stratum+tcp://…     │
│  (IBD 中时此处是主角)      │  user/pass 提示               │
├──────────────────────────┴──────────────────────────────┤
│ ⑤ System 卡（CPU/内存/磁盘/温度 四个水平进度条 + 数值行）      │
├─────────────────────────────────────────────────────────┤
│ ⑥ Network 卡（IP/SSID/RSSI/NTP，收敛为紧凑列表）            │
└─────────────────────────────────────────────────────────┘
```

要点：
- **① 是三核心主题的载体**：三个 status-badge 对应 bitcoind / ckpool / miners，全部复用现有
  `status.cgi` 和 `miners.cgi` 已返回的字段（sync badge、ckpool_state、miners count），
  只是新增 DOM 容器在 Dashboard 顶部渲染——JS 侧只加"填三个徽章"的代码，不改任何请求。
- 矿机摘要上提到 Dashboard（Workers / Pool Hashrate / Best Share），明细仍留在 Miners 视图。
- IBD 大圆环降级为**水平进度条 + 百分比数字**（ck-monitor 风格）；同步完成后该区域让位给块高 hero 数字。
- System 四个 88px orb 降级为紧凑进度条行，温度沿用阈值着色（<50 绿 / 50-70 黄 / ≥70 红）。

### 4.2 Miners 视图

- Node Summary 四 KPI 改 stat-card 指标墙（与 Dashboard 同组件）
- 算力历史图：换新序列色（§3.4），网格线 `rgba(48,54,61,0.5)`、刻度 `--muted`
- 矿工 `<details>` 卡片 → ck-monitor 表格风格；worker 表格表头全大写小字，排序箭头 `▲/▼` + accent 着色
- 保留 `<details>` 折叠状态跨刷新保持的逻辑

### 4.3 Logs 视图

- 终端式深色输出区 + 关键字着色
- bitcoind / stratum tab 改"文件夹页签"风格（激活态 `--accent` 文字 + 底部连通线）
- Follow / 刷新控件改 ghost 按钮

### 4.4 Settings 视图（精简重点）

- **删除 Bitcoind/Stratum 硬编码假数据区**（"902,120"、"126.7T" 等静态 HTML）——这些是占位噪音，
  与"主题明确"直接冲突；区块移除或替换为真实只读摘要（复用 status.cgi 数据）
- WiFi 表单保留全部流程，输入框改 `--bg3` 底 + focus accent 边框 + `0 0 0 3px rgba(88,166,255,.15)` 外发光环

### 4.5 About 视图

- 纯样式适配，无结构改动

### 4.6 动效纪律

| 元素 | 决策 |
|------|------|
| roll-char 数字翻滚 | **保留**（JS 钩子已存在，删它要动逻辑；视觉上通过降字号/换色弱化） |
| conic 圆环 `--pct` | 同上，保留驱动方式，只改视觉呈现 |
| 新增过渡 | 统一 `0.15s~0.2s` 颜色/边框过渡；进度条 `0.5s ease` |
| `prefers-reduced-motion` | 现有处理保留 |

---

## 5. 实施步骤

> 遵循项目核心规则：**先热部署到真实板子验证，再写回源码**（skill: `rockbase-hotfix`）。
> 所有改动文件必须逐项追踪勾销。

1. **第一步：CSS 变量替换**（风险最低，收益最大）
   - 改 `style.css` 的 `:root` 和 `[data-theme="dark"]` 两个变量块为 §3.1 新色板
   - 同步改 `app.js` 两处硬编码颜色 + 修 `--shadow-soft`
   - bump `index.html` 里 `style.css?v=` / `app.js?v=` 缓存号
   - 热部署到 `192.168.124.15`（1G 板）验证
2. **第二步：组件样式重写**
   - 圆角/阴影/边框体系（卡片、徽章、按钮、表格、进度条、tab、导航）
   - 圆环视觉降级（保留 `--pct` 钩子）
   - 逐视图热验证
3. **第三步：Dashboard 信息架构重排**
   - 新增顶部三状态总览条（新 DOM 容器 + JS 填充，复用现有 fetch 结果）
   - KPI 指标墙、卡片次序重排
   - ⚠️ 所有被移动的元素必须保留原 `id`，只动父容器
4. **第四步：Settings 精简**
   - 移除硬编码假数据区块（需用户确认，见 §6 开放问题）
5. **第五步：固化写回源码**
   - 逐项勾销热部署改动清单，写回 `buildroot-overlay/.../root/web/`
   - 按"打进镜像"规则：只固化，不编译（除非用户明确要求）

---

## 6. 开放问题（实施前确认）

| # | 问题 | 建议 |
|---|------|------|
| 1 | Settings 页 Bitcoind/Stratum 硬编码假数据区块，直接删还是换成真实只读数据？ | 建议换成 status.cgi 真实数据摘要（无新 API），若嫌麻烦则直接删 |
| 2 | light 主题是否保留？ | 建议保留（控件已存在），但只做到"能用"，设计验收以 dark 为准 |
| 3 | 语言下拉（死控件）如何处理？ | 本次保持不动（删控件 = 改交互）；样式上正常适配 |
| 4 | Dashboard 顶部状态总览条算"重排版"还是"改交互"？ | 判定为排版（只读渲染，无新请求、无新交互），按本方案执行 |

---

## 7. 验收清单

- [ ] dark 主题下对照 ck-monitor（`http://<pi5>:5000`）目测色感一致
- [ ] 三大核心状态在 Dashboard 首屏无需滚动即可确认
- [ ] IBD 进行中 / 已同步 两种状态下 Dashboard 都成立
- [ ] 主题切换、WiFi 表单、日志 Follow、矿工折叠、表格排序全部功能回归通过
- [ ] 1180px / 980px / 720px 三档断点无横向溢出
- [ ] 1G 板（内存紧）页面流畅度无回退（阴影/blur 移除后应更快）
- [ ] `grep -c 'getElementById' app.js` 涉及的所有 id 在新 DOM 中仍存在
