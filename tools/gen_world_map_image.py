#!/usr/bin/env python3
"""Generate the lcdui full-screen peer-map backdrop:
world_map_800x480.argb (raw ARGB8888, LVGL byte order B,G,R,A per pixel).

Same projection as tools/gen_world_map.py (equirectangular, lat 85..-60,
LON_CENTER=170 so the seam sits mid-Atlantic east of Greenland), rendered
at 800x480 for the Rockbase LCD. Dark theme colors match the web peer map:
  background #0b0e14, graticule rgba(120,130,150,0.35),
  land fill rgba(130,140,160,0.55), borders rgba(170,180,200,0.8).

Usage: gen_world_map_image.py [countries.geojson] [out.argb]
Default input: /tmp/ne_110m_countries.geojson
"""
import json
import sys

from PIL import Image, ImageDraw

W, H = 800, 480
LAT_MAX, LAT_MIN = 85, -60
LON_CENTER = 170

BG = (0x0b, 0x0e, 0x14)
GRATICULE = (120, 130, 150, 89)   # 0.35 * 255
LAND_FILL = (130, 140, 160, 140)  # 0.55 * 255
BORDER = (170, 180, 200, 204)     # 0.8 * 255


def norm_lon(lon):
    lo = LON_CENTER - 180
    return ((lon - lo) % 360) + lo


def px(lon):
    return (norm_lon(lon) - (LON_CENTER - 180)) / 360 * W


def py(lat):
    return (LAT_MAX - lat) / (LAT_MAX - LAT_MIN) * H


def ring_to_segments(coords):
    """Closed ring [[lon,lat],...] -> list of pixel segments, split at the
    map seam and dropping points outside the lat crop (same rules as
    gen_world_map.py)."""
    segs, cur, prev_nlon = [], [], None
    for c in coords:
        lon, lat = c[0], c[1]
        if lat > LAT_MAX + 1 or lat < LAT_MIN - 1:
            if len(cur) > 2:
                segs.append(cur)
            cur, prev_nlon = [], None
            continue
        nlon = norm_lon(lon)
        if prev_nlon is not None and abs(nlon - prev_nlon) > 180:
            if len(cur) > 2:
                segs.append(cur)
            cur = []
        cur.append((px(lon), py(lat)))
        prev_nlon = nlon
    if len(cur) > 2:
        segs.append(cur)
    return segs


def main():
    geo_path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ne_110m_countries.geojson"
    out_path = sys.argv[2] if len(sys.argv) > 2 else \
        "buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/usr/share/rockbase-lcdui/world_map_800x480.argb"

    with open(geo_path) as f:
        geo = json.load(f)

    base = Image.new("RGBA", (W, H), BG + (255,))
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    # Graticule: every 30 deg.
    for lon in range(-180, 181, 30):
        x = px(lon)
        draw.line([(x, 0), (x, H)], fill=GRATICULE, width=1)
    for lat in range(LAT_MAX, LAT_MIN - 1, -30):
        y = py(lat)
        draw.line([(0, y), (W, y)], fill=GRATICULE, width=1)

    # Landmasses: fill first, then stroke borders on top.
    fills, strokes = [], []
    for feature in geo.get("features", []):
        geom = feature.get("geometry", {})
        polys = geom.get("coordinates", [])
        if geom.get("type") == "Polygon":
            polys = [polys]
        for poly in polys:
            for ring in poly:
                for seg in ring_to_segments(ring):
                    pts = [(x, y) for x, y in seg]
                    fills.append(pts)
                    strokes.append(pts)
    for pts in fills:
        draw.polygon(pts, fill=LAND_FILL)
    for pts in strokes:
        draw.polygon(pts, outline=BORDER)

    img = Image.alpha_composite(base, layer)

    # LVGL ARGB8888 raw: per-pixel byte order is B, G, R, A (little-endian
    # ABGR32). Verified against existing boot-logo png/argb pairs.
    rgb = img.convert("RGB").tobytes()
    raw = bytearray(W * H * 4)
    raw[0::4] = rgb[2::3]  # B
    raw[1::4] = rgb[1::3]  # G
    raw[2::4] = rgb[0::3]  # R
    raw[3::4] = b"\xff" * (W * H)  # A

    with open(out_path, "wb") as f:
        f.write(raw)
    print(f"wrote {out_path} ({len(raw)} bytes = {W}x{H}x4)")


if __name__ == "__main__":
    main()
