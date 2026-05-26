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
#define OSD_SCREEN_W 1080
#define OSD_SCREEN_H 1920
#define OSD_SCREEN_STRIDE ALIGN_UP_LOCAL(OSD_SCREEN_W, 64)
#define OSD_CAMERA_POOL 2
#define OSD_RESIZE_POOL 9
#define OSD_OUTPUT_POOL 7
#define OSD_VMIX_POOL 8
#define OSD_DISPLAY_POOL 10
#define OSD_RESIZE_GRP 61
#define OSD_GRP 60
#define OSD_VMIX_GRP 80
#define OSD_DISPLAY_GRP 82
#define OSD_SRC_W 3840
#define OSD_SRC_H 2160
#define OSD_SRC_STRIDE 3840
#define OSD_VIEW_W 1080
#define OSD_VIEW_H 608
#define OSD_VIEW_STRIDE ALIGN_UP_LOCAL(OSD_VIEW_W, 64)
#define OSD_VIEW_X 0
#define OSD_VIEW_Y 320
#define OSD_FPS 30
#define OSD_CAMERA_DEVICE "/dev/video-camera0"
#define OSD_SRC_SIZE ((size_t)OSD_SRC_STRIDE * (size_t)OSD_SRC_H * 3u / 2u)
#define OSD_VIEW_SIZE ((size_t)OSD_VIEW_STRIDE * (size_t)OSD_VIEW_H * 3u / 2u)
#define OSD_DISPLAY_SIZE ((size_t)OSD_SCREEN_STRIDE * (size_t)OSD_SCREEN_H * 3u / 2u)
#define OSD_TEXT_MASK_W 1024
#define OSD_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vmix_pool_ok;
    int display_pool_ok;
    int vi_attr_ok;
    int resize_ok;
    int osd_ok;
    int vmix_ok;
    int display_osd_ok;
    int vo_ok;
    int bind_vi_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vmix_ok;
    int bind_vmix_display_ok;
    int bind_display_vo_ok;
    int vi_enabled;
} osd_chain_t;

static int set_rect_region_on(int osd_grp, int region, int x, int y, int w, int h,
                              int zorder, uint8_t r, uint8_t g, uint8_t b,
                              uint8_t alpha, int filled, int enabled) {
    MEDIA_OSD_REGION_ATTR attr = {0};
    MEDIA_OSD_RECT_DESC rect = {0};
    attr.enabled = enabled;
    attr.x = x;
    attr.y = y;
    attr.width = w;
    attr.height = h;
    attr.zorder = zorder;
    attr.global_alpha = alpha;
    rect.filled = filled;
    rect.line_width = filled ? 1 : 7;
    rect.color.r = r;
    rect.color.g = g;
    rect.color.b = b;
    rect.color.a = alpha;
    if (MEDIA_OSD_UpdateRegion(osd_grp, region, &attr) != 0 ||
        MEDIA_OSD_SetRegionRect(osd_grp, region, &rect) != 0) {
        return -1;
    }
    return 0;
}

static int set_rect_region(int region, int x, int y, int w, int h,
                           int zorder, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t alpha, int filled, int enabled) {
    return set_rect_region_on(OSD_GRP, region, x, y, w, h, zorder,
                              r, g, b, alpha, filled, enabled);
}

static int update_osd_regions(int frame) {
    int box_w = 420;
    int box_h = 220;
    int box_x = 64 + ((frame * 5) % (OSD_VIEW_W - box_w - 128));
    int box_y = 54 + ((frame * 3) % (OSD_VIEW_H - box_h - 132));
    int bar_w = 240 + ((frame * 7) % 520);
    int scan_x = (frame * 13) % (OSD_VIEW_W - 28);
    int alert = ((frame / 18) % 2) == 0;
    uint8_t alpha = (uint8_t)(120 + ((frame * 5) % 100));

    if (set_rect_region(0, box_x, box_y, box_w, 7, 0, 0, 255, 190, 255, 1, 1) != 0 ||
        set_rect_region(1, box_x, box_y + box_h - 7, box_w, 7, 0, 0, 255, 190, 255, 1, 1) != 0 ||
        set_rect_region(2, box_x, box_y, 7, box_h, 0, 0, 255, 190, 255, 1, 1) != 0 ||
        set_rect_region(3, box_x + box_w - 7, box_y, 7, box_h, 0, 0, 255, 190, 255, 1, 1) != 0 ||
        set_rect_region(4, 56, OSD_VIEW_H - 78, bar_w, 34, 1, 255, 220, 70, alpha, 1, 1) != 0 ||
        set_rect_region(5, scan_x, 42, 28, OSD_VIEW_H - 84, 2, 255, 80, 80, 210, 1, 1) != 0 ||
        set_rect_region(6, OSD_VIEW_W - 220, 48, 164, 54, 3, 255, 60, 90, 215, 1, alert) != 0 ||
        set_rect_region(7, 48, OSD_VIEW_H - 94, 760, 64, 0, 8, 18, 30, 150, 1, 1) != 0) {
        return -1;
    }
    return 0;
}

