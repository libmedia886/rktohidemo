#include "page_ops.h"

#include "app/page_registry.h"
#include "app/tile_state.h"
#include "page_overlay.h"
#include "page_surface.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#define LICENSE_PAGE_W 1080
#define LICENSE_PAGE_H 1920
#define LICENSE_PAGE_STRIDE 1088
#define LICENSE_PAGE_POOL 1
#define LICENSE_PAGE_FPS 30
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    int license_ok;
} license_page_state_t;

static int file_readable(const char *path) {
    return access(path, R_OK) == 0;
}

static void draw_license_page(uint8_t *dst, int stride, int width, int height,
                              int frame, void *opaque) {
    license_page_state_t *state = (license_page_state_t *)opaque;
    int license_ok = state ? state->license_ok : 0;
    page_overlay_perf_t perf = {0};
    char page_text[32];
    char perf_text[96];
    char frame_text[32];
    snprintf(page_text, sizeof(page_text), "PAGE %02d/%02d",
             module_page_number("LICENSE"), module_page_total());
    snprintf(frame_text, sizeof(frame_text), "FRAME %06d", frame);
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_text, sizeof(perf_text));

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 18, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 112, 13, 134, 118);
    page_surface_fill_rect_nv12(dst, stride, width, height, 32, 132, width - 64, 1580, 24, 128, 128);

    page_surface_draw_text(dst, stride, width, height, 34, 38, "LICENSE", 3, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, width - 210, 44, page_text, 2, 170, 144, 84);

    page_surface_fill_rect_nv12(dst, stride, width, height, 86, 300, 908, 4, 190, 84, 188);
    page_surface_fill_rect_nv12(dst, stride, width, height, 86, 300, 4, 152, 190, 84, 188);
    page_surface_fill_rect_nv12(dst, stride, width, height, 990, 300, 4, 152, 190, 84, 188);
    page_surface_fill_rect_nv12(dst, stride, width, height, 86, 448, 908, 4, 190, 84, 188);
    page_surface_draw_text(dst, stride, width, height, 124, 346, "LICENSE", 4, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 124, 412,
                           "SYNTHETIC PLACEHOLDER", 1, 170, 144, 84);

    page_surface_fill_rect_nv12(dst, stride, width, height, 88, 1508, 872, 36, 22, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 88, 1564, 872, 36, 22, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 88, 1620, 872, 36, 22, 128, 128);
    page_surface_draw_text(dst, stride, width, height, 122, 1517, "STATE", 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 310, 1517,
                           license_ok ? "LIVE" : "OFFLINE", 1,
                           license_ok ? 160 : 255, license_ok ? 255 : 150,
                           license_ok ? 220 : 160);
    page_surface_draw_text(dst, stride, width, height, 122, 1573, "SOURCE", 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 310, 1573,
                           LICENSE_PATH, 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 122, 1629, "MODE", 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 310, 1629,
                           "MODULE AUTH CHECK", 1, 170, 144, 84);

    page_surface_draw_text(dst, stride, width, height, 34, 1824, perf_text, 1, 255, 230, 120);
    page_surface_draw_text(dst, stride, width, height, 850, 1824,
                           license_ok ? "LIVE" : "OFFLINE", 1,
                           license_ok ? 160 : 255, license_ok ? 255 : 150,
                           license_ok ? 220 : 160);
    page_surface_draw_text(dst, stride, width, height, 34, 1874, frame_text, 1, 170, 205, 235);
}

int page_license_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    license_page_state_t state = {
        .license_ok = file_readable(LICENSE_PATH),
    };

    if (page_surface_open(&surface, LICENSE_PAGE_POOL, LICENSE_PAGE_W, LICENSE_PAGE_H,
                          LICENSE_PAGE_STRIDE, LICENSE_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        return 1;
    }

    if (state.license_ok) set_tile_status("LICENSE", TILE_LIVE);
    set_tile_status("VO", TILE_LIVE);
    printf("LICENSE standalone page path=%s readable=%d. Ctrl+C to stop.\n",
           LICENSE_PATH, state.license_ok);

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_license_page, &state, frame) != 0) {
            usleep(1000);
            continue;
        }
        frame++;
        if ((frame % LICENSE_PAGE_FPS) == 0) {
            printf("LICENSE frames=%d path=%s readable=%d\n",
                   frame, LICENSE_PATH, state.license_ok);
        }
        usleep(1000000 / LICENSE_PAGE_FPS);
    }

    page_surface_close(&surface);
    return 0;
}
