#include "page_overlay.h"

#include "media_api.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE_LOCAL(a) (sizeof(a) / sizeof((a)[0]))

static uint8_t overlay_font5x7(char c, int row) {
    static const uint8_t digit[10][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,30,1,1,17,14},
        {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,2,12},
    };
    static const uint8_t upper[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    };
    if (row < 0 || row >= 7) return 0;
    if (c >= '0' && c <= '9') return digit[c - '0'][row];
    if (c >= 'A' && c <= 'Z') return upper[c - 'A'][row];
    if (c >= 'a' && c <= 'z') return upper[c - 'a'][row];
    if (c == '-') return row == 3 ? 31 : 0;
    if (c == '_') return row == 6 ? 31 : 0;
    if (c == '=') return (row == 2 || row == 4) ? 31 : 0;
    if (c == '>') return row < 4 ? (uint8_t)(1u << row) : (uint8_t)(1u << (6 - row));
    if (c == '<') return row < 4 ? (uint8_t)(1u << (4 - row)) : (uint8_t)(1u << (row - 2));
    if (c == '/') {
        int col = 6 - row;
        if (col > 4) col = 4;
        if (col < 0) col = 0;
        return (uint8_t)(1u << col);
    }
    if (c == ':') return (row == 2 || row == 4) ? 4 : 0;
    if (c == '.') return row == 6 ? 4 : 0;
    if (c == ',') return row == 6 ? 8 : 0;
    if (c == '%') return (row == 0 || row == 5) ? 17 : (row == 1 ? 18 : (row == 2 ? 4 : (row == 3 ? 8 : (row == 4 ? 9 : 0))));
    if (c == '(') return (row == 0 || row == 6) ? 2 : 4;
    if (c == ')') return (row == 0 || row == 6) ? 8 : 4;
    if (c == '+') return row == 3 ? 31 : ((row == 1 || row == 2 || row == 4 || row == 5) ? 4 : 0);
    return 0;
}

static int render_text_mask(const char *text, int scale, uint8_t *mask,
                            size_t mask_size, int max_w, int max_h,
                            int *out_w, int *out_h) {
    if (!text || !mask || scale <= 0 || max_w <= 0 || max_h <= 0) return -1;
    if (mask_size < (size_t)max_w * (size_t)max_h) return -1;
    memset(mask, 0, (size_t)max_w * (size_t)max_h);
    int cx = 0;
    int h = 7 * scale;
    if (h > max_h) return -1;
    for (const char *p = text; *p; ++p) {
        int adv = (*p == ' ') ? 4 * scale : 6 * scale;
        if (cx + adv > max_w) break;
        if (*p != ' ') {
            for (int row = 0; row < 7; ++row) {
                uint8_t bits = overlay_font5x7(*p, row);
                for (int col = 0; col < 5; ++col) {
                    if (!(bits & (1u << (4 - col)))) continue;
                    for (int yy = 0; yy < scale; ++yy) {
                        uint8_t *dst = mask + (row * scale + yy) * max_w + cx + col * scale;
                        memset(dst, 255, (size_t)scale);
                    }
                }
            }
        }
        cx += adv;
    }
    if (cx <= 0) return -1;
    *out_w = cx;
    *out_h = h;
    return 0;
}

int page_overlay_set_text(int osd_grp, int region_id, int x, int y, int scale,
                          const char *text, uint8_t *mask, size_t mask_size,
                          int max_w, int max_h,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t alpha) {
    int w = 0;
    int h = 0;
    if (render_text_mask(text, scale, mask, mask_size, max_w, max_h, &w, &h) != 0) return -1;

    MEDIA_OSD_REGION_ATTR attr = {0};
    attr.enabled = 1;
    attr.x = x;
    attr.y = y;
    attr.width = w;
    attr.height = h;
    attr.zorder = region_id;
    attr.global_alpha = alpha;

    MEDIA_OSD_MASK_DESC desc = {0};
    desc.width = w;
    desc.height = h;
    desc.stride = max_w;
    desc.data = mask;
    desc.data_size = mask_size;
    desc.color.r = r;
    desc.color.g = g;
    desc.color.b = b;
    desc.color.a = alpha;

    if (MEDIA_OSD_UpdateRegion(osd_grp, region_id, &attr) != 0 ||
        MEDIA_OSD_SetRegionMask(osd_grp, region_id, &desc) != 0) {
        return -1;
    }
    return 0;
}

int page_overlay_hide(int osd_grp, int region_id) {
    MEDIA_OSD_REGION_ATTR attr = {0};
    attr.enabled = 0;
    return MEDIA_OSD_UpdateRegion(osd_grp, region_id, &attr);
}

static int read_cpu_times(unsigned long long *idle, unsigned long long *total) {
    FILE *fp = fopen("/proc/stat", "r");
    char line[256];
    if (!fp) return -1;
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    unsigned long long user = 0, nice = 0, system = 0, idle_v = 0, iowait = 0;
    unsigned long long irq = 0, softirq = 0, steal = 0, guest = 0, guest_nice = 0;
    int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle_v, &iowait, &irq, &softirq,
                   &steal, &guest, &guest_nice);
    if (n < 4) return -1;
    *idle = idle_v + iowait;
    *total = user + nice + system + idle_v + iowait + irq + softirq + steal + guest + guest_nice;
    return 0;
}

