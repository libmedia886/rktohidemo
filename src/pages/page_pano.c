#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_surface.h"

#include <linux/dma-buf.h>
#include <stdio.h>
#include <jpeglib.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PANO_PAGE_W 1080
#define PANO_PAGE_H 1920
#define PANO_PAGE_STRIDE 1088
#define PANO_PAGE_POOL 1
#define PANO_PAGE_FPS 30
#define PANO_GRP 74
#define PANO_INPUT_POOL 6
#define PANO_OUTPUT_POOL 7
#define PANO_INPUT_COUNT 6
#define PANO_DOMAIN_W 8378
#define PANO_DOMAIN_H 4189
#define PANO_LUT_W 1920
#define PANO_LUT_H 960
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define PANO_STITCH_W PANO_DOMAIN_W
#define PANO_STITCH_H ALIGN_UP(PANO_DOMAIN_H, 2)
#define PANO_STITCH_STRIDE ALIGN_UP(PANO_STITCH_W, 4)
#define PANO_OUTPUT_SIZE ((size_t)PANO_STITCH_STRIDE * PANO_STITCH_H * 3 / 2)
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} pano_image_t;

typedef struct {
    const char *pto_path;
    const char *image_paths[PANO_INPUT_COUNT];
    pano_image_t inputs[PANO_INPUT_COUNT];
    uint8_t *nv12[PANO_INPUT_COUNT];
    int input_count;
    int in_w;
    int in_h;
    int loaded;
    int module_ok;
    int output_ready;
    char mode[32];
    uint8_t *output;
} pano_ctx_t;

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

static int load_jpeg_rgb(const char *path, pano_image_t *out) {
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

static void free_image(pano_image_t *img) {
    if (!img) return;
    free(img->rgb);
    memset(img, 0, sizeof(*img));
}

static void image_to_nv12_packed(const pano_image_t *img, uint8_t *dst) {
    if (!img || !img->rgb || !dst || img->width <= 0 || img->height <= 0) return;
    int w = img->width;
    int h = img->height;
    uint8_t *yp = dst;
    uint8_t *uv = dst + (size_t)w * h;

    for (int y = 0; y < h; ++y) {
        uint8_t *drow = yp + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            const uint8_t *rgb = img->rgb + ((size_t)y * w + x) * 3;
            uint8_t yy, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &u, &v);
            (void)u;
            (void)v;
            drow[x] = yy;
        }
    }

    for (int y = 0; y < h; y += 2) {
        uint8_t *drow = uv + (size_t)(y / 2) * w;
        for (int x = 0; x < w; x += 2) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const uint8_t *rgb = img->rgb + ((size_t)(y + dy) * w + (x + dx)) * 3;
                    r += rgb[0];
                    g += rgb[1];
                    b += rgb[2];
                }
            }
            uint8_t yy, u, v;
            rgb_to_yuv((uint8_t)(r / 4), (uint8_t)(g / 4), (uint8_t)(b / 4), &yy, &u, &v);
            (void)yy;
            drow[x] = u;
            drow[x + 1] = v;
        }
    }
}

static int init_pano_ctx(pano_ctx_t *ctx) {
    static const char *paths[PANO_INPUT_COUNT] = {
        "assets/loop/pano/sample2/camera_0.jpg",
        "assets/loop/pano/sample2/camera_1.jpg",
        "assets/loop/pano/sample2/camera_2.jpg",
        "assets/loop/pano/sample2/camera_3.jpg",
        "assets/loop/pano/sample2/camera_4.jpg",
        "assets/loop/pano/sample2/camera_5.jpg",
    };
    if (!ctx) return 0;
    memset(ctx, 0, sizeof(*ctx));
    ctx->pto_path = "assets/loop/pano/sample2/calib_file.pto";
    ctx->input_count = PANO_INPUT_COUNT;
    snprintf(ctx->mode, sizeof(ctx->mode), "not-ready");
    for (int i = 0; i < PANO_INPUT_COUNT; ++i) ctx->image_paths[i] = paths[i];
    return 1;
}

