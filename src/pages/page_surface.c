#include "page_surface.h"

#include "media_api.h"

#include <linux/dma-buf.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static uint8_t font5x7(char c, int row) {
    static const uint8_t digit[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
        {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,2,12},
    };
    static const uint8_t upper[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    };
    if (row < 0 || row >= 7) return 0;
    if (c >= '0' && c <= '9') return digit[c - '0'][row];
    if (c >= 'A' && c <= 'Z') return upper[c - 'A'][row];
    if (c >= 'a' && c <= 'z') return upper[c - 'a'][row];
    if (c == '-') return row == 3 ? 31 : 0;
    if (c == '/') {
        int col = 6 - row;
        if (col > 4) col = 4;
        if (col < 0) col = 0;
        return (uint8_t)(1u << col);
    }
    if (c == ':') return (row == 2 || row == 4) ? 4 : 0;
    return 0;
}

int page_surface_open(page_surface_t *surface, int pool_id, int width, int height,
                      int stride, int fps, int buffer_count, const char *license_path) {
    if (!surface || width <= 0 || height <= 0 || stride < width ||
        fps <= 0 || buffer_count <= 0) {
        return -1;
    }
    memset(surface, 0, sizeof(*surface));
    surface->pool_id = pool_id;
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->fps = fps;
    surface->buffer_count = buffer_count;
    surface->frame_size = (size_t)stride * height * 3 / 2;

    if (MEDIA_SYS_Init() != 0) {
        fprintf(stderr, "page surface: MEDIA_SYS_Init failed\n");
        return -1;
    }
    surface->sys_ok = 1;
    if (license_path && license_path[0]) {
        MEDIA_SYS_SetLicense(license_path);
    }

    if (MEDIA_POOL_Create(pool_id, surface->frame_size, buffer_count) != 0) {
        fprintf(stderr, "page surface: display pool create failed\n");
        page_surface_close(surface);
        return -1;
    }
    surface->pool_ok = 1;

    MEDIA_VO_ATTR vo = {0};
    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = width;
    vo.height = height;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, 0, 0, width, height, stride, 4,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0 ||
        MEDIA_VO_Start(0, 0) != 0) {
        fprintf(stderr, "page surface: VO setup failed\n");
        page_surface_close(surface);
        return -1;
    }
    surface->vo_ok = 1;
    return 0;
}

void page_surface_close(page_surface_t *surface) {
    if (!surface) return;
    if (surface->vo_ok) {
        MEDIA_VO_Stop(0, 0);
        MEDIA_VO_DestroyChn(0, 0);
        surface->vo_ok = 0;
    }
    if (surface->pool_ok) {
        MEDIA_POOL_Destroy(surface->pool_id);
        surface->pool_ok = 0;
    }
    if (surface->sys_ok) {
        MEDIA_SYS_Exit();
        surface->sys_ok = 0;
    }
}

int page_surface_send_frame(page_surface_t *surface, page_surface_draw_fn draw,
                            void *opaque, int frame) {
    if (!surface || !draw || !surface->pool_ok || !surface->vo_ok) return -1;

    MEDIA_BUFFER buf = {-1, -1};
    if (MEDIA_POOL_GetBuffer(surface->pool_id, &buf) != 0) return -1;

    size_t size = MEDIA_POOL_GetSize(buf);
    if (size == 0) size = surface->frame_size;
    void *addr = MEDIA_POOL_GetVaddr(buf);
    int mapped = 0;
    if (!addr) {
        int fd = -1;
        if (MEDIA_POOL_GetFd(buf, &fd, &size) == 0 && fd >= 0) {
            addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (addr == MAP_FAILED) {
                addr = NULL;
            } else {
                mapped = 1;
            }
        }
    }
    if (!addr) {
        MEDIA_POOL_PutBuffer(buf);
        return -1;
    }

    (void)MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    draw((uint8_t *)addr, surface->stride, surface->width, surface->height, frame, opaque);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    if (mapped) munmap(addr, size);

    if (MEDIA_SYS_SendFrame("VO", 0, "input0", buf, 1000) != 0) {
        MEDIA_POOL_PutBuffer(buf);
        return -1;
    }
    return 0;
}

void page_surface_fill_rect_nv12(uint8_t *dst, int stride, int width, int height,
                                 int x, int y, int w, int h,
                                 uint8_t yy, uint8_t uu, uint8_t vv) {
    if (!dst || w <= 0 || h <= 0 || width <= 0 || height <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > width) w = width - x;
    if (y + h > height) h = height - y;
    if (w <= 0 || h <= 0) return;

    for (int row = 0; row < h; ++row) {
        memset(dst + (size_t)(y + row) * stride + x, yy, (size_t)w);
    }

    uint8_t *uv = dst + (size_t)stride * height;
    int uv_x = x & ~1;
    int uv_w = (w + (x - uv_x) + 1) & ~1;
    int uv_y = y / 2;
    int uv_h = (h + 1) / 2;
    for (int row = 0; row < uv_h; ++row) {
        uint8_t *p = uv + (size_t)(uv_y + row) * stride + uv_x;
        for (int col = 0; col < uv_w; col += 2) {
            p[col] = uu;
            p[col + 1] = vv;
        }
    }
}

void page_surface_draw_text(uint8_t *dst, int stride, int width, int height,
                            int x, int y, const char *text, int scale,
                            uint8_t yy, uint8_t uu, uint8_t vv) {
    if (!dst || !text || scale <= 0) return;
    int cx = x;
    for (const char *p = text; *p; ++p) {
        if (*p == ' ') {
            cx += 4 * scale;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = font5x7(*p, row);
            for (int col = 0; col < 5; ++col) {
                if (bits & (1u << (4 - col))) {
                    page_surface_fill_rect_nv12(dst, stride, width, height,
                                                cx + col * scale, y + row * scale,
                                                scale, scale, yy, uu, vv);
                }
            }
        }
        cx += 6 * scale;
    }
}
