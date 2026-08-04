#include "display.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <thead.h>

#include "lvgl/lvgl.h"

namespace {

constexpr uint32_t kUiWidth = 800;
constexpr uint32_t kUiHeight = 480;
constexpr const char* kBootLogoPath = "A:/usr/share/rockbase-lcdui/boot-logo-800x480.png";
constexpr const char* kDashboardPath = "A:/usr/share/rockbase-lcdui/bitcoin_dashboard_800x480.png";
volatile sig_atomic_t g_stop = 0;
display* g_display = nullptr;
display_plane* g_plane = nullptr;
display_buffer* g_buffer = nullptr;

uint32_t tick_ms() {
    timeval tv {};
    gettimeofday(&tv, nullptr);
    return static_cast<uint32_t>(tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL);
}

void handle_signal(int) {
    g_stop = 1;
}

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (g_buffer == nullptr || g_buffer->map == nullptr || area == nullptr || px_map == nullptr) {
        lv_display_flush_ready(disp);
        return;
    }

    const uint32_t area_w = static_cast<uint32_t>(area->x2 - area->x1 + 1);
    const auto* src = reinterpret_cast<const uint32_t*>(px_map);
    auto* dst = static_cast<uint32_t*>(g_buffer->map);
    const uint32_t dst_stride_px = g_buffer->stride / sizeof(uint32_t);

    for (int32_t sy = area->y1; sy <= area->y2; ++sy) {
        if (sy < 0 || sy >= static_cast<int32_t>(kUiHeight)) {
            continue;
        }
        for (int32_t sx = area->x1; sx <= area->x2; ++sx) {
            if (sx < 0 || sx >= static_cast<int32_t>(kUiWidth)) {
                continue;
            }

            const uint32_t src_x = static_cast<uint32_t>(sx - area->x1);
            const uint32_t src_y = static_cast<uint32_t>(sy - area->y1);
            const uint32_t pixel = src[src_y * area_w + src_x];

            /* 800x480 logical landscape -> 480x800 physical portrait. */
            const uint32_t dx = kUiHeight - 1U - static_cast<uint32_t>(sy);
            const uint32_t dy = static_cast<uint32_t>(sx);
            if (dx < g_buffer->width && dy < g_buffer->height) {
                dst[dy * dst_stride_px + dx] = pixel;
            }
        }
    }

    thead_csi_dcache_clean_invalid_range(g_buffer->map, g_buffer->size);
    display_wait_vsync(g_display);
    if (display_update_buffer(g_buffer, 0, 0) != 0 || display_commit(g_display) != 0) {
        std::fprintf(stderr, "rockbase-lcdui: display commit failed\n");
    }

    lv_display_flush_ready(disp);
}

bool init_display() {
    g_display = display_init(0);
    if (g_display == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: display_init failed\n");
        return false;
    }

    if (g_display->conn->connector_type != DRM_MODE_CONNECTOR_DSI) {
        std::fprintf(stderr, "rockbase-lcdui: connector is not DSI\n");
        return false;
    }

    g_display->drm_rotation = rotation_0;
    g_plane = display_get_plane(g_display, DRM_FORMAT_ARGB8888);
    if (g_plane == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: ARGB8888 plane unavailable\n");
        return false;
    }

    g_plane->drm_rotation = rotation_0;
    g_buffer = display_allocate_buffer(g_plane, g_display->width, g_display->height);
    if (g_buffer == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: display buffer allocation failed\n");
        return false;
    }

    auto* pixels = static_cast<uint32_t*>(g_buffer->map);
    const uint32_t pixel_count = g_buffer->size / sizeof(uint32_t);
    for (uint32_t i = 0; i < pixel_count; ++i) {
        pixels[i] = 0xff000000;
    }
    std::printf("rockbase-lcdui: panel=%ux%u buffer=%ux%u stride=%u size=%u fourcc=ARGB8888 rotation=software-ccw\n",
                g_display->width, g_display->height, g_buffer->width, g_buffer->height,
                g_buffer->stride, g_buffer->size);
    return true;
}

