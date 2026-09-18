/* drmshot — Rockbase (K230) LCD 截屏工具
 *
 * 从 DRM framebuffer 抓取当前屏幕内容，输出 PPM (P6)。
 * 用于 rockbase-lcdui 的闭环 UI 调试（改代码 → 编译 → 部署 → 截屏 → 目视验证）。
 *
 * 用法: drmshot [out.ppm]        默认 /tmp/screen.ppm
 *
 * 关键事实（踩坑记录，详见 .github/skills/rockbase-screenshot/SKILL.md）:
 *  - framebuffer 是 480x800 竖屏 (ARGB8888, stride=1920)，显示时旋转成 800x480 横屏
 *  - 跨进程访问 GEM 对象必须走 PRIME_HANDLE_TO_FD（本 sysroot 的 drm.h 没有
 *    标准 drm_gem_mmap 结构体，DRM_IOCTL_GEM_MMAP 不可用）
 *  - drmModeFB2 用 pitches[]/offsets[] 数组（不是 pitch/offset 单值）
 *  - 本地旋转: PIL rotate(-90, expand=True) 再 rotate(180) 得到正方向
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "/tmp/screen.ppm";

    int fd = open("/dev/dri/card0", O_RDONLY);
    if (fd < 0) { perror("open card0"); return 1; }

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { perror("GetResources"); close(fd); return 1; }

    /* 找 connected 的 connector（Rockbase 是 card0-DSI-1） */
    int conn_id = 0;
    for (int i = 0; i < res->count_connectors; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED) {
            conn_id = c->connector_id;
            drmModeFreeConnector(c);
            break;
        }
        if (c) drmModeFreeConnector(c);
    }
    if (!conn_id) { fprintf(stderr, "no connected connector\n"); return 1; }

    drmModeConnector *conn = drmModeGetConnector(fd, conn_id);
    if (!conn) { perror("GetConnector"); return 1; }

    /* connector -> encoder -> crtc -> buffer_id */
    uint32_t fb_id = 0;
    if (conn->encoder_id) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, conn->encoder_id);
        if (enc && enc->crtc_id) {
            drmModeCrtc *crtc = drmModeGetCrtc(fd, enc->crtc_id);
            if (crtc) { fb_id = crtc->buffer_id; drmModeFreeCrtc(crtc); }
            drmModeFreeEncoder(enc);
        } else if (enc) {
            drmModeFreeEncoder(enc);
        }
    }
    drmModeFreeConnector(conn);
    if (!fb_id) { fprintf(stderr, "no active framebuffer\n"); return 1; }

    drmModeFB2 *fb = drmModeGetFB2(fd, fb_id);
    if (!fb) { perror("GetFB2"); return 1; }

    int fb_w = fb->width, fb_h = fb->height;
    int stride = fb->pitches[0];
    uint32_t handle = fb->handles[0];
    uint32_t size = (uint32_t)stride * fb_h;

    /* 跨进程 GEM 访问: handle -> prime fd, 再 mmap prime fd（偏移 0） */
    struct drm_prime_handle prime = { .fd = -1, .handle = handle,
                                      .flags = DRM_CLOEXEC };
    if (ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime) < 0) {
        perror("PRIME_HANDLE_TO_FD");
        return 1;
    }
    void *map = mmap(NULL, size, PROT_READ, MAP_SHARED, prime.fd, 0);
    if (map == MAP_FAILED) { perror("mmap prime fd"); close(prime.fd); return 1; }

    printf("fb: %dx%d stride=%d\n", fb_w, fb_h, stride);

    /* 写 PPM (RGB, top-to-bottom)。ARGB8888 内存序: B G R A */
    FILE *f = fopen(out, "wb");
    if (!f) { perror("fopen"); return 1; }
    fprintf(f, "P6\n%d %d\n255\n", fb_w, fb_h);

    const uint8_t *base = (const uint8_t *)map + fb->offsets[0];
    for (int y = 0; y < fb_h; y++) {
        const uint8_t *row = base + (size_t)y * stride;
        for (int x = 0; x < fb_w; x++) {
            fputc(row[x * 4 + 2], f);  /* R */
            fputc(row[x * 4 + 1], f);  /* G */
            fputc(row[x * 4 + 0], f);  /* B */
        }
    }
    fclose(f);
    munmap(map, size);
    close(prime.fd);
    drmModeFreeFB2(fb);
    drmModeFreeResources(res);
    close(fd);
    printf("OK: %s\n", out);
    return 0;
}
