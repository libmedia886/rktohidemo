#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_overlay.h"
#include "page_surface.h"

#include <dirent.h>
#include <stdio.h>
#include <jpeglib.h>
#include <linux/dma-buf.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CAP_PAGE_W 1080
#define CAP_PAGE_H 1920
#define CAP_PAGE_STRIDE 1088
#define CAP_PAGE_POOL 1
#define CAP_PAGE_FPS 30
#define CAP_GRP 66
#define DCP_GRP 67
#define CAP_INPUT_POOL 6
#define CAP_OUTPUT_POOL 7
#define CAP_W 640
#define CAP_H 640
#define CAP_STRIDE (CAP_W * 3)
#define CAP_FRAME_SIZE (CAP_STRIDE * CAP_H)
#define CAP_MAX_SAMPLES 16
#define CAP_SAMPLE_SECONDS 3
#define CAP_ASSET_DIR "assets/loop/cap_dehaze"
#define CAP_ALIGN_UP_LOCAL(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define CAP_LIVE_SCREEN_W 1080
#define CAP_LIVE_SCREEN_H 1920
#define CAP_LIVE_CAMERA_POOL 2
#define CAP_LIVE_CSC_POOL 10
#define CAP_LIVE_DEHAZE_POOL 11
#define CAP_LIVE_PRE_POOL 12
#define CAP_LIVE_BACK_POOL 13
#define CAP_LIVE_RESIZE_POOL 9
#define CAP_LIVE_OSD_POOL 8
#define CAP_LIVE_CSC_GRP 64
#define CAP_LIVE_BACK_CSC_GRP 75
#define CAP_LIVE_PRE_RESIZE_GRP 62
#define CAP_LIVE_RESIZE_GRP 61
#define CAP_LIVE_OSD_GRP 81
#define CAP_LIVE_SRC_W 1920
#define CAP_LIVE_SRC_H 1080
#define CAP_LIVE_SRC_STRIDE 3840
#define CAP_LIVE_CPU_SRC_STRIDE 1920
#define CAP_LIVE_CPU_BUF_H 1088
#define CAP_LIVE_CPU_SRC_SIZE ((size_t)CAP_LIVE_CPU_SRC_STRIDE * (size_t)CAP_LIVE_CPU_BUF_H * 3u / 2u)
#define CAP_LIVE_RGB_STRIDE CAP_ALIGN_UP_LOCAL(CAP_LIVE_SRC_W * 3, 64)
#define CAP_LIVE_PROC_W 640
#define CAP_LIVE_PROC_H 640
#define CAP_LIVE_PROC_STRIDE 640
#define CAP_LIVE_PROC_SIZE ((size_t)CAP_LIVE_PROC_STRIDE * (size_t)CAP_LIVE_PROC_H * 3u / 2u)
#define CAP_LIVE_DCP_PROC_W CAP_LIVE_PROC_W
#define CAP_LIVE_DCP_PROC_H CAP_LIVE_PROC_H
#define CAP_LIVE_DCP_PROC_STRIDE CAP_LIVE_PROC_STRIDE
#define CAP_LIVE_DCP_PROC_SIZE CAP_LIVE_PROC_SIZE
#define CAP_LIVE_PROC_NV12_STRIDE CAP_ALIGN_UP_LOCAL(CAP_LIVE_SRC_W, 64)
#define CAP_LIVE_VIEW_W 1080
#define CAP_LIVE_VIEW_H 608
#define CAP_LIVE_VIEW_STRIDE CAP_ALIGN_UP_LOCAL(CAP_LIVE_VIEW_W, 64)
#define CAP_LIVE_VIEW_RGB_STRIDE CAP_ALIGN_UP_LOCAL(CAP_LIVE_VIEW_W * 3, 64)
#define CAP_LIVE_VIEW_X 0
#define CAP_LIVE_VIEW_Y 320
#define CAP_LIVE_FPS 30
#define CAP_LIVE_DCP_FPS 20
#define CAP_LIVE_CAMERA_DEVICE "/dev/video-camera0"
#define CAP_LIVE_SRC_SIZE ((size_t)CAP_LIVE_SRC_STRIDE * (size_t)CAP_LIVE_SRC_H * 3u / 2u)
#define CAP_LIVE_RGB_SIZE ((size_t)CAP_LIVE_RGB_STRIDE * (size_t)CAP_LIVE_SRC_H)
#define CAP_LIVE_BACK_SIZE ((size_t)CAP_LIVE_PROC_NV12_STRIDE * (size_t)CAP_LIVE_SRC_H * 3u / 2u)
#define CAP_LIVE_VIEW_SIZE ((size_t)CAP_LIVE_VIEW_STRIDE * (size_t)CAP_LIVE_VIEW_H * 3u / 2u)
#define CAP_LIVE_VIEW_RGB_SIZE ((size_t)CAP_LIVE_VIEW_RGB_STRIDE * (size_t)CAP_LIVE_VIEW_H)
#define CAP_LIVE_TEXT_MASK_W 1024
#define CAP_LIVE_TEXT_MASK_H 64
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} image_t;

typedef struct {
    char paths[CAP_MAX_SAMPLES][PATH_MAX];
    int count;
    int current_index;
    char current_name[128];
    uint8_t *input;
    uint8_t *output;
    int processed;
    int module_ok;
    const char *page_name;
    const char *title;
    int use_dcp;
    int live_input;
    int vi_frames;
    int passthrough;
    char perf_line[128];
} cap_page_ctx_t;

typedef struct {
    int sys_ok;
    int camera_pool_ok;
    int pre_pool_ok;
    int csc_pool_ok;
    int dehaze_pool_ok;
    int back_pool_ok;
    int resize_pool_ok;
    int osd_pool_ok;
    int vi_attr_ok;
    int pre_resize_ok;
    int csc_ok;
    int dehaze_ok;
    int back_csc_ok;
    int resize_ok;
    int osd_ok;
    int vo_ok;
    int bind_vi_csc_ok;
    int bind_vi_pre_ok;
    int bind_pre_csc_ok;
    int bind_csc_dehaze_ok;
    int bind_dehaze_resize_ok;
    int bind_dehaze_back_ok;
    int bind_back_resize_ok;
    int bind_resize_osd_ok;
    int bind_osd_vo_ok;
    int vi_enabled;
    int use_dcp;
    int passthrough;
} dehaze_live_chain_t;

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void rgb_to_yuv(uint8_t r, uint8_t g, uint8_t b,
                       uint8_t *yy, uint8_t *uu, uint8_t *vv) {
    int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
    int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
    *yy = clamp_u8(y);
    *uu = clamp_u8(u);
    *vv = clamp_u8(v);
}

static void yuv_to_rgb(uint8_t yy, uint8_t uu, uint8_t vv,
                       uint8_t *r, uint8_t *g, uint8_t *b) {
    int c = (int)yy - 16;
    int d = (int)uu - 128;
    int e = (int)vv - 128;
    if (c < 0) c = 0;
    *r = clamp_u8((298 * c + 409 * e + 128) >> 8);
    *g = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    *b = clamp_u8((298 * c + 516 * d + 128) >> 8);
}

