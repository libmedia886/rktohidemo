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

#define DV_PAGE_W 1080
#define DV_PAGE_H 1920
#define DV_PAGE_STRIDE 1088
#define DV_PAGE_POOL 1
#define DV_PAGE_FPS 30
#define DV_INPUT0_POOL 10
#define DV_INPUT1_POOL 11
#define DV_SBS_OUTPUT_POOL 12
#define DV_LBL_OUTPUT_POOL 13
#define DV_SBS_GRP 71
#define DV_LBL_GRP 72
#define DV_W 640
#define DV_H 640
#define DV_STRIDE (DV_W * 3)
#define DV_FRAME_SIZE (DV_STRIDE * DV_H)
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *input0;
    uint8_t *input1;
    uint8_t *sbs;
    uint8_t *lbl;
    int processed;
    int module_ok;
} dualview_ctx_t;

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

static int send_copied_frame(int grp, const char *port, int pool, const uint8_t *src) {
    MEDIA_BUFFER in = {-1, -1};
    if (MEDIA_POOL_GetBuffer(pool, &in) != 0) return -1;
    if (copy_to_buffer(in, src, DV_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("DUALVIEW", grp, port, in, 1000) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);
    return 0;
}

static int setup_dualview_one(int grp, int output_pool, int mode) {
    MEDIA_DUALVIEW_ATTR attr = {0};
    attr.input_width = DV_W;
    attr.input_height = DV_H;
    attr.input_stride = DV_STRIDE;
    attr.output_width = DV_W;
    attr.output_height = DV_H;
    attr.output_stride = DV_STRIDE;
    attr.mode = mode;
    attr.format = MEDIA_FORMAT_RGB888;
    attr.input_depth = 2;
    attr.output_pool_id = output_pool;
    attr.inputs[0].enabled = 1;
    attr.inputs[1].enabled = 1;
    if (mode == MEDIA_DUALVIEW_MODE_SIDE_BY_SIDE) {
        attr.inputs[0].x = 0;
        attr.inputs[0].y = 0;
        attr.inputs[0].width = DV_W / 2;
        attr.inputs[0].height = DV_H;
        attr.inputs[1].x = DV_W / 2;
        attr.inputs[1].y = 0;
        attr.inputs[1].width = DV_W / 2;
        attr.inputs[1].height = DV_H;
    }
    if (MEDIA_DUALVIEW_CreateGrp(grp, &attr) != 0 ||
        MEDIA_DUALVIEW_Start(grp) != 0) {
        MEDIA_DUALVIEW_DestroyGrp(grp);
        return -1;
    }
    return 0;
}

static int setup_dualview_module(void) {
    if (MEDIA_POOL_Create(DV_INPUT0_POOL, DV_FRAME_SIZE, 2) != 0) return -1;
    if (MEDIA_POOL_Create(DV_INPUT1_POOL, DV_FRAME_SIZE, 2) != 0) goto fail;
    if (MEDIA_POOL_Create(DV_SBS_OUTPUT_POOL, DV_FRAME_SIZE, 2) != 0) goto fail;
    if (MEDIA_POOL_Create(DV_LBL_OUTPUT_POOL, DV_FRAME_SIZE, 2) != 0) goto fail;
    if (setup_dualview_one(DV_SBS_GRP, DV_SBS_OUTPUT_POOL,
                           MEDIA_DUALVIEW_MODE_SIDE_BY_SIDE) != 0) goto fail;
    if (setup_dualview_one(DV_LBL_GRP, DV_LBL_OUTPUT_POOL,
                           MEDIA_DUALVIEW_MODE_LINE_BY_LINE) != 0) {
        MEDIA_DUALVIEW_Stop(DV_SBS_GRP);
        MEDIA_DUALVIEW_DestroyGrp(DV_SBS_GRP);
        goto fail;
    }
    return 0;
fail:
    MEDIA_POOL_Destroy(DV_LBL_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DV_SBS_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DV_INPUT1_POOL);
    MEDIA_POOL_Destroy(DV_INPUT0_POOL);
    return -1;
}

static void cleanup_dualview_module(int enabled) {
    if (!enabled) return;
    MEDIA_DUALVIEW_Stop(DV_LBL_GRP);
    MEDIA_DUALVIEW_DestroyGrp(DV_LBL_GRP);
    MEDIA_DUALVIEW_Stop(DV_SBS_GRP);
    MEDIA_DUALVIEW_DestroyGrp(DV_SBS_GRP);
    MEDIA_POOL_Destroy(DV_LBL_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DV_SBS_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DV_INPUT1_POOL);
    MEDIA_POOL_Destroy(DV_INPUT0_POOL);
}

static void fill_dualview_rgb(uint8_t *dst, int left, int frame) {
    (void)frame;
    if (!dst) return;
    for (int y = 0; y < DV_H; ++y) {
        uint8_t *row = dst + (size_t)y * DV_STRIDE;
        for (int x = 0; x < DV_W; ++x) {
            uint8_t *px = row + x * 3;
            if (left) {
                px[0] = 255;
                px[1] = 28;
                px[2] = 18;
            } else {
                px[0] = 22;
                px[1] = 32;
                px[2] = 255;
            }
        }
    }
}

static int process_dualview_one(int grp, uint8_t *dst,
                                const uint8_t *input0, const uint8_t *input1) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame(grp, "input0", DV_INPUT0_POOL, input0) != 0) return -1;
    if (send_copied_frame(grp, "input1", DV_INPUT1_POOL, input1) != 0) return -1;
    if (MEDIA_DUALVIEW_GetFrame(grp, &out, 1000) == 0) {
        ret = copy_from_buffer(out, dst, DV_FRAME_SIZE);
        MEDIA_DUALVIEW_ReleaseFrame(grp, out);
    }
    return ret;
}

