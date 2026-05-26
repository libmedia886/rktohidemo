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
#define RGA_SCREEN_W 1080
#define RGA_SCREEN_H 1920
#define RGA_CAMERA_POOL 2
#define RGA_OUTPUT_POOL 9
#define RGA_OSD_POOL 8
#define RGA_GRP 59
#define RGA_OSD_GRP 81
#define RGA_SRC_W 3840
#define RGA_SRC_H 2160
#define RGA_SRC_STRIDE 3840
#define RGA_VIEW_W 1080
#define RGA_VIEW_H 608
#define RGA_VIEW_STRIDE ALIGN_UP_LOCAL(RGA_VIEW_W, 64)
#define RGA_VIEW_X 0
#define RGA_VIEW_Y 320
#define RGA_FPS 30
#define RGA_OP_SECONDS 3
#define RGA_CAMERA_DEVICE "/dev/video-camera0"
#define RGA_DEMO_W 640
#define RGA_DEMO_H 640
#define RGA_SRC_SIZE ((size_t)RGA_SRC_STRIDE * (size_t)RGA_SRC_H * 3u / 2u)
#define RGA_VIEW_SIZE ((size_t)RGA_VIEW_STRIDE * (size_t)RGA_VIEW_H * 3u / 2u)
#define RGA_TEXT_MASK_W 1024
#define RGA_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    const char *label;
    int algo;
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
    int rotate;
    int flip_h;
    int flip_v;
} rga_op_t;

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int output_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int rga_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_rga_ok;
    int bind_rga_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
    int active_op;
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
} rga_chain_t;

static const rga_op_t g_rga_ops[] = {
    {"COPY", MEDIA_RGA_ALG_COPY, 0, 0, RGA_DEMO_W, RGA_DEMO_H, 0, 0, 0},
    {"CROP_ZOOM", MEDIA_RGA_ALG_RESIZE, 120, 120, 400, 400, 0, 0, 0},
    {"FLIP_H", MEDIA_RGA_ALG_FLIP, 0, 0, RGA_DEMO_W, RGA_DEMO_H, 0, 1, 0},
    {"FLIP_V", MEDIA_RGA_ALG_FLIP, 0, 0, RGA_DEMO_W, RGA_DEMO_H, 0, 0, 1},
    {"ROTATE90", MEDIA_RGA_ALG_ROTATE, 0, 0, RGA_DEMO_W, RGA_DEMO_H, 90, 0, 0},
    {"ROTATE180", MEDIA_RGA_ALG_ROTATE, 0, 0, RGA_DEMO_W, RGA_DEMO_H, 180, 0, 0},
    {"ROTATE270", MEDIA_RGA_ALG_ROTATE, 0, 0, RGA_DEMO_W, RGA_DEMO_H, 270, 0, 0},
};

static int rga_wave_i(int t, int max_value) {
    int period;
    int v;
    if (max_value <= 0) return 0;
    period = max_value * 2;
    v = t % period;
    return v <= max_value ? v : period - v;
}

static void scale_demo_rect(const rga_op_t *op, int *x, int *y, int *w, int *h) {
    int sx = ((op->crop_x * RGA_SRC_W) / RGA_DEMO_W) & ~1;
    int sy = ((op->crop_y * RGA_SRC_H) / RGA_DEMO_H) & ~1;
    int sw = ((op->crop_w * RGA_SRC_W) / RGA_DEMO_W) & ~1;
    int sh = ((op->crop_h * RGA_SRC_H) / RGA_DEMO_H) & ~1;
    if (sw < 2) sw = 2;
    if (sh < 2) sh = 2;
    if (sx + sw > RGA_SRC_W) sw = (RGA_SRC_W - sx) & ~1;
    if (sy + sh > RGA_SRC_H) sh = (RGA_SRC_H - sy) & ~1;
    if (x) *x = sx;
    if (y) *y = sy;
    if (w) *w = sw;
    if (h) *h = sh;
}

static void dynamic_crop_zoom_rect(int frame, int *x, int *y, int *w, int *h) {
    int max_h = RGA_SRC_H;
    int min_h = max_h / 2;
    int crop_h = min_h + (rga_wave_i(frame * 3, max_h / 4) & ~1);
    int crop_w = (crop_h * 16 / 9) & ~1;
    int max_x;
    int max_y;
    if (crop_w > RGA_SRC_W) {
        crop_w = RGA_SRC_W & ~1;
        crop_h = (crop_w * 9 / 16) & ~1;
    }
    max_x = RGA_SRC_W - crop_w;
    max_y = RGA_SRC_H - crop_h;
    if (x) *x = rga_wave_i(frame * 5, max_x) & ~1;
    if (y) *y = rga_wave_i(frame * 4, max_y) & ~1;
    if (w) *w = crop_w;
    if (h) *h = crop_h;
}

