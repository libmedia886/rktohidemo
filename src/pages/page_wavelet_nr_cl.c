#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_surface.h"

#include <linux/dma-buf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define WNR_PAGE_W 1080
#define WNR_PAGE_H 1920
#define WNR_PAGE_STRIDE 1088
#define WNR_PAGE_POOL 1
#define WNR_PAGE_FPS 30
#define WNR_H264_PATH "assets/loop/tnr_cl/online_lowlight_noisy_640x640_120.h264"
#define WNR_VDEC_CHN 377
#define WNR_VPSS_GRP 376
#define WNR_GRP 134
#define WNR_VDEC_POOL 2
#define WNR_VPSS_RAW_POOL 6
#define WNR_VPSS_WNR_POOL 7
#define WNR_OUTPUT_POOL 8
#define WNR_W 640
#define WNR_H 640
#define WNR_STRIDE 640
#define WNR_FRAME_SIZE (WNR_STRIDE * WNR_H * 3 / 2)
#define WNR_VDEC_SIZE ((size_t)WNR_STRIDE * (size_t)(((WNR_H + 15) / 16) * 16) * 2u)
#define WNR_PANE_X ((WNR_PAGE_W - WNR_W) / 2)
#define WNR_TOP_Y 246
#define WNR_BOTTOM_Y 930
#define LICENSE_PATH "/root/licence.dat"

#define WNR_LEVELS 1
#define WNR_THRESHOLD_Y (6.0f / 255.0f)
#define WNR_STRENGTH 0.65f

typedef struct {
    uint8_t *input;
    uint8_t *output;
    int input_ready;
    int output_ready;
    int processed;
    int module_ok;
    MEDIA_WAVELET_NR_CL_PERF perf;
} wavelet_page_ctx_t;

typedef struct {
    uint8_t *data;
    size_t size;
    int *offsets;
    int count;
    int index;
    uint64_t pts;
} wavelet_stream_t;

typedef struct {
    int vdec_pool_ok;
    int vpss_raw_pool_ok;
    int vpss_wnr_pool_ok;
    int wnr_pool_ok;
    int vdec_ok;
    int vpss_ok;
    int wnr_ok;
    int bind_vdec_vpss_ok;
    int bind_vpss_wnr_ok;
    int vdec_started;
} wavelet_chain_t;

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

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

static int start_code_len(const uint8_t *data, size_t size, size_t pos) {
    if (!data || pos + 3 > size) return 0;
    if (data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) return 3;
    if (pos + 4 <= size && data[pos] == 0x00 && data[pos + 1] == 0x00 &&
        data[pos + 2] == 0x00 && data[pos + 3] == 0x01) return 4;
    return 0;
}

static void free_stream(wavelet_stream_t *stream) {
    if (!stream) return;
    free(stream->data);
    free(stream->offsets);
    memset(stream, 0, sizeof(*stream));
}

static int read_stream(const char *path, wavelet_stream_t *stream) {
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
        } else {
            ++i;
        }
    }
    if (stream->count == 0) stream->offsets[stream->count++] = 0;
    return 0;
}

static int stream_nal_is_vcl(const wavelet_stream_t *stream, int idx) {
    if (!stream || idx < 0 || idx >= stream->count) return 0;
    int start = stream->offsets[idx];
    int end = (idx + 1 < stream->count) ? stream->offsets[idx + 1] : (int)stream->size;
    int sc = start_code_len(stream->data, stream->size, (size_t)start);
    int nal_start = start + sc;
    if (nal_start >= end) return 0;
    int nal = stream->data[nal_start] & 0x1f;
    return nal == 1 || nal == 5;
}

static int feed_stream_frame(wavelet_stream_t *stream) {
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

    if (MEDIA_VDEC_SendPacket(WNR_VDEC_CHN, stream->data + start,
                              (size_t)len, stream->pts) != 0) {
        return -1;
    }
    stream->pts += (uint64_t)(1000 / WNR_PAGE_FPS);
    stream->index = (end_idx < stream->count) ? end_idx : 0;
    return 0;
}

static void fill_wavelet_input(uint8_t *dst, int frame) {
    uint32_t seed = 0x9e3779b9u + (uint32_t)frame * 2654435761u;
    if (!dst) return;
    for (int y = 0; y < WNR_H; ++y) {
        uint8_t *row = dst + (size_t)y * WNR_STRIDE;
        for (int x = 0; x < WNR_W; ++x) {
            seed = seed * 1664525u + 1013904223u;
            int base = 52 + (x * 94) / WNR_W + (y * 48) / WNR_H;
            int block = ((x / 48) ^ (y / 48)) ? 18 : -12;
            int fine = ((x + frame * 2) % 96 < 48) ? 10 : -8;
            int noise = (int)((seed >> 24) & 0x3f) - 32;
            row[x] = clamp_u8(base + block + fine + noise);
        }
    }

    uint8_t *uv = dst + (size_t)WNR_STRIDE * WNR_H;
    for (int y = 0; y < WNR_H / 2; ++y) {
        memset(uv + (size_t)y * WNR_STRIDE, 128, WNR_STRIDE);
    }
}

