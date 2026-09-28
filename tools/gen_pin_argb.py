#!/usr/bin/env python3
"""Generate the lcdui peer-map water-drop pin markers as raw ARGB8888 images.

The web dashboard renders the same pins from PNG files under /root/web/img/,
but the LVGL side cannot decode PNG at runtime, so we pre-render each pin to a
raw ARGB8888 blob (LVGL byte order B,G,R,A per pixel, little-endian ABGR32)
under /usr/share/rockbase-lcdui/. The two front-ends therefore keep *independent*
icon resources (web = PNG, lcdui = ARGB) even though they are generated from the
same source artwork.

Each pin is cropped to its opaque bounding box and scaled to a target height;
the drop tip sits at the bottom-centre of the cropped image, which the overlay
anchors on the projected geo point.

Usage: gen_pin_argb.py [web_img_dir] [out_dir]
Defaults:
  web_img_dir = buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/img
  out_dir     = buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/usr/share/rockbase-lcdui
"""
import os
import sys

from PIL import Image

# role -> (source png name, output argb name, canvas size in px).
# Each pin is rendered into a fixed square canvas (size x size) so the C++
# side can hard-code the dimensions: the drop is scaled to fill the height,
# centred horizontally, with its tip on the bottom-centre of the canvas.
PINS = [
    ("self", "pin_classic_red.png", "pin_self.argb", 48),
    ("inbound", "pin_classic_blue.png", "pin_inbound.argb", 36),
    ("outbound", "pin_classic_orange_yellow.png", "pin_outbound.argb", 36),
]


def to_argb(rgba: Image.Image) -> bytes:
    """RGBA PIL image -> raw LVGL ARGB8888 (B,G,R,A per pixel)."""
    w, h = rgba.size
    r, g, b, a = rgba.split()
    rb = r.tobytes()
    gb = g.tobytes()
    bb = b.tobytes()
    ab = a.tobytes()
    raw = bytearray(w * h * 4)
    raw[0::4] = bb  # B
    raw[1::4] = gb  # G
    raw[2::4] = rb  # R
    raw[3::4] = ab  # A
    return bytes(raw)


def main():
    web_img = sys.argv[1] if len(sys.argv) > 1 else (
        "buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/img")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else (
        "buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/usr/share/rockbase-lcdui")
    os.makedirs(out_dir, exist_ok=True)

    for role, src_name, out_name, size in PINS:
        src = os.path.join(web_img, src_name)
        im = Image.open(src).convert("RGBA")
        # Crop to the opaque content so the tip lands on the bottom edge.
        box = im.getbbox()
        if box is None:
            raise SystemExit(f"{src_name}: fully transparent, nothing to crop")
        im = im.crop(box)
        cw, ch = im.size
        # Scale to fill the canvas height, then centre in a square canvas.
        new_w = max(1, round(cw * size / ch))
        im = im.resize((new_w, size), Image.LANCZOS)
        canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        canvas.paste(im, ((size - new_w) // 2, 0), im)
        out_path = os.path.join(out_dir, out_name)
        with open(out_path, "wb") as f:
            f.write(to_argb(canvas))
        print(f"{role}: {src_name} -> {out_name} ({size}x{size} canvas, "
              f"pin {new_w}x{size}, {os.path.getsize(out_path)} bytes)")


if __name__ == "__main__":
    main()