static int update_osd_info_overlay(uint64_t vi_count, uint64_t resize_count,
                                   uint64_t osd_count, uint64_t vmix_count,
                                   uint64_t vo_count) {
    static uint8_t masks[4][OSD_TEXT_MASK_W * OSD_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char count_line[160];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(count_line, sizeof(count_line), "VI %llu  RGA %llu  OSD %llu  VMIX %llu  VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vmix_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(OSD_GRP, 8, 28, 16, 2,
                              "OSD LIVE VI DYNAMIC REGIONS",
                              masks[0], sizeof(masks[0]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_GRP, 9, 28, 522, 2,
                              "FLOW VI->RESIZE_RGA->OSD->VMIX->VO",
                              masks[1], sizeof(masks[1]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_GRP, 10, 28, 548, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_GRP, 11, 28, 574, 2,
                              count_line,
                              masks[3], sizeof(masks[3]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int update_osd_display_overlay(uint64_t vi_count, uint64_t resize_count,
                                      uint64_t osd_count, uint64_t vmix_count,
                                      uint64_t vo_count, int frame) {
    static uint8_t masks[24][OSD_TEXT_MASK_W * OSD_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char region_line[160];
    char count_line[160];
    char flow_line[180];
    int box_w = 420;
    int box_h = 220;
    int box_x = 64 + ((frame * 5) % (OSD_VIEW_W - box_w - 128));
    int box_y = 54 + ((frame * 3) % (OSD_VIEW_H - box_h - 132));
    int bar_w = 240 + ((frame * 7) % 520);
    int alert = ((frame / 18) % 2) == 0;
    uint8_t alpha = (uint8_t)(120 + ((frame * 5) % 100));

    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(region_line, sizeof(region_line),
             "REGIONS BOX(%d,%d) BAR %d ALPHA %u ALERT %s",
             box_x, box_y, bar_w, (unsigned)alpha, alert ? "ON" : "OFF");
    snprintf(count_line, sizeof(count_line),
             "COUNTS VI %llu RGA %llu OSD %llu VMIX %llu VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vmix_count,
             (unsigned long long)vo_count);
    snprintf(flow_line, sizeof(flow_line),
             "FLOW VI 3840X2160 -> RESIZE_RGA 1080X608 -> DYNAMIC OSD -> VMIX -> PAGE OSD -> VO");

    if (set_rect_region_on(OSD_DISPLAY_GRP, 0, 34, 968, 1012, 666,
                           0, 10, 18, 30, 226, 1, 1) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 1, 66, 996, 3,
                              "OSD VIDEO OVERLAY AND DYNAMIC REGIONS",
                              masks[0], sizeof(masks[0]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 2, 68, 1052, 2,
                              "VI FRAMES ENTER OSD; BOX, BAR AND ALERT REGIONS ARE OVERLAID BEFORE DISPLAY.",
                              masks[1], sizeof(masks[1]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 3, 68, 1096, 2,
                              "OSD UPDATES ONLY CHANGED REGIONS WITHOUT CPU FULL-FRAME REDRAW.",
                              masks[2], sizeof(masks[2]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;

    const int card_y = 1172;
    const int card_w = 168;
    const int card_h = 156;
    const int xs[5] = {52, 248, 444, 640, 836};
    const char *titles[5] = {"VI", "OSD", "VMIX", "PAGE OSD", "VO"};
    const char *values[5] = {"FRAME", "REGION", "LAYOUT", "INFO", "DISPLAY"};
    for (int i = 0; i < 5; ++i) {
        int highlight = (i == 1 || i == 3);
        if (set_rect_region_on(OSD_DISPLAY_GRP, 4 + i, xs[i], card_y,
                               card_w, card_h, 1,
                               highlight ? 16 : 12,
                               highlight ? 46 : 28,
                               highlight ? 72 : 46,
                               240, 1, 1) != 0) return -1;
        if (page_overlay_set_text(OSD_DISPLAY_GRP, 9 + i, xs[i] + 18, card_y + 16, 2,
                                  titles[i], masks[3 + i], sizeof(masks[3 + i]),
                                  OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                                  160, 255, 220, 255) != 0) return -1;
        if (page_overlay_set_text(OSD_DISPLAY_GRP, 14 + i, xs[i] + 18, card_y + 62, 2,
                                  values[i], masks[8 + i], sizeof(masks[8 + i]),
                                  OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                                  255, 230, 120, 255) != 0) return -1;
    }

    if (page_overlay_set_text(OSD_DISPLAY_GRP, 19, 214, card_y + 54, 3,
                              ">", masks[13], sizeof(masks[13]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 20, 410, card_y + 54, 3,
                              ">", masks[14], sizeof(masks[14]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 21, 606, card_y + 54, 3,
                              ">", masks[15], sizeof(masks[15]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 22, 802, card_y + 54, 3,
                              ">", masks[16], sizeof(masks[16]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;

    int packet_x = 58 + ((frame * 18) % 890);
    if (set_rect_region_on(OSD_DISPLAY_GRP, 23, packet_x, 1350, 58, 14,
                           2, 255, 255, 255, 190, 1, 1) != 0) return -1;
    if (set_rect_region_on(OSD_DISPLAY_GRP, 24, 74, 1382, 240, 34,
                           2, 0, 255, 190, 220, 1, 1) != 0) return -1;
    if (set_rect_region_on(OSD_DISPLAY_GRP, 25, 74, 1436, alpha * 2, 34,
                           2, 255, 220, 70, alpha, 1, 1) != 0) return -1;
    if (set_rect_region_on(OSD_DISPLAY_GRP, 26, 74, 1490, 180, 34,
                           2, 255, 80, 80, alert ? 220 : 70, 1, 1) != 0) return -1;

    if (page_overlay_set_text(OSD_DISPLAY_GRP, 27, 342, 1372, 2,
                              "TARGET BOX POSITION UPDATES",
                              masks[17], sizeof(masks[17]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 28, 342, 1426, 2,
                              "STATUS BAR LENGTH AND ALPHA CHANGE",
                              masks[18], sizeof(masks[18]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 29, 342, 1480, 2,
                              "ALERT BLOCK TOGGLES ON AND OFF",
                              masks[19], sizeof(masks[19]), OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              255, 150, 160, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 30, 72, 1530, 2,
                              region_line, masks[20], sizeof(masks[20]),
                              OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              170, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 31, 72, 1562, 2,
                              count_line, masks[21], sizeof(masks[21]),
                              OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              170, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 32, 72, 1594, 2,
                              flow_line, masks[22], sizeof(masks[22]),
                              OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              170, 205, 235, 255) != 0) return -1;
    if (page_overlay_set_text(OSD_DISPLAY_GRP, 33, 72, 1624, 2,
                              perf_line, masks[23], sizeof(masks[23]),
                              OSD_TEXT_MASK_W, OSD_TEXT_MASK_H,
                              170, 205, 235, 255) != 0) return -1;
    return 0;
}

static void drain_resize_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_RESIZE_RGA_GetFrame(OSD_RESIZE_GRP, &out, 0) != 0) break;
        MEDIA_RESIZE_RGA_ReleaseFrame(OSD_RESIZE_GRP, out);
    }
}

static void drain_osd_output_grp(int osd_grp) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(osd_grp, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(osd_grp, out);
    }
}

static void drain_osd_output(void) {
    drain_osd_output_grp(OSD_GRP);
}

static void drain_vmix_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_VMIX_GetFrame(OSD_VMIX_GRP, &out, 0) != 0) break;
        MEDIA_VMIX_ReleaseFrame(OSD_VMIX_GRP, out);
    }
}

static int setup_osd_chain(osd_chain_t *chain) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VMIX_ATTR vmix = {0};
    MEDIA_OSD_ATTR display = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(OSD_CAMERA_POOL, OSD_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(OSD_RESIZE_POOL, OSD_VIEW_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, OSD_VIEW_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;
    if (MEDIA_POOL_Create(OSD_VMIX_POOL, OSD_DISPLAY_SIZE, 4) != 0) return -1;
    chain->vmix_pool_ok = 1;
    if (MEDIA_POOL_Create(OSD_DISPLAY_POOL, OSD_DISPLAY_SIZE, 4) != 0) return -1;
    chain->display_pool_ok = 1;

    vi.device = OSD_CAMERA_DEVICE;
    vi.width = OSD_SRC_W;
    vi.height = OSD_SRC_H;
    vi.stride = OSD_SRC_STRIDE;
    vi.fps = OSD_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = OSD_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = OSD_SRC_W;
    resize.src_height = OSD_SRC_H;
    resize.input_width = OSD_SRC_W;
    resize.input_height = OSD_SRC_H;
    resize.input_stride = OSD_SRC_STRIDE;
    resize.input_format = MEDIA_FORMAT_NV12;
    resize.input_depth = 6;
    resize.out_width = OSD_VIEW_W;
    resize.out_height = OSD_VIEW_H;
    resize.out_stride = OSD_VIEW_STRIDE;
    resize.output_format = MEDIA_FORMAT_NV12;
    resize.output_pool_id = OSD_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(OSD_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(OSD_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(OSD_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = OSD_VIEW_W;
    osd.input_height = OSD_VIEW_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = OSD_OUTPUT_POOL;
    osd.input_stride = OSD_VIEW_STRIDE;
    osd.output_stride = OSD_VIEW_STRIDE;
    osd.max_regions = 12;
    if (MEDIA_OSD_CreateGrp(OSD_GRP, &osd) != 0 ||
        update_osd_regions(0) != 0 ||
        MEDIA_OSD_Start(OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vmix.input_count = 1;
    vmix.output_width = OSD_SCREEN_W;
    vmix.output_height = OSD_SCREEN_H;
    vmix.output_stride = OSD_SCREEN_STRIDE;
    vmix.format = MEDIA_FORMAT_NV12;
    vmix.input_depth = 4;
    vmix.output_pool_id = OSD_VMIX_POOL;
    vmix.primary_index = -1;
    vmix.channels[0].enabled = 1;
    vmix.channels[0].x = OSD_VIEW_X;
    vmix.channels[0].y = OSD_VIEW_Y;
    vmix.channels[0].width = OSD_VIEW_W;
    vmix.channels[0].height = OSD_VIEW_H;
    vmix.channels[0].alpha = 1.0f;
    vmix.channels[0].stride = OSD_VIEW_STRIDE;
    vmix.channels[0].format = MEDIA_FORMAT_NV12;
    if (MEDIA_VMIX_CreateGrp(OSD_VMIX_GRP, &vmix) != 0 ||
        MEDIA_VMIX_Start(OSD_VMIX_GRP) != 0 ||
        MEDIA_VMIX_Enable(OSD_VMIX_GRP) != 0) {
        return -1;
    }
    chain->vmix_ok = 1;

    display.input_width = OSD_SCREEN_W;
    display.input_height = OSD_SCREEN_H;
    display.format = MEDIA_FORMAT_NV12;
    display.input_depth = 4;
    display.output_pool_id = OSD_DISPLAY_POOL;
    display.input_stride = OSD_SCREEN_STRIDE;
    display.output_stride = OSD_SCREEN_STRIDE;
    display.max_regions = 40;
    if (MEDIA_OSD_CreateGrp(OSD_DISPLAY_GRP, &display) != 0 ||
        update_osd_display_overlay(0, 0, 0, 0, 0, 0) != 0 ||
        MEDIA_OSD_Start(OSD_DISPLAY_GRP) != 0) {
        return -1;
    }
    chain->display_osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = OSD_SCREEN_W;
    vo.height = OSD_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, 0, 0, OSD_SCREEN_W, OSD_SCREEN_H,
                           OSD_SCREEN_STRIDE, 4, MEDIA_VO_PLANE_TYPE_AUTO,
                           MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output", "RESIZE_RGA", OSD_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_vi_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", OSD_RESIZE_GRP, "output0", "OSD", OSD_GRP, "input") != 0) return -1;
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", OSD_GRP, "output0", "VMIX", OSD_VMIX_GRP, "input0") != 0) return -1;
    chain->bind_osd_vmix_ok = 1;
    if (MEDIA_SYS_Bind("VMIX", OSD_VMIX_GRP, "output0",
                       "OSD", OSD_DISPLAY_GRP, "input") != 0) return -1;
    chain->bind_vmix_display_ok = 1;
    if (MEDIA_SYS_Bind("OSD", OSD_DISPLAY_GRP, "output0", "VO", 0, "input0") != 0) return -1;
    chain->bind_display_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VMIX", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_osd_chain(osd_chain_t *chain) {
    if (!chain) return;
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_display_vo_ok) {
        MEDIA_SYS_UnBind("OSD", OSD_DISPLAY_GRP, "output0", "VO", 0, "input0");
        chain->bind_display_vo_ok = 0;
    }
    if (chain->bind_vmix_display_ok) {
        MEDIA_SYS_UnBind("VMIX", OSD_VMIX_GRP, "output0",
                         "OSD", OSD_DISPLAY_GRP, "input");
        chain->bind_vmix_display_ok = 0;
    }
    if (chain->bind_osd_vmix_ok) {
        MEDIA_SYS_UnBind("OSD", OSD_GRP, "output0", "VMIX", OSD_VMIX_GRP, "input0");
        chain->bind_osd_vmix_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", OSD_RESIZE_GRP, "output0", "OSD", OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_vi_resize_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output", "RESIZE_RGA", OSD_RESIZE_GRP, "input0");
        chain->bind_vi_resize_ok = 0;
    }
    if (chain->display_osd_ok) {
        drain_osd_output_grp(OSD_DISPLAY_GRP);
        MEDIA_OSD_Stop(OSD_DISPLAY_GRP);
        drain_osd_output_grp(OSD_DISPLAY_GRP);
        MEDIA_OSD_DestroyGrp(OSD_DISPLAY_GRP);
        chain->display_osd_ok = 0;
    }
    if (chain->vmix_ok) {
        drain_vmix_output();
        MEDIA_VMIX_Disable(OSD_VMIX_GRP);
        MEDIA_VMIX_Stop(OSD_VMIX_GRP);
        drain_vmix_output();
        MEDIA_VMIX_DestroyGrp(OSD_VMIX_GRP);
        chain->vmix_ok = 0;
    }
    if (chain->osd_ok) {
        drain_osd_output();
        MEDIA_OSD_Stop(OSD_GRP);
        drain_osd_output();
        MEDIA_OSD_DestroyGrp(OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_resize_output();
        MEDIA_RESIZE_RGA_Disable(OSD_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(OSD_RESIZE_GRP);
        drain_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(OSD_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->vmix_pool_ok) {
        MEDIA_POOL_Destroy(OSD_VMIX_POOL);
        chain->vmix_pool_ok = 0;
    }
    if (chain->display_pool_ok) {
        MEDIA_POOL_Destroy(OSD_DISPLAY_POOL);
        chain->display_pool_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(OSD_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(OSD_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_osd_run(volatile sig_atomic_t *running) {
    osd_chain_t chain = {0};
    if (setup_osd_chain(&chain) != 0) {
        fprintf(stderr, "OSD standalone VI chain setup failed\n");
        cleanup_osd_chain(&chain);
        return 1;
    }

    printf("OSD standalone: VI %s %dx%d -> RESIZE_RGA %dx%d -> OSD -> VMIX -> VO. Ctrl+C to stop.\n",
           OSD_CAMERA_DEVICE, OSD_SRC_W, OSD_SRC_H, OSD_VIEW_W, OSD_VIEW_H);
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vmix_count = 0;
        uint64_t vo_count = 0;
        sleep(1);
        tick++;
        (void)update_osd_regions(tick * OSD_FPS);
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", OSD_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VMIX", OSD_VMIX_GRP, &vmix_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_osd_info_overlay(vi_count, resize_count, osd_count, vmix_count, vo_count) == 0;
        int display_overlay_ok = update_osd_display_overlay(vi_count, resize_count, osd_count,
                                                            vmix_count, vo_count,
                                                            tick * OSD_FPS) == 0;
        printf("OSD vi=%llu resize=%llu osd=%llu vmix=%llu vo=%llu regions=12 tick=%d overlay=%s standalone=1\n",
               (unsigned long long)vi_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vmix_count,
               (unsigned long long)vo_count,
               tick, (overlay_ok && display_overlay_ok) ? "perf_text" : "failed");
    }

    cleanup_osd_chain(&chain);
    return 0;
}
