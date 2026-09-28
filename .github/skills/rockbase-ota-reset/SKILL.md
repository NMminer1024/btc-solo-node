---
name: rockbase-ota-reset
description: 'Rockbase OTA 版本线重置：一次性清空发布仓库所有 Release + tag、源码仓库所有产品 tag，并把 VERSION 重置回 0.0.01 重新起版。Use when: 版本重置, 版本线重置, 从0重来, 从0开始, 重新起版, 清空release, 清空tag, 删所有release, 删所有tag, 重置版本号, 版本清零, ota版本重置, 全部删除重发, 版本重新开始, reset version, version line restart, clear all releases, wipe tags.'
---

# Rockbase OTA 版本线重置

把 Rockbase OTA 的版本历史**整体清零**：删掉发布仓库的所有 Release 和 tag、
源码仓库的所有产品 tag，并把 `VERSION` 文件重置回 `0.0.01`，让版本号从 0 重新开始。

> 这是「版本线重置」操作，**不可逆**。历史上做过两次：2026-09-05（首次）、
> 2026-09-28（本次）。本 skill 固化的是 2026-09-28 这次完整流程 + 踩坑。

## 何时使用

- 用户明确说「OTA 版本从 0 重来」「版本线重置」「清空所有 release/tag 重新起版」。
- 历史版本号作废、不想再在旧版本基础上递增时。
- **不要**用于普通发布或单个 release 的删除/重发——那是 `rockbase-ota-release`
  skill 里「GitHub API Token」小节的单删流程，不是本 skill。

## 前置认知（两个仓库，别搞混）

| 仓库 | remote | 内容 |
|------|--------|------|
| **源码仓库** | `git@github-nmminer1024:NMminer1024/btc-solo-node.git` | 产品源码 + `VERSION` 文件 + 纯版本 tag（`v0.2.03` 这种） |
| **发布仓库** | `git@github-nmminer1024:NMminer1024/btc-solo-node-release.git` | OTA 包 + `*-ota` tag（`v0.2.05-rockbase-k230-20260925-9fe9e95-ota` 这种）+ GitHub Release 资产 |

⚠️ **关键事实**：发布仓库远端**同时存在两类 tag**——
1. `*-ota` 完整 tag（每个 Release 对应一个）；
2. **纯版本 tag**（`v0.2.03` 这种，早期误推到发布仓库的残留）。
所以清发布仓库时，`git ls-remote --tags` 会看到比 Release 数量更多的 tag，
两类都要删干净。

## 标准流程

### 0. 前置检查（必做）

```bash
# 1. API token 就位（删 Release 必需，不打印内容）
[ -s ~/.rockbase-ota-keys/github-api-token ] && echo "token OK len=$(wc -c < ~/.rockbase-ota-keys/github-api-token)" || echo "缺 token"

# 2. 列出源码仓库所有产品 tag
cd /home/bitpony/k230_linux_sdk
git tag -l --sort=-v:refname

# 3. 列出发布仓库远端所有 tag（含纯版本 tag 残留）
git ls-remote --tags git@github-nmminer1024:NMminer1024/btc-solo-node-release.git

# 4. 列出发布仓库所有 Release（拿 id，删 Release 用）
TOKEN=$(cat ~/.rockbase-ota-keys/github-api-token)
curl -sf "https://api.github.com/repos/NMminer1024/btc-solo-node-release/releases?per_page=100" \
  -H "Authorization: token $TOKEN" | python3 -c "import json,sys; d=json.load(sys.stdin); print('Release 总数:', len(d)); [print(' -', r['tag_name'], '| id=', r['id'], '| assets=', len(r['assets'])) for r in d]"
```

**把上面查到的清单完整列给用户，获得明确确认后再执行。** 这是不可逆操作，
skill 约束要求「删除已有 Release/tag 必须用户明确授权」。

### 1. 删发布仓库所有 Release（API DELETE）

```bash
TOKEN=$(cat ~/.rockbase-ota-keys/github-api-token)
RELEASE_REPO="NMminer1024/btc-solo-node-release"
# RID 列表来自第 0 步查到的 release id，逐个删
for RID in <id1> <id2> ...; do
  code=$(curl -s -o /dev/null -w "%{http_code}" -X DELETE \
    "https://api.github.com/repos/$RELEASE_REPO/releases/$RID" -H "Authorization: token $TOKEN")
  echo "release id=$RID -> HTTP $code"   # 期望 204
done
```

### 2. 删发布仓库所有 tag（本地 + 远端）

```bash
cd /home/bitpony/Desktop/mining/_work/btc-solo-node-release   # 发布仓库本地 checkout
# 先确认 remote 是 btc-solo-node-release（见下方踩坑 1）
git remote -v | head -1
# 删本地所有 tag
git tag -l | xargs -I{} git tag -d {}
# 删远端所有 tag（用第 0 步 ls-remote 查到的完整清单，含纯版本 tag）
for t in <tag1> <tag2> ...; do git push origin :refs/tags/$t; done
```

### 3. 删源码仓库所有产品 tag（本地 + 远端）

```bash
cd /home/bitpony/k230_linux_sdk
git remote -v | head -1   # 必须是 btc-solo-node.git
for t in <v0.2.03> <v0.2.02> ...; do
  git tag -d $t
  git push origin :refs/tags/$t
done
git tag -l   # 应为空
```

