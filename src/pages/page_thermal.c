#include "page_ops.h"

#include "app/tile_state.h"
#include "page_surface.h"

#include <png.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TH_PAGE_W 1080
#define TH_PAGE_H 1920
#define TH_PAGE_STRIDE 1088
#define TH_PAGE_POOL 1
#define TH_PAGE_FPS 30
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} thermal_image_t;

typedef struct {
    thermal_image_t images[2];
    int image_count;
} thermal_ctx_t;

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

static int load_png_rgb(const char *path, thermal_image_t *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    uint8_t sig[8];
    if (fread(sig, 1, sizeof(sig), fp) != sizeof(sig) ||
        png_sig_cmp(sig, 0, sizeof(sig))) {
        fclose(fp);
        return -1;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return -1;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        free(out->rgb);
        memset(out, 0, sizeof(*out));
        return -1;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, sizeof(sig));
    png_read_info(png, info);

    int width = (int)png_get_image_width(png, info);
    int height = (int)png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);
    if (width <= 0 || height <= 0) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    if (color_type & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png);

    png_read_update_info(png, info);
    int channels = png_get_channels(png, info);
    png_size_t rowbytes = png_get_rowbytes(png, info);
    uint8_t *raw = malloc((size_t)rowbytes * (size_t)height);
    png_bytep *rows = malloc(sizeof(png_bytep) * (size_t)height);
    uint8_t *rgb = malloc((size_t)width * (size_t)height * 3);
    if (!raw || !rows || !rgb) {
        free(raw);
        free(rows);
        free(rgb);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    for (int y = 0; y < height; ++y) rows[y] = raw + (size_t)y * rowbytes;
    png_read_image(png, rows);
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = rows[y];
        uint8_t *dst = rgb + (size_t)y * (size_t)width * 3;
        for (int x = 0; x < width; ++x) {
            if (channels >= 3) {
                dst[x * 3 + 0] = src[x * channels + 0];
                dst[x * 3 + 1] = src[x * channels + 1];
                dst[x * 3 + 2] = src[x * channels + 2];
            } else {
                dst[x * 3 + 0] = src[x];
                dst[x * 3 + 1] = src[x];
                dst[x * 3 + 2] = src[x];
            }
        }
    }

    free(raw);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    out->width = width;
    out->height = height;
    out->rgb = rgb;
    return 0;
}

static void free_image(thermal_image_t *img) {
    if (!img) return;
    free(img->rgb);
    memset(img, 0, sizeof(*img));
}

static void load_thermal_assets(thermal_ctx_t *ctx) {
    static const char *paths[] = {
        "assets/loop/thermal/thermal_1.png",
        "assets/loop/thermal/thermal_2.png",
    };
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < 2; ++i) {
        if (load_png_rgb(paths[i], &ctx->images[ctx->image_count]) == 0) {
            ctx->image_count++;
        }
    }
}

static void free_thermal_assets(thermal_ctx_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->image_count; ++i) free_image(&ctx->images[i]);
    ctx->image_count = 0;
}

static void palette_lerp(float t, const uint8_t (*p)[3], int count,
                         uint8_t *r, uint8_t *g, uint8_t *b) {
    if (!p || count <= 0) {
        *r = *g = *b = 0;
        return;
    }
    if (t <= 0.0f) {
        *r = p[0][0]; *g = p[0][1]; *b = p[0][2];
        return;
    }
    if (t >= 1.0f) {
        *r = p[count - 1][0]; *g = p[count - 1][1]; *b = p[count - 1][2];
        return;
    }
    float pos = t * (float)(count - 1);
    int idx = (int)pos;
    float f = pos - (float)idx;
    *r = (uint8_t)((1.0f - f) * p[idx][0] + f * p[idx + 1][0] + 0.5f);
    *g = (uint8_t)((1.0f - f) * p[idx][1] + f * p[idx + 1][1] + 0.5f);
    *b = (uint8_t)((1.0f - f) * p[idx][2] + f * p[idx + 1][2] + 0.5f);
}

