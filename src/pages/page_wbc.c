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

#define WBC_PAGE_W 1080
#define WBC_PAGE_H 1920
#define WBC_PAGE_STRIDE 1088
#define WBC_PAGE_POOL 1
#define WBC_PAGE_FPS 30
#define WBC_CHN 0
#define WBC_POOL 15
#define WBC_CAPTURE_W 1024
#define WBC_CAPTURE_H 1920
#define WBC_CAPTURE_STRIDE 1024
#define WBC_CAPTURE_SIZE (WBC_CAPTURE_STRIDE * WBC_CAPTURE_H * 3 / 2)
#define WBC_ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define WBC_LIVE_SCREEN_W 1080
#define WBC_LIVE_SCREEN_H 1920
#define WBC_LIVE_CAMERA_POOL 2
#define WBC_LIVE_RESIZE_POOL 9
#define WBC_LIVE_OSD_POOL 8
#define WBC_LIVE_RESIZE_GRP 61
#define WBC_LIVE_OSD_GRP 81
#define WBC_LIVE_SRC_W 3840
#define WBC_LIVE_SRC_H 2160
#define WBC_LIVE_SRC_STRIDE 3840
#define WBC_LIVE_VIEW_W 1080
#define WBC_LIVE_VIEW_H 608
#define WBC_LIVE_VIEW_STRIDE WBC_ALIGN_UP_LOCAL(WBC_LIVE_VIEW_W, 64)
#define WBC_LIVE_VIEW_X 0
#define WBC_LIVE_VIEW_Y 320
#define WBC_LIVE_FPS 30
#define WBC_LIVE_CAMERA_DEVICE "/dev/video-camera0"
#define WBC_LIVE_SRC_SIZE ((size_t)WBC_LIVE_SRC_STRIDE * (size_t)WBC_LIVE_SRC_H * 3u / 2u)
#define WBC_LIVE_VIEW_SIZE ((size_t)WBC_LIVE_VIEW_STRIDE * (size_t)WBC_LIVE_VIEW_H * 3u / 2u)
#define WBC_LIVE_TEXT_MASK_W 1024
#define WBC_LIVE_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *capture;
    int captured;
    int wbc_ok;
} wbc_ctx_t;

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int wbc_ok;
    int vi_attr_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
} wbc_live_chain_t;

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

static void fill_empty_capture(uint8_t *dst, int frame) {
    if (!dst) return;
    uint8_t *uv = dst + (size_t)WBC_CAPTURE_STRIDE * WBC_CAPTURE_H;
    for (int y = 0; y < WBC_CAPTURE_H; ++y) {
        uint8_t *row = dst + (size_t)y * WBC_CAPTURE_STRIDE;
        for (int x = 0; x < WBC_CAPTURE_W; ++x) {
            row[x] = (uint8_t)(18 + ((x / 24 + y / 24 + frame) & 31));
        }
    }
    memset(uv, 128, WBC_CAPTURE_STRIDE * WBC_CAPTURE_H / 2);
}

static int setup_wbc_module(void) {
    int connector_id = 0;
    int crtc_id = 0;
    MEDIA_VO_WBC_ATTR attr = {0};
    if (MEDIA_VO_WBC_Probe(NULL, &connector_id, &crtc_id) != 0) return -1;
    (void)connector_id;
    (void)crtc_id;
    if (MEDIA_POOL_Create(WBC_POOL, WBC_CAPTURE_SIZE, 4) != 0) return -1;
    attr.device = NULL;
    attr.target = NULL;
    attr.width = WBC_CAPTURE_W;
    attr.height = WBC_CAPTURE_H;
    attr.stride = WBC_CAPTURE_STRIDE;
    attr.fps = WBC_PAGE_FPS;
    attr.format = MEDIA_FORMAT_NV12;
    attr.pool_id = WBC_POOL;
    attr.output_depth = 4;
    if (MEDIA_VO_WBC_CreateChn(WBC_CHN, &attr) != 0 ||
        MEDIA_VO_WBC_Start(WBC_CHN) != 0) {
        MEDIA_VO_WBC_Stop(WBC_CHN);
        MEDIA_VO_WBC_DestroyChn(WBC_CHN);
        MEDIA_POOL_Destroy(WBC_POOL);
        return -1;
    }
    return 0;
}

