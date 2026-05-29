#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "media_api.h"

#define WIDTH 1280
#define HEIGHT 1024
#define STRIDE 1280

#define VDEC_CHN 0
#define VENC_CHN 0
#define VDEC_POOL_ID 20
#define VENC_POOL_ID 21
#define VDEC_POOL_COUNT 8
#define VENC_POOL_COUNT 8
#define DEFAULT_FPS 30
#define DEFAULT_BITRATE 8000000
#define DEFAULT_LICENSE_PATH "/root/licence.dat"

typedef struct {
    unsigned char *data;
    size_t size;
    int *offsets;
    int count;
} h264_stream_t;

typedef struct {
    int packets;
    size_t bytes;
} output_stats_t;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

static int parse_positive_int(const char *text, int *value) {
    char *end = NULL;
    long v;

    if (!text || text[0] == '\0' || !value) {
        return -1;
    }

    errno = 0;
    v = strtol(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || v <= 0 || v > INT_MAX) {
        return -1;
    }

    *value = (int)v;
    return 0;
}

static int start_code_len(const unsigned char *data, size_t size, size_t pos) {
    if (pos + 3 <= size &&
        data[pos] == 0x00 && data[pos + 1] == 0x00 && data[pos + 2] == 0x01) {
        return 3;
    }
    if (pos + 4 <= size &&
        data[pos] == 0x00 && data[pos + 1] == 0x00 &&
        data[pos + 2] == 0x00 && data[pos + 3] == 0x01) {
        return 4;
    }
    return 0;
}

static int read_file(const char *path, unsigned char **data, size_t *size) {
    FILE *fp = NULL;
    long len;
    unsigned char *buf = NULL;

    if (!path || !data || !size) {
        return -1;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "open input failed: %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "seek input failed: %s\n", strerror(errno));
        fclose(fp);
        return -1;
    }
    len = ftell(fp);
    if (len <= 0) {
        fprintf(stderr, "input is empty or ftell failed: %s\n", path);
        fclose(fp);
        return -1;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fprintf(stderr, "rewind input failed: %s\n", strerror(errno));
        fclose(fp);
        return -1;
    }

    buf = (unsigned char *)malloc((size_t)len);
    if (!buf) {
        fprintf(stderr, "malloc input buffer failed: %ld bytes\n", len);
        fclose(fp);
        return -1;
    }
    if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
        fprintf(stderr, "read input failed: %s\n", strerror(errno));
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    *data = buf;
    *size = (size_t)len;
    return 0;
}

static void free_stream(h264_stream_t *stream) {
    if (!stream) {
        return;
    }
    free(stream->data);
    free(stream->offsets);
    memset(stream, 0, sizeof(*stream));
}

static int load_h264_stream(const char *path, h264_stream_t *stream) {
    int cap = 256;

    if (!stream) {
        return -1;
    }
    memset(stream, 0, sizeof(*stream));

    if (read_file(path, &stream->data, &stream->size) != 0) {
        return -1;
    }

    stream->offsets = (int *)malloc((size_t)cap * sizeof(stream->offsets[0]));
    if (!stream->offsets) {
        fprintf(stderr, "malloc nal offsets failed\n");
        free_stream(stream);
        return -1;
    }

    for (size_t i = 0; i + 3 < stream->size;) {
        int sc = start_code_len(stream->data, stream->size, i);
        if (sc > 0) {
            if (stream->count >= cap) {
                int new_cap = cap * 2;
                int *tmp = (int *)realloc(stream->offsets,
                                          (size_t)new_cap * sizeof(stream->offsets[0]));
                if (!tmp) {
                    fprintf(stderr, "realloc nal offsets failed\n");
                    free_stream(stream);
                    return -1;
                }
                stream->offsets = tmp;
                cap = new_cap;
            }
            stream->offsets[stream->count++] = (int)i;
            i += (size_t)sc;
            continue;
        }
        ++i;
    }

    if (stream->count == 0) {
        fprintf(stderr, "input is not Annex-B H.264: no start code found\n");
        free_stream(stream);
        return -1;
    }

    return 0;
}

static int nal_is_vcl(const h264_stream_t *stream, int idx) {
    int start;
    int end;
    int sc;
    int nal_start;
    int nal_type;

    if (!stream || idx < 0 || idx >= stream->count) {
        return 0;
    }
    start = stream->offsets[idx];
    end = (idx + 1 < stream->count) ? stream->offsets[idx + 1] : (int)stream->size;
    sc = start_code_len(stream->data, stream->size, (size_t)start);
    nal_start = start + sc;
    if (nal_start >= end) {
        return 0;
    }

    nal_type = stream->data[nal_start] & 0x1f;
    return nal_type == 1 || nal_type == 5;
}

