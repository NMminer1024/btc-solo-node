---
name: rockbase-ota-release
description: 用于把现成的 Rockbase OTA 包发布到公开 GitHub Releases 仓库，校验 manifest / 签名 / 校验和，或在不重新编译镜像的前提下更新版本化 OTA 发布。也包含发布仓库 README 双语规则（英文被 git 跟踪发布、中文本地参考不跟踪、中英同步修改）。Use when: OTA 发布, release, README, 双语, 中英文, 产品说明, 发布仓库文档.
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
   推送时应显式使用 `github.com-NMminer1024` 这个 SSH host。
   所有发布仓库操作都在 `RELEASE_WORKDIR` 内进行；如果目录不存在，先 clone 到这里再继续。
5. 优先复用 `~/ota-dist/<version>/` 下已经生成好的包。
6. 发布前先本地验签：
   - `openssl dgst -sha256 -verify <(openssl pkey -in "$PRIVATE_KEY" -pubout) -signature "$PKG_DIR/slot-$VERSION.img.gz.sig" "$PKG_DIR/slot-$VERSION.img.gz"`
   - `sha256sum` 必须和 `manifest.json` 里的值一致。
7. 如果包不存在、已过期，或 `manifest` 指向了错误仓库，就重新生成：
   - `tools/mk-ota-package.sh --version "$VERSION" --key "$PRIVATE_KEY" --slot-img "$SLOT_IMG" --base-url "https://github.com/$RELEASE_REPO/releases/download/$VERSION" --out-dir "$PKG_DIR"`
8. 把发布仓库里的 `release-packages/$VERSION/` 和 GitHub Actions workflow 一起提交并推送。
   提交作者也应为 `NMminer1024 <nmminer1024@users.noreply.github.com>`。
9. 用版本 tag 触发 workflow：
   - `git tag -a "$VERSION" -m "$VERSION"`
   - `git push origin main --tags`
10. 发布后做闭环收尾检查：
   - 先等 GitHub Actions / Release 生成完成；如果刚推完就查不到资产，先轮询重试
   - `curl -sfI -L "https://github.com/$RELEASE_REPO/releases/tag/$VERSION"`，确认 release 页面可打开
   - `curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/manifest.json"`
   - `curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/slot-$VERSION.img.gz"`
   - `curl -sfI -L "https://github.com/$RELEASE_REPO/releases/download/$VERSION/slot-$VERSION.img.gz.sig"`
   如果任一项失败，立刻停下来修流程或等 GitHub Actions 完成，不要假设发布成功。
11. 发布只是这条链路的中间一环：`rockbase-build`（编译）→ 本 skill（打包发布）→
    `rockbase-ota`（真机 apply，优先用生产域名 `https://ota.nmminer.com/rockbase/manifest.json`
    或本文 curl 验证过的 GitHub 直链触发 `rockbase-ota.sh check/apply`）。

## 约束

- 不编译，不重建镜像。
- 不上传私钥，不把私钥提交进仓库。
- 验签失败就停止，不准发布。
- 源码仓库和发布仓库要分开。
- 没有发布仓库写权限时，不要尝试绕过流程。
- **签名密钥必须与源码固化公钥配对**，发布前核对不一致就先处理，不要带着不匹配的密钥发布。

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
tools/mk-ota-package.sh \
  --version "$VERSION" \
  --key "$PRIVATE_KEY" \
  --slot-img "$SLOT_IMG" \
  --base-url "https://github.com/$RELEASE_REPO/releases/download/$VERSION" \
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
