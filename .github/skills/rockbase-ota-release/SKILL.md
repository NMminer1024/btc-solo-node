---
name: rockbase-ota-release
description: 用于把现成的 Rockbase OTA 包发布到公开 GitHub Releases 仓库，校验 manifest / 签名 / 校验和，或在不重新编译镜像的前提下更新版本化 OTA 发布。
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
   - `PKG_DIR`，默认 `~/ota-dist/$VERSION`
   - `PRIVATE_KEY`，默认 `~/.rockbase-ota-keys/ota-private.pem`
   - `SLOT_IMG`，只在需要重打包时才用
2. 优先复用 `~/ota-dist/<version>/` 下已经生成好的包。
3. 发布前先本地验签：
   - `openssl dgst -sha256 -verify <(openssl pkey -in "$PRIVATE_KEY" -pubout) -signature "$PKG_DIR/slot-$VERSION.img.gz.sig" "$PKG_DIR/slot-$VERSION.img.gz"`
   - `sha256sum` 必须和 `manifest.json` 里的值一致。
4. 如果包不存在、已过期，或 `manifest` 指向了错误仓库，就重新生成：
   - `tools/mk-ota-package.sh --version "$VERSION" --key "$PRIVATE_KEY" --slot-img "$SLOT_IMG" --base-url "https://github.com/$RELEASE_REPO/releases/download/$VERSION" --out-dir "$PKG_DIR"`
5. 用公开 GitHub Releases 发布：
   - 新发布：`gh release create "$VERSION" ... --repo "$RELEASE_REPO"`
   - 已存在发布：`gh release upload "$VERSION" ... --repo "$RELEASE_REPO" --clobber`
6. 发布后重新拉取资产 URL，确认匿名可访问。

## 约束

- 不编译，不重建镜像。
- 不上传私钥，不把私钥提交进仓库。
- 验签失败就停止，不准发布。
- 源码仓库和发布仓库要分开。

## 常用命令

```bash
tools/mk-ota-package.sh \
  --version "$VERSION" \
  --key "$PRIVATE_KEY" \
  --slot-img "$SLOT_IMG" \
  --base-url "https://github.com/$RELEASE_REPO/releases/download/$VERSION" \
  --out-dir "$PKG_DIR"

gh release create "$VERSION" \
  "$PKG_DIR/manifest.json" \
  "$PKG_DIR/slot-$VERSION.img.gz" \
  "$PKG_DIR/slot-$VERSION.img.gz.sig" \
  --repo "$RELEASE_REPO" \
  --title "$VERSION"
```
