#include "page_ops.h"

#include "page_surface.h"
#include "media_api.h"

#include <linux/dma-buf.h>
#include <png.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define TSR_SCREEN_W 1080
#define TSR_SCREEN_H 1920
#define TSR_SCREEN_STRIDE 1088
#define TSR_SCREEN_POOL 1
#define TSR_FPS 30
#define TSR_INPUT_POOL 8
#define TSR_OUTPUT_POOL 9
#define TSR_GRP 0
#define TSR_IN_W 320
#define TSR_IN_H 256
#define TSR_OUT_W 1280
#define TSR_OUT_H 1024
#define TSR_MODEL_PATH "assets/thermal_sr/A_ETISR_256X320.rknn"
#define TSR_INPUT_PATH "assets/thermal_sr/input_320x256.png"
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *input_gray;
    uint8_t *sr_gray;
    MEDIA_THERMAL_SR_NPU_PERF perf;
    int ready;
} thermal_sr_page_ctx_t;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int load_png_gray(const char *path, uint8_t **out, int *out_w, int *out_h) {
    FILE *fp = fopen(path, "rb");
    png_structp png = NULL;
    png_infop info = NULL;
    uint8_t *raw = NULL;
    png_bytep *rows = NULL;
    uint8_t *gray = NULL;
    int ret = -1;

    if (!fp) return -1;
    uint8_t sig[8];
    if (fread(sig, 1, sizeof(sig), fp) != sizeof(sig) ||
        png_sig_cmp(sig, 0, sizeof(sig)) != 0) {
        goto done;
    }
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) goto done;
    info = png_create_info_struct(png);
    if (!info) goto done;
    if (setjmp(png_jmpbuf(png))) goto done;

    png_init_io(png, fp);
    png_set_sig_bytes(png, sizeof(sig));
    png_read_info(png, info);

    int width = (int)png_get_image_width(png, info);
    int height = (int)png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);
    if (width <= 0 || height <= 0) goto done;
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png);

    png_read_update_info(png, info);
    int channels = png_get_channels(png, info);
    png_size_t rowbytes = png_get_rowbytes(png, info);
    raw = (uint8_t *)malloc(rowbytes * (size_t)height);
    rows = (png_bytep *)malloc(sizeof(png_bytep) * (size_t)height);
    gray = (uint8_t *)malloc((size_t)width * (size_t)height);
    if (!raw || !rows || !gray) goto done;
    for (int y = 0; y < height; ++y) rows[y] = raw + (size_t)y * rowbytes;
    png_read_image(png, rows);

    for (int y = 0; y < height; ++y) {
        const uint8_t *src = rows[y];
        uint8_t *dst = gray + (size_t)y * (size_t)width;
        for (int x = 0; x < width; ++x) {
            if (channels >= 3) {
                int r = src[x * channels + 0];
                int g = src[x * channels + 1];
                int b = src[x * channels + 2];
                dst[x] = (uint8_t)((77 * r + 150 * g + 29 * b + 128) >> 8);
            } else {
                dst[x] = src[x * channels];
            }
        }
    }
    *out = gray;
    *out_w = width;
    *out_h = height;
    gray = NULL;
    ret = 0;

done:
    free(gray);
    free(rows);
    free(raw);
    if (png || info) png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return ret;
}

