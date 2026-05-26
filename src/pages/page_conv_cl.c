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

#define CV_PAGE_W 1080
#define CV_PAGE_H 1920
#define CV_PAGE_STRIDE 1088
#define CV_PAGE_POOL 1
#define CV_PAGE_FPS 30
#define CV_GRP 68
#define CV_INPUT_POOL 6
#define CV_OUTPUT_POOL 7
#define CV_W 640
#define CV_H 640
#define CV_STRIDE (CV_W * 4)
#define CV_FRAME_SIZE (CV_STRIDE * CV_H)
#define CV_ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define CV_LIVE_SCREEN_W 1080
#define CV_LIVE_SCREEN_H 1920
#define CV_LIVE_CAMERA_POOL 2
#define CV_LIVE_PRE_POOL 12
#define CV_LIVE_RGBA_POOL 10
#define CV_LIVE_CONV_POOL 11
#define CV_LIVE_BACK_POOL 13
#define CV_LIVE_RESIZE_POOL 9
#define CV_LIVE_OSD_POOL 8
#define CV_LIVE_PRE_RESIZE_GRP 62
#define CV_LIVE_CSC_GRP 64
#define CV_LIVE_CONV_GRP 68
#define CV_LIVE_BACK_CSC_GRP 75
#define CV_LIVE_RESIZE_GRP 61
#define CV_LIVE_OSD_GRP 81
#define CV_LIVE_SRC_W 1920
#define CV_LIVE_SRC_H 1080
#define CV_LIVE_SRC_STRIDE 3840
#define CV_LIVE_CPU_SRC_STRIDE 1920
#define CV_LIVE_CPU_BUF_H 1088
#define CV_LIVE_PROC_W 960
#define CV_LIVE_PROC_H 540
#define CV_LIVE_PROC_STRIDE 960
#define CV_LIVE_RGBA_STRIDE (CV_LIVE_PROC_W * 4)
#define CV_LIVE_VIEW_W 1080
#define CV_LIVE_VIEW_H 608
#define CV_LIVE_VIEW_STRIDE CV_ALIGN_UP_LOCAL(CV_LIVE_VIEW_W, 64)
#define CV_LIVE_VIEW_X 0
#define CV_LIVE_VIEW_Y 320
#define CV_LIVE_FPS 30
#define CV_LIVE_CAMERA_DEVICE "/dev/video-camera0"
#define CV_LIVE_SRC_SIZE ((size_t)CV_LIVE_SRC_STRIDE * (size_t)CV_LIVE_SRC_H * 3u / 2u)
#define CV_LIVE_CPU_SRC_SIZE ((size_t)CV_LIVE_CPU_SRC_STRIDE * (size_t)CV_LIVE_CPU_BUF_H * 3u / 2u)
#define CV_LIVE_PROC_NV12_SIZE ((size_t)CV_LIVE_PROC_STRIDE * (size_t)CV_LIVE_PROC_H * 3u / 2u)
#define CV_LIVE_RGBA_SIZE ((size_t)CV_LIVE_RGBA_STRIDE * (size_t)CV_LIVE_PROC_H)
#define CV_LIVE_VIEW_SIZE ((size_t)CV_LIVE_VIEW_STRIDE * (size_t)CV_LIVE_VIEW_H * 3u / 2u)
#define CV_LIVE_TEXT_MASK_W 1024
#define CV_LIVE_TEXT_MASK_H 64
#define CV_EFFECT_COUNT 4
#define CV_STAGE_SECONDS 10
#define CV_STAGE_COUNT 3
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *input;
    uint8_t *output;
    int processed;
    int module_ok;
    MEDIA_CONV_CL_PERF perf;
} conv_ctx_t;

typedef struct {
    const char *name;
    int kernel_size;
    const float *table;
    int table_size;
} conv_kernel_stage_t;

typedef struct {
    const char *title;
    const char *focus;
    const char *log_name;
    const conv_kernel_stage_t *effects;
} conv_showcase_stage_t;

typedef struct {
    uint8_t *input;
    uint8_t *outputs[CV_EFFECT_COUNT];
    uint64_t counts[CV_STAGE_COUNT][CV_EFFECT_COUNT];
    int vi_frames;
    int module_ok;
    int current_stage;
    double kernel_ms;
    double queue_ms;
    char perf_line[128];
} conv_showcase_ctx_t;

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int pre_pool_ok;
    int rgba_pool_ok;
    int conv_pool_ok;
    int back_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int pre_resize_ok;
    int csc_ok;
    int conv_ok;
    int back_csc_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_pre_ok;
    int bind_pre_csc_ok;
    int bind_csc_conv_ok;
    int bind_conv_back_ok;
    int bind_back_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
    int kernel_index;
} conv_live_chain_t;

static const float k_conv_sharpen3[9] = {
    0.0f, -1.0f, 0.0f,
    -1.0f, 5.0f, -1.0f,
    0.0f, -1.0f, 0.0f,
};

static const float k_conv_edge3[9] = {
    -1.0f, -1.0f, -1.0f,
    -1.0f, 8.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
};

static const float k_conv_emboss3[9] = {
    -2.0f, -1.0f, 0.0f,
    -1.0f, 1.0f, 1.0f,
    0.0f, 1.0f, 2.0f,
};

static const float k_conv_blur3[9] = {
    1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f,
    1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f,
    1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f,
};

static float k_conv_sharpen11[11 * 11];
static float k_conv_sharpen21[21 * 21];
static float k_conv_blur11[11 * 11];
static float k_conv_blur21[21 * 21];
static int k_conv_kernel_tables_ready = 0;

static const conv_kernel_stage_t k_conv_live_stages[] = {
    {"SHARPEN3", 3, k_conv_sharpen3, 9},
    {"EDGE3", 3, k_conv_edge3, 9},
    {"BLUR3", 3, k_conv_blur3, 9},
};

static const conv_kernel_stage_t k_conv_four_effects[CV_EFFECT_COUNT] = {
    {"SHARPEN", 3, k_conv_sharpen3, 9},
    {"EDGE", 3, k_conv_edge3, 9},
    {"EMBOSS", 3, k_conv_emboss3, 9},
    {"BLUR", 3, k_conv_blur3, 9},
};

static const conv_kernel_stage_t k_conv_sharpen_size_effects[CV_EFFECT_COUNT] = {
    {"RAW", 0, NULL, 0},
    {"SHARP 3X3", 3, k_conv_sharpen3, 9},
    {"SHARP 11X11", 11, k_conv_sharpen11, 11 * 11},
    {"SHARP 21X21", 21, k_conv_sharpen21, 21 * 21},
};