static void fill_rga_attr(MEDIA_RGA_GRP_ATTR *attr, int op_index, int frame,
                          int *crop_x, int *crop_y, int *crop_w, int *crop_h) {
    const rga_op_t *op = &g_rga_ops[op_index];
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    if (!attr) return;
    memset(attr, 0, sizeof(*attr));
    if (op_index == 1) {
        dynamic_crop_zoom_rect(frame, &sx, &sy, &sw, &sh);
    } else {
        scale_demo_rect(op, &sx, &sy, &sw, &sh);
    }

    attr->algo = op->algo;
    if (op->algo == MEDIA_RGA_ALG_COPY &&
        (RGA_SRC_W != RGA_VIEW_W || RGA_SRC_H != RGA_VIEW_H)) {
        attr->algo = MEDIA_RGA_ALG_RESIZE;
    }
    attr->input_count = 1;
    attr->output_count = 1;
    attr->input_depth = 4;
    attr->output_depth = 4;
    attr->inputs[0].port_id = 0;
    attr->inputs[0].width = RGA_SRC_W;
    attr->inputs[0].height = RGA_SRC_H;
    attr->inputs[0].stride = RGA_SRC_STRIDE;
    attr->inputs[0].format = MEDIA_FORMAT_NV12;
    attr->inputs[0].crop_x = sx;
    attr->inputs[0].crop_y = sy;
    attr->inputs[0].crop_w = sw;
    attr->inputs[0].crop_h = sh;
    attr->outputs[0].port_id = 0;
    attr->outputs[0].width = RGA_VIEW_W;
    attr->outputs[0].height = RGA_VIEW_H;
    attr->outputs[0].stride = RGA_VIEW_STRIDE;
    attr->outputs[0].format = MEDIA_FORMAT_NV12;
    attr->outputs[0].pool_id = RGA_OUTPUT_POOL;
    attr->outputs[0].rotate = op->rotate;
    attr->outputs[0].flip_h = op->flip_h;
    attr->outputs[0].flip_v = op->flip_v;

    if (crop_x) *crop_x = sx;
    if (crop_y) *crop_y = sy;
    if (crop_w) *crop_w = sw;
    if (crop_h) *crop_h = sh;
}

static void drain_rga_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_RGA_GetFrame(RGA_GRP, &out, 0) != 0) break;
            MEDIA_RGA_ReleaseFrame(RGA_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(RGA_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(RGA_OSD_GRP, out);
    }
}

static int set_rga_op(rga_chain_t *chain, int op_index, int frame) {
    MEDIA_RGA_GRP_ATTR attr = {0};
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    if (!chain || op_index < 0 || op_index >= (int)(sizeof(g_rga_ops) / sizeof(g_rga_ops[0]))) {
        return -1;
    }
    fill_rga_attr(&attr, op_index, frame, &sx, &sy, &sw, &sh);
    if (MEDIA_RGA_SetGrpAttr(RGA_GRP, &attr) != 0) return -1;
    chain->active_op = op_index;
    chain->crop_x = sx;
    chain->crop_y = sy;
    chain->crop_w = sw;
    chain->crop_h = sh;
    return 0;
}

