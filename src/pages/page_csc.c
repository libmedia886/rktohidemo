#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_overlay.h"
#include "page_surface.h"

#include <linux/dma-buf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CSC_PAGE_W 1080
#define CSC_PAGE_H 1920
#define CSC_PAGE_STRIDE 1088
#define CSC_PAGE_POOL 1
#define CSC_PAGE_FPS 30
#define CSC_ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define CSC_RGA_GRP 64
#define CSC_CL_GRP 76
#define CSC_INPUT_POOL 6
#define CSC_OUTPUT_POOL 7
#define CSC_W 640
#define CSC_H 640
#define CSC_NV12_STRIDE 640
#define CSC_RGB_STRIDE (CSC_W * 4)
#define CSC_NV12_SIZE (CSC_NV12_STRIDE * CSC_H * 3 / 2)
#define CSC_RGB_SIZE (CSC_RGB_STRIDE * CSC_H)
#define CSC_LIVE_SCREEN_W 1080
#define CSC_LIVE_SCREEN_H 1920
#define CSC_LIVE_CAMERA_POOL 2
#define CSC_LIVE_RESIZE_POOL 9
#define CSC_LIVE_RGB_POOL 10
#define CSC_LIVE_BACK_POOL 11
#define CSC_LIVE_OSD_POOL 8
#define CSC_LIVE_RESIZE_GRP 61
#define CSC_RGA_BACK_GRP 75
#define CSC_LIVE_OSD_GRP 81
#define CSC_LIVE_SRC_W 3840
#define CSC_LIVE_SRC_H 2160
#define CSC_LIVE_SRC_STRIDE 3840
#define CSC_LIVE_VIEW_W 1080
#define CSC_LIVE_VIEW_H 608
#define CSC_LIVE_VIEW_STRIDE 1088
#define CSC_LIVE_RGBA_STRIDE (CSC_LIVE_VIEW_W * 4)
#define CSC_LIVE_VIEW_X 0
#define CSC_LIVE_VIEW_Y 320
#define CSC_LIVE_FPS 30
#define CSC_LIVE_CAMERA_DEVICE "/dev/video-camera0"
#define CSC_LIVE_SRC_SIZE ((size_t)CSC_LIVE_SRC_STRIDE * (size_t)CSC_LIVE_SRC_H * 3u / 2u)
#define CSC_LIVE_VIEW_SIZE ((size_t)CSC_LIVE_VIEW_STRIDE * (size_t)CSC_LIVE_VIEW_H * 3u / 2u)
#define CSC_LIVE_RGBA_SIZE ((size_t)CSC_LIVE_RGBA_STRIDE * (size_t)CSC_LIVE_VIEW_H)
#define CSC_LIVE_TEXT_MASK_W 1024
#define CSC_LIVE_TEXT_MASK_H 64
#define CSC_CL_LIVE_RGB_POOL 12
#define CSC_CL_LIVE_RESIZE_POOL 13
#define CSC_CL_LIVE_OSD_POOL 14
#define CSC_CL_LIVE_SRC_RGBA_STRIDE (CSC_LIVE_SRC_W * 4)
#define CSC_CL_LIVE_SRC_RGBA_SIZE ((size_t)CSC_CL_LIVE_SRC_RGBA_STRIDE * (size_t)CSC_LIVE_SRC_H)
#define CSC_CL_LIVE_VIEW_W 1080
#define CSC_CL_LIVE_VIEW_H 1920
#define CSC_CL_LIVE_VIEW_X 0
#define CSC_CL_LIVE_VIEW_Y 0
#define CSC_CL_LIVE_VIEW_RGBA_STRIDE CSC_ALIGN_UP_LOCAL(CSC_CL_LIVE_VIEW_W * 4, 64)
#define CSC_CL_LIVE_VIEW_RGBA_SIZE ((size_t)CSC_CL_LIVE_VIEW_RGBA_STRIDE * (size_t)CSC_CL_LIVE_VIEW_H)
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    const char *page_name;
    const char *title;
    int use_cl;
    uint8_t *input;
    uint8_t *output;
    int processed;
    int module_ok;
    MEDIA_CSC_CL_PERF perf;
} csc_ctx_t;

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int resize_pool_ok;
    int rgb_pool_ok;
    int back_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int resize_ok;
    int front_ok;
    int back_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_resize_ok;
    int bind_resize_front_ok;
    int bind_front_back_ok;
    int bind_back_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
} csc_rga_live_chain_t;

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int rgb_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int csc_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_csc_ok;
    int bind_csc_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
} csc_cl_live_chain_t;

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