static const conv_kernel_stage_t k_conv_blur_size_effects[CV_EFFECT_COUNT] = {
    {"RAW", 0, NULL, 0},
    {"BLUR 3X3", 3, k_conv_blur3, 9},
    {"BLUR 11X11", 11, k_conv_blur11, 11 * 11},
    {"BLUR 21X21", 21, k_conv_blur21, 21 * 21},
};

static const conv_showcase_stage_t k_conv_showcase_stages[CV_STAGE_COUNT] = {
    {"CONV CL FOUR KERNELS",
     "FOUR 3X3 KERNELS SAME VI FRAME",
     "four-kernels",
     k_conv_four_effects},
    {"CONV CL SHARP SIZE",
     "RAW AND 3X3 11X11 21X21 SHARPEN",
     "sharpen-size",
     k_conv_sharpen_size_effects},
    {"CONV CL BLUR SIZE",
     "RAW AND 3X3 11X11 21X21 BLUR",
     "blur-size",
     k_conv_blur_size_effects},
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

static void yuv_to_rgb(uint8_t yy, uint8_t uu, uint8_t vv,
                       uint8_t *r, uint8_t *g, uint8_t *b) {
    int c = (int)yy - 16;
    int d = (int)uu - 128;
    int e = (int)vv - 128;
    if (c < 0) c = 0;
    *r = clamp_u8((298 * c + 409 * e + 128) >> 8);
    *g = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = clamp_u8((298 * c + 516 * d + 128) >> 8);
}

static void init_box_blur_kernel(float *table, int kernel_size) {
    if (!table || kernel_size <= 0) return;
    int total = kernel_size * kernel_size;
    float v = 1.0f / (float)total;
    for (int i = 0; i < total; ++i) {
        table[i] = v;
    }
}

static void init_unsharp_kernel(float *table, int kernel_size, float amount) {
    if (!table || kernel_size <= 0) return;
    int total = kernel_size * kernel_size;
    float outer = -amount / (float)(total - 1);
    for (int i = 0; i < total; ++i) {
        table[i] = outer;
    }
    table[(kernel_size / 2) * kernel_size + (kernel_size / 2)] = 1.0f + amount;
}

static void init_conv_kernel_size_tables(void) {
    if (k_conv_kernel_tables_ready) return;
    init_unsharp_kernel(k_conv_sharpen11, 11, 1.2f);
    init_unsharp_kernel(k_conv_sharpen21, 21, 1.2f);
    init_box_blur_kernel(k_conv_blur11, 11);
    init_box_blur_kernel(k_conv_blur21, 21);
    k_conv_kernel_tables_ready = 1;
}

static void nv12_to_rgba_scaled(const uint8_t *src, int src_w, int src_h,
                                int src_stride, uint8_t *dst) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || src_stride < src_w) return;
    const uint8_t *y_plane = src;
    const uint8_t *uv_plane = src + (size_t)src_stride * (size_t)src_h;
    for (int y = 0; y < CV_H; ++y) {
        int sy = (int)(((int64_t)y * src_h) / CV_H);
        const uint8_t *y_row = y_plane + (size_t)sy * src_stride;
        const uint8_t *uv_row = uv_plane + (size_t)(sy / 2) * src_stride;
        uint8_t *rgba_row = dst + (size_t)y * CV_STRIDE;
        for (int x = 0; x < CV_W; ++x) {
            int sx = (int)(((int64_t)x * src_w) / CV_W);
            int uvx = sx & ~1;
            uint8_t *px = rgba_row + x * 4;
            yuv_to_rgb(y_row[sx], uv_row[uvx], uv_row[uvx + 1],
                       &px[0], &px[1], &px[2]);
            px[3] = 255;
        }
    }
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

static void fill_rgba_input(uint8_t *dst, int frame) {
    if (!dst) return;
    for (int y = 0; y < CV_H; ++y) {
        uint8_t *row = dst + (size_t)y * CV_STRIDE;
        for (int x = 0; x < CV_W; ++x) {
            int checker = (((x + frame * 3) / 42) ^ ((y + frame * 2) / 42)) & 1;
            uint8_t *px = row + x * 4;
            px[0] = clamp_u8(38 + x * 170 / CV_W + (checker ? 30 : 0));
            px[1] = clamp_u8(46 + y * 150 / CV_H + (checker ? 0 : 34));
            px[2] = clamp_u8(86 + ((x + y) * 72 / (CV_W + CV_H)));
            px[3] = 255;
        }
    }
    for (int i = 0; i < 9; ++i) {
        int cx = (80 + i * 61 + frame * 5) % (CV_W - 80);
        int cy = (120 + i * 47 + frame * 3) % (CV_H - 80);
        for (int y = 0; y < 56; ++y) {
            uint8_t *row = dst + (size_t)(cy + y) * CV_STRIDE + cx * 4;
            for (int x = 0; x < 56; ++x) {
                row[x * 4 + 0] = (uint8_t)(220 - i * 8);
                row[x * 4 + 1] = (uint8_t)(80 + i * 12);
                row[x * 4 + 2] = (uint8_t)(70 + x * 2);
                row[x * 4 + 3] = 255;
            }
        }
    }
}

