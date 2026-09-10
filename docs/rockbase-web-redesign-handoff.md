# Rockbase Web 视觉重构 Handoff

> 写给接手 Web 视觉改造的 agent/人。读完这份 + `docs/design/rockbase-web-visual-redesign-plan.md` 即可无缝继续。
> 更新日期：2026-09-10　当前分支：`web`（4 个 commit 已 ff 合并进 `dev` 并推送 origin）

---

## 0. 一句话现状

**GitHub Dark 视觉迁移已完成并发布两版 OTA（`v0.0.06` 首版 + `v0.0.07` 修复迭代），
等用户在 124.23 真机 OTA 升级后反馈 `v0.0.07` 的验收结果。** 若验收通过，本任务收尾；
若有新视觉问题，按第 4 节的迭代流程继续改。

---

## 1. 任务目标与硬边界

把树莓派 ck-monitor 项目（`/home/bitpony/Desktop/mining/_work/btc/ck-monitor`）的
GitHub Dark 工程监控台视觉迁移到 K230 Web 控制台。核心主题三大状态：**Bitcoind /
Stratum(ckpool) / 局域网矿机**。

**硬边界（用户拍板，继续迭代时不得推翻）：**

- ❌ 不动 API 契约（5 个 CGI：status/miners/miners-history/log/wifi）
- ❌ 不动交互逻辑（视图切换、轮询、排序、折叠、WiFi 表单、主题切换）
- ❌ 不动 `app.js` 数据流；**所有被 JS 引用的 DOM id 必须保留**（77 个，改完用 grep 自检）
- ✅ 可重排版、精简页面；JS 只允许改硬编码颜色值 + 追加纯渲染代码

设计取值单一事实来源：`docs/design/rockbase-web-visual-redesign-plan.md` §3。

---

## 2. 已完成工作（commit 级）

| commit | 内容 | 对应 OTA |
|---|---|---|
| `73f784d` | feat(web): GitHub Dark 全量迁移（色板/组件/Dashboard 重排/hero 三状态条/KPI 墙/删 Settings 假数据） | `v0.0.06-rockbase-k230-20260910-cdf3d9d-ota` |
| `cdf3d9d` | VERSION → 0.0.06 | 同上 |
| `44b6a09` | fix(web): 环不可见修复 + Peers 改名 + 块高卡片完整显示 + Difficulty 3 位小数 + Miners 摘要恢复 | `v0.0.07-rockbase-k230-20260910-52c5b29-ota` |
| `52c5b29` | VERSION → 0.0.07 | 同上 |

两版都已走完 OTA 发布链路（本地验签 + sha256 核对 + GitHub 4 资产 200 +
`ota.nmminer.com` 入口指向最新），release 仓库 `NMminer1024/btc-solo-node-release`
对应 tag 已存在。

---

## 3. 关键技术事实（不要重复踩坑）

### 3.1 环不可见的根因（v0.0.07 已修）

`radial-gradient(circle at center, <color> P%, transparent P+1%)` 打孔 + conic 环的方案里，
**百分比按"最远角"射线计算**——方形元素内切圆半径 ≈ 70.7%。打孔 ≥72% 会把整条 conic
色带遮掉（环完全不可见）；旧版 56%~58% 才露出 ~6px。v0.0.07 已改为
**conic-gradient 环 + `::before` 内圆盘**（`inset: 3/4/5px` 精确控制环宽），与几何无关。
再调环样式时不要再回到 radial 打孔方案。

### 3.2 数据流事实

- `difficulty_display` 由服务端 `usr/bin/rockbase_statusd.sh` 的 `compact_metric()` 格式化，
  该函数**全仓只服务 difficulty**，已改为固定 `%.3f`（`127.440T`）。
- Dashboard 矿机三 KPI（`minerHash5m`/`minerBestShare`/`minerWorkers`）数据来自
  `miners.cgi`，**仅 Miners 视图激活时轮询**——未访问前 Dashboard 显示 `--`，
  访问后由缓存填充（这是设计使然，不是 bug）。Miners 视图的摘要镜像是
  `summaryWorkers`/`summaryHash5m`/`summaryBestShare`（renderMiners 里同步 setText）。
- hero 三状态条：`heroBitcoindBadge`/`heroStratumBadge` 复用 status.cgi 的
  `syncBadge/syncClass`、`badge/badgeClass`；`heroMinersBadge` 在 renderMiners 里更新。
- 块高 `ibdBlocksCurrent/Divider/Total`：KPI 卡大字只放 current，total 在 `.stat-sub`
  副行（"of NNN,NNN"）；IBD 阶段时 JS 会隐藏 divider、清空 total（既有逻辑，未改）。

