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
#define VP_SCREEN_W 1080
#define VP_SCREEN_H 1920
#define VP_SCREEN_STRIDE ALIGN_UP_LOCAL(VP_SCREEN_W, 64)
#define VP_CAMERA_POOL 2
#define VP_OUTPUT_POOL 14
#define VP_VMIX_POOL 7
#define VP_OSD_POOL 8
#define VP_GRP 63
#define VP_VMIX_GRP 80
#define VP_OSD_GRP 81
#define VP_SRC_W 3840
#define VP_SRC_H 2160
#define VP_SRC_STRIDE 3840
#define VP_TILE_W 480
#define VP_TILE_H 480
#define VP_TILE_STRIDE 640
#define VP_OUTPUTS 4
#define VP_FPS 30
#define VP_CAMERA_DEVICE "/dev/video-camera0"
#define VP_SRC_SIZE ((size_t)VP_SRC_STRIDE * (size_t)VP_SRC_H * 3u / 2u)
#define VP_TILE_SIZE ((size_t)VP_TILE_STRIDE * (size_t)VP_TILE_H * 3u / 2u)
#define VP_DISPLAY_SIZE ((size_t)VP_SCREEN_STRIDE * (size_t)VP_SCREEN_H * 3u / 2u)
#define VP_TEXT_MASK_W 1024
#define VP_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int vpss_pool_ok;
    int vmix_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int vpss_ok;
    int vmix_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_vpss_ok;
    int bind_vpss_vmix_ok[VP_OUTPUTS];
    int bind_vmix_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
} vpss_chain_t;

static int pingpong_i(int t, int max_value) {
    int period;
    int v;
    if (max_value <= 0) return 0;
    period = max_value * 2;
    v = t % period;
    return v <= max_value ? v : period - v;
}

static void fill_vpss_output_attr(MEDIA_VPSS_OUT_ATTR *out, int output_id,
                                  int crop_x, int crop_y,
                                  int crop_w, int crop_h,
                                  int flip_h, int flip_v) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->output_id = output_id;
    out->out_width = VP_TILE_W;
    out->out_height = VP_TILE_H;
    out->out_stride = VP_TILE_STRIDE;
    out->pool_id = VP_OUTPUT_POOL;
    out->crop_x = crop_x;
    out->crop_y = crop_y;
    out->crop_w = crop_w;
    out->crop_h = crop_h;
    out->in_fps = -1;
    out->out_fps = -1;
    out->flip_h = flip_h;
    out->flip_v = flip_v;
    out->output_format = MEDIA_FORMAT_NV12;
}

static void fill_initial_output_attr(MEDIA_VPSS_OUT_ATTR *out, int output_id) {
    if (output_id == 1) {
        fill_vpss_output_attr(out, output_id, VP_SRC_W / 4, VP_SRC_H / 4,
                              VP_SRC_W / 2, VP_SRC_H / 2, 0, 0);
    } else if (output_id == 2) {
        fill_vpss_output_attr(out, output_id, 0, 0,
                              VP_SRC_W, VP_SRC_H, 1, 0);
    } else if (output_id == 3) {
        fill_vpss_output_attr(out, output_id, VP_SRC_W / 8, VP_SRC_H / 8,
                              VP_SRC_W * 3 / 4, VP_SRC_H * 3 / 4, 0, 0);
    } else {
        fill_vpss_output_attr(out, output_id, 0, 0,
                              VP_SRC_W, VP_SRC_H, 0, 0);
    }
}

