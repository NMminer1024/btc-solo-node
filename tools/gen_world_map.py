#!/usr/bin/env python3
"""Generate the dot-grid world map embedded in web/world-map.js.

Source: Natural Earth 110m land (public domain).
Output: a base64 bitfield, one bit per 1-degree cell, MSB-first,
        45 bytes per row (360 cols / 8), 171 rows (lat 85..-85).
Bit set = land.

Usage:
    curl -sL -o /tmp/ne110_land.json \
      https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_land.geojson
    python3 tools/gen_world_map.py /tmp/ne110_land.json buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/root/web/world-map.js
"""
import sys
import json
import base64

GEO_PATH = sys.argv[1] if len(sys.argv) > 1 else "/tmp/ne110_land.json"
OUT_JS = sys.argv[2] if len(sys.argv) > 2 else None


def point_in_ring(x, y, ring):
    """Ray-casting point-in-polygon test."""
    n = len(ring)
    inside = False
    j = n - 1
    for i in range(n):
        xi, yi = ring[i]
        xj, yj = ring[j]
        if ((yi > y) != (yj > y)) and (x < (xj - xi) * (y - yi) / (yj - yi) + xi):
            inside = not inside
        j = i
    return inside


def main():
    geo = json.load(open(GEO_PATH))
    polys = []  # (outer_ring, [hole_rings])
    for f in geo["features"]:
        g = f["geometry"]
        if g["type"] == "Polygon":
            polys.append((g["coordinates"][0], g["coordinates"][1:]))
        elif g["type"] == "MultiPolygon":
            for mp in g["coordinates"]:
                polys.append((mp[0], mp[1:]))

    def on_land(lon, lat):
        for outer, holes in polys:
            if point_in_ring(lon, lat, outer):
                if any(point_in_ring(lon, lat, h) for h in holes):
                    continue
                return True
        return False

    W = 360  # lon -180..179
    H = 171  # lat 85..-85
    bits = []
    for lat in range(85, -86, -1):
        row = 0
        for lon in range(-180, 180):
            if on_land(lon + 0.5, lat + 0.5):
                row |= (1 << (W - 1 - (lon + 180)))
        bits.append(row)

    row_bytes = (W + 7) // 8  # 45
    data = bytearray()
    for row in bits:
        data += row.to_bytes(row_bytes, "big")
    b64 = base64.b64encode(bytes(data)).decode()

    total = sum(bin(r).count("1") for r in bits)
    print(f"rows={H} cols={W} bytes={len(data)} (expect {H*row_bytes}) "
          f"b64_len={len(b64)} land_cells={total}")

    if OUT_JS:
        js = (
            "/* Auto-generated dot-grid world map (1-degree resolution).\n"
            " * Source: Natural Earth 110m land (public domain).\n"
            " * Regenerate: python3 tools/gen_world_map.py\n"
            f" * Layout: {H} rows (lat 85..-85) x {W} cols (lon -180..179),\n"
            " * one bit per cell, MSB-first, 45 bytes per row, base64-encoded.\n"
            " * Bit set = land. */\n"
            "window.WORLD_MAP = {\n"
            f"  rows: {H},\n"
            f"  cols: {W},\n"
            f'  data: "{b64}"\n'
            "};\n"
        )
        open(OUT_JS, "w").write(js)
        print(f"wrote {OUT_JS}")
    else:
        print(b64)


if __name__ == "__main__":
    main()
