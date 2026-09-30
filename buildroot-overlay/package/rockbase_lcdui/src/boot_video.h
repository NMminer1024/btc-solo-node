#pragma once
// Boot-video player (Plan A: offline pre-rendered YUV420P frame sequence).
//
// The K230 rootfs ships no video decoder, so the boot "video" is a short clip
// decoded on the host by tools/gen_boot_video.py into a contiguous YUV420P
// file plus a one-line meta file. At boot, rockbase-lcdui loads the frames,
// converts each YUV420P frame to ARGB8888 and software-rotates it
// 800x480 landscape -> 480x800 portrait (identical transform to the LVGL
// flush path), then commits the DRM plane. No new runtime dependencies.
//
// Expected files under the asset dir (/usr/share/rockbase-lcdui/boot-video):
//   frames.yuv   W*H*3 bytes per frame, planar YUV420P
//   meta.txt     "<width> <height> <frame_count> <fps>\n"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace bootvideo {

struct Player {
    bool ready = false;
    int width = 0;      // logical width  (800)
    int height = 0;     // logical height (480)
    int frame_count = 0;
    int fps = 15;
    std::vector<uint8_t> frames;  // contiguous YUV420P frames

    // Load frames.yuv + meta.txt from `dir`. Returns true if a playable
    // sequence was found.
    bool load(const char* dir);

    // Convert frame `index` (YUV420P) to ARGB8888 and write it into `dst`
    // using the same 800x480 -> 480x800 CCW rotation as the LVGL flush path.
    // `dst` is the physical framebuffer: 480 columns x 800 rows.
    // `stride_px` is the physical pixels per row (== 480 for our panel).
    void render_frame(int index, uint32_t* dst, uint32_t stride_px) const;
};

}  // namespace bootvideo
