#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_surface.h"
#include "rknn_api.h"

#include <linux/dma-buf.h>
#include <math.h>
#include <png.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SEG_SCREEN_W 1080
#define SEG_SCREEN_H 1920
#define SEG_SCREEN_STRIDE 1088
#define SEG_SCREEN_POOL 1
#define SEG_FPS 30
#define SEG_FRAME_COUNT 1
#define SEG_SOURCE_HOLD 1
#define SEG_INFER_INTERVAL 4
#define SEG_CLASS_COUNT 19
#define SEG_MODEL_PATH "assets/npu/pp_liteseg_cityscapes_rk3588_i8.rknn"
#define SEG_H264_PATH "assets/loop/npu/bus_640x640.h264"
#define SEG_SRC_W 640
#define SEG_SRC_H 640
#define SEG_SRC_STRIDE 640
#define SEG_MODEL_IO_W 512
#define SEG_MODEL_IO_H 512
#define SEG_VDEC_CHN 320
#define SEG_RGA_GRP 321
#define SEG_VDEC_POOL 2
#define SEG_RGB_POOL 3
#define SEG_VDEC_FRAME_SIZE ((size_t)SEG_SRC_STRIDE * (size_t)((SEG_SRC_H + 15) & ~15) * 2u)
#define SEG_RGB_STRIDE (SEG_MODEL_IO_W * 3)
#define SEG_RGB_FRAME_SIZE ((size_t)SEG_RGB_STRIDE * (size_t)SEG_MODEL_IO_H)
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int id;
    const char *name;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} seg_label_t;

static const seg_label_t g_cityscapes[SEG_CLASS_COUNT] = {
    {0, "road", 128, 64, 128},
    {1, "sidewalk", 244, 35, 232},
    {2, "building", 70, 70, 70},
    {3, "wall", 102, 102, 156},
    {4, "fence", 190, 153, 153},
    {5, "pole", 153, 153, 153},
    {6, "traffic light", 250, 170, 30},
    {7, "traffic sign", 220, 220, 0},
    {8, "vegetation", 107, 142, 35},
    {9, "terrain", 152, 251, 152},
    {10, "sky", 70, 130, 180},
    {11, "person", 220, 20, 60},
    {12, "rider", 255, 0, 0},
    {13, "car", 0, 0, 142},
    {14, "truck", 0, 0, 70},
    {15, "bus", 0, 60, 100},
    {16, "train", 0, 80, 100},
    {17, "motorcycle", 0, 0, 230},
    {18, "bicycle", 119, 11, 32},
};

typedef struct {
    uint8_t *data;
    size_t size;
    int *nal_offsets;
    int nal_count;
    int nal_index;
    uint64_t pts;
} h264_stream_t;

typedef struct {
    uint8_t *rgb[SEG_FRAME_COUNT];
    uint8_t *video_rgb;
    uint8_t *request_rgb;
    uint8_t *infer_rgb;
    uint8_t *mask_class;
    uint8_t *mask_work;
    int frame_w;
    int frame_h;
    int loaded_count;
    int current_index;
    int display_index;
    int result_index;
    int model_w;
    int model_h;
    int model_c;
    int class_count;
    int output_w;
    int output_h;
    int output_layout; /* 0=NCHW, 1=NHWC */
    rknn_context rknn;
    rknn_input_output_num io_num;
    double run_ms;
    double post_ms;
    double total_ms;
    int result_ready;
    int sync_ready;
    int worker_started;
    int stop_worker;
    int worker_busy;
    int request_pending;
    int request_index;
    int request_frame_no;
    int mask_frame_no;
    int dropped_requests;
    int video_ready;
    int video_frame_no;
    int vdec_pool_ok;
    int rgb_pool_ok;
    int vdec_ok;
    int rga_ok;
    int bind_vdec_rga_ok;
    int vdec_started;
    const char *vdec_src_port;
    const char *rga_in_port;
    h264_stream_t stream;
    pthread_t worker;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} segment_page_ctx_t;

static long long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

