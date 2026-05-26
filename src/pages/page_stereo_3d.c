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

#define ST_PAGE_W 1080
#define ST_PAGE_H 1920
#define ST_PAGE_STRIDE 1088
#define ST_PAGE_POOL 1
#define ST_PAGE_FPS 30
#define ST_GRP 62
#define ST_INPUT0_POOL 10
#define ST_INPUT1_POOL 11
#define ST_OUTPUT_POOL 12
#define ST_W 640
#define ST_H 640
#define ST_STRIDE 640
#define ST_FRAME_SIZE (ST_STRIDE * ST_H * 3 / 2)
#define ST_SAMPLE_SECONDS 3
#define ST_ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define ST_LIVE_SCREEN_W 1080
#define ST_LIVE_SCREEN_H 1920
#define ST_LIVE_CAMERA_POOL 2
#define ST_LIVE_VPSS_POOL 14
#define ST_LIVE_STEREO_POOL 12
#define ST_LIVE_RESIZE_POOL 9
#define ST_LIVE_OSD_POOL 8
#define ST_LIVE_VPSS_GRP 63
#define ST_LIVE_STEREO_GRP 62
#define ST_LIVE_RESIZE_GRP 61
#define ST_LIVE_OSD_GRP 81
#define ST_LIVE_SRC_W 3840
#define ST_LIVE_SRC_H 2160
#define ST_LIVE_SRC_STRIDE 3840
#define ST_LIVE_PROC_W 640
#define ST_LIVE_PROC_H 640
#define ST_LIVE_PROC_STRIDE 640
#define ST_LIVE_VIEW_W 1080
#define ST_LIVE_VIEW_H 608
#define ST_LIVE_VIEW_STRIDE ST_ALIGN_UP_LOCAL(ST_LIVE_VIEW_W, 64)
#define ST_LIVE_VIEW_X 0
#define ST_LIVE_VIEW_Y 320
#define ST_LIVE_INPUTS 2
#define ST_LIVE_FPS 30
#define ST_LIVE_CAMERA_DEVICE "/dev/video-camera0"
#define ST_LIVE_SRC_SIZE ((size_t)ST_LIVE_SRC_STRIDE * (size_t)ST_LIVE_SRC_H * 3u / 2u)
#define ST_LIVE_PROC_SIZE ((size_t)ST_LIVE_PROC_STRIDE * (size_t)ST_LIVE_PROC_H * 3u / 2u)
#define ST_LIVE_VIEW_SIZE ((size_t)ST_LIVE_VIEW_STRIDE * (size_t)ST_LIVE_VIEW_H * 3u / 2u)
#define ST_LIVE_TEXT_MASK_W 1024
#define ST_LIVE_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *left;
    uint8_t *right;
    uint8_t *output;
    int processed;
    int module_ok;
    int sample;
    char mode[32];
} stereo_ctx_t;

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int vpss_pool_ok;
    int stereo_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int vpss_ok;
    int stereo_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_vpss_ok;
    int bind_vpss_stereo_ok[ST_LIVE_INPUTS];
    int bind_stereo_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
} stereo_live_chain_t;

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

static void fill_input_pattern(uint8_t *dst, int sample, int right) {
    if (!dst) return;
    uint8_t *uv = dst + (size_t)ST_STRIDE * ST_H;
    int shift = (sample * 37) % ST_W;
    for (int y = 0; y < ST_H; ++y) {
        uint8_t *row = dst + (size_t)y * ST_STRIDE;
        for (int x = 0; x < ST_W; ++x) {
            int stripe = ((x + shift) / 40 + y / 64 + right) & 1;
            int value = right ? (80 + (x * 120 / ST_W)) : (70 + (y * 130 / ST_H));
            row[x] = clamp_u8(value + (stripe ? 35 : -10));
        }
    }
    for (int y = 0; y < ST_H / 2; ++y) {
        uint8_t *row = uv + (size_t)y * ST_STRIDE;
        for (int x = 0; x < ST_W; x += 2) {
            row[x] = right ? 92 : 160;
            row[x + 1] = right ? 172 : 94;
        }
    }

    fill_rect(dst, ST_STRIDE, ST_W, ST_H, 36, 36, 190, 92,
              right ? 35 : 10, right ? 26 : 60, right ? 72 : 32);
    draw_text(dst, ST_STRIDE, ST_W, ST_H, 58, 64,
              right ? "RIGHT" : "LEFT", 4, 245, 250, 255);
    for (int i = 0; i < 8; ++i) {
        int px = (i * 71 + sample * 17 + (right ? 49 : 0)) % (ST_W - 54);
        int py = (i * 43 + sample * 29 + (right ? 25 : 0)) % (ST_H - 54);
        fill_rect(dst, ST_STRIDE, ST_W, ST_H, px, py, 54, 54,
                  right ? 220 : 80, right ? 120 : 210, right ? 70 : 160);
    }
}

