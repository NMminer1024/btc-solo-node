---
name: git-commit-rules
description: 'Git 提交规范：所有 commit 内容必须用英文。Use when: git commit, 提交, commit message, 写提交信息, git 规范, commit 规范, 提交信息, commit message 检查, 英文提交, English commit.'
---

# Git 提交规则

> 适用范围：本仓库（`btc-solo-node`）所有 git commit。
> 触发场景：写/审查/修正 commit message、执行 `git commit`、要求提交代码时。

---

## 核心规则（强制）

### R1 · 所有 commit 内容必须用英文

- **subject（标题行）**、**body（正文）**、**footer（脚注）** 全部使用英文，不得出现中文或其他非英文字符。
- 若需要补充说明给中文读者，写在 commit 之外（如 PR 描述、IM、口头沟通），不要写进 commit message。
- 例外：文件名、命令、路径、变量名、issue 号等**本身就是非英文的字面量**可保留原样
  （例如文件名 `S00resizemmc`、命令 `fw_setenv`、中文命名的测试文件路径）。

### R2 · 提交格式约定（配合 R1）

采用 Conventional Commits 风格，subject 用英文：

```
<type>(<scope>): <英文简述>

<可选的英文正文，逐条说明>

<可选的英文脚注，如 Co-authored-by / Ref / Fixes #123>
```

常用 type：`feat` / `fix` / `docs` / `refactor` / `perf` / `test` / `chore` / `build` / `ci` / `revert`。

---

## 检查清单（commit 前逐条过一遍）

- [ ] subject 全英文、无中文，首字母小写、无结尾句号
- [ ] body 若有，全英文，逐条用 `- ` 或 `* ` 开头
- [ ] footer 若有，全英文
- [ ] 仅字面量（路径/命令/变量名）允许非英文，其余一律英文

## 错误示例（不要这样写）

```
fix: 修复 OTA 写盘进度卡 0% 的问题        ❌ 中文
docs: 移除 1G/2G 单独验证                  ❌ 中文
feat: 支持 OTA 断点续传                     ❌ 中文
```

## 正确示例

```
fix: reset OTA flash progress via /proc/diskstats deltas

- progress previously stuck at 0% due to unreliable $! in ash background pipeline
- compute progress from /proc/diskstats write-sector deltas of the target partition
```

```
docs: drop separate 1G/2G OTA validation

Memory size is auto-detected at runtime by U-Boot, mmc topology is unified.
```

---

## 提交后校验（可选，快速自检）

若需确认最近提交是否合规：

```bash
git log --oneline -20 | LC_ALL=C grep -nP '[\x{4e00}-\x{9fff}]' && echo "含中文，需重写" || echo "OK"
```

（检出中文则用 `git commit --amend` 或 `git rebase -i` 改写，改写后仍是英文。）