static int load_file(const char *path, uint8_t **data, int *size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long len = ftell(fp);
    if (len <= 0) {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)len);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);
    *data = buf;
    *size = (int)len;
    return 0;
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
    uint8_t *data = NULL;
    int size = 0;
    int cap = 256;
    if (!path || !stream) return -1;
    memset(stream, 0, sizeof(*stream));
    if (load_file(path, &data, &size) != 0 || size <= 0) return -1;
    stream->data = data;
    stream->size = (size_t)size;
    stream->nal_offsets = (int *)malloc((size_t)cap * sizeof(int));
    if (!stream->nal_offsets) {
        unload_h264_stream(stream);
        return -1;
    }
    for (size_t i = 0; i + 3 < stream->size;) {
        int sc_len = h264_start_code_len(stream->data, stream->size, i);
        if (sc_len > 0) {
            if (stream->nal_count >= cap) {
                cap *= 2;
                int *tmp = (int *)realloc(stream->nal_offsets, (size_t)cap * sizeof(int));
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
    return 0;
}

static int feed_h264_packet(int vdec_chn, h264_stream_t *stream, int *is_vcl) {
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
    if (vcl) stream->pts += (uint64_t)(1000 / SEG_FPS);
    if (is_vcl) *is_vcl = vcl;
    return 0;
}

static int bind_first_match(const char *src_mod, int src_id,
                            const char *const *src_ports, int src_count,
                            const char *dst_mod, int dst_id,
                            const char *const *dst_ports, int dst_count,
                            const char **bound_src, const char **bound_dst) {
    for (int i = 0; i < src_count; ++i) {
        for (int j = 0; j < dst_count; ++j) {
            if (MEDIA_SYS_Bind(src_mod, src_id, src_ports[i], dst_mod, dst_id, dst_ports[j]) == 0) {
                if (bound_src) *bound_src = src_ports[i];
                if (bound_dst) *bound_dst = dst_ports[j];
                return 0;
            }
        }
    }
    return -1;
}

static int load_png_rgb(const char *path, uint8_t **out, int *out_w, int *out_h) {
    FILE *fp = fopen(path, "rb");
    png_structp png = NULL;
    png_infop info = NULL;
    uint8_t *raw = NULL;
    png_bytep *rows = NULL;
    uint8_t *rgb = NULL;
    int ret = -1;
    if (!fp) return -1;

    uint8_t sig[8];
    if (fread(sig, 1, sizeof(sig), fp) != sizeof(sig) ||
        png_sig_cmp(sig, 0, sizeof(sig)) != 0) {
        goto done;
    }
    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) goto done;
    info = png_create_info_struct(png);
    if (!info) goto done;
    if (setjmp(png_jmpbuf(png))) goto done;

    png_init_io(png, fp);
    png_set_sig_bytes(png, sizeof(sig));
    png_read_info(png, info);
    int width = (int)png_get_image_width(png, info);
    int height = (int)png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);
    if (width <= 0 || height <= 0) goto done;
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
        png_set_add_alpha(png, 0xff, PNG_FILLER_AFTER);
    }
    png_read_update_info(png, info);

    png_size_t rowbytes = png_get_rowbytes(png, info);
    raw = (uint8_t *)malloc(rowbytes * (size_t)height);
    rows = (png_bytep *)malloc(sizeof(png_bytep) * (size_t)height);
    rgb = (uint8_t *)malloc((size_t)width * (size_t)height * 3);
    if (!raw || !rows || !rgb) goto done;
    for (int y = 0; y < height; ++y) rows[y] = raw + (size_t)y * rowbytes;
    png_read_image(png, rows);
    for (int y = 0; y < height; ++y) {
        const uint8_t *src = rows[y];
        uint8_t *dst = rgb + (size_t)y * (size_t)width * 3;
        for (int x = 0; x < width; ++x) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
    }
    *out = rgb;
    *out_w = width;
    *out_h = height;
    rgb = NULL;
    ret = 0;

done:
    free(rgb);
    free(rows);
    free(raw);
    if (png || info) png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    return ret;
}

static void rgb_to_yuv(uint8_t r, uint8_t g, uint8_t b,
                       uint8_t *yy, uint8_t *uu, uint8_t *vv) {
    int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
    if (y < 0) y = 0; else if (y > 255) y = 255;
    if (u < 0) u = 0; else if (u > 255) u = 255;
    if (v < 0) v = 0; else if (v > 255) v = 255;
    *yy = (uint8_t)y;
    *uu = (uint8_t)u;
    *vv = (uint8_t)v;
}