static void fill_input_nv12(uint8_t *dst, int frame) {
    if (!dst) return;
    uint8_t *uv = dst + (size_t)CSC_NV12_STRIDE * CSC_H;
    for (int y = 0; y < CSC_H; ++y) {
        uint8_t *row = dst + (size_t)y * CSC_NV12_STRIDE;
        for (int x = 0; x < CSC_W; ++x) {
            int checker = (((x + frame * 3) / 48) ^ ((y + frame * 2) / 48)) & 1;
            row[x] = clamp_u8(48 + x * 115 / CSC_W + y * 70 / CSC_H + (checker ? 26 : -12));
        }
    }
    for (int y = 0; y < CSC_H / 2; ++y) {
        uint8_t *row = uv + (size_t)y * CSC_NV12_STRIDE;
        for (int x = 0; x < CSC_W; x += 2) {
            row[x] = (uint8_t)(96 + ((x + frame * 4) % 120));
            row[x + 1] = (uint8_t)(96 + ((y * 3 + frame * 5) % 120));
        }
    }
}

static int setup_csc_module(csc_ctx_t *ctx) {
    if (!ctx) return -1;
    if (MEDIA_POOL_Create(CSC_INPUT_POOL, CSC_NV12_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(CSC_OUTPUT_POOL, CSC_RGB_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(CSC_INPUT_POOL);
        return -1;
    }

    if (ctx->use_cl) {
        static const float k_bt601_limit_matrix[9] = {
            0.257f, 0.504f, 0.098f,
            -0.148f, -0.291f, 0.439f,
            0.439f, -0.368f, -0.071f
        };
        MEDIA_CSC_CL_ATTR attr = {0};
        attr.input_width = CSC_W;
        attr.input_height = CSC_H;
        attr.input_format = MEDIA_FORMAT_NV12;
        attr.output_format = MEDIA_FORMAT_RGBA8888;
        attr.input_depth = 3;
        attr.output_pool_id = CSC_OUTPUT_POOL;
        attr.input_stride = CSC_NV12_STRIDE;
        attr.output_stride = CSC_RGB_STRIDE;
        if (MEDIA_CSC_CL_CreateGrp(CSC_CL_GRP, &attr) != 0 ||
            MEDIA_CSC_CL_SetMatrix(CSC_CL_GRP, k_bt601_limit_matrix, 9) != 0 ||
            MEDIA_CSC_CL_Start(CSC_CL_GRP) != 0 ||
            MEDIA_CSC_CL_Enable(CSC_CL_GRP) != 0) {
            MEDIA_CSC_CL_DestroyGrp(CSC_CL_GRP);
            MEDIA_POOL_Destroy(CSC_OUTPUT_POOL);
            MEDIA_POOL_Destroy(CSC_INPUT_POOL);
            return -1;
        }
    } else {
        MEDIA_CSC_RGA_ATTR attr = {0};
        attr.input_width = CSC_W;
        attr.input_height = CSC_H;
        attr.input_format = MEDIA_FORMAT_NV12;
        attr.output_format = MEDIA_FORMAT_RGBA8888;
        attr.input_depth = 3;
        attr.output_pool_id = CSC_OUTPUT_POOL;
        attr.input_stride = CSC_NV12_STRIDE;
        attr.output_stride = CSC_RGB_STRIDE;
        attr.csc_mode = 0;
        if (MEDIA_CSC_RGA_CreateGrp(CSC_RGA_GRP, &attr) != 0 ||
            MEDIA_CSC_RGA_Start(CSC_RGA_GRP) != 0 ||
            MEDIA_CSC_RGA_Enable(CSC_RGA_GRP) != 0) {
            MEDIA_CSC_RGA_DestroyGrp(CSC_RGA_GRP);
            MEDIA_POOL_Destroy(CSC_OUTPUT_POOL);
            MEDIA_POOL_Destroy(CSC_INPUT_POOL);
            return -1;
        }
    }
    return 0;
}

static void cleanup_csc_module(csc_ctx_t *ctx) {
    if (!ctx || !ctx->module_ok) return;
    if (ctx->use_cl) {
        MEDIA_CSC_CL_Disable(CSC_CL_GRP);
        MEDIA_CSC_CL_Stop(CSC_CL_GRP);
        MEDIA_CSC_CL_DestroyGrp(CSC_CL_GRP);
    } else {
        MEDIA_CSC_RGA_Disable(CSC_RGA_GRP);
        MEDIA_CSC_RGA_Stop(CSC_RGA_GRP);
        MEDIA_CSC_RGA_DestroyGrp(CSC_RGA_GRP);
    }
    MEDIA_POOL_Destroy(CSC_OUTPUT_POOL);
    MEDIA_POOL_Destroy(CSC_INPUT_POOL);
    ctx->module_ok = 0;
}

static int process_csc(csc_ctx_t *ctx) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (!ctx || !ctx->input || !ctx->output || !ctx->module_ok) return -1;
    if (MEDIA_POOL_GetBuffer(CSC_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, ctx->input, CSC_NV12_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }

    if (ctx->use_cl) {
        if (MEDIA_CSC_CL_SendFrame(CSC_CL_GRP, in, 1000) != 0) {
            MEDIA_POOL_PutBuffer(in);
            return -1;
        }
        if (MEDIA_CSC_CL_GetFrame(CSC_CL_GRP, &out, 1000) == 0) {
            ret = copy_from_buffer(out, ctx->output, CSC_RGB_SIZE);
            MEDIA_CSC_CL_ReleaseFrame(CSC_CL_GRP, out);
        }
        if (ret == 0) (void)MEDIA_CSC_CL_GetLastPerf(CSC_CL_GRP, &ctx->perf);
    } else {
        if (MEDIA_SYS_SendFrame("CSC_RGA", CSC_RGA_GRP, "input", in, 1000) != 0) {
            MEDIA_POOL_PutBuffer(in);
            return -1;
        }
        MEDIA_POOL_PutBuffer(in);
        if (MEDIA_CSC_RGA_GetFrame(CSC_RGA_GRP, &out, 1000) == 0) {
            ret = copy_from_buffer(out, ctx->output, CSC_RGB_SIZE);
            MEDIA_CSC_RGA_ReleaseFrame(CSC_RGA_GRP, out);
        }
    }
    if (ret == 0) ctx->processed++;
    return ret;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * CSC_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * CSC_NV12_STRIDE;
        for (int x = 0; x < dw; ++x) drow[x] = srow[x * CSC_W / dw];
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)CSC_NV12_STRIDE * CSC_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (CSC_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * CSC_NV12_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = ((x * CSC_W / dw) & ~1);
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_rgba_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                            int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    uint8_t *duv = dst + (size_t)dstride * dheight;
    for (int y = 0; y < dh; ++y) {
        int sy = y * CSC_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        for (int x = 0; x < dw; ++x) {
            int sx = x * CSC_W / dw;
            const uint8_t *rgb = src + ((size_t)sy * CSC_W + sx) * 4;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)uu;
            (void)vv;
            drow[x] = yy;
        }
    }
    for (int y = 0; y < dh; y += 2) {
        int sy = y * CSC_H / dh;
        uint8_t *drow = duv + (size_t)((dy + y) / 2) * dstride + (dx & ~1);
        for (int x = 0; x < dw; x += 2) {
            int sx = x * CSC_W / dw;
            const uint8_t *rgb = src + ((size_t)sy * CSC_W + sx) * 4;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)yy;
            drow[x] = uu;
            drow[x + 1] = vv;
        }
    }
}