static void compose_fallback(dualview_ctx_t *ctx) {
    if (!ctx || !ctx->input0 || !ctx->input1 || !ctx->sbs || !ctx->lbl) return;
    for (int y = 0; y < DV_H; ++y) {
        for (int x = 0; x < DV_W; ++x) {
            uint8_t *sbs = ctx->sbs + ((size_t)y * DV_W + x) * 3;
            const uint8_t *src = x < DV_W / 2 ?
                ctx->input0 + ((size_t)y * DV_W + x * 2) * 3 :
                ctx->input1 + ((size_t)y * DV_W + (x - DV_W / 2) * 2) * 3;
            memcpy(sbs, src, 3);
            uint8_t *lbl = ctx->lbl + ((size_t)y * DV_W + x) * 3;
            const uint8_t *line_src = (y & 1) ? ctx->input1 : ctx->input0;
            memcpy(lbl, line_src + ((size_t)y * DV_W + x) * 3, 3);
        }
    }
}

static void draw_rgb_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                            int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    uint8_t *duv = dst + (size_t)dstride * dheight;
    for (int y = 0; y < dh; ++y) {
        int sy = y * DV_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        for (int x = 0; x < dw; ++x) {
            int sx = x * DV_W / dw;
            const uint8_t *rgb = src + ((size_t)sy * DV_W + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)uu;
            (void)vv;
            drow[x] = yy;
        }
    }
    for (int y = 0; y < dh; y += 2) {
        int sy = y * DV_H / dh;
        uint8_t *drow = duv + (size_t)((dy + y) / 2) * dstride + (dx & ~1);
        for (int x = 0; x < dw; x += 2) {
            int sx = x * DV_W / dw;
            const uint8_t *rgb = src + ((size_t)sy * DV_W + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)yy;
            drow[x] = uu;
            drow[x + 1] = vv;
        }
    }
}

static void draw_dualview_page(uint8_t *dst, int stride, int width, int height,
                               int frame, void *opaque) {
    dualview_ctx_t *ctx = (dualview_ctx_t *)opaque;
    char processed[48];
    if (ctx) {
        fill_dualview_rgb(ctx->input0, 1, frame);
        fill_dualview_rgb(ctx->input1, 0, frame);
        if (!ctx->module_ok ||
            process_dualview_one(DV_SBS_GRP, ctx->sbs, ctx->input0, ctx->input1) != 0 ||
            process_dualview_one(DV_LBL_GRP, ctx->lbl, ctx->input0, ctx->input1) != 0) {
            compose_fallback(ctx);
        } else {
            ctx->processed++;
        }
    }
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "DUALVIEW", 9, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 60, 142,
                           ctx && ctx->module_ok ? "MODULE LIVE" : "MODULE FALLBACK",
                           3, 210, 144, 84);

    int margin = 42;
    int gap = 18;
    int pane_w = (width - margin * 2 - gap) / 2;
    int pane_h = 560;
    int y0 = 246;
    int y1 = 926;
    page_surface_draw_text(dst, stride, width, height, margin, y0 - 34,
                           "INPUT LEFT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, margin + pane_w + gap, y0 - 34,
                           "INPUT RIGHT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, margin, y1 - 34,
                           "SIDE BY SIDE", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, margin + pane_w + gap, y1 - 34,
                           "LINE BY LINE", 3, 220, 108, 176);
    if (ctx) {
        draw_rgb_scaled(dst, stride, width, height, margin, y0, pane_w, pane_h, ctx->input0);
        draw_rgb_scaled(dst, stride, width, height, margin + pane_w + gap, y0, pane_w, pane_h, ctx->input1);
        draw_rgb_scaled(dst, stride, width, height, margin, y1, pane_w, pane_h, ctx->sbs);
        draw_rgb_scaled(dst, stride, width, height, margin + pane_w + gap, y1, pane_w, pane_h, ctx->lbl);
    }
    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           "RGB888 TWO INPUT TWO OUTPUT MODES",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
}

int page_dualview_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    dualview_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.input0 = malloc(DV_FRAME_SIZE);
    ctx.input1 = malloc(DV_FRAME_SIZE);
    ctx.sbs = malloc(DV_FRAME_SIZE);
    ctx.lbl = malloc(DV_FRAME_SIZE);
    if (!ctx.input0 || !ctx.input1 || !ctx.sbs || !ctx.lbl) {
        free(ctx.input0); free(ctx.input1); free(ctx.sbs); free(ctx.lbl);
        return 1;
    }
    memset(ctx.sbs, 0, DV_FRAME_SIZE);
    memset(ctx.lbl, 0, DV_FRAME_SIZE);

    if (page_surface_open(&surface, DV_PAGE_POOL, DV_PAGE_W, DV_PAGE_H,
                          DV_PAGE_STRIDE, DV_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input0); free(ctx.input1); free(ctx.sbs); free(ctx.lbl);
        return 1;
    }

    ctx.module_ok = setup_dualview_module() == 0;
    if (ctx.module_ok) set_tile_status("DUALVIEW", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("DUALVIEW standalone page module=%s. Ctrl+C to stop.\n",
           ctx.module_ok ? "live" : "fallback");

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_dualview_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % DV_PAGE_FPS) == 0) {
                printf("DUALVIEW frames=%d processed=%d mode=sbs+line_by_line standalone=1\n",
                       frame, ctx.processed);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / DV_PAGE_FPS);
    }

    cleanup_dualview_module(ctx.module_ok);
    page_surface_close(&surface);
    free(ctx.input0);
    free(ctx.input1);
    free(ctx.sbs);
    free(ctx.lbl);
    return 0;
}
