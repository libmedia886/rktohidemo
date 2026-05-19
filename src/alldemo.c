#define _GNU_SOURCE

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/input.h>
#include <linux/videodev2.h>
#include <math.h>
#include <netinet/in.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <jpeglib.h>
#include <png.h>

#include "media_api.h"

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define SCREEN_W 1080
#define SCREEN_H 1920
#define DISPLAY_POOL 1
#define CAMERA_POOL 2
#define WORK_POOL_NV12 3
#define WORK_POOL_RGB 4
#define WORK_POOL_OUT 5
#define OSD_INPUT_POOL 6
#define OSD_OUTPUT_POOL 7
#define RESIZE_INPUT_POOL 8
#define RESIZE_OUTPUT_POOL 9
#define STEREO_INPUT0_POOL 10
#define STEREO_INPUT1_POOL 11
#define STEREO_OUTPUT_POOL 12
#define VPSS_INPUT_POOL 13
#define VPSS_OUTPUT_POOL 14
#define CONV_CL_OUTPUT_POOL 15
#define WBC_OUTPUT_POOL CONV_CL_OUTPUT_POOL
#define CLAHE_OUTPUT_POOL CONV_CL_OUTPUT_POOL
#define CONV_CL_QUAD_INPUT_POOL_BASE 16
#define CONV_CL_QUAD_OUTPUT_POOL_BASE 20
#define VPSS_DEMO_OUTPUTS 4
#define DISPLAY_VMIX_INPUT_POOL 6
#define DISPLAY_VMIX_OUTPUT_POOL 7
#define DISPLAY_OSD_OUTPUT_POOL 8
#define DUALVIEW_INPUT0_POOL 10
#define DUALVIEW_INPUT1_POOL 11
#define DUALVIEW_SBS_OUTPUT_POOL 12
#define DUALVIEW_LBL_OUTPUT_POOL 13
#define LIVE_OSD_GRP 60
#define LIVE_RGA_GRP 59
#define LIVE_RESIZE_GRP 61
#define LIVE_STEREO_GRP 62
#define LIVE_VPSS_GRP 63
#define LIVE_CSC_RGA_GRP 64
#define LIVE_TRANSFORM_GRP 65
#define LIVE_CAP_DEHAZE_GRP 66
#define LIVE_DCP_DEHAZE_GRP 67
#define LIVE_CONV_CL_GRP 68
#define LIVE_CLAHE_GRP 69
#define LIVE_EDOF_GRP 70
#define LIVE_DUALVIEW_SBS_GRP 71
#define LIVE_DUALVIEW_LBL_GRP 72
#define LIVE_RETINEX_GRP 73
#define LIVE_PANO_GRP 74
#define LIVE_CSC_RGA_BACK_GRP 75
#define LIVE_CSC_CL_GRP 76
#define LIVE_WBC_RESIZE_GRP 78
#define LIVE_WBC_VI_RESIZE_GRP 79
#define DISPLAY_VMIX_GRP 80
#define DISPLAY_OSD_GRP 81
#define LIVE_MCF_GRP 114
#define LIVE_COMPARE_RAW_RESIZE_GRP 115
#define LIVE_COMPARE_EFFECT_RESIZE_GRP 116
#define LIVE_TRANSFORM_DISPLAY_RESIZE_BASE_GRP 117
#define LIVE_WBC_CHN 0
#define LIVE_CONV_CL_QUAD_BASE_GRP 82
#define LIVE_CONV_CL_PRE_CSC_BASE_GRP 86
#define LIVE_CONV_CL_CSC_BASE_GRP 90
#define LIVE_TRANSFORM_QUAD_BASE_GRP 94
#define NPU_DEMO_VDEC_CHN 110
#define NPU_DEMO_PRE_RGA_GRP 111
#define NPU_DEMO_NPU_GRP 112
#define NPU_DEMO_POST_RGA_GRP 113
#define MCF_INPUT0_POOL STEREO_INPUT0_POOL
#define MCF_INPUT1_POOL STEREO_INPUT1_POOL
#define MCF_OUTPUT_POOL STEREO_OUTPUT_POOL

#define CAM_W 640
#define CAM_H 640
#define CAM_STRIDE 640
#define COMPARE_VIEW_W SCREEN_W
#define COMPARE_VIEW_H 608
#define COMPARE_VIEW_STRIDE ALIGN_UP(COMPARE_VIEW_W, 64)
#define VI_BIG_W 3840
#define VI_BIG_H 2160
#define VI_BIG_STRIDE 3840
#define VI_BIG_FRAME_SIZE (VI_BIG_STRIDE * VI_BIG_H * 3 / 2)
#define VI_BIG_VIEW_W SCREEN_W
#define VI_BIG_VIEW_H 608
#define VI_BIG_VIEW_STRIDE ALIGN_UP(VI_BIG_VIEW_W, 64)
#define VI_BIG_VIEW_FRAME_SIZE (VI_BIG_VIEW_STRIDE * VI_BIG_VIEW_H * 3 / 2)
#define VI_BIG_VIEW_X 0
#define VI_BIG_VIEW_Y 320
#define DEHAZE_RGB4K_STRIDE ALIGN_UP(VI_BIG_W * 3, 64)
#define DEHAZE_RGB4K_FRAME_SIZE (DEHAZE_RGB4K_STRIDE * VI_BIG_H)
#define VPSS_TILE_W 480
#define VPSS_TILE_H 480
#define VPSS_TILE_NV12_STRIDE CAM_STRIDE
#define VPSS_TILE_NV12_SIZE (VPSS_TILE_NV12_STRIDE * VPSS_TILE_H * 3 / 2)
#define VPSS_TILE_RGBA_STRIDE (VPSS_TILE_W * 4)
#define VPSS_TILE_RGBA_SIZE (VPSS_TILE_RGBA_STRIDE * VPSS_TILE_H)
#define VPSS_QUAD_OUTPUT_POOL_COUNT (VPSS_DEMO_OUTPUTS * 6)
#define TRANSFORM_OUTPUT_POOL CONV_CL_OUTPUT_POOL
#define STEREO_INPUT_COUNT 2
#define STEREO_ROTATED_OUTPUT 1
#define STEREO_VPSS_ROTATE_DEGREES 90
#define NPU_DEMO_VDEC_POOL WORK_POOL_NV12
#define NPU_DEMO_RGB_POOL WORK_POOL_RGB
#define NPU_DEMO_VDEC_FRAME_SIZE (CAM_STRIDE * 768 * 3 / 2)
#define NPU_DEMO_H264_PATH "assets/loop/npu/bus_640x640.h264"
#define NPU_DEMO_MODEL_PATH "assets/npu/yolov5s-640-640.rknn"
#define NPU_DEMO_LABEL_PATH "assets/npu/coco_80_labels_list.txt"
#define NPU_DEMO_MAX_DRAW_OBJECTS 5
#define WBC_CAPTURE_W (SCREEN_W - (SCREEN_W % 64))
#define WBC_CAPTURE_H SCREEN_H
#define WBC_CAPTURE_STRIDE WBC_CAPTURE_W
#define WBC_FRAME_SIZE (WBC_CAPTURE_STRIDE * WBC_CAPTURE_H * 3 / 2)
#define WBC_PANEL_W 480
#define WBC_PANEL_H 900
#define WBC_PANEL_STRIDE ALIGN_UP(WBC_PANEL_W, 64)
#define WBC_PANEL_FRAME_SIZE (WBC_PANEL_STRIDE * WBC_PANEL_H * 3 / 2)
#define WBC_PANEL_X 552
#define WBC_PANEL_Y 96
#define WBC_VI_PANEL_W 480
#define WBC_VI_PANEL_H 480
#define WBC_VI_PANEL_STRIDE ALIGN_UP(WBC_VI_PANEL_W, 64)
#define WBC_VI_PANEL_FRAME_SIZE (WBC_VI_PANEL_STRIDE * WBC_VI_PANEL_H * 3 / 2)
#define WBC_VI_PANEL_X 48
#define WBC_VI_PANEL_Y 306
#define PANO_INPUT_COUNT 6
#define PANO_DOMAIN_W 8378
#define PANO_DOMAIN_H 4189
#define PANO_STITCH_W 1920
#define PANO_STITCH_H 960
#define PANO_STITCH_STRIDE ALIGN_UP(PANO_STITCH_W, 4)
#define FPS 30
#define CAMERA_DEVICE "/dev/video-camera0"
#define LICENSE_PATH "/root/licence.dat"
#define UI_FONT_PATH "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
#define RTSP_PORT 8554
#define CAM_FRAME_SIZE (CAM_STRIDE * CAM_H * 3 / 2)
#define COMPARE_VIEW_FRAME_SIZE (COMPARE_VIEW_STRIDE * COMPARE_VIEW_H * 3 / 2)
#define RGB_FRAME_SIZE (CAM_W * CAM_H * 3)
#define RGBA_FRAME_SIZE (CAM_W * CAM_H * 4)
#define PANO_OUTPUT_SIZE (PANO_STITCH_STRIDE * PANO_STITCH_H * 3 / 2)
#define MAIN_ROTATE_SECONDS 5
#define PAGE_ROTATE_SECONDS 8
#define CUSTOMER_ROTATE_SECONDS 10
#define RGA_DEMO_OP_COUNT 7
#define NAV_KEY_MAX_FDS 16
#define NAV_ENV_PAGE "ALLDEMO_LOOP_PAGE"
#define NAV_ENV_TOTAL "ALLDEMO_LOOP_TOTAL"
#define NAV_ENV_MODE "ALLDEMO_LOOP_MODE"
#define NAV_ENV_PROFILE "ALLDEMO_LOOP_PROFILE"
#define RGA_OP_SECONDS 3
#define EDOF_PAIR_SECONDS 3
#define MCF_PAIR_SECONDS 3
#define RESIZE_DEMO_UPDATE_FRAMES (FPS * 2)
#define VO_CAPTURE_SECONDS 5
#define TILE_FIRST_INDEX 4
#define TILE_ROTATE_COUNT 16
#define DISPLAY_VMIX_LAYOUT_DEFAULT 0
#define DISPLAY_VMIX_LAYOUT_WBC 1
#define DISPLAY_VMIX_LAYOUT_DEMO 2
#define DISPLAY_VMIX_LAYOUT_VI_BIG 3
#define CSC_CL_VIEW_W SCREEN_W
#define CSC_CL_VIEW_H SCREEN_H
#define CSC_CL_VIEW_X 0
#define CSC_CL_VIEW_Y 0
#define CSC_CL_VIEW_STRIDE ALIGN_UP(CSC_CL_VIEW_W * 4, 64)
#define CSC_CL_VIEW_FRAME_SIZE (CSC_CL_VIEW_STRIDE * CSC_CL_VIEW_H)

#ifndef KEY_VOLUMEDOWN
#define KEY_VOLUMEDOWN 114
#endif
#ifndef KEY_VOLUMEUP
#define KEY_VOLUMEUP 115
#endif

typedef struct {
    const char *name;
    int active;
    int frames;
    int status;
} module_tile_t;

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} image_asset_t;

typedef struct {
    const char *tile_name;
    const char *caption;
    const char *paths[4];
    int path_count;
    image_asset_t images[4];
    int loaded_count;
} loop_asset_t;

typedef enum {
    NAV_KEY_NONE = 0,
    NAV_KEY_PREV = -1,
    NAV_KEY_NEXT = 1,
} nav_key_action_t;

typedef struct {
    int fds[NAV_KEY_MAX_FDS];
    int count;
} nav_key_reader_t;

typedef struct {
    float x;
    float y;
    float w;
    float h;
    float score;
    int class_id;
    char label[64];
} npu_demo_object_t;

typedef struct {
    int object_count;
    uint64_t frame_id;
    npu_demo_object_t objects[NPU_DEMO_MAX_DRAW_OBJECTS];
} npu_demo_result_t;

typedef struct {
    uint8_t *data;
    size_t size;
    int *nal_offsets;
    int nal_count;
    int nal_index;
    uint64_t pts;
} h264_stream_t;

typedef struct {
    const char *left_path;
    const char *right_path;
    const char *fused_path;
    image_asset_t left;
    image_asset_t right;
    image_asset_t fused;
    int loaded;
} edof_pair_t;

typedef struct {
    const char *name;
    const char *color_path;
    const char *mono_path;
    const char *reference_path;
    image_asset_t color;
    image_asset_t mono;
    image_asset_t reference;
    int loaded;
} mcf_pair_t;

typedef struct {
    const char *pto_path;
    const char *image_paths[PANO_INPUT_COUNT];
    image_asset_t inputs[PANO_INPUT_COUNT];
    uint8_t *nv12[PANO_INPUT_COUNT];
    int input_count;
    int in_w;
    int in_h;
    int loaded;
} pano_sample_t;

typedef struct {
    int license_ok;
    int camera_node_ok;
    int drm_ok;
    int media_lib_ok;
    int rtsp_port_free;
    int npu_model_ok;
    int avm_lut_ok;
    int pano_calib_ok;
    int svm_assets_ok;
    int loop_loaded;
    int loop_expected;
    int camera_running;
    int camera_frames;
} health_status_t;

typedef struct {
    float cpu_percent;
    float gpu_percent;
    int gpu_available;
    float rga_percent;
    int rga_available;
} perf_status_t;

typedef struct {
    const uint8_t *cam;
    const uint8_t *osd_live;
    const uint8_t *resize_live;
    const uint8_t *vpss_live;
    const uint8_t *csc_rga_live;
    const uint8_t *transform_live;
    const uint8_t *cap_live;
    const uint8_t *dcp_live;
    const uint8_t *conv_live;
    const uint8_t *clahe_live;
    const uint8_t *retinex_live;
    const uint8_t *pano_out;
    const uint8_t *edof_in0;
    const uint8_t *edof_in1;
    const uint8_t *edof_out;
    const uint8_t *mcf_color;
    const uint8_t *mcf_mono;
    const uint8_t *mcf_out;
    const uint8_t *stereo_live;
    const uint8_t *dual_in0;
    const uint8_t *dual_in1;
    const uint8_t *dual_sbs;
    const uint8_t *dual_lbl;
} display_refs_t;

typedef struct {
    const char *name;
    int kernel_size;
    const float *table;
    int table_size;
} conv_cl_effect_t;

typedef struct {
    const char *name;
    const char *desc;
} transform_effect_t;

static volatile sig_atomic_t g_running = 1;
static volatile sig_atomic_t g_signal_count = 0;
static health_status_t g_health = {0};
static perf_status_t g_perf = {0};
static int g_vo_demo_state = 0;
static int g_vo_demo_prev_state = -1;
static int g_vo_demo_phase_frame = 0;
static const char *g_vo_demo_api = "MEDIA_SYS_SendFrame";
static const char *g_vo_demo_status = "NORMAL REFRESH";
static const char *g_bind_vi_src_port = NULL;
static const char *g_bind_rga_in_port = NULL;
static const char *g_bind_rga_src_port = NULL;
static const char *g_bind_resize_in_port = NULL;
static const char *g_bind_resize_src_port = NULL;
static const char *g_bind_csc_front_in_port = NULL;
static const char *g_bind_csc_front_src_port = NULL;
static const char *g_bind_csc_back_in_port = NULL;
static const char *g_bind_csc_back_src_port = NULL;
static const char *g_bind_csc_cl_front_in_port = NULL;
static const char *g_bind_csc_cl_front_src_port = NULL;
static const char *g_bind_live_osd_in_port = NULL;
static const char *g_bind_live_osd_src_port = NULL;
static const char *g_bind_vpss_raw_src_port = NULL;
static const char *g_bind_vpss_clahe_src_port = NULL;
static const char *g_bind_vmix_raw_in_port = NULL;
static const char *g_bind_clahe_in_port = NULL;
static const char *g_bind_clahe_src_port = NULL;
static const char *g_clahe_mix_module = "VMIX";
static const char *g_bind_retinex_in_port = NULL;
static const char *g_bind_retinex_src_port = NULL;
static const char *g_retinex_mix_module = "VMIX";
static const char *g_bind_dehaze_in_port = NULL;
static const char *g_bind_dehaze_src_port = NULL;
static const char *g_conv_cl_mix_module = "VMIX";
static const char *g_bind_vmix_in_port = NULL;
static const char *g_bind_vmix_src_port = NULL;
static const char *g_bind_osd_in_port = NULL;
static const char *g_bind_osd_src_port = NULL;
static const char *g_bind_vo_in_port = NULL;
static const char *g_bind_vpss_in_port = NULL;
static const char *g_bind_vpss_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_vmix_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static uint8_t *g_vpss_preview_frames[VPSS_DEMO_OUTPUTS] = {NULL};
static int g_vpss_preview_ready[VPSS_DEMO_OUTPUTS] = {0};
static const char *g_bind_vpss_conv_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_conv_pre_csc_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_conv_pre_csc_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_conv_cl_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_conv_cl_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_conv_csc_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_conv_csc_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_conv_vmix_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_vpss_transform_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_transform_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_transform_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_transform_resize_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_transform_resize_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_transform_vmix_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_vpss_stereo_src_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_stereo_in_ports[VPSS_DEMO_OUTPUTS] = {NULL};
static const char *g_bind_stereo_src_ports[2] = {NULL};
static const char *g_bind_stereo_vmix_in_ports[2] = {NULL};
static const char *g_bind_vdec_src_port = NULL;
static const char *g_bind_npu_pre_rga_in_port = NULL;
static const char *g_bind_npu_pre_rga_src_port = NULL;
static const char *g_bind_npu_in_port = NULL;
static const char *g_bind_npu_src_port = NULL;
static const char *g_bind_npu_post_rga_in_port = NULL;
static const char *g_bind_npu_post_rga_src_port = NULL;
static const char *g_bind_wbc_src_port = NULL;
static const char *g_bind_wbc_resize_in_port = NULL;
static const char *g_bind_wbc_resize_src_port = NULL;
static const char *g_bind_wbc_vmix_in_port = NULL;
static const char *g_bind_wbc_vi_resize_in_port = NULL;
static const char *g_bind_wbc_vi_resize_src_port = NULL;
static const char *g_bind_wbc_vi_vmix_in_port = NULL;
static const char *g_bind_compare_raw_resize_in_port = NULL;
static const char *g_bind_compare_raw_resize_src_port = NULL;
static const char *g_bind_compare_effect_resize_in_port = NULL;
static const char *g_bind_compare_effect_resize_src_port = NULL;
static int g_wbc_connector_id = -1;
static int g_wbc_crtc_id = -1;

static const int g_conv_cl_quad_grps[VPSS_DEMO_OUTPUTS] = {
    LIVE_CONV_CL_QUAD_BASE_GRP + 0,
    LIVE_CONV_CL_QUAD_BASE_GRP + 1,
    LIVE_CONV_CL_QUAD_BASE_GRP + 2,
    LIVE_CONV_CL_QUAD_BASE_GRP + 3,
};
static const int g_conv_cl_pre_csc_grps[VPSS_DEMO_OUTPUTS] = {
    LIVE_CONV_CL_PRE_CSC_BASE_GRP + 0,
    LIVE_CONV_CL_PRE_CSC_BASE_GRP + 1,
    LIVE_CONV_CL_PRE_CSC_BASE_GRP + 2,
    LIVE_CONV_CL_PRE_CSC_BASE_GRP + 3,
};
static const int g_conv_cl_csc_grps[VPSS_DEMO_OUTPUTS] = {
    LIVE_CONV_CL_CSC_BASE_GRP + 0,
    LIVE_CONV_CL_CSC_BASE_GRP + 1,
    LIVE_CONV_CL_CSC_BASE_GRP + 2,
    LIVE_CONV_CL_CSC_BASE_GRP + 3,
};
static const int g_conv_cl_input_pools[VPSS_DEMO_OUTPUTS] = {
    CONV_CL_QUAD_INPUT_POOL_BASE + 0,
    CONV_CL_QUAD_INPUT_POOL_BASE + 1,
    CONV_CL_QUAD_INPUT_POOL_BASE + 2,
    CONV_CL_QUAD_INPUT_POOL_BASE + 3,
};
static const int g_conv_cl_output_pools[VPSS_DEMO_OUTPUTS] = {
    CONV_CL_QUAD_OUTPUT_POOL_BASE + 0,
    CONV_CL_QUAD_OUTPUT_POOL_BASE + 1,
    CONV_CL_QUAD_OUTPUT_POOL_BASE + 2,
    CONV_CL_QUAD_OUTPUT_POOL_BASE + 3,
};
static uint64_t g_conv_cl_show_counts[VPSS_DEMO_OUTPUTS] = {0};
static double g_conv_cl_show_kernel_ms = -1.0;
static double g_conv_cl_show_queue_ms = -1.0;

static const float g_conv_kernel_sharpen[9] = {
     0.0f, -1.0f,  0.0f,
    -1.0f,  5.0f, -1.0f,
     0.0f, -1.0f,  0.0f,
};
static const float g_conv_kernel_edge[9] = {
    -1.0f, -1.0f, -1.0f,
    -1.0f,  8.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,
};
static const float g_conv_kernel_emboss[9] = {
    -2.0f, -1.0f,  0.0f,
    -1.0f,  1.0f,  1.0f,
     0.0f,  1.0f,  2.0f,
};
static const float g_conv_kernel_blur[9] = {
    1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f,
    1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f,
    1.0f / 9.0f, 1.0f / 9.0f, 1.0f / 9.0f,
};
static const conv_cl_effect_t g_conv_cl_effects[VPSS_DEMO_OUTPUTS] = {
    {"SHARPEN", 3, g_conv_kernel_sharpen, (int)ARRAY_SIZE(g_conv_kernel_sharpen)},
    {"EDGE", 3, g_conv_kernel_edge, (int)ARRAY_SIZE(g_conv_kernel_edge)},
    {"EMBOSS", 3, g_conv_kernel_emboss, (int)ARRAY_SIZE(g_conv_kernel_emboss)},
    {"BLUR", 3, g_conv_kernel_blur, (int)ARRAY_SIZE(g_conv_kernel_blur)},
};

static const int g_transform_quad_grps[VPSS_DEMO_OUTPUTS] = {
    -1,
    LIVE_TRANSFORM_QUAD_BASE_GRP + 0,
    LIVE_TRANSFORM_QUAD_BASE_GRP + 1,
    LIVE_TRANSFORM_QUAD_BASE_GRP + 2,
};
static const int g_transform_display_resize_grps[VPSS_DEMO_OUTPUTS] = {
    LIVE_TRANSFORM_DISPLAY_RESIZE_BASE_GRP + 0,
    LIVE_TRANSFORM_DISPLAY_RESIZE_BASE_GRP + 1,
    LIVE_TRANSFORM_DISPLAY_RESIZE_BASE_GRP + 2,
    LIVE_TRANSFORM_DISPLAY_RESIZE_BASE_GRP + 3,
};
static int g_camera_input_w = CAM_W;
static int g_camera_input_h = CAM_H;
static int g_camera_input_stride = CAM_STRIDE;
static const transform_effect_t g_transform_effects[VPSS_DEMO_OUTPUTS] = {
    {"RAW", "原图直通"},
    {"UNDISTORT", "镜头畸变矫正"},
    {"ROTATE ZOOM", "动态旋转放大"},
    {"PERSPECTIVE", "透视梯形矫正"},
};

static void set_tile_status(const char *name, int status);
static int find_tile_index(const char *name);
static size_t live_camera_input_frame_size(void);
static void update_perf_status(void);
static void format_runtime_status(char *buf, size_t len);
static const char *showcase_nav_mode(void);

enum {
    TILE_OFFLINE = 0,
    TILE_SYNTH = 1,
    TILE_LOOP = 2,
    TILE_PROBED = 3,
    TILE_LIVE = 4,
};

enum {
    VO_DEMO_NORMAL = 0,
    VO_DEMO_FREEZE_MAIN = 1,
    VO_DEMO_FREEZE_PLANE = 2,
    VO_DEMO_HIDE_WARN = 3,
    VO_DEMO_HIDE_PLANE = 4,
};

static module_tile_t g_tiles[] = {
    {"VI", 0, 0, TILE_OFFLINE}, {"VPSS", 0, 0, TILE_OFFLINE},
    {"VO", 0, 0, TILE_OFFLINE}, {"WBC", 0, 0, TILE_OFFLINE},
    {"RGA", 0, 0, TILE_OFFLINE},
    {"RESIZE_RGA", 0, 0, TILE_OFFLINE}, {"CSC_RGA", 0, 0, TILE_OFFLINE},
    {"CSC_CL", 0, 0, TILE_OFFLINE}, {"OSD", 0, 0, TILE_OFFLINE},
    {"CLAHE", 0, 0, TILE_OFFLINE}, {"RETINEX", 0, 0, TILE_OFFLINE},
    {"CAP_DEHAZE", 0, 0, TILE_OFFLINE}, {"DCP_FAST_DEHAZE", 0, 0, TILE_OFFLINE},
    {"THERMAL", 0, 0, TILE_OFFLINE}, {"CONV_CL", 0, 0, TILE_OFFLINE},
    {"TRANSFORM", 0, 0, TILE_OFFLINE}, {"BLEND_PYR", 0, 0, TILE_OFFLINE},
    {"EDOF_CL", 0, 0, TILE_OFFLINE}, {"EXPOSURE_FUSION_CL", 0, 0, TILE_OFFLINE},
    {"MCF_FUSION_CL", 0, 0, TILE_OFFLINE},
    {"DUALVIEW", 0, 0, TILE_OFFLINE}, {"STEREO_3D", 0, 0, TILE_OFFLINE},
    {"VMIX", 0, 0, TILE_OFFLINE}, {"VMIX_RGA", 0, 0, TILE_OFFLINE},
    {"PANO", 0, 0, TILE_OFFLINE}, {"AVM", 0, 0, TILE_OFFLINE},
    {"SVM3D", 0, 0, TILE_OFFLINE}, {"NPU", 0, 0, TILE_OFFLINE},
    {"VENC", 0, 0, TILE_OFFLINE}, {"VDEC", 0, 0, TILE_OFFLINE},
    {"RTSP_SEND", 0, 0, TILE_OFFLINE}, {"RTSP_RECV", 0, 0, TILE_OFFLINE},
    {"PIC_IO", 0, 0, TILE_OFFLINE}, {"LICENSE", 0, 0, TILE_OFFLINE},
};

static const char *g_module_pages[] = {
    "VI", "VPSS", "VO", "WBC", "RGA", "RESIZE_RGA", "CSC_RGA", "CSC_CL", "OSD",
    "CLAHE", "RETINEX", "CAP_DEHAZE", "DCP_FAST_DEHAZE", "THERMAL", "CONV_CL",
    "TRANSFORM", "VMIX", "EDOF_CL", "MCF_FUSION_CL", "DUALVIEW", "STEREO_3D", "PANO",
};

static const char *g_default_pages[] = {
    "VI", "VPSS", "VMIX", "OSD", "RGA", "RESIZE_RGA", "CSC_CL", "CLAHE", "RETINEX",
    "CAP_DEHAZE", "CONV_CL", "TRANSFORM", "THERMAL", "EDOF_CL", "MCF_FUSION_CL",
    "PANO",
};

static const char *g_engineering_pages[] = {
    "VI", "VPSS", "VO", "WBC", "OSD", "RESIZE_RGA", "THERMAL", "EDOF_CL",
    "MCF_FUSION_CL", "RGA", "CSC_RGA", "CSC_CL", "CLAHE", "RETINEX",
    "CAP_DEHAZE", "CONV_CL", "TRANSFORM", "VMIX", "STEREO_3D", "PANO",
};

typedef struct {
    const char *name;
    const char *profile;
    const char *const *pages;
    int total_pages;
    int rotate_seconds;
} demo_loop_t;

static const demo_loop_t g_customer_loop = {
    "customer demo",
    "CUSTOMER",
    g_default_pages,
    (int)ARRAY_SIZE(g_default_pages),
    CUSTOMER_ROTATE_SECONDS,
};

static const demo_loop_t g_engineering_loop = {
    "engineering demo",
    "ENGINEERING",
    g_engineering_pages,
    (int)ARRAY_SIZE(g_engineering_pages),
    PAGE_ROTATE_SECONDS,
};

static int loop_page_rotate_seconds(const demo_loop_t *loop, const char *module) {
    int seconds = loop ? loop->rotate_seconds : CUSTOMER_ROTATE_SECONDS;
    if (module && strcasecmp(module, "RGA") == 0) {
        int full_cycle = RGA_OP_SECONDS * RGA_DEMO_OP_COUNT;
        if (seconds < full_cycle) seconds = full_cycle;
    }
    return seconds;
}

static const char *g_vpss_tile_labels[VPSS_DEMO_OUTPUTS] = {
    "FULL SCALE", "MOVING CROP", "FLIP H/V", "ZOOM CROP",
};

static loop_asset_t g_loop_assets[] = {
    {"THERMAL", "RKTOHI THERMAL DEMO", {
        "/userdata/rktohi/demo/thermal/1.png",
        "/userdata/rktohi/demo/thermal/2.png",
    }, 2, {{0}}, 0},
    {"TRANSFORM", "TRANSFORM LOOP", {
        "assets/loop/transform/transform_1.png",
    }, 1, {{0}}, 0},
    {"VMIX", "VMIX LOOP", {
        "assets/loop/vmix/vmix_1.png",
        "assets/loop/vmix/vmix_2.png",
    }, 2, {{0}}, 0},
    {"AVM", "AVM INPUTS", {
        "assets/loop/avm_inputs/src_1.jpg",
        "assets/loop/avm_inputs/src_2.jpg",
        "assets/loop/avm_inputs/src_3.jpg",
        "assets/loop/avm_inputs/src_4.jpg",
    }, 4, {{0}}, 0},
};

static edof_pair_t g_edof_pairs[] = {
    {"assets/loop/edof/mfi_whu/0002/a.jpg",
     "assets/loop/edof/mfi_whu/0002/b.jpg",
     "assets/loop/edof/mfi_whu/0002/fused.png", {0}, {0}, {0}, 0},
    {"assets/loop/edof/mfi_whu/0019/a.jpg",
     "assets/loop/edof/mfi_whu/0019/b.jpg",
     "assets/loop/edof/mfi_whu/0019/fused.png", {0}, {0}, {0}, 0},
    {"assets/loop/edof/mfi_whu/0041/a.jpg",
     "assets/loop/edof/mfi_whu/0041/b.jpg",
     "assets/loop/edof/mfi_whu/0041/fused.png", {0}, {0}, {0}, 0},
};

static mcf_pair_t g_mcf_pairs[] = {
    {"street_0001",
     "assets/loop/mcf_fusion/street_0001/color.jpg",
     "assets/loop/mcf_fusion/street_0001/mono.jpg",
     "assets/loop/mcf_fusion/street_0001/reference.jpg", {0}, {0}, {0}, 0},
    {"urban_0001",
     "assets/loop/mcf_fusion/urban_0001/color.jpg",
     "assets/loop/mcf_fusion/urban_0001/mono.jpg",
     "assets/loop/mcf_fusion/urban_0001/reference.jpg", {0}, {0}, {0}, 0},
    {"country_0002",
     "assets/loop/mcf_fusion/country_0002/color.jpg",
     "assets/loop/mcf_fusion/country_0002/mono.jpg",
     "assets/loop/mcf_fusion/country_0002/reference.jpg", {0}, {0}, {0}, 0},
};

static pano_sample_t g_pano_sample = {
    "assets/loop/pano/sample2/calib_file.pto",
    {
        "assets/loop/pano/sample2/camera_0.jpg",
        "assets/loop/pano/sample2/camera_1.jpg",
        "assets/loop/pano/sample2/camera_2.jpg",
        "assets/loop/pano/sample2/camera_3.jpg",
        "assets/loop/pano/sample2/camera_4.jpg",
        "assets/loop/pano/sample2/camera_5.jpg",
    },
    {{0}},
    {NULL},
    PANO_INPUT_COUNT,
    0,
    0,
    0,
};

static void on_signal(int sig) {
    (void)sig;
    if (++g_signal_count > 1) _exit(128 + sig);
    g_running = 0;
    alarm(5);
}

static int wait_child_nonblocking(pid_t child, int *status) {
    pid_t r;
    do {
        r = waitpid(child, status, WNOHANG);
    } while (r < 0 && errno == EINTR);
    return r == child ? 1 : (r == 0 ? 0 : -1);
}

static int nav_key_reader_open(nav_key_reader_t *reader) {
    if (!reader) return 0;
    memset(reader, 0, sizeof(*reader));

    DIR *dir = opendir("/dev/input");
    if (!dir) return 0;

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "event", 5) != 0) continue;
        if (reader->count >= NAV_KEY_MAX_FDS) break;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            reader->fds[reader->count++] = fd;
        }
    }
    closedir(dir);
    return reader->count;
}

static void nav_key_reader_close(nav_key_reader_t *reader) {
    if (!reader) return;
    for (int i = 0; i < reader->count; ++i) {
        if (reader->fds[i] >= 0) close(reader->fds[i]);
        reader->fds[i] = -1;
    }
    reader->count = 0;
}

static nav_key_action_t nav_key_reader_poll(nav_key_reader_t *reader) {
    if (!reader) return NAV_KEY_NONE;
    for (int i = 0; i < reader->count; ++i) {
        for (;;) {
            struct input_event ev;
            ssize_t n = read(reader->fds[i], &ev, sizeof(ev));
            if (n == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_KEY && ev.value == 1) {
                    if (ev.code == KEY_VOLUMEUP) return NAV_KEY_NEXT;
                    if (ev.code == KEY_VOLUMEDOWN) return NAV_KEY_PREV;
                }
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;
        }
    }
    return NAV_KEY_NONE;
}

static int wrap_page_index(int page, int total) {
    if (total <= 0) return 0;
    while (page < 0) page += total;
    return page % total;
}

static void set_loop_child_env(int page, int total, int manual_mode, const char *profile) {
    char value[32];
    snprintf(value, sizeof(value), "%d", page + 1);
    setenv(NAV_ENV_PAGE, value, 1);
    snprintf(value, sizeof(value), "%d", total);
    setenv(NAV_ENV_TOTAL, value, 1);
    setenv(NAV_ENV_MODE, manual_mode ? "MANUAL" : "AUTO", 1);
    setenv(NAV_ENV_PROFILE, profile && profile[0] ? profile : "DEFAULT", 1);
}

static int wait_default_only_child(pid_t child, int seconds,
                                   nav_key_reader_t *nav_reader,
                                   nav_key_action_t *nav_action) {
    int status = 0;
    int elapsed_ms = 0;
    int limit_ms = seconds > 0 ? seconds * 1000 : -1;
    if (nav_action) *nav_action = NAV_KEY_NONE;

    while (g_running && (limit_ms < 0 || elapsed_ms < limit_ms)) {
        int done = wait_child_nonblocking(child, &status);
        if (done != 0) return done > 0 ? status : -1;
        nav_key_action_t action = nav_key_reader_poll(nav_reader);
        if (action != NAV_KEY_NONE) {
            if (nav_action) *nav_action = action;
            break;
        }
        usleep(100000);
        if (limit_ms > 0) elapsed_ms += 100;
    }

    if (!g_running) {
        for (int i = 0; i < 20; ++i) {
            int done = wait_child_nonblocking(child, &status);
            if (done != 0) return done > 0 ? status : -1;
            usleep(100000);
        }
    }

    kill(child, SIGINT);
    for (int i = 0; i < 50; ++i) {
        int done = wait_child_nonblocking(child, &status);
        if (done != 0) return done > 0 ? status : -1;
        usleep(100000);
    }

    kill(child, SIGTERM);
    do {
        if (waitpid(child, &status, 0) == child) return status;
    } while (errno == EINTR);
    return -1;
}

static int run_default_only_loop(const char *argv0, int rotate_main, const demo_loop_t *loop) {
    char self[PATH_MAX];
    if (!loop) loop = &g_customer_loop;
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = '\0';
    } else {
        snprintf(self, sizeof(self), "%s", argv0 && argv0[0] ? argv0 : "./alldemo");
    }

    printf("alldemo %s reuses --only module pages. Ctrl+C to stop.\n", loop->name);

    nav_key_reader_t nav_reader = {0};
    int nav_count = rotate_main ? nav_key_reader_open(&nav_reader) : 0;
    if (rotate_main) {
        if (nav_count > 0) {
            (void)nav_key_reader_poll(&nav_reader);
            printf("navigation keys: KEY_VOLUMEUP=next, KEY_VOLUMEDOWN=previous; first key switches to manual mode.\n");
        } else {
            printf("navigation keys: no readable /dev/input/event* device, auto loop only.\n");
        }
    }

    int page = 0;
    int manual_mode = 0;
    int total_pages = loop->total_pages;
    while (g_running) {
        int page_index = rotate_main ? wrap_page_index(page, total_pages) : 0;
        const char *module = loop->pages[page_index];
        pid_t child = fork();
        if (child < 0) {
            perror("fork --only page");
            nav_key_reader_close(&nav_reader);
            return 1;
        }
        if (child == 0) {
            set_loop_child_env(page_index, total_pages, manual_mode, loop->profile);
            execl(self, self, "--only", module, (char *)NULL);
            fprintf(stderr, "exec %s --only %s failed: %s\n", self, module, strerror(errno));
            _exit(127);
        }

        printf("%s page: --only %s (%s)\n", loop->name, module, manual_mode ? "manual" : "auto");
        nav_key_action_t nav_action = NAV_KEY_NONE;
        int page_seconds = rotate_main && !manual_mode ?
            loop_page_rotate_seconds(loop, module) : -1;
        (void)wait_default_only_child(child,
                                      page_seconds,
                                      rotate_main ? &nav_reader : NULL,
                                      &nav_action);
        if (!rotate_main) break;
        if (nav_action != NAV_KEY_NONE) {
            manual_mode = 1;
            page = page_index + (nav_action == NAV_KEY_NEXT ? 1 : -1);
        } else if (!manual_mode) {
            page = page_index + 1;
        } else {
            break;
        }
    }
    nav_key_reader_close(&nav_reader);
    return 0;
}

static int path_readable(const char *path) {
    return access(path, R_OK) == 0;
}

static int tcp_port_free(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    int ok = bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);
    return ok;
}

static int expected_loop_asset_count(void) {
    int total = 0;
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        total += g_loop_assets[i].path_count;
    }
    return total;
}

static void collect_health(int loaded_assets) {
    memset(&g_health, 0, sizeof(g_health));
    g_health.license_ok = path_readable(LICENSE_PATH);
    g_health.camera_node_ok = access(CAMERA_DEVICE, F_OK) == 0;
    g_health.drm_ok = access("/dev/dri/card0", F_OK) == 0;
    g_health.media_lib_ok = path_readable("lib/libmedia.so") && path_readable("lib/libmedia.a");
    g_health.rtsp_port_free = tcp_port_free(RTSP_PORT);
    g_health.npu_model_ok = path_readable("assets/npu/yolov5s-640-640.rknn");
    g_health.avm_lut_ok = path_readable("assets/avm/avm_blend.lut");
    g_health.pano_calib_ok = path_readable(g_pano_sample.pto_path);
    g_health.svm_assets_ok = path_readable("assets/svm3d/svm_3d_assets.json");
    g_health.loop_loaded = loaded_assets;
    g_health.loop_expected = expected_loop_asset_count();
}

static int print_check(const char *name, int ok, const char *detail) {
    printf("[%s] %-14s %s\n", ok ? "OK" : "FAIL", name, detail ? detail : "");
    return ok ? 0 : 1;
}

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void rgb_to_yuv(uint8_t r, uint8_t g, uint8_t b, uint8_t *y, uint8_t *u, uint8_t *v) {
    *y = clamp_u8(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    *u = clamp_u8(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
    *v = clamp_u8(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
}

static void yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v,
                       uint8_t *r, uint8_t *g, uint8_t *b) {
    int c = (int)y - 16;
    int d = (int)u - 128;
    int e = (int)v - 128;
    if (c < 0) c = 0;
    *r = clamp_u8((298 * c + 409 * e + 128) >> 8);
    *g = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = clamp_u8((298 * c + 516 * d + 128) >> 8);
}

static void nv12_to_rgb_frame(const uint8_t *src, uint8_t *dst,
                              int width, int height, int stride) {
    if (!src || !dst || width <= 0 || height <= 0 || stride <= 0) return;
    const uint8_t *y_plane = src;
    const uint8_t *uv_plane = src + stride * height;
    for (int y = 0; y < height; ++y) {
        const uint8_t *y_row = y_plane + y * stride;
        const uint8_t *uv_row = uv_plane + (y / 2) * stride;
        uint8_t *rgb_row = dst + (size_t)y * width * 3;
        for (int x = 0; x < width; ++x) {
            uint8_t yy = y_row[x];
            uint8_t uu = uv_row[x & ~1];
            uint8_t vv = uv_row[(x & ~1) + 1];
            yuv_to_rgb(yy, uu, vv, &rgb_row[x * 3], &rgb_row[x * 3 + 1], &rgb_row[x * 3 + 2]);
        }
    }
}

static void nv12_to_rgba_frame(const uint8_t *src, uint8_t *dst,
                               int width, int height, int stride) {
    if (!src || !dst || width <= 0 || height <= 0 || stride <= 0) return;
    const uint8_t *y_plane = src;
    const uint8_t *uv_plane = src + stride * height;
    for (int y = 0; y < height; ++y) {
        const uint8_t *y_row = y_plane + y * stride;
        const uint8_t *uv_row = uv_plane + (y / 2) * stride;
        uint8_t *rgba_row = dst + (size_t)y * width * 4;
        for (int x = 0; x < width; ++x) {
            uint8_t yy = y_row[x];
            uint8_t uu = uv_row[x & ~1];
            uint8_t vv = uv_row[(x & ~1) + 1];
            yuv_to_rgb(yy, uu, vv,
                       &rgba_row[x * 4],
                       &rgba_row[x * 4 + 1],
                       &rgba_row[x * 4 + 2]);
            rgba_row[x * 4 + 3] = 255;
        }
    }
}

static void write_le16(FILE *fp, uint16_t v) {
    fputc((int)(v & 0xff), fp);
    fputc((int)((v >> 8) & 0xff), fp);
}

static void write_le32(FILE *fp, uint32_t v) {
    fputc((int)(v & 0xff), fp);
    fputc((int)((v >> 8) & 0xff), fp);
    fputc((int)((v >> 16) & 0xff), fp);
    fputc((int)((v >> 24) & 0xff), fp);
}

static int write_nv12_bmp(const char *path, const uint8_t *src,
                          int width, int height, int stride) {
    if (!path || !src || width <= 0 || height <= 0 || stride < width) return -1;

    int row_bytes = (width * 3 + 3) & ~3;
    uint32_t pixel_bytes = (uint32_t)row_bytes * (uint32_t)height;
    uint32_t file_bytes = 14u + 40u + pixel_bytes;
    uint8_t *row = malloc((size_t)row_bytes);
    if (!row) return -1;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(row);
        return -1;
    }

    fputc('B', fp);
    fputc('M', fp);
    write_le32(fp, file_bytes);
    write_le16(fp, 0);
    write_le16(fp, 0);
    write_le32(fp, 14u + 40u);

    write_le32(fp, 40u);
    write_le32(fp, (uint32_t)width);
    write_le32(fp, (uint32_t)height);
    write_le16(fp, 1);
    write_le16(fp, 24);
    write_le32(fp, 0);
    write_le32(fp, pixel_bytes);
    write_le32(fp, 2835);
    write_le32(fp, 2835);
    write_le32(fp, 0);
    write_le32(fp, 0);

    const uint8_t *y_plane = src;
    const uint8_t *uv_plane = src + (size_t)stride * height;
    for (int out_y = 0; out_y < height; ++out_y) {
        int y = height - 1 - out_y;
        const uint8_t *y_row = y_plane + (size_t)y * stride;
        const uint8_t *uv_row = uv_plane + (size_t)(y / 2) * stride;
        memset(row, 0, (size_t)row_bytes);
        for (int x = 0; x < width; ++x) {
            uint8_t r, g, b;
            int uv_x = x & ~1;
            yuv_to_rgb(y_row[x], uv_row[uv_x], uv_row[uv_x + 1], &r, &g, &b);
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
        if (fwrite(row, 1, (size_t)row_bytes, fp) != (size_t)row_bytes) {
            fclose(fp);
            free(row);
            return -1;
        }
    }

    int ok = ferror(fp) ? -1 : 0;
    fclose(fp);
    free(row);
    return ok;
}

static int write_argb_bmp(const char *path, const uint8_t *src,
                          int width, int height, int stride) {
    if (!path || !src || width <= 0 || height <= 0 || stride < width * 4) return -1;

    int row_bytes = (width * 3 + 3) & ~3;
    uint32_t pixel_bytes = (uint32_t)row_bytes * (uint32_t)height;
    uint32_t file_bytes = 14u + 40u + pixel_bytes;
    uint8_t *row = malloc((size_t)row_bytes);
    if (!row) return -1;

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(row);
        return -1;
    }

    fputc('B', fp);
    fputc('M', fp);
    write_le32(fp, file_bytes);
    write_le16(fp, 0);
    write_le16(fp, 0);
    write_le32(fp, 14u + 40u);

    write_le32(fp, 40u);
    write_le32(fp, (uint32_t)width);
    write_le32(fp, (uint32_t)height);
    write_le16(fp, 1);
    write_le16(fp, 24);
    write_le32(fp, 0);
    write_le32(fp, pixel_bytes);
    write_le32(fp, 2835);
    write_le32(fp, 2835);
    write_le32(fp, 0);
    write_le32(fp, 0);

    for (int out_y = 0; out_y < height; ++out_y) {
        int y = height - 1 - out_y;
        const uint8_t *src_row = src + (size_t)y * stride;
        memset(row, 0, (size_t)row_bytes);
        for (int x = 0; x < width; ++x) {
            const uint8_t *px = src_row + x * 4;
            row[x * 3 + 0] = px[0];
            row[x * 3 + 1] = px[1];
            row[x * 3 + 2] = px[2];
        }
        if (fwrite(row, 1, (size_t)row_bytes, fp) != (size_t)row_bytes) {
            fclose(fp);
            free(row);
            return -1;
        }
    }

    int ok = ferror(fp) ? -1 : 0;
    fclose(fp);
    free(row);
    return ok;
}

static void maybe_capture_module_vo_frame(const char *only_tile, int frame,
                                          const uint8_t *nv12, int stride) {
    if (!only_tile || !nv12) return;
    if (frame <= 0) return;
    const char *prefix = NULL;
    if (strcasecmp(only_tile, "THERMAL") == 0) {
        prefix = "thermal";
    } else if (strcasecmp(only_tile, "EDOF_CL") == 0) {
        prefix = "edof_cl";
    } else if (strcasecmp(only_tile, "MCF_FUSION_CL") == 0) {
        prefix = "mcf_fusion_cl";
    } else if (strcasecmp(only_tile, "PANO") == 0) {
        prefix = "pano";
    } else if (strcasecmp(only_tile, "RETINEX") == 0) {
        prefix = "retinex";
    } else if (strcasecmp(only_tile, "CAP_DEHAZE") == 0) {
        prefix = "cap_dehaze";
    } else if (strcasecmp(only_tile, "CONV_CL") == 0) {
        prefix = "conv_cl";
    } else {
        return;
    }
    if ((frame % (FPS * VO_CAPTURE_SECONDS)) != 0) return;

    const char *dir = "/userdata/alldemo/vo_captures";
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "%s VO capture mkdir failed: %s\n", only_tile, strerror(errno));
        return;
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/%s_vo_%06d.bmp", dir, prefix, frame);
    if (write_nv12_bmp(path, nv12, SCREEN_W, SCREEN_H, stride) == 0) {
        printf("%s VO capture: %s\n", only_tile, path);
    } else {
        fprintf(stderr, "%s VO capture failed: %s\n", only_tile, path);
    }
}

static void maybe_capture_vo_channel_frame(const char *only_tile, int frame, int stride) {
    if (!only_tile) return;
    int is_retinex = strcasecmp(only_tile, "RETINEX") == 0;
    int is_conv_cl = strcasecmp(only_tile, "CONV_CL") == 0;
    int is_transform = strcasecmp(only_tile, "TRANSFORM") == 0;
    int is_stereo = strcasecmp(only_tile, "STEREO_3D") == 0;
    int is_vi = strcasecmp(only_tile, "VI") == 0;
    int is_vpss = strcasecmp(only_tile, "VPSS") == 0;
    int is_osd = strcasecmp(only_tile, "OSD") == 0;
    int is_rga = strcasecmp(only_tile, "RGA") == 0;
    int is_resize_rga = strcasecmp(only_tile, "RESIZE_RGA") == 0;
    int is_csc_cl = strcasecmp(only_tile, "CSC_CL") == 0;
    int is_clahe = strcasecmp(only_tile, "CLAHE") == 0;
    int is_cap_dehaze = strcasecmp(only_tile, "CAP_DEHAZE") == 0;
    int is_dcp_dehaze = strcasecmp(only_tile, "DCP_FAST_DEHAZE") == 0;
    if (!is_retinex && !is_conv_cl && !is_transform && !is_stereo && !is_vi && !is_vpss && !is_osd && !is_rga && !is_resize_rga && !is_csc_cl && !is_clahe && !is_cap_dehaze && !is_dcp_dehaze) return;
    if (frame <= 0) return;
    if ((frame % (FPS * VO_CAPTURE_SECONDS)) != 0) return;

    const char *label = "RETINEX";
    const char *prefix = "retinex";
    if (is_conv_cl) {
        label = "CONV_CL";
        prefix = "conv_cl";
    } else if (is_transform) {
        label = "TRANSFORM";
        prefix = "transform";
    } else if (is_stereo) {
        label = "STEREO_3D";
        prefix = "stereo_3d";
    } else if (is_vi) {
        label = "VI";
        prefix = "vi";
    } else if (is_vpss) {
        label = "VPSS";
        prefix = "vpss";
    } else if (is_osd) {
        label = "OSD";
        prefix = "osd";
    } else if (is_rga) {
        label = "RGA";
        prefix = "rga";
    } else if (is_resize_rga) {
        label = "RESIZE_RGA";
        prefix = "resize_rga";
    } else if (is_csc_cl) {
        label = "CSC_CL";
        prefix = "csc_cl";
    } else if (is_clahe) {
        label = "CLAHE";
        prefix = "clahe";
    } else if (is_cap_dehaze) {
        label = "CAP_DEHAZE";
        prefix = "cap_dehaze";
    } else if (is_dcp_dehaze) {
        label = "DCP_FAST_DEHAZE";
        prefix = "dcp_fast_dehaze";
    }
    MEDIA_BUFFER buf = {-1, -1};
    int source = 0;
    if (MEDIA_VO_GetFrame(0, 0, &buf, 20) == 0) {
        source = 1;
    } else if (is_csc_cl && MEDIA_RESIZE_RGA_GetFrame(LIVE_RESIZE_GRP, &buf, 20) == 0) {
        source = 4;
    } else if (MEDIA_OSD_GetFrame(DISPLAY_OSD_GRP, &buf, 20) == 0) {
        source = 2;
    } else if (MEDIA_VMIX_GetFrame(DISPLAY_VMIX_GRP, &buf, 20) == 0) {
        source = 3;
    } else {
        fprintf(stderr, "%s VO capture no output frame at %d\n", label, frame);
        return;
    }

    void *addr = NULL;
    size_t size = 0;
    int fd = -1;
    int cpu_access = MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_READ) == 0;
    if (cpu_access &&
        MEDIA_POOL_GetFd(buf, &fd, &size) == 0 && fd >= 0 && size > 0 &&
        (addr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0)) != MAP_FAILED) {
        const char *dir = "/userdata/alldemo/vo_captures";
        if (mkdir(dir, 0755) == 0 || errno == EEXIST) {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s_vo_%06d.bmp", dir, prefix, frame);
            if (is_csc_cl) {
                int cap_w = (source == 1 || source == 4) ? CSC_CL_VIEW_W : CAM_W;
                int cap_h = (source == 1 || source == 4) ? CSC_CL_VIEW_H : CAM_H;
                int cap_stride = (source == 1 || source == 4) ? CSC_CL_VIEW_STRIDE : CAM_W * 4;
                if (size >= (size_t)cap_stride * cap_h &&
                    write_argb_bmp(path, (const uint8_t *)addr, cap_w, cap_h, cap_stride) == 0) {
                    printf("%s VO capture: %s source=%s\n", label, path,
                           source == 1 ? "VO" : (source == 2 ? "OSD" : (source == 3 ? "VMIX" : "RESIZE_RGA")));
                } else {
                    fprintf(stderr, "%s VO capture failed: %s\n", label, path);
                }
            } else if (!is_csc_cl && size >= (size_t)stride * SCREEN_H * 3 / 2 &&
                write_nv12_bmp(path, (const uint8_t *)addr, SCREEN_W, SCREEN_H, stride) == 0) {
                printf("%s VO capture: %s source=%s\n", label, path,
                       source == 1 ? "VO" : (source == 2 ? "OSD" : "VMIX"));
            } else {
                fprintf(stderr, "%s VO capture failed: %s\n", label, path);
            }
        } else {
            fprintf(stderr, "%s VO capture mkdir failed: %s\n", label, strerror(errno));
        }
        munmap(addr, size);
    }
    if (cpu_access) (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
    if (source == 1) {
        MEDIA_VO_ReleaseFrame(0, 0, buf);
    } else if (source == 2) {
        MEDIA_OSD_ReleaseFrame(DISPLAY_OSD_GRP, buf);
    } else if (source == 3) {
        MEDIA_VMIX_ReleaseFrame(DISPLAY_VMIX_GRP, buf);
    } else if (source == 4) {
        MEDIA_RESIZE_RGA_ReleaseFrame(LIVE_RESIZE_GRP, buf);
    }
}

static int load_png_rgb(const char *path, image_asset_t *out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    uint8_t sig[8];
    if (fread(sig, 1, sizeof(sig), fp) != sizeof(sig) || png_sig_cmp(sig, 0, sizeof(sig))) {
        fclose(fp);
        return -1;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return -1;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, sizeof(sig));
    png_read_info(png, info);

    int width = (int)png_get_image_width(png, info);
    int height = (int)png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (color_type & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png);

    png_read_update_info(png, info);
    int channels = png_get_channels(png, info);
    png_size_t rowbytes = png_get_rowbytes(png, info);
    uint8_t *raw = malloc((size_t)rowbytes * height);
    png_bytep *rows = malloc(sizeof(png_bytep) * (size_t)height);
    uint8_t *rgb = malloc((size_t)width * height * 3);
    if (!raw || !rows || !rgb || width <= 0 || height <= 0) {
        free(raw);
        free(rows);
        free(rgb);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    for (int y = 0; y < height; ++y) rows[y] = raw + (size_t)y * rowbytes;
    png_read_image(png, rows);
    for (int y = 0; y < height; ++y) {
        uint8_t *src = rows[y];
        uint8_t *dst = rgb + (size_t)y * width * 3;
        for (int x = 0; x < width; ++x) {
            if (channels >= 3) {
                dst[x * 3 + 0] = src[x * channels + 0];
                dst[x * 3 + 1] = src[x * channels + 1];
                dst[x * 3 + 2] = src[x * channels + 2];
            } else {
                dst[x * 3 + 0] = src[x];
                dst[x * 3 + 1] = src[x];
                dst[x * 3 + 2] = src[x];
            }
        }
    }

    free(raw);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    out->width = width;
    out->height = height;
    out->rgb = rgb;
    return 0;
}

static int load_jpeg_rgb(const char *path, image_asset_t *out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return -1;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int width = (int)cinfo.output_width;
    int height = (int)cinfo.output_height;
    int channels = (int)cinfo.output_components;
    uint8_t *rgb = malloc((size_t)width * height * 3);
    uint8_t *row = malloc((size_t)width * channels);
    if (!rgb || !row || width <= 0 || height <= 0 || channels < 3) {
        free(rgb);
        free(row);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return -1;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW rows[1] = {row};
        JDIMENSION y = cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, rows, 1);
        memcpy(rgb + (size_t)y * width * 3, row, (size_t)width * 3);
    }

    free(row);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    out->width = width;
    out->height = height;
    out->rgb = rgb;
    return 0;
}

static int load_image_rgb(const char *path, image_asset_t *out) {
    const char *ext = strrchr(path, '.');
    memset(out, 0, sizeof(*out));
    if (ext && strcasecmp(ext, ".png") == 0) return load_png_rgb(path, out);
    if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) {
        return load_jpeg_rgb(path, out);
    }
    return -1;
}

static void fill_rect_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                           uint8_t r, uint8_t g, uint8_t b) {
    uint8_t yy, uu, vv;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    uint8_t *yp = dst;
    uint8_t *uv = dst + stride * SCREEN_H;
    for (int row = y; row < y + h; ++row) {
        memset(yp + row * stride + x, yy, (size_t)w);
    }
    int uv_y0 = y / 2;
    int uv_y1 = (y + h + 1) / 2;
    int uv_x0 = x & ~1;
    int uv_x1 = (x + w + 1) & ~1;
    for (int row = uv_y0; row < uv_y1; ++row) {
        uint8_t *p = uv + row * stride + uv_x0;
        for (int col = uv_x0; col < uv_x1; col += 2) {
            p[0] = uu;
            p[1] = vv;
            p += 2;
        }
    }
}

static void fill_rect_nv12_frame(uint8_t *dst, int frame_w, int frame_h, int stride,
                                 int x, int y, int w, int h,
                                 uint8_t r, uint8_t g, uint8_t b) {
    uint8_t yy, uu, vv;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > frame_w) w = frame_w - x;
    if (y + h > frame_h) h = frame_h - y;
    if (w <= 0 || h <= 0) return;

    uint8_t *yp = dst;
    uint8_t *uv = dst + stride * frame_h;
    for (int row = y; row < y + h; ++row) {
        memset(yp + row * stride + x, yy, (size_t)w);
    }
    int uv_y0 = y / 2;
    int uv_y1 = (y + h + 1) / 2;
    int uv_x0 = x & ~1;
    int uv_x1 = (x + w + 1) & ~1;
    for (int row = uv_y0; row < uv_y1; ++row) {
        uint8_t *p = uv + row * stride + uv_x0;
        for (int col = uv_x0; col < uv_x1; col += 2) {
            p[0] = uu;
            p[1] = vv;
            p += 2;
        }
    }
}

static void draw_nv12_to_frame(uint8_t *dst, int dframe_w, int dframe_h, int dstride,
                               int dx, int dy, int dw, int dh,
                               const uint8_t *src, int sw, int sh, int sstride) {
    if (!dst || !src || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    if (dx < 0) { dw += dx; dx = 0; }
    if (dy < 0) { dh += dy; dy = 0; }
    if (dx + dw > dframe_w) dw = dframe_w - dx;
    if (dy + dh > dframe_h) dh = dframe_h - dy;
    if (dw <= 0 || dh <= 0) return;

    const uint8_t *sy = src;
    const uint8_t *suv = src + sstride * sh;
    uint8_t *dy_base = dst;
    uint8_t *duv_base = dst + dstride * dframe_h;

    for (int y = 0; y < dh; ++y) {
        int src_y = y * sh / dh;
        uint8_t *drow = dy_base + (dy + y) * dstride + dx;
        const uint8_t *srow = sy + src_y * sstride;
        for (int x = 0; x < dw; ++x) {
            drow[x] = srow[x * sw / dw];
        }
    }
    for (int y = 0; y < dh / 2; ++y) {
        int src_y = (y * 2) * sh / dh;
        const uint8_t *srow = suv + (src_y / 2) * sstride;
        uint8_t *drow = duv_base + ((dy / 2) + y) * dstride + (dx & ~1);
        for (int x = 0; x < dw; x += 2) {
            int src_x = ((x * sw / dw) & ~1);
            drow[x] = srow[src_x];
            drow[x + 1] = srow[src_x + 1];
        }
    }
}

static loop_asset_t *find_loop_asset(const char *tile_name) {
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        if (strcmp(g_loop_assets[i].tile_name, tile_name) == 0) return &g_loop_assets[i];
    }
    return NULL;
}

static void stroke_rect_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                             int thick, uint8_t r, uint8_t g, uint8_t b);
static void draw_text(uint8_t *dst, int stride, int x, int y, const char *text,
                      int scale, uint8_t r, uint8_t g, uint8_t b);
static void draw_utf8_text(uint8_t *dst, int stride, int x, int y, const char *text,
                           int pixel_size, uint8_t r, uint8_t g, uint8_t b);

static void draw_rgb_image_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                                const image_asset_t *img) {
    if (!img || !img->rgb || img->width <= 0 || img->height <= 0 || w <= 0 || h <= 0) return;

    int out_w = w;
    int out_h = (int)((int64_t)img->height * w / img->width);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)img->width * h / img->height);
    }
    if (out_w <= 0 || out_h <= 0) return;

    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;
    uint8_t *yp = dst;
    uint8_t *uv = dst + stride * SCREEN_H;

    for (int dy = 0; dy < out_h; ++dy) {
        int sy = dy * img->height / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = yp + yy * stride;
        for (int dx = 0; dx < out_w; ++dx) {
            int sx = dx * img->width / out_w;
            int xx = ox + dx;
            if (xx < 0 || xx >= SCREEN_W) continue;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yv, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yv, &u, &v);
            (void)u;
            (void)v;
            drow[xx] = yv;
        }
    }

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy = dy * img->height / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = uv + (yy / 2) * stride;
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx = dx * img->width / out_w;
            int xx = (ox + dx) & ~1;
            if (xx < 0 || xx + 1 >= SCREEN_W) continue;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yv, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yv, &u, &v);
            (void)yv;
            drow[xx] = u;
            drow[xx + 1] = v;
        }
    }
}

static void thermal_palette(float t, const uint8_t stops[][3], int count,
                            uint8_t *r, uint8_t *g, uint8_t *b) {
    if (count <= 0) {
        *r = *g = *b = 0;
        return;
    }
    if (t <= 0.0f) {
        *r = stops[0][0];
        *g = stops[0][1];
        *b = stops[0][2];
        return;
    }
    if (t >= 1.0f) {
        *r = stops[count - 1][0];
        *g = stops[count - 1][1];
        *b = stops[count - 1][2];
        return;
    }

    float scaled = t * (float)(count - 1);
    int idx = (int)scaled;
    float f = scaled - (float)idx;
    const uint8_t *a = stops[idx];
    const uint8_t *c = stops[idx + 1];
    *r = (uint8_t)((float)a[0] + ((float)c[0] - (float)a[0]) * f);
    *g = (uint8_t)((float)a[1] + ((float)c[1] - (float)a[1]) * f);
    *b = (uint8_t)((float)a[2] + ((float)c[2] - (float)a[2]) * f);
}

static void thermal_color_for_mode(int mode, uint8_t value,
                                   uint8_t *r, uint8_t *g, uint8_t *b) {
    float t = (float)value / 255.0f;
    static const uint8_t rainbow[][3] = {
        {0, 0, 70}, {0, 70, 210}, {0, 220, 170}, {220, 230, 0}, {255, 80, 0}, {255, 255, 255}
    };
    static const uint8_t rainbow1[][3] = {
        {22, 0, 92}, {0, 136, 255}, {0, 255, 128}, {255, 220, 0}, {255, 32, 32}
    };
    static const uint8_t rainbow2[][3] = {
        {0, 0, 0}, {0, 0, 160}, {0, 255, 255}, {255, 255, 0}, {255, 0, 0}
    };
    static const uint8_t rainbow3[][3] = {
        {0, 0, 80}, {60, 0, 160}, {0, 160, 255}, {0, 255, 70}, {255, 230, 0}, {255, 255, 255}
    };
    static const uint8_t iron[][3] = {
        {0, 0, 0}, {76, 0, 96}, {180, 36, 0}, {255, 154, 0}, {255, 255, 210}
    };
    static const uint8_t sepia[][3] = {
        {20, 12, 6}, {88, 54, 24}, {164, 104, 48}, {236, 190, 116}, {255, 246, 200}
    };
    static const uint8_t blue_red[][3] = {
        {0, 26, 140}, {0, 170, 255}, {238, 238, 238}, {255, 120, 0}, {170, 0, 0}
    };
    static const uint8_t pseudo1[][3] = {
        {0, 0, 0}, {0, 96, 96}, {64, 255, 64}, {255, 255, 80}, {255, 255, 255}
    };
    static const uint8_t pseudo2[][3] = {
        {12, 18, 42}, {50, 110, 180}, {120, 210, 150}, {246, 205, 92}, {255, 72, 72}
    };
    static const uint8_t metal1[][3] = {
        {8, 10, 14}, {42, 52, 66}, {110, 124, 132}, {200, 178, 126}, {255, 246, 210}
    };
    static const uint8_t metal2[][3] = {
        {0, 0, 20}, {28, 62, 92}, {100, 110, 116}, {178, 144, 88}, {255, 255, 235}
    };
    static const uint8_t zhou[][3] = {
        {0, 12, 32}, {0, 92, 118}, {80, 170, 86}, {220, 184, 52}, {252, 248, 190}
    };
    static const uint8_t ning[][3] = {
        {12, 4, 30}, {52, 18, 96}, {130, 52, 130}, {220, 108, 96}, {255, 236, 190}
    };

    switch (mode) {
    case MEDIA_THERMAL_COLOR_BLACK_HOT:
        *r = *g = *b = 255 - value;
        break;
    case MEDIA_THERMAL_COLOR_WHITE_HOT:
    case MEDIA_THERMAL_COLOR_GRAYSCALE:
        *r = *g = *b = value;
        break;
    case MEDIA_THERMAL_COLOR_IRON:
        thermal_palette(t, iron, (int)(sizeof(iron) / sizeof(iron[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_SEPIA:
        thermal_palette(t, sepia, (int)(sizeof(sepia) / sizeof(sepia[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_BLUE_RED:
        thermal_palette(t, blue_red, (int)(sizeof(blue_red) / sizeof(blue_red[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_RAINBOW1:
        thermal_palette(t, rainbow1, (int)(sizeof(rainbow1) / sizeof(rainbow1[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_RAINBOW2:
        thermal_palette(t, rainbow2, (int)(sizeof(rainbow2) / sizeof(rainbow2[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_RAINBOW3:
        thermal_palette(t, rainbow3, (int)(sizeof(rainbow3) / sizeof(rainbow3[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_PSEUDO1:
        thermal_palette(t, pseudo1, (int)(sizeof(pseudo1) / sizeof(pseudo1[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_PSEUDO2:
        thermal_palette(t, pseudo2, (int)(sizeof(pseudo2) / sizeof(pseudo2[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_METAL1:
        thermal_palette(t, metal1, (int)(sizeof(metal1) / sizeof(metal1[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_METAL2:
        thermal_palette(t, metal2, (int)(sizeof(metal2) / sizeof(metal2[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_ZHOU:
        thermal_palette(t, zhou, (int)(sizeof(zhou) / sizeof(zhou[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_NING:
        thermal_palette(t, ning, (int)(sizeof(ning) / sizeof(ning[0])), r, g, b);
        break;
    case MEDIA_THERMAL_COLOR_RAINBOW:
    default:
        thermal_palette(t, rainbow, (int)(sizeof(rainbow) / sizeof(rainbow[0])), r, g, b);
        break;
    }
}

static uint8_t thermal_source_luma(const image_asset_t *img, int sx, int sy) {
    const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
    return (uint8_t)((77 * rgb[0] + 150 * rgb[1] + 29 * rgb[2]) >> 8);
}

static void draw_thermal_mode_image_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                                         const image_asset_t *img, int mode) {
    if (!img || !img->rgb || img->width <= 0 || img->height <= 0 || w <= 0 || h <= 0) return;

    int out_w = w;
    int out_h = (int)((int64_t)img->height * w / img->width);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)img->width * h / img->height);
    }
    if (out_w <= 0 || out_h <= 0) return;

    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;
    uint8_t *yp = dst;
    uint8_t *uv = dst + stride * SCREEN_H;

    for (int dy = 0; dy < out_h; ++dy) {
        int sy = dy * img->height / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = yp + yy * stride;
        for (int dx = 0; dx < out_w; ++dx) {
            int sx = dx * img->width / out_w;
            int xx = ox + dx;
            if (xx < 0 || xx >= SCREEN_W) continue;
            uint8_t r, g, b, yv, u, v;
            thermal_color_for_mode(mode, thermal_source_luma(img, sx, sy), &r, &g, &b);
            rgb_to_yuv(r, g, b, &yv, &u, &v);
            (void)u;
            (void)v;
            drow[xx] = yv;
        }
    }

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy = dy * img->height / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = uv + (yy / 2) * stride;
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx = dx * img->width / out_w;
            int xx = (ox + dx) & ~1;
            if (xx < 0 || xx + 1 >= SCREEN_W) continue;
            uint8_t r, g, b, yv, u, v;
            thermal_color_for_mode(mode, thermal_source_luma(img, sx, sy), &r, &g, &b);
            rgb_to_yuv(r, g, b, &yv, &u, &v);
            (void)yv;
            drow[xx] = u;
            drow[xx + 1] = v;
        }
    }
}

static void draw_thermal_mode_grid(uint8_t *dst, int stride, int x, int y, int w, int h,
                                   int frame) {
    static const struct {
        int mode;
        const char *name;
    } modes[] = {
        {MEDIA_THERMAL_COLOR_RAINBOW, "RAINBOW"},
        {MEDIA_THERMAL_COLOR_BLACK_HOT, "BLACK HOT"},
        {MEDIA_THERMAL_COLOR_WHITE_HOT, "WHITE HOT"},
        {MEDIA_THERMAL_COLOR_IRON, "IRON"},
        {MEDIA_THERMAL_COLOR_SEPIA, "SEPIA"},
        {MEDIA_THERMAL_COLOR_BLUE_RED, "BLUE RED"},
        {MEDIA_THERMAL_COLOR_GRAYSCALE, "GRAYSCALE"},
        {MEDIA_THERMAL_COLOR_RAINBOW1, "RAINBOW1"},
        {MEDIA_THERMAL_COLOR_RAINBOW2, "RAINBOW2"},
        {MEDIA_THERMAL_COLOR_RAINBOW3, "RAINBOW3"},
        {MEDIA_THERMAL_COLOR_PSEUDO1, "PSEUDO1"},
        {MEDIA_THERMAL_COLOR_PSEUDO2, "PSEUDO2"},
        {MEDIA_THERMAL_COLOR_METAL1, "METAL1"},
        {MEDIA_THERMAL_COLOR_METAL2, "METAL2"},
        {MEDIA_THERMAL_COLOR_ZHOU, "ZHOU"},
        {MEDIA_THERMAL_COLOR_NING, "NING"},
    };
    loop_asset_t *loop = find_loop_asset("THERMAL");
    if (!loop || loop->loaded_count <= 0) return;

    const image_asset_t *img = &loop->images[(frame / (FPS * 3)) % loop->loaded_count];
    int header_h = h >= 620 ? 88 : 0;
    if (header_h > 0) {
        fill_rect_nv12(dst, stride, x, y, w, header_h - 12, 6, 13, 24);
        stroke_rect_nv12(dst, stride, x, y, w, header_h - 12, 2, 66, 150, 210);
        draw_utf8_text(dst, stride, x + 24, y + 14,
                       "THERMAL 热成像伪彩模式对比", 26, 170, 255, 220);
        draw_utf8_text(dst, stride, x + 24, y + 50,
                       "为什么这样做：同一张红外灰度图表示温度强弱，不同色表只改变显示颜色，便于按场景突出热目标。每3秒切换下一张rktohi样张。",
                       20, 210, 235, 255);
    }

    int grid_y = y + header_h;
    int grid_h = h - header_h;
    int cols = 4;
    int rows = 4;
    int gap = 8;
    int label_h = 18;
    int cell_w = (w - gap * (cols - 1)) / cols;
    int cell_h = (grid_h - gap * (rows - 1)) / rows;

    for (int i = 0; i < (int)(sizeof(modes) / sizeof(modes[0])); ++i) {
        int col = i % cols;
        int row = i / cols;
        int cx = x + col * (cell_w + gap);
        int cy = grid_y + row * (cell_h + gap);
        fill_rect_nv12(dst, stride, cx, cy, cell_w, cell_h, 5, 10, 18);
        draw_thermal_mode_image_nv12(dst, stride, cx + 3, cy + 3,
                                     cell_w - 6, cell_h - label_h - 6,
                                     img, modes[i].mode);
        fill_rect_nv12(dst, stride, cx, cy + cell_h - label_h, cell_w, label_h, 0, 0, 0);
        draw_text(dst, stride, cx + 6, cy + cell_h - label_h + 4, modes[i].name,
                  1, 210, 235, 255);
        stroke_rect_nv12(dst, stride, cx, cy, cell_w, cell_h, 1, 68, 110, 140);
    }
}

static void draw_rgba_frame_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                                 const uint8_t *rgba, int src_w, int src_h, int src_stride) {
    if (!rgba || src_w <= 0 || src_h <= 0 || src_stride < src_w * 4 || w <= 0 || h <= 0) return;

    int out_w = w;
    int out_h = (int)((int64_t)src_h * w / src_w);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)src_w * h / src_h);
    }
    if (out_w <= 0 || out_h <= 0) return;

    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;
    uint8_t *yp = dst;
    uint8_t *uv = dst + stride * SCREEN_H;

    for (int dy = 0; dy < out_h; ++dy) {
        int sy = dy * src_h / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = yp + yy * stride;
        for (int dx = 0; dx < out_w; ++dx) {
            int sx = dx * src_w / out_w;
            int xx = ox + dx;
            if (xx < 0 || xx >= SCREEN_W) continue;
            const uint8_t *px = rgba + (size_t)sy * src_stride + sx * 4;
            uint8_t yv, u, v;
            rgb_to_yuv(px[0], px[1], px[2], &yv, &u, &v);
            (void)u;
            (void)v;
            drow[xx] = yv;
        }
    }

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy = dy * src_h / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = uv + (yy / 2) * stride;
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx = dx * src_w / out_w;
            int xx = (ox + dx) & ~1;
            if (xx < 0 || xx + 1 >= SCREEN_W) continue;
            const uint8_t *px = rgba + (size_t)sy * src_stride + sx * 4;
            uint8_t yv, u, v;
            rgb_to_yuv(px[0], px[1], px[2], &yv, &u, &v);
            (void)yv;
            drow[xx] = u;
            drow[xx + 1] = v;
        }
    }
}

static void stroke_rect_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                             int thick, uint8_t r, uint8_t g, uint8_t b) {
    fill_rect_nv12(dst, stride, x, y, w, thick, r, g, b);
    fill_rect_nv12(dst, stride, x, y + h - thick, w, thick, r, g, b);
    fill_rect_nv12(dst, stride, x, y, thick, h, r, g, b);
    fill_rect_nv12(dst, stride, x + w - thick, y, thick, h, r, g, b);
}

static uint8_t glyph_row(char c, int row) {
    static const uint8_t digits[10][7] = {
        {31,17,19,21,25,17,31}, {4,12,4,4,4,4,14}, {31,1,1,31,16,16,31},
        {31,1,1,15,1,1,31}, {17,17,17,31,1,1,1}, {31,16,16,31,1,1,31},
        {31,16,16,31,17,17,31}, {31,1,2,4,8,8,8}, {31,17,17,31,17,17,31},
        {31,17,17,31,1,1,31},
    };
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {15,16,16,16,16,16,15},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {15,16,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    };
    if (c >= '0' && c <= '9') return digits[c - '0'][row];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'][row];
    if (c == '-') return row == 3 ? 31 : 0;
    if (c == '_') return row == 6 ? 31 : 0;
    if (c == ':') return (row == 2 || row == 4) ? 4 : 0;
    if (c == '.') return row == 6 ? 4 : 0;
    if (c == '=') return (row == 2 || row == 4) ? 31 : 0;
    if (c == '%') {
        static const uint8_t percent[7] = {17, 18, 4, 8, 9, 17, 0};
        return percent[row];
    }
    if (c == '/') return 1 << (6 - row > 4 ? 4 : (6 - row));
    if (c == '>') return row < 3 ? (1 << row) : (row == 3 ? 16 : (1 << (6 - row)));
    return 0;
}

static void draw_text(uint8_t *dst, int stride, int x, int y, const char *text,
                      int scale, uint8_t r, uint8_t g, uint8_t b) {
    int cx = x;
    for (const char *p = text; *p; ++p) {
        if (*p == ' ') {
            cx += 4 * scale;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = glyph_row(*p, row);
            for (int col = 0; col < 5; ++col) {
                if (bits & (1 << (4 - col))) {
                    fill_rect_nv12(dst, stride, cx + col * scale, y + row * scale,
                                   scale, scale, r, g, b);
                }
            }
        }
        cx += 6 * scale;
    }
}

static int render_text_mask(const char *text, int scale, uint8_t *mask,
                            int max_w, int max_h, int *out_w, int *out_h) {
    if (!text || !mask || scale <= 0 || max_w <= 0 || max_h <= 0) return -1;
    int w = 0;
    for (const char *p = text; *p; ++p) {
        w += (*p == ' ') ? 4 * scale : 6 * scale;
    }
    int h = 7 * scale;
    if (w <= 0 || h <= 0 || w > max_w || h > max_h) return -1;
    memset(mask, 0, (size_t)max_w * max_h);
    int cx = 0;
    for (const char *p = text; *p; ++p) {
        if (*p == ' ') {
            cx += 4 * scale;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = glyph_row(*p, row);
            for (int col = 0; col < 5; ++col) {
                if (!(bits & (1 << (4 - col)))) continue;
                for (int sy = 0; sy < scale; ++sy) {
                    int yy = row * scale + sy;
                    if (yy >= h) continue;
                    uint8_t *dst = mask + yy * max_w + cx + col * scale;
                    for (int sx = 0; sx < scale; ++sx) {
                        if (cx + col * scale + sx < w) dst[sx] = 255;
                    }
                }
            }
        }
        cx += 6 * scale;
    }
    *out_w = w;
    *out_h = h;
    return 0;
}

static uint32_t utf8_next_codepoint(const char **p) {
    const unsigned char *s = (const unsigned char *)(*p);
    uint32_t cp = 0;
    if (s[0] < 0x80) {
        cp = s[0];
        *p += 1;
    } else if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x1f) << 6) | (uint32_t)(s[1] & 0x3f);
        *p += 2;
    } else if ((s[0] & 0xf0) == 0xe0 &&
               (s[1] & 0xc0) == 0x80 &&
               (s[2] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x0f) << 12) |
             ((uint32_t)(s[1] & 0x3f) << 6) |
             (uint32_t)(s[2] & 0x3f);
        *p += 3;
    } else if ((s[0] & 0xf8) == 0xf0 &&
               (s[1] & 0xc0) == 0x80 &&
               (s[2] & 0xc0) == 0x80 &&
               (s[3] & 0xc0) == 0x80) {
        cp = ((uint32_t)(s[0] & 0x07) << 18) |
             ((uint32_t)(s[1] & 0x3f) << 12) |
             ((uint32_t)(s[2] & 0x3f) << 6) |
             (uint32_t)(s[3] & 0x3f);
        *p += 4;
    } else {
        cp = '?';
        *p += 1;
    }
    return cp;
}

static int render_utf8_text_mask(const char *text, int pixel_size, uint8_t *mask,
                                 int max_w, int max_h, int *out_w, int *out_h) {
    static FT_Library ft_lib = NULL;
    static FT_Face ft_face = NULL;
    int baseline;
    int width = 0;
    int height = 0;

    if (!text || !mask || pixel_size <= 0 || max_w <= 0 || max_h <= 0) return -1;
    if (!ft_lib && FT_Init_FreeType(&ft_lib) != 0) return -1;
    if (!ft_face && FT_New_Face(ft_lib, UI_FONT_PATH, 0, &ft_face) != 0) return -1;
    if (FT_Set_Pixel_Sizes(ft_face, 0, (FT_UInt)pixel_size) != 0) return -1;

    baseline = (int)(ft_face->size->metrics.ascender >> 6);
    height = (int)(ft_face->size->metrics.height >> 6);
    if (height <= 0) height = pixel_size + 4;
    if (height > max_h) return -1;

    const char *p = text;
    while (*p) {
        uint32_t cp = utf8_next_codepoint(&p);
        if (FT_Load_Char(ft_face, cp, FT_LOAD_RENDER) != 0) continue;
        width += (int)(ft_face->glyph->advance.x >> 6);
    }
    if (width <= 0 || width > max_w) return -1;

    memset(mask, 0, (size_t)max_w * max_h);
    int pen_x = 0;
    p = text;
    while (*p) {
        uint32_t cp = utf8_next_codepoint(&p);
        if (FT_Load_Char(ft_face, cp, FT_LOAD_RENDER) != 0) continue;
        FT_GlyphSlot glyph = ft_face->glyph;
        int gx = pen_x + glyph->bitmap_left;
        int gy = baseline - glyph->bitmap_top;
        for (int row = 0; row < (int)glyph->bitmap.rows; ++row) {
            int yy = gy + row;
            if (yy < 0 || yy >= max_h) continue;
            const uint8_t *src = glyph->bitmap.buffer + row * glyph->bitmap.pitch;
            uint8_t *dst = mask + yy * max_w;
            for (int col = 0; col < (int)glyph->bitmap.width; ++col) {
                int xx = gx + col;
                if (xx >= 0 && xx < max_w && src[col] > dst[xx]) dst[xx] = src[col];
            }
        }
        pen_x += (int)(glyph->advance.x >> 6);
    }

    *out_w = width;
    *out_h = height;
    return 0;
}

static void draw_utf8_text(uint8_t *dst, int stride, int x, int y, const char *text,
                           int pixel_size, uint8_t r, uint8_t g, uint8_t b) {
    static uint8_t mask[1024 * 96];
    int w = 0;
    int h = 0;
    if (render_utf8_text_mask(text, pixel_size, mask, 1024, 96, &w, &h) != 0) return;
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            if (mask[yy * 1024 + xx] > 16) {
                fill_rect_nv12(dst, stride, x + xx, y + yy, 1, 1, r, g, b);
            }
        }
    }
}

static void draw_camera_tile(uint8_t *dst, int dstride, int dx, int dy, int dw, int dh,
                             const uint8_t *src, int sw, int sh, int sstride) {
    if (!src) return;
    const uint8_t *sy = src;
    const uint8_t *suv = src + sstride * sh;
    uint8_t *dy_base = dst;
    uint8_t *duv_base = dst + dstride * SCREEN_H;

    for (int y = 0; y < dh; ++y) {
        int src_y = y * sh / dh;
        uint8_t *drow = dy_base + (dy + y) * dstride + dx;
        const uint8_t *srow = sy + src_y * sstride;
        for (int x = 0; x < dw; ++x) {
            drow[x] = srow[x * sw / dw];
        }
    }
    for (int y = 0; y < dh / 2; ++y) {
        int src_y = (y * 2) * sh / dh;
        const uint8_t *srow = suv + (src_y / 2) * sstride;
        uint8_t *drow = duv_base + ((dy / 2) + y) * dstride + (dx & ~1);
        for (int x = 0; x < dw; x += 2) {
            int src_x = ((x * sw / dw) & ~1);
            drow[x] = srow[src_x];
            drow[x + 1] = srow[src_x + 1];
        }
    }
}

static int fit_rect_for_aspect(int dx, int dy, int dw, int dh, int sw, int sh,
                               int *ox, int *oy, int *ow, int *oh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return -1;
    int out_w = dw;
    int out_h = (int)((int64_t)sh * out_w / sw);
    if (out_h > dh) {
        out_h = dh;
        out_w = (int)((int64_t)sw * out_h / sh);
    }
    if (out_w <= 0 || out_h <= 0) return -1;
    if (ox) *ox = dx + (dw - out_w) / 2;
    if (oy) *oy = dy + (dh - out_h) / 2;
    if (ow) *ow = out_w;
    if (oh) *oh = out_h;
    return 0;
}

static int draw_camera_tile_fit_rect(uint8_t *dst, int dstride, int dx, int dy, int dw, int dh,
                                     const uint8_t *src, int sw, int sh, int sstride,
                                     int *rx, int *ry, int *rw, int *rh) {
    int ox = 0, oy = 0, out_w = 0, out_h = 0;
    if (!src || fit_rect_for_aspect(dx, dy, dw, dh, sw, sh, &ox, &oy, &out_w, &out_h) != 0) {
        return -1;
    }
    draw_camera_tile(dst, dstride, ox, oy, out_w, out_h, src, sw, sh, sstride);
    if (rx) *rx = ox;
    if (ry) *ry = oy;
    if (rw) *rw = out_w;
    if (rh) *rh = out_h;
    return 0;
}

static void draw_camera_tile_fit(uint8_t *dst, int dstride, int dx, int dy, int dw, int dh,
                                 const uint8_t *src, int sw, int sh, int sstride) {
    (void)draw_camera_tile_fit_rect(dst, dstride, dx, dy, dw, dh, src, sw, sh, sstride,
                                    NULL, NULL, NULL, NULL);
}

static void nv12_content_rect(const uint8_t *src, int sw, int sh, int sstride,
                              int *sx, int *sy, int *cw, int *ch) {
    int min_x = sw, min_y = sh, max_x = -1, max_y = -1;
    if (!src || sw <= 0 || sh <= 0 || sstride < sw) {
        *sx = 0; *sy = 0; *cw = sw; *ch = sh;
        return;
    }

    for (int y = 0; y < sh; ++y) {
        const uint8_t *row = src + (size_t)y * sstride;
        for (int x = 0; x < sw; ++x) {
            if (row[x] > 24) {
                if (x < min_x) min_x = x;
                if (x > max_x) max_x = x;
                if (y < min_y) min_y = y;
                if (y > max_y) max_y = y;
            }
        }
    }

    if (max_x < min_x || max_y < min_y) {
        *sx = 0; *sy = 0; *cw = sw; *ch = sh;
        return;
    }
    min_x &= ~1;
    min_y &= ~1;
    max_x |= 1;
    max_y |= 1;
    if (max_x >= sw) max_x = sw - 1;
    if (max_y >= sh) max_y = sh - 1;
    *sx = min_x;
    *sy = min_y;
    *cw = max_x - min_x + 1;
    *ch = max_y - min_y + 1;
}

static void draw_camera_tile_crop(uint8_t *dst, int dstride, int dx, int dy, int dw, int dh,
                                  const uint8_t *src, int sw, int sh, int sstride,
                                  int sx, int sy, int cw, int ch) {
    if (!src || dw <= 0 || dh <= 0 || cw <= 0 || ch <= 0) return;
    const uint8_t *src_y = src;
    const uint8_t *src_uv = src + (size_t)sstride * sh;
    uint8_t *dst_y = dst;
    uint8_t *dst_uv = dst + (size_t)dstride * SCREEN_H;

    for (int y = 0; y < dh; ++y) {
        int sample_y = sy + y * ch / dh;
        uint8_t *drow = dst_y + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src_y + (size_t)sample_y * sstride;
        for (int x = 0; x < dw; ++x) {
            drow[x] = srow[sx + x * cw / dw];
        }
    }

    for (int y = 0; y < dh / 2; ++y) {
        int sample_y = sy + (y * 2) * ch / dh;
        const uint8_t *srow = src_uv + (size_t)(sample_y / 2) * sstride;
        uint8_t *drow = dst_uv + (size_t)((dy / 2) + y) * dstride + (dx & ~1);
        for (int x = 0; x < dw; x += 2) {
            int sample_x = (sx + x * cw / dw) & ~1;
            drow[x] = srow[sample_x];
            drow[x + 1] = srow[sample_x + 1];
        }
    }
}

static void draw_camera_tile_fit_content(uint8_t *dst, int dstride, int dx, int dy, int dw, int dh,
                                         const uint8_t *src, int sw, int sh, int sstride) {
    int sx = 0, sy = 0, cw = sw, ch = sh;
    if (!src || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    nv12_content_rect(src, sw, sh, sstride, &sx, &sy, &cw, &ch);
    int out_w = dw;
    int out_h = (int)((int64_t)ch * out_w / cw);
    if (out_h > dh) {
        out_h = dh;
        out_w = (int)((int64_t)cw * out_h / ch);
    }
    if (out_w <= 0 || out_h <= 0) return;
    int ox = dx + (dw - out_w) / 2;
    int oy = dy + (dh - out_h) / 2;
    draw_camera_tile_crop(dst, dstride, ox, oy, out_w, out_h, src, sw, sh, sstride,
                          sx, sy, cw, ch);
}

static void image_to_nv12_frame(const image_asset_t *img, uint8_t *dst) {
    if (!img || !img->rgb || !dst || img->width <= 0 || img->height <= 0) return;

    memset(dst, 16, CAM_STRIDE * CAM_H);
    memset(dst + CAM_STRIDE * CAM_H, 128, CAM_STRIDE * CAM_H / 2);

    int out_w = CAM_W;
    int out_h = (int)((int64_t)img->height * CAM_W / img->width);
    if (out_h > CAM_H) {
        out_h = CAM_H;
        out_w = (int)((int64_t)img->width * CAM_H / img->height);
    }
    int ox = (CAM_W - out_w) / 2;
    int oy = (CAM_H - out_h) / 2;
    uint8_t *yp = dst;
    uint8_t *uv = dst + CAM_STRIDE * CAM_H;

    for (int y = 0; y < out_h; ++y) {
        int sy = y * img->height / out_h;
        uint8_t *drow = yp + (oy + y) * CAM_STRIDE + ox;
        for (int x = 0; x < out_w; ++x) {
            int sx = x * img->width / out_w;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &u, &v);
            (void)u;
            (void)v;
            drow[x] = yy;
        }
    }

    for (int y = 0; y < out_h; y += 2) {
        int sy = y * img->height / out_h;
        uint8_t *drow = uv + ((oy + y) / 2) * CAM_STRIDE + (ox & ~1);
        for (int x = 0; x < out_w; x += 2) {
            int sx = x * img->width / out_w;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &u, &v);
            (void)yy;
            drow[x & ~1] = u;
            drow[(x & ~1) + 1] = v;
        }
    }
}

static void image_to_nv12_packed(const image_asset_t *img, uint8_t *dst) {
    if (!img || !img->rgb || !dst || img->width <= 0 || img->height <= 0) return;
    int w = img->width;
    int h = img->height;
    uint8_t *yp = dst;
    uint8_t *uv = dst + (size_t)w * h;

    for (int y = 0; y < h; ++y) {
        uint8_t *drow = yp + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            const uint8_t *rgb = img->rgb + ((size_t)y * w + x) * 3;
            uint8_t yy, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &u, &v);
            (void)u;
            (void)v;
            drow[x] = yy;
        }
    }

    for (int y = 0; y < h; y += 2) {
        uint8_t *drow = uv + (size_t)(y / 2) * w;
        for (int x = 0; x < w; x += 2) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const uint8_t *rgb = img->rgb + ((size_t)(y + dy) * w + (x + dx)) * 3;
                    r += rgb[0];
                    g += rgb[1];
                    b += rgb[2];
                }
            }
            uint8_t yy, u, v;
            rgb_to_yuv((uint8_t)(r / 4), (uint8_t)(g / 4), (uint8_t)(b / 4), &yy, &u, &v);
            (void)yy;
            drow[x] = u;
            drow[x + 1] = v;
        }
    }
}

static void fill_dualview_demo_rgb(uint8_t *dst, int input0) {
    if (!dst) return;
    for (int y = 0; y < CAM_H; ++y) {
        uint8_t *row = dst + (size_t)y * CAM_W * 3;
        for (int x = 0; x < CAM_W; ++x) {
            uint8_t *px = row + x * 3;
            px[0] = input0 ? 255 : 0;
            px[1] = 0;
            px[2] = input0 ? 0 : 255;
        }
    }
}

static int compose_pano_reference_preview(uint8_t *dst) {
    if (!dst || !g_pano_sample.loaded || g_pano_sample.input_count <= 0) return -1;

    uint8_t *yp = dst;
    uint8_t *uvp = dst + (size_t)PANO_STITCH_STRIDE * PANO_STITCH_H;
    int segment_w = PANO_STITCH_W / g_pano_sample.input_count;
    if (segment_w <= 0) return -1;

    for (int y = 0; y < PANO_STITCH_H; ++y) {
        uint8_t *row = yp + (size_t)y * PANO_STITCH_STRIDE;
        for (int x = 0; x < PANO_STITCH_W; ++x) {
            int idx = x / segment_w;
            if (idx >= g_pano_sample.input_count) idx = g_pano_sample.input_count - 1;
            image_asset_t *img = &g_pano_sample.inputs[idx];
            if (!img->rgb || img->width <= 0 || img->height <= 0) {
                row[x] = 16;
                continue;
            }
            int local_x = x - idx * segment_w;
            int sx = local_x * img->width / segment_w;
            int sy = y * img->height / PANO_STITCH_H;
            if (sx >= img->width) sx = img->width - 1;
            if (sy >= img->height) sy = img->height - 1;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &u, &v);
            (void)u;
            (void)v;
            row[x] = yy;
        }
    }

    for (int y = 0; y < PANO_STITCH_H; y += 2) {
        uint8_t *row = uvp + (size_t)(y / 2) * PANO_STITCH_STRIDE;
        for (int x = 0; x < PANO_STITCH_W; x += 2) {
            int idx = x / segment_w;
            if (idx >= g_pano_sample.input_count) idx = g_pano_sample.input_count - 1;
            image_asset_t *img = &g_pano_sample.inputs[idx];
            if (!img->rgb || img->width <= 0 || img->height <= 0) {
                row[x] = 128;
                row[x + 1] = 128;
                continue;
            }
            int local_x = x - idx * segment_w;
            int sx = local_x * img->width / segment_w;
            int sy = y * img->height / PANO_STITCH_H;
            if (sx >= img->width) sx = img->width - 1;
            if (sy >= img->height) sy = img->height - 1;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &u, &v);
            (void)yy;
            row[x] = u;
            row[x + 1] = v;
        }
    }
    return 0;
}

static void draw_edof_comparison(uint8_t *dst, int stride, int x, int y, int w, int h,
                                 const uint8_t *left, const uint8_t *right, const uint8_t *out) {
    int gap = 12;
    int label_h = 22;
    int top_h = (h * 42) / 100;
    int bottom_h = h - top_h - gap;
    int input_w = (w - gap) / 2;

    struct {
        const char *label;
        const uint8_t *frame;
        int x, y, w, h;
        int highlight;
    } panels[3] = {
        {"INPUT A", left, x, y, input_w, top_h, 0},
        {"INPUT B", right, x + input_w + gap, y, input_w, top_h, 0},
        {"OUTPUT", out, x, y + top_h + gap, w, bottom_h, 1},
    };

    for (int i = 0; i < 3; ++i) {
        int px = panels[i].x;
        int py = panels[i].y;
        int pw = panels[i].w;
        int ph = panels[i].h;
        fill_rect_nv12(dst, stride, px, py, pw, ph, 5, 10, 18);
        stroke_rect_nv12(dst, stride, px, py, pw, ph, 1,
                         panels[i].highlight ? 80 : 70,
                         panels[i].highlight ? 255 : 140,
                         panels[i].highlight ? 180 : 210);
        fill_rect_nv12(dst, stride, px, py + ph - label_h, pw, label_h, 0, 0, 0);
        draw_text(dst, stride, px + 8, py + ph - label_h + 5,
                  panels[i].label, 1, 180, 230, 255);
        if (panels[i].frame) {
            draw_camera_tile_fit_content(dst, stride, px + 4, py + 4, pw - 8, ph - label_h - 8,
                                         panels[i].frame, CAM_W, CAM_H, CAM_STRIDE);
        } else {
            draw_text(dst, stride, px + 12, py + ph / 2 - 10, "WAIT", 1, 255, 190, 100);
        }
    }
}

static void draw_nv12_comparison(uint8_t *dst, int stride, int x, int y, int w, int h,
                                 const uint8_t *input, const uint8_t *output,
                                 const char *input_label, const char *output_label) {
    int gap = 12;
    int label_h = 22;
    int row_h = (h - gap) / 2;
    const uint8_t *frames[2] = {input, output};
    const char *labels[2] = {input_label, output_label};

    for (int i = 0; i < 2; ++i) {
        int cy = y + i * (row_h + gap);
        fill_rect_nv12(dst, stride, x, cy, w, row_h, 5, 10, 18);
        stroke_rect_nv12(dst, stride, x, cy, w, row_h, 1,
                         i == 0 ? 70 : 80, i == 0 ? 140 : 255, i == 0 ? 210 : 180);
        if (frames[i]) {
            draw_camera_tile_fit(dst, stride, x + 4, cy + 4, w - 8, row_h - label_h - 8,
                                 frames[i], CAM_W, CAM_H, CAM_STRIDE);
        } else {
            draw_text(dst, stride, x + 12, cy + row_h / 2 - 10, "WAIT", 1, 255, 190, 100);
        }
        fill_rect_nv12(dst, stride, x, cy + row_h - label_h, w, label_h, 0, 0, 0);
        draw_text(dst, stride, x + 8, cy + row_h - label_h + 5, labels[i], 1, 180, 230, 255);
    }
}

static void draw_nv12_rgb_comparison(uint8_t *dst, int stride, int x, int y, int w, int h,
                                     const uint8_t *input_nv12, const uint8_t *output_rgb,
                                     const char *input_label, const char *output_label) {
    int gap = 14;
    int label_h = 30;
    int pane_w = w;
    int pane_h = (h - gap) / 2;
    int top_y = y;
    int bottom_y = y + pane_h + gap;
    int image_box = pane_w - 12;
    int image_max_h = pane_h - label_h - 10;
    if (image_box > image_max_h) image_box = image_max_h;
    if (image_box <= 0) return;
    int top_image_y = top_y + 6 + (image_max_h - image_box) / 2;
    int bottom_image_y = bottom_y + 6 + (image_max_h - image_box) / 2;
    int image_x = x + (pane_w - image_box) / 2;

    fill_rect_nv12(dst, stride, x, top_y, pane_w, pane_h, 8, 16, 28);
    fill_rect_nv12(dst, stride, x, bottom_y, pane_w, pane_h, 8, 16, 28);
    stroke_rect_nv12(dst, stride, x, top_y, pane_w, pane_h, 1, 70, 140, 210);
    stroke_rect_nv12(dst, stride, x, bottom_y, pane_w, pane_h, 1, 80, 255, 180);

    if (input_nv12) {
        draw_camera_tile(dst, stride, image_x, top_image_y, image_box, image_box,
                         input_nv12, CAM_W, CAM_H, CAM_STRIDE);
    } else {
        draw_text(dst, stride, x + 18, top_y + pane_h / 2 - 10, "WAIT", 1, 255, 190, 100);
    }
    if (output_rgb) {
        image_asset_t rgb_frame = {CAM_W, CAM_H, (uint8_t *)output_rgb};
        draw_rgb_image_nv12(dst, stride, image_x, bottom_image_y, image_box, image_box, &rgb_frame);
    } else {
        draw_text(dst, stride, x + 18, bottom_y + pane_h / 2 - 10, "WAIT", 1, 255, 190, 100);
    }

    fill_rect_nv12(dst, stride, x, top_y + pane_h - label_h, pane_w, label_h, 0, 0, 0);
    fill_rect_nv12(dst, stride, x, bottom_y + pane_h - label_h, pane_w, label_h, 0, 0, 0);
    draw_text(dst, stride, x + 14, top_y + pane_h - label_h + 8, input_label, 1, 190, 230, 255);
    draw_text(dst, stride, x + 14, bottom_y + pane_h - label_h + 8, output_label, 1, 170, 255, 220);
}

static void draw_dehaze_showcase(uint8_t *dst, int stride, int x, int y, int w, int h,
                                 const uint8_t *input_nv12, const uint8_t *output_rgb,
                                 const char *title, const char *flow, const char *why,
                                 const char *param_line, const char *output_label) {
    char runtime[128];
    int top_h = 134;
    int bottom_h = 178;
    int gap = 12;
    int compare_y = y + top_h + gap;
    int compare_h = h - top_h - bottom_h - gap * 2;
    int bottom_y = y + h - bottom_h;

    if (compare_h < 320) return;
    format_runtime_status(runtime, sizeof(runtime));

    fill_rect_nv12(dst, stride, x, y, w, top_h, 6, 13, 24);
    stroke_rect_nv12(dst, stride, x, y, w, top_h, 2, 66, 150, 210);
    draw_utf8_text(dst, stride, x + 24, y + 18, title, 28, 170, 255, 220);
    draw_utf8_text(dst, stride, x + 24, y + 62, flow, 20, 210, 235, 255);
    draw_utf8_text(dst, stride, x + 24, y + 96, why, 20, 255, 230, 120);

    draw_nv12_rgb_comparison(dst, stride, x, compare_y, w, compare_h,
                             input_nv12, output_rgb,
                             "TOP: VI RAW INPUT", output_label);

    fill_rect_nv12(dst, stride, x, bottom_y, w, bottom_h, 5, 10, 18);
    stroke_rect_nv12(dst, stride, x, bottom_y, w, bottom_h, 2, 0, 190, 170);
    draw_utf8_text(dst, stride, x + 24, bottom_y + 22,
                   "展示重点：同尺寸上下对比，上方为实时输入，下方为算法输出。",
                   22, 170, 255, 220);
    draw_text(dst, stride, x + 24, bottom_y + 72, param_line, 1, 190, 230, 255);
    draw_text(dst, stride, x + 24, bottom_y + 110, runtime, 2, 255, 230, 120);
}

static void vpss_bind_layout(int *start_x, int *start_y, int *cell, int *gap) {
    int c = VPSS_TILE_W;
    int g = 40;
    if (cell) *cell = c;
    if (gap) *gap = g;
    if (start_x) *start_x = (SCREEN_W - c * 2 - g) / 2;
    if (start_y) *start_y = 300;
}

static void draw_vpss_showcase_labels(uint8_t *dst, int stride, int x, int y, int w, int h) {
    int gap = 10;
    int label_h = 24;
    int col_w = (w - gap) / 2;
    int row_h = (h - gap) / 2;

    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        int cx = x + (i % 2) * (col_w + gap);
        int cy = y + (i / 2) * (row_h + gap);
        stroke_rect_nv12(dst, stride, cx, cy, col_w, row_h, 2,
                         i == 0 ? 70 : 80, i == 0 ? 140 : 255, i == 0 ? 210 : 180);
        fill_rect_nv12(dst, stride, cx, cy + row_h - label_h, col_w, label_h, 0, 0, 0);
        draw_text(dst, stride, cx + 8, cy + row_h - label_h + 6,
                  g_vpss_tile_labels[i], 1, 180, 230, 255);
    }
}

static void draw_vpss_bind_style_flow(uint8_t *dst, int stride) {
    const int box_y = 1332;
    const int box_w = 166;
    const int box_h = 88;
    const int box_gap = 14;
    const int box_x0 = 90;
    const char *box_title[5] = {"采集 VI", "缩放 VPSS", "合成 VMIX", "叠字 OSD", "出屏 VO"};
    const char *box_note[5] = {
        "摄像头原始帧", "缩放 裁剪 翻转", "只排版不缩放", "叠加标题状态", "送显示屏",
    };

    fill_rect_nv12(dst, stride, 58, 1280, 964, 488, 5, 10, 18);
    stroke_rect_nv12(dst, stride, 58, 1280, 964, 488, 2, 0, 190, 170);
    draw_utf8_text(dst, stride, 84, 1304,
                   "VPSS测试：一路输入生成四路处理结果", 29, 160, 255, 220);
    draw_utf8_text(dst, stride, 86, 1348,
                   "这一页用于确认VPSS缩放、裁剪、翻转和多路输出是否正常。", 22, 190, 230, 255);

    for (int i = 0; i < 5; ++i) {
        int bx = box_x0 + i * (box_w + box_gap);
        fill_rect_nv12(dst, stride, bx, box_y + 58, box_w, box_h, 18, 42, 64);
        stroke_rect_nv12(dst, stride, bx, box_y + 58, box_w, box_h, 1, 70, 140, 210);
        draw_utf8_text(dst, stride, bx + 18, box_y + 74, box_title[i], 23, 160, 255, 220);
        draw_utf8_text(dst, stride, bx + 8, box_y + 110, box_note[i], 17, 190, 230, 255);
        if (i < 4) {
            draw_utf8_text(dst, stride, bx + box_w + 3, box_y + 88,
                           "→", 24, 255, 230, 120);
        }
    }

    draw_utf8_text(dst, stride, 86, 1538,
                   "VPSS把同一份VI输入处理成四路480x480输出。", 23, 255, 230, 120);
    draw_utf8_text(dst, stride, 86, 1578,
                   "VMIX只负责摆放四路画面，不依赖VMIX缩小。", 23, 140, 210, 180);
}

static void draw_vpss_bind_style_page(uint8_t *dst, int stride, int frame,
                                      int page, int total_pages) {
    int start_x = 0, start_y = 0, cell = 0, gap = 0;
    char perf[160];
    char runtime[128];
    (void)frame;
    vpss_bind_layout(&start_x, &start_y, &cell, &gap);
    format_runtime_status(runtime, sizeof(runtime));

    fill_rect_nv12(dst, stride, 0, 0, SCREEN_W, SCREEN_H, 4, 9, 16);
    fill_rect_nv12(dst, stride, 0, 0, SCREEN_W, 96, 5, 10, 18);
    fill_rect_nv12(dst, stride, 0, SCREEN_H - 112, SCREEN_W, 112, 5, 10, 18);
    draw_text(dst, stride, 32, 24, "VPSS TEST", 3, 160, 255, 220);
    snprintf(perf, sizeof(perf), "PAGE %02d/%02d  FULL  MOVING CROP  FLIP  ZOOM  %s",
             page + 1, total_pages, runtime);
    draw_text(dst, stride, 32, SCREEN_H - 72, perf, 2, 255, 230, 120);

    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        int x = start_x + (i % 2) * (cell + gap);
        int y = start_y + (i / 2) * (cell + gap);
        fill_rect_nv12(dst, stride, x, y, cell, cell, 8, 16, 28);
        if (g_vpss_preview_ready[i] && g_vpss_preview_frames[i]) {
            draw_nv12_to_frame(dst, SCREEN_W, SCREEN_H, stride, x, y, cell, cell,
                               g_vpss_preview_frames[i],
                               VPSS_TILE_W, VPSS_TILE_H, VPSS_TILE_NV12_STRIDE);
        } else {
            draw_text(dst, stride, x + 28, y + cell / 2 - 10, "WAIT VPSS", 2, 255, 190, 100);
        }
        fill_rect_nv12(dst, stride, x, y + cell - 30, cell, 30, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + cell - 22,
                  g_vpss_tile_labels[i], 1, 180, 230, 255);
        stroke_rect_nv12(dst, stride, x, y, cell, cell, 2,
                         i == 0 ? 70 : 80, i == 0 ? 140 : 255, i == 0 ? 210 : 180);
    }

    draw_vpss_bind_style_flow(dst, stride);
}

static void draw_vo_showcase(uint8_t *dst, int stride, int x, int y, int w, int h, int frame) {
    fill_rect_nv12(dst, stride, x, y, w, h, 4, 9, 16);
    stroke_rect_nv12(dst, stride, x, y, w, h, 4, 0, 220, 180);

    int margin = 34;
    int panel_x = x + margin;
    int panel_y = y + 42;
    int panel_w = w - margin * 2;
    int panel_h = h - 84;
    fill_rect_nv12(dst, stride, panel_x, panel_y, panel_w, panel_h, 7, 13, 24);
    stroke_rect_nv12(dst, stride, panel_x, panel_y, panel_w, panel_h, 3, 70, 180, 255);

    draw_utf8_text(dst, stride, panel_x + 34, panel_y + 28,
                   "VO显示输出控制", 34, 160, 255, 220);
    draw_utf8_text(dst, stride, panel_x + 36, panel_y + 86,
                   "数据流：程序生成NV12画面 -> VO通道0 -> MIPI/DSI屏幕",
                   22, 190, 230, 255);
    draw_utf8_text(dst, stride, panel_x + 36, panel_y + 124,
                   "VO是显示链路最后一站，负责冻结、隐藏和恢复显示平面。",
                   22, 255, 230, 120);

    fill_rect_nv12(dst, stride, panel_x + 34, panel_y + 170, panel_w - 68, 112, 10, 22, 38);
    stroke_rect_nv12(dst, stride, panel_x + 34, panel_y + 170, panel_w - 68, 112, 2, 120, 190, 255);
    draw_text(dst, stride, panel_x + 58, panel_y + 194, "CURRENT API", 2, 160, 255, 220);
    draw_utf8_text(dst, stride, panel_x + 58, panel_y + 232, g_vo_demo_api, 24, 190, 230, 255);
    draw_text(dst, stride, panel_x + 520, panel_y + 194, "STATE", 2, 160, 255, 220);
    draw_utf8_text(dst, stride, panel_x + 520, panel_y + 232, g_vo_demo_status, 24,
                   255,
                   g_vo_demo_state == VO_DEMO_NORMAL ? 255 : 210,
                   g_vo_demo_state == VO_DEMO_NORMAL ? 220 : 90);

    int bar_y = panel_y + 330;
    int bar_h = 112;
    int bar_w = panel_w / 6;
    const uint8_t colors[6][3] = {
        {220, 40, 50}, {40, 170, 90}, {50, 100, 220},
        {240, 220, 80}, {220, 80, 200}, {80, 230, 230},
    };
    for (int i = 0; i < 6; ++i) {
        fill_rect_nv12(dst, stride, panel_x + i * bar_w, bar_y, bar_w, bar_h,
                       colors[i][0], colors[i][1], colors[i][2]);
    }
    stroke_rect_nv12(dst, stride, panel_x, bar_y, bar_w * 6, bar_h, 3, 240, 240, 240);

    int scan_area_y = bar_y + 168;
    int scan_area_h = panel_h - (scan_area_y - panel_y) - 220;
    if (scan_area_h < 120) scan_area_h = 120;
    fill_rect_nv12(dst, stride, panel_x + 40, scan_area_y, panel_w - 80, scan_area_h, 8, 16, 30);
    stroke_rect_nv12(dst, stride, panel_x + 40, scan_area_y, panel_w - 80, scan_area_h, 2, 120, 190, 255);
    for (int i = 0; i < 12; ++i) {
        int yy = scan_area_y + 18 + i * (scan_area_h - 36) / 12;
        fill_rect_nv12(dst, stride, panel_x + 70, yy, panel_w - 140, 2, 20, 45, 70);
    }
    int scan_y = scan_area_y + 12 + ((frame * 8) % (scan_area_h - 24));
    fill_rect_nv12(dst, stride, panel_x + 58, scan_y, panel_w - 116, 10, 0, 255, 190);

    int step_y = panel_y + panel_h - 178;
    const char *steps[4] = {"NORMAL", "FREEZE MAIN", "FREEZE PLANE", "HIDE PLANE"};
    for (int i = 0; i < 4; ++i) {
        int sx = panel_x + 44 + i * 205;
        int active =
            (i == 0 && g_vo_demo_state == VO_DEMO_NORMAL) ||
            (i == 1 && g_vo_demo_state == VO_DEMO_FREEZE_MAIN) ||
            (i == 2 && g_vo_demo_state == VO_DEMO_FREEZE_PLANE) ||
            (i == 3 && (g_vo_demo_state == VO_DEMO_HIDE_WARN || g_vo_demo_state == VO_DEMO_HIDE_PLANE));
        fill_rect_nv12(dst, stride, sx, step_y, 174, 62,
                       active ? 22 : 8, active ? 58 : 24, active ? 66 : 36);
        stroke_rect_nv12(dst, stride, sx, step_y, 174, 62, 2,
                         active ? 255 : 80, active ? 230 : 120, active ? 120 : 170);
        draw_text(dst, stride, sx + 16, step_y + 22, steps[i], 1,
                  active ? 255 : 150, active ? 245 : 180, active ? 160 : 210);
    }

    draw_utf8_text(dst, stride, panel_x + 46, panel_y + panel_h - 82,
                   "隐藏平面只持续1秒，演示完成后会自动恢复。", 22, 255, 230, 120);
    draw_text(dst, stride, panel_x + 50, panel_y + panel_h - 38,
              "VO CHN0  PLANE AUTO  FRAME COUNTER", 2, 190, 255, 230);
}

static void draw_rga_panel(uint8_t *dst, int stride, int x, int y, int w, int h,
                           const char *label, int mode, int frame) {
    int label_h = 28;
    fill_rect_nv12(dst, stride, x, y, w, h, 5, 10, 18);
    stroke_rect_nv12(dst, stride, x, y, w, h, 2, 80, 180, 255);
    fill_rect_nv12(dst, stride, x, y + h - label_h, w, label_h, 0, 0, 0);
    draw_text(dst, stride, x + 10, y + h - label_h + 7, label, 1, 180, 230, 255);

    int px = x + 18;
    int py = y + 18;
    int pw = w - 36;
    int ph = h - label_h - 36;
    if (pw <= 0 || ph <= 0) return;

    if (mode == 0) {
        for (int i = 0; i < 6; ++i) {
            int bx = px + i * pw / 6;
            int bw = pw / 6 + 1;
            fill_rect_nv12(dst, stride, bx, py, bw, ph,
                           40 + i * 30, 80 + i * 22, 220 - i * 24);
        }
        fill_rect_nv12(dst, stride, px + 24 + (frame % 60), py + ph / 3, pw / 2, ph / 5, 255, 230, 80);
        draw_text(dst, stride, px + 28, py + ph / 3 + 10, "BLIT", 2, 20, 30, 40);
    } else if (mode == 1) {
        fill_rect_nv12(dst, stride, px, py, pw, ph, 20, 40, 70);
        int cx = px + pw / 4;
        int cy = py + ph / 4;
        fill_rect_nv12(dst, stride, cx, cy, pw / 2, ph / 2, 220, 80, 60);
        stroke_rect_nv12(dst, stride, cx, cy, pw / 2, ph / 2, 4, 255, 255, 255);
        fill_rect_nv12(dst, stride, px + pw / 2 - pw / 5, py + ph / 2 - ph / 5, pw * 2 / 5, ph * 2 / 5, 255, 230, 80);
    } else if (mode == 2) {
        fill_rect_nv12(dst, stride, px, py, pw, ph, 12, 24, 42);
        fill_rect_nv12(dst, stride, px + pw / 2 - 12, py + 16, 24, ph - 32, 0, 220, 180);
        fill_rect_nv12(dst, stride, px + pw / 2 - 56, py + 20, 112, 34, 0, 220, 180);
        draw_text(dst, stride, px + pw / 2 - 70, py + ph / 2 - 12, "90", 4, 255, 230, 80);
    } else if (mode == 3) {
        fill_rect_nv12(dst, stride, px, py, pw, ph, 12, 24, 42);
        fill_rect_nv12(dst, stride, px + 28, py + ph / 2 - 16, pw - 56, 32, 0, 220, 180);
        fill_rect_nv12(dst, stride, px + 28, py + ph / 2 - 54, 42, 108, 0, 220, 180);
        draw_text(dst, stride, px + pw - 114, py + ph / 2 - 20, "MIRROR", 2, 255, 230, 80);
    } else if (mode == 4) {
        int blocks = 8;
        for (int yy = 0; yy < blocks; ++yy) {
            for (int xx = 0; xx < blocks; ++xx) {
                int r = 40 + ((xx * 31 + yy * 17 + frame) % 170);
                int g = 70 + ((xx * 13 + yy * 29) % 150);
                int b = 90 + ((xx * 23 + yy * 11) % 130);
                fill_rect_nv12(dst, stride,
                               px + xx * pw / blocks, py + yy * ph / blocks,
                               pw / blocks + 1, ph / blocks + 1,
                               (uint8_t)r, (uint8_t)g, (uint8_t)b);
            }
        }
    } else {
        fill_rect_nv12(dst, stride, px, py, pw, ph, 18, 34, 55);
        fill_rect_nv12(dst, stride, px + 18, py + 18, pw * 2 / 3, ph * 2 / 3, 40, 120, 220);
        fill_rect_nv12(dst, stride, px + pw / 3, py + ph / 3, pw / 2, ph / 2, 220, 90, 60);
        stroke_rect_nv12(dst, stride, px + 28, py + 28, pw - 56, ph - 56, 5, 0, 255, 190);
        draw_text(dst, stride, px + 44, py + 48, "OSD", 3, 255, 255, 255);
    }
}

static void draw_rga_showcase(uint8_t *dst, int stride, int x, int y, int w, int h, int frame) {
    fill_rect_nv12(dst, stride, x, y, w, h, 4, 9, 16);
    stroke_rect_nv12(dst, stride, x, y, w, h, 4, 0, 220, 180);
    draw_text(dst, stride, x + 40, y + 34, "RGA HARDWARE 2D OPS", 4, 160, 255, 220);
    draw_text(dst, stride, x + 44, y + 104, "CROP  SCALE  ROTATE  FLIP  MOSAIC  BLIT  COMPOSE", 2, 190, 230, 255);

    int grid_x = x + 36;
    int grid_y = y + 164;
    int gap = 16;
    int cell_w = (w - 72 - gap) / 2;
    int cell_h = (h - 220 - gap * 2) / 3;
    const char *labels[6] = {
        "FAST BLIT", "CROP+SCALE", "ROTATE 90",
        "FLIP H", "MOSAIC", "COMPOSE/OSD",
    };
    for (int i = 0; i < 6; ++i) {
        int cx = grid_x + (i % 2) * (cell_w + gap);
        int cy = grid_y + (i / 2) * (cell_h + gap);
        draw_rga_panel(dst, stride, cx, cy, cell_w, cell_h, labels[i], i, frame);
    }
}

static void draw_dualview_comparison(uint8_t *dst, int stride, int x, int y, int w, int h,
                                     const uint8_t *in0, const uint8_t *in1,
                                     const uint8_t *sbs, const uint8_t *lbl) {
    int gap = 10;
    int label_h = 22;
    int col_w = (w - gap) / 2;
    int row_h = (h - gap) / 2;
    const char *labels[4] = {"INPUT 0", "INPUT 1", "SIDE BY SIDE", "LINE BY LINE"};
    const uint8_t *frames[4] = {in0, in1, sbs, lbl};

    for (int i = 0; i < 4; ++i) {
        int cx = x + (i % 2) * (col_w + gap);
        int cy = y + (i / 2) * (row_h + gap);
        image_asset_t frame_img = {CAM_W, CAM_H, (uint8_t *)frames[i]};

        fill_rect_nv12(dst, stride, cx, cy, col_w, row_h, 5, 10, 18);
        stroke_rect_nv12(dst, stride, cx, cy, col_w, row_h, 1,
                         i >= 2 ? 80 : 70, i >= 2 ? 255 : 140, i >= 2 ? 180 : 210);
        if (frames[i]) {
            draw_rgb_image_nv12(dst, stride, cx + 4, cy + 4, col_w - 8, row_h - label_h - 8,
                                &frame_img);
        } else {
            draw_text(dst, stride, cx + 12, cy + row_h / 2 - 10, "WAIT", 1, 255, 190, 100);
        }
        fill_rect_nv12(dst, stride, cx, cy + row_h - label_h, col_w, label_h, 0, 0, 0);
        draw_text(dst, stride, cx + 8, cy + row_h - label_h + 5, labels[i], 1, 180, 230, 255);
    }
}

static void draw_mcf_comparison(uint8_t *dst, int stride, int x, int y, int w, int h,
                                const uint8_t *color, const uint8_t *mono,
                                const uint8_t *fused) {
    int gap = 10;
    int label_h = 22;
    int top_h = (h - gap) / 2;
    int bottom_h = h - top_h - gap;
    int col_w = (w - gap) / 2;

    const uint8_t *top_frames[2] = {color, mono};
    const char *top_labels[2] = {"COLOR INPUT", "MONO DETAIL"};
    for (int i = 0; i < 2; ++i) {
        int cx = x + i * (col_w + gap);
        int cy = y;
        fill_rect_nv12(dst, stride, cx, cy, col_w, top_h, 5, 10, 18);
        stroke_rect_nv12(dst, stride, cx, cy, col_w, top_h, 1, 70, 140, 210);
        if (top_frames[i]) {
            draw_camera_tile_fit(dst, stride, cx + 4, cy + 4, col_w - 8,
                                 top_h - label_h - 8, top_frames[i],
                                 CAM_W, CAM_H, CAM_STRIDE);
        } else {
            draw_text(dst, stride, cx + 12, cy + top_h / 2 - 10,
                      "WAIT", 1, 255, 190, 100);
        }
        fill_rect_nv12(dst, stride, cx, cy + top_h - label_h, col_w, label_h, 0, 0, 0);
        draw_text(dst, stride, cx + 8, cy + top_h - label_h + 5,
                  top_labels[i], 1, 180, 230, 255);
    }

    int oy = y + top_h + gap;
    fill_rect_nv12(dst, stride, x, oy, w, bottom_h, 5, 10, 18);
    stroke_rect_nv12(dst, stride, x, oy, w, bottom_h, 1, 80, 255, 180);
    if (fused) {
        draw_camera_tile_fit(dst, stride, x + 4, oy + 4, w - 8,
                             bottom_h - label_h - 8, fused,
                             CAM_W, CAM_H, CAM_STRIDE);
    } else {
        draw_text(dst, stride, x + 12, oy + bottom_h / 2 - 10,
                  "WAIT", 1, 255, 190, 100);
    }
    fill_rect_nv12(dst, stride, x, oy + bottom_h - label_h, w, label_h, 0, 0, 0);
    draw_text(dst, stride, x + 8, oy + bottom_h - label_h + 5,
              "MCF OUTPUT", 1, 180, 230, 255);
}

static void draw_pano_comparison(uint8_t *dst, int stride, int x, int y, int w, int h,
                                 const uint8_t *pano_out) {
    int gap = 10;
    int label_h = 22;
    int top_h = (h * 2) / 5;
    int bottom_h = h - top_h - gap;
    int cols = 3;
    int rows = 2;
    int cell_w = (w - gap * (cols - 1)) / cols;
    int cell_h = (top_h - gap * (rows - 1)) / rows;

    for (int i = 0; i < PANO_INPUT_COUNT; ++i) {
        int cx = x + (i % cols) * (cell_w + gap);
        int cy = y + (i / cols) * (cell_h + gap);
        char label[16];
        snprintf(label, sizeof(label), "INPUT %d", i);
        fill_rect_nv12(dst, stride, cx, cy, cell_w, cell_h, 5, 10, 18);
        stroke_rect_nv12(dst, stride, cx, cy, cell_w, cell_h, 1, 70, 140, 210);
        if (g_pano_sample.loaded) {
            draw_rgb_image_nv12(dst, stride, cx + 4, cy + 4, cell_w - 8, cell_h - label_h - 8,
                                &g_pano_sample.inputs[i]);
        } else {
            draw_text(dst, stride, cx + 12, cy + cell_h / 2 - 10, "WAIT", 1, 255, 190, 100);
        }
        fill_rect_nv12(dst, stride, cx, cy + cell_h - label_h, cell_w, label_h, 0, 0, 0);
        draw_text(dst, stride, cx + 8, cy + cell_h - label_h + 5, label, 1, 180, 230, 255);
    }

    int oy = y + top_h + gap;
    fill_rect_nv12(dst, stride, x, oy, w, bottom_h, 5, 10, 18);
    stroke_rect_nv12(dst, stride, x, oy, w, bottom_h, 1, 80, 255, 180);
    if (pano_out) {
        int fx = 0, fy = 0, fw = 0, fh = 0;
        if (draw_camera_tile_fit_rect(dst, stride, x + 4, oy + 4,
                                      w - 8, bottom_h - label_h - 8,
                                      pano_out, PANO_STITCH_W, PANO_STITCH_H, PANO_STITCH_STRIDE,
                                      &fx, &fy, &fw, &fh) == 0) {
            stroke_rect_nv12(dst, stride, fx, fy, fw, fh, 1, 120, 255, 210);
        }
    } else {
        draw_text(dst, stride, x + 12, oy + bottom_h / 2 - 10, "WAIT", 1, 255, 190, 100);
    }
    fill_rect_nv12(dst, stride, x, oy + bottom_h - label_h, w, label_h, 0, 0, 0);
    draw_text(dst, stride, x + 8, oy + bottom_h - label_h + 5, "PANORAMA OUTPUT", 1, 180, 230, 255);
}

static void draw_showcase_header(uint8_t *dst, int stride, int x, int y, int w,
                                 const char *title, const char *flow, const char *why) {
    fill_rect_nv12(dst, stride, x, y, w, 126, 6, 13, 24);
    stroke_rect_nv12(dst, stride, x, y, w, 126, 2, 66, 150, 210);
    draw_utf8_text(dst, stride, x + 24, y + 18, title, 28, 170, 255, 220);
    draw_utf8_text(dst, stride, x + 24, y + 62, flow, 20, 210, 235, 255);
    draw_utf8_text(dst, stride, x + 24, y + 94, why, 20, 255, 230, 120);
}

static void draw_showcase_footer(uint8_t *dst, int stride, int x, int y, int w,
                                 const char *detail) {
    char runtime[128];
    format_runtime_status(runtime, sizeof(runtime));

    fill_rect_nv12(dst, stride, x, y, w, 156, 5, 10, 18);
    stroke_rect_nv12(dst, stride, x, y, w, 156, 2, 0, 190, 170);
    draw_utf8_text(dst, stride, x + 24, y + 22, detail, 22, 170, 255, 220);
    draw_text(dst, stride, x + 24, y + 76, runtime, 2, 255, 230, 120);
}

static void draw_conv_cl_showcase(uint8_t *dst, int stride, int x, int y, int w, int h,
                                  const uint8_t *conv_grid) {
    int top_h = 126;
    int bottom_h = 218;
    int gap = 18;
    int grid_max_w = w - 48;
    int grid_max_h = h - top_h - bottom_h - gap * 2;
    int grid_size = grid_max_w < grid_max_h ? grid_max_w : grid_max_h;
    if (grid_size > 980) grid_size = 980;
    if (grid_size < 360) return;
    grid_size &= ~1;

    draw_showcase_header(dst, stride, x, y, w,
                         "CONV_CL：四种卷积核同屏比较",
                         "数据流：VI采集NV12 -> RGBA staging -> 单个CONV_CL顺序加载四个核 -> 页面合成 -> VO。",
                         "为什么这样做：同源画面只改变卷积核，稳定展示锐化、边缘、浮雕和模糊差异。");

    int grid_x = x + (w - grid_size) / 2;
    int grid_y = y + top_h + gap;
    fill_rect_nv12(dst, stride, grid_x - 6, grid_y - 6, grid_size + 12, grid_size + 12,
                   5, 10, 18);
    if (conv_grid) {
        draw_rgba_frame_nv12(dst, stride, grid_x, grid_y, grid_size, grid_size,
                             conv_grid, CAM_W, CAM_H, CAM_W * 4);
    } else {
        fill_rect_nv12(dst, stride, grid_x, grid_y, grid_size, grid_size, 10, 18, 30);
        draw_text(dst, stride, grid_x + 24, grid_y + 24, "WAITING CONV_CL", 2, 255, 230, 120);
    }

    const uint8_t colors[VPSS_DEMO_OUTPUTS][3] = {
        {70, 170, 255}, {255, 110, 120}, {255, 210, 90}, {110, 230, 170},
    };
    int cell = grid_size / 2;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        int cx = grid_x + (i % 2) * cell;
        int cy = grid_y + (i / 2) * cell;
        char out_line[64];
        snprintf(out_line, sizeof(out_line), "%s OUT %llu",
                 g_conv_cl_effects[i].name,
                 (unsigned long long)g_conv_cl_show_counts[i]);
        fill_rect_nv12(dst, stride, cx + 14, cy + 14, 238, 38, 0, 0, 0);
        fill_rect_nv12(dst, stride, cx + 14, cy + 14, 238, 5,
                       colors[i][0], colors[i][1], colors[i][2]);
        draw_text(dst, stride, cx + 26, cy + 28, out_line, 1, 230, 245, 255);
        stroke_rect_nv12(dst, stride, cx, cy, cell, cell, 2,
                         colors[i][0], colors[i][1], colors[i][2]);
    }

    char runtime[128];
    char cl_line[96];
    char count_line[192];
    format_runtime_status(runtime, sizeof(runtime));
    if (g_conv_cl_show_kernel_ms >= 0.0) {
        snprintf(cl_line, sizeof(cl_line), "CL %.2f/%.2fms",
                 g_conv_cl_show_kernel_ms,
                 g_conv_cl_show_queue_ms >= 0.0 ? g_conv_cl_show_queue_ms : 0.0);
    } else {
        snprintf(cl_line, sizeof(cl_line), "CL N/A");
    }
    snprintf(count_line, sizeof(count_line), "%s=%llu  %s=%llu  %s=%llu  %s=%llu",
             g_conv_cl_effects[0].name, (unsigned long long)g_conv_cl_show_counts[0],
             g_conv_cl_effects[1].name, (unsigned long long)g_conv_cl_show_counts[1],
             g_conv_cl_effects[2].name, (unsigned long long)g_conv_cl_show_counts[2],
             g_conv_cl_effects[3].name, (unsigned long long)g_conv_cl_show_counts[3]);

    int footer_y = y + h - bottom_h;
    fill_rect_nv12(dst, stride, x, footer_y, w, bottom_h, 5, 10, 18);
    stroke_rect_nv12(dst, stride, x, footer_y, w, bottom_h, 2, 0, 190, 170);
    draw_utf8_text(dst, stride, x + 24, footer_y + 22,
                   "展示重点：同一帧画面分成四格，四格都来自真实CONV_CL输出，不是滤镜贴图。",
                   22, 170, 255, 220);
    draw_text(dst, stride, x + 24, footer_y + 74, runtime, 2, 255, 230, 120);
    draw_text(dst, stride, x + 24, footer_y + 124, cl_line, 2, 255, 210, 120);
    draw_text(dst, stride, x + 24, footer_y + 170, count_line, 1, 190, 230, 255);
}

static void draw_edof_showcase(uint8_t *dst, int stride, int x, int y, int w, int h,
                               const uint8_t *left, const uint8_t *right, const uint8_t *out) {
    int top_h = 126;
    int bottom_h = 156;
    int gap = 12;
    int compare_y = y + top_h + gap;
    int compare_h = h - top_h - bottom_h - gap * 2;
    if (compare_h < 320) return;

    draw_showcase_header(dst, stride, x, y, w,
                         "EDOF_CL：多焦点融合前后对比",
                         "数据流：近焦图 + 远焦图进入EDOF_CL融合流程，右侧显示清晰融合结果。",
                         "为什么这样做：GPU根据清晰度权重融合近景和远景，扩展景深。");
    draw_edof_comparison(dst, stride, x, compare_y, w, compare_h, left, right, out);
    draw_showcase_footer(dst, stride, x, y + h - bottom_h, w,
                         "展示重点：每3秒切换一组样张，输出保留两张输入中更清晰的区域。");
}

static void draw_mcf_showcase(uint8_t *dst, int stride, int x, int y, int w, int h,
                              const uint8_t *color, const uint8_t *mono,
                              const uint8_t *out) {
    int top_h = 126;
    int bottom_h = 156;
    int gap = 12;
    int compare_y = y + top_h + gap;
    int compare_h = h - top_h - bottom_h - gap * 2;
    if (compare_h < 320) return;

    draw_showcase_header(dst, stride, x, y, w,
                         "MCF_FUSION_CL：彩色/单色细节融合",
                         "数据流：彩色图 + 单色细节图 -> MCF_FUSION_CL -> 彩色增强输出。",
                         "为什么这样做：GPU在Y通道注入单色高频细节，保留彩色观感。");
    draw_mcf_comparison(dst, stride, x, compare_y, w, compare_h, color, mono, out);
    draw_showcase_footer(dst, stride, x, y + h - bottom_h, w,
                         "展示重点：每3秒切换一组样张，真实模块可用时显示OpenCL输出，否则显示参考融合结果。");
}

static void draw_dualview_showcase(uint8_t *dst, int stride, int x, int y, int w, int h,
                                   const uint8_t *in0, const uint8_t *in1,
                                   const uint8_t *sbs, const uint8_t *lbl) {
    int top_h = 126;
    int bottom_h = 156;
    int gap = 12;
    int compare_y = y + top_h + gap;
    int compare_h = h - top_h - bottom_h - gap * 2;
    if (compare_h < 320) return;

    draw_showcase_header(dst, stride, x, y, w,
                         "DUALVIEW：双输入双输出格式演示",
                         "数据流：红/蓝两路RGB输入 -> DUALVIEW -> 左右并排和逐行交错。",
                         "为什么这样做：同一组输入可直接生成不同双视图显示格式。");
    draw_dualview_comparison(dst, stride, x, compare_y, w, compare_h, in0, in1, sbs, lbl);
    draw_showcase_footer(dst, stride, x, y + h - bottom_h, w,
                         "展示重点：input0/input1、side-by-side、line-by-line 四路同屏比较。");
}

static void draw_pano_showcase(uint8_t *dst, int stride, int x, int y, int w, int h,
                               const uint8_t *pano_out) {
    int top_h = 126;
    int bottom_h = 156;
    int gap = 12;
    int compare_y = y + top_h + gap;
    int compare_h = h - top_h - bottom_h - gap * 2;
    if (compare_h < 320) return;

    draw_showcase_header(dst, stride, x, y, w,
                         "PANO：六路输入全景拼接",
                         "数据流：6张标定图 + PTO标定 -> PANO -> 1920x960全景预览输出。",
                         "为什么这样做：PTO查表映射完整全景域，再输出客户页预览，适合多摄像头环视。");
    draw_pano_comparison(dst, stride, x, compare_y, w, compare_h, pano_out);
    draw_showcase_footer(dst, stride, x, y + h - bottom_h, w,
                         "展示重点：上方是六路输入，下方是同一标定文件拼接后的全景结果。");
}

static const char *tile_status_text(int status) {
    switch (status) {
    case TILE_LIVE: return "LIVE";
    case TILE_PROBED: return "PROBED";
    case TILE_LOOP: return "LOOP";
    case TILE_SYNTH: return "SYNTH";
    default: return "OFFLINE";
    }
}

static void tile_status_color(int status, uint8_t *r, uint8_t *g, uint8_t *b) {
    switch (status) {
    case TILE_LIVE:
        *r = 80; *g = 255; *b = 180; return;
    case TILE_PROBED:
        *r = 120; *g = 190; *b = 255; return;
    case TILE_LOOP:
        *r = 255; *g = 220; *b = 100; return;
    case TILE_SYNTH:
        *r = 160; *g = 180; *b = 205; return;
    default:
        *r = 255; *g = 120; *b = 90; return;
    }
}

static void draw_effect_tile(uint8_t *dst, int stride, int x, int y, int w, int h,
                             int idx, int frame, int active, const uint8_t *osd_live,
                             const uint8_t *resize_live, const uint8_t *vpss_live,
                             const uint8_t *csc_rga_live, const uint8_t *transform_live,
                             const uint8_t *cap_live, const uint8_t *dcp_live,
                             const uint8_t *conv_live, const uint8_t *clahe_live,
                             const uint8_t *retinex_in, const uint8_t *retinex_live,
                             const uint8_t *pano_out,
                             const uint8_t *edof_in0, const uint8_t *edof_in1,
                             const uint8_t *edof_out,
                             const uint8_t *mcf_color, const uint8_t *mcf_mono,
                             const uint8_t *mcf_out, const uint8_t *stereo_live,
                             const uint8_t *dual_in0, const uint8_t *dual_in1,
                             const uint8_t *dual_sbs, const uint8_t *dual_lbl) {
    uint8_t r0 = (uint8_t)((idx * 47 + frame * 2) % 180 + 40);
    uint8_t g0 = (uint8_t)((idx * 83 + frame * 3) % 180 + 40);
    uint8_t b0 = (uint8_t)((idx * 29 + frame * 5) % 180 + 40);
    uint8_t sr, sg, sb;
    tile_status_color(g_tiles[idx].status, &sr, &sg, &sb);
    fill_rect_nv12(dst, stride, x, y, w, h, 6, 12, 20);
    loop_asset_t *loop = find_loop_asset(g_tiles[idx].name);
    if (strcmp(g_tiles[idx].name, "VI") == 0 && retinex_in) {
        draw_camera_tile_fit(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             retinex_in, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "LIVE VI INPUT", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "OSD") == 0 && osd_live) {
        draw_camera_tile_fit(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             osd_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > OSD", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "VPSS") == 0 && vpss_live) {
        int vx = 0, vy = 0, vw = 0, vh = 0;
        if (draw_camera_tile_fit_rect(dst, stride, x + 6, y + 30, w - 12, h - 38,
                                      vpss_live, CAM_W, CAM_H, CAM_STRIDE,
                                      &vx, &vy, &vw, &vh) == 0) {
            draw_vpss_showcase_labels(dst, stride, vx, vy, vw, vh);
        }
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "VPSS MULTI-OUT", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "RESIZE_RGA") == 0 && resize_live) {
        draw_camera_tile_fit(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             resize_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > RESIZE", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "CSC_RGA") == 0 && csc_rga_live) {
        draw_camera_tile_fit(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             csc_rga_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > CSC_RGA", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "TRANSFORM") == 0 && transform_live) {
        draw_camera_tile_fit(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             transform_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH > TRANSFORM", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "CAP_DEHAZE") == 0 && cap_live) {
        image_asset_t rgb_frame = {CAM_W, CAM_H, (uint8_t *)cap_live};
        draw_rgb_image_nv12(dst, stride, x + 6, y + 30, w - 12, h - 38, &rgb_frame);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH RGB > CAP", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "DCP_FAST_DEHAZE") == 0 && (retinex_in || dcp_live)) {
        draw_nv12_rgb_comparison(dst, stride, x + 6, y + 30, w - 12, h - 38,
                                 retinex_in, dcp_live, "VI RAW", "DCP OUT");
    } else if (strcmp(g_tiles[idx].name, "CONV_CL") == 0 && conv_live) {
        draw_rgba_frame_nv12(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             conv_live, CAM_W, CAM_H, CAM_W * 4);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH RGBA > CONV", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "CLAHE") == 0 && clahe_live) {
        draw_camera_tile_fit(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             clahe_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH NV12 > CLAHE", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "RETINEX") == 0 && (retinex_in || retinex_live)) {
        draw_nv12_comparison(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             retinex_in, retinex_live, "VIDEO IN", "RETINEX OUT");
    } else if (strcmp(g_tiles[idx].name, "PANO") == 0 && (g_pano_sample.loaded || pano_out)) {
        draw_pano_comparison(dst, stride, x + 6, y + 30, w - 12, h - 38, pano_out);
    } else if (strcmp(g_tiles[idx].name, "EDOF_CL") == 0 && (edof_in0 || edof_in1 || edof_out)) {
        draw_edof_comparison(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             edof_in0, edof_in1, edof_out);
    } else if (strcmp(g_tiles[idx].name, "MCF_FUSION_CL") == 0 &&
               (mcf_color || mcf_mono || mcf_out)) {
        draw_mcf_comparison(dst, stride, x + 6, y + 30, w - 12, h - 38,
                            mcf_color, mcf_mono, mcf_out);
    } else if (strcmp(g_tiles[idx].name, "DUALVIEW") == 0 &&
               (dual_in0 || dual_in1 || dual_sbs || dual_lbl)) {
        draw_dualview_comparison(dst, stride, x + 6, y + 30, w - 12, h - 38,
                                 dual_in0, dual_in1, dual_sbs, dual_lbl);
    } else if (strcmp(g_tiles[idx].name, "STEREO_3D") == 0 && stereo_live) {
        draw_camera_tile_fit(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             stereo_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > STEREO", 1, 220, 255, 230);
    } else if (loop && loop->loaded_count > 0) {
        const image_asset_t *img = &loop->images[(frame / 30) % loop->loaded_count];
        draw_rgb_image_nv12(dst, stride, x + 6, y + 30, w - 12, h - 38, img);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, loop->caption, 1, 220, 255, 230);
    } else {
        fill_rect_nv12(dst, stride, x + 14, y + 48, 44, 44, r0, g0, b0);
        fill_rect_nv12(dst, stride, x + 66, y + 52, w - 82, 12, 16, 28, 42);
        fill_rect_nv12(dst, stride, x + 66, y + 74, (w - 82) * 3 / 4, 12, 16, 28, 42);
        if (g_tiles[idx].status == TILE_PROBED) {
            draw_text(dst, stride, x + 14, y + 108, "INIT OK", 1, 190, 230, 255);
            draw_text(dst, stride, x + 14, y + 126, "NO LIVE OUTPUT", 1, 140, 170, 200);
        } else if (g_tiles[idx].status == TILE_LIVE) {
            draw_text(dst, stride, x + 14, y + 108, "LIVE MODULE", 1, 190, 255, 220);
            draw_text(dst, stride, x + 14, y + 126, "REAL FRAMES", 1, 140, 210, 180);
        } else {
            draw_text(dst, stride, x + 14, y + 108, "SYNTHETIC FEED", 1, 190, 230, 255);
            draw_text(dst, stride, x + 14, y + 126, "NO LIVE ALGO", 1, 140, 170, 200);
        }
    }
    stroke_rect_nv12(dst, stride, x, y, w, h, active ? 3 : 1,
                     active ? sr : 70, active ? sg : 70, active ? sb : 70);
    int name_scale = strlen(g_tiles[idx].name) > 11 ? 1 : 2;
    draw_text(dst, stride, x + 10, y + 10, g_tiles[idx].name, name_scale,
              active ? 170 : 90, active ? 255 : 90, active ? 220 : 90);
    fill_rect_nv12(dst, stride, x + w - 74, y + 8, 64, 20, 0, 0, 0);
    draw_text(dst, stride, x + w - 68, y + 12, tile_status_text(g_tiles[idx].status), 1, sr, sg, sb);
}

static void draw_tile_content(uint8_t *dst, int stride, int x, int y, int w, int h,
                              int idx, int frame, const uint8_t *cam, const uint8_t *osd_live,
                              const uint8_t *resize_live, const uint8_t *vpss_live,
                              const uint8_t *csc_rga_live, const uint8_t *transform_live,
                              const uint8_t *cap_live, const uint8_t *dcp_live,
                              const uint8_t *conv_live, const uint8_t *clahe_live,
                              const uint8_t *retinex_live,
                              const uint8_t *pano_out,
                              const uint8_t *edof_in0, const uint8_t *edof_in1,
                              const uint8_t *edof_out,
                              const uint8_t *mcf_color, const uint8_t *mcf_mono,
                              const uint8_t *mcf_out, const uint8_t *stereo_live,
                              const uint8_t *dual_in0, const uint8_t *dual_in1,
                              const uint8_t *dual_sbs, const uint8_t *dual_lbl) {
    loop_asset_t *loop = find_loop_asset(g_tiles[idx].name);
    fill_rect_nv12(dst, stride, x, y, w, h, 7, 13, 24);

    if (strcmp(g_tiles[idx].name, "VO") == 0) {
        draw_vo_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24, frame);
        return;
    }

    if (strcmp(g_tiles[idx].name, "RGA") == 0) {
        draw_rga_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24, frame);
        return;
    }

    if (strcmp(g_tiles[idx].name, "OSD") == 0 && osd_live) {
        draw_camera_tile_fit(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             osd_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "VPSS") == 0 && vpss_live) {
        int vx = 0, vy = 0, vw = 0, vh = 0;
        if (draw_camera_tile_fit_rect(dst, stride, x + 12, y + 12, w - 24, h - 24,
                                      vpss_live, CAM_W, CAM_H, CAM_STRIDE,
                                      &vx, &vy, &vw, &vh) == 0) {
            draw_vpss_showcase_labels(dst, stride, vx, vy, vw, vh);
        }
        return;
    }

    if (strcmp(g_tiles[idx].name, "RESIZE_RGA") == 0 && resize_live) {
        draw_camera_tile_fit(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             resize_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "CSC_RGA") == 0 && csc_rga_live) {
        draw_camera_tile_fit(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             csc_rga_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "TRANSFORM") == 0 && transform_live) {
        draw_camera_tile_fit(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             transform_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "CAP_DEHAZE") == 0 && (cam || cap_live)) {
        draw_dehaze_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             cam, cap_live,
                             "CAP_DEHAZE：实时去雾前后对比",
                             "数据流：VI 640x640 NV12 -> RGB -> CAP_DEHAZE -> 上下对比显示。",
                             "为什么这样做：快速估计雾气透射率和空气光，突出低能见度场景细节。",
                             "PARAM guided_r=24 t0=0.12 beta=0.121/0.960/-0.780",
                             "BOTTOM: CAP_DEHAZE OUTPUT");
        return;
    }

    if (strcmp(g_tiles[idx].name, "DCP_FAST_DEHAZE") == 0 && (cam || dcp_live)) {
        draw_dehaze_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             cam, dcp_live,
                             "DCP_FAST_DEHAZE：暗通道去雾前后对比",
                             "数据流：VI 640x640 NV12 -> RGB -> DCP_FAST_DEHAZE -> 上下对比显示。",
                             "为什么这样做：暗通道先验估计雾浓度，用快速细化保持实时展示。",
                             "PARAM patch=15 omega=0.95 t0=0.12 refine=0.5",
                             "BOTTOM: DCP_FAST OUTPUT");
        return;
    }

    if (strcmp(g_tiles[idx].name, "THERMAL") == 0 && loop && loop->loaded_count > 0) {
        draw_thermal_mode_grid(dst, stride, x + 12, y + 12, w - 24, h - 24, frame);
        return;
    }

    if (strcmp(g_tiles[idx].name, "CONV_CL") == 0 && conv_live) {
        draw_conv_cl_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24,
                              conv_live);
        return;
    }

    if (strcmp(g_tiles[idx].name, "CLAHE") == 0 && clahe_live) {
        draw_camera_tile_fit(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             clahe_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "RETINEX") == 0 && (cam || retinex_live)) {
        draw_nv12_comparison(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             cam, retinex_live, "VIDEO IN", "RETINEX OUT");
        return;
    }

    if (strcmp(g_tiles[idx].name, "PANO") == 0 && (g_pano_sample.loaded || pano_out)) {
        draw_pano_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24, pano_out);
        return;
    }

    if (strcmp(g_tiles[idx].name, "EDOF_CL") == 0 && (edof_in0 || edof_in1 || edof_out)) {
        draw_edof_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24,
                           edof_in0, edof_in1, edof_out);
        return;
    }

    if (strcmp(g_tiles[idx].name, "MCF_FUSION_CL") == 0 &&
        (mcf_color || mcf_mono || mcf_out)) {
        draw_mcf_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24,
                          mcf_color, mcf_mono, mcf_out);
        return;
    }

    if (strcmp(g_tiles[idx].name, "DUALVIEW") == 0 &&
        (dual_in0 || dual_in1 || dual_sbs || dual_lbl)) {
        draw_dualview_showcase(dst, stride, x + 12, y + 12, w - 24, h - 24,
                               dual_in0, dual_in1, dual_sbs, dual_lbl);
        return;
    }

    if (strcmp(g_tiles[idx].name, "STEREO_3D") == 0 && stereo_live) {
        draw_camera_tile_fit(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             stereo_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "VI") == 0 && cam) {
        draw_camera_tile_fit(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             cam, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (loop && loop->loaded_count > 0) {
        const image_asset_t *img = &loop->images[(frame / FPS) % loop->loaded_count];
        draw_rgb_image_nv12(dst, stride, x + 12, y + 12, w - 24, h - 24, img);
        return;
    }

    uint8_t sr, sg, sb;
    tile_status_color(g_tiles[idx].status, &sr, &sg, &sb);
    fill_rect_nv12(dst, stride, x + 60, y + 70, w - 120, 150, 10, 22, 34);
    stroke_rect_nv12(dst, stride, x + 60, y + 70, w - 120, 150, 5, sr, sg, sb);
    draw_text(dst, stride, x + 92, y + 114, g_tiles[idx].name, strlen(g_tiles[idx].name) > 11 ? 3 : 4, 190, 255, 230);
    draw_text(dst, stride, x + 92, y + 198,
              g_tiles[idx].status == TILE_PROBED ? "INIT OK  NO LIVE OUTPUT" : "SYNTHETIC PLACEHOLDER",
              2, 170, 210, 240);

    const char *state =
        g_tiles[idx].status == TILE_PROBED ? "API PROBE PASSED" :
        g_tiles[idx].status == TILE_LIVE ? "LIVE FRAMES" : "OFFLINE";
    const char *source =
        g_tiles[idx].status == TILE_PROBED ? "NO DISPLAY FRAME API" :
        g_tiles[idx].status == TILE_LIVE ? "MODULE OUTPUT" : "WAITING FOR SAFE PATH";
    const char *mode =
        loop ? "LOOP ASSET" : "MODULE TILE";
    int row_y = y + h - 206;
    for (int i = 0; i < 3; ++i) {
        const char *label = i == 0 ? "STATE" : (i == 1 ? "SOURCE" : "MODE");
        const char *value = i == 0 ? state : (i == 1 ? source : mode);
        fill_rect_nv12(dst, stride, x + 88, row_y + i * 50, w - 176, 34, 8, 18, 30);
        draw_text(dst, stride, x + 110, row_y + 8 + i * 50, label, 1, 110, 170, 210);
        draw_text(dst, stride, x + 250, row_y + 8 + i * 50, value, 1, 190, 230, 255);
    }
}

static void draw_main_showcase(uint8_t *canvas, int stride, int frame,
                               int rotate_main, const char *only_tile,
                               const uint8_t *cam, const uint8_t *osd_live,
                               const uint8_t *resize_live, const uint8_t *vpss_live,
                               const uint8_t *csc_rga_live, const uint8_t *transform_live,
                               const uint8_t *cap_live, const uint8_t *dcp_live,
                               const uint8_t *conv_live, const uint8_t *clahe_live,
                               const uint8_t *retinex_live,
                               const uint8_t *pano_out,
                               const uint8_t *edof_in0, const uint8_t *edof_in1,
                               const uint8_t *edof_out,
                               const uint8_t *mcf_color, const uint8_t *mcf_mono,
                               const uint8_t *mcf_out, const uint8_t *stereo_live,
                               const uint8_t *dual_in0, const uint8_t *dual_in1,
                               const uint8_t *dual_sbs, const uint8_t *dual_lbl) {
    const int x = 28;
    const int y = 116;
    const int w = 1024;
    const int h = 612;
    int idx = -1;

    if (only_tile) {
        idx = find_tile_index(only_tile);
    } else if (rotate_main) {
        idx = TILE_FIRST_INDEX + (frame / (FPS * MAIN_ROTATE_SECONDS)) % TILE_ROTATE_COUNT;
    }

    fill_rect_nv12(canvas, stride, x, y, w, h, 7, 13, 24);
    if (idx >= 0) {
        uint8_t sr, sg, sb;
        tile_status_color(g_tiles[idx].status, &sr, &sg, &sb);
        draw_tile_content(canvas, stride, x + 14, y + 14, w - 28, h - 70,
                          idx, frame, cam, osd_live, resize_live, vpss_live,
                          csc_rga_live, transform_live, cap_live, dcp_live, conv_live, clahe_live,
                          retinex_live, pano_out, edof_in0, edof_in1, edof_out,
                          mcf_color, mcf_mono, mcf_out, stereo_live,
                          dual_in0, dual_in1, dual_sbs, dual_lbl);
        fill_rect_nv12(canvas, stride, x + 14, y + h - 50, w - 28, 34, 0, 0, 0);
        draw_text(canvas, stride, x + 32, y + h - 42, "MAIN ROTATE", 2, 190, 230, 255);
        draw_text(canvas, stride, x + 268, y + h - 42, g_tiles[idx].name,
                  strlen(g_tiles[idx].name) > 11 ? 1 : 2, 170, 255, 220);
        draw_text(canvas, stride, x + 786, y + h - 42, tile_status_text(g_tiles[idx].status), 2, sr, sg, sb);
    } else if (cam) {
        draw_camera_tile_fit(canvas, stride, 42, 130, 996, 560, cam, CAM_W, CAM_H, CAM_STRIDE);
        draw_text(canvas, stride, 52, 704, "LIVE VISIBLE CAMERA  VI > VPSS > WALL", 2, 190, 255, 230);
    } else {
        draw_text(canvas, stride, 294, 336, "CAMERA OFFLINE", 4, 255, 180, 80);
        draw_text(canvas, stride, 318, 406, CAMERA_DEVICE, 2, 190, 230, 255);
    }
    stroke_rect_nv12(canvas, stride, x, y, w, h, 4, 0, 220, 180);
}

static int map_buffer(MEDIA_BUFFER buf, void **addr, size_t *size, int prot) {
    int fd = -1;
    if (MEDIA_POOL_GetFd(buf, &fd, size) != 0 || fd < 0 || *size == 0) return -1;
    *addr = mmap(NULL, *size, prot, MAP_SHARED, fd, 0);
    if (*addr == MAP_FAILED) {
        *addr = NULL;
        return -1;
    }
    return 0;
}

static void fill_synthetic_nv12(uint8_t *p, int w, int h, int stride, int frame) {
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            p[yy * stride + xx] = (uint8_t)((xx * 2 + yy + frame * 5) & 255);
        }
    }
    uint8_t *uv = p + stride * h;
    for (int yy = 0; yy < h / 2; ++yy) {
        for (int xx = 0; xx < w; xx += 2) {
            uv[yy * stride + xx] = (uint8_t)(90 + ((xx + frame) & 63));
            uv[yy * stride + xx + 1] = (uint8_t)(130 + ((yy * 2 + frame) & 63));
        }
    }
}

static void fill_synthetic_rgb(uint8_t *p, int w, int h, int stride, int frame) {
    for (int yy = 0; yy < h; ++yy) {
        uint8_t *row = p + (size_t)yy * stride;
        for (int xx = 0; xx < w; ++xx) {
            row[xx * 3 + 0] = (uint8_t)((xx * 2 + frame * 4) & 255);
            row[xx * 3 + 1] = (uint8_t)((yy * 2 + frame * 3) & 255);
            row[xx * 3 + 2] = (uint8_t)(((xx + yy) + frame * 5) & 255);
        }
    }
}

static void fill_synthetic_rgba(uint8_t *p, int w, int h, int stride, int frame) {
    for (int yy = 0; yy < h; ++yy) {
        uint8_t *row = p + (size_t)yy * stride;
        for (int xx = 0; xx < w; ++xx) {
            row[xx * 4 + 0] = (uint8_t)((xx * 2 + frame * 4) & 255);
            row[xx * 4 + 1] = (uint8_t)((yy * 2 + frame * 3) & 255);
            row[xx * 4 + 2] = (uint8_t)(((xx + yy) + frame * 5) & 255);
            row[xx * 4 + 3] = 255;
        }
    }
}

static MEDIA_BUFFER make_work_frame(int pool, int w, int h, int stride, int frame) {
    MEDIA_BUFFER buf = {-1, -1};
    if (MEDIA_POOL_GetBuffer(pool, &buf) != 0) return buf;
    void *addr = NULL;
    size_t size = 0;
    if (map_buffer(buf, &addr, &size, PROT_READ | PROT_WRITE) != 0) {
        MEDIA_POOL_PutBuffer(buf);
        buf.pool_id = -1;
        return buf;
    }
    if (MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_WRITE) != 0) {
        munmap(addr, size);
        MEDIA_POOL_PutBuffer(buf);
        buf.pool_id = -1;
        return buf;
    }
    (void)size;
    fill_synthetic_nv12((uint8_t *)addr, w, h, stride, frame);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    munmap(addr, size);
    return buf;
}

static int copy_to_buffer(MEDIA_BUFFER buf, const uint8_t *src, size_t need) {
    void *addr = NULL;
    size_t size = 0;
    if (!src) return -1;
    if (map_buffer(buf, &addr, &size, PROT_READ | PROT_WRITE) != 0) return -1;
    if (MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_WRITE) != 0) {
        munmap(addr, size);
        return -1;
    }
    if (size < need) {
        (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
        munmap(addr, size);
        return -1;
    }
    memcpy(addr, src, need);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    munmap(addr, size);
    return 0;
}

static int copy_from_buffer(MEDIA_BUFFER buf, uint8_t *dst, size_t need) {
    void *addr = NULL;
    size_t size = 0;
    if (!dst) return -1;
    if (MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_READ) != 0) return -1;
    if (map_buffer(buf, &addr, &size, PROT_READ) != 0) {
        (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
        return -1;
    }
    if (size < need) {
        munmap(addr, size);
        (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
        return -1;
    }
    memcpy(dst, addr, need);
    munmap(addr, size);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
    return 0;
}

static int send_copied_frame(const char *module, int grp, const char *port,
                             int pool, const uint8_t *src, size_t need, int timeout_ms) {
    MEDIA_BUFFER in = {-1, -1};
    if (MEDIA_POOL_GetBuffer(pool, &in) != 0) return -1;
    if (copy_to_buffer(in, src, need) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame(module, grp, port, in, timeout_ms) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);
    return 0;
}

static int update_display_osd_text(const char *title, const char *perf) {
    static uint8_t title_mask[1024 * 64];
    static uint8_t perf_mask[1024 * 64];
    char title_buf[160];
    int tw = 0, th = 0, pw = 0, ph = 0;
    const char *mode = showcase_nav_mode();

    if (mode) {
        snprintf(title_buf, sizeof(title_buf), "%s  %s", title, mode);
        title = title_buf;
    }

    if (render_text_mask(title, 3, title_mask, 1024, 64, &tw, &th) != 0) return -1;
    MEDIA_OSD_REGION_ATTR title_attr = {0};
    title_attr.enabled = 1;
    title_attr.x = 32;
    title_attr.y = 24;
    title_attr.width = tw;
    title_attr.height = th;
    title_attr.zorder = 2;
    title_attr.global_alpha = 255;
    MEDIA_OSD_MASK_DESC title_desc = {0};
    title_desc.width = tw;
    title_desc.height = th;
    title_desc.stride = 1024;
    title_desc.data = title_mask;
    title_desc.data_size = sizeof(title_mask);
    title_desc.color.r = 160;
    title_desc.color.g = 255;
    title_desc.color.b = 220;
    title_desc.color.a = 255;

    if (render_text_mask(perf, 2, perf_mask, 1024, 64, &pw, &ph) != 0) return -1;
    MEDIA_OSD_REGION_ATTR perf_attr = {0};
    perf_attr.enabled = 1;
    perf_attr.x = 32;
    perf_attr.y = SCREEN_H - 72;
    perf_attr.width = pw;
    perf_attr.height = ph;
    perf_attr.zorder = 2;
    perf_attr.global_alpha = 255;
    MEDIA_OSD_MASK_DESC perf_desc = {0};
    perf_desc.width = pw;
    perf_desc.height = ph;
    perf_desc.stride = 1024;
    perf_desc.data = perf_mask;
    perf_desc.data_size = sizeof(perf_mask);
    perf_desc.color.r = 255;
    perf_desc.color.g = 230;
    perf_desc.color.b = 120;
    perf_desc.color.a = 255;

    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 2, &title_attr) != 0 ||
        MEDIA_OSD_SetRegionMask(DISPLAY_OSD_GRP, 2, &title_desc) != 0 ||
        MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 3, &perf_attr) != 0 ||
        MEDIA_OSD_SetRegionMask(DISPLAY_OSD_GRP, 3, &perf_desc) != 0) {
        return -1;
    }
    return 0;
}

static int update_osd_text_region(int region_id, int x, int y, int scale,
                                  uint8_t r, uint8_t g, uint8_t b,
                                  const char *text, uint8_t *mask,
                                  size_t mask_size) {
    int w = 0;
    int h = 0;
    if (render_text_mask(text, scale, mask, 1024, 64, &w, &h) != 0) return -1;

    MEDIA_OSD_REGION_ATTR attr = {0};
    attr.enabled = 1;
    attr.x = x;
    attr.y = y;
    attr.width = w;
    attr.height = h;
    attr.zorder = 3;
    attr.global_alpha = 255;

    MEDIA_OSD_MASK_DESC desc = {0};
    desc.width = w;
    desc.height = h;
    desc.stride = 1024;
    desc.data = mask;
    desc.data_size = mask_size;
    desc.color.r = r;
    desc.color.g = g;
    desc.color.b = b;
    desc.color.a = 255;

    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, region_id, &attr) != 0 ||
        MEDIA_OSD_SetRegionMask(DISPLAY_OSD_GRP, region_id, &desc) != 0) {
        return -1;
    }
    return 0;
}

static int update_osd_utf8_text_region(int region_id, int x, int y, int pixel_size,
                                       uint8_t r, uint8_t g, uint8_t b,
                                       const char *text, uint8_t *mask,
                                       size_t mask_size, int max_w, int max_h) {
    int w = 0;
    int h = 0;
    if (render_utf8_text_mask(text, pixel_size, mask, max_w, max_h, &w, &h) != 0) {
        return -1;
    }

    MEDIA_OSD_REGION_ATTR attr = {0};
    attr.enabled = 1;
    attr.x = x;
    attr.y = y;
    attr.width = w;
    attr.height = h;
    attr.zorder = 3;
    attr.global_alpha = 255;

    MEDIA_OSD_MASK_DESC desc = {0};
    desc.width = w;
    desc.height = h;
    desc.stride = max_w;
    desc.data = mask;
    desc.data_size = mask_size;
    desc.color.r = r;
    desc.color.g = g;
    desc.color.b = b;
    desc.color.a = 255;

    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, region_id, &attr) != 0 ||
        MEDIA_OSD_SetRegionMask(DISPLAY_OSD_GRP, region_id, &desc) != 0) {
        return -1;
    }
    return 0;
}

static int disable_osd_region(int region_id);

static int update_osd_rect_region(int region_id, int x, int y, int w, int h,
                                  uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (w <= 0 || h <= 0) return disable_osd_region(region_id);

    MEDIA_OSD_REGION_ATTR attr = {0};
    attr.enabled = 1;
    attr.x = x;
    attr.y = y;
    attr.width = w;
    attr.height = h;
    attr.zorder = 2;
    attr.global_alpha = a;

    MEDIA_OSD_RECT_DESC rect = {0};
    rect.filled = 1;
    rect.color.r = r;
    rect.color.g = g;
    rect.color.b = b;
    rect.color.a = a;

    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, region_id, &attr) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, region_id, &rect) != 0) {
        return -1;
    }
    return 0;
}

static int disable_osd_region(int region_id) {
    MEDIA_OSD_REGION_ATTR attr = {0};
    attr.enabled = 0;
    return MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, region_id, &attr);
}

static int update_osd_outline_region(int region_id, int x, int y, int w, int h,
                                     int line_width,
                                     uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (w <= 0 || h <= 0) return disable_osd_region(region_id);

    MEDIA_OSD_REGION_ATTR attr = {0};
    attr.enabled = 1;
    attr.x = x;
    attr.y = y;
    attr.width = w;
    attr.height = h;
    attr.zorder = 4;
    attr.global_alpha = a;

    MEDIA_OSD_RECT_DESC rect = {0};
    rect.filled = 0;
    rect.line_width = line_width > 0 ? line_width : 3;
    rect.color.r = r;
    rect.color.g = g;
    rect.color.b = b;
    rect.color.a = a;

    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, region_id, &attr) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, region_id, &rect) != 0) {
        return -1;
    }
    return 0;
}

static int update_vi_bind_flow_overlay(void) {
    static uint8_t masks[16][1024 * 96];
    const int box_y = 1114;
    const int box_w = 196;
    const int box_h = 96;
    const int box_gap = 24;
    const int box_x0 = 112;

    MEDIA_OSD_RECT_DESC panel = {0};
    panel.filled = 1;
    panel.color.r = 5;
    panel.color.g = 10;
    panel.color.b = 18;
    panel.color.a = 230;

    MEDIA_OSD_REGION_ATTR panel_attr = {0};
    panel_attr.enabled = 1;
    panel_attr.x = 70;
    panel_attr.y = 980;
    panel_attr.width = 940;
    panel_attr.height = 500;
    panel_attr.zorder = 2;
    panel_attr.global_alpha = 230;

    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 4, &panel_attr) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, 4, &panel) != 0) {
        return -1;
    }

    if (update_osd_utf8_text_region(5, 96, 1008, 30, 160, 255, 220,
                                    "VI测试：摄像头实时采集画面",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 98, 1054, 23, 190, 230, 255,
                                    "这一页用于确认VI输入是否稳定、画面是否正常。",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;

    if (update_osd_rect_region(7, box_x0, box_y, box_w, box_h, 18, 42, 64, 235) != 0) return -1;
    if (update_osd_rect_region(8, box_x0 + (box_w + box_gap), box_y, box_w, box_h, 18, 42, 64, 235) != 0) return -1;
    if (update_osd_rect_region(9, box_x0 + (box_w + box_gap) * 2, box_y, box_w, box_h, 18, 42, 64, 235) != 0) return -1;
    if (update_osd_rect_region(10, box_x0 + (box_w + box_gap) * 3, box_y, box_w, box_h, 18, 42, 64, 235) != 0) return -1;

    if (update_osd_utf8_text_region(11, box_x0 + 34, box_y + 20, 24, 160, 255, 220,
                                    "采集 VI",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(12, box_x0 + box_w + box_gap + 22, box_y + 20, 24, 160, 255, 220,
                                    "排版 VMIX",
                                    masks[3], sizeof(masks[3]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(13, box_x0 + (box_w + box_gap) * 2 + 34, box_y + 20, 24, 160, 255, 220,
                                    "叠字 OSD",
                                    masks[4], sizeof(masks[4]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(14, box_x0 + (box_w + box_gap) * 3 + 34, box_y + 20, 24, 160, 255, 220,
                                    "出屏 VO",
                                    masks[5], sizeof(masks[5]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(15, box_x0 + 18, box_y + 58, 18, 190, 230, 255,
                                    "摄像头输入",
                                    masks[6], sizeof(masks[6]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(16, box_x0 + box_w + box_gap + 18, box_y + 58, 18, 190, 230, 255,
                                    "放到屏幕画布",
                                    masks[7], sizeof(masks[7]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(17, box_x0 + (box_w + box_gap) * 2 + 18, box_y + 58, 18, 190, 230, 255,
                                    "叠加说明文字",
                                    masks[8], sizeof(masks[8]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(18, box_x0 + (box_w + box_gap) * 3 + 18, box_y + 58, 18, 190, 230, 255,
                                    "送到显示屏",
                                    masks[9], sizeof(masks[9]), 1024, 96) != 0) return -1;

    if (update_osd_utf8_text_region(19, 314, box_y + 30, 28, 255, 230, 120,
                                    "→", masks[10], sizeof(masks[10]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(20, 534, box_y + 30, 28, 255, 230, 120,
                                    "→", masks[11], sizeof(masks[11]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(21, 754, box_y + 30, 28, 255, 230, 120,
                                    "→", masks[12], sizeof(masks[12]), 1024, 96) != 0) return -1;

    if (update_osd_utf8_text_region(22, 96, 1248, 24, 255, 230, 120,
                                    "为什么这样做：模块直接绑定，画面不经CPU拷贝。",
                                    masks[13], sizeof(masks[13]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(23, 96, 1292, 24, 140, 210, 180,
                                    "好处：延迟更低，占用更少，现场看到真实链路。",
                                    masks[14], sizeof(masks[14]), 1024, 96) != 0) return -1;
    return 0;
}

static int update_wbc_bind_flow_overlay(uint64_t vi_count, uint64_t wbc_count, int frame) {
    static uint8_t masks[22][1024 * 96];
    const int box_y = 1224;
    const int box_w = 190;
    const int box_h = 92;
    const int box_gap = 20;
    const int box_x0 = 84;
    char line[256];

    MEDIA_OSD_RECT_DESC panel = {0};
    panel.filled = 1;
    panel.color.r = 5;
    panel.color.g = 10;
    panel.color.b = 18;
    panel.color.a = 230;

    MEDIA_OSD_REGION_ATTR panel_attr = {0};
    panel_attr.enabled = 1;
    panel_attr.x = 48;
    panel_attr.y = 1032;
    panel_attr.width = 984;
    panel_attr.height = 506;
    panel_attr.zorder = 2;
    panel_attr.global_alpha = 230;

    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 4, &panel_attr) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, 4, &panel) != 0) {
        return -1;
    }

    if (update_osd_outline_region(25, WBC_PANEL_X, WBC_PANEL_Y,
                                  WBC_PANEL_W, WBC_PANEL_H, 5, 255, 230, 120, 255) != 0) return -1;
    if (update_osd_outline_region(26, WBC_VI_PANEL_X, WBC_VI_PANEL_Y,
                                  WBC_VI_PANEL_W, WBC_VI_PANEL_H, 5, 80, 220, 255, 255) != 0) return -1;
    if (update_osd_utf8_text_region(27, WBC_PANEL_X + 12, WBC_PANEL_Y + 12, 20, 255, 230, 120,
                                    "WBC缩放 480x900",
                                    masks[16], sizeof(masks[16]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(28, WBC_VI_PANEL_X + 12, WBC_VI_PANEL_Y + 12, 20, 80, 220, 255,
                                    "VI实时 480x480",
                                    masks[17], sizeof(masks[17]), 1024, 96) != 0) return -1;

    if (update_osd_utf8_text_region(5, 76, 1058, 28, 160, 255, 220,
                                    "WBC测试：VI和WBC缩放后并排显示",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 78, 1104, 22, 190, 230, 255,
                                    "左侧VI先缩到480x480；右侧WBC是当前VO回抓，会包含上一帧已经显示的VI和OSD。",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;

    if (update_osd_rect_region(7, box_x0, box_y, box_w, box_h, 18, 42, 64, 235) != 0) return -1;
    if (update_osd_rect_region(8, box_x0 + (box_w + box_gap), box_y, box_w, box_h, 18, 42, 64, 235) != 0) return -1;
    if (update_osd_rect_region(9, box_x0 + (box_w + box_gap) * 2, box_y, box_w, box_h, 18, 42, 64, 235) != 0) return -1;
    if (update_osd_rect_region(10, box_x0 + (box_w + box_gap) * 3, box_y, box_w, box_h, 18, 42, 64, 235) != 0) return -1;

    if (update_osd_utf8_text_region(11, box_x0 + 36, box_y + 18, 23, 160, 255, 220,
                                    "VI采集",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(12, box_x0 + box_w + box_gap + 28, box_y + 18, 23, 160, 255, 220,
                                    "WBC缩放",
                                    masks[3], sizeof(masks[3]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(13, box_x0 + (box_w + box_gap) * 2 + 30, box_y + 18, 23, 160, 255, 220,
                                    "VMIX合成",
                                    masks[4], sizeof(masks[4]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(14, box_x0 + (box_w + box_gap) * 3 + 42, box_y + 18, 23, 160, 255, 220,
                                    "VO出屏",
                                    masks[5], sizeof(masks[5]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(15, box_x0 + 18, box_y + 56, 17, 190, 230, 255,
                                    "RGA缩到480",
                                    masks[6], sizeof(masks[6]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(16, box_x0 + box_w + box_gap + 16, box_y + 56, 17, 190, 230, 255,
                                    "VO_WBC->RGA",
                                    masks[7], sizeof(masks[7]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(17, box_x0 + (box_w + box_gap) * 2 + 16, box_y + 56, 17, 190, 230, 255,
                                    "两路放进画布",
                                    masks[8], sizeof(masks[8]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(18, box_x0 + (box_w + box_gap) * 3 + 18, box_y + 56, 17, 190, 230, 255,
                                    "显示闭环结果",
                                    masks[9], sizeof(masks[9]), 1024, 96) != 0) return -1;

    if (update_osd_utf8_text_region(19, 290, box_y + 28, 26, 255, 230, 120,
                                    "+", masks[10], sizeof(masks[10]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(20, 506, box_y + 28, 26, 255, 230, 120,
                                    "→", masks[11], sizeof(masks[11]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(21, 716, box_y + 28, 26, 255, 230, 120,
                                    "→", masks[12], sizeof(masks[12]), 1024, 96) != 0) return -1;

    snprintf(line, sizeof(line), "数据流：VI0.%s->RGA%d->VMIX.%s  WBC%d.%s->RGA%d->VMIX.%s  VMIX->OSD->VO",
             g_bind_vi_src_port ? g_bind_vi_src_port : "output",
             LIVE_WBC_VI_RESIZE_GRP,
             g_bind_wbc_vi_vmix_in_port ? g_bind_wbc_vi_vmix_in_port : "input0",
             LIVE_WBC_CHN,
             g_bind_wbc_src_port ? g_bind_wbc_src_port : "output",
             LIVE_WBC_RESIZE_GRP,
             g_bind_wbc_vmix_in_port ? g_bind_wbc_vmix_in_port : "input1");
    if (update_osd_utf8_text_region(22, 76, 1368, 19, 255, 230, 120,
                                    line, masks[13], sizeof(masks[13]), 1024, 96) != 0) return -1;
    snprintf(line, sizeof(line), "计数：VI %llu  WBC %llu  connector %d  crtc %d  frame %d",
             (unsigned long long)vi_count,
             (unsigned long long)wbc_count,
             g_wbc_connector_id,
             g_wbc_crtc_id,
             frame);
    if (update_osd_utf8_text_region(23, 76, 1412, 21, 140, 210, 180,
                                    line, masks[14], sizeof(masks[14]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(24, 76, 1456, 21, 190, 230, 255,
                                    "说明：WBC源是1024x1920 NV12，宽度按64字节对齐；VI/WBC显示前都先缩放。",
                                    masks[15], sizeof(masks[15]), 1024, 96) != 0) return -1;
    return 0;
}

static int update_vpss_bind_flow_overlay(void) {
    static uint8_t masks[24][1024 * 96];
    const int box_y = 1332;
    const int box_w = 166;
    const int box_h = 88;
    const int box_gap = 14;
    const int box_x0 = 90;
    MEDIA_OSD_RECT_DESC panel = {0};
    panel.filled = 1;
    panel.color.r = 5;
    panel.color.g = 10;
    panel.color.b = 18;
    panel.color.a = 230;

    MEDIA_OSD_REGION_ATTR panel_attr = {0};
    panel_attr.enabled = 1;
    panel_attr.x = 58;
    panel_attr.y = 1280;
    panel_attr.width = 964;
    panel_attr.height = 488;
    panel_attr.zorder = 2;
    panel_attr.global_alpha = 230;

    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 4, &panel_attr) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, 4, &panel) != 0) {
        return -1;
    }

    if (update_osd_utf8_text_region(5, 84, 1304, 29, 160, 255, 220,
                                    "VPSS测试：一路输入生成四路处理结果",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 86, 1348, 22, 190, 230, 255,
                                    "这一页用于确认VPSS缩放、裁剪、翻转和多路输出是否正常。",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;

    for (int i = 0; i < 5; ++i) {
        if (update_osd_rect_region(7 + i, box_x0 + i * (box_w + box_gap), box_y + 58,
                                   box_w, box_h, 18, 42, 64, 235) != 0) {
            return -1;
        }
    }

    if (update_osd_utf8_text_region(12, box_x0 + 30, box_y + 74, 23, 160, 255, 220,
                                    "采集 VI",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(13, box_x0 + box_w + box_gap + 18, box_y + 74, 23, 160, 255, 220,
                                    "缩放 VPSS",
                                    masks[3], sizeof(masks[3]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(14, box_x0 + (box_w + box_gap) * 2 + 18, box_y + 74, 23, 160, 255, 220,
                                    "合成 VMIX",
                                    masks[4], sizeof(masks[4]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(15, box_x0 + (box_w + box_gap) * 3 + 30, box_y + 74, 23, 160, 255, 220,
                                    "叠字 OSD",
                                    masks[5], sizeof(masks[5]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(16, box_x0 + (box_w + box_gap) * 4 + 30, box_y + 74, 23, 160, 255, 220,
                                    "出屏 VO",
                                    masks[6], sizeof(masks[6]), 1024, 96) != 0) return -1;

    if (update_osd_utf8_text_region(17, box_x0 + 14, box_y + 110, 17, 190, 230, 255,
                                    "摄像头原始帧",
                                    masks[7], sizeof(masks[7]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(18, box_x0 + box_w + box_gap + 8, box_y + 110, 17, 190, 230, 255,
                                    "缩放 裁剪 翻转",
                                    masks[8], sizeof(masks[8]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(19, box_x0 + (box_w + box_gap) * 2 + 8, box_y + 110, 17, 190, 230, 255,
                                    "只排版不缩放",
                                    masks[9], sizeof(masks[9]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(20, box_x0 + (box_w + box_gap) * 3 + 8, box_y + 110, 17, 190, 230, 255,
                                    "叠加标题状态",
                                    masks[10], sizeof(masks[10]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(21, box_x0 + (box_w + box_gap) * 4 + 22, box_y + 110, 17, 190, 230, 255,
                                    "送显示屏",
                                    masks[11], sizeof(masks[11]), 1024, 96) != 0) return -1;

    for (int i = 0; i < 4; ++i) {
        if (update_osd_utf8_text_region(22 + i, box_x0 + box_w + 3 + i * (box_w + box_gap),
                                        box_y + 88, 24, 255, 230, 120,
                                        "→", masks[12 + i], sizeof(masks[12 + i]), 1024, 96) != 0) {
            return -1;
        }
    }

    if (update_osd_utf8_text_region(26, 86, 1538, 23, 255, 230, 120,
                                    "VPSS把同一份VI输入处理成四路480x480输出。",
                                    masks[16], sizeof(masks[16]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(27, 86, 1578, 23, 140, 210, 180,
                                    "VMIX只负责摆放四路画面，不依赖VMIX缩小。",
                                    masks[17], sizeof(masks[17]), 1024, 96) != 0) return -1;

    return 0;
}

static int update_vmix_bind_flow_overlay(uint64_t vi_count, uint64_t vmix_count, int frame) {
    static uint8_t masks[18][1024 * 96];
    static const int pos_x[VPSS_DEMO_OUTPUTS] = {54, 546, 54, 546};
    static const int pos_y[VPSS_DEMO_OUTPUTS] = {170, 170, 690, 690};
    static const char *labels[VPSS_DEMO_OUTPUTS] = {
        "输入1", "输入2", "输入3", "输入4",
    };
    static const uint8_t colors[VPSS_DEMO_OUTPUTS][3] = {
        {80, 180, 255}, {160, 255, 220}, {255, 210, 120}, {255, 150, 180},
    };
    char stats[160];
    int refresh_static = frame <= 0;

    if (refresh_static) {
        MEDIA_OSD_RECT_DESC panel = {0};
        panel.filled = 1;
        panel.color.r = 5;
        panel.color.g = 10;
        panel.color.b = 18;
        panel.color.a = 230;

        MEDIA_OSD_REGION_ATTR panel_attr = {0};
        panel_attr.enabled = 1;
        panel_attr.x = 58;
        panel_attr.y = 1280;
        panel_attr.width = 964;
        panel_attr.height = 488;
        panel_attr.zorder = 2;
        panel_attr.global_alpha = 230;

        if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 4, &panel_attr) != 0 ||
            MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, 4, &panel) != 0) {
            return -1;
        }

        for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
            if (update_osd_outline_region(5 + i, pos_x[i], pos_y[i],
                                          VPSS_TILE_W, VPSS_TILE_H, 4,
                                          colors[i][0], colors[i][1], colors[i][2], 255) != 0) {
                return -1;
            }
            if (update_osd_utf8_text_region(9 + i, pos_x[i] + 14, pos_y[i] + VPSS_TILE_H - 36,
                                            20, colors[i][0], colors[i][1], colors[i][2],
                                            labels[i], masks[i], sizeof(masks[i]), 1024, 96) != 0) {
                return -1;
            }
        }

        if (update_osd_utf8_text_region(13, 84, 1304, 29, 160, 255, 220,
                                        "VMIX：四路实时输入稳定合成",
                                        masks[4], sizeof(masks[4]), 1024, 96) != 0) return -1;
        if (update_osd_utf8_text_region(14, 86, 1350, 22, 190, 230, 255,
                                        "四路输入使用固定2x2位置，避免半透明重叠导致客户看到闪烁。",
                                        masks[5], sizeof(masks[5]), 1024, 96) != 0) return -1;
        if (update_osd_utf8_text_region(15, 86, 1410, 23, 255, 230, 120,
                                        "流程图：VI 3840x2160 -> VPSS四路480x480 -> VMIX合成 -> OSD -> VO",
                                        masks[6], sizeof(masks[6]), 1024, 96) != 0) return -1;
        if (update_osd_utf8_text_region(16, 86, 1460, 22, 140, 210, 180,
                                        "分辨率：摄像头输入3840x2160；VPSS输出四路480x480工作帧。",
                                        masks[7], sizeof(masks[7]), 1024, 96) != 0) return -1;
    }

    snprintf(stats, sizeof(stats), "帧计数 VI=%llu  VMIX=%llu  frame=%d",
             (unsigned long long)vi_count,
             (unsigned long long)vmix_count,
             frame);
    if (update_osd_utf8_text_region(17, 86, 1522, 22, 255, 230, 120,
                                    stats, masks[8], sizeof(masks[8]), 1024, 96) != 0) return -1;

    return 0;
}

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
} rga_demo_op_t;

static const rga_demo_op_t g_rga_demo_ops[] = {
    {"COPY", MEDIA_RGA_ALG_COPY, 0, 0, CAM_W, CAM_H, 0, 0, 0},
    {"CROP_ZOOM", MEDIA_RGA_ALG_RESIZE, 120, 120, 400, 400, 0, 0, 0},
    {"FLIP_H", MEDIA_RGA_ALG_FLIP, 0, 0, CAM_W, CAM_H, 0, 1, 0},
    {"FLIP_V", MEDIA_RGA_ALG_FLIP, 0, 0, CAM_W, CAM_H, 0, 0, 1},
    {"ROTATE90", MEDIA_RGA_ALG_ROTATE, 0, 0, CAM_W, CAM_H, 90, 0, 0},
    {"ROTATE180", MEDIA_RGA_ALG_ROTATE, 0, 0, CAM_W, CAM_H, 180, 0, 0},
    {"ROTATE270", MEDIA_RGA_ALG_ROTATE, 0, 0, CAM_W, CAM_H, 270, 0, 0},
};

typedef struct {
    int crop_x;
    int crop_y;
    int crop_w;
    int crop_h;
} resize_demo_params_t;

static int resize_wave_i(int t, int max_value) {
    int period;
    int v;
    if (max_value <= 0) return 0;
    period = max_value * 2;
    v = t % period;
    return v <= max_value ? v : period - v;
}

static resize_demo_params_t resize_demo_params_for_frame(int frame) {
    resize_demo_params_t p;
    int range;
    p.crop_w = 520 - (resize_wave_i(frame * 2, 200) & ~1);
    if (p.crop_w < 320) p.crop_w = 320;
    p.crop_h = p.crop_w;
    range = CAM_W - p.crop_w;
    p.crop_x = resize_wave_i(frame * 5, range) & ~1;
    p.crop_y = resize_wave_i(frame * 3, range) & ~1;
    return p;
}

static int update_rga_bind_flow_overlay(int op_index, int frame) {
    static uint8_t masks[22][1024 * 96];
    const int panel_x = 58;
    const int panel_y = 1028;
    const int card_y = 1248;
    const int card_w = 122;
    const int card_h = 58;
    const int card_gap = 10;
    char op_line[160];
    char bind_line[180];
    const rga_demo_op_t *op = &g_rga_demo_ops[op_index];

    snprintf(op_line, sizeof(op_line), "当前RGA操作：%s", op->label);
    snprintf(bind_line, sizeof(bind_line), "流程图：VI 3840x2160 -> RGA硬件变换 -> 1080x608显示帧 -> VMIX固定位置 -> OSD说明 -> VO");

    if (update_osd_rect_region(4, panel_x, panel_y, 964, 458, 5, 10, 18, 225) != 0) return -1;
    if (update_osd_utf8_text_region(5, 86, 1056, 31, 160, 255, 220,
                                    "RGA硬件2D图像处理",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 88, 1104, 23, 190, 230, 255,
                                    "数据流：VI实时画面进入RGA，硬件完成拷贝、裁剪、翻转和旋转。",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(7, 88, 1144, 23, 255, 230, 120,
                                    "为什么这样做：图像变换交给RGA，CPU不用搬帧逐像素处理。",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;

    if (update_osd_utf8_text_region(8, 88, 1196, 24, 255, 230, 120,
                                    op_line, masks[3], sizeof(masks[3]), 1024, 96) != 0) return -1;

    for (int i = 0; i < (int)ARRAY_SIZE(g_rga_demo_ops); ++i) {
        int x = 88 + i * (card_w + card_gap);
        int active = i == op_index;
        uint8_t rr = active ? 32 : 10;
        uint8_t gg = active ? (uint8_t)(70 + ((frame * 3) & 31)) : 28;
        uint8_t bb = active ? 78 : 42;
        if (update_osd_rect_region(9 + i, x, card_y, card_w, card_h, rr, gg, bb, 235) != 0) return -1;
        if (update_osd_text_region(16 + i, x + 12, card_y + 22, 1,
                                   active ? 255 : 150,
                                   active ? 240 : 180,
                                   active ? 130 : 210,
                                   g_rga_demo_ops[i].label,
                                   masks[4 + i], sizeof(masks[4 + i])) != 0) return -1;
    }

    if (update_osd_utf8_text_region(23, 88, 1344, 23, 140, 210, 180,
                                    "效果会自动轮播：COPY、移动裁剪放大、翻转、90/180/270度旋转。",
                                    masks[11], sizeof(masks[11]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(24, 88, 1404, 21, 170, 205, 235,
                                    bind_line, masks[12], sizeof(masks[12]), 1024, 96) != 0) return -1;

    return 0;
}

static int update_resize_bind_flow_overlay(resize_demo_params_t p, int frame) {
    static uint8_t masks[16][1024 * 96];
    char crop_line[160];
    char zoom_line[96];
    char bind_line[180];
    int box_x = 102 + (p.crop_x * 260) / CAM_W;
    int box_y = 1198 + (p.crop_y * 260) / CAM_H;
    int box_w = (p.crop_w * 260) / CAM_W;
    int box_h = (p.crop_h * 260) / CAM_H;
    int pulse = 180 + ((frame * 6) & 63);
    if (box_w < 40) box_w = 40;
    if (box_h < 40) box_h = 40;

    snprintf(crop_line, sizeof(crop_line), "CROP X=%d Y=%d W=%d H=%d -> OUT 1080x608",
             p.crop_x, p.crop_y, p.crop_w, p.crop_h);
    snprintf(zoom_line, sizeof(zoom_line), "ZOOM %.2fX", (float)CAM_W / (float)p.crop_w);
    snprintf(bind_line, sizeof(bind_line), "VI0.%s -> RESIZE_RGA%d.%s  |  RESIZE_RGA%d.%s -> VMIX%d.%s  |  OSD%d.%s -> VO0.%s",
             g_bind_vi_src_port ? g_bind_vi_src_port : "output0",
             LIVE_RESIZE_GRP,
             g_bind_resize_in_port ? g_bind_resize_in_port : "input0",
             LIVE_RESIZE_GRP,
             g_bind_resize_src_port ? g_bind_resize_src_port : "output0",
             DISPLAY_VMIX_GRP,
             g_bind_vmix_in_port ? g_bind_vmix_in_port : "input0",
             DISPLAY_OSD_GRP,
             g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
             g_bind_vo_in_port ? g_bind_vo_in_port : "input0");

    if (update_osd_rect_region(4, 58, 1030, 964, 454, 5, 10, 18, 225) != 0) return -1;
    if (update_osd_utf8_text_region(5, 86, 1058, 31, 160, 255, 220,
                                    "RESIZE_RGA动态裁剪放大",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 88, 1106, 23, 190, 230, 255,
                                    "数据流：VI实时画面进入RESIZE_RGA，移动裁剪框被放大到输出画面。",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(7, 88, 1146, 23, 255, 230, 120,
                                    "为什么这样做：把裁剪和缩放交给RGA硬件，CPU不用逐像素缩放。",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;

    if (update_osd_rect_region(8, 88, 1190, 300, 300, 9, 19, 34, 235) != 0) return -1;
    if (update_osd_rect_region(9, box_x, box_y, box_w, box_h, 255, 230, 80, 210) != 0) return -1;
    if (update_osd_rect_region(10, 458, 1190, 496, 224, 10, 22, 38, 235) != 0) return -1;

    if (update_osd_utf8_text_region(11, 112, 1208, 22, 160, 255, 220,
                                    "4K等比例取景", masks[3], sizeof(masks[3]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(12, 484, 1214, 27, 160, 255, 220,
                                    "输出：裁剪区域被放大铺满", masks[4], sizeof(masks[4]), 1024, 96) != 0) return -1;
    if (update_osd_text_region(13, 484, 1278, 2, 255, 230, 120,
                               zoom_line, masks[5], sizeof(masks[5])) != 0) return -1;
    if (update_osd_text_region(14, 484, 1330, 1, 190, 230, 255,
                               crop_line, masks[6], sizeof(masks[6])) != 0) return -1;
    if (update_osd_text_region(15, 88, 1430, 1, 170, 205, 235,
                               bind_line, masks[7], sizeof(masks[7])) != 0) return -1;

    (void)pulse;
    return 0;
}

static int update_csc_rga_bind_flow_overlay(uint64_t front_count, uint64_t back_count, int frame) {
    static uint8_t masks[22][1024 * 96];
    char count_line[128];
    char bind_line1[180];
    char bind_line2[180];

    snprintf(count_line, sizeof(count_line), "FRAMES  CSC0=%llu  CSC1=%llu",
             (unsigned long long)front_count, (unsigned long long)back_count);
    snprintf(bind_line1, sizeof(bind_line1), "VI0.%s -> CSC_RGA%d.%s -> CSC_RGA%d.%s",
             g_bind_vi_src_port ? g_bind_vi_src_port : "output0",
             LIVE_CSC_RGA_GRP,
             g_bind_csc_front_in_port ? g_bind_csc_front_in_port : "input",
             LIVE_CSC_RGA_BACK_GRP,
             g_bind_csc_back_in_port ? g_bind_csc_back_in_port : "input");
    snprintf(bind_line2, sizeof(bind_line2), "CSC_RGA%d.%s -> VMIX%d.%s  |  OSD%d.%s -> VO0.%s",
             LIVE_CSC_RGA_BACK_GRP,
             g_bind_csc_back_src_port ? g_bind_csc_back_src_port : "output0",
             DISPLAY_VMIX_GRP,
             g_bind_vmix_in_port ? g_bind_vmix_in_port : "input0",
             DISPLAY_OSD_GRP,
             g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
             g_bind_vo_in_port ? g_bind_vo_in_port : "input0");

    if (update_osd_rect_region(4, 42, 980, 996, 530, 5, 10, 18, 225) != 0) return -1;
    if (update_osd_utf8_text_region(5, 72, 1012, 36, 160, 255, 220,
                                    "CSC_RGA硬件颜色格式转换",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 74, 1070, 26, 190, 230, 255,
                                    "数据流：摄像头NV12先转ARGB8888，再转回NV12送显示。",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(7, 74, 1116, 25, 255, 230, 120,
                                    "为什么这样做：算法和图形常用ARGB，摄像头和显示常用NV12。",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;

    int card_y = 1192;
    int card_w = 280;
    int card_h = 154;
    int xs[3] = {74, 400, 726};
    const char *titles[3] = {"输入", "转换中", "输出"};
    const char *formats[3] = {"NV12", "ARGB8888", "NV12"};
    const char *subs[3] = {"摄像头 Y/UV", "A/R/G/B 四通道", "显示 Y/UV"};
    for (int i = 0; i < 3; ++i) {
        if (update_osd_rect_region(8 + i, xs[i], card_y, card_w, card_h, 12, 28, 46, 240) != 0) return -1;
        if (update_osd_utf8_text_region(11 + i, xs[i] + 24, card_y + 18, 26, 160, 255, 220,
                                        titles[i], masks[3 + i], sizeof(masks[3 + i]), 1024, 96) != 0) return -1;
        if (update_osd_text_region(14 + i, xs[i] + 24, card_y + 66, 3, 255, 230, 120,
                                   formats[i], masks[6 + i], sizeof(masks[6 + i])) != 0) return -1;
        if (update_osd_utf8_text_region(17 + i, xs[i] + 24, card_y + 118, 19, 190, 230, 255,
                                        subs[i], masks[18 + i], sizeof(masks[18 + i]), 1024, 96) != 0) return -1;
    }

    if (update_osd_utf8_text_region(20, 356, card_y + 54, 42, 255, 230, 120,
                                    "→", masks[9], sizeof(masks[9]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(21, 682, card_y + 54, 42, 255, 230, 120,
                                    "→", masks[10], sizeof(masks[10]), 1024, 96) != 0) return -1;

    int bar_y = 1380;
    const uint8_t colors[4][3] = {
        {230, 230, 230}, {240, 70, 70}, {70, 220, 110}, {80, 150, 255},
    };
    const char *labels[4] = {"Y", "R", "G", "B"};
    for (int i = 0; i < 4; ++i) {
        int w = 120 + ((frame * (i + 2) + i * 37) % 120);
        if (update_osd_rect_region(22 + i, 88 + i * 225, bar_y, w, 32,
                                   colors[i][0], colors[i][1], colors[i][2], 230) != 0) return -1;
        if (update_osd_text_region(26 + i, 92 + i * 225, bar_y + 48, 2, 190, 230, 255,
                                   labels[i], masks[11 + i], sizeof(masks[11 + i])) != 0) return -1;
    }

    if (update_osd_text_region(30, 74, 1464, 2, 170, 255, 220,
                               count_line, masks[15], sizeof(masks[15])) != 0) return -1;
    if (update_osd_text_region(31, 74, 1508, 1, 170, 205, 235,
                               bind_line1, masks[16], sizeof(masks[16])) != 0) return -1;
    if (update_osd_text_region(32, 74, 1532, 1, 170, 205, 235,
                               bind_line2, masks[17], sizeof(masks[17])) != 0) return -1;

    return 0;
}

static int update_csc_cl_bind_flow_overlay(uint64_t front_count, uint64_t back_count,
                                           double kernel_ms, double queue_ms, int frame) {
    static uint8_t masks[28][1024 * 96];
    char count_line[128];
    char bind_line1[180];
    char bind_line2[180];
    char kernel_text[64];
    char queue_text[64];
    char runtime[128];

    format_runtime_status(runtime, sizeof(runtime));
    if (kernel_ms >= 0.0) {
        snprintf(kernel_text, sizeof(kernel_text), "内核 %.2f ms", kernel_ms);
    } else {
        snprintf(kernel_text, sizeof(kernel_text), "内核 N/A");
    }
    if (queue_ms >= 0.0) {
        snprintf(queue_text, sizeof(queue_text), "排队 %.2f ms", queue_ms);
    } else {
        snprintf(queue_text, sizeof(queue_text), "排队 N/A");
    }
    snprintf(count_line, sizeof(count_line), "帧计数：CSC_CL=%llu  DISPLAY/OSD=%llu  %s",
             (unsigned long long)front_count, (unsigned long long)back_count,
             runtime);
    snprintf(bind_line1, sizeof(bind_line1), "流程图：VI 3840x2160 -> CSC_CL颜色矩阵4K处理 -> RESIZE_RGA全屏缩放");
    snprintf(bind_line2, sizeof(bind_line2), "输出：RESIZE_RGA 1080x1920 ARGB -> OSD说明层 -> VO。");

    if (update_osd_rect_region(4, 18, 18, 604, 604, 5, 10, 18, 226) != 0) return -1;
    if (update_osd_utf8_text_region(5, 34, 38, 24, 160, 255, 220,
                                    "CSC_CL：GPU/OpenCL颜色矩阵转换",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 34, 78, 16, 190, 230, 255,
                                    "数据流：摄像头4K直接进OpenCL转ARGB，算法完成后再缩放到全屏并叠加说明。",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(7, 34, 108, 16, 255, 230, 120,
                                    "分辨率：VI输入3840x2160；CSC_CL模块输入3840x2160；显示通道1080x1920全屏。",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;

    const int card_y = 156;
    const int card_w = 136;
    const int card_h = 116;
    const int xs[4] = {28, 180, 332, 484};
    const char *titles[4] = {"输入", "OpenCL", "显示缩放", "说明层"};
    const char *values[4] = {"4K", "4K ARGB", "1080x1920", "OSD"};
    const char *subs[4] = {"VI直连", "GPU颜色矩阵", "RESIZE_RGA", "叠加后VO"};
    for (int i = 0; i < 4; ++i) {
        int r = (i == 1) ? 16 : 12;
        int g = (i == 1) ? 48 : 28;
        int b = (i == 1) ? 76 : 46;
        if (update_osd_rect_region(8 + i, xs[i], card_y, card_w, card_h, r, g, b, 240) != 0) return -1;
        if (update_osd_utf8_text_region(12 + i, xs[i] + 12, card_y + 14, 17, 160, 255, 220,
                                        titles[i], masks[3 + i], sizeof(masks[3 + i]), 1024, 96) != 0) return -1;
        if (update_osd_text_region(16 + i, xs[i] + 12, card_y + 48, 2, 255, 230, 120,
                                   values[i], masks[7 + i], sizeof(masks[7 + i])) != 0) return -1;
        if (update_osd_utf8_text_region(20 + i, xs[i] + 12, card_y + 86, 13, 190, 230, 255,
                                        subs[i], masks[11 + i], sizeof(masks[11 + i]), 1024, 96) != 0) return -1;
    }

    if (update_osd_utf8_text_region(24, 158, card_y + 46, 26, 255, 230, 120,
                                    "→", masks[15], sizeof(masks[15]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(25, 310, card_y + 46, 26, 255, 230, 120,
                                    "→", masks[16], sizeof(masks[16]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(26, 462, card_y + 46, 26, 255, 230, 120,
                                    "→", masks[17], sizeof(masks[17]), 1024, 96) != 0) return -1;

    int pulse = 28 + ((frame * 7) % 52);
    if (update_osd_rect_region(27, 194, card_y + 38, pulse, 10, 120, 220, 255, 210) != 0) return -1;
    if (update_osd_rect_region(28, 498, card_y + 38, pulse, 10, 120, 220, 255, 210) != 0) return -1;

    int kernel_w = kernel_ms >= 0.0 ? (int)(kernel_ms * 38.0) : 80;
    int queue_w = queue_ms >= 0.0 ? (int)(queue_ms * 26.0) : 80;
    if (kernel_w < 80) kernel_w = 80;
    if (kernel_w > 280) kernel_w = 280;
    if (queue_w < 80) queue_w = 80;
    if (queue_w > 280) queue_w = 280;
    if (update_osd_rect_region(29, 34, 330, kernel_w, 24, 120, 220, 255, 228) != 0) return -1;
    if (update_osd_rect_region(30, 34, 370, queue_w, 24, 255, 210, 90, 228) != 0) return -1;
    if (update_osd_utf8_text_region(31, 336, 320, 17, 170, 255, 220,
                                    kernel_text, masks[18], sizeof(masks[18]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(37, 336, 360, 17, 255, 230, 120,
                                    queue_text, masks[23], sizeof(masks[23]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(32, 34, 420, 16, 255, 230, 120,
                                    "BT.601矩阵：把YUV颜色分量按矩阵换算成RGB。",
                                    masks[19], sizeof(masks[19]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(33, 34, 468, 16, 170, 255, 220,
                                    count_line, masks[20], sizeof(masks[20]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(34, 34, 510, 16, 170, 205, 235,
                                    bind_line1, masks[21], sizeof(masks[21]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(35, 34, 540, 16, 170, 205, 235,
                                    bind_line2, masks[22], sizeof(masks[22]), 1024, 96) != 0) return -1;

    int packet_x = 34 + ((frame * 18) % 520);
    if (update_osd_rect_region(36, packet_x, 304, 34, 8, 255, 255, 255, 190) != 0) return -1;

    return 0;
}

static int update_conv_cl_quad_overlay(uint64_t vi_count,
                                       const uint64_t counts[VPSS_DEMO_OUTPUTS],
                                       double kernel_ms, double queue_ms, int frame) {
    static uint8_t masks[40][1024 * 96];
    char count_line[192];
    char status_line[192];
    char usage_line[192];
    char bind_line1[192];
    char bind_line2[192];
    char cl_text[48];
    char runtime[128];
    const char *mix = g_conv_cl_mix_module ? g_conv_cl_mix_module : "VMIX";

    format_runtime_status(runtime, sizeof(runtime));
    if (kernel_ms >= 0.0) {
        snprintf(cl_text, sizeof(cl_text), "%.2f/%.2fms",
                 kernel_ms, queue_ms >= 0.0 ? queue_ms : 0.0);
    } else {
        snprintf(cl_text, sizeof(cl_text), "N/A");
    }

    snprintf(count_line, sizeof(count_line), "CONV OUT  %s=%llu  %s=%llu  %s=%llu  %s=%llu",
             g_conv_cl_effects[0].name, (unsigned long long)counts[0],
             g_conv_cl_effects[1].name, (unsigned long long)counts[1],
             g_conv_cl_effects[2].name, (unsigned long long)counts[2],
             g_conv_cl_effects[3].name, (unsigned long long)counts[3]);
    snprintf(status_line, sizeof(status_line), "VI=%llu  %s  CL %s",
             (unsigned long long)vi_count, runtime, cl_text);
    snprintf(usage_line, sizeof(usage_line), "VPSS四路同源同尺寸；画面差异只来自四个CONV_CL卷积核。");
    snprintf(bind_line1, sizeof(bind_line1), "VI0.%s -> VPSS%d.%s -> CSC_RGA[%d..%d] -> CONV_CL[%d..%d]",
             g_bind_vi_src_port ? g_bind_vi_src_port : "output",
             LIVE_VPSS_GRP,
             g_bind_vpss_in_port ? g_bind_vpss_in_port : "input",
             g_conv_cl_pre_csc_grps[0], g_conv_cl_pre_csc_grps[VPSS_DEMO_OUTPUTS - 1],
             g_conv_cl_quad_grps[0], g_conv_cl_quad_grps[VPSS_DEMO_OUTPUTS - 1]);
    snprintf(bind_line2, sizeof(bind_line2), "CONV_CL -> CSC_RGA[%d..%d] -> %s%d inputs0..3 -> OSD%d.%s -> VO0.%s",
             g_conv_cl_csc_grps[0], g_conv_cl_csc_grps[VPSS_DEMO_OUTPUTS - 1],
             mix,
             DISPLAY_VMIX_GRP,
             DISPLAY_OSD_GRP,
             g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
             g_bind_vo_in_port ? g_bind_vo_in_port : "input0");

    const int cell = VPSS_TILE_W;
    const int gap = 40;
    const int start_x = (SCREEN_W - cell * 2 - gap) / 2;
    const int start_y = 300;
    const uint8_t colors[4][3] = {
        {70, 170, 255}, {255, 110, 120}, {255, 210, 90}, {110, 230, 170},
    };
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        int x = start_x + (i % 2) * (cell + gap);
        int y = start_y + (i / 2) * (cell + gap);
        char frames[48];
        snprintf(frames, sizeof(frames), "OUT %llu", (unsigned long long)counts[i]);
        if (update_osd_rect_region(4 + i, x + 18, y + 18, 220, 78,
                                   5, 10, 18, 220) != 0) return -1;
        if (update_osd_rect_region(8 + i, x + 18, y + 18, 220, 8,
                                   colors[i][0], colors[i][1], colors[i][2], 235) != 0) return -1;
        if (update_osd_text_region(12 + i, x + 34, y + 36, 2, 255, 230, 120,
                                   g_conv_cl_effects[i].name,
                                   masks[i], sizeof(masks[i])) != 0) return -1;
        if (update_osd_text_region(16 + i, x + 34, y + 72, 1, 190, 230, 255,
                                   frames, masks[4 + i], sizeof(masks[4 + i])) != 0) return -1;
    }

    const int panel_y = 1328;
    if (update_osd_rect_region(20, 34, panel_y, 1012, 420, 5, 10, 18, 226) != 0) return -1;
    if (update_osd_utf8_text_region(21, 66, panel_y + 28, 32, 160, 255, 220,
                                    "CONV_CL：VMIX_RGA后叠加OSD，再送VO显示",
                                    masks[8], sizeof(masks[8]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(22, 68, panel_y + 80, 23, 255, 230, 120,
                                    usage_line,
                                    masks[9], sizeof(masks[9]), 1024, 96) != 0) return -1;
    if (update_osd_text_region(23, 68, panel_y + 126, 2, 170, 255, 220,
                               status_line, masks[10], sizeof(masks[10])) != 0) return -1;
    if (update_osd_text_region(24, 68, panel_y + 174, 1, 190, 230, 255,
                               count_line, masks[11], sizeof(masks[11])) != 0) return -1;
    if (update_osd_text_region(25, 68, panel_y + 214, 1, 170, 205, 235,
                               bind_line1, masks[12], sizeof(masks[12])) != 0) return -1;
    if (update_osd_text_region(26, 68, panel_y + 246, 1, 170, 205, 235,
                               bind_line2, masks[13], sizeof(masks[13])) != 0) return -1;

    int cpu_w = (int)(g_perf.cpu_percent * 3.0);
    int gpu_w = g_perf.gpu_available ? (int)(g_perf.gpu_percent * 3.0) : 0;
    int rga_w = g_perf.rga_available ? (int)(g_perf.rga_percent * 3.0) : 0;
    int cl_w = kernel_ms >= 0.0 ? (int)(kernel_ms * 34.0) : 0;
    if (cpu_w < 0) cpu_w = 0;
    if (cpu_w > 300) cpu_w = 300;
    if (gpu_w < 0) gpu_w = 0;
    if (gpu_w > 300) gpu_w = 300;
    if (rga_w < 0) rga_w = 0;
    if (rga_w > 300) rga_w = 300;
    if (cl_w < 0) cl_w = 0;
    if (cl_w > 300) cl_w = 300;

    if (update_osd_text_region(27, 82, panel_y + 306, 1, 255, 230, 120,
                               "CPU", masks[14], sizeof(masks[14])) != 0) return -1;
    if (update_osd_text_region(28, 82, panel_y + 352, 1, 255, 230, 120,
                               "GPU", masks[15], sizeof(masks[15])) != 0) return -1;
    if (update_osd_text_region(29, 532, panel_y + 306, 1, 255, 230, 120,
                               "RGA", masks[16], sizeof(masks[16])) != 0) return -1;
    if (update_osd_text_region(30, 532, panel_y + 352, 1, 255, 230, 120,
                               "CL", masks[17], sizeof(masks[17])) != 0) return -1;
    if (update_osd_rect_region(31, 148, panel_y + 304, 300, 22, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(32, 148, panel_y + 304, cpu_w, 22, 120, 220, 255, 230) != 0) return -1;
    if (update_osd_rect_region(33, 148, panel_y + 350, 300, 22, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(34, 148, panel_y + 350, gpu_w, 22, 110, 230, 170, 230) != 0) return -1;
    if (update_osd_rect_region(35, 598, panel_y + 304, 300, 22, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(36, 598, panel_y + 304, rga_w, 22, 255, 210, 90, 230) != 0) return -1;
    if (update_osd_rect_region(37, 598, panel_y + 350, 300, 22, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(38, 598, panel_y + 350, cl_w, 22, 255, 110, 120, 230) != 0) return -1;

    int packet_x = 68 + ((frame * 18) % 890);
    if (update_osd_rect_region(39, packet_x, panel_y - 22, 52, 10, 255, 255, 255, 180) != 0) return -1;
    return 0;
}

static int update_transform_quad_overlay(uint64_t vi_count,
                                         const uint64_t counts[VPSS_DEMO_OUTPUTS],
                                         const uint64_t resize_counts[VPSS_DEMO_OUTPUTS],
                                         int frame) {
    static uint8_t masks[40][1024 * 96];
    int active = (frame / FPS) % VPSS_DEMO_OUTPUTS;
    char status_line[192];
    char count_line[192];
    char bind_line1[192];
    char bind_line2[192];
    char lut_line[160];
    char runtime[128];

    format_runtime_status(runtime, sizeof(runtime));
    snprintf(status_line, sizeof(status_line), "VI=%llu  %s  MODE=%s  SWITCH=1s",
             (unsigned long long)vi_count, runtime,
             g_transform_effects[active].name);
    snprintf(count_line, sizeof(count_line), "ACTIVE %s  VPSS=%llu  TRANSFORM=%llu  RESIZE=%llu",
             g_transform_effects[active].name,
             (unsigned long long)counts[0],
             (unsigned long long)counts[active],
             (unsigned long long)resize_counts[active]);
    snprintf(bind_line1, sizeof(bind_line1), "VI0.%s -> VPSS%d.%s -> TRANSFORM%d(mode switch) -> RESIZE%d -> VMIX%d.input0",
             g_bind_vi_src_port ? g_bind_vi_src_port : "output",
             LIVE_VPSS_GRP,
             g_bind_vpss_in_port ? g_bind_vpss_in_port : "input",
             g_transform_quad_grps[1],
             g_transform_display_resize_grps[0],
             DISPLAY_VMIX_GRP);
    snprintf(bind_line2, sizeof(bind_line2), "VMIX%d.output0 -> OSD%d.%s -> VO0.%s; GPU只跑一路4K LUT，避免最后模式被队列压住",
             DISPLAY_VMIX_GRP,
             DISPLAY_OSD_GRP,
             g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
             g_bind_vo_in_port ? g_bind_vo_in_port : "input0");
    snprintf(lut_line, sizeof(lut_line),
             "四种模式每秒切换：RAW、UNDISTORT、ROTATE ZOOM、PERSPECTIVE；同一路4K输入先LUT变换再缩放显示。");

    const int cell = VPSS_TILE_W;
    const int gap = 40;
    const int start_x = (SCREEN_W - cell * 2 - gap) / 2;
    const int start_y = 300;
    const uint8_t colors[4][3] = {
        {70, 170, 255}, {110, 230, 170}, {255, 210, 90}, {255, 110, 120},
    };
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        int x = start_x + (i % 2) * (cell + gap);
        int y = start_y + (i / 2) * (cell + gap);
        char frames[48];
        snprintf(frames, sizeof(frames), "OUT %llu RZ %llu",
                 (unsigned long long)counts[i],
                 (unsigned long long)resize_counts[i]);
        if (update_osd_rect_region(4 + i, x + 18, y + 18, 250, 96,
                                   i == active ? 20 : 5,
                                   i == active ? 52 : 10,
                                   i == active ? 42 : 18,
                                   220) != 0) return -1;
        if (update_osd_rect_region(8 + i, x + 18, y + 18, 250, 8,
                                   colors[i][0], colors[i][1], colors[i][2], 235) != 0) return -1;
        if (update_osd_text_region(12 + i, x + 34, y + 36, i == 2 ? 1 : 2,
                                   255, 230, 120,
                                   g_transform_effects[i].name,
                                   masks[i], sizeof(masks[i])) != 0) return -1;
        if (update_osd_utf8_text_region(16 + i, x + 34, y + 70, 18,
                                        190, 230, 255,
                                        g_transform_effects[i].desc,
                                        masks[4 + i], sizeof(masks[4 + i]), 1024, 96) != 0) return -1;
        if (update_osd_text_region(20 + i, x + 34, y + 96, 1, 190, 230, 255,
                                   frames, masks[8 + i], sizeof(masks[8 + i])) != 0) return -1;
    }

    const int panel_y = 1328;
    if (update_osd_rect_region(24, 34, panel_y, 1012, 420, 5, 10, 18, 226) != 0) return -1;
    if (update_osd_utf8_text_region(25, 66, panel_y + 28, 32, 160, 255, 220,
                                    "TRANSFORM：先处理4K图像，最后缩放、VMIX、OSD、VO显示",
                                    masks[12], sizeof(masks[12]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(26, 68, panel_y + 80, 23, 255, 230, 120,
                                    lut_line,
                                    masks[13], sizeof(masks[13]), 1024, 96) != 0) return -1;
    if (update_osd_text_region(27, 68, panel_y + 126, 2, 170, 255, 220,
                               status_line, masks[14], sizeof(masks[14])) != 0) return -1;
    if (update_osd_text_region(28, 68, panel_y + 174, 1, 190, 230, 255,
                               count_line, masks[15], sizeof(masks[15])) != 0) return -1;
    if (update_osd_text_region(29, 68, panel_y + 214, 1, 170, 205, 235,
                               bind_line1, masks[16], sizeof(masks[16])) != 0) return -1;
    if (update_osd_text_region(30, 68, panel_y + 246, 1, 170, 205, 235,
                               bind_line2, masks[17], sizeof(masks[17])) != 0) return -1;

    int cpu_w = (int)(g_perf.cpu_percent * 3.0);
    int gpu_w = g_perf.gpu_available ? (int)(g_perf.gpu_percent * 3.0) : 0;
    if (cpu_w < 0) cpu_w = 0;
    if (cpu_w > 300) cpu_w = 300;
    if (gpu_w < 0) gpu_w = 0;
    if (gpu_w > 300) gpu_w = 300;

    if (update_osd_text_region(31, 82, panel_y + 306, 1, 255, 230, 120,
                               "CPU", masks[18], sizeof(masks[18])) != 0) return -1;
    if (update_osd_text_region(32, 82, panel_y + 352, 1, 255, 230, 120,
                               "GPU", masks[19], sizeof(masks[19])) != 0) return -1;
    if (update_osd_text_region(33, 532, panel_y + 306, 1, 255, 230, 120,
                               "RGA IN STATUS", masks[20], sizeof(masks[20])) != 0) return -1;
    if (update_osd_text_region(34, 532, panel_y + 352, 1, 255, 230, 120,
                               "ROTATE LUT LIVE", masks[21], sizeof(masks[21])) != 0) return -1;
    if (update_osd_rect_region(35, 148, panel_y + 304, 300, 22, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(36, 148, panel_y + 304, cpu_w, 22, 120, 220, 255, 230) != 0) return -1;
    if (update_osd_rect_region(37, 148, panel_y + 350, 300, 22, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(38, 148, panel_y + 350, gpu_w, 22, 110, 230, 170, 230) != 0) return -1;
    int packet_x = 68 + ((frame * 18) % 890);
    if (update_osd_rect_region(39, packet_x, panel_y - 22, 52, 10, 255, 255, 255, 180) != 0) return -1;
    return 0;
}

static int update_stereo_3d_overlay(uint64_t vi_count, uint64_t stereo_count,
                                    int frame) {
    static uint8_t masks[28][1024 * 96];
    const int view_x = (SCREEN_W - CAM_W) / 2;
    const int view_y = 360;
    const int panel_y = 1118;
    char status_line[192];
    char count_line[160];
    char bind_line1[224];
    char bind_line2[224];
    char bind_line3[192];
    char runtime[128];

    format_runtime_status(runtime, sizeof(runtime));
    snprintf(status_line, sizeof(status_line),
             "VI=%llu  STEREO=%llu  %s",
             (unsigned long long)vi_count, (unsigned long long)stereo_count, runtime);
    snprintf(count_line, sizeof(count_line), "VPSS OUTPUT0 RAW  +  OUTPUT1 ROTATE_%d  -> ONE MERGED OUTPUT",
             STEREO_VPSS_ROTATE_DEGREES);
    snprintf(bind_line1, sizeof(bind_line1),
             "VI0.%s -> VPSS%d.%s  |  VPSS.%s RAW -> STEREO_3D%d.%s",
             g_bind_vi_src_port ? g_bind_vi_src_port : "output",
             LIVE_VPSS_GRP,
             g_bind_vpss_in_port ? g_bind_vpss_in_port : "input",
             g_bind_vpss_stereo_src_ports[0] ? g_bind_vpss_stereo_src_ports[0] : "output0",
             LIVE_STEREO_GRP,
             g_bind_stereo_in_ports[0] ? g_bind_stereo_in_ports[0] : "input0");
    snprintf(bind_line2, sizeof(bind_line2),
             "VPSS.%s ROT%d -> STEREO_3D%d.%s  |  STEREO.%s -> VMIX%d.%s",
             g_bind_vpss_stereo_src_ports[1] ? g_bind_vpss_stereo_src_ports[1] : "output1",
             STEREO_VPSS_ROTATE_DEGREES,
             LIVE_STEREO_GRP,
             g_bind_stereo_in_ports[1] ? g_bind_stereo_in_ports[1] : "input1",
             g_bind_stereo_src_ports[0] ? g_bind_stereo_src_ports[0] : "output0",
             DISPLAY_VMIX_GRP,
             g_bind_stereo_vmix_in_ports[0] ? g_bind_stereo_vmix_in_ports[0] : "input0");
    snprintf(bind_line3, sizeof(bind_line3),
             "VMIX%d.%s -> OSD%d.%s -> VO0.%s",
             DISPLAY_VMIX_GRP,
             g_bind_vmix_src_port ? g_bind_vmix_src_port : "output0",
             DISPLAY_OSD_GRP,
             g_bind_osd_in_port ? g_bind_osd_in_port : "input0",
             g_bind_vo_in_port ? g_bind_vo_in_port : "input0");

    if (update_osd_rect_region(4, view_x + 18, view_y + 18, 420, 92, 5, 10, 18, 220) != 0) return -1;
    if (update_osd_rect_region(5, view_x + 18, view_y + 18, 420, 8, 80, 170, 255, 235) != 0) return -1;
    if (update_osd_text_region(6, view_x + 34, view_y + 38, 2, 255, 230, 120,
                               "RAW + ROTATE MERGE", masks[0], sizeof(masks[0])) != 0) return -1;
    if (update_osd_utf8_text_region(7, view_x + 34, view_y + 74, 18, 190, 230, 255,
                                    "左侧原图，右侧VPSS旋转90度后合并",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;

    if (update_osd_rect_region(8, 34, panel_y, 1012, 386, 5, 10, 18, 226) != 0) return -1;
    if (update_osd_utf8_text_region(9, 66, panel_y + 24, 30, 160, 255, 220,
                                    "STEREO_3D：不做颜色tint，只合并原图和旋转图",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;
    if (update_osd_text_region(10, 68, panel_y + 78, 2, 170, 255, 220,
                               status_line, masks[3], sizeof(masks[3])) != 0) return -1;
    if (update_osd_text_region(11, 68, panel_y + 124, 1, 190, 230, 255,
                               count_line, masks[4], sizeof(masks[4])) != 0) return -1;
    if (update_osd_text_region(12, 68, panel_y + 166, 1, 170, 205, 235,
                               bind_line1, masks[5], sizeof(masks[5])) != 0) return -1;
    if (update_osd_text_region(13, 68, panel_y + 198, 1, 170, 205, 235,
                               bind_line2, masks[6], sizeof(masks[6])) != 0) return -1;
    if (update_osd_text_region(14, 68, panel_y + 230, 1, 170, 205, 235,
                               bind_line3, masks[7], sizeof(masks[7])) != 0) return -1;

    int cpu_w = (int)(g_perf.cpu_percent * 3.0);
    int gpu_w = g_perf.gpu_available ? (int)(g_perf.gpu_percent * 3.0) : 0;
    if (cpu_w < 0) cpu_w = 0;
    if (cpu_w > 300) cpu_w = 300;
    if (gpu_w < 0) gpu_w = 0;
    if (gpu_w > 300) gpu_w = 300;
    if (update_osd_text_region(15, 82, panel_y + 302, 1, 255, 230, 120,
                               "CPU", masks[8], sizeof(masks[8])) != 0) return -1;
    if (update_osd_text_region(16, 82, panel_y + 348, 1, 255, 230, 120,
                               "GPU", masks[9], sizeof(masks[9])) != 0) return -1;
    if (update_osd_rect_region(17, 148, panel_y + 300, 300, 22, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(18, 148, panel_y + 300, cpu_w, 22, 120, 220, 255, 230) != 0) return -1;
    if (update_osd_rect_region(19, 148, panel_y + 346, 300, 22, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(20, 148, panel_y + 346, gpu_w, 22, 110, 230, 170, 230) != 0) return -1;
    if (update_osd_text_region(21, 532, panel_y + 302, 1, 255, 230, 120,
                               "NO COLOR TINT", masks[10], sizeof(masks[10])) != 0) return -1;
    if (update_osd_text_region(22, 532, panel_y + 348, 1, 255, 230, 120,
                               "VPSS ROTATE 90", masks[11], sizeof(masks[11])) != 0) return -1;

    int packet_x = 68 + ((frame * 18) % 890);
    if (update_osd_rect_region(23, packet_x, panel_y - 22, 52, 10, 255, 255, 255, 180) != 0) return -1;
    return 0;
}

static int update_npu_vdec_overlay(const npu_demo_result_t *det,
                                   uint64_t vdec_count,
                                   uint64_t pre_rga_count,
                                   uint64_t npu_count,
                                   uint64_t post_rga_count,
                                   uint64_t vmix_count,
                                   const char *stream_path,
                                   int frame) {
    static uint8_t masks[24][1024 * 96];
    const int view_x = (SCREEN_W - CAM_W) / 2;
    const int view_y = 360;
    const int panel_y = 1118;
    char title[192];
    char flow1[224];
    char flow2[224];
    char count_line[192];
    char resource_line[160];
    char runtime[128];
    int object_count = det ? det->object_count : 0;

    if (object_count < 0) object_count = 0;
    if (object_count > NPU_DEMO_MAX_DRAW_OBJECTS) object_count = NPU_DEMO_MAX_DRAW_OBJECTS;

    format_runtime_status(runtime, sizeof(runtime));
    snprintf(title, sizeof(title), "NPU：H264视频检测，OSD实时画框");
    snprintf(flow1, sizeof(flow1),
             "数据流：H264文件 -> VDEC解码NV12 -> RGA转640x640 RGB -> NPU(YOLOv5)");
    snprintf(flow2, sizeof(flow2),
             "为什么这样做：模型吃RGB，显示链路吃NV12；VMIX摆画面，OSD只叠加框和说明。");
    snprintf(count_line, sizeof(count_line),
             "帧计数  VDEC=%llu  RGA前=%llu  NPU=%llu  RGA后=%llu  VMIX=%llu  DET=%d",
             (unsigned long long)vdec_count,
             (unsigned long long)pre_rga_count,
             (unsigned long long)npu_count,
             (unsigned long long)post_rga_count,
             (unsigned long long)vmix_count,
             object_count);
    snprintf(resource_line, sizeof(resource_line),
             "SRC %s  %s",
             stream_path ? stream_path : "h264", runtime);

    if (update_osd_rect_region(4, 34, panel_y, 1012, 340, 5, 10, 18, 226) != 0) return -1;
    if (update_osd_utf8_text_region(5, 66, panel_y + 24, 30, 160, 255, 220,
                                    title, masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 68, panel_y + 76, 22, 190, 230, 255,
                                    flow1, masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(7, 68, panel_y + 118, 22, 190, 230, 255,
                                    flow2, masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;
    if (update_osd_text_region(8, 68, panel_y + 166, 1, 255, 230, 120,
                               count_line, masks[3], sizeof(masks[3])) != 0) return -1;
    if (update_osd_text_region(9, 68, panel_y + 204, 1, 170, 205, 235,
                               resource_line, masks[4], sizeof(masks[4])) != 0) return -1;

    int cpu_w = (int)(g_perf.cpu_percent * 2.2f);
    int gpu_w = g_perf.gpu_available ? (int)(g_perf.gpu_percent * 2.2f) : 0;
    int rga_w = g_perf.rga_available ? (int)(g_perf.rga_percent * 2.2f) : 0;
    if (cpu_w < 0) cpu_w = 0;
    if (gpu_w < 0) gpu_w = 0;
    if (rga_w < 0) rga_w = 0;
    if (cpu_w > 220) cpu_w = 220;
    if (gpu_w > 220) gpu_w = 220;
    if (rga_w > 220) rga_w = 220;
    if (update_osd_text_region(10, 68, panel_y + 258, 1, 255, 230, 120,
                               "CPU", masks[5], sizeof(masks[5])) != 0) return -1;
    if (update_osd_text_region(11, 372, panel_y + 258, 1, 255, 230, 120,
                               "GPU", masks[6], sizeof(masks[6])) != 0) return -1;
    if (update_osd_text_region(12, 676, panel_y + 258, 1, 255, 230, 120,
                               "RGA", masks[7], sizeof(masks[7])) != 0) return -1;
    if (update_osd_rect_region(13, 126, panel_y + 258, 220, 20, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(14, 126, panel_y + 258, cpu_w, 20, 120, 220, 255, 230) != 0) return -1;
    if (update_osd_rect_region(15, 430, panel_y + 258, 220, 20, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(16, 430, panel_y + 258, gpu_w, 20, 110, 230, 170, 230) != 0) return -1;
    if (update_osd_rect_region(17, 734, panel_y + 258, 220, 20, 28, 48, 68, 220) != 0) return -1;
    if (update_osd_rect_region(18, 734, panel_y + 258, rga_w, 20, 255, 170, 100, 230) != 0) return -1;

    int packet_x = 68 + ((frame * 18) % 890);
    if (update_osd_rect_region(19, packet_x, panel_y - 24, 52, 10, 255, 255, 255, 180) != 0) return -1;

    static const uint8_t colors[NPU_DEMO_MAX_DRAW_OBJECTS][3] = {
        {80, 220, 255}, {255, 190, 90}, {140, 255, 160}, {255, 120, 170}, {190, 170, 255},
    };
    for (int i = 0; i < NPU_DEMO_MAX_DRAW_OBJECTS; ++i) {
        int box_region = 20 + i * 2;
        int label_region = box_region + 1;
        if (!det || i >= object_count) {
            (void)disable_osd_region(box_region);
            (void)disable_osd_region(label_region);
            continue;
        }

        const npu_demo_object_t *obj = &det->objects[i];
        int x = (int)(obj->x + 0.5f);
        int y = (int)(obj->y + 0.5f);
        int w = (int)(obj->w + 0.5f);
        int h = (int)(obj->h + 0.5f);
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > CAM_W) w = CAM_W - x;
        if (y + h > CAM_H) h = CAM_H - y;
        if (w < 8 || h < 8) {
            (void)disable_osd_region(box_region);
            (void)disable_osd_region(label_region);
            continue;
        }

        uint8_t r = colors[i][0];
        uint8_t g = colors[i][1];
        uint8_t b = colors[i][2];
        if (update_osd_outline_region(box_region, view_x + x, view_y + y, w, h,
                                      4, r, g, b, 255) != 0) return -1;

        char label[96];
        const char *name = obj->label[0] ? obj->label : "object";
        snprintf(label, sizeof(label), "%s %.2f", name, obj->score);
        int label_y = view_y + y - 30;
        if (label_y < 110) label_y = view_y + y + 8;
        if (update_osd_text_region(label_region, view_x + x, label_y, 1, r, g, b,
                                   label, masks[8 + i], sizeof(masks[8 + i])) != 0) return -1;
    }

    return 0;
}

static int setup_display_vmix_osd(int dstride, size_t display_size, int input_count, int layout_mode) {
    int input_pool_count = input_count >= VPSS_DEMO_OUTPUTS ? VPSS_DEMO_OUTPUTS * 4 : 3;
    size_t input_pool_size = layout_mode == DISPLAY_VMIX_LAYOUT_WBC ? WBC_PANEL_FRAME_SIZE : CAM_FRAME_SIZE;
    if (layout_mode == DISPLAY_VMIX_LAYOUT_WBC) {
        input_pool_count = 6;
    } else if (layout_mode == DISPLAY_VMIX_LAYOUT_VI_BIG) {
        input_pool_size = VI_BIG_VIEW_FRAME_SIZE;
        input_pool_count = 4;
    }
    if (MEDIA_POOL_Create(DISPLAY_VMIX_INPUT_POOL, input_pool_size, input_pool_count) != 0) return -1;
    if (MEDIA_POOL_Create(DISPLAY_VMIX_OUTPUT_POOL, display_size, 4) != 0) goto fail_input;
    if (MEDIA_POOL_Create(DISPLAY_OSD_OUTPUT_POOL, display_size, 4) != 0) goto fail_vmix_pool;

    MEDIA_VMIX_ATTR vmix = {0};
    vmix.input_count = input_count;
    vmix.output_width = SCREEN_W;
    vmix.output_height = SCREEN_H;
    vmix.output_stride = dstride;
    vmix.format = MEDIA_FORMAT_NV12;
    vmix.input_depth = 3;
    vmix.output_pool_id = DISPLAY_VMIX_OUTPUT_POOL;
    vmix.primary_index = -1;
    if (layout_mode == DISPLAY_VMIX_LAYOUT_WBC && input_count == 2) {
        vmix.channels[0].enabled = 1;
        vmix.channels[0].x = WBC_VI_PANEL_X;
        vmix.channels[0].y = WBC_VI_PANEL_Y;
        vmix.channels[0].width = WBC_VI_PANEL_W;
        vmix.channels[0].height = WBC_VI_PANEL_H;
        vmix.channels[0].alpha = 1.0f;
        vmix.channels[0].stride = WBC_VI_PANEL_STRIDE;
        vmix.channels[0].format = MEDIA_FORMAT_NV12;

        vmix.channels[1].enabled = 1;
        vmix.channels[1].x = WBC_PANEL_X;
        vmix.channels[1].y = WBC_PANEL_Y;
        vmix.channels[1].width = WBC_PANEL_W;
        vmix.channels[1].height = WBC_PANEL_H;
        vmix.channels[1].alpha = 1.0f;
        vmix.channels[1].stride = WBC_PANEL_STRIDE;
        vmix.channels[1].format = MEDIA_FORMAT_NV12;
    } else if (layout_mode == DISPLAY_VMIX_LAYOUT_VI_BIG && input_count == 1) {
        vmix.channels[0].enabled = 1;
        vmix.channels[0].x = VI_BIG_VIEW_X;
        vmix.channels[0].y = VI_BIG_VIEW_Y;
        vmix.channels[0].width = VI_BIG_VIEW_W;
        vmix.channels[0].height = VI_BIG_VIEW_H;
        vmix.channels[0].alpha = 1.0f;
        vmix.channels[0].stride = VI_BIG_VIEW_STRIDE;
        vmix.channels[0].format = MEDIA_FORMAT_NV12;
    } else if (layout_mode == DISPLAY_VMIX_LAYOUT_DEMO && input_count == VPSS_DEMO_OUTPUTS) {
        static const int pos_x[VPSS_DEMO_OUTPUTS] = {54, 546, 54, 546};
        static const int pos_y[VPSS_DEMO_OUTPUTS] = {170, 170, 690, 690};
        for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
            vmix.channels[i].enabled = 1;
            vmix.channels[i].x = pos_x[i];
            vmix.channels[i].y = pos_y[i];
            vmix.channels[i].width = VPSS_TILE_W;
            vmix.channels[i].height = VPSS_TILE_H;
            vmix.channels[i].alpha = 1.0f;
            vmix.channels[i].stride = VPSS_TILE_NV12_STRIDE;
            vmix.channels[i].format = MEDIA_FORMAT_NV12;
        }
    } else if (input_count == VPSS_DEMO_OUTPUTS) {
        int start_x = 0, start_y = 0, cell = 0, gap = 0;
        vpss_bind_layout(&start_x, &start_y, &cell, &gap);
        for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
            vmix.channels[i].enabled = 1;
            vmix.channels[i].x = start_x + (i % 2) * (cell + gap);
            vmix.channels[i].y = start_y + (i / 2) * (cell + gap);
            vmix.channels[i].width = cell;
            vmix.channels[i].height = cell;
            vmix.channels[i].alpha = 1.0f;
            vmix.channels[i].stride = CAM_STRIDE;
        }
    } else if (input_count == 2) {
        int start_x = (SCREEN_W - CAM_W) / 2;
        int top_y = 164;
        int bottom_y = 840;
        for (int i = 0; i < 2; ++i) {
            vmix.channels[i].enabled = 1;
            vmix.channels[i].x = start_x;
            vmix.channels[i].y = i == 0 ? top_y : bottom_y;
            vmix.channels[i].width = CAM_W;
            vmix.channels[i].height = CAM_H;
            vmix.channels[i].alpha = 1.0f;
            vmix.channels[i].stride = CAM_STRIDE;
        }
    } else {
        vmix.input_count = 1;
        vmix.channels[0].enabled = 1;
        vmix.channels[0].x = (SCREEN_W - CAM_W) / 2;
        vmix.channels[0].y = 360;
        vmix.channels[0].width = CAM_W;
        vmix.channels[0].height = CAM_H;
        vmix.channels[0].alpha = 1.0f;
        vmix.channels[0].stride = CAM_STRIDE;
    }
    if (MEDIA_VMIX_CreateGrp(DISPLAY_VMIX_GRP, &vmix) != 0 ||
        MEDIA_VMIX_Start(DISPLAY_VMIX_GRP) != 0 ||
        MEDIA_VMIX_Enable(DISPLAY_VMIX_GRP) != 0) {
        MEDIA_VMIX_DestroyGrp(DISPLAY_VMIX_GRP);
        goto fail_osd_pool;
    }

    MEDIA_OSD_ATTR osd = {0};
    osd.input_width = SCREEN_W;
    osd.input_height = SCREEN_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 3;
    osd.output_pool_id = DISPLAY_OSD_OUTPUT_POOL;
    osd.input_stride = dstride;
    osd.output_stride = dstride;
    osd.max_regions = 40;
    if (MEDIA_OSD_CreateGrp(DISPLAY_OSD_GRP, &osd) != 0) goto fail_vmix;

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
    top.width = SCREEN_W;
    top.height = 96;
    top.zorder = 0;
    top.global_alpha = 220;
    MEDIA_OSD_REGION_ATTR bottom = top;
    bottom.y = SCREEN_H - 112;
    bottom.height = 112;
    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 0, &top) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, 0, &rect) != 0 ||
        MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 1, &bottom) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, 1, &rect) != 0 ||
        MEDIA_OSD_Start(DISPLAY_OSD_GRP) != 0) {
        MEDIA_OSD_DestroyGrp(DISPLAY_OSD_GRP);
        goto fail_vmix;
    }
    return 0;

fail_vmix:
    MEDIA_VMIX_Disable(DISPLAY_VMIX_GRP);
    MEDIA_VMIX_Stop(DISPLAY_VMIX_GRP);
    MEDIA_VMIX_DestroyGrp(DISPLAY_VMIX_GRP);
fail_osd_pool:
    MEDIA_POOL_Destroy(DISPLAY_OSD_OUTPUT_POOL);
fail_vmix_pool:
    MEDIA_POOL_Destroy(DISPLAY_VMIX_OUTPUT_POOL);
fail_input:
    MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
    return -1;
}

static int module_page_number(const char *name) {
    const char *env = getenv(NAV_ENV_PAGE);
    if (env && env[0]) {
        char *end = NULL;
        long v = strtol(env, &end, 10);
        if (end != env && v > 0 && v < 1000) return (int)v;
    }
    if (!name) return 1;
    for (size_t i = 0; i < ARRAY_SIZE(g_module_pages); ++i) {
        if (strcasecmp(g_module_pages[i], name) == 0) return (int)i + 1;
    }
    return 1;
}

static int module_page_total(void) {
    const char *env = getenv(NAV_ENV_TOTAL);
    if (env && env[0]) {
        char *end = NULL;
        long v = strtol(env, &end, 10);
        if (end != env && v > 0 && v < 1000) return (int)v;
    }
    return (int)(sizeof(g_module_pages) / sizeof(g_module_pages[0]));
}

static const char *showcase_nav_mode(void) {
    const char *mode = getenv(NAV_ENV_MODE);
    const char *profile = getenv(NAV_ENV_PROFILE);
    if (!mode || !mode[0]) return NULL;
    if (profile && strcasecmp(profile, "CUSTOMER") == 0) {
        return strcasecmp(mode, "MANUAL") == 0 ? "CUSTOMER MANUAL" : "CUSTOMER DEMO";
    }
    if (profile && strcasecmp(profile, "ENGINEERING") == 0) {
        return strcasecmp(mode, "MANUAL") == 0 ? "ENGINEER MANUAL" : "ENGINEER DEMO";
    }
    if (strcasecmp(mode, "MANUAL") == 0) return "MANUAL";
    if (strcasecmp(mode, "AUTO") == 0) return "AUTO LOOP";
    return NULL;
}

static int default_page_index(const char *name) {
    if (!name) return -1;
    for (size_t i = 0; i < ARRAY_SIZE(g_default_pages); ++i) {
        if (strcasecmp(g_default_pages[i], name) == 0) return (int)i;
    }
    return -1;
}

static void fill_live_rga_attr(MEDIA_RGA_GRP_ATTR *attr, const rga_demo_op_t *op) {
    memset(attr, 0, sizeof(*attr));
    attr->algo = op->algo;
    if (op->algo == MEDIA_RGA_ALG_COPY &&
        (g_camera_input_w != CAM_W || g_camera_input_h != CAM_H)) {
        attr->algo = MEDIA_RGA_ALG_RESIZE;
    }
    attr->input_count = 1;
    attr->output_count = 1;
    attr->input_depth = 4;
    attr->output_depth = 4;
    attr->inputs[0].port_id = 0;
    attr->inputs[0].width = g_camera_input_w;
    attr->inputs[0].height = g_camera_input_h;
    attr->inputs[0].stride = g_camera_input_stride;
    attr->inputs[0].format = MEDIA_FORMAT_NV12;
    attr->inputs[0].crop_x = (op->crop_x * g_camera_input_w) / CAM_W;
    attr->inputs[0].crop_y = (op->crop_y * g_camera_input_h) / CAM_H;
    attr->inputs[0].crop_w = (op->crop_w * g_camera_input_w) / CAM_W;
    attr->inputs[0].crop_h = (op->crop_h * g_camera_input_h) / CAM_H;
    attr->outputs[0].port_id = 0;
    attr->outputs[0].width = VI_BIG_VIEW_W;
    attr->outputs[0].height = VI_BIG_VIEW_H;
    attr->outputs[0].stride = VI_BIG_VIEW_STRIDE;
    attr->outputs[0].format = MEDIA_FORMAT_NV12;
    attr->outputs[0].pool_id = DISPLAY_VMIX_INPUT_POOL;
    attr->outputs[0].rotate = op->rotate;
    attr->outputs[0].flip_h = op->flip_h;
    attr->outputs[0].flip_v = op->flip_v;
}

static int setup_live_rga(void) {
    MEDIA_RGA_GRP_ATTR attr = {0};
    fill_live_rga_attr(&attr, &g_rga_demo_ops[0]);
    if (MEDIA_RGA_CreateGrp(LIVE_RGA_GRP, &attr) != 0 ||
        MEDIA_RGA_Start(LIVE_RGA_GRP) != 0) {
        MEDIA_RGA_Stop(LIVE_RGA_GRP);
        MEDIA_RGA_DestroyChn(LIVE_RGA_GRP);
        return -1;
    }
    set_tile_status("RGA", TILE_LIVE);
    return 0;
}

static void cleanup_live_rga(int enabled) {
    if (!enabled) return;
    MEDIA_RGA_Stop(LIVE_RGA_GRP);
    MEDIA_RGA_DestroyChn(LIVE_RGA_GRP);
}

static int rga_wave_i(int t, int max_value) {
    int period;
    int v;
    if (max_value <= 0) return 0;
    period = max_value * 2;
    v = t % period;
    return v <= max_value ? v : period - v;
}

static int set_live_rga_op(int op_index, int frame) {
    MEDIA_RGA_GRP_ATTR attr = {0};
    if (op_index < 0 || op_index >= (int)ARRAY_SIZE(g_rga_demo_ops)) return -1;
    fill_live_rga_attr(&attr, &g_rga_demo_ops[op_index]);
    if (op_index == 1) {
        int max_h = g_camera_input_h;
        int min_h = max_h / 2;
        int crop_h = min_h + (rga_wave_i(frame * 3, max_h / 4) & ~1);
        int crop_w = (crop_h * 16 / 9) & ~1;
        if (crop_w > g_camera_input_w) {
            crop_w = g_camera_input_w & ~1;
            crop_h = ((crop_w * 9 / 16) & ~1);
        }
        int max_x = g_camera_input_w - crop_w;
        int max_y = g_camera_input_h - crop_h;
        attr.inputs[0].crop_x = rga_wave_i(frame * 5, max_x) & ~1;
        attr.inputs[0].crop_y = rga_wave_i(frame * 4, max_y) & ~1;
        attr.inputs[0].crop_w = crop_w;
        attr.inputs[0].crop_h = crop_h;
    }
    return MEDIA_RGA_SetGrpAttr(LIVE_RGA_GRP, &attr);
}

static void drain_display_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER buf = {-1, -1};
        if (MEDIA_OSD_GetFrame(DISPLAY_OSD_GRP, &buf, 0) != 0) {
            break;
        }
        MEDIA_OSD_ReleaseFrame(DISPLAY_OSD_GRP, buf);
    }
}

static void drain_display_vmix_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER buf = {-1, -1};
            if (MEDIA_VMIX_GetFrame(DISPLAY_VMIX_GRP, &buf, 0) != 0) {
                break;
            }
            MEDIA_VMIX_ReleaseFrame(DISPLAY_VMIX_GRP, buf);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_display_vmix_rga_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER buf = {-1, -1};
            if (MEDIA_VMIX_RGA_GetFrame(DISPLAY_VMIX_GRP, &buf, 0) != 0) {
                break;
            }
            MEDIA_VMIX_RGA_ReleaseFrame(DISPLAY_VMIX_GRP, buf);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_csc_cl_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER buf = {-1, -1};
            if (MEDIA_CSC_CL_GetFrame(LIVE_CSC_CL_GRP, &buf, 0) != 0) {
                break;
            }
            MEDIA_CSC_CL_ReleaseFrame(LIVE_CSC_CL_GRP, buf);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_csc_cl_resize_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER buf = {-1, -1};
            if (MEDIA_RESIZE_RGA_GetFrame(LIVE_RESIZE_GRP, &buf, 0) != 0) {
                break;
            }
            MEDIA_RESIZE_RGA_ReleaseFrame(LIVE_RESIZE_GRP, buf);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_live_vpss_outputs(int output_count) {
    if (output_count < 1) return;
    if (output_count > VPSS_DEMO_OUTPUTS) output_count = VPSS_DEMO_OUTPUTS;
    for (int output_id = 0; output_id < output_count; ++output_id) {
        for (int pass = 0; pass < 6; ++pass) {
            int drained = 0;
            for (int i = 0; i < 8; ++i) {
                MEDIA_BUFFER buf = {-1, -1};
                if (MEDIA_VPSS_Chn_GetFrame(LIVE_VPSS_GRP, output_id, &buf, 0) != 0) {
                    break;
                }
                MEDIA_VPSS_Chn_ReleaseFrame(LIVE_VPSS_GRP, output_id, buf);
                drained = 1;
            }
            if (!drained) break;
            usleep(1000);
        }
    }
}

static void drain_live_clahe_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER buf = {-1, -1};
            if (MEDIA_CLAHE_GetFrame(LIVE_CLAHE_GRP, &buf, 0) != 0) break;
            MEDIA_CLAHE_ReleaseFrame(LIVE_CLAHE_GRP, buf);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_live_retinex_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER buf = {-1, -1};
            if (MEDIA_RETINEX_GetFrame(LIVE_RETINEX_GRP, &buf, 0) != 0) break;
            MEDIA_RETINEX_ReleaseFrame(LIVE_RETINEX_GRP, buf);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_resize_rga_output(int grp) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER buf = {-1, -1};
            if (MEDIA_RESIZE_RGA_GetFrame(grp, &buf, 0) != 0) break;
            MEDIA_RESIZE_RGA_ReleaseFrame(grp, buf);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void stop_destroy_vo_channel(void) {
    MEDIA_VO_Stop(0, 0);
    MEDIA_VO_DestroyChn(0, 0);
}

static void cleanup_display_vmix_osd_ex(int enabled, int destroy_output_pools) {
    if (!enabled) return;
    drain_display_osd_output();
    MEDIA_OSD_Stop(DISPLAY_OSD_GRP);
    drain_display_osd_output();
    MEDIA_OSD_DestroyGrp(DISPLAY_OSD_GRP);
    drain_display_vmix_output();
    MEDIA_VMIX_Disable(DISPLAY_VMIX_GRP);
    MEDIA_VMIX_Stop(DISPLAY_VMIX_GRP);
    drain_display_vmix_output();
    MEDIA_VMIX_DestroyGrp(DISPLAY_VMIX_GRP);
    if (destroy_output_pools) {
        MEDIA_POOL_Destroy(DISPLAY_OSD_OUTPUT_POOL);
        MEDIA_POOL_Destroy(DISPLAY_VMIX_OUTPUT_POOL);
    }
    MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
}

static void cleanup_display_vmix_osd(int enabled) {
    cleanup_display_vmix_osd_ex(enabled, 1);
}

static int setup_display_vmix_rga_osd_inputs(int dstride, size_t display_size, int input_count) {
    int input_pool_count = input_count >= VPSS_DEMO_OUTPUTS ? VPSS_DEMO_OUTPUTS * 4 : 3;
    size_t input_pool_size = input_count >= VPSS_DEMO_OUTPUTS ? CAM_FRAME_SIZE : COMPARE_VIEW_FRAME_SIZE;
    if (MEDIA_POOL_Create(DISPLAY_VMIX_INPUT_POOL, input_pool_size, input_pool_count) != 0) return -1;
    if (MEDIA_POOL_Create(DISPLAY_VMIX_OUTPUT_POOL, display_size, 4) != 0) goto fail_input;
    if (MEDIA_POOL_Create(DISPLAY_OSD_OUTPUT_POOL, display_size, 4) != 0) goto fail_vmix_pool;

    MEDIA_VMIX_RGA_ATTR vmix = {0};
    vmix.input_count = input_count;
    vmix.output_width = SCREEN_W;
    vmix.output_height = SCREEN_H;
    vmix.output_stride = dstride;
    vmix.format = MEDIA_FORMAT_NV12;
    vmix.input_depth = 3;
    vmix.output_pool_id = DISPLAY_VMIX_OUTPUT_POOL;
    vmix.primary_index = -1;
    vmix.use_rga_compose = 0;
    if (input_count == VPSS_DEMO_OUTPUTS) {
        int start_x = 0, start_y = 0, cell = 0, gap = 0;
        vpss_bind_layout(&start_x, &start_y, &cell, &gap);
        for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
            vmix.channels[i].enabled = 1;
            vmix.channels[i].x = start_x + (i % 2) * (cell + gap);
            vmix.channels[i].y = start_y + (i / 2) * (cell + gap);
            vmix.channels[i].width = cell;
            vmix.channels[i].height = cell;
            vmix.channels[i].stride = CAM_STRIDE;
            vmix.channels[i].format = MEDIA_FORMAT_NV12;
        }
    } else if (input_count == 1) {
        int start_x = (SCREEN_W - COMPARE_VIEW_W) / 2;
        int start_y = (SCREEN_H - COMPARE_VIEW_H) / 2;
        vmix.input_count = 1;
        vmix.channels[0].enabled = 1;
        vmix.channels[0].x = start_x;
        vmix.channels[0].y = start_y;
        vmix.channels[0].width = COMPARE_VIEW_W;
        vmix.channels[0].height = COMPARE_VIEW_H;
        vmix.channels[0].stride = COMPARE_VIEW_STRIDE;
        vmix.channels[0].format = MEDIA_FORMAT_NV12;
    } else {
        int start_x = (SCREEN_W - COMPARE_VIEW_W) / 2;
        int top_y = 156;
        int bottom_y = 856;
        vmix.input_count = 2;
        for (int i = 0; i < 2; ++i) {
            vmix.channels[i].enabled = 1;
            vmix.channels[i].x = start_x;
            vmix.channels[i].y = i == 0 ? top_y : bottom_y;
            vmix.channels[i].width = COMPARE_VIEW_W;
            vmix.channels[i].height = COMPARE_VIEW_H;
            vmix.channels[i].stride = COMPARE_VIEW_STRIDE;
            vmix.channels[i].format = MEDIA_FORMAT_NV12;
        }
    }
    if (MEDIA_VMIX_RGA_CreateGrp(DISPLAY_VMIX_GRP, &vmix) != 0 ||
        MEDIA_VMIX_RGA_Start(DISPLAY_VMIX_GRP) != 0 ||
        MEDIA_VMIX_RGA_Enable(DISPLAY_VMIX_GRP) != 0) {
        MEDIA_VMIX_RGA_DestroyGrp(DISPLAY_VMIX_GRP);
        goto fail_osd_pool;
    }

    MEDIA_OSD_ATTR osd = {0};
    osd.input_width = SCREEN_W;
    osd.input_height = SCREEN_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 3;
    osd.output_pool_id = DISPLAY_OSD_OUTPUT_POOL;
    osd.input_stride = dstride;
    osd.output_stride = dstride;
    osd.max_regions = 40;
    if (MEDIA_OSD_CreateGrp(DISPLAY_OSD_GRP, &osd) != 0) goto fail_vmix;

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
    top.width = SCREEN_W;
    top.height = 96;
    top.zorder = 0;
    top.global_alpha = 220;
    MEDIA_OSD_REGION_ATTR bottom = top;
    bottom.y = SCREEN_H - 112;
    bottom.height = 112;
    if (MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 0, &top) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, 0, &rect) != 0 ||
        MEDIA_OSD_UpdateRegion(DISPLAY_OSD_GRP, 1, &bottom) != 0 ||
        MEDIA_OSD_SetRegionRect(DISPLAY_OSD_GRP, 1, &rect) != 0 ||
        MEDIA_OSD_Start(DISPLAY_OSD_GRP) != 0) {
        MEDIA_OSD_DestroyGrp(DISPLAY_OSD_GRP);
        goto fail_vmix;
    }
    return 0;

fail_vmix:
    MEDIA_VMIX_RGA_Disable(DISPLAY_VMIX_GRP);
    MEDIA_VMIX_RGA_Stop(DISPLAY_VMIX_GRP);
    MEDIA_VMIX_RGA_DestroyGrp(DISPLAY_VMIX_GRP);
fail_osd_pool:
    MEDIA_POOL_Destroy(DISPLAY_OSD_OUTPUT_POOL);
fail_vmix_pool:
    MEDIA_POOL_Destroy(DISPLAY_VMIX_OUTPUT_POOL);
fail_input:
    MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
    return -1;
}

static int setup_display_vmix_rga_osd(int dstride, size_t display_size) {
    return setup_display_vmix_rga_osd_inputs(dstride, display_size, 2);
}

static void cleanup_display_vmix_rga_osd(int enabled) {
    if (!enabled) return;
    drain_display_osd_output();
    MEDIA_OSD_Stop(DISPLAY_OSD_GRP);
    drain_display_osd_output();
    MEDIA_OSD_DestroyGrp(DISPLAY_OSD_GRP);
    drain_display_vmix_rga_output();
    MEDIA_VMIX_RGA_Disable(DISPLAY_VMIX_GRP);
    MEDIA_VMIX_RGA_Stop(DISPLAY_VMIX_GRP);
    drain_display_vmix_rga_output();
    MEDIA_VMIX_RGA_DestroyGrp(DISPLAY_VMIX_GRP);
    MEDIA_POOL_Destroy(DISPLAY_OSD_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DISPLAY_VMIX_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
}

static void fill_compare_display_resize_attr(MEDIA_RESIZE_RGA_ATTR *attr) {
    memset(attr, 0, sizeof(*attr));
    attr->src_x = 0;
    attr->src_y = 0;
    attr->src_width = g_camera_input_w;
    attr->src_height = g_camera_input_h;
    attr->input_width = g_camera_input_w;
    attr->input_height = g_camera_input_h;
    attr->input_stride = g_camera_input_stride;
    attr->input_format = MEDIA_FORMAT_NV12;
    attr->input_depth = 4;
    attr->out_width = COMPARE_VIEW_W;
    attr->out_height = COMPARE_VIEW_H;
    attr->out_stride = COMPARE_VIEW_STRIDE;
    attr->output_format = MEDIA_FORMAT_NV12;
    attr->output_pool_id = DISPLAY_VMIX_INPUT_POOL;
}

static void fill_transform_display_resize_attr(MEDIA_RESIZE_RGA_ATTR *attr) {
    fill_compare_display_resize_attr(attr);
    attr->out_width = VPSS_TILE_W;
    attr->out_height = VPSS_TILE_H;
    attr->out_stride = CAM_STRIDE;
}

static int setup_live_compare_display_resizes(void) {
    MEDIA_RESIZE_RGA_ATTR raw = {0};
    MEDIA_RESIZE_RGA_ATTR effect = {0};
    fill_compare_display_resize_attr(&raw);
    fill_compare_display_resize_attr(&effect);

    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_COMPARE_RAW_RESIZE_GRP, &raw) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_COMPARE_RAW_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_COMPARE_RAW_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(LIVE_COMPARE_RAW_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_COMPARE_RAW_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_COMPARE_RAW_RESIZE_GRP);
        return -1;
    }

    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_COMPARE_EFFECT_RESIZE_GRP, &effect) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_COMPARE_EFFECT_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_COMPARE_EFFECT_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(LIVE_COMPARE_EFFECT_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_COMPARE_EFFECT_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_COMPARE_EFFECT_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Disable(LIVE_COMPARE_RAW_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_COMPARE_RAW_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_COMPARE_RAW_RESIZE_GRP);
        return -1;
    }
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;
}

static int setup_live_compare_effect_display_resize(void) {
    MEDIA_RESIZE_RGA_ATTR effect = {0};
    fill_compare_display_resize_attr(&effect);

    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_COMPARE_EFFECT_RESIZE_GRP, &effect) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_COMPARE_EFFECT_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_COMPARE_EFFECT_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(LIVE_COMPARE_EFFECT_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_COMPARE_EFFECT_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_COMPARE_EFFECT_RESIZE_GRP);
        return -1;
    }
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;
}

static void cleanup_live_compare_display_resizes(int enabled) {
    if (!enabled) return;
    drain_resize_rga_output(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Disable(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    if (enabled == 2) return;
    drain_resize_rga_output(LIVE_COMPARE_RAW_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Disable(LIVE_COMPARE_RAW_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_COMPARE_RAW_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_COMPARE_RAW_RESIZE_GRP);
}

static int setup_live_wbc(void) {
    int connector_id = 0;
    int crtc_id = 0;
    MEDIA_VO_WBC_ATTR attr = {0};

    if (MEDIA_VO_WBC_Probe(NULL, &connector_id, &crtc_id) != 0) {
        fprintf(stderr, "WBC setup failed: no usable VO_WBC writeback connector\n");
        return -1;
    }
    if (MEDIA_POOL_Create(WBC_OUTPUT_POOL, WBC_FRAME_SIZE, 4) != 0) {
        fprintf(stderr, "WBC setup failed: output pool create failed\n");
        return -1;
    }

    attr.device = NULL;
    attr.target = NULL;
    attr.width = WBC_CAPTURE_W;
    attr.height = WBC_CAPTURE_H;
    attr.stride = WBC_CAPTURE_STRIDE;
    attr.fps = FPS;
    attr.format = MEDIA_FORMAT_NV12;
    attr.pool_id = WBC_OUTPUT_POOL;
    attr.output_depth = 4;

    if (MEDIA_VO_WBC_CreateChn(LIVE_WBC_CHN, &attr) != 0) {
        MEDIA_POOL_Destroy(WBC_OUTPUT_POOL);
        return -1;
    }
    if (MEDIA_VO_WBC_Start(LIVE_WBC_CHN) != 0) {
        MEDIA_VO_WBC_DestroyChn(LIVE_WBC_CHN);
        MEDIA_POOL_Destroy(WBC_OUTPUT_POOL);
        return -1;
    }

    MEDIA_RESIZE_RGA_ATTR resize = {0};
    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = WBC_CAPTURE_W;
    resize.src_height = WBC_CAPTURE_H;
    resize.input_stride = WBC_CAPTURE_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 4;
    resize.out_width = WBC_PANEL_W;
    resize.out_height = WBC_PANEL_H;
    resize.out_stride = WBC_PANEL_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = DISPLAY_VMIX_INPUT_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_WBC_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_WBC_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_WBC_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(LIVE_WBC_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_WBC_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_WBC_RESIZE_GRP);
        MEDIA_VO_WBC_Stop(LIVE_WBC_CHN);
        MEDIA_VO_WBC_DestroyChn(LIVE_WBC_CHN);
        MEDIA_POOL_Destroy(WBC_OUTPUT_POOL);
        return -1;
    }

    MEDIA_RESIZE_RGA_ATTR vi_resize = {0};
    vi_resize.src_x = 0;
    vi_resize.src_y = 0;
    vi_resize.src_width = CAM_W;
    vi_resize.src_height = CAM_H;
    vi_resize.input_stride = CAM_STRIDE;
    vi_resize.input_format = MEDIA_FORMAT_NV12;
    vi_resize.input_depth = 4;
    vi_resize.out_width = WBC_VI_PANEL_W;
    vi_resize.out_height = WBC_VI_PANEL_H;
    vi_resize.out_stride = WBC_VI_PANEL_STRIDE;
    vi_resize.output_format = MEDIA_FORMAT_NV12;
    vi_resize.output_pool_id = DISPLAY_VMIX_INPUT_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_WBC_VI_RESIZE_GRP, &vi_resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_WBC_VI_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_WBC_VI_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(LIVE_WBC_VI_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_WBC_VI_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_WBC_VI_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Disable(LIVE_WBC_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_WBC_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_WBC_RESIZE_GRP);
        MEDIA_VO_WBC_Stop(LIVE_WBC_CHN);
        MEDIA_VO_WBC_DestroyChn(LIVE_WBC_CHN);
        MEDIA_POOL_Destroy(WBC_OUTPUT_POOL);
        return -1;
    }

    g_wbc_connector_id = connector_id;
    g_wbc_crtc_id = crtc_id;
    set_tile_status("WBC", TILE_LIVE);
    return 0;
}

static void cleanup_live_wbc(int enabled) {
    if (!enabled) return;
    MEDIA_RESIZE_RGA_Disable(LIVE_WBC_VI_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_WBC_VI_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_WBC_VI_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Disable(LIVE_WBC_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_WBC_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_WBC_RESIZE_GRP);
    MEDIA_VO_WBC_Stop(LIVE_WBC_CHN);
    MEDIA_VO_WBC_DestroyChn(LIVE_WBC_CHN);
    MEDIA_POOL_Destroy(WBC_OUTPUT_POOL);
    g_wbc_connector_id = -1;
    g_wbc_crtc_id = -1;
}

static int send_vmix_osd_to_vo(const uint8_t *src, const char *module_name,
                               int frame, int total_pages) {
    MEDIA_BUFFER vmix_out = {-1, -1};
    MEDIA_BUFFER osd_out = {-1, -1};
    char title[96];
    char perf[128];
    int ret = -1;

    if (!src) return -1;
    if ((frame % 15) == 0) update_perf_status();
    snprintf(title, sizeof(title), "%s  VMIX OSD", module_name ? module_name : "MODULE");
    if (g_perf.gpu_available) {
        snprintf(perf, sizeof(perf), "PAGE 01/%02d  CPU %.0f%%  GPU %.0f%%  LIVE",
                 total_pages, g_perf.cpu_percent, g_perf.gpu_percent);
    } else {
        snprintf(perf, sizeof(perf), "PAGE 01/%02d  CPU %.0f%%  GPU N/A  LIVE",
                 total_pages, g_perf.cpu_percent);
    }
    (void)update_display_osd_text(title, perf);

    if (send_copied_frame("VMIX", DISPLAY_VMIX_GRP, "input0",
                          DISPLAY_VMIX_INPUT_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_VMIX_GetFrame(DISPLAY_VMIX_GRP, &vmix_out, 20) != 0) return -1;
    if (MEDIA_OSD_SendFrame(DISPLAY_OSD_GRP, vmix_out, 20) != 0) {
        MEDIA_VMIX_ReleaseFrame(DISPLAY_VMIX_GRP, vmix_out);
        return -1;
    }
    MEDIA_VMIX_ReleaseFrame(DISPLAY_VMIX_GRP, vmix_out);
    if (MEDIA_OSD_GetFrame(DISPLAY_OSD_GRP, &osd_out, 20) == 0) {
        if (MEDIA_SYS_SendFrame("VO", 0, "input0", osd_out, 1000) == 0) {
            ret = 0;
        } else {
            MEDIA_OSD_ReleaseFrame(DISPLAY_OSD_GRP, osd_out);
        }
    }
    return ret;
}

static int bind_first_match(const char *src_mod, int src_id, const char **src_ports, int src_count,
                            const char *dst_mod, int dst_id, const char **dst_ports, int dst_count,
                            const char **bound_src, const char **bound_dst) {
    for (int i = 0; i < src_count; ++i) {
        for (int j = 0; j < dst_count; ++j) {
            if (MEDIA_SYS_Bind(src_mod, src_id, src_ports[i], dst_mod, dst_id, dst_ports[j]) == 0) {
                if (bound_src) *bound_src = src_ports[i];
                if (bound_dst) *bound_dst = dst_ports[j];
                return 0;
            }
        }
    }
    return -1;
}

static int bind_vi_wbc_vmix_osd_vo(void) {
    const char *wbc_out_ports[] = {"output", "output0"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *resize_in_ports[] = {"input0", "input"};
    const char *resize_out_ports[] = {"output0", "output"};
    const char *vi_vmix_in_ports[] = {"input0"};
    const char *wbc_vmix_in_ports[] = {"input1"};
    const char *vmix_out_ports[] = {"output0", "output"};
    const char *osd_in_ports[] = {"input", "input0"};
    const char *osd_out_ports[] = {"output0", "output"};
    const char *vo_in_ports[] = {"input0", "input"};

    g_bind_wbc_src_port = NULL;
    g_bind_wbc_resize_in_port = NULL;
    g_bind_wbc_resize_src_port = NULL;
    g_bind_wbc_vmix_in_port = NULL;
    g_bind_vi_src_port = NULL;
    g_bind_wbc_vi_resize_in_port = NULL;
    g_bind_wbc_vi_resize_src_port = NULL;
    g_bind_wbc_vi_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VO_WBC", LIVE_WBC_CHN,
                         wbc_out_ports, (int)ARRAY_SIZE(wbc_out_ports),
                         "RESIZE_RGA", LIVE_WBC_RESIZE_GRP,
                         resize_in_ports, (int)ARRAY_SIZE(resize_in_ports),
                         &g_bind_wbc_src_port, &g_bind_wbc_resize_in_port) != 0) {
        fprintf(stderr, "bind failed: VO_WBC -> RESIZE_RGA\n");
        return -1;
    }
    if (bind_first_match("VI", 0,
                         vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP,
                         resize_in_ports, (int)ARRAY_SIZE(resize_in_ports),
                         &g_bind_vi_src_port, &g_bind_wbc_vi_resize_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> RESIZE_RGA(WBC VI)\n");
        MEDIA_SYS_UnBind("VO_WBC", LIVE_WBC_CHN, g_bind_wbc_src_port,
                         "RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_in_port);
        return -1;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP,
                         resize_out_ports, (int)ARRAY_SIZE(resize_out_ports),
                         "VMIX", DISPLAY_VMIX_GRP,
                         vi_vmix_in_ports, (int)ARRAY_SIZE(vi_vmix_in_ports),
                         &g_bind_wbc_vi_resize_src_port, &g_bind_wbc_vi_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: RESIZE_RGA(WBC VI) -> VMIX input0\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_in_port);
        MEDIA_SYS_UnBind("VO_WBC", LIVE_WBC_CHN, g_bind_wbc_src_port,
                         "RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_in_port);
        return -1;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_WBC_RESIZE_GRP,
                         resize_out_ports, (int)ARRAY_SIZE(resize_out_ports),
                         "VMIX", DISPLAY_VMIX_GRP,
                         wbc_vmix_in_ports, (int)ARRAY_SIZE(wbc_vmix_in_ports),
                         &g_bind_wbc_resize_src_port, &g_bind_wbc_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: RESIZE_RGA(WBC) -> VMIX input1\n");
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_wbc_vi_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_in_port);
        MEDIA_SYS_UnBind("VO_WBC", LIVE_WBC_CHN, g_bind_wbc_src_port,
                         "RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_in_port);
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP,
                         vmix_out_ports, (int)ARRAY_SIZE(vmix_out_ports),
                         "OSD", DISPLAY_OSD_GRP,
                         osd_in_ports, (int)ARRAY_SIZE(osd_in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_wbc_vmix_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_wbc_vi_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_in_port);
        MEDIA_SYS_UnBind("VO_WBC", LIVE_WBC_CHN, g_bind_wbc_src_port,
                         "RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP,
                         osd_out_ports, (int)ARRAY_SIZE(osd_out_ports),
                         "VO", 0,
                         vo_in_ports, (int)ARRAY_SIZE(vo_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_wbc_vmix_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_wbc_vi_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_in_port);
        MEDIA_SYS_UnBind("VO_WBC", LIVE_WBC_CHN, g_bind_wbc_src_port,
                         "RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_in_port);
        return -1;
    }
    return 0;
}

static void unbind_vi_wbc_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_wbc_vi_resize_src_port && g_bind_wbc_vi_vmix_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_wbc_vi_vmix_in_port);
    }
    if (g_bind_wbc_resize_src_port && g_bind_wbc_vmix_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_wbc_vmix_in_port);
    }
    if (g_bind_vi_src_port && g_bind_wbc_vi_resize_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_WBC_VI_RESIZE_GRP, g_bind_wbc_vi_resize_in_port);
    }
    if (g_bind_wbc_src_port && g_bind_wbc_resize_in_port) {
        MEDIA_SYS_UnBind("VO_WBC", LIVE_WBC_CHN, g_bind_wbc_src_port,
                         "RESIZE_RGA", LIVE_WBC_RESIZE_GRP, g_bind_wbc_resize_in_port);
    }
}

static int h264_start_code_len(const uint8_t *data, size_t size, size_t pos) {
    if (!data || pos + 3 > size) return 0;
    if (data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) return 3;
    if (pos + 4 <= size && data[pos] == 0x00 && data[pos + 1] == 0x00 &&
        data[pos + 2] == 0x00 && data[pos + 3] == 0x01) return 4;
    return 0;
}

static void unload_h264_stream(h264_stream_t *stream) {
    if (!stream) return;
    free(stream->data);
    free(stream->nal_offsets);
    memset(stream, 0, sizeof(*stream));
}

static int load_h264_stream(const char *path, h264_stream_t *stream) {
    FILE *fp = NULL;
    long file_size = 0;
    int capacity = 256;
    if (!path || !stream) return -1;
    memset(stream, 0, sizeof(*stream));

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "NPU demo: open H264 failed: %s\n", path);
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (file_size = ftell(fp)) <= 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    stream->data = (uint8_t *)malloc((size_t)file_size);
    stream->nal_offsets = (int *)malloc((size_t)capacity * sizeof(int));
    if (!stream->data || !stream->nal_offsets) {
        fclose(fp);
        unload_h264_stream(stream);
        return -1;
    }
    if (fread(stream->data, 1, (size_t)file_size, fp) != (size_t)file_size) {
        fclose(fp);
        unload_h264_stream(stream);
        return -1;
    }
    fclose(fp);
    stream->size = (size_t)file_size;

    for (size_t i = 0; i + 3 < stream->size;) {
        int sc_len = h264_start_code_len(stream->data, stream->size, i);
        if (sc_len > 0) {
            if (stream->nal_count >= capacity) {
                capacity *= 2;
                int *tmp = (int *)realloc(stream->nal_offsets, (size_t)capacity * sizeof(int));
                if (!tmp) {
                    unload_h264_stream(stream);
                    return -1;
                }
                stream->nal_offsets = tmp;
            }
            stream->nal_offsets[stream->nal_count++] = (int)i;
            i += (size_t)sc_len;
            continue;
        }
        ++i;
    }
    if (stream->nal_count == 0) {
        stream->nal_offsets[stream->nal_count++] = 0;
    }
    stream->nal_index = 0;
    stream->pts = 0;
    return 0;
}

static int feed_h264_stream_packet(int vdec_chn, h264_stream_t *stream, int *is_vcl) {
    if (!stream || !stream->data || stream->size == 0 || stream->nal_count <= 0) return -1;
    if (is_vcl) *is_vcl = 0;
    if (stream->nal_index >= stream->nal_count) {
        stream->nal_index = 0;
    }

    int idx = stream->nal_index++;
    int start = stream->nal_offsets[idx];
    int end = (idx + 1 < stream->nal_count) ? stream->nal_offsets[idx + 1] : (int)stream->size;
    if (start < 0 || end <= start || (size_t)end > stream->size) return -1;

    int sc_len = h264_start_code_len(stream->data, stream->size, (size_t)start);
    int nal_start = start + sc_len;
    int vcl = 0;
    if (nal_start < end) {
        int nal_type = stream->data[nal_start] & 0x1f;
        vcl = (nal_type == 1 || nal_type == 5);
    }
    if (MEDIA_VDEC_SendPacket(vdec_chn, stream->data + start,
                              (size_t)(end - start), stream->pts) != 0) {
        return -1;
    }
    if (vcl) stream->pts += 33;
    if (is_vcl) *is_vcl = vcl;
    return 0;
}

static void copy_npu_detect_result(const MEDIA_NPU_RESULT *src, npu_demo_result_t *dst) {
    if (!dst) return;
    if (!src || src->type != MEDIA_NPU_RESULT_DETECT) {
        return;
    }
    memset(dst, 0, sizeof(*dst));
    dst->frame_id = src->frame_id;
    int count = src->u.detect.object_count;
    if (count > NPU_DEMO_MAX_DRAW_OBJECTS) count = NPU_DEMO_MAX_DRAW_OBJECTS;
    if (count < 0) count = 0;
    dst->object_count = count;
    for (int i = 0; i < count; ++i) {
        const MEDIA_NPU_OBJECT *obj = &src->u.detect.objects[i];
        npu_demo_object_t *out = &dst->objects[i];
        out->x = obj->x;
        out->y = obj->y;
        out->w = obj->w;
        out->h = obj->h;
        out->score = obj->score;
        out->class_id = obj->class_id;
        snprintf(out->label, sizeof(out->label), "%s",
                 obj->label ? obj->label : "object");
    }
}

static int setup_npu_vdec_chain(void) {
    int vdec_pool_ok = 0;
    int rgb_pool_ok = 0;
    int vdec_created = 0;
    int pre_rga_created = 0;
    int npu_created = 0;
    int post_rga_created = 0;
    int pre_rga_started = 0;
    int npu_started = 0;
    int post_rga_started = 0;

    if (MEDIA_POOL_Create(NPU_DEMO_VDEC_POOL, NPU_DEMO_VDEC_FRAME_SIZE, 6) != 0) goto fail;
    vdec_pool_ok = 1;
    if (MEDIA_POOL_Create(NPU_DEMO_RGB_POOL, RGB_FRAME_SIZE, 4) != 0) goto fail;
    rgb_pool_ok = 1;

    MEDIA_VDEC_ATTR vdec = {0};
    vdec.width = CAM_W;
    vdec.height = CAM_H;
    vdec.stride = CAM_STRIDE;
    vdec.buf_cnt = 4;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = NPU_DEMO_VDEC_POOL;
    vdec.has_input_port = 0;
    vdec.input_depth = 0;
    if (MEDIA_VDEC_CreateChn(NPU_DEMO_VDEC_CHN, &vdec) != 0) goto fail;
    vdec_created = 1;

    MEDIA_RGA_GRP_ATTR pre = {0};
    pre.algo = MEDIA_RGA_ALG_CSC;
    pre.input_count = 1;
    pre.output_count = 1;
    pre.input_depth = 4;
    pre.output_depth = 4;
    pre.inputs[0].width = CAM_W;
    pre.inputs[0].height = CAM_H;
    pre.inputs[0].stride = CAM_STRIDE;
    pre.inputs[0].format = MEDIA_FORMAT_NV12;
    pre.outputs[0].width = CAM_W;
    pre.outputs[0].height = CAM_H;
    pre.outputs[0].stride = CAM_W * 3;
    pre.outputs[0].format = MEDIA_FORMAT_RGB888;
    pre.outputs[0].pool_id = NPU_DEMO_RGB_POOL;
    if (MEDIA_RGA_CreateGrp(NPU_DEMO_PRE_RGA_GRP, &pre) != 0) goto fail;
    pre_rga_created = 1;

    MEDIA_NPU_ATTR npu = {0};
    npu.model_path = NPU_DEMO_MODEL_PATH;
    npu.label_path = path_readable(NPU_DEMO_LABEL_PATH) ? NPU_DEMO_LABEL_PATH : NULL;
    npu.adapter_name = "yolov5";
    npu.backend = MEDIA_NPU_BACKEND_RKNN;
    npu.task = MEDIA_NPU_TASK_DETECT;
    npu.input_width = CAM_W;
    npu.input_height = CAM_H;
    npu.input_format = MEDIA_FORMAT_RGB888;
    npu.input_layout = MEDIA_NPU_LAYOUT_NHWC;
    npu.input_depth = 4;
    npu.passthrough = 1;
    npu.score_thresh = 0.25f;
    npu.nms_thresh = 0.45f;
    if (MEDIA_NPU_CreateGrp(NPU_DEMO_NPU_GRP, &npu) != 0) goto fail;
    npu_created = 1;

    MEDIA_RGA_GRP_ATTR post = {0};
    post.algo = MEDIA_RGA_ALG_CSC;
    post.input_count = 1;
    post.output_count = 1;
    post.input_depth = 4;
    post.output_depth = 4;
    post.inputs[0].width = CAM_W;
    post.inputs[0].height = CAM_H;
    post.inputs[0].stride = CAM_W * 3;
    post.inputs[0].format = MEDIA_FORMAT_RGB888;
    post.outputs[0].width = CAM_W;
    post.outputs[0].height = CAM_H;
    post.outputs[0].stride = CAM_STRIDE;
    post.outputs[0].format = MEDIA_FORMAT_NV12;
    post.outputs[0].pool_id = DISPLAY_VMIX_INPUT_POOL;
    if (MEDIA_RGA_CreateGrp(NPU_DEMO_POST_RGA_GRP, &post) != 0) goto fail;
    post_rga_created = 1;

    if (MEDIA_RGA_Start(NPU_DEMO_PRE_RGA_GRP) != 0) goto fail;
    pre_rga_started = 1;
    if (MEDIA_NPU_Start(NPU_DEMO_NPU_GRP) != 0) goto fail;
    npu_started = 1;
    if (MEDIA_RGA_Start(NPU_DEMO_POST_RGA_GRP) != 0) goto fail;
    post_rga_started = 1;

    set_tile_status("RGA", TILE_LIVE);
    set_tile_status("NPU", TILE_LIVE);
    return 0;

fail:
    if (post_rga_started) MEDIA_RGA_Stop(NPU_DEMO_POST_RGA_GRP);
    if (npu_started) MEDIA_NPU_Stop(NPU_DEMO_NPU_GRP);
    if (pre_rga_started) MEDIA_RGA_Stop(NPU_DEMO_PRE_RGA_GRP);
    if (post_rga_created) MEDIA_RGA_DestroyChn(NPU_DEMO_POST_RGA_GRP);
    if (npu_created) MEDIA_NPU_DestroyGrp(NPU_DEMO_NPU_GRP);
    if (pre_rga_created) MEDIA_RGA_DestroyChn(NPU_DEMO_PRE_RGA_GRP);
    if (vdec_created) MEDIA_VDEC_DestroyChn(NPU_DEMO_VDEC_CHN);
    if (rgb_pool_ok) MEDIA_POOL_Destroy(NPU_DEMO_RGB_POOL);
    if (vdec_pool_ok) MEDIA_POOL_Destroy(NPU_DEMO_VDEC_POOL);
    return -1;
}

static void cleanup_npu_vdec_chain(int enabled) {
    if (!enabled) return;
    MEDIA_RGA_Stop(NPU_DEMO_POST_RGA_GRP);
    MEDIA_NPU_Stop(NPU_DEMO_NPU_GRP);
    MEDIA_RGA_Stop(NPU_DEMO_PRE_RGA_GRP);
    MEDIA_RGA_DestroyChn(NPU_DEMO_POST_RGA_GRP);
    MEDIA_NPU_DestroyGrp(NPU_DEMO_NPU_GRP);
    MEDIA_RGA_DestroyChn(NPU_DEMO_PRE_RGA_GRP);
    MEDIA_VDEC_DestroyChn(NPU_DEMO_VDEC_CHN);
    MEDIA_POOL_Destroy(NPU_DEMO_RGB_POOL);
    MEDIA_POOL_Destroy(NPU_DEMO_VDEC_POOL);
}

static int start_npu_vdec_decoder(void) {
    if (MEDIA_VDEC_Start(NPU_DEMO_VDEC_CHN) != 0) {
        return -1;
    }
    set_tile_status("VDEC", TILE_LIVE);
    return 0;
}

static int bind_npu_vdec_vmix_osd_vo(void) {
    const char *vdec_out_ports[] = {"output", "output0"};
    const char *rga_in_ports[] = {"input0", "input"};
    const char *rga_out_ports[] = {"output0", "output"};
    const char *npu_in_ports[] = {"input", "input0"};
    const char *npu_out_ports[] = {"output0", "output"};
    const char *vmix_in_ports[] = {"input0", "input"};
    const char *vmix_out_ports[] = {"output0", "output"};
    const char *osd_in_ports[] = {"input", "input0"};
    const char *osd_out_ports[] = {"output0", "output"};
    const char *vo_in_ports[] = {"input0", "input"};

    g_bind_vdec_src_port = NULL;
    g_bind_npu_pre_rga_in_port = NULL;
    g_bind_npu_pre_rga_src_port = NULL;
    g_bind_npu_in_port = NULL;
    g_bind_npu_src_port = NULL;
    g_bind_npu_post_rga_in_port = NULL;
    g_bind_npu_post_rga_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VDEC", NPU_DEMO_VDEC_CHN, vdec_out_ports, (int)ARRAY_SIZE(vdec_out_ports),
                         "RGA", NPU_DEMO_PRE_RGA_GRP, rga_in_ports, (int)ARRAY_SIZE(rga_in_ports),
                         &g_bind_vdec_src_port, &g_bind_npu_pre_rga_in_port) != 0) {
        fprintf(stderr, "bind failed: VDEC -> RGA(pre)\n");
        return -1;
    }
    if (bind_first_match("RGA", NPU_DEMO_PRE_RGA_GRP, rga_out_ports, (int)ARRAY_SIZE(rga_out_ports),
                         "NPU", NPU_DEMO_NPU_GRP, npu_in_ports, (int)ARRAY_SIZE(npu_in_ports),
                         &g_bind_npu_pre_rga_src_port, &g_bind_npu_in_port) != 0) {
        fprintf(stderr, "bind failed: RGA(pre) -> NPU\n");
        return -1;
    }
    if (bind_first_match("NPU", NPU_DEMO_NPU_GRP, npu_out_ports, (int)ARRAY_SIZE(npu_out_ports),
                         "RGA", NPU_DEMO_POST_RGA_GRP, rga_in_ports, (int)ARRAY_SIZE(rga_in_ports),
                         &g_bind_npu_src_port, &g_bind_npu_post_rga_in_port) != 0) {
        fprintf(stderr, "bind failed: NPU -> RGA(post)\n");
        return -1;
    }
    if (bind_first_match("RGA", NPU_DEMO_POST_RGA_GRP, rga_out_ports, (int)ARRAY_SIZE(rga_out_ports),
                         "VMIX", DISPLAY_VMIX_GRP, vmix_in_ports, (int)ARRAY_SIZE(vmix_in_ports),
                         &g_bind_npu_post_rga_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: RGA(post) -> VMIX\n");
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, vmix_out_ports, (int)ARRAY_SIZE(vmix_out_ports),
                         "OSD", DISPLAY_OSD_GRP, osd_in_ports, (int)ARRAY_SIZE(osd_in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, osd_out_ports, (int)ARRAY_SIZE(osd_out_ports),
                         "VO", 0, vo_in_ports, (int)ARRAY_SIZE(vo_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        return -1;
    }
    return 0;
}

static void unbind_npu_vdec_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_npu_post_rga_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("RGA", NPU_DEMO_POST_RGA_GRP, g_bind_npu_post_rga_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_npu_src_port && g_bind_npu_post_rga_in_port) {
        MEDIA_SYS_UnBind("NPU", NPU_DEMO_NPU_GRP, g_bind_npu_src_port,
                         "RGA", NPU_DEMO_POST_RGA_GRP, g_bind_npu_post_rga_in_port);
    }
    if (g_bind_npu_pre_rga_src_port && g_bind_npu_in_port) {
        MEDIA_SYS_UnBind("RGA", NPU_DEMO_PRE_RGA_GRP, g_bind_npu_pre_rga_src_port,
                         "NPU", NPU_DEMO_NPU_GRP, g_bind_npu_in_port);
    }
    if (g_bind_vdec_src_port && g_bind_npu_pre_rga_in_port) {
        MEDIA_SYS_UnBind("VDEC", NPU_DEMO_VDEC_CHN, g_bind_vdec_src_port,
                         "RGA", NPU_DEMO_PRE_RGA_GRP, g_bind_npu_pre_rga_in_port);
    }
}

static int bind_vi_vmix_osd_vo(void) {
    const char *out_ports[] = {"output0", "output"};
    const char *in_ports[] = {"input0", "input"};
    g_bind_vi_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VMIX", DISPLAY_VMIX_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> VMIX\n");
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_live_osd_vmix_osd_vo(void) {
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *in_ports[] = {"input", "input0"};
    const char *vmix_in_ports[] = {"input0", "input"};
    g_bind_vi_src_port = NULL;
    g_bind_live_osd_in_port = NULL;
    g_bind_live_osd_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "OSD", LIVE_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_live_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> live OSD\n");
        return -1;
    }
    if (bind_first_match("OSD", LIVE_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VMIX", DISPLAY_VMIX_GRP, vmix_in_ports, (int)ARRAY_SIZE(vmix_in_ports),
                         &g_bind_live_osd_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: live OSD -> VMIX\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "OSD", LIVE_OSD_GRP, g_bind_live_osd_in_port);
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> display OSD\n");
        MEDIA_SYS_UnBind("OSD", LIVE_OSD_GRP, g_bind_live_osd_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "OSD", LIVE_OSD_GRP, g_bind_live_osd_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vmix_in_ports, (int)ARRAY_SIZE(vmix_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: display OSD -> VO\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("OSD", LIVE_OSD_GRP, g_bind_live_osd_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "OSD", LIVE_OSD_GRP, g_bind_live_osd_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_resize_live_osd_vmix_osd_vo(void) {
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *in_ports[] = {"input", "input0"};
    const char *resize_in_ports[] = {"input0", "input"};
    const char *vmix_in_ports[] = {"input0", "input"};
    g_bind_vi_src_port = NULL;
    g_bind_resize_in_port = NULL;
    g_bind_resize_src_port = NULL;
    g_bind_live_osd_in_port = NULL;
    g_bind_live_osd_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "RESIZE_RGA", LIVE_RESIZE_GRP,
                         resize_in_ports, (int)ARRAY_SIZE(resize_in_ports),
                         &g_bind_vi_src_port, &g_bind_resize_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> pre-resize\n");
        return -1;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_RESIZE_GRP,
                         out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", LIVE_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_resize_src_port, &g_bind_live_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: pre-resize -> live OSD\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        return -1;
    }
    if (bind_first_match("OSD", LIVE_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VMIX", DISPLAY_VMIX_GRP,
                         vmix_in_ports, (int)ARRAY_SIZE(vmix_in_ports),
                         &g_bind_live_osd_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: live OSD -> VMIX\n");
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "OSD", LIVE_OSD_GRP, g_bind_live_osd_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> display OSD\n");
        MEDIA_SYS_UnBind("OSD", LIVE_OSD_GRP, g_bind_live_osd_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "OSD", LIVE_OSD_GRP, g_bind_live_osd_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vmix_in_ports, (int)ARRAY_SIZE(vmix_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: display OSD -> VO\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("OSD", LIVE_OSD_GRP, g_bind_live_osd_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "OSD", LIVE_OSD_GRP, g_bind_live_osd_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_clahe_vmix_osd_vo(void) {
    const char *mix = g_clahe_mix_module ? g_clahe_mix_module : "VMIX";
    const char *out_ports[] = {"output0", "output"};
    const char *clahe_out_ports[] = {"output", "output0"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *in_ports[] = {"input", "input0"};
    const char *vmix_clahe_ports[] = {"input0", "input"};
    const char *vo_in_ports[] = {"input0", "input"};
    g_bind_vi_src_port = NULL;
    g_bind_compare_raw_resize_in_port = NULL;
    g_bind_compare_raw_resize_src_port = NULL;
    g_bind_compare_effect_resize_in_port = NULL;
    g_bind_compare_effect_resize_src_port = NULL;
    g_bind_clahe_in_port = NULL;
    g_bind_clahe_src_port = NULL;
    g_bind_vmix_raw_in_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "CLAHE", LIVE_CLAHE_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_clahe_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> CLAHE\n");
        return -1;
    }
    if (bind_first_match("CLAHE", LIVE_CLAHE_GRP, clahe_out_ports, (int)ARRAY_SIZE(clahe_out_ports),
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_clahe_src_port, &g_bind_compare_effect_resize_in_port) != 0) {
        fprintf(stderr, "bind failed: CLAHE -> display resize\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_in_port);
        return -1;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         out_ports, (int)ARRAY_SIZE(out_ports),
                         mix, DISPLAY_VMIX_GRP, vmix_clahe_ports, (int)ARRAY_SIZE(vmix_clahe_ports),
                         &g_bind_compare_effect_resize_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: CLAHE display resize -> %s\n", mix);
        MEDIA_SYS_UnBind("CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_in_port);
        return -1;
    }
    if (bind_first_match(mix, DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: %s -> OSD\n", mix);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         mix, DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vo_in_ports, (int)ARRAY_SIZE(vo_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind(mix, DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         mix, DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_retinex_vmix_osd_vo(void) {
    const char *mix = g_retinex_mix_module ? g_retinex_mix_module : "VMIX";
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *retinex_in_ports[] = {"input0", "input"};
    const char *vmix_retinex_ports[] = {"input0", "input"};
    const char *vo_in_ports[] = {"input0", "input"};
    const char *osd_in_ports[] = {"input", "input0"};
    g_bind_vi_src_port = NULL;
    g_bind_vpss_in_port = NULL;
    g_bind_vpss_raw_src_port = NULL;
    g_bind_compare_raw_resize_in_port = NULL;
    g_bind_compare_raw_resize_src_port = NULL;
    g_bind_compare_effect_resize_in_port = NULL;
    g_bind_compare_effect_resize_src_port = NULL;
    g_bind_vmix_raw_in_port = NULL;
    g_bind_retinex_in_port = NULL;
    g_bind_retinex_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "RETINEX", LIVE_RETINEX_GRP, retinex_in_ports, (int)ARRAY_SIZE(retinex_in_ports),
                         &g_bind_vi_src_port, &g_bind_retinex_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> RETINEX\n");
        return -1;
    }
    if (bind_first_match("RETINEX", LIVE_RETINEX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         retinex_in_ports, (int)ARRAY_SIZE(retinex_in_ports),
                         &g_bind_retinex_src_port, &g_bind_compare_effect_resize_in_port) != 0) {
        fprintf(stderr, "bind failed: RETINEX -> display resize\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_in_port);
        return -1;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         out_ports, (int)ARRAY_SIZE(out_ports),
                         mix, DISPLAY_VMIX_GRP, vmix_retinex_ports, (int)ARRAY_SIZE(vmix_retinex_ports),
                         &g_bind_compare_effect_resize_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: RETINEX display resize -> %s\n", mix);
        MEDIA_SYS_UnBind("RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_in_port);
        return -1;
    }
    if (bind_first_match(mix, DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, osd_in_ports, (int)ARRAY_SIZE(osd_in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: %s -> OSD\n", mix);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         mix, DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vo_in_ports, (int)ARRAY_SIZE(vo_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind(mix, DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         mix, DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_dehaze_vmix_osd_vo(int use_dcp) {
    const char *dehaze_module = use_dcp ? "DCP_FAST_DEHAZE" : "CAP_DEHAZE";
    const int dehaze_grp = use_dcp ? LIVE_DCP_DEHAZE_GRP : LIVE_CAP_DEHAZE_GRP;
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *in_ports[] = {"input0", "input"};
    const char *osd_in_ports[] = {"input", "input0"};
    g_bind_vi_src_port = NULL;
    g_bind_csc_front_in_port = NULL;
    g_bind_csc_front_src_port = NULL;
    g_bind_dehaze_in_port = NULL;
    g_bind_dehaze_src_port = NULL;
    g_bind_compare_effect_resize_in_port = NULL;
    g_bind_compare_effect_resize_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "CSC_RGA", LIVE_CSC_RGA_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_csc_front_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> CSC_RGA(dehaze)\n");
        return -1;
    }
    if (bind_first_match("CSC_RGA", LIVE_CSC_RGA_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         dehaze_module, dehaze_grp, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_csc_front_src_port, &g_bind_dehaze_in_port) != 0) {
        fprintf(stderr, "bind failed: CSC_RGA -> %s\n", dehaze_module);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    if (bind_first_match(dehaze_module, dehaze_grp, out_ports, (int)ARRAY_SIZE(out_ports),
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_dehaze_src_port, &g_bind_compare_effect_resize_in_port) != 0) {
        fprintf(stderr, "bind failed: %s -> RESIZE_RGA\n", dehaze_module);
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         dehaze_module, dehaze_grp, g_bind_dehaze_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         out_ports, (int)ARRAY_SIZE(out_ports),
                         "VMIX", DISPLAY_VMIX_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_compare_effect_resize_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: RESIZE_RGA(dehaze) -> VMIX\n");
        MEDIA_SYS_UnBind(dehaze_module, dehaze_grp, g_bind_dehaze_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         dehaze_module, dehaze_grp, g_bind_dehaze_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, osd_in_ports, (int)ARRAY_SIZE(osd_in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD(dehaze)\n");
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind(dehaze_module, dehaze_grp, g_bind_dehaze_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         dehaze_module, dehaze_grp, g_bind_dehaze_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO(dehaze)\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind(dehaze_module, dehaze_grp, g_bind_dehaze_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         dehaze_module, dehaze_grp, g_bind_dehaze_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_rga_vmix_osd_vo(void) {
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *in_ports[] = {"input0", "input"};
    g_bind_vi_src_port = NULL;
    g_bind_rga_in_port = NULL;
    g_bind_rga_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "RGA", LIVE_RGA_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_rga_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> RGA\n");
        return -1;
    }
    if (bind_first_match("RGA", LIVE_RGA_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VMIX", DISPLAY_VMIX_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_rga_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: RGA -> VMIX\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RGA", LIVE_RGA_GRP, g_bind_rga_in_port);
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        MEDIA_SYS_UnBind("RGA", LIVE_RGA_GRP, g_bind_rga_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RGA", LIVE_RGA_GRP, g_bind_rga_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("RGA", LIVE_RGA_GRP, g_bind_rga_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RGA", LIVE_RGA_GRP, g_bind_rga_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_resize_vmix_osd_vo(void) {
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *in_ports[] = {"input0", "input"};
    g_bind_vi_src_port = NULL;
    g_bind_resize_in_port = NULL;
    g_bind_resize_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "RESIZE_RGA", LIVE_RESIZE_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_resize_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> RESIZE_RGA\n");
        return -1;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_RESIZE_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VMIX", DISPLAY_VMIX_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_resize_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: RESIZE_RGA -> VMIX\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_csc_chain_vmix_osd_vo(void) {
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *in_ports[] = {"input", "input0"};
    const char *vmix_in_ports[] = {"input0", "input"};
    g_bind_vi_src_port = NULL;
    g_bind_csc_front_in_port = NULL;
    g_bind_csc_front_src_port = NULL;
    g_bind_csc_back_in_port = NULL;
    g_bind_csc_back_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "CSC_RGA", LIVE_CSC_RGA_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_csc_front_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> CSC_RGA front\n");
        return -1;
    }
    if (bind_first_match("CSC_RGA", LIVE_CSC_RGA_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "CSC_RGA", LIVE_CSC_RGA_BACK_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_csc_front_src_port, &g_bind_csc_back_in_port) != 0) {
        fprintf(stderr, "bind failed: CSC_RGA front -> CSC_RGA back\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    if (bind_first_match("CSC_RGA", LIVE_CSC_RGA_BACK_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VMIX", DISPLAY_VMIX_GRP, vmix_in_ports, (int)ARRAY_SIZE(vmix_in_ports),
                         &g_bind_csc_back_src_port, &g_bind_vmix_in_port) != 0) {
        fprintf(stderr, "bind failed: CSC_RGA back -> VMIX\n");
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_BACK_GRP, g_bind_csc_back_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_BACK_GRP, g_bind_csc_back_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_BACK_GRP, g_bind_csc_back_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vmix_in_ports, (int)ARRAY_SIZE(vmix_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_BACK_GRP, g_bind_csc_back_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_BACK_GRP, g_bind_csc_back_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_csc_cl_chain_vmix_osd_vo(void) {
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *resize_in_ports[] = {"input0", "input"};
    const char *in_ports[] = {"input", "input0"};
    const char *vo_in_ports[] = {"input0", "input"};
    g_bind_vi_src_port = NULL;
    g_bind_resize_in_port = NULL;
    g_bind_resize_src_port = NULL;
    g_bind_csc_cl_front_in_port = NULL;
    g_bind_csc_cl_front_src_port = NULL;
    g_bind_vmix_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "CSC_CL", LIVE_CSC_CL_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_csc_cl_front_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> CSC_CL front\n");
        return -1;
    }
    if (bind_first_match("CSC_CL", LIVE_CSC_CL_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "RESIZE_RGA", LIVE_RESIZE_GRP,
                         resize_in_ports, (int)ARRAY_SIZE(resize_in_ports),
                         &g_bind_csc_cl_front_src_port, &g_bind_resize_in_port) != 0) {
        fprintf(stderr, "bind failed: CSC_CL -> RESIZE_RGA display scale\n");
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_CL", LIVE_CSC_CL_GRP, g_bind_csc_cl_front_in_port);
        return -1;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_RESIZE_GRP,
                         out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_resize_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: RESIZE_RGA display scale -> OSD\n");
        MEDIA_SYS_UnBind("CSC_CL", LIVE_CSC_CL_GRP, g_bind_csc_cl_front_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_CL", LIVE_CSC_CL_GRP, g_bind_csc_cl_front_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vo_in_ports, (int)ARRAY_SIZE(vo_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        MEDIA_SYS_UnBind("CSC_CL", LIVE_CSC_CL_GRP, g_bind_csc_cl_front_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_CL", LIVE_CSC_CL_GRP, g_bind_csc_cl_front_in_port);
        return -1;
    }
    return 0;
}

static int bind_vpss_vmix_osd_vo(void) {
    const char *out_ports[] = {"output0", "output"};
    const char *in_ports[] = {"input0", "input"};
    const char *vpss_in_ports[] = {"input0", "input"};
    const char *vpss_out_ports[VPSS_DEMO_OUTPUTS] = {"output0", "output1", "output2", "output3"};
    const char *vmix_in_ports[VPSS_DEMO_OUTPUTS] = {"input0", "input1", "input2", "input3"};

    g_bind_vi_src_port = NULL;
    g_bind_vpss_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        g_bind_vpss_src_ports[i] = NULL;
        g_bind_vmix_in_ports[i] = NULL;
    }

    if (bind_first_match("VI", 0, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VPSS", LIVE_VPSS_GRP, vpss_in_ports, (int)ARRAY_SIZE(vpss_in_ports),
                         &g_bind_vi_src_port, &g_bind_vpss_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> VPSS\n");
        return -1;
    }
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        if (MEDIA_SYS_Bind("VPSS", LIVE_VPSS_GRP, vpss_out_ports[i],
                           "VMIX", DISPLAY_VMIX_GRP, vmix_in_ports[i]) != 0) {
            fprintf(stderr, "bind failed: VPSS output%d -> VMIX input%d\n", i, i);
            for (int j = i - 1; j >= 0; --j) {
                MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP, g_bind_vpss_src_ports[j],
                                 "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_ports[j]);
            }
            MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                             "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
            return -1;
        }
        g_bind_vpss_src_ports[i] = vpss_out_ports[i];
        g_bind_vmix_in_ports[i] = vmix_in_ports[i];
    }
    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        for (int i = VPSS_DEMO_OUTPUTS - 1; i >= 0; --i) {
            MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP, g_bind_vpss_src_ports[i],
                             "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_ports[i]);
        }
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
        return -1;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
        for (int i = VPSS_DEMO_OUTPUTS - 1; i >= 0; --i) {
            MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP, g_bind_vpss_src_ports[i],
                             "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_ports[i]);
        }
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
        return -1;
    }
    return 0;
}

static int bind_vi_vpss_conv_cl_vmix_osd_vo(void) {
    /*
     * Keep each branch explicit instead of relying on auto-selected ports:
     * output0 maps to input0, output1 maps to input1, and so on.  That makes
     * the 2x2 VMIX layout stable and keeps each CONV_CL effect in a known
     * quadrant even with the extra CSC_RGA format-bridge stages.
     */
    const char *vi_out_ports[] = {"output", "output0"};
    const char *vpss_in_ports[] = {"input", "input0"};
    const char *vpss_out_ports[VPSS_DEMO_OUTPUTS] = {"output0", "output1", "output2", "output3"};
    const char *conv_in_ports[] = {"input", "input0"};
    const char *conv_out_ports[] = {"output0", "output"};
    const char *csc_in_ports[] = {"input", "input0"};
    const char *csc_out_ports[] = {"output0", "output"};
    const char *vmix_in_ports[VPSS_DEMO_OUTPUTS] = {"input0", "input1", "input2", "input3"};
    const char *out_ports[] = {"output0", "output"};
    const char *osd_in_ports[] = {"input", "input0"};
    const char *vo_in_ports[] = {"input0", "input"};
    const char *mix = g_conv_cl_mix_module ? g_conv_cl_mix_module : "VMIX";

    g_bind_vi_src_port = NULL;
    g_bind_vpss_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        g_bind_vpss_conv_src_ports[i] = NULL;
        g_bind_conv_pre_csc_in_ports[i] = NULL;
        g_bind_conv_pre_csc_src_ports[i] = NULL;
        g_bind_conv_cl_in_ports[i] = NULL;
        g_bind_conv_cl_src_ports[i] = NULL;
        g_bind_conv_csc_in_ports[i] = NULL;
        g_bind_conv_csc_src_ports[i] = NULL;
        g_bind_conv_vmix_in_ports[i] = NULL;
    }

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "VPSS", LIVE_VPSS_GRP, vpss_in_ports, (int)ARRAY_SIZE(vpss_in_ports),
                         &g_bind_vi_src_port, &g_bind_vpss_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> VPSS for CONV_CL quad\n");
        return -1;
    }

    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        const char *one_vpss_out[] = {vpss_out_ports[i]};
        if (bind_first_match("VPSS", LIVE_VPSS_GRP, one_vpss_out, 1,
                             "CSC_RGA", g_conv_cl_pre_csc_grps[i],
                             csc_in_ports, (int)ARRAY_SIZE(csc_in_ports),
                             &g_bind_vpss_conv_src_ports[i],
                             &g_bind_conv_pre_csc_in_ports[i]) != 0) {
            fprintf(stderr, "bind failed: VPSS output%d -> CSC_RGA%d\n",
                    i, g_conv_cl_pre_csc_grps[i]);
            goto fail;
        }
    }

    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        if (bind_first_match("CSC_RGA", g_conv_cl_pre_csc_grps[i],
                             csc_out_ports, (int)ARRAY_SIZE(csc_out_ports),
                             "CONV_CL", g_conv_cl_quad_grps[i],
                             conv_in_ports, (int)ARRAY_SIZE(conv_in_ports),
                             &g_bind_conv_pre_csc_src_ports[i],
                             &g_bind_conv_cl_in_ports[i]) != 0) {
            fprintf(stderr, "bind failed: CSC_RGA%d -> CONV_CL%d\n",
                    g_conv_cl_pre_csc_grps[i], g_conv_cl_quad_grps[i]);
            goto fail;
        }
    }

    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        if (bind_first_match("CONV_CL", g_conv_cl_quad_grps[i],
                             conv_out_ports, (int)ARRAY_SIZE(conv_out_ports),
                             "CSC_RGA", g_conv_cl_csc_grps[i],
                             csc_in_ports, (int)ARRAY_SIZE(csc_in_ports),
                             &g_bind_conv_cl_src_ports[i],
                             &g_bind_conv_csc_in_ports[i]) != 0) {
            fprintf(stderr, "bind failed: CONV_CL%d -> CSC_RGA%d\n",
                    g_conv_cl_quad_grps[i], g_conv_cl_csc_grps[i]);
            goto fail;
        }
    }

    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        const char *one_vmix_in[] = {vmix_in_ports[i]};
        if (bind_first_match("CSC_RGA", g_conv_cl_csc_grps[i],
                             csc_out_ports, (int)ARRAY_SIZE(csc_out_ports),
                             mix, DISPLAY_VMIX_GRP, one_vmix_in, 1,
                             &g_bind_conv_csc_src_ports[i],
                             &g_bind_conv_vmix_in_ports[i]) != 0) {
            fprintf(stderr, "bind failed: CSC_RGA%d -> %s input%d\n",
                    g_conv_cl_csc_grps[i], mix, i);
            goto fail;
        }
    }

    if (bind_first_match(mix, DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, osd_in_ports, (int)ARRAY_SIZE(osd_in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: %s -> OSD\n", mix);
        goto fail;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vo_in_ports, (int)ARRAY_SIZE(vo_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        goto fail;
    }
    return 0;

fail:
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind(mix, DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    for (int i = VPSS_DEMO_OUTPUTS - 1; i >= 0; --i) {
        if (g_bind_conv_csc_src_ports[i] && g_bind_conv_vmix_in_ports[i]) {
            MEDIA_SYS_UnBind("CSC_RGA", g_conv_cl_csc_grps[i],
                             g_bind_conv_csc_src_ports[i],
                             mix, DISPLAY_VMIX_GRP,
                             g_bind_conv_vmix_in_ports[i]);
        }
        if (g_bind_conv_cl_src_ports[i] && g_bind_conv_csc_in_ports[i]) {
            MEDIA_SYS_UnBind("CONV_CL", g_conv_cl_quad_grps[i],
                             g_bind_conv_cl_src_ports[i],
                             "CSC_RGA", g_conv_cl_csc_grps[i],
                             g_bind_conv_csc_in_ports[i]);
        }
        if (g_bind_conv_pre_csc_src_ports[i] && g_bind_conv_cl_in_ports[i]) {
            MEDIA_SYS_UnBind("CSC_RGA", g_conv_cl_pre_csc_grps[i],
                             g_bind_conv_pre_csc_src_ports[i],
                             "CONV_CL", g_conv_cl_quad_grps[i],
                             g_bind_conv_cl_in_ports[i]);
        }
        if (g_bind_vpss_conv_src_ports[i] && g_bind_conv_pre_csc_in_ports[i]) {
            MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP,
                             g_bind_vpss_conv_src_ports[i],
                             "CSC_RGA", g_conv_cl_pre_csc_grps[i],
                             g_bind_conv_pre_csc_in_ports[i]);
        }
    }
    if (g_bind_vi_src_port && g_bind_vpss_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
    }
    return -1;
}

static int bind_vi_vpss_transform_vmix_osd_vo(void) {
    const char *vi_out_ports[] = {"output", "output0"};
    const char *vpss_in_ports[] = {"input", "input0"};
    const char *vpss_out_ports[VPSS_DEMO_OUTPUTS] = {"output0", "output1", "output2", "output3"};
    const char *transform_in_ports[] = {"input", "input0"};
    const char *transform_out_ports[] = {"output"};
    const char *vmix_in_ports[VPSS_DEMO_OUTPUTS] = {"input0", "input1", "input2", "input3"};
    const char *out_ports[] = {"output0", "output"};
    const char *osd_in_ports[] = {"input", "input0"};
    const char *vo_in_ports[] = {"input0", "input"};

    g_bind_vi_src_port = NULL;
    g_bind_vpss_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        g_bind_vpss_transform_src_ports[i] = NULL;
        g_bind_transform_in_ports[i] = NULL;
        g_bind_transform_src_ports[i] = NULL;
        g_bind_transform_resize_in_ports[i] = NULL;
        g_bind_transform_resize_src_ports[i] = NULL;
        g_bind_transform_vmix_in_ports[i] = NULL;
    }

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "VPSS", LIVE_VPSS_GRP, vpss_in_ports, (int)ARRAY_SIZE(vpss_in_ports),
                         &g_bind_vi_src_port, &g_bind_vpss_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> VPSS for TRANSFORM quad\n");
        return -1;
    }

    const char *single_vpss_out[] = {vpss_out_ports[0]};
    const char *resize_out[] = {"output0", "output"};
    const char *single_vmix_in[] = {vmix_in_ports[0]};
    if (bind_first_match("VPSS", LIVE_VPSS_GRP, single_vpss_out, 1,
                         "TRANSFORM", g_transform_quad_grps[1],
                         transform_in_ports, (int)ARRAY_SIZE(transform_in_ports),
                         &g_bind_vpss_transform_src_ports[0],
                         &g_bind_transform_in_ports[0]) != 0) {
        fprintf(stderr, "bind failed: VPSS output0 -> TRANSFORM switcher\n");
        goto fail;
    }
    if (bind_first_match("TRANSFORM", g_transform_quad_grps[1],
                         transform_out_ports, (int)ARRAY_SIZE(transform_out_ports),
                         "RESIZE_RGA", g_transform_display_resize_grps[0],
                         transform_in_ports, (int)ARRAY_SIZE(transform_in_ports),
                         &g_bind_transform_src_ports[0],
                         &g_bind_transform_resize_in_ports[0]) != 0) {
        fprintf(stderr, "bind failed: TRANSFORM switcher -> display resize0\n");
        goto fail;
    }
    if (bind_first_match("RESIZE_RGA", g_transform_display_resize_grps[0],
                         resize_out, (int)ARRAY_SIZE(resize_out),
                         "VMIX", DISPLAY_VMIX_GRP, single_vmix_in, 1,
                         &g_bind_transform_resize_src_ports[0],
                         &g_bind_transform_vmix_in_ports[0]) != 0) {
        fprintf(stderr, "bind failed: TRANSFORM display resize -> VMIX input0\n");
        goto fail;
    }

    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP, osd_in_ports, (int)ARRAY_SIZE(osd_in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        goto fail;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vo_in_ports, (int)ARRAY_SIZE(vo_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        goto fail;
    }
    return 0;

fail:
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_transform_resize_src_ports[0] && g_bind_transform_vmix_in_ports[0]) {
        MEDIA_SYS_UnBind("RESIZE_RGA", g_transform_display_resize_grps[0],
                         g_bind_transform_resize_src_ports[0],
                         "VMIX", DISPLAY_VMIX_GRP,
                         g_bind_transform_vmix_in_ports[0]);
    }
    if (g_bind_transform_src_ports[0] && g_bind_transform_resize_in_ports[0]) {
        MEDIA_SYS_UnBind("TRANSFORM", g_transform_quad_grps[1],
                         g_bind_transform_src_ports[0],
                         "RESIZE_RGA", g_transform_display_resize_grps[0],
                         g_bind_transform_resize_in_ports[0]);
    }
    if (g_bind_vpss_transform_src_ports[0] && g_bind_transform_in_ports[0]) {
        MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP,
                         g_bind_vpss_transform_src_ports[0],
                         "TRANSFORM", g_transform_quad_grps[1],
                         g_bind_transform_in_ports[0]);
    }
    if (g_bind_vi_src_port && g_bind_vpss_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
    }
    return -1;
}

static int bind_vi_vpss_stereo_vmix_osd_vo(void) {
    const char *vi_out_ports[] = {"output", "output0"};
    const char *vpss_in_ports[] = {"input", "input0"};
    const char *vpss_out_ports[STEREO_INPUT_COUNT] = {"output0", "output1"};
    const char *stereo_in_ports[STEREO_INPUT_COUNT] = {"input0", "input1"};
    const char *stereo_out_ports[] = {"output0", "output"};
    const char *vmix_in_ports[] = {"input0", "input"};
    const char *out_ports[] = {"output0", "output"};
    const char *display_osd_in_ports[] = {"input", "input0"};
    const char *vo_in_ports[] = {"input0", "input"};

    g_bind_vi_src_port = NULL;
    g_bind_vpss_in_port = NULL;
    g_bind_vmix_src_port = NULL;
    g_bind_osd_in_port = NULL;
    g_bind_osd_src_port = NULL;
    g_bind_vo_in_port = NULL;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        g_bind_vpss_stereo_src_ports[i] = NULL;
        g_bind_stereo_in_ports[i] = NULL;
    }
    for (int i = 0; i < 2; ++i) {
        g_bind_stereo_src_ports[i] = NULL;
        g_bind_stereo_vmix_in_ports[i] = NULL;
    }

    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "VPSS", LIVE_VPSS_GRP, vpss_in_ports, (int)ARRAY_SIZE(vpss_in_ports),
                         &g_bind_vi_src_port, &g_bind_vpss_in_port) != 0) {
        fprintf(stderr, "bind failed: VI -> VPSS for STEREO_3D\n");
        return -1;
    }

    for (int i = 0; i < STEREO_INPUT_COUNT; ++i) {
        const char *one_vpss_out[] = {vpss_out_ports[i]};
        const char *one_stereo_in[] = {stereo_in_ports[i]};
        if (bind_first_match("VPSS", LIVE_VPSS_GRP, one_vpss_out, 1,
                             "STEREO_3D", LIVE_STEREO_GRP,
                             one_stereo_in, 1,
                             &g_bind_vpss_stereo_src_ports[i],
                             &g_bind_stereo_in_ports[i]) != 0) {
            fprintf(stderr, "bind failed: VPSS output%d -> STEREO_3D%d.%s\n",
                    i, LIVE_STEREO_GRP, stereo_in_ports[i]);
            goto fail;
        }
    }

    if (bind_first_match("STEREO_3D", LIVE_STEREO_GRP,
                         stereo_out_ports, (int)ARRAY_SIZE(stereo_out_ports),
                         "VMIX", DISPLAY_VMIX_GRP,
                         vmix_in_ports, (int)ARRAY_SIZE(vmix_in_ports),
                         &g_bind_stereo_src_ports[0],
                         &g_bind_stereo_vmix_in_ports[0]) != 0) {
        fprintf(stderr, "bind failed: STEREO_3D%d -> VMIX input0\n", LIVE_STEREO_GRP);
        goto fail;
    }

    if (bind_first_match("VMIX", DISPLAY_VMIX_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "OSD", DISPLAY_OSD_GRP,
                         display_osd_in_ports, (int)ARRAY_SIZE(display_osd_in_ports),
                         &g_bind_vmix_src_port, &g_bind_osd_in_port) != 0) {
        fprintf(stderr, "bind failed: VMIX -> OSD\n");
        goto fail;
    }
    if (bind_first_match("OSD", DISPLAY_OSD_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VO", 0, vo_in_ports, (int)ARRAY_SIZE(vo_in_ports),
                         &g_bind_osd_src_port, &g_bind_vo_in_port) != 0) {
        fprintf(stderr, "bind failed: OSD -> VO\n");
        goto fail;
    }
    return 0;

fail:
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_stereo_src_ports[0] && g_bind_stereo_vmix_in_ports[0]) {
        MEDIA_SYS_UnBind("STEREO_3D", LIVE_STEREO_GRP,
                         g_bind_stereo_src_ports[0],
                         "VMIX", DISPLAY_VMIX_GRP,
                         g_bind_stereo_vmix_in_ports[0]);
    }
    for (int i = STEREO_INPUT_COUNT - 1; i >= 0; --i) {
        if (g_bind_vpss_stereo_src_ports[i] && g_bind_stereo_in_ports[i]) {
            MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP,
                             g_bind_vpss_stereo_src_ports[i],
                             "STEREO_3D", LIVE_STEREO_GRP,
                             g_bind_stereo_in_ports[i]);
        }
    }
    if (g_bind_vi_src_port && g_bind_vpss_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
    }
    return -1;
}

static void unbind_vi_resize_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_resize_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_vi_src_port && g_bind_resize_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
    }
}

static void unbind_vi_csc_chain_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_csc_back_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_BACK_GRP, g_bind_csc_back_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_csc_front_src_port && g_bind_csc_back_in_port) {
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_BACK_GRP, g_bind_csc_back_in_port);
    }
    if (g_bind_vi_src_port && g_bind_csc_front_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
    }
}

static void unbind_vi_csc_cl_chain_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_resize_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_csc_cl_front_src_port && g_bind_resize_in_port) {
        MEDIA_SYS_UnBind("CSC_CL", LIVE_CSC_CL_GRP, g_bind_csc_cl_front_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
    }
    if (g_bind_vi_src_port && g_bind_csc_cl_front_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_CL", LIVE_CSC_CL_GRP, g_bind_csc_cl_front_in_port);
    }
}

static void unbind_vi_rga_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_rga_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("RGA", LIVE_RGA_GRP, g_bind_rga_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_vi_src_port && g_bind_rga_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RGA", LIVE_RGA_GRP, g_bind_rga_in_port);
    }
}

static void unbind_vi_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_vi_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
}

static void unbind_vi_live_osd_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_live_osd_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("OSD", LIVE_OSD_GRP, g_bind_live_osd_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_resize_src_port && g_bind_live_osd_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "OSD", LIVE_OSD_GRP, g_bind_live_osd_in_port);
    } else if (g_bind_vi_src_port && g_bind_live_osd_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "OSD", LIVE_OSD_GRP, g_bind_live_osd_in_port);
    }
    if (g_bind_vi_src_port && g_bind_resize_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
    }
}

static void unbind_vi_clahe_vmix_osd_vo(int enabled) {
    const char *mix = g_clahe_mix_module ? g_clahe_mix_module : "VMIX";
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind(mix, DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_compare_effect_resize_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         mix, DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_clahe_src_port && g_bind_compare_effect_resize_in_port) {
        MEDIA_SYS_UnBind("CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
    }
    if (g_bind_vi_src_port && g_bind_clahe_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CLAHE", LIVE_CLAHE_GRP, g_bind_clahe_in_port);
    }
}

static void unbind_vi_retinex_vmix_osd_vo(int enabled) {
    const char *mix = g_retinex_mix_module ? g_retinex_mix_module : "VMIX";
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind(mix, DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_compare_effect_resize_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         mix, DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_retinex_src_port && g_bind_compare_effect_resize_in_port) {
        MEDIA_SYS_UnBind("RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
    }
    if (g_bind_vi_src_port && g_bind_retinex_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RETINEX", LIVE_RETINEX_GRP, g_bind_retinex_in_port);
    }
}

static void unbind_vi_dehaze_vmix_osd_vo(int enabled, int use_dcp) {
    const char *dehaze_module = use_dcp ? "DCP_FAST_DEHAZE" : "CAP_DEHAZE";
    const int dehaze_grp = use_dcp ? LIVE_DCP_DEHAZE_GRP : LIVE_CAP_DEHAZE_GRP;
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_compare_effect_resize_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_dehaze_src_port && g_bind_compare_effect_resize_in_port) {
        MEDIA_SYS_UnBind(dehaze_module, dehaze_grp, g_bind_dehaze_src_port,
                         "RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP,
                         g_bind_compare_effect_resize_in_port);
    }
    if (g_bind_csc_front_src_port && g_bind_dehaze_in_port) {
        MEDIA_SYS_UnBind("CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_src_port,
                         dehaze_module, dehaze_grp, g_bind_dehaze_in_port);
    }
    if (g_bind_vi_src_port && g_bind_csc_front_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "CSC_RGA", LIVE_CSC_RGA_GRP, g_bind_csc_front_in_port);
    }
}

static void unbind_vpss_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    for (int i = VPSS_DEMO_OUTPUTS - 1; i >= 0; --i) {
        if (g_bind_vpss_src_ports[i] && g_bind_vmix_in_ports[i]) {
            MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP, g_bind_vpss_src_ports[i],
                             "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_ports[i]);
        }
    }
    if (g_bind_vi_src_port && g_bind_vpss_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
    }
}

static void unbind_vi_vpss_conv_cl_vmix_osd_vo(int enabled) {
    const char *mix = g_conv_cl_mix_module ? g_conv_cl_mix_module : "VMIX";
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind(mix, DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    for (int i = VPSS_DEMO_OUTPUTS - 1; i >= 0; --i) {
        if (g_bind_conv_csc_src_ports[i] && g_bind_conv_vmix_in_ports[i]) {
            MEDIA_SYS_UnBind("CSC_RGA", g_conv_cl_csc_grps[i],
                             g_bind_conv_csc_src_ports[i],
                             mix, DISPLAY_VMIX_GRP,
                             g_bind_conv_vmix_in_ports[i]);
        }
        if (g_bind_conv_cl_src_ports[i] && g_bind_conv_csc_in_ports[i]) {
            MEDIA_SYS_UnBind("CONV_CL", g_conv_cl_quad_grps[i],
                             g_bind_conv_cl_src_ports[i],
                             "CSC_RGA", g_conv_cl_csc_grps[i],
                             g_bind_conv_csc_in_ports[i]);
        }
        if (g_bind_conv_pre_csc_src_ports[i] && g_bind_conv_cl_in_ports[i]) {
            MEDIA_SYS_UnBind("CSC_RGA", g_conv_cl_pre_csc_grps[i],
                             g_bind_conv_pre_csc_src_ports[i],
                             "CONV_CL", g_conv_cl_quad_grps[i],
                             g_bind_conv_cl_in_ports[i]);
        }
        if (g_bind_vpss_conv_src_ports[i] && g_bind_conv_pre_csc_in_ports[i]) {
            MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP,
                             g_bind_vpss_conv_src_ports[i],
                             "CSC_RGA", g_conv_cl_pre_csc_grps[i],
                             g_bind_conv_pre_csc_in_ports[i]);
        }
    }
    if (g_bind_vi_src_port && g_bind_vpss_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
    }
}

static void unbind_vi_vpss_transform_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_transform_resize_src_ports[0] && g_bind_transform_vmix_in_ports[0]) {
        MEDIA_SYS_UnBind("RESIZE_RGA", g_transform_display_resize_grps[0],
                         g_bind_transform_resize_src_ports[0],
                         "VMIX", DISPLAY_VMIX_GRP,
                         g_bind_transform_vmix_in_ports[0]);
    }
    if (g_bind_transform_src_ports[0] && g_bind_transform_resize_in_ports[0]) {
        MEDIA_SYS_UnBind("TRANSFORM", g_transform_quad_grps[1],
                         g_bind_transform_src_ports[0],
                         "RESIZE_RGA", g_transform_display_resize_grps[0],
                         g_bind_transform_resize_in_ports[0]);
    }
    if (g_bind_vpss_transform_src_ports[0] && g_bind_transform_in_ports[0]) {
        MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP,
                         g_bind_vpss_transform_src_ports[0],
                         "TRANSFORM", g_transform_quad_grps[1],
                         g_bind_transform_in_ports[0]);
    }
    if (g_bind_vi_src_port && g_bind_vpss_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
    }
}

static void unbind_vi_vpss_stereo_vmix_osd_vo(int enabled) {
    if (!enabled) return;
    if (g_bind_osd_src_port && g_bind_vo_in_port) {
        MEDIA_SYS_UnBind("OSD", DISPLAY_OSD_GRP, g_bind_osd_src_port,
                         "VO", 0, g_bind_vo_in_port);
    }
    if (g_bind_vmix_src_port && g_bind_osd_in_port) {
        MEDIA_SYS_UnBind("VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_src_port,
                         "OSD", DISPLAY_OSD_GRP, g_bind_osd_in_port);
    }
    if (g_bind_stereo_src_ports[0] && g_bind_stereo_vmix_in_ports[0]) {
        MEDIA_SYS_UnBind("STEREO_3D", LIVE_STEREO_GRP,
                         g_bind_stereo_src_ports[0],
                         "VMIX", DISPLAY_VMIX_GRP,
                         g_bind_stereo_vmix_in_ports[0]);
    }
    for (int i = STEREO_INPUT_COUNT - 1; i >= 0; --i) {
        if (g_bind_vpss_stereo_src_ports[i] && g_bind_stereo_in_ports[i]) {
            MEDIA_SYS_UnBind("VPSS", LIVE_VPSS_GRP,
                             g_bind_vpss_stereo_src_ports[i],
                             "STEREO_3D", LIVE_STEREO_GRP,
                             g_bind_stereo_in_ports[i]);
        }
    }
    if (g_bind_vi_src_port && g_bind_vpss_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "VPSS", LIVE_VPSS_GRP, g_bind_vpss_in_port);
    }
}

static int run_only_npu_vdec_demo(int dstride, size_t display_size) {
    h264_stream_t stream;
    memset(&stream, 0, sizeof(stream));
    int vo_ok = 0;
    int display_ok = 0;
    int chain_ok = 0;
    int bind_ok = 0;
    int vdec_started = 0;
    int ret = 1;
    npu_demo_result_t last_det;
    memset(&last_det, 0, sizeof(last_det));

    if (!path_readable(NPU_DEMO_H264_PATH)) {
        fprintf(stderr, "NPU demo H264 missing: %s\n", NPU_DEMO_H264_PATH);
        return 1;
    }
    if (!path_readable(NPU_DEMO_MODEL_PATH)) {
        fprintf(stderr, "NPU demo model missing: %s\n", NPU_DEMO_MODEL_PATH);
        return 1;
    }
    if (load_h264_stream(NPU_DEMO_H264_PATH, &stream) != 0) {
        fprintf(stderr, "NPU demo: failed to load H264 stream\n");
        return 1;
    }

    MEDIA_VO_ATTR vo = {0};
    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = SCREEN_W;
    vo.height = SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, 0, 0, SCREEN_W, SCREEN_H, dstride, 4,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0 ||
        MEDIA_VO_Start(0, 0) != 0) {
        fprintf(stderr, "NPU demo VO setup failed\n");
        goto done;
    }
    vo_ok = 1;
    set_tile_status("VO", TILE_LIVE);

    if (setup_display_vmix_osd(dstride, display_size, 1, DISPLAY_VMIX_LAYOUT_DEFAULT) != 0) {
        fprintf(stderr, "NPU demo VMIX/OSD setup failed\n");
        goto done;
    }
    display_ok = 1;
    set_tile_status("VMIX", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);

    if (setup_npu_vdec_chain() != 0) {
        fprintf(stderr, "NPU demo VDEC/RGA/NPU setup failed\n");
        goto done;
    }
    chain_ok = 1;

    if (bind_npu_vdec_vmix_osd_vo() != 0) {
        fprintf(stderr, "NPU demo bind setup failed\n");
        goto done;
    }
    bind_ok = 1;

    if (start_npu_vdec_decoder() != 0) {
        fprintf(stderr, "NPU demo VDEC start failed\n");
        goto done;
    }
    vdec_started = 1;

    (void)update_display_osd_text("NPU  VDEC RGA YOLO VMIX OSD",
                                  "H264 -> VDEC -> RGB PREPROCESS -> NPU -> VMIX -> OSD");
    (void)update_npu_vdec_overlay(&last_det, 0, 0, 0, 0, 0, NPU_DEMO_H264_PATH, 0);

    printf("NPU VDEC flow: H264(%s) -> VDEC%d.%s -> RGA%d.%s -> NPU%d.%s -> RGA%d.%s -> VMIX%d.%s -> OSD%d.%s -> VO0.%s\n",
           NPU_DEMO_H264_PATH,
           NPU_DEMO_VDEC_CHN,
           g_bind_vdec_src_port ? g_bind_vdec_src_port : "output",
           NPU_DEMO_PRE_RGA_GRP,
           g_bind_npu_pre_rga_src_port ? g_bind_npu_pre_rga_src_port : "output0",
           NPU_DEMO_NPU_GRP,
           g_bind_npu_src_port ? g_bind_npu_src_port : "output0",
           NPU_DEMO_POST_RGA_GRP,
           g_bind_npu_post_rga_src_port ? g_bind_npu_post_rga_src_port : "output0",
           DISPLAY_VMIX_GRP,
           g_bind_vmix_src_port ? g_bind_vmix_src_port : "output0",
           DISPLAY_OSD_GRP,
           g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
           g_bind_vo_in_port ? g_bind_vo_in_port : "input0");

    int frame = 0;
    while (g_running) {
        int is_vcl = 0;
        if (feed_h264_stream_packet(NPU_DEMO_VDEC_CHN, &stream, &is_vcl) != 0) {
            fprintf(stderr, "NPU demo: MEDIA_VDEC_SendPacket failed\n");
            usleep(1000000 / FPS);
            continue;
        }

        for (int i = 0; i < 8; ++i) {
            MEDIA_NPU_RESULT result;
            memset(&result, 0, sizeof(result));
            if (MEDIA_NPU_GetResult(NPU_DEMO_NPU_GRP, &result, 0) != 0) {
                break;
            }
            copy_npu_detect_result(&result, &last_det);
            MEDIA_NPU_ReleaseResult(NPU_DEMO_NPU_GRP, &result);
        }

        if (is_vcl && (frame % 15) == 0) {
            uint64_t vdec_count = 0;
            uint64_t pre_rga_count = 0;
            uint64_t npu_count = 0;
            uint64_t post_rga_count = 0;
            uint64_t vmix_count = 0;
            char perf[180];
            char runtime[128];
            update_perf_status();
            (void)MEDIA_SYS_GetModuleFrameCount("VDEC", NPU_DEMO_VDEC_CHN, &vdec_count);
            (void)MEDIA_SYS_GetModuleFrameCount("RGA", NPU_DEMO_PRE_RGA_GRP, &pre_rga_count);
            (void)MEDIA_SYS_GetModuleFrameCount("NPU", NPU_DEMO_NPU_GRP, &npu_count);
            (void)MEDIA_SYS_GetModuleFrameCount("RGA", NPU_DEMO_POST_RGA_GRP, &post_rga_count);
            (void)MEDIA_SYS_GetModuleFrameCount("VMIX", DISPLAY_VMIX_GRP, &vmix_count);
            format_runtime_status(runtime, sizeof(runtime));
            snprintf(perf, sizeof(perf), "PAGE %02d/%02d DET %d VDEC %llu NPU %llu %s",
                     module_page_number("NPU"), module_page_total(),
                     last_det.object_count,
                     (unsigned long long)vdec_count,
                     (unsigned long long)npu_count,
                     runtime);
            (void)update_display_osd_text("NPU  VDEC RGA YOLO VMIX OSD", perf);
            (void)update_npu_vdec_overlay(&last_det, vdec_count, pre_rga_count,
                                          npu_count, post_rga_count, vmix_count,
                                          NPU_DEMO_H264_PATH, frame);
            if ((frame % FPS) == 0) {
                printf("NPU vdec=%llu pre_rga=%llu npu=%llu post_rga=%llu vmix=%llu det=%d %s\n",
                       (unsigned long long)vdec_count,
                       (unsigned long long)pre_rga_count,
                       (unsigned long long)npu_count,
                       (unsigned long long)post_rga_count,
                       (unsigned long long)vmix_count,
                       last_det.object_count,
                       runtime);
            }
        }

        if (is_vcl) {
            frame++;
            usleep(1000000 / FPS);
        } else {
            usleep(1000);
        }
    }
    ret = 0;

done:
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    alarm(5);
    if (vdec_started) {
        MEDIA_VDEC_Stop(NPU_DEMO_VDEC_CHN);
    }
    unbind_npu_vdec_vmix_osd_vo(bind_ok);
    cleanup_npu_vdec_chain(chain_ok);
    cleanup_display_vmix_osd(display_ok);
    if (vo_ok) {
        MEDIA_VO_Stop(0, 0);
        MEDIA_VO_DestroyChn(0, 0);
    }
    unload_h264_stream(&stream);
    return ret;
}

static int setup_live_osd(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_OSD_ATTR attr = {0};
    attr.input_width = CAM_W;
    attr.input_height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.max_regions = 3;
    if (MEDIA_OSD_CreateGrp(LIVE_OSD_GRP, &attr) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }

    MEDIA_OSD_REGION_ATTR box_attr = {0};
    box_attr.enabled = 1;
    box_attr.x = 36;
    box_attr.y = 36;
    box_attr.width = 240;
    box_attr.height = 140;
    box_attr.zorder = 0;
    box_attr.global_alpha = 255;
    MEDIA_OSD_RECT_DESC box = {0};
    box.filled = 0;
    box.line_width = 5;
    box.color.r = 0;
    box.color.g = 255;
    box.color.b = 190;
    box.color.a = 255;
    if (MEDIA_OSD_UpdateRegion(LIVE_OSD_GRP, 0, &box_attr) != 0 ||
        MEDIA_OSD_SetRegionRect(LIVE_OSD_GRP, 0, &box) != 0) {
        MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }

    MEDIA_OSD_REGION_ATTR hud_attr = {0};
    hud_attr.enabled = 1;
    hud_attr.x = 36;
    hud_attr.y = 520;
    hud_attr.width = 420;
    hud_attr.height = 54;
    hud_attr.zorder = 1;
    hud_attr.global_alpha = 180;
    MEDIA_OSD_RECT_DESC hud = {0};
    hud.filled = 1;
    hud.line_width = 1;
    hud.color.r = 8;
    hud.color.g = 18;
    hud.color.b = 30;
    hud.color.a = 255;
    if (MEDIA_OSD_UpdateRegion(LIVE_OSD_GRP, 1, &hud_attr) != 0 ||
        MEDIA_OSD_SetRegionRect(LIVE_OSD_GRP, 1, &hud) != 0) {
        MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }

    if (MEDIA_OSD_Start(LIVE_OSD_GRP) != 0) {
        MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("OSD", TILE_LIVE);
    return 0;
}

static void cleanup_live_osd(int enabled) {
    if (!enabled) return;
    MEDIA_OSD_Stop(LIVE_OSD_GRP);
    MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

typedef struct {
    int box_x;
    int box_y;
    int box_w;
    int box_h;
    int bar_w;
    int scan_x;
    int alpha;
    int alert_enabled;
} OSD_DEMO_PARAMS;

static OSD_DEMO_PARAMS osd_demo_params_for_frame(int frame) {
    OSD_DEMO_PARAMS p = {0};
    p.box_w = 420;
    p.box_h = 220;
    p.box_x = 64 + ((frame * 5) % (VI_BIG_VIEW_W - p.box_w - 128));
    p.box_y = 54 + ((frame * 3) % (VI_BIG_VIEW_H - p.box_h - 132));
    p.bar_w = 240 + ((frame * 7) % 520);
    p.scan_x = (frame * 13) % (VI_BIG_VIEW_W - 28);
    p.alpha = 120 + ((frame * 5) % 100);
    p.alert_enabled = ((frame / 18) % 2) == 0;
    return p;
}

static int set_live_osd_rect_region(int region_id, int x, int y, int w, int h,
                                    int zorder, uint8_t r, uint8_t g, uint8_t b,
                                    uint8_t alpha, int enabled) {
    MEDIA_OSD_REGION_ATTR attr = {0};
    MEDIA_OSD_RECT_DESC rect = {0};
    attr.enabled = enabled;
    attr.x = x;
    attr.y = y;
    attr.width = w;
    attr.height = h;
    attr.zorder = zorder;
    attr.global_alpha = alpha;
    rect.filled = 1;
    rect.line_width = 1;
    rect.color.r = r;
    rect.color.g = g;
    rect.color.b = b;
    rect.color.a = alpha;
    if (MEDIA_OSD_UpdateRegion(LIVE_OSD_GRP, region_id, &attr) != 0 ||
        MEDIA_OSD_SetRegionRect(LIVE_OSD_GRP, region_id, &rect) != 0) {
        return -1;
    }
    return 0;
}

static int update_live_osd_bind_regions(int frame) {
    OSD_DEMO_PARAMS p = osd_demo_params_for_frame(frame);
    int t = 7;

    if (set_live_osd_rect_region(0, p.box_x, p.box_y, p.box_w, t, 0, 0, 255, 190, 255, 1) != 0 ||
        set_live_osd_rect_region(1, p.box_x, p.box_y + p.box_h - t, p.box_w, t, 0, 0, 255, 190, 255, 1) != 0 ||
        set_live_osd_rect_region(2, p.box_x, p.box_y, t, p.box_h, 0, 0, 255, 190, 255, 1) != 0 ||
        set_live_osd_rect_region(3, p.box_x + p.box_w - t, p.box_y, t, p.box_h, 0, 0, 255, 190, 255, 1) != 0 ||
        set_live_osd_rect_region(4, 56, VI_BIG_VIEW_H - 78, p.bar_w, 34, 1, 255, 220, 70, (uint8_t)p.alpha, 1) != 0 ||
        set_live_osd_rect_region(5, p.scan_x, 42, 28, VI_BIG_VIEW_H - 84, 2, 255, 80, 80, 210, 1) != 0 ||
        set_live_osd_rect_region(6, VI_BIG_VIEW_W - 220, 48, 164, 54, 3, 255, 60, 90, 215, p.alert_enabled) != 0 ||
        set_live_osd_rect_region(7, 48, VI_BIG_VIEW_H - 94, 760, 64, 0, 8, 18, 30, 150, 1) != 0) {
        return -1;
    }
    return 0;
}

static int update_osd_bind_flow_overlay(uint64_t osd_count, uint64_t display_osd_count,
                                        OSD_DEMO_PARAMS p, int frame) {
    static uint8_t masks[30][1024 * 96];
    char region_line[160];
    char count_line[128];
    char bind_line1[180];
    char bind_line2[180];

    snprintf(region_line, sizeof(region_line), "动态叠加：目标框(%d,%d)  状态条宽度%d  透明度%d  告警%s",
             p.box_x, p.box_y, p.bar_w, p.alpha, p.alert_enabled ? "开启" : "关闭");
    snprintf(count_line, sizeof(count_line), "帧计数：业务OSD=%llu  页面OSD=%llu",
             (unsigned long long)osd_count, (unsigned long long)display_osd_count);
    snprintf(bind_line1, sizeof(bind_line1), "流程图：VI 3840x2160 -> RESIZE_RGA 1080x608 -> 动态OSD -> VMIX排版 -> 页面OSD -> VO");
    snprintf(bind_line2, sizeof(bind_line2), "分辨率：摄像头输入3840x2160；OSD主实时画面1080x608，stride=1088。");

    if (update_osd_rect_region(4, 34, 968, 1012, 666, 5, 10, 18, 226) != 0) return -1;
    if (update_osd_utf8_text_region(5, 66, 996, 36, 160, 255, 220,
                                    "OSD：视频叠加层和动态Region",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(6, 68, 1052, 25, 190, 230, 255,
                                    "数据流：VI原始视频进入OSD，叠加框、状态条和告警后再上屏。",
                                    masks[1], sizeof(masks[1]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(7, 68, 1096, 24, 255, 230, 120,
                                    "为什么这样做：OSD只更新叠加区域，不需要CPU逐像素重画整帧视频。",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;

    const int card_y = 1172;
    const int card_w = 168;
    const int card_h = 156;
    const int xs[5] = {52, 248, 444, 640, 836};
    const char *titles[5] = {"VI", "OSD", "VMIX", "页面OSD", "VO"};
    const char *values[5] = {"原始帧", "叠加层", "排版", "说明层", "显示"};
    for (int i = 0; i < 5; ++i) {
        int highlight = (i == 1 || i == 3);
        if (update_osd_rect_region(8 + i, xs[i], card_y, card_w, card_h,
                                   highlight ? 16 : 12, highlight ? 46 : 28, highlight ? 72 : 46, 240) != 0) return -1;
        if (update_osd_utf8_text_region(13 + i, xs[i] + 18, card_y + 16, 23, 160, 255, 220,
                                        titles[i], masks[3 + i], sizeof(masks[3 + i]), 1024, 96) != 0) return -1;
        if (update_osd_utf8_text_region(18 + i, xs[i] + 18, card_y + 62, 22, 255, 230, 120,
                                        values[i], masks[8 + i], sizeof(masks[8 + i]), 1024, 96) != 0) return -1;
    }

    if (update_osd_utf8_text_region(23, 214, card_y + 54, 34, 255, 230, 120,
                                    "→", masks[13], sizeof(masks[13]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(24, 410, card_y + 54, 34, 255, 230, 120,
                                    "→", masks[14], sizeof(masks[14]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(25, 606, card_y + 54, 34, 255, 230, 120,
                                    "→", masks[15], sizeof(masks[15]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(26, 802, card_y + 54, 34, 255, 230, 120,
                                    "→", masks[16], sizeof(masks[16]), 1024, 96) != 0) return -1;

    int packet_x = 58 + ((frame * 18) % 890);
    if (update_osd_rect_region(27, packet_x, 1350, 58, 14, 255, 255, 255, 190) != 0) return -1;

    if (update_osd_rect_region(28, 74, 1382, 240, 34, 0, 255, 190, 220) != 0) return -1;
    if (update_osd_rect_region(29, 74, 1436, p.alpha * 2, 34, 255, 220, 70, (uint8_t)p.alpha) != 0) return -1;
    if (update_osd_rect_region(30, 74, 1490, 180, 34, 255, 80, 80, p.alert_enabled ? 220 : 70) != 0) return -1;

    if (update_osd_utf8_text_region(31, 342, 1372, 22, 160, 255, 220,
                                    "目标框：坐标动态更新",
                                    masks[17], sizeof(masks[17]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(32, 342, 1426, 22, 255, 230, 120,
                                    "状态条：透明度和长度变化",
                                    masks[18], sizeof(masks[18]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(33, 342, 1480, 22, 255, 150, 160,
                                    "告警块：开关状态变化",
                                    masks[19], sizeof(masks[19]), 1024, 96) != 0) return -1;

    if (update_osd_utf8_text_region(34, 72, 1530, 19, 170, 255, 220,
                                    region_line, masks[20], sizeof(masks[20]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(35, 72, 1562, 19, 170, 255, 220,
                                    count_line, masks[21], sizeof(masks[21]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(36, 72, 1594, 19, 170, 205, 235,
                                    bind_line1, masks[22], sizeof(masks[22]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(37, 72, 1624, 19, 170, 205, 235,
                                    bind_line2, masks[23], sizeof(masks[23]), 1024, 96) != 0) return -1;

    return 0;
}

static int setup_live_osd_bind(void) {
    MEDIA_OSD_ATTR attr = {0};
    attr.input_width = VI_BIG_VIEW_W;
    attr.input_height = VI_BIG_VIEW_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.output_pool_id = DISPLAY_VMIX_INPUT_POOL;
    attr.input_stride = VI_BIG_VIEW_STRIDE;
    attr.output_stride = VI_BIG_VIEW_STRIDE;
    attr.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(LIVE_OSD_GRP, &attr) != 0) {
        return -1;
    }
    if (update_live_osd_bind_regions(0) != 0 ||
        MEDIA_OSD_Start(LIVE_OSD_GRP) != 0) {
        MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
        return -1;
    }
    set_tile_status("OSD", TILE_LIVE);
    return 0;
}

static void cleanup_live_osd_bind(int enabled) {
    if (!enabled) return;
    MEDIA_OSD_Stop(LIVE_OSD_GRP);
    MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
}

static int process_live_osd(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, CAM_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_OSD_SendFrame(LIVE_OSD_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_OSD_GetFrame(LIVE_OSD_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_OSD_ReleaseFrame(LIVE_OSD_GRP, out);
    }
    return ret;
}

static void fill_resize_attr(MEDIA_RESIZE_RGA_ATTR *attr, resize_demo_params_t p, int output_pool, int input_depth) {
    memset(attr, 0, sizeof(*attr));
    int src_x = ((p.crop_x * g_camera_input_w) / CAM_W) & ~1;
    int src_y = ((p.crop_y * g_camera_input_h) / CAM_H) & ~1;
    int src_w = ((p.crop_w * g_camera_input_w) / CAM_W) & ~1;
    int src_h = ((p.crop_h * g_camera_input_h) / CAM_H) & ~1;
    if (src_w < 2) src_w = 2;
    if (src_h < 2) src_h = 2;
    if (src_x + src_w > g_camera_input_w) src_w = (g_camera_input_w - src_x) & ~1;
    if (src_y + src_h > g_camera_input_h) src_h = (g_camera_input_h - src_y) & ~1;
    attr->src_x = src_x;
    attr->src_y = src_y;
    attr->src_width = src_w;
    attr->src_height = src_h;
    attr->input_width = g_camera_input_w;
    attr->input_height = g_camera_input_h;
    attr->input_stride = g_camera_input_stride;
    attr->input_format = MEDIA_FORMAT_NV12;
    attr->input_depth = input_depth;
    attr->out_width = CAM_W;
    attr->out_height = CAM_H;
    attr->out_stride = CAM_STRIDE;
    attr->output_format = MEDIA_FORMAT_NV12;
    attr->output_pool_id = output_pool;
}

static int setup_live_resize(void) {
    if (MEDIA_POOL_Create(RESIZE_INPUT_POOL, live_camera_input_frame_size(), 3) != 0) return -1;
    if (MEDIA_POOL_Create(RESIZE_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(RESIZE_INPUT_POOL);
        return -1;
    }

    MEDIA_RESIZE_RGA_ATTR attr = {0};
    fill_resize_attr(&attr, resize_demo_params_for_frame(0), RESIZE_OUTPUT_POOL, 3);

    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_RESIZE_GRP, &attr) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
        MEDIA_POOL_Destroy(RESIZE_INPUT_POOL);
        MEDIA_POOL_Destroy(RESIZE_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;
}

static int setup_live_resize_bind_with_params(resize_demo_params_t p) {
    MEDIA_RESIZE_RGA_ATTR attr = {0};
    fill_resize_attr(&attr, p, DISPLAY_VMIX_INPUT_POOL, 4);
    attr.out_width = VI_BIG_VIEW_W;
    attr.out_height = VI_BIG_VIEW_H;
    attr.out_stride = VI_BIG_VIEW_STRIDE;

    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_RESIZE_GRP, &attr) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
        return -1;
    }
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;
}

static int setup_live_camera_to_cam_resize_bind(void) {
    if (MEDIA_POOL_Create(WORK_POOL_OUT, VI_BIG_VIEW_FRAME_SIZE, 4) != 0) return -1;

    MEDIA_RESIZE_RGA_ATTR attr = {0};
    attr.src_x = 0;
    attr.src_y = 0;
    attr.src_width = g_camera_input_w;
    attr.src_height = g_camera_input_h;
    attr.input_width = g_camera_input_w;
    attr.input_height = g_camera_input_h;
    attr.input_stride = g_camera_input_stride;
    attr.input_format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.out_width = VI_BIG_VIEW_W;
    attr.out_height = VI_BIG_VIEW_H;
    attr.out_stride = VI_BIG_VIEW_STRIDE;
    attr.output_format = MEDIA_FORMAT_NV12;
    attr.output_pool_id = WORK_POOL_OUT;

    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_RESIZE_GRP, &attr) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
        MEDIA_POOL_Destroy(WORK_POOL_OUT);
        return -1;
    }
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;
}

static void cleanup_live_camera_to_cam_resize_bind(int enabled) {
    if (!enabled) return;
    MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
    MEDIA_POOL_Destroy(WORK_POOL_OUT);
}

static int setup_live_resize_bind(void) {
    return setup_live_resize_bind_with_params(resize_demo_params_for_frame(0));
}

static int setup_live_vi_big_resize_bind(void) {
    MEDIA_RESIZE_RGA_ATTR attr = {0};
    attr.src_x = 0;
    attr.src_y = 0;
    attr.src_width = VI_BIG_W;
    attr.src_height = VI_BIG_H;
    attr.input_width = VI_BIG_W;
    attr.input_height = VI_BIG_H;
    attr.input_stride = VI_BIG_STRIDE;
    attr.input_format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.out_width = VI_BIG_VIEW_W;
    attr.out_height = VI_BIG_VIEW_H;
    attr.out_stride = VI_BIG_VIEW_STRIDE;
    attr.output_format = MEDIA_FORMAT_NV12;
    attr.output_pool_id = DISPLAY_VMIX_INPUT_POOL;

    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_RESIZE_GRP, &attr) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
        return -1;
    }
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;
}

static void cleanup_live_resize_bind(int enabled) {
    if (!enabled) return;
    MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
}

static int reconfigure_resize_bind(resize_demo_params_t p) {
    const char *out_ports[] = {"output0", "output"};
    const char *vi_out_ports[] = {"output", "output0"};
    const char *in_ports[] = {"input0", "input"};
    const char *saved_vmix_in = g_bind_vmix_in_port;

    if (g_bind_resize_src_port && g_bind_vmix_in_port) {
        MEDIA_SYS_UnBind("RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_src_port,
                         "VMIX", DISPLAY_VMIX_GRP, g_bind_vmix_in_port);
    }
    if (g_bind_vi_src_port && g_bind_resize_in_port) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
    }
    cleanup_live_resize_bind(1);

    g_bind_resize_in_port = NULL;
    g_bind_resize_src_port = NULL;
    if (setup_live_resize_bind_with_params(p) != 0) return -1;
    if (bind_first_match("VI", 0, vi_out_ports, (int)ARRAY_SIZE(vi_out_ports),
                         "RESIZE_RGA", LIVE_RESIZE_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_vi_src_port, &g_bind_resize_in_port) != 0) {
        return -1;
    }
    if (saved_vmix_in &&
        MEDIA_SYS_Bind("RESIZE_RGA", LIVE_RESIZE_GRP, out_ports[0],
                       "VMIX", DISPLAY_VMIX_GRP, saved_vmix_in) == 0) {
        g_bind_resize_src_port = out_ports[0];
        g_bind_vmix_in_port = saved_vmix_in;
        return 0;
    }
    if (bind_first_match("RESIZE_RGA", LIVE_RESIZE_GRP, out_ports, (int)ARRAY_SIZE(out_ports),
                         "VMIX", DISPLAY_VMIX_GRP, in_ports, (int)ARRAY_SIZE(in_ports),
                         &g_bind_resize_src_port, &g_bind_vmix_in_port) != 0) {
        MEDIA_SYS_UnBind("VI", 0, g_bind_vi_src_port,
                         "RESIZE_RGA", LIVE_RESIZE_GRP, g_bind_resize_in_port);
        return -1;
    }
    return 0;
}

static void cleanup_live_resize(int enabled) {
    if (!enabled) return;
    MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
    MEDIA_POOL_Destroy(RESIZE_INPUT_POOL);
    MEDIA_POOL_Destroy(RESIZE_OUTPUT_POOL);
}

static int process_live_resize(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("RESIZE_RGA", LIVE_RESIZE_GRP, "input0",
                          RESIZE_INPUT_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_RESIZE_RGA_GetFrame(LIVE_RESIZE_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_RESIZE_RGA_ReleaseFrame(LIVE_RESIZE_GRP, out);
    }
    return ret;
}

static int pingpong_i(int t, int max_value) {
    int period;
    int v;
    if (max_value <= 0) return 0;
    period = max_value * 2;
    v = t % period;
    return v <= max_value ? v : period - v;
}

static size_t live_camera_input_frame_size(void) {
    return (size_t)g_camera_input_stride * g_camera_input_h * 3 / 2;
}

static void scale_nv12_to_cam_frame(const uint8_t *src, int src_w, int src_h, int src_stride,
                                    uint8_t *dst) {
    uint8_t *dst_uv = dst + CAM_STRIDE * CAM_H;
    const uint8_t *src_uv = src + (size_t)src_stride * src_h;

    for (int y = 0; y < CAM_H; ++y) {
        int sy = ((int64_t)y * src_h) / CAM_H;
        const uint8_t *srow = src + (size_t)sy * src_stride;
        uint8_t *drow = dst + (size_t)y * CAM_STRIDE;
        for (int x = 0; x < CAM_W; ++x) {
            int sx = ((int64_t)x * src_w) / CAM_W;
            drow[x] = srow[sx];
        }
    }

    for (int y = 0; y < CAM_H / 2; ++y) {
        int sy = ((int64_t)y * (src_h / 2)) / (CAM_H / 2);
        const uint8_t *srow = src_uv + (size_t)sy * src_stride;
        uint8_t *drow = dst_uv + (size_t)y * CAM_STRIDE;
        for (int x = 0; x < CAM_W; x += 2) {
            int sx = ((int64_t)x * src_w) / CAM_W;
            sx &= ~1;
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void fill_vpss_demo_output_attr(MEDIA_VPSS_OUT_ATTR *out, int output_id,
                                       int crop_x, int crop_y, int crop_w, int crop_h,
                                       int flip_h, int flip_v) {
    memset(out, 0, sizeof(*out));
    out->output_id = output_id;
    out->out_width = VPSS_TILE_W;
    out->out_height = VPSS_TILE_H;
    out->out_stride = CAM_STRIDE;
    out->pool_id = VPSS_OUTPUT_POOL;
    out->crop_x = 0;
    out->crop_y = 0;
    out->crop_w = CAM_W;
    out->crop_h = CAM_H;
    out->in_fps = -1;
    out->out_fps = -1;
    out->output_format = MEDIA_FORMAT_NV12;
    out->crop_x = crop_x;
    out->crop_y = crop_y;
    out->crop_w = crop_w;
    out->crop_h = crop_h;
    out->flip_h = flip_h;
    out->flip_v = flip_v;
}

static void fill_vpss_initial_output_attr(MEDIA_VPSS_OUT_ATTR *out, int output_id) {
    fill_vpss_demo_output_attr(out, output_id, 0, 0,
                               g_camera_input_w, g_camera_input_h, 0, 0);
    if (output_id == 1) {
        fill_vpss_demo_output_attr(out, output_id,
                                   g_camera_input_w / 4, g_camera_input_h / 4,
                                   g_camera_input_w / 2, g_camera_input_h / 2,
                                   0, 0);
    } else if (output_id == 2) {
        fill_vpss_demo_output_attr(out, output_id, 0, 0,
                                   g_camera_input_w, g_camera_input_h, 1, 0);
    } else if (output_id == 3) {
        fill_vpss_demo_output_attr(out, output_id,
                                   g_camera_input_w / 8, g_camera_input_h / 8,
                                   g_camera_input_w * 3 / 4, g_camera_input_h * 3 / 4,
                                   0, 0);
    }
}

static int update_vpss_dynamic_attrs(int frame) {
    MEDIA_VPSS_OUT_ATTR out = {0};
    int ret = 0;

    int max_square = g_camera_input_w < g_camera_input_h ? g_camera_input_w : g_camera_input_h;
    int crop_size = max_square / 2;
    int crop_range = g_camera_input_w - crop_size;
    int crop_x = pingpong_i(frame * 4, crop_range) & ~1;
    int crop_y = pingpong_i(frame * 3, g_camera_input_h - crop_size) & ~1;
    fill_vpss_demo_output_attr(&out, 1, crop_x, crop_y, crop_size, crop_size, 0, 0);
    if (MEDIA_VPSS_SetOutAttr(LIVE_VPSS_GRP, &out) != 0) ret = -1;

    int flip_h = ((frame / (FPS * 2)) & 1) != 0;
    int flip_v = ((frame / (FPS * 4)) & 1) != 0;
    fill_vpss_demo_output_attr(&out, 2, 0, 0,
                               g_camera_input_w, g_camera_input_h, flip_h, flip_v);
    if (MEDIA_VPSS_SetOutAttr(LIVE_VPSS_GRP, &out) != 0) ret = -1;

    crop_size = max_square - (pingpong_i(frame * 2, max_square / 2) & ~1);
    if (crop_size < max_square / 2) crop_size = max_square / 2;
    crop_x = ((g_camera_input_w - crop_size) / 2) & ~1;
    crop_y = ((g_camera_input_h - crop_size) / 2) & ~1;
    fill_vpss_demo_output_attr(&out, 3, crop_x, crop_y, crop_size, crop_size, 0, 0);
    if (MEDIA_VPSS_SetOutAttr(LIVE_VPSS_GRP, &out) != 0) ret = -1;

    return ret;
}

static int setup_live_vpss(void) {
    if (MEDIA_POOL_Create(VPSS_INPUT_POOL, live_camera_input_frame_size(), 3) != 0) return -1;
    if (MEDIA_POOL_Create(VPSS_OUTPUT_POOL, VPSS_TILE_NV12_SIZE, VPSS_QUAD_OUTPUT_POOL_COUNT) != 0) {
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        return -1;
    }

    MEDIA_VPSS_ATTR attr = {0};
    attr.width = g_camera_input_w;
    attr.height = g_camera_input_h;
    attr.input_stride = g_camera_input_stride;
    attr.input_depth = 3;
    attr.input_format = MEDIA_FORMAT_NV12;
    attr.output_count = VPSS_DEMO_OUTPUTS;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        fill_vpss_initial_output_attr(&attr.outputs[i], i);
    }

    if (MEDIA_VPSS_SetAttr(LIVE_VPSS_GRP, &attr) != 0 ||
        MEDIA_VPSS_Enable(LIVE_VPSS_GRP) != 0) {
        MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("VPSS", TILE_LIVE);
    return 0;
}

static void cleanup_live_vpss(int enabled) {
    if (!enabled) return;
    MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
    MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
    MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
    MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        free(g_vpss_preview_frames[i]);
        g_vpss_preview_frames[i] = NULL;
        g_vpss_preview_ready[i] = 0;
    }
}

static void compose_vpss_showcase(uint8_t *dst, uint8_t **frames, const int *ready) {
    fill_rect_nv12_frame(dst, CAM_W, CAM_H, CAM_STRIDE, 0, 0, CAM_W, CAM_H, 5, 10, 18);
    int gap = 10;
    int cell_w = (CAM_W - gap * 3) / 2;
    int cell_h = (CAM_H - gap * 3) / 2;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        int x = gap + (i % 2) * (cell_w + gap);
        int y = gap + (i / 2) * (cell_h + gap);
        fill_rect_nv12_frame(dst, CAM_W, CAM_H, CAM_STRIDE, x, y, cell_w, cell_h, 8, 16, 28);
        if (ready[i] && frames[i]) {
            draw_nv12_to_frame(dst, CAM_W, CAM_H, CAM_STRIDE, x + 4, y + 4, cell_w - 8, cell_h - 8,
                               frames[i], VPSS_TILE_W, VPSS_TILE_H, VPSS_TILE_NV12_STRIDE);
        }
    }
}

static int process_live_vpss(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        if (!g_vpss_preview_frames[i]) g_vpss_preview_frames[i] = malloc(VPSS_TILE_NV12_SIZE);
        if (!g_vpss_preview_frames[i]) return -1;
        g_vpss_preview_ready[i] = 0;
    }
    if (MEDIA_POOL_GetBuffer(VPSS_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, CAM_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_VPSS_Group_SendFrame(LIVE_VPSS_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        out.pool_id = -1;
        out.index = -1;
        if (MEDIA_VPSS_Chn_GetFrame(LIVE_VPSS_GRP, i, &out, 20) == 0) {
            if (copy_from_buffer(out, g_vpss_preview_frames[i], VPSS_TILE_NV12_SIZE) == 0) {
                g_vpss_preview_ready[i] = 1;
            }
            MEDIA_VPSS_Chn_ReleaseFrame(LIVE_VPSS_GRP, i, out);
        }
    }
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        if (g_vpss_preview_ready[i]) {
            compose_vpss_showcase(dst, g_vpss_preview_frames, g_vpss_preview_ready);
            ret = 0;
            break;
        }
    }
    return ret;
}

static int setup_live_csc_rga(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_CSC_RGA_ATTR attr = {0};
    attr.input_width = CAM_W;
    attr.input_height = CAM_H;
    attr.input_format = MEDIA_FORMAT_NV12;
    attr.output_format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.csc_mode = 0;

    if (MEDIA_CSC_RGA_CreateGrp(LIVE_CSC_RGA_GRP, &attr) != 0 ||
        MEDIA_CSC_RGA_Start(LIVE_CSC_RGA_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(LIVE_CSC_RGA_GRP) != 0) {
        MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CSC_RGA", TILE_LIVE);
    return 0;
}

static void cleanup_live_csc_rga(int enabled) {
    if (!enabled) return;
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_csc_rga(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("CSC_RGA", LIVE_CSC_RGA_GRP, "input",
                          OSD_INPUT_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_CSC_RGA_GetFrame(LIVE_CSC_RGA_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_CSC_RGA_ReleaseFrame(LIVE_CSC_RGA_GRP, out);
    }
    return ret;
}

static int setup_live_csc_rga_chain(void) {
    if (MEDIA_POOL_Create(WORK_POOL_RGB, RGBA_FRAME_SIZE, 4) != 0) return -1;

    MEDIA_CSC_RGA_ATTR front = {0};
    front.input_width = CAM_W;
    front.input_height = CAM_H;
    front.input_format = MEDIA_FORMAT_NV12;
    front.output_format = MEDIA_FORMAT_ARGB8888;
    front.input_depth = 4;
    front.output_pool_id = WORK_POOL_RGB;
    front.input_stride = CAM_STRIDE;
    front.output_stride = CAM_W * 4;
    front.csc_mode = 0;

    MEDIA_CSC_RGA_ATTR back = {0};
    back.input_width = CAM_W;
    back.input_height = CAM_H;
    back.input_format = MEDIA_FORMAT_RGBA8888;
    back.output_format = MEDIA_FORMAT_NV12;
    back.input_depth = 4;
    back.output_pool_id = DISPLAY_VMIX_INPUT_POOL;
    back.input_stride = CAM_W * 4;
    back.output_stride = CAM_STRIDE;
    back.csc_mode = 0;

    if (MEDIA_CSC_RGA_CreateGrp(LIVE_CSC_RGA_GRP, &front) != 0 ||
        MEDIA_CSC_RGA_Start(LIVE_CSC_RGA_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(LIVE_CSC_RGA_GRP) != 0) {
        goto fail_front;
    }
    if (MEDIA_CSC_RGA_CreateGrp(LIVE_CSC_RGA_BACK_GRP, &back) != 0 ||
        MEDIA_CSC_RGA_Start(LIVE_CSC_RGA_BACK_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(LIVE_CSC_RGA_BACK_GRP) != 0) {
        goto fail_back;
    }
    set_tile_status("CSC_RGA", TILE_LIVE);
    return 0;

fail_back:
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_BACK_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_BACK_GRP);
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_BACK_GRP);
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
    return -1;
fail_front:
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
    return -1;
}

static void cleanup_live_csc_rga_chain(int enabled) {
    if (!enabled) return;
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_BACK_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_BACK_GRP);
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_BACK_GRP);
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
}

static int setup_live_csc_cl_chain(void) {
    const int csc_w = g_camera_input_w;
    const int csc_h = g_camera_input_h;
    const int csc_stride = ALIGN_UP(csc_w * 4, 64);
    const size_t csc_frame_size = (size_t)csc_stride * csc_h;
    if (MEDIA_POOL_Create(WORK_POOL_RGB, csc_frame_size, 2) != 0) return -1;
    if (MEDIA_POOL_Create(DISPLAY_VMIX_INPUT_POOL, CSC_CL_VIEW_FRAME_SIZE, 4) != 0) {
        MEDIA_POOL_Destroy(WORK_POOL_RGB);
        return -1;
    }
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CSC_CL_VIEW_FRAME_SIZE, 4) != 0) {
        MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
        MEDIA_POOL_Destroy(WORK_POOL_RGB);
        return -1;
    }

    MEDIA_CSC_CL_ATTR front = {0};
    front.input_width = csc_w;
    front.input_height = csc_h;
    front.input_format = MEDIA_FORMAT_NV12;
    front.output_format = MEDIA_FORMAT_ARGB8888;
    front.input_depth = 4;
    front.output_pool_id = WORK_POOL_RGB;
    front.input_stride = g_camera_input_stride;
    front.output_stride = csc_stride;

    MEDIA_OSD_ATTR osd = {0};
    osd.input_width = CSC_CL_VIEW_W;
    osd.input_height = CSC_CL_VIEW_H;
    osd.format = MEDIA_FORMAT_ARGB8888;
    osd.input_depth = 4;
    osd.output_pool_id = OSD_OUTPUT_POOL;
    osd.input_stride = CSC_CL_VIEW_STRIDE;
    osd.output_stride = CSC_CL_VIEW_STRIDE;
    osd.max_regions = 40;

    MEDIA_RESIZE_RGA_ATTR scale = {0};
    scale.src_x = 0;
    scale.src_y = 0;
    scale.src_width = csc_w;
    scale.src_height = csc_h;
    scale.input_width = csc_w;
    scale.input_height = csc_h;
    scale.input_stride = csc_stride;
    scale.input_format = MEDIA_FORMAT_ARGB8888;
    scale.input_depth = 4;
    scale.out_width = CSC_CL_VIEW_W;
    scale.out_height = CSC_CL_VIEW_H;
    scale.out_stride = CSC_CL_VIEW_STRIDE;
    scale.output_format = MEDIA_FORMAT_ARGB8888;
    scale.output_pool_id = DISPLAY_VMIX_INPUT_POOL;

    static const float k_bt601_limit_matrix[9] = {
        0.257f, 0.504f, 0.098f,
        -0.148f, -0.291f, 0.439f,
        0.439f, -0.368f, -0.071f
    };

    if (MEDIA_CSC_CL_CreateGrp(LIVE_CSC_CL_GRP, &front) != 0 ||
        MEDIA_CSC_CL_SetMatrix(LIVE_CSC_CL_GRP, k_bt601_limit_matrix, 9) != 0 ||
        MEDIA_CSC_CL_Start(LIVE_CSC_CL_GRP) != 0 ||
        MEDIA_CSC_CL_Enable(LIVE_CSC_CL_GRP) != 0) {
        goto fail_front;
    }
    if (MEDIA_OSD_CreateGrp(DISPLAY_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(DISPLAY_OSD_GRP) != 0 ||
        MEDIA_OSD_Enable(DISPLAY_OSD_GRP) != 0) {
        goto fail_osd;
    }
    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_RESIZE_GRP, &scale) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_RESIZE_GRP) != 0) {
        goto fail_rga;
    }
    set_tile_status("CSC_CL", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;

fail_rga:
    MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
    MEDIA_OSD_Disable(DISPLAY_OSD_GRP);
    MEDIA_OSD_Stop(DISPLAY_OSD_GRP);
    MEDIA_OSD_DestroyGrp(DISPLAY_OSD_GRP);
    MEDIA_CSC_CL_Disable(LIVE_CSC_CL_GRP);
    MEDIA_CSC_CL_Stop(LIVE_CSC_CL_GRP);
    MEDIA_CSC_CL_DestroyGrp(LIVE_CSC_CL_GRP);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
    return -1;
fail_osd:
    MEDIA_OSD_Disable(DISPLAY_OSD_GRP);
    MEDIA_OSD_Stop(DISPLAY_OSD_GRP);
    MEDIA_OSD_DestroyGrp(DISPLAY_OSD_GRP);
    MEDIA_CSC_CL_Disable(LIVE_CSC_CL_GRP);
    MEDIA_CSC_CL_Stop(LIVE_CSC_CL_GRP);
    MEDIA_CSC_CL_DestroyGrp(LIVE_CSC_CL_GRP);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
    return -1;
fail_front:
    MEDIA_CSC_CL_Disable(LIVE_CSC_CL_GRP);
    MEDIA_CSC_CL_Stop(LIVE_CSC_CL_GRP);
    MEDIA_CSC_CL_DestroyGrp(LIVE_CSC_CL_GRP);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
    return -1;
}

static void cleanup_live_csc_cl_chain(int enabled) {
    if (!enabled) return;
    drain_csc_cl_resize_output();
    MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
    drain_csc_cl_resize_output();
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
    drain_display_osd_output();
    MEDIA_OSD_Disable(DISPLAY_OSD_GRP);
    MEDIA_OSD_Stop(DISPLAY_OSD_GRP);
    drain_display_osd_output();
    MEDIA_OSD_DestroyGrp(DISPLAY_OSD_GRP);
    drain_csc_cl_output();
    MEDIA_CSC_CL_Disable(LIVE_CSC_CL_GRP);
    MEDIA_CSC_CL_Stop(LIVE_CSC_CL_GRP);
    drain_csc_cl_output();
    MEDIA_CSC_CL_DestroyGrp(LIVE_CSC_CL_GRP);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DISPLAY_VMIX_INPUT_POOL);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
}

static void fill_vpss_same_tile_initial_output_attr(MEDIA_VPSS_OUT_ATTR *out, int output_id);

static float *g_transform_lut = NULL;
static float *g_transform_quad_luts[VPSS_DEMO_OUTPUTS] = {NULL};

static void free_transform_quad_luts(void) {
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        free(g_transform_quad_luts[i]);
        g_transform_quad_luts[i] = NULL;
    }
}

static void clamp_transform_xy(float *sx, float *sy, int w, int h) {
    if (*sx < 0.0f) *sx = 0.0f;
    if (*sy < 0.0f) *sy = 0.0f;
    if (*sx > (float)(w - 1)) *sx = (float)(w - 1);
    if (*sy > (float)(h - 1)) *sy = (float)(h - 1);
}

static void build_transform_quad_lut(int effect_index, int frame, float *lut) {
    const int w = g_camera_input_w;
    const int h = g_camera_input_h;
    const float cx = (float)(w - 1) * 0.5f;
    const float cy = (float)(h - 1) * 0.5f;
    if (!lut) return;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float sx = (float)x;
            float sy = (float)y;
            float dx = (float)x - cx;
            float dy = (float)y - cy;

            if (effect_index == 1) {
                /*
                 * Lens undistortion is represented as an inverse radial map:
                 * each corrected output pixel samples farther from the source
                 * center, which compensates a barrel-distorted camera image.
                 */
                float nx = dx / cx;
                float ny = dy / cy;
                float r2 = nx * nx + ny * ny;
                float scale = 1.0f + 0.18f * r2 + 0.06f * r2 * r2;
                sx = cx + dx * scale;
                sy = cy + dy * scale;
            } else if (effect_index == 2) {
                float angle = (float)frame * 0.025f;
                float zoom = 1.12f + 0.10f * sinf((float)frame * 0.035f);
                float c = cosf(angle);
                float s = sinf(angle);
                float rx = dx / zoom;
                float ry = dy / zoom;
                sx = cx + c * rx + s * ry;
                sy = cy - s * rx + c * ry;
            } else if (effect_index == 3) {
                /*
                 * Perspective correction samples a trapezoid from the input
                 * and maps it back to a rectangle.  It demonstrates the same
                 * LUT mechanism used for keystone or document rectification.
                 */
                float u = (float)x / (float)(w - 1);
                float v = (float)y / (float)(h - 1);
                const float tlx = (float)w * 0.158f, tly = (float)h * 0.071f;
                const float trx = (float)w * 0.842f, try_ = (float)h * 0.142f;
                const float blx = (float)w * 0.050f, bly = (float)h * 0.946f;
                const float brx = (float)w * 0.950f, bry = (float)h * 0.896f;
                float top_x = tlx + (trx - tlx) * u;
                float top_y = tly + (try_ - tly) * u;
                float bot_x = blx + (brx - blx) * u;
                float bot_y = bly + (bry - bly) * u;
                sx = top_x + (bot_x - top_x) * v;
                sy = top_y + (bot_y - top_y) * v;
            }
            clamp_transform_xy(&sx, &sy, w, h);
            size_t off = ((size_t)y * w + x) * 2;
            lut[off] = sx;
            lut[off + 1] = sy;
        }
    }
}

static int alloc_transform_quad_luts(void) {
    size_t count = (size_t)g_camera_input_w * g_camera_input_h * 2;
    free_transform_quad_luts();
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        g_transform_quad_luts[i] = malloc(count * sizeof(float));
        if (!g_transform_quad_luts[i]) {
            free_transform_quad_luts();
            return -1;
        }
        build_transform_quad_lut(i, 0, g_transform_quad_luts[i]);
    }
    return 0;
}

static int update_transform_switch_lut(int mode, int frame) {
    if (mode < 0 || mode >= VPSS_DEMO_OUTPUTS || !g_transform_quad_luts[mode]) return -1;
    build_transform_quad_lut(mode, frame, g_transform_quad_luts[mode]);
    return MEDIA_TRANSFORM_UpdateLut(g_transform_quad_grps[1],
                                     g_transform_quad_luts[mode],
                                     (size_t)g_camera_input_w * g_camera_input_h * 2 * sizeof(float));
}

static int build_transform_lut(void) {
    size_t count = (size_t)CAM_W * CAM_H * 2;
    g_transform_lut = malloc(count * sizeof(float));
    if (!g_transform_lut) return -1;
    float cx = (float)(CAM_W - 1) * 0.5f;
    float cy = (float)(CAM_H - 1) * 0.5f;
    for (int y = 0; y < CAM_H; ++y) {
        for (int x = 0; x < CAM_W; ++x) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float radius = sqrtf(dx * dx + dy * dy);
            float wave = sinf(radius * 0.035f) * 10.0f;
            float sx = (float)x + wave * (dy / (radius + 1.0f));
            float sy = (float)y - wave * (dx / (radius + 1.0f));
            if (sx < 0.0f) sx = 0.0f;
            if (sy < 0.0f) sy = 0.0f;
            if (sx > (float)(CAM_W - 1)) sx = (float)(CAM_W - 1);
            if (sy > (float)(CAM_H - 1)) sy = (float)(CAM_H - 1);
            size_t off = ((size_t)y * CAM_W + x) * 2;
            g_transform_lut[off] = sx;
            g_transform_lut[off + 1] = sy;
        }
    }
    return 0;
}

static int setup_live_transform(void) {
    if (build_transform_lut() != 0) return -1;
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail_lut;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail_input_pool;

    MEDIA_TRANSFORM_ATTR attr = {0};
    attr.out_width = CAM_W;
    attr.out_height = CAM_H;
    attr.out_stride = CAM_STRIDE;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.pool_id = OSD_OUTPUT_POOL;
    attr.in_width = CAM_W;
    attr.in_height = CAM_H;
    attr.in_stride = CAM_STRIDE;
    attr.lut_width = CAM_W;
    attr.lut_height = CAM_H;
    attr.lut = g_transform_lut;
    attr.lut_size = (size_t)CAM_W * CAM_H * 2 * sizeof(float);

    if (MEDIA_TRANSFORM_CreateGrp(LIVE_TRANSFORM_GRP, &attr) != 0 ||
        MEDIA_TRANSFORM_Start(LIVE_TRANSFORM_GRP) != 0) {
        MEDIA_TRANSFORM_DestroyGrp(LIVE_TRANSFORM_GRP);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        goto fail_lut;
    }
    set_tile_status("TRANSFORM", TILE_LIVE);
    return 0;

fail_input_pool:
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
fail_lut:
    free(g_transform_lut);
    g_transform_lut = NULL;
    return -1;
}

static void cleanup_live_transform(int enabled) {
    if (!enabled) return;
    MEDIA_TRANSFORM_Stop(LIVE_TRANSFORM_GRP);
    MEDIA_TRANSFORM_DestroyGrp(LIVE_TRANSFORM_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
    free(g_transform_lut);
    g_transform_lut = NULL;
}

static int process_live_transform(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, CAM_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_TRANSFORM_Process(LIVE_TRANSFORM_GRP, in, &out) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_POOL_PutBuffer(out);
    }
    MEDIA_POOL_PutBuffer(in);
    return ret;
}

static int setup_live_transform_quad(void) {
    /*
     * Keep the demo responsive by running one 4K TRANSFORM branch and switching
     * its LUT once per second between RAW, UNDISTORT, ROTATE ZOOM, PERSPECTIVE.
     */
    if (alloc_transform_quad_luts() != 0) return -1;
    if (MEDIA_POOL_Create(VPSS_INPUT_POOL, live_camera_input_frame_size(), 3) != 0) goto fail_lut;
    if (MEDIA_POOL_Create(VPSS_OUTPUT_POOL, live_camera_input_frame_size(), 4) != 0) {
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        goto fail_lut;
    }
    if (MEDIA_POOL_Create(TRANSFORM_OUTPUT_POOL, live_camera_input_frame_size(), 4) != 0) {
        MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        goto fail_lut;
    }

    MEDIA_VPSS_ATTR vpss = {0};
    vpss.width = g_camera_input_w;
    vpss.height = g_camera_input_h;
    vpss.input_stride = g_camera_input_stride;
    vpss.input_depth = 4;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.output_count = 1;
    for (int i = 0; i < vpss.output_count; ++i) {
        vpss.outputs[i].output_id = i;
        vpss.outputs[i].out_width = g_camera_input_w;
        vpss.outputs[i].out_height = g_camera_input_h;
        vpss.outputs[i].out_stride = g_camera_input_stride;
        vpss.outputs[i].pool_id = VPSS_OUTPUT_POOL;
        vpss.outputs[i].crop_x = 0;
        vpss.outputs[i].crop_y = 0;
        vpss.outputs[i].crop_w = g_camera_input_w;
        vpss.outputs[i].crop_h = g_camera_input_h;
        vpss.outputs[i].in_fps = -1;
        vpss.outputs[i].out_fps = -1;
        vpss.outputs[i].output_format = MEDIA_FORMAT_NV12;
    }
    if (MEDIA_VPSS_SetAttr(LIVE_VPSS_GRP, &vpss) != 0 ||
        MEDIA_VPSS_Enable(LIVE_VPSS_GRP) != 0) {
        MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
        MEDIA_POOL_Destroy(TRANSFORM_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        goto fail_lut;
    }

    int created = 0;
    MEDIA_TRANSFORM_ATTR attr = {0};
    attr.out_width = g_camera_input_w;
    attr.out_height = g_camera_input_h;
    attr.out_stride = g_camera_input_stride;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.pool_id = TRANSFORM_OUTPUT_POOL;
    attr.in_width = g_camera_input_w;
    attr.in_height = g_camera_input_h;
    attr.in_stride = g_camera_input_stride;
    attr.lut_width = g_camera_input_w;
    attr.lut_height = g_camera_input_h;
    attr.lut = g_transform_quad_luts[0];
    attr.lut_size = (size_t)g_camera_input_w * g_camera_input_h * 2 * sizeof(float);

    if (MEDIA_TRANSFORM_CreateGrp(g_transform_quad_grps[1], &attr) != 0 ||
        MEDIA_TRANSFORM_Start(g_transform_quad_grps[1]) != 0) {
        MEDIA_TRANSFORM_DestroyGrp(g_transform_quad_grps[1]);
        MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
        MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
        MEDIA_POOL_Destroy(TRANSFORM_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        goto fail_lut;
    }
    created = 1;

    MEDIA_RESIZE_RGA_ATTR resize = {0};
    fill_compare_display_resize_attr(&resize);
    resize.out_width = VI_BIG_VIEW_W;
    resize.out_height = VI_BIG_VIEW_H;
    resize.out_stride = VI_BIG_VIEW_STRIDE;
    if (MEDIA_RESIZE_RGA_CreateGrp(g_transform_display_resize_grps[0], &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(g_transform_display_resize_grps[0]) != 0 ||
        MEDIA_RESIZE_RGA_Enable(g_transform_display_resize_grps[0]) != 0) {
        MEDIA_RESIZE_RGA_Disable(g_transform_display_resize_grps[0]);
        MEDIA_RESIZE_RGA_Stop(g_transform_display_resize_grps[0]);
        MEDIA_RESIZE_RGA_DestroyGrp(g_transform_display_resize_grps[0]);
        for (int j = created; j >= 1; --j) {
            MEDIA_TRANSFORM_Stop(g_transform_quad_grps[j]);
            MEDIA_TRANSFORM_DestroyGrp(g_transform_quad_grps[j]);
        }
        MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
        MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
        MEDIA_POOL_Destroy(TRANSFORM_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        goto fail_lut;
    }
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("TRANSFORM", TILE_LIVE);
    set_tile_status("VMIX", TILE_LIVE);
    return 0;

fail_lut:
    free_transform_quad_luts();
    return -1;
}

static void cleanup_live_transform_quad(int enabled) {
    if (!enabled) return;
    for (int i = 0; i < 1; ++i) {
        drain_resize_rga_output(g_transform_display_resize_grps[i]);
        MEDIA_RESIZE_RGA_Disable(g_transform_display_resize_grps[i]);
        MEDIA_RESIZE_RGA_Stop(g_transform_display_resize_grps[i]);
        MEDIA_RESIZE_RGA_DestroyGrp(g_transform_display_resize_grps[i]);
    }
    for (int i = 1; i >= 1; --i) {
        for (int j = 0; j < 8; ++j) {
            MEDIA_BUFFER buf = {-1, -1};
            if (MEDIA_TRANSFORM_GetFrame(g_transform_quad_grps[i], &buf, 0) != 0) {
                break;
            }
            MEDIA_TRANSFORM_ReleaseFrame(g_transform_quad_grps[i], buf);
        }
        MEDIA_TRANSFORM_Stop(g_transform_quad_grps[i]);
        MEDIA_TRANSFORM_DestroyGrp(g_transform_quad_grps[i]);
    }
    drain_live_vpss_outputs(1);
    MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
    MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
    MEDIA_POOL_Destroy(TRANSFORM_OUTPUT_POOL);
    MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
    MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
    free_transform_quad_luts();
}

static int setup_live_cap_dehaze(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, RGB_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, RGB_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_CAP_DEHAZE_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_RGB888;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_W * 3;
    attr.output_stride = CAM_W * 3;
    attr.guided_radius = 24;
    attr.guided_eps = 1e-3f;
    attr.t0 = 0.12f;
    attr.beta0 = 0.121779f;
    attr.beta1 = 0.959710f;
    attr.beta2 = -0.780245f;
    attr.depth_scale = 1.0f;

    if (MEDIA_CAP_DEHAZE_CreateGrp(LIVE_CAP_DEHAZE_GRP, &attr) != 0 ||
        MEDIA_CAP_DEHAZE_Start(LIVE_CAP_DEHAZE_GRP) != 0 ||
        MEDIA_CAP_DEHAZE_Enable(LIVE_CAP_DEHAZE_GRP) != 0) {
        MEDIA_CAP_DEHAZE_DestroyGrp(LIVE_CAP_DEHAZE_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CAP_DEHAZE", TILE_LIVE);
    return 0;
}

static void cleanup_live_cap_dehaze(int enabled) {
    if (!enabled) return;
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_CAP_DEHAZE_GetFrame(LIVE_CAP_DEHAZE_GRP, &out, 0) != 0) {
            break;
        }
        MEDIA_CAP_DEHAZE_ReleaseFrame(LIVE_CAP_DEHAZE_GRP, out);
    }
    MEDIA_CAP_DEHAZE_Disable(LIVE_CAP_DEHAZE_GRP);
    MEDIA_CAP_DEHAZE_Stop(LIVE_CAP_DEHAZE_GRP);
    MEDIA_CAP_DEHAZE_DestroyGrp(LIVE_CAP_DEHAZE_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int setup_live_cap_dehaze_bind_chain(int use_dcp) {
    const int dehaze_grp = use_dcp ? LIVE_DCP_DEHAZE_GRP : LIVE_CAP_DEHAZE_GRP;
    if (MEDIA_POOL_Create(WORK_POOL_RGB, DEHAZE_RGB4K_FRAME_SIZE, 2) != 0) return -1;
    if (MEDIA_POOL_Create(WORK_POOL_OUT, DEHAZE_RGB4K_FRAME_SIZE, 2) != 0) {
        MEDIA_POOL_Destroy(WORK_POOL_RGB);
        return -1;
    }

    MEDIA_CSC_RGA_ATTR csc = {0};
    csc.input_width = g_camera_input_w;
    csc.input_height = g_camera_input_h;
    csc.input_format = MEDIA_FORMAT_NV12;
    csc.output_format = MEDIA_FORMAT_RGB888;
    csc.input_depth = 4;
    csc.output_pool_id = WORK_POOL_RGB;
    csc.input_stride = g_camera_input_stride;
    csc.output_stride = DEHAZE_RGB4K_STRIDE;
    csc.csc_mode = 0;

    if (MEDIA_CSC_RGA_CreateGrp(LIVE_CSC_RGA_GRP, &csc) != 0 ||
        MEDIA_CSC_RGA_Start(LIVE_CSC_RGA_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(LIVE_CSC_RGA_GRP) != 0) {
        goto fail_csc;
    }

    if (use_dcp) {
        MEDIA_DCP_FAST_DEHAZE_ATTR attr = {0};
        attr.width = g_camera_input_w;
        attr.height = g_camera_input_h;
        attr.format = MEDIA_FORMAT_RGB888;
        attr.input_depth = 1;
        attr.output_pool_id = WORK_POOL_OUT;
        attr.input_stride = DEHAZE_RGB4K_STRIDE;
        attr.output_stride = DEHAZE_RGB4K_STRIDE;
        attr.patch = 15;
        attr.omega = 0.95f;
        attr.t0 = 0.12f;
        attr.airlight_percent = 0.001f;
        attr.guided_radius = 24;
        attr.guided_eps = 1e-3f;
        attr.refine_scale = 0.5f;
        attr.passthrough = 1;
        if (MEDIA_DCP_FAST_DEHAZE_CreateGrp(dehaze_grp, &attr) != 0 ||
            MEDIA_DCP_FAST_DEHAZE_Start(dehaze_grp) != 0 ||
            MEDIA_DCP_FAST_DEHAZE_Enable(dehaze_grp) != 0) {
            goto fail_dehaze;
        }
        set_tile_status("DCP_FAST_DEHAZE", TILE_LIVE);
    } else {
        MEDIA_CAP_DEHAZE_ATTR attr = {0};
        attr.width = g_camera_input_w;
        attr.height = g_camera_input_h;
        attr.format = MEDIA_FORMAT_RGB888;
        attr.input_depth = 1;
        attr.output_pool_id = WORK_POOL_OUT;
        attr.input_stride = DEHAZE_RGB4K_STRIDE;
        attr.output_stride = DEHAZE_RGB4K_STRIDE;
        attr.guided_radius = 24;
        attr.guided_eps = 1e-3f;
        attr.t0 = 0.12f;
        attr.beta0 = 0.121779f;
        attr.beta1 = 0.959710f;
        attr.beta2 = -0.780245f;
        attr.depth_scale = 1.0f;
        attr.passthrough = 1;
        if (MEDIA_CAP_DEHAZE_CreateGrp(dehaze_grp, &attr) != 0 ||
            MEDIA_CAP_DEHAZE_Start(dehaze_grp) != 0 ||
            MEDIA_CAP_DEHAZE_Enable(dehaze_grp) != 0) {
            goto fail_dehaze;
        }
        set_tile_status("CAP_DEHAZE", TILE_LIVE);
    }

    MEDIA_RESIZE_RGA_ATTR resize = {0};
    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = g_camera_input_w;
    resize.src_height = g_camera_input_h;
    resize.input_width = g_camera_input_w;
    resize.input_height = g_camera_input_h;
    resize.input_stride = DEHAZE_RGB4K_STRIDE;
    resize.input_format = MEDIA_FORMAT_RGB888;
    resize.input_depth = 4;
    resize.out_width = VI_BIG_VIEW_W;
    resize.out_height = VI_BIG_VIEW_H;
    resize.out_stride = VI_BIG_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = DISPLAY_VMIX_INPUT_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_COMPARE_EFFECT_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_COMPARE_EFFECT_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_COMPARE_EFFECT_RESIZE_GRP) != 0) {
        goto fail_resize;
    }
    set_tile_status("CSC_RGA", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;

fail_resize:
    MEDIA_RESIZE_RGA_Disable(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_COMPARE_EFFECT_RESIZE_GRP);
fail_dehaze:
    if (use_dcp) {
        MEDIA_DCP_FAST_DEHAZE_Disable(dehaze_grp);
        MEDIA_DCP_FAST_DEHAZE_Stop(dehaze_grp);
        MEDIA_DCP_FAST_DEHAZE_DestroyGrp(dehaze_grp);
    } else {
        MEDIA_CAP_DEHAZE_Disable(dehaze_grp);
        MEDIA_CAP_DEHAZE_Stop(dehaze_grp);
        MEDIA_CAP_DEHAZE_DestroyGrp(dehaze_grp);
    }
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
fail_csc:
    MEDIA_POOL_Destroy(WORK_POOL_OUT);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
    return -1;
}

static void cleanup_live_cap_dehaze_bind_chain(int enabled, int use_dcp) {
    if (!enabled) return;
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_GRP);
    if (use_dcp) {
        MEDIA_DCP_FAST_DEHAZE_Disable(LIVE_DCP_DEHAZE_GRP);
        MEDIA_DCP_FAST_DEHAZE_Stop(LIVE_DCP_DEHAZE_GRP);
    } else {
        MEDIA_CAP_DEHAZE_Disable(LIVE_CAP_DEHAZE_GRP);
        MEDIA_CAP_DEHAZE_Stop(LIVE_CAP_DEHAZE_GRP);
    }
    MEDIA_RESIZE_RGA_Disable(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER buf = {-1, -1};
        if (MEDIA_RESIZE_RGA_GetFrame(LIVE_COMPARE_EFFECT_RESIZE_GRP, &buf, 0) != 0) break;
        MEDIA_RESIZE_RGA_ReleaseFrame(LIVE_COMPARE_EFFECT_RESIZE_GRP, buf);
    }
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER buf = {-1, -1};
        if (use_dcp) {
            if (MEDIA_DCP_FAST_DEHAZE_GetFrame(LIVE_DCP_DEHAZE_GRP, &buf, 0) != 0) break;
            MEDIA_DCP_FAST_DEHAZE_ReleaseFrame(LIVE_DCP_DEHAZE_GRP, buf);
        } else {
            if (MEDIA_CAP_DEHAZE_GetFrame(LIVE_CAP_DEHAZE_GRP, &buf, 0) != 0) break;
            MEDIA_CAP_DEHAZE_ReleaseFrame(LIVE_CAP_DEHAZE_GRP, buf);
        }
    }
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER buf = {-1, -1};
        if (MEDIA_CSC_RGA_GetFrame(LIVE_CSC_RGA_GRP, &buf, 0) != 0) break;
        MEDIA_CSC_RGA_ReleaseFrame(LIVE_CSC_RGA_GRP, buf);
    }
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_COMPARE_EFFECT_RESIZE_GRP);
    if (use_dcp) {
        MEDIA_DCP_FAST_DEHAZE_DestroyGrp(LIVE_DCP_DEHAZE_GRP);
    } else {
        MEDIA_CAP_DEHAZE_DestroyGrp(LIVE_CAP_DEHAZE_GRP);
    }
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
    MEDIA_POOL_Destroy(WORK_POOL_OUT);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
}

static int process_live_cap_dehaze(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, RGB_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_CAP_DEHAZE_SendFrame(LIVE_CAP_DEHAZE_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_CAP_DEHAZE_GetFrame(LIVE_CAP_DEHAZE_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, RGB_FRAME_SIZE);
        MEDIA_CAP_DEHAZE_ReleaseFrame(LIVE_CAP_DEHAZE_GRP, out);
    }
    return ret;
}

static int setup_live_dcp_dehaze(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, RGB_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, RGB_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_DCP_FAST_DEHAZE_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_RGB888;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_W * 3;
    attr.output_stride = CAM_W * 3;
    attr.patch = 15;
    attr.omega = 0.95f;
    attr.t0 = 0.12f;
    attr.airlight_percent = 0.001f;
    attr.guided_radius = 24;
    attr.guided_eps = 1e-3f;
    attr.refine_scale = 0.5f;

    if (MEDIA_DCP_FAST_DEHAZE_CreateGrp(LIVE_DCP_DEHAZE_GRP, &attr) != 0 ||
        MEDIA_DCP_FAST_DEHAZE_Start(LIVE_DCP_DEHAZE_GRP) != 0 ||
        MEDIA_DCP_FAST_DEHAZE_Enable(LIVE_DCP_DEHAZE_GRP) != 0) {
        MEDIA_DCP_FAST_DEHAZE_DestroyGrp(LIVE_DCP_DEHAZE_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("DCP_FAST_DEHAZE", TILE_LIVE);
    return 0;
}

static void cleanup_live_dcp_dehaze(int enabled) {
    if (!enabled) return;
    MEDIA_DCP_FAST_DEHAZE_Disable(LIVE_DCP_DEHAZE_GRP);
    MEDIA_DCP_FAST_DEHAZE_Stop(LIVE_DCP_DEHAZE_GRP);
    MEDIA_DCP_FAST_DEHAZE_DestroyGrp(LIVE_DCP_DEHAZE_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_dcp_dehaze(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, RGB_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_DCP_FAST_DEHAZE_SendFrame(LIVE_DCP_DEHAZE_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_DCP_FAST_DEHAZE_GetFrame(LIVE_DCP_DEHAZE_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, RGB_FRAME_SIZE);
        MEDIA_DCP_FAST_DEHAZE_ReleaseFrame(LIVE_DCP_DEHAZE_GRP, out);
    }
    return ret;
}

static int setup_live_conv_cl(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, RGBA_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, RGBA_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_CONV_CL_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_RGBA8888;
    attr.kernel_size = 3;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_W * 4;
    attr.output_stride = CAM_W * 4;

    if (MEDIA_CONV_CL_CreateGrp(LIVE_CONV_CL_GRP, &attr) != 0 ||
        MEDIA_CONV_CL_Start(LIVE_CONV_CL_GRP) != 0) {
        MEDIA_CONV_CL_DestroyGrp(LIVE_CONV_CL_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CONV_CL", TILE_LIVE);
    return 0;
}

static void cleanup_live_conv_cl(int enabled) {
    if (!enabled) return;
    MEDIA_CONV_CL_Stop(LIVE_CONV_CL_GRP);
    MEDIA_CONV_CL_DestroyGrp(LIVE_CONV_CL_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_conv_cl(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("CONV_CL", LIVE_CONV_CL_GRP, "input",
                          OSD_INPUT_POOL, src, RGBA_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_CONV_CL_GetFrame(LIVE_CONV_CL_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, RGBA_FRAME_SIZE);
        MEDIA_CONV_CL_ReleaseFrame(LIVE_CONV_CL_GRP, out);
    }
    return ret;
}

static void blit_conv_rgba_quadrant(uint8_t *dst, const uint8_t *src, int idx) {
    if (!dst || !src || idx < 0 || idx >= VPSS_DEMO_OUTPUTS) return;
    const int qw = CAM_W / 2;
    const int qh = CAM_H / 2;
    const int ox = (idx % 2) * qw;
    const int oy = (idx / 2) * qh;
    for (int y = 0; y < qh; ++y) {
        int sy = y * CAM_H / qh;
        uint8_t *drow = dst + ((size_t)(oy + y) * CAM_W + ox) * 4;
        const uint8_t *srow = src + (size_t)sy * CAM_W * 4;
        for (int x = 0; x < qw; ++x) {
            int sx = x * CAM_W / qw;
            memcpy(drow + x * 4, srow + sx * 4, 4);
        }
    }
}

static int process_live_conv_cl_effect(int effect_idx, const uint8_t *src,
                                       uint8_t *dst,
                                       MEDIA_CONV_CL_PERF *perf) {
    if (effect_idx < 0 || effect_idx >= VPSS_DEMO_OUTPUTS || !src || !dst) {
        return -1;
    }
    if (MEDIA_CONV_CL_SetTable(LIVE_CONV_CL_GRP,
                               g_conv_cl_effects[effect_idx].table,
                               g_conv_cl_effects[effect_idx].table_size) != 0) {
        return -1;
    }
    if (process_live_conv_cl(src, dst) != 0) {
        return -1;
    }
    if (perf) {
        memset(perf, 0, sizeof(*perf));
        (void)MEDIA_CONV_CL_GetLastPerf(LIVE_CONV_CL_GRP, perf);
    }
    return 0;
}

static int process_live_conv_cl_grid(const uint8_t *src, uint8_t *tmp,
                                     uint8_t *grid) {
    if (!src || !tmp || !grid) return -1;
    memset(grid, 0, RGBA_FRAME_SIZE);
    g_conv_cl_show_kernel_ms = -1.0;
    g_conv_cl_show_queue_ms = -1.0;

    double kernel_sum = 0.0;
    double queue_sum = 0.0;
    int perf_ok = 1;
    int ok_count = 0;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        MEDIA_CONV_CL_PERF perf = {0};
        if (process_live_conv_cl_effect(i, src, tmp, &perf) == 0) {
            blit_conv_rgba_quadrant(grid, tmp, i);
            g_conv_cl_show_counts[i]++;
            ok_count++;
            if (perf.gpu_kernel_total_ms >= 0.0 &&
                perf.gpu_queue_total_ms >= 0.0) {
                kernel_sum += perf.gpu_kernel_total_ms;
                queue_sum += perf.gpu_queue_total_ms;
            } else {
                perf_ok = 0;
            }
        } else {
            perf_ok = 0;
        }
    }
    if (perf_ok && ok_count > 0) {
        g_conv_cl_show_kernel_ms = kernel_sum;
        g_conv_cl_show_queue_ms = queue_sum;
    }
    return ok_count == VPSS_DEMO_OUTPUTS ? 0 : -1;
}

static void fill_vpss_same_tile_output_attr(MEDIA_VPSS_OUT_ATTR *out, int output_id,
                                            int crop_x, int crop_y, int crop_w, int crop_h,
                                            int flip_h, int flip_v) {
    memset(out, 0, sizeof(*out));
    out->output_id = output_id;
    out->out_width = VPSS_TILE_W;
    out->out_height = VPSS_TILE_H;
    out->out_stride = VPSS_TILE_NV12_STRIDE;
    out->pool_id = VPSS_OUTPUT_POOL;
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

static void fill_vpss_same_tile_initial_output_attr(MEDIA_VPSS_OUT_ATTR *out, int output_id) {
    /*
     * Four-up comparison pages compare downstream modules, not VPSS transforms.
     * Keep every VPSS output on the same full-frame crop, size, and orientation
     * so the only visual difference between quadrants comes from the target
     * module effect.
     */
    fill_vpss_same_tile_output_attr(out, output_id, 0, 0,
                                    g_camera_input_w, g_camera_input_h, 0, 0);
}

static void destroy_live_conv_cl_quad_pools(int conv_input_count,
                                            int conv_output_count,
                                            int vpss_output_created,
                                            int vpss_input_created) {
    for (int i = conv_output_count - 1; i >= 0; --i) {
        MEDIA_POOL_Destroy(g_conv_cl_output_pools[i]);
    }
    for (int i = conv_input_count - 1; i >= 0; --i) {
        MEDIA_POOL_Destroy(g_conv_cl_input_pools[i]);
    }
    if (vpss_output_created) {
        MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
    }
    if (vpss_input_created) {
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
    }
}

static int setup_live_conv_cl_quad(void) {
    /*
     * VI, VPSS, VMIX, OSD and VO stay on NV12 for the realtime display path.
     * CONV_CL itself accepts RGB-family frames, and RGB888 dma-buf import is not
     * reliable on this EGL/OpenCL stack, so each branch uses RGBA8888 as the
     * GPU-facing bridge format:
     *
     *   VI(NV12) -> VPSS(4x NV12)
     *     -> 4x CSC_RGA(NV12->RGBA8888)
     *     -> 4x CONV_CL(RGBA8888)
     *     -> 4x CSC_RGA(RGBA8888->NV12) -> VMIX -> OSD -> VO
     *
     * Each CONV_CL group owns one branch and applies a different 3x3 kernel.
     * The explicit pre/post CSC_RGA stages make the format transitions visible
     * in logs and keep the branch binding one-to-one with the VMIX quadrants.
     */
    int vpss_input_pool_created = 0;
    int vpss_output_pool_created = 0;
    int conv_input_pools_created = 0;
    int conv_output_pools_created = 0;

    if (MEDIA_POOL_Create(VPSS_INPUT_POOL, live_camera_input_frame_size(), 3) != 0) return -1;
    vpss_input_pool_created = 1;
    if (MEDIA_POOL_Create(VPSS_OUTPUT_POOL, VPSS_TILE_NV12_SIZE, VPSS_QUAD_OUTPUT_POOL_COUNT) != 0) {
        destroy_live_conv_cl_quad_pools(conv_input_pools_created, conv_output_pools_created,
                                        vpss_output_pool_created, vpss_input_pool_created);
        return -1;
    }
    vpss_output_pool_created = 1;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        if (MEDIA_POOL_Create(g_conv_cl_input_pools[i], VPSS_TILE_RGBA_SIZE, 4) != 0) {
            destroy_live_conv_cl_quad_pools(conv_input_pools_created, conv_output_pools_created,
                                            vpss_output_pool_created, vpss_input_pool_created);
            return -1;
        }
        conv_input_pools_created++;
    }
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        if (MEDIA_POOL_Create(g_conv_cl_output_pools[i], VPSS_TILE_RGBA_SIZE, 4) != 0) {
            destroy_live_conv_cl_quad_pools(conv_input_pools_created, conv_output_pools_created,
                                            vpss_output_pool_created, vpss_input_pool_created);
            return -1;
        }
        conv_output_pools_created++;
    }

    MEDIA_VPSS_ATTR vpss = {0};
    vpss.width = g_camera_input_w;
    vpss.height = g_camera_input_h;
    vpss.input_stride = g_camera_input_stride;
    vpss.input_depth = 4;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.output_count = VPSS_DEMO_OUTPUTS;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        fill_vpss_same_tile_initial_output_attr(&vpss.outputs[i], i);
    }
    if (MEDIA_VPSS_SetAttr(LIVE_VPSS_GRP, &vpss) != 0 ||
        MEDIA_VPSS_Enable(LIVE_VPSS_GRP) != 0) {
        MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
        destroy_live_conv_cl_quad_pools(conv_input_pools_created, conv_output_pools_created,
                                        vpss_output_pool_created, vpss_input_pool_created);
        return -1;
    }

    int pre_csc_created = 0;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        MEDIA_CSC_RGA_ATTR attr = {0};
        attr.input_width = VPSS_TILE_W;
        attr.input_height = VPSS_TILE_H;
        attr.input_format = MEDIA_FORMAT_NV12;
        attr.output_format = MEDIA_FORMAT_RGBA8888;
        attr.input_depth = 3;
        attr.output_pool_id = g_conv_cl_input_pools[i];
        attr.input_stride = VPSS_TILE_NV12_STRIDE;
        attr.output_stride = VPSS_TILE_RGBA_STRIDE;
        attr.csc_mode = 0;

        int grp = g_conv_cl_pre_csc_grps[i];
        if (MEDIA_CSC_RGA_CreateGrp(grp, &attr) != 0 ||
            MEDIA_CSC_RGA_Start(grp) != 0 ||
            MEDIA_CSC_RGA_Enable(grp) != 0) {
            for (int j = pre_csc_created - 1; j >= 0; --j) {
                MEDIA_CSC_RGA_Disable(g_conv_cl_pre_csc_grps[j]);
                MEDIA_CSC_RGA_Stop(g_conv_cl_pre_csc_grps[j]);
                MEDIA_CSC_RGA_DestroyGrp(g_conv_cl_pre_csc_grps[j]);
            }
            MEDIA_CSC_RGA_DestroyGrp(grp);
            MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
            MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
            destroy_live_conv_cl_quad_pools(conv_input_pools_created, conv_output_pools_created,
                                            vpss_output_pool_created, vpss_input_pool_created);
            return -1;
        }
        pre_csc_created++;
    }

    int conv_created = 0;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        MEDIA_CONV_CL_ATTR attr = {0};
        attr.width = VPSS_TILE_W;
        attr.height = VPSS_TILE_H;
        attr.format = MEDIA_FORMAT_RGBA8888;
        attr.kernel_size = g_conv_cl_effects[i].kernel_size;
        attr.input_depth = 3;
        attr.output_pool_id = g_conv_cl_output_pools[i];
        attr.input_stride = VPSS_TILE_RGBA_STRIDE;
        attr.output_stride = VPSS_TILE_RGBA_STRIDE;

        int grp = g_conv_cl_quad_grps[i];
        if (MEDIA_CONV_CL_CreateGrp(grp, &attr) != 0 ||
            MEDIA_CONV_CL_SetTable(grp, g_conv_cl_effects[i].table,
                                   g_conv_cl_effects[i].table_size) != 0 ||
            MEDIA_CONV_CL_Start(grp) != 0) {
            for (int j = conv_created - 1; j >= 0; --j) {
                MEDIA_CONV_CL_Stop(g_conv_cl_quad_grps[j]);
                MEDIA_CONV_CL_DestroyGrp(g_conv_cl_quad_grps[j]);
            }
            MEDIA_CONV_CL_DestroyGrp(grp);
            for (int j = pre_csc_created - 1; j >= 0; --j) {
                MEDIA_CSC_RGA_Disable(g_conv_cl_pre_csc_grps[j]);
                MEDIA_CSC_RGA_Stop(g_conv_cl_pre_csc_grps[j]);
                MEDIA_CSC_RGA_DestroyGrp(g_conv_cl_pre_csc_grps[j]);
            }
            MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
            MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
            destroy_live_conv_cl_quad_pools(conv_input_pools_created, conv_output_pools_created,
                                            vpss_output_pool_created, vpss_input_pool_created);
            return -1;
        }
        conv_created++;
    }

    int csc_created = 0;
    for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
        MEDIA_CSC_RGA_ATTR attr = {0};
        attr.input_width = VPSS_TILE_W;
        attr.input_height = VPSS_TILE_H;
        attr.input_format = MEDIA_FORMAT_RGBA8888;
        attr.output_format = MEDIA_FORMAT_NV12;
        attr.input_depth = 3;
        attr.output_pool_id = DISPLAY_VMIX_INPUT_POOL;
        attr.input_stride = VPSS_TILE_RGBA_STRIDE;
        attr.output_stride = VPSS_TILE_NV12_STRIDE;
        attr.csc_mode = 0;

        int grp = g_conv_cl_csc_grps[i];
        if (MEDIA_CSC_RGA_CreateGrp(grp, &attr) != 0 ||
            MEDIA_CSC_RGA_Start(grp) != 0 ||
            MEDIA_CSC_RGA_Enable(grp) != 0) {
            for (int j = csc_created - 1; j >= 0; --j) {
                MEDIA_CSC_RGA_Disable(g_conv_cl_csc_grps[j]);
                MEDIA_CSC_RGA_Stop(g_conv_cl_csc_grps[j]);
                MEDIA_CSC_RGA_DestroyGrp(g_conv_cl_csc_grps[j]);
            }
            MEDIA_CSC_RGA_DestroyGrp(grp);
            for (int j = conv_created - 1; j >= 0; --j) {
                MEDIA_CONV_CL_Stop(g_conv_cl_quad_grps[j]);
                MEDIA_CONV_CL_DestroyGrp(g_conv_cl_quad_grps[j]);
            }
            for (int j = pre_csc_created - 1; j >= 0; --j) {
                MEDIA_CSC_RGA_Disable(g_conv_cl_pre_csc_grps[j]);
                MEDIA_CSC_RGA_Stop(g_conv_cl_pre_csc_grps[j]);
                MEDIA_CSC_RGA_DestroyGrp(g_conv_cl_pre_csc_grps[j]);
            }
            MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
            MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
            destroy_live_conv_cl_quad_pools(conv_input_pools_created, conv_output_pools_created,
                                            vpss_output_pool_created, vpss_input_pool_created);
            return -1;
        }
        csc_created++;
    }

    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("CONV_CL", TILE_LIVE);
    set_tile_status("CSC_RGA", TILE_LIVE);
    set_tile_status("VMIX", TILE_LIVE);
    return 0;
}

static void cleanup_live_conv_cl_quad(int enabled) {
    if (!enabled) return;
    for (int i = VPSS_DEMO_OUTPUTS - 1; i >= 0; --i) {
        MEDIA_CSC_RGA_Disable(g_conv_cl_csc_grps[i]);
        MEDIA_CSC_RGA_Stop(g_conv_cl_csc_grps[i]);
        MEDIA_CSC_RGA_DestroyGrp(g_conv_cl_csc_grps[i]);
    }
    for (int i = VPSS_DEMO_OUTPUTS - 1; i >= 0; --i) {
        MEDIA_CONV_CL_Stop(g_conv_cl_quad_grps[i]);
        MEDIA_CONV_CL_DestroyGrp(g_conv_cl_quad_grps[i]);
    }
    for (int i = VPSS_DEMO_OUTPUTS - 1; i >= 0; --i) {
        MEDIA_CSC_RGA_Disable(g_conv_cl_pre_csc_grps[i]);
        MEDIA_CSC_RGA_Stop(g_conv_cl_pre_csc_grps[i]);
        MEDIA_CSC_RGA_DestroyGrp(g_conv_cl_pre_csc_grps[i]);
    }
    MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
    MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
    destroy_live_conv_cl_quad_pools(VPSS_DEMO_OUTPUTS, VPSS_DEMO_OUTPUTS, 1, 1);
}

static int setup_live_clahe(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_CLAHE_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.tile_grid_x = 8;
    attr.tile_grid_y = 8;
    attr.bins = 256;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.clip_limit = 2.5f;
    attr.highlight_protect_start = 0.92f;
    attr.highlight_protect_strength = 0.4f;

    if (MEDIA_CLAHE_CreateGrp(LIVE_CLAHE_GRP, &attr) != 0 ||
        MEDIA_CLAHE_Start(LIVE_CLAHE_GRP) != 0 ||
        MEDIA_CLAHE_Enable(LIVE_CLAHE_GRP) != 0) {
        MEDIA_CLAHE_DestroyGrp(LIVE_CLAHE_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CLAHE", TILE_LIVE);
    return 0;
}

static void cleanup_live_clahe(int enabled) {
    if (!enabled) return;
    MEDIA_CLAHE_Disable(LIVE_CLAHE_GRP);
    MEDIA_CLAHE_Stop(LIVE_CLAHE_GRP);
    drain_live_clahe_output();
    MEDIA_CLAHE_DestroyGrp(LIVE_CLAHE_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int setup_live_clahe_bind(void) {
    if (MEDIA_POOL_Create(CLAHE_OUTPUT_POOL, live_camera_input_frame_size(), 4) != 0) {
        return -1;
    }

    MEDIA_CLAHE_ATTR attr = {0};
    attr.width = g_camera_input_w;
    attr.height = g_camera_input_h;
    attr.format = MEDIA_FORMAT_NV12;
    attr.tile_grid_x = 8;
    attr.tile_grid_y = 8;
    attr.bins = 256;
    attr.input_depth = 4;
    attr.output_pool_id = CLAHE_OUTPUT_POOL;
    attr.input_stride = g_camera_input_stride;
    attr.output_stride = g_camera_input_stride;
    attr.clip_limit = 2.5f;
    attr.highlight_protect_start = 0.92f;
    attr.highlight_protect_strength = 0.4f;
    attr.passthrough = 1;

    if (MEDIA_CLAHE_CreateGrp(LIVE_CLAHE_GRP, &attr) != 0 ||
        MEDIA_CLAHE_Start(LIVE_CLAHE_GRP) != 0 ||
        MEDIA_CLAHE_Enable(LIVE_CLAHE_GRP) != 0) {
        MEDIA_CLAHE_DestroyGrp(LIVE_CLAHE_GRP);
        MEDIA_POOL_Destroy(CLAHE_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CLAHE", TILE_LIVE);
    return 0;
}

static void cleanup_live_clahe_bind(int enabled) {
    if (!enabled) return;
    MEDIA_CLAHE_Disable(LIVE_CLAHE_GRP);
    MEDIA_CLAHE_Stop(LIVE_CLAHE_GRP);
    drain_live_clahe_output();
    MEDIA_CLAHE_DestroyGrp(LIVE_CLAHE_GRP);
    MEDIA_POOL_Destroy(CLAHE_OUTPUT_POOL);
}

static int update_clahe_compare_overlay(uint64_t vi_count, uint64_t clahe_count,
                                        uint64_t effect_resize_count,
                                        float clip, int passthrough_mode,
                                        int frame) {
    static uint8_t masks[16][1024 * 96];
    const int view_x = (SCREEN_W - COMPARE_VIEW_W) / 2;
    const int view_y = (SCREEN_H - COMPARE_VIEW_H) / 2;
    char count_line[128];
    char clip_line[96];
    char bind_line1[180];
    char mode_line[96];
    const char *mix = g_clahe_mix_module ? g_clahe_mix_module : "VMIX";

    snprintf(count_line, sizeof(count_line), "FRAMES  VI=%llu  CLAHE=%llu  RESIZE=%llu",
             (unsigned long long)vi_count, (unsigned long long)clahe_count,
             (unsigned long long)effect_resize_count);
    snprintf(clip_line, sizeof(clip_line), "CLIP LIMIT %.2f  TILE GRID 8x8",
             clip);
    snprintf(mode_line, sizeof(mode_line), "MODE %s  1s AUTO SWITCH",
             passthrough_mode ? "PASSTHROUGH" : "CLAHE ENHANCE");
    snprintf(bind_line1, sizeof(bind_line1), "VI0.%s -> CLAHE%d.%s -> RESIZE%d.%s -> %s%d.%s -> OSD%d -> VO0",
             g_bind_vi_src_port ? g_bind_vi_src_port : "output0",
             LIVE_CLAHE_GRP,
             g_bind_clahe_in_port ? g_bind_clahe_in_port : "input",
             LIVE_COMPARE_EFFECT_RESIZE_GRP,
             g_bind_compare_effect_resize_src_port ? g_bind_compare_effect_resize_src_port : "output0",
             mix,
             DISPLAY_VMIX_GRP,
             g_bind_vmix_in_port ? g_bind_vmix_in_port : "input0",
             DISPLAY_OSD_GRP);

    if (update_osd_rect_region(18, view_x, view_y - 50, 430, 38, 5, 10, 18, 220) != 0) return -1;
    if (update_osd_utf8_text_region(20, view_x + 16, view_y - 45, 24, 190, 230, 255,
                                    passthrough_mode ? "当前：CLAHE直通原图" : "当前：CLAHE增强输出",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;

    if (update_osd_rect_region(23, 34, 1510, 1012, 286, 5, 10, 18, 226) != 0) return -1;
    if (update_osd_utf8_text_region(24, 66, 1536, 34, 160, 255, 220,
                                    "CLAHE：4K输入，1秒直通、1秒增强交替显示",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(25, 68, 1588, 24, 190, 230, 255,
                                    "数据流：VI 3840x2160直接进入CLAHE，最后RESIZE_RGA等比例缩放为1080x608显示。",
                                    masks[3], sizeof(masks[3]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(26, 68, 1630, 23, 255, 230, 120,
                                    "为什么这样做：按8x8小块增强暗部细节，clip limit限制噪声被过度放大。",
                                    masks[4], sizeof(masks[4]), 1024, 96) != 0) return -1;

    int clip_w = (int)(clip * 120.0f);
    if (clip_w < 120) clip_w = 120;
    if (clip_w > 456) clip_w = 456;
    if (update_osd_rect_region(27, 68, 1676, clip_w, 28, 255, 220, 70, 220) != 0) return -1;
    if (update_osd_text_region(28, 548, 1666, 1, 170, 255, 220,
                               clip_line, masks[5], sizeof(masks[5])) != 0) return -1;
    if (update_osd_text_region(29, 68, 1716, 1, 170, 255, 220,
                               count_line, masks[6], sizeof(masks[6])) != 0) return -1;
    if (update_osd_text_region(30, 68, 1746, 1, 170, 205, 235,
                               bind_line1, masks[7], sizeof(masks[7])) != 0) return -1;
    if (update_osd_text_region(31, 68, 1772, 1, 170, 205, 235,
                               mode_line, masks[8], sizeof(masks[8])) != 0) return -1;

    return 0;
}

static int process_live_clahe(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("CLAHE", LIVE_CLAHE_GRP, "input",
                          OSD_INPUT_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_CLAHE_GetFrame(LIVE_CLAHE_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_CLAHE_ReleaseFrame(LIVE_CLAHE_GRP, out);
    }
    return ret;
}

static int setup_live_retinex(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;

    MEDIA_RETINEX_ATTR attr = {0};
    attr.scale_count = 1;
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_depth = 3;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.gain = 20.0f;
    attr.threshold = 0.5f;
    attr.log_min = -3.0f;
    attr.log_max = 8.5f;

    if (MEDIA_RETINEX_CreateGrp(LIVE_RETINEX_GRP, &attr) != 0 ||
        MEDIA_RETINEX_Start(LIVE_RETINEX_GRP) != 0) {
        MEDIA_RETINEX_DestroyGrp(LIVE_RETINEX_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }
    set_tile_status("RETINEX", TILE_LIVE);
    return 0;
}

static void cleanup_live_retinex(int enabled) {
    if (!enabled) return;
    MEDIA_RETINEX_Stop(LIVE_RETINEX_GRP);
    drain_live_retinex_output();
    MEDIA_RETINEX_DestroyGrp(LIVE_RETINEX_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
}

static int setup_live_retinex_bind(void) {
    MEDIA_RETINEX_ATTR attr = {0};
    attr.scale_count = 1;
    attr.width = g_camera_input_w;
    attr.height = g_camera_input_h;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.output_depth = 4;
    attr.input_stride = g_camera_input_stride;
    attr.output_stride = g_camera_input_stride;
    attr.gain = 20.0f;
    attr.threshold = 0.5f;
    attr.log_min = -3.0f;
    attr.log_max = 8.5f;

    if (MEDIA_RETINEX_CreateGrp(LIVE_RETINEX_GRP, &attr) != 0 ||
        MEDIA_RETINEX_Start(LIVE_RETINEX_GRP) != 0) {
        MEDIA_RETINEX_DestroyGrp(LIVE_RETINEX_GRP);
        return -1;
    }
    set_tile_status("RETINEX", TILE_LIVE);
    return 0;
}

static void cleanup_live_retinex_bind(int enabled) {
    if (!enabled) return;
    MEDIA_RETINEX_Stop(LIVE_RETINEX_GRP);
    MEDIA_RETINEX_DestroyGrp(LIVE_RETINEX_GRP);
}

static int process_live_retinex(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("RETINEX", LIVE_RETINEX_GRP, "input0",
                          OSD_INPUT_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_RETINEX_GetFrame(LIVE_RETINEX_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_RETINEX_ReleaseFrame(LIVE_RETINEX_GRP, out);
    }
    return ret;
}

static int update_retinex_compare_overlay(uint64_t vi_count, uint64_t retinex_count,
                                          uint64_t raw_resize_count,
                                          uint64_t effect_resize_count,
                                          int frame) {
    static uint8_t masks[16][1024 * 96];
    const int view_x = (SCREEN_W - COMPARE_VIEW_W) / 2;
    const int view_y = (SCREEN_H - COMPARE_VIEW_H) / 2;
    char count_line[128];
    char param_line[128];
    char bind_line1[180];
    char mode_line[96];
    const char *mix = g_retinex_mix_module ? g_retinex_mix_module : "VMIX";
    (void)raw_resize_count;

    snprintf(count_line, sizeof(count_line), "FRAMES  VI=%llu  RETINEX=%llu  RESIZE=%llu",
             (unsigned long long)vi_count, (unsigned long long)retinex_count,
             (unsigned long long)effect_resize_count);
    snprintf(param_line, sizeof(param_line), "GAIN 20.0  THRESHOLD 0.5  LOG -3.0..8.5");
    snprintf(mode_line, sizeof(mode_line), "MODE %s  1s AUTO SWITCH",
             ((frame / FPS) % 2) == 0 ? "PASSTHROUGH" : "RETINEX ENHANCE");
    snprintf(bind_line1, sizeof(bind_line1), "VI0.%s -> RETINEX%d.%s -> RESIZE%d.%s -> %s%d.%s -> OSD%d -> VO0",
             g_bind_vi_src_port ? g_bind_vi_src_port : "output0",
             LIVE_RETINEX_GRP,
             g_bind_retinex_in_port ? g_bind_retinex_in_port : "input0",
             LIVE_COMPARE_EFFECT_RESIZE_GRP,
             g_bind_compare_effect_resize_src_port ? g_bind_compare_effect_resize_src_port : "output0",
             mix,
             DISPLAY_VMIX_GRP,
             g_bind_vmix_in_port ? g_bind_vmix_in_port : "input0",
             DISPLAY_OSD_GRP);

    if (update_osd_rect_region(18, view_x, view_y - 50, 430, 38, 5, 10, 18, 220) != 0) return -1;
    if (update_osd_utf8_text_region(20, view_x + 16, view_y - 45, 24, 190, 230, 255,
                                    ((frame / FPS) % 2) == 0 ? "当前：RETINEX直通原图" : "当前：RETINEX增强输出",
                                    masks[0], sizeof(masks[0]), 1024, 96) != 0) return -1;

    if (update_osd_rect_region(22, 34, 1510, 1012, 286, 5, 10, 18, 226) != 0) return -1;
    if (update_osd_utf8_text_region(23, 66, 1536, 34, 160, 255, 220,
                                    "RETINEX：4K输入，1秒直通、1秒增强交替显示",
                                    masks[2], sizeof(masks[2]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(24, 68, 1588, 24, 190, 230, 255,
                                    "数据流：VI 3840x2160直接进入RETINEX，最后RESIZE_RGA等比例缩放为1080x608显示。",
                                    masks[3], sizeof(masks[3]), 1024, 96) != 0) return -1;
    if (update_osd_utf8_text_region(25, 68, 1630, 23, 255, 230, 120,
                                    "为什么这样做：压低不均匀光照影响，让阴影和背光区域细节更清楚。",
                                    masks[4], sizeof(masks[4]), 1024, 96) != 0) return -1;

    int pulse_w = 180 + ((frame * 7) % 260);
    if (update_osd_rect_region(26, 68, 1676, pulse_w, 28, 120, 220, 255, 220) != 0) return -1;
    if (update_osd_text_region(27, 548, 1666, 1, 170, 255, 220,
                               param_line, masks[5], sizeof(masks[5])) != 0) return -1;
    if (update_osd_text_region(28, 68, 1716, 1, 170, 255, 220,
                               count_line, masks[6], sizeof(masks[6])) != 0) return -1;
    if (update_osd_text_region(29, 68, 1746, 1, 170, 205, 235,
                               mode_line, masks[7], sizeof(masks[7])) != 0) return -1;
    if (update_osd_text_region(30, 68, 1772, 1, 170, 205, 235,
                               bind_line1, masks[8], sizeof(masks[8])) != 0) return -1;

    return 0;
}

static int setup_live_edof(void) {
    if (MEDIA_POOL_Create(STEREO_INPUT0_POOL, CAM_FRAME_SIZE, 2) != 0) return -1;
    if (MEDIA_POOL_Create(STEREO_INPUT1_POOL, CAM_FRAME_SIZE, 2) != 0) goto fail;
    if (MEDIA_POOL_Create(STEREO_OUTPUT_POOL, CAM_FRAME_SIZE, 4) != 0) goto fail;

    MEDIA_EDOF_CL_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.focus_radius = 2;
    attr.input_depth = 2;
    attr.output_pool_id = STEREO_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.score_eps = 1e-3f;

    if (MEDIA_EDOF_CL_CreateGrp(LIVE_EDOF_GRP, &attr) != 0 ||
        MEDIA_EDOF_CL_Enable(LIVE_EDOF_GRP) != 0) {
        MEDIA_EDOF_CL_DestroyGrp(LIVE_EDOF_GRP);
        goto fail;
    }

    float identity_warp[6] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    (void)MEDIA_EDOF_CL_SetAffineWarp(LIVE_EDOF_GRP, identity_warp);
    set_tile_status("EDOF_CL", TILE_LIVE);
    return 0;

fail:
    MEDIA_POOL_Destroy(STEREO_INPUT0_POOL);
    MEDIA_POOL_Destroy(STEREO_INPUT1_POOL);
    MEDIA_POOL_Destroy(STEREO_OUTPUT_POOL);
    return -1;
}

static void cleanup_live_edof(int enabled) {
    if (!enabled) return;
    MEDIA_EDOF_CL_Disable(LIVE_EDOF_GRP);
    MEDIA_EDOF_CL_DestroyGrp(LIVE_EDOF_GRP);
    MEDIA_POOL_Destroy(STEREO_INPUT0_POOL);
    MEDIA_POOL_Destroy(STEREO_INPUT1_POOL);
    MEDIA_POOL_Destroy(STEREO_OUTPUT_POOL);
}

static int process_live_edof(const uint8_t *src0, const uint8_t *src1, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("EDOF_CL", LIVE_EDOF_GRP, "input0",
                          STEREO_INPUT0_POOL, src0, CAM_FRAME_SIZE, 1000) != 0) {
        return -1;
    }
    if (send_copied_frame("EDOF_CL", LIVE_EDOF_GRP, "input1",
                          STEREO_INPUT1_POOL, src1, CAM_FRAME_SIZE, 1000) != 0) {
        return -1;
    }
    if (MEDIA_EDOF_CL_GetFrame(LIVE_EDOF_GRP, &out, 2000) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_EDOF_CL_ReleaseFrame(LIVE_EDOF_GRP, out);
    }
    return ret;
}

static int setup_live_mcf(void) {
    if (MEDIA_POOL_Create(MCF_INPUT0_POOL, CAM_FRAME_SIZE, 2) != 0) return -1;
    if (MEDIA_POOL_Create(MCF_INPUT1_POOL, CAM_FRAME_SIZE, 2) != 0) goto fail;
    if (MEDIA_POOL_Create(MCF_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail;

    MEDIA_MCF_FUSION_CL_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 2;
    attr.output_pool_id = MCF_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.path = MEDIA_MCF_FUSION_CL_PATH_FUSION;
    attr.normalize_mode = MEDIA_MCF_FUSION_CL_NORMALIZE_MEAN_STD;
    attr.blur_radius = 2;
    attr.base_alpha = 0.25f;
    attr.detail_gain = 0.80f;
    attr.alpha_min = 0.15f;
    attr.alpha_max = 0.85f;
    attr.gain_min = 0.5f;
    attr.gain_max = 2.0f;
    attr.epsilon = 1e-6f;

    if (MEDIA_MCF_FUSION_CL_CreateGrp(LIVE_MCF_GRP, &attr) != 0 ||
        MEDIA_MCF_FUSION_CL_Enable(LIVE_MCF_GRP) != 0) {
        MEDIA_MCF_FUSION_CL_DestroyGrp(LIVE_MCF_GRP);
        goto fail;
    }

    set_tile_status("MCF_FUSION_CL", TILE_LIVE);
    return 0;

fail:
    MEDIA_POOL_Destroy(MCF_OUTPUT_POOL);
    MEDIA_POOL_Destroy(MCF_INPUT1_POOL);
    MEDIA_POOL_Destroy(MCF_INPUT0_POOL);
    return -1;
}

static void cleanup_live_mcf(int enabled) {
    if (!enabled) return;
    MEDIA_MCF_FUSION_CL_Disable(LIVE_MCF_GRP);
    MEDIA_MCF_FUSION_CL_DestroyGrp(LIVE_MCF_GRP);
    MEDIA_POOL_Destroy(MCF_OUTPUT_POOL);
    MEDIA_POOL_Destroy(MCF_INPUT1_POOL);
    MEDIA_POOL_Destroy(MCF_INPUT0_POOL);
}

static int process_live_mcf(const uint8_t *color, const uint8_t *mono, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("MCF_FUSION_CL", LIVE_MCF_GRP, "input0",
                          MCF_INPUT0_POOL, color, CAM_FRAME_SIZE, 1000) != 0) {
        return -1;
    }
    if (send_copied_frame("MCF_FUSION_CL", LIVE_MCF_GRP, "input1",
                          MCF_INPUT1_POOL, mono, CAM_FRAME_SIZE, 1000) != 0) {
        return -1;
    }
    if (MEDIA_MCF_FUSION_CL_GetFrame(LIVE_MCF_GRP, &out, 3000) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_MCF_FUSION_CL_ReleaseFrame(LIVE_MCF_GRP, out);
    }
    return ret;
}

static int setup_live_dualview_one(int grp, int output_pool, int mode) {
    MEDIA_DUALVIEW_ATTR attr = {0};
    attr.input_width = CAM_W;
    attr.input_height = CAM_H;
    attr.input_stride = CAM_W * 3;
    attr.output_width = CAM_W;
    attr.output_height = CAM_H;
    attr.output_stride = CAM_W * 3;
    attr.mode = mode;
    attr.format = MEDIA_FORMAT_RGB888;
    attr.input_depth = 2;
    attr.output_pool_id = output_pool;
    attr.inputs[0].enabled = 1;
    attr.inputs[1].enabled = 1;

    if (mode == MEDIA_DUALVIEW_MODE_SIDE_BY_SIDE) {
        attr.inputs[0].x = 0;
        attr.inputs[0].y = 0;
        attr.inputs[0].width = CAM_W / 2;
        attr.inputs[0].height = CAM_H;
        attr.inputs[1].x = CAM_W / 2;
        attr.inputs[1].y = 0;
        attr.inputs[1].width = CAM_W / 2;
        attr.inputs[1].height = CAM_H;
    }

    if (MEDIA_DUALVIEW_CreateGrp(grp, &attr) != 0 ||
        MEDIA_DUALVIEW_Start(grp) != 0) {
        MEDIA_DUALVIEW_DestroyGrp(grp);
        return -1;
    }
    return 0;
}

static int setup_live_dualview(void) {
    if (MEDIA_POOL_Create(DUALVIEW_INPUT0_POOL, RGB_FRAME_SIZE, 2) != 0) return -1;
    if (MEDIA_POOL_Create(DUALVIEW_INPUT1_POOL, RGB_FRAME_SIZE, 2) != 0) goto fail;
    if (MEDIA_POOL_Create(DUALVIEW_SBS_OUTPUT_POOL, RGB_FRAME_SIZE, 2) != 0) goto fail;
    if (MEDIA_POOL_Create(DUALVIEW_LBL_OUTPUT_POOL, RGB_FRAME_SIZE, 2) != 0) goto fail;

    if (setup_live_dualview_one(LIVE_DUALVIEW_SBS_GRP, DUALVIEW_SBS_OUTPUT_POOL,
                                MEDIA_DUALVIEW_MODE_SIDE_BY_SIDE) != 0) {
        goto fail;
    }
    if (setup_live_dualview_one(LIVE_DUALVIEW_LBL_GRP, DUALVIEW_LBL_OUTPUT_POOL,
                                MEDIA_DUALVIEW_MODE_LINE_BY_LINE) != 0) {
        MEDIA_DUALVIEW_Stop(LIVE_DUALVIEW_SBS_GRP);
        MEDIA_DUALVIEW_DestroyGrp(LIVE_DUALVIEW_SBS_GRP);
        goto fail;
    }
    set_tile_status("DUALVIEW", TILE_LIVE);
    return 0;

fail:
    MEDIA_POOL_Destroy(DUALVIEW_INPUT0_POOL);
    MEDIA_POOL_Destroy(DUALVIEW_INPUT1_POOL);
    MEDIA_POOL_Destroy(DUALVIEW_SBS_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DUALVIEW_LBL_OUTPUT_POOL);
    return -1;
}

static void cleanup_live_dualview(int enabled) {
    if (!enabled) return;
    MEDIA_DUALVIEW_Stop(LIVE_DUALVIEW_LBL_GRP);
    MEDIA_DUALVIEW_DestroyGrp(LIVE_DUALVIEW_LBL_GRP);
    MEDIA_DUALVIEW_Stop(LIVE_DUALVIEW_SBS_GRP);
    MEDIA_DUALVIEW_DestroyGrp(LIVE_DUALVIEW_SBS_GRP);
    MEDIA_POOL_Destroy(DUALVIEW_INPUT0_POOL);
    MEDIA_POOL_Destroy(DUALVIEW_INPUT1_POOL);
    MEDIA_POOL_Destroy(DUALVIEW_SBS_OUTPUT_POOL);
    MEDIA_POOL_Destroy(DUALVIEW_LBL_OUTPUT_POOL);
}

static int process_live_dualview_one(int grp, const uint8_t *src0, const uint8_t *src1, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("DUALVIEW", grp, "input0",
                          DUALVIEW_INPUT0_POOL, src0, RGB_FRAME_SIZE, 1000) != 0) {
        return -1;
    }
    if (send_copied_frame("DUALVIEW", grp, "input1",
                          DUALVIEW_INPUT1_POOL, src1, RGB_FRAME_SIZE, 1000) != 0) {
        return -1;
    }
    if (MEDIA_DUALVIEW_GetFrame(grp, &out, 1000) == 0) {
        ret = copy_from_buffer(out, dst, RGB_FRAME_SIZE);
        MEDIA_DUALVIEW_ReleaseFrame(grp, out);
    }
    return ret;
}

static int setup_live_pano(void) {
    if (!g_pano_sample.loaded || g_pano_sample.in_w <= 0 || g_pano_sample.in_h <= 0) return -1;
    size_t input_size = (size_t)g_pano_sample.in_w * g_pano_sample.in_h * 3 / 2;
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, input_size, g_pano_sample.input_count) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, PANO_OUTPUT_SIZE, 1) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_PANO_ATTR attr = {0};
    attr.input_count = g_pano_sample.input_count;
    attr.in_width = g_pano_sample.in_w;
    attr.in_height = g_pano_sample.in_h;
    attr.in_stride = g_pano_sample.in_w;
    attr.out_width = PANO_STITCH_W;
    attr.out_height = PANO_STITCH_H;
    attr.out_stride = PANO_STITCH_STRIDE;
    attr.crop_enable = 1;
    attr.crop_x = 0;
    attr.crop_y = 0;
    attr.crop_width = PANO_DOMAIN_W;
    attr.crop_height = PANO_DOMAIN_H;
    attr.lut_width = PANO_STITCH_W;
    attr.lut_height = PANO_STITCH_H;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_depth = g_pano_sample.input_count;
    attr.output_depth = 1;
    attr.sync_timeout_ms = 200;
    attr.pto_path = g_pano_sample.pto_path;

    if (MEDIA_PANO_CreateGrp(LIVE_PANO_GRP, &attr) != 0 ||
        MEDIA_PANO_Start(LIVE_PANO_GRP) != 0) {
        MEDIA_PANO_DestroyGrp(LIVE_PANO_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("PANO", TILE_LIVE);
    return 0;
}

static void cleanup_live_pano(int enabled) {
    if (!enabled) return;
    MEDIA_PANO_Stop(LIVE_PANO_GRP);
    MEDIA_PANO_DestroyGrp(LIVE_PANO_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_pano(uint8_t *dst) {
    MEDIA_BUFFER in_bufs[PANO_INPUT_COUNT];
    MEDIA_BUFFER out = {-1, -1};
    int sent[PANO_INPUT_COUNT] = {0};
    int ret = -1;
    size_t input_size = (size_t)g_pano_sample.in_w * g_pano_sample.in_h * 3 / 2;

    memset(in_bufs, 0xff, sizeof(in_bufs));
    if (!dst || !g_pano_sample.loaded) return -1;

    for (int i = 0; i < g_pano_sample.input_count; ++i) {
        if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in_bufs[i]) != 0) goto done;
        if (copy_to_buffer(in_bufs[i], g_pano_sample.nv12[i], input_size) != 0) goto done;
        if (MEDIA_PANO_SendFrame(LIVE_PANO_GRP, i, in_bufs[i], 1000) != 0) goto done;
        sent[i] = 1;
        in_bufs[i].pool_id = -1;
        in_bufs[i].index = -1;
    }

    if (MEDIA_PANO_GetFrame(LIVE_PANO_GRP, &out, 30000) == 0) {
        ret = copy_from_buffer(out, dst, PANO_OUTPUT_SIZE);
        MEDIA_PANO_ReleaseFrame(LIVE_PANO_GRP, out);
        out.pool_id = -1;
        out.index = -1;
    }

done:
    if (out.pool_id >= 0) {
        MEDIA_PANO_ReleaseFrame(LIVE_PANO_GRP, out);
    }
    for (int i = 0; i < g_pano_sample.input_count; ++i) {
        if (!sent[i] && in_bufs[i].pool_id >= 0) {
            MEDIA_POOL_PutBuffer(in_bufs[i]);
        }
    }
    return ret;
}

static int setup_live_stereo(void) {
    if (MEDIA_POOL_Create(STEREO_INPUT0_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(STEREO_INPUT1_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail;
    if (MEDIA_POOL_Create(STEREO_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail;

    MEDIA_STEREO_3D_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_pool_id = STEREO_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.mode = MEDIA_STEREO_3D_MODE_LINE_BY_LINE;
    if (MEDIA_STEREO_3D_CreateGrp(LIVE_STEREO_GRP, &attr) != 0 ||
        MEDIA_STEREO_3D_Enable(LIVE_STEREO_GRP) != 0) {
        goto fail;
    }
    set_tile_status("STEREO_3D", TILE_LIVE);
    return 0;

fail:
    MEDIA_STEREO_3D_DestroyGrp(LIVE_STEREO_GRP);
    MEDIA_POOL_Destroy(STEREO_INPUT0_POOL);
    MEDIA_POOL_Destroy(STEREO_INPUT1_POOL);
    MEDIA_POOL_Destroy(STEREO_OUTPUT_POOL);
    return -1;
}

static void cleanup_live_stereo(int enabled) {
    if (!enabled) return;
    MEDIA_STEREO_3D_Disable(LIVE_STEREO_GRP);
    MEDIA_STEREO_3D_DestroyGrp(LIVE_STEREO_GRP);
    MEDIA_POOL_Destroy(STEREO_INPUT0_POOL);
    MEDIA_POOL_Destroy(STEREO_INPUT1_POOL);
    MEDIA_POOL_Destroy(STEREO_OUTPUT_POOL);
}

static int process_live_stereo(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("STEREO_3D", LIVE_STEREO_GRP, "input0",
                          STEREO_INPUT0_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (send_copied_frame("STEREO_3D", LIVE_STEREO_GRP, "input1",
                          STEREO_INPUT1_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_STEREO_3D_GetFrame(LIVE_STEREO_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_STEREO_3D_ReleaseFrame(LIVE_STEREO_GRP, out);
    }
    return ret;
}

static void fill_vpss_stereo_output_attr(MEDIA_VPSS_OUT_ATTR *out, int output_id) {
    memset(out, 0, sizeof(*out));
    out->output_id = output_id;
    out->out_width = CAM_W;
    out->out_height = CAM_H;
    out->out_stride = CAM_STRIDE;
    out->pool_id = VPSS_OUTPUT_POOL;
    out->crop_x = 0;
    out->crop_y = 0;
    out->crop_w = g_camera_input_w;
    out->crop_h = g_camera_input_h;
    out->in_fps = -1;
    out->out_fps = -1;
    out->output_format = MEDIA_FORMAT_NV12;
    out->rotate = output_id == STEREO_ROTATED_OUTPUT ? STEREO_VPSS_ROTATE_DEGREES : 0;
}

static int create_stereo_group(int grp, int mode) {
    MEDIA_STEREO_3D_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.output_pool_id = STEREO_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.mode = mode;
    attr.rotate_input = 1;
    attr.rotation_degrees = 0.0f;
    if (MEDIA_STEREO_3D_CreateGrp(grp, &attr) != 0 ||
        MEDIA_STEREO_3D_Enable(grp) != 0) {
        MEDIA_STEREO_3D_DestroyGrp(grp);
        return -1;
    }
    return 0;
}

static int setup_live_stereo_bind(void) {
    /*
     * The realtime STEREO_3D page now keeps one simple, visible path:
     *
     *   VI -> VPSS(output0 raw, output1 rotated 90 degrees)
     *      -> STEREO_3D side-by-side merge
     *      -> VMIX -> OSD -> VO
     *
     * There is no color tint stage.  The second branch is rotated by VPSS so
     * the two STEREO_3D inputs are visibly different before they are merged.
     */
    if (MEDIA_POOL_Create(VPSS_INPUT_POOL, live_camera_input_frame_size(), 3) != 0) return -1;
    if (MEDIA_POOL_Create(VPSS_OUTPUT_POOL, CAM_FRAME_SIZE, STEREO_INPUT_COUNT * 4) != 0) {
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        return -1;
    }
    if (MEDIA_POOL_Create(STEREO_OUTPUT_POOL, CAM_FRAME_SIZE, 8) != 0) {
        MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        return -1;
    }

    MEDIA_VPSS_ATTR vpss = {0};
    vpss.width = g_camera_input_w;
    vpss.height = g_camera_input_h;
    vpss.input_stride = g_camera_input_stride;
    vpss.input_depth = 4;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.output_count = STEREO_INPUT_COUNT;
    for (int i = 0; i < STEREO_INPUT_COUNT; ++i) {
        fill_vpss_stereo_output_attr(&vpss.outputs[i], i);
    }
    if (MEDIA_VPSS_SetAttr(LIVE_VPSS_GRP, &vpss) != 0 ||
        MEDIA_VPSS_Enable(LIVE_VPSS_GRP) != 0) {
        MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
        goto fail_pools;
    }

    if (create_stereo_group(LIVE_STEREO_GRP, MEDIA_STEREO_3D_MODE_SIDE_BY_SIDE) != 0) {
        MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
        MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
        goto fail_pools;
    }

    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("STEREO_3D", TILE_LIVE);
    set_tile_status("VMIX", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    return 0;

fail_pools:
    MEDIA_POOL_Destroy(STEREO_OUTPUT_POOL);
    MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
    MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
    return -1;
}

static void cleanup_live_stereo_bind(int enabled) {
    if (!enabled) return;
    MEDIA_STEREO_3D_Disable(LIVE_STEREO_GRP);
    MEDIA_STEREO_3D_DestroyGrp(LIVE_STEREO_GRP);
    MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
    MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
    MEDIA_POOL_Destroy(STEREO_OUTPUT_POOL);
    MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
    MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
}

static void mark(const char *name, int active) {
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (strcmp(g_tiles[i].name, name) == 0) {
            g_tiles[i].active = active;
            return;
        }
    }
}

static void set_tile_status(const char *name, int status) {
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (strcmp(g_tiles[i].name, name) == 0) {
            g_tiles[i].status = status;
            g_tiles[i].active = status != TILE_OFFLINE;
            return;
        }
    }
}

static int find_tile_index(const char *name) {
    if (!name || !*name) return -1;
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (strcasecmp(g_tiles[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int is_mcf_tile_name(const char *name) {
    return name && (strcasecmp(name, "MCF") == 0 ||
                    strcasecmp(name, "MCF_FUSION_CL") == 0);
}

static const char *canonical_tile_name(const char *name) {
    if (is_mcf_tile_name(name)) return "MCF_FUSION_CL";
    return name;
}

static int tile_needs_camera(const char *name) {
    if (!name) return 1;
    return strcasecmp(name, "VI") == 0 ||
           strcasecmp(name, "WBC") == 0 ||
           strcasecmp(name, "OSD") == 0 ||
           strcasecmp(name, "RGA") == 0 ||
           strcasecmp(name, "RESIZE_RGA") == 0 ||
           strcasecmp(name, "VPSS") == 0 ||
           strcasecmp(name, "CSC_RGA") == 0 ||
           strcasecmp(name, "CSC_CL") == 0 ||
           strcasecmp(name, "CAP_DEHAZE") == 0 ||
           strcasecmp(name, "DCP_FAST_DEHAZE") == 0 ||
           strcasecmp(name, "CONV_CL") == 0 ||
           strcasecmp(name, "TRANSFORM") == 0 ||
           strcasecmp(name, "VMIX") == 0 ||
           strcasecmp(name, "CLAHE") == 0 ||
           strcasecmp(name, "RETINEX") == 0 ||
           strcasecmp(name, "STEREO_3D") == 0;
}

static int tile_uses_4k_camera_input(const char *name) {
    if (!name) return 0;
    return strcasecmp(name, "VI") == 0 ||
           strcasecmp(name, "VPSS") == 0 ||
           strcasecmp(name, "VMIX") == 0 ||
           strcasecmp(name, "OSD") == 0 ||
           strcasecmp(name, "CSC_CL") == 0 ||
           strcasecmp(name, "RGA") == 0 ||
           strcasecmp(name, "RESIZE_RGA") == 0 ||
           strcasecmp(name, "CLAHE") == 0 ||
           strcasecmp(name, "RETINEX") == 0 ||
           strcasecmp(name, "CAP_DEHAZE") == 0 ||
           strcasecmp(name, "CONV_CL") == 0 ||
           strcasecmp(name, "TRANSFORM") == 0;
}

static int load_loop_assets(void) {
    int total = 0;
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        loop_asset_t *loop = &g_loop_assets[i];
        loop->loaded_count = 0;
        for (int j = 0; j < loop->path_count; ++j) {
            image_asset_t img = {0};
            if (load_image_rgb(loop->paths[j], &img) == 0) {
                loop->images[loop->loaded_count++] = img;
                total++;
            } else {
                fprintf(stderr, "warning: failed to load loop asset %s\n", loop->paths[j]);
            }
        }
        if (loop->loaded_count > 0 && default_page_index(loop->tile_name) >= 0) {
            set_tile_status(loop->tile_name, TILE_LOOP);
        }
    }
    return total;
}

static void unload_loop_assets(void) {
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        loop_asset_t *loop = &g_loop_assets[i];
        for (int j = 0; j < loop->loaded_count; ++j) {
            free(loop->images[j].rgb);
            loop->images[j].rgb = NULL;
        }
        loop->loaded_count = 0;
    }
}

static int load_edof_pairs(void) {
    int total = 0;
    for (size_t i = 0; i < sizeof(g_edof_pairs) / sizeof(g_edof_pairs[0]); ++i) {
        edof_pair_t *pair = &g_edof_pairs[i];
        pair->loaded = 0;
        if (load_image_rgb(pair->left_path, &pair->left) == 0 &&
            load_image_rgb(pair->right_path, &pair->right) == 0 &&
            load_image_rgb(pair->fused_path, &pair->fused) == 0) {
            pair->loaded = 1;
            total++;
        } else {
            fprintf(stderr, "warning: failed to load EDOF set %s %s %s\n",
                    pair->left_path, pair->right_path, pair->fused_path);
            free(pair->left.rgb);
            free(pair->right.rgb);
            free(pair->fused.rgb);
            memset(&pair->left, 0, sizeof(pair->left));
            memset(&pair->right, 0, sizeof(pair->right));
            memset(&pair->fused, 0, sizeof(pair->fused));
        }
    }
    return total;
}

static void unload_edof_pairs(void) {
    for (size_t i = 0; i < sizeof(g_edof_pairs) / sizeof(g_edof_pairs[0]); ++i) {
        edof_pair_t *pair = &g_edof_pairs[i];
        free(pair->left.rgb);
        free(pair->right.rgb);
        free(pair->fused.rgb);
        memset(&pair->left, 0, sizeof(pair->left));
        memset(&pair->right, 0, sizeof(pair->right));
        memset(&pair->fused, 0, sizeof(pair->fused));
        pair->loaded = 0;
    }
}

static int load_mcf_pairs(void) {
    int total = 0;
    for (size_t i = 0; i < sizeof(g_mcf_pairs) / sizeof(g_mcf_pairs[0]); ++i) {
        mcf_pair_t *pair = &g_mcf_pairs[i];
        pair->loaded = 0;
        if (load_image_rgb(pair->color_path, &pair->color) == 0 &&
            load_image_rgb(pair->mono_path, &pair->mono) == 0 &&
            load_image_rgb(pair->reference_path, &pair->reference) == 0) {
            pair->loaded = 1;
            total++;
        } else {
            fprintf(stderr, "warning: failed to load MCF set %s %s %s\n",
                    pair->color_path, pair->mono_path, pair->reference_path);
            free(pair->color.rgb);
            free(pair->mono.rgb);
            free(pair->reference.rgb);
            memset(&pair->color, 0, sizeof(pair->color));
            memset(&pair->mono, 0, sizeof(pair->mono));
            memset(&pair->reference, 0, sizeof(pair->reference));
        }
    }
    return total;
}

static void unload_mcf_pairs(void) {
    for (size_t i = 0; i < sizeof(g_mcf_pairs) / sizeof(g_mcf_pairs[0]); ++i) {
        mcf_pair_t *pair = &g_mcf_pairs[i];
        free(pair->color.rgb);
        free(pair->mono.rgb);
        free(pair->reference.rgb);
        memset(&pair->color, 0, sizeof(pair->color));
        memset(&pair->mono, 0, sizeof(pair->mono));
        memset(&pair->reference, 0, sizeof(pair->reference));
        pair->loaded = 0;
    }
}

static int load_pano_sample(void) {
    pano_sample_t *sample = &g_pano_sample;
    sample->loaded = 0;
    sample->in_w = 0;
    sample->in_h = 0;

    if (access(sample->pto_path, R_OK) != 0) {
        fprintf(stderr, "warning: failed to access PANO PTO %s\n", sample->pto_path);
        return 0;
    }

    for (int i = 0; i < sample->input_count; ++i) {
        image_asset_t *img = &sample->inputs[i];
        if (load_image_rgb(sample->image_paths[i], img) != 0) {
            fprintf(stderr, "warning: failed to load PANO input %s\n", sample->image_paths[i]);
            goto fail;
        }
        if ((img->width & 1) || (img->height & 1)) {
            fprintf(stderr, "warning: PANO input must be even-sized: %s %dx%d\n",
                    sample->image_paths[i], img->width, img->height);
            goto fail;
        }
        if (i == 0) {
            sample->in_w = img->width;
            sample->in_h = img->height;
        } else if (img->width != sample->in_w || img->height != sample->in_h) {
            fprintf(stderr, "warning: PANO input size mismatch: %s %dx%d expected %dx%d\n",
                    sample->image_paths[i], img->width, img->height, sample->in_w, sample->in_h);
            goto fail;
        }
    }

    size_t nv12_size = (size_t)sample->in_w * sample->in_h * 3 / 2;
    for (int i = 0; i < sample->input_count; ++i) {
        sample->nv12[i] = malloc(nv12_size);
        if (!sample->nv12[i]) goto fail;
        image_to_nv12_packed(&sample->inputs[i], sample->nv12[i]);
    }

    sample->loaded = 1;
    return 1;

fail:
    for (int i = 0; i < sample->input_count; ++i) {
        free(sample->inputs[i].rgb);
        memset(&sample->inputs[i], 0, sizeof(sample->inputs[i]));
        free(sample->nv12[i]);
        sample->nv12[i] = NULL;
    }
    sample->in_w = 0;
    sample->in_h = 0;
    return 0;
}

static void unload_pano_sample(void) {
    pano_sample_t *sample = &g_pano_sample;
    for (int i = 0; i < sample->input_count; ++i) {
        free(sample->inputs[i].rgb);
        memset(&sample->inputs[i], 0, sizeof(sample->inputs[i]));
        free(sample->nv12[i]);
        sample->nv12[i] = NULL;
    }
    sample->in_w = 0;
    sample->in_h = 0;
    sample->loaded = 0;
}

static edof_pair_t *get_loaded_edof_pair(int idx) {
    int seen = 0;
    for (size_t i = 0; i < sizeof(g_edof_pairs) / sizeof(g_edof_pairs[0]); ++i) {
        if (!g_edof_pairs[i].loaded) continue;
        if (seen == idx) return &g_edof_pairs[i];
        seen++;
    }
    return NULL;
}

static mcf_pair_t *get_loaded_mcf_pair(int idx) {
    int seen = 0;
    for (size_t i = 0; i < sizeof(g_mcf_pairs) / sizeof(g_mcf_pairs[0]); ++i) {
        if (!g_mcf_pairs[i].loaded) continue;
        if (seen == idx) return &g_mcf_pairs[i];
        seen++;
    }
    return NULL;
}

static void print_loop_asset_summary(void) {
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        const loop_asset_t *loop = &g_loop_assets[i];
        printf("%-8s %d/%d", loop->tile_name, loop->loaded_count, loop->path_count);
        for (int j = 0; j < loop->loaded_count; ++j) {
            printf("  %dx%d", loop->images[j].width, loop->images[j].height);
        }
        printf("\n");
    }
}

static int run_self_test(void) {
    int loaded = load_loop_assets();
    collect_health(loaded);

    printf("RKTohi AllDemo self-test\n");
    int failures = 0;
    failures += print_check("license", g_health.license_ok, LICENSE_PATH);
    failures += print_check("camera node", g_health.camera_node_ok, CAMERA_DEVICE);
    failures += print_check("drm card", g_health.drm_ok, "/dev/dri/card0");
    failures += print_check("media lib", g_health.media_lib_ok, "lib/libmedia.so lib/libmedia.a");
    failures += print_check("rtsp port", g_health.rtsp_port_free, "8554");
    failures += print_check("npu model", g_health.npu_model_ok, "assets/npu/yolov5s-640-640.rknn");
    failures += print_check("avm lut", g_health.avm_lut_ok, "assets/avm/avm_blend.lut");
    failures += print_check("pano calib", g_health.pano_calib_ok, g_pano_sample.pto_path);
    failures += print_check("svm assets", g_health.svm_assets_ok, "assets/svm3d/svm_3d_assets.json");

    char detail[64];
    snprintf(detail, sizeof(detail), "%d/%d decoded", g_health.loop_loaded, g_health.loop_expected);
    failures += print_check("loop assets", g_health.loop_loaded == g_health.loop_expected, detail);
    print_loop_asset_summary();

    unload_loop_assets();
    printf("summary: %s failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

static void mark_showcase_modules(void) {
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        int in_default = default_page_index(g_tiles[i].name) >= 0;
        g_tiles[i].active = in_default;
        g_tiles[i].status = in_default ? TILE_SYNTH : TILE_OFFLINE;
    }
}

static void probe_modules(void) {
    const int w = 320, h = 180, stride = 320;
    MEDIA_POOL_Create(WORK_POOL_NV12, stride * h * 3 / 2, 16);
    MEDIA_POOL_Create(WORK_POOL_RGB, w * h * 3, 8);
    MEDIA_POOL_Create(WORK_POOL_OUT, stride * h * 3 / 2, 16);

    uint64_t dummy = 0;
    if (MEDIA_SYS_GetVersion()) set_tile_status("LICENSE", TILE_PROBED);

    MEDIA_VPSS_ATTR vpss = {0};
    vpss.width = w; vpss.height = h; vpss.input_stride = stride; vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.input_depth = 2; vpss.output_count = 1;
    vpss.outputs[0].output_id = 0; vpss.outputs[0].out_width = w; vpss.outputs[0].out_height = h;
    vpss.outputs[0].out_stride = stride; vpss.outputs[0].pool_id = WORK_POOL_OUT;
    vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(20, &vpss) == 0) { set_tile_status("VPSS", TILE_PROBED); MEDIA_VPSS_DestroyGrp(20); }

    MEDIA_RGA_GRP_ATTR rga = {0};
    rga.algo = MEDIA_RGA_ALG_RESIZE; rga.input_count = 1; rga.output_count = 1; rga.input_depth = 2; rga.output_depth = 2;
    rga.inputs[0].port_id = 0; rga.inputs[0].width = w; rga.inputs[0].height = h; rga.inputs[0].format = MEDIA_FORMAT_NV12; rga.inputs[0].queue_depth = 2;
    rga.outputs[0].port_id = 0; rga.outputs[0].width = w; rga.outputs[0].height = h; rga.outputs[0].format = MEDIA_FORMAT_NV12; rga.outputs[0].pool_id = WORK_POOL_OUT;
    if (MEDIA_RGA_CreateGrp(20, &rga) == 0) { set_tile_status("RGA", TILE_PROBED); MEDIA_RGA_DestroyChn(20); }

    MEDIA_RESIZE_RGA_ATTR rz = {0};
    rz.input_format = MEDIA_FORMAT_NV12; rz.input_stride = stride; rz.input_depth = 2;
    rz.out_width = w; rz.out_height = h; rz.out_stride = stride; rz.output_format = MEDIA_FORMAT_NV12; rz.output_pool_id = WORK_POOL_OUT;
    if (MEDIA_RESIZE_RGA_CreateGrp(21, &rz) == 0) { set_tile_status("RESIZE_RGA", TILE_PROBED); MEDIA_RESIZE_RGA_DestroyGrp(21); }

    MEDIA_CSC_RGA_ATTR cr = {0};
    cr.input_width = w; cr.input_height = h; cr.input_format = MEDIA_FORMAT_NV12; cr.output_format = MEDIA_FORMAT_RGB888;
    cr.input_depth = 2; cr.output_pool_id = WORK_POOL_RGB; cr.input_stride = stride; cr.output_stride = w * 3;
    if (MEDIA_CSC_RGA_CreateGrp(22, &cr) == 0) { set_tile_status("CSC_RGA", TILE_PROBED); MEDIA_CSC_RGA_DestroyGrp(22); }

    MEDIA_CSC_CL_ATTR cc = {0};
    cc.input_width = w; cc.input_height = h; cc.input_format = MEDIA_FORMAT_NV12; cc.output_format = MEDIA_FORMAT_RGB888;
    cc.input_depth = 2; cc.output_pool_id = WORK_POOL_RGB; cc.input_stride = stride; cc.output_stride = w * 3;
    if (MEDIA_CSC_CL_CreateGrp(23, &cc) == 0) { set_tile_status("CSC_CL", TILE_PROBED); MEDIA_CSC_CL_DestroyGrp(23); }

    MEDIA_THERMAL_ATTR th = {0};
    th.width = w; th.height = h; th.format = MEDIA_FORMAT_NV12; th.color_mode = MEDIA_THERMAL_COLOR_RAINBOW3;
    th.input_depth = 2; th.output_depth = 2;
    if (MEDIA_THERMAL_CreateGrp(24, &th) == 0) { set_tile_status("THERMAL", TILE_PROBED); MEDIA_THERMAL_DestroyGrp(24); }

    MEDIA_OSD_ATTR osd = {0};
    osd.input_width = w; osd.input_height = h; osd.format = MEDIA_FORMAT_NV12; osd.input_depth = 2;
    osd.output_pool_id = WORK_POOL_OUT; osd.input_stride = stride; osd.output_stride = stride; osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(25, &osd) == 0) { set_tile_status("OSD", TILE_PROBED); MEDIA_OSD_DestroyGrp(25); }

    MEDIA_CONV_CL_ATTR conv = {0};
    conv.width = w; conv.height = h; conv.format = MEDIA_FORMAT_RGB888; conv.kernel_size = 5;
    conv.input_depth = 2; conv.output_pool_id = WORK_POOL_RGB; conv.input_stride = w * 3; conv.output_stride = w * 3;
    if (MEDIA_CONV_CL_CreateGrp(26, &conv) == 0) { set_tile_status("CONV_CL", TILE_PROBED); MEDIA_CONV_CL_DestroyGrp(26); }

    MEDIA_CLAHE_ATTR clahe = {0};
    clahe.width = w; clahe.height = h; clahe.format = MEDIA_FORMAT_RGB888; clahe.tile_grid_x = 8; clahe.tile_grid_y = 8;
    clahe.bins = 256; clahe.input_depth = 2; clahe.output_pool_id = WORK_POOL_RGB; clahe.input_stride = w * 3; clahe.output_stride = w * 3; clahe.clip_limit = 2.5f;
    if (MEDIA_CLAHE_CreateGrp(27, &clahe) == 0) { set_tile_status("CLAHE", TILE_PROBED); MEDIA_CLAHE_DestroyGrp(27); }

    MEDIA_RETINEX_ATTR ret = {0};
    ret.scale_count = 1; ret.width = w; ret.height = h; ret.format = MEDIA_FORMAT_NV12; ret.output_depth = 2; ret.input_depth = 2;
    ret.input_stride = stride; ret.output_stride = stride; ret.gain = 1.0f; ret.threshold = 0.01f; ret.log_min = 0.0f; ret.log_max = 1.0f;
    if (MEDIA_RETINEX_CreateGrp(28, &ret) == 0) { set_tile_status("RETINEX", TILE_PROBED); MEDIA_RETINEX_DestroyGrp(28); }

    MEDIA_CAP_DEHAZE_ATTR cap = {0};
    cap.width = w; cap.height = h; cap.format = MEDIA_FORMAT_RGB888; cap.input_depth = 2; cap.output_pool_id = WORK_POOL_RGB;
    cap.input_stride = w * 3; cap.output_stride = w * 3; cap.guided_radius = 8; cap.guided_eps = 0.01f; cap.t0 = 0.1f;
    if (MEDIA_CAP_DEHAZE_CreateGrp(29, &cap) == 0) { set_tile_status("CAP_DEHAZE", TILE_PROBED); MEDIA_CAP_DEHAZE_DestroyGrp(29); }

    MEDIA_DCP_FAST_DEHAZE_ATTR dcp = {0};
    dcp.width = w; dcp.height = h; dcp.format = MEDIA_FORMAT_RGB888; dcp.input_depth = 2; dcp.output_pool_id = WORK_POOL_RGB;
    dcp.input_stride = w * 3; dcp.output_stride = w * 3; dcp.patch = 15; dcp.omega = 0.95f; dcp.t0 = 0.1f; dcp.airlight_percent = 0.001f; dcp.guided_radius = 8; dcp.guided_eps = 0.01f; dcp.refine_scale = 0.25f;
    if (MEDIA_DCP_FAST_DEHAZE_CreateGrp(30, &dcp) == 0) { set_tile_status("DCP_FAST_DEHAZE", TILE_PROBED); MEDIA_DCP_FAST_DEHAZE_DestroyGrp(30); }

    MEDIA_BLEND_PYR_ATTR bp = {0};
    bp.width = w; bp.height = h; bp.input_stride = stride; bp.input_depth = 2; bp.input_format = MEDIA_FORMAT_NV12; bp.output_stride = stride;
    if (MEDIA_BLEND_PYR_SetAttr(31, &bp) == 0) { set_tile_status("BLEND_PYR", TILE_PROBED); MEDIA_BLEND_PYR_DestroyGrp(31); }

    MEDIA_EDOF_CL_ATTR edof = {0};
    edof.width = w; edof.height = h; edof.format = MEDIA_FORMAT_NV12; edof.focus_radius = 5; edof.input_depth = 2; edof.output_pool_id = WORK_POOL_OUT; edof.input_stride = stride; edof.output_stride = stride; edof.score_eps = 0.01f;
    if (MEDIA_EDOF_CL_CreateGrp(32, &edof) == 0) { set_tile_status("EDOF_CL", TILE_PROBED); MEDIA_EDOF_CL_DestroyGrp(32); }

    MEDIA_EXPOSURE_FUSION_CL_ATTR ex = {0};
    ex.width = w; ex.height = h; ex.format = MEDIA_FORMAT_NV12; ex.input_depth = 2; ex.output_pool_id = WORK_POOL_OUT; ex.input_stride = stride; ex.output_stride = stride;
    ex.contrast_power = 1.0f; ex.saturation_power = 1.0f; ex.exposedness_power = 1.0f; ex.sigma = 0.2f; ex.epsilon = 0.0001f;
    if (MEDIA_EXPOSURE_FUSION_CL_CreateGrp(33, &ex) == 0) { set_tile_status("EXPOSURE_FUSION_CL", TILE_PROBED); MEDIA_EXPOSURE_FUSION_CL_DestroyGrp(33); }

    MEDIA_MCF_FUSION_CL_ATTR mcf = {0};
    mcf.width = w; mcf.height = h; mcf.format = MEDIA_FORMAT_NV12; mcf.input_depth = 2; mcf.output_pool_id = WORK_POOL_OUT; mcf.input_stride = stride; mcf.output_stride = stride;
    mcf.path = MEDIA_MCF_FUSION_CL_PATH_FUSION; mcf.normalize_mode = MEDIA_MCF_FUSION_CL_NORMALIZE_MEAN_STD; mcf.blur_radius = 2;
    mcf.base_alpha = 0.25f; mcf.detail_gain = 0.80f; mcf.alpha_min = 0.15f; mcf.alpha_max = 0.85f; mcf.gain_min = 0.5f; mcf.gain_max = 2.0f; mcf.epsilon = 1e-6f;
    if (MEDIA_MCF_FUSION_CL_CreateGrp(38, &mcf) == 0) { set_tile_status("MCF_FUSION_CL", TILE_PROBED); MEDIA_MCF_FUSION_CL_DestroyGrp(38); }

    MEDIA_DUALVIEW_ATTR dv = {0};
    dv.input_width = w; dv.input_height = h; dv.input_stride = w * 3; dv.output_width = w * 2; dv.output_height = h; dv.output_stride = w * 6; dv.mode = MEDIA_DUALVIEW_MODE_SIDE_BY_SIDE;
    dv.format = MEDIA_FORMAT_RGB888; dv.input_depth = 2; dv.output_pool_id = WORK_POOL_RGB; dv.inputs[0].enabled = 1; dv.inputs[1].enabled = 1;
    if (MEDIA_DUALVIEW_CreateGrp(34, &dv) == 0) { set_tile_status("DUALVIEW", TILE_PROBED); MEDIA_DUALVIEW_DestroyGrp(34); }

    MEDIA_STEREO_3D_ATTR st = {0};
    st.width = w; st.height = h; st.format = MEDIA_FORMAT_NV12; st.input_depth = 2; st.output_pool_id = WORK_POOL_OUT; st.input_stride = stride; st.output_stride = stride; st.mode = MEDIA_STEREO_3D_MODE_SIDE_BY_SIDE;
    if (MEDIA_STEREO_3D_CreateGrp(35, &st) == 0) { set_tile_status("STEREO_3D", TILE_PROBED); MEDIA_STEREO_3D_DestroyGrp(35); }

    MEDIA_VMIX_ATTR vm = {0};
    vm.input_count = 2; vm.output_width = w; vm.output_height = h; vm.format = MEDIA_FORMAT_NV12; vm.input_depth = 2; vm.output_pool_id = WORK_POOL_OUT; vm.output_stride = stride; vm.primary_index = 0;
    vm.channels[0].enabled = 1; vm.channels[0].width = w; vm.channels[0].height = h; vm.channels[0].alpha = 1.0f; vm.channels[0].stride = stride;
    vm.channels[1].enabled = 1; vm.channels[1].x = w / 2; vm.channels[1].width = w / 2; vm.channels[1].height = h / 2; vm.channels[1].alpha = 0.6f; vm.channels[1].stride = stride;
    if (MEDIA_VMIX_CreateGrp(36, &vm) == 0) { set_tile_status("VMIX", TILE_PROBED); MEDIA_VMIX_DestroyGrp(36); }

    MEDIA_VMIX_RGA_ATTR vr = {0};
    vr.input_count = 2; vr.output_width = w; vr.output_height = h; vr.format = MEDIA_FORMAT_NV12; vr.input_depth = 2; vr.output_pool_id = WORK_POOL_OUT; vr.output_stride = stride; vr.primary_index = 0;
    vr.channels[0].enabled = 1; vr.channels[0].width = w; vr.channels[0].height = h; vr.channels[0].stride = stride; vr.channels[0].format = MEDIA_FORMAT_NV12;
    vr.channels[1].enabled = 1; vr.channels[1].x = w / 2; vr.channels[1].width = w / 2; vr.channels[1].height = h / 2; vr.channels[1].stride = stride; vr.channels[1].format = MEDIA_FORMAT_NV12;
    if (MEDIA_VMIX_RGA_CreateGrp(37, &vr) == 0) { set_tile_status("VMIX_RGA", TILE_PROBED); MEDIA_VMIX_RGA_DestroyGrp(37); }

    int wbc_connector = 0;
    int wbc_crtc = 0;
    if (MEDIA_VO_WBC_Probe(NULL, &wbc_connector, &wbc_crtc) == 0) {
        set_tile_status("WBC", TILE_PROBED);
    }

    MEDIA_VENC_ATTR venc = {0};
    venc.width = w; venc.height = h; venc.stride = stride; venc.fps = FPS; venc.buf_cnt = 4; venc.input_depth = 2; venc.bitrate = 1000000; venc.gop = FPS;
    venc.video_format = MEDIA_FORMAT_H264; venc.rc_mode = MEDIA_VENC_RC_CBR; venc.input_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VENC_SetAttr(38, &venc) == 0) { set_tile_status("VENC", TILE_PROBED); MEDIA_VENC_DestroyChn(38); }

    MEDIA_VDEC_ATTR vdec = {0};
    vdec.width = w; vdec.height = h; vdec.stride = stride; vdec.buf_cnt = 4; vdec.video_type = MEDIA_VIDEO_H264; vdec.pool_id = WORK_POOL_OUT; vdec.has_input_port = 1; vdec.input_depth = 2;
    if (MEDIA_VDEC_CreateChn(39, &vdec) == 0) { set_tile_status("VDEC", TILE_PROBED); MEDIA_VDEC_DestroyChn(39); }

    MEDIA_RTSP_SEND_ATTR tx = {0};
    tx.bind_addr = "0.0.0.0"; tx.port = 8554; tx.url_suffix = "rktohi_all"; tx.width = w; tx.height = h; tx.fps = FPS; tx.max_clients = 2; tx.transport_mode = RTSP_TRANSPORT_TCP; tx.video_format = MEDIA_FORMAT_H264;
    if (MEDIA_RTSP_SEND_CreateGrp(40, &tx) == 0) { set_tile_status("RTSP_SEND", TILE_PROBED); MEDIA_RTSP_SEND_DestroyGrp(40); }

    MEDIA_RTSP_RECV_ATTR rx = {0};
    rx.url = "rtsp://127.0.0.1:8554/rktohi_all"; rx.output_pool_id = WORK_POOL_OUT; rx.video_format = MEDIA_FORMAT_H264; rx.width = w; rx.height = h; rx.fps = FPS; rx.transport_mode = RTSP_TRANSPORT_TCP;
    if (MEDIA_RTSP_RECV_CreateGrp(41, &rx) == 0) { set_tile_status("RTSP_RECV", TILE_PROBED); MEDIA_RTSP_RECV_DestroyGrp(41); }

    MEDIA_PANO_ATTR pano = {0};
    pano.input_count = 2; pano.in_width = w; pano.in_height = h; pano.in_stride = stride; pano.out_width = w; pano.out_height = h; pano.out_stride = stride; pano.output_pool_id = WORK_POOL_OUT; pano.input_depth = 2; pano.output_depth = 2; pano.pto_path = "assets/panorama/calib_file.pto";
    if (MEDIA_PANO_CreateGrp(42, &pano) == 0) { set_tile_status("PANO", TILE_PROBED); MEDIA_PANO_DestroyGrp(42); }

    MEDIA_AVM_ATTR avm = {0};
    avm.input_count = 4; avm.in_width = w; avm.in_height = h; avm.in_stride = stride; avm.out_width = w; avm.out_height = h; avm.out_stride = stride; avm.output_pool_id = WORK_POOL_OUT; avm.input_depth = 2; avm.output_depth = 2; avm.lut_path = "assets/avm/avm_blend.lut";
    if (MEDIA_AVM_CreateGrp(43, &avm) == 0) { set_tile_status("AVM", TILE_PROBED); MEDIA_AVM_DestroyGrp(43); }

    MEDIA_SVM3D_ATTR svm = {0};
    svm.input_count = 4; svm.input_format = MEDIA_FORMAT_NV12; svm.in_width = w; svm.in_height = h; svm.in_stride = stride; svm.output_format = MEDIA_FORMAT_RGB888; svm.out_width = w; svm.out_height = h; svm.out_stride = w * 3; svm.output_pool_id = WORK_POOL_RGB; svm.input_depth = 2; svm.output_depth = 2; svm.asset_path = "assets/svm3d/svm_3d_assets.json";
    if (MEDIA_SVM3D_CreateGrp(44, &svm) == 0) { set_tile_status("SVM3D", TILE_PROBED); MEDIA_SVM3D_DestroyGrp(44); }

    MEDIA_NPU_ATTR npu = {0};
    npu.model_path = "assets/npu/yolov5s-640-640.rknn"; npu.backend = MEDIA_NPU_BACKEND_RKNN; npu.task = MEDIA_NPU_TASK_DETECT;
    npu.input_width = 640; npu.input_height = 640; npu.input_format = MEDIA_FORMAT_RGB888; npu.input_layout = MEDIA_NPU_LAYOUT_NHWC; npu.input_depth = 2; npu.passthrough = 1; npu.score_thresh = 0.25f; npu.nms_thresh = 0.45f;
    if (MEDIA_NPU_CreateGrp(45, &npu) == 0) { set_tile_status("NPU", TILE_PROBED); MEDIA_NPU_DestroyGrp(45); }

    set_tile_status("PIC_IO", TILE_PROBED);
    (void)dummy;
}

static int active_module_count(void) {
    int total = 0;
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (g_tiles[i].active) total++;
    }
    return total;
}

static int read_cpu_times(unsigned long long *idle, unsigned long long *total) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;
    char line[256];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    unsigned long long user = 0, nice = 0, system = 0, idle_v = 0, iowait = 0;
    unsigned long long irq = 0, softirq = 0, steal = 0, guest = 0, guest_nice = 0;
    int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle_v, &iowait, &irq, &softirq,
                   &steal, &guest, &guest_nice);
    if (n < 4) return -1;
    *idle = idle_v + iowait;
    *total = user + nice + system + idle_v + iowait + irq + softirq + steal + guest + guest_nice;
    return 0;
}

static int read_first_line(const char *path, char *buf, size_t len) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (!fgets(buf, len, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int read_gpu_percent_file(const char *path, float *percent) {
    char buf[128];
    char *end = NULL;
    if (read_first_line(path, buf, sizeof(buf)) != 0) return -1;
    errno = 0;
    float v = strtof(buf, &end);
    if (errno != 0 || end == buf) return -1;
    if (v < 0.0f) return -1;
    if (v > 100.0f && v <= 1000.0f) v /= 10.0f;
    if (v > 100.0f) v = 100.0f;
    *percent = v;
    return 0;
}

static int find_gpu_load_path(char *path, size_t len) {
    static const char *candidates[] = {
        "/sys/class/devfreq/fb000000.gpu/load",
        "/sys/class/devfreq/gpu/load",
        "/sys/class/misc/mali0/device/utilization",
        "/sys/kernel/debug/mali0/gpu_usage",
    };
    float dummy = 0.0f;
    for (size_t i = 0; i < ARRAY_SIZE(candidates); ++i) {
        if (read_gpu_percent_file(candidates[i], &dummy) == 0) {
            snprintf(path, len, "%s", candidates[i]);
            return 0;
        }
    }

    DIR *dir = opendir("/sys/class/devfreq");
    if (!dir) return -1;
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!strstr(ent->d_name, "gpu") && !strstr(ent->d_name, "mali")) continue;
        char candidate[256];
        snprintf(candidate, sizeof(candidate), "/sys/class/devfreq/%s/load", ent->d_name);
        if (read_gpu_percent_file(candidate, &dummy) == 0) {
            snprintf(path, len, "%s", candidate);
            closedir(dir);
            return 0;
        }
        snprintf(candidate, sizeof(candidate), "/sys/class/devfreq/%s/device/load", ent->d_name);
        if (read_gpu_percent_file(candidate, &dummy) == 0) {
            snprintf(path, len, "%s", candidate);
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1;
}

static int read_rga_percent_file(const char *path, float *percent) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    char line[256];
    float max_load = 0.0f;
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *p = strstr(line, "load");
        if (!p) continue;
        p = strchr(p, '=');
        if (!p) continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        char *end = NULL;
        errno = 0;
        float v = strtof(p, &end);
        if (errno == 0 && end != p && v >= 0.0f) {
            if (v > 100.0f) v = 100.0f;
            if (v > max_load) max_load = v;
            count++;
        }
    }
    fclose(fp);
    if (count <= 0) return -1;
    *percent = max_load;
    return 0;
}

static void update_perf_status(void) {
    static unsigned long long prev_idle = 0;
    static unsigned long long prev_total = 0;
    static int have_cpu = 0;
    static char gpu_path[256] = {0};
    static int gpu_path_checked = 0;

    unsigned long long idle = 0, total = 0;
    if (read_cpu_times(&idle, &total) == 0) {
        if (have_cpu && total > prev_total) {
            unsigned long long total_delta = total - prev_total;
            unsigned long long idle_delta = idle - prev_idle;
            if (total_delta > 0 && idle_delta <= total_delta) {
                g_perf.cpu_percent = (float)(total_delta - idle_delta) * 100.0f / (float)total_delta;
            }
        }
        prev_idle = idle;
        prev_total = total;
        have_cpu = 1;
    }

    if (!gpu_path_checked) {
        gpu_path_checked = 1;
        if (find_gpu_load_path(gpu_path, sizeof(gpu_path)) != 0) {
            gpu_path[0] = '\0';
        }
    }
    if (gpu_path[0]) {
        float gpu = 0.0f;
        if (read_gpu_percent_file(gpu_path, &gpu) == 0) {
            g_perf.gpu_percent = gpu;
            g_perf.gpu_available = 1;
        } else {
            g_perf.gpu_available = 0;
        }
    } else {
        g_perf.gpu_available = 0;
    }

    float rga = 0.0f;
    if (read_rga_percent_file("/sys/kernel/debug/rkrga/load", &rga) == 0) {
        g_perf.rga_percent = rga;
        g_perf.rga_available = 1;
    } else {
        g_perf.rga_available = 0;
    }
}

static void format_runtime_status(char *buf, size_t len) {
    char gpu_text[24];
    char rga_text[24];

    if (g_perf.gpu_available) {
        snprintf(gpu_text, sizeof(gpu_text), "%.0f%%", g_perf.gpu_percent);
    } else {
        snprintf(gpu_text, sizeof(gpu_text), "N/A");
    }
    if (g_perf.rga_available) {
        snprintf(rga_text, sizeof(rga_text), "%.0f%%", g_perf.rga_percent);
    } else {
        snprintf(rga_text, sizeof(rga_text), "N/A");
    }

    snprintf(buf, len, "CPU %.0f%%  GPU %s  RGA %s",
             g_perf.cpu_percent, gpu_text, rga_text);
}

static const char *module_flow_note(const char *name) {
    if (!name) return "数据流：模块输入 -> 算法处理 -> 显示输出。";
    if (strcasecmp(name, "VI") == 0) return "数据流：CAM 640x640 -> VI -> VMIX -> OSD -> VO。";
    if (strcasecmp(name, "VPSS") == 0) return "数据流：VI 640x640 NV12 -> VPSS四路裁剪/缩放 -> 同屏展示。";
    if (strcasecmp(name, "VO") == 0) return "数据流：合成NV12画面送入VO，输出到MIPI/DSI屏。";
    if (strcasecmp(name, "WBC") == 0) return "数据流：VI参考窗 + VO_WBC回抓 -> RGA缩放 -> VMIX/OSD -> VO。";
    if (strcasecmp(name, "OSD") == 0) return "数据流：VI 3840x2160 -> RESIZE_RGA 1080x608 -> OSD动态图层 -> VMIX -> VO。";
    if (strcasecmp(name, "RGA") == 0) return "数据流：VI 640x640 NV12 -> RGA裁剪/缩放/旋转 -> VO。";
    if (strcasecmp(name, "RESIZE_RGA") == 0) return "数据流：VI 640x640 NV12 -> RESIZE_RGA裁剪放大 -> VO。";
    if (strcasecmp(name, "CSC_RGA") == 0) return "数据流：NV12和ARGB互转，展示RGA颜色转换链路。";
    if (strcasecmp(name, "CSC_CL") == 0) return "数据流：VI 3840x2160 -> CSC_CL 4K ARGB -> RESIZE_RGA 1080x1920 -> OSD -> VO。";
    if (strcasecmp(name, "CLAHE") == 0) return "数据流：VI 3840x2160 -> CLAHE增强 -> RESIZE_RGA缩放 -> 上下对比显示。";
    if (strcasecmp(name, "RETINEX") == 0) return "数据流：VI 3840x2160 -> RETINEX校正/直通 -> RESIZE_RGA缩放 -> 单路显示。";
    if (strcasecmp(name, "CAP_DEHAZE") == 0) return "数据流：VI 640x640 -> RGB -> CAP_DEHAZE，下方输出去雾效果。";
    if (strcasecmp(name, "DCP_FAST_DEHAZE") == 0) return "数据流：VI 640x640 -> RGB -> DCP_FAST_DEHAZE，下方输出去雾效果。";
    if (strcasecmp(name, "THERMAL") == 0) return "数据流：同一红外灰度图映射到多种热成像伪彩模式。";
    if (strcasecmp(name, "CONV_CL") == 0) return "数据流：VI 640x640 -> RGBA -> CONV_CL四核卷积 -> 2x2输出。";
    if (strcasecmp(name, "TRANSFORM") == 0) return "数据流：VI 3840x2160 -> TRANSFORM LUT -> RESIZE_RGA缩放 -> 四宫格显示。";
    if (strcasecmp(name, "VMIX") == 0) return "数据流：VI 640x640 -> VPSS四路 -> VMIX固定2x2合成 -> VO。";
    if (strcasecmp(name, "EDOF_CL") == 0) return "数据流：近焦图和远焦图进入EDOF_CL，输出融合清晰图。";
    if (strcasecmp(name, "MCF_FUSION_CL") == 0) return "数据流：彩色图和单色细节图进入MCF_FUSION_CL，输出彩色细节增强图。";
    if (strcasecmp(name, "DUALVIEW") == 0) return "数据流：两路RGB输入生成左右并排和逐行交错输出。";
    if (strcasecmp(name, "STEREO_3D") == 0) return "数据流：VI经VPSS分两路，原图和旋转图进入STEREO_3D合并输出。";
    if (strcasecmp(name, "PANO") == 0) return "数据流：六路图像按PTO标定查表拼接成全景输出。";
    if (strcasecmp(name, "NPU") == 0) return "数据流：H264解码，经RGA预处理后送NPU检测并叠框。";
    return "数据流：模块输入 -> 算法处理 -> 显示输出。";
}

static const char *module_showcase_note(const char *name) {
    if (!name) return "展示重点：观察输入、处理结果和实时性能。";
    if (strcasecmp(name, "VI") == 0) return "展示重点：确认摄像头输入稳定，显示真实采集链路。";
    if (strcasecmp(name, "VPSS") == 0) return "展示重点：同一输入同时生成缩放、裁剪、翻转和放大输出。";
    if (strcasecmp(name, "VO") == 0) return "展示重点：验证显示输出控制和上屏状态。";
    if (strcasecmp(name, "WBC") == 0) return "展示重点：从当前显示回抓画面并和实时输入参考窗对照。";
    if (strcasecmp(name, "OSD") == 0) return "展示重点：动态叠加图层、状态文字和扫描提示。";
    if (strcasecmp(name, "RGA") == 0) return "展示重点：硬件完成裁剪、缩放、旋转和快速搬运。";
    if (strcasecmp(name, "RESIZE_RGA") == 0) return "展示重点：移动裁剪窗口并实时硬件放大。";
    if (strcasecmp(name, "CSC_RGA") == 0) return "展示重点：RGA颜色格式转换链路，NV12与ARGB往返。";
    if (strcasecmp(name, "CSC_CL") == 0) return "展示重点：GPU/OpenCL颜色矩阵转换和耗时指标。";
    if (strcasecmp(name, "CLAHE") == 0) return "展示重点：上下对比局部对比度增强效果。";
    if (strcasecmp(name, "RETINEX") == 0) return "展示重点：1秒直通、1秒增强，观察光照校正和暗部细节增强。";
    if (strcasecmp(name, "CAP_DEHAZE") == 0) return "展示重点：实时去雾前后对比，突出低能见度细节。";
    if (strcasecmp(name, "DCP_FAST_DEHAZE") == 0) return "展示重点：暗通道快速去雾前后对比。";
    if (strcasecmp(name, "THERMAL") == 0) return "展示重点：同一灰度输入映射为多种热成像色表。";
    if (strcasecmp(name, "CONV_CL") == 0) return "展示重点：四路GPU卷积同屏比较。";
    if (strcasecmp(name, "TRANSFORM") == 0) return "展示重点：LUT畸变矫正、旋转缩放和透视变换。";
    if (strcasecmp(name, "VMIX") == 0) return "展示重点：四路实时输入由VMIX合成，观察位置、层级和alpha效果。";
    if (strcasecmp(name, "EDOF_CL") == 0) return "展示重点：近焦和远焦图融合成更清晰输出。";
    if (strcasecmp(name, "MCF_FUSION_CL") == 0) return "展示重点：单色高频细节注入彩色图，观察纹理增强和GPU耗时。";
    if (strcasecmp(name, "DUALVIEW") == 0) return "展示重点：两路输入生成左右并排和逐行交错输出。";
    if (strcasecmp(name, "STEREO_3D") == 0) return "展示重点：原图和旋转分支合并为一个3D格式输出。";
    if (strcasecmp(name, "PANO") == 0) return "展示重点：六张标定图拼接为完整全景。";
    if (strcasecmp(name, "NPU") == 0) return "展示重点：检测框、类别和NPU帧计数叠加显示。";
    return "展示重点：观察输入、处理结果和实时性能。";
}

static void draw_single_module_page(uint8_t *canvas, int stride, int frame,
                                    const char *title, int page, int total_pages,
                                    const char *name, const display_refs_t *refs) {
    int idx = find_tile_index(name);
    char runtime[128];
    char page_text[64];
    const char *mode = showcase_nav_mode();
    (void)title;

    format_runtime_status(runtime, sizeof(runtime));
    fill_rect_nv12(canvas, stride, 0, 0, SCREEN_W, SCREEN_H, 4, 9, 16);
    fill_rect_nv12(canvas, stride, 0, 0, SCREEN_W, 96, 5, 10, 18);
    fill_rect_nv12(canvas, stride, 0, SCREEN_H - 112, SCREEN_W, 112, 5, 10, 18);

    if (idx >= 0) {
        uint8_t sr, sg, sb;
        tile_status_color(g_tiles[idx].status, &sr, &sg, &sb);
        draw_text(canvas, stride, 32, 24, g_tiles[idx].name,
                  strlen(g_tiles[idx].name) > 11 ? 2 : 3, 160, 255, 220);
        snprintf(page_text, sizeof(page_text), "PAGE %02d/%02d", page + 1, total_pages);
        draw_text(canvas, stride, 838, 28, page_text, 2, 160, 255, 220);
        if (mode) {
            draw_text(canvas, stride, 838, 62, mode, 1, 255, 230, 120);
        }
        draw_utf8_text(canvas, stride, 32, 64, module_showcase_note(g_tiles[idx].name),
                       20, 210, 235, 255);
        draw_text(canvas, stride, 32, SCREEN_H - 72, runtime, 2, 255, 230, 120);
        draw_tile_content(canvas, stride, 28, 118, 1024, 1660,
                          idx, frame, refs->cam, refs->osd_live, refs->resize_live,
                          refs->vpss_live, refs->csc_rga_live, refs->transform_live,
                          refs->cap_live, refs->dcp_live, refs->conv_live,
                          refs->clahe_live, refs->retinex_live, refs->pano_out,
                          refs->edof_in0, refs->edof_in1, refs->edof_out,
                          refs->mcf_color, refs->mcf_mono, refs->mcf_out,
                          refs->stereo_live, refs->dual_in0, refs->dual_in1,
                          refs->dual_sbs, refs->dual_lbl);
        draw_text(canvas, stride, 838, SCREEN_H - 40, tile_status_text(g_tiles[idx].status),
                  2, sr, sg, sb);
        draw_utf8_text(canvas, stride, 32, SCREEN_H - 40, module_flow_note(g_tiles[idx].name),
                       20, 210, 235, 255);
    } else {
        draw_text(canvas, stride, 32, 24, "UNKNOWN MODULE", 3, 255, 180, 80);
        draw_text(canvas, stride, 320, 850, "UNKNOWN MODULE", 4, 255, 180, 80);
    }
}

static void draw_showcase_page_refs(uint8_t *canvas, int stride, int frame, int rotate_main,
                                    const char *only_tile, const display_refs_t *refs) {
    if (only_tile) {
        int loop_page = module_page_number(only_tile) - 1;
        int loop_total = module_page_total();
        if (loop_page < 0) loop_page = 0;
        draw_single_module_page(canvas, stride, frame, "MANUAL MODULE SCREEN",
                                loop_page, loop_total, only_tile, refs);
    } else {
        int total_pages = (int)ARRAY_SIZE(g_default_pages);
        int page = rotate_main ?
            (frame / (FPS * PAGE_ROTATE_SECONDS)) % total_pages : 0;
        const char *name = g_default_pages[page];
        if (strcasecmp(name, "VPSS") == 0) {
            draw_vpss_bind_style_page(canvas, stride, frame, page, total_pages);
            return;
        } else {
            draw_single_module_page(canvas, stride, frame, "SHOWCASE SCREEN",
                                    page, total_pages, name, refs);
        }
    }
}

static void draw_showcase_page(uint8_t *canvas, int stride, int frame, int rotate_main,
                               const char *only_tile, const uint8_t *cam, const uint8_t *osd_live,
                               const uint8_t *resize_live, const uint8_t *vpss_live,
                               const uint8_t *csc_rga_live, const uint8_t *transform_live,
                               const uint8_t *cap_live, const uint8_t *dcp_live,
                               const uint8_t *conv_live, const uint8_t *clahe_live,
                               const uint8_t *retinex_live,
                               const uint8_t *pano_out,
                               const uint8_t *edof_in0, const uint8_t *edof_in1,
                               const uint8_t *edof_out,
                               const uint8_t *mcf_color, const uint8_t *mcf_mono,
                               const uint8_t *mcf_out, const uint8_t *stereo_live,
                               const uint8_t *dual_in0, const uint8_t *dual_in1,
                               const uint8_t *dual_sbs, const uint8_t *dual_lbl) {
    display_refs_t refs = {
        cam, osd_live, resize_live, vpss_live, csc_rga_live, transform_live,
        cap_live, dcp_live, conv_live, clahe_live, retinex_live, pano_out,
        edof_in0, edof_in1, edof_out, mcf_color, mcf_mono, mcf_out,
        stereo_live, dual_in0, dual_in1, dual_sbs, dual_lbl
    };
    draw_showcase_page_refs(canvas, stride, frame, rotate_main, only_tile, &refs);
}

static void draw_solid_test(uint8_t *canvas, int stride) {
    fill_rect_nv12(canvas, stride, 0, 0, SCREEN_W, SCREEN_H, 8, 16, 24);
    fill_rect_nv12(canvas, stride, 0, 0, SCREEN_W / 3, SCREEN_H, 180, 40, 50);
    fill_rect_nv12(canvas, stride, SCREEN_W / 3, 0, SCREEN_W / 3, SCREEN_H, 40, 170, 90);
    fill_rect_nv12(canvas, stride, SCREEN_W * 2 / 3, 0, SCREEN_W / 3, SCREEN_H, 50, 100, 220);
    fill_rect_nv12(canvas, stride, 80, 160, 920, 240, 8, 16, 24);
    stroke_rect_nv12(canvas, stride, 80, 160, 920, 240, 4, 240, 240, 240);
    draw_text(canvas, stride, 130, 230, "SOLID DISPLAY TEST", 5, 255, 255, 255);
    draw_text(canvas, stride, 150, 330, "NO CAMERA  NO ASSETS  NO ANIMATION", 2, 230, 245, 255);
}

static int vo_demo_state_for_frame(int frame) {
    int t = frame % (FPS * 18);
    if (t < FPS * 4) return VO_DEMO_NORMAL;
    if (t < FPS * 7) return VO_DEMO_FREEZE_MAIN;
    if (t < FPS * 10) return VO_DEMO_NORMAL;
    if (t < FPS * 13) return VO_DEMO_FREEZE_PLANE;
    if (t < FPS * 16) return VO_DEMO_NORMAL;
    if (t < FPS * 17) return VO_DEMO_HIDE_WARN;
    return VO_DEMO_HIDE_PLANE;
}

static void vo_demo_restore_output(void) {
    (void)MEDIA_VO_FreezeMain(0);
    (void)MEDIA_VO_UnfreezeMain();
    (void)MEDIA_VO_FreezePlane(0, 0, 0);
    (void)MEDIA_VO_HidePlane(0, 0, 0);
}

static int vo_demo_prepare(int frame) {
    int state = vo_demo_state_for_frame(frame);
    int pending_apply = 0;
    g_vo_demo_phase_frame = frame;

    if (state != g_vo_demo_prev_state) {
        if (g_vo_demo_prev_state == VO_DEMO_FREEZE_MAIN ||
            g_vo_demo_prev_state == VO_DEMO_FREEZE_PLANE ||
            g_vo_demo_prev_state == VO_DEMO_HIDE_PLANE) {
            vo_demo_restore_output();
        }
        pending_apply = 1;
        g_vo_demo_prev_state = state;
    }

    g_vo_demo_state = state;
    switch (state) {
    case VO_DEMO_FREEZE_MAIN:
        g_vo_demo_api = "MEDIA_VO_FreezeMain(1)";
        g_vo_demo_status = "FREEZE MAIN";
        break;
    case VO_DEMO_FREEZE_PLANE:
        g_vo_demo_api = "MEDIA_VO_FreezePlane(0,0,1)";
        g_vo_demo_status = "FREEZE PLANE";
        break;
    case VO_DEMO_HIDE_WARN:
        g_vo_demo_api = "MEDIA_VO_HidePlane(0,0,1)";
        g_vo_demo_status = "HIDE WARNING";
        break;
    case VO_DEMO_HIDE_PLANE:
        g_vo_demo_api = "MEDIA_VO_HidePlane(0,0,1)";
        g_vo_demo_status = "HIDE PLANE";
        break;
    case VO_DEMO_NORMAL:
    default:
        g_vo_demo_api = "MEDIA_SYS_SendFrame";
        g_vo_demo_status = "NORMAL REFRESH";
        break;
    }
    return pending_apply;
}

static void vo_demo_apply_after_send(void) {
    if (g_vo_demo_state == VO_DEMO_FREEZE_MAIN) {
        (void)MEDIA_VO_FreezeMain(1);
    } else if (g_vo_demo_state == VO_DEMO_FREEZE_PLANE) {
        (void)MEDIA_VO_FreezePlane(0, 0, 1);
    } else if (g_vo_demo_state == VO_DEMO_HIDE_PLANE) {
        (void)MEDIA_VO_HidePlane(0, 0, 1);
    }
}

int main(int argc, char **argv) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGALRM, on_signal);

    int heavy_probe = 0;
    int asset_check = 0;
    int self_test = 0;
    int solid_test = 0;
    int rotate_main = 1;
    const demo_loop_t *demo_loop = &g_customer_loop;
    const char *only_tile = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--probe") == 0) heavy_probe = 1;
        if (strcmp(argv[i], "--asset-check") == 0) asset_check = 1;
        if (strcmp(argv[i], "--self-test") == 0) self_test = 1;
        if (strcmp(argv[i], "--solid-test") == 0) solid_test = 1;
        if (strcmp(argv[i], "--customer-demo") == 0) demo_loop = &g_customer_loop;
        if (strcmp(argv[i], "--engineering-demo") == 0) demo_loop = &g_engineering_loop;
        if (strcmp(argv[i], "--no-rotate-main") == 0) rotate_main = 0;
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only_tile = argv[++i];
            rotate_main = 0;
        }
    }
    only_tile = canonical_tile_name(only_tile);

    if (self_test) {
        return run_self_test();
    }

    if (asset_check) {
        int loaded = load_loop_assets();
        print_loop_asset_summary();
        unload_loop_assets();
        return loaded > 0 ? 0 : 1;
    }

    if (only_tile && find_tile_index(only_tile) < 0) {
        fprintf(stderr, "unknown tile for --only: %s\n", only_tile);
        return 1;
    }

    int use_4k_camera_input = !solid_test && only_tile && tile_uses_4k_camera_input(only_tile);
    g_camera_input_w = use_4k_camera_input ? VI_BIG_W : CAM_W;
    g_camera_input_h = use_4k_camera_input ? VI_BIG_H : CAM_H;
    g_camera_input_stride = use_4k_camera_input ? VI_BIG_STRIDE : CAM_STRIDE;

    if (!only_tile && !solid_test && !heavy_probe) {
        return run_default_only_loop(argv[0], rotate_main, demo_loop);
    }

    const int dstride = ALIGN_UP(SCREEN_W, 64);
    const size_t display_size = (size_t)dstride * SCREEN_H * 3 / 2;
    const int thermal_only_page = only_tile && strcasecmp(only_tile, "THERMAL") == 0;
    const int edof_only_page = only_tile && strcasecmp(only_tile, "EDOF_CL") == 0;
    const int mcf_only_page = only_tile && strcasecmp(only_tile, "MCF_FUSION_CL") == 0;
    const int pano_only_page = only_tile && strcasecmp(only_tile, "PANO") == 0;

    if (MEDIA_SYS_Init() != 0) {
        fprintf(stderr, "MEDIA_SYS_Init failed\n");
        return 1;
    }
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (!solid_test) mark_showcase_modules();
    int loaded_assets = solid_test ? 0 : load_loop_assets();
    int loaded_edof_pairs = (!solid_test && (!only_tile || strcasecmp(only_tile, "EDOF_CL") == 0)) ?
        load_edof_pairs() : 0;
    int loaded_mcf_pairs = (!solid_test && (!only_tile || strcasecmp(only_tile, "MCF_FUSION_CL") == 0)) ?
        load_mcf_pairs() : 0;
    int loaded_pano_sample = (!solid_test && (!only_tile || strcasecmp(only_tile, "PANO") == 0)) ?
        load_pano_sample() : 0;
    collect_health(loaded_assets);
    if (heavy_probe && !solid_test) {
        probe_modules();
    }
    const char *edof_live_env = getenv("ALLDEMO_EDOF_CL_LIVE");
    int edof_live_requested = edof_live_env && strcmp(edof_live_env, "0") != 0;
    const char *pano_live_env = getenv("ALLDEMO_PANO_LIVE");
    int pano_live_requested = pano_live_env && strcmp(pano_live_env, "0") != 0;

    if (!solid_test && only_tile && strcasecmp(only_tile, "NPU") == 0) {
        int npu_ret = run_only_npu_vdec_demo(dstride, display_size);
        unload_pano_sample();
        unload_mcf_pairs();
        unload_edof_pairs();
        unload_loop_assets();
        MEDIA_SYS_Exit();
        return npu_ret;
    }

    int live_osd_ok = 0;
    if (!solid_test && !only_tile &&
        setup_live_osd() == 0) {
        live_osd_ok = 1;
    }
    int live_resize_ok = 0;
    if (!solid_test && !only_tile &&
        setup_live_resize() == 0) {
        live_resize_ok = 1;
    }
    int live_vpss_ok = 0;
    if (!solid_test && (!only_tile ||
                        strcasecmp(only_tile, "VPSS") == 0 ||
                        strcasecmp(only_tile, "VMIX") == 0) &&
        setup_live_vpss() == 0) {
        live_vpss_ok = 1;
    }
    int live_csc_rga_ok = 0;
    if (!solid_test && !only_tile && default_page_index("CSC_RGA") >= 0 &&
        setup_live_csc_rga() == 0) {
        live_csc_rga_ok = 1;
    }
    int live_transform_ok = 0;
    int live_cap_ok = 0;
    if (!solid_test && !only_tile && default_page_index("CAP_DEHAZE") >= 0 &&
        setup_live_cap_dehaze() == 0) {
        live_cap_ok = 1;
    }
    int live_dcp_ok = 0;
    if (!solid_test && !only_tile && default_page_index("DCP_FAST_DEHAZE") >= 0 &&
        setup_live_dcp_dehaze() == 0) {
        live_dcp_ok = 1;
    }
    int live_conv_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "CONV_CL") == 0 &&
        setup_live_conv_cl() == 0) {
        live_conv_ok = 1;
    }
    int live_clahe_ok = 0;
    if (!solid_test && !only_tile && default_page_index("CLAHE") >= 0 &&
        setup_live_clahe() == 0) {
        live_clahe_ok = 1;
    }
    int live_retinex_ok = 0;
    if (!solid_test && !only_tile && default_page_index("RETINEX") >= 0 &&
        setup_live_retinex() == 0) {
        live_retinex_ok = 1;
    }
    int live_edof_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "EDOF_CL") == 0 &&
        loaded_edof_pairs > 0 && edof_live_requested && setup_live_edof() == 0) {
        live_edof_ok = 1;
    } else if (!solid_test && loaded_edof_pairs > 0) {
        set_tile_status("EDOF_CL", TILE_LOOP);
    }
    int live_mcf_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "MCF_FUSION_CL") == 0 &&
        loaded_mcf_pairs > 0 && setup_live_mcf() == 0) {
        live_mcf_ok = 1;
    } else if (!solid_test && loaded_mcf_pairs > 0) {
        set_tile_status("MCF_FUSION_CL", TILE_LOOP);
    }
    int live_dualview_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "DUALVIEW") == 0 &&
        setup_live_dualview() == 0) {
        live_dualview_ok = 1;
    }
    int live_pano_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "PANO") == 0 &&
        loaded_pano_sample > 0 && pano_live_requested && setup_live_pano() == 0) {
        live_pano_ok = 1;
    } else if (!solid_test && only_tile && strcasecmp(only_tile, "PANO") == 0 &&
               loaded_pano_sample > 0) {
        set_tile_status("PANO", TILE_LOOP);
    }
    int live_stereo_ok = 0;
    int live_rga_ok = 0;

    if (MEDIA_POOL_Create(DISPLAY_POOL, display_size, 4) != 0) {
        fprintf(stderr, "display pool create failed\n");
        cleanup_live_stereo(live_stereo_ok);
        cleanup_live_pano(live_pano_ok);
        cleanup_live_dualview(live_dualview_ok);
        cleanup_live_mcf(live_mcf_ok);
        cleanup_live_edof(live_edof_ok);
        cleanup_live_retinex(live_retinex_ok);
        cleanup_live_clahe(live_clahe_ok);
        cleanup_live_conv_cl(live_conv_ok);
        cleanup_live_dcp_dehaze(live_dcp_ok);
        cleanup_live_cap_dehaze(live_cap_ok);
        cleanup_live_transform(live_transform_ok);
        cleanup_live_csc_rga(live_csc_rga_ok);
        cleanup_live_rga(live_rga_ok);
        cleanup_live_vpss(live_vpss_ok);
        cleanup_live_resize(live_resize_ok);
        cleanup_live_osd(live_osd_ok);
        unload_pano_sample();
        unload_mcf_pairs();
        unload_edof_pairs();
        unload_loop_assets();
        MEDIA_SYS_Exit();
        return 1;
    }

    MEDIA_VO_ATTR vo = {0};
    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = SCREEN_W;
    vo.height = SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0,
                           0,
                           0,
                           SCREEN_W,
                           SCREEN_H,
                           only_tile && strcasecmp(only_tile, "CSC_CL") == 0 ? CSC_CL_VIEW_STRIDE : dstride,
                           4, MEDIA_VO_PLANE_TYPE_AUTO,
                           only_tile && strcasecmp(only_tile, "CSC_CL") == 0 ? MEDIA_FORMAT_ARGB8888 : MEDIA_FORMAT_NV12) != 0 ||
        MEDIA_VO_Start(0, 0) != 0) {
        fprintf(stderr, "VO setup failed\n");
        MEDIA_POOL_Destroy(DISPLAY_POOL);
        cleanup_live_stereo(live_stereo_ok);
        cleanup_live_pano(live_pano_ok);
        cleanup_live_dualview(live_dualview_ok);
        cleanup_live_mcf(live_mcf_ok);
        cleanup_live_edof(live_edof_ok);
        cleanup_live_retinex(live_retinex_ok);
        cleanup_live_clahe(live_clahe_ok);
        cleanup_live_conv_cl(live_conv_ok);
        cleanup_live_dcp_dehaze(live_dcp_ok);
        cleanup_live_cap_dehaze(live_cap_ok);
        cleanup_live_transform(live_transform_ok);
        cleanup_live_csc_rga(live_csc_rga_ok);
        cleanup_live_rga(live_rga_ok);
        cleanup_live_vpss(live_vpss_ok);
        cleanup_live_resize(live_resize_ok);
        cleanup_live_osd(live_osd_ok);
        unload_pano_sample();
        unload_mcf_pairs();
        unload_edof_pairs();
        unload_loop_assets();
        MEDIA_SYS_Exit();
        return 1;
    }
    set_tile_status("VO", TILE_LIVE);

    int vo_demo_enabled = !solid_test && only_tile && strcasecmp(only_tile, "VO") == 0;
    if (vo_demo_enabled) {
        vo_demo_restore_output();
        g_vo_demo_prev_state = -1;
    }

    int use_vmix_osd_display = !solid_test && only_tile &&
        (strcasecmp(only_tile, "VI") == 0 ||
         strcasecmp(only_tile, "VPSS") == 0 ||
         strcasecmp(only_tile, "OSD") == 0 ||
         strcasecmp(only_tile, "WBC") == 0 ||
         strcasecmp(only_tile, "RGA") == 0 ||
         strcasecmp(only_tile, "RESIZE_RGA") == 0 ||
         strcasecmp(only_tile, "VMIX") == 0 ||
         strcasecmp(only_tile, "CSC_RGA") == 0 ||
         strcasecmp(only_tile, "CSC_CL") == 0 ||
         strcasecmp(only_tile, "TRANSFORM") == 0 ||
         strcasecmp(only_tile, "STEREO_3D") == 0 ||
         strcasecmp(only_tile, "CLAHE") == 0 ||
         strcasecmp(only_tile, "RETINEX") == 0 ||
         strcasecmp(only_tile, "CAP_DEHAZE") == 0 ||
         strcasecmp(only_tile, "DCP_FAST_DEHAZE") == 0);
    int use_vi_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "VI") == 0;
    int use_wbc_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "WBC") == 0;
    int use_vmix_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "VMIX") == 0;
    int use_vpss_bind_display = !solid_test && only_tile &&
        (strcasecmp(only_tile, "VPSS") == 0 || use_vmix_bind_display);
    int use_osd_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "OSD") == 0;
    int use_clahe_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "CLAHE") == 0;
    int use_retinex_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "RETINEX") == 0;
    int use_cap_dehaze_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "CAP_DEHAZE") == 0;
    int use_dcp_dehaze_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "DCP_FAST_DEHAZE") == 0;
    int use_rga_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "RGA") == 0;
    int use_resize_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "RESIZE_RGA") == 0;
    int use_csc_rga_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "CSC_RGA") == 0;
    int use_csc_cl_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "CSC_CL") == 0;
    int use_conv_cl_bind_display = 0;
    int use_transform_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "TRANSFORM") == 0;
    int use_stereo_bind_display = !solid_test && only_tile && strcasecmp(only_tile, "STEREO_3D") == 0;
    int use_vi_big_display = use_vi_bind_display;
    int vi_bind_display_ok = 0;
    int vi_resize_bind_display_ok = 0;
    int wbc_bind_display_ok = 0;
    int vpss_bind_display_ok = 0;
    int osd_bind_display_ok = 0;
    int clahe_bind_display_ok = 0;
    int retinex_bind_display_ok = 0;
    int cap_dehaze_bind_display_ok = 0;
    int dcp_dehaze_bind_display_ok = 0;
    int rga_bind_display_ok = 0;
    int resize_bind_display_ok = 0;
    int csc_rga_bind_display_ok = 0;
    int csc_cl_bind_display_ok = 0;
    int conv_cl_bind_display_ok = 0;
    int transform_bind_display_ok = 0;
    int stereo_bind_display_ok = 0;
    int live_resize_bind_ok = 0;
    int live_vi_resize_bind_ok = 0;
    int live_osd_bind_ok = 0;
    int live_clahe_bind_ok = 0;
    int live_retinex_bind_ok = 0;
    int live_dehaze_bind_ok = 0;
    int live_csc_rga_chain_ok = 0;
    int live_csc_cl_chain_ok = 0;
    int live_conv_cl_quad_ok = 0;
    int live_transform_quad_ok = 0;
    int live_stereo_bind_ok = 0;
    int live_wbc_ok = 0;
    int live_pre_resize_bind_ok = 0;
    int live_compare_display_resizes_ok = 0;
    int display_vmix_osd_ok = 0;
    int display_vmix_rga_osd_ok = 0;
    int display_vmix_inputs =
        (use_vpss_bind_display || use_conv_cl_bind_display) ? VPSS_DEMO_OUTPUTS :
        ((use_wbc_bind_display || use_clahe_bind_display || use_retinex_bind_display) ? 2 : 1);
    int display_vmix_layout = use_wbc_bind_display ? DISPLAY_VMIX_LAYOUT_WBC :
        (use_vmix_bind_display ? DISPLAY_VMIX_LAYOUT_DEMO :
         ((use_vi_big_display || use_osd_bind_display ||
           use_rga_bind_display || use_resize_bind_display ||
           use_transform_bind_display ||
           use_cap_dehaze_bind_display || use_dcp_dehaze_bind_display) ? DISPLAY_VMIX_LAYOUT_VI_BIG : DISPLAY_VMIX_LAYOUT_DEFAULT));
    int defer_vmix_osd_for_csc_cl = use_csc_cl_bind_display;
    int defer_vmix_osd_for_clahe = use_clahe_bind_display;
    int defer_vmix_osd_for_retinex = use_retinex_bind_display;
    int defer_vmix_osd_for_conv_cl = use_conv_cl_bind_display;
    if (use_vmix_osd_display && !defer_vmix_osd_for_csc_cl &&
        !defer_vmix_osd_for_clahe && !defer_vmix_osd_for_retinex &&
        !defer_vmix_osd_for_conv_cl &&
        setup_display_vmix_osd(dstride, display_size, display_vmix_inputs, display_vmix_layout) == 0) {
        display_vmix_osd_ok = 1;
    } else if (!defer_vmix_osd_for_csc_cl && !defer_vmix_osd_for_clahe &&
               !defer_vmix_osd_for_retinex && !defer_vmix_osd_for_conv_cl) {
        use_vmix_osd_display = 0;
    }
    if (use_csc_cl_bind_display && setup_live_csc_cl_chain() == 0) {
        live_csc_cl_chain_ok = 1;
    }
    if (use_csc_cl_bind_display) use_vmix_osd_display = 0;
    if (use_clahe_bind_display && setup_live_clahe_bind() == 0) {
        live_clahe_bind_ok = 1;
        g_clahe_mix_module = "VMIX_RGA";
        if (use_vmix_osd_display &&
            setup_display_vmix_rga_osd_inputs(dstride, display_size, 1) == 0) {
            display_vmix_osd_ok = 1;
            display_vmix_rga_osd_ok = 1;
            if (setup_live_compare_effect_display_resize() == 0) {
                live_compare_display_resizes_ok = 2;
            } else {
                cleanup_display_vmix_rga_osd(display_vmix_osd_ok);
                display_vmix_osd_ok = 0;
                display_vmix_rga_osd_ok = 0;
            }
        }
        if (!live_compare_display_resizes_ok) {
            cleanup_live_compare_display_resizes(live_compare_display_resizes_ok);
            live_compare_display_resizes_ok = 0;
            cleanup_live_clahe_bind(live_clahe_bind_ok);
            live_clahe_bind_ok = 0;
            use_vmix_osd_display = 0;
            g_clahe_mix_module = "VMIX";
        }
    }
    if (use_retinex_bind_display && setup_live_retinex_bind() == 0) {
        live_retinex_bind_ok = 1;
        g_retinex_mix_module = "VMIX_RGA";
        if (use_vmix_osd_display &&
            setup_display_vmix_rga_osd_inputs(dstride, display_size, 1) == 0) {
            display_vmix_osd_ok = 1;
            display_vmix_rga_osd_ok = 1;
            if (setup_live_compare_effect_display_resize() == 0) {
                live_compare_display_resizes_ok = 2;
            } else {
                cleanup_display_vmix_rga_osd(display_vmix_osd_ok);
                display_vmix_osd_ok = 0;
                display_vmix_rga_osd_ok = 0;
            }
        }
        if (!live_compare_display_resizes_ok) {
            cleanup_live_compare_display_resizes(live_compare_display_resizes_ok);
            live_compare_display_resizes_ok = 0;
            cleanup_live_retinex_bind(live_retinex_bind_ok);
            live_retinex_bind_ok = 0;
            use_vmix_osd_display = 0;
            g_retinex_mix_module = "VMIX";
        }
    }
    if (use_conv_cl_bind_display) {
        g_conv_cl_mix_module = "VMIX_RGA";
        if (use_vmix_osd_display &&
            setup_display_vmix_rga_osd_inputs(dstride, display_size, VPSS_DEMO_OUTPUTS) == 0) {
            display_vmix_osd_ok = 1;
            display_vmix_rga_osd_ok = 1;
        } else {
            use_vmix_osd_display = 0;
            g_conv_cl_mix_module = "VMIX";
        }
    }
    if ((use_cap_dehaze_bind_display || use_dcp_dehaze_bind_display) &&
        display_vmix_osd_ok &&
        setup_live_cap_dehaze_bind_chain(use_dcp_dehaze_bind_display) == 0) {
        live_dehaze_bind_ok = 1;
    }
    if (use_wbc_bind_display && display_vmix_osd_ok && setup_live_wbc() == 0) {
        live_wbc_ok = 1;
    }
    if (use_rga_bind_display && display_vmix_osd_ok && setup_live_rga() == 0) {
        live_rga_ok = 1;
    }
    if (use_conv_cl_bind_display && display_vmix_osd_ok &&
        setup_live_conv_cl_quad() == 0) {
        live_conv_cl_quad_ok = 1;
    }
    if (use_transform_bind_display && display_vmix_osd_ok &&
        setup_live_transform_quad() == 0) {
        live_transform_quad_ok = 1;
    }
    if (use_stereo_bind_display && display_vmix_osd_ok &&
        setup_live_stereo_bind() == 0) {
        live_stereo_bind_ok = 1;
    }
    if (use_osd_bind_display && use_4k_camera_input &&
        setup_live_camera_to_cam_resize_bind() == 0) {
        live_pre_resize_bind_ok = 1;
    }
    if (use_osd_bind_display && display_vmix_osd_ok && setup_live_osd_bind() == 0) {
        live_osd_bind_ok = 1;
    }
    if (use_resize_bind_display && display_vmix_osd_ok && setup_live_resize_bind() == 0) {
        live_resize_bind_ok = 1;
    }
    if (use_csc_rga_bind_display && display_vmix_osd_ok && setup_live_csc_rga_chain() == 0) {
        live_csc_rga_chain_ok = 1;
    }

    int camera_ok = 0;
    int need_camera = !solid_test && tile_needs_camera(only_tile);
    int camera_w = g_camera_input_w;
    int camera_h = g_camera_input_h;
    int camera_stride = g_camera_input_stride;
    int camera_fps = (use_cap_dehaze_bind_display || use_dcp_dehaze_bind_display) ? 10 : FPS;
    int camera_v4l2_buf_cnt = 4;
    size_t camera_frame_size = (size_t)camera_stride * camera_h * 3 / 2;
    /*
     * VI queues buf_cnt buffers into V4L2.  The pool must keep spare buffers
     * so a captured buffer can be handed downstream while VI re-queues a
     * replacement; pool_count == buf_cnt makes bound pipelines starve.
     */
    int camera_pool_count = camera_v4l2_buf_cnt + 2;
    if (need_camera && MEDIA_POOL_Create(CAMERA_POOL, camera_frame_size, camera_pool_count) == 0) {
        MEDIA_VI_ATTR vi = {0};
        vi.device = CAMERA_DEVICE;
        vi.width = camera_w;
        vi.height = camera_h;
        vi.stride = camera_stride;
        vi.fps = camera_fps;
        vi.buf_cnt = camera_v4l2_buf_cnt;
        vi.pool_id = CAMERA_POOL;
        vi.format = MEDIA_FORMAT_NV12;
        if (MEDIA_VI_SetAttr(0, &vi) == 0 && MEDIA_VI_Enable(0) == 0) {
            camera_ok = 1;
            set_tile_status("VI", TILE_LIVE);
        }
    }
    g_health.camera_running = camera_ok;

    if (use_vi_bind_display) {
        if (use_vi_big_display && display_vmix_osd_ok) {
            live_vi_resize_bind_ok = setup_live_vi_big_resize_bind() == 0;
        }
        if (!camera_ok || !display_vmix_osd_ok ||
            (use_vi_big_display && !live_vi_resize_bind_ok) ||
            (use_vi_big_display ? bind_vi_resize_vmix_osd_vo() : bind_vi_vmix_osd_vo()) != 0) {
            fprintf(stderr, "VI bind display setup failed; CPU copy fallback is disabled for --only VI\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_resize_bind(live_vi_resize_bind_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        vi_bind_display_ok = 1;
        vi_resize_bind_display_ok = use_vi_big_display;
        char initial_perf[128];
        if (use_vi_big_display) {
            snprintf(initial_perf, sizeof(initial_perf), "IN %dx%d -> RGA %dx%d  CPU 0%%  GPU N/A",
                     camera_w, camera_h, VI_BIG_VIEW_W, VI_BIG_VIEW_H);
            (void)update_display_osd_text("VI LARGE INPUT", initial_perf);
        } else {
            snprintf(initial_perf, sizeof(initial_perf), "PAGE 01/%02d  CPU 0%%  GPU N/A  LIVE",
                     module_page_total());
            (void)update_display_osd_text("VI TEST", initial_perf);
        }
        (void)update_vi_bind_flow_overlay();
        if (use_vi_big_display) {
            printf("VI bind flow: VI0.%s -> RESIZE_RGA%d.%s; RESIZE_RGA%d.%s -> VMIX%d.%s; VMIX%d.%s -> OSD%d.%s; OSD%d.%s -> VO0.%s\n",
                   g_bind_vi_src_port ? g_bind_vi_src_port : "output",
                   LIVE_RESIZE_GRP,
                   g_bind_resize_in_port ? g_bind_resize_in_port : "input0",
                   LIVE_RESIZE_GRP,
                   g_bind_resize_src_port ? g_bind_resize_src_port : "output0",
                   DISPLAY_VMIX_GRP,
                   g_bind_vmix_in_port ? g_bind_vmix_in_port : "input0",
                   DISPLAY_VMIX_GRP,
                   g_bind_vmix_src_port ? g_bind_vmix_src_port : "output0",
                   DISPLAY_OSD_GRP,
                   g_bind_osd_in_port ? g_bind_osd_in_port : "input",
                   DISPLAY_OSD_GRP,
                   g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
                   g_bind_vo_in_port ? g_bind_vo_in_port : "input0");
        } else {
            printf("VI bind flow: VI0.%s -> VMIX%d.%s; VMIX%d.%s -> OSD%d.%s; OSD%d.%s -> VO0.%s\n",
                   g_bind_vi_src_port ? g_bind_vi_src_port : "output",
                   DISPLAY_VMIX_GRP,
                   g_bind_vmix_in_port ? g_bind_vmix_in_port : "input0",
                   DISPLAY_VMIX_GRP,
                   g_bind_vmix_src_port ? g_bind_vmix_src_port : "output0",
                   DISPLAY_OSD_GRP,
                   g_bind_osd_in_port ? g_bind_osd_in_port : "input",
                   DISPLAY_OSD_GRP,
                   g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
                   g_bind_vo_in_port ? g_bind_vo_in_port : "input0");
        }
    }
    if (use_wbc_bind_display) {
        if (!camera_ok || !live_wbc_ok || !display_vmix_osd_ok ||
            bind_vi_wbc_vmix_osd_vo() != 0) {
            fprintf(stderr, "WBC bind display setup failed; CPU copy fallback is disabled for --only WBC\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_wbc(live_wbc_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        wbc_bind_display_ok = 1;
        (void)update_display_osd_text("WBC  VI + WRITEBACK", "VI RGA + WBC RGA -> VMIX -> VO");
        (void)update_wbc_bind_flow_overlay(0, 0, 0);
        printf("WBC bind flow: VI0.%s -> RESIZE_RGA%d.%s; RESIZE_RGA%d.%s -> VMIX%d.%s; VO_WBC%d.%s -> RESIZE_RGA%d.%s; RESIZE_RGA%d.%s -> VMIX%d.%s; VMIX%d.%s -> OSD%d.%s; OSD%d.%s -> VO0.%s\n",
               g_bind_vi_src_port ? g_bind_vi_src_port : "output",
               LIVE_WBC_VI_RESIZE_GRP,
               g_bind_wbc_vi_resize_in_port ? g_bind_wbc_vi_resize_in_port : "input0",
               LIVE_WBC_VI_RESIZE_GRP,
               g_bind_wbc_vi_resize_src_port ? g_bind_wbc_vi_resize_src_port : "output0",
               DISPLAY_VMIX_GRP,
               g_bind_wbc_vi_vmix_in_port ? g_bind_wbc_vi_vmix_in_port : "input0",
               LIVE_WBC_CHN,
               g_bind_wbc_src_port ? g_bind_wbc_src_port : "output",
               LIVE_WBC_RESIZE_GRP,
               g_bind_wbc_resize_in_port ? g_bind_wbc_resize_in_port : "input0",
               LIVE_WBC_RESIZE_GRP,
               g_bind_wbc_resize_src_port ? g_bind_wbc_resize_src_port : "output0",
               DISPLAY_VMIX_GRP,
               g_bind_wbc_vmix_in_port ? g_bind_wbc_vmix_in_port : "input1",
               DISPLAY_VMIX_GRP,
               g_bind_vmix_src_port ? g_bind_vmix_src_port : "output0",
               DISPLAY_OSD_GRP,
               g_bind_osd_in_port ? g_bind_osd_in_port : "input",
               DISPLAY_OSD_GRP,
               g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
               g_bind_vo_in_port ? g_bind_vo_in_port : "input0");
    }
    if (use_vpss_bind_display) {
        if (!camera_ok || !live_vpss_ok || !display_vmix_osd_ok || bind_vpss_vmix_osd_vo() != 0) {
            fprintf(stderr, "VPSS bind display setup failed; CPU copy fallback is disabled for --only VPSS\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        vpss_bind_display_ok = 1;
        if (use_vmix_bind_display) {
            set_tile_status("VMIX", TILE_LIVE);
            (void)update_display_osd_text("VMIX  4 INPUT COMPOSITE",
                                          "VI VPSS4 -> VMIX ALPHA -> OSD VO");
            (void)update_vmix_bind_flow_overlay(0, 0, 0);
            printf("VMIX bind flow: VI0.%s -> VPSS%d.%s; VPSS outputs0..3 -> VMIX%d inputs0..3; VMIX%d.%s -> OSD%d.%s; OSD%d.%s -> VO0.%s\n",
                   g_bind_vi_src_port ? g_bind_vi_src_port : "output",
                   LIVE_VPSS_GRP,
                   g_bind_vpss_in_port ? g_bind_vpss_in_port : "input",
                   DISPLAY_VMIX_GRP,
                   DISPLAY_VMIX_GRP,
                   g_bind_vmix_src_port ? g_bind_vmix_src_port : "output0",
                   DISPLAY_OSD_GRP,
                   g_bind_osd_in_port ? g_bind_osd_in_port : "input",
                   DISPLAY_OSD_GRP,
                   g_bind_osd_src_port ? g_bind_osd_src_port : "output0",
                   g_bind_vo_in_port ? g_bind_vo_in_port : "input0");
        } else {
            (void)update_display_osd_text("VPSS TEST", "FULL  MOVING CROP  FLIP  ZOOM");
            (void)update_vpss_bind_flow_overlay();
        }
    }
    if (use_osd_bind_display) {
        if (!camera_ok || !live_osd_bind_ok || !display_vmix_osd_ok ||
            (use_4k_camera_input && !live_pre_resize_bind_ok) ||
            (use_4k_camera_input ?
             bind_vi_resize_live_osd_vmix_osd_vo() :
             bind_vi_live_osd_vmix_osd_vo()) != 0) {
            fprintf(stderr, "OSD bind display setup failed; CPU copy fallback is disabled for --only OSD\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_osd_bind(live_osd_bind_ok);
            cleanup_live_camera_to_cam_resize_bind(live_pre_resize_bind_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        osd_bind_display_ok = 1;
        (void)update_display_osd_text("OSD  VI OSD VMIX OSD VO", "REGIONS  DYNAMIC  BIND");
        (void)update_osd_bind_flow_overlay(0, 0, osd_demo_params_for_frame(0), 0);
    }
    if (use_clahe_bind_display) {
        if (!camera_ok || !live_clahe_bind_ok || !display_vmix_osd_ok ||
            bind_vi_clahe_vmix_osd_vo() != 0) {
            fprintf(stderr, "CLAHE bind display setup failed; CPU copy fallback is disabled for --only CLAHE\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_compare_display_resizes(live_compare_display_resizes_ok);
            live_compare_display_resizes_ok = 0;
            cleanup_live_clahe_bind(live_clahe_bind_ok);
            if (display_vmix_rga_osd_ok) {
                cleanup_display_vmix_rga_osd(display_vmix_osd_ok);
            } else {
                cleanup_display_vmix_osd(display_vmix_osd_ok);
            }
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        clahe_bind_display_ok = 1;
        (void)update_display_osd_text("CLAHE  4K PASSTHROUGH/ENHANCE", "VI 4K -> CLAHE -> RESIZE -> VO");
        (void)update_clahe_compare_overlay(0, 0, 0, 2.5f, 1, 0);
    }
    if (use_retinex_bind_display) {
        if (!camera_ok || !live_retinex_bind_ok || !display_vmix_osd_ok ||
            bind_vi_retinex_vmix_osd_vo() != 0) {
            fprintf(stderr, "RETINEX bind display setup failed; CPU copy fallback is disabled for --only RETINEX\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_compare_display_resizes(live_compare_display_resizes_ok);
            live_compare_display_resizes_ok = 0;
            cleanup_live_retinex_bind(live_retinex_bind_ok);
            if (display_vmix_rga_osd_ok) {
                cleanup_display_vmix_rga_osd(display_vmix_osd_ok);
            } else {
                cleanup_display_vmix_osd(display_vmix_osd_ok);
            }
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        retinex_bind_display_ok = 1;
        (void)update_display_osd_text("RETINEX  4K PROCESS THEN RESIZE", "VI 4K -> RETINEX -> RESIZE -> VO");
        (void)update_retinex_compare_overlay(0, 0, 0, 0, 0);
    }
    if (use_cap_dehaze_bind_display || use_dcp_dehaze_bind_display) {
        int use_dcp = use_dcp_dehaze_bind_display;
        const char *name = use_dcp ? "DCP_FAST_DEHAZE" : "CAP_DEHAZE";
        if (!camera_ok || !live_dehaze_bind_ok || !display_vmix_osd_ok ||
            bind_vi_dehaze_vmix_osd_vo(use_dcp) != 0) {
            fprintf(stderr, "%s bind display setup failed; CPU copy fallback is disabled for --only %s\n",
                    name, name);
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_cap_dehaze_bind_chain(live_dehaze_bind_ok, use_dcp);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        cap_dehaze_bind_display_ok = use_cap_dehaze_bind_display;
        dcp_dehaze_bind_display_ok = use_dcp_dehaze_bind_display;
        (void)update_display_osd_text(use_dcp ? "DCP_FAST_DEHAZE  4K PASS/ENHANCE" :
                                                "CAP_DEHAZE  4K PASS/ENHANCE",
                                      "VI 4K -> CSC_RGA RGB -> DEHAZE -> RESIZE -> VO");
    }
    if (use_rga_bind_display) {
        if (!camera_ok || !live_rga_ok || !display_vmix_osd_ok || bind_vi_rga_vmix_osd_vo() != 0) {
            fprintf(stderr, "RGA bind display setup failed; CPU copy fallback is disabled for --only RGA\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_rga(live_rga_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        rga_bind_display_ok = 1;
        (void)update_display_osd_text("RGA  NV12 BIND", "VI RGA VMIX OSD VO  OP COPY");
        (void)update_rga_bind_flow_overlay(0, 0);
    }
    if (use_resize_bind_display) {
        if (!camera_ok || !live_resize_bind_ok || !display_vmix_osd_ok ||
            bind_vi_resize_vmix_osd_vo() != 0) {
            fprintf(stderr, "RESIZE_RGA bind display setup failed; CPU copy fallback is disabled for --only RESIZE_RGA\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_resize_bind(live_resize_bind_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        resize_bind_display_ok = 1;
        (void)update_display_osd_text("RESIZE_RGA  NV12 BIND", "DYNAMIC CROP ZOOM");
        (void)update_resize_bind_flow_overlay(resize_demo_params_for_frame(0), 0);
    }
    if (use_csc_rga_bind_display) {
        if (!camera_ok || !live_csc_rga_chain_ok || !display_vmix_osd_ok ||
            bind_vi_csc_chain_vmix_osd_vo() != 0) {
            fprintf(stderr, "CSC_RGA bind display setup failed; CPU copy fallback is disabled for --only CSC_RGA\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_csc_rga_chain(live_csc_rga_chain_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        csc_rga_bind_display_ok = 1;
        (void)update_display_osd_text("CSC_RGA  DOUBLE CSC", "NV12 TO ARGB8888 TO NV12");
        (void)update_csc_rga_bind_flow_overlay(0, 0, 0);
    }
    if (use_csc_cl_bind_display) {
        if (!camera_ok || !live_csc_cl_chain_ok ||
            bind_vi_csc_cl_chain_vmix_osd_vo() != 0) {
            fprintf(stderr, "CSC_CL bind display setup failed; CPU copy fallback is disabled for --only CSC_CL\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_csc_cl_chain(live_csc_cl_chain_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        csc_cl_bind_display_ok = 1;
        (void)update_csc_cl_bind_flow_overlay(0, 0, -1.0, -1.0, 0);
    }
    if (use_conv_cl_bind_display) {
        if (!camera_ok || !live_conv_cl_quad_ok || !display_vmix_osd_ok ||
            bind_vi_vpss_conv_cl_vmix_osd_vo() != 0) {
            fprintf(stderr, "CONV_CL quad bind display setup failed; CPU copy fallback is disabled for --only CONV_CL\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_conv_cl_quad(live_conv_cl_quad_ok);
            if (display_vmix_rga_osd_ok) {
                cleanup_display_vmix_rga_osd(display_vmix_osd_ok);
            } else {
                cleanup_display_vmix_osd(display_vmix_osd_ok);
            }
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        conv_cl_bind_display_ok = 1;
        (void)update_display_osd_text("CONV_CL  VPSS 4X GPU CONV", "SHARPEN EDGE EMBOSS BLUR");
        uint64_t zero_counts[VPSS_DEMO_OUTPUTS] = {0};
        (void)update_conv_cl_quad_overlay(0, zero_counts, -1.0, -1.0, 0);
    }
    if (use_transform_bind_display) {
        if (!camera_ok || !live_transform_quad_ok || !display_vmix_osd_ok ||
            bind_vi_vpss_transform_vmix_osd_vo() != 0) {
            fprintf(stderr, "TRANSFORM quad bind display setup failed; CPU copy fallback is disabled for --only TRANSFORM\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_transform_quad(live_transform_quad_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        transform_bind_display_ok = 1;
        (void)update_display_osd_text("TRANSFORM  4K PROCESS THEN RESIZE", "RAW UNDISTORT ROTATE PERSPECTIVE");
        uint64_t zero_counts[VPSS_DEMO_OUTPUTS] = {0};
        (void)update_transform_quad_overlay(0, zero_counts, zero_counts, 0);
    }
    if (use_stereo_bind_display) {
        if (!camera_ok || !live_stereo_bind_ok || !display_vmix_osd_ok ||
            bind_vi_vpss_stereo_vmix_osd_vo() != 0) {
            fprintf(stderr, "STEREO_3D bind display setup failed; CPU copy fallback is disabled for --only STEREO_3D\n");
            if (camera_ok) MEDIA_VI_Disable(0);
            cleanup_live_stereo_bind(live_stereo_bind_ok);
            cleanup_display_vmix_osd(display_vmix_osd_ok);
            MEDIA_VO_Stop(0, 0);
            MEDIA_VO_DestroyChn(0, 0);
            MEDIA_POOL_Destroy(DISPLAY_POOL);
            cleanup_live_stereo(live_stereo_ok);
            cleanup_live_pano(live_pano_ok);
            cleanup_live_dualview(live_dualview_ok);
            cleanup_live_edof(live_edof_ok);
            cleanup_live_retinex(live_retinex_ok);
            cleanup_live_clahe(live_clahe_ok);
            cleanup_live_conv_cl(live_conv_ok);
            cleanup_live_dcp_dehaze(live_dcp_ok);
            cleanup_live_cap_dehaze(live_cap_ok);
            cleanup_live_transform(live_transform_ok);
            cleanup_live_csc_rga(live_csc_rga_ok);
            cleanup_live_rga(live_rga_ok);
            cleanup_live_vpss(live_vpss_ok);
            cleanup_live_resize(live_resize_ok);
            cleanup_live_osd(live_osd_ok);
            if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
            unload_pano_sample();
            unload_edof_pairs();
            unload_loop_assets();
            MEDIA_SYS_Exit();
            return 1;
        }
        stereo_bind_display_ok = 1;
        (void)update_display_osd_text("STEREO_3D  RAW ROTATE", "VPSS RAW + ROT90 -> MERGE");
        (void)update_stereo_3d_overlay(0, 0, 0);
    }

    printf("alldemo running on DSI 1080x1920%s%s%s%s. Ctrl+C to stop.\n",
           solid_test ? " solid-test" : "",
           (!solid_test && rotate_main) ? " rotate-main" : "",
           only_tile ? " only=" : "",
           only_tile ? only_tile : "");

    int frame = 0;
    int cam_frames = 0;
    int osd_frames = 0;
    int resize_frames = 0;
    int vpss_frames = 0;
    int csc_rga_frames = 0;
    int transform_frames = 0;
    int cap_frames = 0;
    int dcp_frames = 0;
    int conv_frames = 0;
    int clahe_frames = 0;
    int retinex_frames = 0;
    int edof_frames = 0;
    int edof_pair_index = -1;
    int mcf_frames = 0;
    int mcf_pair_index = -1;
    int dualview_frames = 0;
    int pano_frames = 0;
    int stereo_frames = 0;
    int thermal_frames = 0;
    int rga_op_index = 0;
    int last_rga_op_index = -1;
    uint8_t *last_cam = malloc(CAM_FRAME_SIZE);
    uint8_t *last_osd = malloc(CAM_FRAME_SIZE);
    uint8_t *last_resize = malloc(CAM_FRAME_SIZE);
    uint8_t *last_vpss = malloc(CAM_FRAME_SIZE);
    uint8_t *last_csc_rga = malloc(CAM_FRAME_SIZE);
    uint8_t *last_transform = malloc(CAM_FRAME_SIZE);
    uint8_t *last_cap = malloc(RGB_FRAME_SIZE);
    uint8_t *last_dcp = malloc(RGB_FRAME_SIZE);
    uint8_t *last_conv = malloc(RGBA_FRAME_SIZE);
    uint8_t *last_conv_tmp = malloc(RGBA_FRAME_SIZE);
    uint8_t *last_clahe = malloc(CAM_FRAME_SIZE);
    uint8_t *last_retinex = malloc(CAM_FRAME_SIZE);
    uint8_t *last_edof_in0 = malloc(CAM_FRAME_SIZE);
    uint8_t *last_edof_in1 = malloc(CAM_FRAME_SIZE);
    uint8_t *last_edof = malloc(CAM_FRAME_SIZE);
    uint8_t *last_mcf_color = malloc(CAM_FRAME_SIZE);
    uint8_t *last_mcf_mono = malloc(CAM_FRAME_SIZE);
    uint8_t *last_mcf = malloc(CAM_FRAME_SIZE);
    uint8_t *last_dual_in0 = malloc(RGB_FRAME_SIZE);
    uint8_t *last_dual_in1 = malloc(RGB_FRAME_SIZE);
    uint8_t *last_dual_sbs = malloc(RGB_FRAME_SIZE);
    uint8_t *last_dual_lbl = malloc(RGB_FRAME_SIZE);
    uint8_t *last_pano = malloc(PANO_OUTPUT_SIZE);
    uint8_t *last_rgb_src = malloc(RGB_FRAME_SIZE);
    uint8_t *last_rgba_src = malloc(RGBA_FRAME_SIZE);
    uint8_t *last_stereo = malloc(CAM_FRAME_SIZE);
    uint8_t *thermal_page_cache = thermal_only_page ? malloc(display_size) : NULL;
    int thermal_page_cache_asset = -1;
    uint8_t *edof_page_cache = edof_only_page ? malloc(display_size) : NULL;
    int edof_page_cache_sample = -1;
    uint8_t *mcf_page_cache = mcf_only_page ? malloc(display_size) : NULL;
    int mcf_page_cache_sample = -1;
    uint8_t *pano_page_cache = pano_only_page ? malloc(display_size) : NULL;
    int pano_page_cache_generation = -1;
    if (thermal_only_page && !thermal_page_cache) {
        fprintf(stderr, "warning: THERMAL page cache allocation failed, rendering every frame\n");
    }
    if (edof_only_page && !edof_page_cache) {
        fprintf(stderr, "warning: EDOF_CL page cache allocation failed, rendering every frame\n");
    }
    if (mcf_only_page && !mcf_page_cache) {
        fprintf(stderr, "warning: MCF_FUSION_CL page cache allocation failed, rendering every frame\n");
    }
    if (pano_only_page && !pano_page_cache) {
        fprintf(stderr, "warning: PANO page cache allocation failed, rendering every frame\n");
    }
    if (last_cam) memset(last_cam, 0, CAM_FRAME_SIZE);
    if (last_osd) memset(last_osd, 0, CAM_FRAME_SIZE);
    if (last_resize) memset(last_resize, 0, CAM_FRAME_SIZE);
    if (last_vpss) memset(last_vpss, 0, CAM_FRAME_SIZE);
    if (last_csc_rga) memset(last_csc_rga, 0, CAM_FRAME_SIZE);
    if (last_transform) memset(last_transform, 0, CAM_FRAME_SIZE);
    if (last_cap) memset(last_cap, 0, RGB_FRAME_SIZE);
    if (last_dcp) memset(last_dcp, 0, RGB_FRAME_SIZE);
    if (last_conv) memset(last_conv, 0, RGBA_FRAME_SIZE);
    if (last_conv_tmp) memset(last_conv_tmp, 0, RGBA_FRAME_SIZE);
    if (last_clahe) memset(last_clahe, 0, CAM_FRAME_SIZE);
    if (last_retinex) memset(last_retinex, 0, CAM_FRAME_SIZE);
    if (last_edof_in0) memset(last_edof_in0, 0, CAM_FRAME_SIZE);
    if (last_edof_in1) memset(last_edof_in1, 0, CAM_FRAME_SIZE);
    if (last_edof) memset(last_edof, 0, CAM_FRAME_SIZE);
    if (last_mcf_color) memset(last_mcf_color, 0, CAM_FRAME_SIZE);
    if (last_mcf_mono) memset(last_mcf_mono, 0, CAM_FRAME_SIZE);
    if (last_mcf) memset(last_mcf, 0, CAM_FRAME_SIZE);
    if (last_dual_in0) memset(last_dual_in0, 0, RGB_FRAME_SIZE);
    if (last_dual_in1) memset(last_dual_in1, 0, RGB_FRAME_SIZE);
    if (last_dual_sbs) memset(last_dual_sbs, 0, RGB_FRAME_SIZE);
    if (last_dual_lbl) memset(last_dual_lbl, 0, RGB_FRAME_SIZE);
    if (last_pano) memset(last_pano, 0, PANO_OUTPUT_SIZE);
    if (last_rgb_src) memset(last_rgb_src, 0, RGB_FRAME_SIZE);
    if (last_rgba_src) memset(last_rgba_src, 0, RGBA_FRAME_SIZE);
    if (last_stereo) memset(last_stereo, 0, CAM_FRAME_SIZE);
    if (thermal_page_cache) memset(thermal_page_cache, 0, display_size);
    if (edof_page_cache) memset(edof_page_cache, 0, display_size);
    if (mcf_page_cache) memset(mcf_page_cache, 0, display_size);
    if (pano_page_cache) memset(pano_page_cache, 0, display_size);
    while (g_running) {
        if (camera_ok && !vi_bind_display_ok && !vpss_bind_display_ok &&
            !osd_bind_display_ok &&
            !wbc_bind_display_ok &&
            !clahe_bind_display_ok &&
            !retinex_bind_display_ok &&
            !rga_bind_display_ok && !resize_bind_display_ok &&
            !csc_rga_bind_display_ok && !csc_cl_bind_display_ok &&
            !conv_cl_bind_display_ok &&
            !transform_bind_display_ok &&
            !stereo_bind_display_ok) {
            MEDIA_BUFFER cbuf = {-1, -1};
            if (MEDIA_VI_GetFrame(0, &cbuf, 1) == 0) {
                void *addr = NULL;
                size_t size = 0;
                if (MEDIA_POOL_BeginCpuAccess(cbuf, DMA_BUF_SYNC_READ) == 0) {
                    if (map_buffer(cbuf, &addr, &size, PROT_READ) == 0) {
                        size_t need = camera_frame_size;
                        if (last_cam && size >= need) {
                            if (camera_w == CAM_W && camera_h == CAM_H &&
                                camera_stride == CAM_STRIDE) {
                                memcpy(last_cam, addr, CAM_FRAME_SIZE);
                            } else {
                                scale_nv12_to_cam_frame((const uint8_t *)addr,
                                                        camera_w, camera_h,
                                                        camera_stride, last_cam);
                            }
                            cam_frames++;
                            g_health.camera_frames = cam_frames;
                            if (live_osd_ok && last_osd &&
                                process_live_osd(last_cam, last_osd) == 0) {
                                osd_frames++;
                            }
                            if (live_resize_ok && last_resize &&
                                process_live_resize(last_cam, last_resize) == 0) {
                                resize_frames++;
                            }
                            if (live_vpss_ok && last_vpss &&
                                process_live_vpss(last_cam, last_vpss) == 0) {
                                vpss_frames++;
                            }
                            if (live_csc_rga_ok && last_csc_rga &&
                                process_live_csc_rga(last_cam, last_csc_rga) == 0) {
                                csc_rga_frames++;
                            }
                            if (live_stereo_ok && last_stereo && (cam_frames % 3) == 0 &&
                                process_live_stereo(last_cam, last_stereo) == 0) {
                                stereo_frames++;
                            }
                            if (live_retinex_ok && last_retinex &&
                                process_live_retinex(last_cam, last_retinex) == 0) {
                                retinex_frames++;
                            }
                            if (live_cap_ok && last_rgb_src && last_cap) {
                                nv12_to_rgb_frame(last_cam, last_rgb_src, CAM_W, CAM_H, CAM_STRIDE);
                                if (process_live_cap_dehaze(last_rgb_src, last_cap) == 0) {
                                    cap_frames++;
                                }
                            }
                            if (live_dcp_ok && last_rgb_src && last_dcp) {
                                nv12_to_rgb_frame(last_cam, last_rgb_src, CAM_W, CAM_H, CAM_STRIDE);
                                if (process_live_dcp_dehaze(last_rgb_src, last_dcp) == 0) {
                                    dcp_frames++;
                                }
                            }
                            if (live_conv_ok && last_rgba_src && last_conv_tmp && last_conv) {
                                nv12_to_rgba_frame(last_cam, last_rgba_src, CAM_W, CAM_H, CAM_STRIDE);
                                if (process_live_conv_cl_grid(last_rgba_src, last_conv_tmp,
                                                              last_conv) == 0) {
                                    conv_frames++;
                                }
                            }
                        }
                        munmap(addr, size);
                    }
                    (void)MEDIA_POOL_EndCpuAccess(cbuf, DMA_BUF_SYNC_READ);
                }
                MEDIA_VI_ReleaseFrame(0, cbuf);
            }
        }

        if (live_transform_ok && last_cam && last_transform) {
            fill_synthetic_nv12(last_cam, CAM_W, CAM_H, CAM_STRIDE, frame);
            if (process_live_transform(last_cam, last_transform) == 0) {
                transform_frames++;
            }
        }
        if (live_cap_ok && last_rgb_src && last_cap &&
            !(only_tile && strcasecmp(only_tile, "CAP_DEHAZE") == 0 && camera_ok)) {
            fill_synthetic_rgb(last_rgb_src, CAM_W, CAM_H, CAM_W * 3, frame);
            if (process_live_cap_dehaze(last_rgb_src, last_cap) == 0) {
                cap_frames++;
            }
        }
        if (live_dcp_ok && last_rgb_src && last_dcp &&
            !(only_tile && strcasecmp(only_tile, "DCP_FAST_DEHAZE") == 0 && camera_ok)) {
            fill_synthetic_rgb(last_rgb_src, CAM_W, CAM_H, CAM_W * 3, frame);
            if (process_live_dcp_dehaze(last_rgb_src, last_dcp) == 0) {
                dcp_frames++;
            }
        }
        if (live_conv_ok && last_rgba_src && last_conv_tmp && last_conv &&
            !(only_tile && strcasecmp(only_tile, "CONV_CL") == 0 && camera_ok)) {
            fill_synthetic_rgba(last_rgba_src, CAM_W, CAM_H, CAM_W * 4, frame);
            if (process_live_conv_cl_grid(last_rgba_src, last_conv_tmp, last_conv) == 0) {
                conv_frames++;
            }
        }
        if (live_clahe_ok && last_cam && last_clahe) {
            fill_synthetic_nv12(last_cam, CAM_W, CAM_H, CAM_STRIDE, frame);
            if (process_live_clahe(last_cam, last_clahe) == 0) {
                clahe_frames++;
            }
        }
        if (loaded_edof_pairs > 0 && last_edof_in0 && last_edof_in1 && last_edof) {
            int next_pair = (frame / (FPS * EDOF_PAIR_SECONDS)) % loaded_edof_pairs;
            if (next_pair != edof_pair_index) {
                edof_pair_t *pair = get_loaded_edof_pair(next_pair);
                if (pair) {
                    image_to_nv12_frame(&pair->left, last_edof_in0);
                    image_to_nv12_frame(&pair->right, last_edof_in1);
                    image_to_nv12_frame(&pair->fused, last_edof);
                    if (live_edof_ok) {
                        (void)process_live_edof(last_edof_in0, last_edof_in1, last_edof);
                    }
                    edof_pair_index = next_pair;
                    edof_frames++;
                }
            }
        }
        if (loaded_mcf_pairs > 0 && last_mcf_color && last_mcf_mono && last_mcf) {
            int next_pair = (frame / (FPS * MCF_PAIR_SECONDS)) % loaded_mcf_pairs;
            if (next_pair != mcf_pair_index) {
                mcf_pair_t *pair = get_loaded_mcf_pair(next_pair);
                if (pair) {
                    image_to_nv12_frame(&pair->color, last_mcf_color);
                    image_to_nv12_frame(&pair->mono, last_mcf_mono);
                    image_to_nv12_frame(&pair->reference, last_mcf);
                    if (live_mcf_ok) {
                        (void)process_live_mcf(last_mcf_color, last_mcf_mono, last_mcf);
                    }
                    mcf_pair_index = next_pair;
                    mcf_frames++;
                }
            }
        }
        if (!solid_test && only_tile && strcasecmp(only_tile, "DUALVIEW") == 0 &&
            last_dual_in0 && last_dual_in1 &&
            last_dual_sbs && last_dual_lbl && dualview_frames == 0) {
            fill_dualview_demo_rgb(last_dual_in0, 1);
            fill_dualview_demo_rgb(last_dual_in1, 0);
            if (live_dualview_ok &&
                process_live_dualview_one(LIVE_DUALVIEW_SBS_GRP, last_dual_in0,
                                          last_dual_in1, last_dual_sbs) == 0 &&
                process_live_dualview_one(LIVE_DUALVIEW_LBL_GRP, last_dual_in0,
                                          last_dual_in1, last_dual_lbl) == 0) {
                dualview_frames++;
            }
        }
        if (!solid_test && only_tile && strcasecmp(only_tile, "PANO") == 0 &&
            last_pano && live_pano_ok && pano_frames == 0 &&
            process_live_pano(last_pano) == 0) {
            pano_frames++;
        } else if (!solid_test && only_tile && strcasecmp(only_tile, "PANO") == 0 &&
                   last_pano && !live_pano_ok && g_pano_sample.loaded && pano_frames == 0 &&
                   compose_pano_reference_preview(last_pano) == 0) {
            pano_frames++;
        }

        if (vi_bind_display_ok) {
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t resize_count = 0;
                char perf[160];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                if (use_vi_big_display) {
                    (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", LIVE_RESIZE_GRP, &resize_count);
                    if (g_perf.gpu_available) {
                        snprintf(perf, sizeof(perf), "IN %dx%d -> RGA %dx%d  VI %llu  CPU %.0f%% GPU %.0f%%",
                                 camera_w, camera_h, VI_BIG_VIEW_W, VI_BIG_VIEW_H,
                                 (unsigned long long)vi_count,
                                 g_perf.cpu_percent, g_perf.gpu_percent);
                    } else {
                        snprintf(perf, sizeof(perf), "IN %dx%d -> RGA %dx%d  VI %llu  CPU %.0f%% GPU N/A",
                                 camera_w, camera_h, VI_BIG_VIEW_W, VI_BIG_VIEW_H,
                                 (unsigned long long)vi_count,
                                 g_perf.cpu_percent);
                    }
                    (void)update_display_osd_text("VI LARGE INPUT", perf);
                } else {
                    if (g_perf.gpu_available) {
                        snprintf(perf, sizeof(perf), "PAGE 01/%02d  CPU %.0f%%  GPU %.0f%%  LIVE",
                                 module_page_total(), g_perf.cpu_percent, g_perf.gpu_percent);
                    } else {
                        snprintf(perf, sizeof(perf), "PAGE 01/%02d  CPU %.0f%%  GPU N/A  LIVE",
                                 module_page_total(), g_perf.cpu_percent);
                    }
                    (void)update_display_osd_text("VI TEST", perf);
                }
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    int display_w = use_vi_big_display ? VI_BIG_VIEW_W : camera_w;
                    int display_h = use_vi_big_display ? VI_BIG_VIEW_H : camera_h;
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                             g_perf.rga_percent);
                    printf("VI input=%dx%d display=%dx%d vi_frames=%llu resize_frames=%llu cpu=%.0f%% gpu=%s rga=%s\n",
                           camera_w, camera_h, display_w, display_h,
                           (unsigned long long)vi_count,
                           (unsigned long long)resize_count,
                           g_perf.cpu_percent, gpu_text, rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (wbc_bind_display_ok) {
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t wbc_count = 0;
                char perf[180];
                char runtime[128];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("VO_WBC", LIVE_WBC_CHN, &wbc_count);
                format_runtime_status(runtime, sizeof(runtime));
                snprintf(perf, sizeof(perf), "PAGE %02d/%02d WBC %llu VI %llu %s",
                         module_page_number("WBC"), module_page_total(),
                         (unsigned long long)wbc_count,
                         (unsigned long long)vi_count,
                         runtime);
                (void)update_display_osd_text("WBC  VI + WRITEBACK", perf);
                (void)update_wbc_bind_flow_overlay(vi_count, wbc_count, frame);
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                             g_perf.rga_percent);
                    printf("WBC vi_frames=%llu wbc_frames=%llu connector=%d crtc=%d cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)wbc_count,
                           g_wbc_connector_id,
                           g_wbc_crtc_id,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (vpss_bind_display_ok) {
            int crop_size = 320;
            int crop_x = pingpong_i(frame * 4, CAM_W - crop_size) & ~1;
            int crop_y = pingpong_i(frame * 3, CAM_H - crop_size) & ~1;
            int zoom_crop = 640 - (pingpong_i(frame * 2, 320) & ~1);
            int stable_vmix_page = only_tile && strcasecmp(only_tile, "VMIX") == 0;
            if (zoom_crop < 320) zoom_crop = 320;
            if (!stable_vmix_page && (frame % 4) == 0) {
                if (update_vpss_dynamic_attrs(frame) != 0 && (frame % FPS) == 0) {
                    fprintf(stderr, "warning: VPSS dynamic attr update rejected\n");
                }
            }
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t vpss_count = 0;
                uint64_t vmix_count = 0;
                char perf[128];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("VPSS", LIVE_VPSS_GRP, &vpss_count);
                if (use_vmix_bind_display) {
                    (void)MEDIA_SYS_GetModuleFrameCount("VMIX", DISPLAY_VMIX_GRP, &vmix_count);
                    if (g_perf.gpu_available) {
                        snprintf(perf, sizeof(perf), "PAGE %02d/%02d VMIX CPU %.0f%% GPU %.0f%% OUT %llu",
                                 module_page_number("VMIX"), module_page_total(),
                                 g_perf.cpu_percent, g_perf.gpu_percent,
                                 (unsigned long long)vmix_count);
                    } else {
                        snprintf(perf, sizeof(perf), "PAGE %02d/%02d VMIX CPU %.0f%% GPU N/A OUT %llu",
                                 module_page_number("VMIX"), module_page_total(),
                                 g_perf.cpu_percent,
                                 (unsigned long long)vmix_count);
                    }
                    (void)update_display_osd_text("VMIX  4 INPUT COMPOSITE", perf);
                    (void)update_vmix_bind_flow_overlay(vi_count, vmix_count, frame);
                    if ((frame % FPS) == 0) {
                        char gpu_text[24];
                        char rga_text[24];
                        snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                                 g_perf.gpu_percent);
                        snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                                 g_perf.rga_percent);
                        printf("VMIX vi_frames=%llu vmix_frames=%llu alpha_layout=4 cpu=%.0f%% gpu=%s rga=%s\n",
                               (unsigned long long)vi_count,
                               (unsigned long long)vmix_count,
                               g_perf.cpu_percent,
                               gpu_text,
                               rga_text);
                    }
                } else if (g_perf.gpu_available) {
                    snprintf(perf, sizeof(perf), "CROP %d,%d ZOOM %d CPU %.0f%% GPU %.0f%%",
                             crop_x, crop_y, zoom_crop, g_perf.cpu_percent, g_perf.gpu_percent);
                    (void)update_display_osd_text("VPSS TEST", perf);
                } else {
                    snprintf(perf, sizeof(perf), "CROP %d,%d ZOOM %d CPU %.0f%% GPU N/A",
                             crop_x, crop_y, zoom_crop, g_perf.cpu_percent);
                    (void)update_display_osd_text("VPSS TEST", perf);
                }
                if (!use_vmix_bind_display && (frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                             g_perf.rga_percent);
                    printf("VPSS vi_frames=%llu vpss_frames=%llu input=%dx%d outputs=4x%dx%d cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)vpss_count,
                           camera_w, camera_h, VPSS_TILE_W, VPSS_TILE_H,
                           g_perf.cpu_percent, gpu_text, rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (osd_bind_display_ok) {
            if ((frame % 2) == 0) {
                (void)update_live_osd_bind_regions(frame);
            }
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t resize_count = 0;
                uint64_t osd_count = 0;
                uint64_t display_osd_count = 0;
                char perf[160];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", LIVE_RESIZE_GRP, &resize_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", LIVE_OSD_GRP, &osd_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", DISPLAY_OSD_GRP, &display_osd_count);
                if (g_perf.gpu_available && g_perf.rga_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d OSD %llu CPU %.0f%% GPU %.0f%% RGA %.0f%%",
                             module_page_number("OSD"), module_page_total(),
                             (unsigned long long)osd_count,
                             g_perf.cpu_percent, g_perf.gpu_percent, g_perf.rga_percent);
                } else if (g_perf.gpu_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d OSD %llu CPU %.0f%% GPU %.0f%% RGA N/A",
                             module_page_number("OSD"), module_page_total(),
                             (unsigned long long)osd_count,
                             g_perf.cpu_percent, g_perf.gpu_percent);
                } else if (g_perf.rga_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d OSD %llu CPU %.0f%% GPU N/A RGA %.0f%%",
                             module_page_number("OSD"), module_page_total(),
                             (unsigned long long)osd_count,
                             g_perf.cpu_percent, g_perf.rga_percent);
                } else {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d OSD %llu CPU %.0f%% GPU N/A RGA N/A",
                             module_page_number("OSD"), module_page_total(),
                             (unsigned long long)osd_count,
                             g_perf.cpu_percent);
                }
                (void)update_display_osd_text("OSD  VI OSD VMIX OSD VO", perf);
                (void)update_osd_bind_flow_overlay(osd_count, display_osd_count,
                                                   osd_demo_params_for_frame(frame), frame);
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                             g_perf.rga_percent);
                    printf("OSD vi_frames=%llu resize_frames=%llu osd_frames=%llu display_osd_frames=%llu cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)resize_count,
                           (unsigned long long)osd_count,
                           (unsigned long long)display_osd_count,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (clahe_bind_display_ok) {
            float clip = 1.4f + 2.4f * (0.5f + 0.5f * sinf((float)frame * 0.035f));
            int passthrough_mode = ((frame / FPS) % 2) == 0;
            if ((frame % 15) == 0) {
                (void)MEDIA_CLAHE_SetClipLimit(LIVE_CLAHE_GRP, clip);
            }
            if ((frame % FPS) == 0) {
                (void)MEDIA_CLAHE_SetPassthrough(LIVE_CLAHE_GRP, passthrough_mode);
            }
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t clahe_count = 0;
                uint64_t effect_resize_count = 0;
                char perf[160];
                char runtime[128];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("CLAHE", LIVE_CLAHE_GRP, &clahe_count);
                (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP, &effect_resize_count);
                format_runtime_status(runtime, sizeof(runtime));
                snprintf(perf, sizeof(perf), "P%02d/%02d CLAHE 4K %s CLIP %.2f OUT %llu RESIZE %llu %s",
                         module_page_number("CLAHE"), module_page_total(),
                         passthrough_mode ? "PASS" : "ENHANCE",
                         clip, (unsigned long long)clahe_count,
                         (unsigned long long)effect_resize_count, runtime);
                (void)update_display_osd_text("CLAHE  4K PASSTHROUGH/ENHANCE", perf);
                (void)update_clahe_compare_overlay(vi_count, clahe_count,
                                                   effect_resize_count,
                                                   clip, passthrough_mode, frame);
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                             g_perf.rga_percent);
                    printf("CLAHE vi_frames=%llu clahe_out_frames=%llu resize_frames=%llu passthrough=%d clip=%.2f cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)clahe_count,
                           (unsigned long long)effect_resize_count,
                           passthrough_mode,
                           clip,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (retinex_bind_display_ok) {
            int passthrough_mode = ((frame / FPS) % 2) == 0;
            if ((frame % FPS) == 0) {
                (void)MEDIA_RETINEX_SetPassthrough(LIVE_RETINEX_GRP, passthrough_mode);
            }
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t retinex_count = 0;
                uint64_t effect_resize_count = 0;
                char perf[160];
                char runtime[128];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("RETINEX", LIVE_RETINEX_GRP, &retinex_count);
                (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP, &effect_resize_count);
                format_runtime_status(runtime, sizeof(runtime));
                snprintf(perf, sizeof(perf), "P%02d/%02d RETINEX 4K %s OUT %llu RESIZE %llu %s",
                         module_page_number("RETINEX"), module_page_total(),
                         passthrough_mode ? "PASS" : "ENHANCE",
                         (unsigned long long)retinex_count,
                         (unsigned long long)effect_resize_count, runtime);
                (void)update_display_osd_text("RETINEX  4K PASSTHROUGH/ENHANCE", perf);
                (void)update_retinex_compare_overlay(vi_count, retinex_count,
                                                     0, effect_resize_count,
                                                     frame);
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                             g_perf.rga_percent);
                    printf("RETINEX vi_frames=%llu retinex_frames=%llu resize_frames=%llu passthrough=%d gain=20.0 threshold=0.5 cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)retinex_count,
                           (unsigned long long)effect_resize_count,
                           passthrough_mode,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (cap_dehaze_bind_display_ok || dcp_dehaze_bind_display_ok) {
            int use_dcp = dcp_dehaze_bind_display_ok;
            int passthrough_mode = ((frame / FPS) % 2) == 0;
            const char *module_name = use_dcp ? "DCP_FAST_DEHAZE" : "CAP_DEHAZE";
            int module_grp = use_dcp ? LIVE_DCP_DEHAZE_GRP : LIVE_CAP_DEHAZE_GRP;
            if ((frame % FPS) == 0) {
                if (use_dcp) {
                    (void)MEDIA_DCP_FAST_DEHAZE_SetPassthrough(module_grp, passthrough_mode);
                } else {
                    (void)MEDIA_CAP_DEHAZE_SetPassthrough(module_grp, passthrough_mode);
                }
            }
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t csc_count = 0;
                uint64_t dehaze_count = 0;
                uint64_t resize_count = 0;
                uint64_t display_osd_count = 0;
                char perf[180];
                char gpu_text[24];
                char rga_text[24];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("CSC_RGA", LIVE_CSC_RGA_GRP, &csc_count);
                (void)MEDIA_SYS_GetModuleFrameCount(module_name, module_grp, &dehaze_count);
                (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", LIVE_COMPARE_EFFECT_RESIZE_GRP, &resize_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", DISPLAY_OSD_GRP, &display_osd_count);
                snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                         g_perf.gpu_percent);
                snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                         g_perf.rga_percent);
                snprintf(perf, sizeof(perf), "PAGE %02d/%02d %s 4K %s OUT %llu RESIZE %llu CPU %.0f%% GPU %s RGA %s",
                         module_page_number(module_name), module_page_total(),
                         use_dcp ? "DCP" : "CAP",
                         passthrough_mode ? "PASS" : "ENHANCE",
                         (unsigned long long)dehaze_count,
                         (unsigned long long)resize_count,
                         g_perf.cpu_percent, gpu_text, rga_text);
                (void)update_display_osd_text(use_dcp ? "DCP_FAST_DEHAZE  4K PASS/ENHANCE" :
                                                        "CAP_DEHAZE  4K PASS/ENHANCE",
                                              perf);
                if ((frame % FPS) == 0) {
                    printf("%s vi_frames=%llu csc_frames=%llu dehaze_frames=%llu resize_frames=%llu display_osd_frames=%llu passthrough=%d cpu=%.0f%% gpu=%s rga=%s\n",
                           module_name,
                           (unsigned long long)vi_count,
                           (unsigned long long)csc_count,
                           (unsigned long long)dehaze_count,
                           (unsigned long long)resize_count,
                           (unsigned long long)display_osd_count,
                           passthrough_mode,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (rga_bind_display_ok) {
            rga_op_index = (frame / (FPS * RGA_OP_SECONDS)) % (int)ARRAY_SIZE(g_rga_demo_ops);
            if (rga_op_index != last_rga_op_index || (rga_op_index == 1 && (frame % 4) == 0)) {
                if (set_live_rga_op(rga_op_index, frame) != 0) {
                    fprintf(stderr, "warning: RGA op %s rejected\n", g_rga_demo_ops[rga_op_index].label);
                } else {
                    last_rga_op_index = rga_op_index;
                }
            }
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t rga_count = 0;
                uint64_t display_osd_count = 0;
                char perf[160];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("RGA", LIVE_RGA_GRP, &rga_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", DISPLAY_OSD_GRP, &display_osd_count);
                if (g_perf.gpu_available && g_perf.rga_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d OP %s CPU %.0f%% GPU %.0f%% RGA %.0f%%",
                             module_page_number("RGA"), module_page_total(),
                             g_rga_demo_ops[rga_op_index].label,
                             g_perf.cpu_percent, g_perf.gpu_percent, g_perf.rga_percent);
                } else if (g_perf.gpu_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d OP %s CPU %.0f%% GPU %.0f%% RGA N/A",
                             module_page_number("RGA"), module_page_total(),
                             g_rga_demo_ops[rga_op_index].label,
                             g_perf.cpu_percent, g_perf.gpu_percent);
                } else if (g_perf.rga_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d OP %s CPU %.0f%% GPU N/A RGA %.0f%%",
                             module_page_number("RGA"), module_page_total(),
                             g_rga_demo_ops[rga_op_index].label,
                             g_perf.cpu_percent, g_perf.rga_percent);
                } else {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d OP %s CPU %.0f%% GPU N/A RGA N/A",
                             module_page_number("RGA"), module_page_total(),
                             g_rga_demo_ops[rga_op_index].label,
                             g_perf.cpu_percent);
                }
                (void)update_display_osd_text("RGA  VI RGA VMIX OSD VO", perf);
                (void)update_rga_bind_flow_overlay(rga_op_index, frame);
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    if (g_perf.gpu_available) {
                        snprintf(gpu_text, sizeof(gpu_text), "%.0f%%", g_perf.gpu_percent);
                    } else {
                        snprintf(gpu_text, sizeof(gpu_text), "N/A");
                    }
                    if (g_perf.rga_available) {
                        snprintf(rga_text, sizeof(rga_text), "%.0f%%", g_perf.rga_percent);
                    } else {
                        snprintf(rga_text, sizeof(rga_text), "N/A");
                    }
                    printf("RGA op=%s vi_frames=%llu rga_frames=%llu display_osd_frames=%llu cpu=%.0f%% gpu=%s rga=%s\n",
                           g_rga_demo_ops[rga_op_index].label,
                           (unsigned long long)vi_count,
                           (unsigned long long)rga_count,
                           (unsigned long long)display_osd_count,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (resize_bind_display_ok) {
            int param_frame = (frame / RESIZE_DEMO_UPDATE_FRAMES) * RESIZE_DEMO_UPDATE_FRAMES;
            resize_demo_params_t rp = resize_demo_params_for_frame(param_frame);
            if ((frame % RESIZE_DEMO_UPDATE_FRAMES) == 0) {
                if (reconfigure_resize_bind(rp) != 0) {
                    fprintf(stderr, "warning: RESIZE_RGA dynamic crop update rejected\n");
                }
            }
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t resize_count = 0;
                uint64_t display_osd_count = 0;
                char perf[160];
                float zoom = (float)CAM_W / (float)rp.crop_w;
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", LIVE_RESIZE_GRP, &resize_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", DISPLAY_OSD_GRP, &display_osd_count);
                if (g_perf.gpu_available && g_perf.rga_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d CROP %dx%d ZOOM %.2f CPU %.0f%% GPU %.0f%% RGA %.0f%%",
                             module_page_number("RESIZE_RGA"), module_page_total(),
                             rp.crop_w, rp.crop_h, zoom,
                             g_perf.cpu_percent, g_perf.gpu_percent, g_perf.rga_percent);
                } else if (g_perf.gpu_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d CROP %dx%d ZOOM %.2f CPU %.0f%% GPU %.0f%% RGA N/A",
                             module_page_number("RESIZE_RGA"), module_page_total(),
                             rp.crop_w, rp.crop_h, zoom,
                             g_perf.cpu_percent, g_perf.gpu_percent);
                } else if (g_perf.rga_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d CROP %dx%d ZOOM %.2f CPU %.0f%% GPU N/A RGA %.0f%%",
                             module_page_number("RESIZE_RGA"), module_page_total(),
                             rp.crop_w, rp.crop_h, zoom,
                             g_perf.cpu_percent, g_perf.rga_percent);
                } else {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d CROP %dx%d ZOOM %.2f CPU %.0f%% GPU N/A RGA N/A",
                             module_page_number("RESIZE_RGA"), module_page_total(),
                             rp.crop_w, rp.crop_h, zoom,
                             g_perf.cpu_percent);
                }
                (void)update_display_osd_text("RESIZE_RGA  VI RESIZE VMIX OSD VO", perf);
                (void)update_resize_bind_flow_overlay(rp, frame);
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                             g_perf.rga_percent);
                    printf("RESIZE_RGA vi_frames=%llu resize_frames=%llu display_osd_frames=%llu crop=%d,%d,%d,%d cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)resize_count,
                           (unsigned long long)display_osd_count,
                           rp.crop_x, rp.crop_y, rp.crop_w, rp.crop_h,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (csc_rga_bind_display_ok) {
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t csc_front_count = 0;
                uint64_t csc_back_count = 0;
                char perf[160];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("CSC_RGA", LIVE_CSC_RGA_GRP, &csc_front_count);
                (void)MEDIA_SYS_GetModuleFrameCount("CSC_RGA", LIVE_CSC_RGA_BACK_GRP, &csc_back_count);
                if (g_perf.gpu_available && g_perf.rga_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d NV12 ARGB NV12 CPU %.0f%% GPU %.0f%% RGA %.0f%%",
                             module_page_number("CSC_RGA"), module_page_total(),
                             g_perf.cpu_percent, g_perf.gpu_percent, g_perf.rga_percent);
                } else if (g_perf.gpu_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d NV12 ARGB NV12 CPU %.0f%% GPU %.0f%% RGA N/A",
                             module_page_number("CSC_RGA"), module_page_total(),
                             g_perf.cpu_percent, g_perf.gpu_percent);
                } else if (g_perf.rga_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d NV12 ARGB NV12 CPU %.0f%% GPU N/A RGA %.0f%%",
                             module_page_number("CSC_RGA"), module_page_total(),
                             g_perf.cpu_percent, g_perf.rga_percent);
                } else {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d NV12 ARGB NV12 CPU %.0f%% GPU N/A RGA N/A",
                             module_page_number("CSC_RGA"), module_page_total(),
                             g_perf.cpu_percent);
                }
                (void)update_display_osd_text("CSC_RGA  VI CSC CSC VMIX OSD VO", perf);
                (void)update_csc_rga_bind_flow_overlay(csc_front_count, csc_back_count, frame);
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char rga_text[24];
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                             g_perf.rga_percent);
                    printf("CSC_RGA vi_frames=%llu csc0_frames=%llu csc1_frames=%llu cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)csc_front_count,
                           (unsigned long long)csc_back_count,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (csc_cl_bind_display_ok) {
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t csc_front_count = 0;
                uint64_t osd_count = 0;
                MEDIA_CSC_CL_PERF perf0 = {0};
                double kernel_ms = -1.0;
                double queue_ms = -1.0;
                char perf[180];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("CSC_CL", LIVE_CSC_CL_GRP, &csc_front_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", DISPLAY_OSD_GRP, &osd_count);
                if (MEDIA_CSC_CL_GetLastPerf(LIVE_CSC_CL_GRP, &perf0) == 0) {
                    kernel_ms = perf0.gpu_kernel_total_ms;
                    queue_ms = perf0.gpu_queue_total_ms;
                }
                if (g_perf.gpu_available && kernel_ms >= 0.0) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d NV12 ARGB OSD CPU %.0f%% GPU %.0f%% CL %.2f/%.2fMS",
                             module_page_number("CSC_CL"), module_page_total(),
                             g_perf.cpu_percent, g_perf.gpu_percent,
                             kernel_ms, queue_ms >= 0.0 ? queue_ms : 0.0);
                } else if (g_perf.gpu_available) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d NV12 ARGB OSD CPU %.0f%% GPU %.0f%% CL N/A",
                             module_page_number("CSC_CL"), module_page_total(),
                             g_perf.cpu_percent, g_perf.gpu_percent);
                } else if (kernel_ms >= 0.0) {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d NV12 ARGB OSD CPU %.0f%% GPU N/A CL %.2f/%.2fMS",
                             module_page_number("CSC_CL"), module_page_total(),
                             g_perf.cpu_percent, kernel_ms, queue_ms >= 0.0 ? queue_ms : 0.0);
                } else {
                    snprintf(perf, sizeof(perf), "PAGE %02d/%02d NV12 ARGB OSD CPU %.0f%% GPU N/A CL N/A",
                             module_page_number("CSC_CL"), module_page_total(),
                             g_perf.cpu_percent);
                }
                (void)update_csc_cl_bind_flow_overlay(csc_front_count, osd_count,
                                                       kernel_ms, queue_ms, frame);
                if ((frame % FPS) == 0) {
                    char gpu_text[24];
                    char cl_text[48];
                    snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                             g_perf.gpu_percent);
                    if (kernel_ms >= 0.0) {
                        snprintf(cl_text, sizeof(cl_text), "%.2f/%.2fms",
                                 kernel_ms, queue_ms >= 0.0 ? queue_ms : 0.0);
                    } else {
                        snprintf(cl_text, sizeof(cl_text), "N/A");
                    }
                    printf("CSC_CL vi_frames=%llu csc_frames=%llu display_osd_frames=%llu cpu=%.0f%% gpu=%s cl=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)csc_front_count,
                           (unsigned long long)osd_count,
                           g_perf.cpu_percent,
                           gpu_text,
                           cl_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (conv_cl_bind_display_ok) {
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t conv_counts[VPSS_DEMO_OUTPUTS] = {0};
                double kernel_ms = 0.0;
                double queue_ms = 0.0;
                int perf_ok = 1;
                char perf[180];
                char gpu_text[24];
                char rga_text[24];
                char cl_text[48];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                for (int i = 0; i < VPSS_DEMO_OUTPUTS; ++i) {
                    MEDIA_CONV_CL_PERF cl_perf = {0};
                    (void)MEDIA_SYS_GetModuleFrameCount("CONV_CL", g_conv_cl_quad_grps[i],
                                                        &conv_counts[i]);
                    if (MEDIA_CONV_CL_GetLastPerf(g_conv_cl_quad_grps[i], &cl_perf) == 0 &&
                        cl_perf.gpu_kernel_total_ms >= 0.0 &&
                        cl_perf.gpu_queue_total_ms >= 0.0) {
                        kernel_ms += cl_perf.gpu_kernel_total_ms;
                        queue_ms += cl_perf.gpu_queue_total_ms;
                    } else {
                        perf_ok = 0;
                    }
                }
                if (!perf_ok) {
                    kernel_ms = -1.0;
                    queue_ms = -1.0;
                }
                snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                         g_perf.gpu_percent);
                snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                         g_perf.rga_percent);
                if (kernel_ms >= 0.0) {
                    snprintf(cl_text, sizeof(cl_text), "%.2f/%.2fms", kernel_ms, queue_ms);
                } else {
                    snprintf(cl_text, sizeof(cl_text), "N/A");
                }
                snprintf(perf, sizeof(perf), "PAGE %02d/%02d CONV_CL CPU %.0f%% GPU %s RGA %s CL %s",
                         module_page_number("CONV_CL"), module_page_total(),
                         g_perf.cpu_percent, gpu_text, rga_text, cl_text);
                (void)update_display_osd_text("CONV_CL  VMIX OSD VO", perf);
                (void)update_conv_cl_quad_overlay(vi_count, conv_counts, kernel_ms, queue_ms, frame);
                if ((frame % FPS) == 0) {
                    printf("CONV_CL vi_frames=%llu %s=%llu %s=%llu %s=%llu %s=%llu cpu=%.0f%% gpu=%s rga=%s cl=%s\n",
                           (unsigned long long)vi_count,
                           g_conv_cl_effects[0].name, (unsigned long long)conv_counts[0],
                           g_conv_cl_effects[1].name, (unsigned long long)conv_counts[1],
                           g_conv_cl_effects[2].name, (unsigned long long)conv_counts[2],
                           g_conv_cl_effects[3].name, (unsigned long long)conv_counts[3],
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text,
                           cl_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (transform_bind_display_ok) {
            int transform_mode = (frame / FPS) % VPSS_DEMO_OUTPUTS;
            if ((frame % FPS) == 0 &&
                update_transform_switch_lut(transform_mode, frame) != 0) {
                fprintf(stderr, "warning: TRANSFORM mode LUT update rejected mode=%d\n", transform_mode);
            }
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t vpss_count = 0;
                uint64_t transform_counts[VPSS_DEMO_OUTPUTS] = {0};
                uint64_t resize_counts[VPSS_DEMO_OUTPUTS] = {0};
                char perf[180];
                char gpu_text[24];
                char rga_text[24];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                if (MEDIA_SYS_GetModuleFrameCount("VPSS", LIVE_VPSS_GRP, &vpss_count) == 0) {
                    transform_counts[0] = vpss_count;
                } else {
                    transform_counts[0] = vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("TRANSFORM", g_transform_quad_grps[1],
                                                    &transform_counts[transform_mode]);
                (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", g_transform_display_resize_grps[0],
                                                    &resize_counts[transform_mode]);
                snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                         g_perf.gpu_percent);
                snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                         g_perf.rga_percent);
                snprintf(perf, sizeof(perf), "PAGE %02d/%02d TRANSFORM MODE %s CPU %.0f%% GPU %s RGA %s",
                         module_page_number("TRANSFORM"), module_page_total(),
                         g_transform_effects[transform_mode].name,
                         g_perf.cpu_percent, gpu_text, rga_text);
                (void)update_display_osd_text("TRANSFORM  4K LUT MODE SWITCH", perf);
                (void)update_transform_quad_overlay(vi_count, transform_counts, resize_counts, frame);
                if ((frame % FPS) == 0) {
                    printf("TRANSFORM vi_frames=%llu mode=%s vpss=%llu transform=%llu resize=%llu cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           g_transform_effects[transform_mode].name,
                           (unsigned long long)vpss_count,
                           (unsigned long long)transform_counts[transform_mode],
                           (unsigned long long)resize_counts[transform_mode],
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }
        if (stereo_bind_display_ok) {
            if ((frame % 15) == 0) {
                uint64_t vi_count = 0;
                uint64_t stereo_count = 0;
                char perf[180];
                char gpu_text[24];
                char rga_text[24];
                update_perf_status();
                if (MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count) == 0) {
                    g_health.camera_frames = (int)vi_count;
                }
                (void)MEDIA_SYS_GetModuleFrameCount("STEREO_3D", LIVE_STEREO_GRP,
                                                    &stereo_count);
                snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                         g_perf.gpu_percent);
                snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                         g_perf.rga_percent);
                snprintf(perf, sizeof(perf), "PAGE %02d/%02d RAW ROT90 MERGE CPU %.0f%% GPU %s RGA %s OUT %llu",
                         module_page_number("STEREO_3D"), module_page_total(),
                         g_perf.cpu_percent, gpu_text, rga_text,
                         (unsigned long long)stereo_count);
                (void)update_display_osd_text("STEREO_3D  RAW ROTATE", perf);
                (void)update_stereo_3d_overlay(vi_count, stereo_count, frame);
                if ((frame % FPS) == 0) {
                    printf("STEREO_3D vi_frames=%llu merged_frames=%llu rotate=%d cpu=%.0f%% gpu=%s rga=%s\n",
                           (unsigned long long)vi_count,
                           (unsigned long long)stereo_count,
                           STEREO_VPSS_ROTATE_DEGREES,
                           g_perf.cpu_percent,
                           gpu_text,
                           rga_text);
                }
            }
            maybe_capture_vo_channel_frame(only_tile, frame, dstride);
            frame++;
            usleep(1000000 / FPS);
            continue;
        }

        if (use_vmix_osd_display) {
            const uint8_t *display_src = NULL;
            if (strcasecmp(only_tile, "VI") == 0 && cam_frames > 0) {
                display_src = last_cam;
            } else if (strcasecmp(only_tile, "VPSS") == 0 && vpss_frames > 0) {
                display_src = last_vpss;
            }
            if (display_src &&
                send_vmix_osd_to_vo(display_src, only_tile, frame,
                                    module_page_total()) == 0) {
                frame++;
                usleep(1000000 / FPS);
                continue;
            }
        }

        int vo_demo_pending_apply = 0;
        if (vo_demo_enabled) {
            vo_demo_pending_apply = vo_demo_prepare(frame);
        }

        MEDIA_BUFFER dbuf = {-1, -1};
        if (MEDIA_POOL_GetBuffer(DISPLAY_POOL, &dbuf) != 0) {
            usleep(1000);
            continue;
        }
        void *addr = NULL;
        size_t size = 0;
        if (map_buffer(dbuf, &addr, &size, PROT_READ | PROT_WRITE) != 0) {
            MEDIA_POOL_PutBuffer(dbuf);
            continue;
        }
        if (MEDIA_POOL_BeginCpuAccess(dbuf, DMA_BUF_SYNC_WRITE) != 0) {
            munmap(addr, size);
            MEDIA_POOL_PutBuffer(dbuf);
            continue;
        }
        (void)size;
        if ((frame % 15) == 0) {
            update_perf_status();
        }
        display_refs_t display_refs = {
            cam_frames > 0 ? last_cam : NULL,
            osd_frames > 0 ? last_osd : NULL,
            resize_frames > 0 ? last_resize : NULL,
            vpss_frames > 0 ? last_vpss : NULL,
            csc_rga_frames > 0 ? last_csc_rga : NULL,
            transform_frames > 0 ? last_transform : NULL,
            cap_frames > 0 ? last_cap : NULL,
            dcp_frames > 0 ? last_dcp : NULL,
            conv_frames > 0 ? last_conv : NULL,
            clahe_frames > 0 ? last_clahe : NULL,
            retinex_frames > 0 ? last_retinex : NULL,
            pano_frames > 0 ? last_pano : NULL,
            last_edof_in0,
            last_edof_in1,
            edof_frames > 0 ? last_edof : NULL,
            last_mcf_color,
            last_mcf_mono,
            mcf_frames > 0 ? last_mcf : NULL,
            stereo_frames > 0 ? last_stereo : NULL,
            (only_tile && strcasecmp(only_tile, "DUALVIEW") == 0) ? last_dual_in0 : NULL,
            (only_tile && strcasecmp(only_tile, "DUALVIEW") == 0) ? last_dual_in1 : NULL,
            dualview_frames > 0 ? last_dual_sbs : NULL,
            dualview_frames > 0 ? last_dual_lbl : NULL
        };
        int thermal_cache_rendered = 0;
        int edof_cache_rendered = 0;
        int mcf_cache_rendered = 0;
        int pano_cache_rendered = 0;
        if (solid_test) {
            draw_solid_test((uint8_t *)addr, dstride);
        } else if (thermal_only_page && thermal_page_cache) {
            loop_asset_t *thermal_loop = find_loop_asset("THERMAL");
            if (thermal_loop && thermal_loop->loaded_count > 0) {
                int asset_index = (frame / (FPS * 3)) % thermal_loop->loaded_count;
                if (thermal_page_cache_asset != asset_index) {
                    draw_showcase_page_refs(thermal_page_cache, dstride, frame, rotate_main,
                                            only_tile, &display_refs);
                    thermal_page_cache_asset = asset_index;
                    thermal_cache_rendered = 1;
                }
                memcpy(addr, thermal_page_cache, display_size);
            } else {
                draw_showcase_page_refs((uint8_t *)addr, dstride, frame, rotate_main,
                                        only_tile, &display_refs);
            }
        } else if (edof_only_page && edof_page_cache) {
            if (loaded_edof_pairs > 0 && edof_pair_index >= 0) {
                if (edof_page_cache_sample != edof_pair_index) {
                    draw_showcase_page_refs(edof_page_cache, dstride, frame, rotate_main,
                                            only_tile, &display_refs);
                    edof_page_cache_sample = edof_pair_index;
                    edof_cache_rendered = 1;
                }
                memcpy(addr, edof_page_cache, display_size);
            } else {
                draw_showcase_page_refs((uint8_t *)addr, dstride, frame, rotate_main,
                                        only_tile, &display_refs);
            }
        } else if (mcf_only_page && mcf_page_cache) {
            if (loaded_mcf_pairs > 0 && mcf_pair_index >= 0) {
                if (mcf_page_cache_sample != mcf_pair_index) {
                    draw_showcase_page_refs(mcf_page_cache, dstride, frame, rotate_main,
                                            only_tile, &display_refs);
                    mcf_page_cache_sample = mcf_pair_index;
                    mcf_cache_rendered = 1;
                }
                memcpy(addr, mcf_page_cache, display_size);
            } else {
                draw_showcase_page_refs((uint8_t *)addr, dstride, frame, rotate_main,
                                        only_tile, &display_refs);
            }
        } else if (pano_only_page && pano_page_cache) {
            int generation = pano_frames > 0 ? pano_frames : 0;
            if (g_pano_sample.loaded) {
                if (pano_page_cache_generation != generation) {
                    draw_showcase_page_refs(pano_page_cache, dstride, frame, rotate_main,
                                            only_tile, &display_refs);
                    pano_page_cache_generation = generation;
                    pano_cache_rendered = 1;
                }
                memcpy(addr, pano_page_cache, display_size);
            } else {
                draw_showcase_page_refs((uint8_t *)addr, dstride, frame, rotate_main,
                                        only_tile, &display_refs);
            }
        } else {
            draw_showcase_page_refs((uint8_t *)addr, dstride, frame, rotate_main,
                                    only_tile, &display_refs);
        }
        if (thermal_only_page) {
            loop_asset_t *thermal_loop = find_loop_asset("THERMAL");
            int asset_count = thermal_loop ? thermal_loop->loaded_count : 0;
            int asset_number = asset_count > 0 ?
                ((frame / (FPS * 3)) % asset_count) + 1 : 0;
            const char *cache_state = thermal_page_cache ?
                (thermal_cache_rendered ? "refresh" : "hit") : "off";
            if (asset_count <= 0) cache_state = "no-assets";
            thermal_frames++;
            if ((thermal_frames % FPS) == 0 || thermal_cache_rendered) {
                char gpu_text[24];
                char rga_text[24];
                snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                         g_perf.gpu_percent);
                snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                         g_perf.rga_percent);
                printf("THERMAL frames=%d asset=%d/%d modes=16 cache=%s cpu=%.0f%% gpu=%s rga=%s\n",
                       thermal_frames, asset_number, asset_count, cache_state,
                       g_perf.cpu_percent, gpu_text, rga_text);
            }
        }
        if (only_tile && strcasecmp(only_tile, "EDOF_CL") == 0 &&
            (frame % FPS) == 0) {
            char gpu_text[24];
            char rga_text[24];
            int sample_number = edof_pair_index >= 0 ? edof_pair_index + 1 : 0;
            const char *cache_state = edof_page_cache ?
                (edof_cache_rendered ? "refresh" : "hit") : "off";
            if (loaded_edof_pairs <= 0 || edof_pair_index < 0) cache_state = "no-sample";
            snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                     g_perf.gpu_percent);
            snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                     g_perf.rga_percent);
            printf("EDOF_CL frames=%d sample=%d/%d updates=%d mode=%s cache=%s cpu=%.0f%% gpu=%s rga=%s\n",
                   frame, sample_number, loaded_edof_pairs, edof_frames,
                   live_edof_ok ? "opencl" : "reference", cache_state,
                   g_perf.cpu_percent, gpu_text, rga_text);
        }
        if (mcf_only_page && (frame % FPS) == 0) {
            MEDIA_MCF_FUSION_CL_PERF mcf_perf = {0};
            char gpu_text[24];
            char rga_text[24];
            char cl_text[64];
            int sample_number = mcf_pair_index >= 0 ? mcf_pair_index + 1 : 0;
            const char *cache_state = mcf_page_cache ?
                (mcf_cache_rendered ? "refresh" : "hit") : "off";
            if (loaded_mcf_pairs <= 0 || mcf_pair_index < 0) cache_state = "no-sample";
            snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                     g_perf.gpu_percent);
            snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                     g_perf.rga_percent);
            if (live_mcf_ok && MEDIA_MCF_FUSION_CL_GetLastPerf(LIVE_MCF_GRP, &mcf_perf) == 0 &&
                mcf_perf.gpu_total_ms >= 0.0) {
                snprintf(cl_text, sizeof(cl_text), "%.2f/%.2f/%.2fms",
                         mcf_perf.gpu_total_ms,
                         mcf_perf.stats_kernel_ms,
                         mcf_perf.fusion_kernel_ms);
            } else {
                snprintf(cl_text, sizeof(cl_text), "N/A");
            }
            printf("MCF_FUSION_CL frames=%d sample=%d/%d updates=%d mode=%s cache=%s cpu=%.0f%% gpu=%s rga=%s cl=%s\n",
                   frame, sample_number, loaded_mcf_pairs, mcf_frames,
                   live_mcf_ok ? "opencl" : "reference", cache_state,
                   g_perf.cpu_percent, gpu_text, rga_text, cl_text);
        }
        if (pano_only_page && (frame % FPS) == 0) {
            char gpu_text[24];
            char rga_text[24];
            const char *cache_state = pano_page_cache ?
                (pano_cache_rendered ? "refresh" : "hit") : "off";
            if (!g_pano_sample.loaded) cache_state = "no-sample";
            snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                     g_perf.gpu_percent);
            snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                     g_perf.rga_percent);
            printf("PANO frames=%d output=%d/%d mode=%s cache=%s pano=%dx%d domain=%dx%d cpu=%.0f%% gpu=%s rga=%s\n",
                   frame, pano_frames, loaded_pano_sample,
                   live_pano_ok ? "module-preview" : "reference-strip", cache_state,
                   PANO_STITCH_W, PANO_STITCH_H, PANO_DOMAIN_W, PANO_DOMAIN_H,
                   g_perf.cpu_percent, gpu_text, rga_text);
        }
        if (only_tile && strcasecmp(only_tile, "CAP_DEHAZE") == 0 &&
            (frame % FPS) == 0) {
            char gpu_text[24];
            char rga_text[24];
            snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                     g_perf.gpu_percent);
            snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                     g_perf.rga_percent);
            printf("CAP_DEHAZE vi_frames=%d cap_frames=%d guided_r=24 t0=0.12 cpu=%.0f%% gpu=%s rga=%s\n",
                   cam_frames, cap_frames, g_perf.cpu_percent, gpu_text, rga_text);
        }
        if (only_tile && strcasecmp(only_tile, "CONV_CL") == 0 &&
            (frame % FPS) == 0) {
            char gpu_text[24];
            char rga_text[24];
            char cl_text[48];
            snprintf(gpu_text, sizeof(gpu_text), g_perf.gpu_available ? "%.0f%%" : "N/A",
                     g_perf.gpu_percent);
            snprintf(rga_text, sizeof(rga_text), g_perf.rga_available ? "%.0f%%" : "N/A",
                     g_perf.rga_percent);
            if (g_conv_cl_show_kernel_ms >= 0.0) {
                snprintf(cl_text, sizeof(cl_text), "%.2f/%.2fms",
                         g_conv_cl_show_kernel_ms,
                         g_conv_cl_show_queue_ms >= 0.0 ? g_conv_cl_show_queue_ms : 0.0);
            } else {
                snprintf(cl_text, sizeof(cl_text), "N/A");
            }
            printf("CONV_CL vi_frames=%d %s=%llu %s=%llu %s=%llu %s=%llu cpu=%.0f%% gpu=%s rga=%s cl=%s mode=single-sequential\n",
                   cam_frames,
                   g_conv_cl_effects[0].name,
                   (unsigned long long)g_conv_cl_show_counts[0],
                   g_conv_cl_effects[1].name,
                   (unsigned long long)g_conv_cl_show_counts[1],
                   g_conv_cl_effects[2].name,
                   (unsigned long long)g_conv_cl_show_counts[2],
                   g_conv_cl_effects[3].name,
                   (unsigned long long)g_conv_cl_show_counts[3],
                   g_perf.cpu_percent,
                   gpu_text,
                   rga_text,
                   cl_text);
        }
        maybe_capture_module_vo_frame(only_tile, frame, (const uint8_t *)addr, dstride);
        (void)MEDIA_POOL_EndCpuAccess(dbuf, DMA_BUF_SYNC_WRITE);
        munmap(addr, size);

        if (MEDIA_SYS_SendFrame("VO", 0, "input0", dbuf, 1000) != 0) {
            MEDIA_POOL_PutBuffer(dbuf);
        } else if (vo_demo_enabled && vo_demo_pending_apply) {
            vo_demo_apply_after_send();
        }
        frame++;
        usleep(1000000 / FPS);
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    alarm(5);

    if (vo_demo_enabled) vo_demo_restore_output();
    unbind_vi_wbc_vmix_osd_vo(wbc_bind_display_ok);
    unbind_vi_vpss_stereo_vmix_osd_vo(stereo_bind_display_ok);
    unbind_vi_vpss_transform_vmix_osd_vo(transform_bind_display_ok);
    unbind_vi_vpss_conv_cl_vmix_osd_vo(conv_cl_bind_display_ok);
    unbind_vpss_vmix_osd_vo(vpss_bind_display_ok);
    unbind_vi_csc_cl_chain_vmix_osd_vo(csc_cl_bind_display_ok);
    unbind_vi_csc_chain_vmix_osd_vo(csc_rga_bind_display_ok);
    unbind_vi_dehaze_vmix_osd_vo(cap_dehaze_bind_display_ok || dcp_dehaze_bind_display_ok,
                                 dcp_dehaze_bind_display_ok);
    unbind_vi_resize_vmix_osd_vo(resize_bind_display_ok || vi_resize_bind_display_ok);
    unbind_vi_rga_vmix_osd_vo(rga_bind_display_ok);
    unbind_vi_retinex_vmix_osd_vo(retinex_bind_display_ok);
    unbind_vi_clahe_vmix_osd_vo(clahe_bind_display_ok);
    unbind_vi_live_osd_vmix_osd_vo(osd_bind_display_ok);
    unbind_vi_vmix_osd_vo(vi_bind_display_ok && !vi_resize_bind_display_ok);
    if (camera_ok) MEDIA_VI_Disable(0);
    if (cap_dehaze_bind_display_ok || dcp_dehaze_bind_display_ok) {
        usleep(1500000);
    }
    if (resize_bind_display_ok || rga_bind_display_ok || vi_resize_bind_display_ok) {
        usleep(100000);
        drain_display_osd_output();
        drain_display_vmix_output();
    }
    if (csc_cl_bind_display_ok || display_vmix_rga_osd_ok) {
        stop_destroy_vo_channel();
    }
    if (display_vmix_rga_osd_ok) {
        cleanup_live_compare_display_resizes(live_compare_display_resizes_ok);
        live_compare_display_resizes_ok = 0;
        cleanup_display_vmix_rga_osd(display_vmix_osd_ok);
        display_vmix_osd_ok = 0;
    }
    if (!csc_cl_bind_display_ok && !display_vmix_rga_osd_ok) {
        stop_destroy_vo_channel();
    }
    cleanup_live_resize_bind(live_resize_bind_ok || live_vi_resize_bind_ok);
    live_resize_bind_ok = 0;
    live_vi_resize_bind_ok = 0;
    cleanup_live_rga(live_rga_ok);
    live_rga_ok = 0;
    cleanup_live_cap_dehaze_bind_chain(live_dehaze_bind_ok, dcp_dehaze_bind_display_ok);
    live_dehaze_bind_ok = 0;
    cleanup_display_vmix_osd_ex(display_vmix_osd_ok, !transform_bind_display_ok);
    display_vmix_osd_ok = 0;
    cleanup_live_wbc(live_wbc_ok);
    cleanup_live_stereo_bind(live_stereo_bind_ok);
    cleanup_live_transform_quad(live_transform_quad_ok);
    cleanup_live_conv_cl_quad(live_conv_cl_quad_ok);
    cleanup_live_csc_cl_chain(live_csc_cl_chain_ok);
    cleanup_live_csc_rga_chain(live_csc_rga_chain_ok);
    cleanup_live_camera_to_cam_resize_bind(live_pre_resize_bind_ok);
    cleanup_live_osd_bind(live_osd_bind_ok);
    cleanup_live_clahe_bind(live_clahe_bind_ok);
    cleanup_live_retinex_bind(live_retinex_bind_ok);
    cleanup_live_stereo(live_stereo_ok);
    cleanup_live_pano(live_pano_ok);
    cleanup_live_dualview(live_dualview_ok);
    cleanup_live_mcf(live_mcf_ok);
    cleanup_live_edof(live_edof_ok);
    cleanup_live_retinex(live_retinex_ok);
    cleanup_live_clahe(live_clahe_ok);
    cleanup_live_conv_cl(live_conv_ok);
    cleanup_live_dcp_dehaze(live_dcp_ok);
    cleanup_live_cap_dehaze(live_cap_ok);
    cleanup_live_transform(live_transform_ok);
    cleanup_live_csc_rga(live_csc_rga_ok);
    cleanup_live_vpss(live_vpss_ok);
    cleanup_live_resize(live_resize_ok);
    cleanup_live_osd(live_osd_ok);
    MEDIA_POOL_Destroy(DISPLAY_POOL);
    if (need_camera && !live_cap_ok && !live_dcp_ok && !live_conv_ok) MEDIA_POOL_Destroy(CAMERA_POOL);
    MEDIA_POOL_Destroy(WORK_POOL_NV12);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
    MEDIA_POOL_Destroy(WORK_POOL_OUT);
    unload_pano_sample();
    unload_mcf_pairs();
    unload_edof_pairs();
    unload_loop_assets();
    free(thermal_page_cache);
    free(edof_page_cache);
    free(mcf_page_cache);
    free(pano_page_cache);
    free(last_stereo);
    free(last_rgba_src);
    free(last_rgb_src);
    free(last_dual_lbl);
    free(last_dual_sbs);
    free(last_dual_in1);
    free(last_dual_in0);
    free(last_pano);
    free(last_mcf);
    free(last_mcf_mono);
    free(last_mcf_color);
    free(last_edof);
    free(last_edof_in1);
    free(last_edof_in0);
    free(last_retinex);
    free(last_clahe);
    free(last_conv_tmp);
    free(last_conv);
    free(last_dcp);
    free(last_cap);
    free(last_transform);
    free(last_csc_rga);
    free(last_vpss);
    free(last_resize);
    free(last_osd);
    free(last_cam);
    MEDIA_SYS_Exit();
    return 0;
}
