#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_overlay.h"

#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define TF_SCREEN_W 1080
#define TF_SCREEN_H 1920
#define TF_CAMERA_POOL 2
#define TF_VPSS_POOL 14
#define TF_OUTPUT_POOL 15
#define TF_OSD_POOL 8
#define TF_RESIZE_POOL 9
#define TF_VPSS_GRP 63
#define TF_GRP 95
#define TF_OSD_GRP 81
#define TF_RESIZE_GRP 61
#define TF_SRC_W 3840
#define TF_SRC_H 2160
#define TF_SRC_STRIDE 3840
#define TF_VIEW_W 1072
#define TF_VIEW_H 608
#define TF_VIEW_STRIDE ALIGN_UP_LOCAL(TF_VIEW_W, 64)
#define TF_VIEW_X ((TF_SCREEN_W - TF_VIEW_W) / 2)
#define TF_VIEW_Y 320
#define TF_FPS 30
#define TF_CAMERA_DEVICE "/dev/video-camera0"
#define TF_LUT_W 960
#define TF_LUT_H 540
#define TF_SRC_SIZE ((size_t)TF_SRC_STRIDE * (size_t)TF_SRC_H * 3u / 2u)
#define TF_VIEW_SIZE ((size_t)TF_VIEW_STRIDE * (size_t)TF_VIEW_H * 3u / 2u)
#define TF_LUT_SIZE ((size_t)TF_LUT_W * (size_t)TF_LUT_H * 2u * sizeof(float))
#define TF_TEXT_MASK_W 1024
#define TF_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef enum {
    TF_MODE_RAW = 0,
    TF_MODE_UNDISTORT,
    TF_MODE_ROTATE_ZOOM,
    TF_MODE_PERSPECTIVE,
    TF_MODE_COUNT,
} transform_mode_t;

typedef struct {
    const char *name;
    const char *desc;
} transform_mode_desc_t;

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int vpss_pool_ok;
    int transform_pool_ok;
    int osd_pool_ok;
    int resize_pool_ok;
    int vi_attr_ok;
    int vpss_ok;
    int transform_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_vpss_ok;
    int bind_vpss_transform_ok;
    int bind_transform_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
    float *lut;
} transform_chain_t;

static const transform_mode_desc_t g_modes[TF_MODE_COUNT] = {
    {"RAW", "identity"},
    {"UNDISTORT", "radial correction"},
    {"ROTATE ZOOM", "dynamic rotation and zoom"},
    {"PERSPECTIVE", "keystone correction"},
};

static void clamp_xy(float *sx, float *sy) {
    if (*sx < 0.0f) *sx = 0.0f;
    if (*sy < 0.0f) *sy = 0.0f;
    if (*sx > (float)(TF_SRC_W - 1)) *sx = (float)(TF_SRC_W - 1);
    if (*sy > (float)(TF_SRC_H - 1)) *sy = (float)(TF_SRC_H - 1);
}