### 4. VERSION 重置 + 提交 + 打新 tag

```bash
cd /home/bitpony/k230_linux_sdk
# 先把未提交的业务改动单独提交（英文 message，见 git-commit-rules skill）
git add <业务文件> && git commit -m "fix(...): ..."

# 重置 VERSION
printf '0.0.01\n' > VERSION
git add VERSION
git commit -m "chore(version): reset to 0.0.01 (version line restart after full tag/release cleanup)"

# 打新 tag 并推送
git tag v0.0.01
git push origin dev
git push origin v0.0.01
```

### 5. 收尾验证

```bash
# 源码仓库：tag 只剩 v0.0.01，VERSION=0.0.01
cd /home/bitpony/k230_linux_sdk && git tag -l && cat VERSION
# 发布仓库：远端 tag 全空
git ls-remote --tags git@github-nmminer1024:NMminer1024/btc-solo-node-release.git   # 应无输出
# 发布仓库：Release 全空
TOKEN=$(cat ~/.rockbase-ota-keys/github-api-token)
curl -sf "https://api.github.com/repos/NMminer1024/btc-solo-node-release/releases?per_page=100" \
  -H "Authorization: token $TOKEN" | python3 -c "import json,sys; print('剩余 Release:', len(json.load(sys.stdin)))"   # 应为 0
```

## 踩坑记录

### ⚠️ 坑 1：shell 工作目录串台，`git rev-parse --show-toplevel` 返回错误仓库

- **症状**：执行「删源码仓库 tag」的命令时，`git tag -d` 报 `error: tag 'v0.2.03' not found`，
  但 `git push origin :refs/tags/v0.2.03` 却显示 `[deleted]`。
- **根因**：上一条命令 `cd` 进了发布仓库本地 checkout（`~/Desktop/mining/_work/btc-solo-node-release`），
  而 run_in_terminal 的 shell 是**持久复用**的，cwd 没回到源码仓库。于是
  `cd "$(git rev-parse --show-toplevel)"` 解析到的是**发布仓库**的 toplevel，
  导致 `git tag -d` 在发布仓库找源码仓库的 tag（找不到），
  而 `git push origin :refs/tags/...` 删的其实是**发布仓库远端**的 tag。
- **修复**：每个涉及 git 的命令块**开头显式 `cd` 到目标仓库的绝对路径**，
  并用 `git remote -v | head -1` 当场核对 remote 是不是预期仓库，再执行删除。
  不要依赖上一条命令留下的 cwd。

### ⚠️ 坑 2：发布仓库远端混存「纯版本 tag」，只删 `*-ota` 会漏

- **症状**：以为发布仓库只有 `*-ota` tag，按 Release 数量（7 个）去删，
  结果 `git ls-remote --tags` 实际有 14 个 `*-ota` + 7 个纯版本 tag（`v0.2.03` 等）。
- **根因**：早期发布流程曾把纯版本 tag 误推到发布仓库远端，形成残留。
  这些 tag 没有对应 Release，但仍是「历史版本 tag」，属于要清的对象。
- **修复**：清发布仓库时**以 `git ls-remote --tags` 的完整输出为准**，
  逐条删，不要假设 tag 数量等于 Release 数量。

### ⚠️ 坑 3：正在进行的编译会读到重置后的 VERSION

- **症状**：重置 `VERSION` 后，后台正在跑的 `make buildroot` 末尾
  `post-build.sh gen_version()` 读到的已是 `0.0.01`，镜像版本变成 `v0.0.01`。
- **根因**：`gen_version()` 在编译末尾才读 `VERSION` 文件，重置发生在编译中途。
- **修复**：这是**预期行为**（用户要"顺带一起编译"时正好让新镜像带 `v0.0.01`）。
  但要主动向用户说明这个副作用，避免用户以为镜像还是旧版本号。
  若不希望影响在途编译，应等编译结束再重置 `VERSION`。

### ⚠️ 坑 4：删 Release 用 API，删 tag 用 git，两套机制别混

- **症状**：试图用 `git push :refs/tags/` 删 Release 资产，或以为删了 tag 就删了 Release。
- **根因**：GitHub 上 **Release 对象**（含 `manifest.json`/`slot-*.img.gz`/`.sig` 资产）
  和 **git tag** 是两个独立实体。删 tag 不会删 Release，删 Release 也不会删 tag。
- **修复**：Release 走 `DELETE /releases/{id}`（需 token），tag 走
  `git push origin :refs/tags/<tag>`。两者都要做，缺一不可。

## 约束

- **不可逆**：执行前必须把完整清单列给用户并获得明确确认。
- 不打印 API token 内容（只校验存在性/长度）。
- 每个 git 命令块显式 `cd` 到目标仓库绝对路径 + 核对 remote，防 cwd 串台。
- 删完必须做第 5 步收尾验证，确认两边都清空、VERSION=0.0.01、新 tag 已推。
- 与 `rockbase-version` skill 的「版本线重置」先例一致：重置后版本号从 `v0.0.01` 重新开始，
  旧版本号作废不复用。

## 关联 skill

- `rockbase-version`：版本号定义、升版规则、版本线重置先例记录。
- `rockbase-ota-release`：单个 release 的发布/删除/重发（非批量重置）。
- `rockbase-ota`：真机 OTA 升级流程。
- `git-commit-rules`：commit message 必须英文。
