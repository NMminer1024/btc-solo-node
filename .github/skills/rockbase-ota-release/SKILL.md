---
name: rockbase-ota-release
description: 用于把现成的 Rockbase OTA 包发布到公开 GitHub Releases 仓库，校验 manifest / 签名 / 校验和，或在不重新编译镜像的前提下更新版本化 OTA 发布。也包含发布仓库 README 双语规则（英文被 git 跟踪发布、中文本地参考不跟踪、中英同步修改）、release 变更说明（RELEASE_NOTES.md）规则、GitHub API token 用法（删/改已发布 release）。Use when: OTA 发布, release, README, 双语, 中英文, 产品说明, 发布仓库文档, api token, github token, 删release, 重发, 覆盖发布, release body, 变更说明, release notes.
---

# Rockbase OTA 发布

把现成的 Rockbase OTA 产物发布到 GitHub Releases。

## 何时使用

- 用户要发布一个 Rockbase OTA 包。
- 镜像已经存在，不应该重新编译。
- 需要把 `manifest.json`、`slot-<ver>.img.gz`、`slot-<ver>.img.gz.sig` 发布到 release 仓库。

## 流程

1. 先确认发布输入：
   - `VERSION`
   - `RELEASE_REPO`，默认 `NMminer1024/btc-solo-node-release`
   - `RELEASE_WORKDIR`，默认 `/home/bitpony/Desktop/mining/_work/btc-solo-node-release`
     （发布仓库的本地固定 checkout 目录；不要把它放在 `/tmp` 作为长期位置）
   - `PKG_DIR`，默认 `~/ota-dist/$VERSION`
   - `PRIVATE_KEY`，默认 `~/.rockbase-ota-keys/ota-private.pem`
   - `SLOT_IMG`，只在需要重打包时才用；**默认取最近一次编译产物**
     `output/k230_rockbase_defconfig/images/rootfs.ext2`（见 `rockbase-build` skill）。
     用之前先确认它是最新编译结果（检查 mtime、`/etc/version/release_version`，
     或用 `debugfs -R 'cat ...'` 抽关键文件核对），不要拿旧编译产物打包发布。
2. **版本命名规则（必须统一）**：
   - 格式：`vX.X.XX-<board-model>-YYYYMMDD-<shortsha>-ota`
   - 当前 Rockbase K230 板固定使用：`vX.X.XX-rockbase-k230-YYYYMMDD-<shortsha>-ota`
   - 示例：`v0.0.01-rockbase-k230-20260905-abc1234-ota`
   - `vX.X.XX`：产品发布版本号，唯一真源是仓库根目录的 `VERSION` 文件（纯数字
     `MAJOR.MINOR.PATCH`，不带 `v` 前缀，**PATCH 始终两位**，例如 `0.0.01`）；
     发布时读它加上 `v` 前缀，不要再用占位值。版本号含义、何时该加哪一位、
     如何升级见 skill `rockbase-version`。
   - `board-model`：当前固定为 `rockbase-k230`；以后增加其他板型时替换为对应板型 slug。
   - `YYYYMMDD`：优先取待发布 `SLOT_IMG` 的 `/etc/version/release_version` 里的构建日期，不用当前日期猜。
   - `shortsha`：优先取待发布 `SLOT_IMG` 的 `/etc/version/release_version` 里的源码短 hash；取不到再用源码仓库 `git rev-parse --short HEAD`。
   - 版本名同时用于 tag、`release-packages/$VERSION/`、`slot-$VERSION.img.gz` 和 manifest 里的 `version`，四处必须完全一致。
   - 去掉时间戳后，同一 `vX.X.XX + board-model + YYYYMMDD + shortsha` 只能发布一次；如果远端已有同名 tag，停止并让用户提升产品版本号或基于新 commit 重新构建，不要覆盖已有 release。

   生成 `VERSION` 的推荐片段：
   ```bash
   SLOT_IMG=output/k230_rockbase_defconfig/images/rootfs.ext2
   PRODUCT_VERSION="${PRODUCT_VERSION:-v$(cat VERSION | tr -d ' \t\r\n')}"
   BOARD_MODEL="${BOARD_MODEL:-rockbase-k230}"
   # 发布前如果这次要升版本号，先编辑仓库根 VERSION 文件再来这里生成 PRODUCT_VERSION
   # （具体升哪一位见 skill rockbase-version），不要直接手写覆盖。
   image_version=$(debugfs -R 'cat /etc/version/release_version' "$SLOT_IMG" 2>/dev/null \
     | sed -n 's/^sdk://p' | head -1)
   build_date=$(printf '%s\n' "$image_version" | grep -oE '[0-9]{8}-[0-9]{6}' | head -1 | cut -d- -f1)
   shortsha=$(printf '%s\n' "$image_version" | grep -oE '[0-9a-f]{7,12}' | tail -1)
   [ -n "$build_date" ] || build_date=$(date +%Y%m%d)
   [ -n "$shortsha" ] || shortsha=$(git rev-parse --short HEAD)
   VERSION="${PRODUCT_VERSION}-${BOARD_MODEL}-${build_date}-${shortsha}-ota"
   ```
