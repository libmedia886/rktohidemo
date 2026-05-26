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
#include <sys/stat.h>
#include <unistd.h>

#define TNR_PAGE_W 1080
#define TNR_PAGE_H 1920
#define TNR_PAGE_STRIDE 1088
#define TNR_PAGE_POOL 1
#define TNR_PAGE_FPS 30
#define TNR_GRP 77
#define TNR_INPUT_POOL 6
#define TNR_OUTPUT_POOL 7
#define TNR_W 640
#define TNR_H 640
#define TNR_STRIDE 640
#define TNR_FRAME_SIZE (TNR_STRIDE * TNR_H * 3 / 2)
#define TNR_ASSET_PATH "assets/loop/tnr_cl/synthetic_random_spatial_noise_640x640_120.nv12"
#define LICENSE_PATH "/root/licence.dat"

#define TNR_THRESHOLD 0.07f
#define TNR_STATIC_ALPHA 0.82f
#define TNR_MOTION_ALPHA 1.0f

typedef struct {
    uint8_t *frames;
    int frame_count;
    uint8_t *output;
    int processed;
    int module_ok;
    MEDIA_TNR_CL_PERF perf;
} tnr_page_ctx_t;

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

static void fill_synthetic_frame(uint8_t *dst, int frame) {
    if (!dst) return;
    uint32_t seed = 0x12345678u + (uint32_t)frame * 747796405u;
    for (int y = 0; y < TNR_H; ++y) {
        uint8_t *row = dst + (size_t)y * TNR_STRIDE;
        for (int x = 0; x < TNR_W; ++x) {
            seed = seed * 1664525u + 1013904223u;
            int base = 48 + (x * 110) / TNR_W + (y * 48) / TNR_H;
            int noise = (int)((seed >> 24) & 0x3f) - 32;
            row[x] = (uint8_t)(base + noise < 16 ? 16 : (base + noise > 235 ? 235 : base + noise));
        }
    }
    uint8_t *uv = dst + (size_t)TNR_STRIDE * TNR_H;
    for (int y = 0; y < TNR_H / 2; ++y) {
        memset(uv + (size_t)y * TNR_STRIDE, 128, TNR_STRIDE);
    }
}

static int load_tnr_asset(tnr_page_ctx_t *ctx) {
    struct stat st;
    if (!ctx) return -1;
    if (stat(TNR_ASSET_PATH, &st) != 0 || st.st_size <= 0 ||
        ((size_t)st.st_size % TNR_FRAME_SIZE) != 0) {
        ctx->frames = malloc(TNR_FRAME_SIZE);
        if (!ctx->frames) return -1;
        fill_synthetic_frame(ctx->frames, 0);
        ctx->frame_count = 1;
        return 0;
    }

    ctx->frames = malloc((size_t)st.st_size);
    if (!ctx->frames) return -1;
    FILE *fp = fopen(TNR_ASSET_PATH, "rb");
    if (!fp) {
        free(ctx->frames);
        ctx->frames = NULL;
        return -1;
    }
    size_t got = fread(ctx->frames, 1, (size_t)st.st_size, fp);
    fclose(fp);
    if (got != (size_t)st.st_size) {
        free(ctx->frames);
        ctx->frames = NULL;
        return -1;
    }
    ctx->frame_count = (int)((size_t)st.st_size / TNR_FRAME_SIZE);
    return ctx->frame_count > 0 ? 0 : -1;
}

static int setup_tnr_module(void) {
    if (MEDIA_POOL_Create(TNR_INPUT_POOL, TNR_FRAME_SIZE, 4) != 0) return -1;
    if (MEDIA_POOL_Create(TNR_OUTPUT_POOL, TNR_FRAME_SIZE, 4) != 0) {
        MEDIA_POOL_Destroy(TNR_INPUT_POOL);
        return -1;
    }

    MEDIA_TNR_CL_ATTR attr = {0};
    attr.width = TNR_W;
    attr.height = TNR_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.output_pool_id = TNR_OUTPUT_POOL;
    attr.input_stride = TNR_STRIDE;
    attr.output_stride = TNR_STRIDE;
    attr.block_size = 16;
    attr.threshold = TNR_THRESHOLD;
    attr.static_alpha = TNR_STATIC_ALPHA;
    attr.motion_alpha = TNR_MOTION_ALPHA;

    if (MEDIA_TNR_CL_CreateGrp(TNR_GRP, &attr) != 0 ||
        MEDIA_TNR_CL_Start(TNR_GRP) != 0) {
        MEDIA_TNR_CL_DestroyGrp(TNR_GRP);
        MEDIA_POOL_Destroy(TNR_OUTPUT_POOL);
        MEDIA_POOL_Destroy(TNR_INPUT_POOL);
        return -1;
    }
    return 0;
}

static void cleanup_tnr_module(int enabled) {
    if (!enabled) return;
    MEDIA_TNR_CL_Stop(TNR_GRP);
    MEDIA_TNR_CL_DestroyGrp(TNR_GRP);
    MEDIA_POOL_Destroy(TNR_OUTPUT_POOL);
    MEDIA_POOL_Destroy(TNR_INPUT_POOL);
}