static int setup_wavelet_decode_chain(wavelet_chain_t *chain) {
    MEDIA_VDEC_ATTR vdec = {0};
    MEDIA_VPSS_ATTR vpss = {0};
    MEDIA_WAVELET_NR_CL_ATTR attr = {0};
    if (!chain) return -1;

    if (MEDIA_POOL_Create(WNR_VDEC_POOL, WNR_VDEC_SIZE, 8) != 0) return -1;
    chain->vdec_pool_ok = 1;
    if (MEDIA_POOL_Create(WNR_VPSS_RAW_POOL, WNR_FRAME_SIZE, 8) != 0) return -1;
    chain->vpss_raw_pool_ok = 1;
    if (MEDIA_POOL_Create(WNR_VPSS_WNR_POOL, WNR_FRAME_SIZE, 8) != 0) return -1;
    chain->vpss_wnr_pool_ok = 1;
    if (MEDIA_POOL_Create(WNR_OUTPUT_POOL, WNR_FRAME_SIZE, 8) != 0) return -1;
    chain->wnr_pool_ok = 1;

    vdec.width = WNR_W;
    vdec.height = WNR_H;
    vdec.stride = WNR_STRIDE;
    vdec.buf_cnt = 8;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = WNR_VDEC_POOL;
    if (MEDIA_VDEC_CreateChn(WNR_VDEC_CHN, &vdec) != 0) return -1;
    chain->vdec_ok = 1;

    vpss.width = WNR_W;
    vpss.height = WNR_H;
    vpss.input_stride = WNR_STRIDE;
    vpss.input_depth = 8;
    vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.in_fps = WNR_PAGE_FPS;
    vpss.out_fps = WNR_PAGE_FPS;
    vpss.output_count = 2;
    vpss.outputs[0].output_id = 0;
    vpss.outputs[0].out_width = WNR_W;
    vpss.outputs[0].out_height = WNR_H;
    vpss.outputs[0].out_stride = WNR_STRIDE;
    vpss.outputs[0].pool_id = WNR_VPSS_RAW_POOL;
    vpss.outputs[0].crop_w = WNR_W;
    vpss.outputs[0].crop_h = WNR_H;
    vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    vpss.outputs[1].output_id = 1;
    vpss.outputs[1].out_width = WNR_W;
    vpss.outputs[1].out_height = WNR_H;
    vpss.outputs[1].out_stride = WNR_STRIDE;
    vpss.outputs[1].pool_id = WNR_VPSS_WNR_POOL;
    vpss.outputs[1].crop_w = WNR_W;
    vpss.outputs[1].crop_h = WNR_H;
    vpss.outputs[1].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(WNR_VPSS_GRP, &vpss) != 0) return -1;
    chain->vpss_ok = 1;

    attr.width = WNR_W;
    attr.height = WNR_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 8;
    attr.output_pool_id = WNR_OUTPUT_POOL;
    attr.input_stride = WNR_STRIDE;
    attr.output_stride = WNR_STRIDE;
    attr.levels = WNR_LEVELS;
    attr.threshold_y = WNR_THRESHOLD_Y;
    attr.strength = WNR_STRENGTH;
    attr.uv_enable = 0;
    attr.uv_strength = 0.0f;

    if (MEDIA_WAVELET_NR_CL_CreateGrp(WNR_GRP, &attr) != 0) return -1;
    chain->wnr_ok = 1;

    if (MEDIA_SYS_Bind("VDEC", WNR_VDEC_CHN, "output", "VPSS", WNR_VPSS_GRP, "input") != 0) return -1;
    chain->bind_vdec_vpss_ok = 1;
    if (MEDIA_SYS_Bind("VPSS", WNR_VPSS_GRP, "output1", "WAVELET_NR_CL", WNR_GRP, "input") != 0) return -1;
    chain->bind_vpss_wnr_ok = 1;

    if (MEDIA_WAVELET_NR_CL_Start(WNR_GRP) != 0 ||
        MEDIA_VPSS_Enable(WNR_VPSS_GRP) != 0 ||
        MEDIA_VDEC_Start(WNR_VDEC_CHN) != 0) {
        return -1;
    }
    chain->vdec_started = 1;
    set_tile_status("VDEC", TILE_LIVE);
    set_tile_status("VPSS", TILE_LIVE);
    set_tile_status("WAVELET_NR_CL", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void drain_wavelet_vpss_output(int chn) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_VPSS_Chn_GetFrame(WNR_VPSS_GRP, chn, &out, 0) != 0) break;
            MEDIA_VPSS_Chn_ReleaseFrame(WNR_VPSS_GRP, chn, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_wavelet_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_WAVELET_NR_CL_GetFrame(WNR_GRP, &out, 0) != 0) break;
            MEDIA_WAVELET_NR_CL_ReleaseFrame(WNR_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void cleanup_wavelet_decode_chain(wavelet_chain_t *chain) {
    if (!chain) return;
    if (chain->vdec_started) {
        MEDIA_VDEC_Stop(WNR_VDEC_CHN);
        chain->vdec_started = 0;
        usleep(50000);
    }
    if (chain->vpss_ok) MEDIA_VPSS_Disable(WNR_VPSS_GRP);
    if (chain->wnr_ok) {
        drain_wavelet_output();
        MEDIA_WAVELET_NR_CL_Stop(WNR_GRP);
        drain_wavelet_output();
    }
    if (chain->bind_vpss_wnr_ok) {
        MEDIA_SYS_UnBind("VPSS", WNR_VPSS_GRP, "output1", "WAVELET_NR_CL", WNR_GRP, "input");
        chain->bind_vpss_wnr_ok = 0;
    }
    if (chain->bind_vdec_vpss_ok) {
        MEDIA_SYS_UnBind("VDEC", WNR_VDEC_CHN, "output", "VPSS", WNR_VPSS_GRP, "input");
        chain->bind_vdec_vpss_ok = 0;
    }
    if (chain->vpss_ok) {
        drain_wavelet_vpss_output(0);
        drain_wavelet_vpss_output(1);
        MEDIA_VPSS_DestroyGrp(WNR_VPSS_GRP);
        chain->vpss_ok = 0;
    }
    if (chain->wnr_ok) {
        MEDIA_WAVELET_NR_CL_DestroyGrp(WNR_GRP);
        chain->wnr_ok = 0;
    }
    if (chain->vdec_ok) {
        MEDIA_VDEC_DestroyChn(WNR_VDEC_CHN);
        chain->vdec_ok = 0;
    }
    if (chain->wnr_pool_ok) {
        MEDIA_POOL_Destroy(WNR_OUTPUT_POOL);
        chain->wnr_pool_ok = 0;
    }
    if (chain->vpss_wnr_pool_ok) {
        MEDIA_POOL_Destroy(WNR_VPSS_WNR_POOL);
        chain->vpss_wnr_pool_ok = 0;
    }
    if (chain->vpss_raw_pool_ok) {
        MEDIA_POOL_Destroy(WNR_VPSS_RAW_POOL);
        chain->vpss_raw_pool_ok = 0;
    }
    if (chain->vdec_pool_ok) {
        MEDIA_POOL_Destroy(WNR_VDEC_POOL);
        chain->vdec_pool_ok = 0;
    }
}

static void poll_wavelet_frames(wavelet_page_ctx_t *ctx) {
    if (!ctx) return;
    for (;;) {
        MEDIA_BUFFER raw = {-1, -1};
        if (MEDIA_VPSS_Chn_GetFrame(WNR_VPSS_GRP, 0, &raw, 0) != 0) break;
        if (copy_from_buffer(raw, ctx->input, WNR_FRAME_SIZE) == 0) ctx->input_ready = 1;
        MEDIA_VPSS_Chn_ReleaseFrame(WNR_VPSS_GRP, 0, raw);
    }
    for (;;) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_WAVELET_NR_CL_GetFrame(WNR_GRP, &out, 0) != 0) break;
        if (copy_from_buffer(out, ctx->output, WNR_FRAME_SIZE) == 0) {
            ctx->output_ready = 1;
            ctx->processed++;
            (void)MEDIA_WAVELET_NR_CL_GetLastPerf(WNR_GRP, &ctx->perf);
        }
        MEDIA_WAVELET_NR_CL_ReleaseFrame(WNR_GRP, out);
    }
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * WNR_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * WNR_STRIDE;
        for (int x = 0; x < dw; ++x) {
            int sx = x * WNR_W / dw;
            drow[x] = srow[sx];
        }
    }

    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)WNR_STRIDE * WNR_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (WNR_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * WNR_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = ((x * WNR_W / dw) & ~1);
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_wavelet_page(uint8_t *dst, int stride, int width, int height,
                              int frame, void *opaque) {
    wavelet_page_ctx_t *ctx = (wavelet_page_ctx_t *)opaque;
    char processed[48];
    char perf[72];
    snprintf(processed, sizeof(processed), "PROCESSED %06d", ctx ? ctx->processed : 0);
    if (ctx && ctx->perf.gpu_enabled) {
        snprintf(perf, sizeof(perf), "GPU K %03d US Q %03d US",
                 (int)(ctx->perf.gpu_kernel_ms * 1000.0),
                 (int)(ctx->perf.gpu_queue_ms * 1000.0));
    } else if (ctx) {
        snprintf(perf, sizeof(perf), "CPU %03d US", (int)(ctx->perf.cpu_ms * 1000.0));
    } else {
        snprintf(perf, sizeof(perf), "PERF NA");
    }

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 8, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           "WAVELET NR CL H264", 6, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 60, 142,
                           "H264 VDEC REALTIME INPUT", 3, 210, 144, 84);

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
                           "WAVELET OUTPUT", 3, 220, 108, 176);
    if (ctx) {
        if (ctx->input_ready) draw_nv12_scaled(dst, stride, width, height, x, top_y, pane, pane, ctx->input);
        if (ctx->output_ready) draw_nv12_scaled(dst, stride, width, height, x, bottom_y, pane, pane, ctx->output);
    }

    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           "LEVEL 1 THRESHOLD 6/255 STRENGTH 65",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1725,
                           perf, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1810,
                           "H264->VDEC->VPSS RAW/WAVELET NO TEMPORAL BLEND",
                           2, 190, 144, 84);
    (void)frame;
}

