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
#define EIS_VI_SCREEN_W 1080
#define EIS_VI_SCREEN_H 1920
#define EIS_VI_W 3840
#define EIS_VI_H 2160
#define EIS_VI_STRIDE 3840
#define EIS_VI_OUT_W 1072
#define EIS_VI_OUT_H 608
#define EIS_VI_OUT_STRIDE ALIGN_UP_LOCAL(EIS_VI_OUT_W, 64)
#define EIS_VI_OUT_X ((EIS_VI_SCREEN_W - EIS_VI_OUT_W) / 2)
#define EIS_VI_OUT_Y 320
#define EIS_VI_FPS 30
#define EIS_VI_EIS_GRP 215
#define EIS_VI_VPSS_GRP 216
#define EIS_VI_INPUT_POOL 0
#define EIS_VI_EIS_POOL 1
#define EIS_VI_VPSS_POOL 2
#define EIS_VI_OSD_POOL 8
#define EIS_VI_OSD_GRP 81
#define EIS_VI_CAMERA_DEVICE "/dev/video-camera0"
#define EIS_VI_INPUT_SIZE ((size_t)EIS_VI_STRIDE * (size_t)ALIGN_UP_LOCAL(EIS_VI_H, 16) * 3u / 2u)
#define EIS_VI_EIS_SIZE ((size_t)EIS_VI_STRIDE * (size_t)EIS_VI_H * 3u / 2u)
#define EIS_VI_OUT_SIZE ((size_t)EIS_VI_OUT_STRIDE * (size_t)EIS_VI_OUT_H * 3u / 2u)
#define EIS_VI_TEXT_MASK_W 1024
#define EIS_VI_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int sys_ok;
    int input_pool_ok;
    int eis_pool_ok;
    int vpss_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int eis_ok;
    int vpss_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_eis_ok;
    int bind_eis_vpss_ok;
    int bind_vpss_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
} eis_vi_chain_t;