static int setup_conv_module(void) {
    static const float sharpen3x3[9] = {
        0.0f, -1.0f, 0.0f,
        -1.0f, 5.0f, -1.0f,
        0.0f, -1.0f, 0.0f,
    };
    if (MEDIA_POOL_Create(CV_INPUT_POOL, CV_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(CV_OUTPUT_POOL, CV_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(CV_INPUT_POOL);
        return -1;
    }

    MEDIA_CONV_CL_ATTR attr = {0};
    attr.width = CV_W;
    attr.height = CV_H;
    attr.format = MEDIA_FORMAT_RGBA8888;
    attr.kernel_size = 3;
    attr.input_depth = 3;
    attr.output_pool_id = CV_OUTPUT_POOL;
    attr.input_stride = CV_STRIDE;
    attr.output_stride = CV_STRIDE;

    if (MEDIA_CONV_CL_CreateGrp(CV_GRP, &attr) != 0 ||
        MEDIA_CONV_CL_SetKernelSize(CV_GRP, 3) != 0 ||
        MEDIA_CONV_CL_SetTable(CV_GRP, sharpen3x3, 9) != 0 ||
        MEDIA_CONV_CL_Start(CV_GRP) != 0) {
        MEDIA_CONV_CL_Stop(CV_GRP);
        MEDIA_CONV_CL_DestroyGrp(CV_GRP);
        MEDIA_POOL_Destroy(CV_OUTPUT_POOL);
        MEDIA_POOL_Destroy(CV_INPUT_POOL);
        return -1;
    }
    return 0;
}

static void cleanup_conv_module(int enabled) {
    if (!enabled) return;
    MEDIA_CONV_CL_Stop(CV_GRP);
    MEDIA_CONV_CL_DestroyGrp(CV_GRP);
    MEDIA_POOL_Destroy(CV_OUTPUT_POOL);
    MEDIA_POOL_Destroy(CV_INPUT_POOL);
}

static int process_conv(conv_ctx_t *ctx) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (!ctx || !ctx->input || !ctx->output || !ctx->module_ok) return -1;
    if (MEDIA_POOL_GetBuffer(CV_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, ctx->input, CV_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("CONV_CL", CV_GRP, "input", in, 1000) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);
    if (MEDIA_CONV_CL_GetFrame(CV_GRP, &out, 1000) == 0) {
        ret = copy_from_buffer(out, ctx->output, CV_FRAME_SIZE);
        MEDIA_CONV_CL_ReleaseFrame(CV_GRP, out);
    }
    if (ret == 0) {
        ctx->processed++;
        (void)MEDIA_CONV_CL_GetLastPerf(CV_GRP, &ctx->perf);
    }
    return ret;
}

static void draw_rgba_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    uint8_t *duv = dst + (size_t)dstride * dheight;
    for (int y = 0; y < dh; ++y) {
        int sy = y * CV_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        for (int x = 0; x < dw; ++x) {
            int sx = x * CV_W / dw;
            const uint8_t *rgba = src + ((size_t)sy * CV_W + sx) * 4;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgba[0], rgba[1], rgba[2], &yy, &uu, &vv);
            (void)uu;
            (void)vv;
            drow[x] = yy;
        }
    }
    for (int y = 0; y < dh; y += 2) {
        int sy = y * CV_H / dh;
        uint8_t *drow = duv + (size_t)((dy + y) / 2) * dstride + (dx & ~1);
        for (int x = 0; x < dw; x += 2) {
            int sx = x * CV_W / dw;
            const uint8_t *rgba = src + ((size_t)sy * CV_W + sx) * 4;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgba[0], rgba[1], rgba[2], &yy, &uu, &vv);
            (void)yy;
            drow[x] = uu;
            drow[x + 1] = vv;
        }
    }
}

static void fill_rect_rgb(uint8_t *dst, int stride, int width, int height,
                          int x, int y, int w, int h,
                          uint8_t r, uint8_t g, uint8_t b) {
    uint8_t yy, uu, vv;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    page_surface_fill_rect_nv12(dst, stride, width, height, x, y, w, h, yy, uu, vv);
}

static void draw_text_rgb(uint8_t *dst, int stride, int width, int height,
                          int x, int y, const char *text, int scale,
                          uint8_t r, uint8_t g, uint8_t b) {
    uint8_t yy, uu, vv;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    page_surface_draw_text(dst, stride, width, height, x, y, text, scale, yy, uu, vv);
}

static void stroke_rect_rgb(uint8_t *dst, int stride, int width, int height,
                            int x, int y, int w, int h, int t,
                            uint8_t r, uint8_t g, uint8_t b) {
    if (t <= 0) return;
    fill_rect_rgb(dst, stride, width, height, x, y, w, t, r, g, b);
    fill_rect_rgb(dst, stride, width, height, x, y + h - t, w, t, r, g, b);
    fill_rect_rgb(dst, stride, width, height, x, y, t, h, r, g, b);
    fill_rect_rgb(dst, stride, width, height, x + w - t, y, t, h, r, g, b);
}

static int conv_stage_for_frame(int frame) {
    int stage = (frame / (CV_PAGE_FPS * CV_STAGE_SECONDS)) % CV_STAGE_COUNT;
    return stage < 0 ? 0 : stage;
}