static int update_vpss_dynamic_attrs(int frame) {
    MEDIA_VPSS_OUT_ATTR out = {0};
    int ret = 0;
    int max_square = VP_SRC_W < VP_SRC_H ? VP_SRC_W : VP_SRC_H;
    int crop_size = max_square / 2;
    int crop_x = pingpong_i(frame * 4, VP_SRC_W - crop_size) & ~1;
    int crop_y = pingpong_i(frame * 3, VP_SRC_H - crop_size) & ~1;

    fill_vpss_output_attr(&out, 1, crop_x, crop_y, crop_size, crop_size, 0, 0);
    if (MEDIA_VPSS_SetOutAttr(VP_GRP, &out) != 0) ret = -1;

    fill_vpss_output_attr(&out, 2, 0, 0, VP_SRC_W, VP_SRC_H,
                          ((frame / (VP_FPS * 2)) & 1) != 0,
                          ((frame / (VP_FPS * 4)) & 1) != 0);
    if (MEDIA_VPSS_SetOutAttr(VP_GRP, &out) != 0) ret = -1;

    crop_size = max_square - (pingpong_i(frame * 2, max_square / 2) & ~1);
    if (crop_size < max_square / 2) crop_size = max_square / 2;
    crop_x = ((VP_SRC_W - crop_size) / 2) & ~1;
    crop_y = ((VP_SRC_H - crop_size) / 2) & ~1;
    fill_vpss_output_attr(&out, 3, crop_x, crop_y, crop_size, crop_size, 0, 0);
    if (MEDIA_VPSS_SetOutAttr(VP_GRP, &out) != 0) ret = -1;
    return ret;
}

static void drain_vpss_outputs(void) {
    for (int ch = 0; ch < VP_OUTPUTS; ++ch) {
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VPSS_Chn_GetFrame(VP_GRP, ch, &out, 0) != 0) break;
            MEDIA_VPSS_Chn_ReleaseFrame(VP_GRP, ch, out);
        }
    }
}

static void drain_vmix_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_VMIX_GetFrame(VP_VMIX_GRP, &out, 0) != 0) break;
        MEDIA_VMIX_ReleaseFrame(VP_VMIX_GRP, out);
    }
}

static void drain_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(VP_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(VP_OSD_GRP, out);
    }
}

