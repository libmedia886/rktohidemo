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

#define VM_PAGE_W 1080
#define VM_PAGE_H 1920
#define VM_PAGE_STRIDE 1088
#define VM_PAGE_POOL 1
#define VM_PAGE_FPS 30
#define VM_GRP 80
#define VM_INPUT_POOL 6
#define VM_OUTPUT_POOL 7
#define VM_INPUTS 4
#define VM_IN_W 320
#define VM_IN_H 320
#define VM_IN_STRIDE 320
#define VM_OUT_W 640
#define VM_OUT_H 640
#define VM_OUT_STRIDE 640
#define VM_IN_SIZE (VM_IN_STRIDE * VM_IN_H * 3 / 2)
#define VM_OUT_SIZE (VM_OUT_STRIDE * VM_OUT_H * 3 / 2)
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *inputs[VM_INPUTS];
    uint8_t *output;
    int processed;
    int module_ok;
} vmix_ctx_t;

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

static void fill_input_nv12(uint8_t *dst, int input_id, int frame) {
    static const uint8_t base_y[VM_INPUTS] = {58, 88, 118, 148};
    static const uint8_t base_u[VM_INPUTS] = {86, 148, 96, 172};
    static const uint8_t base_v[VM_INPUTS] = {170, 92, 150, 108};
    if (!dst || input_id < 0 || input_id >= VM_INPUTS) return;
    uint8_t *uv = dst + (size_t)VM_IN_STRIDE * VM_IN_H;
    for (int y = 0; y < VM_IN_H; ++y) {
        uint8_t *row = dst + (size_t)y * VM_IN_STRIDE;
        for (int x = 0; x < VM_IN_W; ++x) {
            int grid = (((x + frame * (2 + input_id)) / 38) ^
                        ((y + frame * (3 + input_id)) / 38)) & 1;
            int luma = base_y[input_id] + x * 56 / VM_IN_W + y * 46 / VM_IN_H + (grid ? 26 : -8);
            if (luma < 16) luma = 16;
            if (luma > 235) luma = 235;
            row[x] = (uint8_t)luma;
        }
    }
    for (int y = 0; y < VM_IN_H / 2; ++y) {
        uint8_t *row = uv + (size_t)y * VM_IN_STRIDE;
        for (int x = 0; x < VM_IN_W; x += 2) {
            row[x] = (uint8_t)(base_u[input_id] + ((x + frame * 3) % 28));
            row[x + 1] = (uint8_t)(base_v[input_id] + ((y + frame * 4) % 28));
        }
    }
}

static int setup_vmix_module(void) {
    if (MEDIA_POOL_Create(VM_INPUT_POOL, VM_IN_SIZE, VM_INPUTS * 2) != 0) return -1;
    if (MEDIA_POOL_Create(VM_OUTPUT_POOL, VM_OUT_SIZE, 4) != 0) {
        MEDIA_POOL_Destroy(VM_INPUT_POOL);
        return -1;
    }

    MEDIA_VMIX_ATTR attr = {0};
    attr.input_count = VM_INPUTS;
    attr.output_width = VM_OUT_W;
    attr.output_height = VM_OUT_H;
    attr.output_stride = VM_OUT_STRIDE;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_pool_id = VM_OUTPUT_POOL;
    attr.primary_index = -1;
    for (int i = 0; i < VM_INPUTS; ++i) {
        MEDIA_VMIX_CHANNEL *ch = &attr.channels[i];
        ch->enabled = 1;
        ch->x = 32 + (i % 2) * 304;
        ch->y = 32 + (i / 2) * 304;
        ch->width = 272;
        ch->height = 272;
        ch->alpha = i == 0 ? 1.0f : 0.88f;
        ch->stride = VM_IN_STRIDE;
        ch->format = MEDIA_FORMAT_NV12;
    }

    if (MEDIA_VMIX_CreateGrp(VM_GRP, &attr) != 0 ||
        MEDIA_VMIX_Start(VM_GRP) != 0 ||
        MEDIA_VMIX_Enable(VM_GRP) != 0) {
        MEDIA_VMIX_Disable(VM_GRP);
        MEDIA_VMIX_Stop(VM_GRP);
        MEDIA_VMIX_DestroyGrp(VM_GRP);
        MEDIA_POOL_Destroy(VM_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VM_INPUT_POOL);
        return -1;
    }
    return 0;
}

static void cleanup_vmix_module(int enabled) {
    if (!enabled) return;
    MEDIA_VMIX_Disable(VM_GRP);
    MEDIA_VMIX_Stop(VM_GRP);
    MEDIA_VMIX_DestroyGrp(VM_GRP);
    MEDIA_POOL_Destroy(VM_OUTPUT_POOL);
    MEDIA_POOL_Destroy(VM_INPUT_POOL);
}