static void cpu_side_by_side(const uint8_t *left, const uint8_t *right, uint8_t *out) {
    if (!left || !right || !out) return;
    memset(out, 16, ST_STRIDE * ST_H);
    memset(out + ST_STRIDE * ST_H, 128, ST_STRIDE * ST_H / 2);
    for (int y = 0; y < ST_H; ++y) {
        uint8_t *dst = out + (size_t)y * ST_STRIDE;
        const uint8_t *lrow = left + (size_t)y * ST_STRIDE;
        const uint8_t *rrow = right + (size_t)y * ST_STRIDE;
        for (int x = 0; x < ST_W / 2; ++x) {
            dst[x] = lrow[x * 2];
            dst[x + ST_W / 2] = rrow[x * 2];
        }
    }
    uint8_t *duv = out + (size_t)ST_STRIDE * ST_H;
    const uint8_t *luv = left + (size_t)ST_STRIDE * ST_H;
    const uint8_t *ruv = right + (size_t)ST_STRIDE * ST_H;
    for (int y = 0; y < ST_H / 2; ++y) {
        uint8_t *dst = duv + (size_t)y * ST_STRIDE;
        const uint8_t *lrow = luv + (size_t)y * ST_STRIDE;
        const uint8_t *rrow = ruv + (size_t)y * ST_STRIDE;
        for (int x = 0; x < ST_W / 2; x += 2) {
            dst[x] = lrow[(x * 2) & ~1];
            dst[x + 1] = lrow[((x * 2) & ~1) + 1];
            dst[x + ST_W / 2] = rrow[(x * 2) & ~1];
            dst[x + ST_W / 2 + 1] = rrow[((x * 2) & ~1) + 1];
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

static int send_copied_frame(const char *port, int pool, const uint8_t *src) {
    MEDIA_BUFFER in = {-1, -1};
    if (MEDIA_POOL_GetBuffer(pool, &in) != 0) return -1;
    if (copy_to_buffer(in, src, ST_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("STEREO_3D", ST_GRP, port, in, 1000) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);
    return 0;
}

static int setup_stereo_module(void) {
    if (MEDIA_POOL_Create(ST_INPUT0_POOL, ST_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(ST_INPUT1_POOL, ST_FRAME_SIZE, 3) != 0) goto fail;
    if (MEDIA_POOL_Create(ST_OUTPUT_POOL, ST_FRAME_SIZE, 3) != 0) goto fail;

    MEDIA_STEREO_3D_ATTR attr = {0};
    attr.width = ST_W;
    attr.height = ST_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_pool_id = ST_OUTPUT_POOL;
    attr.input_stride = ST_STRIDE;
    attr.output_stride = ST_STRIDE;
    attr.mode = MEDIA_STEREO_3D_MODE_SIDE_BY_SIDE;
    attr.rotate_input = 0;
    attr.rotation_degrees = 0.0f;
    if (MEDIA_STEREO_3D_CreateGrp(ST_GRP, &attr) != 0 ||
        MEDIA_STEREO_3D_Enable(ST_GRP) != 0) {
        MEDIA_STEREO_3D_DestroyGrp(ST_GRP);
        goto fail;
    }
    return 0;

fail:
    MEDIA_POOL_Destroy(ST_INPUT0_POOL);
    MEDIA_POOL_Destroy(ST_INPUT1_POOL);
    MEDIA_POOL_Destroy(ST_OUTPUT_POOL);
    return -1;
}

static void cleanup_stereo_module(stereo_ctx_t *ctx) {
    if (!ctx || !ctx->module_ok) return;
    MEDIA_STEREO_3D_Disable(ST_GRP);
    MEDIA_STEREO_3D_DestroyGrp(ST_GRP);
    MEDIA_POOL_Destroy(ST_INPUT0_POOL);
    MEDIA_POOL_Destroy(ST_INPUT1_POOL);
    MEDIA_POOL_Destroy(ST_OUTPUT_POOL);
    ctx->module_ok = 0;
}

static int process_stereo(stereo_ctx_t *ctx) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (!ctx || !ctx->left || !ctx->right || !ctx->output || !ctx->module_ok) return -1;
    if (send_copied_frame("input0", ST_INPUT0_POOL, ctx->left) != 0) return -1;
    if (send_copied_frame("input1", ST_INPUT1_POOL, ctx->right) != 0) return -1;
    if (MEDIA_STEREO_3D_GetFrame(ST_GRP, &out, 1000) == 0) {
        ret = copy_from_buffer(out, ctx->output, ST_FRAME_SIZE);
        MEDIA_STEREO_3D_ReleaseFrame(ST_GRP, out);
    }
    return ret;
}

static void draw_nv12_fit(uint8_t *dst, int stride, int width, int height,
                          int x, int y, int w, int h, const uint8_t *src) {
    if (!dst || !src || w <= 0 || h <= 0) return;
    int out_w = w;
    int out_h = (int)((int64_t)ST_H * out_w / ST_W);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)ST_W * out_h / ST_H);
    }
    if (out_w <= 0 || out_h <= 0) return;
    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;
    const uint8_t *src_uv = src + (size_t)ST_STRIDE * ST_H;

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy0 = dy * ST_H / out_h;
        int sy1 = (dy + 1 < out_h) ? ((dy + 1) * ST_H / out_h) : sy0;
        uint8_t *yrow0 = dst + (size_t)(oy + dy) * stride + ox;
        uint8_t *yrow1 = (dy + 1 < out_h) ?
            dst + (size_t)(oy + dy + 1) * stride + ox : NULL;
        uint8_t *uv = dst + (size_t)stride * height +
            (size_t)((oy + dy) / 2) * stride + (ox & ~1);
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx0 = dx * ST_W / out_w;
            int sx1 = (dx + 1 < out_w) ? ((dx + 1) * ST_W / out_w) : sx0;
            yrow0[dx] = src[(size_t)sy0 * ST_STRIDE + sx0];
            if (dx + 1 < out_w) yrow0[dx + 1] = src[(size_t)sy0 * ST_STRIDE + sx1];
            if (yrow1) {
                yrow1[dx] = src[(size_t)sy1 * ST_STRIDE + sx0];
                if (dx + 1 < out_w) yrow1[dx + 1] = src[(size_t)sy1 * ST_STRIDE + sx1];
            }
            int uv_sx = sx0 & ~1;
            const uint8_t *suv = src_uv + (size_t)(sy0 / 2) * ST_STRIDE + uv_sx;
            int uv_col = ((ox + dx) & ~1) - (ox & ~1);
            uv[uv_col] = suv[0];
            uv[uv_col + 1] = suv[1];
        }
    }
}