static void nv12_to_rgb_scaled(const uint8_t *src, int src_w, int src_h,
                               int src_stride, uint8_t *dst) {
    if (!src || !dst || src_w <= 0 || src_h <= 0 || src_stride < src_w) return;
    const uint8_t *y_plane = src;
    const uint8_t *uv_plane = src + (size_t)src_stride * (size_t)src_h;
    for (int y = 0; y < CAP_H; ++y) {
        int sy = (int)(((int64_t)y * src_h) / CAP_H);
        const uint8_t *y_row = y_plane + (size_t)sy * src_stride;
        const uint8_t *uv_row = uv_plane + (size_t)(sy / 2) * src_stride;
        uint8_t *rgb_row = dst + (size_t)y * CAP_STRIDE;
        for (int x = 0; x < CAP_W; ++x) {
            int sx = (int)(((int64_t)x * src_w) / CAP_W);
            int uvx = sx & ~1;
            yuv_to_rgb(y_row[sx], uv_row[uvx], uv_row[uvx + 1],
                       &rgb_row[x * 3], &rgb_row[x * 3 + 1], &rgb_row[x * 3 + 2]);
        }
    }
}

static int load_jpeg_rgb(const char *path, image_t *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return -1;
    }
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    int width = (int)cinfo.output_width;
    int height = (int)cinfo.output_height;
    int channels = (int)cinfo.output_components;
    uint8_t *rgb = malloc((size_t)width * (size_t)height * 3);
    uint8_t *row = malloc((size_t)width * (size_t)channels);
    if (!rgb || !row || width <= 0 || height <= 0 || channels < 3) {
        free(rgb);
        free(row);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return -1;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW rows[1] = {row};
        JDIMENSION y = cinfo.output_scanline;
        jpeg_read_scanlines(&cinfo, rows, 1);
        memcpy(rgb + (size_t)y * (size_t)width * 3, row, (size_t)width * 3);
    }
    free(row);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    out->width = width;
    out->height = height;
    out->rgb = rgb;
    return 0;
}

static void free_image(image_t *img) {
    if (!img) return;
    free(img->rgb);
    memset(img, 0, sizeof(*img));
}

static int path_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static int scan_samples(cap_page_ctx_t *ctx) {
    if (!ctx) return 0;
    DIR *dir = opendir(CAP_ASSET_DIR);
    struct dirent *ent = NULL;
    ctx->count = 0;
    ctx->current_index = -1;
    ctx->current_name[0] = '\0';
    if (!dir) return 0;

    while ((ent = readdir(dir)) != NULL && ctx->count < CAP_MAX_SAMPLES) {
        char path[PATH_MAX];
        struct stat st;
        const char *ext = strrchr(ent->d_name, '.');
        if (ent->d_name[0] == '.' || !ext ||
            (strcasecmp(ext, ".jpg") != 0 && strcasecmp(ext, ".jpeg") != 0)) {
            continue;
        }
        if (snprintf(path, sizeof(path), "%s/%s", CAP_ASSET_DIR, ent->d_name) >=
            (int)sizeof(path)) {
            continue;
        }
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        snprintf(ctx->paths[ctx->count], sizeof(ctx->paths[ctx->count]), "%s", path);
        ctx->count++;
    }
    closedir(dir);
    if (ctx->count > 1) {
        qsort(ctx->paths, (size_t)ctx->count, sizeof(ctx->paths[0]), path_cmp);
    }
    return ctx->count;
}

static void image_to_rgb_frame(const image_t *img, uint8_t *dst) {
    if (!img || !img->rgb || !dst || img->width <= 0 || img->height <= 0) return;
    memset(dst, 0, CAP_FRAME_SIZE);

    int out_w = CAP_W;
    int out_h = (int)((int64_t)img->height * CAP_W / img->width);
    if (out_h > CAP_H) {
        out_h = CAP_H;
        out_w = (int)((int64_t)img->width * CAP_H / img->height);
    }
    if (out_w <= 0 || out_h <= 0) return;
    int ox = (CAP_W - out_w) / 2;
    int oy = (CAP_H - out_h) / 2;

    for (int y = 0; y < out_h; ++y) {
        int sy = y * img->height / out_h;
        uint8_t *drow = dst + ((size_t)(oy + y) * CAP_W + ox) * 3;
        for (int x = 0; x < out_w; ++x) {
            int sx = x * img->width / out_w;
            const uint8_t *src = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t *dp = drow + x * 3;
            dp[0] = src[0];
            dp[1] = src[1];
            dp[2] = src[2];
        }
    }
}

