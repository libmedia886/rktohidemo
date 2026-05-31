#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_overlay.h"
#include "page_surface.h"

#include <linux/dma-buf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define TNR_PAGE_W 1080
#define TNR_PAGE_H 1920
#define TNR_PAGE_STRIDE 1088
#define TNR_PAGE_POOL 1
#define TNR_PAGE_FPS 30
#define TNR_H264_PATH "assets/loop/tnr_cl/online_lowlight_noisy_640x640_120.h264"
#define TNR_VDEC_CHN 277
#define TNR_VPSS_GRP 276
#define TNR_GRP 77
#define TNR_VMIX_GRP 278
#define TNR_OSD_GRP 88
#define TNR_VDEC_POOL 2
#define TNR_VPSS_RAW_POOL 6
#define TNR_VPSS_TNR_POOL 7
#define TNR_OUTPUT_POOL 8
#define TNR_VMIX_POOL 9
#define TNR_OSD_POOL 10
#define TNR_INPUT_POOL 11
#define TNR_W 640
#define TNR_H 640
#define TNR_STRIDE 640
#define TNR_FRAME_SIZE (TNR_STRIDE * TNR_H * 3 / 2)
#define TNR_VDEC_SIZE ((size_t)TNR_STRIDE * (size_t)ALIGN_UP_LOCAL(TNR_H, 16) * 2u)
#define TNR_OUT_SIZE ((size_t)TNR_PAGE_STRIDE * (size_t)TNR_PAGE_H * 3u / 2u)
#define TNR_PANE_X ((TNR_PAGE_W - TNR_W) / 2)
#define TNR_TOP_Y 246
#define TNR_BOTTOM_Y 930
#define TNR_TEXT_MASK_W 1000
#define TNR_TEXT_MASK_H 64
#define TNR_ASSET_PATH "assets/loop/tnr_cl/online_lowlight_noisy_640x640_120.nv12"
#define LICENSE_PATH "/root/licence.dat"

#define TNR_THRESHOLD 0.12f
#define TNR_STATIC_ALPHA 0.50f
#define TNR_MOTION_ALPHA 0.96f

typedef struct {
    uint8_t *frames;
    int frame_count;
    uint8_t *output;
    int processed;
    int module_ok;
    MEDIA_TNR_CL_PERF perf;
} tnr_page_ctx_t;

static int map_buffer(MEDIA_BUFFER buf, void **addr, size_t *size, int prot) {
    int fd = -1;
    if (!addr || !size) return -1;
    if (MEDIA_POOL_GetFd(buf, &fd, size) != 0 || fd < 0 || *size == 0) return -1;
    *addr = mmap(NULL, *size, prot, MAP_SHARED, fd, 0);
    if (*addr == MAP_FAILED) {
        *addr = NULL;
        return -1;
    }
    return 0;
}

static int copy_to_buffer(MEDIA_BUFFER buf, const uint8_t *src, size_t need) {
    void *addr = NULL;
    size_t size = 0;
    if (!src) return -1;
    if (map_buffer(buf, &addr, &size, PROT_READ | PROT_WRITE) != 0) return -1;
    if (size < need) {
        munmap(addr, size);
        return -1;
    }
    (void)MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    memcpy(addr, src, need);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    munmap(addr, size);
    return 0;
}

static int copy_from_buffer(MEDIA_BUFFER buf, uint8_t *dst, size_t need) {
    void *addr = NULL;
    size_t size = 0;
    if (!dst) return -1;
    if (map_buffer(buf, &addr, &size, PROT_READ) != 0) return -1;
    if (size < need) {
        munmap(addr, size);
        return -1;
    }
    (void)MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_READ);
    memcpy(dst, addr, need);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
    munmap(addr, size);
    return 0;
}

static void fill_synthetic_frame(uint8_t *dst, int frame) {
    if (!dst) return;
    uint32_t seed = 0x12345678u + (uint32_t)frame * 747796405u;
    for (int y = 0; y < TNR_H; ++y) {
        uint8_t *row = dst + (size_t)y * TNR_STRIDE;
        for (int x = 0; x < TNR_W; ++x) {
            seed = seed * 1664525u + 1013904223u;
            int base = 48 + (x * 110) / TNR_W + (y * 48) / TNR_H;
            int noise = (int)((seed >> 24) & 0x3f) - 32;
            row[x] = (uint8_t)(base + noise < 16 ? 16 : (base + noise > 235 ? 235 : base + noise));
        }
    }
    uint8_t *uv = dst + (size_t)TNR_STRIDE * TNR_H;
    for (int y = 0; y < TNR_H / 2; ++y) {
        memset(uv + (size_t)y * TNR_STRIDE, 128, TNR_STRIDE);
    }
}

static int load_tnr_asset(tnr_page_ctx_t *ctx) {
    struct stat st;
    if (!ctx) return -1;
    if (stat(TNR_ASSET_PATH, &st) != 0 || st.st_size <= 0 ||
        ((size_t)st.st_size % TNR_FRAME_SIZE) != 0) {
        ctx->frames = malloc(TNR_FRAME_SIZE);
        if (!ctx->frames) return -1;
        fill_synthetic_frame(ctx->frames, 0);
        ctx->frame_count = 1;
        return 0;
    }

    ctx->frames = malloc((size_t)st.st_size);
    if (!ctx->frames) return -1;
    FILE *fp = fopen(TNR_ASSET_PATH, "rb");
    if (!fp) {
        free(ctx->frames);
        ctx->frames = NULL;
        return -1;
    }
    size_t got = fread(ctx->frames, 1, (size_t)st.st_size, fp);
    fclose(fp);
    if (got != (size_t)st.st_size) {
        free(ctx->frames);
        ctx->frames = NULL;
        return -1;
    }
    ctx->frame_count = (int)((size_t)st.st_size / TNR_FRAME_SIZE);
    return ctx->frame_count > 0 ? 0 : -1;
}