3. **发布前必须核对签名密钥一致性**（这是本流程最容易踩的坑，2026-09-03 真机测试时
   实际踩过一次）：
   ```bash
   md5sum ~/.rockbase-ota-keys/ota-pub.pem \
     buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/etc/rockbase-ota-pub.pem
   ```
   两个 md5 必须一致。如果不一致，说明 `PRIVATE_KEY` 和源码里固化、随镜像烧录到设备的
   公钥不是一对——用这把私钥签出来的包，**任何还没重新烧录过新镜像的设备验签都会失败**。
   - 如果源码公钥是旧的：先把 `~/.rockbase-ota-keys/ota-pub.pem` 同步进源码 overlay 文件并提交，
     再继续发布；同时提醒用户已部署但未重新烧录的设备需要手动同步新公钥到
     `/etc/rockbase-ota-pub.pem`（临时补丁）才能吃到这次发布的包，直到镜像重新编译烧录。
   - 不要跳过这一步直接签名发布，否则问题只会在真机 apply 验签阶段才暴露。
   - 背景与细节见 `docs/rockbase-ota-handoff.md` 第 13.4 节、repo memory `ota-signing-key.md`。
4. 先确认对 `RELEASE_REPO` 有写权限；用 `git push --dry-run origin main` 或等价操作验证。
   如果 `Permission denied`，立刻停止并让用户补写权限，不要继续试发布。
   发布仓库提交必须使用 `NMminer1024` 身份，不要走默认 `github.com` 的 `betterpony` 身份。
   推送时应显式使用 `github-nmminer1024` 这个 SSH host（发布仓库 remote 为 `origin`，
   即 `git@github-nmminer1024:NMminer1024/btc-solo-node-release.git`；注意不是
   `github.com-NMminer1024`，2026-09-24 实测该写法 push 会报 "does not appear to be a git repository"）。
   所有发布仓库操作都在 `RELEASE_WORKDIR` 内进行；如果目录不存在，先 clone 到这里再继续。
5. 优先复用 `~/ota-dist/<version>/` 下已经生成好的包。
6. 发布前先本地验签：
   - `openssl dgst -sha256 -verify <(openssl pkey -in "$PRIVATE_KEY" -pubout) -signature "$PKG_DIR/slot-$VERSION.img.gz.sig" "$PKG_DIR/slot-$VERSION.img.gz"`
   - `sha256sum` 必须和 `manifest.json` 里的值一致。
   - **镜像内二进制含新代码校验（2026-09-28 实锤的坑，必做）**：如果本次发布包含
     local 包（如 `rockbase_lcdui`）的改动，必须从 `SLOT_IMG` 抽出对应二进制，
     用 `strings` 验证含本次改动的关键标记，防止把旧二进制打进 OTA：
     ```bash
     /sbin/debugfs -R 'cat /usr/bin/rockbase-lcdui' "$SLOT_IMG" 2>/dev/null > /tmp/verify-lcdui.bin
     # 旧代码标记应已消失（示例：删掉了 GATEWAY/DEVICE/DNS 列头）
     strings /tmp/verify-lcdui.bin | grep -cE '^(GATEWAY|DEVICE|DNS)$'   # 期望 0
     # 新代码标记应存在（换成本次改动的独特字符串）
     strings /tmp/verify-lcdui.bin | grep -c '<新代码独特字符串>'          # 期望 >0
     ```
     校验不过就停止发布，回 `rockbase-build` skill 的 **local 包漏编陷阱** 章节
     强制重编后再打包。注意：镜像 `release_version` 版本号正确**不代表**二进制是新的
     （VERSION 文件走 genimage 路径，与包编译无关），必须 strings 验证二进制本身。
