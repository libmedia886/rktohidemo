#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_surface.h"

#include <linux/dma-buf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define MCF_PAGE_W 1080
#define MCF_PAGE_H 1920
#define MCF_PAGE_STRIDE 1088
#define MCF_PAGE_POOL 1
#define MCF_PAGE_FPS 30
#define MCF_GRP 114
#define MCF_INPUT0_POOL 10
#define MCF_INPUT1_POOL 11
#define MCF_OUTPUT_POOL 12
#define MCF_W 640
#define MCF_H 640
#define MCF_STRIDE 640
#define MCF_FRAME_SIZE (MCF_STRIDE * MCF_H * 3 / 2)
#define MCF_SAMPLE_SECONDS 3
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    const char *name;
    const char *color_path;
    const char *mono_path;
} mcf_sample_t;

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} image_t;

typedef struct {
    uint8_t *color;
    uint8_t *mono;
    uint8_t *output;
    int current_index;
    int processed;
    int module_ok;
    MEDIA_MCF_FUSION_CL_PERF perf;
} mcf_ctx_t;

static const mcf_sample_t g_samples[] = {
    {"street", "assets/loop/mcf_fusion/street_0001/color.jpg", "assets/loop/mcf_fusion/street_0001/mono.jpg"},
    {"urban", "assets/loop/mcf_fusion/urban_0001/color.jpg", "assets/loop/mcf_fusion/urban_0001/mono.jpg"},
    {"country", "assets/loop/mcf_fusion/country_0002/color.jpg", "assets/loop/mcf_fusion/country_0002/mono.jpg"},
};

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

static int load_jpeg_rgb(const char *path, image_t *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return -1;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int width = (int)cinfo.output_width;
    int height = (int)cinfo.output_height;
    int channels = (int)cinfo.output_components;
    uint8_t *rgb = malloc((size_t)width * (size_t)height * 3);
    uint8_t *row = malloc((size_t)width * (size_t)channels);
    if (!rgb || !row || width <= 0 || height <= 0 || channels < 3) {
        free(rgb);
        free(row);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return -1;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW rows[1] = {row};
        JDIMENSION y = cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, rows, 1);
        memcpy(rgb + (size_t)y * (size_t)width * 3, row, (size_t)width * 3);
    }
    free(row);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    out->width = width;
    out->height = height;
    out->rgb = rgb;
    return 0;
}

static void free_image(image_t *img) {
    if (!img) return;
    free(img->rgb);
    memset(img, 0, sizeof(*img));
}

static void image_to_nv12_frame(const image_t *img, uint8_t *dst) {
    if (!img || !img->rgb || !dst || img->width <= 0 || img->height <= 0) return;
    memset(dst, 16, MCF_STRIDE * MCF_H);
    memset(dst + MCF_STRIDE * MCF_H, 128, MCF_STRIDE * MCF_H / 2);

    int out_w = MCF_W;
    int out_h = (int)((int64_t)img->height * MCF_W / img->width);
    if (out_h > MCF_H) {
        out_h = MCF_H;
        out_w = (int)((int64_t)img->width * MCF_H / img->height);
    }
    if (out_w <= 0 || out_h <= 0) return;
    int ox = (MCF_W - out_w) / 2;
    int oy = (MCF_H - out_h) / 2;
    uint8_t *uv = dst + MCF_STRIDE * MCF_H;

    for (int y = 0; y < out_h; ++y) {
        int sy = y * img->height / out_h;
        uint8_t *drow = dst + (size_t)(oy + y) * MCF_STRIDE + ox;
        for (int x = 0; x < out_w; ++x) {
            int sx = x * img->width / out_w;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)uu;
            (void)vv;
            drow[x] = yy;
        }
    }
    for (int y = 0; y < out_h; y += 2) {
        int sy = y * img->height / out_h;
        uint8_t *drow = uv + (size_t)((oy + y) / 2) * MCF_STRIDE + (ox & ~1);
        for (int x = 0; x < out_w; x += 2) {
            int sx = x * img->width / out_w;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)yy;
            drow[x & ~1] = uu;
            drow[(x & ~1) + 1] = vv;
        }
    }
}