static void thermal_color(int mode, uint8_t value,
                          uint8_t *r, uint8_t *g, uint8_t *b) {
    static const uint8_t rainbow[][3] = {
        {0, 0, 64}, {0, 128, 255}, {40, 220, 120}, {255, 230, 60}, {255, 40, 0}
    };
    static const uint8_t iron[][3] = {
        {0, 0, 0}, {90, 22, 12}, {190, 70, 20}, {255, 190, 80}, {255, 255, 235}
    };
    static const uint8_t sepia[][3] = {
        {18, 12, 8}, {80, 54, 30}, {150, 106, 58}, {225, 186, 112}, {255, 244, 190}
    };
    static const uint8_t blue_red[][3] = {
        {0, 8, 80}, {0, 110, 210}, {236, 236, 180}, {226, 74, 52}, {96, 0, 0}
    };
    static const uint8_t rainbow1[][3] = {
        {0, 0, 0}, {0, 0, 180}, {0, 200, 255}, {20, 220, 70}, {255, 240, 40}, {255, 0, 180}, {255, 255, 255}
    };
    static const uint8_t rainbow2[][3] = {
        {0, 0, 120}, {0, 210, 255}, {60, 230, 85}, {255, 235, 70}, {255, 0, 170}, {255, 255, 255}
    };
    static const uint8_t rainbow3[][3] = {
        {0, 0, 80}, {0, 180, 235}, {80, 220, 80}, {255, 226, 70}, {255, 128, 36}, {230, 0, 0}
    };
    static const uint8_t pseudo1[][3] = {
        {0, 0, 0}, {0, 96, 96}, {64, 255, 64}, {255, 255, 80}, {255, 255, 255}
    };
    static const uint8_t pseudo2[][3] = {
        {12, 18, 42}, {50, 110, 180}, {120, 210, 150}, {246, 205, 92}, {255, 72, 72}
    };
    static const uint8_t metal1[][3] = {
        {8, 10, 14}, {42, 52, 66}, {110, 124, 132}, {200, 178, 126}, {255, 246, 210}
    };
    static const uint8_t metal2[][3] = {
        {0, 0, 20}, {28, 62, 92}, {100, 110, 116}, {178, 144, 88}, {255, 255, 235}
    };
    static const uint8_t zhou[][3] = {
        {0, 12, 32}, {0, 92, 118}, {80, 170, 86}, {220, 184, 52}, {252, 248, 190}
    };
    static const uint8_t ning[][3] = {
        {12, 4, 30}, {52, 18, 96}, {130, 52, 130}, {220, 108, 96}, {255, 236, 190}
    };

    float t = (float)value / 255.0f;
    switch (mode) {
        case 1: *r = *g = *b = 255 - value; break;
        case 2: case 6: *r = *g = *b = value; break;
        case 3: palette_lerp(t, iron, (int)(sizeof(iron) / sizeof(iron[0])), r, g, b); break;
        case 4: palette_lerp(t, sepia, (int)(sizeof(sepia) / sizeof(sepia[0])), r, g, b); break;
        case 5: palette_lerp(t, blue_red, (int)(sizeof(blue_red) / sizeof(blue_red[0])), r, g, b); break;
        case 7: palette_lerp(t, rainbow1, (int)(sizeof(rainbow1) / sizeof(rainbow1[0])), r, g, b); break;
        case 8: palette_lerp(t, rainbow2, (int)(sizeof(rainbow2) / sizeof(rainbow2[0])), r, g, b); break;
        case 9: palette_lerp(t, rainbow3, (int)(sizeof(rainbow3) / sizeof(rainbow3[0])), r, g, b); break;
        case 10: palette_lerp(t, pseudo1, (int)(sizeof(pseudo1) / sizeof(pseudo1[0])), r, g, b); break;
        case 11: palette_lerp(t, pseudo2, (int)(sizeof(pseudo2) / sizeof(pseudo2[0])), r, g, b); break;
        case 12: palette_lerp(t, metal1, (int)(sizeof(metal1) / sizeof(metal1[0])), r, g, b); break;
        case 13: palette_lerp(t, metal2, (int)(sizeof(metal2) / sizeof(metal2[0])), r, g, b); break;
        case 14: palette_lerp(t, zhou, (int)(sizeof(zhou) / sizeof(zhou[0])), r, g, b); break;
        case 15: palette_lerp(t, ning, (int)(sizeof(ning) / sizeof(ning[0])), r, g, b); break;
        case 0:
        default:
            palette_lerp(t, rainbow, (int)(sizeof(rainbow) / sizeof(rainbow[0])), r, g, b);
            break;
    }
}