static int run_thermal_sr_once(thermal_sr_page_ctx_t *ctx) {
    MEDIA_BUFFER in = { .pool_id = -1, .index = -1 };
    MEDIA_BUFFER out = { .pool_id = -1, .index = -1 };
    int input_pool = 0;
    int output_pool = 0;
    int module_created = 0;
    int module_started = 0;
    int ret = -1;
    int png_w = 0;
    int png_h = 0;

    if (!ctx) return -1;
    if (load_png_gray(TSR_INPUT_PATH, &ctx->input_gray, &png_w, &png_h) != 0 ||
        png_w != TSR_IN_W || png_h != TSR_IN_H) {
        fprintf(stderr, "THERMAL_SR_NPU page: load input failed %s %dx%d\n", TSR_INPUT_PATH, png_w, png_h);
        return -1;
    }
    ctx->sr_gray = (uint8_t *)calloc(1, (size_t)TSR_OUT_W * (size_t)TSR_OUT_H);
    if (!ctx->sr_gray) return -1;

    if (MEDIA_POOL_Create(TSR_INPUT_POOL, TSR_IN_W * TSR_IN_H, 2) != 0) goto done;
    input_pool = 1;
    if (MEDIA_POOL_Create(TSR_OUTPUT_POOL, TSR_OUT_W * TSR_OUT_H, 2) != 0) goto done;
    output_pool = 1;

    MEDIA_THERMAL_SR_NPU_ATTR attr;
    memset(&attr, 0, sizeof(attr));
    attr.model_path = TSR_MODEL_PATH;
    attr.input_width = TSR_IN_W;
    attr.input_height = TSR_IN_H;
    attr.input_format = MEDIA_FORMAT_GRAY8;
    attr.output_pool_id = TSR_OUTPUT_POOL;
    attr.core_mask = 7;
    if (MEDIA_THERMAL_SR_NPU_CreateGrp(TSR_GRP, &attr) != 0) goto done;
    module_created = 1;
    if (MEDIA_THERMAL_SR_NPU_Start(TSR_GRP) != 0) goto done;
    module_started = 1;

    for (int i = 0; i < 6; ++i) {
        if (MEDIA_POOL_GetBuffer(TSR_INPUT_POOL, &in) != 0) goto done;
        uint8_t *in_addr = (uint8_t *)MEDIA_POOL_GetVaddr(in);
        if (!in_addr) goto done;
        MEDIA_POOL_BeginCpuAccess(in, DMA_BUF_SYNC_WRITE);
        memcpy(in_addr, ctx->input_gray, TSR_IN_W * TSR_IN_H);
        MEDIA_POOL_EndCpuAccess(in, DMA_BUF_SYNC_WRITE);
        if (MEDIA_THERMAL_SR_NPU_SendFrame(TSR_GRP, in, 1000) != 0) goto done;
        in.pool_id = -1;
        in.index = -1;
        if (MEDIA_THERMAL_SR_NPU_GetFrame(TSR_GRP, &out, 5000) != 0) goto done;
        uint8_t *out_addr = (uint8_t *)MEDIA_POOL_GetVaddr(out);
        if (!out_addr) goto done;
        MEDIA_POOL_BeginCpuAccess(out, DMA_BUF_SYNC_READ);
        memcpy(ctx->sr_gray, out_addr, (size_t)TSR_OUT_W * (size_t)TSR_OUT_H);
        MEDIA_POOL_EndCpuAccess(out, DMA_BUF_SYNC_READ);
        MEDIA_THERMAL_SR_NPU_GetLastPerf(TSR_GRP, &ctx->perf);
        MEDIA_THERMAL_SR_NPU_ReleaseFrame(TSR_GRP, out);
        out.pool_id = -1;
        out.index = -1;
    }
    ctx->ready = 1;
    ret = 0;

done:
    if (out.pool_id >= 0) MEDIA_THERMAL_SR_NPU_ReleaseFrame(TSR_GRP, out);
    if (in.pool_id >= 0) MEDIA_POOL_PutBuffer(in);
    if (module_started) MEDIA_THERMAL_SR_NPU_Stop(TSR_GRP);
    if (module_created) MEDIA_THERMAL_SR_NPU_DestroyGrp(TSR_GRP);
    if (output_pool) MEDIA_POOL_Destroy(TSR_OUTPUT_POOL);
    if (input_pool) MEDIA_POOL_Destroy(TSR_INPUT_POOL);
    return ret;
}

