---
name: rockbase-version
description: 'Rockbase 产品可读版本号（VERSION 文件 / vX.Y.Z）的定义与升版规则。Use when: 版本号, VERSION文件, product_version, 升版本, bump version, vX.Y.Z, 语义化版本, semver, 版本迭代, release_version, 版本命名.'
---

# Rockbase 产品可读版本号

面向人类可读的产品版本号，和 SDK 编译时间戳（`sdk:` 那行）是两回事，互不覆盖。

## 唯一真源

仓库根目录的 `VERSION` 文件，纯文本一行，**不带 `v` 前缀**，格式 `MAJOR.MINOR.PATCH`：

```
0.0.10
```

- 展示 / 打 tag 时统一在前面加 `v`，例如 `v0.0.10`。
- 从 `v0.0.10` 开始正式纳入管理；再往前的 `0.0.1`–`0.0.9` 是没有规范化之前的内部试跑，
  不需要、也不要回填。
- 与仓库里 `v0.1.0`、`v1.3` 这两个历史 tag 无关——那是上游 Canaan K230 SDK 遗留的 tag，
  不是 Rockbase 产品版本，不要混用、不要在这两个基础上继续递增。

## 版本号含义（参照 Semantic Versioning 2.0.0，semver.org，按固件场景收窄）

`MAJOR.MINOR.PATCH`：

- **MAJOR**：不兼容变更。出现以下任一情况才加：
  - OTA 格式/签名方案变更，旧版本设备无法直接升级到这次的包
  - 分区布局变更，需要重新烧录而不是 OTA
  - Web/status API 破坏性变更导致旧前端读不出关键字段
  - 里程碑级别的正式产品发布（如从内测转正式发布）
  - 加了 MAJOR 之后，MINOR 和 PATCH 都归零。
- **MINOR**：新增向后兼容的功能。例如新页面、新监控指标、新的 OTA 子命令、
  新的 web 接口字段（旧字段不删不改）。加了 MINOR 之后 PATCH 归零。
- **PATCH**：向后兼容的修复/小改动。文案修正、UI 卡片调整、bug fix、性能调优、
  依赖版本号小改，且不引入新功能、不破坏兼容性。

不使用 semver 的预发布/构建元数据后缀（如 `-rc1`、`+build.5`）：保持
`MAJOR.MINOR.PATCH` 三段纯数字，方便 shell（`rockbase-ota.sh` 等 busybox 环境）
按数值而不是字符串比较版本大小。

## 版本比较规则（写 OTA 检查逻辑时必须遵守）

禁止直接按字符串比较版本号（`"0.0.9" > "0.0.10"` 这种字符串比较会得到错误结果）。
必须拆成三段分别按数值比较：

```sh
# 伪代码：is_newer(a, b) 表示 a 是否比 b 新
IFS=. read -r a_maj a_min a_pat <<EOF
$a
EOF
IFS=. read -r b_maj b_min b_pat <<EOF
$b
EOF
[ "$a_maj" -gt "$b_maj" ] && return true
[ "$a_maj" -lt "$b_maj" ] && return false
[ "$a_min" -gt "$b_min" ] && return true
[ "$a_min" -lt "$b_min" ] && return false
[ "$a_pat" -gt "$b_pat" ]
```

## 版本号在系统里的流转

```mermaid
flowchart LR
    V["仓库根 VERSION\n(0.0.10)"] --> PB["post-build.sh gen_version()"]
    PB --> RV["/etc/version/release_version\n(product:v0.0.10 一行)"]
    PB --> PV["/etc/version/product_version\n(纯文本 v0.0.10)"]
    V --> OTA["rockbase-ota-release skill\nPRODUCT_VERSION 默认值"]
    OTA --> TAG["发布 tag / manifest.json version\nvX.Y.Z-rockbase-k230-YYYYMMDD-sha-ota"]
    PV --> LCD["LCD UI / web 未来展示\n及 OTA check 版本比较"]
```

- **编译期**：`buildroot-overlay/board/canaan/k230_rockbase/post-build.sh` 的
  `gen_version()` 读取仓库根 `VERSION`，写入两处：
  - `/etc/version/release_version` 里追加一行 `product:v<VERSION>`（跟 `sdk:` 那行放一起，方便一次性核对）
  - `/etc/version/product_version`：只有一行 `v<VERSION>`，专门给需要"干净版本号"的读取方（LCD、web、OTA 脚本）用，不用再解析多行 banner。
- **发布期**：`rockbase-ota-release` skill 生成 `VERSION`（发布用的环境变量，和本文件同名但含义是"这次发布用的版本串"）时，默认从仓库根 `VERSION` 文件读取 `PRODUCT_VERSION`，不再用占位符 `vx.x.xx`。
- **设备侧未来使用**（LCD Check Update 按钮等）：读 `/etc/version/product_version` 作为"当前安装版本"，与 manifest.json 里的 `version` 字段比较，按上面的数值比较规则判断是否有更新。

## 升版流程

1. 确认这次改动量级属于 MAJOR / MINOR / PATCH（按上面的定义判断，拿不准就走 PATCH，
   不要为了"看起来正式"就跳级）。
2. 编辑仓库根 `VERSION` 文件，写入新版本号（纯数字，不带 `v`）。
3. 单独提交，commit message 里说明升版原因，例如：
   `chore(version): bump to 0.0.11 (fix system page load-avg card removal)`。
4. 版本号只能递增，不能回退；同一个版本号已经发布过 OTA release 后不能复用
   （见 `rockbase-ota-release` skill 里"同一 vX.X.XX+board+date+sha 只能发布一次"的规则）。
5. 需要发布 OTA 时，先确认 `VERSION` 已经按第 1-3 步更新完，再走
   `rockbase-build` → `rockbase-ota-release` → `rockbase-ota` 三步链路。

## 常见错误

- 不要把仓库根 `VERSION` 文件和 `/etc/version/release_version` 里的 `sdk:` 那行
  混为一谈——后者是编译时间戳+commit，每次编译都变，不代表产品版本号。
- 不要用字符串比较版本号（`0.0.9` vs `0.0.10`，字符串比较会错误地认为 `0.0.9` 更大）。
- 不要跳过 `VERSION` 文件直接在发布脚本里手写版本号——`rockbase-ota-release`
  的默认值必须来自这个文件，手写等于绕过唯一真源。