7. 如果包不存在、已过期，或 `manifest` 指向了错误仓库，就重新生成：
   - `tools/mk-ota-package.sh --version "$VERSION" --key "$PRIVATE_KEY" --slot-img "$SLOT_IMG" --base-url "https://github.com/$RELEASE_REPO/releases/download/$VERSION" --min-version v0.0.01 --notes-file "$RELEASE_WORKDIR/release-packages/$VERSION/RELEASE_NOTES.md" --out-dir "$PKG_DIR"`
   - `--notes-file` 会将 RELEASE_NOTES.md 内容嵌入 `manifest.json` 的 `notes` 字段，
     设备 Web 端 OTA 弹窗会自动渲染显示（简易 markdown：`###` 标题 + `-` 列表）。
   - 如果 RELEASE_NOTES.md 尚未写好，先写再打包（见下一步）。
8. **写发布说明 `RELEASE_NOTES.md`**（必须，见下方「Release 说明规则」）：
   在 `release-packages/$VERSION/RELEASE_NOTES.md` 里总结**从上一个 OTA tag 到当前版本**
   所有影响用户使用的改动（性能优化、操作方式变化、功能增删等），分点描述，
   不写技术细节。该文件有两个用途：
   - workflow 用作 GitHub Release 页面的 body；
   - 通过 `--notes-file` 嵌入 manifest.json，设备 Web 端 OTA 更新弹窗中展示给用户。
9. 把发布包目录中的**全部四个文件**提交到发布仓库：
  `manifest.json`、`slot-$VERSION.img.gz`、`slot-$VERSION.img.gz.sig` 和
  `RELEASE_NOTES.md`。只提交 `RELEASE_NOTES.md` 会导致 workflow 的文件校验失败。
  同时提交必要的 GitHub Actions workflow 修改。
   提交作者也应为 `NMminer1024 <nmminer1024@users.noreply.github.com>`。
10. 用版本 tag 触发 workflow：
    - `git tag -a "$VERSION" -m "$VERSION"`
    - `git push origin main --tags`
    - ⚠️ **一次 push 带多个新 tag 时，GitHub Actions 的 `on: push: tags`
      只触发一次（甚至不触发目标 tag 的 run）**（2026-09-25 实测：一次推 8 个
      tag，目标 tag 的 run 没出现）。如果本地积累了多个未推 tag，先确认哪些
      需要发布，**只推本次要发布的 tag**；若已经批量推了且 workflow 没触发，
      单独重推目标 tag 即可：
      `git push origin :refs/tags/$VERSION && git push origin $VERSION`
      （删的是刚推的未发布 tag，不算删除已发布 release，不违反约束）。
      判断是否触发的可靠方法：查 `actions/runs` API 里有没有该 tag 的 run，
      而不是看 release 页面 URL 是否 200（GitHub 对不存在的 tag 也返回 200）。
11. 发布后做闭环收尾检查：
   - 先等 GitHub Actions / Release 生成完成；如果刚推完就查不到资产，先轮询重试
   - `curl -sfI -L "https://github.com/$RELEASE_REPO/releases/tag/$VERSION"`，确认 release 页面可打开
   - `curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/manifest.json"`
   - `curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/slot-$VERSION.img.gz"`
   - `curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/slot-$VERSION.img.gz.sig"`
   如果任一项失败，立刻停下来修流程或等 GitHub Actions 完成，不要假设发布成功。
    - 同时确认 Release 页面 body 已显示 `RELEASE_NOTES.md` 的内容
      （`curl -sfL "https://api.github.com/repos/$RELEASE_REPO/releases/tags/$VERSION"`
      检查 `body` 字段非空且与本地文件一致）。