static int update_rga_info_overlay(const rga_chain_t *chain,
                                   uint64_t vi_count,
                                   uint64_t rga_count,
                                   uint64_t osd_count,
                                   uint64_t vo_count) {
    static uint8_t masks[5][RGA_TEXT_MASK_W * RGA_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char op_line[160];
    char count_line[128];
    int op = chain ? chain->active_op : 0;
    if (op < 0 || op >= (int)(sizeof(g_rga_ops) / sizeof(g_rga_ops[0]))) op = 0;
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(op_line, sizeof(op_line), "OP %s  CROP %d,%d %dX%d",
             g_rga_ops[op].label,
             chain ? chain->crop_x : 0,
             chain ? chain->crop_y : 0,
             chain ? chain->crop_w : 0,
             chain ? chain->crop_h : 0);
    snprintf(count_line, sizeof(count_line), "VI %llu  RGA %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)rga_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(RGA_OSD_GRP, 0, 24, 16, 2,
                              "RGA LIVE VI HARDWARE OPS",
                              masks[0], sizeof(masks[0]), RGA_TEXT_MASK_W, RGA_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(RGA_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), RGA_TEXT_MASK_W, RGA_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(RGA_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), RGA_TEXT_MASK_W, RGA_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(RGA_OSD_GRP, 3, 24, 554, 2,
                              op_line,
                              masks[3], sizeof(masks[3]), RGA_TEXT_MASK_W, RGA_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(RGA_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), RGA_TEXT_MASK_W, RGA_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_rga_chain(rga_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_RGA_GRP_ATTR rga = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    int sx = 0;
    int sy = 0;
    int sw = 0;
    int sh = 0;
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(RGA_CAMERA_POOL, RGA_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(RGA_OUTPUT_POOL, RGA_VIEW_SIZE, 6) != 0) return -1;
    chain->output_pool_ok = 1;
    if (MEDIA_POOL_Create(RGA_OSD_POOL, RGA_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = RGA_CAMERA_DEVICE;
    vi.width = RGA_SRC_W;
    vi.height = RGA_SRC_H;
    vi.stride = RGA_SRC_STRIDE;
    vi.fps = RGA_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = RGA_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    fill_rga_attr(&rga, 0, 0, &sx, &sy, &sw, &sh);
    if (MEDIA_RGA_CreateGrp(RGA_GRP, &rga) != 0 ||
        MEDIA_RGA_Start(RGA_GRP) != 0) {
        MEDIA_RGA_Stop(RGA_GRP);
        MEDIA_RGA_DestroyChn(RGA_GRP);
        return -1;
    }
    chain->rga_ok = 1;
    chain->active_op = 0;
    chain->crop_x = sx;
    chain->crop_y = sy;
    chain->crop_w = sw;
    chain->crop_h = sh;

    osd.input_width = RGA_VIEW_W;
    osd.input_height = RGA_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = RGA_OSD_POOL;
    osd.input_stride = RGA_VIEW_STRIDE;
    osd.output_stride = RGA_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(RGA_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(RGA_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = RGA_SCREEN_W;
    vo.height = RGA_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, RGA_VIEW_X, RGA_VIEW_Y,
                           RGA_VIEW_W, RGA_VIEW_H, RGA_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output", "RGA", RGA_GRP, "input0") != 0) return -1;
    chain->bind_vi_rga_ok = 1;
    if (MEDIA_SYS_Bind("RGA", RGA_GRP, "output0", "OSD", RGA_OSD_GRP, "input") != 0) return -1;
    chain->bind_rga_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", RGA_OSD_GRP, "output0", "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_rga_chain(rga_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", RGA_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_rga_osd_ok) {
        MEDIA_SYS_UnBind("RGA", RGA_GRP, "output0", "OSD", RGA_OSD_GRP, "input");
        chain->bind_rga_osd_ok = 0;
    }
    if (chain->bind_vi_rga_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output", "RGA", RGA_GRP, "input0");
        chain->bind_vi_rga_ok = 0;
    }
    if (chain->osd_ok) {
        drain_osd_output();
        MEDIA_OSD_Stop(RGA_OSD_GRP);
        drain_osd_output();
        MEDIA_OSD_DestroyGrp(RGA_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->rga_ok) {
        drain_rga_output();
        MEDIA_RGA_Stop(RGA_GRP);
        drain_rga_output();
        MEDIA_RGA_DestroyChn(RGA_GRP);
        chain->rga_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(RGA_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->output_pool_ok) {
        MEDIA_POOL_Destroy(RGA_OUTPUT_POOL);
        chain->output_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(RGA_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_rga_run(volatile sig_atomic_t *running) {
    rga_chain_t chain = {0};
    if (setup_rga_chain(&chain) != 0) {
        fprintf(stderr, "RGA standalone VI chain setup failed\n");
        cleanup_rga_chain(&chain);
        return 1;
    }

    printf("RGA standalone: VI %s %dx%d -> RGA hardware ops %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           RGA_CAMERA_DEVICE, RGA_SRC_W, RGA_SRC_H, RGA_VIEW_W, RGA_VIEW_H);
    int tick = 0;
    int last_op = -1;
    while (!running || *running) {
        int op = (tick / RGA_OP_SECONDS) % (int)(sizeof(g_rga_ops) / sizeof(g_rga_ops[0]));
        int op_frame = tick * RGA_FPS;
        if (op != last_op || op == 1) {
            if (set_rga_op(&chain, op, op_frame) != 0) {
                fprintf(stderr, "warning: RGA op %s rejected\n", g_rga_ops[op].label);
            } else {
                last_op = op;
            }
        }

        sleep(1);
        tick++;
        uint64_t vi_count = 0;
        uint64_t rga_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RGA", RGA_GRP, &rga_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", RGA_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_rga_info_overlay(&chain, vi_count, rga_count,
                                                 osd_count, vo_count) == 0;
        printf("RGA vi=%llu rga=%llu osd=%llu vo=%llu op=%s crop=%d,%d,%d,%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)rga_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               g_rga_ops[chain.active_op].label,
               chain.crop_x,
               chain.crop_y,
               chain.crop_w,
               chain.crop_h,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_rga_chain(&chain);
    return 0;
}
