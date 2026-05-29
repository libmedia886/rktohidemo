#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_surface.h"

#include <linux/dma-buf.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <jpeglib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define TLF_PAGE_W 1080
#define TLF_PAGE_H 1920
#define TLF_PAGE_STRIDE 1088
#define TLF_PAGE_POOL 1
#define TLF_PAGE_FPS 30
#define TLF_FRAME_W 640
#define TLF_FRAME_H 480
#define TLF_FRAME_STRIDE 640
#define TLF_FRAME_SIZE (TLF_FRAME_STRIDE * TLF_FRAME_H * 3 / 2)
#define TLF_GRAY_GRP 0
#define TLF_OVERLAY_GRP 1
#define TLF_POOL_THERMAL 10
#define TLF_POOL_LOWLIGHT 11
#define TLF_POOL_GRAY_OUT 12
#define TLF_POOL_OVERLAY_OUT 13
#define TLF_PYRAMID_LEVELS 2
#define TLF_SAMPLE_SECONDS 3
#define TLF_ASSET_DIR "assets/loop/thermal_lowlight_fusion_cl_real_preview"
#define TLF_GENERATOR_BIN "/userdata/rktohi/build/demo/demo_thermal_lowlight_fusion_cl_smoke"
#define TLF_GENERATED_DIR "build/thermal_lowlight_fusion_cl_live_cache"
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    const char *name;
    const char *title;
} tlf_sample_t;

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} image_t;

typedef struct {
    uint8_t *thermal;
    uint8_t *lowlight;
    uint8_t *gray;
    uint8_t *tif;
    uint8_t *overlay;
    int valid_indices[8];
    int valid_count;
    int current_index;
    int loaded;
    int generated;
    int generation_failed;
    int generated_ok[8];
    uint8_t *gray_cache[8];
    uint8_t *tif_cache[8];
    uint8_t *overlay_cache[8];
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF gray_perf_cache[8];
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF tif_perf_cache[8];
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF overlay_perf_cache[8];
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF gray_perf;
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF tif_perf;
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF overlay_perf;
} tlf_ctx_t;

static const tlf_sample_t g_samples[] = {
    {"carLight", "CARLIGHT"},
    {"manlight", "MANLIGHT"},
    {"nightCar", "NIGHTCAR"},
    {"walkingnight", "WALKINGNIGHT"},
};

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

static void clear_nv12(uint8_t *dst, uint8_t yy, uint8_t uu, uint8_t vv) {
    if (!dst) return;
    memset(dst, yy, TLF_FRAME_STRIDE * TLF_FRAME_H);
    uint8_t *uv = dst + TLF_FRAME_STRIDE * TLF_FRAME_H;
    for (int y = 0; y < TLF_FRAME_H / 2; ++y) {
        uint8_t *row = uv + (size_t)y * TLF_FRAME_STRIDE;
        for (int x = 0; x < TLF_FRAME_STRIDE; x += 2) {
            row[x] = uu;
            row[x + 1] = vv;
        }
    }
}

static void image_to_nv12_frame(const image_t *img, uint8_t *dst) {
    if (!img || !img->rgb || !dst || img->width <= 0 || img->height <= 0) return;
    clear_nv12(dst, 14, 128, 128);

    int out_w = TLF_FRAME_W;
    int out_h = (int)((int64_t)img->height * TLF_FRAME_W / img->width);
    if (out_h > TLF_FRAME_H) {
        out_h = TLF_FRAME_H;
        out_w = (int)((int64_t)img->width * TLF_FRAME_H / img->height);
    }
    out_w &= ~1;
    out_h &= ~1;
    if (out_w <= 0 || out_h <= 0) return;

    int ox = ((TLF_FRAME_W - out_w) / 2) & ~1;
    int oy = ((TLF_FRAME_H - out_h) / 2) & ~1;
    uint8_t *uv = dst + TLF_FRAME_STRIDE * TLF_FRAME_H;

    for (int y = 0; y < out_h; ++y) {
        int sy = y * img->height / out_h;
        uint8_t *drow = dst + (size_t)(oy + y) * TLF_FRAME_STRIDE + ox;
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
        uint8_t *drow = uv + (size_t)((oy + y) / 2) * TLF_FRAME_STRIDE + ox;
        for (int x = 0; x < out_w; x += 2) {
            int sx = x * img->width / out_w;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yy, uu, vv;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yy, &uu, &vv);
            (void)yy;
            drow[x] = uu;
            drow[x + 1] = vv;
        }
    }
}