static int load_pano_assets(pano_ctx_t *ctx) {
    if (!ctx) return 0;
    if (access(ctx->pto_path, R_OK) != 0) {
        fprintf(stderr, "warning: failed to access PANO PTO %s\n", ctx->pto_path);
        return 0;
    }

    for (int i = 0; i < ctx->input_count; ++i) {
        pano_image_t *img = &ctx->inputs[i];
        if (load_jpeg_rgb(ctx->image_paths[i], img) != 0) {
            fprintf(stderr, "warning: failed to load PANO input %s\n", ctx->image_paths[i]);
            return 0;
        }
        if ((img->width & 1) || (img->height & 1)) {
            fprintf(stderr, "warning: PANO input must be even-sized: %s %dx%d\n",
                    ctx->image_paths[i], img->width, img->height);
            return 0;
        }
        if (i == 0) {
            ctx->in_w = img->width;
            ctx->in_h = img->height;
        } else if (img->width != ctx->in_w || img->height != ctx->in_h) {
            fprintf(stderr, "warning: PANO input size mismatch: %s %dx%d expected %dx%d\n",
                    ctx->image_paths[i], img->width, img->height, ctx->in_w, ctx->in_h);
            return 0;
        }
    }

    size_t nv12_size = (size_t)ctx->in_w * ctx->in_h * 3 / 2;
    for (int i = 0; i < ctx->input_count; ++i) {
        ctx->nv12[i] = malloc(nv12_size);
        if (!ctx->nv12[i]) return 0;
        image_to_nv12_packed(&ctx->inputs[i], ctx->nv12[i]);
    }
    ctx->output = malloc(PANO_OUTPUT_SIZE);
    if (!ctx->output) return 0;
    memset(ctx->output, 0, PANO_OUTPUT_SIZE);
    ctx->loaded = 1;
    return 1;
}

static void free_pano_assets(pano_ctx_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < PANO_INPUT_COUNT; ++i) {
        free_image(&ctx->inputs[i]);
        free(ctx->nv12[i]);
        ctx->nv12[i] = NULL;
    }
    free(ctx->output);
    ctx->output = NULL;
    ctx->loaded = 0;
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

static int setup_pano_module(pano_ctx_t *ctx) {
    if (!ctx || !ctx->loaded || ctx->in_w <= 0 || ctx->in_h <= 0) return -1;
    size_t input_size = (size_t)ctx->in_w * ctx->in_h * 3 / 2;
    if (MEDIA_POOL_Create(PANO_INPUT_POOL, input_size, ctx->input_count) != 0) return -1;
    if (MEDIA_POOL_Create(PANO_OUTPUT_POOL, PANO_OUTPUT_SIZE, 1) != 0) {
        MEDIA_POOL_Destroy(PANO_INPUT_POOL);
        return -1;
    }

    MEDIA_PANO_ATTR attr = {0};
    attr.input_count = ctx->input_count;
    attr.in_width = ctx->in_w;
    attr.in_height = ctx->in_h;
    attr.in_stride = ctx->in_w;
    attr.out_width = PANO_STITCH_W;
    attr.out_height = PANO_STITCH_H;
    attr.out_stride = PANO_STITCH_STRIDE;
    attr.crop_enable = 1;
    attr.crop_x = 0;
    attr.crop_y = 0;
    attr.crop_width = PANO_DOMAIN_W;
    attr.crop_height = PANO_DOMAIN_H;
    attr.lut_width = PANO_LUT_W;
    attr.lut_height = PANO_LUT_H;
    attr.output_pool_id = PANO_OUTPUT_POOL;
    attr.input_depth = ctx->input_count;
    attr.output_depth = 1;
    attr.sync_timeout_ms = 200;
    attr.pto_path = ctx->pto_path;

    if (MEDIA_PANO_CreateGrp(PANO_GRP, &attr) != 0 ||
        MEDIA_PANO_Start(PANO_GRP) != 0) {
        MEDIA_PANO_DestroyGrp(PANO_GRP);
        MEDIA_POOL_Destroy(PANO_INPUT_POOL);
        MEDIA_POOL_Destroy(PANO_OUTPUT_POOL);
        return -1;
    }
    ctx->module_ok = 1;
    return 0;
}