static int setup_tnr_module(void) {
    if (MEDIA_POOL_Create(TNR_INPUT_POOL, TNR_FRAME_SIZE, 4) != 0) return -1;
    if (MEDIA_POOL_Create(TNR_OUTPUT_POOL, TNR_FRAME_SIZE, 4) != 0) {
        MEDIA_POOL_Destroy(TNR_INPUT_POOL);
        return -1;
    }

    MEDIA_TNR_CL_ATTR attr = {0};
    attr.width = TNR_W;
    attr.height = TNR_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 4;
    attr.output_pool_id = TNR_OUTPUT_POOL;
    attr.input_stride = TNR_STRIDE;
    attr.output_stride = TNR_STRIDE;
    attr.block_size = 16;
    attr.threshold = TNR_THRESHOLD;
    attr.static_alpha = TNR_STATIC_ALPHA;
    attr.motion_alpha = TNR_MOTION_ALPHA;

    if (MEDIA_TNR_CL_CreateGrp(TNR_GRP, &attr) != 0 ||
        MEDIA_TNR_CL_Start(TNR_GRP) != 0) {
        MEDIA_TNR_CL_DestroyGrp(TNR_GRP);
        MEDIA_POOL_Destroy(TNR_OUTPUT_POOL);
        MEDIA_POOL_Destroy(TNR_INPUT_POOL);
        return -1;
    }
    return 0;
}

static void cleanup_tnr_module(int enabled) {
    if (!enabled) return;
    MEDIA_TNR_CL_Stop(TNR_GRP);
    MEDIA_TNR_CL_DestroyGrp(TNR_GRP);
    MEDIA_POOL_Destroy(TNR_OUTPUT_POOL);
    MEDIA_POOL_Destroy(TNR_INPUT_POOL);
}

static int process_tnr(tnr_page_ctx_t *ctx, const uint8_t *input) {
    if (!ctx || !input || !ctx->output || !ctx->module_ok) return -1;
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    if (MEDIA_POOL_GetBuffer(TNR_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, input, TNR_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("TNR_CL", TNR_GRP, "input", in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);

    if (MEDIA_TNR_CL_GetFrame(TNR_GRP, &out, 1000) != 0) return -1;
    int ret = copy_from_buffer(out, ctx->output, TNR_FRAME_SIZE);
    MEDIA_TNR_CL_ReleaseFrame(TNR_GRP, out);
    if (ret == 0) {
        ctx->processed++;
        (void)MEDIA_TNR_CL_GetLastPerf(TNR_GRP, &ctx->perf);
    }
    return ret;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * TNR_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * TNR_STRIDE;
        for (int x = 0; x < dw; ++x) {
            int sx = x * TNR_W / dw;
            drow[x] = srow[sx];
        }
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)TNR_STRIDE * TNR_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (TNR_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * TNR_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = ((x * TNR_W / dw) & ~1);
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_tnr_page(uint8_t *dst, int stride, int width, int height,
                          int frame, void *opaque) {
    tnr_page_ctx_t *ctx = (tnr_page_ctx_t *)opaque;
    int sample = ctx && ctx->frame_count > 0 ? frame % ctx->frame_count : 0;
    const uint8_t *input = ctx && ctx->frames ?
        ctx->frames + (size_t)sample * TNR_FRAME_SIZE : NULL;
    char sample_text[64];
    char processed[48];
    char perf[72];

    if (input && ctx && ctx->module_ok) {
        if (process_tnr(ctx, input) != 0) memcpy(ctx->output, input, TNR_FRAME_SIZE);
    } else if (input && ctx && ctx->output) {
        memcpy(ctx->output, input, TNR_FRAME_SIZE);
    }

    snprintf(sample_text, sizeof(sample_text), "SAMPLE %03d/%03d",
             sample + 1, ctx ? ctx->frame_count : 0);
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);
    if (ctx) {
        snprintf(perf, sizeof(perf), "CL M %03d US B %03d US Q %03d US",
                 (int)(ctx->perf.gpu_motion_ms * 1000.0),
                 (int)(ctx->perf.gpu_blend_ms * 1000.0),
                 (int)(ctx->perf.gpu_queue_total_ms * 1000.0));
    } else {
        snprintf(perf, sizeof(perf), "PERF NA");
    }

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "TNR CL", 9, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 60, 142,
                           sample_text, 3, 210, 144, 84);

    int pane = 600;
    int x = (width - pane) / 2;
    int top_y = 246;
    int bottom_y = 930;
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, top_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, bottom_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, top_y - 34,
                           "NOISY INPUT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, x, bottom_y - 34,
                           "TEMPORAL OUTPUT", 3, 220, 108, 176);
    if (input) draw_nv12_scaled(dst, stride, width, height, x, top_y, pane, pane, input);
    if (ctx && ctx->output) {
        draw_nv12_scaled(dst, stride, width, height, x, bottom_y, pane, pane, ctx->output);
    }

    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           "THRESHOLD 07 STATIC 82 MOTION 100 BLOCK 16",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1725,
                           perf, 2, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1810,
                           "STATIC AREAS ACCUMULATE MOTION AREAS RESET",
                           2, 190, 144, 84);
}

typedef struct {
    uint8_t *data;
    size_t size;
    int *offsets;
    int count;
    int index;
    uint64_t pts;
} tnr_stream_t;