static void draw_scaled_rgb(uint8_t *dst, int stride, int screen_w, int screen_h,
                            int dx, int dy, int dw, int dh,
                            const uint8_t *rgb, int sw, int sh) {
    if (!dst || !rgb || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    uint8_t *uv = dst + (size_t)stride * screen_h;
    for (int y = 0; y < dh; ++y) {
        int sy = y * sh / dh;
        int yypos = dy + y;
        if (yypos < 0 || yypos >= screen_h) continue;
        for (int x = 0; x < dw; ++x) {
            int sx = x * sw / dw;
            int xx = dx + x;
            if (xx < 0 || xx >= screen_w) continue;
            const uint8_t *p = rgb + ((size_t)sy * (size_t)sw + sx) * 3;
            uint8_t yv, u, v;
            rgb_to_yuv(p[0], p[1], p[2], &yv, &u, &v);
            dst[(size_t)yypos * stride + xx] = yv;
            if ((xx & 1) == 0 && (yypos & 1) == 0) {
                uv[(size_t)(yypos / 2) * stride + xx] = u;
                uv[(size_t)(yypos / 2) * stride + xx + 1] = v;
            }
        }
    }
}

static void draw_scaled_segment_overlay(uint8_t *dst, int stride, int screen_w, int screen_h,
                                        int dx, int dy, int dw, int dh,
                                        const uint8_t *rgb, const uint8_t *mask,
                                        int sw, int sh) {
    if (!dst || !rgb || !mask || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    uint8_t *uv = dst + (size_t)stride * screen_h;
    for (int y = 0; y < dh; ++y) {
        int sy = y * sh / dh;
        int yypos = dy + y;
        if (yypos < 0 || yypos >= screen_h) continue;
        for (int x = 0; x < dw; ++x) {
            int sx = x * sw / dw;
            int xx = dx + x;
            if (xx < 0 || xx >= screen_w) continue;
            const uint8_t *src = rgb + ((size_t)sy * (size_t)sw + sx) * 3;
            const seg_label_t *label = &g_cityscapes[mask[(size_t)sy * (size_t)sw + sx] % SEG_CLASS_COUNT];
            int alpha = (label->id == 0 || label->id == 2 || label->id == 8) ? 48 : 84;
            uint8_t rr = (uint8_t)((src[0] * (255 - alpha) + label->r * alpha) / 255);
            uint8_t gg = (uint8_t)((src[1] * (255 - alpha) + label->g * alpha) / 255);
            uint8_t bb = (uint8_t)((src[2] * (255 - alpha) + label->b * alpha) / 255);
            uint8_t yv, u, v;
            rgb_to_yuv(rr, gg, bb, &yv, &u, &v);
            dst[(size_t)yypos * stride + xx] = yv;
            if ((xx & 1) == 0 && (yypos & 1) == 0) {
                uv[(size_t)(yypos / 2) * stride + xx] = u;
                uv[(size_t)(yypos / 2) * stride + xx + 1] = v;
            }
        }
    }
}

static int segment_init_model(segment_page_ctx_t *ctx) {
    uint8_t *model = NULL;
    int model_size = 0;
    if (load_file(SEG_MODEL_PATH, &model, &model_size) != 0) {
        fprintf(stderr, "SEGMENT_NPU: model missing %s\n", SEG_MODEL_PATH);
        return -1;
    }
    int ret = rknn_init(&ctx->rknn, model, (uint32_t)model_size, 0, NULL);
    free(model);
    if (ret < 0) {
        fprintf(stderr, "SEGMENT_NPU: rknn_init failed ret=%d\n", ret);
        return -1;
    }
    ret = rknn_query(ctx->rknn, RKNN_QUERY_IN_OUT_NUM, &ctx->io_num, sizeof(ctx->io_num));
    if (ret != RKNN_SUCC || ctx->io_num.n_input < 1 || ctx->io_num.n_output < 1) {
        fprintf(stderr, "SEGMENT_NPU: query io failed ret=%d\n", ret);
        return -1;
    }

    rknn_tensor_attr input = {0};
    input.index = 0;
    ret = rknn_query(ctx->rknn, RKNN_QUERY_INPUT_ATTR, &input, sizeof(input));
    if (ret != RKNN_SUCC) return -1;
    if (input.fmt == RKNN_TENSOR_NCHW) {
        ctx->model_c = input.dims[1];
        ctx->model_h = input.dims[2];
        ctx->model_w = input.dims[3];
    } else {
        ctx->model_h = input.dims[1];
        ctx->model_w = input.dims[2];
        ctx->model_c = input.dims[3];
    }
    if (ctx->model_w <= 0 || ctx->model_h <= 0 || ctx->model_c != 3) {
        fprintf(stderr, "SEGMENT_NPU: unexpected input dims %d %d %d\n",
                ctx->model_w, ctx->model_h, ctx->model_c);
        return -1;
    }

    rknn_tensor_attr output = {0};
    output.index = 0;
    ret = rknn_query(ctx->rknn, RKNN_QUERY_OUTPUT_ATTR, &output, sizeof(output));
    if (ret != RKNN_SUCC) return -1;
    ctx->class_count = SEG_CLASS_COUNT;
    if (output.n_dims == 4 && output.dims[1] == SEG_CLASS_COUNT) {
        ctx->output_layout = 0;
        ctx->output_h = output.dims[2];
        ctx->output_w = output.dims[3];
    } else if (output.n_dims == 4 && output.dims[3] == SEG_CLASS_COUNT) {
        ctx->output_layout = 1;
        ctx->output_h = output.dims[1];
        ctx->output_w = output.dims[2];
    } else {
        ctx->output_layout = 0;
        ctx->output_h = ctx->model_h;
        ctx->output_w = ctx->model_w;
    }
    printf("SEGMENT_NPU model: input=%dx%dx%d output=%dx%dx%d layout=%s\n",
           ctx->model_w, ctx->model_h, ctx->model_c,
           ctx->output_w, ctx->output_h, ctx->class_count,
           ctx->output_layout ? "NHWC" : "NCHW");
    return 0;
}

static int segment_load_frames(segment_page_ctx_t *ctx) {
    if (!ctx) return -1;
    ctx->frame_w = SEG_MODEL_IO_W;
    ctx->frame_h = SEG_MODEL_IO_H;
    ctx->loaded_count = 1;
    ctx->video_rgb = (uint8_t *)malloc(SEG_RGB_FRAME_SIZE);
    ctx->request_rgb = (uint8_t *)malloc(SEG_RGB_FRAME_SIZE);
    ctx->infer_rgb = (uint8_t *)malloc(SEG_RGB_FRAME_SIZE);
    ctx->mask_class = (uint8_t *)malloc((size_t)ctx->frame_w * (size_t)ctx->frame_h);
    ctx->mask_work = (uint8_t *)malloc((size_t)ctx->frame_w * (size_t)ctx->frame_h);
    if (!ctx->video_rgb || !ctx->request_rgb || !ctx->infer_rgb ||
        !ctx->mask_class || !ctx->mask_work) {
        return -1;
    }
    memset(ctx->video_rgb, 0, SEG_RGB_FRAME_SIZE);
    memset(ctx->request_rgb, 0, SEG_RGB_FRAME_SIZE);
    memset(ctx->infer_rgb, 0, SEG_RGB_FRAME_SIZE);
    memset(ctx->mask_class, 0, (size_t)ctx->frame_w * (size_t)ctx->frame_h);
    memset(ctx->mask_work, 0, (size_t)ctx->frame_w * (size_t)ctx->frame_h);
    ctx->rgb[0] = ctx->infer_rgb;
    if (!path_readable(SEG_H264_PATH) || load_h264_stream(SEG_H264_PATH, &ctx->stream) != 0) {
        fprintf(stderr, "SEGMENT_NPU: H264 source missing/read failed %s\n", SEG_H264_PATH);
        return -1;
    }
    return 0;
}

static float seg_output_value(const float *out, int cls, int y, int x,
                              int class_count, int h, int w, int layout) {
    if (layout) {
        return out[((size_t)y * (size_t)w + x) * (size_t)class_count + cls];
    }
    return out[(size_t)cls * (size_t)h * (size_t)w + (size_t)y * (size_t)w + x];
}

static int segment_run_frame(segment_page_ctx_t *ctx, const uint8_t *input_rgb,
                             int frame_no, uint8_t *mask_out,
                             double *run_ms, double *post_ms, double *total_ms) {
    if (!ctx || !input_rgb || !mask_out) return -1;
    if (ctx->frame_w != ctx->model_w || ctx->frame_h != ctx->model_h) {
        fprintf(stderr, "SEGMENT_NPU: frame/model size mismatch %dx%d vs %dx%d\n",
                ctx->frame_w, ctx->frame_h, ctx->model_w, ctx->model_h);
        return -1;
    }

    rknn_input input;
    memset(&input, 0, sizeof(input));
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    input.size = (uint32_t)((size_t)ctx->model_w * (size_t)ctx->model_h * 3);
    input.buf = (void *)input_rgb;

    long long t0 = now_us();
    int ret = rknn_inputs_set(ctx->rknn, 1, &input);
    if (ret < 0) return -1;
    long long run0 = now_us();
    ret = rknn_run(ctx->rknn, NULL);
    long long run1 = now_us();
    if (ret < 0) return -1;

    rknn_output output;
    memset(&output, 0, sizeof(output));
    output.want_float = 1;
    ret = rknn_outputs_get(ctx->rknn, 1, &output, NULL);
    if (ret < 0) return -1;

    const float *scores = (const float *)output.buf;
    int out_w = ctx->output_w > 0 ? ctx->output_w : ctx->frame_w;
    int out_h = ctx->output_h > 0 ? ctx->output_h : ctx->frame_h;
    for (int y = 0; y < ctx->frame_h; ++y) {
        int oy = y * out_h / ctx->frame_h;
        for (int x = 0; x < ctx->frame_w; ++x) {
            int ox = x * out_w / ctx->frame_w;
            int best = 0;
            float best_score = seg_output_value(scores, 0, oy, ox, ctx->class_count, out_h, out_w, ctx->output_layout);
            for (int c = 1; c < ctx->class_count; ++c) {
                float v = seg_output_value(scores, c, oy, ox, ctx->class_count, out_h, out_w, ctx->output_layout);
                if (v > best_score) {
                    best_score = v;
                    best = c;
                }
            }
            mask_out[(size_t)y * (size_t)ctx->frame_w + x] = (uint8_t)best;
        }
    }
    long long t1 = now_us();
    rknn_outputs_release(ctx->rknn, 1, &output);
    if (run_ms) *run_ms = (double)(run1 - run0) / 1000.0;
    if (post_ms) *post_ms = (double)(t1 - run1) / 1000.0;
    if (total_ms) *total_ms = (double)(t1 - t0) / 1000.0;
    printf("SEGMENT_NPU frame=%d run=%.2fms post=%.2fms total=%.2fms classes=%d\n",
           frame_no,
           run_ms ? *run_ms : -1.0,
           post_ms ? *post_ms : -1.0,
           total_ms ? *total_ms : -1.0,
           ctx->class_count);
    return 0;
}

static void *segment_worker_main(void *opaque) {
    segment_page_ctx_t *ctx = (segment_page_ctx_t *)opaque;
    for (;;) {
        pthread_mutex_lock(&ctx->lock);
        while (!ctx->stop_worker && !ctx->request_pending) {
            pthread_cond_wait(&ctx->cond, &ctx->lock);
        }
        if (ctx->stop_worker) {
            pthread_mutex_unlock(&ctx->lock);
            break;
        }
        int index = ctx->request_index;
        int frame_no = ctx->request_frame_no;
        memcpy(ctx->infer_rgb, ctx->request_rgb, SEG_RGB_FRAME_SIZE);
        ctx->request_pending = 0;
        ctx->worker_busy = 1;
        pthread_mutex_unlock(&ctx->lock);

        double run_ms = 0.0;
        double post_ms = 0.0;
        double total_ms = 0.0;
        int ok = segment_run_frame(ctx, ctx->infer_rgb, frame_no,
                                   ctx->mask_work, &run_ms, &post_ms, &total_ms);

        pthread_mutex_lock(&ctx->lock);
        if (ok == 0) {
            memcpy(ctx->mask_class, ctx->mask_work,
                   (size_t)ctx->frame_w * (size_t)ctx->frame_h);
            ctx->run_ms = run_ms;
            ctx->post_ms = post_ms;
            ctx->total_ms = total_ms;
            ctx->current_index = index;
            ctx->result_index = index;
            ctx->mask_frame_no = frame_no;
            ctx->result_ready = 1;
        }
        ctx->worker_busy = 0;
        pthread_mutex_unlock(&ctx->lock);
    }
    return NULL;
}

static void segment_request_frame(segment_page_ctx_t *ctx, int index) {
    if (!ctx || !ctx->worker_started) return;
    pthread_mutex_lock(&ctx->lock);
    if (ctx->video_ready && !ctx->worker_busy && !ctx->request_pending) {
        memcpy(ctx->request_rgb, ctx->video_rgb, SEG_RGB_FRAME_SIZE);
        ctx->request_index = index;
        ctx->request_frame_no = ctx->video_frame_no;
        ctx->request_pending = 1;
        pthread_cond_signal(&ctx->cond);
    } else {
        ctx->dropped_requests++;
    }
    pthread_mutex_unlock(&ctx->lock);
}

static void segment_stop_worker(segment_page_ctx_t *ctx) {
    if (!ctx || !ctx->worker_started) return;
    pthread_mutex_lock(&ctx->lock);
    ctx->stop_worker = 1;
    pthread_cond_signal(&ctx->cond);
    pthread_mutex_unlock(&ctx->lock);
    pthread_join(ctx->worker, NULL);
    ctx->worker_started = 0;
}

static int segment_setup_video_chain(segment_page_ctx_t *ctx) {
    if (!ctx) return -1;
    MEDIA_VDEC_ATTR vdec = {0};
    MEDIA_RGA_GRP_ATTR rga = {0};
    const char *vdec_out_ports[] = {"output", "output0"};
    const char *rga_in_ports[] = {"input0", "input"};

    if (MEDIA_POOL_Create(SEG_VDEC_POOL, SEG_VDEC_FRAME_SIZE, 6) != 0) return -1;
    ctx->vdec_pool_ok = 1;
    if (MEDIA_POOL_Create(SEG_RGB_POOL, SEG_RGB_FRAME_SIZE, 4) != 0) return -1;
    ctx->rgb_pool_ok = 1;

    vdec.width = SEG_SRC_W;
    vdec.height = SEG_SRC_H;
    vdec.stride = SEG_SRC_STRIDE;
    vdec.buf_cnt = 4;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = SEG_VDEC_POOL;
    vdec.has_input_port = 0;
    vdec.input_depth = 0;
    if (MEDIA_VDEC_CreateChn(SEG_VDEC_CHN, &vdec) != 0) return -1;
    ctx->vdec_ok = 1;

    rga.algo = MEDIA_RGA_ALG_RESIZE;
    rga.input_count = 1;
    rga.output_count = 1;
    rga.input_depth = 4;
    rga.output_depth = 4;
    rga.inputs[0].width = SEG_SRC_W;
    rga.inputs[0].height = SEG_SRC_H;
    rga.inputs[0].stride = SEG_SRC_STRIDE;
    rga.inputs[0].format = MEDIA_FORMAT_NV12;
    rga.outputs[0].width = SEG_MODEL_IO_W;
    rga.outputs[0].height = SEG_MODEL_IO_H;
    rga.outputs[0].stride = SEG_RGB_STRIDE;
    rga.outputs[0].format = MEDIA_FORMAT_RGB888;
    rga.outputs[0].pool_id = SEG_RGB_POOL;
    if (MEDIA_RGA_CreateGrp(SEG_RGA_GRP, &rga) != 0 ||
        MEDIA_RGA_Start(SEG_RGA_GRP) != 0) {
        return -1;
    }
    ctx->rga_ok = 1;

    if (bind_first_match("VDEC", SEG_VDEC_CHN, vdec_out_ports, 2,
                         "RGA", SEG_RGA_GRP, rga_in_ports, 2,
                         &ctx->vdec_src_port, &ctx->rga_in_port) != 0) {
        return -1;
    }
    ctx->bind_vdec_rga_ok = 1;
    if (MEDIA_VDEC_Start(SEG_VDEC_CHN) != 0) return -1;
    ctx->vdec_started = 1;
    set_tile_status("VDEC", TILE_LIVE);
    set_tile_status("RGA", TILE_LIVE);
    return 0;
}

static void segment_cleanup_video_chain(segment_page_ctx_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < 16; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_RGA_GetFrame(SEG_RGA_GRP, &out, 0) != 0) break;
        MEDIA_RGA_ReleaseFrame(SEG_RGA_GRP, out);
    }
    if (ctx->vdec_started) {
        MEDIA_VDEC_Stop(SEG_VDEC_CHN);
        ctx->vdec_started = 0;
    }
    if (ctx->bind_vdec_rga_ok) {
        MEDIA_SYS_UnBind("VDEC", SEG_VDEC_CHN,
                         ctx->vdec_src_port ? ctx->vdec_src_port : "output",
                         "RGA", SEG_RGA_GRP,
                         ctx->rga_in_port ? ctx->rga_in_port : "input0");
        ctx->bind_vdec_rga_ok = 0;
    }
    if (ctx->rga_ok) {
        MEDIA_RGA_Stop(SEG_RGA_GRP);
        MEDIA_RGA_DestroyChn(SEG_RGA_GRP);
        ctx->rga_ok = 0;
    }
    if (ctx->vdec_ok) {
        MEDIA_VDEC_DestroyChn(SEG_VDEC_CHN);
        ctx->vdec_ok = 0;
    }
    if (ctx->rgb_pool_ok) {
        MEDIA_POOL_Destroy(SEG_RGB_POOL);
        ctx->rgb_pool_ok = 0;
    }
    if (ctx->vdec_pool_ok) {
        MEDIA_POOL_Destroy(SEG_VDEC_POOL);
        ctx->vdec_pool_ok = 0;
    }
}