static int load_sample_rgb(cap_page_ctx_t *ctx, int index) {
    if (!ctx || !ctx->input || index < 0 || index >= ctx->count) return -1;
    image_t img;
    if (load_jpeg_rgb(ctx->paths[index], &img) != 0) return -1;
    image_to_rgb_frame(&img, ctx->input);
    const char *base = strrchr(ctx->paths[index], '/');
    snprintf(ctx->current_name, sizeof(ctx->current_name), "%s", base ? base + 1 : ctx->paths[index]);
    ctx->current_index = index;
    free_image(&img);
    return 0;
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

static int setup_dehaze_module(int use_dcp) {
    if (MEDIA_POOL_Create(CAP_INPUT_POOL, CAP_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(CAP_OUTPUT_POOL, CAP_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(CAP_INPUT_POOL);
        return -1;
    }

    if (use_dcp) {
        MEDIA_DCP_FAST_DEHAZE_ATTR attr = {0};
        attr.width = CAP_W;
        attr.height = CAP_H;
        attr.format = MEDIA_FORMAT_RGB888;
        attr.input_depth = 3;
        attr.output_pool_id = CAP_OUTPUT_POOL;
        attr.input_stride = CAP_STRIDE;
        attr.output_stride = CAP_STRIDE;
        attr.patch = 15;
        attr.omega = 0.95f;
        attr.t0 = 0.12f;
        attr.airlight_percent = 0.001f;
        attr.guided_radius = 24;
        attr.guided_eps = 1e-3f;
        attr.refine_scale = 0.5f;
        if (MEDIA_DCP_FAST_DEHAZE_CreateGrp(DCP_GRP, &attr) != 0 ||
            MEDIA_DCP_FAST_DEHAZE_Start(DCP_GRP) != 0 ||
            MEDIA_DCP_FAST_DEHAZE_Enable(DCP_GRP) != 0) {
            MEDIA_DCP_FAST_DEHAZE_DestroyGrp(DCP_GRP);
            MEDIA_POOL_Destroy(CAP_OUTPUT_POOL);
            MEDIA_POOL_Destroy(CAP_INPUT_POOL);
            return -1;
        }
    } else {
        MEDIA_CAP_DEHAZE_ATTR attr = {0};
        attr.width = CAP_W;
        attr.height = CAP_H;
        attr.format = MEDIA_FORMAT_RGB888;
        attr.input_depth = 3;
        attr.output_pool_id = CAP_OUTPUT_POOL;
        attr.input_stride = CAP_STRIDE;
        attr.output_stride = CAP_STRIDE;
        attr.guided_radius = 24;
        attr.guided_eps = 1e-3f;
        attr.t0 = 0.12f;
        attr.beta0 = 0.121779f;
        attr.beta1 = 0.959710f;
        attr.beta2 = -0.780245f;
        attr.depth_scale = 1.0f;
        attr.refine_scale = 0.25f;
        if (MEDIA_CAP_DEHAZE_CreateGrp(CAP_GRP, &attr) != 0 ||
            MEDIA_CAP_DEHAZE_Start(CAP_GRP) != 0 ||
            MEDIA_CAP_DEHAZE_Enable(CAP_GRP) != 0) {
            MEDIA_CAP_DEHAZE_DestroyGrp(CAP_GRP);
            MEDIA_POOL_Destroy(CAP_OUTPUT_POOL);
            MEDIA_POOL_Destroy(CAP_INPUT_POOL);
            return -1;
        }
    }
    return 0;
}

static void cleanup_dehaze_module(int enabled, int use_dcp) {
    if (!enabled) return;
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (use_dcp) {
            if (MEDIA_DCP_FAST_DEHAZE_GetFrame(DCP_GRP, &out, 0) != 0) break;
            MEDIA_DCP_FAST_DEHAZE_ReleaseFrame(DCP_GRP, out);
        } else {
            if (MEDIA_CAP_DEHAZE_GetFrame(CAP_GRP, &out, 0) != 0) break;
            MEDIA_CAP_DEHAZE_ReleaseFrame(CAP_GRP, out);
        }
    }
    if (use_dcp) {
        MEDIA_DCP_FAST_DEHAZE_Disable(DCP_GRP);
        MEDIA_DCP_FAST_DEHAZE_Stop(DCP_GRP);
        MEDIA_DCP_FAST_DEHAZE_DestroyGrp(DCP_GRP);
    } else {
        MEDIA_CAP_DEHAZE_Disable(CAP_GRP);
        MEDIA_CAP_DEHAZE_Stop(CAP_GRP);
        MEDIA_CAP_DEHAZE_DestroyGrp(CAP_GRP);
    }
    MEDIA_POOL_Destroy(CAP_OUTPUT_POOL);
    MEDIA_POOL_Destroy(CAP_INPUT_POOL);
}

static int process_dehaze(cap_page_ctx_t *ctx) {
    if (!ctx || !ctx->input || !ctx->output || !ctx->module_ok) return -1;
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    if (MEDIA_POOL_GetBuffer(CAP_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, ctx->input, CAP_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (ctx->use_dcp) {
        if (MEDIA_DCP_FAST_DEHAZE_SendFrame(DCP_GRP, in, 20) != 0) {
            MEDIA_POOL_PutBuffer(in);
            return -1;
        }
        if (MEDIA_DCP_FAST_DEHAZE_GetFrame(DCP_GRP, &out, 1000) != 0) return -1;
        int ret = copy_from_buffer(out, ctx->output, CAP_FRAME_SIZE);
        MEDIA_DCP_FAST_DEHAZE_ReleaseFrame(DCP_GRP, out);
        if (ret == 0) ctx->processed++;
        return ret;
    }
    if (MEDIA_CAP_DEHAZE_SendFrame(CAP_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_CAP_DEHAZE_GetFrame(CAP_GRP, &out, 1000) != 0) return -1;
    int ret = copy_from_buffer(out, ctx->output, CAP_FRAME_SIZE);
    MEDIA_CAP_DEHAZE_ReleaseFrame(CAP_GRP, out);
    if (ret == 0) ctx->processed++;
    return ret;
}

static void draw_rgb_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                            int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    uint8_t *duv = dst + (size_t)dstride * dheight;
    for (int y = 0; y < dh; ++y) {
        int sy = y * CAP_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        for (int x = 0; x < dw; ++x) {
            int sx = x * CAP_W / dw;
            const uint8_t *rgb = src + ((size_t)sy * CAP_W + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)uu;
            (void)vv;
            drow[x] = yy;
        }
    }
    for (int y = 0; y < dh; y += 2) {
        int sy = y * CAP_H / dh;
        uint8_t *drow = duv + (size_t)((dy + y) / 2) * dstride + (dx & ~1);
        for (int x = 0; x < dw; x += 2) {
            int sx = x * CAP_W / dw;
            const uint8_t *rgb = src + ((size_t)sy * CAP_W + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)yy;
            drow[x] = uu;
            drow[x + 1] = vv;
        }
    }
}

static void update_cap_perf_line(cap_page_ctx_t *ctx) {
    page_overlay_perf_t perf = {0};
    char gpu_text[24];
    char rga_text[24];
    if (!ctx) return;
    page_overlay_update_perf(&perf);
    if (perf.gpu_available) {
        snprintf(gpu_text, sizeof(gpu_text), "%.0f", perf.gpu_percent);
    } else {
        snprintf(gpu_text, sizeof(gpu_text), "NA");
    }
    if (perf.rga_available) {
        snprintf(rga_text, sizeof(rga_text), "%.0f", perf.rga_percent);
    } else {
        snprintf(rga_text, sizeof(rga_text), "NA");
    }
    snprintf(ctx->perf_line, sizeof(ctx->perf_line),
             "CPU %.0f GPU %s RGA %s", perf.cpu_percent, gpu_text, rga_text);
}

static void draw_cap_page(uint8_t *dst, int stride, int width, int height,
                          int frame, void *opaque) {
    cap_page_ctx_t *ctx = (cap_page_ctx_t *)opaque;
    int sample = ctx && ctx->count > 0 ?
        (frame / (CAP_PAGE_FPS * CAP_SAMPLE_SECONDS)) % ctx->count : -1;
    if (ctx && sample >= 0 && sample != ctx->current_index) {
        if (load_sample_rgb(ctx, sample) == 0) {
            if (!ctx->module_ok || process_dehaze(ctx) != 0) {
                memcpy(ctx->output, ctx->input, CAP_FRAME_SIZE);
            }
        }
    }

    char sample_text[96];
    char processed[48];
    const int live = ctx && ctx->live_input;
    if (live) {
        snprintf(sample_text, sizeof(sample_text), "VI LIVE %06d",
                 ctx ? ctx->vi_frames : 0);
        snprintf(processed, sizeof(processed), "%s %06d VI %06d",
                 ctx && ctx->use_dcp ? "DCP" : "CAP",
                 ctx ? ctx->processed : 0,
                 ctx ? ctx->vi_frames : 0);
    } else {
        snprintf(sample_text, sizeof(sample_text), "SAMPLE %02d/%02d",
                 ctx ? ctx->current_index + 1 : 0, ctx ? ctx->count : 0);
        snprintf(processed, sizeof(processed), "%s %06d",
                 ctx && ctx->use_dcp ? "DCP" : "CAP", ctx ? ctx->processed : 0);
    }

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           ctx && ctx->title ? ctx->title : "CAP DEHAZE", 6, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 60, 142,
                           sample_text, 3, 210, 144, 84);

    if (live) {
        const uint8_t *display = ctx && ctx->passthrough ? ctx->input : ctx->output;
        const char *mode = ctx && ctx->passthrough ? "PASS ORIGINAL" : "ENHANCE OUTPUT";
        page_surface_draw_text(dst, stride, width, height, 60, CAP_LIVE_VIEW_Y - 48,
                               mode, 2, 190, 230, 255);
        if (display) {
            draw_rgb_scaled(dst, stride, width, height,
                            CAP_LIVE_VIEW_X, CAP_LIVE_VIEW_Y,
                            CAP_LIVE_VIEW_W, CAP_LIVE_VIEW_H, display);
        }
        page_surface_fill_rect_nv12(dst, stride, width, height,
                                    34, 1510, 1012, 286, 10, 128, 128);
        page_surface_draw_text(dst, stride, width, height, 66, 1536,
                               ctx && ctx->use_dcp ?
                               "DCP_FAST_DEHAZE : 1920X1080 LIVE PASS/ENHANCE" :
                               "CAP_DEHAZE : 1920X1080 LIVE PASS/ENHANCE",
                               3, 160, 255, 220);
        page_surface_draw_text(dst, stride, width, height, 68, 1588,
                               "FLOW VI 1920X1080 -> RGB -> DEHAZE -> 1080X608 DISPLAY",
                               2, 190, 230, 255);
        page_surface_draw_text(dst, stride, width, height, 68, 1640,
                               processed, 2, 170, 255, 220);
        page_surface_draw_text(dst, stride, width, height, 68, 1692,
                               (ctx && ctx->perf_line[0]) ? ctx->perf_line :
                               (ctx && ctx->module_ok ? "MODULE LIVE" : "MODULE FALLBACK"),
                               2, 255, 230, 120);
        page_surface_draw_text(dst, stride, width, height, 68, 1744,
                               mode, 2, 170, 205, 235);
        return;
    }

    int pane = 600;
    int x = (width - pane) / 2;
    int top_y = 246;
    int bottom_y = 930;
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, top_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, x - 14, bottom_y - 48,
                                pane + 28, pane + 82, 16, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, top_y - 34,
                           live ? "VI INPUT" : "HAZY INPUT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, x, bottom_y - 34,
                           ctx && ctx->use_dcp ? "DCP OUTPUT" : "CAP OUTPUT",
                           3, 220, 108, 176);
    if (ctx && ctx->input) draw_rgb_scaled(dst, stride, width, height, x, top_y, pane, pane, ctx->input);
    if (ctx && ctx->output) draw_rgb_scaled(dst, stride, width, height, x, bottom_y, pane, pane, ctx->output);

    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           live ? "FLOW VI NV12 TO RGB TO DEHAZE TO VO" :
                           (ctx && ctx->use_dcp ?
                           "PATCH15 OMEGA095 T0 012 REFINE 050 RGB888" :
                           "GUIDED R24 T0 012 REFINE 025 RGB888"),
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1725,
                           (ctx && ctx->perf_line[0]) ? ctx->perf_line :
                           (ctx && ctx->module_ok ? "MODULE LIVE" : "MODULE FALLBACK"),
                           3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1810,
                           live ? (ctx && ctx->passthrough ? "LIVE CAMERA PASS MODE" :
                                   "LIVE CAMERA ENHANCE MODE") :
                           "STATIC LOW VISIBILITY SAMPLE",
                           2, 190, 144, 84);
}

static int run_cap_dehaze_page(volatile sig_atomic_t *running,
                               const char *page_name,
                               const char *title,
                               int use_dcp) {
    page_surface_t surface;
    cap_page_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_name = page_name;
    ctx.title = title;
    ctx.use_dcp = use_dcp;
    ctx.input = malloc(CAP_FRAME_SIZE);
    ctx.output = malloc(CAP_FRAME_SIZE);
    if (!ctx.input || !ctx.output || scan_samples(&ctx) <= 0) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }
    memset(ctx.input, 0, CAP_FRAME_SIZE);
    memset(ctx.output, 0, CAP_FRAME_SIZE);

    if (page_surface_open(&surface, CAP_PAGE_POOL, CAP_PAGE_W, CAP_PAGE_H,
                          CAP_PAGE_STRIDE, CAP_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }

    ctx.module_ok = setup_dehaze_module(use_dcp) == 0;
    if (ctx.module_ok) set_tile_status(page_name, TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("%s standalone page module=%s samples=%d. Ctrl+C to stop.\n",
           page_name, ctx.module_ok ? "live" : "fallback", ctx.count);

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_cap_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % CAP_PAGE_FPS) == 0) {
                printf("%s frames=%d sample=%d/%d file=%s %s_frames=%d t0=0.12 refine_scale=%.2f standalone=1\n",
                       page_name, frame, ctx.current_index + 1, ctx.count,
                       ctx.current_name[0] ? ctx.current_name : "waiting",
                       use_dcp ? "dcp" : "cap", ctx.processed,
                       use_dcp ? 0.50 : 0.25);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / CAP_PAGE_FPS);
    }

    cleanup_dehaze_module(ctx.module_ok, use_dcp);
    page_surface_close(&surface);
    free(ctx.input);
    free(ctx.output);
    return 0;
}

