# DSEG7 数码管字体 — Rockbase (K230)

Rockbase LCD UI 的数码管/七段数字字体，基于 **DSEG7 Classic**（免费，见
`dseg7-classic_5.2.5/LICENSE`）。从 NMMiner 项目移植而来，替换其 Shareware 的
DS-Digital，保证商用许可安全。

## 目录结构

```
font/
├── dseg7-classic_5.2.5/
│   ├── LICENSE                          # DSEG7 许可（免费可商用）
│   └── ttf/
│       ├── dseg7-classic-latin-400-normal.ttf   # 400 字重（源）
│       └── dseg7-classic-latin-700-normal.ttf   # 700 字重（源）
├── gen_dseg7_fonts.py                   # 生成脚本（lv_font_conv 封装）
├── dseg7c400_font_<size>.c              # 400 字重，各尺寸（已生成，32 个）
└── dseg7c700_font_<size>.c              # 700 字重，各尺寸（已生成）
```

## 生成方法

依赖 `lv_font_conv`（LVGL 官方字体转换器）：

```sh
npm install -g lv_font_conv        # 一次性安装
python3 font/gen_dseg7_fonts.py    # 生成全部尺寸
python3 font/gen_dseg7_fonts.py --check   # 仅校验 fallback=NULL
```

脚本对每个 (字重, 尺寸) 调用：

```
lv_font_conv --font <ttf> --symbols " !-.0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" \
  --size <N> --bpp 4 --format lvgl --no-compress --lv-font-name dseg7c{400,700}_font_<N> -o <out.c>
```

- **字符集** = DSEG7 TTF 实际包含的全部字形：空格 `! - . 0-9 : A-Z a-z`。
  DSEG7 **没有** `/` `%` `$`，不要加（lv_font_conv 会输出空白字形）。
- **尺寸**：14 16 18 20 22 24 28 32 36 42 48 54 66 80 100 120（800x480 屏）。
- **bpp 4**：4bit 灰度抗锯齿。

## 在 UI 代码中使用

字体 `.c` 已在 `src/Makefile` 里用 `$(CXX) -x c` 编译并链接进 `rockbase-lcdui`。
在任意 `.cpp` 里：

```cpp
#include "lvgl/lvgl.h"
LV_FONT_DECLARE(dseg7c700_font_54);          // 声明（extern "C"，unmangled）
...
lv_obj_set_style_text_font(label, &dseg7c700_font_54, 0);
```

命名规则：`dseg7c400_font_<size>` / `dseg7c700_font_<size>`。

## ⚠️ 关键经验（踩坑记录）

1. **`.fallback` 绝不能自引用**（`.fallback = &self`）。
   遇到字体范围外的字符（如 `°` U+00B0）时 LVGL 会无限递归找 fallback 字形
   → 栈溢出 → 板子卡死且无崩溃/复位。
   正确做法：`.fallback = NULL`，LVGL 优雅跳过缺失字形。
   **重新生成后务必跑 `--check` 确认全部为 NULL。**

2. **LVGL 版本必须匹配**。Rockbase 实际构建用的是 **LVGL 9.6.0**
   （`BR2_PACKAGE_LVGL_CUSTOM_VERSION` 指向 git hash，**不是** overlay 里那个
   8.3.7 目录的 `lv_conf.h`）。字体 C 文件里的 `lv_font_t` / `lv_font_fmt_txt_dsc_t`
   字段必须与目标 LVGL 版本一致。
   - 验证方法：用目标交叉编译器 + staging 头文件实际编译：
     ```sh
     output/k230_rockbase_defconfig/host/bin/riscv64-unknown-linux-gnu-g++ \
       -x c -c -mcpu=c908v -mabi=lp64d font/dseg7c400_font_14.c -o /tmp/t.o
     ```
   - 若 LVGL 大版本变化（8↔9），`lv_font_t` 结构体字段会变，需重新生成/核对。

3. **字体 `.c` 必须按 C 编译**（`-x c`），否则 C++ 编译器会对 `lv_font_t` 符号做
   name mangling，与 `LV_FONT_DECLARE` 的 `extern "C"` 声明不匹配 → 链接失败。
   `src/Makefile` 已用 `$(CXX) -x c` 处理（构建环境只可靠提供 `CXX`，无独立 `CC`）。

4. **符号必须 unmangled**：`nm xxx.o | grep dseg7` 应看到
   `D dseg7c400_font_14`（无 `_Z` 前缀）。

## 体积

32 个字体源文件共约 9.5MB；编译为 const 数据后占用 flash/RAM 远小于此
（`-s` strip + const 段）。大尺寸（100/120）单个源文件 ~1MB，按需选用。
