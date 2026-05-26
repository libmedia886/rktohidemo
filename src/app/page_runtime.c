#include "page_runtime.h"

#include <string.h>

void page_runtime_init(page_runtime_t *rt, const char *only_tile, int solid_test) {
    if (!rt) return;
    memset(rt, 0, sizeof(*rt));
    rt->only_tile = only_tile;
    rt->active = !solid_test && only_tile != NULL;
    if (!rt->active) return;

    rt->need_camera = tile_needs_camera(only_tile);
    rt->use_4k_camera_input = tile_uses_4k_camera_input(only_tile);
    rt->use_1080p_camera_input = tile_uses_1080p_camera_input(only_tile);

    rt->bind_display = page_bind_display(only_tile);
    rt->use_vmix_osd_display = page_has_flag(only_tile, PAGE_FLAG_VMIX_OSD_DISPLAY);
    rt->use_vi_bind_display = rt->bind_display == PAGE_BIND_VI;
    rt->use_wbc_bind_display = rt->bind_display == PAGE_BIND_WBC;
    rt->use_vmix_bind_display = rt->bind_display == PAGE_BIND_VMIX;
    rt->use_vpss_bind_display = rt->bind_display == PAGE_BIND_VPSS ||
        rt->use_vmix_bind_display;
    rt->use_osd_bind_display = rt->bind_display == PAGE_BIND_OSD;
    rt->use_clahe_bind_display = rt->bind_display == PAGE_BIND_CLAHE;
    rt->use_retinex_bind_display = rt->bind_display == PAGE_BIND_RETINEX;
    rt->use_highlight_suppress_vi_bind_display =
        rt->bind_display == PAGE_BIND_HIGHLIGHT_SUPPRESS_VI;
    rt->use_cap_dehaze_bind_display = rt->bind_display == PAGE_BIND_CAP_DEHAZE;
    rt->use_dcp_dehaze_bind_display = rt->bind_display == PAGE_BIND_DCP_DEHAZE;
    rt->use_rga_bind_display = rt->bind_display == PAGE_BIND_RGA;
    rt->use_resize_bind_display = rt->bind_display == PAGE_BIND_RESIZE_RGA;
    rt->use_csc_rga_bind_display = rt->bind_display == PAGE_BIND_CSC_RGA;
    rt->use_csc_cl_bind_display = rt->bind_display == PAGE_BIND_CSC_CL;
    rt->use_conv_cl_bind_display = 0;
    rt->use_transform_bind_display = rt->bind_display == PAGE_BIND_TRANSFORM;
    rt->use_stereo_bind_display = rt->bind_display == PAGE_BIND_STEREO_3D;
    rt->use_vi_big_display = rt->use_vi_bind_display;
}
