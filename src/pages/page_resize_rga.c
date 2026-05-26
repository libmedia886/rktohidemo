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
#define RR_SCREEN_W 1080
#define RR_SCREEN_H 1920
#define RR_CAMERA_POOL 2
#define RR_RESIZE_POOL 9
#define RR_OSD_POOL 8
#define RR_RESIZE_GRP 61
#define RR_OSD_GRP 81
#define RR_SRC_W 3840
#define RR_SRC_H 2160
#define RR_SRC_STRIDE 3840
#define RR_VIEW_W 1080
#define RR_VIEW_H 608
#define RR_VIEW_STRIDE ALIGN_UP_LOCAL(RR_VIEW_W, 64)
#define RR_VIEW_X 0
#define RR_VIEW_Y 320
#define RR_FPS 30
#define RR_CAMERA_DEVICE "/dev/video-camera0"
#define RR_DEMO_W 640
#define RR_DEMO_H 640
#define RR_UPDATE_FRAMES (RR_FPS * 2)
#define RR_SRC_SIZE ((size_t)RR_SRC_STRIDE * (size_t)RR_SRC_H * 3u / 2u)
#define RR_VIEW_SIZE ((size_t)RR_VIEW_STRIDE * (size_t)RR_VIEW_H * 3u / 2u)
#define RR_TEXT_MASK_W 1024
#define RR_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
} resize_crop_t;

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
    resize_crop_t crop;
} resize_rga_chain_t;

static int resize_wave_i(int t, int max_value) {
    int period;
    int v;
    if (max_value <= 0) return 0;
    period = max_value * 2;
    v = t % period;
    return v <= max_value ? v : period - v;
}

static resize_crop_t resize_crop_for_frame(int frame) {
    resize_crop_t p;
    int range;
    p.crop_w = 520 - (resize_wave_i(frame * 2, 200) & ~1);
    if (p.crop_w < 320) p.crop_w = 320;
    p.crop_h = p.crop_w;
    range = RR_DEMO_W - p.crop_w;
    p.crop_x = resize_wave_i(frame * 5, range) & ~1;
    p.crop_y = resize_wave_i(frame * 3, range) & ~1;
    return p;
}

static void crop_to_src_rect(resize_crop_t p, int *x, int *y, int *w, int *h) {
    int src_x = ((p.crop_x * RR_SRC_W) / RR_DEMO_W) & ~1;
    int src_y = ((p.crop_y * RR_SRC_H) / RR_DEMO_H) & ~1;
    int src_w = ((p.crop_w * RR_SRC_W) / RR_DEMO_W) & ~1;
    int src_h = ((p.crop_h * RR_SRC_H) / RR_DEMO_H) & ~1;

    if (src_w < 2) src_w = 2;
    if (src_h < 2) src_h = 2;
    if (src_x + src_w > RR_SRC_W) src_w = (RR_SRC_W - src_x) & ~1;
    if (src_y + src_h > RR_SRC_H) src_h = (RR_SRC_H - src_y) & ~1;

    if (x) *x = src_x;
    if (y) *y = src_y;
    if (w) *w = src_w;
    if (h) *h = src_h;
}

static void fill_resize_attr(MEDIA_RESIZE_RGA_ATTR *attr, resize_crop_t p) {
    int src_x = 0;
    int src_y = 0;
    int src_w = 0;
    int src_h = 0;
    if (!attr) return;
    memset(attr, 0, sizeof(*attr));
    crop_to_src_rect(p, &src_x, &src_y, &src_w, &src_h);

    attr->src_x = src_x;
    attr->src_y = src_y;
    attr->src_width = src_w;
    attr->src_height = src_h;
    attr->input_width = RR_SRC_W;
    attr->input_height = RR_SRC_H;
    attr->input_stride = RR_SRC_STRIDE;
    attr->input_format = MEDIA_FORMAT_NV12;
    attr->input_depth = 4;
    attr->out_width = RR_VIEW_W;
    attr->out_height = RR_VIEW_H;
    attr->out_stride = RR_VIEW_STRIDE;
    attr->output_format = MEDIA_FORMAT_NV12;
    attr->output_pool_id = RR_RESIZE_POOL;
}

static void drain_resize_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_RESIZE_RGA_GetFrame(RR_RESIZE_GRP, &out, 0) != 0) break;
            MEDIA_RESIZE_RGA_ReleaseFrame(RR_RESIZE_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(RR_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(RR_OSD_GRP, out);
    }
}