static void drain_wbc_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VO_WBC_GetFrame(WBC_CHN, &out, 0) != 0) break;
            MEDIA_VO_WBC_ReleaseFrame(WBC_CHN, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void cleanup_wbc_module(int enabled) {
    if (!enabled) return;
    drain_wbc_output();
    MEDIA_VO_WBC_Stop(WBC_CHN);
    drain_wbc_output();
    MEDIA_VO_WBC_DestroyChn(WBC_CHN);
    MEDIA_POOL_Destroy(WBC_POOL);
}

static int capture_wbc(wbc_ctx_t *ctx) {
    MEDIA_BUFFER out = {-1, -1};
    if (!ctx || !ctx->capture || !ctx->wbc_ok) return -1;
    if (MEDIA_VO_WBC_GetFrame(WBC_CHN, &out, 0) != 0) return -1;
    if (copy_from_buffer(out, ctx->capture, WBC_CAPTURE_SIZE) == 0) {
        ctx->captured++;
    }
    MEDIA_VO_WBC_ReleaseFrame(WBC_CHN, out);
    return 0;
}

static void drain_wbc_live_resize_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_RESIZE_RGA_GetFrame(WBC_LIVE_RESIZE_GRP, &out, 0) != 0) break;
        MEDIA_RESIZE_RGA_ReleaseFrame(WBC_LIVE_RESIZE_GRP, out);
    }
}

static void drain_wbc_live_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(WBC_LIVE_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(WBC_LIVE_OSD_GRP, out);
    }
}