void cleanup_display() {
    if (g_buffer != nullptr) {
        display_free_buffer(g_buffer);
        g_buffer = nullptr;
    }
    if (g_plane != nullptr) {
        display_free_plane(g_plane);
        g_plane = nullptr;
    }
    if (g_display != nullptr) {
        display_exit(g_display);
        g_display = nullptr;
    }
}

lv_obj_t* add_label(lv_obj_t* parent, const char* text, int32_t x, int32_t y,
                    const lv_font_t* font, lv_color_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

void build_placeholder_ui() {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10161d), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* card = lv_obj_create(screen);
    lv_obj_set_size(card, 744, 392);
    lv_obj_set_pos(card, 28, 44);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x17232d), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xc9963e), 0);
    lv_obj_set_style_pad_all(card, 28, 0);

    add_label(card, "NM", 30, 24, &lv_font_montserrat_36, lv_color_hex(0xc9963e));
    add_label(card, "Solo BTC Node", 30, 78, &lv_font_montserrat_32, lv_color_hex(0xf2dfb3));
    add_label(card, "LCD UI framework ready", 32, 148, &lv_font_montserrat_22, lv_color_hex(0xb9c2c6));
    add_label(card, "Canvas: 800 x 480 landscape", 32, 198, &lv_font_montserrat_18, lv_color_hex(0x7f8b90));
    add_label(card, "Linux-side boot logo rendering disabled", 32, 236, &lv_font_montserrat_16, lv_color_hex(0x7f8b90));

    lv_obj_t* bar = lv_obj_create(card);
    lv_obj_set_size(bar, 620, 8);
    lv_obj_set_pos(bar, 32, 326);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xc9963e), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
}

void warm_file_cache(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return;
    }

    char buf[16384];
    while (read(fd, buf, sizeof(buf)) > 0) {
    }
    close(fd);
}

void build_dashboard_transition(lv_display_t* disp) {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x05080c), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t* dashboard = lv_image_create(screen);
    lv_image_set_src(dashboard, kDashboardPath);
    lv_obj_set_pos(dashboard, 0, 0);
    lv_obj_set_style_opa(dashboard, LV_OPA_COVER, 0);

    lv_refr_now(disp);
}

}  // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    if (!init_display()) {
        cleanup_display();
        return 1;
    }

    lv_init();
    lv_tick_set_cb(tick_ms);

    lv_display_t* disp = lv_display_create(kUiWidth, kUiHeight);
    if (disp == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: lv_display_create failed\n");
        cleanup_display();
        return 1;
    }

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_flush_cb(disp, flush_cb);

    const size_t draw_buf_size = kUiWidth * kUiHeight * sizeof(uint32_t);
    void* draw_buf = std::malloc(draw_buf_size);
    if (draw_buf == nullptr) {
        std::fprintf(stderr, "rockbase-lcdui: draw buffer allocation failed\n");
        cleanup_display();
        return 1;
    }
    lv_display_set_buffers(disp, draw_buf, nullptr, draw_buf_size, LV_DISPLAY_RENDER_MODE_FULL);

    if (access("/usr/share/rockbase-lcdui/bitcoin_dashboard_800x480.png", R_OK) == 0 &&
        access("/usr/share/rockbase-lcdui/boot-logo-800x480.png", R_OK) == 0) {
        warm_file_cache("/usr/share/rockbase-lcdui/boot-logo-800x480.png");
        warm_file_cache("/usr/share/rockbase-lcdui/bitcoin_dashboard_800x480.png");
        build_dashboard_transition(disp);
    } else {
        build_placeholder_ui();
    }
    lv_refr_now(disp);

    std::printf("rockbase-lcdui: running 800x480 landscape dashboard\n");
    std::fflush(stdout);

    while (!g_stop) {
        lv_timer_handler();
        usleep(20000);
    }

    std::free(draw_buf);
    cleanup_display();
    return 0;
}