static int create_resize_group(resize_rga_chain_t *chain, resize_crop_t crop) {
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    if (!chain) return -1;
    fill_resize_attr(&resize, crop);
    if (MEDIA_RESIZE_RGA_CreateGrp(RR_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(RR_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(RR_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(RR_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(RR_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(RR_RESIZE_GRP);
        return -1;
    }
    chain->resize_ok = 1;
    chain->crop = crop;
    return 0;
}

static void destroy_resize_group(resize_rga_chain_t *chain) {
    if (!chain || !chain->resize_ok) return;
    drain_resize_output();
    MEDIA_RESIZE_RGA_Disable(RR_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(RR_RESIZE_GRP);
    drain_resize_output();
    MEDIA_RESIZE_RGA_DestroyGrp(RR_RESIZE_GRP);
    chain->resize_ok = 0;
}

static int update_resize_info_overlay(resize_crop_t crop,
                                      uint64_t vi_count,
                                      uint64_t resize_count,
                                      uint64_t osd_count,
                                      uint64_t vo_count) {
    static uint8_t masks[5][RR_TEXT_MASK_W * RR_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char crop_line[160];
    char count_line[128];
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    float zoom = (float)RR_DEMO_W / (float)crop.crop_w;
    crop_to_src_rect(crop, &sx, &sy, &sw, &sh);
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(crop_line, sizeof(crop_line),
             "CROP %d,%d %dX%d  SRC %d,%d %dX%d  ZOOM %.2f",
             crop.crop_x, crop.crop_y, crop.crop_w, crop.crop_h,
             sx, sy, sw, sh, zoom);
    snprintf(count_line, sizeof(count_line), "VI %llu  RGA %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(RR_OSD_GRP, 0, 24, 16, 2,
                              "RESIZE_RGA LIVE VI DYNAMIC CROP",
                              masks[0], sizeof(masks[0]), RR_TEXT_MASK_W, RR_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(RR_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), RR_TEXT_MASK_W, RR_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(RR_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), RR_TEXT_MASK_W, RR_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(RR_OSD_GRP, 3, 24, 554, 2,
                              crop_line,
                              masks[3], sizeof(masks[3]), RR_TEXT_MASK_W, RR_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(RR_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), RR_TEXT_MASK_W, RR_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_resize_rga_chain(resize_rga_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    resize_crop_t crop = resize_crop_for_frame(0);
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(RR_CAMERA_POOL, RR_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(RR_RESIZE_POOL, RR_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(RR_OSD_POOL, RR_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = RR_CAMERA_DEVICE;
    vi.width = RR_SRC_W;
    vi.height = RR_SRC_H;
    vi.stride = RR_SRC_STRIDE;
    vi.fps = RR_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = RR_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    if (create_resize_group(chain, crop) != 0) return -1;

    osd.input_width = RR_VIEW_W;
    osd.input_height = RR_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = RR_OSD_POOL;
    osd.input_stride = RR_VIEW_STRIDE;
    osd.output_stride = RR_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(RR_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(RR_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = RR_SCREEN_W;
    vo.height = RR_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, RR_VIEW_X, RR_VIEW_Y, RR_VIEW_W, RR_VIEW_H,
                           RR_VIEW_STRIDE, 6, MEDIA_VO_PLANE_TYPE_AUTO,
                           MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "RESIZE_RGA", RR_RESIZE_GRP, "input0") != 0) {
        return -1;
    }
    chain->bind_vi_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", RR_RESIZE_GRP, "output0",
                       "OSD", RR_OSD_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", RR_OSD_GRP, "output0",
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

static void cleanup_resize_rga_chain(resize_rga_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", RR_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", RR_RESIZE_GRP, "output0",
                         "OSD", RR_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_vi_resize_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "RESIZE_RGA", RR_RESIZE_GRP, "input0");
        chain->bind_vi_resize_ok = 0;
    }
    if (chain->osd_ok) {
        drain_osd_output();
        MEDIA_OSD_Stop(RR_OSD_GRP);
        drain_osd_output();
        MEDIA_OSD_DestroyGrp(RR_OSD_GRP);
        chain->osd_ok = 0;
    }
    destroy_resize_group(chain);
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(RR_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(RR_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(RR_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

static int reconfigure_resize_crop(resize_rga_chain_t *chain, resize_crop_t crop) {
    if (!chain || !chain->resize_ok) return -1;

    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", RR_RESIZE_GRP, "output0",
                         "OSD", RR_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_vi_resize_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "RESIZE_RGA", RR_RESIZE_GRP, "input0");
        chain->bind_vi_resize_ok = 0;
    }

    destroy_resize_group(chain);

    if (create_resize_group(chain, crop) != 0) return -1;
    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "RESIZE_RGA", RR_RESIZE_GRP, "input0") != 0) {
        return -1;
    }
    chain->bind_vi_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", RR_RESIZE_GRP, "output0",
                       "OSD", RR_OSD_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_resize_osd_ok = 1;
    return 0;
}

int page_resize_rga_run(volatile sig_atomic_t *running) {
    resize_rga_chain_t chain = {0};
    if (setup_resize_rga_chain(&chain) != 0) {
        fprintf(stderr, "RESIZE_RGA standalone VI chain setup failed\n");
        cleanup_resize_rga_chain(&chain);
        return 1;
    }

    printf("RESIZE_RGA standalone: VI %s %dx%d -> dynamic RESIZE_RGA %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           RR_CAMERA_DEVICE, RR_SRC_W, RR_SRC_H, RR_VIEW_W, RR_VIEW_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        resize_crop_t crop = resize_crop_for_frame(tick * RR_FPS);

        if (((tick * RR_FPS) % RR_UPDATE_FRAMES) == 0 &&
            (crop.crop_x != chain.crop.crop_x || crop.crop_y != chain.crop.crop_y ||
             crop.crop_w != chain.crop.crop_w || crop.crop_h != chain.crop.crop_h)) {
            if (reconfigure_resize_crop(&chain, crop) != 0) {
                fprintf(stderr, "warning: RESIZE_RGA dynamic crop update rejected\n");
            }
        }

        sleep(1);
        tick++;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", RR_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", RR_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_resize_info_overlay(chain.crop, vi_count, resize_count,
                                                    osd_count, vo_count) == 0;
        printf("RESIZE_RGA vi=%llu resize=%llu osd=%llu vo=%llu crop=%d,%d,%d,%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               chain.crop.crop_x,
               chain.crop.crop_y,
               chain.crop.crop_w,
               chain.crop.crop_h,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_resize_rga_chain(&chain);
    return 0;
}
