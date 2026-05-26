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
#define RT_SCREEN_W 1080
#define RT_SCREEN_H 1920
#define RT_CAMERA_POOL 2
#define RT_RESIZE_POOL 9
#define RT_OSD_POOL 8
#define RT_GRP 73
#define RT_RESIZE_GRP 61
#define RT_OSD_GRP 81
#define RT_SRC_W 3840
#define RT_SRC_H 2160
#define RT_SRC_STRIDE 3840
#define RT_VIEW_W 1080
#define RT_VIEW_H 608
#define RT_VIEW_STRIDE ALIGN_UP_LOCAL(RT_VIEW_W, 64)
#define RT_VIEW_X 0
#define RT_VIEW_Y 656
#define RT_FPS 30
#define RT_CAMERA_DEVICE "/dev/video-camera0"
#define RT_GAIN 40.0f
#define RT_SRC_SIZE ((size_t)RT_SRC_STRIDE * (size_t)RT_SRC_H * 3u / 2u)
#define RT_VIEW_SIZE ((size_t)RT_VIEW_STRIDE * (size_t)RT_VIEW_H * 3u / 2u)
#define RT_TEXT_MASK_W 1024
#define RT_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int retinex_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_retinex_ok;
    int bind_retinex_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
    int passthrough;
} retinex_chain_t;

static void drain_retinex_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_RETINEX_GetFrame(RT_GRP, &out, 0) != 0) break;
            MEDIA_RETINEX_ReleaseFrame(RT_GRP, out);
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
            if (MEDIA_RESIZE_RGA_GetFrame(RT_RESIZE_GRP, &out, 0) != 0) break;
            MEDIA_RESIZE_RGA_ReleaseFrame(RT_RESIZE_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(RT_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(RT_OSD_GRP, out);
    }
}