typedef struct {
    int sys_ok;
    int vdec_pool_ok;
    int vpss_raw_pool_ok;
    int vpss_tnr_pool_ok;
    int tnr_pool_ok;
    int vmix_pool_ok;
    int osd_pool_ok;
    int vdec_ok;
    int vpss_ok;
    int tnr_ok;
    int vmix_ok;
    int osd_ok;
    int vo_ok;
    int bind_vdec_vpss_ok;
    int bind_vpss_raw_vmix_ok;
    int bind_vpss_tnr_ok;
    int bind_tnr_vmix_ok;
    int bind_vmix_osd_ok;
    int bind_osd_vo_ok;
    int vdec_started;
} tnr_chain_t;

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

static void free_stream(tnr_stream_t *stream) {
    if (!stream) return;
    free(stream->data);
    free(stream->offsets);
    memset(stream, 0, sizeof(*stream));
}

static int read_stream(const char *path, tnr_stream_t *stream) {
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

static int stream_nal_is_vcl(const tnr_stream_t *stream, int idx) {
    if (!stream || idx < 0 || idx >= stream->count) return 0;
    int start = stream->offsets[idx];
    int end = (idx + 1 < stream->count) ? stream->offsets[idx + 1] : (int)stream->size;
    int sc = start_code_len(stream->data, stream->size, (size_t)start);
    int nal_start = start + sc;
    if (nal_start >= end) return 0;
    int nal = stream->data[nal_start] & 0x1f;
    return nal == 1 || nal == 5;
}

static int feed_stream_packet(tnr_stream_t *stream, int *is_vcl) {
    if (!stream || !stream->data || stream->count <= 0) return -1;
    if (is_vcl) *is_vcl = 0;
    if (stream->index >= stream->count) stream->index = 0;

    int idx = stream->index++;
    int start = stream->offsets[idx];
    int end = (idx + 1 < stream->count) ? stream->offsets[idx + 1] : (int)stream->size;
    int len = end - start;
    if (start < 0 || len <= 0 || (size_t)end > stream->size) return -1;

    if (MEDIA_VDEC_SendPacket(TNR_VDEC_CHN, stream->data + start,
                              (size_t)len, stream->pts) != 0) {
        return -1;
    }
    stream->pts += (uint64_t)(1000 / TNR_PAGE_FPS);
    if (is_vcl) *is_vcl = stream_nal_is_vcl(stream, idx);
    return 0;
}

static int feed_stream_frame(tnr_stream_t *stream) {
    if (!stream || !stream->data || stream->count <= 0) return -1;
    if (stream->index >= stream->count) stream->index = 0;

    int start_idx = stream->index;
    int first_vcl = -1;
    for (int i = start_idx; i < stream->count; ++i) {
        if (stream_nal_is_vcl(stream, i)) {
            first_vcl = i;
            break;
        }
    }
    if (first_vcl < 0) {
        stream->index = 0;
        start_idx = 0;
        for (int i = 0; i < stream->count; ++i) {
            if (stream_nal_is_vcl(stream, i)) {
                first_vcl = i;
                break;
            }
        }
    }
    if (first_vcl < 0) return -1;

    int end_idx = first_vcl + 1;
    while (end_idx < stream->count && !stream_nal_is_vcl(stream, end_idx)) {
        end_idx++;
    }

    int start = stream->offsets[start_idx];
    int end = (end_idx < stream->count) ? stream->offsets[end_idx] : (int)stream->size;
    int len = end - start;
    if (start < 0 || len <= 0 || (size_t)end > stream->size) return -1;

    if (MEDIA_VDEC_SendPacket(TNR_VDEC_CHN, stream->data + start,
                              (size_t)len, stream->pts) != 0) {
        return -1;
    }
    stream->pts += (uint64_t)(1000 / TNR_PAGE_FPS);
    stream->index = (end_idx < stream->count) ? end_idx : 0;
    return 0;
}

static void drain_tnr_vpss_output(int chn) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VPSS_Chn_GetFrame(TNR_VPSS_GRP, chn, &out, 0) != 0) break;
            MEDIA_VPSS_Chn_ReleaseFrame(TNR_VPSS_GRP, chn, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_tnr_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_TNR_CL_GetFrame(TNR_GRP, &out, 0) != 0) break;
            MEDIA_TNR_CL_ReleaseFrame(TNR_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_tnr_vmix_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VMIX_GetFrame(TNR_VMIX_GRP, &out, 0) != 0) break;
            MEDIA_VMIX_ReleaseFrame(TNR_VMIX_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_tnr_osd_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_OSD_GetFrame(TNR_OSD_GRP, &out, 0) != 0) break;
            MEDIA_OSD_ReleaseFrame(TNR_OSD_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static int update_tnr_overlay(uint64_t vdec_count, uint64_t vpss_count,
                              uint64_t tnr_count, uint64_t osd_count,
                              uint64_t vo_count, int sent_frames,
                              const MEDIA_TNR_CL_PERF *perf) {
    static uint8_t masks[7][TNR_TEXT_MASK_W * TNR_TEXT_MASK_H];
    page_overlay_perf_t sys_perf = {0};
    char perf_line[128];
    char param_line[160];
    char count_line[192];

    page_overlay_update_perf(&sys_perf);
    page_overlay_format_perf(&sys_perf, perf_line, sizeof(perf_line));
    snprintf(param_line, sizeof(param_line),
             "H264 640x640 FPS %d TH %.2f STATIC %.2f MOTION %.2f CL %.3f/%.3f/%.3f ms",
             TNR_PAGE_FPS, TNR_THRESHOLD, TNR_STATIC_ALPHA, TNR_MOTION_ALPHA,
             perf ? perf->gpu_motion_ms : 0.0,
             perf ? perf->gpu_blend_ms : 0.0,
             perf ? perf->gpu_queue_total_ms : 0.0);
    snprintf(count_line, sizeof(count_line),
             "VDEC %llu VPSS %llu TNR %llu OSD %llu VO %llu SENT %d",
             (unsigned long long)vdec_count,
             (unsigned long long)vpss_count,
             (unsigned long long)tnr_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count,
             sent_frames);

    if (page_overlay_set_text(TNR_OSD_GRP, 0, 60, 58, 3,
                              "TNR CL H264 REALTIME DEMO",
                              masks[0], sizeof(masks[0]), TNR_TEXT_MASK_W, TNR_TEXT_MASK_H,
                              235, 108, 176, 255) != 0) return -1;
    if (page_overlay_set_text(TNR_OSD_GRP, 1, TNR_PANE_X, TNR_TOP_Y - 34, 2,
                              "NOISY INPUT",
                              masks[1], sizeof(masks[1]), TNR_TEXT_MASK_W, TNR_TEXT_MASK_H,
                              220, 108, 176, 255) != 0) return -1;
    if (page_overlay_set_text(TNR_OSD_GRP, 2, TNR_PANE_X, TNR_BOTTOM_Y - 34, 2,
                              "TEMPORAL OUTPUT",
                              masks[2], sizeof(masks[2]), TNR_TEXT_MASK_W, TNR_TEXT_MASK_H,
                              220, 108, 176, 255) != 0) return -1;
    if (page_overlay_set_text(TNR_OSD_GRP, 3, 70, 1595, 2,
                              "FLOW H264->VDEC->VPSS RAW/TNR->VMIX->OSD->VO",
                              masks[3], sizeof(masks[3]), TNR_TEXT_MASK_W, TNR_TEXT_MASK_H,
                              190, 144, 84, 255) != 0) return -1;
    if (page_overlay_set_text(TNR_OSD_GRP, 4, 70, 1660, 2,
                              param_line,
                              masks[4], sizeof(masks[4]), TNR_TEXT_MASK_W, TNR_TEXT_MASK_H,
                              210, 108, 176, 255) != 0) return -1;
    if (page_overlay_set_text(TNR_OSD_GRP, 5, 70, 1725, 2,
                              count_line,
                              masks[5], sizeof(masks[5]), TNR_TEXT_MASK_W, TNR_TEXT_MASK_H,
                              210, 108, 176, 255) != 0) return -1;
    if (page_overlay_set_text(TNR_OSD_GRP, 6, 70, 1810, 2,
                              perf_line,
                              masks[6], sizeof(masks[6]), TNR_TEXT_MASK_W, TNR_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    return 0;
}

static int setup_tnr_h264_chain(tnr_chain_t *chain) {
    MEDIA_VDEC_ATTR vdec = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_TNR_CL_ATTR tnr = {0};
    MEDIA_VMIX_ATTR vmix = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    if (!chain) return -1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(TNR_VDEC_POOL, TNR_VDEC_SIZE, 8) != 0) return -1;
    chain->vdec_pool_ok = 1;
    if (MEDIA_POOL_Create(TNR_VPSS_RAW_POOL, TNR_FRAME_SIZE, 8) != 0) return -1;
    chain->vpss_raw_pool_ok = 1;
    if (MEDIA_POOL_Create(TNR_VPSS_TNR_POOL, TNR_FRAME_SIZE, 8) != 0) return -1;
    chain->vpss_tnr_pool_ok = 1;
    if (MEDIA_POOL_Create(TNR_OUTPUT_POOL, TNR_FRAME_SIZE, 8) != 0) return -1;
    chain->tnr_pool_ok = 1;
    if (MEDIA_POOL_Create(TNR_VMIX_POOL, TNR_OUT_SIZE, 4) != 0) return -1;
    chain->vmix_pool_ok = 1;
    if (MEDIA_POOL_Create(TNR_OSD_POOL, TNR_OUT_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vdec.width = TNR_W;
    vdec.height = TNR_H;
    vdec.stride = TNR_STRIDE;
    vdec.buf_cnt = 8;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = TNR_VDEC_POOL;
    if (MEDIA_VDEC_CreateChn(TNR_VDEC_CHN, &vdec) != 0) return -1;
    chain->vdec_ok = 1;

    vpss.width = TNR_W;
    vpss.height = TNR_H;
    vpss.input_stride = TNR_STRIDE;
    vpss.input_depth = 8;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = TNR_PAGE_FPS;
    vpss.out_fps = TNR_PAGE_FPS;
    vpss.output_count = 2;
    vpss.outputs[0].output_id = 0;
    vpss.outputs[0].out_width = TNR_W;
    vpss.outputs[0].out_height = TNR_H;
    vpss.outputs[0].out_stride = TNR_STRIDE;
    vpss.outputs[0].pool_id = TNR_VPSS_RAW_POOL;
    vpss.outputs[0].crop_w = TNR_W;
    vpss.outputs[0].crop_h = TNR_H;
    vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    vpss.outputs[1].output_id = 1;
    vpss.outputs[1].out_width = TNR_W;
    vpss.outputs[1].out_height = TNR_H;
    vpss.outputs[1].out_stride = TNR_STRIDE;
    vpss.outputs[1].pool_id = TNR_VPSS_TNR_POOL;
    vpss.outputs[1].crop_w = TNR_W;
    vpss.outputs[1].crop_h = TNR_H;
    vpss.outputs[1].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(TNR_VPSS_GRP, &vpss) != 0) return -1;
    chain->vpss_ok = 1;

    tnr.width = TNR_W;
    tnr.height = TNR_H;
    tnr.format = MEDIA_FORMAT_NV12;
    tnr.input_depth = 8;
    tnr.output_pool_id = TNR_OUTPUT_POOL;
    tnr.input_stride = TNR_STRIDE;
    tnr.output_stride = TNR_STRIDE;
    tnr.block_size = 16;
    tnr.threshold = TNR_THRESHOLD;
    tnr.static_alpha = TNR_STATIC_ALPHA;
    tnr.motion_alpha = TNR_MOTION_ALPHA;
    if (MEDIA_TNR_CL_CreateGrp(TNR_GRP, &tnr) != 0) return -1;
    chain->tnr_ok = 1;

    vmix.input_count = 2;
    vmix.output_width = TNR_PAGE_W;
    vmix.output_height = TNR_PAGE_H;
    vmix.output_stride = TNR_PAGE_STRIDE;
    vmix.format = MEDIA_FORMAT_NV12;
    vmix.input_depth = 4;
    vmix.output_pool_id = TNR_VMIX_POOL;
    vmix.primary_index = -1;
    vmix.channels[0].enabled = 1;
    vmix.channels[0].x = TNR_PANE_X;
    vmix.channels[0].y = TNR_TOP_Y;
    vmix.channels[0].width = TNR_W;
    vmix.channels[0].height = TNR_H;
    vmix.channels[0].alpha = 1.0f;
    vmix.channels[0].stride = TNR_STRIDE;
    vmix.channels[0].format = MEDIA_FORMAT_NV12;
    vmix.channels[1].enabled = 1;
    vmix.channels[1].x = TNR_PANE_X;
    vmix.channels[1].y = TNR_BOTTOM_Y;
    vmix.channels[1].width = TNR_W;
    vmix.channels[1].height = TNR_H;
    vmix.channels[1].alpha = 1.0f;
    vmix.channels[1].stride = TNR_STRIDE;
    vmix.channels[1].format = MEDIA_FORMAT_NV12;
    if (MEDIA_VMIX_CreateGrp(TNR_VMIX_GRP, &vmix) != 0 ||
        MEDIA_VMIX_Start(TNR_VMIX_GRP) != 0 ||
        MEDIA_VMIX_Enable(TNR_VMIX_GRP) != 0) {
        return -1;
    }
    chain->vmix_ok = 1;

    osd.input_width = TNR_PAGE_W;
    osd.input_height = TNR_PAGE_H;
    osd.format = MEDIA_FORMAT_NV12;
    osd.input_depth = 4;
    osd.output_pool_id = TNR_OSD_POOL;
    osd.input_stride = TNR_PAGE_STRIDE;
    osd.output_stride = TNR_PAGE_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(TNR_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(TNR_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = TNR_PAGE_W;
    vo.height = TNR_PAGE_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, 0, 0, TNR_PAGE_W, TNR_PAGE_H, TNR_PAGE_STRIDE, 8,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VDEC", TNR_VDEC_CHN, "output", "VPSS", TNR_VPSS_GRP, "input") != 0) return -1;
    chain->bind_vdec_vpss_ok = 1;
    if (MEDIA_SYS_Bind("VPSS", TNR_VPSS_GRP, "output0", "VMIX", TNR_VMIX_GRP, "input0") != 0) return -1;
    chain->bind_vpss_raw_vmix_ok = 1;
    if (MEDIA_SYS_Bind("VPSS", TNR_VPSS_GRP, "output1", "TNR_CL", TNR_GRP, "input") != 0) return -1;
    chain->bind_vpss_tnr_ok = 1;
    if (MEDIA_SYS_Bind("TNR_CL", TNR_GRP, "output", "VMIX", TNR_VMIX_GRP, "input1") != 0) return -1;
    chain->bind_tnr_vmix_ok = 1;
    if (MEDIA_SYS_Bind("VMIX", TNR_VMIX_GRP, "output0", "OSD", TNR_OSD_GRP, "input") != 0) return -1;
    chain->bind_vmix_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", TNR_OSD_GRP, "output0", "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_TNR_CL_Start(TNR_GRP) != 0 ||
        MEDIA_VPSS_Enable(TNR_VPSS_GRP) != 0 ||
        MEDIA_VDEC_Start(TNR_VDEC_CHN) != 0) {
        return -1;
    }
    chain->vdec_started = 1;
    set_tile_status("VDEC", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("TNR_CL", TILE_LIVE);
    set_tile_status("VMIX", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_tnr_h264_chain(tnr_chain_t *chain) {
    if (!chain) return;
    if (chain->vdec_started) {
        MEDIA_VDEC_Stop(TNR_VDEC_CHN);
        chain->vdec_started = 0;
        usleep(50000);
    }
    if (chain->vpss_ok) MEDIA_VPSS_Disable(TNR_VPSS_GRP);
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", TNR_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_vmix_osd_ok) {
        MEDIA_SYS_UnBind("VMIX", TNR_VMIX_GRP, "output0", "OSD", TNR_OSD_GRP, "input");
        chain->bind_vmix_osd_ok = 0;
    }
    if (chain->bind_tnr_vmix_ok) {
        MEDIA_SYS_UnBind("TNR_CL", TNR_GRP, "output", "VMIX", TNR_VMIX_GRP, "input1");
        chain->bind_tnr_vmix_ok = 0;
    }
    if (chain->tnr_ok) {
        drain_tnr_output();
        MEDIA_TNR_CL_Stop(TNR_GRP);
        drain_tnr_output();
    }
    if (chain->bind_vpss_tnr_ok) {
        MEDIA_SYS_UnBind("VPSS", TNR_VPSS_GRP, "output1", "TNR_CL", TNR_GRP, "input");
        chain->bind_vpss_tnr_ok = 0;
    }
    if (chain->bind_vpss_raw_vmix_ok) {
        MEDIA_SYS_UnBind("VPSS", TNR_VPSS_GRP, "output0", "VMIX", TNR_VMIX_GRP, "input0");
        chain->bind_vpss_raw_vmix_ok = 0;
    }
    if (chain->bind_vdec_vpss_ok) {
        MEDIA_SYS_UnBind("VDEC", TNR_VDEC_CHN, "output", "VPSS", TNR_VPSS_GRP, "input");
        chain->bind_vdec_vpss_ok = 0;
    }

    if (chain->vpss_ok) {
        drain_tnr_vpss_output(0);
        drain_tnr_vpss_output(1);
    }
    if (chain->vmix_ok) {
        drain_tnr_vmix_output();
        MEDIA_VMIX_Disable(TNR_VMIX_GRP);
        MEDIA_VMIX_Stop(TNR_VMIX_GRP);
        drain_tnr_vmix_output();
        MEDIA_VMIX_DestroyGrp(TNR_VMIX_GRP);
        chain->vmix_ok = 0;
    }
    if (chain->osd_ok) {
        drain_tnr_osd_output();
        MEDIA_OSD_Stop(TNR_OSD_GRP);
        drain_tnr_osd_output();
        MEDIA_OSD_DestroyGrp(TNR_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->tnr_ok) {
        MEDIA_TNR_CL_DestroyGrp(TNR_GRP);
        chain->tnr_ok = 0;
    }
    if (chain->vpss_ok) {
        MEDIA_VPSS_DestroyGrp(TNR_VPSS_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->vdec_ok) {
        MEDIA_VDEC_DestroyChn(TNR_VDEC_CHN);
        chain->vdec_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(TNR_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->vmix_pool_ok) {
        MEDIA_POOL_Destroy(TNR_VMIX_POOL);
        chain->vmix_pool_ok = 0;
    }
    if (chain->tnr_pool_ok) {
        MEDIA_POOL_Destroy(TNR_OUTPUT_POOL);
        chain->tnr_pool_ok = 0;
    }
    if (chain->vpss_tnr_pool_ok) {
        MEDIA_POOL_Destroy(TNR_VPSS_TNR_POOL);
        chain->vpss_tnr_pool_ok = 0;
    }
    if (chain->vpss_raw_pool_ok) {
        MEDIA_POOL_Destroy(TNR_VPSS_RAW_POOL);
        chain->vpss_raw_pool_ok = 0;
    }
    if (chain->vdec_pool_ok) {
        MEDIA_POOL_Destroy(TNR_VDEC_POOL);
        chain->vdec_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

typedef struct {
    uint8_t *input;
    uint8_t *output;
    int input_ready;
    int output_ready;
    int processed;
    MEDIA_TNR_CL_PERF perf;
} tnr_live_ctx_t;

static int setup_tnr_decode_chain(tnr_chain_t *chain) {
    MEDIA_VDEC_ATTR vdec = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_TNR_CL_ATTR tnr = {0};
    if (!chain) return -1;

    if (MEDIA_POOL_Create(TNR_VDEC_POOL, TNR_VDEC_SIZE, 8) != 0) return -1;
    chain->vdec_pool_ok = 1;
    if (MEDIA_POOL_Create(TNR_VPSS_RAW_POOL, TNR_FRAME_SIZE, 8) != 0) return -1;
    chain->vpss_raw_pool_ok = 1;
    if (MEDIA_POOL_Create(TNR_VPSS_TNR_POOL, TNR_FRAME_SIZE, 8) != 0) return -1;
    chain->vpss_tnr_pool_ok = 1;
    if (MEDIA_POOL_Create(TNR_OUTPUT_POOL, TNR_FRAME_SIZE, 8) != 0) return -1;
    chain->tnr_pool_ok = 1;

    vdec.width = TNR_W;
    vdec.height = TNR_H;
    vdec.stride = TNR_STRIDE;
    vdec.buf_cnt = 8;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = TNR_VDEC_POOL;
    if (MEDIA_VDEC_CreateChn(TNR_VDEC_CHN, &vdec) != 0) return -1;
    chain->vdec_ok = 1;

    vpss.width = TNR_W;
    vpss.height = TNR_H;
    vpss.input_stride = TNR_STRIDE;
    vpss.input_depth = 8;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = TNR_PAGE_FPS;
    vpss.out_fps = TNR_PAGE_FPS;
    vpss.output_count = 2;
    vpss.outputs[0].output_id = 0;
    vpss.outputs[0].out_width = TNR_W;
    vpss.outputs[0].out_height = TNR_H;
    vpss.outputs[0].out_stride = TNR_STRIDE;
    vpss.outputs[0].pool_id = TNR_VPSS_RAW_POOL;
    vpss.outputs[0].crop_w = TNR_W;
    vpss.outputs[0].crop_h = TNR_H;
    vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    vpss.outputs[1].output_id = 1;
    vpss.outputs[1].out_width = TNR_W;
    vpss.outputs[1].out_height = TNR_H;
    vpss.outputs[1].out_stride = TNR_STRIDE;
    vpss.outputs[1].pool_id = TNR_VPSS_TNR_POOL;
    vpss.outputs[1].crop_w = TNR_W;
    vpss.outputs[1].crop_h = TNR_H;
    vpss.outputs[1].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(TNR_VPSS_GRP, &vpss) != 0) return -1;
    chain->vpss_ok = 1;

    tnr.width = TNR_W;
    tnr.height = TNR_H;
    tnr.format = MEDIA_FORMAT_NV12;
    tnr.input_depth = 8;
    tnr.output_pool_id = TNR_OUTPUT_POOL;
    tnr.input_stride = TNR_STRIDE;
    tnr.output_stride = TNR_STRIDE;
    tnr.block_size = 16;
    tnr.threshold = TNR_THRESHOLD;
    tnr.static_alpha = TNR_STATIC_ALPHA;
    tnr.motion_alpha = TNR_MOTION_ALPHA;
    if (MEDIA_TNR_CL_CreateGrp(TNR_GRP, &tnr) != 0) return -1;
    chain->tnr_ok = 1;

    if (MEDIA_SYS_Bind("VDEC", TNR_VDEC_CHN, "output", "VPSS", TNR_VPSS_GRP, "input") != 0) return -1;
    chain->bind_vdec_vpss_ok = 1;
    if (MEDIA_SYS_Bind("VPSS", TNR_VPSS_GRP, "output1", "TNR_CL", TNR_GRP, "input") != 0) return -1;
    chain->bind_vpss_tnr_ok = 1;

    if (MEDIA_TNR_CL_Start(TNR_GRP) != 0 ||
        MEDIA_VPSS_Enable(TNR_VPSS_GRP) != 0 ||
        MEDIA_VDEC_Start(TNR_VDEC_CHN) != 0) {
        return -1;
    }
    chain->vdec_started = 1;
    set_tile_status("VDEC", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("TNR_CL", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_tnr_decode_chain(tnr_chain_t *chain) {
    if (!chain) return;
    if (chain->vdec_started) {
        MEDIA_VDEC_Stop(TNR_VDEC_CHN);
        chain->vdec_started = 0;
        usleep(50000);
    }
    if (chain->vpss_ok) MEDIA_VPSS_Disable(TNR_VPSS_GRP);
    if (chain->tnr_ok) {
        drain_tnr_output();
        MEDIA_TNR_CL_Stop(TNR_GRP);
        drain_tnr_output();
    }
    if (chain->bind_vpss_tnr_ok) {
        MEDIA_SYS_UnBind("VPSS", TNR_VPSS_GRP, "output1", "TNR_CL", TNR_GRP, "input");
        chain->bind_vpss_tnr_ok = 0;
    }
    if (chain->bind_vdec_vpss_ok) {
        MEDIA_SYS_UnBind("VDEC", TNR_VDEC_CHN, "output", "VPSS", TNR_VPSS_GRP, "input");
        chain->bind_vdec_vpss_ok = 0;
    }
    if (chain->vpss_ok) {
        drain_tnr_vpss_output(0);
        drain_tnr_vpss_output(1);
        MEDIA_VPSS_DestroyGrp(TNR_VPSS_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->tnr_ok) {
        MEDIA_TNR_CL_DestroyGrp(TNR_GRP);
        chain->tnr_ok = 0;
    }
    if (chain->vdec_ok) {
        MEDIA_VDEC_DestroyChn(TNR_VDEC_CHN);
        chain->vdec_ok = 0;
    }
    if (chain->tnr_pool_ok) {
        MEDIA_POOL_Destroy(TNR_OUTPUT_POOL);
        chain->tnr_pool_ok = 0;
    }
    if (chain->vpss_tnr_pool_ok) {
        MEDIA_POOL_Destroy(TNR_VPSS_TNR_POOL);
        chain->vpss_tnr_pool_ok = 0;
    }
    if (chain->vpss_raw_pool_ok) {
        MEDIA_POOL_Destroy(TNR_VPSS_RAW_POOL);
        chain->vpss_raw_pool_ok = 0;
    }
    if (chain->vdec_pool_ok) {
        MEDIA_POOL_Destroy(TNR_VDEC_POOL);
        chain->vdec_pool_ok = 0;
    }
}

static void poll_tnr_live_frames(tnr_live_ctx_t *ctx) {
    if (!ctx) return;
    for (;;) {
        MEDIA_BUFFER raw = {-1, -1};
        if (MEDIA_VPSS_Chn_GetFrame(TNR_VPSS_GRP, 0, &raw, 0) != 0) break;
        if (copy_from_buffer(raw, ctx->input, TNR_FRAME_SIZE) == 0) ctx->input_ready = 1;
        MEDIA_VPSS_Chn_ReleaseFrame(TNR_VPSS_GRP, 0, raw);
    }
    for (;;) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_TNR_CL_GetFrame(TNR_GRP, &out, 0) != 0) break;
        if (copy_from_buffer(out, ctx->output, TNR_FRAME_SIZE) == 0) {
            ctx->output_ready = 1;
            ctx->processed++;
            (void)MEDIA_TNR_CL_GetLastPerf(TNR_GRP, &ctx->perf);
        }
        MEDIA_TNR_CL_ReleaseFrame(TNR_GRP, out);
    }
}

static void draw_tnr_live_page(uint8_t *dst, int stride, int width, int height,
                               int frame, void *opaque) {
    tnr_live_ctx_t *ctx = (tnr_live_ctx_t *)opaque;
    char processed[48];
    char perf[96];
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);
    snprintf(perf, sizeof(perf), "CL M %03d US B %03d US Q %03d US",
             ctx ? (int)(ctx->perf.gpu_motion_ms * 1000.0) : 0,
             ctx ? (int)(ctx->perf.gpu_blend_ms * 1000.0) : 0,
             ctx ? (int)(ctx->perf.gpu_queue_total_ms * 1000.0) : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "TNR CL H264", 9, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 60, 142,
                           "ZONE NOISE RESOLUTION CHART", 3, 210, 144, 84);

    page_surface_fill_rect_nv12(dst, stride, width, height, TNR_PANE_X - 14, TNR_TOP_Y - 48,
                                TNR_W + 28, TNR_H + 82, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, TNR_PANE_X - 14, TNR_BOTTOM_Y - 48,
                                TNR_W + 28, TNR_H + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, TNR_PANE_X, TNR_TOP_Y - 34,
                           "NOISY INPUT ZONES", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, TNR_PANE_X, TNR_BOTTOM_Y - 34,
                           "TNR OUTPUT DETAIL CHECK", 3, 220, 108, 176);
    if (ctx && ctx->input_ready) draw_nv12_scaled(dst, stride, width, height, TNR_PANE_X, TNR_TOP_Y, TNR_W, TNR_H, ctx->input);
    if (ctx && ctx->output_ready) draw_nv12_scaled(dst, stride, width, height, TNR_PANE_X, TNR_BOTTOM_Y, TNR_W, TNR_H, ctx->output);

    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           "FLOW H264 CHART->VDEC->VPSS RAW/TNR->VO PAGE",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           "NOISE SIGMA 4/12/24/40  TH 12 STATIC 50 MOTION 96",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1725,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1810,
                           perf, 2, 210, 108, 176);
    (void)frame;
}

int page_tnr_cl_run(volatile sig_atomic_t *running) {
    tnr_stream_t stream;
    tnr_chain_t chain = {0};
    page_surface_t surface;
    tnr_live_ctx_t ctx;
    memset(&stream, 0, sizeof(stream));
    memset(&surface, 0, sizeof(surface));
    memset(&ctx, 0, sizeof(ctx));

    if (!path_readable(TNR_H264_PATH)) {
        fprintf(stderr, "TNR_CL input missing: %s\n", TNR_H264_PATH);
        return 1;
    }
    if (read_stream(TNR_H264_PATH, &stream) != 0) {
        fprintf(stderr, "TNR_CL stream load failed: %s\n", TNR_H264_PATH);
        return 1;
    }

    ctx.input = (uint8_t *)malloc(TNR_FRAME_SIZE);
    ctx.output = (uint8_t *)malloc(TNR_FRAME_SIZE);
    if (!ctx.input || !ctx.output) {
        free(ctx.input);
        free(ctx.output);
        free_stream(&stream);
        return 1;
    }
    memset(ctx.input, 16, TNR_FRAME_SIZE);
    memset(ctx.output, 16, TNR_FRAME_SIZE);

    if (page_surface_open(&surface, TNR_PAGE_POOL, TNR_PAGE_W, TNR_PAGE_H,
                          TNR_PAGE_STRIDE, TNR_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input);
        free(ctx.output);
        free_stream(&stream);
        return 1;
    }
    if (setup_tnr_decode_chain(&chain) != 0) {
        fprintf(stderr, "TNR_CL H264 decode chain setup failed\n");
        cleanup_tnr_decode_chain(&chain);
        page_surface_close(&surface);
        free(ctx.input);
        free(ctx.output);
        free_stream(&stream);
        return 1;
    }

    printf("TNR_CL standalone: %s -> VDEC -> VPSS raw/TNR -> page VO. Ctrl+C to stop.\n",
           TNR_H264_PATH);
    int sent_frames = 0;
    while (!running || *running) {
        if (feed_stream_frame(&stream) != 0) {
            fprintf(stderr, "TNR_CL MEDIA_VDEC_SendPacket failed\n");
            usleep(1000000 / TNR_PAGE_FPS);
            continue;
        }
        sent_frames++;
        poll_tnr_live_frames(&ctx);
        (void)page_surface_send_frame(&surface, draw_tnr_live_page, &ctx, sent_frames);
        if ((sent_frames % TNR_PAGE_FPS) == 0) {
            uint64_t vdec_count = 0;
            uint64_t vpss_count = 0;
            uint64_t tnr_count = 0;
            uint64_t vo_count = 0;
            (void)MEDIA_SYS_GetModuleFrameCount("VDEC", TNR_VDEC_CHN, &vdec_count);
            (void)MEDIA_SYS_GetModuleFrameCount("VPSS", TNR_VPSS_GRP, &vpss_count);
            (void)MEDIA_SYS_GetModuleFrameCount("TNR_CL", TNR_GRP, &tnr_count);
            (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
            printf("TNR_CL vdec=%llu vpss=%llu tnr=%llu vo=%llu sent=%d processed=%d threshold=%.2f alpha=%.2f/%.2f cl=%.3f/%.3f/%.3f h264=1\n",
                   (unsigned long long)vdec_count,
                   (unsigned long long)vpss_count,
                   (unsigned long long)tnr_count,
                   (unsigned long long)vo_count,
                   sent_frames, ctx.processed, TNR_THRESHOLD,
                   TNR_STATIC_ALPHA, TNR_MOTION_ALPHA,
                   ctx.perf.gpu_motion_ms, ctx.perf.gpu_blend_ms,
                   ctx.perf.gpu_queue_total_ms);
        }
        usleep(1000000 / TNR_PAGE_FPS);
    }

    cleanup_tnr_decode_chain(&chain);
    page_surface_close(&surface);
    free(ctx.input);
    free(ctx.output);
    free_stream(&stream);
    return 0;
}