static int process_tnr(tnr_page_ctx_t *ctx, const uint8_t *input) {
    if (!ctx || !input || !ctx->output || !ctx->module_ok) return -1;
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    if (MEDIA_POOL_GetBuffer(TNR_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, input, TNR_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("TNR_CL", TNR_GRP, "input", in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);

    if (MEDIA_TNR_CL_GetFrame(TNR_GRP, &out, 1000) != 0) return -1;
    int ret = copy_from_buffer(out, ctx->output, TNR_FRAME_SIZE);
    MEDIA_TNR_CL_ReleaseFrame(TNR_GRP, out);
    if (ret == 0) {
        ctx->processed++;
        (void)MEDIA_TNR_CL_GetLastPerf(TNR_GRP, &ctx->perf);
    }
    return ret;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * TNR_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * TNR_STRIDE;
        for (int x = 0; x < dw; ++x) {
            int sx = x * TNR_W / dw;
            drow[x] = srow[sx];
        }
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)TNR_STRIDE * TNR_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (TNR_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * TNR_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = ((x * TNR_W / dw) & ~1);
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_tnr_page(uint8_t *dst, int stride, int width, int height,
                          int frame, void *opaque) {
    tnr_page_ctx_t *ctx = (tnr_page_ctx_t *)opaque;
    int sample = ctx && ctx->frame_count > 0 ? frame % ctx->frame_count : 0;
    const uint8_t *input = ctx && ctx->frames ?
        ctx->frames + (size_t)sample * TNR_FRAME_SIZE : NULL;
    char sample_text[64];
    char processed[48];
    char perf[72];

    if (input && ctx && ctx->module_ok) {
        if (process_tnr(ctx, input) != 0) memcpy(ctx->output, input, TNR_FRAME_SIZE);
    } else if (input && ctx && ctx->output) {
        memcpy(ctx->output, input, TNR_FRAME_SIZE);
    }

    snprintf(sample_text, sizeof(sample_text), "SAMPLE %03d/%03d",
             sample + 1, ctx ? ctx->frame_count : 0);
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);
    if (ctx) {
        snprintf(perf, sizeof(perf), "CL M %03d US B %03d US Q %03d US",
                 (int)(ctx->perf.gpu_motion_ms * 1000.0),
                 (int)(ctx->perf.gpu_blend_ms * 1000.0),
                 (int)(ctx->perf.gpu_queue_total_ms * 1000.0));
    } else {
        snprintf(perf, sizeof(perf), "PERF NA");
    }

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "TNR CL", 9, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 60, 142,
                           sample_text, 3, 210, 144, 84);

    int pane = 600;
    int x = (width - pane) / 2;
    int top_y = 246;
    int bottom_y = 930;
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, top_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, bottom_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, top_y - 34,
                           "NOISY INPUT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, x, bottom_y - 34,
                           "TEMPORAL OUTPUT", 3, 220, 108, 176);
    if (input) draw_nv12_scaled(dst, stride, width, height, x, top_y, pane, pane, input);
    if (ctx && ctx->output) {
        draw_nv12_scaled(dst, stride, width, height, x, bottom_y, pane, pane, ctx->output);
    }

    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           "THRESHOLD 07 STATIC 82 MOTION 100 BLOCK 16",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1725,
                           perf, 2, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1810,
                           "STATIC AREAS ACCUMULATE MOTION AREAS RESET",
                           2, 190, 144, 84);
}

int page_tnr_cl_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    tnr_page_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.output = malloc(TNR_FRAME_SIZE);
    if (!ctx.output || load_tnr_asset(&ctx) != 0) {
        free(ctx.output);
        free(ctx.frames);
        return 1;
    }
    memcpy(ctx.output, ctx.frames, TNR_FRAME_SIZE);

    if (page_surface_open(&surface, TNR_PAGE_POOL, TNR_PAGE_W, TNR_PAGE_H,
                          TNR_PAGE_STRIDE, TNR_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.output);
        free(ctx.frames);
        return 1;
    }

    ctx.module_ok = setup_tnr_module() == 0;
    if (ctx.module_ok) set_tile_status("TNR_CL", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("TNR_CL standalone page module=%s frames=%d. Ctrl+C to stop.\n",
           ctx.module_ok ? "live" : "fallback", ctx.frame_count);

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_tnr_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % TNR_PAGE_FPS) == 0) {
                printf("TNR_CL frames=%d processed=%d asset_frames=%d threshold=%.2f alpha=%.2f/%.2f cl=%.3f/%.3f/%.3f\n",
                       frame, ctx.processed, ctx.frame_count, TNR_THRESHOLD,
                       TNR_STATIC_ALPHA, TNR_MOTION_ALPHA,
                       ctx.perf.gpu_motion_ms, ctx.perf.gpu_blend_ms,
                       ctx.perf.gpu_queue_total_ms);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / TNR_PAGE_FPS);
    }

    cleanup_tnr_module(ctx.module_ok);
    page_surface_close(&surface);
    free(ctx.output);
    free(ctx.frames);
    return 0;
}
