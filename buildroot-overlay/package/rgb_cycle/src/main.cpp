#include "display.h"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

volatile sig_atomic_t g_stop = 0;

struct Nv12Color {
    const char* name;
    uint8_t y;
    uint8_t u;
    uint8_t v;
};

constexpr Nv12Color kColors[] = {
    {"red", 76, 85, 255},
    {"green", 150, 44, 21},
    {"blue", 29, 255, 107},
};

void handle_signal(int) {
    g_stop = 1;
}

void fill_nv12(struct display_buffer* buffer, uint8_t y, uint8_t u, uint8_t v) {
    auto* y_plane = static_cast<uint8_t*>(buffer->map);
    auto* uv_plane = y_plane + buffer->stride * buffer->height;

    for (uint32_t row = 0; row < buffer->height; ++row) {
        std::memset(y_plane + row * buffer->stride, y, buffer->width);
    }

    for (uint32_t row = 0; row < buffer->height / 2; ++row) {
        auto* line = uv_plane + row * buffer->stride;
        for (uint32_t col = 0; col < buffer->width; col += 2) {
            line[col] = u;
            line[col + 1] = v;
        }
    }
}

}  // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    struct display* display = display_init(0);
    if (display == nullptr) {
        std::fprintf(stderr, "display_init failed\n");
        return 1;
    }

    if (display->conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
        std::fprintf(stderr, "connector is not DSI\n");
        display_exit(display);
        return 1;
    }

    display->drm_rotation = rotation_90;

    struct display_plane* plane = display_get_plane(display, DRM_FORMAT_NV12);
    if (plane == nullptr) {
        std::fprintf(stderr, "display_get_plane failed\n");
        display_exit(display);
        return 1;
    }

    plane->drm_rotation = rotation_90;

    struct display_buffer* buffer = display_allocate_buffer(plane, display->width, display->height);
    if (buffer == nullptr) {
        std::fprintf(stderr, "display_allocate_buffer failed\n");
        display_free_plane(plane);
        display_exit(display);
        return 1;
    }

    std::printf("RGB cycle start: %ux%u\n", display->width, display->height);
    std::fflush(stdout);

    while (!g_stop) {
        for (const auto& color : kColors) {
            if (g_stop) {
                break;
            }

            fill_nv12(buffer, color.y, color.u, color.v);
            display_wait_vsync(display);

            if (display_update_buffer(buffer, 0, 0) != 0 || display_commit(display) != 0) {
                std::fprintf(stderr, "failed to commit %s frame\n", color.name);
                g_stop = 1;
                break;
            }

            std::printf("showing %s\n", color.name);
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    display_wait_vsync(display);
    display_free_buffer(buffer);
    display_free_plane(plane);
    display_exit(display);
    return 0;
}