static void cleanup_pano_module(pano_ctx_t *ctx) {
    if (!ctx || !ctx->module_ok) return;
    MEDIA_PANO_Stop(PANO_GRP);
    MEDIA_PANO_DestroyGrp(PANO_GRP);
    MEDIA_POOL_Destroy(PANO_INPUT_POOL);
    MEDIA_POOL_Destroy(PANO_OUTPUT_POOL);
    ctx->module_ok = 0;
}

static int process_pano_module(pano_ctx_t *ctx) {
    MEDIA_BUFFER in_bufs[PANO_INPUT_COUNT];
    MEDIA_BUFFER out = {-1, -1};
    int sent[PANO_INPUT_COUNT] = {0};
    int ret = -1;
    if (!ctx || !ctx->loaded || !ctx->module_ok || !ctx->output) return -1;
    size_t input_size = (size_t)ctx->in_w * ctx->in_h * 3 / 2;
    memset(in_bufs, 0xff, sizeof(in_bufs));

    for (int i = 0; i < ctx->input_count; ++i) {
        if (MEDIA_POOL_GetBuffer(PANO_INPUT_POOL, &in_bufs[i]) != 0) goto done;
        if (copy_to_buffer(in_bufs[i], ctx->nv12[i], input_size) != 0) goto done;
        if (MEDIA_PANO_SendFrame(PANO_GRP, i, in_bufs[i], 1000) != 0) goto done;
        sent[i] = 1;
        in_bufs[i].pool_id = -1;
        in_bufs[i].index = -1;
    }

    if (MEDIA_PANO_GetFrame(PANO_GRP, &out, 30000) == 0) {
        ret = copy_from_buffer(out, ctx->output, PANO_OUTPUT_SIZE);
        MEDIA_PANO_ReleaseFrame(PANO_GRP, out);
        out.pool_id = -1;
        out.index = -1;
    }

done:
    if (out.pool_id >= 0) MEDIA_PANO_ReleaseFrame(PANO_GRP, out);
    for (int i = 0; i < ctx->input_count; ++i) {
        if (!sent[i] && in_bufs[i].pool_id >= 0) MEDIA_POOL_PutBuffer(in_bufs[i]);
    }
    return ret;
}

static int compose_reference_preview(pano_ctx_t *ctx) {
    if (!ctx || !ctx->loaded || !ctx->output || ctx->input_count <= 0) return -1;
    uint8_t *yp = ctx->output;
    uint8_t *uvp = ctx->output + (size_t)PANO_STITCH_STRIDE * PANO_STITCH_H;
    int segment_w = PANO_STITCH_W / ctx->input_count;
    if (segment_w <= 0) return -1;

    for (int y = 0; y < PANO_STITCH_H; ++y) {
        uint8_t *row = yp + (size_t)y * PANO_STITCH_STRIDE;
        for (int x = 0; x < PANO_STITCH_W; ++x) {
            int idx = x / segment_w;
            if (idx >= ctx->input_count) idx = ctx->input_count - 1;
            pano_image_t *img = &ctx->inputs[idx];
            int local_x = x - idx * segment_w;
            int sx = local_x * img->width / segment_w;
            int sy = y * img->height / PANO_STITCH_H;
            if (sx >= img->width) sx = img->width - 1;
            if (sy >= img->height) sy = img->height - 1;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &u, &v);
            (void)u;
            (void)v;
            row[x] = yy;
        }
    }

    for (int y = 0; y < PANO_STITCH_H; y += 2) {
        uint8_t *row = uvp + (size_t)(y / 2) * PANO_STITCH_STRIDE;
        for (int x = 0; x < PANO_STITCH_W; x += 2) {
            int idx = x / segment_w;
            if (idx >= ctx->input_count) idx = ctx->input_count - 1;
            pano_image_t *img = &ctx->inputs[idx];
            int local_x = x - idx * segment_w;
            int sx = local_x * img->width / segment_w;
            int sy = y * img->height / PANO_STITCH_H;
            if (sx >= img->width) sx = img->width - 1;
            if (sy >= img->height) sy = img->height - 1;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &u, &v);
            (void)yy;
            row[x] = u;
            row[x + 1] = v;
        }
    }
    return 0;
}