static int read_first_line(const char *path, char *buf, size_t len) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    if (!fgets(buf, len, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

static int read_gpu_percent_file(const char *path, float *percent) {
    char buf[128];
    char *end = NULL;
    if (read_first_line(path, buf, sizeof(buf)) != 0) return -1;
    errno = 0;
    float v = strtof(buf, &end);
    if (errno != 0 || end == buf || v < 0.0f) return -1;
    if (v > 100.0f && v <= 1000.0f) v /= 10.0f;
    if (v > 100.0f) v = 100.0f;
    *percent = v;
    return 0;
}

static int find_gpu_load_path(char *path, size_t len) {
    static const char *candidates[] = {
        "/sys/class/devfreq/fb000000.gpu/load",
        "/sys/class/devfreq/gpu/load",
        "/sys/class/misc/mali0/device/utilization",
        "/sys/kernel/debug/mali0/gpu_usage",
    };
    float dummy = 0.0f;
    for (size_t i = 0; i < ARRAY_SIZE_LOCAL(candidates); ++i) {
        if (read_gpu_percent_file(candidates[i], &dummy) == 0) {
            snprintf(path, len, "%s", candidates[i]);
            return 0;
        }
    }

    DIR *dir = opendir("/sys/class/devfreq");
    if (!dir) return -1;
    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!strstr(ent->d_name, "gpu") && !strstr(ent->d_name, "mali")) continue;
        char candidate[256];
        snprintf(candidate, sizeof(candidate), "/sys/class/devfreq/%s/load", ent->d_name);
        if (read_gpu_percent_file(candidate, &dummy) == 0) {
            snprintf(path, len, "%s", candidate);
            closedir(dir);
            return 0;
        }
        snprintf(candidate, sizeof(candidate), "/sys/class/devfreq/%s/device/load", ent->d_name);
        if (read_gpu_percent_file(candidate, &dummy) == 0) {
            snprintf(path, len, "%s", candidate);
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    return -1;
}

static int read_rga_percent_file(const char *path, float *percent) {
    FILE *fp = fopen(path, "r");
    char line[256];
    float max_load = 0.0f;
    int count = 0;
    if (!fp) return -1;
    while (fgets(line, sizeof(line), fp)) {
        char *p = strstr(line, "load");
        if (!p) continue;
        p = strchr(p, '=');
        if (!p) continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        char *end = NULL;
        errno = 0;
        float v = strtof(p, &end);
        if (errno == 0 && end != p && v >= 0.0f) {
            if (v > 100.0f) v = 100.0f;
            if (v > max_load) max_load = v;
            count++;
        }
    }
    fclose(fp);
    if (count <= 0) return -1;
    *percent = max_load;
    return 0;
}

void page_overlay_update_perf(page_overlay_perf_t *perf) {
    static unsigned long long prev_idle = 0;
    static unsigned long long prev_total = 0;
    static int have_cpu = 0;
    static char gpu_path[256] = {0};
    static int gpu_path_checked = 0;
    if (!perf) return;

    unsigned long long idle = 0;
    unsigned long long total = 0;
    if (read_cpu_times(&idle, &total) == 0) {
        if (have_cpu && total > prev_total) {
            unsigned long long total_delta = total - prev_total;
            unsigned long long idle_delta = idle - prev_idle;
            if (total_delta > 0 && idle_delta <= total_delta) {
                perf->cpu_percent = (float)(total_delta - idle_delta) * 100.0f / (float)total_delta;
            }
        }
        prev_idle = idle;
        prev_total = total;
        have_cpu = 1;
    }

    if (!gpu_path_checked) {
        gpu_path_checked = 1;
        if (find_gpu_load_path(gpu_path, sizeof(gpu_path)) != 0) gpu_path[0] = '\0';
    }
    if (gpu_path[0] && read_gpu_percent_file(gpu_path, &perf->gpu_percent) == 0) {
        perf->gpu_available = 1;
    } else {
        perf->gpu_available = 0;
    }

    if (read_rga_percent_file("/sys/kernel/debug/rkrga/load", &perf->rga_percent) == 0) {
        perf->rga_available = 1;
    } else {
        perf->rga_available = 0;
    }
}

void page_overlay_format_perf(const page_overlay_perf_t *perf, char *buf, size_t len) {
    char gpu_text[24];
    char rga_text[24];
    if (!buf || len == 0) return;
    if (!perf) {
        snprintf(buf, len, "CPU N/A  GPU N/A  RGA N/A");
        return;
    }
    if (perf->gpu_available) {
        snprintf(gpu_text, sizeof(gpu_text), "%.0f%%", perf->gpu_percent);
    } else {
        snprintf(gpu_text, sizeof(gpu_text), "N/A");
    }
    if (perf->rga_available) {
        snprintf(rga_text, sizeof(rga_text), "%.0f%%", perf->rga_percent);
    } else {
        snprintf(rga_text, sizeof(rga_text), "N/A");
    }
    snprintf(buf, len, "CPU %.0f%%  GPU %s  RGA %s",
             perf->cpu_percent, gpu_text, rga_text);
}
