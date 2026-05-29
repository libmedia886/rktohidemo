#include "page_ops.h"

#include "app/page_registry.h"

#include <stddef.h>
#include <stdlib.h>
#include <strings.h>

int page_license_run(volatile sig_atomic_t *running);
int page_vo_run(volatile sig_atomic_t *running);
int page_highlight_suppress_run(volatile sig_atomic_t *running);
int page_thermal_run(volatile sig_atomic_t *running);
int page_thermal_lowlight_fusion_cl_run(volatile sig_atomic_t *running);
int page_thermal_sr_npu_run(volatile sig_atomic_t *running);
int page_tnr_cl_run(volatile sig_atomic_t *running);
int page_retinex_run(volatile sig_atomic_t *running);
int page_retinex_live_run(volatile sig_atomic_t *running);
int page_retinex_offline_run(volatile sig_atomic_t *running);
int page_cap_dehaze_run(volatile sig_atomic_t *running);
int page_cap_dehaze_offline_run(volatile sig_atomic_t *running);
int page_clahe_run(volatile sig_atomic_t *running);
int page_dcp_fast_dehaze_run(volatile sig_atomic_t *running);
int page_conv_cl_run(volatile sig_atomic_t *running);
int page_conv_cl_live_run(volatile sig_atomic_t *running);
int page_csc_cl_run(volatile sig_atomic_t *running);
int page_csc_rga_run(volatile sig_atomic_t *running);
int page_dualview_run(volatile sig_atomic_t *running);
int page_edof_cl_run(volatile sig_atomic_t *running);
int page_eis_detect_npu_run(volatile sig_atomic_t *running);
int page_eis_run(volatile sig_atomic_t *running);
int page_eis_vi_run(volatile sig_atomic_t *running);
int page_mcf_fusion_cl_run(volatile sig_atomic_t *running);
int page_npu_run(volatile sig_atomic_t *running);
int page_osd_run(volatile sig_atomic_t *running);
int page_segment_npu_run(volatile sig_atomic_t *running);
int page_highlight_suppress_vi_run(volatile sig_atomic_t *running);
int page_blend_pyr_run(volatile sig_atomic_t *running);
int page_avm2d_run(volatile sig_atomic_t *running);
int page_pano_run(volatile sig_atomic_t *running);
int page_rga_run(volatile sig_atomic_t *running);
int page_resize_rga_run(volatile sig_atomic_t *running);
int page_stereo_3d_run(volatile sig_atomic_t *running);
int page_stereo_3d_live_run(volatile sig_atomic_t *running);
int page_vi_run(volatile sig_atomic_t *running);
int page_transform_run(volatile sig_atomic_t *running);
int page_vmix_run(volatile sig_atomic_t *running);
int page_vmix_live_run(volatile sig_atomic_t *running);
int page_vpss_run(volatile sig_atomic_t *running);
int page_wbc_run(volatile sig_atomic_t *running);
int page_wbc_live_run(volatile sig_atomic_t *running);
int page_info_vmix_rga_run(volatile sig_atomic_t *running);
int page_info_avm_run(volatile sig_atomic_t *running);
int page_info_svm3d_run(volatile sig_atomic_t *running);
int page_info_exposure_fusion_cl_run(volatile sig_atomic_t *running);
int page_info_venc_run(volatile sig_atomic_t *running);
int page_info_vdec_run(volatile sig_atomic_t *running);
int page_info_rtsp_send_run(volatile sig_atomic_t *running);
int page_info_rtsp_recv_run(volatile sig_atomic_t *running);
int page_info_pic_io_run(volatile sig_atomic_t *running);
int page_legacy_vi_run(volatile sig_atomic_t *running);
int page_legacy_vpss_run(volatile sig_atomic_t *running);
int page_legacy_vo_run(volatile sig_atomic_t *running);
int page_legacy_wbc_run(volatile sig_atomic_t *running);
int page_legacy_rga_run(volatile sig_atomic_t *running);
int page_legacy_resize_rga_run(volatile sig_atomic_t *running);
int page_legacy_csc_rga_run(volatile sig_atomic_t *running);
int page_legacy_csc_cl_run(volatile sig_atomic_t *running);
int page_legacy_osd_run(volatile sig_atomic_t *running);
int page_legacy_clahe_run(volatile sig_atomic_t *running);
int page_legacy_retinex_run(volatile sig_atomic_t *running);
int page_legacy_retinex_offline_run(volatile sig_atomic_t *running);
int page_legacy_tnr_cl_run(volatile sig_atomic_t *running);
int page_legacy_highlight_suppress_run(volatile sig_atomic_t *running);
int page_legacy_highlight_suppress_vi_run(volatile sig_atomic_t *running);
int page_legacy_eis_run(volatile sig_atomic_t *running);
int page_legacy_eis_vi_run(volatile sig_atomic_t *running);
int page_legacy_cap_dehaze_run(volatile sig_atomic_t *running);
int page_legacy_cap_dehaze_offline_run(volatile sig_atomic_t *running);
int page_legacy_dcp_fast_dehaze_run(volatile sig_atomic_t *running);
int page_legacy_thermal_run(volatile sig_atomic_t *running);
int page_legacy_conv_cl_run(volatile sig_atomic_t *running);
int page_legacy_transform_run(volatile sig_atomic_t *running);
int page_legacy_blend_pyr_run(volatile sig_atomic_t *running);
int page_legacy_edof_cl_run(volatile sig_atomic_t *running);
int page_legacy_mcf_fusion_cl_run(volatile sig_atomic_t *running);
int page_legacy_dualview_run(volatile sig_atomic_t *running);
int page_legacy_stereo_3d_run(volatile sig_atomic_t *running);
int page_legacy_vmix_run(volatile sig_atomic_t *running);
int page_legacy_pano_run(volatile sig_atomic_t *running);
int page_legacy_avm2d_run(volatile sig_atomic_t *running);
int page_legacy_npu_run(volatile sig_atomic_t *running);

