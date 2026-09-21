---
name: rockbase-web-i18n
description: 'Rockbase web 多语言（i18n）同步规则。每次改 web 端文案/新增界面字符串时，检查是否影响多语言并同步所有受影响语言。Use when: 多语言, i18n, 国际化, 翻译, 语言, language, 文案, 字符串, 新增按钮, 新增页面, 改文字, 改提示, 改标题, data-i18n, I18N, 中文, English, 语言列表.'
---

# Rockbase Web 多语言（i18n）同步规则

**核心原则：每次修改 web 端，先判断改动是否触及用户可见文案；若触及，必须同步所有已实现的语言，不能只改英文。**

Web 前端源码：`buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/`（`index.html` / `app.js` / `style.css`）。

---

## i18n 机制（现状）

- **字典**：`app.js` 里的 `var I18N = {` 对象，按语言码分块：`en: {`、`zh: {`、`es: {`、`fr: {`、`de: {`、`pt: {`、`ru: {`、`ja: {`、`ko: {`、`ar: {`（**10 个语言全部有完整翻译**，key 数量必须一致，当前 159 keys × 10）。
- **标记**：HTML 里需要翻译的元素加 `data-i18n="<key>"`，如 `<button data-i18n="cleanBlock.btn">Clean Block Data</button>`。
- **取值**：`t(key)` 先查 `I18N[currentLang][key]`，查不到回退 `I18N.en[key]`，再查不到返回 key 本身。
- **应用**：`applyI18n()` 遍历所有 `[data-i18n]` 节点写入 `t(key)`，并设置 `<html lang>`。
- **切换**：`#langSelect` 下拉（`index.html`），`setLang()` 持久化到 `localStorage("nmLang")`。
- **语言列表**：`#langSelect` 有 10 个语言选项（en/zh/es/fr/de/pt/ru/ja/ko/ar），**全部有完整翻译**。`LANG_CODES` 从下拉选项动态读取，`isKnownLang()` 校验。

> 关键：新增语言翻译 = 在 `I18N` 里加一个 `I18N.<code> = {...}` 字典块，UI 零改动。

---

## 每次改 web 前的检查清单

1. **是否新增/修改了用户可见文案？**
   - 新增按钮、标题、提示、弹窗、状态文字、错误信息 → **是**
   - 只改布局/颜色/图标/纯样式/纯逻辑（不产生新字符串）→ **否**，跳过

2. **若是，文案是否走了 i18n？**
   - 已有 `data-i18n` 且 key 存在 → 只需确认各语言字典里该 key 的值是否需要同步更新
   - 硬编码英文、没有 `data-i18n` → **必须补上** `data-i18n="<key>"`，并在 `I18N.en` + `I18N.zh` 都加对应 key

3. **同步所有 10 个语言**
   - 每个新增/改动的 key，必须在**全部 10 个**语言块（en/zh/es/fr/de/pt/ru/ja/ko/ar）里都有对应条目
   - 中文翻译要地道，不要机翻腔；技术术语（bitcoind、Stratum、block、share 等）保留英文
   - **Arabic 必须用 python 码点校验**（0x0600–0x06FF），终端显示会乱码，不能目视判断

4. **动态字符串**（JS 里拼接的文案，如 cleanBlock 的 phase 标签）
   - 这类不走 `data-i18n`，而是 `t("cleanBlock.phase.xxx")` 显式取 key
   - 同样要在每个已实现语言块里补齐这些 key

---

## 操作步骤（标准流程）

1. 改 `index.html`：给新文案元素加 `data-i18n="<namespace>.<name>"`（命名沿用现有风格，如 `cleanBlock.*`）。
2. 改 `app.js` 的 `I18N`：
   - `I18N.en` 加 key（英文原文）
   - `I18N.zh` 加 key（中文翻译）
   - 若将来新增了其它语言块，也要一并加
3. 若文案是 JS 动态生成，找到对应 `t("...")` 调用点，确认 key 已在各语言块定义。
4. **热部署** `index.html` + `app.js` 到板子（见 `rockbase-hotfix`），浏览器切到中文验证新文案显示正确，再切回英文确认无回归。
5. 验证通过后再固化到源码、commit。

---

## 常见坑

- **只加了英文忘了其它语言**：最典型的遗漏。改完务必跑 parity 校验（见下方命令），确认 10 个语言块 key 数量一致。
- **硬编码英文没走 `t()`**：两类场景——(1) HTML 静态文案没加 `data-i18n`；(2) **JS 动态消息**（如 `setWifiMessage("SSID is required.")`、`setTzMessage("Applying timezone...")`）直接传英文字符串而非 `t("key")`。后者更隐蔽，界面英文时看不出问题，切其它语言后仍是英文。排查方法：`grep -n 'setWifiMessage\|setTzMessage' app.js` 找所有硬编码字符串调用点。
- **key 拼写不一致**：HTML 的 `data-i18n` 与 `I18N` 字典 key 必须逐字一致，否则回退显示 key 本身。
- **动态文案漏配**：`t()` 取不到会直接显示 key 字符串（如页面上出现 `cleanBlock.phase.xxx`），排查时先看是不是 key 缺失。
- **multi_replace 丢行尾逗号**：在字典块中间插入新 key 时，如果 `oldString` 以某行结尾（含逗号），`newString` 必须保留该逗号并在新行末尾也加逗号。漏掉会导致 `node --check` 报 `SyntaxError: Unexpected string`。**改完必须跑 `node --check app.js`**。
- **语言块头部是无引号标识符**：`en: {` 不是 `"en": {`，写校验脚本时正则要用 `(en|zh|...): \{`。
- **非 en 块没有 `/* about */` 等注释**：只有 en 块有 section 注释，其它语言块直接连续排列，不能用注释行做锚点。
- **bash 历史展开吃 `!`**：`node -e "...!..."` 双引号里的 `!` 会被 bash history expansion 吃掉。用 `cat > /tmp/check.js <<'EOF' ... EOF` 写临时脚本再 `node /tmp/check.js`。
- **eval 提取 I18N 会失败**：字符串值里含 `{`/`}`（如 `{tz}` 占位符），简单花括号计数会错位。用带字符串状态机的解析器，或直接用正则按行提取 key 做 parity 检查。
- **Arabic 终端显示乱码**：终端渲染阿拉伯语会错乱，必须用 python 码点校验（`0x600 <= ord(c) <= 0x6FF`），不能目视判断。

---

## 快速自检命令

```bash
WEB=buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web

# JS 语法检查（每次改完 app.js 必跑）
node --check $WEB/app.js

# 列出所有 data-i18n key（HTML 侧）
grep -rhoE 'data-i18n="[^"]+"' $WEB/index.html | sort -u

# 列出 I18N 里已实现的语言块
grep -nE '^    (en|zh|es|fr|de|pt|ru|ja|ko|ar): \{' $WEB/app.js

# 10 语言 parity 校验（key 数量 + 一致性 + Arabic 码点）
# 写临时脚本 /tmp/i18n_check2.js（正则按行提取 key，不用 eval），然后：
node /tmp/i18n_check2.js
# 期望输出：langs: en,zh,es,fr,de,pt,ru,ja,ko,ar / en keys: N / PARITY OK / arabic check done

# 查找 JS 动态消息中的硬编码英文（应全部走 t()）
grep -nE 'set(Wifi|Tz)Message\("[A-Z]' $WEB/app.js
```

改完新 key 后，用上面第一条命令确认 HTML 引用的每个 key 都能在 `I18N.en` 和 `I18N.zh` 里找到。
