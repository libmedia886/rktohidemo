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

#define BP_PAGE_W 1080
#define BP_PAGE_H 1920
#define BP_PAGE_STRIDE 1088
#define BP_PAGE_POOL 1
#define BP_PAGE_FPS 30
#define BP_GRP 118
#define BP_INPUT0_POOL 10
#define BP_INPUT1_POOL 11
#define BP_OUTPUT_POOL 12
#define BP_MASK_POOL 14
#define BP_W 640
#define BP_H 640
#define BP_STRIDE 640
#define BP_FRAME_SIZE (BP_STRIDE * BP_H * 3 / 2)
#define BP_SAMPLE_SECONDS 3
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    const char *name;
    const char *path;
} blend_sample_t;

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} image_t;

typedef struct {
    uint8_t *base;
    uint8_t *input0;
    uint8_t *input1;
    uint8_t *linear;
    uint8_t *output;
    int current_index;
    int processed;
    int module_ok;
} blend_ctx_t;

static const blend_sample_t g_samples[] = {
    {"0002", "assets/loop/edof/mfi_whu/0002/a.jpg"},
    {"0019", "assets/loop/edof/mfi_whu/0019/a.jpg"},
    {"0041", "assets/loop/edof/mfi_whu/0041/a.jpg"},
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
    memset(dst, 16, BP_STRIDE * BP_H);
    memset(dst + BP_STRIDE * BP_H, 128, BP_STRIDE * BP_H / 2);

    int out_w = BP_W;
    int out_h = (int)((int64_t)img->height * BP_W / img->width);
    if (out_h > BP_H) {
        out_h = BP_H;
        out_w = (int)((int64_t)img->width * BP_H / img->height);
    }
    if (out_w <= 0 || out_h <= 0) return;
    int ox = (BP_W - out_w) / 2;
    int oy = (BP_H - out_h) / 2;
    uint8_t *uv = dst + BP_STRIDE * BP_H;

    for (int y = 0; y < out_h; ++y) {
        int sy = y * img->height / out_h;
        uint8_t *drow = dst + (size_t)(oy + y) * BP_STRIDE + ox;
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
        uint8_t *drow = uv + (size_t)((oy + y) / 2) * BP_STRIDE + (ox & ~1);
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

static int alpha_for_x(int x, int width) {
    int center = width / 2;
    int feather = width / 7;
    int left = center - feather / 2;
    int right = center + feather / 2;
    if (x <= left) return 0;
    if (x >= right) return 255;
    return ((x - left) * 255) / (right - left);
}

static void fill_mask_frame(uint8_t *dst) {
    if (!dst) return;
    uint8_t *uv = dst + (size_t)BP_STRIDE * BP_H;
    for (int y = 0; y < BP_H; ++y) {
        uint8_t *row = dst + (size_t)y * BP_STRIDE;
        for (int x = 0; x < BP_W; ++x) row[x] = (uint8_t)alpha_for_x(x, BP_W);
    }
    for (int y = 0; y < BP_H / 2; ++y) {
        memset(uv + (size_t)y * BP_STRIDE, 128, BP_STRIDE);
    }
}

static void block_blur_nv12(const uint8_t *src, uint8_t *dst, int block) {
    if (!src || !dst || block <= 0) return;
    for (int by = 0; by < BP_H; by += block) {
        int bh = BP_H - by;
        if (bh > block) bh = block;
        for (int bx = 0; bx < BP_W; bx += block) {
            int bw = BP_W - bx;
            if (bw > block) bw = block;
            int sum = 0;
            for (int y = 0; y < bh; ++y) {
                const uint8_t *row = src + (size_t)(by + y) * BP_STRIDE + bx;
                for (int x = 0; x < bw; ++x) sum += row[x];
            }
            uint8_t avg = (uint8_t)(sum / (bw * bh));
            for (int y = 0; y < bh; ++y) {
                memset(dst + (size_t)(by + y) * BP_STRIDE + bx, avg, bw);
            }
        }
    }

    const uint8_t *src_uv = src + (size_t)BP_STRIDE * BP_H;
    uint8_t *dst_uv = dst + (size_t)BP_STRIDE * BP_H;
    for (int by = 0; by < BP_H / 2; by += block) {
        int bh = BP_H / 2 - by;
        if (bh > block) bh = block;
        for (int bx = 0; bx < BP_W; bx += block) {
            int bw = BP_W - bx;
            if (bw > block) bw = block;
            bw &= ~1;
            if (bw <= 0) continue;
            int sum_u = 0;
            int sum_v = 0;
            int count = 0;
            for (int y = 0; y < bh; ++y) {
                const uint8_t *row = src_uv + (size_t)(by + y) * BP_STRIDE + bx;
                for (int x = 0; x < bw; x += 2) {
                    sum_u += row[x];
                    sum_v += row[x + 1];
                    count++;
                }
            }
            uint8_t avg_u = count > 0 ? (uint8_t)(sum_u / count) : 128;
            uint8_t avg_v = count > 0 ? (uint8_t)(sum_v / count) : 128;
            for (int y = 0; y < bh; ++y) {
                uint8_t *row = dst_uv + (size_t)(by + y) * BP_STRIDE + bx;
                for (int x = 0; x < bw; x += 2) {
                    row[x] = avg_u;
                    row[x + 1] = avg_v;
                }
            }
        }
    }
}

static void make_lr_focus_pair(const uint8_t *base, uint8_t *left_clear, uint8_t *right_clear) {
    if (!base || !left_clear || !right_clear) return;
    uint8_t *blur = malloc(BP_FRAME_SIZE);
    if (!blur) {
        memcpy(left_clear, base, BP_FRAME_SIZE);
        memcpy(right_clear, base, BP_FRAME_SIZE);
        return;
    }
    block_blur_nv12(base, blur, 14);
    for (int y = 0; y < BP_H; ++y) {
        const uint8_t *sharp = base + (size_t)y * BP_STRIDE;
        const uint8_t *soft = blur + (size_t)y * BP_STRIDE;
        uint8_t *row_l = left_clear + (size_t)y * BP_STRIDE;
        uint8_t *row_r = right_clear + (size_t)y * BP_STRIDE;
        for (int x = 0; x < BP_W; ++x) {
            int a = alpha_for_x(x, BP_W);
            row_l[x] = (uint8_t)((sharp[x] * (255 - a) + soft[x] * a + 127) / 255);
            row_r[x] = (uint8_t)((soft[x] * (255 - a) + sharp[x] * a + 127) / 255);
        }
    }
    for (int y = 0; y < BP_H / 2; ++y) {
        const uint8_t *sharp = base + (size_t)BP_STRIDE * BP_H + (size_t)y * BP_STRIDE;
        const uint8_t *soft = blur + (size_t)BP_STRIDE * BP_H + (size_t)y * BP_STRIDE;
        uint8_t *row_l = left_clear + (size_t)BP_STRIDE * BP_H + (size_t)y * BP_STRIDE;
        uint8_t *row_r = right_clear + (size_t)BP_STRIDE * BP_H + (size_t)y * BP_STRIDE;
        for (int x = 0; x < BP_W; ++x) {
            int a = alpha_for_x(x, BP_W);
            row_l[x] = (uint8_t)((sharp[x] * (255 - a) + soft[x] * a + 127) / 255);
            row_r[x] = (uint8_t)((soft[x] * (255 - a) + sharp[x] * a + 127) / 255);
        }
    }
    free(blur);
}

static void compose_linear(const uint8_t *src0, const uint8_t *src1, uint8_t *dst) {
    if (!src0 || !src1 || !dst) return;
    for (int y = 0; y < BP_H; ++y) {
        const uint8_t *r0 = src0 + (size_t)y * BP_STRIDE;
        const uint8_t *r1 = src1 + (size_t)y * BP_STRIDE;
        uint8_t *ro = dst + (size_t)y * BP_STRIDE;
        for (int x = 0; x < BP_W; ++x) {
            int a = alpha_for_x(x, BP_W);
            ro[x] = (uint8_t)((r0[x] * (255 - a) + r1[x] * a + 127) / 255);
        }
    }
    for (int y = 0; y < BP_H / 2; ++y) {
        const uint8_t *r0 = src0 + (size_t)BP_STRIDE * BP_H + (size_t)y * BP_STRIDE;
        const uint8_t *r1 = src1 + (size_t)BP_STRIDE * BP_H + (size_t)y * BP_STRIDE;
        uint8_t *ro = dst + (size_t)BP_STRIDE * BP_H + (size_t)y * BP_STRIDE;
        for (int x = 0; x < BP_W; ++x) {
            int a = alpha_for_x(x, BP_W);
            ro[x] = (uint8_t)((r0[x] * (255 - a) + r1[x] * a + 127) / 255);
        }
    }
}

static int load_sample(blend_ctx_t *ctx, int index) {
    image_t img;
    if (!ctx || !ctx->base || !ctx->input0 || !ctx->input1 || !ctx->linear ||
        index < 0 || index >= (int)(sizeof(g_samples) / sizeof(g_samples[0]))) {
        return -1;
    }
    if (load_jpeg_rgb(g_samples[index].path, &img) != 0) return -1;
    image_to_nv12_frame(&img, ctx->base);
    free_image(&img);
    make_lr_focus_pair(ctx->base, ctx->input0, ctx->input1);
    compose_linear(ctx->input0, ctx->input1, ctx->linear);
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
    if (copy_to_buffer(in, src, BP_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("BLEND_PYR", BP_GRP, port, in, 1000) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);
    return 0;
}

static int setup_blend_module(void) {
    MEDIA_BUFFER mask = {-1, -1};
    uint8_t *mask_frame = NULL;
    if (MEDIA_POOL_Create(BP_INPUT0_POOL, BP_FRAME_SIZE, 2) != 0) return -1;
    if (MEDIA_POOL_Create(BP_INPUT1_POOL, BP_FRAME_SIZE, 2) != 0) goto fail;
    if (MEDIA_POOL_Create(BP_OUTPUT_POOL, BP_FRAME_SIZE, 4) != 0) goto fail;
    if (MEDIA_POOL_Create(BP_MASK_POOL, BP_FRAME_SIZE, 1) != 0) goto fail;

    MEDIA_BLEND_PYR_ATTR attr = {0};
    attr.width = BP_W;
    attr.height = BP_H;
    attr.input_stride = BP_STRIDE;
    attr.input_depth = 2;
    attr.input_format = MEDIA_FORMAT_NV12;
    attr.output_stride = BP_STRIDE;
    if (MEDIA_BLEND_PYR_SetAttr(BP_GRP, &attr) != 0) goto fail_module;

    mask_frame = malloc(BP_FRAME_SIZE);
    if (!mask_frame) goto fail_module;
    fill_mask_frame(mask_frame);
    if (MEDIA_POOL_GetBuffer(BP_MASK_POOL, &mask) != 0 ||
        copy_to_buffer(mask, mask_frame, BP_FRAME_SIZE) != 0 ||
        MEDIA_BLEND_PYR_SetMask(BP_GRP, &mask) != 0 ||
        MEDIA_BLEND_PYR_GetMaskStatus(BP_GRP) != 1 ||
        MEDIA_BLEND_PYR_Enable(BP_GRP) != 0) {
        if (mask.pool_id >= 0) MEDIA_POOL_PutBuffer(mask);
        goto fail_module;
    }
    free(mask_frame);
    return 0;

fail_module:
    free(mask_frame);
    MEDIA_BLEND_PYR_DestroyGrp(BP_GRP);
fail:
    MEDIA_POOL_Destroy(BP_MASK_POOL);
    MEDIA_POOL_Destroy(BP_OUTPUT_POOL);
    MEDIA_POOL_Destroy(BP_INPUT1_POOL);
    MEDIA_POOL_Destroy(BP_INPUT0_POOL);
    return -1;
}

static void cleanup_blend_module(int enabled) {
    if (!enabled) return;
    MEDIA_BLEND_PYR_Disable(BP_GRP);
    MEDIA_BLEND_PYR_DestroyGrp(BP_GRP);
    MEDIA_POOL_Destroy(BP_MASK_POOL);
    MEDIA_POOL_Destroy(BP_OUTPUT_POOL);
    MEDIA_POOL_Destroy(BP_INPUT1_POOL);
    MEDIA_POOL_Destroy(BP_INPUT0_POOL);
}

static int process_blend(blend_ctx_t *ctx) {
    if (!ctx || !ctx->input0 || !ctx->input1 || !ctx->output || !ctx->module_ok) return -1;
    MEDIA_BUFFER out = {-1, -1};
    if (send_copied_frame("input0", BP_INPUT0_POOL, ctx->input0) != 0) return -1;
    if (send_copied_frame("input1", BP_INPUT1_POOL, ctx->input1) != 0) return -1;
    if (MEDIA_BLEND_PYR_GetFrame(BP_GRP, &out, 2000) != 0) return -1;
    int ret = copy_from_buffer(out, ctx->output, BP_FRAME_SIZE);
    MEDIA_BLEND_PYR_ReleaseFrame(BP_GRP, out);
    if (ret == 0) ctx->processed++;
    return ret;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * BP_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * BP_STRIDE;
        for (int x = 0; x < dw; ++x) drow[x] = srow[x * BP_W / dw];
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)BP_STRIDE * BP_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (BP_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * BP_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = ((x * BP_W / dw) & ~1);
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_blend_page(uint8_t *dst, int stride, int width, int height,
                            int frame, void *opaque) {
    blend_ctx_t *ctx = (blend_ctx_t *)opaque;
    int sample_count = (int)(sizeof(g_samples) / sizeof(g_samples[0]));
    int sample = (frame / (BP_PAGE_FPS * BP_SAMPLE_SECONDS)) % sample_count;
    if (ctx && sample != ctx->current_index) {
        if (load_sample(ctx, sample) == 0) {
            if (!ctx->module_ok || process_blend(ctx) != 0) {
                memcpy(ctx->output, ctx->linear, BP_FRAME_SIZE);
            }
        }
    }

    char sample_text[48];
    char processed[48];
    snprintf(sample_text, sizeof(sample_text), "SAMPLE %02d/%02d %s",
             ctx ? ctx->current_index + 1 : 0, sample_count,
             ctx && ctx->current_index >= 0 ? g_samples[ctx->current_index].name : "NA");
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "BLEND PYR", 9, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 60, 142,
                           sample_text, 3, 210, 144, 84);

    int margin = 52;
    int gap = 18;
    int pane_w = (width - margin * 2 - gap) / 2;
    int pane_h = 430;
    int y0 = 310;
    int y1 = 820;
    page_surface_draw_text(dst, stride, width, height, margin, y0 - 34,
                           "LEFT CLEAR", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, margin + pane_w + gap, y0 - 34,
                           "RIGHT CLEAR", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, margin, y1 - 34,
                           "LINEAR MASK", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, margin + pane_w + gap, y1 - 34,
                           "BLEND PYR OUT", 3, 220, 108, 176);
    if (ctx) {
        draw_nv12_scaled(dst, stride, width, height, margin, y0, pane_w, pane_h, ctx->input0);
        draw_nv12_scaled(dst, stride, width, height, margin + pane_w + gap, y0, pane_w, pane_h, ctx->input1);
        draw_nv12_scaled(dst, stride, width, height, margin, y1, pane_w, pane_h, ctx->linear);
        draw_nv12_scaled(dst, stride, width, height, margin + pane_w + gap, y1, pane_w, pane_h, ctx->output);
    }
    page_surface_draw_text(dst, stride, width, height, 70, 1515,
                           "CENTER SOFT MASK PYRAMID FUSION",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1580,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1645,
                           ctx && ctx->module_ok ? "MODULE LIVE" : "MODULE FALLBACK",
                           3, 210, 108, 176);
}

int page_blend_pyr_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    blend_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.current_index = -1;
    ctx.base = malloc(BP_FRAME_SIZE);
    ctx.input0 = malloc(BP_FRAME_SIZE);
    ctx.input1 = malloc(BP_FRAME_SIZE);
    ctx.linear = malloc(BP_FRAME_SIZE);
    ctx.output = malloc(BP_FRAME_SIZE);
    if (!ctx.base || !ctx.input0 || !ctx.input1 || !ctx.linear || !ctx.output) {
        free(ctx.base); free(ctx.input0); free(ctx.input1); free(ctx.linear); free(ctx.output);
        return 1;
    }
    memset(ctx.base, 16, BP_FRAME_SIZE);
    memset(ctx.input0, 16, BP_FRAME_SIZE);
    memset(ctx.input1, 16, BP_FRAME_SIZE);
    memset(ctx.linear, 16, BP_FRAME_SIZE);
    memset(ctx.output, 16, BP_FRAME_SIZE);
    memset(ctx.base + BP_STRIDE * BP_H, 128, BP_STRIDE * BP_H / 2);
    memset(ctx.input0 + BP_STRIDE * BP_H, 128, BP_STRIDE * BP_H / 2);
    memset(ctx.input1 + BP_STRIDE * BP_H, 128, BP_STRIDE * BP_H / 2);
    memset(ctx.linear + BP_STRIDE * BP_H, 128, BP_STRIDE * BP_H / 2);
    memset(ctx.output + BP_STRIDE * BP_H, 128, BP_STRIDE * BP_H / 2);

    if (page_surface_open(&surface, BP_PAGE_POOL, BP_PAGE_W, BP_PAGE_H,
                          BP_PAGE_STRIDE, BP_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.base); free(ctx.input0); free(ctx.input1); free(ctx.linear); free(ctx.output);
        return 1;
    }

    ctx.module_ok = setup_blend_module() == 0;
    if (ctx.module_ok) set_tile_status("BLEND_PYR", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("BLEND_PYR standalone page module=%s samples=%zu. Ctrl+C to stop.\n",
           ctx.module_ok ? "live" : "fallback",
           sizeof(g_samples) / sizeof(g_samples[0]));

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_blend_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % BP_PAGE_FPS) == 0) {
                printf("BLEND_PYR frames=%d sample=%d/%zu updates=%d mode=%s mask=seam-band standalone=1\n",
                       frame, ctx.current_index + 1,
                       sizeof(g_samples) / sizeof(g_samples[0]),
                       ctx.processed, ctx.module_ok ? "module" : "fallback");
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / BP_PAGE_FPS);
    }

    cleanup_blend_module(ctx.module_ok);
    page_surface_close(&surface);
    free(ctx.base);
    free(ctx.input0);
    free(ctx.input1);
    free(ctx.linear);
    free(ctx.output);
    return 0;
}