static int load_sample_pair(mcf_ctx_t *ctx, int index) {
    image_t color;
    image_t mono;
    if (!ctx || !ctx->color || !ctx->mono ||
        index < 0 || index >= (int)(sizeof(g_samples) / sizeof(g_samples[0]))) {
        return -1;
    }
    if (load_jpeg_rgb(g_samples[index].color_path, &color) != 0) return -1;
    if (load_jpeg_rgb(g_samples[index].mono_path, &mono) != 0) {
        free_image(&color);
        return -1;
    }
    image_to_nv12_frame(&color, ctx->color);
    image_to_nv12_frame(&mono, ctx->mono);
    free_image(&color);
    free_image(&mono);
    ctx->current_index = index;
    return 0;
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

static int send_copied_frame(const char *port, int pool, const uint8_t *src) {
    MEDIA_BUFFER in = {-1, -1};
    if (MEDIA_POOL_GetBuffer(pool, &in) != 0) return -1;
    if (copy_to_buffer(in, src, MCF_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("MCF_FUSION_CL", MCF_GRP, port, in, 1000) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);
    return 0;
}

static int setup_mcf_module(void) {
    if (MEDIA_POOL_Create(MCF_INPUT0_POOL, MCF_FRAME_SIZE, 2) != 0) return -1;
    if (MEDIA_POOL_Create(MCF_INPUT1_POOL, MCF_FRAME_SIZE, 2) != 0) goto fail;
    if (MEDIA_POOL_Create(MCF_OUTPUT_POOL, MCF_FRAME_SIZE, 3) != 0) goto fail;

    MEDIA_MCF_FUSION_CL_ATTR attr = {0};
    attr.width = MCF_W;
    attr.height = MCF_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 2;
    attr.output_pool_id = MCF_OUTPUT_POOL;
    attr.input_stride = MCF_STRIDE;
    attr.output_stride = MCF_STRIDE;
    attr.path = MEDIA_MCF_FUSION_CL_PATH_FUSION;
    attr.normalize_mode = MEDIA_MCF_FUSION_CL_NORMALIZE_MEAN_STD;
    attr.blur_radius = 2;
    attr.base_alpha = 0.25f;
    attr.detail_gain = 0.80f;
    attr.alpha_min = 0.15f;
    attr.alpha_max = 0.85f;
    attr.gain_min = 0.5f;
    attr.gain_max = 2.0f;
    attr.epsilon = 1e-6f;

    if (MEDIA_MCF_FUSION_CL_CreateGrp(MCF_GRP, &attr) != 0 ||
        MEDIA_MCF_FUSION_CL_Enable(MCF_GRP) != 0) {
        MEDIA_MCF_FUSION_CL_DestroyGrp(MCF_GRP);
        goto fail;
    }
    return 0;
fail:
    MEDIA_POOL_Destroy(MCF_OUTPUT_POOL);
    MEDIA_POOL_Destroy(MCF_INPUT1_POOL);
    MEDIA_POOL_Destroy(MCF_INPUT0_POOL);
    return -1;
}

static void cleanup_mcf_module(int enabled) {
    if (!enabled) return;
    MEDIA_MCF_FUSION_CL_Disable(MCF_GRP);
    MEDIA_MCF_FUSION_CL_DestroyGrp(MCF_GRP);
    MEDIA_POOL_Destroy(MCF_OUTPUT_POOL);
    MEDIA_POOL_Destroy(MCF_INPUT1_POOL);
    MEDIA_POOL_Destroy(MCF_INPUT0_POOL);
}

static int process_mcf(mcf_ctx_t *ctx) {
    if (!ctx || !ctx->color || !ctx->mono || !ctx->output || !ctx->module_ok) return -1;
    MEDIA_BUFFER out = {-1, -1};
    if (send_copied_frame("input0", MCF_INPUT0_POOL, ctx->color) != 0) return -1;
    if (send_copied_frame("input1", MCF_INPUT1_POOL, ctx->mono) != 0) return -1;
    if (MEDIA_MCF_FUSION_CL_GetFrame(MCF_GRP, &out, 3000) != 0) return -1;
    int ret = copy_from_buffer(out, ctx->output, MCF_FRAME_SIZE);
    MEDIA_MCF_FUSION_CL_ReleaseFrame(MCF_GRP, out);
    if (ret == 0) {
        ctx->processed++;
        (void)MEDIA_MCF_FUSION_CL_GetLastPerf(MCF_GRP, &ctx->perf);
    }
    return ret;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * MCF_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * MCF_STRIDE;
        for (int x = 0; x < dw; ++x) {
            int sx = x * MCF_W / dw;
            drow[x] = srow[sx];
        }
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)MCF_STRIDE * MCF_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (MCF_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * MCF_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = ((x * MCF_W / dw) & ~1);
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_mcf_page(uint8_t *dst, int stride, int width, int height,
                          int frame, void *opaque) {
    mcf_ctx_t *ctx = (mcf_ctx_t *)opaque;
    int sample_count = (int)(sizeof(g_samples) / sizeof(g_samples[0]));
    int sample = (frame / (MCF_PAGE_FPS * MCF_SAMPLE_SECONDS)) % sample_count;
    if (ctx && sample != ctx->current_index) {
        if (load_sample_pair(ctx, sample) == 0) {
            if (!ctx->module_ok || process_mcf(ctx) != 0) {
                memcpy(ctx->output, ctx->color, MCF_FRAME_SIZE);
            }
        }
    }

    char sample_text[48];
    char processed[48];
    char perf[72];
    snprintf(sample_text, sizeof(sample_text), "SAMPLE %02d/%02d %s",
             ctx ? ctx->current_index + 1 : 0, sample_count,
             ctx && ctx->current_index >= 0 ? g_samples[ctx->current_index].name : "NA");
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);
    snprintf(perf, sizeof(perf), "CL S %03d US F %03d US T %03d US",
             ctx ? (int)(ctx->perf.stats_kernel_ms * 1000.0) : 0,
             ctx ? (int)(ctx->perf.fusion_kernel_ms * 1000.0) : 0,
             ctx ? (int)(ctx->perf.gpu_total_ms * 1000.0) : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "MCF FUSION CL", 7, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 60, 142,
                           sample_text, 3, 210, 144, 84);

    int margin = 52;
    int gap = 18;
    int pane_w = (width - margin * 2 - gap) / 2;
    int pane_h = 430;
    int y0 = 290;
    int out_y = 812;
    int out_h = 650;
    page_surface_draw_text(dst, stride, width, height, margin, y0 - 32,
                           "COLOR INPUT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, margin + pane_w + gap, y0 - 32,
                           "MONO DETAIL", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, margin, out_y - 32,
                           "FUSION OUTPUT", 3, 220, 108, 176);
    if (ctx) {
        draw_nv12_scaled(dst, stride, width, height, margin, y0, pane_w, pane_h, ctx->color);
        draw_nv12_scaled(dst, stride, width, height, margin + pane_w + gap, y0,
                         pane_w, pane_h, ctx->mono);
        draw_nv12_scaled(dst, stride, width, height, margin, out_y,
                         width - margin * 2, out_h, ctx->output);
    }
    page_surface_draw_text(dst, stride, width, height, 70, 1900 - 160,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1900 - 95,
                           perf, 2, 210, 108, 176);
}

int page_mcf_fusion_cl_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    mcf_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.current_index = -1;
    ctx.color = malloc(MCF_FRAME_SIZE);
    ctx.mono = malloc(MCF_FRAME_SIZE);
    ctx.output = malloc(MCF_FRAME_SIZE);
    if (!ctx.color || !ctx.mono || !ctx.output) {
        free(ctx.color); free(ctx.mono); free(ctx.output);
        return 1;
    }
    memset(ctx.color, 16, MCF_FRAME_SIZE);
    memset(ctx.mono, 16, MCF_FRAME_SIZE);
    memset(ctx.output, 16, MCF_FRAME_SIZE);
    memset(ctx.color + MCF_STRIDE * MCF_H, 128, MCF_STRIDE * MCF_H / 2);
    memset(ctx.mono + MCF_STRIDE * MCF_H, 128, MCF_STRIDE * MCF_H / 2);
    memset(ctx.output + MCF_STRIDE * MCF_H, 128, MCF_STRIDE * MCF_H / 2);

    if (page_surface_open(&surface, MCF_PAGE_POOL, MCF_PAGE_W, MCF_PAGE_H,
                          MCF_PAGE_STRIDE, MCF_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.color); free(ctx.mono); free(ctx.output);
        return 1;
    }

    ctx.module_ok = setup_mcf_module() == 0;
    if (ctx.module_ok) set_tile_status("MCF_FUSION_CL", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("MCF_FUSION_CL standalone page module=%s samples=%zu. Ctrl+C to stop.\n",
           ctx.module_ok ? "live" : "fallback",
           sizeof(g_samples) / sizeof(g_samples[0]));

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_mcf_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % MCF_PAGE_FPS) == 0) {
                printf("MCF_FUSION_CL frames=%d sample=%d/%zu updates=%d mode=%s cl=%.3f/%.3f/%.3f standalone=1\n",
                       frame, ctx.current_index + 1,
                       sizeof(g_samples) / sizeof(g_samples[0]),
                       ctx.processed, ctx.module_ok ? "opencl" : "fallback",
                       ctx.perf.stats_kernel_ms, ctx.perf.fusion_kernel_ms,
                       ctx.perf.gpu_total_ms);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / MCF_PAGE_FPS);
    }

    cleanup_mcf_module(ctx.module_ok);
    page_surface_close(&surface);
    free(ctx.color);
    free(ctx.mono);
    free(ctx.output);
    return 0;
}