static void make_path(char *out, size_t len, const char *sample, const char *suffix) {
    snprintf(out, len, "%s/%s_%s.jpg", TLF_ASSET_DIR, sample, suffix);
}

static int load_frame_path(const char *path, uint8_t *dst) {
    image_t img;
    if (load_jpeg_rgb(path, &img) != 0) return -1;
    image_to_nv12_frame(&img, dst);
    free_image(&img);
    return 0;
}

static int load_asset_frame(const char *sample, const char *suffix, uint8_t *dst) {
    char path[256];
    make_path(path, sizeof(path), sample, suffix);
    return load_frame_path(path, dst);
}

static int load_generated_frame(const char *sample, const char *suffix, uint8_t *dst) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s_%s.jpg", TLF_GENERATED_DIR, sample, suffix);
    return load_frame_path(path, dst);
}

static int run_generator_mode(const char *sample,
                              const char *output_sample,
                              int mode,
                              int algo,
                              int tif_base_radius) {
    char ir_path[256];
    char vi_path[256];
    char mode_arg[16];
    char algo_arg[16];
    char radius_arg[16];
    int status = 0;
    pid_t pid;

    if (!sample || !output_sample) return -1;
    make_path(ir_path, sizeof(ir_path), sample, "input0_ir");
    make_path(vi_path, sizeof(vi_path), sample, "input1_vi");
    snprintf(mode_arg, sizeof(mode_arg), "%d", mode);
    snprintf(algo_arg, sizeof(algo_arg), "%d", algo);
    snprintf(radius_arg, sizeof(radius_arg), "%d", tif_base_radius);

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: fork generator failed\n");
        return -1;
    }
    if (pid == 0) {
        execl(TLF_GENERATOR_BIN,
              TLF_GENERATOR_BIN,
              "--pair-mode",
              output_sample,
              ir_path,
              vi_path,
              TLF_GENERATED_DIR,
              mode_arg,
              algo_arg,
              radius_arg,
              (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: wait generator failed\n");
        return -1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr,
                "THERMAL_LOWLIGHT_FUSION_CL page: generator failed sample=%s mode=%d status=%d\n",
                output_sample,
                mode,
                status);
        return -1;
    }
    return 0;
}

static int sample_assets_ready(const char *sample) {
    static const char *suffixes[] = {
        "input0_ir",
        "input1_vi",
    };
    char path[256];
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i) {
        make_path(path, sizeof(path), sample, suffixes[i]);
        if (access(path, R_OK) != 0) return 0;
    }
    return 1;
}

static int scan_samples(tlf_ctx_t *ctx) {
    if (!ctx) return 0;
    ctx->valid_count = 0;
    for (int i = 0; i < (int)(sizeof(g_samples) / sizeof(g_samples[0])); ++i) {
        if (sample_assets_ready(g_samples[i].name) &&
            ctx->valid_count < (int)(sizeof(ctx->valid_indices) / sizeof(ctx->valid_indices[0]))) {
            ctx->valid_indices[ctx->valid_count++] = i;
        }
    }
    return ctx->valid_count;
}

static int create_fusion_pools(void) {
    if (MEDIA_POOL_Create(TLF_POOL_THERMAL, TLF_FRAME_SIZE, 4) != 0 ||
        MEDIA_POOL_Create(TLF_POOL_LOWLIGHT, TLF_FRAME_SIZE, 4) != 0 ||
        MEDIA_POOL_Create(TLF_POOL_GRAY_OUT, TLF_FRAME_SIZE, 2) != 0 ||
        MEDIA_POOL_Create(TLF_POOL_OVERLAY_OUT, TLF_FRAME_SIZE, 2) != 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: pool create failed\n");
        MEDIA_POOL_Destroy(TLF_POOL_OVERLAY_OUT);
        MEDIA_POOL_Destroy(TLF_POOL_GRAY_OUT);
        MEDIA_POOL_Destroy(TLF_POOL_LOWLIGHT);
        MEDIA_POOL_Destroy(TLF_POOL_THERMAL);
        return -1;
    }
    return 0;
}