static void fill_rect(uint8_t *dst, int stride, int width, int height,
                      int x, int y, int w, int h,
                      uint8_t r, uint8_t g, uint8_t b) {
    uint8_t yy, uu, vv;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    page_surface_fill_rect_nv12(dst, stride, width, height, x, y, w, h, yy, uu, vv);
}

static void draw_text(uint8_t *dst, int stride, int width, int height,
                      int x, int y, const char *text, int scale,
                      uint8_t r, uint8_t g, uint8_t b) {
    uint8_t yy, uu, vv;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    page_surface_draw_text(dst, stride, width, height, x, y, text, scale, yy, uu, vv);
}

static void stroke_rect(uint8_t *dst, int stride, int width, int height,
                        int x, int y, int w, int h, int t,
                        uint8_t r, uint8_t g, uint8_t b) {
    fill_rect(dst, stride, width, height, x, y, w, t, r, g, b);
    fill_rect(dst, stride, width, height, x, y + h - t, w, t, r, g, b);
    fill_rect(dst, stride, width, height, x, y, t, h, r, g, b);
    fill_rect(dst, stride, width, height, x + w - t, y, t, h, r, g, b);
}

static void draw_rgb_fit(uint8_t *dst, int stride, int width, int height,
                         int x, int y, int w, int h, const pano_image_t *img) {
    if (!dst || !img || !img->rgb || img->width <= 0 || img->height <= 0 ||
        w <= 0 || h <= 0) {
        return;
    }
    int out_w = w;
    int out_h = (int)((int64_t)img->height * out_w / img->width);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)img->width * out_h / img->height);
    }
    if (out_w <= 0 || out_h <= 0) return;
    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy0 = dy * img->height / out_h;
        int sy1 = (dy + 1 < out_h) ? ((dy + 1) * img->height / out_h) : sy0;
        uint8_t *yrow0 = dst + (size_t)(oy + dy) * stride + ox;
        uint8_t *yrow1 = (dy + 1 < out_h) ?
            dst + (size_t)(oy + dy + 1) * stride + ox : NULL;
        uint8_t *uv = dst + (size_t)stride * height +
            (size_t)((oy + dy) / 2) * stride + (ox & ~1);
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx0 = dx * img->width / out_w;
            int sx1 = (dx + 1 < out_w) ? ((dx + 1) * img->width / out_w) : sx0;
            const uint8_t *p00 = img->rgb + ((size_t)sy0 * img->width + sx0) * 3;
            const uint8_t *p01 = img->rgb + ((size_t)sy0 * img->width + sx1) * 3;
            const uint8_t *p10 = img->rgb + ((size_t)sy1 * img->width + sx0) * 3;
            const uint8_t *p11 = img->rgb + ((size_t)sy1 * img->width + sx1) * 3;
            uint8_t y00, u00, v00, y01, u01, v01, y10, u10, v10, y11, u11, v11;
            rgb_to_yuv(p00[0], p00[1], p00[2], &y00, &u00, &v00);
            rgb_to_yuv(p01[0], p01[1], p01[2], &y01, &u01, &v01);
            rgb_to_yuv(p10[0], p10[1], p10[2], &y10, &u10, &v10);
            rgb_to_yuv(p11[0], p11[1], p11[2], &y11, &u11, &v11);
            yrow0[dx] = y00;
            if (dx + 1 < out_w) yrow0[dx + 1] = y01;
            if (yrow1) {
                yrow1[dx] = y10;
                if (dx + 1 < out_w) yrow1[dx + 1] = y11;
            }
            int uv_col = ((ox + dx) & ~1) - (ox & ~1);
            uv[uv_col] = (uint8_t)(((int)u00 + u01 + u10 + u11) / 4);
            uv[uv_col + 1] = (uint8_t)(((int)v00 + v01 + v10 + v11) / 4);
        }
    }
}

