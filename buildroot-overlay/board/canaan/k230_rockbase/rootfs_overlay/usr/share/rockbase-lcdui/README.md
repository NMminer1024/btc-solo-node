# Rockbase LCD UI Assets

Place the 800x480 landscape boot logo here:

`boot-logo-800x480.png`

The LCD UI process will use this directory at runtime:

`/usr/share/rockbase-lcdui`

## Boot video (optional, replaces the static logo)

To show a short video instead of the static boot logo, drop your source clip
at:

`boot-video-src.mp4`

then run the offline pre-render tool on the host (needs `ffmpeg`):

```bash
python3 tools/gen_boot_video.py
```

This decodes the clip into a contiguous YUV420P frame sequence under
`boot-video/` (`frames.yuv` + `meta.txt`). The K230 rootfs ships no video
decoder, so the board plays the pre-rendered frames at boot (Plan A). The
video is stretched to fill 800x480 (slight distortion accepted) and capped
at 15fps / 130 frames (~75MB max, ~8s). If `boot-video/` is absent, the
static logo is shown instead.