static const char *dehaze_module_name(int use_dcp);

static int setup_live_vi_capture(void) {
    MEDIA_VI_ATTR vi = {0};
    if (MEDIA_POOL_Create(CAP_LIVE_CAMERA_POOL, CAP_LIVE_CPU_SRC_SIZE, 6) != 0) {
        return -1;
    }
    vi.device = CAP_LIVE_CAMERA_DEVICE;
    vi.width = CAP_LIVE_SRC_W;
    vi.height = CAP_LIVE_SRC_H;
    vi.stride = 0;
    vi.fps = CAP_LIVE_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = CAP_LIVE_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0 || MEDIA_VI_Enable(0) != 0) {
        MEDIA_POOL_Destroy(CAP_LIVE_CAMERA_POOL);
        return -1;
    }
    set_tile_status("VI", TILE_LIVE);
    return 0;
}

static void cleanup_live_vi_capture(int enabled) {
    if (!enabled) return;
    MEDIA_VI_Disable(0);
    usleep(50000);
    /*
     * VI can still own one queued camera buffer immediately after Disable.
     * Let MEDIA_SYS_Exit tear the pool down after VI module cleanup.
     */
}

static int capture_live_vi_rgb(uint8_t *dst) {
    MEDIA_BUFFER cbuf = {-1, -1};
    void *addr = NULL;
    size_t size = 0;
    int mapped = 0;
    int ret = -1;
    if (!dst) return -1;
    if (MEDIA_VI_GetFrame(0, &cbuf, 30) != 0) return -1;
    if (MEDIA_POOL_BeginCpuAccess(cbuf, DMA_BUF_SYNC_READ) == 0) {
        size = MEDIA_POOL_GetSize(cbuf);
        addr = MEDIA_POOL_GetVaddr(cbuf);
        if (!addr && map_buffer(cbuf, &addr, &size, PROT_READ) == 0) {
            mapped = 1;
        }
        if (addr && (size == 0 || size >= CAP_LIVE_CPU_SRC_SIZE)) {
            nv12_to_rgb_scaled((const uint8_t *)addr,
                               CAP_LIVE_SRC_W, CAP_LIVE_SRC_H,
                               CAP_LIVE_CPU_SRC_STRIDE, dst);
            ret = 0;
        }
        if (mapped) {
            munmap(addr, size);
        }
        (void)MEDIA_POOL_EndCpuAccess(cbuf, DMA_BUF_SYNC_READ);
    }
    MEDIA_VI_ReleaseFrame(0, cbuf);
    return ret;
}