12. 发布只是这条链路的中间一环：`rockbase-build`（编译）→ 本 skill（打包发布）→
    或本文 curl 验证过的 GitHub 直链触发 `rockbase-ota.sh check/apply`）。

## 约束

- 不编译，不重建镜像。
- 不上传私钥，不把私钥提交进仓库。
- 验签失败就停止，不准发布。
- 源码仓库和发布仓库要分开。
- 没有发布仓库写权限时，不要尝试绕过流程。
- **签名密钥必须与源码固化公钥配对**，发布前核对不一致就先处理，不要带着不匹配的密钥发布。
- **每个 release 必须带用户向变更说明**（`RELEASE_NOTES.md`），不允许发空 body 的 release。
- **普通发布禁止删除已有 Release 或 tag**。如果同名 Release/tag 已存在，必须停止并报告，
  不得自动覆盖、删除或重推。
- “删除旧 Release 后重发”只允许在用户明确提出“覆盖发布/重发同一版本”时执行，
  且必须先说明将删除的精确 Release/tag，并获得确认。用户临时要求过一次删除，
  不代表后续发布都允许删除。

## GitHub API Token（仅显式授权的重发/修补用）

workflow 本身用默认 `GITHUB_TOKEN` 就能发布，但**本地手动操作**
（删除已发布的 release、修补 body、查 run 日志等）需要个人 token：

- **存放位置（固定）**：`~/.rockbase-ota-keys/github-api-token`（chmod 600，纯 token 无换行）。
  读取方式：`TOKEN=$(cat ~/.rockbase-ota-keys/github-api-token)`，**不要把 token 内容打印到终端输出或贴进聊天**。
- **缺失时让用户创建**（不要自己猜/找）：
  - Classic：https://github.com/settings/tokens/new ，勾 `repo` scope；
  - Fine-grained：https://github.com/settings/personal/access-tokens ，选仓库
    `btc-solo-node-release`，Permissions → Contents: Read and write。
  - 让用户自己在终端执行落盘（避免 token 经过模型上下文）：
    `printf '%s' '<TOKEN>' > ~/.rockbase-ota-keys/github-api-token && chmod 600 ~/.rockbase-ota-keys/github-api-token`
- **常用 API 操作**：
  ```bash
  TOKEN=$(cat ~/.rockbase-ota-keys/github-api-token)
  # 查 release id
  RID=$(curl -sf "https://api.github.com/repos/$RELEASE_REPO/releases/tags/$VERSION" \
    -H "Authorization: token $TOKEN" | python3 -c "import json,sys; print(json.load(sys.stdin)['id'])")
  # 删 release（tag 不会跟着删，需另行 git push origin :refs/tags/$VERSION）
  curl -s -o /dev/null -w "%{http_code}\n" -X DELETE \
    "https://api.github.com/repos/$RELEASE_REPO/releases/$RID" -H "Authorization: token $TOKEN"   # 期望 204
  ```
- **仅在用户明确授权后，重发同一版本号的完整流程**（同名 tag 已发布过、需要换包或补 body 时）：
  1. 更新 `release-packages/$VERSION/` 下的文件（含 `RELEASE_NOTES.md`）并 push main；
  2. 用 API 删掉远端旧 release（上面 DELETE 命令）；
  3. `git push origin :refs/tags/$VERSION && git push origin $VERSION` 重推 tag；
  4. 等 workflow 跑完（约 1 分钟），按收尾检查验证 body + 资产 sha256。
  注意：普通发布 workflow 不应包含自动删除 Release 的步骤。删除动作只能作为这次
  明确授权的临时操作，完成后不得保留为默认惯例。

## Release 说明规则（RELEASE_NOTES.md）

每个 OTA release 的 GitHub Release 页面必须有一段面向**最终用户**的变更说明，
由 `release-packages/$VERSION/RELEASE_NOTES.md` 提供（workflow 通过
`body_path` 写入 release body；注意 `action-gh-release@v2` 的输入名是
`body_path`，不是 `body_file`，写错会被静默忽略导致 release body 为空）。规则：