static void draw_conv_showcase_page(uint8_t *dst, int stride, int width, int height,
                                    int frame, void *opaque) {
    conv_showcase_ctx_t *ctx = (conv_showcase_ctx_t *)opaque;
    int stage_idx = ctx ? ctx->current_stage : conv_stage_for_frame(frame);
    if (stage_idx < 0 || stage_idx >= CV_STAGE_COUNT) stage_idx = 0;
    const conv_showcase_stage_t *stage = &k_conv_showcase_stages[stage_idx];
    const int grid = 904;
    const int cell = grid / 2;
    const int grid_x = (width - grid) / 2;
    const int grid_y = 276;
    const uint8_t colors[CV_EFFECT_COUNT][3] = {
        {70, 170, 255},
        {255, 110, 120},
        {255, 210, 90},
        {110, 230, 170},
    };

    fill_rect_rgb(dst, stride, width, height, 0, 0, width, height, 17, 23, 34);
    fill_rect_rgb(dst, stride, width, height, 0, 0, width, 116, 13, 20, 30);
    draw_text_rgb(dst, stride, width, height, 30, 28,
                  "CONV CL", 4, 170, 255, 220);
    draw_text_rgb(dst, stride, width, height, 808, 30,
                  "PAGE 22/42", 2, 170, 255, 220);
    draw_text_rgb(dst, stride, width, height, 30, 76,
                  "FOUR GPU CONV OUTPUTS", 2, 210, 235, 255);

    fill_rect_rgb(dst, stride, width, height, 38, 130, width - 76, 126, 17, 25, 38);
    stroke_rect_rgb(dst, stride, width, height, 38, 130, width - 76, 126, 2, 66, 150, 210);
    draw_text_rgb(dst, stride, width, height, 60, 154,
                  stage->title, 4, 170, 255, 220);
    draw_text_rgb(dst, stride, width, height, 60, 204,
                  "FLOW VI NV12 TO RGBA TO CONV CL TO PAGE TO VO",
                  2, 210, 235, 255);
    draw_text_rgb(dst, stride, width, height, 60, 232,
                  "SAME FRAME ONLY KERNEL CHANGES",
                  2, 255, 230, 120);

    fill_rect_rgb(dst, stride, width, height, grid_x - 6, grid_y - 6,
                  grid + 12, grid + 12, 5, 10, 18);
    for (int i = 0; i < CV_EFFECT_COUNT; ++i) {
        int cx = grid_x + (i % 2) * cell;
        int cy = grid_y + (i / 2) * cell;
        const uint8_t *img = ctx ? ctx->outputs[i] : NULL;
        if (img) {
            draw_rgba_scaled(dst, stride, width, height, cx, cy, cell, cell, img);
        } else {
            fill_rect_rgb(dst, stride, width, height, cx, cy, cell, cell, 8, 12, 18);
        }
        stroke_rect_rgb(dst, stride, width, height, cx, cy, cell, cell, 2,
                        colors[i][0], colors[i][1], colors[i][2]);
        fill_rect_rgb(dst, stride, width, height, cx + 14, cy + 18, 238, 36, 0, 0, 0);
        fill_rect_rgb(dst, stride, width, height, cx + 14, cy + 18, 238, 5,
                      colors[i][0], colors[i][1], colors[i][2]);
        char label[80];
        snprintf(label, sizeof(label), "%s OUT %llu",
                 stage->effects[i].name,
                 ctx ? (unsigned long long)ctx->counts[stage_idx][i] : 0ULL);
        draw_text_rgb(dst, stride, width, height, cx + 26, cy + 34,
                      label, 1, 230, 245, 255);
    }

    fill_rect_rgb(dst, stride, width, height, 38, 1548, width - 76, 218, 12, 20, 31);
    stroke_rect_rgb(dst, stride, width, height, 38, 1548, width - 76, 218, 2, 0, 190, 170);
    draw_text_rgb(dst, stride, width, height, 60, 1582,
                  stage->focus, 3, 170, 255, 220);
    draw_text_rgb(dst, stride, width, height, 60, 1640,
                  ctx && ctx->perf_line[0] ? ctx->perf_line : "CPU NA GPU NA RGA NA",
                  3, 255, 230, 120);
    char cl_line[96];
    snprintf(cl_line, sizeof(cl_line), "CL KERNEL %04d QUEUE %04d",
             ctx && ctx->kernel_ms >= 0.0 ? (int)(ctx->kernel_ms * 100.0) : 0,
             ctx && ctx->queue_ms >= 0.0 ? (int)(ctx->queue_ms * 100.0) : 0);
    draw_text_rgb(dst, stride, width, height, 60, 1698,
                  cl_line, 3, 255, 230, 120);
    char count_line[160];
    snprintf(count_line, sizeof(count_line), "%s %llu  %s %llu  %s %llu  %s %llu",
             stage->effects[0].name, ctx ? (unsigned long long)ctx->counts[stage_idx][0] : 0ULL,
             stage->effects[1].name, ctx ? (unsigned long long)ctx->counts[stage_idx][1] : 0ULL,
             stage->effects[2].name, ctx ? (unsigned long long)ctx->counts[stage_idx][2] : 0ULL,
             stage->effects[3].name, ctx ? (unsigned long long)ctx->counts[stage_idx][3] : 0ULL);
    draw_text_rgb(dst, stride, width, height, 60, 1738,
                  count_line, 1, 190, 230, 255);

    draw_text_rgb(dst, stride, width, height, 30, 1828,
                  ctx && ctx->perf_line[0] ? ctx->perf_line : "CPU NA GPU NA RGA NA",
                  3, 255, 230, 120);
    draw_text_rgb(dst, stride, width, height, 30, 1888,
                  "FLOW VI 640X640 TO RGBA TO CONV CL FOUR OUTPUTS",
                  2, 210, 235, 255);
    draw_text_rgb(dst, stride, width, height, 842, 1888,
                  ctx && ctx->module_ok ? "LIVE" : "FALLBACK",
                  3, 170, 255, 220);
}

static void draw_conv_page(uint8_t *dst, int stride, int width, int height,
                           int frame, void *opaque) {
    conv_ctx_t *ctx = (conv_ctx_t *)opaque;
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
                           "CONV CL", 7, 235, 108, 176);

    int pane = 600;
    int x = (width - pane) / 2;
    int top_y = 246;
    int bottom_y = 930;
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, top_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, bottom_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, top_y - 34,
                           "RGBA INPUT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, x, bottom_y - 34,
                           "SHARPEN OUTPUT", 3, 220, 108, 176);
    if (ctx) {
        draw_rgba_scaled(dst, stride, width, height, x, top_y, pane, pane, ctx->input);
        draw_rgba_scaled(dst, stride, width, height, x, bottom_y, pane, pane, ctx->output);
    }
    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           "KERNEL 3X3 SHARPEN RGBA8888", 2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1725,
                           perf, 2, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1810,
                           ctx && ctx->module_ok ? "MODULE LIVE" : "MODULE FALLBACK",
                           3, 210, 108, 176);
}

static void drain_conv_live_resize_output(int grp) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_RESIZE_RGA_GetFrame(grp, &out, 0) != 0) break;
        MEDIA_RESIZE_RGA_ReleaseFrame(grp, out);
    }
}

static void drain_conv_live_csc_output(int grp) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_CSC_RGA_GetFrame(grp, &out, 0) != 0) break;
        MEDIA_CSC_RGA_ReleaseFrame(grp, out);
    }
}

static void drain_conv_live_conv_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_CONV_CL_GetFrame(CV_LIVE_CONV_GRP, &out, 0) != 0) break;
        MEDIA_CONV_CL_ReleaseFrame(CV_LIVE_CONV_GRP, out);
    }
}

static void drain_conv_live_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(CV_LIVE_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(CV_LIVE_OSD_GRP, out);
    }
}

static int update_conv_live_kernel(conv_live_chain_t *chain, int kernel_index) {
    int count = (int)(sizeof(k_conv_live_stages) / sizeof(k_conv_live_stages[0]));
    if (!chain || count <= 0) return -1;
    if (kernel_index < 0) kernel_index = 0;
    kernel_index %= count;
    const conv_kernel_stage_t *stage = &k_conv_live_stages[kernel_index];
    if (MEDIA_CONV_CL_SetKernelSize(CV_LIVE_CONV_GRP, stage->kernel_size) != 0 ||
        MEDIA_CONV_CL_SetTable(CV_LIVE_CONV_GRP, stage->table, stage->table_size) != 0) {
        return -1;
    }
    chain->kernel_index = kernel_index;
    return 0;
}