static int drain_venc(FILE *out, int timeout_ms, output_stats_t *stats) {
    for (;;) {
        MEDIA_PACKET pkt;
        memset(&pkt, 0, sizeof(pkt));

        if (MEDIA_VENC_GetPacket(VENC_CHN, &pkt, timeout_ms) != 0) {
            return 0;
        }

        if (pkt.data && pkt.len > 0) {
            if (fwrite(pkt.data, 1, pkt.len, out) != pkt.len) {
                fprintf(stderr, "write output failed: %s\n", strerror(errno));
                MEDIA_VENC_ReleasePacket(VENC_CHN, &pkt);
                return -1;
            }
            if (stats) {
                stats->packets++;
                stats->bytes += pkt.len;
            }
        }

        MEDIA_VENC_ReleasePacket(VENC_CHN, &pkt);
        timeout_ms = 0;
    }
}

static int send_nal_with_retry(const h264_stream_t *stream, int idx, uint64_t pts,
                               FILE *out, output_stats_t *stats) {
    int start = stream->offsets[idx];
    int end = (idx + 1 < stream->count) ? stream->offsets[idx + 1] : (int)stream->size;
    int len = end - start;

    if (start < 0 || len <= 0 || (size_t)end > stream->size) {
        return -1;
    }

    for (int retry = 0; retry < 100; ++retry) {
        if (MEDIA_VDEC_SendPacket(VDEC_CHN, stream->data + start, (size_t)len, pts) == 0) {
            return 0;
        }
        if (drain_venc(out, 10, stats) != 0) {
            return -1;
        }
        usleep(10000);
    }

    fprintf(stderr, "MEDIA_VDEC_SendPacket failed at nal=%d size=%d\n", idx, len);
    return -1;
}

static void print_usage(const char *prog) {
    printf("Usage: %s input.h264 output.h264 [fps] [bitrate] [license]\n", prog);
    printf("Fixed format: H.264 Annex-B input -> VDEC(%dx%d NV12) -> VENC(H.264 %dx%d)\n",
           WIDTH, HEIGHT, WIDTH, HEIGHT);
    printf("Defaults: fps=%d bitrate=%d license=%s\n",
           DEFAULT_FPS, DEFAULT_BITRATE, DEFAULT_LICENSE_PATH);
}