static int run_dehaze_live_cpu_page(volatile sig_atomic_t *running, int use_dcp) {
    page_surface_t surface;
    cap_page_ctx_t ctx;
    int vi_ok = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_name = dehaze_module_name(use_dcp);
    ctx.title = use_dcp ? "DCP FAST DEHAZE LIVE" : "CAP DEHAZE LIVE";
    ctx.use_dcp = use_dcp;
    ctx.live_input = 1;
    ctx.current_index = 0;
    ctx.count = 1;
    ctx.input = malloc(CAP_FRAME_SIZE);
    ctx.output = malloc(CAP_FRAME_SIZE);
    if (!ctx.input || !ctx.output) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }
    memset(ctx.input, 0, CAP_FRAME_SIZE);
    memset(ctx.output, 0, CAP_FRAME_SIZE);
    snprintf(ctx.current_name, sizeof(ctx.current_name), "%s", CAP_LIVE_CAMERA_DEVICE);
    update_cap_perf_line(&ctx);

    if (page_surface_open(&surface, CAP_PAGE_POOL, CAP_PAGE_W, CAP_PAGE_H,
                          CAP_PAGE_STRIDE, CAP_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }
    vi_ok = setup_live_vi_capture() == 0;
    ctx.module_ok = setup_dehaze_module(use_dcp) == 0;
    if (!vi_ok || !ctx.module_ok) {
        fprintf(stderr, "%s live CPU page setup failed vi=%d module=%d\n",
                dehaze_module_name(use_dcp), vi_ok, ctx.module_ok);
        cleanup_dehaze_module(ctx.module_ok, use_dcp);
        cleanup_live_vi_capture(vi_ok);
        page_surface_close(&surface);
        free(ctx.input);
        free(ctx.output);
        return 1;
    }
    set_tile_status(dehaze_module_name(use_dcp), TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("%s standalone: VI %s %dx%d NV12 -> CPU RGB %dx%d -> DEHAZE -> CPU page compare -> VO. Ctrl+C to stop.\n",
           dehaze_module_name(use_dcp), CAP_LIVE_CAMERA_DEVICE,
           CAP_LIVE_SRC_W, CAP_LIVE_SRC_H, CAP_W, CAP_H);

    int frame = 0;
    while (!running || *running) {
        if (capture_live_vi_rgb(ctx.input) == 0) {
            ctx.vi_frames++;
            ctx.passthrough = ((frame / CAP_PAGE_FPS) % 2) == 0;
            if (use_dcp) {
                (void)MEDIA_DCP_FAST_DEHAZE_SetPassthrough(DCP_GRP, ctx.passthrough);
            } else {
                (void)MEDIA_CAP_DEHAZE_SetPassthrough(CAP_GRP, ctx.passthrough);
            }
            if (process_dehaze(&ctx) != 0) {
                memcpy(ctx.output, ctx.input, CAP_FRAME_SIZE);
            }
        }
        if ((frame % 15) == 0) update_cap_perf_line(&ctx);
        if (page_surface_send_frame(&surface, draw_cap_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % CAP_PAGE_FPS) == 0) {
                printf("%s vi_frames=%d %s_frames=%d passthrough=%d %s standalone=1\n",
                       dehaze_module_name(use_dcp),
                       ctx.vi_frames,
                       use_dcp ? "dcp" : "cap",
                       ctx.processed,
                       ctx.passthrough,
                       ctx.perf_line[0] ? ctx.perf_line : "perf NA");
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / CAP_PAGE_FPS);
    }

    cleanup_dehaze_module(ctx.module_ok, use_dcp);
    cleanup_live_vi_capture(vi_ok);
    page_surface_close(&surface);
    free(ctx.input);
    free(ctx.output);
    return 0;
}

static int dehaze_grp_id(int use_dcp) {
    return use_dcp ? DCP_GRP : CAP_GRP;
}

static const char *dehaze_module_name(int use_dcp) {
    return use_dcp ? "DCP_FAST_DEHAZE" : "CAP_DEHAZE";
}

static void drain_live_csc_output(int grp) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_CSC_RGA_GetFrame(grp, &out, 0) != 0) break;
            MEDIA_CSC_RGA_ReleaseFrame(grp, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_live_dehaze_output(int use_dcp) {
    const int grp = dehaze_grp_id(use_dcp);
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (use_dcp) {
                if (MEDIA_DCP_FAST_DEHAZE_GetFrame(grp, &out, 0) != 0) break;
                MEDIA_DCP_FAST_DEHAZE_ReleaseFrame(grp, out);
            } else {
                if (MEDIA_CAP_DEHAZE_GetFrame(grp, &out, 0) != 0) break;
                MEDIA_CAP_DEHAZE_ReleaseFrame(grp, out);
            }
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_live_resize_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_RESIZE_RGA_GetFrame(CAP_LIVE_RESIZE_GRP, &out, 0) != 0) break;
            MEDIA_RESIZE_RGA_ReleaseFrame(CAP_LIVE_RESIZE_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_live_pre_resize_output(void) {
    for (int pass = 0; pass < 3; ++pass) {
        int drained = 0;
        for (int i = 0; i < 8; ++i) {
            MEDIA_BUFFER out = {-1, -1};
            if (MEDIA_RESIZE_RGA_GetFrame(CAP_LIVE_PRE_RESIZE_GRP, &out, 0) != 0) break;
            MEDIA_RESIZE_RGA_ReleaseFrame(CAP_LIVE_PRE_RESIZE_GRP, out);
            drained = 1;
        }
        if (!drained) break;
        usleep(1000);
    }
}

static void drain_live_osd_output(void) {
    for (int i = 0; i < 8; ++i) {
        MEDIA_BUFFER out = {-1, -1};
        if (MEDIA_OSD_GetFrame(CAP_LIVE_OSD_GRP, &out, 0) != 0) break;
        MEDIA_OSD_ReleaseFrame(CAP_LIVE_OSD_GRP, out);
    }
}

static int update_dehaze_live_overlay(const dehaze_live_chain_t *chain,
                                      uint64_t vi_count,
                                      uint64_t csc_count,
                                      uint64_t dehaze_count,
                                      uint64_t resize_count,
                                      uint64_t osd_count,
                                      uint64_t vo_count) {
    static uint8_t masks[5][CAP_LIVE_TEXT_MASK_W * CAP_LIVE_TEXT_MASK_H];
    page_overlay_perf_t perf = {0};
    char perf_line[128];
    char title_line[128];
    int use_dcp = chain ? chain->use_dcp : 0;
    const char *flow_line =
        "FLOW VI->RESIZE_RGA->CSC_RGA->DEHAZE->RESIZE_RGA->OSD->VO";
    char mode_line[160];
    char count_line[180];
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_line, sizeof(perf_line));
    snprintf(title_line, sizeof(title_line), "%s LIVE VI DEHAZE",
             use_dcp ? "DCP_FAST_DEHAZE" : "CAP_DEHAZE");
    if (use_dcp) {
        snprintf(mode_line, sizeof(mode_line),
                 "MODE %s  FORMAT NV12->RGB->NV12  PROC %dx%d  PATCH15 OMEGA0.95",
                 chain && chain->passthrough ? "PASS" : "ENHANCE",
                 CAP_LIVE_DCP_PROC_W, CAP_LIVE_DCP_PROC_H);
    } else {
        snprintf(mode_line, sizeof(mode_line),
                 "MODE %s  FORMAT NV12->RGB->NV12  PROC %dx%d  R24 T0 0.12",
                 chain && chain->passthrough ? "PASS" : "ENHANCE",
                 CAP_LIVE_SRC_W, CAP_LIVE_SRC_H);
    }
    snprintf(count_line, sizeof(count_line), "VI %llu CSC %llu DEHAZE %llu RGA %llu OSD %llu VO %llu",
             (unsigned long long)vi_count,
             (unsigned long long)csc_count,
             (unsigned long long)dehaze_count,
             (unsigned long long)resize_count,
             (unsigned long long)osd_count,
             (unsigned long long)vo_count);

    if (page_overlay_set_text(CAP_LIVE_OSD_GRP, 0, 24, 16, 2,
                              title_line,
                              masks[0], sizeof(masks[0]), CAP_LIVE_TEXT_MASK_W, CAP_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CAP_LIVE_OSD_GRP, 1, 24, 502, 2,
                              flow_line,
                              masks[1], sizeof(masks[1]), CAP_LIVE_TEXT_MASK_W, CAP_LIVE_TEXT_MASK_H,
                              190, 230, 255, 255) != 0) return -1;
    if (page_overlay_set_text(CAP_LIVE_OSD_GRP, 2, 24, 528, 2,
                              perf_line,
                              masks[2], sizeof(masks[2]), CAP_LIVE_TEXT_MASK_W, CAP_LIVE_TEXT_MASK_H,
                              255, 230, 120, 255) != 0) return -1;
    if (page_overlay_set_text(CAP_LIVE_OSD_GRP, 3, 24, 554, 2,
                              mode_line,
                              masks[3], sizeof(masks[3]), CAP_LIVE_TEXT_MASK_W, CAP_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    if (page_overlay_set_text(CAP_LIVE_OSD_GRP, 4, 24, 580, 2,
                              count_line,
                              masks[4], sizeof(masks[4]), CAP_LIVE_TEXT_MASK_W, CAP_LIVE_TEXT_MASK_H,
                              160, 255, 220, 255) != 0) return -1;
    return 0;
}

static int setup_dehaze_live_chain(dehaze_live_chain_t *chain, int use_dcp) {
    MEDIA_VI_ATTR vi = {0};
    MEDIA_CSC_RGA_ATTR csc = {0};
    MEDIA_RESIZE_RGA_ATTR pre = {0};
    MEDIA_RESIZE_RGA_ATTR resize = {0};
    MEDIA_OSD_ATTR osd = {0};
    MEDIA_VO_ATTR vo = {0};
    const int grp = dehaze_grp_id(use_dcp);
    const int proc_w = CAP_LIVE_PROC_W;
    const int proc_h = CAP_LIVE_PROC_H;
    const int proc_nv12_stride = CAP_LIVE_PROC_STRIDE;
    const int proc_rgb_stride = CAP_ALIGN_UP_LOCAL(proc_w * 3, 64);
    if (!chain) return -1;
    chain->use_dcp = use_dcp;
    chain->passthrough = 1;

    if (MEDIA_SYS_Init() != 0) return -1;
    chain->sys_ok = 1;
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (MEDIA_POOL_Create(CAP_LIVE_CAMERA_POOL, CAP_LIVE_SRC_SIZE, 6) != 0) return -1;
    chain->camera_pool_ok = 1;
    if (MEDIA_POOL_Create(CAP_LIVE_PRE_POOL, CAP_LIVE_PROC_SIZE, 4) != 0) return -1;
    chain->pre_pool_ok = 1;
    if (MEDIA_POOL_Create(CAP_LIVE_CSC_POOL, CAP_LIVE_RGB_SIZE, 3) != 0) return -1;
    chain->csc_pool_ok = 1;
    if (MEDIA_POOL_Create(CAP_LIVE_DEHAZE_POOL, CAP_LIVE_RGB_SIZE, 3) != 0) return -1;
    chain->dehaze_pool_ok = 1;
    if (MEDIA_POOL_Create(CAP_LIVE_RESIZE_POOL, CAP_LIVE_VIEW_RGB_SIZE, 6) != 0) return -1;
    chain->resize_pool_ok = 1;
    if (MEDIA_POOL_Create(CAP_LIVE_OSD_POOL, CAP_LIVE_VIEW_RGB_SIZE, 4) != 0) return -1;
    chain->osd_pool_ok = 1;

    vi.device = CAP_LIVE_CAMERA_DEVICE;
    vi.width = CAP_LIVE_SRC_W;
    vi.height = CAP_LIVE_SRC_H;
    vi.stride = CAP_LIVE_SRC_STRIDE;
    vi.fps = use_dcp ? CAP_LIVE_DCP_FPS : CAP_LIVE_FPS;
    vi.buf_cnt = 4;
    vi.pool_id = CAP_LIVE_CAMERA_POOL;
    vi.format = MEDIA_FORMAT_NV12;
    if (MEDIA_VI_SetAttr(0, &vi) != 0) return -1;
    chain->vi_attr_ok = 1;

    pre.src_x = 0;
    pre.src_y = 0;
    pre.src_width = CAP_LIVE_SRC_W;
    pre.src_height = CAP_LIVE_SRC_H;
    pre.input_width = CAP_LIVE_SRC_W;
    pre.input_height = CAP_LIVE_SRC_H;
    pre.input_stride = CAP_LIVE_SRC_STRIDE;
    pre.input_format = MEDIA_FORMAT_NV12;
    pre.input_depth = 4;
    pre.out_width = proc_w;
    pre.out_height = proc_h;
    pre.out_stride = proc_nv12_stride;
    pre.output_format = MEDIA_FORMAT_NV12;
    pre.output_pool_id = CAP_LIVE_PRE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(CAP_LIVE_PRE_RESIZE_GRP, &pre) != 0 ||
        MEDIA_RESIZE_RGA_Start(CAP_LIVE_PRE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(CAP_LIVE_PRE_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->pre_resize_ok = 1;

    csc.input_width = proc_w;
    csc.input_height = proc_h;
    csc.input_format = MEDIA_FORMAT_NV12;
    csc.output_format = MEDIA_FORMAT_RGB888;
    csc.input_depth = 4;
    csc.output_pool_id = CAP_LIVE_CSC_POOL;
    csc.input_stride = proc_nv12_stride;
    csc.output_stride = proc_rgb_stride;
    csc.csc_mode = 0;
    if (MEDIA_CSC_RGA_CreateGrp(CAP_LIVE_CSC_GRP, &csc) != 0 ||
        MEDIA_CSC_RGA_Start(CAP_LIVE_CSC_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(CAP_LIVE_CSC_GRP) != 0) {
        return -1;
    }
    chain->csc_ok = 1;

    if (use_dcp) {
        MEDIA_DCP_FAST_DEHAZE_ATTR attr = {0};
        attr.width = proc_w;
        attr.height = proc_h;
        attr.format = MEDIA_FORMAT_RGB888;
        attr.input_depth = 4;
        attr.output_pool_id = CAP_LIVE_DEHAZE_POOL;
        attr.input_stride = proc_rgb_stride;
        attr.output_stride = proc_rgb_stride;
        attr.patch = 15;
        attr.omega = 0.95f;
        attr.t0 = 0.12f;
        attr.airlight_percent = 0.001f;
        attr.guided_radius = 24;
        attr.guided_eps = 1e-3f;
        attr.refine_scale = 0.5f;
        attr.passthrough = chain->passthrough;
        if (MEDIA_DCP_FAST_DEHAZE_CreateGrp(grp, &attr) != 0 ||
            MEDIA_DCP_FAST_DEHAZE_Start(grp) != 0 ||
            MEDIA_DCP_FAST_DEHAZE_Enable(grp) != 0) {
            return -1;
        }
    } else {
        MEDIA_CAP_DEHAZE_ATTR attr = {0};
        attr.width = proc_w;
        attr.height = proc_h;
        attr.format = MEDIA_FORMAT_RGB888;
        attr.input_depth = 4;
        attr.output_pool_id = CAP_LIVE_DEHAZE_POOL;
        attr.input_stride = proc_rgb_stride;
        attr.output_stride = proc_rgb_stride;
        attr.guided_radius = 24;
        attr.guided_eps = 1e-3f;
        attr.t0 = 0.12f;
        attr.beta0 = 0.121779f;
        attr.beta1 = 0.959710f;
        attr.beta2 = -0.780245f;
        attr.depth_scale = 1.0f;
        attr.refine_scale = 0.25f;
        attr.passthrough = chain->passthrough;
        if (MEDIA_CAP_DEHAZE_CreateGrp(grp, &attr) != 0 ||
            MEDIA_CAP_DEHAZE_Start(grp) != 0 ||
            MEDIA_CAP_DEHAZE_Enable(grp) != 0) {
            return -1;
        }
    }
    chain->dehaze_ok = 1;

    resize.src_x = 0;
    resize.src_y = 0;
    resize.src_width = proc_w;
    resize.src_height = proc_h;
    resize.input_width = proc_w;
    resize.input_height = proc_h;
    resize.input_stride = proc_rgb_stride;
    resize.input_format = MEDIA_FORMAT_RGB888;
    resize.input_depth = 4;
    resize.out_width = CAP_LIVE_VIEW_W;
    resize.out_height = CAP_LIVE_VIEW_H;
    resize.out_stride = CAP_LIVE_VIEW_RGB_STRIDE;
    resize.output_format = MEDIA_FORMAT_RGB888;
    resize.output_pool_id = CAP_LIVE_RESIZE_POOL;
    if (MEDIA_RESIZE_RGA_CreateGrp(CAP_LIVE_RESIZE_GRP, &resize) != 0 ||
        MEDIA_RESIZE_RGA_Start(CAP_LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(CAP_LIVE_RESIZE_GRP) != 0) {
        return -1;
    }
    chain->resize_ok = 1;

    osd.input_width = CAP_LIVE_VIEW_W;
    osd.input_height = CAP_LIVE_VIEW_H;
    osd.format = MEDIA_FORMAT_RGB888;
    osd.input_depth = 4;
    osd.output_pool_id = CAP_LIVE_OSD_POOL;
    osd.input_stride = CAP_LIVE_VIEW_RGB_STRIDE;
    osd.output_stride = CAP_LIVE_VIEW_RGB_STRIDE;
    osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(CAP_LIVE_OSD_GRP, &osd) != 0 ||
        MEDIA_OSD_Start(CAP_LIVE_OSD_GRP) != 0) {
        return -1;
    }
    chain->osd_ok = 1;

    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = CAP_LIVE_SCREEN_W;
    vo.height = CAP_LIVE_SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, CAP_LIVE_VIEW_X, CAP_LIVE_VIEW_Y,
                           CAP_LIVE_VIEW_W, CAP_LIVE_VIEW_H,
                           CAP_LIVE_VIEW_RGB_STRIDE, 6,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_RGB888) != 0) {
        return -1;
    }
    chain->vo_ok = 1;

    if (MEDIA_SYS_Bind("VI", 0, "output",
                       "RESIZE_RGA", CAP_LIVE_PRE_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_vi_pre_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", CAP_LIVE_PRE_RESIZE_GRP, "output0",
                       "CSC_RGA", CAP_LIVE_CSC_GRP, "input") != 0) return -1;
    chain->bind_pre_csc_ok = 1;
    if (MEDIA_SYS_Bind("CSC_RGA", CAP_LIVE_CSC_GRP, "output0",
                       dehaze_module_name(use_dcp), grp, "input") != 0) return -1;
    chain->bind_csc_dehaze_ok = 1;
    if (MEDIA_SYS_Bind(dehaze_module_name(use_dcp), grp, "output0",
                       "RESIZE_RGA", CAP_LIVE_RESIZE_GRP, "input0") != 0) return -1;
    chain->bind_dehaze_resize_ok = 1;
    if (MEDIA_SYS_Bind("RESIZE_RGA", CAP_LIVE_RESIZE_GRP, "output0",
                       "OSD", CAP_LIVE_OSD_GRP, "input") != 0) return -1;
    chain->bind_resize_osd_ok = 1;
    if (MEDIA_SYS_Bind("OSD", CAP_LIVE_OSD_GRP, "output0",
                       "VO", 0, "input0") != 0) return -1;
    chain->bind_osd_vo_ok = 1;

    if (MEDIA_VO_Start(0, 0) != 0 ||
        MEDIA_VI_Enable(0) != 0) {
        return -1;
    }
    chain->vi_enabled = 1;
    set_tile_status("VI", TILE_LIVE);
    set_tile_status("CSC_RGA", TILE_LIVE);
    set_tile_status(dehaze_module_name(use_dcp), TILE_LIVE);
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    set_tile_status("OSD", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    return 0;
}

static void cleanup_dehaze_live_chain(dehaze_live_chain_t *chain) {
    if (!chain) return;
    const int grp = dehaze_grp_id(chain->use_dcp);
    const char *module = dehaze_module_name(chain->use_dcp);
    if (chain->vi_enabled) {
        MEDIA_VI_Disable(0);
        chain->vi_enabled = 0;
        usleep(50000);
    }
    if (chain->vo_ok) MEDIA_VO_Stop(0, 0);

    if (chain->bind_osd_vo_ok) {
        MEDIA_SYS_UnBind("OSD", CAP_LIVE_OSD_GRP, "output0", "VO", 0, "input0");
        chain->bind_osd_vo_ok = 0;
    }
    if (chain->bind_resize_osd_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", CAP_LIVE_RESIZE_GRP, "output0",
                         "OSD", CAP_LIVE_OSD_GRP, "input");
        chain->bind_resize_osd_ok = 0;
    }
    if (chain->bind_dehaze_resize_ok) {
        MEDIA_SYS_UnBind(module, grp, "output0",
                         "RESIZE_RGA", CAP_LIVE_RESIZE_GRP, "input0");
        chain->bind_dehaze_resize_ok = 0;
    }
    if (chain->bind_csc_dehaze_ok) {
        MEDIA_SYS_UnBind("CSC_RGA", CAP_LIVE_CSC_GRP, "output0",
                         module, grp, "input");
        chain->bind_csc_dehaze_ok = 0;
    }
    if (chain->bind_pre_csc_ok) {
        MEDIA_SYS_UnBind("RESIZE_RGA", CAP_LIVE_PRE_RESIZE_GRP, "output0",
                         "CSC_RGA", CAP_LIVE_CSC_GRP, "input");
        chain->bind_pre_csc_ok = 0;
    }
    if (chain->bind_vi_pre_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "RESIZE_RGA", CAP_LIVE_PRE_RESIZE_GRP, "input0");
        chain->bind_vi_pre_ok = 0;
    }
    if (chain->bind_vi_csc_ok) {
        MEDIA_SYS_UnBind("VI", 0, "output",
                         "CSC_RGA", CAP_LIVE_CSC_GRP, "input");
        chain->bind_vi_csc_ok = 0;
    }
    if (chain->vo_ok) {
        MEDIA_VO_DestroyChn(0, 0);
        chain->vo_ok = 0;
    }
    if (chain->osd_ok) {
        drain_live_osd_output();
        MEDIA_OSD_Stop(CAP_LIVE_OSD_GRP);
        drain_live_osd_output();
        MEDIA_OSD_DestroyGrp(CAP_LIVE_OSD_GRP);
        chain->osd_ok = 0;
    }
    if (chain->resize_ok) {
        drain_live_resize_output();
        MEDIA_RESIZE_RGA_Disable(CAP_LIVE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(CAP_LIVE_RESIZE_GRP);
        drain_live_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(CAP_LIVE_RESIZE_GRP);
        chain->resize_ok = 0;
    }
    if (chain->back_csc_ok) {
        drain_live_csc_output(CAP_LIVE_BACK_CSC_GRP);
        MEDIA_CSC_RGA_Disable(CAP_LIVE_BACK_CSC_GRP);
        MEDIA_CSC_RGA_Stop(CAP_LIVE_BACK_CSC_GRP);
        drain_live_csc_output(CAP_LIVE_BACK_CSC_GRP);
        MEDIA_CSC_RGA_DestroyGrp(CAP_LIVE_BACK_CSC_GRP);
        chain->back_csc_ok = 0;
    }
    if (chain->dehaze_ok) {
        drain_live_dehaze_output(chain->use_dcp);
        if (chain->use_dcp) {
            MEDIA_DCP_FAST_DEHAZE_Disable(grp);
            MEDIA_DCP_FAST_DEHAZE_Stop(grp);
            drain_live_dehaze_output(chain->use_dcp);
            MEDIA_DCP_FAST_DEHAZE_DestroyGrp(grp);
        } else {
            MEDIA_CAP_DEHAZE_Disable(grp);
            MEDIA_CAP_DEHAZE_Stop(grp);
            drain_live_dehaze_output(chain->use_dcp);
            MEDIA_CAP_DEHAZE_DestroyGrp(grp);
        }
        chain->dehaze_ok = 0;
    }
    if (chain->csc_ok) {
        drain_live_csc_output(CAP_LIVE_CSC_GRP);
        MEDIA_CSC_RGA_Disable(CAP_LIVE_CSC_GRP);
        MEDIA_CSC_RGA_Stop(CAP_LIVE_CSC_GRP);
        drain_live_csc_output(CAP_LIVE_CSC_GRP);
        MEDIA_CSC_RGA_DestroyGrp(CAP_LIVE_CSC_GRP);
        chain->csc_ok = 0;
    }
    if (chain->pre_resize_ok) {
        drain_live_pre_resize_output();
        MEDIA_RESIZE_RGA_Disable(CAP_LIVE_PRE_RESIZE_GRP);
        MEDIA_RESIZE_RGA_Stop(CAP_LIVE_PRE_RESIZE_GRP);
        drain_live_pre_resize_output();
        MEDIA_RESIZE_RGA_DestroyGrp(CAP_LIVE_PRE_RESIZE_GRP);
        chain->pre_resize_ok = 0;
    }
    if (chain->osd_pool_ok) {
        MEDIA_POOL_Destroy(CAP_LIVE_OSD_POOL);
        chain->osd_pool_ok = 0;
    }
    if (chain->resize_pool_ok) {
        MEDIA_POOL_Destroy(CAP_LIVE_RESIZE_POOL);
        chain->resize_pool_ok = 0;
    }
    if (chain->dehaze_pool_ok) {
        MEDIA_POOL_Destroy(CAP_LIVE_DEHAZE_POOL);
        chain->dehaze_pool_ok = 0;
    }
    if (chain->back_pool_ok) {
        MEDIA_POOL_Destroy(CAP_LIVE_BACK_POOL);
        chain->back_pool_ok = 0;
    }
    if (chain->csc_pool_ok) {
        MEDIA_POOL_Destroy(CAP_LIVE_CSC_POOL);
        chain->csc_pool_ok = 0;
    }
    if (chain->pre_pool_ok) {
        MEDIA_POOL_Destroy(CAP_LIVE_PRE_POOL);
        chain->pre_pool_ok = 0;
    }
    if (chain->camera_pool_ok) {
        MEDIA_POOL_Destroy(CAP_LIVE_CAMERA_POOL);
        chain->camera_pool_ok = 0;
    }
    if (chain->sys_ok) {
        MEDIA_SYS_Exit();
        chain->sys_ok = 0;
    }
}

static int run_dehaze_live_page(volatile sig_atomic_t *running, int use_dcp) {
    dehaze_live_chain_t chain = {0};
    if (setup_dehaze_live_chain(&chain, use_dcp) != 0) {
        fprintf(stderr, "%s standalone VI chain setup failed\n", dehaze_module_name(use_dcp));
        cleanup_dehaze_live_chain(&chain);
        return 1;
    }

    if (use_dcp) {
        printf("%s standalone: VI %s %dx%d -> PRE_RESIZE %dx%d -> CSC_RGA RGB -> DEHAZE -> RESIZE_RGA RGB %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
               dehaze_module_name(use_dcp), CAP_LIVE_CAMERA_DEVICE,
               CAP_LIVE_SRC_W, CAP_LIVE_SRC_H,
               CAP_LIVE_PROC_W, CAP_LIVE_PROC_H,
               CAP_LIVE_VIEW_W, CAP_LIVE_VIEW_H);
    } else {
        printf("%s standalone: VI %s %dx%d -> PRE_RESIZE %dx%d -> CSC_RGA RGB -> DEHAZE -> RESIZE_RGA RGB %dx%d -> OSD -> VO. Ctrl+C to stop.\n",
               dehaze_module_name(use_dcp), CAP_LIVE_CAMERA_DEVICE,
               CAP_LIVE_SRC_W, CAP_LIVE_SRC_H,
               CAP_LIVE_PROC_W, CAP_LIVE_PROC_H,
               CAP_LIVE_VIEW_W, CAP_LIVE_VIEW_H);
    }
    int tick = 0;
    while (!running || *running) {
        uint64_t vi_count = 0;
        uint64_t csc_count = 0;
        uint64_t dehaze_count = 0;
        uint64_t resize_count = 0;
        uint64_t osd_count = 0;
        uint64_t vo_count = 0;
        sleep(1);
        tick++;
        chain.passthrough = (tick % 2) == 0;
        if (use_dcp) {
            (void)MEDIA_DCP_FAST_DEHAZE_SetPassthrough(DCP_GRP, chain.passthrough);
        } else {
            (void)MEDIA_CAP_DEHAZE_SetPassthrough(CAP_GRP, chain.passthrough);
        }
        (void)MEDIA_SYS_GetModuleFrameCount("VI", 0, &vi_count);
        (void)MEDIA_SYS_GetModuleFrameCount("CSC_RGA", CAP_LIVE_CSC_GRP, &csc_count);
        (void)MEDIA_SYS_GetModuleFrameCount(dehaze_module_name(use_dcp),
                                            dehaze_grp_id(use_dcp), &dehaze_count);
        (void)MEDIA_SYS_GetModuleFrameCount("RESIZE_RGA", CAP_LIVE_RESIZE_GRP, &resize_count);
        (void)MEDIA_SYS_GetModuleFrameCount("OSD", CAP_LIVE_OSD_GRP, &osd_count);
        (void)MEDIA_SYS_GetModuleFrameCount("VO", 0, &vo_count);
        int overlay_ok = update_dehaze_live_overlay(&chain, vi_count, csc_count,
                                                    dehaze_count, resize_count,
                                                    osd_count, vo_count) == 0;
        printf("%s vi=%llu csc=%llu dehaze=%llu resize=%llu osd=%llu vo=%llu passthrough=%d tick=%d overlay=%s standalone=1\n",
               dehaze_module_name(use_dcp),
               (unsigned long long)vi_count,
               (unsigned long long)csc_count,
               (unsigned long long)dehaze_count,
               (unsigned long long)resize_count,
               (unsigned long long)osd_count,
               (unsigned long long)vo_count,
               chain.passthrough,
               tick,
               overlay_ok ? "perf_text" : "failed");
    }

    cleanup_dehaze_live_chain(&chain);
    return 0;
}

int page_cap_dehaze_run(volatile sig_atomic_t *running) {
    return run_dehaze_live_cpu_page(running, 0);
}

int page_cap_dehaze_offline_run(volatile sig_atomic_t *running) {
    return run_cap_dehaze_page(running, "CAP_DEHAZE_OFFLINE", "CAP DEHAZE OFFLINE", 0);
}

int page_dcp_fast_dehaze_run(volatile sig_atomic_t *running) {
    return run_dehaze_live_cpu_page(running, 1);
}