static int update_vpss_info_overlay(const char *title,
                                    uint64_t vi_count,
                                    uint64_t vpss_count,
                                    uint64_t vmix_count, uint64_t osd_count,
                                    uint64_t vo_count) {
    static uint8_t masks[4][VP_TEXT_MASK_W * VP_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char count_line[160];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(count_line, sizeof(count_line), "VI %llu  VPSS %llu  VMIX %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)vpss_count,
             (unsigned long long)vmix_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(VP_OSD_GRP, 2, 32, 24, 3,
                              title,
                              masks[0], sizeof(masks[0]), VP_TEXT_MASK_W, VP_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(VP_OSD_GRP, 3, 32, 1816, 2,
                              perf_line,
                              masks[1], sizeof(masks[1]), VP_TEXT_MASK_W, VP_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(VP_OSD_GRP, 4, 32, 1848, 2,
                              "FLOW VI->VPSS->VMIX->OSD->VO  OUT FULL CROP FLIP ZOOM",
                              masks[2], sizeof(masks[2]), VP_TEXT_MASK_W, VP_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(VP_OSD_GRP, 5, 32, 1880, 2,
                              count_line,
                              masks[3], sizeof(masks[3]), VP_TEXT_MASK_W, VP_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_vpss_chain(vpss_chain_t *chain) {
    static const int pos_x[VP_OUTPUTS] = {54, 546, 54, 546};
    static const int pos_y[VP_OUTPUTS] = {170, 170, 690, 690};
    MEDIA_VI_ATTR vi = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_VMIX_ATTR vmix = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(VP_CAMERA_POOL, VP_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(VP_OUTPUT_POOL, VP_TILE_SIZE, VP_OUTPUTS * 6) != 0) return -1;
    chain->vpss_pool_ok = 1;
    if (MEDIA_POOL_Create(VP_VMIX_POOL, VP_DISPLAY_SIZE, 4) != 0) return -1;
    chain->vmix_pool_ok = 1;
    if (MEDIA_POOL_Create(VP_OSD_POOL, VP_DISPLAY_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = VP_CAMERA_DEVICE;
    vi.width = VP_SRC_W;
    vi.height = VP_SRC_H;
    vi.stride = VP_SRC_STRIDE;
    vi.fps = VP_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = VP_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    vpss.width = VP_SRC_W;
    vpss.height = VP_SRC_H;
    vpss.input_stride = VP_SRC_STRIDE;
    vpss.input_depth = 3;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = -1;
    vpss.out_fps = -1;
    vpss.output_count = VP_OUTPUTS;
    for (int i = 0; i < VP_OUTPUTS; ++i) fill_initial_output_attr(&vpss.outputs[i], i);
    if (MEDIA_VPSS_SetAttr(VP_GRP, &vpss) != 0) return -1;
    chain->vpss_ok = 1;

    vmix.input_count = VP_OUTPUTS;
    vmix.output_width = VP_SCREEN_W;
    vmix.output_height = VP_SCREEN_H;
    vmix.output_stride = VP_SCREEN_STRIDE;
    vmix.format = MEDIA_FORMAT_NV12;
    vmix.input_depth = 3;
    vmix.output_pool_id = VP_VMIX_POOL;
    vmix.primary_index = -1;
    for (int i = 0; i < VP_OUTPUTS; ++i) {
        vmix.channels[i].enabled = 1;
        vmix.channels[i].x = pos_x[i];
        vmix.channels[i].y = pos_y[i];
        vmix.channels[i].width = VP_TILE_W;
        vmix.channels[i].height = VP_TILE_H;
        vmix.channels[i].alpha = 1.0f;
        vmix.channels[i].stride = VP_TILE_STRIDE;
        vmix.channels[i].format = MEDIA_FORMAT_NV12;
    }
    if (MEDIA_VMIX_CreateGrp(VP_VMIX_GRP, &vmix) != 0 ||
        MEDIA_VMIX_Start(VP_VMIX_GRP) != 0 ||
        MEDIA_VMIX_Enable(VP_VMIX_GRP) != 0) {
        return -1;
    }
    chain->vmix_ok = 1;

    osd.input_width = VP_SCREEN_W;
    osd.input_height = VP_SCREEN_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 3;
    osd.output_pool_id = VP_OSD_POOL;
    osd.input_stride = VP_SCREEN_STRIDE;
    osd.output_stride = VP_SCREEN_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(VP_OSD_GRP, &osd) != 0) return -1;
    chain->osd_ok = 1;

    MEDIA_OSD_RECT_DESC rect = {0};
    rect.filled = 1;
    rect.color.r = 5;
    rect.color.g = 10;
    rect.color.b = 18;
    rect.color.a = 220;
    MEDIA_OSD_REGION_ATTR top = {0};
    top.enabled = 1;
    top.x = 0;
    top.y = 0;
    top.width = VP_SCREEN_W;
    top.height = 96;
    top.zorder = 0;
    top.global_alpha = 220;
    MEDIA_OSD_REGION_ATTR bottom = top;
    bottom.y = VP_SCREEN_H - 112;
    bottom.height = 112;
    if (MEDIA_OSD_UpdateRegion(VP_OSD_GRP, 0, &top) != 0 ||
        MEDIA_OSD_SetRegionRect(VP_OSD_GRP, 0, &rect) != 0 ||
        MEDIA_OSD_UpdateRegion(VP_OSD_GRP, 1, &bottom) != 0 ||
        MEDIA_OSD_SetRegionRect(VP_OSD_GRP, 1, &rect) != 0 ||
        MEDIA_OSD_Start(VP_OSD_GRP) != 0) {
        return -1;
    }

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = VP_SCREEN_W;
    vo.height = VP_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, 0, 0, VP_SCREEN_W, VP_SCREEN_H,
                           VP_SCREEN_STRIDE, 4, MEDIA_VO_PLANE_TYPE_AUTO,
                           MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output", "VPSS", VP_GRP, "input") != 0) return -1;
    chain->bind_vi_vpss_ok = 1;
    for (int i = 0; i < VP_OUTPUTS; ++i) {
        char src_port[16];
        char dst_port[16];
        snprintf(src_port, sizeof(src_port), "output%d", i);
        snprintf(dst_port, sizeof(dst_port), "input%d", i);
        if (MEDIA_SYS_Bind("VPSS", VP_GRP, src_port,
                           "VMIX", VP_VMIX_GRP, dst_port) != 0) {
            return -1;
        }
        chain->bind_vpss_vmix_ok[i] = 1;
    }
    if (MEDIA_SYS_Bind("VMIX", VP_VMIX_GRP, "output0", "OSD", VP_OSD_GRP, "input") != 0) return -1;
    chain->bind_vmix_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", VP_OSD_GRP, "output0", "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VPSS_Enable(VP_GRP) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("VMIX", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_vpss_chain(vpss_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vpss_ok) MEDIA_VPSS_Disable(VP_GRP);
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", VP_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_vmix_osd_ok) {
        MEDIA_SYS_UnBind("VMIX", VP_VMIX_GRP, "output0", "OSD", VP_OSD_GRP, "input");
        chain->bind_vmix_osd_ok = 0;
    }
    for (int i = VP_OUTPUTS - 1; i >= 0; --i) {
        if (chain->bind_vpss_vmix_ok[i]) {
            char src_port[16];
            char dst_port[16];
            snprintf(src_port, sizeof(src_port), "output%d", i);
            snprintf(dst_port, sizeof(dst_port), "input%d", i);
            MEDIA_SYS_UnBind("VPSS", VP_GRP, src_port, "VMIX", VP_VMIX_GRP, dst_port);
            chain->bind_vpss_vmix_ok[i] = 0;
        }
    }
    if (chain->bind_vi_vpss_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output", "VPSS", VP_GRP, "input");
        chain->bind_vi_vpss_ok = 0;
    }

    if (chain->osd_ok) {
        drain_osd_output();
        MEDIA_OSD_Stop(VP_OSD_GRP);
        drain_osd_output();
        MEDIA_OSD_DestroyGrp(VP_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->vmix_ok) {
        drain_vmix_output();
        MEDIA_VMIX_Disable(VP_VMIX_GRP);
        MEDIA_VMIX_Stop(VP_VMIX_GRP);
        drain_vmix_output();
        MEDIA_VMIX_DestroyGrp(VP_VMIX_GRP);
        chain->vmix_ok = 0;
    }
    if (chain->vpss_ok) {
        drain_vpss_outputs();
        MEDIA_VPSS_DestroyGrp(VP_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(VP_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->vmix_pool_ok) {
        MEDIA_POOL_Destroy(VP_VMIX_POOL);
        chain->vmix_pool_ok = 0;
    }
    if (chain->vpss_pool_ok) {
        MEDIA_POOL_Destroy(VP_OUTPUT_POOL);
        chain->vpss_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(VP_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

static int run_vpss_vmix_page(volatile sig_atomic_t *running,
                              const char *page_name,
                              const char *title) {
    vpss_chain_t chain = {0};
    if (setup_vpss_chain(&chain) != 0) {
        fprintf(stderr, "%s standalone VI chain setup failed\n", page_name);
        cleanup_vpss_chain(&chain);
        return 1;
    }

    printf("%s standalone: VI %s %dx%d -> VPSS four outputs -> VMIX -> OSD -> VO. Ctrl+C to stop.\n",
           page_name, VP_CAMERA_DEVICE, VP_SRC_W, VP_SRC_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t vpss_count = 0;
        uint64_t vmix_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        sleep(1);
        tick++;
        (void)update_vpss_dynamic_attrs(tick * VP_FPS);
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VPSS", VP_GRP, &vpss_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VMIX", VP_VMIX_GRP, &vmix_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", VP_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_vpss_info_overlay(title, vi_count, vpss_count, vmix_count, osd_count, vo_count) == 0;
        printf("%s vi=%llu vpss=%llu vmix=%llu osd=%llu vo=%llu outputs=%d tile=%dx%d tick=%d overlay=%s standalone=1\n",
               page_name,
               (unsigned long long)vi_count,
               (unsigned long long)vpss_count,
               (unsigned long long)vmix_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               VP_OUTPUTS, VP_TILE_W, VP_TILE_H, tick, overlay_ok ? "perf_text" : "failed");
    }

    cleanup_vpss_chain(&chain);
    return 0;
}

int page_vpss_run(volatile sig_atomic_t *running) {
    return run_vpss_vmix_page(running, "VPSS", "VPSS LIVE VI FOUR OUTPUTS");
}

int page_vmix_live_run(volatile sig_atomic_t *running) {
    return run_vpss_vmix_page(running, "VMIX", "VMIX LIVE VI FOUR INPUTS");
}
