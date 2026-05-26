#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_overlay.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define EIS_SCREEN_W 1080
#define EIS_SCREEN_H 1920
#define EIS_INPUT_PATH "assets/eis/eis_shaky_640x360.h264"
#define EIS_W 640
#define EIS_H 360
#define EIS_FPS 10
#define EIS_SRC_STRIDE ALIGN_UP_LOCAL(EIS_W, 64)
#define EIS_DISPLAY_W 1024
#define EIS_PANE_H 576
#define EIS_INFO_H 128
#define EIS_OUT_W EIS_DISPLAY_W
#define EIS_OUT_H (EIS_PANE_H * 2 + EIS_INFO_H)
#define EIS_PANE_STRIDE ALIGN_UP_LOCAL(EIS_DISPLAY_W, 64)
#define EIS_OUT_STRIDE ALIGN_UP_LOCAL(EIS_OUT_W, 64)
#define EIS_VIEW_X ((EIS_SCREEN_W - EIS_OUT_W) / 2)
#define EIS_VIEW_Y 0
#define EIS_VDEC_CHN 210
#define EIS_VPSS_GRP 211
#define EIS_GRP 212
#define EIS_VMIX_GRP 213
#define EIS_VDEC_POOL 0
#define EIS_VPSS_ORIG_POOL 1
#define EIS_VPSS_EIS_POOL 2
#define EIS_EIS_POOL 3
#define EIS_VMIX_POOL 4
#define EIS_OSD_POOL 8
#define EIS_OSD_GRP 81
#define EIS_VDEC_SIZE ((size_t)EIS_SRC_STRIDE * (size_t)ALIGN_UP_LOCAL(EIS_H, 16) * 2u)
#define EIS_PANE_SIZE ((size_t)EIS_PANE_STRIDE * (size_t)EIS_PANE_H * 3u / 2u)
#define EIS_OUT_SIZE ((size_t)EIS_OUT_STRIDE * (size_t)EIS_OUT_H * 3u / 2u)
#define EIS_TEXT_MASK_W 1000
#define EIS_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *data;
    size_t size;
    int *offsets;
    int count;
    int index;
    uint64_t pts;
} eis_stream_t;

typedef struct {
    int sys_ok;
    int vdec_pool_ok;
    int vpss_orig_pool_ok;
    int vpss_eis_pool_ok;
    int eis_pool_ok;
    int vmix_pool_ok;
    int osd_pool_ok;
    int vdec_ok;
    int vpss_ok;
    int eis_ok;
    int vmix_ok;
    int osd_ok;
    int vo_ok;
    int bind_vdec_vpss_ok;
    int bind_vpss_orig_vmix_ok;
    int bind_vpss_eis_ok;
    int bind_eis_vmix_ok;
    int bind_vmix_osd_ok;
    int bind_osd_vo_ok;
    int vdec_started;
} eis_chain_t;

static int path_readable(const char *path) {
    return path && access(path, R_OK) == 0;
}

static int start_code_len(const uint8_t *data, size_t size, size_t pos) {
    if (!data || pos + 3 > size) return 0;
    if (data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) return 3;
    if (pos + 4 <= size && data[pos] == 0x00 && data[pos + 1] == 0x00 &&
        data[pos + 2] == 0x00 && data[pos + 3] == 0x01) {
        return 4;
    }
    return 0;
}

static void free_stream(eis_stream_t *stream) {
    if (!stream) return;
    free(stream->data);
    free(stream->offsets);
    memset(stream, 0, sizeof(*stream));
}

static int read_stream(const char *path, eis_stream_t *stream) {
    FILE *fp = NULL;
    long file_size = 0;
    int cap = 256;
    if (!path || !stream) return -1;
    memset(stream, 0, sizeof(*stream));

    fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0 || (file_size = ftell(fp)) <= 0 ||
        fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    stream->data = (uint8_t *)malloc((size_t)file_size);
    stream->offsets = (int *)malloc((size_t)cap * sizeof(int));
    if (!stream->data || !stream->offsets) {
        fclose(fp);
        free_stream(stream);
        return -1;
    }
    if (fread(stream->data, 1, (size_t)file_size, fp) != (size_t)file_size) {
        fclose(fp);
        free_stream(stream);
        return -1;
    }
    fclose(fp);
    stream->size = (size_t)file_size;

    for (size_t i = 0; i + 3 < stream->size;) {
        int sc = start_code_len(stream->data, stream->size, i);
        if (sc > 0) {
            if (stream->count >= cap) {
                cap *= 2;
                int *tmp = (int *)realloc(stream->offsets, (size_t)cap * sizeof(int));
                if (!tmp) {
                    free_stream(stream);
                    return -1;
                }
                stream->offsets = tmp;
            }
            stream->offsets[stream->count++] = (int)i;
            i += (size_t)sc;
            continue;
        }
        ++i;
    }
    if (stream->count == 0) stream->offsets[stream->count++] = 0;
    return 0;
}