static void destroy_fusion_pools(void) {
    MEDIA_POOL_Destroy(TLF_POOL_OVERLAY_OUT);
    MEDIA_POOL_Destroy(TLF_POOL_GRAY_OUT);
    MEDIA_POOL_Destroy(TLF_POOL_LOWLIGHT);
    MEDIA_POOL_Destroy(TLF_POOL_THERMAL);
}

static void fill_fusion_attr(MEDIA_THERMAL_LOWLIGHT_FUSION_CL_ATTR *attr,
                             int mode, int output_pool_id) {
    memset(attr, 0, sizeof(*attr));
    attr->width = TLF_FRAME_W;
    attr->height = TLF_FRAME_H;
    attr->format = MEDIA_FORMAT_NV12;
    attr->input_depth = 2;
    attr->output_pool_id = output_pool_id;
    attr->input_stride = TLF_FRAME_STRIDE;
    attr->output_stride = TLF_FRAME_STRIDE;
    attr->mode = mode;
    attr->thermal_weight = 0.50f;
    attr->hot_threshold = 0.58f;
    attr->hot_soft_width = 0.12f;
    attr->overlay_alpha = 0.90f;
    attr->pyramid_levels = TLF_PYRAMID_LEVELS;
}

static int create_fusion_group(int grp, int mode, int output_pool_id) {
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_ATTR attr;

    fill_fusion_attr(&attr, mode, output_pool_id);
    if (MEDIA_THERMAL_LOWLIGHT_FUSION_CL_CreateGrp(grp, &attr) != 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: create failed grp=%d mode=%d\n",
                grp, mode);
        return -1;
    }
    if (MEDIA_THERMAL_LOWLIGHT_FUSION_CL_Start(grp) != 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: start failed grp=%d mode=%d\n",
                grp, mode);
        MEDIA_THERMAL_LOWLIGHT_FUSION_CL_DestroyGrp(grp);
        return -1;
    }
    return 0;
}

static void destroy_fusion_group(int grp) {
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_Stop(grp);
    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_DestroyGrp(grp);
}

static int create_fusion_groups(void) {
    if (create_fusion_group(TLF_GRAY_GRP,
                            MEDIA_THERMAL_LOWLIGHT_FUSION_CL_MODE_GRAY,
                            TLF_POOL_GRAY_OUT) != 0) {
        return -1;
    }
    if (create_fusion_group(TLF_OVERLAY_GRP,
                            MEDIA_THERMAL_LOWLIGHT_FUSION_CL_MODE_BLACK_RED,
                            TLF_POOL_OVERLAY_OUT) != 0) {
        destroy_fusion_group(TLF_GRAY_GRP);
        return -1;
    }
    return 0;
}

static void destroy_fusion_groups(void) {
    destroy_fusion_group(TLF_OVERLAY_GRP);
    destroy_fusion_group(TLF_GRAY_GRP);
}

static int run_fusion_mode(int grp, const uint8_t *thermal, const uint8_t *lowlight,
                           uint8_t *dst, MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF *perf);

static int run_fusion_mode_isolated(const uint8_t *thermal, const uint8_t *lowlight,
                                    int mode, uint8_t *dst,
                                    MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF *perf) {
    int ret = -1;

    if (MEDIA_SYS_Init() != 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: MEDIA_SYS_Init failed\n");
        return -1;
    }
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (create_fusion_pools() != 0) {
        goto out_media;
    }
    if (create_fusion_group(TLF_GRAY_GRP,
                            mode,
                            mode == MEDIA_THERMAL_LOWLIGHT_FUSION_CL_MODE_GRAY ?
                                TLF_POOL_GRAY_OUT : TLF_POOL_OVERLAY_OUT) != 0) {
        goto out_pools;
    }

    ret = run_fusion_mode(TLF_GRAY_GRP, thermal, lowlight, dst, perf);

    destroy_fusion_group(TLF_GRAY_GRP);
out_pools:
    destroy_fusion_pools();
out_media:
    MEDIA_SYS_Exit();
    return ret;
}