static void draw_csc_page(uint8_t *dst, int stride, int width, int height,
                          int frame, void *opaque) {
    csc_ctx_t *ctx = (csc_ctx_t *)opaque;
    (void)frame;
    char processed[64];
    char perf[64];
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);
    snprintf(perf, sizeof(perf), "GPU %.3f MS QUEUE %.3f",
             ctx ? ctx->perf.gpu_kernel_total_ms : 0.0,
             ctx ? ctx->perf.gpu_queue_total_ms : 0.0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 8, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           ctx && ctx->title ? ctx->title : "CSC", 7, 235, 108, 176);

    int pane = 600;
    int x = (width - pane) / 2;
    int top_y = 246;
    int bottom_y = 930;
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, top_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, bottom_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, top_y - 34,
                           "NV12 INPUT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, x, bottom_y - 34,
                           "RGBA8888 OUTPUT", 3, 220, 108, 176);
    if (ctx) {
        draw_nv12_scaled(dst, stride, width, height, x, top_y, pane, pane, ctx->input);
        draw_rgba_scaled(dst, stride, width, height, x, bottom_y, pane, pane, ctx->output);
    }
    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           ctx && ctx->use_cl ? "OPENCL NV12 TO RGB888" : "RGA NV12 TO RGB888",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
    if (ctx && ctx->use_cl) {
        page_surface_draw_text(dst, stride, width, height, 70, 1725,
                               perf, 2, 210, 108, 176);
    } else {
        page_surface_draw_text(dst, stride, width, height, 70, 1725,
                               "RGA HARDWARE PATH", 2, 210, 108, 176);
    }
    page_surface_draw_text(dst, stride, width, height, 70, 1810,
                           ctx && ctx->module_ok ? "MODULE LIVE" : "MODULE FALLBACK",
                           3, 210, 108, 176);
}

