#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_overlay.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define HS_VI_SCREEN_W 1080
#define HS_VI_SCREEN_H 1920
#define HS_VI_CAMERA_POOL 2
#define HS_VI_OUTPUT_POOL 15
#define HS_VI_RESIZE_POOL 9
#define HS_VI_OSD_POOL 8
#define HS_VI_GRP 120
#define HS_VI_RESIZE_GRP 61
#define HS_VI_OSD_GRP 81
#define HS_VI_SRC_W 3840
#define HS_VI_SRC_H 2160
#define HS_VI_SRC_STRIDE 3840
#define HS_VI_VIEW_W 1072
#define HS_VI_VIEW_H 608
#define HS_VI_VIEW_STRIDE 1088
#define HS_VI_VIEW_X 4
#define HS_VI_VIEW_Y 656
#define HS_VI_FPS 30
#define HS_VI_CAMERA_DEVICE "/dev/video-camera0"
#define HS_VI_SRC_SIZE (HS_VI_SRC_STRIDE * HS_VI_SRC_H * 3 / 2)
#define HS_VI_VIEW_SIZE (HS_VI_VIEW_STRIDE * HS_VI_VIEW_H * 3 / 2)
#define HS_VI_TEXT_MASK_W 1024
#define HS_VI_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

#define HS_VI_THRESHOLD_LOW 0.58f
#define HS_VI_THRESHOLD_HIGH 0.86f
#define HS_VI_KNEE 0.68f
#define HS_VI_RATIO 0.08f
#define HS_VI_STRENGTH 0.94f
#define HS_VI_CHROMA_LOW 0.05f
#define HS_VI_CHROMA_HIGH 0.52f

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int hs_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int hs_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_hs_ok;
    int bind_hs_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
    const char *vi_src_port;
    const char *hs_in_port;
    const char *hs_src_port;
    const char *resize_in_port;
    const char *resize_src_port;
    const char *osd_in_port;
    const char *osd_src_port;
    const char *vo_in_port;
} hs_vi_chain_t;

static int bind_first_match(const char *src_mod, int src_id,
                            const char **src_ports, int src_count,
                            const char *dst_mod, int dst_id,
                            const char **dst_ports, int dst_count,
                            const char **used_src_port,
                            const char **used_dst_port) {
    for (int i = 0; i < src_count; ++i) {
        for (int j = 0; j < dst_count; ++j) {
            if (MEDIA_SYS_Bind(src_mod, src_id, src_ports[i],
                               dst_mod, dst_id, dst_ports[j]) == 0) {
                if (used_src_port) *used_src_port = src_ports[i];
                if (used_dst_port) *used_dst_port = dst_ports[j];
                return 0;
            }
        }
    }
    return -1;
}