static int ensure_sample_cache(tlf_ctx_t *ctx, int sample_index) {
    if (!ctx || sample_index < 0 ||
        sample_index >= (int)(sizeof(ctx->gray_cache) / sizeof(ctx->gray_cache[0]))) {
        return -1;
    }
    if (!ctx->gray_cache[sample_index]) {
        ctx->gray_cache[sample_index] = malloc(TLF_FRAME_SIZE);
    }
    if (!ctx->tif_cache[sample_index]) {
        ctx->tif_cache[sample_index] = malloc(TLF_FRAME_SIZE);
    }
    if (!ctx->overlay_cache[sample_index]) {
        ctx->overlay_cache[sample_index] = malloc(TLF_FRAME_SIZE);
    }
    if (!ctx->gray_cache[sample_index] || !ctx->tif_cache[sample_index] ||
        !ctx->overlay_cache[sample_index]) {
        return -1;
    }
    clear_nv12(ctx->gray_cache[sample_index], 16, 128, 128);
    clear_nv12(ctx->tif_cache[sample_index], 16, 128, 128);
    clear_nv12(ctx->overlay_cache[sample_index], 16, 128, 128);
    return 0;
}

static int generate_sample_outputs(tlf_ctx_t *ctx, int sample_index) {
    const char *sample;
    char pyramid_sample[64];
    char tif_sample[64];
    char overlay_sample[64];

    if (!ctx || sample_index < 0 ||
        sample_index >= (int)(sizeof(g_samples) / sizeof(g_samples[0]))) {
        return -1;
    }
    sample = g_samples[sample_index].name;
    if (ensure_sample_cache(ctx, sample_index) != 0) {
        ctx->generated_ok[sample_index] = 0;
        return -1;
    }

    memset(&ctx->gray_perf_cache[sample_index], 0, sizeof(ctx->gray_perf_cache[sample_index]));
    memset(&ctx->tif_perf_cache[sample_index], 0, sizeof(ctx->tif_perf_cache[sample_index]));
    memset(&ctx->overlay_perf_cache[sample_index], 0, sizeof(ctx->overlay_perf_cache[sample_index]));
    snprintf(pyramid_sample, sizeof(pyramid_sample), "%s_pyramid", sample);
    snprintf(tif_sample, sizeof(tif_sample), "%s_tif", sample);
    snprintf(overlay_sample, sizeof(overlay_sample), "%s_overlay", sample);
    if (run_generator_mode(sample,
                           pyramid_sample,
                           MEDIA_THERMAL_LOWLIGHT_FUSION_CL_MODE_GRAY,
                           MEDIA_THERMAL_LOWLIGHT_FUSION_CL_ALGO_PYRAMID,
                           0) != 0 ||
        run_generator_mode(sample,
                           tif_sample,
                           MEDIA_THERMAL_LOWLIGHT_FUSION_CL_MODE_GRAY,
                           MEDIA_THERMAL_LOWLIGHT_FUSION_CL_ALGO_TIF,
                           8) != 0 ||
        run_generator_mode(sample,
                           overlay_sample,
                           MEDIA_THERMAL_LOWLIGHT_FUSION_CL_MODE_BLACK_RED,
                           MEDIA_THERMAL_LOWLIGHT_FUSION_CL_ALGO_PYRAMID,
                           0) != 0 ||
        load_generated_frame(pyramid_sample, "mode0_gray_fusion", ctx->gray_cache[sample_index]) != 0 ||
        load_generated_frame(tif_sample, "mode0_gray_fusion", ctx->tif_cache[sample_index]) != 0 ||
        load_generated_frame(overlay_sample, "mode1_black_red_overlay", ctx->overlay_cache[sample_index]) != 0) {
        ctx->generated_ok[sample_index] = 0;
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: generate failed sample=%s\n", sample);
        return -1;
    }

    ctx->generated_ok[sample_index] = 1;
    ctx->generated += 3;
    printf("THERMAL_LOWLIGHT_FUSION_CL generated sample=%s levels=%d algos=pyramid,tif radius=8 source=jpg generator=pair-mode\n",
           sample,
           TLF_PYRAMID_LEVELS);
    return 0;
}

static void generate_all_outputs(tlf_ctx_t *ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->valid_count; ++i) {
        (void)generate_sample_outputs(ctx, ctx->valid_indices[i]);
    }
}