static int stream_nal_is_vcl(const eis_stream_t *stream, int idx) {
    if (!stream || idx < 0 || idx >= stream->count) return 0;
    int start = stream->offsets[idx];
    int end = (idx + 1 < stream->count) ? stream->offsets[idx + 1] : (int)stream->size;
    int sc = start_code_len(stream->data, stream->size, (size_t)start);
    int nal_start = start + sc;
    if (nal_start >= end) return 0;
    int nal = stream->data[nal_start] & 0x1f;
    return nal == 1 || nal == 5;
}

static int feed_stream_packet(eis_stream_t *stream, int *is_vcl) {
    if (!stream || !stream->data || stream->count <= 0) return -1;
    if (is_vcl) *is_vcl = 0;
    if (stream->index >= stream->count) stream->index = 0;

    int idx = stream->index++;
    int start = stream->offsets[idx];
    int end = (idx + 1 < stream->count) ? stream->offsets[idx + 1] : (int)stream->size;
    int len = end - start;
    if (start < 0 || len <= 0 || (size_t)end > stream->size) return -1;

    if (MEDIA_VDEC_SendPacket(EIS_VDEC_CHN, stream->data + start,
                              (size_t)len, stream->pts) != 0) {
        return -1;
    }
    stream->pts += (uint64_t)(1000 / EIS_FPS);
    if (is_vcl) *is_vcl = stream_nal_is_vcl(stream, idx);
    return 0;
}