static int run_csc_page(volatile sig_atomic_t *running,
                        const char *page_name,
                        const char *title,
                        int use_cl) {
    page_surface_t surface;
    csc_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_name = page_name;
    ctx.title = title;
    ctx.use_cl = use_cl;
    ctx.input = malloc(CSC_NV12_SIZE);
    ctx.output = malloc(CSC_RGB_SIZE);
    if (!ctx.input || !ctx.output) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }
    memset(ctx.input, 16, CSC_NV12_SIZE);
    memset(ctx.input + CSC_NV12_STRIDE * CSC_H, 128, CSC_NV12_STRIDE * CSC_H / 2);
    memset(ctx.output, 0, CSC_RGB_SIZE);

    if (page_surface_open(&surface, CSC_PAGE_POOL, CSC_PAGE_W, CSC_PAGE_H,
                          CSC_PAGE_STRIDE, CSC_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }

    ctx.module_ok = setup_csc_module(&ctx) == 0;
    if (ctx.module_ok) set_tile_status(page_name, TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("%s standalone page module=%s path=%s. Ctrl+C to stop.\n",
           page_name, ctx.module_ok ? "live" : "fallback",
           use_cl ? "opencl" : "rga");

    int frame = 0;
    while (!running || *running) {
        fill_input_nv12(ctx.input, frame);
        if (!ctx.module_ok || process_csc(&ctx) != 0) memset(ctx.output, 0, CSC_RGB_SIZE);
        if (page_surface_send_frame(&surface, draw_csc_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % CSC_PAGE_FPS) == 0) {
                printf("%s frames=%d processed=%d mode=%s gpu=%.3f/%.3f standalone=1\n",
                       page_name, frame, ctx.processed, use_cl ? "opencl" : "rga",
                       ctx.perf.gpu_kernel_total_ms, ctx.perf.gpu_queue_total_ms);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / CSC_PAGE_FPS);
    }

    cleanup_csc_module(&ctx);
    page_surface_close(&surface);
    free(ctx.input);
    free(ctx.output);
    return 0;
}

static void drain_live_resize_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_RESIZE_RGA_GetFrame(CSC_LIVE_RESIZE_GRP, &out, 0) != 0) break;
            MEDIA_RESIZE_RGA_ReleaseFrame(CSC_LIVE_RESIZE_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_live_csc_output(int grp) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_CSC_RGA_GetFrame(grp, &out, 0) != 0) break;
            MEDIA_CSC_RGA_ReleaseFrame(grp, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_live_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(CSC_LIVE_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(CSC_LIVE_OSD_GRP, out);
    }
}