static void draw_panel(uint8_t *dst, int stride, int width, int height,
                       int x, int y, int w, int h, const uint8_t *img,
                       const char *label, int accent) {
    int label_h = 28;
    fill_rect(dst, stride, width, height, x, y, w, h, 5, 10, 18);
    stroke_rect(dst, stride, width, height, x, y, w, h, 1,
                accent ? 90 : 70, accent ? 245 : 140, accent ? 180 : 220);
    draw_nv12_fit(dst, stride, width, height, x + 5, y + 5, w - 10, h - label_h - 10, img);
    fill_rect(dst, stride, width, height, x, y + h - label_h, w, label_h, 0, 0, 0);
    draw_text(dst, stride, width, height, x + 9, y + h - label_h + 7,
              label, 1, 180, 230, 255);
}

static void draw_stereo_page(uint8_t *dst, int stride, int width, int height,
                             int frame, void *opaque) {
    stereo_ctx_t *ctx = (stereo_ctx_t *)opaque;
    fill_rect(dst, stride, width, height, 0, 0, width, height, 8, 14, 24);
    int margin = 30;
    int x = margin;
    int y = 34;
    int w = width - margin * 2;
    draw_text(dst, stride, width, height, x, y, "STEREO_3D SIDE BY SIDE", 3,
              235, 248, 255);
    draw_text(dst, stride, width, height, x, y + 42,
              "FLOW: SYNTH LEFT + SYNTH RIGHT -> MEDIA_STEREO_3D", 2,
              165, 210, 235);
    char line[128];
    snprintf(line, sizeof(line), "MODE=%s  SAMPLE=%d  PROCESSED=%d  STANDALONE=1",
             ctx ? ctx->mode : "none", ctx ? ctx->sample : 0, ctx ? ctx->processed : 0);
    draw_text(dst, stride, width, height, x, y + 78, line, 2, 255, 225, 120);

    int gap = 14;
    int top_y = 170;
    int top_h = 720;
    int half_w = (w - gap) / 2;
    draw_panel(dst, stride, width, height, x, top_y, half_w, top_h,
               ctx ? ctx->left : NULL, "INPUT0 LEFT", 0);
    draw_panel(dst, stride, width, height, x + half_w + gap, top_y, half_w, top_h,
               ctx ? ctx->right : NULL, "INPUT1 RIGHT", 0);

    int out_y = top_y + top_h + 22;
    int out_h = height - out_y - 130;
    draw_panel(dst, stride, width, height, x, out_y, w, out_h,
               ctx ? ctx->output : NULL, "STEREO_3D OUTPUT", 1);

    snprintf(line, sizeof(line), "FRAMES=%d  FORMAT=NV12  SIZE=%dx%d", frame, ST_W, ST_H);
    draw_text(dst, stride, width, height, x + 16, height - 82, line, 2,
              190, 225, 245);
}

