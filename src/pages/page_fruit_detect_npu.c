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

#define NPU_SCREEN_W 1080
#define NPU_SCREEN_H 1920
#define NPU_W 640
#define NPU_H 640
#define NPU_STRIDE 640
#define NPU_FPS 30
#define NPU_VDEC_POOL 3
#define NPU_RGB_POOL 4
#define NPU_OUT_POOL 5
#define NPU_OSD_POOL 8
#define NPU_VDEC_CHN 110
#define NPU_PRE_RGA_GRP 111
#define NPU_GRP 112
#define NPU_POST_RGA_GRP 113
#define NPU_OSD_GRP 81
#define NPU_VIEW_X ((NPU_SCREEN_W - NPU_W) / 2)
#define NPU_VIEW_Y 640
#define NPU_VDEC_FRAME_SIZE (NPU_STRIDE * NPU_H * 2)
#define NPU_RGB_FRAME_SIZE (NPU_W * NPU_H * 3)
#define NPU_NV12_FRAME_SIZE (NPU_STRIDE * NPU_H * 3 / 2)
#define NPU_H264_PATH "assets/loop/fruit_detect/deepnir_10fruit_640x640.h264"
#define NPU_MODEL_PATH "assets/npu/deepnir_10fruit_yolov8n_640_fp.rknn"
#define NPU_LABEL_PATH "assets/npu/deepnir_10fruit_labels.txt"
#define NPU_MAX_DRAW_OBJECTS 4
#define NPU_TEXT_MASK_W 620
#define NPU_TEXT_MASK_H 32
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    uint8_t *data;
    size_t size;
    int *nal_offsets;
    int nal_count;
    int nal_index;
    uint64_t pts;
} h264_stream_t;

typedef struct {
    float x;
    float y;
    float w;
    float h;
    float score;
    int class_id;
    char label[64];
} npu_object_t;

typedef struct {
    uint64_t frame_id;
    int object_count;
    npu_object_t objects[NPU_MAX_DRAW_OBJECTS];
} npu_result_snapshot_t;

typedef struct {
    int sys_ok;
    int vdec_pool_ok;
    int rgb_pool_ok;
    int out_pool_ok;
    int osd_pool_ok;
    int vdec_ok;
    int pre_rga_ok;
    int npu_ok;
    int post_rga_ok;
    int osd_ok;
    int vo_ok;
    int bind_vdec_pre_ok;
    int bind_pre_npu_ok;
    int bind_npu_post_ok;
    int bind_post_osd_ok;
    int bind_osd_vo_ok;
    int vdec_started;
    const char *vdec_src_port;
    const char *pre_in_port;
    const char *pre_src_port;
    const char *npu_in_port;
    const char *npu_src_port;
    const char *post_in_port;
    const char *post_src_port;
    const char *osd_in_port;
    const char *osd_src_port;
    const char *vo_in_port;
} npu_chain_t;

