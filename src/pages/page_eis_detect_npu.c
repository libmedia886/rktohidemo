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
#define EIS_DET_SCREEN_W 1080
#define EIS_DET_SCREEN_H 1920
#define EIS_DET_SRC_W 640
#define EIS_DET_SRC_H 360
#define EIS_DET_SRC_STRIDE ALIGN_UP_LOCAL(EIS_DET_SRC_W, 64)
#define EIS_DET_MODEL_W 640
#define EIS_DET_MODEL_H 640
#define EIS_DET_MODEL_STRIDE 640
#define EIS_DET_RGB_STRIDE (EIS_DET_MODEL_W * 3)
#define EIS_DET_DISPLAY_W 640
#define EIS_DET_DISPLAY_H 360
#define EIS_DET_DISPLAY_STRIDE 640
#define EIS_DET_GAP_H 32
#define EIS_DET_INFO_H 128
#define EIS_DET_OUT_W 640
#define EIS_DET_OUT_H (EIS_DET_SRC_H + EIS_DET_GAP_H + EIS_DET_DISPLAY_H + EIS_DET_INFO_H)
#define EIS_DET_OUT_STRIDE ALIGN_UP_LOCAL(EIS_DET_OUT_W, 64)
#define EIS_DET_RAW_Y 0
#define EIS_DET_PROCESSED_Y (EIS_DET_SRC_H + EIS_DET_GAP_H)
#define EIS_DET_INFO_Y (EIS_DET_PROCESSED_Y + EIS_DET_DISPLAY_H)
#define EIS_DET_FPS 10
#define EIS_DET_INPUT_PATH "assets/eis/eis_shaky_640x360.h264"
#define EIS_DET_MODEL_PATH "assets/npu/yolov5s-640-640.rknn"
#define EIS_DET_LABEL_PATH "assets/npu/coco_80_labels_list.txt"
#define EIS_DET_VDEC_POOL 3
#define EIS_DET_VPSS_POOL 4
#define EIS_DET_EIS_POOL 5
#define EIS_DET_RGB_POOL 6
#define EIS_DET_PROC_VPSS_POOL 7
#define EIS_DET_VMIX_POOL 9
#define EIS_DET_OSD_POOL 8
#define EIS_DET_VDEC_CHN 310
#define EIS_DET_VPSS_GRP 311
#define EIS_DET_EIS_GRP 312
#define EIS_DET_PRE_RGA_GRP 313
#define EIS_DET_NPU_GRP 314
#define EIS_DET_PROC_VPSS_GRP 315
#define EIS_DET_VMIX_GRP 80
#define EIS_DET_OSD_GRP 81
#define EIS_DET_VIEW_X ((EIS_DET_SCREEN_W - EIS_DET_OUT_W) / 2)
#define EIS_DET_VIEW_Y ((EIS_DET_SCREEN_H - EIS_DET_OUT_H) / 2)
#define EIS_DET_VDEC_FRAME_SIZE ((size_t)EIS_DET_SRC_STRIDE * (size_t)ALIGN_UP_LOCAL(EIS_DET_SRC_H, 16) * 2u)
#define EIS_DET_VPSS_FRAME_SIZE ((size_t)EIS_DET_SRC_STRIDE * (size_t)EIS_DET_SRC_H * 3u / 2u)
#define EIS_DET_EIS_FRAME_SIZE ((size_t)EIS_DET_SRC_STRIDE * (size_t)EIS_DET_SRC_H * 3u / 2u)
#define EIS_DET_PROC_VPSS_FRAME_SIZE ((size_t)EIS_DET_MODEL_STRIDE * (size_t)EIS_DET_MODEL_H * 3u / 2u)
#define EIS_DET_RGB_FRAME_SIZE ((size_t)EIS_DET_RGB_STRIDE * (size_t)EIS_DET_MODEL_H)
#define EIS_DET_DISPLAY_FRAME_SIZE ((size_t)EIS_DET_OUT_STRIDE * (size_t)EIS_DET_OUT_H * 3u / 2u)
#define EIS_DET_MAX_DRAW_OBJECTS 5
#define EIS_DET_TEXT_MASK_W 620
#define EIS_DET_TEXT_MASK_H 32
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *data;
    size_t size;
    int *nal_offsets;
    int nal_count;
    int nal_index;
    uint64_t pts;
} eis_det_stream_t;

typedef struct {
    float x;
    float y;
    float w;
    float h;
    float score;
    int class_id;
    char label[64];
} eis_det_object_t;

typedef struct {
    uint64_t frame_id;
    int object_count;
    eis_det_object_t objects[EIS_DET_MAX_DRAW_OBJECTS];
} eis_det_result_snapshot_t;

typedef struct {
    int sys_ok;
    int vdec_pool_ok;
    int vpss_pool_ok;
    int eis_pool_ok;
    int proc_vpss_pool_ok;
    int rgb_pool_ok;
    int vmix_pool_ok;
    int osd_pool_ok;
    int vdec_ok;
    int vpss_ok;
    int eis_ok;
    int proc_vpss_ok;
    int pre_rga_ok;
    int npu_ok;
    int vmix_ok;
    int osd_ok;
    int vo_ok;
    int bind_vdec_vpss_ok;
    int bind_vpss_raw_vmix_ok;
    int bind_vpss_eis_ok;
    int bind_eis_proc_vpss_ok;
    int bind_proc_vpss_display_vmix_ok;
    int bind_proc_vpss_pre_ok;
    int bind_pre_npu_ok;
    int bind_vmix_osd_ok;
    int bind_osd_vo_ok;
    int vdec_started;
    const char *vdec_src_port;
    const char *vpss_in_port;
    const char *vpss_raw_src_port;
    const char *vpss_eis_src_port;
    const char *eis_in_port;
    const char *eis_src_port;
    const char *proc_vpss_in_port;
    const char *proc_vpss_display_src_port;
    const char *proc_vpss_pre_src_port;
    const char *pre_in_port;
    const char *pre_src_port;
    const char *npu_in_port;
    const char *vmix_raw_in_port;
    const char *vmix_det_in_port;
    const char *vmix_src_port;
    const char *osd_in_port;
    const char *osd_src_port;
    const char *vo_in_port;
} eis_det_chain_t;