static int segment_feed_until_vcl(segment_page_ctx_t *ctx) {
    if (!ctx) return -1;
    for (int i = 0; i < 12; ++i) {
        int is_vcl = 0;
        if (feed_h264_packet(SEG_VDEC_CHN, &ctx->stream, &is_vcl) != 0) return -1;
        if (is_vcl) return 0;
    }
    return 0;
}

static int segment_poll_video_frame(segment_page_ctx_t *ctx) {
    if (!ctx || !ctx->video_rgb) return -1;
    int got = 0;
    for (;;) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_RGA_GetFrame(SEG_RGA_GRP, &out, 0) != 0) break;
        void *addr = MEDIA_POOL_GetVaddr(out);
        size_t size = MEDIA_POOL_GetSize(out);
        if (addr && size >= SEG_RGB_FRAME_SIZE) {
            (void)MEDIA_POOL_BeginCpuAccess(out, DMA_BUF_SYNC_READ);
            pthread_mutex_lock(&ctx->lock);
            memcpy(ctx->video_rgb, addr, SEG_RGB_FRAME_SIZE);
            ctx->video_ready = 1;
            ctx->video_frame_no++;
            pthread_mutex_unlock(&ctx->lock);
            (void)MEDIA_POOL_EndCpuAccess(out, DMA_BUF_SYNC_READ);
            got = 1;
        }
        MEDIA_RGA_ReleaseFrame(SEG_RGA_GRP, out);
    }
    return got ? 0 : -1;
}