static int update_wbc_live_overlay(uint64_t vi_count,
                                   uint64_t resize_count,
                                   uint64_t osd_count,
                                   uint64_t vo_count,
                                   uint64_t wbc_count,
                                   int captured) {
    static uint8_t masks[5][WBC_LIVE_TEXT_MASK_W * WBC_LIVE_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char count_line[180];
    char wbc_line[128];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(count_line, sizeof(count_line),
             "VI %llu  RGA %llu  OSD %llu  VO %llu  WBC %llu",
             (unsigned long long)vi_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count,
             (unsigned long long)wbc_count);
    snprintf(wbc_line, sizeof(wbc_line), "VO_WBC CAPTURE %d  FORMAT NV12  %dx%d",
             captured, WBC_CAPTURE_W, WBC_CAPTURE_H);

    if (page_overlay_set_text(WBC_LIVE_OSD_GRP, 0, 24, 16, 2,
                              "WBC LIVE VI + WRITEBACK",
                              masks[0], sizeof(masks[0]), WBC_LIVE_TEXT_MASK_W, WBC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(WBC_LIVE_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->RESIZE_RGA->OSD->VO  VO_WBC CAPTURES DISPLAY",
                              masks[1], sizeof(masks[1]), WBC_LIVE_TEXT_MASK_W, WBC_LIVE_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(WBC_LIVE_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), WBC_LIVE_TEXT_MASK_W, WBC_LIVE_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(WBC_LIVE_OSD_GRP, 3, 24, 554, 2,
                              wbc_line,
                              masks[3], sizeof(masks[3]), WBC_LIVE_TEXT_MASK_W, WBC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(WBC_LIVE_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), WBC_LIVE_TEXT_MASK_W, WBC_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_wbc_live_chain(wbc_live_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(WBC_LIVE_CAMERA_POOL, WBC_LIVE_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(WBC_LIVE_RESIZE_POOL, WBC_LIVE_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(WBC_LIVE_OSD_POOL, WBC_LIVE_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = WBC_LIVE_CAMERA_DEVICE;
    vi.width = WBC_LIVE_SRC_W;
    vi.height = WBC_LIVE_SRC_H;
    vi.stride = WBC_LIVE_SRC_STRIDE;
    vi.fps = WBC_LIVE_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = WBC_LIVE_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = WBC_LIVE_SRC_W;
    resize.src_height = WBC_LIVE_SRC_H;
    resize.input_width = WBC_LIVE_SRC_W;
    resize.input_height = WBC_LIVE_SRC_H;
    resize.input_stride = WBC_LIVE_SRC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 4;
    resize.out_width = WBC_LIVE_VIEW_W;
    resize.out_height = WBC_LIVE_VIEW_H;
    resize.out_stride = WBC_LIVE_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = WBC_LIVE_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(WBC_LIVE_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(WBC_LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(WBC_LIVE_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = WBC_LIVE_VIEW_W;
    osd.input_height = WBC_LIVE_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = WBC_LIVE_OSD_POOL;
    osd.input_stride = WBC_LIVE_VIEW_STRIDE;
    osd.output_stride = WBC_LIVE_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(WBC_LIVE_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(WBC_LIVE_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = WBC_LIVE_SCREEN_W;
    vo.height = WBC_LIVE_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, WBC_LIVE_VIEW_X, WBC_LIVE_VIEW_Y,
                           WBC_LIVE_VIEW_W, WBC_LIVE_VIEW_H,
                           WBC_LIVE_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "RESIZE_RGA", WBC_LIVE_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_vi_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", WBC_LIVE_RESIZE_GRP, "output0",
                       "OSD", WBC_LIVE_OSD_GRP, "input") != 0) return -1;
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", WBC_LIVE_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0) return -1;
    if (setup_wbc_module() != 0) return -1;
    chain->wbc_ok = 1;
    if (MEDIA_VI_Enable(0) != 0) return -1;
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    set_tile_status("WBC", TILE_LIVE);
    return 0;
}

static void cleanup_wbc_live_chain(wbc_live_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->wbc_ok) {
        cleanup_wbc_module(1);
        chain->wbc_ok = 0;
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", WBC_LIVE_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", WBC_LIVE_RESIZE_GRP, "output0",
                         "OSD", WBC_LIVE_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_vi_resize_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "RESIZE_RGA", WBC_LIVE_RESIZE_GRP, "input0");
        chain->bind_vi_resize_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_ok) {
        drain_wbc_live_osd_output();
        MEDIA_OSD_Stop(WBC_LIVE_OSD_GRP);
        drain_wbc_live_osd_output();
        MEDIA_OSD_DestroyGrp(WBC_LIVE_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_wbc_live_resize_output();
        MEDIA_RESIZE_RGA_Disable(WBC_LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(WBC_LIVE_RESIZE_GRP);
        drain_wbc_live_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(WBC_LIVE_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(WBC_LIVE_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(WBC_LIVE_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(WBC_LIVE_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

static void draw_capture_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                                int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * WBC_CAPTURE_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * WBC_CAPTURE_STRIDE;
        for (int x = 0; x < dw; ++x) drow[x] = srow[x * WBC_CAPTURE_W / dw];
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)WBC_CAPTURE_STRIDE * WBC_CAPTURE_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (WBC_CAPTURE_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * WBC_CAPTURE_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = (x * WBC_CAPTURE_W / dw) & ~1;
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_scene(uint8_t *dst, int stride, int width, int height,
                       int frame, void *opaque) {
    wbc_ctx_t *ctx = (wbc_ctx_t *)opaque;
    char captured[64];
    snprintf(captured, sizeof(captured), "CAPTURED %06d", ctx ? ctx->captured : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 8, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "WBC", 7, 235, 108, 176);

    int bar_x = 70 + ((frame * 9) % 600);
    page_surface_fill_rect_nv12(dst, stride, width, height, 70, 260, 760, 260, 20, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, bar_x, 318, 180, 80, 210, 80, 170);
    page_surface_fill_rect_nv12(dst, stride, width, height, 110, 430, 520, 34, 170, 100, 90);
    page_surface_draw_text(dst, stride, width, height, 96, 284,
                           "LIVE VO SCENE", 3, 220, 108, 176);

    int cap_w = 512;
    int cap_h = 960;
    int cap_x = (width - cap_w) / 2;
    int cap_y = 650;
    page_surface_fill_rect_nv12(dst, stride, width, height, cap_x - 14, cap_y - 48,
                                cap_w + 28, cap_h + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, cap_x, cap_y - 34,
                           "VO WBC CAPTURE", 3, 220, 108, 176);
    if (ctx && ctx->capture) {
        draw_capture_scaled(dst, stride, width, height, cap_x, cap_y, cap_w, cap_h, ctx->capture);
    }

    page_surface_draw_text(dst, stride, width, height, 70, 1700,
                           captured, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1800,
                           ctx && ctx->wbc_ok ? "MODULE LIVE" : "MODULE FALLBACK",
                           3, 210, 108, 176);
}

int page_wbc_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    wbc_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.capture = malloc(WBC_CAPTURE_SIZE);
    if (!ctx.capture) return 1;
    fill_empty_capture(ctx.capture, 0);

    if (page_surface_open(&surface, WBC_PAGE_POOL, WBC_PAGE_W, WBC_PAGE_H,
                          WBC_PAGE_STRIDE, WBC_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.capture);
        return 1;
    }

    ctx.wbc_ok = setup_wbc_module() == 0;
    if (ctx.wbc_ok) set_tile_status("WBC", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("WBC standalone page module=%s capture=%dx%d. Ctrl+C to stop.\n",
           ctx.wbc_ok ? "live" : "fallback", WBC_CAPTURE_W, WBC_CAPTURE_H);

    int frame = 0;
    while (!running || *running) {
        if (!ctx.wbc_ok || capture_wbc(&ctx) != 0) {
            if (!ctx.wbc_ok) fill_empty_capture(ctx.capture, frame);
        }
        if (page_surface_send_frame(&surface, draw_scene, &ctx, frame) == 0) {
            frame++;
            if ((frame % WBC_PAGE_FPS) == 0) {
                printf("WBC frames=%d captured=%d standalone=1\n", frame, ctx.captured);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / WBC_PAGE_FPS);
    }

    cleanup_wbc_module(ctx.wbc_ok);
    page_surface_close(&surface);
    free(ctx.capture);
    return 0;
}

int page_wbc_live_run(volatile sig_atomic_t *running) {
    wbc_live_chain_t chain = {0};
    int captured = 0;
    if (setup_wbc_live_chain(&chain) != 0) {
        fprintf(stderr, "WBC standalone VI chain setup failed\n");
        cleanup_wbc_live_chain(&chain);
        return 1;
    }

    printf("WBC standalone: VI %s %dx%d -> RESIZE_RGA %dx%d -> OSD -> VO, VO_WBC capture %dx%d. Ctrl+C to stop.\n",
           WBC_LIVE_CAMERA_DEVICE, WBC_LIVE_SRC_W, WBC_LIVE_SRC_H,
           WBC_LIVE_VIEW_W, WBC_LIVE_VIEW_H, WBC_CAPTURE_W, WBC_CAPTURE_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        uint64_t wbc_count = 0;
        sleep(1);
        tick++;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VO_WBC_GetFrame(WBC_CHN, &out, 0) != 0) break;
            MEDIA_VO_WBC_ReleaseFrame(WBC_CHN, out);
            captured++;
        }
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", WBC_LIVE_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", WBC_LIVE_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO_WBC", WBC_CHN, &wbc_count);
        int overlay_ok = update_wbc_live_overlay(vi_count, resize_count, osd_count,
                                                 vo_count, wbc_count, captured) == 0;
        printf("WBC vi=%llu resize=%llu osd=%llu vo=%llu wbc=%llu captured=%d tick=%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               (unsigned long long)wbc_count,
               captured,
               tick,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_wbc_live_chain(&chain);
    return 0;
}