static int clamp_int_local(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

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

static void unload_stream(eis_det_stream_t *stream) {
    if (!stream) return;
    free(stream->data);
    free(stream->nal_offsets);
    memset(stream, 0, sizeof(*stream));
}

static int load_stream(const char *path, eis_det_stream_t *stream) {
    FILE *fp = NULL;
    long file_size = 0;
    int capacity = 256;
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
    stream->nal_offsets = (int *)malloc((size_t)capacity * sizeof(int));
    if (!stream->data || !stream->nal_offsets) {
        fclose(fp);
        unload_stream(stream);
        return -1;
    }
    if (fread(stream->data, 1, (size_t)file_size, fp) != (size_t)file_size) {
        fclose(fp);
        unload_stream(stream);
        return -1;
    }
    fclose(fp);
    stream->size = (size_t)file_size;

    for (size_t i = 0; i + 3 < stream->size;) {
        int sc_len = start_code_len(stream->data, stream->size, i);
        if (sc_len > 0) {
            if (stream->nal_count >= capacity) {
                capacity *= 2;
                int *tmp = (int *)realloc(stream->nal_offsets, (size_t)capacity * sizeof(int));
                if (!tmp) {
                    unload_stream(stream);
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
    if (stream->nal_count == 0) stream->nal_offsets[stream->nal_count++] = 0;
    return 0;
}

static int feed_stream_packet(int vdec_chn, eis_det_stream_t *stream, int *is_vcl) {
    if (!stream || !stream->data || stream->size == 0 || stream->nal_count <= 0) return -1;
    if (is_vcl) *is_vcl = 0;
    if (stream->nal_index >= stream->nal_count) stream->nal_index = 0;

    int idx = stream->nal_index++;
    int start = stream->nal_offsets[idx];
    int end = (idx + 1 < stream->nal_count) ? stream->nal_offsets[idx + 1] : (int)stream->size;
    if (start < 0 || end <= start || (size_t)end > stream->size) return -1;

    int sc_len = start_code_len(stream->data, stream->size, (size_t)start);
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
    if (vcl) stream->pts += (uint64_t)(1000 / EIS_DET_FPS);
    if (is_vcl) *is_vcl = vcl;
    return 0;
}

static int bind_first_match(const char *src_mod, int src_id,
                            const char **src_ports, int src_count,
                            const char *dst_mod, int dst_id,
                            const char **dst_ports, int dst_count,
                            const char **used_src_port,
                            const char **used_dst_port) {
    for (int i = 0; i < src_count; ++i) {
        for (int j = 0; j < dst_count; ++j) {
            if (MEDIA_SYS_Bind(src_mod, src_id, src_ports[i],
                               dst_mod, dst_id, dst_ports[j]) == 0) {
                if (used_src_port) *used_src_port = src_ports[i];
                if (used_dst_port) *used_dst_port = dst_ports[j];
                return 0;
            }
        }
    }
    return -1;
}

static void copy_detect_result(const MEDIA_NPU_RESULT *src, eis_det_result_snapshot_t *dst) {
    if (!src || !dst || src->type != MEDIA_NPU_RESULT_DETECT) return;
    memset(dst, 0, sizeof(*dst));
    dst->frame_id = src->frame_id;
    int count = src->u.detect.object_count;
    if (count < 0) count = 0;
    if (count > EIS_DET_MAX_DRAW_OBJECTS) count = EIS_DET_MAX_DRAW_OBJECTS;
    dst->object_count = count;
    for (int i = 0; i < count; ++i) {
        const MEDIA_NPU_OBJECT *obj = &src->u.detect.objects[i];
        eis_det_object_t *out = &dst->objects[i];
        out->x = obj->x;
        out->y = obj->y;
        out->w = obj->w;
        out->h = obj->h;
        out->score = obj->score;
        out->class_id = obj->class_id;
        snprintf(out->label, sizeof(out->label), "%s", obj->label ? obj->label : "object");
    }
}

static int update_box_regions(const eis_det_result_snapshot_t *det) {
    for (int i = 0; i < EIS_DET_MAX_DRAW_OBJECTS; ++i) {
        int region_id = 10 + i;
        MEDIA_OSD_REGION_ATTR attr = {0};
        if (!det || i >= det->object_count) {
            attr.enabled = 0;
            if (MEDIA_OSD_UpdateRegion(EIS_DET_OSD_GRP, region_id, &attr) != 0) return -1;
            continue;
        }

        const eis_det_object_t *obj = &det->objects[i];
        int x = clamp_int_local((int)(obj->x + 0.5f), 0, EIS_DET_MODEL_W - 2);
        int y = EIS_DET_PROCESSED_Y +
                clamp_int_local((int)(obj->y * EIS_DET_DISPLAY_H / EIS_DET_MODEL_H + 0.5f),
                                0, EIS_DET_DISPLAY_H - 2);
        int w = clamp_int_local((int)(obj->w + 0.5f), 2, EIS_DET_MODEL_W - x);
        int h = clamp_int_local((int)(obj->h * EIS_DET_DISPLAY_H / EIS_DET_MODEL_H + 0.5f),
                                2, EIS_DET_OUT_H - y);

        attr.enabled = 1;
        attr.x = x;
        attr.y = y;
        attr.width = w;
        attr.height = h;
        attr.zorder = 20 + i;
        attr.global_alpha = 220;

        MEDIA_OSD_RECT_DESC rect = {0};
        rect.filled = 0;
        rect.line_width = i == 0 ? 4 : 3;
        rect.color.r = i == 0 ? 80 : 40;
        rect.color.g = 255;
        rect.color.b = i == 0 ? 80 : 220;
        rect.color.a = 220;

        if (MEDIA_OSD_UpdateRegion(EIS_DET_OSD_GRP, region_id, &attr) != 0 ||
            MEDIA_OSD_SetRegionRect(EIS_DET_OSD_GRP, region_id, &rect) != 0) {
            return -1;
        }
    }
    return 0;
}

static int update_overlay(uint64_t vdec_count, uint64_t vpss_count,
                          uint64_t eis_count, uint64_t proc_vpss_count,
                          uint64_t pre_count, uint64_t npu_count,
                          uint64_t vmix_count, uint64_t osd_count,
                          uint64_t vo_count, int frame,
                          const MEDIA_EIS_STATS *stats,
                          const eis_det_result_snapshot_t *det) {
    static uint8_t masks[6][EIS_DET_TEXT_MASK_W * EIS_DET_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char count_line[192];
    char eis_line[192];
    char det_line[160];

    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(count_line, sizeof(count_line),
             "VDEC %llu VPSS %llu EIS %llu PVPSS %llu PRE %llu NPU %llu VMIX %llu OSD %llu VO %llu F %d",
             (unsigned long long)vdec_count,
             (unsigned long long)vpss_count,
             (unsigned long long)eis_count,
             (unsigned long long)proc_vpss_count,
             (unsigned long long)pre_count,
             (unsigned long long)npu_count,
             (unsigned long long)vmix_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count,
             frame);
    snprintf(eis_line, sizeof(eis_line),
             "EIS TOTAL %.3f EST %.3f WARP %.3f FB %d  DISPLAY %dx%d  DET %dx%d",
             stats ? stats->total_ms : 0.0,
             stats ? stats->estimate_ms : 0.0,
             stats ? stats->warp_ms : 0.0,
             stats ? stats->fallback_used : 0,
             EIS_DET_DISPLAY_W, EIS_DET_DISPLAY_H, EIS_DET_MODEL_W, EIS_DET_MODEL_H);
    if (det && det->object_count > 0) {
        const eis_det_object_t *obj = &det->objects[0];
        snprintf(det_line, sizeof(det_line), "DET %d TOP %s %.2f BOX %.0f %.0f %.0f %.0f",
                 det->object_count, obj->label, obj->score,
                 obj->x, obj->y, obj->w, obj->h);
    } else {
        snprintf(det_line, sizeof(det_line), "DET 0 TOP NONE");
    }

    if (page_overlay_set_text(EIS_DET_OSD_GRP, 0, 12, EIS_DET_SRC_H, 1,
                              "TOP ORIGINAL  /  BOTTOM EIS + DETECT_NPU YOLOV5",
                              masks[0], sizeof(masks[0]), EIS_DET_TEXT_MASK_W, EIS_DET_TEXT_MASK_H,
                              220, 245, 255, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_DET_OSD_GRP, 1, 12, EIS_DET_INFO_Y + 8, 1,
                              "FLOW VDEC->VPSS top / VPSS->EIS->VPSS display+RGA model->DETECT_NPU boxes",
                              masks[1], sizeof(masks[1]), EIS_DET_TEXT_MASK_W, EIS_DET_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_DET_OSD_GRP, 2, 12, EIS_DET_INFO_Y + 24, 1,
                              perf_line,
                              masks[2], sizeof(masks[2]), EIS_DET_TEXT_MASK_W, EIS_DET_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_DET_OSD_GRP, 3, 12, EIS_DET_INFO_Y + 40, 1,
                              eis_line,
                              masks[3], sizeof(masks[3]), EIS_DET_TEXT_MASK_W, EIS_DET_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_DET_OSD_GRP, 4, 12, EIS_DET_INFO_Y + 56, 1,
                              count_line,
                              masks[4], sizeof(masks[4]), EIS_DET_TEXT_MASK_W, EIS_DET_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(EIS_DET_OSD_GRP, 5, 12, EIS_DET_INFO_Y + 72, 1,
                              det_line,
                              masks[5], sizeof(masks[5]), EIS_DET_TEXT_MASK_W, EIS_DET_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return update_box_regions(det);
}

static int setup_chain(eis_det_chain_t *chain) {
    MEDIA_VDEC_ATTR vdec = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_EIS_ATTR eis = {0};
    MEDIA_VPSS_ATTR proc_vpss = {0};
    MEDIA_RGA_GRP_ATTR pre = {0};
    MEDIA_NPU_ATTR npu = {0};
    MEDIA_VMIX_ATTR vmix = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    const char *vdec_out_ports[] = {"output", "output0"};
    const char *vpss_in_ports[] = {"input", "input0"};
    const char *vpss_raw_out_ports[] = {"output0", "output"};
    const char *vpss_eis_out_ports[] = {"output1"};
    const char *eis_in_ports[] = {"input", "input0"};
    const char *eis_out_ports[] = {"output", "output0"};
    const char *proc_vpss_in_ports[] = {"input", "input0"};
    const char *proc_vpss_display_out_ports[] = {"output0", "output"};
    const char *proc_vpss_model_out_ports[] = {"output1"};
    const char *rga_in_ports[] = {"input0", "input"};
    const char *rga_out_ports[] = {"output0", "output"};
    const char *npu_in_ports[] = {"input", "input0"};
    const char *vmix_in0_ports[] = {"input0"};
    const char *vmix_in1_ports[] = {"input1"};
    const char *vmix_out_ports[] = {"output0", "output"};
    const char *osd_in_ports[] = {"input", "input0"};
    const char *osd_out_ports[] = {"output0", "output"};
    const char *vo_in_ports[] = {"input0", "input"};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(EIS_DET_VDEC_POOL, EIS_DET_VDEC_FRAME_SIZE, 6) != 0) return -1;
    chain->vdec_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_DET_VPSS_POOL, EIS_DET_VPSS_FRAME_SIZE, 12) != 0) return -1;
    chain->vpss_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_DET_EIS_POOL, EIS_DET_EIS_FRAME_SIZE, 6) != 0) return -1;
    chain->eis_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_DET_PROC_VPSS_POOL, EIS_DET_PROC_VPSS_FRAME_SIZE, 12) != 0) return -1;
    chain->proc_vpss_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_DET_RGB_POOL, EIS_DET_RGB_FRAME_SIZE, 4) != 0) return -1;
    chain->rgb_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_DET_VMIX_POOL, EIS_DET_DISPLAY_FRAME_SIZE, 4) != 0) return -1;
    chain->vmix_pool_ok = 1;
    if (MEDIA_POOL_Create(EIS_DET_OSD_POOL, EIS_DET_DISPLAY_FRAME_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vdec.width = EIS_DET_SRC_W;
    vdec.height = EIS_DET_SRC_H;
    vdec.stride = EIS_DET_SRC_STRIDE;
    vdec.buf_cnt = 6;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = EIS_DET_VDEC_POOL;
    vdec.has_input_port = 0;
    vdec.input_depth = 0;
    if (MEDIA_VDEC_CreateChn(EIS_DET_VDEC_CHN, &vdec) != 0) return -1;
    chain->vdec_ok = 1;

    vpss.width = EIS_DET_SRC_W;
    vpss.height = EIS_DET_SRC_H;
    vpss.input_stride = EIS_DET_SRC_STRIDE;
    vpss.input_depth = 6;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = EIS_DET_FPS;
    vpss.out_fps = EIS_DET_FPS;
    vpss.output_count = 2;
    for (int i = 0; i < 2; ++i) {
        vpss.outputs[i].output_id = i;
        vpss.outputs[i].out_width = EIS_DET_SRC_W;
        vpss.outputs[i].out_height = EIS_DET_SRC_H;
        vpss.outputs[i].out_stride = EIS_DET_SRC_STRIDE;
        vpss.outputs[i].pool_id = EIS_DET_VPSS_POOL;
        vpss.outputs[i].crop_w = EIS_DET_SRC_W;
        vpss.outputs[i].crop_h = EIS_DET_SRC_H;
        vpss.outputs[i].output_format = MEDIA_FORMAT_NV12;
    }
    if (MEDIA_VPSS_SetAttr(EIS_DET_VPSS_GRP, &vpss) != 0) return -1;
    chain->vpss_ok = 1;

    eis.width = EIS_DET_SRC_W;
    eis.height = EIS_DET_SRC_H;
    eis.format = MEDIA_FORMAT_NV12;
    eis.input_depth = 6;
    eis.output_pool_id = EIS_DET_EIS_POOL;
    eis.input_stride = EIS_DET_SRC_STRIDE;
    eis.output_stride = EIS_DET_SRC_STRIDE;
    eis.crop_ratio = 0.08f;
    eis.smoothing_window = 15;
    eis.estimate_width = 320;
    eis.search_radius = 16;
    eis.block_step = 4;
    if (MEDIA_EIS_CreateGrp(EIS_DET_EIS_GRP, &eis) != 0) return -1;
    chain->eis_ok = 1;

    proc_vpss.width = EIS_DET_SRC_W;
    proc_vpss.height = EIS_DET_SRC_H;
    proc_vpss.input_stride = EIS_DET_SRC_STRIDE;
    proc_vpss.input_depth = 6;
    proc_vpss.input_format = MEDIA_FORMAT_NV12;
    proc_vpss.in_fps = EIS_DET_FPS;
    proc_vpss.out_fps = EIS_DET_FPS;
    proc_vpss.output_count = 2;
    proc_vpss.outputs[0].output_id = 0;
    proc_vpss.outputs[0].out_width = EIS_DET_DISPLAY_W;
    proc_vpss.outputs[0].out_height = EIS_DET_DISPLAY_H;
    proc_vpss.outputs[0].out_stride = EIS_DET_DISPLAY_STRIDE;
    proc_vpss.outputs[0].pool_id = EIS_DET_PROC_VPSS_POOL;
    proc_vpss.outputs[0].crop_w = EIS_DET_SRC_W;
    proc_vpss.outputs[0].crop_h = EIS_DET_SRC_H;
    proc_vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    proc_vpss.outputs[1].output_id = 1;
    proc_vpss.outputs[1].out_width = EIS_DET_MODEL_W;
    proc_vpss.outputs[1].out_height = EIS_DET_MODEL_H;
    proc_vpss.outputs[1].out_stride = EIS_DET_MODEL_STRIDE;
    proc_vpss.outputs[1].pool_id = EIS_DET_PROC_VPSS_POOL;
    proc_vpss.outputs[1].crop_w = EIS_DET_SRC_W;
    proc_vpss.outputs[1].crop_h = EIS_DET_SRC_H;
    proc_vpss.outputs[1].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(EIS_DET_PROC_VPSS_GRP, &proc_vpss) != 0) return -1;
    chain->proc_vpss_ok = 1;

    pre.algo = MEDIA_RGA_ALG_CSC;
    pre.input_count = 1;
    pre.output_count = 1;
    pre.input_depth = 4;
    pre.output_depth = 4;
    pre.inputs[0].width = EIS_DET_MODEL_W;
    pre.inputs[0].height = EIS_DET_MODEL_H;
    pre.inputs[0].stride = EIS_DET_MODEL_STRIDE;
    pre.inputs[0].format = MEDIA_FORMAT_NV12;
    pre.outputs[0].width = EIS_DET_MODEL_W;
    pre.outputs[0].height = EIS_DET_MODEL_H;
    pre.outputs[0].stride = EIS_DET_RGB_STRIDE;
    pre.outputs[0].format = MEDIA_FORMAT_RGB888;
    pre.outputs[0].pool_id = EIS_DET_RGB_POOL;
    if (MEDIA_RGA_CreateGrp(EIS_DET_PRE_RGA_GRP, &pre) != 0 ||
        MEDIA_RGA_Start(EIS_DET_PRE_RGA_GRP) != 0) {
        return -1;
    }
    chain->pre_rga_ok = 1;

    npu.model_path = EIS_DET_MODEL_PATH;
    npu.label_path = path_readable(EIS_DET_LABEL_PATH) ? EIS_DET_LABEL_PATH : NULL;
    npu.adapter_name = "yolov5";
    npu.backend = MEDIA_NPU_BACKEND_RKNN;
    npu.task = MEDIA_NPU_TASK_DETECT;
    npu.input_width = EIS_DET_MODEL_W;
    npu.input_height = EIS_DET_MODEL_H;
    npu.input_format = MEDIA_FORMAT_RGB888;
    npu.input_layout = MEDIA_NPU_LAYOUT_NHWC;
    npu.input_depth = 4;
    npu.passthrough = 0;
    npu.score_thresh = 0.25f;
    npu.nms_thresh = 0.45f;
    if (MEDIA_NPU_CreateGrp(EIS_DET_NPU_GRP, &npu) != 0 ||
        MEDIA_NPU_Start(EIS_DET_NPU_GRP) != 0) {
        return -1;
    }
    chain->npu_ok = 1;

    vmix.input_count = 2;
    vmix.output_width = EIS_DET_OUT_W;
    vmix.output_height = EIS_DET_OUT_H;
    vmix.output_stride = EIS_DET_OUT_STRIDE;
    vmix.format = MEDIA_FORMAT_NV12;
    vmix.input_depth = 4;
    vmix.output_pool_id = EIS_DET_VMIX_POOL;
    vmix.primary_index = -1;
    vmix.channels[0].enabled = 1;
    vmix.channels[0].x = 0;
    vmix.channels[0].y = EIS_DET_RAW_Y;
    vmix.channels[0].width = EIS_DET_SRC_W;
    vmix.channels[0].height = EIS_DET_SRC_H;
    vmix.channels[0].alpha = 1.0f;
    vmix.channels[0].stride = EIS_DET_SRC_STRIDE;
    vmix.channels[0].format = MEDIA_FORMAT_NV12;
    vmix.channels[1].enabled = 1;
    vmix.channels[1].x = 0;
    vmix.channels[1].y = EIS_DET_PROCESSED_Y;
    vmix.channels[1].width = EIS_DET_DISPLAY_W;
    vmix.channels[1].height = EIS_DET_DISPLAY_H;
    vmix.channels[1].alpha = 1.0f;
    vmix.channels[1].stride = EIS_DET_DISPLAY_STRIDE;
    vmix.channels[1].format = MEDIA_FORMAT_NV12;
    if (MEDIA_VMIX_CreateGrp(EIS_DET_VMIX_GRP, &vmix) != 0 ||
        MEDIA_VMIX_Start(EIS_DET_VMIX_GRP) != 0 ||
        MEDIA_VMIX_Enable(EIS_DET_VMIX_GRP) != 0) {
        return -1;
    }
    chain->vmix_ok = 1;

    osd.input_width = EIS_DET_OUT_W;
    osd.input_height = EIS_DET_OUT_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = EIS_DET_OSD_POOL;
    osd.input_stride = EIS_DET_OUT_STRIDE;
    osd.output_stride = EIS_DET_OUT_STRIDE;
    osd.max_regions = 16;
    if (MEDIA_OSD_CreateGrp(EIS_DET_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(EIS_DET_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = EIS_DET_SCREEN_W;
    vo.height = EIS_DET_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, EIS_DET_VIEW_X, EIS_DET_VIEW_Y,
                           EIS_DET_OUT_W, EIS_DET_OUT_H, EIS_DET_OUT_STRIDE, 4,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (bind_first_match("VDEC", EIS_DET_VDEC_CHN, vdec_out_ports, 2,
                         "VPSS", EIS_DET_VPSS_GRP, vpss_in_ports, 2,
                         &chain->vdec_src_port, &chain->vpss_in_port) != 0) {
        return -1;
    }
    chain->bind_vdec_vpss_ok = 1;
    if (bind_first_match("VPSS", EIS_DET_VPSS_GRP, vpss_raw_out_ports, 2,
                         "VMIX", EIS_DET_VMIX_GRP, vmix_in0_ports, 1,
                         &chain->vpss_raw_src_port, &chain->vmix_raw_in_port) != 0) {
        return -1;
    }
    chain->bind_vpss_raw_vmix_ok = 1;
    if (bind_first_match("VPSS", EIS_DET_VPSS_GRP, vpss_eis_out_ports, 1,
                         "EIS", EIS_DET_EIS_GRP, eis_in_ports, 2,
                         &chain->vpss_eis_src_port, &chain->eis_in_port) != 0) {
        return -1;
    }
    chain->bind_vpss_eis_ok = 1;
    if (bind_first_match("EIS", EIS_DET_EIS_GRP, eis_out_ports, 2,
                         "VPSS", EIS_DET_PROC_VPSS_GRP, proc_vpss_in_ports, 2,
                         &chain->eis_src_port, &chain->proc_vpss_in_port) != 0) {
        return -1;
    }
    chain->bind_eis_proc_vpss_ok = 1;
    if (bind_first_match("VPSS", EIS_DET_PROC_VPSS_GRP, proc_vpss_display_out_ports, 2,
                         "VMIX", EIS_DET_VMIX_GRP, vmix_in1_ports, 1,
                         &chain->proc_vpss_display_src_port, &chain->vmix_det_in_port) != 0) {
        return -1;
    }
    chain->bind_proc_vpss_display_vmix_ok = 1;
    if (bind_first_match("VPSS", EIS_DET_PROC_VPSS_GRP, proc_vpss_model_out_ports, 1,
                         "RGA", EIS_DET_PRE_RGA_GRP, rga_in_ports, 2,
                         &chain->proc_vpss_pre_src_port, &chain->pre_in_port) != 0) {
        return -1;
    }
    chain->bind_proc_vpss_pre_ok = 1;
    if (bind_first_match("RGA", EIS_DET_PRE_RGA_GRP, rga_out_ports, 2,
                         "NPU", EIS_DET_NPU_GRP, npu_in_ports, 2,
                         &chain->pre_src_port, &chain->npu_in_port) != 0) {
        return -1;
    }
    chain->bind_pre_npu_ok = 1;
    if (bind_first_match("VMIX", EIS_DET_VMIX_GRP, vmix_out_ports, 2,
                         "OSD", EIS_DET_OSD_GRP, osd_in_ports, 2,
                         &chain->vmix_src_port, &chain->osd_in_port) != 0) {
        return -1;
    }
    chain->bind_vmix_osd_ok = 1;
    if (bind_first_match("OSD", EIS_DET_OSD_GRP, osd_out_ports, 2,
                         "VO", 0, vo_in_ports, 2,
                         &chain->osd_src_port, &chain->vo_in_port) != 0) {
        return -1;
    }
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_EIS_Start(EIS_DET_EIS_GRP) != 0 ||
        MEDIA_VPSS_Enable(EIS_DET_PROC_VPSS_GRP) != 0 ||
        MEDIA_VPSS_Enable(EIS_DET_VPSS_GRP) != 0 ||
        MEDIA_VDEC_Start(EIS_DET_VDEC_CHN) != 0) {
        return -1;
    }
    chain->vdec_started = 1;
    set_tile_status("VDEC", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("EIS", TILE_LIVE);
    set_tile_status("RGA", TILE_LIVE);
    set_tile_status("DETECT_NPU", TILE_LIVE);
    set_tile_status("EIS_DETECT_NPU", TILE_LIVE);
    set_tile_status("VMIX", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void drain_eis_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_EIS_GetFrame(EIS_DET_EIS_GRP, &out, 0) != 0) break;
            MEDIA_EIS_ReleaseFrame(EIS_DET_EIS_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_vpss_outputs(void) {
    for (int ch = 0; ch < 2; ++ch) {
        for (int pass = 0; pass < 3; ++pass) {
            int drained = 0;
            for (int i = 0; i < 8; ++i) {
                MEDIA_BUFFER out = {-1, -1};
                if (MEDIA_VPSS_Chn_GetFrame(EIS_DET_VPSS_GRP, ch, &out, 0) != 0) break;
                MEDIA_VPSS_Chn_ReleaseFrame(EIS_DET_VPSS_GRP, ch, out);
                drained = 1;
            }
            if (!drained) break;
            usleep(1000);
        }
    }
}

static void drain_proc_vpss_outputs(void) {
    for (int ch = 0; ch < 2; ++ch) {
        for (int pass = 0; pass < 3; ++pass) {
            int drained = 0;
            for (int i = 0; i < 8; ++i) {
                MEDIA_BUFFER out = {-1, -1};
                if (MEDIA_VPSS_Chn_GetFrame(EIS_DET_PROC_VPSS_GRP, ch, &out, 0) != 0) break;
                MEDIA_VPSS_Chn_ReleaseFrame(EIS_DET_PROC_VPSS_GRP, ch, out);
                drained = 1;
            }
            if (!drained) break;
            usleep(1000);
        }
    }
}

static void drain_vmix_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VMIX_GetFrame(EIS_DET_VMIX_GRP, &out, 0) != 0) break;
            MEDIA_VMIX_ReleaseFrame(EIS_DET_VMIX_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_npu_results(void) {
    for (int i = 0; i < 16; ++i) {
        MEDIA_NPU_RESULT result;
        memset(&result, 0, sizeof(result));
        if (MEDIA_NPU_GetResult(EIS_DET_NPU_GRP, &result, 0) != 0) break;
        MEDIA_NPU_ReleaseResult(EIS_DET_NPU_GRP, &result);
    }
}

static void drain_npu_frames(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_NPU_GetFrame(EIS_DET_NPU_GRP, &out, 0) != 0) break;
            MEDIA_NPU_ReleaseFrame(EIS_DET_NPU_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_osd_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_OSD_GetFrame(EIS_DET_OSD_GRP, &out, 0) != 0) break;
            MEDIA_OSD_ReleaseFrame(EIS_DET_OSD_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void cleanup_chain(eis_det_chain_t *chain) {
    if (!chain) return;
    if (chain->vdec_started) {
        MEDIA_VDEC_Stop(EIS_DET_VDEC_CHN);
        chain->vdec_started = 0;
        usleep(50000);
    }
    if (chain->npu_ok) drain_npu_results();
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", EIS_DET_OSD_GRP, chain->osd_src_port,
                         "VO", 0, chain->vo_in_port);
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_vmix_osd_ok) {
        MEDIA_SYS_UnBind("VMIX", EIS_DET_VMIX_GRP, chain->vmix_src_port,
                         "OSD", EIS_DET_OSD_GRP, chain->osd_in_port);
        chain->bind_vmix_osd_ok = 0;
    }
    if (chain->bind_proc_vpss_display_vmix_ok) {
        MEDIA_SYS_UnBind("VPSS", EIS_DET_PROC_VPSS_GRP, chain->proc_vpss_display_src_port,
                         "VMIX", EIS_DET_VMIX_GRP, chain->vmix_det_in_port);
        chain->bind_proc_vpss_display_vmix_ok = 0;
    }
    if (chain->bind_pre_npu_ok) {
        MEDIA_SYS_UnBind("RGA", EIS_DET_PRE_RGA_GRP, chain->pre_src_port,
                         "NPU", EIS_DET_NPU_GRP, chain->npu_in_port);
        chain->bind_pre_npu_ok = 0;
    }
    if (chain->bind_proc_vpss_pre_ok) {
        MEDIA_SYS_UnBind("VPSS", EIS_DET_PROC_VPSS_GRP, chain->proc_vpss_pre_src_port,
                         "RGA", EIS_DET_PRE_RGA_GRP, chain->pre_in_port);
        chain->bind_proc_vpss_pre_ok = 0;
    }
    if (chain->bind_eis_proc_vpss_ok) {
        MEDIA_SYS_UnBind("EIS", EIS_DET_EIS_GRP, chain->eis_src_port,
                         "VPSS", EIS_DET_PROC_VPSS_GRP, chain->proc_vpss_in_port);
        chain->bind_eis_proc_vpss_ok = 0;
    }
    if (chain->eis_ok) {
        drain_eis_output();
        MEDIA_EIS_Stop(EIS_DET_EIS_GRP);
        drain_eis_output();
    }
    if (chain->bind_vpss_eis_ok) {
        MEDIA_SYS_UnBind("VPSS", EIS_DET_VPSS_GRP, chain->vpss_eis_src_port,
                         "EIS", EIS_DET_EIS_GRP, chain->eis_in_port);
        chain->bind_vpss_eis_ok = 0;
    }
    if (chain->bind_vpss_raw_vmix_ok) {
        MEDIA_SYS_UnBind("VPSS", EIS_DET_VPSS_GRP, chain->vpss_raw_src_port,
                         "VMIX", EIS_DET_VMIX_GRP, chain->vmix_raw_in_port);
        chain->bind_vpss_raw_vmix_ok = 0;
    }
    if (chain->bind_vdec_vpss_ok) {
        MEDIA_SYS_UnBind("VDEC", EIS_DET_VDEC_CHN, chain->vdec_src_port,
                         "VPSS", EIS_DET_VPSS_GRP, chain->vpss_in_port);
        chain->bind_vdec_vpss_ok = 0;
    }
    if (chain->proc_vpss_ok) MEDIA_VPSS_Disable(EIS_DET_PROC_VPSS_GRP);
    if (chain->vpss_ok) MEDIA_VPSS_Disable(EIS_DET_VPSS_GRP);

    if (chain->npu_ok) {
        drain_npu_results();
        drain_npu_frames();
    }
    if (chain->osd_ok) {
        drain_osd_output();
        MEDIA_OSD_Stop(EIS_DET_OSD_GRP);
        drain_osd_output();
        MEDIA_OSD_DestroyGrp(EIS_DET_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->vmix_ok) {
        drain_vmix_output();
        MEDIA_VMIX_Disable(EIS_DET_VMIX_GRP);
        MEDIA_VMIX_Stop(EIS_DET_VMIX_GRP);
        drain_vmix_output();
        MEDIA_VMIX_DestroyGrp(EIS_DET_VMIX_GRP);
        chain->vmix_ok = 0;
    }
    if (chain->npu_ok) {
        MEDIA_NPU_Stop(EIS_DET_NPU_GRP);
        drain_npu_results();
        drain_npu_frames();
        MEDIA_NPU_DestroyGrp(EIS_DET_NPU_GRP);
        chain->npu_ok = 0;
    }
    if (chain->pre_rga_ok) {
        MEDIA_RGA_Stop(EIS_DET_PRE_RGA_GRP);
        MEDIA_RGA_DestroyChn(EIS_DET_PRE_RGA_GRP);
        chain->pre_rga_ok = 0;
    }
    if (chain->proc_vpss_ok) {
        drain_proc_vpss_outputs();
        MEDIA_VPSS_DestroyGrp(EIS_DET_PROC_VPSS_GRP);
        chain->proc_vpss_ok = 0;
    }
    if (chain->eis_ok) {
        MEDIA_EIS_DestroyGrp(EIS_DET_EIS_GRP);
        chain->eis_ok = 0;
    }
    if (chain->vpss_ok) {
        drain_vpss_outputs();
        MEDIA_VPSS_DestroyGrp(EIS_DET_VPSS_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->vdec_ok) {
        MEDIA_VDEC_DestroyChn(EIS_DET_VDEC_CHN);
        chain->vdec_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(EIS_DET_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->vmix_pool_ok) {
        MEDIA_POOL_Destroy(EIS_DET_VMIX_POOL);
        chain->vmix_pool_ok = 0;
    }
    if (chain->rgb_pool_ok) {
        MEDIA_POOL_Destroy(EIS_DET_RGB_POOL);
        chain->rgb_pool_ok = 0;
    }
    if (chain->proc_vpss_pool_ok) {
        /*
         * VMIX/RGA can release the final processed VPSS buffer late during
         * teardown, so leave this pool to MEDIA_SYS_Exit like other VMIX-fed
         * intermediate pools in this demo.
         */
        chain->proc_vpss_pool_ok = 0;
    }
    if (chain->eis_pool_ok) {
        chain->eis_pool_ok = 0;
    }
    if (chain->vpss_pool_ok) {
        MEDIA_POOL_Destroy(EIS_DET_VPSS_POOL);
        chain->vpss_pool_ok = 0;
    }
    if (chain->vdec_pool_ok) {
        MEDIA_POOL_Destroy(EIS_DET_VDEC_POOL);
        chain->vdec_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_eis_detect_npu_run(volatile sig_atomic_t *running) {
    eis_det_stream_t stream;
    eis_det_chain_t chain = {0};
    eis_det_result_snapshot_t last_det;
    memset(&stream, 0, sizeof(stream));
    memset(&last_det, 0, sizeof(last_det));

    if (!path_readable(EIS_DET_INPUT_PATH)) {
        fprintf(stderr, "EIS_DETECT_NPU input missing: %s\n", EIS_DET_INPUT_PATH);
        return 1;
    }
    if (!path_readable(EIS_DET_MODEL_PATH)) {
        fprintf(stderr, "EIS_DETECT_NPU model missing: %s\n", EIS_DET_MODEL_PATH);
        return 1;
    }
    if (load_stream(EIS_DET_INPUT_PATH, &stream) != 0) {
        fprintf(stderr, "EIS_DETECT_NPU: failed to load H264 stream\n");
        return 1;
    }
    if (setup_chain(&chain) != 0) {
        fprintf(stderr, "EIS_DETECT_NPU chain setup failed\n");
        cleanup_chain(&chain);
        unload_stream(&stream);
        return 1;
    }

    MEDIA_NPU_MODEL_INFO info = {0};
    (void)MEDIA_NPU_GetModelInfo(EIS_DET_NPU_GRP, &info);
    printf("EIS_DETECT_NPU standalone: H264(%s) -> VDEC%d.%s -> VPSS%d(raw+eis) -> EIS%d.%s -> VPSS%d(display+model) -> RGA%d.%s -> NPU%d -> VMIX%d.%s -> OSD%d.%s -> VO0.%s model=%s adapter=%s. Ctrl+C to stop.\n",
           EIS_DET_INPUT_PATH,
           EIS_DET_VDEC_CHN, chain.vdec_src_port ? chain.vdec_src_port : "output",
           EIS_DET_VPSS_GRP,
           EIS_DET_EIS_GRP, chain.eis_src_port ? chain.eis_src_port : "output",
           EIS_DET_PROC_VPSS_GRP,
           EIS_DET_PRE_RGA_GRP, chain.pre_src_port ? chain.pre_src_port : "output0",
           EIS_DET_NPU_GRP,
           EIS_DET_VMIX_GRP, chain.vmix_src_port ? chain.vmix_src_port : "output0",
           EIS_DET_OSD_GRP, chain.osd_src_port ? chain.osd_src_port : "output0",
           chain.vo_in_port ? chain.vo_in_port : "input0",
           info.model_name ? info.model_name : EIS_DET_MODEL_PATH,
           info.adapter_name ? info.adapter_name : "yolov5");

    int frame = 0;
    while (!running || *running) {
        int is_vcl = 0;
        if (feed_stream_packet(EIS_DET_VDEC_CHN, &stream, &is_vcl) != 0) {
            fprintf(stderr, "EIS_DETECT_NPU: MEDIA_VDEC_SendPacket failed\n");
            usleep(1000000 / EIS_DET_FPS);
            continue;
        }

        for (int i = 0; i < 8; ++i) {
            MEDIA_NPU_RESULT result;
            memset(&result, 0, sizeof(result));
            if (MEDIA_NPU_GetResult(EIS_DET_NPU_GRP, &result, 0) != 0) break;
            copy_detect_result(&result, &last_det);
            MEDIA_NPU_ReleaseResult(EIS_DET_NPU_GRP, &result);
        }

        if (is_vcl) {
            frame++;
            if ((frame % EIS_DET_FPS) != 0) {
                (void)update_box_regions(&last_det);
            }
            if ((frame % EIS_DET_FPS) == 0) {
                uint64_t vdec_count = 0;
                uint64_t vpss_count = 0;
                uint64_t eis_count = 0;
                uint64_t proc_vpss_count = 0;
                uint64_t pre_count = 0;
                uint64_t npu_count = 0;
                uint64_t vmix_count = 0;
                uint64_t osd_count = 0;
                uint64_t vo_count = 0;
                MEDIA_EIS_STATS stats;
                memset(&stats, 0, sizeof(stats));
                (void)MEDIA_SYS_GetModuleFrameCount("VDEC", EIS_DET_VDEC_CHN, &vdec_count);
                (void)MEDIA_SYS_GetModuleFrameCount("VPSS", EIS_DET_VPSS_GRP, &vpss_count);
                (void)MEDIA_SYS_GetModuleFrameCount("EIS", EIS_DET_EIS_GRP, &eis_count);
                (void)MEDIA_SYS_GetModuleFrameCount("VPSS", EIS_DET_PROC_VPSS_GRP, &proc_vpss_count);
                (void)MEDIA_SYS_GetModuleFrameCount("RGA", EIS_DET_PRE_RGA_GRP, &pre_count);
                (void)MEDIA_SYS_GetModuleFrameCount("NPU", EIS_DET_NPU_GRP, &npu_count);
                (void)MEDIA_SYS_GetModuleFrameCount("VMIX", EIS_DET_VMIX_GRP, &vmix_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", EIS_DET_OSD_GRP, &osd_count);
                (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
                (void)MEDIA_EIS_GetStats(EIS_DET_EIS_GRP, &stats);
                (void)update_overlay(vdec_count, vpss_count, eis_count, proc_vpss_count,
                                     pre_count, npu_count, vmix_count,
                                     osd_count, vo_count, frame,
                                     &stats, &last_det);
                printf("EIS_DETECT_NPU vdec=%llu vpss=%llu eis=%llu proc_vpss=%llu pre_rga=%llu npu=%llu vmix=%llu osd=%llu vo=%llu det=%d frame=%d total=%.3fms estimate=%.3fms warp=%.3fms fallback=%d standalone=1",
                       (unsigned long long)vdec_count,
                       (unsigned long long)vpss_count,
                       (unsigned long long)eis_count,
                       (unsigned long long)proc_vpss_count,
                       (unsigned long long)pre_count,
                       (unsigned long long)npu_count,
                       (unsigned long long)vmix_count,
                       (unsigned long long)osd_count,
                       (unsigned long long)vo_count,
                       last_det.object_count,
                       frame,
                       stats.total_ms,
                       stats.estimate_ms,
                       stats.warp_ms,
                       stats.fallback_used);
                if (last_det.object_count > 0) {
                    const eis_det_object_t *obj = &last_det.objects[0];
                    printf(" top0=%s %.2f box=%.0f,%.0f %.0fx%.0f",
                           obj->label, obj->score, obj->x, obj->y, obj->w, obj->h);
                }
                printf("\n");
            }
            usleep(1000000 / EIS_DET_FPS);
        } else {
            usleep(1000);
        }
    }

    cleanup_chain(&chain);
    unload_stream(&stream);
    return 0;
}