- **范围**：总结从**上一个 OTA tag** 到**当前版本**之间的所有改动
  （在源码仓库用 `git log <prev-tag>..HEAD --oneline` 拉全量 commit 列表作为素材，
  上一个 tag 用 `git describe --tags --abbrev=0 HEAD~` 或发布仓库里上一个
  `v*-ota` tag 确定）。
- **只写用户能感知到的东西**：性能优化（更快/更流畅/更省电/更省 SD 卡）、
  操作方式变化（新按钮、新页面、新设置项、交互流程变化）、功能新增/移除、
  稳定性修复（用户之前遇到过的卡顿、闪退、断连等）。**不写技术细节**
  （不提 commit hash、内核配置、驱动、编译参数、内部模块名）。
- **分点描述**，按类别分组，用以下小标题（没有的类别就省略）：
  - `### Added` — 新功能
  - `### Improved` — 性能/体验优化
  - `### Fixed` — 问题修复
  - `### Removed` — 移除的功能或行为
- 每条一句话，用户视角措辞（"LCD 页面切换更流畅" 而不是
  "lvgl tileview 刷新间隔从 500ms 调到 2s"）。
- 语言：英文（发布仓库面向海外用户）；如有必要可附中文，但英文在前。
- 示例：
  ```markdown
  ### Improved
  - LCD UI page switching is noticeably smoother and uses less power.
  - Boot time reduced by about 10 seconds.

  ### Fixed
  - WiFi no longer drops after long idle periods.
  - Stratum page now shows the correct hashrate after reconnect.

  ### Removed
  - The old SD-card swap is removed; swap now runs in RAM (no SD wear).
  ```
- 写完自查：把说明读给不懂技术的用户听，他能否明白"这版升级对我有什么用"。
  如果某条改动用户完全无感（纯内部重构、构建系统调整），就不要写进去。

## README 双语规则（发布仓库）

发布仓库的 README 采用「英文发布 + 中文本地参考」模式：

- **git 只跟踪英文版 `README.md`**（GitHub 默认展示，最终发布物）。
- **中文版 `README.zh-CN.md` 不被 git 跟踪**（已写入发布仓库 `.gitignore`），
  仅作为本地参考阅读、方便纠错。文件保留在本地工作目录，不要删除。
- **每次修改 README 内容时，必须中英两份同步修改**：先改哪份都行，但提交前
  确认两份内容一致（章节、事实、图片引用都要对齐），只提交 `README.md`。
- 两份文件顶部都有语言切换链接（`[ English ] · [ 中文 ](README.zh-CN.md)` 互链），
  保持这个格式。
- 检查命令：
  ```bash
  cd "$RELEASE_WORKDIR"
  git status --short          # README.zh-CN.md 不应出现在待提交列表
  git check-ignore README.zh-CN.md   # 应输出该文件名（确认被忽略）
  ```
- 如果 `git status` 里出现 `README.zh-CN.md`（说明被误跟踪），执行
  `git rm --cached README.zh-CN.md` 取消跟踪，文件本身保留。

## 常用命令

```bash
# 先写 release-packages/$VERSION/RELEASE_NOTES.md（见「Release 说明规则」），
# 再打包（--notes-file 把说明嵌入 manifest.json，设备 Web OTA 弹窗会展示）
tools/mk-ota-package.sh \
  --version "$VERSION" \
  --key "$PRIVATE_KEY" \
  --slot-img "$SLOT_IMG" \
  --base-url "https://github.com/$RELEASE_REPO/releases/download/$VERSION" \
  --min-version v0.0.01 \
  --notes-file "$RELEASE_WORKDIR/release-packages/$VERSION/RELEASE_NOTES.md" \
  --out-dir "$PKG_DIR"

git add -A
git commit -m "feat: publish OTA release $VERSION"
git tag -a "$VERSION" -m "$VERSION"
git push origin main --tags

curl -sfI -L "https://github.com/$RELEASE_REPO/releases/tag/$VERSION"
curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/manifest.json"
curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/slot-$VERSION.img.gz"
curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/slot-$VERSION.img.gz.sig"

# 如果刚推送完成，这里允许短暂等待 GitHub Actions
for i in 1 2 3 4 5; do
  curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/manifest.json" && break
  sleep 10
done
```