static void drain_eis_vi_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_EIS_GetFrame(EIS_VI_EIS_GRP, &out, 0) != 0) break;
            MEDIA_EIS_ReleaseFrame(EIS_VI_EIS_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_eis_vi_vpss_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VPSS_Chn_GetFrame(EIS_VI_VPSS_GRP, 0, &out, 0) != 0) break;
            MEDIA_VPSS_Chn_ReleaseFrame(EIS_VI_VPSS_GRP, 0, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_eis_vi_osd_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_OSD_GetFrame(EIS_VI_OSD_GRP, &out, 0) != 0) break;
            MEDIA_OSD_ReleaseFrame(EIS_VI_OSD_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static int update_eis_vi_overlay(uint64_t vi_count, uint64_t eis_count,
                                 uint64_t vpss_count, uint64_t osd_count,
                                 uint64_t vo_count,
                                 const MEDIA_EIS_STATS *stats) {
    static uint8_t masks[5][EIS_VI_TEXT_MASK_W * EIS_VI_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char mode_line[192];
    char count_line[192];

    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(mode_line, sizeof(mode_line),
             "EIS CROP %.2f WINDOW %d TOTAL %.3f EST %.3f WARP %.3f FB %d",
             0.08f, 15,
             stats ? stats->total_ms : 0.0,
             stats ? stats->estimate_ms : 0.0,
             stats ? stats->warp_ms : 0.0,
             stats ? stats->fallback_used : 0);
    snprintf(count_line, sizeof(count_line),
             "VI %llu EIS %llu VPSS %llu OSD %llu VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)eis_count,
             (unsigned long long)vpss_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(EIS_VI_OSD_GRP, 0, 24, 16, 2,
                              "EIS_VI LIVE CAMERA STABILIZE",
                              masks[0], sizeof(masks[0]), EIS_VI_TEXT_MASK_W, EIS_VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_VI_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->EIS->VPSS->OSD->VO",
                              masks[1], sizeof(masks[1]), EIS_VI_TEXT_MASK_W, EIS_VI_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_VI_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), EIS_VI_TEXT_MASK_W, EIS_VI_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_VI_OSD_GRP, 3, 24, 554, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), EIS_VI_TEXT_MASK_W, EIS_VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_VI_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), EIS_VI_TEXT_MASK_W, EIS_VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_eis_vi_chain(eis_vi_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_EIS_ATTR eis = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(EIS_VI_INPUT_POOL, EIS_VI_INPUT_SIZE, 6) != 0) return -1;
    chain->input_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_VI_EIS_POOL, EIS_VI_EIS_SIZE, 6) != 0) return -1;
    chain->eis_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_VI_VPSS_POOL, EIS_VI_OUT_SIZE, 6) != 0) return -1;
    chain->vpss_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_VI_OSD_POOL, EIS_VI_OUT_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = EIS_VI_CAMERA_DEVICE;
    vi.width = EIS_VI_W;
    vi.height = EIS_VI_H;
    vi.stride = EIS_VI_STRIDE;
    vi.fps = EIS_VI_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = EIS_VI_INPUT_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    eis.width = EIS_VI_W;
    eis.height = EIS_VI_H;
    eis.format = MEDIA_FORMAT_NV12;
    eis.input_depth = 6;
    eis.output_pool_id = EIS_VI_EIS_POOL;
    eis.input_stride = EIS_VI_STRIDE;
    eis.output_stride = EIS_VI_STRIDE;
    eis.crop_ratio = 0.08f;
    eis.smoothing_window = 15;
    eis.estimate_width = 320;
    eis.search_radius = 16;
    eis.block_step = 4;
    if (MEDIA_EIS_CreateGrp(EIS_VI_EIS_GRP, &eis) != 0) return -1;
    chain->eis_ok = 1;

    vpss.width = EIS_VI_W;
    vpss.height = EIS_VI_H;
    vpss.input_stride = EIS_VI_STRIDE;
    vpss.input_depth = 6;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = -1;
    vpss.out_fps = -1;
    vpss.output_count = 1;
    vpss.outputs[0].output_id = 0;
    vpss.outputs[0].out_width = EIS_VI_OUT_W;
    vpss.outputs[0].out_height = EIS_VI_OUT_H;
    vpss.outputs[0].out_stride = EIS_VI_OUT_STRIDE;
    vpss.outputs[0].pool_id = EIS_VI_VPSS_POOL;
    vpss.outputs[0].crop_x = 0;
    vpss.outputs[0].crop_y = 0;
    vpss.outputs[0].crop_w = EIS_VI_W;
    vpss.outputs[0].crop_h = EIS_VI_H;
    vpss.outputs[0].in_fps = -1;
    vpss.outputs[0].out_fps = -1;
    vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(EIS_VI_VPSS_GRP, &vpss) != 0) return -1;
    chain->vpss_ok = 1;

    osd.input_width = EIS_VI_OUT_W;
    osd.input_height = EIS_VI_OUT_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = EIS_VI_OSD_POOL;
    osd.input_stride = EIS_VI_OUT_STRIDE;
    osd.output_stride = EIS_VI_OUT_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(EIS_VI_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(EIS_VI_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = EIS_VI_SCREEN_W;
    vo.height = EIS_VI_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, EIS_VI_OUT_X, EIS_VI_OUT_Y,
                           EIS_VI_OUT_W, EIS_VI_OUT_H, EIS_VI_OUT_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "EIS", EIS_VI_EIS_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_vi_eis_ok = 1;
    if (MEDIA_SYS_Bind("EIS", EIS_VI_EIS_GRP, "output",
                       "VPSS", EIS_VI_VPSS_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_eis_vpss_ok = 1;
    if (MEDIA_SYS_Bind("VPSS", EIS_VI_VPSS_GRP, "output0",
                       "OSD", EIS_VI_OSD_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_vpss_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", EIS_VI_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) {
        return -1;
    }
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VPSS_Enable(EIS_VI_VPSS_GRP) != 0 ||
        MEDIA_EIS_Start(EIS_VI_EIS_GRP) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("EIS", TILE_LIVE);
    set_tile_status("EIS_VI", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_eis_vi_chain(eis_vi_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vpss_ok) MEDIA_VPSS_Disable(EIS_VI_VPSS_GRP);
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", EIS_VI_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_vpss_osd_ok) {
        MEDIA_SYS_UnBind("VPSS", EIS_VI_VPSS_GRP, "output0", "OSD", EIS_VI_OSD_GRP, "input");
        chain->bind_vpss_osd_ok = 0;
    }
    if (chain->bind_eis_vpss_ok) {
        MEDIA_SYS_UnBind("EIS", EIS_VI_EIS_GRP, "output", "VPSS", EIS_VI_VPSS_GRP, "input");
        chain->bind_eis_vpss_ok = 0;
    }
    if (chain->eis_ok) {
        drain_eis_vi_output();
        MEDIA_EIS_Stop(EIS_VI_EIS_GRP);
        drain_eis_vi_output();
    }
    if (chain->bind_vi_eis_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output", "EIS", EIS_VI_EIS_GRP, "input");
        chain->bind_vi_eis_ok = 0;
    }
    if (chain->vpss_ok) drain_eis_vi_vpss_output();
    if (chain->osd_ok) {
        drain_eis_vi_osd_output();
        MEDIA_OSD_Stop(EIS_VI_OSD_GRP);
        drain_eis_vi_osd_output();
        MEDIA_OSD_DestroyGrp(EIS_VI_OSD_GRP);
        chain->osd_ok = 0;
    }

    if (chain->vpss_ok) {
        MEDIA_VPSS_DestroyGrp(EIS_VI_VPSS_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->eis_ok) {
        MEDIA_EIS_DestroyGrp(EIS_VI_EIS_GRP);
        chain->eis_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(EIS_VI_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->vpss_pool_ok) {
        MEDIA_POOL_Destroy(EIS_VI_VPSS_POOL);
        chain->vpss_pool_ok = 0;
    }
    if (chain->eis_pool_ok) {
        MEDIA_POOL_Destroy(EIS_VI_EIS_POOL);
        chain->eis_pool_ok = 0;
    }
    if (chain->input_pool_ok) {
        MEDIA_POOL_Destroy(EIS_VI_INPUT_POOL);
        chain->input_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_eis_vi_run(volatile sig_atomic_t *running) {
    eis_vi_chain_t chain = {0};
    if (setup_eis_vi_chain(&chain) != 0) {
        fprintf(stderr, "EIS_VI standalone chain setup failed\n");
        cleanup_eis_vi_chain(&chain);
        return 1;
    }

    printf("EIS_VI standalone: VI %s %dx%d -> EIS -> VPSS %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           EIS_VI_CAMERA_DEVICE, EIS_VI_W, EIS_VI_H, EIS_VI_OUT_W, EIS_VI_OUT_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t eis_count = 0;
        uint64_t vpss_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        MEDIA_EIS_STATS stats;
        memset(&stats, 0, sizeof(stats));
        sleep(1);
        tick++;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("EIS", EIS_VI_EIS_GRP, &eis_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VPSS", EIS_VI_VPSS_GRP, &vpss_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", EIS_VI_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        (void)MEDIA_EIS_GetStats(EIS_VI_EIS_GRP, &stats);
        (void)update_eis_vi_overlay(vi_count, eis_count, vpss_count, osd_count, vo_count, &stats);
        printf("EIS_VI vi=%llu eis=%llu vpss=%llu osd=%llu vo=%llu frame=%d total=%.3fms estimate=%.3fms warp=%.3fms fallback=%d comp=(%.2f,%.2f,%.4f) tick=%d standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)eis_count,
               (unsigned long long)vpss_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               stats.frame_index,
               stats.total_ms,
               stats.estimate_ms,
               stats.warp_ms,
               stats.fallback_used,
               stats.comp_dx,
               stats.comp_dy,
               stats.comp_angle,
               tick);
    }

    cleanup_eis_vi_chain(&chain);
    return 0;
}
