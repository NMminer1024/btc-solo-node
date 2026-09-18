#!/usr/bin/env python3
"""Generate DSEG7 Classic LVGL font C files for Rockbase (K230, 800x480 LCD).

DSEG7 Classic is a free 7-segment / digital-tube style font (see LICENSE).
It replaces the Shareware DS-Digital used in NMMiner, so Rockbase can ship a
commercially-safe digital font.

Two weights are generated:
  - dseg7c400  (400 / normal)
  - dseg7c700  (700 / bold)

Character set = everything the DSEG7 TTF actually contains:
  space ! - . 0-9 : A-Z a-z
(DSEG7 has no '/', '%', '$' — do not add them, lv_font_conv would emit blanks.)

Output: one C file per (weight, size) at src/font/dseg7c{400,700}_font_<size>.c
Each file defines a global `lv_font_t` symbol of the same name.

Usage (from the package src/ dir, or anywhere):
    python3 font/gen_dseg7_fonts.py            # generate all sizes
    python3 font/gen_dseg7_fonts.py --check    # only verify fallback=NULL

Requires: lv_font_conv (npm i -g lv_font_conv). Falls back to `npx lv_font_conv`
if the global binary is not on PATH.
"""

import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TTF_DIR = os.path.join(HERE, "dseg7-classic_5.2.5", "ttf")
OUT_DIR = HERE  # generated .c files live next to this script (src/font/)

# (weight tag, ttf file, symbol prefix)
TARGETS = [
    ("400", "dseg7-classic-latin-400-normal.ttf", "dseg7c400_font"),
    ("700", "dseg7-classic-latin-700-normal.ttf", "dseg7c700_font"),
]

# Full DSEG7 glyph set (space ! - . digits : letters). No '/' '%' '$'.
SYMBOLS = " !-.0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"

# Sizes for the 800x480 Rockbase UI. Small = labels, mid = values, large = hero numbers.
SIZES = [14, 16, 18, 20, 22, 24, 28, 32, 36, 42, 48, 54, 66, 80, 100, 120]

BPP = 4


def find_conv():
    """Return argv prefix for lv_font_conv, preferring the global binary."""
    exe = shutil.which("lv_font_conv")
    if exe:
        return [exe]
    if shutil.which("npx"):
        return ["npx", "lv_font_conv"]
    return None


def gen_one(conv, ttf, name, size, out):
    args = conv + [
        "--font", ttf,
        "--symbols", SYMBOLS,
        "--size", str(size),
        "--bpp", str(BPP),
        "--format", "lvgl",
        "--no-compress",
        "--lv-font-name", name,
        "-o", out,
    ]
    r = subprocess.run(args)
    return r.returncode == 0


def check_fallback(out):
    """Ensure the generated font does NOT self-reference its fallback.

    A `.fallback = &<self>` causes LVGL to infinite-recurse on out-of-range
    glyphs -> stack overflow -> board hang. It must be NULL.
    """
    with open(out, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    m = re.search(r"\.fallback\s*=\s*([^,]+),", text)
    if not m:
        return True, "no fallback field (LVGL < 8.2)"
    val = m.group(1).strip()
    return val == "NULL", val


def main():
    check_only = "--check" in sys.argv

    if check_only:
        bad = 0
        for _, _, prefix in TARGETS:
            for size in SIZES:
                out = os.path.join(OUT_DIR, "%s_%d.c" % (prefix, size))
                if not os.path.isfile(out):
                    continue
                ok, val = check_fallback(out)
                if not ok:
                    bad += 1
                    print("  BAD  %s  fallback=%s" % (os.path.basename(out), val))
        if bad:
            print("FAIL: %d font(s) have a non-NULL fallback" % bad)
            sys.exit(1)
        print("OK: all generated fonts have fallback=NULL")
        return

    conv = find_conv()
    if not conv:
        print("ERROR: lv_font_conv not found. Install with: npm i -g lv_font_conv",
              file=sys.stderr)
        sys.exit(1)

    total = len(TARGETS) * len(SIZES)
    idx = 0
    for tag, ttf_file, prefix in TARGETS:
        ttf = os.path.join(TTF_DIR, ttf_file)
        if not os.path.isfile(ttf):
            print("ERROR: TTF not found: %s" % ttf, file=sys.stderr)
            sys.exit(1)
        for size in SIZES:
            idx += 1
            name = "%s_%d" % (prefix, size)
            out = os.path.join(OUT_DIR, name + ".c")
            print("[%d/%d] %s.c ..." % (idx, total, name), flush=True)
            if not gen_one(conv, ttf, name, size, out):
                print("  ERROR generating %s" % name, file=sys.stderr)
                sys.exit(1)
            ok, val = check_fallback(out)
            if not ok:
                print("  ERROR: %s fallback=%s (must be NULL)" % (name, val),
                      file=sys.stderr)
                sys.exit(1)
            kb = os.path.getsize(out) // 1024
            print("  OK -> %s (%dKB)" % (os.path.basename(out), kb))

    print("\nDone. %d font files generated in %s" % (total, OUT_DIR))


if __name__ == "__main__":
    main()
