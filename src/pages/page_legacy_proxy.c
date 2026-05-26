#include "page_ops.h"

#include "legacy/alldemo_legacy.h"

#include <signal.h>

static int run_legacy_page(const char *name, volatile sig_atomic_t *running) {
    (void)running;
    char *argv[] = {
        "alldemo-legacy",
        "--only",
        (char *)name,
        0,
    };
    return alldemo_legacy_main(3, argv);
}

#define DEFINE_LEGACY_PAGE_RUN(fn, page_name) \
    int fn(volatile sig_atomic_t *running) { return run_legacy_page(page_name, running); }

DEFINE_LEGACY_PAGE_RUN(page_legacy_vi_run, "VI")
DEFINE_LEGACY_PAGE_RUN(page_legacy_vpss_run, "VPSS")
DEFINE_LEGACY_PAGE_RUN(page_legacy_vo_run, "VO")
DEFINE_LEGACY_PAGE_RUN(page_legacy_wbc_run, "WBC")
DEFINE_LEGACY_PAGE_RUN(page_legacy_rga_run, "RGA")
DEFINE_LEGACY_PAGE_RUN(page_legacy_resize_rga_run, "RESIZE_RGA")
DEFINE_LEGACY_PAGE_RUN(page_legacy_csc_rga_run, "CSC_RGA")
DEFINE_LEGACY_PAGE_RUN(page_legacy_csc_cl_run, "CSC_CL")
DEFINE_LEGACY_PAGE_RUN(page_legacy_osd_run, "OSD")
DEFINE_LEGACY_PAGE_RUN(page_legacy_clahe_run, "CLAHE")
DEFINE_LEGACY_PAGE_RUN(page_legacy_retinex_run, "RETINEX")
DEFINE_LEGACY_PAGE_RUN(page_legacy_retinex_offline_run, "RETINEX_OFFLINE")
DEFINE_LEGACY_PAGE_RUN(page_legacy_tnr_cl_run, "TNR_CL")
DEFINE_LEGACY_PAGE_RUN(page_legacy_highlight_suppress_run, "HIGHLIGHT_SUPPRESS")
DEFINE_LEGACY_PAGE_RUN(page_legacy_highlight_suppress_vi_run, "HIGHLIGHT_SUPPRESS_VI")
DEFINE_LEGACY_PAGE_RUN(page_legacy_eis_run, "EIS")
DEFINE_LEGACY_PAGE_RUN(page_legacy_eis_vi_run, "EIS_VI")
DEFINE_LEGACY_PAGE_RUN(page_legacy_cap_dehaze_run, "CAP_DEHAZE")
DEFINE_LEGACY_PAGE_RUN(page_legacy_cap_dehaze_offline_run, "CAP_DEHAZE_OFFLINE")
DEFINE_LEGACY_PAGE_RUN(page_legacy_dcp_fast_dehaze_run, "DCP_FAST_DEHAZE")
DEFINE_LEGACY_PAGE_RUN(page_legacy_thermal_run, "THERMAL")
DEFINE_LEGACY_PAGE_RUN(page_legacy_conv_cl_run, "CONV_CL")
DEFINE_LEGACY_PAGE_RUN(page_legacy_transform_run, "TRANSFORM")
DEFINE_LEGACY_PAGE_RUN(page_legacy_blend_pyr_run, "BLEND_PYR")
DEFINE_LEGACY_PAGE_RUN(page_legacy_edof_cl_run, "EDOF_CL")
DEFINE_LEGACY_PAGE_RUN(page_legacy_mcf_fusion_cl_run, "MCF_FUSION_CL")
DEFINE_LEGACY_PAGE_RUN(page_legacy_dualview_run, "DUALVIEW")
DEFINE_LEGACY_PAGE_RUN(page_legacy_stereo_3d_run, "STEREO_3D")
DEFINE_LEGACY_PAGE_RUN(page_legacy_vmix_run, "VMIX")
DEFINE_LEGACY_PAGE_RUN(page_legacy_pano_run, "PANO")
DEFINE_LEGACY_PAGE_RUN(page_legacy_avm2d_run, "AVM2D")
DEFINE_LEGACY_PAGE_RUN(page_legacy_npu_run, "NPU")
