#!/usr/bin/env python3
"""Pre-render a short boot video into a raw YUV420P frame sequence for the
Rockbase lcdui boot-logo (Plan A: offline pre-render, zero runtime deps).

The K230 board has no video decoder in the rootfs, so instead of shipping an
mp4 we decode it here on the host and emit a single contiguous YUV420P file
plus a tiny meta file. At boot, rockbase-lcdui reads the frames, converts
YUV420P -> ARGB8888, software-rotates 800x480 landscape -> 480x800 portrait
(same as the LVGL flush path) and commits to the DRM plane.

Output layout (under /usr/share/rockbase-lcdui/boot-video/):
    frames.yuv   W*H*3 bytes per frame, YUV420P (planar Y then U then V)
    meta.txt     "<width> <height> <frame_count> <fps>\n"

Usage:
    gen_boot_video.py [src.mp4] [out_dir]

Defaults:
    src.mp4 = buildroot-overlay/board/canaan/k230_rockbase/rootfs_overlay/
              usr/share/rockbase-lcdui/boot-video-src.mp4
    out_dir = .../usr/share/rockbase-lcdui/boot-video

The video is scaled to exactly 800x480 (the logical UI size). The source is
stretched to fill the full canvas (no letterboxing), so a slight distortion is
accepted when the source aspect ratio differs from 5:3.
"""

import os
import subprocess
import sys

W = 800
H = 480
# Cap the number of frames so the boot hold stays short and the .yuv stays
# small (~576KB/frame). 8s @ 15fps = 120 frames ~= 69MB.
MAX_FRAMES = 130
TARGET_FPS = 15


def default_paths():
    base = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "..",
        "buildroot-overlay",
        "board",
        "canaan",
        "k230_rockbase",
        "rootfs_overlay",
        "usr",
        "share",
        "rockbase-lcdui",
    )
    src = os.path.normpath(os.path.join(base, "boot-video-src.mp4"))
    out = os.path.normpath(os.path.join(base, "boot-video"))
    return src, out


def probe_duration(path):
    """Return video duration in seconds via ffprobe."""
    out = subprocess.check_output(
        [
            "ffprobe", "-v", "error",
            "-select_streams", "v:0",
            "-show_entries", "stream=duration",
            "-of", "default=noprint_wrappers=1:nokey=1",
            path,
        ],
        text=True,
    ).strip()
    if not out:
        # Some containers only expose format-level duration.
        out = subprocess.check_output(
            [
                "ffprobe", "-v", "error",
                "-show_entries", "format=duration",
                "-of", "default=noprint_wrappers=1:nokey=1",
                path,
            ],
            text=True,
        ).strip()
    return float(out)


def main():
    args = sys.argv[1:]
    if len(args) >= 2:
        src, out_dir = args[0], args[1]
    elif len(args) == 1:
        d_src, d_out = default_paths()
        src, out_dir = args[0], d_out
    else:
        src, out_dir = default_paths()

    if not os.path.isfile(src):
        sys.exit(f"source video not found: {src}")

    os.makedirs(out_dir, exist_ok=True)
    frames_path = os.path.join(out_dir, "frames.yuv")
    meta_path = os.path.join(out_dir, "meta.txt")

    duration = probe_duration(src)
    n_frames = int(round(duration * TARGET_FPS))
    if n_frames < 1:
        n_frames = 1
    if n_frames > MAX_FRAMES:
        print(f"note: capping {n_frames} frames to {MAX_FRAMES}")
        n_frames = MAX_FRAMES

    # scale to exactly 800x480 (force stretch, no letterbox) -> force fps ->
    # emit planar yuv420p. -frames:v caps the count.
    vf = (
        f"scale={W}:{H},"
        f"fps={TARGET_FPS},format=yuv420p"
    )
    cmd = [
        "ffmpeg", "-y", "-v", "error",
        "-i", src,
        "-vf", vf,
        "-frames:v", str(n_frames),
        "-f", "rawvideo",
        "-pix_fmt", "yuv420p",
        frames_path,
    ]
    print("running:", " ".join(cmd))
    subprocess.check_call(cmd)

    # YUV420P: Y plane is W*H, U and V are each W*H/4 -> 1.5 bytes/pixel.
    expected = n_frames * W * H * 3 // 2
    actual = os.path.getsize(frames_path)
    if actual != expected:
        print(f"warning: frames.yuv is {actual} bytes, expected {expected}")

    with open(meta_path, "w") as f:
        f.write(f"{W} {H} {n_frames} {TARGET_FPS}\n")

    print(
        f"wrote {frames_path} ({actual} bytes = {n_frames} frames "
        f"@ {W}x{H} yuv420p, {TARGET_FPS}fps)"
    )
    print(f"wrote {meta_path}")


if __name__ == "__main__":
    main()