static void build_lut(transform_mode_t mode, int frame, float *lut) {
    const float cx = (float)(TF_SRC_W - 1) * 0.5f;
    const float cy = (float)(TF_SRC_H - 1) * 0.5f;
    if (!lut) return;

    for (int y = 0; y < TF_LUT_H; ++y) {
        float oy = (TF_LUT_H <= 1) ? 0.0f :
            (float)y * (float)(TF_SRC_H - 1) / (float)(TF_LUT_H - 1);
        for (int x = 0; x < TF_LUT_W; ++x) {
            float ox = (TF_LUT_W <= 1) ? 0.0f :
                (float)x * (float)(TF_SRC_W - 1) / (float)(TF_LUT_W - 1);
            float sx = ox;
            float sy = oy;
            float dx = ox - cx;
            float dy = oy - cy;

            if (mode == TF_MODE_UNDISTORT) {
                float nx = dx / cx;
                float ny = dy / cy;
                float r2 = nx * nx + ny * ny;
                float scale = 1.0f + 0.18f * r2 + 0.06f * r2 * r2;
                sx = cx + dx * scale;
                sy = cy + dy * scale;
            } else if (mode == TF_MODE_ROTATE_ZOOM) {
                float angle = (float)frame * 0.025f;
                float zoom = 1.12f + 0.10f * sinf((float)frame * 0.035f);
                float c = cosf(angle);
                float s = sinf(angle);
                float rx = dx / zoom;
                float ry = dy / zoom;
                sx = cx + c * rx + s * ry;
                sy = cy - s * rx + c * ry;
            } else if (mode == TF_MODE_PERSPECTIVE) {
                float u = ox / (float)(TF_SRC_W - 1);
                float v = oy / (float)(TF_SRC_H - 1);
                const float tlx = (float)TF_SRC_W * 0.158f;
                const float tly = (float)TF_SRC_H * 0.071f;
                const float trx = (float)TF_SRC_W * 0.842f;
                const float try_ = (float)TF_SRC_H * 0.142f;
                const float blx = (float)TF_SRC_W * 0.050f;
                const float bly = (float)TF_SRC_H * 0.946f;
                const float brx = (float)TF_SRC_W * 0.950f;
                const float bry = (float)TF_SRC_H * 0.896f;
                float top_x = tlx + (trx - tlx) * u;
                float top_y = tly + (try_ - tly) * u;
                float bot_x = blx + (brx - blx) * u;
                float bot_y = bly + (bry - bly) * u;
                sx = top_x + (bot_x - top_x) * v;
                sy = top_y + (bot_y - top_y) * v;
            }
            clamp_xy(&sx, &sy);
            size_t off = ((size_t)y * TF_LUT_W + (size_t)x) * 2u;
            lut[off] = sx;
            lut[off + 1] = sy;
        }
    }
}

static void drain_transform_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_TRANSFORM_GetFrame(TF_GRP, &out, 0) != 0) break;
            MEDIA_TRANSFORM_ReleaseFrame(TF_GRP, out);
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
            if (MEDIA_RESIZE_RGA_GetFrame(TF_RESIZE_GRP, &out, 0) != 0) break;
            MEDIA_RESIZE_RGA_ReleaseFrame(TF_RESIZE_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(TF_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(TF_OSD_GRP, out);
    }
}

