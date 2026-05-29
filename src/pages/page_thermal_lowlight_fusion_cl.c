#include "page_ops.h"

#include "app/tile_state.h"
#include "page_surface.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TLF_PAGE_W 1080
#define TLF_PAGE_H 1920
#define TLF_PAGE_STRIDE 1088
#define TLF_PAGE_POOL 1
#define TLF_PAGE_FPS 30
#define TLF_FRAME_W 640
#define TLF_FRAME_H 480
#define TLF_FRAME_STRIDE 640
#define TLF_FRAME_SIZE (TLF_FRAME_STRIDE * TLF_FRAME_H * 3 / 2)
#define TLF_SAMPLE_SECONDS 3
#define TLF_ASSET_DIR "assets/loop/thermal_lowlight_fusion_cl_real_preview"
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    const char *name;
    const char *title;
} tlf_sample_t;

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} image_t;

typedef struct {
    uint8_t *thermal;
    uint8_t *lowlight;
    uint8_t *gray;
    uint8_t *overlay;
    int valid_indices[8];
    int valid_count;
    int current_index;
    int loaded;
} tlf_ctx_t;

static const tlf_sample_t g_samples[] = {
    {"carLight", "CARLIGHT"},
    {"manlight", "MANLIGHT"},
    {"nightCar", "NIGHTCAR"},
    {"walkingnight", "WALKINGNIGHT"},
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

static void clear_nv12(uint8_t *dst, uint8_t yy, uint8_t uu, uint8_t vv) {
    if (!dst) return;
    memset(dst, yy, TLF_FRAME_STRIDE * TLF_FRAME_H);
    uint8_t *uv = dst + TLF_FRAME_STRIDE * TLF_FRAME_H;
    for (int y = 0; y < TLF_FRAME_H / 2; ++y) {
        uint8_t *row = uv + (size_t)y * TLF_FRAME_STRIDE;
        for (int x = 0; x < TLF_FRAME_STRIDE; x += 2) {
            row[x] = uu;
            row[x + 1] = vv;
        }
    }
}

static void image_to_nv12_frame(const image_t *img, uint8_t *dst) {
    if (!img || !img->rgb || !dst || img->width <= 0 || img->height <= 0) return;
    clear_nv12(dst, 14, 128, 128);

    int out_w = TLF_FRAME_W;
    int out_h = (int)((int64_t)img->height * TLF_FRAME_W / img->width);
    if (out_h > TLF_FRAME_H) {
        out_h = TLF_FRAME_H;
        out_w = (int)((int64_t)img->width * TLF_FRAME_H / img->height);
    }
    out_w &= ~1;
    out_h &= ~1;
    if (out_w <= 0 || out_h <= 0) return;

    int ox = ((TLF_FRAME_W - out_w) / 2) & ~1;
    int oy = ((TLF_FRAME_H - out_h) / 2) & ~1;
    uint8_t *uv = dst + TLF_FRAME_STRIDE * TLF_FRAME_H;

    for (int y = 0; y < out_h; ++y) {
        int sy = y * img->height / out_h;
        uint8_t *drow = dst + (size_t)(oy + y) * TLF_FRAME_STRIDE + ox;
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
        uint8_t *drow = uv + (size_t)((oy + y) / 2) * TLF_FRAME_STRIDE + ox;
        for (int x = 0; x < out_w; x += 2) {
            int sx = x * img->width / out_w;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)yy;
            drow[x] = uu;
            drow[x + 1] = vv;
        }
    }
}

static void make_path(char *out, size_t len, const char *sample, const char *suffix) {
    snprintf(out, len, "%s/%s_%s.jpg", TLF_ASSET_DIR, sample, suffix);
}

static int load_asset_frame(const char *sample, const char *suffix, uint8_t *dst) {
    char path[256];
    image_t img;
    make_path(path, sizeof(path), sample, suffix);
    if (load_jpeg_rgb(path, &img) != 0) return -1;
    image_to_nv12_frame(&img, dst);
    free_image(&img);
    return 0;
}

