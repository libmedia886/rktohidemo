#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_surface.h"

#include <linux/dma-buf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define HS_PAGE_W 1080
#define HS_PAGE_H 1920
#define HS_PAGE_STRIDE 1088
#define HS_PAGE_POOL 1
#define HS_PAGE_FPS 30
#define HS_GRP 120
#define HS_INPUT_POOL 6
#define HS_OUTPUT_POOL 7
#define HS_W 640
#define HS_H 640
#define HS_STRIDE 640
#define HS_FRAME_SIZE (HS_STRIDE * HS_H * 3 / 2)
#define LICENSE_PATH "/root/licence.dat"

#define HS_THRESHOLD_LOW 0.58f
#define HS_THRESHOLD_HIGH 0.86f
#define HS_KNEE 0.68f
#define HS_RATIO 0.08f
#define HS_STRENGTH 0.94f
#define HS_CHROMA_LOW 0.05f
#define HS_CHROMA_HIGH 0.52f

typedef struct {
    uint8_t *input;
    uint8_t *output;
    int processed;
    int module_ok;
    MEDIA_HIGHLIGHT_SUPPRESS_PERF perf;
} hs_page_ctx_t;

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void rgb_to_yuv(uint8_t r, uint8_t g, uint8_t b,
                       uint8_t *yy, uint8_t *uu, uint8_t *vv) {
    int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
    *yy = clamp_u8(y);
    *uu = clamp_u8(u);
    *vv = clamp_u8(v);
}

static int map_buffer(MEDIA_BUFFER buf, void **addr, size_t *size, int prot) {
    int fd = -1;
    if (!addr || !size) return -1;
    if (MEDIA_POOL_GetFd(buf, &fd, size) != 0 || fd < 0 || *size == 0) return -1;
    *addr = mmap(NULL, *size, prot, MAP_SHARED, fd, 0);
    if (*addr == MAP_FAILED) {
        *addr = NULL;
        return -1;
    }
    return 0;
}

static int copy_to_buffer(MEDIA_BUFFER buf, const uint8_t *src, size_t need) {
    void *addr = NULL;
    size_t size = 0;
    if (!src) return -1;
    if (map_buffer(buf, &addr, &size, PROT_READ | PROT_WRITE) != 0) return -1;
    if (size < need) {
        munmap(addr, size);
        return -1;
    }
    (void)MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    memcpy(addr, src, need);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    munmap(addr, size);
    return 0;
}

static int copy_from_buffer(MEDIA_BUFFER buf, uint8_t *dst, size_t need) {
    void *addr = NULL;
    size_t size = 0;
    if (!dst) return -1;
    if (map_buffer(buf, &addr, &size, PROT_READ) != 0) return -1;
    if (size < need) {
        munmap(addr, size);
        return -1;
    }
    (void)MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_READ);
    memcpy(dst, addr, need);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
    munmap(addr, size);
    return 0;
}

static void draw_circle_nv12(uint8_t *dst, int cx, int cy, int radius,
                             uint8_t r, uint8_t g, uint8_t b, int feather) {
    uint8_t yy, uu, vv;
    int r2 = radius * radius;
    int inner = radius - feather;
    int inner2 = inner > 0 ? inner * inner : 0;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    for (int y = cy - radius; y <= cy + radius; ++y) {
        if (y < 0 || y >= HS_H) continue;
        uint8_t *yp = dst + (size_t)y * HS_STRIDE;
        for (int x = cx - radius; x <= cx + radius; ++x) {
            if (x < 0 || x >= HS_W) continue;
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            int alpha = 255;
            if (d2 > inner2 && feather > 0) {
                alpha = 255 - ((d2 - inner2) * 255) / (r2 - inner2 + 1);
                if (alpha < 0) alpha = 0;
            }
            int v = yp[x];
            yp[x] = (uint8_t)((v * (255 - alpha) + yy * alpha) / 255);
        }
    }

    uint8_t *uv = dst + (size_t)HS_STRIDE * HS_H;
    for (int y = (cy - radius) / 2; y <= (cy + radius) / 2; ++y) {
        if (y < 0 || y >= HS_H / 2) continue;
        for (int x = (cx - radius) & ~1; x <= cx + radius; x += 2) {
            if (x < 0 || x + 1 >= HS_W) continue;
            int dx = x + 1 - cx;
            int dy = y * 2 + 1 - cy;
            if (dx * dx + dy * dy > r2) continue;
            uint8_t *p = uv + (size_t)y * HS_STRIDE + x;
            p[0] = uu;
            p[1] = vv;
        }
    }
}