static int update_csc_rga_live_overlay(uint64_t vi_count,
                                       uint64_t resize_count,
                                       uint64_t front_count,
                                       uint64_t back_count,
                                       uint64_t osd_count,
                                       uint64_t vo_count) {
    static uint8_t masks[5][CSC_LIVE_TEXT_MASK_W * CSC_LIVE_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char fmt_line[128];
    char count_line[160];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(fmt_line, sizeof(fmt_line), "FORMAT NV12->RGBA->NV12  VIEW %dX%d",
             CSC_LIVE_VIEW_W, CSC_LIVE_VIEW_H);
    snprintf(count_line, sizeof(count_line), "VI %llu  RGA0 %llu  RGA1 %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)front_count,
             (unsigned long long)back_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 0, 24, 16, 2,
                              "CSC_RGA LIVE VI COLOR ROUNDTRIP",
                              masks[0], sizeof(masks[0]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->RESIZE_RGA->CSC_RGA->CSC_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 3, 24, 554, 2,
                              fmt_line,
                              masks[3], sizeof(masks[3]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    (void)resize_count;
    return 0;
}

static int setup_csc_rga_live_chain(csc_rga_live_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_CSC_RGA_ATTR front = {0};
    MEDIA_CSC_RGA_ATTR back = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(CSC_LIVE_CAMERA_POOL, CSC_LIVE_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(CSC_LIVE_RESIZE_POOL, CSC_LIVE_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(CSC_LIVE_RGB_POOL, CSC_LIVE_RGBA_SIZE, 4) != 0) return -1;
    chain->rgb_pool_ok = 1;
    if (MEDIA_POOL_Create(CSC_LIVE_BACK_POOL, CSC_LIVE_VIEW_SIZE, 4) != 0) return -1;
    chain->back_pool_ok = 1;
    if (MEDIA_POOL_Create(CSC_LIVE_OSD_POOL, CSC_LIVE_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = CSC_LIVE_CAMERA_DEVICE;
    vi.width = CSC_LIVE_SRC_W;
    vi.height = CSC_LIVE_SRC_H;
    vi.stride = CSC_LIVE_SRC_STRIDE;
    vi.fps = CSC_LIVE_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = CSC_LIVE_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = CSC_LIVE_SRC_W;
    resize.src_height = CSC_LIVE_SRC_H;
    resize.input_width = CSC_LIVE_SRC_W;
    resize.input_height = CSC_LIVE_SRC_H;
    resize.input_stride = CSC_LIVE_SRC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 4;
    resize.out_width = CSC_LIVE_VIEW_W;
    resize.out_height = CSC_LIVE_VIEW_H;
    resize.out_stride = CSC_LIVE_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = CSC_LIVE_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(CSC_LIVE_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(CSC_LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(CSC_LIVE_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    front.input_width = CSC_LIVE_VIEW_W;
    front.input_height = CSC_LIVE_VIEW_H;
    front.input_format = MEDIA_FORMAT_NV12;
    front.output_format = MEDIA_FORMAT_RGBA8888;
    front.input_depth = 4;
    front.output_pool_id = CSC_LIVE_RGB_POOL;
    front.input_stride = CSC_LIVE_VIEW_STRIDE;
    front.output_stride = CSC_LIVE_RGBA_STRIDE;
    front.csc_mode = 0;
    if (MEDIA_CSC_RGA_CreateGrp(CSC_RGA_GRP, &front) != 0 ||
        MEDIA_CSC_RGA_Start(CSC_RGA_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(CSC_RGA_GRP) != 0) {
        return -1;
    }
    chain->front_ok = 1;

    back.input_width = CSC_LIVE_VIEW_W;
    back.input_height = CSC_LIVE_VIEW_H;
    back.input_format = MEDIA_FORMAT_RGBA8888;
    back.output_format = MEDIA_FORMAT_NV12;
    back.input_depth = 4;
    back.output_pool_id = CSC_LIVE_BACK_POOL;
    back.input_stride = CSC_LIVE_RGBA_STRIDE;
    back.output_stride = CSC_LIVE_VIEW_STRIDE;
    back.csc_mode = 0;
    if (MEDIA_CSC_RGA_CreateGrp(CSC_RGA_BACK_GRP, &back) != 0 ||
        MEDIA_CSC_RGA_Start(CSC_RGA_BACK_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(CSC_RGA_BACK_GRP) != 0) {
        return -1;
    }
    chain->back_ok = 1;

    osd.input_width = CSC_LIVE_VIEW_W;
    osd.input_height = CSC_LIVE_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = CSC_LIVE_OSD_POOL;
    osd.input_stride = CSC_LIVE_VIEW_STRIDE;
    osd.output_stride = CSC_LIVE_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(CSC_LIVE_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(CSC_LIVE_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = CSC_LIVE_SCREEN_W;
    vo.height = CSC_LIVE_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, CSC_LIVE_VIEW_X, CSC_LIVE_VIEW_Y,
                           CSC_LIVE_VIEW_W, CSC_LIVE_VIEW_H,
                           CSC_LIVE_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "RESIZE_RGA", CSC_LIVE_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_vi_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", CSC_LIVE_RESIZE_GRP, "output0",
                       "CSC_RGA", CSC_RGA_GRP, "input") != 0) return -1;
    chain->bind_resize_front_ok = 1;
    if (MEDIA_SYS_Bind("CSC_RGA", CSC_RGA_GRP, "output0",
                       "CSC_RGA", CSC_RGA_BACK_GRP, "input") != 0) return -1;
    chain->bind_front_back_ok = 1;
    if (MEDIA_SYS_Bind("CSC_RGA", CSC_RGA_BACK_GRP, "output0",
                       "OSD", CSC_LIVE_OSD_GRP, "input") != 0) return -1;
    chain->bind_back_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", CSC_LIVE_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("CSC_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_csc_rga_live_chain(csc_rga_live_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", CSC_LIVE_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_back_osd_ok) {
        MEDIA_SYS_UnBind("CSC_RGA", CSC_RGA_BACK_GRP, "output0",
                         "OSD", CSC_LIVE_OSD_GRP, "input");
        chain->bind_back_osd_ok = 0;
    }
    if (chain->bind_front_back_ok) {
        MEDIA_SYS_UnBind("CSC_RGA", CSC_RGA_GRP, "output0",
                         "CSC_RGA", CSC_RGA_BACK_GRP, "input");
        chain->bind_front_back_ok = 0;
    }
    if (chain->bind_resize_front_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", CSC_LIVE_RESIZE_GRP, "output0",
                         "CSC_RGA", CSC_RGA_GRP, "input");
        chain->bind_resize_front_ok = 0;
    }
    if (chain->bind_vi_resize_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "RESIZE_RGA", CSC_LIVE_RESIZE_GRP, "input0");
        chain->bind_vi_resize_ok = 0;
    }
    if (chain->osd_ok) {
        drain_live_osd_output();
        MEDIA_OSD_Stop(CSC_LIVE_OSD_GRP);
        drain_live_osd_output();
        MEDIA_OSD_DestroyGrp(CSC_LIVE_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->back_ok) {
        drain_live_csc_output(CSC_RGA_BACK_GRP);
        MEDIA_CSC_RGA_Disable(CSC_RGA_BACK_GRP);
        MEDIA_CSC_RGA_Stop(CSC_RGA_BACK_GRP);
        drain_live_csc_output(CSC_RGA_BACK_GRP);
        MEDIA_CSC_RGA_DestroyGrp(CSC_RGA_BACK_GRP);
        chain->back_ok = 0;
    }
    if (chain->front_ok) {
        drain_live_csc_output(CSC_RGA_GRP);
        MEDIA_CSC_RGA_Disable(CSC_RGA_GRP);
        MEDIA_CSC_RGA_Stop(CSC_RGA_GRP);
        drain_live_csc_output(CSC_RGA_GRP);
        MEDIA_CSC_RGA_DestroyGrp(CSC_RGA_GRP);
        chain->front_ok = 0;
    }
    if (chain->resize_ok) {
        drain_live_resize_output();
        MEDIA_RESIZE_RGA_Disable(CSC_LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(CSC_LIVE_RESIZE_GRP);
        drain_live_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(CSC_LIVE_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(CSC_LIVE_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->back_pool_ok) {
        MEDIA_POOL_Destroy(CSC_LIVE_BACK_POOL);
        chain->back_pool_ok = 0;
    }
    if (chain->rgb_pool_ok) {
        MEDIA_POOL_Destroy(CSC_LIVE_RGB_POOL);
        chain->rgb_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(CSC_LIVE_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(CSC_LIVE_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

static int run_csc_rga_live_page(volatile sig_atomic_t *running) {
    csc_rga_live_chain_t chain = {0};
    if (setup_csc_rga_live_chain(&chain) != 0) {
        fprintf(stderr, "CSC_RGA standalone VI chain setup failed\n");
        cleanup_csc_rga_live_chain(&chain);
        return 1;
    }

    printf("CSC_RGA standalone: VI %s %dx%d -> RESIZE_RGA -> CSC_RGA NV12/RGBA/NV12 -> OSD -> VO. Ctrl+C to stop.\n",
           CSC_LIVE_CAMERA_DEVICE, CSC_LIVE_SRC_W, CSC_LIVE_SRC_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t resize_count = 0;
        uint64_t front_count = 0;
        uint64_t back_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        sleep(1);
        tick++;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", CSC_LIVE_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("CSC_RGA", CSC_RGA_GRP, &front_count);
        (void)MEDIA_SYS_GetModuleFrameCount("CSC_RGA", CSC_RGA_BACK_GRP, &back_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", CSC_LIVE_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_csc_rga_live_overlay(vi_count, resize_count,
                                                     front_count, back_count,
                                                     osd_count, vo_count) == 0;
        printf("CSC_RGA vi=%llu resize=%llu front=%llu back=%llu osd=%llu vo=%llu format=NV12-RGBA-NV12 tick=%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)resize_count,
               (unsigned long long)front_count,
               (unsigned long long)back_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               tick,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_csc_rga_live_chain(&chain);
    return 0;
}

static void drain_live_csc_cl_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_CSC_CL_GetFrame(CSC_CL_GRP, &out, 0) != 0) break;
            MEDIA_CSC_CL_ReleaseFrame(CSC_CL_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static int update_csc_cl_live_overlay(const MEDIA_CSC_CL_PERF *cl_perf,
                                      uint64_t vi_count,
                                      uint64_t csc_count,
                                      uint64_t resize_count,
                                      uint64_t osd_count,
                                      uint64_t vo_count) {
    static uint8_t masks[5][CSC_LIVE_TEXT_MASK_W * CSC_LIVE_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char cl_line[128];
    char count_line[160];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(cl_line, sizeof(cl_line), "FORMAT NV12->ARGB  CL %.3fMS QUEUE %.3fMS",
             cl_perf ? cl_perf->gpu_kernel_total_ms : 0.0,
             cl_perf ? cl_perf->gpu_queue_total_ms : 0.0);
    snprintf(count_line, sizeof(count_line), "VI %llu  CL %llu  RGA %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)csc_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 0, 24, 16, 2,
                              "CSC_CL LIVE VI OPENCL COLOR",
                              masks[0], sizeof(masks[0]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->CSC_CL->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 3, 24, 554, 2,
                              cl_line,
                              masks[3], sizeof(masks[3]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CSC_LIVE_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), CSC_LIVE_TEXT_MASK_W, CSC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_csc_cl_live_chain(csc_cl_live_chain_t *chain) {
    static const float k_bt601_limit_matrix[9] = {
        0.257f, 0.504f, 0.098f,
        -0.148f, -0.291f, 0.439f,
        0.439f, -0.368f, -0.071f
    };
    MEDIA_VI_ATTR vi = {0};
    MEDIA_CSC_CL_ATTR csc = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(CSC_LIVE_CAMERA_POOL, CSC_LIVE_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(CSC_CL_LIVE_RGB_POOL, CSC_CL_LIVE_SRC_RGBA_SIZE, 3) != 0) return -1;
    chain->rgb_pool_ok = 1;
    if (MEDIA_POOL_Create(CSC_CL_LIVE_RESIZE_POOL, CSC_CL_LIVE_VIEW_RGBA_SIZE, 4) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(CSC_CL_LIVE_OSD_POOL, CSC_CL_LIVE_VIEW_RGBA_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = CSC_LIVE_CAMERA_DEVICE;
    vi.width = CSC_LIVE_SRC_W;
    vi.height = CSC_LIVE_SRC_H;
    vi.stride = CSC_LIVE_SRC_STRIDE;
    vi.fps = CSC_LIVE_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = CSC_LIVE_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    csc.input_width = CSC_LIVE_SRC_W;
    csc.input_height = CSC_LIVE_SRC_H;
    csc.input_format = MEDIA_FORMAT_NV12;
    csc.output_format = MEDIA_FORMAT_ARGB8888;
    csc.input_depth = 4;
    csc.output_pool_id = CSC_CL_LIVE_RGB_POOL;
    csc.input_stride = CSC_LIVE_SRC_STRIDE;
    csc.output_stride = CSC_CL_LIVE_SRC_RGBA_STRIDE;
    if (MEDIA_CSC_CL_CreateGrp(CSC_CL_GRP, &csc) != 0 ||
        MEDIA_CSC_CL_SetMatrix(CSC_CL_GRP, k_bt601_limit_matrix, 9) != 0 ||
        MEDIA_CSC_CL_Start(CSC_CL_GRP) != 0 ||
        MEDIA_CSC_CL_Enable(CSC_CL_GRP) != 0) {
        return -1;
    }
    chain->csc_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = CSC_LIVE_SRC_W;
    resize.src_height = CSC_LIVE_SRC_H;
    resize.input_width = CSC_LIVE_SRC_W;
    resize.input_height = CSC_LIVE_SRC_H;
    resize.input_stride = CSC_CL_LIVE_SRC_RGBA_STRIDE;
    resize.input_format = MEDIA_FORMAT_ARGB8888;
    resize.input_depth = 4;
    resize.out_width = CSC_CL_LIVE_VIEW_W;
    resize.out_height = CSC_CL_LIVE_VIEW_H;
    resize.out_stride = CSC_CL_LIVE_VIEW_RGBA_STRIDE;
    resize.output_format = MEDIA_FORMAT_ARGB8888;
    resize.output_pool_id = CSC_CL_LIVE_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(CSC_LIVE_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(CSC_LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(CSC_LIVE_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = CSC_CL_LIVE_VIEW_W;
    osd.input_height = CSC_CL_LIVE_VIEW_H;
    osd.format = MEDIA_FORMAT_ARGB8888;
    osd.input_depth = 4;
    osd.output_pool_id = CSC_CL_LIVE_OSD_POOL;
    osd.input_stride = CSC_CL_LIVE_VIEW_RGBA_STRIDE;
    osd.output_stride = CSC_CL_LIVE_VIEW_RGBA_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(CSC_LIVE_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(CSC_LIVE_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = CSC_LIVE_SCREEN_W;
    vo.height = CSC_LIVE_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, CSC_CL_LIVE_VIEW_X, CSC_CL_LIVE_VIEW_Y,
                           CSC_CL_LIVE_VIEW_W, CSC_CL_LIVE_VIEW_H,
                           CSC_CL_LIVE_VIEW_RGBA_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_ARGB8888) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "CSC_CL", CSC_CL_GRP, "input") != 0) return -1;
    chain->bind_vi_csc_ok = 1;
    if (MEDIA_SYS_Bind("CSC_CL", CSC_CL_GRP, "output0",
                       "RESIZE_RGA", CSC_LIVE_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_csc_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", CSC_LIVE_RESIZE_GRP, "output0",
                       "OSD", CSC_LIVE_OSD_GRP, "input") != 0) return -1;
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", CSC_LIVE_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("CSC_CL", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_csc_cl_live_chain(csc_cl_live_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", CSC_LIVE_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", CSC_LIVE_RESIZE_GRP, "output0",
                         "OSD", CSC_LIVE_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_csc_resize_ok) {
        MEDIA_SYS_UnBind("CSC_CL", CSC_CL_GRP, "output0",
                         "RESIZE_RGA", CSC_LIVE_RESIZE_GRP, "input0");
        chain->bind_csc_resize_ok = 0;
    }
    if (chain->bind_vi_csc_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output", "CSC_CL", CSC_CL_GRP, "input");
        chain->bind_vi_csc_ok = 0;
    }
    if (chain->osd_ok) {
        drain_live_osd_output();
        MEDIA_OSD_Stop(CSC_LIVE_OSD_GRP);
        drain_live_osd_output();
        MEDIA_OSD_DestroyGrp(CSC_LIVE_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_live_resize_output();
        MEDIA_RESIZE_RGA_Disable(CSC_LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(CSC_LIVE_RESIZE_GRP);
        drain_live_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(CSC_LIVE_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->csc_ok) {
        drain_live_csc_cl_output();
        MEDIA_CSC_CL_Disable(CSC_CL_GRP);
        MEDIA_CSC_CL_Stop(CSC_CL_GRP);
        drain_live_csc_cl_output();
        MEDIA_CSC_CL_DestroyGrp(CSC_CL_GRP);
        chain->csc_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(CSC_CL_LIVE_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(CSC_CL_LIVE_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->rgb_pool_ok) {
        MEDIA_POOL_Destroy(CSC_CL_LIVE_RGB_POOL);
        chain->rgb_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(CSC_LIVE_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

static int run_csc_cl_live_page(volatile sig_atomic_t *running) {
    csc_cl_live_chain_t chain = {0};
    if (setup_csc_cl_live_chain(&chain) != 0) {
        fprintf(stderr, "CSC_CL standalone VI chain setup failed\n");
        cleanup_csc_cl_live_chain(&chain);
        return 1;
    }

    printf("CSC_CL standalone: VI %s %dx%d -> CSC_CL ARGB -> RESIZE_RGA -> OSD -> VO. Ctrl+C to stop.\n",
           CSC_LIVE_CAMERA_DEVICE, CSC_LIVE_SRC_W, CSC_LIVE_SRC_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t csc_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        MEDIA_CSC_CL_PERF perf = {0};
        sleep(1);
        tick++;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("CSC_CL", CSC_CL_GRP, &csc_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", CSC_LIVE_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", CSC_LIVE_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        (void)MEDIA_CSC_CL_GetLastPerf(CSC_CL_GRP, &perf);
        int overlay_ok = update_csc_cl_live_overlay(&perf, vi_count, csc_count,
                                                    resize_count, osd_count, vo_count) == 0;
        printf("CSC_CL vi=%llu csc=%llu resize=%llu osd=%llu vo=%llu cl=%.3f/%.3fms tick=%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)csc_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               perf.gpu_kernel_total_ms,
               perf.gpu_queue_total_ms,
               tick,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_csc_cl_live_chain(&chain);
    return 0;
}

int page_csc_cl_run(volatile sig_atomic_t *running) {
    return run_csc_cl_live_page(running);
}

int page_csc_rga_run(volatile sig_atomic_t *running) {
    return run_csc_rga_live_page(running);
}
