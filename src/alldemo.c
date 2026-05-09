#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/videodev2.h>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <jpeglib.h>
#include <png.h>

#include "media_api.h"

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

#define SCREEN_W 1080
#define SCREEN_H 1920
#define DISPLAY_POOL 1
#define CAMERA_POOL 2
#define WORK_POOL_NV12 3
#define WORK_POOL_RGB 4
#define WORK_POOL_OUT 5
#define OSD_INPUT_POOL 6
#define OSD_OUTPUT_POOL 7
#define RESIZE_INPUT_POOL 8
#define RESIZE_OUTPUT_POOL 9
#define STEREO_INPUT0_POOL 10
#define STEREO_INPUT1_POOL 11
#define STEREO_OUTPUT_POOL 12
#define VPSS_INPUT_POOL 13
#define VPSS_OUTPUT_POOL 14
#define LIVE_OSD_GRP 60
#define LIVE_RESIZE_GRP 61
#define LIVE_STEREO_GRP 62
#define LIVE_VPSS_GRP 63
#define LIVE_CSC_RGA_GRP 64
#define LIVE_TRANSFORM_GRP 65
#define LIVE_CAP_DEHAZE_GRP 66
#define LIVE_DCP_DEHAZE_GRP 67
#define LIVE_CONV_CL_GRP 68
#define LIVE_CLAHE_GRP 69

#define CAM_W 640
#define CAM_H 640
#define CAM_STRIDE 640
#define FPS 30
#define CAMERA_DEVICE "/dev/video-camera0"
#define LICENSE_PATH "/root/licence.dat"
#define RTSP_PORT 8554
#define CAM_FRAME_SIZE (CAM_STRIDE * CAM_H * 3 / 2)
#define RGB_FRAME_SIZE (CAM_W * CAM_H * 3)
#define RGBA_FRAME_SIZE (CAM_W * CAM_H * 4)
#define MAIN_ROTATE_SECONDS 5
#define TILE_FIRST_INDEX 4
#define TILE_ROTATE_COUNT 16

typedef struct {
    const char *name;
    int active;
    int frames;
    int status;
} module_tile_t;

typedef struct {
    int width;
    int height;
    uint8_t *rgb;
} image_asset_t;

typedef struct {
    const char *tile_name;
    const char *caption;
    const char *paths[4];
    int path_count;
    image_asset_t images[4];
    int loaded_count;
} loop_asset_t;

typedef struct {
    int license_ok;
    int camera_node_ok;
    int drm_ok;
    int media_lib_ok;
    int rtsp_port_free;
    int npu_model_ok;
    int avm_lut_ok;
    int pano_calib_ok;
    int svm_assets_ok;
    int loop_loaded;
    int loop_expected;
    int camera_running;
    int camera_frames;
} health_status_t;

static volatile int g_running = 1;
static health_status_t g_health = {0};

static void set_tile_status(const char *name, int status);
static int find_tile_index(const char *name);

enum {
    TILE_OFFLINE = 0,
    TILE_SYNTH = 1,
    TILE_LOOP = 2,
    TILE_PROBED = 3,
    TILE_LIVE = 4,
};

static module_tile_t g_tiles[] = {
    {"VI", 0, 0, TILE_OFFLINE}, {"VPSS", 0, 0, TILE_OFFLINE},
    {"VO", 0, 0, TILE_OFFLINE}, {"RGA", 0, 0, TILE_OFFLINE},
    {"RESIZE_RGA", 0, 0, TILE_OFFLINE}, {"CSC_RGA", 0, 0, TILE_OFFLINE},
    {"CSC_CL", 0, 0, TILE_OFFLINE}, {"OSD", 0, 0, TILE_OFFLINE},
    {"CLAHE", 0, 0, TILE_OFFLINE}, {"RETINEX", 0, 0, TILE_OFFLINE},
    {"CAP_DEHAZE", 0, 0, TILE_OFFLINE}, {"DCP_FAST_DEHAZE", 0, 0, TILE_OFFLINE},
    {"THERMAL", 0, 0, TILE_OFFLINE}, {"CONV_CL", 0, 0, TILE_OFFLINE},
    {"TRANSFORM", 0, 0, TILE_OFFLINE}, {"BLEND_PYR", 0, 0, TILE_OFFLINE},
    {"EDOF_CL", 0, 0, TILE_OFFLINE}, {"EXPOSURE_FUSION_CL", 0, 0, TILE_OFFLINE},
    {"DUALVIEW", 0, 0, TILE_OFFLINE}, {"STEREO_3D", 0, 0, TILE_OFFLINE},
    {"VMIX", 0, 0, TILE_OFFLINE}, {"VMIX_RGA", 0, 0, TILE_OFFLINE},
    {"PANO", 0, 0, TILE_OFFLINE}, {"AVM", 0, 0, TILE_OFFLINE},
    {"SVM3D", 0, 0, TILE_OFFLINE}, {"NPU", 0, 0, TILE_OFFLINE},
    {"VENC", 0, 0, TILE_OFFLINE}, {"VDEC", 0, 0, TILE_OFFLINE},
    {"RTSP_SEND", 0, 0, TILE_OFFLINE}, {"RTSP_RECV", 0, 0, TILE_OFFLINE},
    {"PIC_IO", 0, 0, TILE_OFFLINE}, {"LICENSE", 0, 0, TILE_OFFLINE},
};

static loop_asset_t g_loop_assets[] = {
    {"THERMAL", "THERMAL LOOP", {
        "assets/loop/thermal/thermal_1.png",
        "assets/loop/thermal/thermal_2.png",
    }, 2, {{0}}, 0},
    {"TRANSFORM", "TRANSFORM LOOP", {
        "assets/loop/transform/transform_1.png",
    }, 1, {{0}}, 0},
    {"VMIX", "VMIX LOOP", {
        "assets/loop/vmix/vmix_1.png",
        "assets/loop/vmix/vmix_2.png",
    }, 2, {{0}}, 0},
    {"AVM", "AVM INPUTS", {
        "assets/loop/avm_inputs/src_1.jpg",
        "assets/loop/avm_inputs/src_2.jpg",
        "assets/loop/avm_inputs/src_3.jpg",
        "assets/loop/avm_inputs/src_4.jpg",
    }, 4, {{0}}, 0},
};

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static int path_readable(const char *path) {
    return access(path, R_OK) == 0;
}

static int tcp_port_free(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    int ok = bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    close(fd);
    return ok;
}

static int expected_loop_asset_count(void) {
    int total = 0;
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        total += g_loop_assets[i].path_count;
    }
    return total;
}

static void collect_health(int loaded_assets) {
    memset(&g_health, 0, sizeof(g_health));
    g_health.license_ok = path_readable(LICENSE_PATH);
    g_health.camera_node_ok = access(CAMERA_DEVICE, F_OK) == 0;
    g_health.drm_ok = access("/dev/dri/card0", F_OK) == 0;
    g_health.media_lib_ok = path_readable("lib/libmedia.so") && path_readable("lib/libmedia.a");
    g_health.rtsp_port_free = tcp_port_free(RTSP_PORT);
    g_health.npu_model_ok = path_readable("assets/npu/yolov5s-640-640.rknn");
    g_health.avm_lut_ok = path_readable("assets/avm/avm_blend.lut");
    g_health.pano_calib_ok = path_readable("assets/panorama/calib_file.pto");
    g_health.svm_assets_ok = path_readable("assets/svm3d/svm_3d_assets.json");
    g_health.loop_loaded = loaded_assets;
    g_health.loop_expected = expected_loop_asset_count();
}

static int print_check(const char *name, int ok, const char *detail) {
    printf("[%s] %-14s %s\n", ok ? "OK" : "FAIL", name, detail ? detail : "");
    return ok ? 0 : 1;
}

static uint8_t clamp_u8(int v) {
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void rgb_to_yuv(uint8_t r, uint8_t g, uint8_t b, uint8_t *y, uint8_t *u, uint8_t *v) {
    *y = clamp_u8(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    *u = clamp_u8(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
    *v = clamp_u8(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
}

static int load_png_rgb(const char *path, image_asset_t *out) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    uint8_t sig[8];
    if (fread(sig, 1, sizeof(sig), fp) != sizeof(sig) || png_sig_cmp(sig, 0, sizeof(sig))) {
        fclose(fp);
        return -1;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) {
        fclose(fp);
        return -1;
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        return -1;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, sizeof(sig));
    png_read_info(png, info);

    int width = (int)png_get_image_width(png, info);
    int height = (int)png_get_image_height(png, info);
    int color_type = png_get_color_type(png, info);
    int bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if (color_type & PNG_COLOR_MASK_ALPHA) png_set_strip_alpha(png);

    png_read_update_info(png, info);
    int channels = png_get_channels(png, info);
    png_size_t rowbytes = png_get_rowbytes(png, info);
    uint8_t *raw = malloc((size_t)rowbytes * height);
    png_bytep *rows = malloc(sizeof(png_bytep) * (size_t)height);
    uint8_t *rgb = malloc((size_t)width * height * 3);
    if (!raw || !rows || !rgb || width <= 0 || height <= 0) {
        free(raw);
        free(rows);
        free(rgb);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return -1;
    }

    for (int y = 0; y < height; ++y) rows[y] = raw + (size_t)y * rowbytes;
    png_read_image(png, rows);
    for (int y = 0; y < height; ++y) {
        uint8_t *src = rows[y];
        uint8_t *dst = rgb + (size_t)y * width * 3;
        for (int x = 0; x < width; ++x) {
            if (channels >= 3) {
                dst[x * 3 + 0] = src[x * channels + 0];
                dst[x * 3 + 1] = src[x * channels + 1];
                dst[x * 3 + 2] = src[x * channels + 2];
            } else {
                dst[x * 3 + 0] = src[x];
                dst[x * 3 + 1] = src[x];
                dst[x * 3 + 2] = src[x];
            }
        }
    }

    free(raw);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    out->width = width;
    out->height = height;
    out->rgb = rgb;
    return 0;
}

static int load_jpeg_rgb(const char *path, image_asset_t *out) {
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
    uint8_t *rgb = malloc((size_t)width * height * 3);
    uint8_t *row = malloc((size_t)width * channels);
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
        memcpy(rgb + (size_t)y * width * 3, row, (size_t)width * 3);
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

static int load_image_rgb(const char *path, image_asset_t *out) {
    const char *ext = strrchr(path, '.');
    memset(out, 0, sizeof(*out));
    if (ext && strcasecmp(ext, ".png") == 0) return load_png_rgb(path, out);
    if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0)) {
        return load_jpeg_rgb(path, out);
    }
    return -1;
}

static void fill_rect_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                           uint8_t r, uint8_t g, uint8_t b) {
    uint8_t yy, uu, vv;
    rgb_to_yuv(r, g, b, &yy, &uu, &vv);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    uint8_t *yp = dst;
    uint8_t *uv = dst + stride * SCREEN_H;
    for (int row = y; row < y + h; ++row) {
        memset(yp + row * stride + x, yy, (size_t)w);
    }
    int uv_y0 = y / 2;
    int uv_y1 = (y + h + 1) / 2;
    int uv_x0 = x & ~1;
    int uv_x1 = (x + w + 1) & ~1;
    for (int row = uv_y0; row < uv_y1; ++row) {
        uint8_t *p = uv + row * stride + uv_x0;
        for (int col = uv_x0; col < uv_x1; col += 2) {
            p[0] = uu;
            p[1] = vv;
            p += 2;
        }
    }
}

static loop_asset_t *find_loop_asset(const char *tile_name) {
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        if (strcmp(g_loop_assets[i].tile_name, tile_name) == 0) return &g_loop_assets[i];
    }
    return NULL;
}

static void draw_rgb_image_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                                const image_asset_t *img) {
    if (!img || !img->rgb || img->width <= 0 || img->height <= 0 || w <= 0 || h <= 0) return;

    int out_w = w;
    int out_h = (int)((int64_t)img->height * w / img->width);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)img->width * h / img->height);
    }
    if (out_w <= 0 || out_h <= 0) return;

    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;
    uint8_t *yp = dst;
    uint8_t *uv = dst + stride * SCREEN_H;

    for (int dy = 0; dy < out_h; ++dy) {
        int sy = dy * img->height / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = yp + yy * stride;
        for (int dx = 0; dx < out_w; ++dx) {
            int sx = dx * img->width / out_w;
            int xx = ox + dx;
            if (xx < 0 || xx >= SCREEN_W) continue;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yv, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yv, &u, &v);
            (void)u;
            (void)v;
            drow[xx] = yv;
        }
    }

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy = dy * img->height / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = uv + (yy / 2) * stride;
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx = dx * img->width / out_w;
            int xx = (ox + dx) & ~1;
            if (xx < 0 || xx + 1 >= SCREEN_W) continue;
            const uint8_t *rgb = img->rgb + ((size_t)sy * img->width + sx) * 3;
            uint8_t yv, u, v;
            rgb_to_yuv(rgb[0], rgb[1], rgb[2], &yv, &u, &v);
            (void)yv;
            drow[xx] = u;
            drow[xx + 1] = v;
        }
    }
}