static int send_vmix_input(int input_id, const uint8_t *src) {
    MEDIA_BUFFER in = {-1, -1};
    char port[16];
    if (MEDIA_POOL_GetBuffer(VM_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, VM_IN_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    snprintf(port, sizeof(port), "input%d", input_id);
    if (MEDIA_SYS_SendFrame("VMIX", VM_GRP, port, in, 1000) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);
    return 0;
}

static int process_vmix(vmix_ctx_t *ctx) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (!ctx || !ctx->output || !ctx->module_ok) return -1;
    for (int i = 0; i < VM_INPUTS; ++i) {
        if (!ctx->inputs[i] || send_vmix_input(i, ctx->inputs[i]) != 0) return -1;
    }
    if (MEDIA_VMIX_GetFrame(VM_GRP, &out, 1000) == 0) {
        ret = copy_from_buffer(out, ctx->output, VM_OUT_SIZE);
        MEDIA_VMIX_ReleaseFrame(VM_GRP, out);
    }
    if (ret == 0) ctx->processed++;
    return ret;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh,
                             const uint8_t *src, int sw, int sh, int sstride) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * sh / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * sstride;
        for (int x = 0; x < dw; ++x) drow[x] = srow[x * sw / dw];
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)sstride * sh;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (sh / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * sstride;
        for (int x = 0; x < dw; x += 2) {
            int sx = (x * sw / dw) & ~1;
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_vmix_page(uint8_t *dst, int stride, int width, int height,
                           int frame, void *opaque) {
    vmix_ctx_t *ctx = (vmix_ctx_t *)opaque;
    (void)frame;
    char processed[64];
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 8, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "VMIX", 7, 235, 108, 176);

    int pane = 640;
    int x = (width - pane) / 2;
    int y = 270;
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, y - 34,
                           "MIXED OUTPUT", 3, 220, 108, 176);
    if (ctx) {
        draw_nv12_scaled(dst, stride, width, height, x, y, pane, pane,
                         ctx->output, VM_OUT_W, VM_OUT_H, VM_OUT_STRIDE);
    }

    int thumb = 180;
    int tx = (width - (thumb * 4 + 28 * 3)) / 2;
    int ty = 1030;
    for (int i = 0; i < VM_INPUTS; ++i) {
        int ix = tx + i * (thumb + 28);
        page_surface_fill_rect_nv12(dst, stride, width, height, ix - 8, ty - 34,
                                    thumb + 16, thumb + 58, 18, 128, 128);
        char label[16];
        snprintf(label, sizeof(label), "INPUT%d", i);
        page_surface_draw_text(dst, stride, width, height, ix, ty - 24,
                               label, 1, 220, 108, 176);
        if (ctx) {
            draw_nv12_scaled(dst, stride, width, height, ix, ty, thumb, thumb,
                             ctx->inputs[i], VM_IN_W, VM_IN_H, VM_IN_STRIDE);
        }
    }
    page_surface_draw_text(dst, stride, width, height, 70, 1510,
                           "4 INPUT VIDEO MIX", 2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1590,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1690,
                           ctx && ctx->module_ok ? "MODULE LIVE" : "MODULE FALLBACK",
                           3, 210, 108, 176);
}

int page_vmix_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    vmix_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    for (int i = 0; i < VM_INPUTS; ++i) ctx.inputs[i] = malloc(VM_IN_SIZE);
    ctx.output = malloc(VM_OUT_SIZE);
    if (!ctx.output) goto fail_alloc;
    for (int i = 0; i < VM_INPUTS; ++i) if (!ctx.inputs[i]) goto fail_alloc;
    for (int i = 0; i < VM_INPUTS; ++i) memset(ctx.inputs[i], 16, VM_IN_SIZE);
    memset(ctx.output, 0, VM_OUT_SIZE);

    if (page_surface_open(&surface, VM_PAGE_POOL, VM_PAGE_W, VM_PAGE_H,
                          VM_PAGE_STRIDE, VM_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        goto fail_alloc;
    }

    ctx.module_ok = setup_vmix_module() == 0;
    if (ctx.module_ok) set_tile_status("VMIX", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("VMIX standalone page module=%s inputs=%d output=%dx%d. Ctrl+C to stop.\n",
           ctx.module_ok ? "live" : "fallback", VM_INPUTS, VM_OUT_W, VM_OUT_H);

    int frame = 0;
    while (!running || *running) {
        for (int i = 0; i < VM_INPUTS; ++i) fill_input_nv12(ctx.inputs[i], i, frame);
        if (!ctx.module_ok || process_vmix(&ctx) != 0) memset(ctx.output, 0, VM_OUT_SIZE);
        if (page_surface_send_frame(&surface, draw_vmix_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % VM_PAGE_FPS) == 0) {
                printf("VMIX frames=%d processed=%d inputs=%d standalone=1\n",
                       frame, ctx.processed, VM_INPUTS);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / VM_PAGE_FPS);
    }

    cleanup_vmix_module(ctx.module_ok);
    page_surface_close(&surface);
    free(ctx.output);
    for (int i = 0; i < VM_INPUTS; ++i) free(ctx.inputs[i]);
    return 0;

fail_alloc:
    free(ctx.output);
    for (int i = 0; i < VM_INPUTS; ++i) free(ctx.inputs[i]);
    return 1;
}