static void draw_nv12_fit(uint8_t *dst, int stride, int width, int height,
                          int x, int y, int w, int h,
                          const uint8_t *src, int src_w, int src_h, int src_stride) {
    if (!dst || !src || src_w <= 0 || src_h <= 0 || src_stride < src_w ||
        w <= 0 || h <= 0) {
        return;
    }
    int out_w = w;
    int out_h = (int)((int64_t)src_h * out_w / src_w);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)src_w * out_h / src_h);
    }
    if (out_w <= 0 || out_h <= 0) return;
    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;
    const uint8_t *src_uv = src + (size_t)src_stride * src_h;

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy0 = dy * src_h / out_h;
        int sy1 = (dy + 1 < out_h) ? ((dy + 1) * src_h / out_h) : sy0;
        uint8_t *yrow0 = dst + (size_t)(oy + dy) * stride + ox;
        uint8_t *yrow1 = (dy + 1 < out_h) ?
            dst + (size_t)(oy + dy + 1) * stride + ox : NULL;
        uint8_t *uv = dst + (size_t)stride * height +
            (size_t)((oy + dy) / 2) * stride + (ox & ~1);
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx0 = dx * src_w / out_w;
            int sx1 = (dx + 1 < out_w) ? ((dx + 1) * src_w / out_w) : sx0;
            yrow0[dx] = src[(size_t)sy0 * src_stride + sx0];
            if (dx + 1 < out_w) yrow0[dx + 1] = src[(size_t)sy0 * src_stride + sx1];
            if (yrow1) {
                yrow1[dx] = src[(size_t)sy1 * src_stride + sx0];
                if (dx + 1 < out_w) yrow1[dx + 1] = src[(size_t)sy1 * src_stride + sx1];
            }
            int uv_sx = sx0 & ~1;
            const uint8_t *suv = src_uv + (size_t)(sy0 / 2) * src_stride + uv_sx;
            int uv_col = ((ox + dx) & ~1) - (ox & ~1);
            uv[uv_col] = suv[0];
            uv[uv_col + 1] = suv[1];
        }
    }
    stroke_rect(dst, stride, width, height, ox, oy, out_w, out_h, 1, 110, 245, 190);
}

static void draw_input_panel(uint8_t *dst, int stride, int width, int height,
                             int x, int y, int w, int h,
                             const pano_image_t *img, const char *label) {
    int label_h = 24;
    fill_rect(dst, stride, width, height, x, y, w, h, 5, 10, 18);
    stroke_rect(dst, stride, width, height, x, y, w, h, 1, 70, 140, 210);
    draw_rgb_fit(dst, stride, width, height, x + 4, y + 4, w - 8, h - label_h - 8, img);
    fill_rect(dst, stride, width, height, x, y + h - label_h, w, label_h, 0, 0, 0);
    draw_text(dst, stride, width, height, x + 8, y + h - label_h + 6,
              label, 1, 180, 230, 255);
}