static void fill_rect_rgb(uint8_t *dst, int x, int y, int w, int h,
                          uint8_t r, uint8_t g, uint8_t b) {
    uint8_t yy, uu, vv;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    page_surface_fill_rect_nv12(dst, HS_STRIDE, HS_W, HS_H, x, y, w, h, yy, uu, vv);
}

static void fill_highlight_input(uint8_t *dst, int frame) {
    fill_rect_rgb(dst, 0, 0, HS_W, HS_H, 30, 42, 48);
    for (int y = 0; y < HS_H; y += 40) {
        uint8_t shade = (uint8_t)(42 + (y * 64) / HS_H);
        fill_rect_rgb(dst, 0, y, HS_W, 20,
                      shade, (uint8_t)(shade + 18), (uint8_t)(shade + 28));
    }
    fill_rect_rgb(dst, 72, 118, 240, 330, 50, 86, 110);
    fill_rect_rgb(dst, 350, 172, 190, 260, 110, 70, 48);
    fill_rect_rgb(dst, 90, 470, 460, 54, 68, 92, 102);
    for (int i = 0; i < 5; ++i) {
        int x = 130 + i * 86 + ((frame * 3 + i * 17) % 18);
        int y = 218 + ((i * 41 + frame * 2) % 74);
        draw_circle_nv12(dst, x, y, 44 + i * 4, 255, 255, 246, 22);
    }
    draw_circle_nv12(dst, 430 + ((frame * 2) % 34), 242, 96, 255, 255, 255, 42);
    draw_circle_nv12(dst, 210, 362, 64, 255, 246, 224, 24);
    draw_circle_nv12(dst, 116, 180, 58, 255, 255, 255, 28);
    draw_circle_nv12(dst, 520, 492, 34, 238, 170, 70, 18);
}

static int process_highlight(hs_page_ctx_t *ctx) {
    if (!ctx || !ctx->input || !ctx->output || !ctx->module_ok) return -1;
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    if (MEDIA_POOL_GetBuffer(HS_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, ctx->input, HS_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("HIGHLIGHT_SUPPRESS", HS_GRP, "input", in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);

    if (MEDIA_HIGHLIGHT_SUPPRESS_GetFrame(HS_GRP, &out, 1000) != 0) return -1;
    int ret = copy_from_buffer(out, ctx->output, HS_FRAME_SIZE);
    MEDIA_HIGHLIGHT_SUPPRESS_ReleaseFrame(HS_GRP, out);
    if (ret == 0) {
        ctx->processed++;
        (void)MEDIA_HIGHLIGHT_SUPPRESS_GetLastPerf(HS_GRP, &ctx->perf);
    }
    return ret;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * HS_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * HS_STRIDE;
        for (int x = 0; x < dw; ++x) {
            int sx = x * HS_W / dw;
            drow[x] = srow[sx];
        }
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)HS_STRIDE * HS_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (HS_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * HS_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = ((x * HS_W / dw) & ~1);
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_hs_page(uint8_t *dst, int stride, int width, int height,
                         int frame, void *opaque) {
    hs_page_ctx_t *ctx = (hs_page_ctx_t *)opaque;
    char processed[48];
    char perf[64];
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);
    if (ctx && ctx->perf.gpu_enabled) {
        snprintf(perf, sizeof(perf), "GPU KERNEL %03d US",
                 (int)(ctx->perf.gpu_kernel_ms * 1000.0));
    } else if (ctx) {
        snprintf(perf, sizeof(perf), "CPU %03d US", (int)(ctx->perf.cpu_ms * 1000.0));
    } else {
        snprintf(perf, sizeof(perf), "PERF NA");
    }

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 8, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "HIGHLIGHT SUPPRESS", 7, 235, 108, 176);

    int pane = 600;
    int x = (width - pane) / 2;
    int top_y = 246;
    int bottom_y = 930;
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, top_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, bottom_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, top_y - 34,
                           "INPUT GLARE", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, x, bottom_y - 34,
                           "SOFT KNEE OUTPUT", 3, 220, 108, 176);
    if (ctx) {
        draw_nv12_scaled(dst, stride, width, height, x, top_y, pane, pane, ctx->input);
        draw_nv12_scaled(dst, stride, width, height, x, bottom_y, pane, pane, ctx->output);
    }

    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           "LOW 58 HIGH 86 KNEE 68 RATIO 08 STRENGTH 94",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1725,
                           perf, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1810,
                           "WHITE GLARE SOFTENED COLOR TARGETS PRESERVED",
                           2, 190, 144, 84);
    (void)frame;
}