static int update_conv_live_overlay(const conv_live_chain_t *chain,
                                    uint64_t vi_count,
                                    uint64_t pre_count,
                                    uint64_t csc_count,
                                    uint64_t conv_count,
                                    uint64_t back_count,
                                    uint64_t resize_count,
                                    uint64_t osd_count,
                                    uint64_t vo_count,
                                    const MEDIA_CONV_CL_PERF *conv_perf) {
    static uint8_t masks[5][CV_LIVE_TEXT_MASK_W * CV_LIVE_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char mode_line[160];
    char count_line[220];
    int stage_count = (int)(sizeof(k_conv_live_stages) / sizeof(k_conv_live_stages[0]));
    int kernel_index = chain ? chain->kernel_index : 0;
    if (kernel_index < 0 || kernel_index >= stage_count) kernel_index = 0;
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(mode_line, sizeof(mode_line),
             "KERNEL %s  PROC %dx%d RGBA8888  CL %.3f/%.3f MS",
             k_conv_live_stages[kernel_index].name,
             CV_LIVE_PROC_W, CV_LIVE_PROC_H,
             conv_perf ? conv_perf->gpu_kernel_total_ms : 0.0,
             conv_perf ? conv_perf->gpu_queue_total_ms : 0.0);
    snprintf(count_line, sizeof(count_line),
             "VI %llu PRE %llu CSC %llu CONV %llu BACK %llu RGA %llu OSD %llu VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)pre_count,
             (unsigned long long)csc_count,
             (unsigned long long)conv_count,
             (unsigned long long)back_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(CV_LIVE_OSD_GRP, 0, 24, 16, 2,
                              "CONV_CL LIVE VI GPU CONV",
                              masks[0], sizeof(masks[0]), CV_LIVE_TEXT_MASK_W, CV_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CV_LIVE_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->RESIZE_RGA->CSC_RGA->CONV_CL->CSC_RGA->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), CV_LIVE_TEXT_MASK_W, CV_LIVE_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(CV_LIVE_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), CV_LIVE_TEXT_MASK_W, CV_LIVE_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(CV_LIVE_OSD_GRP, 3, 24, 554, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), CV_LIVE_TEXT_MASK_W, CV_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CV_LIVE_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), CV_LIVE_TEXT_MASK_W, CV_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_conv_live_chain(conv_live_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_RESIZE_RGA_ATTR pre = {0};
    MEDIA_CSC_RGA_ATTR csc = {0};
    MEDIA_CONV_CL_ATTR conv = {0};
    MEDIA_CSC_RGA_ATTR back = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(CV_LIVE_CAMERA_POOL, CV_LIVE_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(CV_LIVE_PRE_POOL, CV_LIVE_PROC_NV12_SIZE, 6) != 0) return -1;
    chain->pre_pool_ok = 1;
    if (MEDIA_POOL_Create(CV_LIVE_RGBA_POOL, CV_LIVE_RGBA_SIZE, 4) != 0) return -1;
    chain->rgba_pool_ok = 1;
    if (MEDIA_POOL_Create(CV_LIVE_CONV_POOL, CV_LIVE_RGBA_SIZE, 4) != 0) return -1;
    chain->conv_pool_ok = 1;
    if (MEDIA_POOL_Create(CV_LIVE_BACK_POOL, CV_LIVE_PROC_NV12_SIZE, 6) != 0) return -1;
    chain->back_pool_ok = 1;
    if (MEDIA_POOL_Create(CV_LIVE_RESIZE_POOL, CV_LIVE_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(CV_LIVE_OSD_POOL, CV_LIVE_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = CV_LIVE_CAMERA_DEVICE;
    vi.width = CV_LIVE_SRC_W;
    vi.height = CV_LIVE_SRC_H;
    vi.stride = CV_LIVE_SRC_STRIDE;
    vi.fps = CV_LIVE_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = CV_LIVE_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    pre.src_x = 0;
    pre.src_y = 0;
    pre.src_width = CV_LIVE_SRC_W;
    pre.src_height = CV_LIVE_SRC_H;
    pre.input_width = CV_LIVE_SRC_W;
    pre.input_height = CV_LIVE_SRC_H;
    pre.input_stride = CV_LIVE_SRC_STRIDE;
    pre.input_format = MEDIA_FORMAT_NV12;
    pre.input_depth = 4;
    pre.out_width = CV_LIVE_PROC_W;
    pre.out_height = CV_LIVE_PROC_H;
    pre.out_stride = CV_LIVE_PROC_STRIDE;
    pre.output_format = MEDIA_FORMAT_NV12;
    pre.output_pool_id = CV_LIVE_PRE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(CV_LIVE_PRE_RESIZE_GRP, &pre) != 0 ||
        MEDIA_RESIZE_RGA_Start(CV_LIVE_PRE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(CV_LIVE_PRE_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->pre_resize_ok = 1;

    csc.input_width = CV_LIVE_PROC_W;
    csc.input_height = CV_LIVE_PROC_H;
    csc.input_format = MEDIA_FORMAT_NV12;
    csc.output_format = MEDIA_FORMAT_RGBA8888;
    csc.input_depth = 4;
    csc.output_pool_id = CV_LIVE_RGBA_POOL;
    csc.input_stride = CV_LIVE_PROC_STRIDE;
    csc.output_stride = CV_LIVE_RGBA_STRIDE;
    csc.csc_mode = 0;
    if (MEDIA_CSC_RGA_CreateGrp(CV_LIVE_CSC_GRP, &csc) != 0 ||
        MEDIA_CSC_RGA_Start(CV_LIVE_CSC_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(CV_LIVE_CSC_GRP) != 0) {
        return -1;
    }
    chain->csc_ok = 1;

    conv.width = CV_LIVE_PROC_W;
    conv.height = CV_LIVE_PROC_H;
    conv.format = MEDIA_FORMAT_RGBA8888;
    conv.kernel_size = 3;
    conv.input_depth = 4;
    conv.output_pool_id = CV_LIVE_CONV_POOL;
    conv.input_stride = CV_LIVE_RGBA_STRIDE;
    conv.output_stride = CV_LIVE_RGBA_STRIDE;
    if (MEDIA_CONV_CL_CreateGrp(CV_LIVE_CONV_GRP, &conv) != 0 ||
        MEDIA_CONV_CL_Start(CV_LIVE_CONV_GRP) != 0) {
        return -1;
    }
    chain->conv_ok = 1;
    if (update_conv_live_kernel(chain, 0) != 0) return -1;

    back.input_width = CV_LIVE_PROC_W;
    back.input_height = CV_LIVE_PROC_H;
    back.input_format = MEDIA_FORMAT_RGBA8888;
    back.output_format = MEDIA_FORMAT_NV12;
    back.input_depth = 4;
    back.output_pool_id = CV_LIVE_BACK_POOL;
    back.input_stride = CV_LIVE_RGBA_STRIDE;
    back.output_stride = CV_LIVE_PROC_STRIDE;
    back.csc_mode = 0;
    if (MEDIA_CSC_RGA_CreateGrp(CV_LIVE_BACK_CSC_GRP, &back) != 0 ||
        MEDIA_CSC_RGA_Start(CV_LIVE_BACK_CSC_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(CV_LIVE_BACK_CSC_GRP) != 0) {
        return -1;
    }
    chain->back_csc_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = CV_LIVE_PROC_W;
    resize.src_height = CV_LIVE_PROC_H;
    resize.input_width = CV_LIVE_PROC_W;
    resize.input_height = CV_LIVE_PROC_H;
    resize.input_stride = CV_LIVE_PROC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 4;
    resize.out_width = CV_LIVE_VIEW_W;
    resize.out_height = CV_LIVE_VIEW_H;
    resize.out_stride = CV_LIVE_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = CV_LIVE_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(CV_LIVE_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(CV_LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(CV_LIVE_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = CV_LIVE_VIEW_W;
    osd.input_height = CV_LIVE_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = CV_LIVE_OSD_POOL;
    osd.input_stride = CV_LIVE_VIEW_STRIDE;
    osd.output_stride = CV_LIVE_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(CV_LIVE_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(CV_LIVE_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = CV_LIVE_SCREEN_W;
    vo.height = CV_LIVE_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, CV_LIVE_VIEW_X, CV_LIVE_VIEW_Y,
                           CV_LIVE_VIEW_W, CV_LIVE_VIEW_H,
                           CV_LIVE_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "RESIZE_RGA", CV_LIVE_PRE_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_vi_pre_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", CV_LIVE_PRE_RESIZE_GRP, "output0",
                       "CSC_RGA", CV_LIVE_CSC_GRP, "input") != 0) return -1;
    chain->bind_pre_csc_ok = 1;
    if (MEDIA_SYS_Bind("CSC_RGA", CV_LIVE_CSC_GRP, "output0",
                       "CONV_CL", CV_LIVE_CONV_GRP, "input") != 0) return -1;
    chain->bind_csc_conv_ok = 1;
    if (MEDIA_SYS_Bind("CONV_CL", CV_LIVE_CONV_GRP, "output0",
                       "CSC_RGA", CV_LIVE_BACK_CSC_GRP, "input") != 0) return -1;
    chain->bind_conv_back_ok = 1;
    if (MEDIA_SYS_Bind("CSC_RGA", CV_LIVE_BACK_CSC_GRP, "output0",
                       "RESIZE_RGA", CV_LIVE_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_back_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", CV_LIVE_RESIZE_GRP, "output0",
                       "OSD", CV_LIVE_OSD_GRP, "input") != 0) return -1;
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", CV_LIVE_OSD_GRP, "output0",
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
    set_tile_status("CONV_CL", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_conv_live_chain(conv_live_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", CV_LIVE_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", CV_LIVE_RESIZE_GRP, "output0",
                         "OSD", CV_LIVE_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_back_resize_ok) {
        MEDIA_SYS_UnBind("CSC_RGA", CV_LIVE_BACK_CSC_GRP, "output0",
                         "RESIZE_RGA", CV_LIVE_RESIZE_GRP, "input0");
        chain->bind_back_resize_ok = 0;
    }
    if (chain->bind_conv_back_ok) {
        MEDIA_SYS_UnBind("CONV_CL", CV_LIVE_CONV_GRP, "output0",
                         "CSC_RGA", CV_LIVE_BACK_CSC_GRP, "input");
        chain->bind_conv_back_ok = 0;
    }
    if (chain->bind_csc_conv_ok) {
        MEDIA_SYS_UnBind("CSC_RGA", CV_LIVE_CSC_GRP, "output0",
                         "CONV_CL", CV_LIVE_CONV_GRP, "input");
        chain->bind_csc_conv_ok = 0;
    }
    if (chain->bind_pre_csc_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", CV_LIVE_PRE_RESIZE_GRP, "output0",
                         "CSC_RGA", CV_LIVE_CSC_GRP, "input");
        chain->bind_pre_csc_ok = 0;
    }
    if (chain->bind_vi_pre_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "RESIZE_RGA", CV_LIVE_PRE_RESIZE_GRP, "input0");
        chain->bind_vi_pre_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_ok) {
        drain_conv_live_osd_output();
        MEDIA_OSD_Stop(CV_LIVE_OSD_GRP);
        drain_conv_live_osd_output();
        MEDIA_OSD_DestroyGrp(CV_LIVE_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_conv_live_resize_output(CV_LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Disable(CV_LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(CV_LIVE_RESIZE_GRP);
        drain_conv_live_resize_output(CV_LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(CV_LIVE_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->back_csc_ok) {
        drain_conv_live_csc_output(CV_LIVE_BACK_CSC_GRP);
        MEDIA_CSC_RGA_Disable(CV_LIVE_BACK_CSC_GRP);
        MEDIA_CSC_RGA_Stop(CV_LIVE_BACK_CSC_GRP);
        drain_conv_live_csc_output(CV_LIVE_BACK_CSC_GRP);
        MEDIA_CSC_RGA_DestroyGrp(CV_LIVE_BACK_CSC_GRP);
        chain->back_csc_ok = 0;
    }
    if (chain->conv_ok) {
        drain_conv_live_conv_output();
        MEDIA_CONV_CL_Stop(CV_LIVE_CONV_GRP);
        drain_conv_live_conv_output();
        MEDIA_CONV_CL_DestroyGrp(CV_LIVE_CONV_GRP);
        chain->conv_ok = 0;
    }
    if (chain->csc_ok) {
        drain_conv_live_csc_output(CV_LIVE_CSC_GRP);
        MEDIA_CSC_RGA_Disable(CV_LIVE_CSC_GRP);
        MEDIA_CSC_RGA_Stop(CV_LIVE_CSC_GRP);
        drain_conv_live_csc_output(CV_LIVE_CSC_GRP);
        MEDIA_CSC_RGA_DestroyGrp(CV_LIVE_CSC_GRP);
        chain->csc_ok = 0;
    }
    if (chain->pre_resize_ok) {
        drain_conv_live_resize_output(CV_LIVE_PRE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Disable(CV_LIVE_PRE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(CV_LIVE_PRE_RESIZE_GRP);
        drain_conv_live_resize_output(CV_LIVE_PRE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(CV_LIVE_PRE_RESIZE_GRP);
        chain->pre_resize_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(CV_LIVE_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(CV_LIVE_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->back_pool_ok) {
        MEDIA_POOL_Destroy(CV_LIVE_BACK_POOL);
        chain->back_pool_ok = 0;
    }
    if (chain->conv_pool_ok) {
        MEDIA_POOL_Destroy(CV_LIVE_CONV_POOL);
        chain->conv_pool_ok = 0;
    }
    if (chain->rgba_pool_ok) {
        MEDIA_POOL_Destroy(CV_LIVE_RGBA_POOL);
        chain->rgba_pool_ok = 0;
    }
    if (chain->pre_pool_ok) {
        MEDIA_POOL_Destroy(CV_LIVE_PRE_POOL);
        chain->pre_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(CV_LIVE_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

static int setup_conv_live_vi_capture(void) {
    MEDIA_VI_ATTR vi = {0};
    if (MEDIA_POOL_Create(CV_LIVE_CAMERA_POOL, CV_LIVE_CPU_SRC_SIZE, 6) != 0) {
        return -1;
    }
    vi.device = CV_LIVE_CAMERA_DEVICE;
    vi.width = CV_LIVE_SRC_W;
    vi.height = CV_LIVE_SRC_H;
    vi.stride = 0;
    vi.fps = CV_LIVE_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = CV_LIVE_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0 || MEDIA_VI_Enable(0) != 0) {
        MEDIA_POOL_Destroy(CV_LIVE_CAMERA_POOL);
        return -1;
    }
    set_tile_status("VI", TILE_LIVE);
    return 0;
}

static void cleanup_conv_live_vi_capture(int enabled) {
    if (!enabled) return;
    MEDIA_VI_Disable(0);
    usleep(50000);
    /*
     * VI may still own one queued camera buffer immediately after Disable.
     * MEDIA_SYS_Exit tears this pool down after the VI module is gone.
     */
}

static int capture_conv_live_vi_rgba(uint8_t *dst) {
    MEDIA_BUFFER cbuf = {-1, -1};
    void *addr = NULL;
    size_t size = 0;
    int mapped = 0;
    int ret = -1;
    if (!dst) return -1;
    if (MEDIA_VI_GetFrame(0, &cbuf, 30) != 0) return -1;
    if (MEDIA_POOL_BeginCpuAccess(cbuf, DMA_BUF_SYNC_READ) == 0) {
        size = MEDIA_POOL_GetSize(cbuf);
        addr = MEDIA_POOL_GetVaddr(cbuf);
        if (!addr && map_buffer(cbuf, &addr, &size, PROT_READ) == 0) {
            mapped = 1;
        }
        if (addr && (size == 0 || size >= CV_LIVE_CPU_SRC_SIZE)) {
            nv12_to_rgba_scaled((const uint8_t *)addr,
                                CV_LIVE_SRC_W, CV_LIVE_SRC_H,
                                CV_LIVE_CPU_SRC_STRIDE, dst);
            ret = 0;
        }
        if (mapped) {
            munmap(addr, size);
        }
        (void)MEDIA_POOL_EndCpuAccess(cbuf, DMA_BUF_SYNC_READ);
    }
    MEDIA_VI_ReleaseFrame(0, cbuf);
    return ret;
}

static void update_conv_showcase_perf_line(conv_showcase_ctx_t *ctx) {
    page_overlay_perf_t perf = {0};
    char gpu_text[24];
    char rga_text[24];
    if (!ctx) return;
    page_overlay_update_perf(&perf);
    snprintf(gpu_text, sizeof(gpu_text), perf.gpu_available ? "%.0f" : "NA",
             perf.gpu_percent);
    snprintf(rga_text, sizeof(rga_text), perf.rga_available ? "%.0f" : "NA",
             perf.rga_percent);
    snprintf(ctx->perf_line, sizeof(ctx->perf_line),
             "CPU %.0f GPU %s RGA %s VI %d",
             perf.cpu_percent, gpu_text, rga_text, ctx->vi_frames);
}

static int process_conv_effect(conv_showcase_ctx_t *ctx,
                               const conv_kernel_stage_t *effect,
                               int stage_idx,
                               int effect_idx,
                               double *kernel_sum,
                               double *queue_sum) {
    if (!ctx || !effect || effect_idx < 0 || effect_idx >= CV_EFFECT_COUNT) return -1;
    if (!ctx->outputs[effect_idx] || !ctx->input) return -1;
    if (effect->kernel_size <= 0) {
        memcpy(ctx->outputs[effect_idx], ctx->input, CV_FRAME_SIZE);
        ctx->counts[stage_idx][effect_idx]++;
        return 0;
    }
    if (!ctx->module_ok ||
        MEDIA_CONV_CL_SetKernelSize(CV_GRP, effect->kernel_size) != 0 ||
        MEDIA_CONV_CL_SetTable(CV_GRP, effect->table, effect->table_size) != 0) {
        memcpy(ctx->outputs[effect_idx], ctx->input, CV_FRAME_SIZE);
        return -1;
    }

    conv_ctx_t one = {0};
    one.input = ctx->input;
    one.output = ctx->outputs[effect_idx];
    one.module_ok = ctx->module_ok;
    if (process_conv(&one) != 0) {
        memcpy(ctx->outputs[effect_idx], ctx->input, CV_FRAME_SIZE);
        return -1;
    }
    ctx->counts[stage_idx][effect_idx]++;
    if (kernel_sum) *kernel_sum += one.perf.gpu_kernel_total_ms;
    if (queue_sum) *queue_sum += one.perf.gpu_queue_total_ms;
    return 0;
}

static void process_conv_showcase_frame(conv_showcase_ctx_t *ctx, int frame) {
    if (!ctx) return;
    int stage_idx = conv_stage_for_frame(frame);
    if (stage_idx < 0 || stage_idx >= CV_STAGE_COUNT) stage_idx = 0;
    const conv_showcase_stage_t *stage = &k_conv_showcase_stages[stage_idx];
    double kernel_sum = 0.0;
    double queue_sum = 0.0;
    int ok_count = 0;
    ctx->current_stage = stage_idx;
    ctx->kernel_ms = -1.0;
    ctx->queue_ms = -1.0;

    for (int i = 0; i < CV_EFFECT_COUNT; ++i) {
        if (process_conv_effect(ctx, &stage->effects[i], stage_idx, i,
                                &kernel_sum, &queue_sum) == 0) {
            ok_count++;
        }
    }
    if (ok_count == CV_EFFECT_COUNT) {
        ctx->kernel_ms = kernel_sum;
        ctx->queue_ms = queue_sum;
    }
}

static int run_conv_cl_showcase_page(volatile sig_atomic_t *running) {
    page_surface_t surface;
    conv_showcase_ctx_t ctx;
    int vi_ok = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.kernel_ms = -1.0;
    ctx.queue_ms = -1.0;
    ctx.input = malloc(CV_FRAME_SIZE);
    if (!ctx.input) return 1;
    for (int i = 0; i < CV_EFFECT_COUNT; ++i) {
        ctx.outputs[i] = malloc(CV_FRAME_SIZE);
        if (!ctx.outputs[i]) {
            for (int j = 0; j <= i; ++j) free(ctx.outputs[j]);
            free(ctx.input);
            return 1;
        }
        memset(ctx.outputs[i], 0, CV_FRAME_SIZE);
    }
    memset(ctx.input, 0, CV_FRAME_SIZE);
    init_conv_kernel_size_tables();
    update_conv_showcase_perf_line(&ctx);

    if (page_surface_open(&surface, CV_PAGE_POOL, CV_PAGE_W, CV_PAGE_H,
                          CV_PAGE_STRIDE, CV_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        for (int i = 0; i < CV_EFFECT_COUNT; ++i) free(ctx.outputs[i]);
        free(ctx.input);
        return 1;
    }

    vi_ok = setup_conv_live_vi_capture() == 0;
    ctx.module_ok = setup_conv_module() == 0;
    if (!vi_ok || !ctx.module_ok) {
        fprintf(stderr, "CONV_CL live showcase setup failed vi=%d module=%d\n",
                vi_ok, ctx.module_ok);
        cleanup_conv_module(ctx.module_ok);
        cleanup_conv_live_vi_capture(vi_ok);
        page_surface_close(&surface);
        for (int i = 0; i < CV_EFFECT_COUNT; ++i) free(ctx.outputs[i]);
        free(ctx.input);
        return 1;
    }
    set_tile_status("CONV_CL", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("CONV_CL standalone: VI %s %dx%d NV12 -> RGBA %dx%d -> single CONV_CL sequential 2x2 page -> VO. Ctrl+C to stop.\n",
           CV_LIVE_CAMERA_DEVICE, CV_LIVE_SRC_W, CV_LIVE_SRC_H, CV_W, CV_H);

    int frame = 0;
    while (!running || *running) {
        if (capture_conv_live_vi_rgba(ctx.input) == 0) {
            ctx.vi_frames++;
            process_conv_showcase_frame(&ctx, frame);
        }
        if ((frame % 15) == 0) update_conv_showcase_perf_line(&ctx);
        if (page_surface_send_frame(&surface, draw_conv_showcase_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % CV_PAGE_FPS) == 0) {
                int stage_idx = ctx.current_stage;
                if (stage_idx < 0 || stage_idx >= CV_STAGE_COUNT) stage_idx = 0;
                const conv_showcase_stage_t *stage = &k_conv_showcase_stages[stage_idx];
                printf("CONV_CL vi_frames=%d stage=%s %s=%llu %s=%llu %s=%llu %s=%llu cpu_gpu_rga=\"%s\" cl=%.3f/%.3f mode=single-sequential standalone=1\n",
                       ctx.vi_frames,
                       stage->log_name,
                       stage->effects[0].name,
                       (unsigned long long)ctx.counts[stage_idx][0],
                       stage->effects[1].name,
                       (unsigned long long)ctx.counts[stage_idx][1],
                       stage->effects[2].name,
                       (unsigned long long)ctx.counts[stage_idx][2],
                       stage->effects[3].name,
                       (unsigned long long)ctx.counts[stage_idx][3],
                       ctx.perf_line,
                       ctx.kernel_ms,
                       ctx.queue_ms);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / CV_PAGE_FPS);
    }

    cleanup_conv_module(ctx.module_ok);
    cleanup_conv_live_vi_capture(vi_ok);
    page_surface_close(&surface);
    for (int i = 0; i < CV_EFFECT_COUNT; ++i) free(ctx.outputs[i]);
    free(ctx.input);
    return 0;
}

int page_conv_cl_live_run(volatile sig_atomic_t *running) {
    return run_conv_cl_showcase_page(running);
}

int page_conv_cl_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    conv_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.input = malloc(CV_FRAME_SIZE);
    ctx.output = malloc(CV_FRAME_SIZE);
    if (!ctx.input || !ctx.output) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }
    memset(ctx.input, 0, CV_FRAME_SIZE);
    memset(ctx.output, 0, CV_FRAME_SIZE);

    if (page_surface_open(&surface, CV_PAGE_POOL, CV_PAGE_W, CV_PAGE_H,
                          CV_PAGE_STRIDE, CV_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }

    ctx.module_ok = setup_conv_module() == 0;
    if (ctx.module_ok) set_tile_status("CONV_CL", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("CONV_CL standalone page module=%s kernel=3x3 sharpen. Ctrl+C to stop.\n",
           ctx.module_ok ? "live" : "fallback");

    int frame = 0;
    while (!running || *running) {
        fill_rgba_input(ctx.input, frame);
        if (!ctx.module_ok || process_conv(&ctx) != 0) {
            memcpy(ctx.output, ctx.input, CV_FRAME_SIZE);
        }
        if (page_surface_send_frame(&surface, draw_conv_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % CV_PAGE_FPS) == 0) {
                printf("CONV_CL frames=%d processed=%d mode=%s kernel=3x3 gpu=%.3f/%.3f standalone=1\n",
                       frame, ctx.processed, ctx.module_ok ? "module" : "fallback",
                       ctx.perf.gpu_kernel_total_ms, ctx.perf.gpu_queue_total_ms);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / CV_PAGE_FPS);
    }

    cleanup_conv_module(ctx.module_ok);
    page_surface_close(&surface);
    free(ctx.input);
    free(ctx.output);
    return 0;
}