static void draw_pano_page(uint8_t *dst, int stride, int width, int height,
                           int frame, void *opaque) {
    pano_ctx_t *ctx = (pano_ctx_t *)opaque;
    fill_rect(dst, stride, width, height, 0, 0, width, height, 7, 13, 23);

    int margin = 30;
    int x = margin;
    int y = 32;
    int w = width - margin * 2;
    draw_text(dst, stride, width, height, x, y, "PANO SIX CAMERA STITCH", 3,
              235, 248, 255);
    draw_text(dst, stride, width, height, x, y + 42,
              "FLOW: 6 JPG INPUTS + PTO -> MEDIA_PANO -> PANORAMA", 2,
              165, 210, 235);

    char line[128];
    snprintf(line, sizeof(line), "MODE=%s  INPUTS=%d  OUT=%dx%d  STANDALONE=1",
             ctx ? ctx->mode : "none", ctx ? ctx->input_count : 0,
             PANO_STITCH_W, PANO_STITCH_H);
    draw_text(dst, stride, width, height, x, y + 78, line, 2, 255, 225, 120);

    if (!ctx || !ctx->loaded) {
        fill_rect(dst, stride, width, height, x, 220, w, 360, 20, 18, 12);
        stroke_rect(dst, stride, width, height, x, 220, w, 360, 2, 230, 185, 80);
        draw_text(dst, stride, width, height, x + 26, 370,
                  "PANO SAMPLE NOT LOADED", 3, 255, 220, 120);
        return;
    }

    int top_y = 170;
    int gap = 10;
    int rows = 2;
    int cols = 3;
    int input_h = 470;
    int cell_w = (w - gap * (cols - 1)) / cols;
    int cell_h = (input_h - gap * (rows - 1)) / rows;
    for (int i = 0; i < PANO_INPUT_COUNT; ++i) {
        char label[16];
        snprintf(label, sizeof(label), "INPUT %d", i);
        int cx = x + (i % cols) * (cell_w + gap);
        int cy = top_y + (i / cols) * (cell_h + gap);
        draw_input_panel(dst, stride, width, height, cx, cy, cell_w, cell_h,
                         &ctx->inputs[i], label);
    }

    int out_y = top_y + input_h + 18;
    int out_h = height - out_y - 150;
    fill_rect(dst, stride, width, height, x, out_y, w, out_h, 5, 10, 18);
    stroke_rect(dst, stride, width, height, x, out_y, w, out_h, 1, 80, 255, 180);
    if (ctx->output_ready) {
        int view_margin_x = 70;
        draw_nv12_fit(dst, stride, width, height, x + view_margin_x, out_y + 8,
                      w - view_margin_x * 2, out_h - 44, ctx->output,
                      PANO_STITCH_W, PANO_STITCH_H, PANO_STITCH_STRIDE);
    } else {
        draw_text(dst, stride, width, height, x + 28, out_y + out_h / 2,
                  "PANO OUTPUT NOT READY", 3, 255, 220, 120);
    }
    fill_rect(dst, stride, width, height, x, out_y + out_h - 32, w, 32, 0, 0, 0);
    draw_text(dst, stride, width, height, x + 10, out_y + out_h - 23,
              "PANORAMA OUTPUT", 1, 180, 230, 255);

    snprintf(line, sizeof(line), "FRAMES=%d  PTO=sample2/calib_file.pto", frame);
    draw_text(dst, stride, width, height, x + 16, height - 82, line, 2,
              190, 225, 245);
}

int page_pano_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    pano_ctx_t ctx;
    init_pano_ctx(&ctx);
    load_pano_assets(&ctx);

    if (page_surface_open(&surface, PANO_PAGE_POOL, PANO_PAGE_W, PANO_PAGE_H,
                          PANO_PAGE_STRIDE, PANO_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free_pano_assets(&ctx);
        return 1;
    }

    int live_disabled = 0;
    const char *live_env = getenv("ALLDEMO_PANO_LIVE");
    if (live_env && strcmp(live_env, "0") == 0) live_disabled = 1;

    if (ctx.loaded && !live_disabled && setup_pano_module(&ctx) == 0 &&
        process_pano_module(&ctx) == 0) {
        ctx.output_ready = 1;
        snprintf(ctx.mode, sizeof(ctx.mode), "module-preview");
        set_tile_status("PANO", TILE_LIVE);
    } else if (ctx.loaded && compose_reference_preview(&ctx) == 0) {
        ctx.output_ready = 1;
        snprintf(ctx.mode, sizeof(ctx.mode), "reference-strip");
        set_tile_status("PANO", TILE_LOOP);
    } else {
        snprintf(ctx.mode, sizeof(ctx.mode), "not-ready");
    }
    set_tile_status("VO", TILE_LIVE);

    printf("PANO standalone page loaded=%d inputs=%d size=%dx%d mode=%s. Ctrl+C to stop.\n",
           ctx.loaded, ctx.input_count, PANO_STITCH_W, PANO_STITCH_H, ctx.mode);

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_pano_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % PANO_PAGE_FPS) == 0) {
                printf("PANO frames=%d output=%d/%d mode=%s pano=%dx%d domain=%dx%d standalone=1\n",
                       frame, ctx.output_ready ? 1 : 0, ctx.loaded ? 1 : 0,
                       ctx.mode, PANO_STITCH_W, PANO_STITCH_H,
                       PANO_DOMAIN_W, PANO_DOMAIN_H);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / PANO_PAGE_FPS);
    }

    cleanup_pano_module(&ctx);
    page_surface_close(&surface);
    free_pano_assets(&ctx);
    return 0;
}