static const page_ops_t g_page_ops[] = {
    {"AVM2D", PAGE_IMPL_STANDALONE, page_avm2d_run},
    {"AVM", PAGE_IMPL_STANDALONE, page_info_avm_run},
    {"BLEND_PYR", PAGE_IMPL_STANDALONE, page_blend_pyr_run},
    {"CAP_DEHAZE", PAGE_IMPL_STANDALONE, page_cap_dehaze_run},
    {"CAP_DEHAZE_OFFLINE", PAGE_IMPL_STANDALONE, page_cap_dehaze_offline_run},
    {"CLAHE", PAGE_IMPL_STANDALONE, page_clahe_run},
    {"CONV_CL", PAGE_IMPL_STANDALONE, page_conv_cl_live_run},
    {"CSC_CL", PAGE_IMPL_STANDALONE, page_csc_cl_run},
    {"CSC_RGA", PAGE_IMPL_STANDALONE, page_csc_rga_run},
    {"DCP_FAST_DEHAZE", PAGE_IMPL_STANDALONE, page_dcp_fast_dehaze_run},
    {"DETECT_NPU", PAGE_IMPL_STANDALONE, page_npu_run},
    {"DUALVIEW", PAGE_IMPL_STANDALONE, page_dualview_run},
    {"EDOF_CL", PAGE_IMPL_STANDALONE, page_edof_cl_run},
    {"EIS", PAGE_IMPL_STANDALONE, page_eis_run},
    {"EIS_DETECT_NPU", PAGE_IMPL_STANDALONE, page_eis_detect_npu_run},
    {"EIS_VI", PAGE_IMPL_STANDALONE, page_eis_vi_run},
    {"EXPOSURE_FUSION_CL", PAGE_IMPL_STANDALONE, page_info_exposure_fusion_cl_run},
    {"HIGHLIGHT_SUPPRESS", PAGE_IMPL_STANDALONE, page_highlight_suppress_run},
    {"HIGHLIGHT_SUPPRESS_VI", PAGE_IMPL_STANDALONE, page_highlight_suppress_vi_run},
    {"LICENSE", PAGE_IMPL_STANDALONE, page_license_run},
    {"MCF_FUSION_CL", PAGE_IMPL_STANDALONE, page_mcf_fusion_cl_run},
    {"OSD", PAGE_IMPL_STANDALONE, page_osd_run},
    {"PANO", PAGE_IMPL_STANDALONE, page_pano_run},
    {"PIC_IO", PAGE_IMPL_STANDALONE, page_info_pic_io_run},
    {"RETINEX", PAGE_IMPL_STANDALONE, page_retinex_live_run},
    {"RETINEX_OFFLINE", PAGE_IMPL_STANDALONE, page_retinex_offline_run},
    {"RESIZE_RGA", PAGE_IMPL_STANDALONE, page_resize_rga_run},
    {"RGA", PAGE_IMPL_STANDALONE, page_rga_run},
    {"RTSP_RECV", PAGE_IMPL_STANDALONE, page_info_rtsp_recv_run},
    {"RTSP_SEND", PAGE_IMPL_STANDALONE, page_info_rtsp_send_run},
    {"SEGMENT_NPU", PAGE_IMPL_STANDALONE, page_segment_npu_run},
    {"STEREO_3D", PAGE_IMPL_STANDALONE, page_stereo_3d_live_run},
    {"SVM3D", PAGE_IMPL_STANDALONE, page_info_svm3d_run},
    {"THERMAL", PAGE_IMPL_STANDALONE, page_thermal_run},
    {"THERMAL_LOWLIGHT_FUSION_CL", PAGE_IMPL_STANDALONE, page_thermal_lowlight_fusion_cl_run},
    {"THERMAL_SR_NPU", PAGE_IMPL_STANDALONE, page_thermal_sr_npu_run},
    {"TNR_CL", PAGE_IMPL_STANDALONE, page_tnr_cl_run},
    {"TRANSFORM", PAGE_IMPL_STANDALONE, page_transform_run},
    {"VDEC", PAGE_IMPL_STANDALONE, page_info_vdec_run},
    {"VENC", PAGE_IMPL_STANDALONE, page_info_venc_run},
    {"VI", PAGE_IMPL_STANDALONE, page_vi_run},
    {"VMIX", PAGE_IMPL_STANDALONE, page_vmix_live_run},
    {"VMIX_RGA", PAGE_IMPL_STANDALONE, page_info_vmix_rga_run},
    {"VO", PAGE_IMPL_STANDALONE, page_vo_run},
    {"VPSS", PAGE_IMPL_STANDALONE, page_vpss_run},
    {"WBC", PAGE_IMPL_STANDALONE, page_wbc_live_run},
};

static page_ops_t g_legacy_ops = {NULL, PAGE_IMPL_LEGACY_MAIN, NULL};

const page_ops_t *page_ops_find(const char *name) {
    const page_desc_t *desc = page_desc_find(name);
    if (!desc) return NULL;
    const char *force_legacy = getenv("ALLDEMO_FORCE_LEGACY");
    if (force_legacy && force_legacy[0] && strcasecmp(force_legacy, "0") != 0) {
        g_legacy_ops.name = desc->name;
        return &g_legacy_ops;
    }
    for (size_t i = 0; i < sizeof(g_page_ops) / sizeof(g_page_ops[0]); ++i) {
        if (strcasecmp(g_page_ops[i].name, desc->name) == 0) {
            return &g_page_ops[i];
        }
    }
    g_legacy_ops.name = desc->name;
    return &g_legacy_ops;
}