static void free_sample_caches(tlf_ctx_t *ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < sizeof(ctx->gray_cache) / sizeof(ctx->gray_cache[0]); ++i) {
        free(ctx->gray_cache[i]);
        free(ctx->tif_cache[i]);
        free(ctx->overlay_cache[i]);
        ctx->gray_cache[i] = NULL;
        ctx->tif_cache[i] = NULL;
        ctx->overlay_cache[i] = NULL;
    }
}

static int map_buffer_for_cpu(MEDIA_BUFFER buf, int sync_flags, int prot,
                              void **addr_out, size_t *size_out, int *mapped_out) {
    void *addr;
    int fd = -1;
    size_t size;

    if (!addr_out || !size_out || !mapped_out) return -1;
    *addr_out = NULL;
    *size_out = 0;
    *mapped_out = 0;

    if (MEDIA_POOL_BeginCpuAccess(buf, sync_flags) != 0) {
        return -1;
    }

    size = MEDIA_POOL_GetSize(buf);
    addr = MEDIA_POOL_GetVaddr(buf);
    if (!addr) {
        if (MEDIA_POOL_GetFd(buf, &fd, &size) != 0 || fd < 0 || size == 0) {
            MEDIA_POOL_EndCpuAccess(buf, sync_flags);
            return -1;
        }
        addr = mmap(NULL, size, prot, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED) {
            MEDIA_POOL_EndCpuAccess(buf, sync_flags);
            return -1;
        }
        *mapped_out = 1;
    }
    if (size == 0) {
        size = TLF_FRAME_SIZE;
    }

    *addr_out = addr;
    *size_out = size;
    return 0;
}

static void unmap_buffer_for_cpu(MEDIA_BUFFER buf, int sync_flags,
                                 void *addr, size_t size, int mapped) {
    if (mapped && addr && addr != MAP_FAILED) {
        munmap(addr, size);
    }
    MEDIA_POOL_EndCpuAccess(buf, sync_flags);
}

static int send_fusion_input(int grp, int pool_id, int input_id,
                             const char *input_name, const uint8_t *frame) {
    MEDIA_BUFFER buf = { .pool_id = -1, .index = -1 };
    void *addr = NULL;
    size_t size = 0;
    int mapped = 0;
    int ret = -1;

    if (!frame || MEDIA_POOL_GetBuffer(pool_id, &buf) != 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: get input buffer failed: %s\n",
                input_name);
        return -1;
    }
    if (map_buffer_for_cpu(buf, DMA_BUF_SYNC_WRITE, PROT_READ | PROT_WRITE,
                           &addr, &size, &mapped) != 0 ||
        size < TLF_FRAME_SIZE) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: map input failed: %s\n",
                input_name);
        goto out_put;
    }

    memcpy(addr, frame, TLF_FRAME_SIZE);
    unmap_buffer_for_cpu(buf, DMA_BUF_SYNC_WRITE, addr, size, mapped);
    addr = NULL;

    if (MEDIA_THERMAL_LOWLIGHT_FUSION_CL_SendFrame(grp, input_id, buf, 1000) != 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: send input failed: %s\n",
                input_name);
        goto out_put;
    }
    ret = 0;

out_put:
    if (addr) {
        unmap_buffer_for_cpu(buf, DMA_BUF_SYNC_WRITE, addr, size, mapped);
    }
    if (buf.pool_id >= 0 && buf.index >= 0) {
        MEDIA_POOL_PutBuffer(buf);
    }
    return ret;
}

static int copy_fusion_output(MEDIA_BUFFER buf, uint8_t *dst) {
    void *addr = NULL;
    size_t size = 0;
    int mapped = 0;

    if (!dst) return -1;
    if (map_buffer_for_cpu(buf, DMA_BUF_SYNC_READ, PROT_READ,
                           &addr, &size, &mapped) != 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: map output failed\n");
        return -1;
    }
    if (size < TLF_FRAME_SIZE) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: output buffer too small\n");
        unmap_buffer_for_cpu(buf, DMA_BUF_SYNC_READ, addr, size, mapped);
        return -1;
    }
    memcpy(dst, addr, TLF_FRAME_SIZE);
    unmap_buffer_for_cpu(buf, DMA_BUF_SYNC_READ, addr, size, mapped);
    return 0;
}