static int update_retinex_overlay(const retinex_chain_t *chain,
                                  uint64_t vi_count,
                                  uint64_t retinex_count,
                                  uint64_t resize_count,
                                  uint64_t osd_count,
                                  uint64_t vo_count) {
    static uint8_t masks[5][RT_TEXT_MASK_W * RT_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char mode_line[128];
    char count_line[160];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(mode_line, sizeof(mode_line), "MODE %s  GAIN %.1f  THRESHOLD 0.5",
             chain && chain->passthrough ? "PASS" : "ENHANCE",
             RT_GAIN);
    snprintf(count_line, sizeof(count_line), "VI %llu  RET %llu  RGA %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)retinex_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(RT_OSD_GRP, 0, 24, 16, 2,
                              "RETINEX LIVE VI 4K ENHANCE",
                              masks[0], sizeof(masks[0]), RT_TEXT_MASK_W, RT_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(RT_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->RETINEX->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), RT_TEXT_MASK_W, RT_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(RT_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), RT_TEXT_MASK_W, RT_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(RT_OSD_GRP, 3, 24, 554, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), RT_TEXT_MASK_W, RT_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(RT_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), RT_TEXT_MASK_W, RT_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_retinex_chain(retinex_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_RETINEX_ATTR retinex = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(RT_CAMERA_POOL, RT_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(RT_RESIZE_POOL, RT_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(RT_OSD_POOL, RT_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = RT_CAMERA_DEVICE;
    vi.width = RT_SRC_W;
    vi.height = RT_SRC_H;
    vi.stride = RT_SRC_STRIDE;
    vi.fps = RT_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = RT_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    chain->passthrough = 1;
    retinex.scale_count = 1;
    retinex.width = RT_SRC_W;
    retinex.height = RT_SRC_H;
    retinex.format = MEDIA_FORMAT_NV12;
    retinex.input_depth = 4;
    retinex.output_depth = 4;
    retinex.input_stride = RT_SRC_STRIDE;
    retinex.output_stride = RT_SRC_STRIDE;
    retinex.gain = RT_GAIN;
    retinex.threshold = 0.5f;
    retinex.log_min = -3.0f;
    retinex.log_max = 8.5f;
    retinex.passthrough = chain->passthrough;
    if (MEDIA_RETINEX_CreateGrp(RT_GRP, &retinex) != 0 ||
        MEDIA_RETINEX_Start(RT_GRP) != 0) {
        return -1;
    }
    chain->retinex_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = RT_SRC_W;
    resize.src_height = RT_SRC_H;
    resize.input_width = RT_SRC_W;
    resize.input_height = RT_SRC_H;
    resize.input_stride = RT_SRC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 4;
    resize.out_width = RT_VIEW_W;
    resize.out_height = RT_VIEW_H;
    resize.out_stride = RT_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = RT_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(RT_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(RT_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(RT_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = RT_VIEW_W;
    osd.input_height = RT_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = RT_OSD_POOL;
    osd.input_stride = RT_VIEW_STRIDE;
    osd.output_stride = RT_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(RT_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(RT_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = RT_SCREEN_W;
    vo.height = RT_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, RT_VIEW_X, RT_VIEW_Y,
                           RT_VIEW_W, RT_VIEW_H, RT_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output", "RETINEX", RT_GRP, "input0") != 0) return -1;
    chain->bind_vi_retinex_ok = 1;
    if (MEDIA_SYS_Bind("RETINEX", RT_GRP, "output0",
                       "RESIZE_RGA", RT_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_retinex_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", RT_RESIZE_GRP, "output0",
                       "OSD", RT_OSD_GRP, "input") != 0) return -1;
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", RT_OSD_GRP, "output0", "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("RETINEX", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_retinex_chain(retinex_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", RT_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", RT_RESIZE_GRP, "output0",
                         "OSD", RT_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_retinex_resize_ok) {
        MEDIA_SYS_UnBind("RETINEX", RT_GRP, "output0",
                         "RESIZE_RGA", RT_RESIZE_GRP, "input0");
        chain->bind_retinex_resize_ok = 0;
    }
    if (chain->bind_vi_retinex_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output", "RETINEX", RT_GRP, "input0");
        chain->bind_vi_retinex_ok = 0;
    }
    if (chain->osd_ok) {
        drain_osd_output();
        MEDIA_OSD_Stop(RT_OSD_GRP);
        drain_osd_output();
        MEDIA_OSD_DestroyGrp(RT_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_resize_output();
        MEDIA_RESIZE_RGA_Disable(RT_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(RT_RESIZE_GRP);
        drain_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(RT_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->retinex_ok) {
        drain_retinex_output();
        MEDIA_RETINEX_Stop(RT_GRP);
        drain_retinex_output();
        MEDIA_RETINEX_DestroyGrp(RT_GRP);
        chain->retinex_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(RT_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(RT_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(RT_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_retinex_live_run(volatile sig_atomic_t *running) {
    retinex_chain_t chain = {0};
    if (setup_retinex_chain(&chain) != 0) {
        fprintf(stderr, "RETINEX standalone VI chain setup failed\n");
        cleanup_retinex_chain(&chain);
        return 1;
    }

    printf("RETINEX standalone: VI %s %dx%d -> RETINEX -> RESIZE_RGA %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           RT_CAMERA_DEVICE, RT_SRC_W, RT_SRC_H, RT_VIEW_W, RT_VIEW_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t retinex_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        sleep(1);
        tick++;
        chain.passthrough = (tick % 2) == 0;
        (void)MEDIA_RETINEX_SetPassthrough(RT_GRP, chain.passthrough);
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RETINEX", RT_GRP, &retinex_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", RT_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", RT_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_retinex_overlay(&chain, vi_count, retinex_count,
                                                resize_count, osd_count, vo_count) == 0;
        printf("RETINEX vi=%llu retinex=%llu resize=%llu osd=%llu vo=%llu passthrough=%d gain=%.1f tick=%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)retinex_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               chain.passthrough,
               RT_GAIN,
               tick,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_retinex_chain(&chain);
    return 0;
}
