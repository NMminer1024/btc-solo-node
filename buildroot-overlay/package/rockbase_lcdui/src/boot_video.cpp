#include "boot_video.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace bootvideo {
namespace {

bool read_whole_file(const char* path, std::vector<uint8_t>& out) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    out.clear();
    uint8_t chunk[65536];
    for (;;) {
        const ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        if (n == 0) break;
        out.insert(out.end(), chunk, chunk + n);
    }
    close(fd);
    return true;
}

inline int clamp8(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

}  // namespace

bool Player::load(const char* dir) {
    ready = false;
    frames.clear();

    char meta_path[512];
    char frames_path[512];
    std::snprintf(meta_path, sizeof(meta_path), "%s/meta.txt", dir);
    std::snprintf(frames_path, sizeof(frames_path), "%s/frames.yuv", dir);

    std::string meta;
    {
        int fd = open(meta_path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            std::fprintf(stderr, "rockbase-lcdui: boot-video open %s failed\n", meta_path);
            return false;
        }
        char buf[256] = {};
        const ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) {
            std::fprintf(stderr, "rockbase-lcdui: boot-video read %s failed\n", meta_path);
            return false;
        }
        meta.assign(buf, static_cast<size_t>(n));
    }

    int w = 0, h = 0, count = 0, fps = 0;
    if (std::sscanf(meta.c_str(), "%d %d %d %d", &w, &h, &count, &fps) != 4) {
        std::fprintf(stderr, "rockbase-lcdui: boot-video bad meta: %s\n", meta.c_str());
        return false;
    }
    if (w <= 0 || h <= 0 || count <= 0 || fps <= 0) {
        std::fprintf(stderr, "rockbase-lcdui: boot-video invalid meta values\n");
        return false;
    }

    // YUV420P: Y plane is w*h, U and V are each w*h/4 -> 1.5 bytes/pixel.
    const size_t frame_bytes = static_cast<size_t>(w) * h * 3 / 2;
    const size_t total = frame_bytes * static_cast<size_t>(count);
    if (!read_whole_file(frames_path, frames) || frames.size() < total) {
        std::fprintf(stderr, "rockbase-lcdui: boot-video frames.yuv too small (%zu < %zu)\n",
                     frames.size(), total);
        return false;
    }

    width = w;
    height = h;
    frame_count = count;
    this->fps = fps;
    ready = true;
    std::printf("rockbase-lcdui: boot-video loaded %dx%d %d frames @ %dfps\n",
                w, h, count, fps);
    return true;
}

void Player::render_frame(int index, uint32_t* dst, uint32_t stride_px) const {
    if (index < 0 || index >= frame_count) index = 0;
    const int w = width;
    const int h = height;
    const size_t frame_bytes = static_cast<size_t>(w) * h * 3 / 2;  // YUV420P
    const uint8_t* base = frames.data() + static_cast<size_t>(index) * frame_bytes;
    const uint8_t* y_plane = base;
    const uint8_t* u_plane = base + static_cast<size_t>(w) * h;
    const uint8_t* v_plane = u_plane + static_cast<size_t>(w) * h / 4;
    const int half_w = w / 2;

    for (int sy = 0; sy < h; ++sy) {
        const int uy = sy >> 1;
        for (int sx = 0; sx < w; ++sx) {
            const int uvx = sx >> 1;
            const int y = y_plane[sy * w + sx];
            const int u = u_plane[uy * half_w + uvx];
            const int v = v_plane[uy * half_w + uvx];

            // BT.601 YUV -> RGB.
            const int c = y - 16;
            const int diff_u = u - 128;
            const int diff_v = v - 128;
            const int r = clamp8((298 * c + 409 * diff_v + 128) >> 8);
            const int g = clamp8((298 * c - 100 * diff_u - 208 * diff_v + 128) >> 8);
            const int b = clamp8((298 * c + 516 * diff_u + 128) >> 8);

            // LVGL ARGB8888: memory byte order is B,G,R,A (little-endian ABGR32),
            // so the uint32_t value is 0xAARRGGBB (LSB byte = B).
            const uint32_t pixel = 0xFF000000u | (static_cast<uint32_t>(r) << 16) |
                                  (static_cast<uint32_t>(g) << 8) |
                                  static_cast<uint32_t>(b);

            // Same 800x480 landscape -> 480x800 portrait CCW rotation as the
            // LVGL flush path: physical_x = H-1-sy, physical_y = sx.
            const uint32_t physical_x = static_cast<uint32_t>(h - 1 - sy);
            const uint32_t physical_y = static_cast<uint32_t>(sx);
            dst[physical_y * stride_px + physical_x] = pixel;
        }
    }
}

}  // namespace bootvideo