static void drain_stereo_live_vpss_outputs(void) {
    for (int ch = 0; ch < ST_LIVE_INPUTS; ++ch) {
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VPSS_Chn_GetFrame(ST_LIVE_VPSS_GRP, ch, &out, 0) != 0) break;
            MEDIA_VPSS_Chn_ReleaseFrame(ST_LIVE_VPSS_GRP, ch, out);
        }
    }
}

static void drain_stereo_live_stereo_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_STEREO_3D_GetFrame(ST_LIVE_STEREO_GRP, &out, 0) != 0) break;
        MEDIA_STEREO_3D_ReleaseFrame(ST_LIVE_STEREO_GRP, out);
    }
}

static void drain_stereo_live_resize_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_RESIZE_RGA_GetFrame(ST_LIVE_RESIZE_GRP, &out, 0) != 0) break;
        MEDIA_RESIZE_RGA_ReleaseFrame(ST_LIVE_RESIZE_GRP, out);
    }
}

static void drain_stereo_live_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(ST_LIVE_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(ST_LIVE_OSD_GRP, out);
    }
}

static void fill_stereo_live_vpss_output(MEDIA_VPSS_OUT_ATTR *out, int output_id) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->output_id = output_id;
    out->out_width = ST_LIVE_PROC_W;
    out->out_height = ST_LIVE_PROC_H;
    out->out_stride = ST_LIVE_PROC_STRIDE;
    out->pool_id = ST_LIVE_VPSS_POOL;
    out->crop_y = 0;
    out->crop_w = ST_LIVE_SRC_H;
    out->crop_h = ST_LIVE_SRC_H;
    out->crop_x = output_id == 0 ? 0 : (ST_LIVE_SRC_W - ST_LIVE_SRC_H);
    out->in_fps = -1;
    out->out_fps = -1;
    out->flip_h = output_id == 1;
    out->rotate = output_id == 1 ? 90 : 0;
    out->output_format = MEDIA_FORMAT_NV12;
}