static void draw_rgba_frame_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                                 const uint8_t *rgba, int src_w, int src_h, int src_stride) {
    if (!rgba || src_w <= 0 || src_h <= 0 || src_stride < src_w * 4 || w <= 0 || h <= 0) return;

    int out_w = w;
    int out_h = (int)((int64_t)src_h * w / src_w);
    if (out_h > h) {
        out_h = h;
        out_w = (int)((int64_t)src_w * h / src_h);
    }
    if (out_w <= 0 || out_h <= 0) return;

    int ox = x + (w - out_w) / 2;
    int oy = y + (h - out_h) / 2;
    uint8_t *yp = dst;
    uint8_t *uv = dst + stride * SCREEN_H;

    for (int dy = 0; dy < out_h; ++dy) {
        int sy = dy * src_h / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = yp + yy * stride;
        for (int dx = 0; dx < out_w; ++dx) {
            int sx = dx * src_w / out_w;
            int xx = ox + dx;
            if (xx < 0 || xx >= SCREEN_W) continue;
            const uint8_t *px = rgba + (size_t)sy * src_stride + sx * 4;
            uint8_t yv, u, v;
            rgb_to_yuv(px[0], px[1], px[2], &yv, &u, &v);
            (void)u;
            (void)v;
            drow[xx] = yv;
        }
    }

    for (int dy = 0; dy < out_h; dy += 2) {
        int sy = dy * src_h / out_h;
        int yy = oy + dy;
        if (yy < 0 || yy >= SCREEN_H) continue;
        uint8_t *drow = uv + (yy / 2) * stride;
        for (int dx = 0; dx < out_w; dx += 2) {
            int sx = dx * src_w / out_w;
            int xx = (ox + dx) & ~1;
            if (xx < 0 || xx + 1 >= SCREEN_W) continue;
            const uint8_t *px = rgba + (size_t)sy * src_stride + sx * 4;
            uint8_t yv, u, v;
            rgb_to_yuv(px[0], px[1], px[2], &yv, &u, &v);
            (void)yv;
            drow[xx] = u;
            drow[xx + 1] = v;
        }
    }
}

static void stroke_rect_nv12(uint8_t *dst, int stride, int x, int y, int w, int h,
                             int thick, uint8_t r, uint8_t g, uint8_t b) {
    fill_rect_nv12(dst, stride, x, y, w, thick, r, g, b);
    fill_rect_nv12(dst, stride, x, y + h - thick, w, thick, r, g, b);
    fill_rect_nv12(dst, stride, x, y, thick, h, r, g, b);
    fill_rect_nv12(dst, stride, x + w - thick, y, thick, h, r, g, b);
}

static uint8_t glyph_row(char c, int row) {
    static const uint8_t digits[10][7] = {
        {31,17,19,21,25,17,31}, {4,12,4,4,4,4,14}, {31,1,1,31,16,16,31},
        {31,1,1,15,1,1,31}, {17,17,17,31,1,1,1}, {31,16,16,31,1,1,31},
        {31,16,16,31,17,17,31}, {31,1,2,4,8,8,8}, {31,17,17,31,17,17,31},
        {31,17,17,31,1,1,31},
    };
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {15,16,16,16,16,16,15},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {15,16,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,27,17}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    };
    if (c >= '0' && c <= '9') return digits[c - '0'][row];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'][row];
    if (c == '-') return row == 3 ? 31 : 0;
    if (c == '_') return row == 6 ? 31 : 0;
    if (c == ':') return (row == 2 || row == 4) ? 4 : 0;
    if (c == '.') return row == 6 ? 4 : 0;
    if (c == '/') return 1 << (6 - row > 4 ? 4 : (6 - row));
    if (c == '>') return row < 3 ? (1 << row) : (row == 3 ? 16 : (1 << (6 - row)));
    return 0;
}

static void draw_text(uint8_t *dst, int stride, int x, int y, const char *text,
                      int scale, uint8_t r, uint8_t g, uint8_t b) {
    int cx = x;
    for (const char *p = text; *p; ++p) {
        if (*p == ' ') {
            cx += 4 * scale;
            continue;
        }
        for (int row = 0; row < 7; ++row) {
            uint8_t bits = glyph_row(*p, row);
            for (int col = 0; col < 5; ++col) {
                if (bits & (1 << (4 - col))) {
                    fill_rect_nv12(dst, stride, cx + col * scale, y + row * scale,
                                   scale, scale, r, g, b);
                }
            }
        }
        cx += 6 * scale;
    }
}

static void draw_camera_tile(uint8_t *dst, int dstride, int dx, int dy, int dw, int dh,
                             const uint8_t *src, int sw, int sh, int sstride) {
    if (!src) return;
    const uint8_t *sy = src;
    const uint8_t *suv = src + sstride * sh;
    uint8_t *dy_base = dst;
    uint8_t *duv_base = dst + dstride * SCREEN_H;

    for (int y = 0; y < dh; ++y) {
        int src_y = y * sh / dh;
        uint8_t *drow = dy_base + (dy + y) * dstride + dx;
        const uint8_t *srow = sy + src_y * sstride;
        for (int x = 0; x < dw; ++x) {
            drow[x] = srow[x * sw / dw];
        }
    }
    for (int y = 0; y < dh / 2; ++y) {
        int src_y = (y * 2) * sh / dh;
        const uint8_t *srow = suv + (src_y / 2) * sstride;
        uint8_t *drow = duv_base + ((dy / 2) + y) * dstride + (dx & ~1);
        for (int x = 0; x < dw; x += 2) {
            int src_x = ((x * sw / dw) & ~1);
            drow[x] = srow[src_x];
            drow[x + 1] = srow[src_x + 1];
        }
    }
}

static const char *tile_status_text(int status) {
    switch (status) {
    case TILE_LIVE: return "LIVE";
    case TILE_PROBED: return "PROBED";
    case TILE_LOOP: return "LOOP";
    case TILE_SYNTH: return "SYNTH";
    default: return "OFFLINE";
    }
}

static void tile_status_color(int status, uint8_t *r, uint8_t *g, uint8_t *b) {
    switch (status) {
    case TILE_LIVE:
        *r = 80; *g = 255; *b = 180; return;
    case TILE_PROBED:
        *r = 120; *g = 190; *b = 255; return;
    case TILE_LOOP:
        *r = 255; *g = 220; *b = 100; return;
    case TILE_SYNTH:
        *r = 160; *g = 180; *b = 205; return;
    default:
        *r = 255; *g = 120; *b = 90; return;
    }
}