static int run_fusion_mode(int grp, const uint8_t *thermal, const uint8_t *lowlight,
                           uint8_t *dst, MEDIA_THERMAL_LOWLIGHT_FUSION_CL_PERF *perf) {
    MEDIA_BUFFER out = { .pool_id = -1, .index = -1 };
    int ret = -1;

    if (send_fusion_input(grp,
                          TLF_POOL_THERMAL,
                          MEDIA_THERMAL_LOWLIGHT_FUSION_CL_INPUT_THERMAL,
                          "input0_ir", thermal) != 0 ||
        send_fusion_input(grp,
                          TLF_POOL_LOWLIGHT,
                          MEDIA_THERMAL_LOWLIGHT_FUSION_CL_INPUT_LOWLIGHT,
                          "input1_vi", lowlight) != 0) {
        goto out;
    }
    if (MEDIA_THERMAL_LOWLIGHT_FUSION_CL_GetFrame(grp, &out, 5000) != 0) {
        fprintf(stderr, "THERMAL_LOWLIGHT_FUSION_CL page: get output failed grp=%d\n", grp);
        goto out;
    }
    if (copy_fusion_output(out, dst) != 0) {
        goto out;
    }
    if (perf && MEDIA_THERMAL_LOWLIGHT_FUSION_CL_GetLastPerf(grp, perf) != 0) {
        memset(perf, 0, sizeof(*perf));
    }
    ret = 0;

out:
    if (out.pool_id >= 0 && out.index >= 0) {
        MEDIA_THERMAL_LOWLIGHT_FUSION_CL_ReleaseFrame(grp, out);
    }
    return ret;
}

static int load_sample(tlf_ctx_t *ctx, int sample_index) {
    const char *sample;
    if (!ctx || sample_index < 0 ||
        sample_index >= (int)(sizeof(g_samples) / sizeof(g_samples[0]))) {
        return -1;
    }
    sample = g_samples[sample_index].name;
    if (load_asset_frame(sample, "input0_ir", ctx->thermal) != 0 ||
        load_asset_frame(sample, "input1_vi", ctx->lowlight) != 0) {
        return -1;
    }
    clear_nv12(ctx->gray, 16, 128, 128);
    clear_nv12(ctx->tif, 16, 128, 128);
    clear_nv12(ctx->overlay, 16, 128, 128);
    memset(&ctx->gray_perf, 0, sizeof(ctx->gray_perf));
    memset(&ctx->tif_perf, 0, sizeof(ctx->tif_perf));
    memset(&ctx->overlay_perf, 0, sizeof(ctx->overlay_perf));
    ctx->current_index = sample_index;
    if (ctx->generated_ok[sample_index] &&
        ctx->gray_cache[sample_index] &&
        ctx->tif_cache[sample_index] &&
        ctx->overlay_cache[sample_index]) {
        memcpy(ctx->gray, ctx->gray_cache[sample_index], TLF_FRAME_SIZE);
        memcpy(ctx->tif, ctx->tif_cache[sample_index], TLF_FRAME_SIZE);
        memcpy(ctx->overlay, ctx->overlay_cache[sample_index], TLF_FRAME_SIZE);
        ctx->gray_perf = ctx->gray_perf_cache[sample_index];
        ctx->tif_perf = ctx->tif_perf_cache[sample_index];
        ctx->overlay_perf = ctx->overlay_perf_cache[sample_index];
        ctx->generation_failed = 0;
    } else {
        ctx->generation_failed = 1;
    }
    ctx->loaded++;
    return ctx->generation_failed ? -1 : 0;
}

static int update_sample_for_frame(tlf_ctx_t *ctx, int frame) {
    int sample_index;
    int slot;

    if (!ctx || ctx->valid_count <= 0) return -1;
    slot = (frame / (TLF_PAGE_FPS * TLF_SAMPLE_SECONDS)) % ctx->valid_count;
    sample_index = ctx->valid_indices[slot];
    if (sample_index == ctx->current_index) return 0;
    return load_sample(ctx, sample_index);
}