static void draw_segment_page(uint8_t *dst, int stride, int width, int height,
                              int frame, void *opaque) {
    segment_page_ctx_t *ctx = (segment_page_ctx_t *)opaque;
    char line[160];
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 166, 28, 128, 128);
    page_surface_draw_text(dst, stride, width, height, 42, 42,
                           "SEGMENT_NPU PP-LITESEG RKNN", 5, 235, 80, 200);
    page_surface_draw_text(dst, stride, width, height, 42, 114,
                           "H264 VDEC VIDEO  MAIN DISPLAY REALTIME  SEGMENT BRANCH /4", 3, 210, 128, 128);

    page_surface_fill_rect_nv12(dst, stride, width, height, 110, 176, 860, 820, 34, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 110, 1024, 860, 820, 34, 128, 128);
    if (ctx) {
        pthread_mutex_lock(&ctx->lock);
        const uint8_t *src = ctx->video_ready ? ctx->video_rgb : ctx->infer_rgb;
        draw_scaled_rgb(dst, stride, width, height, 155, 198, 770, 770, src, ctx->frame_w, ctx->frame_h);
        if (ctx->result_ready && ctx->mask_class) {
            draw_scaled_segment_overlay(dst, stride, width, height, 155, 1046, 770, 770,
                                        src, ctx->mask_class, ctx->frame_w, ctx->frame_h);
        }
        pthread_mutex_unlock(&ctx->lock);
    }
    page_surface_draw_text(dst, stride, width, height, 130, 184,
                           "INPUT STREET FRAME", 3, 235, 128, 128);
    page_surface_draw_text(dst, stride, width, height, 130, 1032,
                           "SOFT SEGMENT MASK OVERLAY", 3, 235, 128, 128);

    double run_ms = -1.0;
    double post_ms = -1.0;
    double total_ms = -1.0;
    int mask_frame_no = 0;
    int video_frame_no = 0;
    int dropped = 0;
    int busy = 0;
    int class_count = 0;
    if (ctx) {
        pthread_mutex_lock(&ctx->lock);
        run_ms = ctx->run_ms;
        post_ms = ctx->post_ms;
        total_ms = ctx->total_ms;
        mask_frame_no = ctx->mask_frame_no;
        video_frame_no = ctx->video_frame_no;
        dropped = ctx->dropped_requests;
        busy = ctx->worker_busy || ctx->request_pending;
        class_count = ctx->class_count;
        pthread_mutex_unlock(&ctx->lock);
    }
    snprintf(line, sizeof(line), "RKNN RUN %.1fMS  POST %.1fMS  TOTAL %.1fMS  %s",
             run_ms, post_ms, total_ms, busy ? "BUSY" : "IDLE");
    page_surface_draw_text(dst, stride, width, height, 54, 1850, line, 2, 220, 128, 128);
    snprintf(line, sizeof(line), "MODEL PP-LITESEG I8  CLASSES %d  VIDEO F %d  MASK F %d  SEG /%d",
             class_count, video_frame_no, mask_frame_no, SEG_INFER_INTERVAL);
    page_surface_draw_text(dst, stride, width, height, 54, 1886, line, 2, 180, 128, 128);
    snprintf(line, sizeof(line), "LIVE VIDEO NORMAL  MASK CACHE REUSED WHEN NPU IS BUSY  DROP %d", dropped);
    page_surface_draw_text(dst, stride, width, height, 54, 1914, line, 1, 180, 128, 128);
}

