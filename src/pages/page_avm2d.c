#include "page_ops.h"

#include "app/tile_state.h"
#include "page_surface.h"

#include <stdio.h>
#include <jpeglib.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define AVM2D_PAGE_W 1080
#define AVM2D_PAGE_H 1920
#define AVM2D_PAGE_STRIDE 1088
#define AVM2D_PAGE_POOL 1
#define AVM2D_PAGE_FPS 30
#define AVM2D_FRAME_COUNT 25
#define AVM2D_CAMERA_COUNT 4
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} avm2d_image_t;

typedef struct {
    avm2d_image_t frames[AVM2D_FRAME_COUNT][AVM2D_CAMERA_COUNT];
    avm2d_image_t reference;
    avm2d_image_t gpu;
    int frame_count;
    int outputs_loaded;
} avm2d_ctx_t;

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

static int load_jpeg_rgb(const char *path, avm2d_image_t *out) {
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

static void free_image(avm2d_image_t *img) {
    if (!img) return;
    free(img->rgb);
    memset(img, 0, sizeof(*img));
}

static int load_avm2d_assets(avm2d_ctx_t *ctx) {
    static const char *camera_names[AVM2D_CAMERA_COUNT] = {
        "front", "rear", "left", "right"
    };
    if (!ctx) return 0;
    memset(ctx, 0, sizeof(*ctx));

    for (int frame = 0; frame < AVM2D_FRAME_COUNT; ++frame) {
        int frame_ok = 1;
        for (int cam = 0; cam < AVM2D_CAMERA_COUNT; ++cam) {
            char path[256];
            snprintf(path, sizeof(path), "assets/loop/avm2d/video/%03d/%s.jpg",
                     frame, camera_names[cam]);
            if (load_jpeg_rgb(path, &ctx->frames[frame][cam]) != 0) {
                fprintf(stderr, "warning: failed to load AVM2D frame %s\n", path);
                frame_ok = 0;
                break;
            }
        }
        if (!frame_ok) {
            for (int cam = 0; cam < AVM2D_CAMERA_COUNT; ++cam) {
                free_image(&ctx->frames[frame][cam]);
            }
            break;
        }
        ctx->frame_count++;
    }

    if (load_jpeg_rgb("assets/loop/avm2d/dyfcalid/surround_blend_1_balance_1_car_1.jpg",
                      &ctx->reference) == 0 &&
        load_jpeg_rgb("assets/loop/avm2d/dyfcalid/avm_gpu_output_overlay.jpg",
                      &ctx->gpu) == 0) {
        ctx->outputs_loaded = 1;
    } else {
        fprintf(stderr, "warning: failed to load AVM2D BEV outputs\n");
    }
    return ctx->frame_count;
}

static void free_avm2d_assets(avm2d_ctx_t *ctx) {
    if (!ctx) return;
    for (int frame = 0; frame < AVM2D_FRAME_COUNT; ++frame) {
        for (int cam = 0; cam < AVM2D_CAMERA_COUNT; ++cam) {
            free_image(&ctx->frames[frame][cam]);
        }
    }
    free_image(&ctx->reference);
    free_image(&ctx->gpu);
    memset(ctx, 0, sizeof(*ctx));
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
                         int x, int y, int w, int h, const avm2d_image_t *img) {
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
    stroke_rect(dst, stride, width, height, ox, oy, out_w, out_h, 1, 90, 130, 170);
}

static void draw_panel(uint8_t *dst, int stride, int width, int height,
                       int x, int y, int w, int h, const avm2d_image_t *img,
                       const char *label, int accent) {
    int label_h = 28;
    fill_rect(dst, stride, width, height, x, y, w, h, 5, 10, 18);
    stroke_rect(dst, stride, width, height, x, y, w, h, 1,
                accent ? 80 : 55, accent ? 240 : 150, accent ? 185 : 220);
    draw_rgb_fit(dst, stride, width, height, x + 5, y + 5, w - 10, h - label_h - 10, img);
    fill_rect(dst, stride, width, height, x, y + h - label_h, w, label_h, 0, 0, 0);
    draw_text(dst, stride, width, height, x + 9, y + h - label_h + 7,
              label, 1, 180, 230, 255);
}

static void draw_avm2d_page(uint8_t *dst, int stride, int width, int height,
                            int frame, void *opaque) {
    avm2d_ctx_t *ctx = (avm2d_ctx_t *)opaque;
    fill_rect(dst, stride, width, height, 0, 0, width, height, 8, 14, 24);

    int margin = 30;
    int x = margin;
    int y = 32;
    int w = width - margin * 2;
    draw_text(dst, stride, width, height, x, y, "AVM2D PARKING BEV RESEARCH", 3,
              235, 248, 255);
    draw_text(dst, stride, width, height, x, y + 42,
              "FLOW: 4 VIDEO VIEWS -> CALIB LUT/IPM -> 2D BEV", 2,
              165, 210, 235);
    draw_text(dst, stride, width, height, x, y + 72,
              "REFERENCE OUTPUT VS GPU LUT OUTPUT", 2, 155, 230, 180);

    if (!ctx || ctx->frame_count <= 0 || !ctx->outputs_loaded) {
        fill_rect(dst, stride, width, height, x, 220, w, 360, 20, 18, 12);
        stroke_rect(dst, stride, width, height, x, 220, w, 360, 2, 230, 185, 80);
        draw_text(dst, stride, width, height, x + 26, 370,
                  "AVM2D ASSETS NOT LOADED", 3, 255, 220, 120);
        return;
    }

    int frame_idx = (frame / 3) % ctx->frame_count;
    char line[96];
    snprintf(line, sizeof(line), "VIDEO FRAME %02d/%02d  INPUTS=4  OUTPUTS=2",
             frame_idx + 1, ctx->frame_count);
    draw_text(dst, stride, width, height, x, y + 108, line, 2, 255, 225, 120);

    int top_y = 190;
    int gap = 12;
    int input_h = 520;
    int col_w = (w - gap * 2) / 3;
    int center_h = (input_h - gap) / 2;
    draw_panel(dst, stride, width, height,
               x + col_w + gap, top_y, col_w, center_h,
               &ctx->frames[frame_idx][0], "FRONT VIDEO", 0);
    draw_panel(dst, stride, width, height,
               x, top_y, col_w, input_h,
               &ctx->frames[frame_idx][2], "LEFT VIDEO", 0);
    draw_panel(dst, stride, width, height,
               x + (col_w + gap) * 2, top_y, col_w, input_h,
               &ctx->frames[frame_idx][3], "RIGHT VIDEO", 0);
    draw_panel(dst, stride, width, height,
               x + col_w + gap, top_y + center_h + gap, col_w, center_h,
               &ctx->frames[frame_idx][1], "REAR VIDEO", 0);

    int out_y = top_y + input_h + 18;
    int out_h = height - out_y - 150;
    int out_w = (w - gap) / 2;
    draw_panel(dst, stride, width, height, x, out_y, out_w, out_h,
               &ctx->reference, "CALIBRATED REFERENCE", 1);
    draw_panel(dst, stride, width, height, x + out_w + gap, out_y, out_w, out_h,
               &ctx->gpu, "GPU LUT OUTPUT", 1);

    fill_rect(dst, stride, width, height, x, height - 105, w, 70, 10, 18, 26);
    stroke_rect(dst, stride, width, height, x, height - 105, w, 70, 1, 80, 140, 185);
    draw_text(dst, stride, width, height, x + 16, height - 82,
              "STANDALONE=1  ASSET LOOP PAGE  MODULE=N/A", 2,
              190, 225, 245);
}

int page_avm2d_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    avm2d_ctx_t ctx;
    load_avm2d_assets(&ctx);

    if (page_surface_open(&surface, AVM2D_PAGE_POOL, AVM2D_PAGE_W, AVM2D_PAGE_H,
                          AVM2D_PAGE_STRIDE, AVM2D_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free_avm2d_assets(&ctx);
        return 1;
    }

    if (ctx.frame_count > 0 && ctx.outputs_loaded) set_tile_status("AVM2D", TILE_LOOP);
    set_tile_status("VO", TILE_LIVE);
    printf("AVM2D standalone page frames=%d/%d outputs=%s. Ctrl+C to stop.\n",
           ctx.frame_count, AVM2D_FRAME_COUNT, ctx.outputs_loaded ? "yes" : "no");

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_avm2d_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % AVM2D_PAGE_FPS) == 0) {
                int sample = ctx.frame_count > 0 ? ((frame / 3) % ctx.frame_count) + 1 : 0;
                printf("AVM2D frames=%d sample=%d/%d outputs=%s standalone=1\n",
                       frame, sample, ctx.frame_count, ctx.outputs_loaded ? "yes" : "no");
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / AVM2D_PAGE_FPS);
    }

    page_surface_close(&surface);
    free_avm2d_assets(&ctx);
    return 0;
}
