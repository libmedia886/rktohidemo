#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
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

#define RET_PAGE_W 1080
#define RET_PAGE_H 1920
#define RET_PAGE_STRIDE 1088
#define RET_PAGE_POOL 1
#define RET_PAGE_FPS 30
#define RET_GRP 73
#define RET_INPUT_POOL 6
#define RET_W 640
#define RET_H 640
#define RET_STRIDE 640
#define RET_FRAME_SIZE (RET_STRIDE * RET_H * 3 / 2)
#define RET_MAX_SAMPLES 100
#define RET_SAMPLE_SECONDS 1
#define RET_ASSET_DIR "assets/loop/retinex/exdark"
#define LICENSE_PATH "/root/licence.dat"

#define RET_GAIN 40.0f

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} image_t;

typedef struct {
    char paths[RET_MAX_SAMPLES][PATH_MAX];
    int count;
    int current_index;
    char current_name[128];
    uint8_t *input;
    uint8_t *output;
    int processed;
    int module_ok;
    const char *page_name;
    const char *title;
} ret_page_ctx_t;

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

static int supported_image_path(const char *path) {
    const char *ext = path ? strrchr(path, '.') : NULL;
    return ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0);
}

static int path_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

static int scan_samples(ret_page_ctx_t *ctx) {
    if (!ctx) return 0;
    DIR *dir = opendir(RET_ASSET_DIR);
    struct dirent *ent = NULL;
    ctx->count = 0;
    ctx->current_index = -1;
    ctx->current_name[0] = '\0';
    if (!dir) return 0;

    while ((ent = readdir(dir)) != NULL && ctx->count < RET_MAX_SAMPLES) {
        char path[PATH_MAX];
        struct stat st;
        if (ent->d_name[0] == '.' || !supported_image_path(ent->d_name)) continue;
        if (snprintf(path, sizeof(path), "%s/%s", RET_ASSET_DIR, ent->d_name) >=
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

static void image_to_nv12_frame(const image_t *img, uint8_t *dst) {
    if (!img || !img->rgb || !dst || img->width <= 0 || img->height <= 0) return;
    memset(dst, 16, RET_STRIDE * RET_H);
    memset(dst + RET_STRIDE * RET_H, 128, RET_STRIDE * RET_H / 2);

    int out_w = RET_W;
    int out_h = (int)((int64_t)img->height * RET_W / img->width);
    if (out_h > RET_H) {
        out_h = RET_H;
        out_w = (int)((int64_t)img->width * RET_H / img->height);
    }
    if (out_w <= 0 || out_h <= 0) return;
    int ox = (RET_W - out_w) / 2;
    int oy = (RET_H - out_h) / 2;
    uint8_t *uv = dst + RET_STRIDE * RET_H;

    for (int y = 0; y < out_h; ++y) {
        int sy = y * img->height / out_h;
        uint8_t *drow = dst + (size_t)(oy + y) * RET_STRIDE + ox;
        for (int x = 0; x < out_w; ++x) {
            int sx = x * img->width / out_w;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)uu;
            (void)vv;
            drow[x] = yy;
        }
    }
    for (int y = 0; y < out_h; y += 2) {
        int sy = y * img->height / out_h;
        uint8_t *drow = uv + (size_t)((oy + y) / 2) * RET_STRIDE + (ox & ~1);
        for (int x = 0; x < out_w; x += 2) {
            int sx = x * img->width / out_w;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)yy;
            drow[x & ~1] = uu;
            drow[(x & ~1) + 1] = vv;
        }
    }
}

static int load_sample_nv12(ret_page_ctx_t *ctx, int index) {
    if (!ctx || !ctx->input || index < 0 || index >= ctx->count) return -1;
    image_t img;
    if (load_jpeg_rgb(ctx->paths[index], &img) != 0) return -1;
    image_to_nv12_frame(&img, ctx->input);
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

static int setup_retinex_module(void) {
    if (MEDIA_POOL_Create(RET_INPUT_POOL, RET_FRAME_SIZE, 3) != 0) return -1;

    MEDIA_RETINEX_ATTR attr = {0};
    attr.scale_count = 1;
    attr.width = RET_W;
    attr.height = RET_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_depth = 3;
    attr.input_stride = RET_STRIDE;
    attr.output_stride = RET_STRIDE;
    attr.gain = RET_GAIN;
    attr.threshold = 0.5f;
    attr.log_min = -3.0f;
    attr.log_max = 8.5f;

    if (MEDIA_RETINEX_CreateGrp(RET_GRP, &attr) != 0 ||
        MEDIA_RETINEX_Start(RET_GRP) != 0) {
        MEDIA_RETINEX_DestroyGrp(RET_GRP);
        MEDIA_POOL_Destroy(RET_INPUT_POOL);
        return -1;
    }
    return 0;
}

static void cleanup_retinex_module(int enabled) {
    if (!enabled) return;
    MEDIA_RETINEX_Stop(RET_GRP);
    MEDIA_RETINEX_DestroyGrp(RET_GRP);
    MEDIA_POOL_Destroy(RET_INPUT_POOL);
}

static int process_retinex(ret_page_ctx_t *ctx) {
    if (!ctx || !ctx->input || !ctx->output || !ctx->module_ok) return -1;
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    if (MEDIA_POOL_GetBuffer(RET_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, ctx->input, RET_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame("RETINEX", RET_GRP, "input0", in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);

    if (MEDIA_RETINEX_GetFrame(RET_GRP, &out, 1000) != 0) return -1;
    int ret = copy_from_buffer(out, ctx->output, RET_FRAME_SIZE);
    MEDIA_RETINEX_ReleaseFrame(RET_GRP, out);
    if (ret == 0) ctx->processed++;
    return ret;
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;
    for (int y = 0; y < dh; ++y) {
        int sy = y * RET_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * RET_STRIDE;
        for (int x = 0; x < dw; ++x) {
            int sx = x * RET_W / dw;
            drow[x] = srow[sx];
        }
    }
    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)RET_STRIDE * RET_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (RET_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * RET_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = ((x * RET_W / dw) & ~1);
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_retinex_page(uint8_t *dst, int stride, int width, int height,
                              int frame, void *opaque) {
    ret_page_ctx_t *ctx = (ret_page_ctx_t *)opaque;
    int sample = ctx && ctx->count > 0 ?
        (frame / (RET_PAGE_FPS * RET_SAMPLE_SECONDS)) % ctx->count : -1;
    if (ctx && sample >= 0 && sample != ctx->current_index) {
        if (load_sample_nv12(ctx, sample) == 0) {
            if (!ctx->module_ok || process_retinex(ctx) != 0) {
                memcpy(ctx->output, ctx->input, RET_FRAME_SIZE);
            }
        }
    }

    char sample_text[96];
    char processed[48];
    snprintf(sample_text, sizeof(sample_text), "SAMPLE %03d/%03d",
             ctx ? ctx->current_index + 1 : 0, ctx ? ctx->count : 0);
    snprintf(processed, sizeof(processed), "RETINEX %06d", ctx ? ctx->processed : 0);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 60, 58,
                           ctx && ctx->title ? ctx->title : "RETINEX", 7, 235, 108, 176);
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
                           "EXDARK INPUT", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, x, bottom_y - 34,
                           "RETINEX GAIN40", 3, 220, 108, 176);
    if (ctx && ctx->input) draw_nv12_scaled(dst, stride, width, height, x, top_y, pane, pane, ctx->input);
    if (ctx && ctx->output) draw_nv12_scaled(dst, stride, width, height, x, bottom_y, pane, pane, ctx->output);

    page_surface_draw_text(dst, stride, width, height, 70, 1595,
                           "GAIN 40 THRESHOLD 50 LOG -3 TO 85",
                           2, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 70, 1660,
                           processed, 3, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 70, 1725,
                           ctx && ctx->module_ok ? "MODULE LIVE" : "MODULE FALLBACK",
                           3, 210, 108, 176);
    if (ctx && ctx->current_name[0]) {
        page_surface_draw_text(dst, stride, width, height, 70, 1810,
                               "EXDARK SAMPLE LOADED", 2, 190, 144, 84);
    }
}

static int run_retinex_page(volatile sig_atomic_t *running,
                            const char *page_name,
                            const char *title) {
    page_surface_t surface;
    ret_page_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.page_name = page_name;
    ctx.title = title;
    ctx.input = malloc(RET_FRAME_SIZE);
    ctx.output = malloc(RET_FRAME_SIZE);
    if (!ctx.input || !ctx.output || scan_samples(&ctx) <= 0) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }
    memset(ctx.input, 16, RET_FRAME_SIZE);
    memset(ctx.output, 16, RET_FRAME_SIZE);
    memset(ctx.input + RET_STRIDE * RET_H, 128, RET_STRIDE * RET_H / 2);
    memset(ctx.output + RET_STRIDE * RET_H, 128, RET_STRIDE * RET_H / 2);

    if (page_surface_open(&surface, RET_PAGE_POOL, RET_PAGE_W, RET_PAGE_H,
                          RET_PAGE_STRIDE, RET_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free(ctx.input);
        free(ctx.output);
        return 1;
    }

    ctx.module_ok = setup_retinex_module() == 0;
    if (ctx.module_ok) set_tile_status(page_name, TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("%s standalone page module=%s samples=%d. Ctrl+C to stop.\n",
           page_name, ctx.module_ok ? "live" : "fallback", ctx.count);

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_retinex_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % RET_PAGE_FPS) == 0) {
                printf("%s frames=%d sample=%d/%d file=%s retinex_frames=%d gain=%.1f threshold=0.5 standalone=1\n",
                       page_name, frame, ctx.current_index + 1, ctx.count,
                       ctx.current_name[0] ? ctx.current_name : "waiting",
                       ctx.processed, RET_GAIN);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / RET_PAGE_FPS);
    }

    cleanup_retinex_module(ctx.module_ok);
    page_surface_close(&surface);
    free(ctx.input);
    free(ctx.output);
    return 0;
}

int page_retinex_run(volatile sig_atomic_t *running) {
    return run_retinex_page(running, "RETINEX", "RETINEX");
}

int page_retinex_offline_run(volatile sig_atomic_t *running) {
    return run_retinex_page(running, "RETINEX_OFFLINE", "RETINEX OFFLINE");
}
