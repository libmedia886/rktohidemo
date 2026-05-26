#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_overlay.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define CL_SCREEN_W 1080
#define CL_SCREEN_H 1920
#define CL_CAMERA_POOL 2
#define CL_OUTPUT_POOL 15
#define CL_RESIZE_POOL 9
#define CL_OSD_POOL 8
#define CL_GRP 69
#define CL_RESIZE_GRP 61
#define CL_OSD_GRP 81
#define CL_SRC_W 3840
#define CL_SRC_H 2160
#define CL_SRC_STRIDE 3840
#define CL_VIEW_W 1080
#define CL_VIEW_H 608
#define CL_VIEW_STRIDE ALIGN_UP_LOCAL(CL_VIEW_W, 64)
#define CL_VIEW_X 0
#define CL_VIEW_Y 656
#define CL_FPS 30
#define CL_CAMERA_DEVICE "/dev/video-camera0"
#define CL_SRC_SIZE ((size_t)CL_SRC_STRIDE * (size_t)CL_SRC_H * 3u / 2u)
#define CL_VIEW_SIZE ((size_t)CL_VIEW_STRIDE * (size_t)CL_VIEW_H * 3u / 2u)
#define CL_TEXT_MASK_W 1024
#define CL_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int output_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int clahe_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_clahe_ok;
    int bind_clahe_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
    float clip;
    int passthrough;
} clahe_chain_t;

static int clahe_wave_i(int t, int max_value) {
    int period;
    int v;
    if (max_value <= 0) return 0;
    period = max_value * 2;
    v = t % period;
    return v <= max_value ? v : period - v;
}

static void drain_clahe_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_CLAHE_GetFrame(CL_GRP, &out, 0) != 0) break;
            MEDIA_CLAHE_ReleaseFrame(CL_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_resize_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_RESIZE_RGA_GetFrame(CL_RESIZE_GRP, &out, 0) != 0) break;
            MEDIA_RESIZE_RGA_ReleaseFrame(CL_RESIZE_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(CL_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(CL_OSD_GRP, out);
    }
}