static int update_stereo_live_overlay(uint64_t vi_count,
                                      uint64_t vpss_count,
                                      uint64_t stereo_count,
                                      uint64_t resize_count,
                                      uint64_t osd_count,
                                      uint64_t vo_count) {
    static uint8_t masks[5][ST_LIVE_TEXT_MASK_W * ST_LIVE_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char mode_line[160];
    char count_line[180];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(mode_line, sizeof(mode_line),
             "MODE SIDE_BY_SIDE  INPUT0 LEFT CROP  INPUT1 RIGHT CROP ROT90  PROC %dx%d",
             ST_LIVE_PROC_W, ST_LIVE_PROC_H);
    snprintf(count_line, sizeof(count_line),
             "VI %llu  VPSS %llu  STEREO %llu  RGA %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)vpss_count,
             (unsigned long long)stereo_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(ST_LIVE_OSD_GRP, 0, 24, 16, 2,
                              "STEREO_3D LIVE VI SIDE BY SIDE",
                              masks[0], sizeof(masks[0]), ST_LIVE_TEXT_MASK_W, ST_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(ST_LIVE_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->VPSS(LEFT/ROT90)->STEREO_3D->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), ST_LIVE_TEXT_MASK_W, ST_LIVE_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(ST_LIVE_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), ST_LIVE_TEXT_MASK_W, ST_LIVE_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(ST_LIVE_OSD_GRP, 3, 24, 554, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), ST_LIVE_TEXT_MASK_W, ST_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(ST_LIVE_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), ST_LIVE_TEXT_MASK_W, ST_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_stereo_live_chain(stereo_live_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_STEREO_3D_ATTR stereo = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(ST_LIVE_CAMERA_POOL, ST_LIVE_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(ST_LIVE_VPSS_POOL, ST_LIVE_PROC_SIZE, ST_LIVE_INPUTS * 6) != 0) return -1;
    chain->vpss_pool_ok = 1;
    if (MEDIA_POOL_Create(ST_LIVE_STEREO_POOL, ST_LIVE_PROC_SIZE, 6) != 0) return -1;
    chain->stereo_pool_ok = 1;
    if (MEDIA_POOL_Create(ST_LIVE_RESIZE_POOL, ST_LIVE_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(ST_LIVE_OSD_POOL, ST_LIVE_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = ST_LIVE_CAMERA_DEVICE;
    vi.width = ST_LIVE_SRC_W;
    vi.height = ST_LIVE_SRC_H;
    vi.stride = ST_LIVE_SRC_STRIDE;
    vi.fps = ST_LIVE_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = ST_LIVE_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    vpss.width = ST_LIVE_SRC_W;
    vpss.height = ST_LIVE_SRC_H;
    vpss.input_stride = ST_LIVE_SRC_STRIDE;
    vpss.input_depth = 4;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = -1;
    vpss.out_fps = -1;
    vpss.output_count = ST_LIVE_INPUTS;
    for (int i = 0; i < ST_LIVE_INPUTS; ++i) fill_stereo_live_vpss_output(&vpss.outputs[i], i);
    if (MEDIA_VPSS_SetAttr(ST_LIVE_VPSS_GRP, &vpss) != 0 ||
        MEDIA_VPSS_Enable(ST_LIVE_VPSS_GRP) != 0) {
        return -1;
    }
    chain->vpss_ok = 1;

    stereo.width = ST_LIVE_PROC_W;
    stereo.height = ST_LIVE_PROC_H;
    stereo.format = MEDIA_FORMAT_NV12;
    stereo.input_depth = 4;
    stereo.output_pool_id = ST_LIVE_STEREO_POOL;
    stereo.input_stride = ST_LIVE_PROC_STRIDE;
    stereo.output_stride = ST_LIVE_PROC_STRIDE;
    stereo.mode = MEDIA_STEREO_3D_MODE_SIDE_BY_SIDE;
    stereo.rotate_input = 0;
    stereo.rotation_degrees = 0.0f;
    if (MEDIA_STEREO_3D_CreateGrp(ST_LIVE_STEREO_GRP, &stereo) != 0 ||
        MEDIA_STEREO_3D_Enable(ST_LIVE_STEREO_GRP) != 0) {
        return -1;
    }
    chain->stereo_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = ST_LIVE_PROC_W;
    resize.src_height = ST_LIVE_PROC_H;
    resize.input_width = ST_LIVE_PROC_W;
    resize.input_height = ST_LIVE_PROC_H;
    resize.input_stride = ST_LIVE_PROC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 4;
    resize.out_width = ST_LIVE_VIEW_W;
    resize.out_height = ST_LIVE_VIEW_H;
    resize.out_stride = ST_LIVE_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = ST_LIVE_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(ST_LIVE_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(ST_LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(ST_LIVE_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = ST_LIVE_VIEW_W;
    osd.input_height = ST_LIVE_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = ST_LIVE_OSD_POOL;
    osd.input_stride = ST_LIVE_VIEW_STRIDE;
    osd.output_stride = ST_LIVE_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(ST_LIVE_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(ST_LIVE_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = ST_LIVE_SCREEN_W;
    vo.height = ST_LIVE_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, ST_LIVE_VIEW_X, ST_LIVE_VIEW_Y,
                           ST_LIVE_VIEW_W, ST_LIVE_VIEW_H,
                           ST_LIVE_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "VPSS", ST_LIVE_VPSS_GRP, "input") != 0) return -1;
    chain->bind_vi_vpss_ok = 1;
    for (int i = 0; i < ST_LIVE_INPUTS; ++i) {
        char src_port[16];
        char dst_port[16];
        snprintf(src_port, sizeof(src_port), "output%d", i);
        snprintf(dst_port, sizeof(dst_port), "input%d", i);
        if (MEDIA_SYS_Bind("VPSS", ST_LIVE_VPSS_GRP, src_port,
                           "STEREO_3D", ST_LIVE_STEREO_GRP, dst_port) != 0) {
            return -1;
        }
        chain->bind_vpss_stereo_ok[i] = 1;
    }
    if (MEDIA_SYS_Bind("STEREO_3D", ST_LIVE_STEREO_GRP, "output0",
                       "RESIZE_RGA", ST_LIVE_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_stereo_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", ST_LIVE_RESIZE_GRP, "output0",
                       "OSD", ST_LIVE_OSD_GRP, "input") != 0) return -1;
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", ST_LIVE_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("STEREO_3D", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_stereo_live_chain(stereo_live_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vpss_ok) MEDIA_VPSS_Disable(ST_LIVE_VPSS_GRP);
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", ST_LIVE_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", ST_LIVE_RESIZE_GRP, "output0",
                         "OSD", ST_LIVE_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_stereo_resize_ok) {
        MEDIA_SYS_UnBind("STEREO_3D", ST_LIVE_STEREO_GRP, "output0",
                         "RESIZE_RGA", ST_LIVE_RESIZE_GRP, "input0");
        chain->bind_stereo_resize_ok = 0;
    }
    for (int i = ST_LIVE_INPUTS - 1; i >= 0; --i) {
        if (chain->bind_vpss_stereo_ok[i]) {
            char src_port[16];
            char dst_port[16];
            snprintf(src_port, sizeof(src_port), "output%d", i);
            snprintf(dst_port, sizeof(dst_port), "input%d", i);
            MEDIA_SYS_UnBind("VPSS", ST_LIVE_VPSS_GRP, src_port,
                             "STEREO_3D", ST_LIVE_STEREO_GRP, dst_port);
            chain->bind_vpss_stereo_ok[i] = 0;
        }
    }
    if (chain->bind_vi_vpss_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "VPSS", ST_LIVE_VPSS_GRP, "input");
        chain->bind_vi_vpss_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_ok) {
        drain_stereo_live_osd_output();
        MEDIA_OSD_Stop(ST_LIVE_OSD_GRP);
        drain_stereo_live_osd_output();
        MEDIA_OSD_DestroyGrp(ST_LIVE_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_stereo_live_resize_output();
        MEDIA_RESIZE_RGA_Disable(ST_LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(ST_LIVE_RESIZE_GRP);
        drain_stereo_live_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(ST_LIVE_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->stereo_ok) {
        drain_stereo_live_stereo_output();
        MEDIA_STEREO_3D_Disable(ST_LIVE_STEREO_GRP);
        MEDIA_STEREO_3D_DestroyGrp(ST_LIVE_STEREO_GRP);
        chain->stereo_ok = 0;
    }
    if (chain->vpss_ok) {
        drain_stereo_live_vpss_outputs();
        MEDIA_VPSS_DestroyGrp(ST_LIVE_VPSS_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(ST_LIVE_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(ST_LIVE_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->stereo_pool_ok) {
        MEDIA_POOL_Destroy(ST_LIVE_STEREO_POOL);
        chain->stereo_pool_ok = 0;
    }
    if (chain->vpss_pool_ok) {
        MEDIA_POOL_Destroy(ST_LIVE_VPSS_POOL);
        chain->vpss_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(ST_LIVE_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_stereo_3d_live_run(volatile sig_atomic_t *running) {
    stereo_live_chain_t chain = {0};
    if (setup_stereo_live_chain(&chain) != 0) {
        fprintf(stderr, "STEREO_3D standalone VI chain setup failed\n");
        cleanup_stereo_live_chain(&chain);
        return 1;
    }

    printf("STEREO_3D standalone: VI %s %dx%d -> VPSS two crops -> STEREO_3D side-by-side -> RESIZE_RGA %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           ST_LIVE_CAMERA_DEVICE, ST_LIVE_SRC_W, ST_LIVE_SRC_H,
           ST_LIVE_VIEW_W, ST_LIVE_VIEW_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t vpss_count = 0;
        uint64_t stereo_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        sleep(1);
        tick++;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VPSS", ST_LIVE_VPSS_GRP, &vpss_count);
        (void)MEDIA_SYS_GetModuleFrameCount("STEREO_3D", ST_LIVE_STEREO_GRP, &stereo_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", ST_LIVE_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", ST_LIVE_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_stereo_live_overlay(vi_count, vpss_count, stereo_count,
                                                    resize_count, osd_count, vo_count) == 0;
        printf("STEREO_3D vi=%llu vpss=%llu stereo=%llu resize=%llu osd=%llu vo=%llu inputs=%d tick=%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)vpss_count,
               (unsigned long long)stereo_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               ST_LIVE_INPUTS,
               tick,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_stereo_live_chain(&chain);
    return 0;
}

int page_stereo_3d_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    stereo_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    snprintf(ctx.mode, sizeof(ctx.mode), "not-ready");
    ctx.left = malloc(ST_FRAME_SIZE);
    ctx.right = malloc(ST_FRAME_SIZE);
    ctx.output = malloc(ST_FRAME_SIZE);
    if (!ctx.left || !ctx.right || !ctx.output) {
        free(ctx.left);
        free(ctx.right);
        free(ctx.output);
        return 1;
    }

    if (page_surface_open(&surface, ST_PAGE_POOL, ST_PAGE_W, ST_PAGE_H,
                          ST_PAGE_STRIDE, ST_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.left);
        free(ctx.right);
        free(ctx.output);
        return 1;
    }

    if (setup_stereo_module() == 0) {
        ctx.module_ok = 1;
        snprintf(ctx.mode, sizeof(ctx.mode), "module-sbs");
        set_tile_status("STEREO_3D", TILE_LIVE);
    } else {
        snprintf(ctx.mode, sizeof(ctx.mode), "cpu-sbs");
        set_tile_status("STEREO_3D", TILE_LOOP);
    }
    set_tile_status("VO", TILE_LIVE);

    printf("STEREO_3D standalone page module=%s mode=%s. Ctrl+C to stop.\n",
           ctx.module_ok ? "live" : "fallback", ctx.mode);

    int frame = 0;
    int last_sample = -1;
    while (!running || *running) {
        int sample = (frame / (ST_PAGE_FPS * ST_SAMPLE_SECONDS)) % 4;
        if (sample != last_sample) {
            fill_input_pattern(ctx.left, sample, 0);
            fill_input_pattern(ctx.right, sample, 1);
            if (ctx.module_ok && process_stereo(&ctx) == 0) {
                ctx.processed++;
            } else {
                cpu_side_by_side(ctx.left, ctx.right, ctx.output);
            }
            ctx.sample = sample + 1;
            last_sample = sample;
        }

        if (page_surface_send_frame(&surface, draw_stereo_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % ST_PAGE_FPS) == 0) {
                printf("STEREO_3D frames=%d sample=%d/4 processed=%d mode=%s standalone=1\n",
                       frame, ctx.sample, ctx.processed, ctx.mode);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / ST_PAGE_FPS);
    }

    cleanup_stereo_module(&ctx);
    page_surface_close(&surface);
    free(ctx.left);
    free(ctx.right);
    free(ctx.output);
    return 0;
}
