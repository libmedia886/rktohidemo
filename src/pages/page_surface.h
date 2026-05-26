#ifndef ALLDEMO_PAGE_SURFACE_H
#define ALLDEMO_PAGE_SURFACE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int pool_id;
    int width;
    int height;
    int stride;
    int fps;
    int buffer_count;
    size_t frame_size;
    int sys_ok;
    int pool_ok;
    int vo_ok;
} page_surface_t;

typedef void (*page_surface_draw_fn)(uint8_t *dst, int stride, int width, int height,
                                     int frame, void *opaque);

int page_surface_open(page_surface_t *surface, int pool_id, int width, int height,
                      int stride, int fps, int buffer_count, const char *license_path);
void page_surface_close(page_surface_t *surface);
int page_surface_send_frame(page_surface_t *surface, page_surface_draw_fn draw,
                            void *opaque, int frame);

void page_surface_fill_rect_nv12(uint8_t *dst, int stride, int width, int height,
                                 int x, int y, int w, int h,
                                 uint8_t yy, uint8_t uu, uint8_t vv);
void page_surface_draw_text(uint8_t *dst, int stride, int width, int height,
                            int x, int y, const char *text, int scale,
                            uint8_t yy, uint8_t uu, uint8_t vv);

#endif