static int clamp_int_local(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void drain_npu_osd_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_OSD_GetFrame(NPU_OSD_GRP, &out, 0) != 0) break;
            MEDIA_OSD_ReleaseFrame(NPU_OSD_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static int path_readable(const char *path) {
    return path && access(path, R_OK) == 0;
}

static int h264_start_code_len(const uint8_t *data, size_t size, size_t pos) {
    if (!data || pos + 3 > size) return 0;
    if (data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) return 3;
    if (pos + 4 <= size && data[pos] == 0x00 && data[pos + 1] == 0x00 &&
        data[pos + 2] == 0x00 && data[pos + 3] == 0x01) {
        return 4;
    }
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
    if (stream->nal_count == 0) stream->nal_offsets[stream->nal_count++] = 0;
    stream->nal_index = 0;
    stream->pts = 0;
    return 0;
}

static int feed_h264_stream_packet(int vdec_chn, h264_stream_t *stream, int *is_vcl) {
    if (!stream || !stream->data || stream->size == 0 || stream->nal_count <= 0) return -1;
    if (is_vcl) *is_vcl = 0;
    if (stream->nal_index >= stream->nal_count) stream->nal_index = 0;

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

static void copy_npu_result(const MEDIA_NPU_RESULT *src, npu_result_snapshot_t *dst) {
    if (!src || !dst || src->type != MEDIA_NPU_RESULT_DETECT) return;
    memset(dst, 0, sizeof(*dst));
    dst->frame_id = src->frame_id;
    int count = src->u.detect.object_count;
    if (count < 0) count = 0;
    for (int i = 0; i < count; ++i) {
        const MEDIA_NPU_OBJECT *obj = &src->u.detect.objects[i];
        const char *label = obj->label ? obj->label : "object";
        if (dst->object_count >= NPU_MAX_DRAW_OBJECTS) break;
        npu_object_t *out = &dst->objects[dst->object_count++];
        out->x = obj->x;
        out->y = obj->y;
        out->w = obj->w;
        out->h = obj->h;
        out->score = obj->score;
        out->class_id = obj->class_id;
        snprintf(out->label, sizeof(out->label), "%s", label);
    }
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

static int update_npu_box_regions(const npu_result_snapshot_t *det) {
    for (int i = 0; i < NPU_MAX_DRAW_OBJECTS; ++i) {
        int region_id = 10 + i;
        MEDIA_OSD_REGION_ATTR attr = {0};
        if (!det || i >= det->object_count) {
            attr.enabled = 0;
            attr.x = 0;
            attr.y = 0;
            attr.width = 2;
            attr.height = 2;
            attr.zorder = 20 + i;
            attr.global_alpha = 0;
            if (MEDIA_OSD_UpdateRegion(NPU_OSD_GRP, region_id, &attr) != 0) return -1;
            continue;
        }

        const npu_object_t *obj = &det->objects[i];
        int x = clamp_int_local((int)(obj->x + 0.5f), 0, NPU_W - 2);
        int y = clamp_int_local((int)(obj->y + 0.5f), 0, NPU_H - 2);
        int w = clamp_int_local((int)(obj->w + 0.5f), 2, NPU_W - x);
        int h = clamp_int_local((int)(obj->h + 0.5f), 2, NPU_H - y);

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

        if (MEDIA_OSD_UpdateRegion(NPU_OSD_GRP, region_id, &attr) != 0 ||
            MEDIA_OSD_SetRegionRect(NPU_OSD_GRP, region_id, &rect) != 0) {
            return -1;
        }
    }
    return 0;
}

static int update_npu_overlay(uint64_t vdec_count, uint64_t pre_count,
                              uint64_t npu_count, uint64_t post_count,
                              uint64_t osd_count, uint64_t vo_count,
                              int frame, const npu_result_snapshot_t *det) {
    static uint8_t masks[5][NPU_TEXT_MASK_W * NPU_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char count_line[160];
    char det_line[160];

    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(count_line, sizeof(count_line),
             "VDEC %llu PRE %llu NPU %llu POST %llu OSD %llu VO %llu F %d",
             (unsigned long long)vdec_count,
             (unsigned long long)pre_count,
             (unsigned long long)npu_count,
             (unsigned long long)post_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count,
             frame);
    if (det && det->object_count > 0) {
        const npu_object_t *obj = &det->objects[0];
        snprintf(det_line, sizeof(det_line), "DET %d TOP %s %.2f BOX %.0f %.0f %.0f %.0f",
                 det->object_count, obj->label, obj->score,
                 obj->x, obj->y, obj->w, obj->h);
    } else {
        snprintf(det_line, sizeof(det_line), "DET 0 TOP NONE");
    }

    if (page_overlay_set_text(NPU_OSD_GRP, 0, 12, 12, 2,
                              "FRUIT_DETECT_NPU DEEPNIR YOLOV8 RKNN",
                              masks[0], sizeof(masks[0]), NPU_TEXT_MASK_W, NPU_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(NPU_OSD_GRP, 1, 12, 560, 1,
                              "FLOW DEEPNIR_H264->VDEC->RGA->YOLOV8_NPU->RGA->OSD->VO",
                              masks[1], sizeof(masks[1]), NPU_TEXT_MASK_W, NPU_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(NPU_OSD_GRP, 2, 12, 576, 1,
                              perf_line,
                              masks[2], sizeof(masks[2]), NPU_TEXT_MASK_W, NPU_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(NPU_OSD_GRP, 3, 12, 592, 1,
                              count_line,
                              masks[3], sizeof(masks[3]), NPU_TEXT_MASK_W, NPU_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(NPU_OSD_GRP, 4, 12, 608, 1,
                              det_line,
                              masks[4], sizeof(masks[4]), NPU_TEXT_MASK_W, NPU_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return update_npu_box_regions(det);
}

static int setup_npu_chain(npu_chain_t *chain) {
    MEDIA_VDEC_ATTR vdec = {0};
    MEDIA_RGA_GRP_ATTR pre = {0};
    MEDIA_NPU_ATTR npu = {0};
    MEDIA_RGA_GRP_ATTR post = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    const char *vdec_out_ports[] = {"output", "output0"};
    const char *rga_in_ports[] = {"input0", "input"};
    const char *rga_out_ports[] = {"output0", "output"};
    const char *npu_in_ports[] = {"input", "input0"};
    const char *npu_out_ports[] = {"output0", "output"};
    const char *osd_in_ports[] = {"input", "input0"};
    const char *osd_out_ports[] = {"output0", "output"};
    const char *vo_in_ports[] = {"input0", "input"};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(NPU_VDEC_POOL, NPU_VDEC_FRAME_SIZE, 6) != 0) return -1;
    chain->vdec_pool_ok = 1;
    if (MEDIA_POOL_Create(NPU_RGB_POOL, NPU_RGB_FRAME_SIZE, 4) != 0) return -1;
    chain->rgb_pool_ok = 1;
    if (MEDIA_POOL_Create(NPU_OUT_POOL, NPU_NV12_FRAME_SIZE, 4) != 0) return -1;
    chain->out_pool_ok = 1;
    if (MEDIA_POOL_Create(NPU_OSD_POOL, NPU_NV12_FRAME_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vdec.width = NPU_W;
    vdec.height = NPU_H;
    vdec.stride = NPU_STRIDE;
    vdec.buf_cnt = 4;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = NPU_VDEC_POOL;
    vdec.has_input_port = 0;
    vdec.input_depth = 0;
    if (MEDIA_VDEC_CreateChn(NPU_VDEC_CHN, &vdec) != 0) return -1;
    chain->vdec_ok = 1;

    pre.algo = MEDIA_RGA_ALG_CSC;
    pre.input_count = 1;
    pre.output_count = 1;
    pre.input_depth = 4;
    pre.output_depth = 4;
    pre.inputs[0].width = NPU_W;
    pre.inputs[0].height = NPU_H;
    pre.inputs[0].stride = NPU_STRIDE;
    pre.inputs[0].format = MEDIA_FORMAT_NV12;
    pre.outputs[0].width = NPU_W;
    pre.outputs[0].height = NPU_H;
    pre.outputs[0].stride = NPU_W * 3;
    pre.outputs[0].format = MEDIA_FORMAT_RGB888;
    pre.outputs[0].pool_id = NPU_RGB_POOL;
    if (MEDIA_RGA_CreateGrp(NPU_PRE_RGA_GRP, &pre) != 0 ||
        MEDIA_RGA_Start(NPU_PRE_RGA_GRP) != 0) {
        return -1;
    }
    chain->pre_rga_ok = 1;

    npu.model_path = NPU_MODEL_PATH;
    npu.label_path = path_readable(NPU_LABEL_PATH) ? NPU_LABEL_PATH : NULL;
    npu.adapter_name = "yolov8";
    npu.backend = MEDIA_NPU_BACKEND_RKNN;
    npu.task = MEDIA_NPU_TASK_DETECT;
    npu.input_width = NPU_W;
    npu.input_height = NPU_H;
    npu.input_format = MEDIA_FORMAT_RGB888;
    npu.input_layout = MEDIA_NPU_LAYOUT_NHWC;
    npu.input_depth = 4;
    npu.passthrough = 1;
    npu.score_thresh = 0.60f;
    npu.nms_thresh = 0.45f;
    if (MEDIA_NPU_CreateGrp(NPU_GRP, &npu) != 0 ||
        MEDIA_NPU_Start(NPU_GRP) != 0) {
        return -1;
    }
    chain->npu_ok = 1;

    post.algo = MEDIA_RGA_ALG_CSC;
    post.input_count = 1;
    post.output_count = 1;
    post.input_depth = 4;
    post.output_depth = 4;
    post.inputs[0].width = NPU_W;
    post.inputs[0].height = NPU_H;
    post.inputs[0].stride = NPU_W * 3;
    post.inputs[0].format = MEDIA_FORMAT_RGB888;
    post.outputs[0].width = NPU_W;
    post.outputs[0].height = NPU_H;
    post.outputs[0].stride = NPU_STRIDE;
    post.outputs[0].format = MEDIA_FORMAT_NV12;
    post.outputs[0].pool_id = NPU_OUT_POOL;
    if (MEDIA_RGA_CreateGrp(NPU_POST_RGA_GRP, &post) != 0 ||
        MEDIA_RGA_Start(NPU_POST_RGA_GRP) != 0) {
        return -1;
    }
    chain->post_rga_ok = 1;

    osd.input_width = NPU_W;
    osd.input_height = NPU_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = NPU_OSD_POOL;
    osd.input_stride = NPU_STRIDE;
    osd.output_stride = NPU_STRIDE;
    osd.max_regions = 24;
    if (MEDIA_OSD_CreateGrp(NPU_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(NPU_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = NPU_SCREEN_W;
    vo.height = NPU_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, NPU_VIEW_X, NPU_VIEW_Y, NPU_W, NPU_H, NPU_STRIDE, 4,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (bind_first_match("VDEC", NPU_VDEC_CHN, vdec_out_ports, 2,
                         "RGA", NPU_PRE_RGA_GRP, rga_in_ports, 2,
                         &chain->vdec_src_port, &chain->pre_in_port) != 0) {
        return -1;
    }
    chain->bind_vdec_pre_ok = 1;
    if (bind_first_match("RGA", NPU_PRE_RGA_GRP, rga_out_ports, 2,
                         "NPU", NPU_GRP, npu_in_ports, 2,
                         &chain->pre_src_port, &chain->npu_in_port) != 0) {
        return -1;
    }
    chain->bind_pre_npu_ok = 1;
    if (bind_first_match("NPU", NPU_GRP, npu_out_ports, 2,
                         "RGA", NPU_POST_RGA_GRP, rga_in_ports, 2,
                         &chain->npu_src_port, &chain->post_in_port) != 0) {
        return -1;
    }
    chain->bind_npu_post_ok = 1;
    if (bind_first_match("RGA", NPU_POST_RGA_GRP, rga_out_ports, 2,
                         "OSD", NPU_OSD_GRP, osd_in_ports, 2,
                         &chain->post_src_port, &chain->osd_in_port) != 0) {
        return -1;
    }
    chain->bind_post_osd_ok = 1;
    if (bind_first_match("OSD", NPU_OSD_GRP, osd_out_ports, 2,
                         "VO", 0, vo_in_ports, 2,
                         &chain->osd_src_port, &chain->vo_in_port) != 0) {
        return -1;
    }
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VDEC_Start(NPU_VDEC_CHN) != 0) {
        return -1;
    }
    chain->vdec_started = 1;
    set_tile_status("VDEC", TILE_LIVE);
    set_tile_status("RGA", TILE_LIVE);
    set_tile_status("FRUIT_DETECT_NPU", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void drain_npu_results(void) {
    for (int i = 0; i < 16; ++i) {
        MEDIA_NPU_RESULT result;
        memset(&result, 0, sizeof(result));
        if (MEDIA_NPU_GetResult(NPU_GRP, &result, 0) != 0) break;
        MEDIA_NPU_ReleaseResult(NPU_GRP, &result);
    }
}

static void drain_npu_frames(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_NPU_GetFrame(NPU_GRP, &out, 0) != 0) break;
            MEDIA_NPU_ReleaseFrame(NPU_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void cleanup_npu_chain(npu_chain_t *chain) {
    if (!chain) return;
    if (chain->vdec_started) {
        MEDIA_VDEC_Stop(NPU_VDEC_CHN);
        chain->vdec_started = 0;
    }
    if (chain->npu_ok) {
        usleep(50000);
        drain_npu_results();
    }
    if (chain->vo_ok) {
        MEDIA_VO_Stop(0, 0);
    }
    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", NPU_OSD_GRP, chain->osd_src_port,
                         "VO", 0, chain->vo_in_port);
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_post_osd_ok) {
        MEDIA_SYS_UnBind("RGA", NPU_POST_RGA_GRP, chain->post_src_port,
                         "OSD", NPU_OSD_GRP, chain->osd_in_port);
        chain->bind_post_osd_ok = 0;
    }
    if (chain->bind_npu_post_ok) {
        MEDIA_SYS_UnBind("NPU", NPU_GRP, chain->npu_src_port,
                         "RGA", NPU_POST_RGA_GRP, chain->post_in_port);
        chain->bind_npu_post_ok = 0;
    }
    if (chain->bind_pre_npu_ok) {
        MEDIA_SYS_UnBind("RGA", NPU_PRE_RGA_GRP, chain->pre_src_port,
                         "NPU", NPU_GRP, chain->npu_in_port);
        chain->bind_pre_npu_ok = 0;
    }
    if (chain->bind_vdec_pre_ok) {
        MEDIA_SYS_UnBind("VDEC", NPU_VDEC_CHN, chain->vdec_src_port,
                         "RGA", NPU_PRE_RGA_GRP, chain->pre_in_port);
        chain->bind_vdec_pre_ok = 0;
    }
    if (chain->npu_ok) {
        drain_npu_results();
        drain_npu_frames();
    }
    if (chain->osd_ok) {
        drain_npu_osd_output();
        MEDIA_OSD_Stop(NPU_OSD_GRP);
        drain_npu_osd_output();
        MEDIA_OSD_DestroyGrp(NPU_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->post_rga_ok) {
        MEDIA_RGA_Stop(NPU_POST_RGA_GRP);
        MEDIA_RGA_DestroyChn(NPU_POST_RGA_GRP);
        chain->post_rga_ok = 0;
    }
    if (chain->npu_ok) {
        MEDIA_NPU_Stop(NPU_GRP);
        drain_npu_results();
        drain_npu_frames();
        MEDIA_NPU_DestroyGrp(NPU_GRP);
        chain->npu_ok = 0;
    }
    if (chain->pre_rga_ok) {
        MEDIA_RGA_Stop(NPU_PRE_RGA_GRP);
        MEDIA_RGA_DestroyChn(NPU_PRE_RGA_GRP);
        chain->pre_rga_ok = 0;
    }
    if (chain->vdec_ok) {
        MEDIA_VDEC_DestroyChn(NPU_VDEC_CHN);
        chain->vdec_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->out_pool_ok) {
        MEDIA_POOL_Destroy(NPU_OUT_POOL);
        chain->out_pool_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(NPU_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->rgb_pool_ok) {
        MEDIA_POOL_Destroy(NPU_RGB_POOL);
        chain->rgb_pool_ok = 0;
    }
    if (chain->vdec_pool_ok) {
        MEDIA_POOL_Destroy(NPU_VDEC_POOL);
        chain->vdec_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

int page_fruit_detect_npu_run(volatile sig_atomic_t *running) {
    h264_stream_t stream;
    npu_chain_t chain = {0};
    npu_result_snapshot_t last_det;
    memset(&stream, 0, sizeof(stream));
    memset(&last_det, 0, sizeof(last_det));

    if (!path_readable(NPU_H264_PATH)) {
        fprintf(stderr, "FRUIT_DETECT_NPU demo H264 missing: %s\n", NPU_H264_PATH);
        return 1;
    }
    if (!path_readable(NPU_MODEL_PATH)) {
        fprintf(stderr, "FRUIT_DETECT_NPU demo model missing: %s\n", NPU_MODEL_PATH);
        return 1;
    }
    if (load_h264_stream(NPU_H264_PATH, &stream) != 0) {
        fprintf(stderr, "FRUIT_DETECT_NPU demo: failed to load H264 stream\n");
        return 1;
    }
    if (setup_npu_chain(&chain) != 0) {
        fprintf(stderr, "FRUIT_DETECT_NPU standalone chain setup failed\n");
        cleanup_npu_chain(&chain);
        unload_h264_stream(&stream);
        return 1;
    }

    MEDIA_NPU_MODEL_INFO info = {0};
    (void)MEDIA_NPU_GetModelInfo(NPU_GRP, &info);
    printf("FRUIT_DETECT_NPU standalone: H264(%s) -> VDEC%d.%s -> RGA%d.%s -> MEDIA_NPU%d.%s -> RGA%d.%s -> OSD%d.%s -> VO0.%s model=%s adapter=%s. Ctrl+C to stop.\n",
           NPU_H264_PATH,
           NPU_VDEC_CHN, chain.vdec_src_port ? chain.vdec_src_port : "output",
           NPU_PRE_RGA_GRP, chain.pre_src_port ? chain.pre_src_port : "output0",
           NPU_GRP, chain.npu_src_port ? chain.npu_src_port : "output0",
           NPU_POST_RGA_GRP, chain.post_src_port ? chain.post_src_port : "output0",
           NPU_OSD_GRP, chain.osd_src_port ? chain.osd_src_port : "output0",
           chain.vo_in_port ? chain.vo_in_port : "input0",
           info.model_name ? info.model_name : NPU_MODEL_PATH,
           info.adapter_name ? info.adapter_name : "yolov8");

    int frame = 0;
    while (!running || *running) {
        int is_vcl = 0;
        if (feed_h264_stream_packet(NPU_VDEC_CHN, &stream, &is_vcl) != 0) {
            fprintf(stderr, "FRUIT_DETECT_NPU demo: MEDIA_VDEC_SendPacket failed\n");
            usleep(1000000 / NPU_FPS);
            continue;
        }

        for (int i = 0; i < 8; ++i) {
            MEDIA_NPU_RESULT result;
            memset(&result, 0, sizeof(result));
            if (MEDIA_NPU_GetResult(NPU_GRP, &result, 0) != 0) break;
            copy_npu_result(&result, &last_det);
            MEDIA_NPU_ReleaseResult(NPU_GRP, &result);
        }

        if (is_vcl) {
            frame++;
            if ((frame % NPU_FPS) != 0) {
                (void)update_npu_box_regions(&last_det);
            }
            if ((frame % NPU_FPS) == 0) {
                uint64_t vdec_count = 0;
                uint64_t pre_count = 0;
                uint64_t npu_count = 0;
                uint64_t post_count = 0;
                uint64_t osd_count = 0;
                uint64_t vo_count = 0;
                (void)MEDIA_SYS_GetModuleFrameCount("VDEC", NPU_VDEC_CHN, &vdec_count);
                (void)MEDIA_SYS_GetModuleFrameCount("RGA", NPU_PRE_RGA_GRP, &pre_count);
                (void)MEDIA_SYS_GetModuleFrameCount("NPU", NPU_GRP, &npu_count);
                (void)MEDIA_SYS_GetModuleFrameCount("RGA", NPU_POST_RGA_GRP, &post_count);
                (void)MEDIA_SYS_GetModuleFrameCount("OSD", NPU_OSD_GRP, &osd_count);
                (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
                (void)update_npu_overlay(vdec_count, pre_count, npu_count, post_count,
                                         osd_count, vo_count, frame, &last_det);
                printf("FRUIT_DETECT_NPU vdec=%llu pre_rga=%llu npu=%llu post_rga=%llu osd=%llu vo=%llu det=%d frame=%d standalone=1",
                       (unsigned long long)vdec_count,
                       (unsigned long long)pre_count,
                       (unsigned long long)npu_count,
                       (unsigned long long)post_count,
                       (unsigned long long)osd_count,
                       (unsigned long long)vo_count,
                       last_det.object_count,
                       frame);
                if (last_det.object_count > 0) {
                    const npu_object_t *obj = &last_det.objects[0];
                    printf(" top0=%s %.2f box=%.0f,%.0f %.0fx%.0f",
                           obj->label, obj->score, obj->x, obj->y, obj->w, obj->h);
                }
                printf("\n");
            }
            usleep(1000000 / NPU_FPS);
        } else {
            usleep(1000);
        }
    }

    cleanup_npu_chain(&chain);
    unload_h264_stream(&stream);
    return 0;
}