static void drain_vpss_output(int chn) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VPSS_Chn_GetFrame(EIS_VPSS_GRP, chn, &out, 0) != 0) break;
            MEDIA_VPSS_Chn_ReleaseFrame(EIS_VPSS_GRP, chn, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_eis_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_EIS_GetFrame(EIS_GRP, &out, 0) != 0) break;
            MEDIA_EIS_ReleaseFrame(EIS_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_eis_osd_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_OSD_GetFrame(EIS_OSD_GRP, &out, 0) != 0) break;
            MEDIA_OSD_ReleaseFrame(EIS_OSD_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_eis_vmix_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VMIX_GetFrame(EIS_VMIX_GRP, &out, 0) != 0) break;
            MEDIA_VMIX_ReleaseFrame(EIS_VMIX_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static int update_eis_overlay(uint64_t vdec_count, uint64_t vpss_count,
                              uint64_t eis_count, uint64_t osd_count,
                              uint64_t vo_count, int sent_frames,
                              const MEDIA_EIS_STATS *stats) {
    static uint8_t masks[5][EIS_TEXT_MASK_W * EIS_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char mode_line[192];
    char count_line[192];

    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(mode_line, sizeof(mode_line),
             "H264 %dx%d FPS %d CROP %.2f TOTAL %.3f EST %.3f WARP %.3f FB %d",
             EIS_W, EIS_H, EIS_FPS, 0.08f,
             stats ? stats->total_ms : 0.0,
             stats ? stats->estimate_ms : 0.0,
             stats ? stats->warp_ms : 0.0,
             stats ? stats->fallback_used : 0);
    snprintf(count_line, sizeof(count_line),
             "VDEC %llu VPSS %llu EIS %llu OSD %llu VO %llu SENT %d",
             (unsigned long long)vdec_count,
             (unsigned long long)vpss_count,
             (unsigned long long)eis_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count,
             sent_frames);

    int info_y = EIS_PANE_H * 2 + 8;
    if (page_overlay_set_text(EIS_OSD_GRP, 0, 24, info_y, 2,
                              "EIS VIDEO STABILIZATION LIVE DEMO",
                              masks[0], sizeof(masks[0]), EIS_TEXT_MASK_W, EIS_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_OSD_GRP, 1, 24, info_y + 26, 2,
                              "FLOW H264->VDEC->VPSS RAW/EIS->VMIX->OSD->VO",
                              masks[1], sizeof(masks[1]), EIS_TEXT_MASK_W, EIS_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_OSD_GRP, 2, 24, info_y + 52, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), EIS_TEXT_MASK_W, EIS_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_OSD_GRP, 3, 24, info_y + 78, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), EIS_TEXT_MASK_W, EIS_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_OSD_GRP, 4, 24, info_y + 104, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), EIS_TEXT_MASK_W, EIS_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_eis_chain(eis_chain_t *chain) {
    MEDIA_VDEC_ATTR vdec = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_EIS_ATTR eis = {0};
    MEDIA_VMIX_ATTR vmix = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(EIS_VDEC_POOL, EIS_VDEC_SIZE, 8) != 0) return -1;
    chain->vdec_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_VPSS_ORIG_POOL, EIS_PANE_SIZE, 8) != 0) return -1;
    chain->vpss_orig_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_VPSS_EIS_POOL, EIS_PANE_SIZE, 8) != 0) return -1;
    chain->vpss_eis_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_EIS_POOL, EIS_PANE_SIZE, 8) != 0) return -1;
    chain->eis_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_VMIX_POOL, EIS_OUT_SIZE, 4) != 0) return -1;
    chain->vmix_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_OSD_POOL, EIS_OUT_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vdec.width = EIS_W;
    vdec.height = EIS_H;
    vdec.stride = EIS_SRC_STRIDE;
    vdec.buf_cnt = 8;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = EIS_VDEC_POOL;
    if (MEDIA_VDEC_CreateChn(EIS_VDEC_CHN, &vdec) != 0) return -1;
    chain->vdec_ok = 1;

    vpss.width = EIS_W;
    vpss.height = EIS_H;
    vpss.input_stride = EIS_SRC_STRIDE;
    vpss.input_depth = 8;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = EIS_FPS;
    vpss.out_fps = EIS_FPS;
    vpss.output_count = 2;
    vpss.outputs[0].output_id = 0;
    vpss.outputs[0].out_width = EIS_DISPLAY_W;
    vpss.outputs[0].out_height = EIS_PANE_H;
    vpss.outputs[0].out_stride = EIS_PANE_STRIDE;
    vpss.outputs[0].pool_id = EIS_VPSS_ORIG_POOL;
    vpss.outputs[0].crop_w = EIS_W;
    vpss.outputs[0].crop_h = EIS_H;
    vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    vpss.outputs[1].output_id = 1;
    vpss.outputs[1].out_width = EIS_DISPLAY_W;
    vpss.outputs[1].out_height = EIS_PANE_H;
    vpss.outputs[1].out_stride = EIS_PANE_STRIDE;
    vpss.outputs[1].pool_id = EIS_VPSS_EIS_POOL;
    vpss.outputs[1].crop_w = EIS_W;
    vpss.outputs[1].crop_h = EIS_H;
    vpss.outputs[1].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(EIS_VPSS_GRP, &vpss) != 0) return -1;
    chain->vpss_ok = 1;

    eis.width = EIS_DISPLAY_W;
    eis.height = EIS_PANE_H;
    eis.format = MEDIA_FORMAT_NV12;
    eis.input_depth = 8;
    eis.output_pool_id = EIS_EIS_POOL;
    eis.input_stride = EIS_PANE_STRIDE;
    eis.output_stride = EIS_PANE_STRIDE;
    eis.crop_ratio = 0.08f;
    eis.smoothing_window = 15;
    eis.estimate_width = 320;
    eis.search_radius = 16;
    eis.block_step = 4;
    if (MEDIA_EIS_CreateGrp(EIS_GRP, &eis) != 0) return -1;
    chain->eis_ok = 1;

    vmix.input_count = 2;
    vmix.output_width = EIS_OUT_W;
    vmix.output_height = EIS_OUT_H;
    vmix.output_stride = EIS_OUT_STRIDE;
    vmix.format = MEDIA_FORMAT_NV12;
    vmix.input_depth = 4;
    vmix.output_pool_id = EIS_VMIX_POOL;
    vmix.primary_index = -1;
    vmix.channels[0].enabled = 1;
    vmix.channels[0].x = 0;
    vmix.channels[0].y = 0;
    vmix.channels[0].width = EIS_DISPLAY_W;
    vmix.channels[0].height = EIS_PANE_H;
    vmix.channels[0].alpha = 1.0f;
    vmix.channels[0].stride = EIS_PANE_STRIDE;
    vmix.channels[0].format = MEDIA_FORMAT_NV12;
    vmix.channels[1].enabled = 1;
    vmix.channels[1].x = 0;
    vmix.channels[1].y = EIS_PANE_H;
    vmix.channels[1].width = EIS_DISPLAY_W;
    vmix.channels[1].height = EIS_PANE_H;
    vmix.channels[1].alpha = 1.0f;
    vmix.channels[1].stride = EIS_PANE_STRIDE;
    vmix.channels[1].format = MEDIA_FORMAT_NV12;
    if (MEDIA_VMIX_CreateGrp(EIS_VMIX_GRP, &vmix) != 0 ||
        MEDIA_VMIX_Start(EIS_VMIX_GRP) != 0 ||
        MEDIA_VMIX_Enable(EIS_VMIX_GRP) != 0) {
        return -1;
    }
    chain->vmix_ok = 1;

    osd.input_width = EIS_OUT_W;
    osd.input_height = EIS_OUT_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = EIS_OSD_POOL;
    osd.input_stride = EIS_OUT_STRIDE;
    osd.output_stride = EIS_OUT_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(EIS_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(EIS_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = EIS_SCREEN_W;
    vo.height = EIS_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, EIS_VIEW_X, EIS_VIEW_Y,
                           EIS_OUT_W, EIS_OUT_H, EIS_OUT_STRIDE, 8,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VDEC", EIS_VDEC_CHN, "output",
                       "VPSS", EIS_VPSS_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_vdec_vpss_ok = 1;
    if (MEDIA_SYS_Bind("VPSS", EIS_VPSS_GRP, "output0",
                       "VMIX", EIS_VMIX_GRP, "input0") != 0) {
        return -1;
    }
    chain->bind_vpss_orig_vmix_ok = 1;
    if (MEDIA_SYS_Bind("VPSS", EIS_VPSS_GRP, "output1",
                       "EIS", EIS_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_vpss_eis_ok = 1;
    if (MEDIA_SYS_Bind("EIS", EIS_GRP, "output",
                       "VMIX", EIS_VMIX_GRP, "input1") != 0) {
        return -1;
    }
    chain->bind_eis_vmix_ok = 1;
    if (MEDIA_SYS_Bind("VMIX", EIS_VMIX_GRP, "output0",
                       "OSD", EIS_OSD_GRP, "input") != 0) {
        return -1;
    }
    chain->bind_vmix_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", EIS_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) {
        return -1;
    }
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_EIS_Start(EIS_GRP) != 0 ||
        MEDIA_VPSS_Enable(EIS_VPSS_GRP) != 0 ||
        MEDIA_VDEC_Start(EIS_VDEC_CHN) != 0) {
        return -1;
    }
    chain->vdec_started = 1;
    set_tile_status("VDEC", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("EIS", TILE_LIVE);
    set_tile_status("VMIX", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_eis_chain(eis_chain_t *chain) {
    if (!chain) return;
    if (chain->vdec_started) {
        MEDIA_VDEC_Stop(EIS_VDEC_CHN);
        chain->vdec_started = 0;
        usleep(50000);
    }
    if (chain->vpss_ok) MEDIA_VPSS_Disable(EIS_VPSS_GRP);
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", EIS_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_vmix_osd_ok) {
        MEDIA_SYS_UnBind("VMIX", EIS_VMIX_GRP, "output0", "OSD", EIS_OSD_GRP, "input");
        chain->bind_vmix_osd_ok = 0;
    }
    if (chain->bind_eis_vmix_ok) {
        MEDIA_SYS_UnBind("EIS", EIS_GRP, "output", "VMIX", EIS_VMIX_GRP, "input1");
        chain->bind_eis_vmix_ok = 0;
    }
    if (chain->eis_ok) {
        drain_eis_output();
        MEDIA_EIS_Stop(EIS_GRP);
        drain_eis_output();
    }
    if (chain->bind_vpss_eis_ok) {
        MEDIA_SYS_UnBind("VPSS", EIS_VPSS_GRP, "output1", "EIS", EIS_GRP, "input");
        chain->bind_vpss_eis_ok = 0;
    }
    if (chain->bind_vpss_orig_vmix_ok) {
        MEDIA_SYS_UnBind("VPSS", EIS_VPSS_GRP, "output0", "VMIX", EIS_VMIX_GRP, "input0");
        chain->bind_vpss_orig_vmix_ok = 0;
    }
    if (chain->bind_vdec_vpss_ok) {
        MEDIA_SYS_UnBind("VDEC", EIS_VDEC_CHN, "output", "VPSS", EIS_VPSS_GRP, "input");
        chain->bind_vdec_vpss_ok = 0;
    }

    if (chain->vpss_ok) {
        drain_vpss_output(0);
        drain_vpss_output(1);
    }
    if (chain->vmix_ok) {
        drain_eis_vmix_output();
        MEDIA_VMIX_Disable(EIS_VMIX_GRP);
        MEDIA_VMIX_Stop(EIS_VMIX_GRP);
        drain_eis_vmix_output();
        MEDIA_VMIX_DestroyGrp(EIS_VMIX_GRP);
        chain->vmix_ok = 0;
    }
    if (chain->osd_ok) {
        drain_eis_osd_output();
        MEDIA_OSD_Stop(EIS_OSD_GRP);
        drain_eis_osd_output();
        MEDIA_OSD_DestroyGrp(EIS_OSD_GRP);
        chain->osd_ok = 0;
    }

    if (chain->eis_ok) {
        MEDIA_EIS_DestroyGrp(EIS_GRP);
        chain->eis_ok = 0;
    }
    if (chain->vpss_ok) {
        MEDIA_VPSS_DestroyGrp(EIS_VPSS_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->vdec_ok) {
        MEDIA_VDEC_DestroyChn(EIS_VDEC_CHN);
        chain->vdec_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(EIS_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->vmix_pool_ok) {
        MEDIA_POOL_Destroy(EIS_VMIX_POOL);
        chain->vmix_pool_ok = 0;
    }

    if (chain->eis_pool_ok) {
        /*
         * VMIX can still release the last EIS output buffer during pipeline
         * teardown. Let MEDIA_SYS_Exit reclaim this pool after modules are gone.
         */
        chain->eis_pool_ok = 0;
    }
    if (chain->vpss_eis_pool_ok) {
        MEDIA_POOL_Destroy(EIS_VPSS_EIS_POOL);
        chain->vpss_eis_pool_ok = 0;
    }
    if (chain->vpss_orig_pool_ok) {
        MEDIA_POOL_Destroy(EIS_VPSS_ORIG_POOL);
        chain->vpss_orig_pool_ok = 0;
    }
    if (chain->vdec_pool_ok) {
        MEDIA_POOL_Destroy(EIS_VDEC_POOL);
        chain->vdec_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_eis_run(volatile sig_atomic_t *running) {
    eis_stream_t stream;
    eis_chain_t chain = {0};
    memset(&stream, 0, sizeof(stream));

    if (!path_readable(EIS_INPUT_PATH)) {
        fprintf(stderr, "EIS input missing: %s\n", EIS_INPUT_PATH);
        return 1;
    }
    if (read_stream(EIS_INPUT_PATH, &stream) != 0) {
        fprintf(stderr, "EIS stream load failed: %s\n", EIS_INPUT_PATH);
        return 1;
    }
    if (setup_eis_chain(&chain) != 0) {
        fprintf(stderr, "EIS standalone chain setup failed\n");
        cleanup_eis_chain(&chain);
        free_stream(&stream);
        return 1;
    }

    printf("EIS standalone: %s -> VDEC -> VPSS -> EIS -> OSD -> VO. Ctrl+C to stop.\n",
           EIS_INPUT_PATH);
    int sent_frames = 0;
    while (!running || *running) {
        int is_vcl = 0;
        if (feed_stream_packet(&stream, &is_vcl) != 0) {
            fprintf(stderr, "EIS MEDIA_VDEC_SendPacket failed\n");
            usleep(1000000 / EIS_FPS);
            continue;
        }
        if (is_vcl) {
            sent_frames++;
            if ((sent_frames % EIS_FPS) == 0) {
                uint64_t vdec_count = 0;
                uint64_t vpss_count = 0;
                uint64_t eis_count = 0;
                uint64_t osd_count = 0;
                uint64_t vo_count = 0;
                MEDIA_EIS_STATS stats;
                memset(&stats, 0, sizeof(stats));
                (void)MEDIA_SYS_GetModuleFrameCount("VDEC", EIS_VDEC_CHN, &vdec_count);
                (void)MEDIA_SYS_GetModuleFrameCount("VPSS", EIS_VPSS_GRP, &vpss_count);
                (void)MEDIA_SYS_GetModuleFrameCount("EIS", EIS_GRP, &eis_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", EIS_OSD_GRP, &osd_count);
                (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
                (void)MEDIA_EIS_GetStats(EIS_GRP, &stats);
                (void)update_eis_overlay(vdec_count, vpss_count, eis_count,
                                         osd_count, vo_count, sent_frames, &stats);
                printf("EIS vdec=%llu vpss=%llu eis=%llu osd=%llu vo=%llu sent=%d total=%.3fms estimate=%.3fms warp=%.3fms fallback=%d standalone=1\n",
                       (unsigned long long)vdec_count,
                       (unsigned long long)vpss_count,
                       (unsigned long long)eis_count,
                       (unsigned long long)osd_count,
                       (unsigned long long)vo_count,
                       sent_frames,
                       stats.total_ms,
                       stats.estimate_ms,
                       stats.warp_ms,
                       stats.fallback_used);
            }
            usleep(1000000 / EIS_FPS);
        } else {
            usleep(1000);
        }
    }

    cleanup_eis_chain(&chain);
    free_stream(&stream);
    return 0;
}