static void draw_effect_tile(uint8_t *dst, int stride, int x, int y, int w, int h,
                             int idx, int frame, int active, const uint8_t *osd_live,
                             const uint8_t *resize_live, const uint8_t *vpss_live,
                             const uint8_t *csc_rga_live, const uint8_t *transform_live,
                             const uint8_t *cap_live, const uint8_t *dcp_live,
                             const uint8_t *conv_live, const uint8_t *clahe_live,
                             const uint8_t *stereo_live) {
    uint8_t r0 = (uint8_t)((idx * 47 + frame * 2) % 180 + 40);
    uint8_t g0 = (uint8_t)((idx * 83 + frame * 3) % 180 + 40);
    uint8_t b0 = (uint8_t)((idx * 29 + frame * 5) % 180 + 40);
    uint8_t sr, sg, sb;
    tile_status_color(g_tiles[idx].status, &sr, &sg, &sb);
    fill_rect_nv12(dst, stride, x, y, w, h, 6, 12, 20);
    loop_asset_t *loop = find_loop_asset(g_tiles[idx].name);
    if (strcmp(g_tiles[idx].name, "OSD") == 0 && osd_live) {
        draw_camera_tile(dst, stride, x + 6, y + 30, w - 12, h - 38,
                         osd_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > OSD", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "VPSS") == 0 && vpss_live) {
        draw_camera_tile(dst, stride, x + 6, y + 30, w - 12, h - 38,
                         vpss_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > VPSS", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "RESIZE_RGA") == 0 && resize_live) {
        draw_camera_tile(dst, stride, x + 6, y + 30, w - 12, h - 38,
                         resize_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > RESIZE", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "CSC_RGA") == 0 && csc_rga_live) {
        draw_camera_tile(dst, stride, x + 6, y + 30, w - 12, h - 38,
                         csc_rga_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > CSC_RGA", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "TRANSFORM") == 0 && transform_live) {
        draw_camera_tile(dst, stride, x + 6, y + 30, w - 12, h - 38,
                         transform_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH > TRANSFORM", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "CAP_DEHAZE") == 0 && cap_live) {
        image_asset_t rgb_frame = {CAM_W, CAM_H, (uint8_t *)cap_live};
        draw_rgb_image_nv12(dst, stride, x + 6, y + 30, w - 12, h - 38, &rgb_frame);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH RGB > CAP", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "DCP_FAST_DEHAZE") == 0 && dcp_live) {
        image_asset_t rgb_frame = {CAM_W, CAM_H, (uint8_t *)dcp_live};
        draw_rgb_image_nv12(dst, stride, x + 6, y + 30, w - 12, h - 38, &rgb_frame);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH RGB > DCP", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "CONV_CL") == 0 && conv_live) {
        draw_rgba_frame_nv12(dst, stride, x + 6, y + 30, w - 12, h - 38,
                             conv_live, CAM_W, CAM_H, CAM_W * 4);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH RGBA > CONV", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "CLAHE") == 0 && clahe_live) {
        draw_camera_tile(dst, stride, x + 6, y + 30, w - 12, h - 38,
                         clahe_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "SYNTH NV12 > CLAHE", 1, 220, 255, 230);
    } else if (strcmp(g_tiles[idx].name, "STEREO_3D") == 0 && stereo_live) {
        draw_camera_tile(dst, stride, x + 6, y + 30, w - 12, h - 38,
                         stereo_live, CAM_W, CAM_H, CAM_STRIDE);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, "CAMERA > STEREO", 1, 220, 255, 230);
    } else if (loop && loop->loaded_count > 0) {
        const image_asset_t *img = &loop->images[(frame / 30) % loop->loaded_count];
        draw_rgb_image_nv12(dst, stride, x + 6, y + 30, w - 12, h - 38, img);
        fill_rect_nv12(dst, stride, x + 6, y + h - 24, w - 12, 18, 0, 0, 0);
        draw_text(dst, stride, x + 12, y + h - 22, loop->caption, 1, 220, 255, 230);
    } else {
        fill_rect_nv12(dst, stride, x + 14, y + 48, 44, 44, r0, g0, b0);
        fill_rect_nv12(dst, stride, x + 66, y + 52, w - 82, 12, 16, 28, 42);
        fill_rect_nv12(dst, stride, x + 66, y + 74, (w - 82) * 3 / 4, 12, 16, 28, 42);
        if (g_tiles[idx].status == TILE_PROBED) {
            draw_text(dst, stride, x + 14, y + 108, "INIT OK", 1, 190, 230, 255);
            draw_text(dst, stride, x + 14, y + 126, "NO LIVE OUTPUT", 1, 140, 170, 200);
        } else if (g_tiles[idx].status == TILE_LIVE) {
            draw_text(dst, stride, x + 14, y + 108, "LIVE MODULE", 1, 190, 255, 220);
            draw_text(dst, stride, x + 14, y + 126, "REAL FRAMES", 1, 140, 210, 180);
        } else {
            draw_text(dst, stride, x + 14, y + 108, "SYNTHETIC FEED", 1, 190, 230, 255);
            draw_text(dst, stride, x + 14, y + 126, "NO LIVE ALGO", 1, 140, 170, 200);
        }
    }
    stroke_rect_nv12(dst, stride, x, y, w, h, active ? 3 : 1,
                     active ? sr : 70, active ? sg : 70, active ? sb : 70);
    int name_scale = strlen(g_tiles[idx].name) > 11 ? 1 : 2;
    draw_text(dst, stride, x + 10, y + 10, g_tiles[idx].name, name_scale,
              active ? 170 : 90, active ? 255 : 90, active ? 220 : 90);
    fill_rect_nv12(dst, stride, x + w - 74, y + 8, 64, 20, 0, 0, 0);
    draw_text(dst, stride, x + w - 68, y + 12, tile_status_text(g_tiles[idx].status), 1, sr, sg, sb);
}

static void draw_tile_content(uint8_t *dst, int stride, int x, int y, int w, int h,
                              int idx, int frame, const uint8_t *cam, const uint8_t *osd_live,
                              const uint8_t *resize_live, const uint8_t *vpss_live,
                              const uint8_t *csc_rga_live, const uint8_t *transform_live,
                              const uint8_t *cap_live, const uint8_t *dcp_live,
                              const uint8_t *conv_live, const uint8_t *clahe_live,
                              const uint8_t *stereo_live) {
    loop_asset_t *loop = find_loop_asset(g_tiles[idx].name);
    fill_rect_nv12(dst, stride, x, y, w, h, 7, 13, 24);

    if (strcmp(g_tiles[idx].name, "OSD") == 0 && osd_live) {
        draw_camera_tile(dst, stride, x + 12, y + 12, w - 24, h - 24,
                         osd_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "VPSS") == 0 && vpss_live) {
        draw_camera_tile(dst, stride, x + 12, y + 12, w - 24, h - 24,
                         vpss_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "RESIZE_RGA") == 0 && resize_live) {
        draw_camera_tile(dst, stride, x + 12, y + 12, w - 24, h - 24,
                         resize_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "CSC_RGA") == 0 && csc_rga_live) {
        draw_camera_tile(dst, stride, x + 12, y + 12, w - 24, h - 24,
                         csc_rga_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "TRANSFORM") == 0 && transform_live) {
        draw_camera_tile(dst, stride, x + 12, y + 12, w - 24, h - 24,
                         transform_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "CAP_DEHAZE") == 0 && cap_live) {
        image_asset_t rgb_frame = {CAM_W, CAM_H, (uint8_t *)cap_live};
        draw_rgb_image_nv12(dst, stride, x + 12, y + 12, w - 24, h - 24, &rgb_frame);
        return;
    }

    if (strcmp(g_tiles[idx].name, "DCP_FAST_DEHAZE") == 0 && dcp_live) {
        image_asset_t rgb_frame = {CAM_W, CAM_H, (uint8_t *)dcp_live};
        draw_rgb_image_nv12(dst, stride, x + 12, y + 12, w - 24, h - 24, &rgb_frame);
        return;
    }

    if (strcmp(g_tiles[idx].name, "CONV_CL") == 0 && conv_live) {
        draw_rgba_frame_nv12(dst, stride, x + 12, y + 12, w - 24, h - 24,
                             conv_live, CAM_W, CAM_H, CAM_W * 4);
        return;
    }

    if (strcmp(g_tiles[idx].name, "CLAHE") == 0 && clahe_live) {
        draw_camera_tile(dst, stride, x + 12, y + 12, w - 24, h - 24,
                         clahe_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "STEREO_3D") == 0 && stereo_live) {
        draw_camera_tile(dst, stride, x + 12, y + 12, w - 24, h - 24,
                         stereo_live, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (strcmp(g_tiles[idx].name, "VI") == 0 && cam) {
        draw_camera_tile(dst, stride, x + 12, y + 12, w - 24, h - 24,
                         cam, CAM_W, CAM_H, CAM_STRIDE);
        return;
    }

    if (loop && loop->loaded_count > 0) {
        const image_asset_t *img = &loop->images[(frame / FPS) % loop->loaded_count];
        draw_rgb_image_nv12(dst, stride, x + 12, y + 12, w - 24, h - 24, img);
        return;
    }

    uint8_t sr, sg, sb;
    tile_status_color(g_tiles[idx].status, &sr, &sg, &sb);
    fill_rect_nv12(dst, stride, x + 60, y + 70, w - 120, 150, 10, 22, 34);
    stroke_rect_nv12(dst, stride, x + 60, y + 70, w - 120, 150, 5, sr, sg, sb);
    draw_text(dst, stride, x + 92, y + 114, g_tiles[idx].name, strlen(g_tiles[idx].name) > 11 ? 3 : 4, 190, 255, 230);
    draw_text(dst, stride, x + 92, y + 198,
              g_tiles[idx].status == TILE_PROBED ? "INIT OK  NO LIVE OUTPUT" : "SYNTHETIC PLACEHOLDER",
              2, 170, 210, 240);

    const char *state =
        g_tiles[idx].status == TILE_PROBED ? "API PROBE PASSED" :
        g_tiles[idx].status == TILE_LIVE ? "LIVE FRAMES" : "OFFLINE";
    const char *source =
        g_tiles[idx].status == TILE_PROBED ? "NO DISPLAY FRAME API" :
        g_tiles[idx].status == TILE_LIVE ? "MODULE OUTPUT" : "WAITING FOR SAFE PATH";
    const char *mode =
        loop ? "LOOP ASSET" : "MODULE TILE";
    int row_y = y + h - 206;
    for (int i = 0; i < 3; ++i) {
        const char *label = i == 0 ? "STATE" : (i == 1 ? "SOURCE" : "MODE");
        const char *value = i == 0 ? state : (i == 1 ? source : mode);
        fill_rect_nv12(dst, stride, x + 88, row_y + i * 50, w - 176, 34, 8, 18, 30);
        draw_text(dst, stride, x + 110, row_y + 8 + i * 50, label, 1, 110, 170, 210);
        draw_text(dst, stride, x + 250, row_y + 8 + i * 50, value, 1, 190, 230, 255);
    }
}

static void draw_main_showcase(uint8_t *canvas, int stride, int frame,
                               int rotate_main, const char *only_tile,
                               const uint8_t *cam, const uint8_t *osd_live,
                               const uint8_t *resize_live, const uint8_t *vpss_live,
                               const uint8_t *csc_rga_live, const uint8_t *transform_live,
                               const uint8_t *cap_live, const uint8_t *dcp_live,
                               const uint8_t *conv_live, const uint8_t *clahe_live,
                               const uint8_t *stereo_live) {
    const int x = 28;
    const int y = 116;
    const int w = 1024;
    const int h = 612;
    int idx = -1;

    if (only_tile) {
        idx = find_tile_index(only_tile);
    } else if (rotate_main) {
        idx = TILE_FIRST_INDEX + (frame / (FPS * MAIN_ROTATE_SECONDS)) % TILE_ROTATE_COUNT;
    }

    fill_rect_nv12(canvas, stride, x, y, w, h, 7, 13, 24);
    if (idx >= 0) {
        uint8_t sr, sg, sb;
        tile_status_color(g_tiles[idx].status, &sr, &sg, &sb);
        draw_tile_content(canvas, stride, x + 14, y + 14, w - 28, h - 70,
                          idx, frame, cam, osd_live, resize_live, vpss_live,
                          csc_rga_live, transform_live, cap_live, dcp_live, conv_live, clahe_live, stereo_live);
        fill_rect_nv12(canvas, stride, x + 14, y + h - 50, w - 28, 34, 0, 0, 0);
        draw_text(canvas, stride, x + 32, y + h - 42, "MAIN ROTATE", 2, 190, 230, 255);
        draw_text(canvas, stride, x + 268, y + h - 42, g_tiles[idx].name,
                  strlen(g_tiles[idx].name) > 11 ? 1 : 2, 170, 255, 220);
        draw_text(canvas, stride, x + 786, y + h - 42, tile_status_text(g_tiles[idx].status), 2, sr, sg, sb);
    } else if (cam) {
        draw_camera_tile(canvas, stride, 42, 130, 996, 560, cam, CAM_W, CAM_H, CAM_STRIDE);
        draw_text(canvas, stride, 52, 704, "LIVE VISIBLE CAMERA  VI > VPSS > WALL", 2, 190, 255, 230);
    } else {
        draw_text(canvas, stride, 294, 336, "CAMERA OFFLINE", 4, 255, 180, 80);
        draw_text(canvas, stride, 318, 406, CAMERA_DEVICE, 2, 190, 230, 255);
    }
    stroke_rect_nv12(canvas, stride, x, y, w, h, 4, 0, 220, 180);
}

static int map_buffer(MEDIA_BUFFER buf, void **addr, size_t *size, int prot) {
    int fd = -1;
    if (MEDIA_POOL_GetFd(buf, &fd, size) != 0 || fd < 0 || *size == 0) return -1;
    *addr = mmap(NULL, *size, prot, MAP_SHARED, fd, 0);
    if (*addr == MAP_FAILED) {
        *addr = NULL;
        return -1;
    }
    return 0;
}

static void fill_synthetic_nv12(uint8_t *p, int w, int h, int stride, int frame) {
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            p[yy * stride + xx] = (uint8_t)((xx * 2 + yy + frame * 5) & 255);
        }
    }
    uint8_t *uv = p + stride * h;
    for (int yy = 0; yy < h / 2; ++yy) {
        for (int xx = 0; xx < w; xx += 2) {
            uv[yy * stride + xx] = (uint8_t)(90 + ((xx + frame) & 63));
            uv[yy * stride + xx + 1] = (uint8_t)(130 + ((yy * 2 + frame) & 63));
        }
    }
}

static void fill_synthetic_rgb(uint8_t *p, int w, int h, int stride, int frame) {
    for (int yy = 0; yy < h; ++yy) {
        uint8_t *row = p + (size_t)yy * stride;
        for (int xx = 0; xx < w; ++xx) {
            row[xx * 3 + 0] = (uint8_t)((xx * 2 + frame * 4) & 255);
            row[xx * 3 + 1] = (uint8_t)((yy * 2 + frame * 3) & 255);
            row[xx * 3 + 2] = (uint8_t)(((xx + yy) + frame * 5) & 255);
        }
    }
}

static void fill_synthetic_rgba(uint8_t *p, int w, int h, int stride, int frame) {
    for (int yy = 0; yy < h; ++yy) {
        uint8_t *row = p + (size_t)yy * stride;
        for (int xx = 0; xx < w; ++xx) {
            row[xx * 4 + 0] = (uint8_t)((xx * 2 + frame * 4) & 255);
            row[xx * 4 + 1] = (uint8_t)((yy * 2 + frame * 3) & 255);
            row[xx * 4 + 2] = (uint8_t)(((xx + yy) + frame * 5) & 255);
            row[xx * 4 + 3] = 255;
        }
    }
}

static MEDIA_BUFFER make_work_frame(int pool, int w, int h, int stride, int frame) {
    MEDIA_BUFFER buf = {-1, -1};
    if (MEDIA_POOL_GetBuffer(pool, &buf) != 0) return buf;
    void *addr = NULL;
    size_t size = 0;
    if (map_buffer(buf, &addr, &size, PROT_READ | PROT_WRITE) != 0) {
        MEDIA_POOL_PutBuffer(buf);
        buf.pool_id = -1;
        return buf;
    }
    if (MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_WRITE) != 0) {
        munmap(addr, size);
        MEDIA_POOL_PutBuffer(buf);
        buf.pool_id = -1;
        return buf;
    }
    (void)size;
    fill_synthetic_nv12((uint8_t *)addr, w, h, stride, frame);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    munmap(addr, size);
    return buf;
}

static int copy_to_buffer(MEDIA_BUFFER buf, const uint8_t *src, size_t need) {
    void *addr = NULL;
    size_t size = 0;
    if (!src) return -1;
    if (map_buffer(buf, &addr, &size, PROT_READ | PROT_WRITE) != 0) return -1;
    if (MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_WRITE) != 0) {
        munmap(addr, size);
        return -1;
    }
    if (size < need) {
        (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
        munmap(addr, size);
        return -1;
    }
    memcpy(addr, src, need);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_WRITE);
    munmap(addr, size);
    return 0;
}

static int copy_from_buffer(MEDIA_BUFFER buf, uint8_t *dst, size_t need) {
    void *addr = NULL;
    size_t size = 0;
    if (!dst) return -1;
    if (MEDIA_POOL_BeginCpuAccess(buf, DMA_BUF_SYNC_READ) != 0) return -1;
    if (map_buffer(buf, &addr, &size, PROT_READ) != 0) {
        (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
        return -1;
    }
    if (size < need) {
        munmap(addr, size);
        (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
        return -1;
    }
    memcpy(dst, addr, need);
    munmap(addr, size);
    (void)MEDIA_POOL_EndCpuAccess(buf, DMA_BUF_SYNC_READ);
    return 0;
}

static int send_copied_frame(const char *module, int grp, const char *port,
                             int pool, const uint8_t *src, size_t need, int timeout_ms) {
    MEDIA_BUFFER in = {-1, -1};
    if (MEDIA_POOL_GetBuffer(pool, &in) != 0) return -1;
    if (copy_to_buffer(in, src, need) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_SYS_SendFrame(module, grp, port, in, timeout_ms) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    MEDIA_POOL_PutBuffer(in);
    return 0;
}

static int setup_live_osd(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_OSD_ATTR attr = {0};
    attr.input_width = CAM_W;
    attr.input_height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.max_regions = 3;
    if (MEDIA_OSD_CreateGrp(LIVE_OSD_GRP, &attr) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }

    MEDIA_OSD_REGION_ATTR box_attr = {0};
    box_attr.enabled = 1;
    box_attr.x = 36;
    box_attr.y = 36;
    box_attr.width = 240;
    box_attr.height = 140;
    box_attr.zorder = 0;
    box_attr.global_alpha = 255;
    MEDIA_OSD_RECT_DESC box = {0};
    box.filled = 0;
    box.line_width = 5;
    box.color.r = 0;
    box.color.g = 255;
    box.color.b = 190;
    box.color.a = 255;
    if (MEDIA_OSD_UpdateRegion(LIVE_OSD_GRP, 0, &box_attr) != 0 ||
        MEDIA_OSD_SetRegionRect(LIVE_OSD_GRP, 0, &box) != 0) {
        MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }

    MEDIA_OSD_REGION_ATTR hud_attr = {0};
    hud_attr.enabled = 1;
    hud_attr.x = 36;
    hud_attr.y = 520;
    hud_attr.width = 420;
    hud_attr.height = 54;
    hud_attr.zorder = 1;
    hud_attr.global_alpha = 180;
    MEDIA_OSD_RECT_DESC hud = {0};
    hud.filled = 1;
    hud.line_width = 1;
    hud.color.r = 8;
    hud.color.g = 18;
    hud.color.b = 30;
    hud.color.a = 255;
    if (MEDIA_OSD_UpdateRegion(LIVE_OSD_GRP, 1, &hud_attr) != 0 ||
        MEDIA_OSD_SetRegionRect(LIVE_OSD_GRP, 1, &hud) != 0) {
        MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }

    if (MEDIA_OSD_Start(LIVE_OSD_GRP) != 0) {
        MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("OSD", TILE_LIVE);
    return 0;
}

static void cleanup_live_osd(int enabled) {
    if (!enabled) return;
    MEDIA_OSD_Stop(LIVE_OSD_GRP);
    MEDIA_OSD_DestroyGrp(LIVE_OSD_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_osd(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, CAM_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_OSD_SendFrame(LIVE_OSD_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_OSD_GetFrame(LIVE_OSD_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_OSD_ReleaseFrame(LIVE_OSD_GRP, out);
    }
    return ret;
}

static int setup_live_resize(void) {
    if (MEDIA_POOL_Create(RESIZE_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(RESIZE_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(RESIZE_INPUT_POOL);
        return -1;
    }

    MEDIA_RESIZE_RGA_ATTR attr = {0};
    attr.src_x = 120;
    attr.src_y = 120;
    attr.src_width = 400;
    attr.src_height = 400;
    attr.input_stride = CAM_STRIDE;
    attr.input_format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.out_width = CAM_W;
    attr.out_height = CAM_H;
    attr.out_stride = CAM_STRIDE;
    attr.output_format = MEDIA_FORMAT_NV12;
    attr.output_pool_id = RESIZE_OUTPUT_POOL;

    if (MEDIA_RESIZE_RGA_CreateGrp(LIVE_RESIZE_GRP, &attr) != 0 ||
        MEDIA_RESIZE_RGA_Start(LIVE_RESIZE_GRP) != 0 ||
        MEDIA_RESIZE_RGA_Enable(LIVE_RESIZE_GRP) != 0) {
        MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
        MEDIA_POOL_Destroy(RESIZE_INPUT_POOL);
        MEDIA_POOL_Destroy(RESIZE_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("RESIZE_RGA", TILE_LIVE);
    return 0;
}

static void cleanup_live_resize(int enabled) {
    if (!enabled) return;
    MEDIA_RESIZE_RGA_Disable(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_Stop(LIVE_RESIZE_GRP);
    MEDIA_RESIZE_RGA_DestroyGrp(LIVE_RESIZE_GRP);
    MEDIA_POOL_Destroy(RESIZE_INPUT_POOL);
    MEDIA_POOL_Destroy(RESIZE_OUTPUT_POOL);
}

static int process_live_resize(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("RESIZE_RGA", LIVE_RESIZE_GRP, "input0",
                          RESIZE_INPUT_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_RESIZE_RGA_GetFrame(LIVE_RESIZE_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_RESIZE_RGA_ReleaseFrame(LIVE_RESIZE_GRP, out);
    }
    return ret;
}

static int setup_live_vpss(void) {
    if (MEDIA_POOL_Create(VPSS_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(VPSS_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        return -1;
    }

    MEDIA_VPSS_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.input_stride = CAM_STRIDE;
    attr.input_depth = 3;
    attr.input_format = MEDIA_FORMAT_NV12;
    attr.output_count = 1;
    attr.outputs[0].output_id = 0;
    attr.outputs[0].out_width = CAM_W;
    attr.outputs[0].out_height = CAM_H;
    attr.outputs[0].out_stride = CAM_STRIDE;
    attr.outputs[0].pool_id = VPSS_OUTPUT_POOL;
    attr.outputs[0].crop_x = 80;
    attr.outputs[0].crop_y = 80;
    attr.outputs[0].crop_w = 480;
    attr.outputs[0].crop_h = 480;
    attr.outputs[0].output_format = MEDIA_FORMAT_NV12;

    if (MEDIA_VPSS_SetAttr(LIVE_VPSS_GRP, &attr) != 0 ||
        MEDIA_VPSS_Enable(LIVE_VPSS_GRP) != 0) {
        MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
        MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
        MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("VPSS", TILE_LIVE);
    return 0;
}

static void cleanup_live_vpss(int enabled) {
    if (!enabled) return;
    MEDIA_VPSS_Disable(LIVE_VPSS_GRP);
    MEDIA_VPSS_DestroyGrp(LIVE_VPSS_GRP);
    MEDIA_POOL_Destroy(VPSS_INPUT_POOL);
    MEDIA_POOL_Destroy(VPSS_OUTPUT_POOL);
}

static int process_live_vpss(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(VPSS_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, CAM_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_VPSS_Group_SendFrame(LIVE_VPSS_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_VPSS_Chn_GetFrame(LIVE_VPSS_GRP, 0, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_VPSS_Chn_ReleaseFrame(LIVE_VPSS_GRP, 0, out);
    }
    return ret;
}

static int setup_live_csc_rga(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_CSC_RGA_ATTR attr = {0};
    attr.input_width = CAM_W;
    attr.input_height = CAM_H;
    attr.input_format = MEDIA_FORMAT_NV12;
    attr.output_format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.csc_mode = 0;

    if (MEDIA_CSC_RGA_CreateGrp(LIVE_CSC_RGA_GRP, &attr) != 0 ||
        MEDIA_CSC_RGA_Start(LIVE_CSC_RGA_GRP) != 0 ||
        MEDIA_CSC_RGA_Enable(LIVE_CSC_RGA_GRP) != 0) {
        MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CSC_RGA", TILE_LIVE);
    return 0;
}

static void cleanup_live_csc_rga(int enabled) {
    if (!enabled) return;
    MEDIA_CSC_RGA_Disable(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_Stop(LIVE_CSC_RGA_GRP);
    MEDIA_CSC_RGA_DestroyGrp(LIVE_CSC_RGA_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_csc_rga(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("CSC_RGA", LIVE_CSC_RGA_GRP, "input",
                          OSD_INPUT_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_CSC_RGA_GetFrame(LIVE_CSC_RGA_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_CSC_RGA_ReleaseFrame(LIVE_CSC_RGA_GRP, out);
    }
    return ret;
}

static float *g_transform_lut = NULL;

static int build_transform_lut(void) {
    size_t count = (size_t)CAM_W * CAM_H * 2;
    g_transform_lut = malloc(count * sizeof(float));
    if (!g_transform_lut) return -1;
    float cx = (float)(CAM_W - 1) * 0.5f;
    float cy = (float)(CAM_H - 1) * 0.5f;
    for (int y = 0; y < CAM_H; ++y) {
        for (int x = 0; x < CAM_W; ++x) {
            float dx = (float)x - cx;
            float dy = (float)y - cy;
            float radius = sqrtf(dx * dx + dy * dy);
            float wave = sinf(radius * 0.035f) * 10.0f;
            float sx = (float)x + wave * (dy / (radius + 1.0f));
            float sy = (float)y - wave * (dx / (radius + 1.0f));
            if (sx < 0.0f) sx = 0.0f;
            if (sy < 0.0f) sy = 0.0f;
            if (sx > (float)(CAM_W - 1)) sx = (float)(CAM_W - 1);
            if (sy > (float)(CAM_H - 1)) sy = (float)(CAM_H - 1);
            size_t off = ((size_t)y * CAM_W + x) * 2;
            g_transform_lut[off] = sx;
            g_transform_lut[off + 1] = sy;
        }
    }
    return 0;
}

static int setup_live_transform(void) {
    if (build_transform_lut() != 0) return -1;
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail_lut;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail_input_pool;

    MEDIA_TRANSFORM_ATTR attr = {0};
    attr.out_width = CAM_W;
    attr.out_height = CAM_H;
    attr.out_stride = CAM_STRIDE;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.pool_id = OSD_OUTPUT_POOL;
    attr.in_width = CAM_W;
    attr.in_height = CAM_H;
    attr.in_stride = CAM_STRIDE;
    attr.lut_width = CAM_W;
    attr.lut_height = CAM_H;
    attr.lut = g_transform_lut;
    attr.lut_size = (size_t)CAM_W * CAM_H * 2 * sizeof(float);

    if (MEDIA_TRANSFORM_CreateGrp(LIVE_TRANSFORM_GRP, &attr) != 0 ||
        MEDIA_TRANSFORM_Start(LIVE_TRANSFORM_GRP) != 0) {
        MEDIA_TRANSFORM_DestroyGrp(LIVE_TRANSFORM_GRP);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        goto fail_lut;
    }
    set_tile_status("TRANSFORM", TILE_LIVE);
    return 0;

fail_input_pool:
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
fail_lut:
    free(g_transform_lut);
    g_transform_lut = NULL;
    return -1;
}

static void cleanup_live_transform(int enabled) {
    if (!enabled) return;
    MEDIA_TRANSFORM_Stop(LIVE_TRANSFORM_GRP);
    MEDIA_TRANSFORM_DestroyGrp(LIVE_TRANSFORM_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
    free(g_transform_lut);
    g_transform_lut = NULL;
}

static int process_live_transform(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, CAM_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_TRANSFORM_Process(LIVE_TRANSFORM_GRP, in, &out) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_POOL_PutBuffer(out);
    }
    MEDIA_POOL_PutBuffer(in);
    return ret;
}

static int setup_live_cap_dehaze(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, RGB_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, RGB_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_CAP_DEHAZE_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_RGB888;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_W * 3;
    attr.output_stride = CAM_W * 3;
    attr.guided_radius = 8;
    attr.guided_eps = 0.01f;
    attr.t0 = 0.1f;
    attr.beta0 = 1.0f;
    attr.beta1 = 1.0f;
    attr.beta2 = 1.0f;
    attr.depth_scale = 1.0f;

    if (MEDIA_CAP_DEHAZE_CreateGrp(LIVE_CAP_DEHAZE_GRP, &attr) != 0 ||
        MEDIA_CAP_DEHAZE_Start(LIVE_CAP_DEHAZE_GRP) != 0 ||
        MEDIA_CAP_DEHAZE_Enable(LIVE_CAP_DEHAZE_GRP) != 0) {
        MEDIA_CAP_DEHAZE_DestroyGrp(LIVE_CAP_DEHAZE_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CAP_DEHAZE", TILE_LIVE);
    return 0;
}

static void cleanup_live_cap_dehaze(int enabled) {
    if (!enabled) return;
    MEDIA_CAP_DEHAZE_Disable(LIVE_CAP_DEHAZE_GRP);
    MEDIA_CAP_DEHAZE_Stop(LIVE_CAP_DEHAZE_GRP);
    MEDIA_CAP_DEHAZE_DestroyGrp(LIVE_CAP_DEHAZE_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_cap_dehaze(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, RGB_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_CAP_DEHAZE_SendFrame(LIVE_CAP_DEHAZE_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_CAP_DEHAZE_GetFrame(LIVE_CAP_DEHAZE_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, RGB_FRAME_SIZE);
        MEDIA_CAP_DEHAZE_ReleaseFrame(LIVE_CAP_DEHAZE_GRP, out);
    }
    return ret;
}

static int setup_live_dcp_dehaze(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, RGB_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, RGB_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_DCP_FAST_DEHAZE_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_RGB888;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_W * 3;
    attr.output_stride = CAM_W * 3;
    attr.patch = 15;
    attr.omega = 0.95f;
    attr.t0 = 0.1f;
    attr.airlight_percent = 0.001f;
    attr.guided_radius = 8;
    attr.guided_eps = 0.01f;
    attr.refine_scale = 0.25f;

    if (MEDIA_DCP_FAST_DEHAZE_CreateGrp(LIVE_DCP_DEHAZE_GRP, &attr) != 0 ||
        MEDIA_DCP_FAST_DEHAZE_Start(LIVE_DCP_DEHAZE_GRP) != 0 ||
        MEDIA_DCP_FAST_DEHAZE_Enable(LIVE_DCP_DEHAZE_GRP) != 0) {
        MEDIA_DCP_FAST_DEHAZE_DestroyGrp(LIVE_DCP_DEHAZE_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("DCP_FAST_DEHAZE", TILE_LIVE);
    return 0;
}

static void cleanup_live_dcp_dehaze(int enabled) {
    if (!enabled) return;
    MEDIA_DCP_FAST_DEHAZE_Disable(LIVE_DCP_DEHAZE_GRP);
    MEDIA_DCP_FAST_DEHAZE_Stop(LIVE_DCP_DEHAZE_GRP);
    MEDIA_DCP_FAST_DEHAZE_DestroyGrp(LIVE_DCP_DEHAZE_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_dcp_dehaze(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER in = {-1, -1};
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (MEDIA_POOL_GetBuffer(OSD_INPUT_POOL, &in) != 0) return -1;
    if (copy_to_buffer(in, src, RGB_FRAME_SIZE) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_DCP_FAST_DEHAZE_SendFrame(LIVE_DCP_DEHAZE_GRP, in, 20) != 0) {
        MEDIA_POOL_PutBuffer(in);
        return -1;
    }
    if (MEDIA_DCP_FAST_DEHAZE_GetFrame(LIVE_DCP_DEHAZE_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, RGB_FRAME_SIZE);
        MEDIA_DCP_FAST_DEHAZE_ReleaseFrame(LIVE_DCP_DEHAZE_GRP, out);
    }
    return ret;
}

static int setup_live_conv_cl(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, RGBA_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, RGBA_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_CONV_CL_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_RGBA8888;
    attr.kernel_size = 5;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_W * 4;
    attr.output_stride = CAM_W * 4;

    if (MEDIA_CONV_CL_CreateGrp(LIVE_CONV_CL_GRP, &attr) != 0 ||
        MEDIA_CONV_CL_Start(LIVE_CONV_CL_GRP) != 0) {
        MEDIA_CONV_CL_DestroyGrp(LIVE_CONV_CL_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CONV_CL", TILE_LIVE);
    return 0;
}

static void cleanup_live_conv_cl(int enabled) {
    if (!enabled) return;
    MEDIA_CONV_CL_Stop(LIVE_CONV_CL_GRP);
    MEDIA_CONV_CL_DestroyGrp(LIVE_CONV_CL_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_conv_cl(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("CONV_CL", LIVE_CONV_CL_GRP, "input",
                          OSD_INPUT_POOL, src, RGBA_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_CONV_CL_GetFrame(LIVE_CONV_CL_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, RGBA_FRAME_SIZE);
        MEDIA_CONV_CL_ReleaseFrame(LIVE_CONV_CL_GRP, out);
    }
    return ret;
}

static int setup_live_clahe(void) {
    if (MEDIA_POOL_Create(OSD_INPUT_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(OSD_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) {
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        return -1;
    }

    MEDIA_CLAHE_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.tile_grid_x = 8;
    attr.tile_grid_y = 8;
    attr.bins = 256;
    attr.input_depth = 3;
    attr.output_pool_id = OSD_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.clip_limit = 2.5f;
    attr.highlight_protect_start = 0.92f;
    attr.highlight_protect_strength = 0.4f;

    if (MEDIA_CLAHE_CreateGrp(LIVE_CLAHE_GRP, &attr) != 0 ||
        MEDIA_CLAHE_Start(LIVE_CLAHE_GRP) != 0 ||
        MEDIA_CLAHE_Enable(LIVE_CLAHE_GRP) != 0) {
        MEDIA_CLAHE_DestroyGrp(LIVE_CLAHE_GRP);
        MEDIA_POOL_Destroy(OSD_INPUT_POOL);
        MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
        return -1;
    }
    set_tile_status("CLAHE", TILE_LIVE);
    return 0;
}

static void cleanup_live_clahe(int enabled) {
    if (!enabled) return;
    MEDIA_CLAHE_Disable(LIVE_CLAHE_GRP);
    MEDIA_CLAHE_Stop(LIVE_CLAHE_GRP);
    MEDIA_CLAHE_DestroyGrp(LIVE_CLAHE_GRP);
    MEDIA_POOL_Destroy(OSD_INPUT_POOL);
    MEDIA_POOL_Destroy(OSD_OUTPUT_POOL);
}

static int process_live_clahe(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("CLAHE", LIVE_CLAHE_GRP, "input",
                          OSD_INPUT_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_CLAHE_GetFrame(LIVE_CLAHE_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_CLAHE_ReleaseFrame(LIVE_CLAHE_GRP, out);
    }
    return ret;
}

static int setup_live_stereo(void) {
    if (MEDIA_POOL_Create(STEREO_INPUT0_POOL, CAM_FRAME_SIZE, 3) != 0) return -1;
    if (MEDIA_POOL_Create(STEREO_INPUT1_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail;
    if (MEDIA_POOL_Create(STEREO_OUTPUT_POOL, CAM_FRAME_SIZE, 3) != 0) goto fail;

    MEDIA_STEREO_3D_ATTR attr = {0};
    attr.width = CAM_W;
    attr.height = CAM_H;
    attr.format = MEDIA_FORMAT_NV12;
    attr.input_depth = 3;
    attr.output_pool_id = STEREO_OUTPUT_POOL;
    attr.input_stride = CAM_STRIDE;
    attr.output_stride = CAM_STRIDE;
    attr.mode = MEDIA_STEREO_3D_MODE_LINE_BY_LINE;
    if (MEDIA_STEREO_3D_CreateGrp(LIVE_STEREO_GRP, &attr) != 0 ||
        MEDIA_STEREO_3D_Enable(LIVE_STEREO_GRP) != 0) {
        goto fail;
    }
    set_tile_status("STEREO_3D", TILE_LIVE);
    return 0;

fail:
    MEDIA_STEREO_3D_DestroyGrp(LIVE_STEREO_GRP);
    MEDIA_POOL_Destroy(STEREO_INPUT0_POOL);
    MEDIA_POOL_Destroy(STEREO_INPUT1_POOL);
    MEDIA_POOL_Destroy(STEREO_OUTPUT_POOL);
    return -1;
}

static void cleanup_live_stereo(int enabled) {
    if (!enabled) return;
    MEDIA_STEREO_3D_Disable(LIVE_STEREO_GRP);
    MEDIA_STEREO_3D_DestroyGrp(LIVE_STEREO_GRP);
    MEDIA_POOL_Destroy(STEREO_INPUT0_POOL);
    MEDIA_POOL_Destroy(STEREO_INPUT1_POOL);
    MEDIA_POOL_Destroy(STEREO_OUTPUT_POOL);
}

static int process_live_stereo(const uint8_t *src, uint8_t *dst) {
    MEDIA_BUFFER out = {-1, -1};
    int ret = -1;
    if (send_copied_frame("STEREO_3D", LIVE_STEREO_GRP, "input0",
                          STEREO_INPUT0_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (send_copied_frame("STEREO_3D", LIVE_STEREO_GRP, "input1",
                          STEREO_INPUT1_POOL, src, CAM_FRAME_SIZE, 20) != 0) {
        return -1;
    }
    if (MEDIA_STEREO_3D_GetFrame(LIVE_STEREO_GRP, &out, 20) == 0) {
        ret = copy_from_buffer(out, dst, CAM_FRAME_SIZE);
        MEDIA_STEREO_3D_ReleaseFrame(LIVE_STEREO_GRP, out);
    }
    return ret;
}

static void mark(const char *name, int active) {
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (strcmp(g_tiles[i].name, name) == 0) {
            g_tiles[i].active = active;
            return;
        }
    }
}

static void set_tile_status(const char *name, int status) {
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (strcmp(g_tiles[i].name, name) == 0) {
            g_tiles[i].status = status;
            g_tiles[i].active = status != TILE_OFFLINE;
            return;
        }
    }
}

static int find_tile_index(const char *name) {
    if (!name || !*name) return -1;
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (strcasecmp(g_tiles[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int tile_needs_camera(const char *name) {
    if (!name) return 1;
    return strcasecmp(name, "VI") == 0 ||
           strcasecmp(name, "OSD") == 0 ||
           strcasecmp(name, "RESIZE_RGA") == 0 ||
           strcasecmp(name, "VPSS") == 0 ||
           strcasecmp(name, "CSC_RGA") == 0;
}

static int load_loop_assets(void) {
    int total = 0;
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        loop_asset_t *loop = &g_loop_assets[i];
        loop->loaded_count = 0;
        for (int j = 0; j < loop->path_count; ++j) {
            image_asset_t img = {0};
            if (load_image_rgb(loop->paths[j], &img) == 0) {
                loop->images[loop->loaded_count++] = img;
                total++;
            } else {
                fprintf(stderr, "warning: failed to load loop asset %s\n", loop->paths[j]);
            }
        }
        if (loop->loaded_count > 0) set_tile_status(loop->tile_name, TILE_LOOP);
    }
    return total;
}

static void unload_loop_assets(void) {
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        loop_asset_t *loop = &g_loop_assets[i];
        for (int j = 0; j < loop->loaded_count; ++j) {
            free(loop->images[j].rgb);
            loop->images[j].rgb = NULL;
        }
        loop->loaded_count = 0;
    }
}

static void print_loop_asset_summary(void) {
    for (size_t i = 0; i < sizeof(g_loop_assets) / sizeof(g_loop_assets[0]); ++i) {
        const loop_asset_t *loop = &g_loop_assets[i];
        printf("%-8s %d/%d", loop->tile_name, loop->loaded_count, loop->path_count);
        for (int j = 0; j < loop->loaded_count; ++j) {
            printf("  %dx%d", loop->images[j].width, loop->images[j].height);
        }
        printf("\n");
    }
}

static int run_self_test(void) {
    int loaded = load_loop_assets();
    collect_health(loaded);

    printf("RKTohi AllDemo self-test\n");
    int failures = 0;
    failures += print_check("license", g_health.license_ok, LICENSE_PATH);
    failures += print_check("camera node", g_health.camera_node_ok, CAMERA_DEVICE);
    failures += print_check("drm card", g_health.drm_ok, "/dev/dri/card0");
    failures += print_check("media lib", g_health.media_lib_ok, "lib/libmedia.so lib/libmedia.a");
    failures += print_check("rtsp port", g_health.rtsp_port_free, "8554");
    failures += print_check("npu model", g_health.npu_model_ok, "assets/npu/yolov5s-640-640.rknn");
    failures += print_check("avm lut", g_health.avm_lut_ok, "assets/avm/avm_blend.lut");
    failures += print_check("pano calib", g_health.pano_calib_ok, "assets/panorama/calib_file.pto");
    failures += print_check("svm assets", g_health.svm_assets_ok, "assets/svm3d/svm_3d_assets.json");

    char detail[64];
    snprintf(detail, sizeof(detail), "%d/%d decoded", g_health.loop_loaded, g_health.loop_expected);
    failures += print_check("loop assets", g_health.loop_loaded == g_health.loop_expected, detail);
    print_loop_asset_summary();

    unload_loop_assets();
    printf("summary: %s failures=%d\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

static void mark_showcase_modules(void) {
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        g_tiles[i].active = 1;
        g_tiles[i].status = TILE_SYNTH;
    }
}

static void probe_modules(void) {
    const int w = 320, h = 180, stride = 320;
    MEDIA_POOL_Create(WORK_POOL_NV12, stride * h * 3 / 2, 16);
    MEDIA_POOL_Create(WORK_POOL_RGB, w * h * 3, 8);
    MEDIA_POOL_Create(WORK_POOL_OUT, stride * h * 3 / 2, 16);

    uint64_t dummy = 0;
    if (MEDIA_SYS_GetVersion()) set_tile_status("LICENSE", TILE_PROBED);

    MEDIA_VPSS_ATTR vpss = {0};
    vpss.width = w; vpss.height = h; vpss.input_stride = stride; vpss.input_format = MEDIA_FORMAT_NV12;
    vpss.input_depth = 2; vpss.output_count = 1;
    vpss.outputs[0].output_id = 0; vpss.outputs[0].out_width = w; vpss.outputs[0].out_height = h;
    vpss.outputs[0].out_stride = stride; vpss.outputs[0].pool_id = WORK_POOL_OUT;
    vpss.outputs[0].output_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VPSS_SetAttr(20, &vpss) == 0) { set_tile_status("VPSS", TILE_PROBED); MEDIA_VPSS_DestroyGrp(20); }

    MEDIA_RGA_GRP_ATTR rga = {0};
    rga.algo = MEDIA_RGA_ALG_RESIZE; rga.input_count = 1; rga.output_count = 1; rga.input_depth = 2; rga.output_depth = 2;
    rga.inputs[0].port_id = 0; rga.inputs[0].width = w; rga.inputs[0].height = h; rga.inputs[0].format = MEDIA_FORMAT_NV12; rga.inputs[0].queue_depth = 2;
    rga.outputs[0].port_id = 0; rga.outputs[0].width = w; rga.outputs[0].height = h; rga.outputs[0].format = MEDIA_FORMAT_NV12; rga.outputs[0].pool_id = WORK_POOL_OUT;
    if (MEDIA_RGA_CreateGrp(20, &rga) == 0) { set_tile_status("RGA", TILE_PROBED); MEDIA_RGA_DestroyChn(20); }

    MEDIA_RESIZE_RGA_ATTR rz = {0};
    rz.input_format = MEDIA_FORMAT_NV12; rz.input_stride = stride; rz.input_depth = 2;
    rz.out_width = w; rz.out_height = h; rz.out_stride = stride; rz.output_format = MEDIA_FORMAT_NV12; rz.output_pool_id = WORK_POOL_OUT;
    if (MEDIA_RESIZE_RGA_CreateGrp(21, &rz) == 0) { set_tile_status("RESIZE_RGA", TILE_PROBED); MEDIA_RESIZE_RGA_DestroyGrp(21); }

    MEDIA_CSC_RGA_ATTR cr = {0};
    cr.input_width = w; cr.input_height = h; cr.input_format = MEDIA_FORMAT_NV12; cr.output_format = MEDIA_FORMAT_RGB888;
    cr.input_depth = 2; cr.output_pool_id = WORK_POOL_RGB; cr.input_stride = stride; cr.output_stride = w * 3;
    if (MEDIA_CSC_RGA_CreateGrp(22, &cr) == 0) { set_tile_status("CSC_RGA", TILE_PROBED); MEDIA_CSC_RGA_DestroyGrp(22); }

    MEDIA_CSC_CL_ATTR cc = {0};
    cc.input_width = w; cc.input_height = h; cc.input_format = MEDIA_FORMAT_NV12; cc.output_format = MEDIA_FORMAT_RGB888;
    cc.input_depth = 2; cc.output_pool_id = WORK_POOL_RGB; cc.input_stride = stride; cc.output_stride = w * 3;
    if (MEDIA_CSC_CL_CreateGrp(23, &cc) == 0) { set_tile_status("CSC_CL", TILE_PROBED); MEDIA_CSC_CL_DestroyGrp(23); }

    MEDIA_THERMAL_ATTR th = {0};
    th.width = w; th.height = h; th.format = MEDIA_FORMAT_NV12; th.color_mode = MEDIA_THERMAL_COLOR_RAINBOW3;
    th.input_depth = 2; th.output_depth = 2;
    if (MEDIA_THERMAL_CreateGrp(24, &th) == 0) { set_tile_status("THERMAL", TILE_PROBED); MEDIA_THERMAL_DestroyGrp(24); }

    MEDIA_OSD_ATTR osd = {0};
    osd.input_width = w; osd.input_height = h; osd.format = MEDIA_FORMAT_NV12; osd.input_depth = 2;
    osd.output_pool_id = WORK_POOL_OUT; osd.input_stride = stride; osd.output_stride = stride; osd.max_regions = 8;
    if (MEDIA_OSD_CreateGrp(25, &osd) == 0) { set_tile_status("OSD", TILE_PROBED); MEDIA_OSD_DestroyGrp(25); }

    MEDIA_CONV_CL_ATTR conv = {0};
    conv.width = w; conv.height = h; conv.format = MEDIA_FORMAT_RGB888; conv.kernel_size = 5;
    conv.input_depth = 2; conv.output_pool_id = WORK_POOL_RGB; conv.input_stride = w * 3; conv.output_stride = w * 3;
    if (MEDIA_CONV_CL_CreateGrp(26, &conv) == 0) { set_tile_status("CONV_CL", TILE_PROBED); MEDIA_CONV_CL_DestroyGrp(26); }

    MEDIA_CLAHE_ATTR clahe = {0};
    clahe.width = w; clahe.height = h; clahe.format = MEDIA_FORMAT_RGB888; clahe.tile_grid_x = 8; clahe.tile_grid_y = 8;
    clahe.bins = 256; clahe.input_depth = 2; clahe.output_pool_id = WORK_POOL_RGB; clahe.input_stride = w * 3; clahe.output_stride = w * 3; clahe.clip_limit = 2.5f;
    if (MEDIA_CLAHE_CreateGrp(27, &clahe) == 0) { set_tile_status("CLAHE", TILE_PROBED); MEDIA_CLAHE_DestroyGrp(27); }

    MEDIA_RETINEX_ATTR ret = {0};
    ret.scale_count = 1; ret.width = w; ret.height = h; ret.format = MEDIA_FORMAT_NV12; ret.output_depth = 2; ret.input_depth = 2;
    ret.input_stride = stride; ret.output_stride = stride; ret.gain = 1.0f; ret.threshold = 0.01f; ret.log_min = 0.0f; ret.log_max = 1.0f;
    if (MEDIA_RETINEX_CreateGrp(28, &ret) == 0) { set_tile_status("RETINEX", TILE_PROBED); MEDIA_RETINEX_DestroyGrp(28); }

    MEDIA_CAP_DEHAZE_ATTR cap = {0};
    cap.width = w; cap.height = h; cap.format = MEDIA_FORMAT_RGB888; cap.input_depth = 2; cap.output_pool_id = WORK_POOL_RGB;
    cap.input_stride = w * 3; cap.output_stride = w * 3; cap.guided_radius = 8; cap.guided_eps = 0.01f; cap.t0 = 0.1f;
    if (MEDIA_CAP_DEHAZE_CreateGrp(29, &cap) == 0) { set_tile_status("CAP_DEHAZE", TILE_PROBED); MEDIA_CAP_DEHAZE_DestroyGrp(29); }

    MEDIA_DCP_FAST_DEHAZE_ATTR dcp = {0};
    dcp.width = w; dcp.height = h; dcp.format = MEDIA_FORMAT_RGB888; dcp.input_depth = 2; dcp.output_pool_id = WORK_POOL_RGB;
    dcp.input_stride = w * 3; dcp.output_stride = w * 3; dcp.patch = 15; dcp.omega = 0.95f; dcp.t0 = 0.1f; dcp.airlight_percent = 0.001f; dcp.guided_radius = 8; dcp.guided_eps = 0.01f; dcp.refine_scale = 0.25f;
    if (MEDIA_DCP_FAST_DEHAZE_CreateGrp(30, &dcp) == 0) { set_tile_status("DCP_FAST_DEHAZE", TILE_PROBED); MEDIA_DCP_FAST_DEHAZE_DestroyGrp(30); }

    MEDIA_BLEND_PYR_ATTR bp = {0};
    bp.width = w; bp.height = h; bp.input_stride = stride; bp.input_depth = 2; bp.input_format = MEDIA_FORMAT_NV12; bp.output_stride = stride;
    if (MEDIA_BLEND_PYR_SetAttr(31, &bp) == 0) { set_tile_status("BLEND_PYR", TILE_PROBED); MEDIA_BLEND_PYR_DestroyGrp(31); }

    MEDIA_EDOF_CL_ATTR edof = {0};
    edof.width = w; edof.height = h; edof.format = MEDIA_FORMAT_NV12; edof.focus_radius = 5; edof.input_depth = 2; edof.output_pool_id = WORK_POOL_OUT; edof.input_stride = stride; edof.output_stride = stride; edof.score_eps = 0.01f;
    if (MEDIA_EDOF_CL_CreateGrp(32, &edof) == 0) { set_tile_status("EDOF_CL", TILE_PROBED); MEDIA_EDOF_CL_DestroyGrp(32); }

    MEDIA_EXPOSURE_FUSION_CL_ATTR ex = {0};
    ex.width = w; ex.height = h; ex.format = MEDIA_FORMAT_NV12; ex.input_depth = 2; ex.output_pool_id = WORK_POOL_OUT; ex.input_stride = stride; ex.output_stride = stride;
    ex.contrast_power = 1.0f; ex.saturation_power = 1.0f; ex.exposedness_power = 1.0f; ex.sigma = 0.2f; ex.epsilon = 0.0001f;
    if (MEDIA_EXPOSURE_FUSION_CL_CreateGrp(33, &ex) == 0) { set_tile_status("EXPOSURE_FUSION_CL", TILE_PROBED); MEDIA_EXPOSURE_FUSION_CL_DestroyGrp(33); }

    MEDIA_DUALVIEW_ATTR dv = {0};
    dv.input_width = w; dv.input_height = h; dv.input_stride = w * 3; dv.output_width = w * 2; dv.output_height = h; dv.output_stride = w * 6; dv.mode = MEDIA_DUALVIEW_MODE_SIDE_BY_SIDE;
    dv.format = MEDIA_FORMAT_RGB888; dv.input_depth = 2; dv.output_pool_id = WORK_POOL_RGB; dv.inputs[0].enabled = 1; dv.inputs[1].enabled = 1;
    if (MEDIA_DUALVIEW_CreateGrp(34, &dv) == 0) { set_tile_status("DUALVIEW", TILE_PROBED); MEDIA_DUALVIEW_DestroyGrp(34); }

    MEDIA_STEREO_3D_ATTR st = {0};
    st.width = w; st.height = h; st.format = MEDIA_FORMAT_NV12; st.input_depth = 2; st.output_pool_id = WORK_POOL_OUT; st.input_stride = stride; st.output_stride = stride; st.mode = MEDIA_STEREO_3D_MODE_SIDE_BY_SIDE;
    if (MEDIA_STEREO_3D_CreateGrp(35, &st) == 0) { set_tile_status("STEREO_3D", TILE_PROBED); MEDIA_STEREO_3D_DestroyGrp(35); }

    MEDIA_VMIX_ATTR vm = {0};
    vm.input_count = 2; vm.output_width = w; vm.output_height = h; vm.format = MEDIA_FORMAT_NV12; vm.input_depth = 2; vm.output_pool_id = WORK_POOL_OUT; vm.output_stride = stride; vm.primary_index = 0;
    vm.channels[0].enabled = 1; vm.channels[0].width = w; vm.channels[0].height = h; vm.channels[0].alpha = 1.0f; vm.channels[0].stride = stride;
    vm.channels[1].enabled = 1; vm.channels[1].x = w / 2; vm.channels[1].width = w / 2; vm.channels[1].height = h / 2; vm.channels[1].alpha = 0.6f; vm.channels[1].stride = stride;
    if (MEDIA_VMIX_CreateGrp(36, &vm) == 0) { set_tile_status("VMIX", TILE_PROBED); MEDIA_VMIX_DestroyGrp(36); }

    MEDIA_VMIX_RGA_ATTR vr = {0};
    vr.input_count = 2; vr.output_width = w; vr.output_height = h; vr.format = MEDIA_FORMAT_NV12; vr.input_depth = 2; vr.output_pool_id = WORK_POOL_OUT; vr.output_stride = stride; vr.primary_index = 0;
    vr.channels[0].enabled = 1; vr.channels[0].width = w; vr.channels[0].height = h; vr.channels[0].stride = stride; vr.channels[0].format = MEDIA_FORMAT_NV12;
    vr.channels[1].enabled = 1; vr.channels[1].x = w / 2; vr.channels[1].width = w / 2; vr.channels[1].height = h / 2; vr.channels[1].stride = stride; vr.channels[1].format = MEDIA_FORMAT_NV12;
    if (MEDIA_VMIX_RGA_CreateGrp(37, &vr) == 0) { set_tile_status("VMIX_RGA", TILE_PROBED); MEDIA_VMIX_RGA_DestroyGrp(37); }

    MEDIA_VENC_ATTR venc = {0};
    venc.width = w; venc.height = h; venc.stride = stride; venc.fps = FPS; venc.buf_cnt = 4; venc.input_depth = 2; venc.bitrate = 1000000; venc.gop = FPS;
    venc.video_format = MEDIA_FORMAT_H264; venc.rc_mode = MEDIA_VENC_RC_CBR; venc.input_format = MEDIA_FORMAT_NV12;
    if (MEDIA_VENC_SetAttr(38, &venc) == 0) { set_tile_status("VENC", TILE_PROBED); MEDIA_VENC_DestroyChn(38); }

    MEDIA_VDEC_ATTR vdec = {0};
    vdec.width = w; vdec.height = h; vdec.stride = stride; vdec.buf_cnt = 4; vdec.video_type = MEDIA_VIDEO_H264; vdec.pool_id = WORK_POOL_OUT; vdec.has_input_port = 1; vdec.input_depth = 2;
    if (MEDIA_VDEC_CreateChn(39, &vdec) == 0) { set_tile_status("VDEC", TILE_PROBED); MEDIA_VDEC_DestroyChn(39); }

    MEDIA_RTSP_SEND_ATTR tx = {0};
    tx.bind_addr = "0.0.0.0"; tx.port = 8554; tx.url_suffix = "rktohi_all"; tx.width = w; tx.height = h; tx.fps = FPS; tx.max_clients = 2; tx.transport_mode = RTSP_TRANSPORT_TCP; tx.video_format = MEDIA_FORMAT_H264;
    if (MEDIA_RTSP_SEND_CreateGrp(40, &tx) == 0) { set_tile_status("RTSP_SEND", TILE_PROBED); MEDIA_RTSP_SEND_DestroyGrp(40); }

    MEDIA_RTSP_RECV_ATTR rx = {0};
    rx.url = "rtsp://127.0.0.1:8554/rktohi_all"; rx.output_pool_id = WORK_POOL_OUT; rx.video_format = MEDIA_FORMAT_H264; rx.width = w; rx.height = h; rx.fps = FPS; rx.transport_mode = RTSP_TRANSPORT_TCP;
    if (MEDIA_RTSP_RECV_CreateGrp(41, &rx) == 0) { set_tile_status("RTSP_RECV", TILE_PROBED); MEDIA_RTSP_RECV_DestroyGrp(41); }

    MEDIA_PANO_ATTR pano = {0};
    pano.input_count = 2; pano.in_width = w; pano.in_height = h; pano.in_stride = stride; pano.out_width = w; pano.out_height = h; pano.out_stride = stride; pano.output_pool_id = WORK_POOL_OUT; pano.input_depth = 2; pano.output_depth = 2; pano.pto_path = "assets/panorama/calib_file.pto";
    if (MEDIA_PANO_CreateGrp(42, &pano) == 0) { set_tile_status("PANO", TILE_PROBED); MEDIA_PANO_DestroyGrp(42); }

    MEDIA_AVM_ATTR avm = {0};
    avm.input_count = 4; avm.in_width = w; avm.in_height = h; avm.in_stride = stride; avm.out_width = w; avm.out_height = h; avm.out_stride = stride; avm.output_pool_id = WORK_POOL_OUT; avm.input_depth = 2; avm.output_depth = 2; avm.lut_path = "assets/avm/avm_blend.lut";
    if (MEDIA_AVM_CreateGrp(43, &avm) == 0) { set_tile_status("AVM", TILE_PROBED); MEDIA_AVM_DestroyGrp(43); }

    MEDIA_SVM3D_ATTR svm = {0};
    svm.input_count = 4; svm.input_format = MEDIA_FORMAT_NV12; svm.in_width = w; svm.in_height = h; svm.in_stride = stride; svm.output_format = MEDIA_FORMAT_RGB888; svm.out_width = w; svm.out_height = h; svm.out_stride = w * 3; svm.output_pool_id = WORK_POOL_RGB; svm.input_depth = 2; svm.output_depth = 2; svm.asset_path = "assets/svm3d/svm_3d_assets.json";
    if (MEDIA_SVM3D_CreateGrp(44, &svm) == 0) { set_tile_status("SVM3D", TILE_PROBED); MEDIA_SVM3D_DestroyGrp(44); }

    MEDIA_NPU_ATTR npu = {0};
    npu.model_path = "assets/npu/yolov5s-640-640.rknn"; npu.backend = MEDIA_NPU_BACKEND_RKNN; npu.task = MEDIA_NPU_TASK_DETECT;
    npu.input_width = 640; npu.input_height = 640; npu.input_format = MEDIA_FORMAT_RGB888; npu.input_layout = MEDIA_NPU_LAYOUT_NHWC; npu.input_depth = 2; npu.passthrough = 1; npu.score_thresh = 0.25f; npu.nms_thresh = 0.45f;
    if (MEDIA_NPU_CreateGrp(45, &npu) == 0) { set_tile_status("NPU", TILE_PROBED); MEDIA_NPU_DestroyGrp(45); }

    set_tile_status("PIC_IO", TILE_PROBED);
    (void)dummy;
}

static int active_module_count(void) {
    int total = 0;
    for (size_t i = 0; i < sizeof(g_tiles) / sizeof(g_tiles[0]); ++i) {
        if (g_tiles[i].active) total++;
    }
    return total;
}

static void draw_health_line(uint8_t *canvas, int stride, int y, const char *left, int ok) {
    fill_rect_nv12(canvas, stride, 52, y - 6, 976, 34, ok ? 3 : 34, ok ? 32 : 12, ok ? 24 : 8);
    draw_text(canvas, stride, 64, y, ok ? "OK" : "NO", 2,
              ok ? 150 : 255, ok ? 255 : 150, ok ? 190 : 90);
    draw_text(canvas, stride, 130, y, left, 2, 190, 230, 255);
}

static void draw_dashboard(uint8_t *canvas, int stride, int frame, int rotate_main,
                           const char *only_tile, const uint8_t *cam, const uint8_t *osd_live,
                           const uint8_t *resize_live, const uint8_t *vpss_live,
                           const uint8_t *csc_rga_live, const uint8_t *transform_live,
                           const uint8_t *cap_live, const uint8_t *dcp_live,
                           const uint8_t *conv_live, const uint8_t *clahe_live,
                           const uint8_t *stereo_live) {
    fill_rect_nv12(canvas, stride, 0, 0, SCREEN_W, SCREEN_H, 4, 9, 16);
    fill_rect_nv12(canvas, stride, 0, 0, SCREEN_W, 90, 10, 18, 34);
    draw_text(canvas, stride, 28, 24, "RKTOHI VISUAL ENGINE", 4, 160, 255, 220);
    draw_text(canvas, stride, 760, 26, "1080X1920", 2, 90, 180, 255);

    draw_main_showcase(canvas, stride, frame, rotate_main, only_tile, cam, osd_live, resize_live,
                       vpss_live, csc_rga_live, transform_live, cap_live, dcp_live, conv_live, clahe_live, stereo_live);

    int cols = 4;
    int tile_w = 250;
    int tile_h = 150;
    int start_x = 28;
    int start_y = 760;
    int gap = 14;
    for (int i = 0; i < 16; ++i) {
        int cx = start_x + (i % cols) * (tile_w + gap);
        int cy = start_y + (i / cols) * (tile_h + gap);
        int tile_idx = TILE_FIRST_INDEX + i;
        draw_effect_tile(canvas, stride, cx, cy, tile_w, tile_h, tile_idx, frame,
                         g_tiles[tile_idx].active, osd_live, resize_live, vpss_live,
                         csc_rga_live, transform_live, cap_live, dcp_live, conv_live, clahe_live, stereo_live);
    }

    fill_rect_nv12(canvas, stride, 28, 1432, 1024, 232, 7, 13, 24);
    stroke_rect_nv12(canvas, stride, 28, 1432, 1024, 232, 2, 0, 160, 255);
    draw_text(canvas, stride, 52, 1458, "PIPELINE", 3, 170, 220, 255);
    draw_text(canvas, stride, 52, 1518, "VI>VPSS>RGA>OPENCL>VULKAN>NPU>VENC>RTSP>VO", 2, 160, 255, 220);
    for (int i = 0; i < 8; ++i) {
        int x = 70 + i * 120;
        int hbar = 30 + ((frame * (i + 2)) % 88);
        fill_rect_nv12(canvas, stride, x, 1620 - hbar, 54, hbar, 0, 180 + i * 8, 210);
    }

    fill_rect_nv12(canvas, stride, 28, 1690, 1024, 190, 5, 10, 18);
    stroke_rect_nv12(canvas, stride, 28, 1690, 1024, 190, 2, 0, 220, 180);
    char line[96];
    snprintf(line, sizeof(line), "CAM %s FRAMES %d  ASSETS %d/%d  MODULES %d/32",
             g_health.camera_running ? "OK" : "NO",
             g_health.camera_frames,
             g_health.loop_loaded,
             g_health.loop_expected,
             active_module_count());
    draw_health_line(canvas, stride, 1718, line, g_health.camera_running);
    snprintf(line, sizeof(line), "LICENSE %s  DRM %s  RTSP8554 %s  MODEL %s",
             g_health.license_ok ? "OK" : "NO",
             g_health.drm_ok ? "OK" : "NO",
             g_health.rtsp_port_free ? "FREE" : "BUSY",
             g_health.npu_model_ok ? "OK" : "NO");
    draw_health_line(canvas, stride, 1764, line,
                     g_health.license_ok && g_health.drm_ok && g_health.npu_model_ok);
    draw_text(canvas, stride, 52, 1816, "RTSP RTSP://172.16.9.195:8554/RKTOHI_ALL", 2, 255, 230, 120);
}

static void draw_solid_test(uint8_t *canvas, int stride) {
    fill_rect_nv12(canvas, stride, 0, 0, SCREEN_W, SCREEN_H, 8, 16, 24);
    fill_rect_nv12(canvas, stride, 0, 0, SCREEN_W / 3, SCREEN_H, 180, 40, 50);
    fill_rect_nv12(canvas, stride, SCREEN_W / 3, 0, SCREEN_W / 3, SCREEN_H, 40, 170, 90);
    fill_rect_nv12(canvas, stride, SCREEN_W * 2 / 3, 0, SCREEN_W / 3, SCREEN_H, 50, 100, 220);
    fill_rect_nv12(canvas, stride, 80, 160, 920, 240, 8, 16, 24);
    stroke_rect_nv12(canvas, stride, 80, 160, 920, 240, 4, 240, 240, 240);
    draw_text(canvas, stride, 130, 230, "SOLID DISPLAY TEST", 5, 255, 255, 255);
    draw_text(canvas, stride, 150, 330, "NO CAMERA  NO ASSETS  NO ANIMATION", 2, 230, 245, 255);
}

int main(int argc, char **argv) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int heavy_probe = 0;
    int asset_check = 0;
    int self_test = 0;
    int solid_test = 0;
    int rotate_main = 1;
    const char *only_tile = NULL;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--probe") == 0) heavy_probe = 1;
        if (strcmp(argv[i], "--asset-check") == 0) asset_check = 1;
        if (strcmp(argv[i], "--self-test") == 0) self_test = 1;
        if (strcmp(argv[i], "--solid-test") == 0) solid_test = 1;
        if (strcmp(argv[i], "--no-rotate-main") == 0) rotate_main = 0;
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only_tile = argv[++i];
            rotate_main = 0;
        }
    }

    if (self_test) {
        return run_self_test();
    }

    if (asset_check) {
        int loaded = load_loop_assets();
        print_loop_asset_summary();
        unload_loop_assets();
        return loaded > 0 ? 0 : 1;
    }

    if (only_tile && find_tile_index(only_tile) < 0) {
        fprintf(stderr, "unknown tile for --only: %s\n", only_tile);
        return 1;
    }

    const int dstride = ALIGN_UP(SCREEN_W, 64);
    const size_t display_size = (size_t)dstride * SCREEN_H * 3 / 2;

    if (MEDIA_SYS_Init() != 0) {
        fprintf(stderr, "MEDIA_SYS_Init failed\n");
        return 1;
    }
    MEDIA_SYS_SetLicense(LICENSE_PATH);

    if (!solid_test) mark_showcase_modules();
    int loaded_assets = solid_test ? 0 : load_loop_assets();
    collect_health(loaded_assets);
    if (heavy_probe && !solid_test) {
        probe_modules();
    }

    int live_osd_ok = 0;
    if (!solid_test && (!only_tile || strcasecmp(only_tile, "OSD") == 0) &&
        setup_live_osd() == 0) {
        live_osd_ok = 1;
    }
    int live_resize_ok = 0;
    if (!solid_test && (!only_tile || strcasecmp(only_tile, "RESIZE_RGA") == 0) &&
        setup_live_resize() == 0) {
        live_resize_ok = 1;
    }
    int live_vpss_ok = 0;
    if (!solid_test && (!only_tile || strcasecmp(only_tile, "VPSS") == 0) &&
        setup_live_vpss() == 0) {
        live_vpss_ok = 1;
    }
    int live_csc_rga_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "CSC_RGA") == 0 &&
        setup_live_csc_rga() == 0) {
        live_csc_rga_ok = 1;
    }
    int live_transform_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "TRANSFORM") == 0 &&
        setup_live_transform() == 0) {
        live_transform_ok = 1;
    }
    int live_cap_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "CAP_DEHAZE") == 0 &&
        setup_live_cap_dehaze() == 0) {
        live_cap_ok = 1;
    }
    int live_dcp_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "DCP_FAST_DEHAZE") == 0 &&
        setup_live_dcp_dehaze() == 0) {
        live_dcp_ok = 1;
    }
    int live_conv_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "CONV_CL") == 0 &&
        setup_live_conv_cl() == 0) {
        live_conv_ok = 1;
    }
    int live_clahe_ok = 0;
    if (!solid_test && only_tile && strcasecmp(only_tile, "CLAHE") == 0 &&
        setup_live_clahe() == 0) {
        live_clahe_ok = 1;
    }
    int live_stereo_ok = 0;

    if (MEDIA_POOL_Create(DISPLAY_POOL, display_size, 4) != 0) {
        fprintf(stderr, "display pool create failed\n");
        cleanup_live_stereo(live_stereo_ok);
        cleanup_live_clahe(live_clahe_ok);
        cleanup_live_conv_cl(live_conv_ok);
        cleanup_live_dcp_dehaze(live_dcp_ok);
        cleanup_live_cap_dehaze(live_cap_ok);
        cleanup_live_transform(live_transform_ok);
        cleanup_live_csc_rga(live_csc_rga_ok);
        cleanup_live_vpss(live_vpss_ok);
        cleanup_live_resize(live_resize_ok);
        cleanup_live_osd(live_osd_ok);
        unload_loop_assets();
        MEDIA_SYS_Exit();
        return 1;
    }

    MEDIA_VO_ATTR vo = {0};
    vo.intf = MEDIA_VO_INTF_MIPI;
    vo.width = SCREEN_W;
    vo.height = SCREEN_H;
    vo.plane_count = 1;
    if (MEDIA_VO_SetAttr(0, &vo) != 0 ||
        MEDIA_VO_CreateChn(0, 0, 0, 0, SCREEN_W, SCREEN_H, dstride, 4,
                           MEDIA_VO_PLANE_TYPE_AUTO, MEDIA_FORMAT_NV12) != 0 ||
        MEDIA_VO_Start(0, 0) != 0) {
        fprintf(stderr, "VO setup failed\n");
        MEDIA_POOL_Destroy(DISPLAY_POOL);
        cleanup_live_stereo(live_stereo_ok);
        cleanup_live_clahe(live_clahe_ok);
        cleanup_live_conv_cl(live_conv_ok);
        cleanup_live_dcp_dehaze(live_dcp_ok);
        cleanup_live_cap_dehaze(live_cap_ok);
        cleanup_live_transform(live_transform_ok);
        cleanup_live_csc_rga(live_csc_rga_ok);
        cleanup_live_vpss(live_vpss_ok);
        cleanup_live_resize(live_resize_ok);
        cleanup_live_osd(live_osd_ok);
        unload_loop_assets();
        MEDIA_SYS_Exit();
        return 1;
    }
    set_tile_status("VO", TILE_LIVE);

    int camera_ok = 0;
    int need_camera = !solid_test && tile_needs_camera(only_tile);
    if (need_camera && MEDIA_POOL_Create(CAMERA_POOL, CAM_STRIDE * CAM_H * 3 / 2, 6) == 0) {
        MEDIA_VI_ATTR vi = {0};
        vi.device = CAMERA_DEVICE;
        vi.width = CAM_W;
        vi.height = CAM_H;
        vi.stride = CAM_STRIDE;
        vi.fps = FPS;
        vi.buf_cnt = 4;
        vi.pool_id = CAMERA_POOL;
        vi.format = MEDIA_FORMAT_NV12;
        if (MEDIA_VI_SetAttr(0, &vi) == 0 && MEDIA_VI_Enable(0) == 0) {
            camera_ok = 1;
            set_tile_status("VI", TILE_LIVE);
        }
    }
    g_health.camera_running = camera_ok;

    printf("alldemo running on DSI 1080x1920%s%s%s%s. Ctrl+C to stop.\n",
           solid_test ? " solid-test" : "",
           (!solid_test && rotate_main) ? " rotate-main" : "",
           only_tile ? " only=" : "",
           only_tile ? only_tile : "");
    printf("RTSP showcase address reserved: rtsp://172.16.9.195:8554/rktohi_all\n");

    int frame = 0;
    int cam_frames = 0;
    int osd_frames = 0;
    int resize_frames = 0;
    int vpss_frames = 0;
    int csc_rga_frames = 0;
    int transform_frames = 0;
    int cap_frames = 0;
    int dcp_frames = 0;
    int conv_frames = 0;
    int clahe_frames = 0;
    int stereo_frames = 0;
    uint8_t *last_cam = malloc(CAM_FRAME_SIZE);
    uint8_t *last_osd = malloc(CAM_FRAME_SIZE);
    uint8_t *last_resize = malloc(CAM_FRAME_SIZE);
    uint8_t *last_vpss = malloc(CAM_FRAME_SIZE);
    uint8_t *last_csc_rga = malloc(CAM_FRAME_SIZE);
    uint8_t *last_transform = malloc(CAM_FRAME_SIZE);
    uint8_t *last_cap = malloc(RGB_FRAME_SIZE);
    uint8_t *last_dcp = malloc(RGB_FRAME_SIZE);
    uint8_t *last_conv = malloc(RGBA_FRAME_SIZE);
    uint8_t *last_clahe = malloc(CAM_FRAME_SIZE);
    uint8_t *last_rgb_src = malloc(RGB_FRAME_SIZE);
    uint8_t *last_rgba_src = malloc(RGBA_FRAME_SIZE);
    uint8_t *last_stereo = malloc(CAM_FRAME_SIZE);
    if (last_cam) memset(last_cam, 0, CAM_FRAME_SIZE);
    if (last_osd) memset(last_osd, 0, CAM_FRAME_SIZE);
    if (last_resize) memset(last_resize, 0, CAM_FRAME_SIZE);
    if (last_vpss) memset(last_vpss, 0, CAM_FRAME_SIZE);
    if (last_csc_rga) memset(last_csc_rga, 0, CAM_FRAME_SIZE);
    if (last_transform) memset(last_transform, 0, CAM_FRAME_SIZE);
    if (last_cap) memset(last_cap, 0, RGB_FRAME_SIZE);
    if (last_dcp) memset(last_dcp, 0, RGB_FRAME_SIZE);
    if (last_conv) memset(last_conv, 0, RGBA_FRAME_SIZE);
    if (last_clahe) memset(last_clahe, 0, CAM_FRAME_SIZE);
    if (last_rgb_src) memset(last_rgb_src, 0, RGB_FRAME_SIZE);
    if (last_rgba_src) memset(last_rgba_src, 0, RGBA_FRAME_SIZE);
    if (last_stereo) memset(last_stereo, 0, CAM_FRAME_SIZE);

    while (g_running) {
        if (camera_ok) {
            MEDIA_BUFFER cbuf = {-1, -1};
            if (MEDIA_VI_GetFrame(0, &cbuf, 1) == 0) {
                void *addr = NULL;
                size_t size = 0;
                if (MEDIA_POOL_BeginCpuAccess(cbuf, DMA_BUF_SYNC_READ) == 0) {
                    if (map_buffer(cbuf, &addr, &size, PROT_READ) == 0) {
                        size_t need = CAM_STRIDE * CAM_H * 3 / 2;
                        if (last_cam && size >= need) {
                            memcpy(last_cam, addr, need);
                            cam_frames++;
                            g_health.camera_frames = cam_frames;
                            if (live_osd_ok && last_osd &&
                                process_live_osd(last_cam, last_osd) == 0) {
                                osd_frames++;
                            }
                            if (live_resize_ok && last_resize &&
                                process_live_resize(last_cam, last_resize) == 0) {
                                resize_frames++;
                            }
                            if (live_vpss_ok && last_vpss &&
                                process_live_vpss(last_cam, last_vpss) == 0) {
                                vpss_frames++;
                            }
                            if (live_csc_rga_ok && last_csc_rga &&
                                process_live_csc_rga(last_cam, last_csc_rga) == 0) {
                                csc_rga_frames++;
                            }
                            if (live_stereo_ok && last_stereo && (cam_frames % 3) == 0 &&
                                process_live_stereo(last_cam, last_stereo) == 0) {
                                stereo_frames++;
                            }
                        }
                        munmap(addr, size);
                    }
                    (void)MEDIA_POOL_EndCpuAccess(cbuf, DMA_BUF_SYNC_READ);
                }
                MEDIA_VI_ReleaseFrame(0, cbuf);
            }
        }

        if (live_transform_ok && last_cam && last_transform) {
            fill_synthetic_nv12(last_cam, CAM_W, CAM_H, CAM_STRIDE, frame);
            if (process_live_transform(last_cam, last_transform) == 0) {
                transform_frames++;
            }
        }
        if (live_cap_ok && last_rgb_src && last_cap) {
            fill_synthetic_rgb(last_rgb_src, CAM_W, CAM_H, CAM_W * 3, frame);
            if (process_live_cap_dehaze(last_rgb_src, last_cap) == 0) {
                cap_frames++;
            }
        }
        if (live_dcp_ok && last_rgb_src && last_dcp) {
            fill_synthetic_rgb(last_rgb_src, CAM_W, CAM_H, CAM_W * 3, frame);
            if (process_live_dcp_dehaze(last_rgb_src, last_dcp) == 0) {
                dcp_frames++;
            }
        }
        if (live_conv_ok && last_rgba_src && last_conv) {
            fill_synthetic_rgba(last_rgba_src, CAM_W, CAM_H, CAM_W * 4, frame);
            if (process_live_conv_cl(last_rgba_src, last_conv) == 0) {
                conv_frames++;
            }
        }
        if (live_clahe_ok && last_cam && last_clahe) {
            fill_synthetic_nv12(last_cam, CAM_W, CAM_H, CAM_STRIDE, frame);
            if (process_live_clahe(last_cam, last_clahe) == 0) {
                clahe_frames++;
            }
        }

        MEDIA_BUFFER dbuf = {-1, -1};
        if (MEDIA_POOL_GetBuffer(DISPLAY_POOL, &dbuf) != 0) {
            usleep(1000);
            continue;
        }
        void *addr = NULL;
        size_t size = 0;
        if (map_buffer(dbuf, &addr, &size, PROT_READ | PROT_WRITE) != 0) {
            MEDIA_POOL_PutBuffer(dbuf);
            continue;
        }
        if (MEDIA_POOL_BeginCpuAccess(dbuf, DMA_BUF_SYNC_WRITE) != 0) {
            munmap(addr, size);
            MEDIA_POOL_PutBuffer(dbuf);
            continue;
        }
        (void)size;
        if (solid_test) {
            draw_solid_test((uint8_t *)addr, dstride);
        } else {
            draw_dashboard((uint8_t *)addr, dstride, frame, rotate_main,
                           only_tile,
                           cam_frames > 0 ? last_cam : NULL,
                           osd_frames > 0 ? last_osd : NULL,
                           resize_frames > 0 ? last_resize : NULL,
                           vpss_frames > 0 ? last_vpss : NULL,
                           csc_rga_frames > 0 ? last_csc_rga : NULL,
                           transform_frames > 0 ? last_transform : NULL,
                           cap_frames > 0 ? last_cap : NULL,
                           dcp_frames > 0 ? last_dcp : NULL,
                           conv_frames > 0 ? last_conv : NULL,
                           clahe_frames > 0 ? last_clahe : NULL,
                           stereo_frames > 0 ? last_stereo : NULL);
        }
        (void)MEDIA_POOL_EndCpuAccess(dbuf, DMA_BUF_SYNC_WRITE);
        munmap(addr, size);

        if (MEDIA_SYS_SendFrame("VO", 0, "input0", dbuf, 1000) != 0) {
            MEDIA_POOL_PutBuffer(dbuf);
        }
        frame++;
        usleep(1000000 / FPS);
    }

    if (camera_ok) MEDIA_VI_Disable(0);
    MEDIA_VO_Stop(0, 0);
    MEDIA_VO_DestroyChn(0, 0);
    cleanup_live_stereo(live_stereo_ok);
    cleanup_live_clahe(live_clahe_ok);
    cleanup_live_conv_cl(live_conv_ok);
    cleanup_live_dcp_dehaze(live_dcp_ok);
    cleanup_live_cap_dehaze(live_cap_ok);
    cleanup_live_transform(live_transform_ok);
    cleanup_live_csc_rga(live_csc_rga_ok);
    cleanup_live_vpss(live_vpss_ok);
    cleanup_live_resize(live_resize_ok);
    cleanup_live_osd(live_osd_ok);
    MEDIA_POOL_Destroy(DISPLAY_POOL);
    if (need_camera) MEDIA_POOL_Destroy(CAMERA_POOL);
    MEDIA_POOL_Destroy(WORK_POOL_NV12);
    MEDIA_POOL_Destroy(WORK_POOL_RGB);
    MEDIA_POOL_Destroy(WORK_POOL_OUT);
    unload_loop_assets();
    free(last_stereo);
    free(last_rgba_src);
    free(last_rgb_src);
    free(last_clahe);
    free(last_conv);
    free(last_dcp);
    free(last_cap);
    free(last_transform);
    free(last_csc_rga);
    free(last_vpss);
    free(last_resize);
    free(last_osd);
    free(last_cam);
    MEDIA_SYS_Exit();
    return 0;
}