static int setup_highlight_module(void) {
    if (MEDIA_POOL_Create(HS_INPUT_POOL, HS_FRAME_SIZE, 4) != 0) return -1;
    if (MEDIA_POOL_Create(HS_OUTPUT_POOL, HS_FRAME_SIZE, 4) != 0) {
        MEDIA_POOL_Destroy(HS_INPUT_POOL);
        return -1;
    }

    MEDIA_HIGHLIGHT_SUPPRESS_ATTR attr = {0};
    attr.width = HS_W;
    attr.height = HS_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.output_pool_id = HS_OUTPUT_POOL;
    attr.input_stride = HS_STRIDE;
    attr.output_stride = HS_STRIDE;
    attr.threshold_low = HS_THRESHOLD_LOW;
    attr.threshold_high = HS_THRESHOLD_HIGH;
    attr.knee = HS_KNEE;
    attr.ratio = HS_RATIO;
    attr.strength = HS_STRENGTH;
    attr.chroma_low = HS_CHROMA_LOW;
    attr.chroma_high = HS_CHROMA_HIGH;
    if (MEDIA_HIGHLIGHT_SUPPRESS_CreateGrp(HS_GRP, &attr) != 0 ||
        MEDIA_HIGHLIGHT_SUPPRESS_Start(HS_GRP) != 0) {
        MEDIA_HIGHLIGHT_SUPPRESS_DestroyGrp(HS_GRP);
        MEDIA_POOL_Destroy(HS_OUTPUT_POOL);
        MEDIA_POOL_Destroy(HS_INPUT_POOL);
        return -1;
    }
    return 0;
}

static void cleanup_highlight_module(int enabled) {
    if (!enabled) return;
    MEDIA_HIGHLIGHT_SUPPRESS_Stop(HS_GRP);
    MEDIA_HIGHLIGHT_SUPPRESS_DestroyGrp(HS_GRP);
    MEDIA_POOL_Destroy(HS_OUTPUT_POOL);
    MEDIA_POOL_Destroy(HS_INPUT_POOL);
}

int page_highlight_suppress_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    hs_page_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.input = (uint8_t *)malloc(HS_FRAME_SIZE);
    ctx.output = (uint8_t *)malloc(HS_FRAME_SIZE);
    if (!ctx.input || !ctx.output) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }

    if (page_surface_open(&surface, HS_PAGE_POOL, HS_PAGE_W, HS_PAGE_H,
                          HS_PAGE_STRIDE, HS_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }

    ctx.module_ok = setup_highlight_module() == 0;
    if (ctx.module_ok) set_tile_status("HIGHLIGHT_SUPPRESS", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("HIGHLIGHT_SUPPRESS standalone page module=%s. Ctrl+C to stop.\n",
           ctx.module_ok ? "live" : "fallback");

    int frame = 0;
    while (!running || *running) {
        fill_highlight_input(ctx.input, frame);
        if (process_highlight(&ctx) != 0) {
            memcpy(ctx.output, ctx.input, HS_FRAME_SIZE);
        }
        if (page_surface_send_frame(&surface, draw_hs_page, &ctx, frame) != 0) {
            usleep(1000);
            continue;
        }
        frame++;
        if ((frame % HS_PAGE_FPS) == 0) {
            printf("HIGHLIGHT_SUPPRESS frames=%d processed=%d low=%.2f high=%.2f ratio=%.2f strength=%.2f gpu=%d kernel=%.3f queue=%.3f cpu=%.3f\n",
                   frame, ctx.processed, HS_THRESHOLD_LOW, HS_THRESHOLD_HIGH,
                   HS_RATIO, HS_STRENGTH, ctx.perf.gpu_enabled,
                   ctx.perf.gpu_kernel_ms, ctx.perf.gpu_queue_ms, ctx.perf.cpu_ms);
        }
        usleep(1000000 / HS_PAGE_FPS);
    }

    cleanup_highlight_module(ctx.module_ok);
    page_surface_close(&surface);
    free(ctx.input);
    free(ctx.output);
    return 0;
}