static void drain_highlight_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_HIGHLIGHT_SUPPRESS_GetFrame(HS_VI_GRP, &out, 0) != 0) break;
            MEDIA_HIGHLIGHT_SUPPRESS_ReleaseFrame(HS_VI_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_hs_vi_osd_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_OSD_GetFrame(HS_VI_OSD_GRP, &out, 0) != 0) break;
            MEDIA_OSD_ReleaseFrame(HS_VI_OSD_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static int update_hs_vi_overlay(int passthrough,
                                uint64_t vi_count, uint64_t hs_count,
                                uint64_t resize_count, uint64_t osd_count,
                                uint64_t vo_count,
                                const MEDIA_HIGHLIGHT_SUPPRESS_PERF *hs_perf) {
    static uint8_t masks[5][HS_VI_TEXT_MASK_W * HS_VI_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char mode_line[192];
    char count_line[192];

    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    if (hs_perf && hs_perf->gpu_enabled && hs_perf->gpu_kernel_ms >= 0.0f) {
        snprintf(mode_line, sizeof(mode_line),
                 "MODE %s LOW %.2f HIGH %.2f RATIO %.2f STRENGTH %.2f GPU %.3f/%.3fMS",
                 passthrough ? "BYPASS" : "SUPPRESS",
                 HS_VI_THRESHOLD_LOW, HS_VI_THRESHOLD_HIGH,
                 HS_VI_RATIO, HS_VI_STRENGTH,
                 hs_perf->gpu_kernel_ms, hs_perf->gpu_queue_ms);
    } else if (hs_perf && hs_perf->cpu_ms >= 0.0f) {
        snprintf(mode_line, sizeof(mode_line),
                 "MODE %s LOW %.2f HIGH %.2f RATIO %.2f STRENGTH %.2f CPU %.3fMS",
                 passthrough ? "BYPASS" : "SUPPRESS",
                 HS_VI_THRESHOLD_LOW, HS_VI_THRESHOLD_HIGH,
                 HS_VI_RATIO, HS_VI_STRENGTH,
                 hs_perf->cpu_ms);
    } else {
        snprintf(mode_line, sizeof(mode_line),
                 "MODE %s LOW %.2f HIGH %.2f RATIO %.2f STRENGTH %.2f PROC N/A",
                 passthrough ? "BYPASS" : "SUPPRESS",
                 HS_VI_THRESHOLD_LOW, HS_VI_THRESHOLD_HIGH,
                 HS_VI_RATIO, HS_VI_STRENGTH);
    }
    snprintf(count_line, sizeof(count_line),
             "VI %llu HS %llu RGA %llu OSD %llu VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)hs_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(HS_VI_OSD_GRP, 0, 24, 16, 2,
                              "HIGHLIGHT_SUPPRESS VI LIVE",
                              masks[0], sizeof(masks[0]), HS_VI_TEXT_MASK_W, HS_VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(HS_VI_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->HIGHLIGHT_SUPPRESS->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), HS_VI_TEXT_MASK_W, HS_VI_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(HS_VI_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), HS_VI_TEXT_MASK_W, HS_VI_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(HS_VI_OSD_GRP, 3, 24, 554, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), HS_VI_TEXT_MASK_W, HS_VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(HS_VI_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), HS_VI_TEXT_MASK_W, HS_VI_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_hs_vi_chain(hs_vi_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_HIGHLIGHT_SUPPRESS_ATTR hs = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *hs_in_ports[] = {"input", "input0"};
    const char *out_ports[] = {"output0", "output"};
    const char *resize_in_ports[] = {"input0", "input"};
    const char *osd_in_ports[] = {"input", "input0"};
    const char *osd_out_ports[] = {"output0", "output"};
    const char *vo_in_ports[] = {"input0", "input"};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(HS_VI_CAMERA_POOL, HS_VI_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(HS_VI_OUTPUT_POOL, HS_VI_SRC_SIZE, 4) != 0) return -1;
    chain->hs_pool_ok = 1;
    if (MEDIA_POOL_Create(HS_VI_RESIZE_POOL, HS_VI_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(HS_VI_OSD_POOL, HS_VI_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = HS_VI_CAMERA_DEVICE;
    vi.width = HS_VI_SRC_W;
    vi.height = HS_VI_SRC_H;
    vi.stride = HS_VI_SRC_STRIDE;
    vi.fps = HS_VI_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = HS_VI_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    hs.width = HS_VI_SRC_W;
    hs.height = HS_VI_SRC_H;
    hs.format = MEDIA_FORMAT_NV12;
    hs.input_depth = 4;
    hs.output_pool_id = HS_VI_OUTPUT_POOL;
    hs.input_stride = HS_VI_SRC_STRIDE;
    hs.output_stride = HS_VI_SRC_STRIDE;
    hs.threshold_low = HS_VI_THRESHOLD_LOW;
    hs.threshold_high = HS_VI_THRESHOLD_HIGH;
    hs.knee = HS_VI_KNEE;
    hs.ratio = HS_VI_RATIO;
    hs.strength = HS_VI_STRENGTH;
    hs.chroma_low = HS_VI_CHROMA_LOW;
    hs.chroma_high = HS_VI_CHROMA_HIGH;
    hs.passthrough = 1;
    if (MEDIA_HIGHLIGHT_SUPPRESS_CreateGrp(HS_VI_GRP, &hs) != 0 ||
        MEDIA_HIGHLIGHT_SUPPRESS_Start(HS_VI_GRP) != 0) {
        return -1;
    }
    chain->hs_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = HS_VI_SRC_W;
    resize.src_height = HS_VI_SRC_H;
    resize.input_width = HS_VI_SRC_W;
    resize.input_height = HS_VI_SRC_H;
    resize.input_stride = HS_VI_SRC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 6;
    resize.out_width = HS_VI_VIEW_W;
    resize.out_height = HS_VI_VIEW_H;
    resize.out_stride = HS_VI_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = HS_VI_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(HS_VI_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(HS_VI_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(HS_VI_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = HS_VI_VIEW_W;
    osd.input_height = HS_VI_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = HS_VI_OSD_POOL;
    osd.input_stride = HS_VI_VIEW_STRIDE;
    osd.output_stride = HS_VI_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(HS_VI_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(HS_VI_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = HS_VI_SCREEN_W;
    vo.height = HS_VI_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, HS_VI_VIEW_X, HS_VI_VIEW_Y,
                           HS_VI_VIEW_W, HS_VI_VIEW_H, HS_VI_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (bind_first_match("VI", 0, vi_out_ports, 2,
                         "HIGHLIGHT_SUPPRESS", HS_VI_GRP, hs_in_ports, 2,
                         &chain->vi_src_port, &chain->hs_in_port) != 0) {
        return -1;
    }
    chain->bind_vi_hs_ok = 1;
    if (bind_first_match("HIGHLIGHT_SUPPRESS", HS_VI_GRP, out_ports, 2,
                         "RESIZE_RGA", HS_VI_RESIZE_GRP, resize_in_ports, 2,
                         &chain->hs_src_port, &chain->resize_in_port) != 0) {
        return -1;
    }
    chain->bind_hs_resize_ok = 1;
    if (bind_first_match("RESIZE_RGA", HS_VI_RESIZE_GRP, out_ports, 2,
                         "OSD", HS_VI_OSD_GRP, osd_in_ports, 2,
                         &chain->resize_src_port, &chain->osd_in_port) != 0) {
        return -1;
    }
    chain->bind_resize_osd_ok = 1;
    if (bind_first_match("OSD", HS_VI_OSD_GRP, osd_out_ports, 2,
                         "VO", 0, vo_in_ports, 2,
                         &chain->osd_src_port, &chain->vo_in_port) != 0) {
        return -1;
    }
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("HIGHLIGHT_SUPPRESS_VI", TILE_LIVE);
    set_tile_status("HIGHLIGHT_SUPPRESS", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_hs_vi_chain(hs_vi_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_Stop(0, 0);
    }
    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", HS_VI_OSD_GRP, chain->osd_src_port,
                         "VO", 0, chain->vo_in_port);
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", HS_VI_RESIZE_GRP, chain->resize_src_port,
                         "OSD", HS_VI_OSD_GRP, chain->osd_in_port);
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_hs_resize_ok) {
        MEDIA_SYS_UnBind("HIGHLIGHT_SUPPRESS", HS_VI_GRP, chain->hs_src_port,
                         "RESIZE_RGA", HS_VI_RESIZE_GRP, chain->resize_in_port);
        chain->bind_hs_resize_ok = 0;
    }
    if (chain->bind_vi_hs_ok) {
        MEDIA_SYS_UnBind("VI", 0, chain->vi_src_port,
                         "HIGHLIGHT_SUPPRESS", HS_VI_GRP, chain->hs_in_port);
        chain->bind_vi_hs_ok = 0;
    }
    if (chain->hs_ok) {
        drain_highlight_output();
    }
    if (chain->resize_ok) {
        MEDIA_RESIZE_RGA_Disable(HS_VI_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(HS_VI_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(HS_VI_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->osd_ok) {
        drain_hs_vi_osd_output();
        MEDIA_OSD_Stop(HS_VI_OSD_GRP);
        drain_hs_vi_osd_output();
        MEDIA_OSD_DestroyGrp(HS_VI_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->hs_ok) {
        MEDIA_HIGHLIGHT_SUPPRESS_Stop(HS_VI_GRP);
        drain_highlight_output();
        MEDIA_HIGHLIGHT_SUPPRESS_DestroyGrp(HS_VI_GRP);
        chain->hs_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(HS_VI_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(HS_VI_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->hs_pool_ok) {
        MEDIA_POOL_Destroy(HS_VI_OUTPUT_POOL);
        chain->hs_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(HS_VI_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_highlight_suppress_vi_run(volatile sig_atomic_t *running) {
    hs_vi_chain_t chain = {0};
    if (setup_hs_vi_chain(&chain) != 0) {
        fprintf(stderr, "HIGHLIGHT_SUPPRESS_VI standalone bind chain setup failed\n");
        cleanup_hs_vi_chain(&chain);
        return 1;
    }

    printf("HIGHLIGHT_SUPPRESS_VI standalone: VI %s %dx%d -> HIGHLIGHT_SUPPRESS -> RESIZE_RGA %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           HS_VI_CAMERA_DEVICE, HS_VI_SRC_W, HS_VI_SRC_H,
           HS_VI_VIEW_W, HS_VI_VIEW_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t hs_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        MEDIA_HIGHLIGHT_SUPPRESS_PERF perf = {0};
        int passthrough = (tick % 2) == 0;
        (void)MEDIA_HIGHLIGHT_SUPPRESS_SetPassthrough(HS_VI_GRP, passthrough);
        sleep(1);
        tick++;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("HIGHLIGHT_SUPPRESS", HS_VI_GRP, &hs_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", HS_VI_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", HS_VI_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        (void)MEDIA_HIGHLIGHT_SUPPRESS_GetLastPerf(HS_VI_GRP, &perf);
        (void)update_hs_vi_overlay(passthrough, vi_count, hs_count, resize_count,
                                   osd_count, vo_count, &perf);
        printf("HIGHLIGHT_SUPPRESS_VI vi_frames=%llu suppress_frames=%llu resize_frames=%llu osd_frames=%llu vo_frames=%llu mode=%s low=%.2f high=%.2f ratio=%.2f strength=%.2f module=%s %.3f/%.3fms tick=%d standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)hs_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               passthrough ? "bypass" : "suppress",
               HS_VI_THRESHOLD_LOW,
               HS_VI_THRESHOLD_HIGH,
               HS_VI_RATIO,
               HS_VI_STRENGTH,
               perf.gpu_enabled ? "gpu" : "cpu",
               perf.gpu_enabled ? perf.gpu_kernel_ms : perf.cpu_ms,
               perf.gpu_enabled ? perf.gpu_queue_ms : 0.0,
               tick);
    }

    cleanup_hs_vi_chain(&chain);
    return 0;
}
