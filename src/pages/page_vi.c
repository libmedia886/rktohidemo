#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_overlay.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define VI_SCREEN_W 1080
#define VI_SCREEN_H 1920
#define VI_CAMERA_POOL 2
#define VI_RESIZE_POOL 9
#define VI_OSD_POOL 8
#define VI_RESIZE_GRP 61
#define VI_OSD_GRP 81
#define VI_SRC_W 3840
#define VI_SRC_H 2160
#define VI_SRC_STRIDE 3840
#define VI_VIEW_W 1072
#define VI_VIEW_H 608
#define VI_VIEW_STRIDE 1088
#define VI_VIEW_X 4
#define VI_VIEW_Y 320
#define VI_FPS 30
#define VI_CAMERA_DEVICE "/dev/video-camera0"
#define VI_SRC_SIZE (VI_SRC_STRIDE * VI_SRC_H * 3 / 2)
#define VI_VIEW_SIZE (VI_VIEW_STRIDE * VI_VIEW_H * 3 / 2)
#define VI_TEXT_MASK_W 1024
#define VI_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
} vi_chain_t;

static void drain_vi_osd_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_OSD_GetFrame(VI_OSD_GRP, &out, 0) != 0) break;
            MEDIA_OSD_ReleaseFrame(VI_OSD_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static int update_vi_overlay(uint64_t vi_count, uint64_t resize_count,
                             uint64_t osd_count, uint64_t vo_count) {
    static uint8_t masks[5][VI_TEXT_MASK_W * VI_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char size_line[160];
    char count_line[192];

    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(size_line, sizeof(size_line),
             "CAM %dx%d STRIDE %d  VIEW %dx%d STRIDE %d",
             VI_SRC_W, VI_SRC_H, VI_SRC_STRIDE,
             VI_VIEW_W, VI_VIEW_H, VI_VIEW_STRIDE);
    snprintf(count_line, sizeof(count_line),
             "VI %llu RGA %llu OSD %llu VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(VI_OSD_GRP, 0, 24, 16, 2,
                              "VI LIVE 4K CAMERA",
                              masks[0], sizeof(masks[0]), VI_TEXT_MASK_W, VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(VI_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), VI_TEXT_MASK_W, VI_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(VI_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), VI_TEXT_MASK_W, VI_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(VI_OSD_GRP, 3, 24, 554, 2,
                              size_line,
                              masks[3], sizeof(masks[3]), VI_TEXT_MASK_W, VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(VI_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), VI_TEXT_MASK_W, VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_vi_chain(vi_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(VI_CAMERA_POOL, VI_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(VI_RESIZE_POOL, VI_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(VI_OSD_POOL, VI_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = VI_CAMERA_DEVICE;
    vi.width = VI_SRC_W;
    vi.height = VI_SRC_H;
    vi.stride = VI_SRC_STRIDE;
    vi.fps = VI_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = VI_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = VI_SRC_W;
    resize.src_height = VI_SRC_H;
    resize.input_width = VI_SRC_W;
    resize.input_height = VI_SRC_H;
    resize.input_stride = VI_SRC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 6;
    resize.out_width = VI_VIEW_W;
    resize.out_height = VI_VIEW_H;
    resize.out_stride = VI_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = VI_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(VI_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(VI_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(VI_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = VI_VIEW_W;
    osd.input_height = VI_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = VI_OSD_POOL;
    osd.input_stride = VI_VIEW_STRIDE;
    osd.output_stride = VI_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(VI_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(VI_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = VI_SCREEN_W;
    vo.height = VI_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, VI_VIEW_X, VI_VIEW_Y, VI_VIEW_W, VI_VIEW_H,
                           VI_VIEW_STRIDE, 6, MEDIA_VO_PLANE_TYPE_AUTO,
                           MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "RESIZE_RGA", VI_RESIZE_GRP, "input0") != 0) {
        return -1;
    }
    chain->bind_vi_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", VI_RESIZE_GRP, "output0",
                       "OSD", VI_OSD_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", VI_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) {
        return -1;
    }
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_vi_chain(vi_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_Stop(0, 0);
    }
    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", VI_OSD_GRP, "output0",
                         "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", VI_RESIZE_GRP, "output0",
                         "OSD", VI_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_vi_resize_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "RESIZE_RGA", VI_RESIZE_GRP, "input0");
        chain->bind_vi_resize_ok = 0;
    }
    if (chain->resize_ok) {
        MEDIA_RESIZE_RGA_Disable(VI_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(VI_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(VI_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->osd_ok) {
        drain_vi_osd_output();
        MEDIA_OSD_Stop(VI_OSD_GRP);
        drain_vi_osd_output();
        MEDIA_OSD_DestroyGrp(VI_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(VI_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(VI_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(VI_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_vi_run(volatile sig_atomic_t *running) {
    vi_chain_t chain = {0};
    if (setup_vi_chain(&chain) != 0) {
        fprintf(stderr, "VI standalone bind chain setup failed\n");
        cleanup_vi_chain(&chain);
        return 1;
    }

    printf("VI standalone bind page: VI %s %dx%d -> RESIZE_RGA %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           VI_CAMERA_DEVICE, VI_SRC_W, VI_SRC_H, VI_VIEW_W, VI_VIEW_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        sleep(1);
        tick++;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", VI_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", VI_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        (void)update_vi_overlay(vi_count, resize_count, osd_count, vo_count);
        printf("VI frames=%llu resize_frames=%llu osd_frames=%llu vo_frames=%llu tick=%d standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               tick);
    }

    cleanup_vi_chain(&chain);
    return 0;
}
