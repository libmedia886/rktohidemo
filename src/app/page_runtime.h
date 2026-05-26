#ifndef ALLDEMO_PAGE_RUNTIME_H
#define ALLDEMO_PAGE_RUNTIME_H

#include "page_registry.h"

typedef struct {
    const char *only_tile;
    int active;
    int need_camera;
    int use_4k_camera_input;
    int use_1080p_camera_input;

    page_bind_display_t bind_display;
    int use_vmix_osd_display;
    int use_vi_bind_display;
    int use_wbc_bind_display;
    int use_vmix_bind_display;
    int use_vpss_bind_display;
    int use_osd_bind_display;
    int use_clahe_bind_display;
    int use_retinex_bind_display;
    int use_highlight_suppress_vi_bind_display;
    int use_cap_dehaze_bind_display;
    int use_dcp_dehaze_bind_display;
    int use_rga_bind_display;
    int use_resize_bind_display;
    int use_csc_rga_bind_display;
    int use_csc_cl_bind_display;
    int use_conv_cl_bind_display;
    int use_transform_bind_display;
    int use_stereo_bind_display;
    int use_vi_big_display;
} page_runtime_t;

void page_runtime_init(page_runtime_t *rt, const char *only_tile, int solid_test);

#endif
