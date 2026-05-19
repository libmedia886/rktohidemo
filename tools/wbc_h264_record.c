#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "media_api.h"

#define WBC_POOL_ID 14
#define VENC_POOL_ID 15
#define WBC_POOL_COUNT 8
#define VENC_POOL_COUNT 8
#define WBC_NV12_WIDTH_ALIGN 64

#define DEFAULT_OUT_PATH "recordings/alldemo_wbc.h264"
#define DEFAULT_DURATION_SEC 180
#define DEFAULT_WIDTH 1024
#define DEFAULT_HEIGHT 1920
#define DEFAULT_FPS 30
#define DEFAULT_BITRATE 8000000
#define DEFAULT_LICENSE_PATH "/root/licence.dat"

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int parse_positive_int(const char *arg, int *value) {
    char *end = NULL;
    long v;

    if (!arg || arg[0] == '\0' || !value) {
        return -1;
    }

    errno = 0;
    v = strtol(arg, &end, 10);
    if (errno != 0 || !end || *end != '\0' || v <= 0 || v > INT_MAX) {
        return -1;
    }

    *value = (int)v;
    return 0;
}

static int mkdir_p(const char *dir) {
    char tmp[PATH_MAX];
    size_t len;

    if (!dir || dir[0] == '\0') {
        return 0;
    }

    len = strlen(dir);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(tmp, dir, len + 1);
    if (len > 1 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

static int ensure_parent_dir(const char *path) {
    char dir[PATH_MAX];
    const char *slash;
    size_t len;

    if (!path) {
        return -1;
    }

    slash = strrchr(path, '/');
    if (!slash) {
        return 0;
    }

    len = (size_t)(slash - path);
    if (len == 0) {
        return 0;
    }
    if (len >= sizeof(dir)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy(dir, path, len);
    dir[len] = '\0';
    return mkdir_p(dir);
}

static void print_usage(const char *prog) {
    printf("Usage: %s [out.h264] [duration_sec] [width] [height] [fps] [bitrate] [target] [license]\n", prog);
    printf("Default: %s %s %d %d %d %d %d\n",
           prog,
           DEFAULT_OUT_PATH,
           DEFAULT_DURATION_SEC,
           DEFAULT_WIDTH,
           DEFAULT_HEIGHT,
           DEFAULT_FPS,
           DEFAULT_BITRATE);
    printf("Note: NV12 VO_WBC width must be %d-pixel aligned; use 1024 instead of 1080.\n",
           WBC_NV12_WIDTH_ALIGN);
}

int main(int argc, char **argv) {
    const char *out_path = DEFAULT_OUT_PATH;
    int duration_sec = DEFAULT_DURATION_SEC;
    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    int fps = DEFAULT_FPS;
    int bitrate = DEFAULT_BITRATE;
    const char *target = NULL;
    const char *license_path = DEFAULT_LICENSE_PATH;
    int stride;
    size_t wbc_frame_size;
    int ret = 1;
    int sys_inited = 0;
    int wbc_pool_created = 0;
    int venc_pool_created = 0;
    int wbc_created = 0;
    int venc_created = 0;
    int bound = 0;
    int venc_started = 0;
    int wbc_started = 0;
    FILE *fp = NULL;
    int got_packets = 0;
    size_t total_bytes = 0;

    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc > 1) {
        out_path = argv[1];
    }
    if (argc > 2 && parse_positive_int(argv[2], &duration_sec) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc > 3 && parse_positive_int(argv[3], &width) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc > 4 && parse_positive_int(argv[4], &height) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc > 5 && parse_positive_int(argv[5], &fps) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc > 6 && parse_positive_int(argv[6], &bitrate) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc > 7 && argv[7][0] != '\0' && strcmp(argv[7], "-") != 0) {
        target = argv[7];
    }
    if (argc > 8 && argv[8][0] != '\0' && strcmp(argv[8], "-") != 0) {
        license_path = argv[8];
    }
    if (argc > 9) {
        print_usage(argv[0]);
        return 1;
    }

    if ((width % WBC_NV12_WIDTH_ALIGN) != 0) {
        fprintf(stderr,
                "VO_WBC H264 recorder: width=%d is not %d-pixel aligned. Suggested width=%d, height=%d\n",
                width,
                WBC_NV12_WIDTH_ALIGN,
                width - (width % WBC_NV12_WIDTH_ALIGN),
                height);
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    stride = width;
    wbc_frame_size = (size_t)stride * (size_t)height +
                     (size_t)stride * (size_t)((height + 1) / 2);

    if (ensure_parent_dir(out_path) != 0) {
        fprintf(stderr, "ensure output dir failed: %s: %s\n", out_path, strerror(errno));
        return 1;
    }

    fp = fopen(out_path, "wb");
    if (!fp) {
        fprintf(stderr, "open output failed: %s: %s\n", out_path, strerror(errno));
        return 1;
    }

    printf("VO_WBC -> VENC(H.264) file recorder\n");
    printf("capture: %dx%d stride=%d fps=%d bitrate=%d target=%s duration=%ds\n",
           width, height, stride, fps, bitrate, target ? target : "auto", duration_sec);
    printf("output: %s\n", out_path);

    if (MEDIA_SYS_Init() != 0) {
        fprintf(stderr, "MEDIA_SYS_Init failed\n");
        goto cleanup;
    }
    sys_inited = 1;

    if (MEDIA_SYS_SetLicense(license_path) != 0) {
        fprintf(stderr, "warning: MEDIA_SYS_SetLicense failed: %s\n", license_path);
    } else {
        printf("license: %s\n", license_path);
    }

    if (MEDIA_POOL_Create(WBC_POOL_ID, wbc_frame_size, WBC_POOL_COUNT) != 0) {
        fprintf(stderr, "MEDIA_POOL_Create WBC failed\n");
        goto cleanup;
    }
    wbc_pool_created = 1;

    if (MEDIA_POOL_Create(VENC_POOL_ID, 8 * 1024 * 1024, VENC_POOL_COUNT) != 0) {
        fprintf(stderr, "MEDIA_POOL_Create VENC failed\n");
        goto cleanup;
    }
    venc_pool_created = 1;

    MEDIA_VO_WBC_ATTR wbc_attr;
    memset(&wbc_attr, 0, sizeof(wbc_attr));
    wbc_attr.target = target;
    wbc_attr.width = width;
    wbc_attr.height = height;
    wbc_attr.stride = stride;
    wbc_attr.fps = fps;
    wbc_attr.format = MEDIA_FORMAT_NV12;
    wbc_attr.pool_id = WBC_POOL_ID;
    wbc_attr.output_depth = 4;

    if (MEDIA_VO_WBC_CreateChn(0, &wbc_attr) != 0) {
        fprintf(stderr, "MEDIA_VO_WBC_CreateChn failed\n");
        goto cleanup;
    }
    wbc_created = 1;

    if (MEDIA_VENC_CreateChn(0,
                             width,
                             height,
                             stride,
                             fps,
                             4,
                             4,
                             VENC_POOL_ID,
                             bitrate,
                             fps * 2,
                             MEDIA_FORMAT_H264) != 0) {
        fprintf(stderr, "MEDIA_VENC_CreateChn failed\n");
        goto cleanup;
    }
    venc_created = 1;

    if (MEDIA_SYS_Bind("VO_WBC", 0, "output", "VENC", 0, "input") != 0) {
        fprintf(stderr, "MEDIA_SYS_Bind VO_WBC->VENC failed\n");
        goto cleanup;
    }
    bound = 1;

    if (MEDIA_VENC_Start(0) != 0) {
        fprintf(stderr, "MEDIA_VENC_Start failed\n");
        goto cleanup;
    }
    venc_started = 1;

    if (MEDIA_VO_WBC_Start(0) != 0) {
        fprintf(stderr, "MEDIA_VO_WBC_Start failed\n");
        goto cleanup;
    }
    wbc_started = 1;

    long long end_ms = now_ms() + (long long)duration_sec * 1000LL;
    while (g_running && now_ms() < end_ms) {
        MEDIA_PACKET pkt;
        memset(&pkt, 0, sizeof(pkt));
        if (MEDIA_VENC_GetPacket(0, &pkt, 500) == 0) {
            if (pkt.data && pkt.len > 0) {
                if (fwrite(pkt.data, 1, pkt.len, fp) != pkt.len) {
                    fprintf(stderr, "write output failed: %s\n", strerror(errno));
                    MEDIA_VENC_ReleasePacket(0, &pkt);
                    goto cleanup;
                }
                total_bytes += pkt.len;
                got_packets++;
            }
            MEDIA_VENC_ReleasePacket(0, &pkt);
        }
    }

    if (wbc_started) {
        MEDIA_VO_WBC_Stop(0);
        wbc_started = 0;
    }

    long long drain_end_ms = now_ms() + 1000LL;
    while (g_running && now_ms() < drain_end_ms) {
        MEDIA_PACKET pkt;
        memset(&pkt, 0, sizeof(pkt));
        if (MEDIA_VENC_GetPacket(0, &pkt, 100) != 0) {
            break;
        }
        if (pkt.data && pkt.len > 0) {
            if (fwrite(pkt.data, 1, pkt.len, fp) != pkt.len) {
                fprintf(stderr, "write output failed while draining: %s\n", strerror(errno));
                MEDIA_VENC_ReleasePacket(0, &pkt);
                goto cleanup;
            }
            total_bytes += pkt.len;
            got_packets++;
        }
        MEDIA_VENC_ReleasePacket(0, &pkt);
    }

    fflush(fp);
    printf("recorded packets=%d bytes=%zu path=%s\n", got_packets, total_bytes, out_path);
    ret = got_packets > 0 ? 0 : 2;

cleanup:
    if (wbc_started) {
        MEDIA_VO_WBC_Stop(0);
    }
    if (venc_started) {
        MEDIA_VENC_Stop(0);
    }
    if (bound) {
        MEDIA_SYS_UnBind("VO_WBC", 0, "output", "VENC", 0, "input");
    }
    if (venc_created) {
        MEDIA_VENC_DestroyChn(0);
    }
    if (wbc_created) {
        MEDIA_VO_WBC_DestroyChn(0);
    }
    if (venc_pool_created) {
        MEDIA_POOL_Destroy(VENC_POOL_ID);
    }
    if (wbc_pool_created) {
        MEDIA_POOL_Destroy(WBC_POOL_ID);
    }
    if (sys_inited) {
        MEDIA_SYS_Exit();
    }
    if (fp) {
        fclose(fp);
    }

    return ret;
}