static uint8_t image_luma(const thermal_image_t *img, int x, int y) {
    if (img && img->rgb && img->width > 0 && img->height > 0) {
        const uint8_t *p = img->rgb + ((size_t)y * (size_t)img->width + (size_t)x) * 3;
        return (uint8_t)((77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8);
    }
    int cx = x - 320;
    int cy = y - 240;
    int d = cx * cx + cy * cy;
    int hot = 255 - d / 900;
    int gradient = 40 + (x * 130) / 640 + (y * 60) / 480;
    if (hot > gradient) gradient = hot;
    return clamp_u8(gradient);
}

static void draw_thermal_image(uint8_t *dst, int stride, int width, int height,
                               int x, int y, int w, int h,
                               const thermal_image_t *img, int mode) {
    int src_w = img && img->width > 0 ? img->width : 640;
    int src_h = img && img->height > 0 ? img->height : 480;
    int out_w = w;
    int out_h = (int)((int64_t)src_h * w / src_w);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)src_w * h / src_h);
    }
    if (out_w <= 0 || out_h <= 0) return;

    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;
    uint8_t *uv = dst + (size_t)stride * (size_t)height;

    for (int dy = 0; dy < out_h; ++dy) {
        int sy = dy * src_h / out_h;
        int yy_pos = oy + dy;
        if (yy_pos < 0 || yy_pos >= height) continue;
        uint8_t *drow = dst + (size_t)yy_pos * (size_t)stride;
        for (int dx = 0; dx < out_w; ++dx) {
            int sx = dx * src_w / out_w;
            int xx = ox + dx;
            if (xx < 0 || xx >= width) continue;
            uint8_t r, g, b, yy, uu, vv;
            thermal_color(mode, image_luma(img, sx, sy), &r, &g, &b);
            rgb_to_yuv(r, g, b, &yy, &uu, &vv);
            (void)uu;
            (void)vv;
            drow[xx] = yy;
        }
    }

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy = dy * src_h / out_h;
        int yy_pos = oy + dy;
        if (yy_pos < 0 || yy_pos >= height) continue;
        uint8_t *drow = uv + (size_t)(yy_pos / 2) * (size_t)stride;
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx = dx * src_w / out_w;
            int xx = (ox + dx) & ~1;
            if (xx < 0 || xx + 1 >= width) continue;
            uint8_t r, g, b, yy, uu, vv;
            thermal_color(mode, image_luma(img, sx, sy), &r, &g, &b);
            rgb_to_yuv(r, g, b, &yy, &uu, &vv);
            (void)yy;
            drow[xx] = uu;
            drow[xx + 1] = vv;
        }
    }
}

static void draw_thermal_page(uint8_t *dst, int stride, int width, int height,
                              int frame, void *opaque) {
    static const char *names[] = {
        "RAINBOW", "BLACK HOT", "WHITE HOT", "IRON",
        "SEPIA", "BLUE RED", "GRAYSCALE", "RAINBOW1",
        "RAINBOW2", "RAINBOW3", "PSEUDO1", "PSEUDO2",
        "METAL1", "METAL2", "ZHOU", "NING",
    };
    thermal_ctx_t *ctx = (thermal_ctx_t *)opaque;
    int asset_index = ctx && ctx->image_count > 0 ?
        (frame / (TH_PAGE_FPS * 3)) % ctx->image_count : -1;
    const thermal_image_t *img = asset_index >= 0 ? &ctx->images[asset_index] : NULL;
    char frame_text[64];
    snprintf(frame_text, sizeof(frame_text), "FRAME %06d ASSET %02d/%02d",
             frame, asset_index + 1, ctx ? ctx->image_count : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height,
                                12, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 178,
                                26, 150, 90);
    page_surface_draw_text(dst, stride, width, height, 54, 52,
                           "THERMAL", 9, 232, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 54, 136,
                           frame_text, 3, 196, 142, 92);

    const int margin_x = 36;
    const int start_y = 224;
    const int gap = 12;
    const int cols = 4;
    const int rows = 4;
    const int cell_w = (width - margin_x * 2 - gap * (cols - 1)) / cols;
    const int cell_h = (height - start_y - 58 - gap * (rows - 1)) / rows;

    for (int i = 0; i < 16; ++i) {
        int col = i % cols;
        int row = i / cols;
        int x = margin_x + col * (cell_w + gap);
        int y = start_y + row * (cell_h + gap);
        page_surface_fill_rect_nv12(dst, stride, width, height, x, y, cell_w, cell_h,
                                    22, 128, 128);
        page_surface_fill_rect_nv12(dst, stride, width, height, x + 4, y + 4,
                                    cell_w - 8, 34, 38, 148, 84);
        page_surface_draw_text(dst, stride, width, height, x + 12, y + 12,
                               names[i], 2, 212, 108, 176);
        draw_thermal_image(dst, stride, width, height, x + 8, y + 48,
                           cell_w - 16, cell_h - 56, img, i);
    }
}

int page_thermal_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    thermal_ctx_t ctx;
    load_thermal_assets(&ctx);

    if (page_surface_open(&surface, TH_PAGE_POOL, TH_PAGE_W, TH_PAGE_H,
                          TH_PAGE_STRIDE, TH_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free_thermal_assets(&ctx);
        return 1;
    }

    set_tile_status("THERMAL", TILE_LOOP);
    set_tile_status("VO", TILE_LIVE);
    printf("THERMAL standalone page assets=%d modes=16. Ctrl+C to stop.\n",
           ctx.image_count);

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_thermal_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % TH_PAGE_FPS) == 0) {
                int asset = ctx.image_count > 0 ?
                    ((frame / (TH_PAGE_FPS * 3)) % ctx.image_count) + 1 : 0;
                printf("THERMAL frames=%d asset=%d/%d modes=16 standalone=1\n",
                       frame, asset, ctx.image_count);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / TH_PAGE_FPS);
    }

    page_surface_close(&surface);
    free_thermal_assets(&ctx);
    return 0;
}