static void draw_scaled_gray(uint8_t *dst, int stride, int screen_w, int screen_h,
                             int dx, int dy, int dw, int dh,
                             const uint8_t *src, int sw, int sh) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * sh / dh;
        int yy = dy + y;
        if (yy < 0 || yy >= screen_h) continue;
        for (int x = 0; x < dw; ++x) {
            int sx = x * sw / dw;
            int xx = dx + x;
            if (xx < 0 || xx >= screen_w) continue;
            uint8_t yv = src[(size_t)sy * (size_t)sw + sx];
            dst[(size_t)yy * stride + xx] = yv;
            if ((xx & 1) == 0 && (yy & 1) == 0) {
                uint8_t *uv = dst + (size_t)stride * screen_h;
                uv[(size_t)(yy / 2) * stride + xx] = 128;
                uv[(size_t)(yy / 2) * stride + xx + 1] = 128;
            }
        }
    }
}

static void draw_page(uint8_t *dst, int stride, int width, int height,
                      int frame, void *opaque) {
    thermal_sr_page_ctx_t *ctx = (thermal_sr_page_ctx_t *)opaque;
    char line[128];
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 190, 28, 128, 128);
    page_surface_draw_text(dst, stride, width, height, 42, 42,
                           "RK3588 THERMAL SR NPU", 5, 235, 80, 200);
    page_surface_draw_text(dst, stride, width, height, 42, 116,
                           "REAL BOARD GRAY8 320X256 TO 1280X1024", 3, 210, 128, 128);

    page_surface_fill_rect_nv12(dst, stride, width, height, 38, 238, 1004, 668, 34, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 38, 974, 1004, 668, 34, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 48, 248, 984, 648, 8, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 48, 984, 984, 648, 8, 128, 128);

    if (ctx && ctx->input_gray) {
        draw_scaled_gray(dst, stride, width, height, 88, 304, 904, 512,
                         ctx->input_gray, TSR_IN_W, TSR_IN_H);
    }
    if (ctx && ctx->sr_gray) {
        draw_scaled_gray(dst, stride, width, height, 88, 1040, 904, 512,
                         ctx->sr_gray, TSR_OUT_W, TSR_OUT_H);
    }

    page_surface_draw_text(dst, stride, width, height, 70, 254,
                           "BYPASS ORIGINAL GRAY8 320X256", 3, 235, 128, 128);
    page_surface_draw_text(dst, stride, width, height, 70, 990,
                           "NPU SUPER RESOLUTION GRAY8 4X", 3, 235, 128, 128);

    snprintf(line, sizeof(line), "INFER %.1fMS  POST %.1fMS  TOTAL %.1fMS",
             ctx ? ctx->perf.infer_ms : -1.0,
             ctx ? ctx->perf.post_ms : -1.0,
             ctx ? ctx->perf.total_ms : -1.0);
    page_surface_draw_text(dst, stride, width, height, 54, 1690, line, 3, 220, 128, 128);
    snprintf(line, sizeof(line), "MODULE THERMAL_SR_NPU  FRAME %05d", frame);
    page_surface_draw_text(dst, stride, width, height, 54, 1745, line, 3, 180, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height,
                                54 + (frame * 11) % 900, 1818, 86, 14,
                                210, 36, 220);
}

int page_thermal_sr_npu_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    thermal_sr_page_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (page_surface_open(&surface, TSR_SCREEN_POOL, TSR_SCREEN_W, TSR_SCREEN_H,
                          TSR_SCREEN_STRIDE, TSR_FPS, 4, LICENSE_PATH) != 0) {
        return 1;
    }
    if (run_thermal_sr_once(&ctx) != 0) {
        fprintf(stderr, "THERMAL_SR_NPU page: module processing failed\n");
    }

    long long next_ms = now_ms();
    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_page, &ctx, frame) != 0) break;
        frame++;
        next_ms += 1000 / TSR_FPS;
        long long wait = next_ms - now_ms();
        if (wait > 0) usleep((useconds_t)wait * 1000);
    }
    free(ctx.input_gray);
    free(ctx.sr_gray);
    page_surface_close(&surface);
    return 0;
}
