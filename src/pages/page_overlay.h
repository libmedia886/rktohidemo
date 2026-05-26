#ifndef ALLDEMO_PAGE_OVERLAY_H
#define ALLDEMO_PAGE_OVERLAY_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    float cpu_percent;
    float gpu_percent;
    int gpu_available;
    float rga_percent;
    int rga_available;
} page_overlay_perf_t;

void page_overlay_update_perf(page_overlay_perf_t *perf);
void page_overlay_format_perf(const page_overlay_perf_t *perf, char *buf, size_t len);

int page_overlay_set_text(int osd_grp, int region_id, int x, int y, int scale,
                          const char *text, uint8_t *mask, size_t mask_size,
                          int max_w, int max_h,
                          uint8_t r, uint8_t g, uint8_t b, uint8_t alpha);
int page_overlay_hide(int osd_grp, int region_id);

#endif