static void draw_nv12_scaled(uint8_t *dst, int dstride, int dwidth, int dheight,
                             int dx, int dy, int dw, int dh, const uint8_t *src) {
    if (!dst || !src || dw <= 0 || dh <= 0) return;
    if (dx < 0 || dy < 0 || dx + dw > dwidth || dy + dh > dheight) return;

    for (int y = 0; y < dh; ++y) {
        int sy = y * TLF_FRAME_H / dh;
        uint8_t *drow = dst + (size_t)(dy + y) * dstride + dx;
        const uint8_t *srow = src + (size_t)sy * TLF_FRAME_STRIDE;
        for (int x = 0; x < dw; ++x) {
            int sx = x * TLF_FRAME_W / dw;
            drow[x] = srow[sx];
        }
    }

    uint8_t *duv = dst + (size_t)dstride * dheight;
    const uint8_t *suv = src + (size_t)TLF_FRAME_STRIDE * TLF_FRAME_H;
    for (int y = 0; y < dh / 2; ++y) {
        int sy = y * (TLF_FRAME_H / 2) / (dh / 2);
        uint8_t *drow = duv + (size_t)(dy / 2 + y) * dstride + (dx & ~1);
        const uint8_t *srow = suv + (size_t)sy * TLF_FRAME_STRIDE;
        for (int x = 0; x < dw; x += 2) {
            int sx = (x * TLF_FRAME_W / dw) & ~1;
            drow[x] = srow[sx];
            drow[x + 1] = srow[sx + 1];
        }
    }
}

static void draw_labeled_pane(uint8_t *dst, int stride, int width, int height,
                              int x, int y, const char *label, const uint8_t *image) {
    const int pane_w = 480;
    const int pane_h = 360;
    page_surface_fill_rect_nv12(dst, stride, width, height,
                                x - 8, y - 40, pane_w + 16, pane_h + 58,
                                22, 128, 128);
    page_surface_draw_text(dst, stride, width, height, x, y - 30,
                           label, 3, 220, 108, 176);
    if (image) {
        draw_nv12_scaled(dst, stride, width, height, x, y, pane_w, pane_h, image);
    }
}

static void draw_tlf_page(uint8_t *dst, int stride, int width, int height,
                          int frame, void *opaque) {
    tlf_ctx_t *ctx = (tlf_ctx_t *)opaque;
    int sample_index = ctx ? ctx->current_index : -1;
    char sample_text[80];
    char status_text[80];
    char module_text[96];
    const char *sample_title = sample_index >= 0 ? g_samples[sample_index].title : "MISSING";
    snprintf(sample_text, sizeof(sample_text), "SAMPLE %02d/%02d %s",
             sample_index >= 0 ? sample_index + 1 : 0,
             (int)(sizeof(g_samples) / sizeof(g_samples[0])),
             sample_title);
    snprintf(status_text, sizeof(status_text), "ASSETS %02d/%02d GENERATED %04d",
             ctx ? ctx->valid_count : 0,
             (int)(sizeof(g_samples) / sizeof(g_samples[0])),
             ctx ? ctx->generated : 0);
    snprintf(module_text, sizeof(module_text), "MODULE LEVELS %d STATUS %s",
             TLF_PYRAMID_LEVELS,
             (ctx && !ctx->generation_failed && sample_index >= 0) ? "OK" : "FAIL");

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 9, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 196, 24, 112, 150);
    page_surface_draw_text(dst, stride, width, height, 46, 52,
                           "THERMAL LOWLIGHT FUSION CL", 5, 235, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 48, 130,
                           sample_text, 3, 210, 144, 84);

    draw_labeled_pane(dst, stride, width, height, 46, 290, "INPUT0 IR", ctx ? ctx->thermal : NULL);
    draw_labeled_pane(dst, stride, width, height, 554, 290, "INPUT1 VI", ctx ? ctx->lowlight : NULL);
    draw_labeled_pane(dst, stride, width, height, 46, 772, "PYRAMID ALGO 0", ctx ? ctx->gray : NULL);
    draw_labeled_pane(dst, stride, width, height, 554, 772, "TIF ALGO 1 R8", ctx ? ctx->tif : NULL);

    page_surface_fill_rect_nv12(dst, stride, width, height, 54, 1244, 972, 306, 18, 128, 128);
    page_surface_draw_text(dst, stride, width, height, 84, 1298,
                           "FLOW IR PLUS VI TO OPENCL FUSION", 3, 210, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 84, 1368,
                           "ATTR PYRAMID LEVELS 2 TIF RADIUS 8", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 84, 1438,
                           module_text, 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 84, 1508,
                           "MODE1 BLACK RED IS RETAINED IN MODULE", 2, 180, 144, 84);

    page_surface_draw_text(dst, stride, width, height, 74, 1660,
                           status_text, 3, 210, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 74, 1730,
                           "SOURCE JPG INPUTS GENERATED ON DEVICE", 3, 180, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 74, 1800,
                           "DIR THERMAL LOWLIGHT FUSION CL REAL PREVIEW", 2, 180, 144, 84);
    page_surface_fill_rect_nv12(dst, stride, width, height,
                                74 + (frame * 9) % 860, 1864, 96, 14,
                                210, 36, 220);
}

