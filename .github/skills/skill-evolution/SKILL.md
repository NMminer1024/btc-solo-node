---
name: skill-evolution
description: 'Skill 自我进化：每次按 skill 标准流程执行时踩坑并解决后，把对应 skill 优化掉坑。Use when: 踩坑, 踩坑了, 遇到坑, 遇到问题, 解决了, 根因, 教训, 经验固化, 经验沉淀, 优化skill, 更新skill, 改进skill, 修改skill, skill进化, skill迭代, 沉淀经验, 固化经验, 写进skill, 记到skill, skill里加, 完善skill, 补充skill.'
---

# Skill 自我进化

> 目标：让 skill 库持续变强。每次「按某个 skill 的标准流程执行 → 踩坑 → 解决」之后，
> 把这次教训回写进对应的 skill，避免下次（以及团队）再踩。

## 何时使用

- 执行某个 skill 的标准流程时，流程本身**缺了一步 / 步骤有误 / 触发关键词没命中**，导致踩坑
- 用户说「把这个坑写进 skill」「优化下 skill」「固化经验」等
- 完成一次踩坑排查后，主动判断要不要回写 skill

## 核心判断：坑该落到哪（先想清楚再写）

**不是所有坑都要进 skill**。按「流程 vs 事实」二分：

| 坑的类型 | 归属 | 例子 |
|---------|------|------|
| 标准流程缺了一步 / 步骤顺序错 / 命令不对 | **skill** | 内核 autoconf.h 丢失 → 该先 `linux-rebuild` 而非手动 syncconfig |
| 某个包/工具的客观事实、已知限制 | **repo memory**（如 `build-notes.md`） | LVGL 8.3.7 无 `lv_indev_set_gesture_limit` |
| 某台设备/某次环境的临时状态 | **会话记录**，不固化 | 某次板子没插网线 |

判定口诀：**「下次按这个 skill 走，还会不会踩？」** 会 → 改 skill；只取决于客观事实/具体环境、与流程无关 → 写 memory。

## 优化 skill 的标准动作

1. **定位归属 skill**：这个坑属于哪个 skill 的流程？只改那个 skill，不要乱塞到无关 skill。
2. **找到插入位置**：优先加进该 skill 已有的「陷阱 / 注意事项 / 编译陷阱」章节；没有就新建小节。
3. **按固定结构写**（每坑一小节）：
   - `### ⚠️ <一句话标题>`（带 `⚠️` 便于检索）
   - **症状**：报错原文 / 关键日志（方便 `grep` 命中）
   - **根因**：为什么标准流程会踩（一句到三句，说清因果链）
   - **修复**：正确命令（带代码块，可直接复制）
4. **补触发关键词**：如果这次是「关键词没命中导致 skill 没被调用」，把缺的词补进 frontmatter 的 `description`。
5. **验收**（写完必须过一遍）：
   - frontmatter 的 `name`/`description` 合法，`description` 里有关键词
   - **没有重复章节**（同一 H1/小节不重复——这是历史踩过的坑，重复会让 agent 命中旧段落执行歧义流程）
   - 内容能让「下次遇到同样报错」的人直接命中

## 反模式（禁止）

- ❌ 把「一次性的环境事故」写成 skill 步骤（污染流程）
- ❌ 只改 memory 不改 skill，但坑其实是流程缺陷
- ❌ 新增重复章节；应合并进已有小节
- ❌ 在 skill 里堆砌无因果链的「又踩了一坑」流水账

## 示例（本次 autoconf.h 踩坑）

- **坑**：`make buildroot` 在 `target-finalize` 报 `depmod: could not open directory .../6.6.87.2-microsoft-standard-WSL2`
- **根因**：内核 `autoconf.h` 丢失但 stamp 还在 → 增量构建跳过 `make prepare` → `kernelrelease` 回退到宿主机版本
- **归属**：`rockbase-build` skill 的「编译陷阱」章节 → 新增「内核 autoconf.h 丢失 → depmod 报 WSL 主机版本」小节（含标准修复 `linux-rebuild` + 应急 `make ARCH=riscv syncconfig` + 不带 ARCH 的递归依赖红鲱鱼）
- **同时**：把「内核目录脏增量」这个客观事实也写进 repo memory `build-notes.md`，与 skill 互补