static int sample_assets_ready(const char *sample) {
    static const char *suffixes[] = {
        "input0_ir",
        "input1_vi",
        "mode0_gray_fusion",
        "mode1_black_red_overlay",
    };
    char path[256];
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        make_path(path, sizeof(path), sample, suffixes[i]);
        if (access(path, R_OK) != 0) return 0;
    }
    return 1;
}

static int scan_samples(tlf_ctx_t *ctx) {
    if (!ctx) return 0;
    ctx->valid_count = 0;
    for (int i = 0; i < (int)(sizeof(g_samples) / sizeof(g_samples[0])); ++i) {
        if (sample_assets_ready(g_samples[i].name) &&
            ctx->valid_count < (int)(sizeof(ctx->valid_indices) / sizeof(ctx->valid_indices[0]))) {
            ctx->valid_indices[ctx->valid_count++] = i;
        }
    }
    return ctx->valid_count;
}

static int load_sample(tlf_ctx_t *ctx, int sample_index) {
    const char *sample;
    if (!ctx || sample_index < 0 ||
        sample_index >= (int)(sizeof(g_samples) / sizeof(g_samples[0]))) {
        return -1;
    }
    sample = g_samples[sample_index].name;
    if (load_asset_frame(sample, "input0_ir", ctx->thermal) != 0 ||
        load_asset_frame(sample, "input1_vi", ctx->lowlight) != 0 ||
        load_asset_frame(sample, "mode0_gray_fusion", ctx->gray) != 0 ||
        load_asset_frame(sample, "mode1_black_red_overlay", ctx->overlay) != 0) {
        return -1;
    }
    ctx->current_index = sample_index;
    ctx->loaded++;
    return 0;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;

    for (int y = 0; y < dh; ++y) {
        int sy = y * TLF_FRAME_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * TLF_FRAME_STRIDE;
        for (int x = 0; x < dw; ++x) {
            int sx = x * TLF_FRAME_W / dw;
            drow[x] = srow[sx];
        }
    }

    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)TLF_FRAME_STRIDE * TLF_FRAME_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (TLF_FRAME_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * TLF_FRAME_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = (x * TLF_FRAME_W / dw) & ~1;
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_labeled_pane(uint8_t *dst, int stride, int width, int height,
                              int x, int y, const char *label, const uint8_t *image) {
    const int pane_w = 480;
    const int pane_h = 360;
    page_surface_fill_rect_nv12(dst, stride, width, height,
                                x - 8, y - 40, pane_w + 16, pane_h + 58,
                                22, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, y - 30,
                           label, 3, 220, 108, 176);
    if (image) {
        draw_nv12_scaled(dst, stride, width, height, x, y, pane_w, pane_h, image);
    }
}

static void draw_tlf_page(uint8_t *dst, int stride, int width, int height,
                          int frame, void *opaque) {
    tlf_ctx_t *ctx = (tlf_ctx_t *)opaque;
    int sample_index = -1;
    if (ctx && ctx->valid_count > 0) {
        int slot = (frame / (TLF_PAGE_FPS * TLF_SAMPLE_SECONDS)) % ctx->valid_count;
        sample_index = ctx->valid_indices[slot];
        if (sample_index != ctx->current_index) {
            if (load_sample(ctx, sample_index) != 0) {
                sample_index = -1;
            }
        }
    }

    char sample_text[80];
    char status_text[80];
    const char *sample_title = sample_index >= 0 ? g_samples[sample_index].title : "MISSING";
    snprintf(sample_text, sizeof(sample_text), "SAMPLE %02d/%02d %s",
             sample_index >= 0 ? sample_index + 1 : 0,
             (int)(sizeof(g_samples) / sizeof(g_samples[0])),
             sample_title);
    snprintf(status_text, sizeof(status_text), "ASSETS %02d/%02d LOADED %04d",
             ctx ? ctx->valid_count : 0,
             (int)(sizeof(g_samples) / sizeof(g_samples[0])),
             ctx ? ctx->loaded : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 196, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 46, 52,
                           "THERMAL LOWLIGHT FUSION CL", 5, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 48, 130,
                           sample_text, 3, 210, 144, 84);

    draw_labeled_pane(dst, stride, width, height, 46, 290, "INPUT0 IR", ctx ? ctx->thermal : NULL);
    draw_labeled_pane(dst, stride, width, height, 554, 290, "INPUT1 VI", ctx ? ctx->lowlight : NULL);
    draw_labeled_pane(dst, stride, width, height, 46, 772, "MODE0 GRAY", ctx ? ctx->gray : NULL);
    draw_labeled_pane(dst, stride, width, height, 554, 772, "MODE1 BLACK RED", ctx ? ctx->overlay : NULL);

    page_surface_fill_rect_nv12(dst, stride, width, height, 54, 1244, 972, 306, 18, 128, 128);
    page_surface_draw_text(dst, stride, width, height, 84, 1298,
                           "FLOW IR PLUS VI TO OPENCL FUSION", 3, 210, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 84, 1368,
                           "MODE0 GRAY FUSES THERMAL DETAIL", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 84, 1438,
                           "MODE1 BLACK RED MARKS HOT TARGETS", 3, 220, 108, 176);

    page_surface_draw_text(dst, stride, width, height, 74, 1660,
                           status_text, 3, 210, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 74, 1730,
                           "SOURCE RKTOHI REAL PREVIEW", 3, 180, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 74, 1800,
                           "DIR THERMAL LOWLIGHT FUSION CL REAL PREVIEW", 2, 180, 144, 84);
    page_surface_fill_rect_nv12(dst, stride, width, height,
                                74 + (frame * 9) % 860, 1864, 96, 14,
                                210, 36, 220);
}

int page_thermal_lowlight_fusion_cl_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    tlf_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.current_index = -1;
    ctx.thermal = malloc(TLF_FRAME_SIZE);
    ctx.lowlight = malloc(TLF_FRAME_SIZE);
    ctx.gray = malloc(TLF_FRAME_SIZE);
    ctx.overlay = malloc(TLF_FRAME_SIZE);
    if (!ctx.thermal || !ctx.lowlight || !ctx.gray || !ctx.overlay) {
        free(ctx.thermal);
        free(ctx.lowlight);
        free(ctx.gray);
        free(ctx.overlay);
        return 1;
    }
    clear_nv12(ctx.thermal, 16, 128, 128);
    clear_nv12(ctx.lowlight, 16, 128, 128);
    clear_nv12(ctx.gray, 16, 128, 128);
    clear_nv12(ctx.overlay, 16, 128, 128);
    scan_samples(&ctx);

    if (page_surface_open(&surface, TLF_PAGE_POOL, TLF_PAGE_W, TLF_PAGE_H,
                          TLF_PAGE_STRIDE, TLF_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.thermal);
        free(ctx.lowlight);
        free(ctx.gray);
        free(ctx.overlay);
        return 1;
    }

    set_tile_status("THERMAL_LOWLIGHT_FUSION_CL", ctx.valid_count > 0 ? TILE_LOOP : TILE_OFFLINE);
    set_tile_status("VO", TILE_LIVE);
    printf("THERMAL_LOWLIGHT_FUSION_CL standalone page assets=%d/%zu dir=%s. Ctrl+C to stop.\n",
           ctx.valid_count, sizeof(g_samples) / sizeof(g_samples[0]), TLF_ASSET_DIR);

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_tlf_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % TLF_PAGE_FPS) == 0) {
                printf("THERMAL_LOWLIGHT_FUSION_CL frames=%d sample=%d/%zu assets=%d loaded=%d standalone=1\n",
                       frame,
                       ctx.current_index >= 0 ? ctx.current_index + 1 : 0,
                       sizeof(g_samples) / sizeof(g_samples[0]),
                       ctx.valid_count,
                       ctx.loaded);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / TLF_PAGE_FPS);
    }

    page_surface_close(&surface);
    free(ctx.thermal);
    free(ctx.lowlight);
    free(ctx.gray);
    free(ctx.overlay);
    return 0;
}