### 3.3 构建/发布链路事实

- **Overlay 同步陷阱**：web 文件是内容修改，`make buildroot` 前必须先 `make sync`。
- 本机无 `/mnt/d`，`post-image.sh` 的 Windows 导出步骤必报
  "failed to create image export directory" 并使 make 以 Error 收尾——**无害**，
  只要 `output/k230_rockbase_defconfig/images/rootfs.ext2` 的 mtime 是本次的即可用于 OTA 打包。
- 镜像内容抽查用 debugfs：
  `debugfs -R 'cat /root/web/style.css' output/k230_rockbase_defconfig/images/rootfs.ext2`。
- 签名密钥核对：`md5sum ~/.rockbase-ota-keys/ota-pub.pem` 必须等于
  `rootfs_overlay/etc/rockbase-ota-pub.pem`（当前均为 `81b7bf9a...`）。
- 版本串格式：`vX.X.XX-rockbase-k230-YYYYMMDD-<shortsha>-ota`，日期/sha 从镜像
  `/etc/version/release_version` 提取；VERSION 文件是唯一真源（PATCH 两位）。
- 发布仓库操作在 `/home/bitpony/Desktop/mining/_work/btc-solo-node-release`，
  提交身份 `NMminer1024 <nmminer1024@users.noreply.github.com>`，SSH host 用
  `github.com-NMminer1024`。
- 完整发布流程见 `.github/skills/rockbase-ota-release/SKILL.md`，
  设备侧升级流程见 `.github/skills/rockbase-ota/SKILL.md`。

### 3.4 已知保留项（不是 bug，勿"顺手修"）

- light 主题只做到"能用"（GitHub Light 对应值），设计验收以 dark 为准。
- 语言下拉是**死控件**（无 JS 绑定），保留原样——删它算改交互。
- roll-char 数字翻滚、`--pct` 圆环钩子是 JS 驱动的既有交互，保留。
- `miners.cgi` 的 `bestshare` 经 `formatShare()` 显示；单位语义 = 历史最佳难度。

---

## 4. 继续迭代的流程（用户反馈新问题时）

1. 改源码（`rootfs_overlay/root/web/` 三个文件 ± `rockbase_statusd.sh`），
   自检：id 全保留、`node --check app.js`、`bash -n rockbase_statusd.sh`、CSS 花括号平衡。
2. bump `index.html` 里的 `?v=` 缓存号（当前 `v=20260910-fix1`）。
3. 英文 commit + VERSION PATCH +1 单独提交（git-commit-rules / rockbase-version）。
4. `make sync` → `make CONF=k230_rockbase_defconfig buildroot -j$(nproc)
   BLOCK_SEED_DIR=/tmp/rockbase-empty-seed K230_BLOCKDATA_SIZE=256M K230_IMAGE_GZIP=0`
   （增量即可；导出报错忽略，见 3.3）。
5. debugfs 抽查镜像含新文件 → `mk-ota-package.sh` 打包签名 → 本地验签 →
   release 仓库 `release-packages/$VERSION/` + tag 推送 → curl 4 资产 + 
   `ota.nmminer.com/rockbase/manifest.json` 闭环。
6. 通知用户 OTA 升级验证。

> 真机快速迭代也可以走热部署（scp 到板子 `/root/web/` 直接刷页面看效果，
> 验证完再固化源码），但 OTA 链路已被用户接受为主路径，两版都是这样交付的。

---

## 5. 验收清单（用户确认 v0.0.07 时对照）

- [ ] System 四环（CPU/Block Data/RAM/Swap）、IBD 环、刷新倒计时环全部可见
- [ ] Block Height 卡两行完整显示（current 大字 + "of total" 副行）
- [ ] Peers 文案生效；Difficulty 显示 3 位小数（如 `127.440T`）
- [ ] Miners 视图 Node Summary 四格：Miners / Workers / Total Hashrate 5m / Best Share Ever
- [ ] 主题切换、WiFi 表单、日志 Follow、矿工折叠、表格排序功能回归通过
- [ ] 1180/980/720 三档断点无横向溢出

## 6. 验收通过后的收尾项

- [ ] 把 `docs/design/rockbase-web-visual-redesign-plan.md` 状态更新为"已完成"
- [ ] 删除/归档本 handoff 或并入 `docs/rockbase-current-handoff.md`
- [ ] `web` 分支已 ff 合并进 `dev`；后续直接在 `dev` 上开发