static int update_clahe_overlay(const clahe_chain_t *chain,
                                uint64_t vi_count,
                                uint64_t clahe_count,
                                uint64_t resize_count,
                                uint64_t osd_count,
                                uint64_t vo_count) {
    static uint8_t masks[5][CL_TEXT_MASK_W * CL_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char mode_line[128];
    char count_line[160];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(mode_line, sizeof(mode_line), "MODE %s  CLIP %.2f  GRID 8X8  BINS 256",
             chain && chain->passthrough ? "PASS" : "ENHANCE",
             chain ? chain->clip : 0.0f);
    snprintf(count_line, sizeof(count_line), "VI %llu  CLAHE %llu  RGA %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)clahe_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(CL_OSD_GRP, 0, 24, 16, 2,
                              "CLAHE LIVE VI 4K ENHANCE",
                              masks[0], sizeof(masks[0]), CL_TEXT_MASK_W, CL_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CL_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->CLAHE->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), CL_TEXT_MASK_W, CL_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(CL_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), CL_TEXT_MASK_W, CL_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(CL_OSD_GRP, 3, 24, 554, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), CL_TEXT_MASK_W, CL_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CL_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), CL_TEXT_MASK_W, CL_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_clahe_chain(clahe_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_CLAHE_ATTR clahe = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(CL_CAMERA_POOL, CL_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(CL_OUTPUT_POOL, CL_SRC_SIZE, 4) != 0) return -1;
    chain->output_pool_ok = 1;
    if (MEDIA_POOL_Create(CL_RESIZE_POOL, CL_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(CL_OSD_POOL, CL_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = CL_CAMERA_DEVICE;
    vi.width = CL_SRC_W;
    vi.height = CL_SRC_H;
    vi.stride = CL_SRC_STRIDE;
    vi.fps = CL_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = CL_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    chain->clip = 2.5f;
    chain->passthrough = 1;
    clahe.width = CL_SRC_W;
    clahe.height = CL_SRC_H;
    clahe.format = MEDIA_FORMAT_NV12;
    clahe.tile_grid_x = 8;
    clahe.tile_grid_y = 8;
    clahe.bins = 256;
    clahe.input_depth = 4;
    clahe.output_pool_id = CL_OUTPUT_POOL;
    clahe.input_stride = CL_SRC_STRIDE;
    clahe.output_stride = CL_SRC_STRIDE;
    clahe.clip_limit = chain->clip;
    clahe.highlight_protect_start = 0.92f;
    clahe.highlight_protect_strength = 0.4f;
    clahe.passthrough = chain->passthrough;
    if (MEDIA_CLAHE_CreateGrp(CL_GRP, &clahe) != 0 ||
        MEDIA_CLAHE_Start(CL_GRP) != 0 ||
        MEDIA_CLAHE_Enable(CL_GRP) != 0) {
        return -1;
    }
    chain->clahe_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = CL_SRC_W;
    resize.src_height = CL_SRC_H;
    resize.input_width = CL_SRC_W;
    resize.input_height = CL_SRC_H;
    resize.input_stride = CL_SRC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 4;
    resize.out_width = CL_VIEW_W;
    resize.out_height = CL_VIEW_H;
    resize.out_stride = CL_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = CL_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(CL_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(CL_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(CL_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = CL_VIEW_W;
    osd.input_height = CL_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = CL_OSD_POOL;
    osd.input_stride = CL_VIEW_STRIDE;
    osd.output_stride = CL_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(CL_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(CL_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = CL_SCREEN_W;
    vo.height = CL_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, CL_VIEW_X, CL_VIEW_Y,
                           CL_VIEW_W, CL_VIEW_H, CL_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output", "CLAHE", CL_GRP, "input") != 0) return -1;
    chain->bind_vi_clahe_ok = 1;
    if (MEDIA_SYS_Bind("CLAHE", CL_GRP, "output",
                       "RESIZE_RGA", CL_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_clahe_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", CL_RESIZE_GRP, "output0",
                       "OSD", CL_OSD_GRP, "input") != 0) return -1;
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", CL_OSD_GRP, "output0", "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("CLAHE", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_clahe_chain(clahe_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", CL_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", CL_RESIZE_GRP, "output0",
                         "OSD", CL_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_clahe_resize_ok) {
        MEDIA_SYS_UnBind("CLAHE", CL_GRP, "output",
                         "RESIZE_RGA", CL_RESIZE_GRP, "input0");
        chain->bind_clahe_resize_ok = 0;
    }
    if (chain->bind_vi_clahe_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output", "CLAHE", CL_GRP, "input");
        chain->bind_vi_clahe_ok = 0;
    }
    if (chain->osd_ok) {
        drain_osd_output();
        MEDIA_OSD_Stop(CL_OSD_GRP);
        drain_osd_output();
        MEDIA_OSD_DestroyGrp(CL_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_resize_output();
        MEDIA_RESIZE_RGA_Disable(CL_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(CL_RESIZE_GRP);
        drain_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(CL_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->clahe_ok) {
        drain_clahe_output();
        MEDIA_CLAHE_Disable(CL_GRP);
        MEDIA_CLAHE_Stop(CL_GRP);
        drain_clahe_output();
        MEDIA_CLAHE_DestroyGrp(CL_GRP);
        chain->clahe_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(CL_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(CL_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->output_pool_ok) {
        MEDIA_POOL_Destroy(CL_OUTPUT_POOL);
        chain->output_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(CL_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_clahe_run(volatile sig_atomic_t *running) {
    clahe_chain_t chain = {0};
    if (setup_clahe_chain(&chain) != 0) {
        fprintf(stderr, "CLAHE standalone VI chain setup failed\n");
        cleanup_clahe_chain(&chain);
        return 1;
    }

    printf("CLAHE standalone: VI %s %dx%d -> CLAHE -> RESIZE_RGA %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           CL_CAMERA_DEVICE, CL_SRC_W, CL_SRC_H, CL_VIEW_W, CL_VIEW_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t clahe_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        sleep(1);
        tick++;
        chain.clip = 1.4f + (float)clahe_wave_i(tick * 12, 240) / 100.0f;
        chain.passthrough = (tick % 2) == 0;
        (void)MEDIA_CLAHE_SetClipLimit(CL_GRP, chain.clip);
        (void)MEDIA_CLAHE_SetPassthrough(CL_GRP, chain.passthrough);
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("CLAHE", CL_GRP, &clahe_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", CL_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", CL_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_clahe_overlay(&chain, vi_count, clahe_count,
                                              resize_count, osd_count, vo_count) == 0;
        printf("CLAHE vi=%llu clahe=%llu resize=%llu osd=%llu vo=%llu passthrough=%d clip=%.2f tick=%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)clahe_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               chain.passthrough,
               chain.clip,
               tick,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_clahe_chain(&chain);
    return 0;
}