static void drain_vpss_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VPSS_Chn_GetFrame(TF_VPSS_GRP, 0, &out, 0) != 0) break;
            MEDIA_VPSS_Chn_ReleaseFrame(TF_VPSS_GRP, 0, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static int update_transform_info_overlay(transform_mode_t mode,
                                         uint64_t vi_count, uint64_t vpss_count,
                                         uint64_t tf_count, uint64_t resize_count,
                                         uint64_t osd_count, uint64_t vo_count) {
    static uint8_t masks[5][TF_TEXT_MASK_W * TF_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char count_line[160];
    char mode_line[128];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(mode_line, sizeof(mode_line), "MODE %s  LUT %dX%d",
             g_modes[mode].name, TF_LUT_W, TF_LUT_H);
    snprintf(count_line, sizeof(count_line), "VI %llu  VPSS %llu  TF %llu  RGA %llu  OSD %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)vpss_count,
             (unsigned long long)tf_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(TF_OSD_GRP, 0, 24, 16, 2,
                              "TRANSFORM LIVE VI 4K LUT",
                              masks[0], sizeof(masks[0]), TF_TEXT_MASK_W, TF_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(TF_OSD_GRP, 1, 24, 502, 2,
                              "FLOW VI->VPSS->TRANSFORM->RESIZE_RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), TF_TEXT_MASK_W, TF_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(TF_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), TF_TEXT_MASK_W, TF_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(TF_OSD_GRP, 3, 24, 554, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), TF_TEXT_MASK_W, TF_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(TF_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), TF_TEXT_MASK_W, TF_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_transform_chain(transform_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_TRANSFORM_ATTR transform = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    chain->lut = (float *)malloc(TF_LUT_SIZE);
    if (!chain->lut) return -1;
    build_lut(TF_MODE_RAW, 0, chain->lut);

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(TF_CAMERA_POOL, TF_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(TF_VPSS_POOL, TF_SRC_SIZE, 4) != 0) return -1;
    chain->vpss_pool_ok = 1;
    if (MEDIA_POOL_Create(TF_OUTPUT_POOL, TF_SRC_SIZE, 4) != 0) return -1;
    chain->transform_pool_ok = 1;
    if (MEDIA_POOL_Create(TF_RESIZE_POOL, TF_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(TF_OSD_POOL, TF_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = TF_CAMERA_DEVICE;
    vi.width = TF_SRC_W;
    vi.height = TF_SRC_H;
    vi.stride = TF_SRC_STRIDE;
    vi.fps = TF_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = TF_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    vpss.width = TF_SRC_W;
    vpss.height = TF_SRC_H;
    vpss.input_stride = TF_SRC_STRIDE;
    vpss.input_depth = 4;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = -1;
    vpss.out_fps = -1;
    vpss.output_count = 1;
    vpss.outputs[0].output_id = 0;
    vpss.outputs[0].out_width = TF_SRC_W;
    vpss.outputs[0].out_height = TF_SRC_H;
    vpss.outputs[0].out_stride = TF_SRC_STRIDE;
    vpss.outputs[0].pool_id = TF_VPSS_POOL;
    vpss.outputs[0].crop_x = 0;
    vpss.outputs[0].crop_y = 0;
    vpss.outputs[0].crop_w = TF_SRC_W;
    vpss.outputs[0].crop_h = TF_SRC_H;
    vpss.outputs[0].in_fps = -1;
    vpss.outputs[0].out_fps = -1;
    vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(TF_VPSS_GRP, &vpss) != 0) return -1;
    chain->vpss_ok = 1;

    transform.out_width = TF_SRC_W;
    transform.out_height = TF_SRC_H;
    transform.out_stride = TF_SRC_STRIDE;
    transform.format = MEDIA_FORMAT_NV12;
    transform.input_depth = 4;
    transform.pool_id = TF_OUTPUT_POOL;
    transform.in_width = TF_SRC_W;
    transform.in_height = TF_SRC_H;
    transform.in_stride = TF_SRC_STRIDE;
    transform.lut_width = TF_LUT_W;
    transform.lut_height = TF_LUT_H;
    transform.lut = chain->lut;
    transform.lut_size = TF_LUT_SIZE;
    if (MEDIA_TRANSFORM_CreateGrp(TF_GRP, &transform) != 0 ||
        MEDIA_TRANSFORM_Start(TF_GRP) != 0) {
        return -1;
    }
    chain->transform_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = TF_SRC_W;
    resize.src_height = TF_SRC_H;
    resize.input_width = TF_SRC_W;
    resize.input_height = TF_SRC_H;
    resize.input_stride = TF_SRC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 6;
    resize.out_width = TF_VIEW_W;
    resize.out_height = TF_VIEW_H;
    resize.out_stride = TF_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = TF_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(TF_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(TF_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(TF_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = TF_VIEW_W;
    osd.input_height = TF_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = TF_OSD_POOL;
    osd.input_stride = TF_VIEW_STRIDE;
    osd.output_stride = TF_VIEW_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(TF_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(TF_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = TF_SCREEN_W;
    vo.height = TF_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, TF_VIEW_X, TF_VIEW_Y,
                           TF_VIEW_W, TF_VIEW_H, TF_VIEW_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "VPSS", TF_VPSS_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_vi_vpss_ok = 1;
    if (MEDIA_SYS_Bind("VPSS", TF_VPSS_GRP, "output0",
                       "TRANSFORM", TF_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_vpss_transform_ok = 1;
    if (MEDIA_SYS_Bind("TRANSFORM", TF_GRP, "output",
                       "RESIZE_RGA", TF_RESIZE_GRP, "input0") != 0) {
        return -1;
    }
    chain->bind_transform_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", TF_RESIZE_GRP, "output0",
                       "OSD", TF_OSD_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", TF_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) {
        return -1;
    }
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VPSS_Enable(TF_VPSS_GRP) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("TRANSFORM", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_transform_chain(transform_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vpss_ok) MEDIA_VPSS_Disable(TF_VPSS_GRP);
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", TF_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", TF_RESIZE_GRP, "output0",
                         "OSD", TF_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->osd_ok) {
        drain_osd_output();
        MEDIA_OSD_Stop(TF_OSD_GRP);
        drain_osd_output();
        MEDIA_OSD_DestroyGrp(TF_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_resize_output();
        MEDIA_RESIZE_RGA_Disable(TF_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(TF_RESIZE_GRP);
        drain_resize_output();
    }
    if (chain->bind_transform_resize_ok) {
        MEDIA_SYS_UnBind("TRANSFORM", TF_GRP, "output",
                         "RESIZE_RGA", TF_RESIZE_GRP, "input0");
        chain->bind_transform_resize_ok = 0;
    }
    if (chain->transform_ok) {
        drain_transform_output();
        MEDIA_TRANSFORM_Stop(TF_GRP);
        drain_transform_output();
    }
    if (chain->bind_vpss_transform_ok) {
        MEDIA_SYS_UnBind("VPSS", TF_VPSS_GRP, "output0",
                         "TRANSFORM", TF_GRP, "input");
        chain->bind_vpss_transform_ok = 0;
    }
    if (chain->bind_vi_vpss_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output", "VPSS", TF_VPSS_GRP, "input");
        chain->bind_vi_vpss_ok = 0;
    }
    if (chain->vpss_ok) drain_vpss_output();

    if (chain->resize_ok) {
        MEDIA_RESIZE_RGA_DestroyGrp(TF_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->transform_ok) {
        MEDIA_TRANSFORM_DestroyGrp(TF_GRP);
        chain->transform_ok = 0;
    }
    if (chain->vpss_ok) {
        MEDIA_VPSS_DestroyGrp(TF_VPSS_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(TF_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(TF_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->transform_pool_ok) {
        MEDIA_POOL_Destroy(TF_OUTPUT_POOL);
        chain->transform_pool_ok = 0;
    }
    if (chain->vpss_pool_ok) {
        MEDIA_POOL_Destroy(TF_VPSS_POOL);
        chain->vpss_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(TF_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
    free(chain->lut);
    chain->lut = NULL;
}

int page_transform_run(volatile sig_atomic_t *running) {
    transform_chain_t chain = {0};
    if (setup_transform_chain(&chain) != 0) {
        fprintf(stderr, "TRANSFORM standalone chain setup failed\n");
        cleanup_transform_chain(&chain);
        return 1;
    }

    printf("TRANSFORM standalone: VI %s %dx%d -> VPSS -> TRANSFORM LUT -> RESIZE_RGA %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
           TF_CAMERA_DEVICE, TF_SRC_W, TF_SRC_H, TF_VIEW_W, TF_VIEW_H);
    int tick = 0;
    int last_mode = -1;
    while (!running || *running) {
        transform_mode_t mode = (transform_mode_t)(tick % TF_MODE_COUNT);
        if ((int)mode != last_mode) {
            build_lut(mode, tick * TF_FPS, chain.lut);
            if (MEDIA_TRANSFORM_UpdateLut(TF_GRP, chain.lut, TF_LUT_SIZE) != 0) {
                fprintf(stderr, "TRANSFORM LUT update failed mode=%s\n", g_modes[mode].name);
            }
            last_mode = (int)mode;
        }
        sleep(1);
        tick++;
        uint64_t vi_count = 0;
        uint64_t vpss_count = 0;
        uint64_t tf_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VPSS", TF_VPSS_GRP, &vpss_count);
        (void)MEDIA_SYS_GetModuleFrameCount("TRANSFORM", TF_GRP, &tf_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", TF_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", TF_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_transform_info_overlay(mode, vi_count, vpss_count, tf_count,
                                                       resize_count, osd_count, vo_count) == 0;
        printf("TRANSFORM vi=%llu vpss=%llu transform=%llu resize=%llu osd=%llu vo=%llu mode=%s desc=%s lut=%dx%d tick=%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)vpss_count,
               (unsigned long long)tf_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               g_modes[mode].name,
               g_modes[mode].desc,
               TF_LUT_W,
               TF_LUT_H,
               tick,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_transform_chain(&chain);
    return 0;
}