int main(int argc, char **argv) {
    const char *in_path;
    const char *out_path;
    const char *license_path = DEFAULT_LICENSE_PATH;
    int fps = DEFAULT_FPS;
    int bitrate = DEFAULT_BITRATE;
    size_t vdec_frame_size = (size_t)STRIDE * (size_t)HEIGHT * 2u;
    h264_stream_t stream;
    output_stats_t stats;
    FILE *out = NULL;
    int ret = 1;
    int sys_ok = 0;
    int vdec_pool_ok = 0;
    int venc_pool_ok = 0;
    int vdec_ok = 0;
    int venc_ok = 0;
    int bound = 0;
    int vdec_started = 0;
    int venc_started = 0;
    int sent_vcl = 0;
    uint64_t pts = 0;

    memset(&stream, 0, sizeof(stream));
    memset(&stats, 0, sizeof(stats));

    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_usage(argv[0]);
        return 0;
    }
    if (argc < 3 || argc > 6) {
        print_usage(argv[0]);
        return 1;
    }

    in_path = argv[1];
    out_path = argv[2];
    if (argc > 3 && parse_positive_int(argv[3], &fps) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc > 4 && parse_positive_int(argv[4], &bitrate) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    if (argc > 5) {
        license_path = argv[5];
    }

    if (load_h264_stream(in_path, &stream) != 0) {
        goto cleanup;
    }

    out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "open output failed: %s: %s\n", out_path, strerror(errno));
        goto cleanup;
    }

    printf("H264 transcode min: %s -> %s\n", in_path, out_path);
    printf("pipeline: VDEC %dx%d stride=%d -> VENC H.264 fps=%d bitrate=%d nals=%d\n",
           WIDTH, HEIGHT, STRIDE, fps, bitrate, stream.count);

    if (MEDIA_SYS_Init() != 0) {
        fprintf(stderr, "MEDIA_SYS_Init failed\n");
        goto cleanup;
    }
    sys_ok = 1;

    if (MEDIA_SYS_SetLicense(license_path) != 0) {
        fprintf(stderr, "warning: MEDIA_SYS_SetLicense failed: %s\n", license_path);
    }

    if (MEDIA_POOL_Create(VDEC_POOL_ID, vdec_frame_size, VDEC_POOL_COUNT) != 0) {
        fprintf(stderr, "MEDIA_POOL_Create VDEC failed\n");
        goto cleanup;
    }
    vdec_pool_ok = 1;

    if (MEDIA_POOL_Create(VENC_POOL_ID, 8u * 1024u * 1024u, VENC_POOL_COUNT) != 0) {
        fprintf(stderr, "MEDIA_POOL_Create VENC failed\n");
        goto cleanup;
    }
    venc_pool_ok = 1;

    MEDIA_VDEC_ATTR vdec;
    memset(&vdec, 0, sizeof(vdec));
    vdec.width = WIDTH;
    vdec.height = HEIGHT;
    vdec.stride = STRIDE;
    vdec.buf_cnt = VDEC_POOL_COUNT;
    vdec.video_type = MEDIA_VIDEO_H264;
    vdec.pool_id = VDEC_POOL_ID;
    vdec.has_input_port = 0;
    vdec.input_depth = 0;

    if (MEDIA_VDEC_CreateChn(VDEC_CHN, &vdec) != 0) {
        fprintf(stderr, "MEDIA_VDEC_CreateChn failed\n");
        goto cleanup;
    }
    vdec_ok = 1;

    if (MEDIA_VENC_CreateChn(VENC_CHN,
                             WIDTH,
                             HEIGHT,
                             STRIDE,
                             fps,
                             VENC_POOL_COUNT,
                             4,
                             VENC_POOL_ID,
                             bitrate,
                             fps * 2,
                             MEDIA_FORMAT_H264) != 0) {
        fprintf(stderr, "MEDIA_VENC_CreateChn failed\n");
        goto cleanup;
    }
    venc_ok = 1;

    if (MEDIA_SYS_Bind("VDEC", VDEC_CHN, "output", "VENC", VENC_CHN, "input") != 0) {
        fprintf(stderr, "MEDIA_SYS_Bind VDEC->VENC failed\n");
        goto cleanup;
    }
    bound = 1;

    if (MEDIA_VENC_Start(VENC_CHN) != 0) {
        fprintf(stderr, "MEDIA_VENC_Start failed\n");
        goto cleanup;
    }
    venc_started = 1;

    if (MEDIA_VDEC_Start(VDEC_CHN) != 0) {
        fprintf(stderr, "MEDIA_VDEC_Start failed\n");
        goto cleanup;
    }
    vdec_started = 1;

    for (int i = 0; i < stream.count; ++i) {
        int vcl = nal_is_vcl(&stream, i);
        if (send_nal_with_retry(&stream, i, pts, out, &stats) != 0) {
            goto cleanup;
        }
        if (drain_venc(out, 0, &stats) != 0) {
            goto cleanup;
        }
        if (vcl) {
            sent_vcl++;
            pts += (uint64_t)(1000 / fps);
            usleep((useconds_t)(1000000 / fps));
        }
    }

    long long drain_end = now_ms() + 2000LL;
    while (now_ms() < drain_end) {
        if (drain_venc(out, 100, &stats) != 0) {
            goto cleanup;
        }
        usleep(1000);
    }

    fflush(out);
    printf("done: input_frames=%d output_packets=%d output_bytes=%zu\n",
           sent_vcl, stats.packets, stats.bytes);
    ret = (sent_vcl > 0 && stats.packets > 0) ? 0 : 2;

cleanup:
    if (vdec_started) {
        MEDIA_VDEC_Stop(VDEC_CHN);
    }
    if (venc_started) {
        MEDIA_VENC_Stop(VENC_CHN);
    }
    if (bound) {
        MEDIA_SYS_UnBind("VDEC", VDEC_CHN, "output", "VENC", VENC_CHN, "input");
    }
    if (venc_ok) {
        MEDIA_VENC_DestroyChn(VENC_CHN);
    }
    if (vdec_ok) {
        MEDIA_VDEC_DestroyChn(VDEC_CHN);
    }
    if (venc_pool_ok) {
        MEDIA_POOL_Destroy(VENC_POOL_ID);
    }
    if (vdec_pool_ok) {
        MEDIA_POOL_Destroy(VDEC_POOL_ID);
    }
    if (sys_ok) {
        MEDIA_SYS_Exit();
    }
    if (out) {
        fclose(out);
    }
    free_stream(&stream);

    return ret;
}