static void segment_cleanup(segment_page_ctx_t *ctx) {
    if (!ctx) return;
    segment_stop_worker(ctx);
    segment_cleanup_video_chain(ctx);
    if (ctx->rknn) {
        rknn_destroy(ctx->rknn);
        ctx->rknn = 0;
    }
    unload_h264_stream(&ctx->stream);
    free(ctx->video_rgb);
    ctx->video_rgb = NULL;
    free(ctx->request_rgb);
    ctx->request_rgb = NULL;
    free(ctx->infer_rgb);
    ctx->infer_rgb = NULL;
    ctx->rgb[0] = NULL;
    free(ctx->mask_class);
    ctx->mask_class = NULL;
    free(ctx->mask_work);
    ctx->mask_work = NULL;
    if (ctx->sync_ready) {
        pthread_cond_destroy(&ctx->cond);
        pthread_mutex_destroy(&ctx->lock);
        ctx->sync_ready = 0;
    }
}

int page_segment_npu_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    segment_page_ctx_t ctx;
    memset(&surface, 0, sizeof(surface));
    memset(&ctx, 0, sizeof(ctx));
    pthread_mutex_init(&ctx.lock, NULL);
    pthread_cond_init(&ctx.cond, NULL);
    ctx.sync_ready = 1;

    if (segment_init_model(&ctx) != 0 || segment_load_frames(&ctx) != 0) {
        segment_cleanup(&ctx);
        return 1;
    }
    ctx.current_index = 0;
    ctx.display_index = 0;
    ctx.result_index = 0;
    if (page_surface_open(&surface, SEG_SCREEN_POOL, SEG_SCREEN_W, SEG_SCREEN_H,
                          SEG_SCREEN_STRIDE, SEG_FPS, 4, LICENSE_PATH) != 0) {
        segment_cleanup(&ctx);
        return 1;
    }
    if (segment_setup_video_chain(&ctx) != 0) {
        segment_cleanup(&ctx);
        page_surface_close(&surface);
        return 1;
    }
    if (pthread_create(&ctx.worker, NULL, segment_worker_main, &ctx) != 0) {
        segment_cleanup(&ctx);
        page_surface_close(&surface);
        return 1;
    }
    ctx.worker_started = 1;
    set_tile_status("SEGMENT_NPU", TILE_LIVE);
    printf("SEGMENT_NPU standalone: H264(%s) -> VDEC%d.%s -> RGA%d.%s -> RKNN side branch model=%s. Ctrl+C to stop.\n",
           SEG_H264_PATH, SEG_VDEC_CHN, ctx.vdec_src_port ? ctx.vdec_src_port : "output",
           SEG_RGA_GRP, ctx.rga_in_port ? ctx.rga_in_port : "input0", SEG_MODEL_PATH);

    int frame = 0;
    long long next_frame_us = now_us();
    const long long frame_interval_us = 1000000LL / SEG_FPS;
    while (!running || *running) {
        if (segment_feed_until_vcl(&ctx) != 0) {
            usleep(1000);
        }
        (void)segment_poll_video_frame(&ctx);
        if ((frame % SEG_INFER_INTERVAL) == 0) {
            segment_request_frame(&ctx, 0);
        }
        (void)page_surface_send_frame(&surface, draw_segment_page, &ctx, frame);
        frame++;

        next_frame_us += frame_interval_us;
        long long wait_us = next_frame_us - now_us();
        if (wait_us > 0) {
            usleep((useconds_t)wait_us);
        } else {
            next_frame_us = now_us();
        }
    }

    segment_cleanup(&ctx);
    page_surface_close(&surface);
    return 0;
}