int page_wavelet_nr_cl_run(volatile sig_atomic_t *running) {
    wavelet_stream_t stream;
    wavelet_chain_t chain;
    page_surface_t surface;
    wavelet_page_ctx_t ctx;
    memset(&stream, 0, sizeof(stream));
    memset(&chain, 0, sizeof(chain));
    memset(&surface, 0, sizeof(surface));
    memset(&ctx, 0, sizeof(ctx));

    if (access(WNR_H264_PATH, R_OK) != 0) {
        fprintf(stderr, "WAVELET_NR_CL input missing: %s\n", WNR_H264_PATH);
        return 1;
    }
    if (read_stream(WNR_H264_PATH, &stream) != 0) {
        fprintf(stderr, "WAVELET_NR_CL stream load failed: %s\n", WNR_H264_PATH);
        return 1;
    }

    ctx.input = (uint8_t *)malloc(WNR_FRAME_SIZE);
    ctx.output = (uint8_t *)malloc(WNR_FRAME_SIZE);
    if (!ctx.input || !ctx.output) {
        free(ctx.input);
        free(ctx.output);
        free_stream(&stream);
        return 1;
    }
    memset(ctx.input, 16, WNR_FRAME_SIZE);
    memset(ctx.output, 16, WNR_FRAME_SIZE);

    if (page_surface_open(&surface, WNR_PAGE_POOL, WNR_PAGE_W, WNR_PAGE_H,
                          WNR_PAGE_STRIDE, WNR_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input);
        free(ctx.output);
        free_stream(&stream);
        return 1;
    }

    ctx.module_ok = setup_wavelet_decode_chain(&chain) == 0;
    if (!ctx.module_ok) {
        fprintf(stderr, "WAVELET_NR_CL H264 decode chain setup failed\n");
        cleanup_wavelet_decode_chain(&chain);
        page_surface_close(&surface);
        free(ctx.input);
        free(ctx.output);
        free_stream(&stream);
        return 1;
    }

    printf("WAVELET_NR_CL standalone: %s -> VDEC -> VPSS raw/WAVELET -> page VO. Ctrl+C to stop.\n",
           WNR_H264_PATH);

    int frame = 0;
    while (!running || *running) {
        if (feed_stream_frame(&stream) != 0) {
            fprintf(stderr, "WAVELET_NR_CL MEDIA_VDEC_SendPacket failed\n");
            usleep(1000000 / WNR_PAGE_FPS);
            continue;
        }
        poll_wavelet_frames(&ctx);
        if (page_surface_send_frame(&surface, draw_wavelet_page, &ctx, frame) != 0) {
            usleep(1000);
            continue;
        }
        frame++;
        if ((frame % WNR_PAGE_FPS) == 0) {
            printf("WAVELET_NR_CL frames=%d processed=%d levels=%d threshold=%.5f strength=%.2f gpu=%d kernel=%.3f queue=%.3f cpu=%.3f\n",
                   frame, ctx.processed, WNR_LEVELS, WNR_THRESHOLD_Y, WNR_STRENGTH,
                   ctx.perf.gpu_enabled, ctx.perf.gpu_kernel_ms,
                   ctx.perf.gpu_queue_ms, ctx.perf.cpu_ms);
        }
        usleep(1000000 / WNR_PAGE_FPS);
    }

    cleanup_wavelet_decode_chain(&chain);
    page_surface_close(&surface);
    free(ctx.input);
    free(ctx.output);
    free_stream(&stream);
    return 0;
}