int page_thermal_lowlight_fusion_cl_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    tlf_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.current_index = -1;
    ctx.thermal = malloc(TLF_FRAME_SIZE);
    ctx.lowlight = malloc(TLF_FRAME_SIZE);
    ctx.gray = malloc(TLF_FRAME_SIZE);
    ctx.tif = malloc(TLF_FRAME_SIZE);
    ctx.overlay = malloc(TLF_FRAME_SIZE);
    if (!ctx.thermal || !ctx.lowlight || !ctx.gray || !ctx.tif || !ctx.overlay) {
        free(ctx.thermal);
        free(ctx.lowlight);
        free(ctx.gray);
        free(ctx.tif);
        free(ctx.overlay);
        return 1;
    }
    clear_nv12(ctx.thermal, 16, 128, 128);
    clear_nv12(ctx.lowlight, 16, 128, 128);
    clear_nv12(ctx.gray, 16, 128, 128);
    clear_nv12(ctx.tif, 16, 128, 128);
    clear_nv12(ctx.overlay, 16, 128, 128);
    scan_samples(&ctx);
    generate_all_outputs(&ctx);

    if (page_surface_open(&surface, TLF_PAGE_POOL, TLF_PAGE_W, TLF_PAGE_H,
                          TLF_PAGE_STRIDE, TLF_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        free_sample_caches(&ctx);
        free(ctx.thermal);
        free(ctx.lowlight);
        free(ctx.gray);
        free(ctx.tif);
        free(ctx.overlay);
        return 1;
    }

    set_tile_status("THERMAL_LOWLIGHT_FUSION_CL", ctx.valid_count > 0 ? TILE_LOOP : TILE_OFFLINE);
    set_tile_status("VO", TILE_LIVE);
    printf("THERMAL_LOWLIGHT_FUSION_CL generated page assets=%d/%zu levels=%d dir=%s. Ctrl+C to stop.\n",
           ctx.valid_count,
           sizeof(g_samples) / sizeof(g_samples[0]),
           TLF_PYRAMID_LEVELS,
           TLF_ASSET_DIR);

    int frame = 0;
    while (!running || *running) {
        (void)update_sample_for_frame(&ctx, frame);
        if (page_surface_send_frame(&surface, draw_tlf_page, &ctx, frame) == 0) {
            frame++;
            if ((frame % TLF_PAGE_FPS) == 0) {
                printf("THERMAL_LOWLIGHT_FUSION_CL frames=%d sample=%d/%zu assets=%d generated=%d levels=%d generated_page=1 pyramid=%.3fms tif=%.3fms\n",
                       frame,
                       ctx.current_index >= 0 ? ctx.current_index + 1 : 0,
                       sizeof(g_samples) / sizeof(g_samples[0]),
                       ctx.valid_count,
                       ctx.generated,
                       TLF_PYRAMID_LEVELS,
                       ctx.gray_perf.gpu_total_ms,
                       ctx.tif_perf.gpu_total_ms);
            }
        } else {
            usleep(1000);
        }
        usleep(1000000 / TLF_PAGE_FPS);
    }

    page_surface_close(&surface);
    free_sample_caches(&ctx);
    free(ctx.thermal);
    free(ctx.lowlight);
    free(ctx.gray);
    free(ctx.tif);
    free(ctx.overlay);
    return 0;
}
