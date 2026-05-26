#include "page_ops.h"

#include "app/page_registry.h"
#include "app/tile_state.h"
#include "page_overlay.h"
#include "page_surface.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define INFO_PAGE_W 1080
#define INFO_PAGE_H 1920
#define INFO_PAGE_STRIDE 1088
#define INFO_PAGE_POOL 1
#define INFO_PAGE_FPS 30
#define LICENSE_PATH "/root/licence.dat"

typedef struct {
    const char *name;
    char label[64];
    int page_number;
    int page_total;
} info_page_state_t;

static void make_ascii_label(char *dst, size_t dst_len, const char *name) {
    if (!dst || dst_len == 0) return;
    if (!name) name = "INFO";
    size_t i = 0;
    for (; name[i] && i + 1 < dst_len; ++i) {
        dst[i] = name[i] == '_' ? ' ' : name[i];
    }
    dst[i] = '\0';
}

static void draw_info_page(uint8_t *dst, int stride, int width, int height,
                           int frame, void *opaque) {
    info_page_state_t *state = (info_page_state_t *)opaque;
    const char *label = state && state->label[0] ? state->label : "INFO";
    page_overlay_perf_t perf = {0};
    char frame_text[32];
    char page_text[32];
    char perf_text[96];
    snprintf(frame_text, sizeof(frame_text), "FRAME %06d", frame);
    snprintf(page_text, sizeof(page_text), "PAGE %02d/%02d",
             state ? state->page_number : 0,
             state ? state->page_total : 0);
    page_overlay_update_perf(&perf);
    page_overlay_format_perf(&perf, perf_text, sizeof(perf_text));

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 18, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 112, 13, 134, 118);
    page_surface_fill_rect_nv12(dst, stride, width, height, 32, 132, width - 64, 1580, 24, 128, 128);

    page_surface_draw_text(dst, stride, width, height, 34, 38, label, 3, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, width - 210, 44, page_text, 2, 170, 144, 84);

    page_surface_fill_rect_nv12(dst, stride, width, height, 86, 300, 908, 4, 190, 84, 188);
    page_surface_fill_rect_nv12(dst, stride, width, height, 86, 300, 4, 152, 190, 84, 188);
    page_surface_fill_rect_nv12(dst, stride, width, height, 990, 300, 4, 152, 190, 84, 188);
    page_surface_fill_rect_nv12(dst, stride, width, height, 86, 448, 908, 4, 190, 84, 188);
    page_surface_draw_text(dst, stride, width, height, 124, 346, label, 4, 190, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 124, 412,
                           "SYNTHETIC PLACEHOLDER", 1, 170, 144, 84);

    page_surface_fill_rect_nv12(dst, stride, width, height, 88, 1508, 872, 36, 22, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 88, 1564, 872, 36, 22, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 88, 1620, 872, 36, 22, 128, 128);
    page_surface_draw_text(dst, stride, width, height, 122, 1517, "STATE", 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 310, 1517, "OFFLINE", 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 122, 1573, "SOURCE", 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 310, 1573, "WAITING FOR DEV PATH", 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 122, 1629, "MODE", 1, 170, 144, 84);
    page_surface_draw_text(dst, stride, width, height, 310, 1629, "MODULE TILE", 1, 170, 144, 84);

    page_surface_draw_text(dst, stride, width, height, 34, 1824, perf_text, 1, 255, 230, 120);
    page_surface_draw_text(dst, stride, width, height, 850, 1824, "OFFLINE", 1, 255, 150, 160);
    page_surface_draw_text(dst, stride, width, height, 34, 1874, frame_text, 1, 170, 205, 235);
}

static int page_info_run_named(const char *name, volatile sig_atomic_t *running) {
    page_surface_t surface;
    info_page_state_t state = {
        .name = name,
        .page_number = module_page_number(name),
        .page_total = module_page_total(),
    };
    make_ascii_label(state.label, sizeof(state.label), name);

    const page_desc_t *desc = page_desc_find(name);
    if (!desc) {
        fprintf(stderr, "info page: unknown page %s\n", name ? name : "(null)");
        return 1;
    }

    if (page_surface_open(&surface, INFO_PAGE_POOL, INFO_PAGE_W, INFO_PAGE_H,
                          INFO_PAGE_STRIDE, INFO_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        return 1;
    }

    set_tile_status(name, TILE_PROBED);
    set_tile_status("VO", TILE_LIVE);
    printf("%s standalone info page. Ctrl+C to stop.\n", name);
    printf("%s flow: %s\n", name, desc->flow_note ? desc->flow_note : "");
    printf("%s showcase: %s\n", name, desc->showcase_note ? desc->showcase_note : "");

    int frame = 0;
    while (!running || *running) {
        if (page_surface_send_frame(&surface, draw_info_page, &state, frame) != 0) {
            usleep(1000);
            continue;
        }
        frame++;
        if ((frame % INFO_PAGE_FPS) == 0) {
            printf("%s info frames=%d\n", name, frame);
        }
        usleep(1000000 / INFO_PAGE_FPS);
    }

    page_surface_close(&surface);
    return 0;
}

int page_info_vmix_rga_run(volatile sig_atomic_t *running) {
    return page_info_run_named("VMIX_RGA", running);
}

int page_info_avm_run(volatile sig_atomic_t *running) {
    return page_info_run_named("AVM", running);
}

int page_info_svm3d_run(volatile sig_atomic_t *running) {
    return page_info_run_named("SVM3D", running);
}

int page_info_exposure_fusion_cl_run(volatile sig_atomic_t *running) {
    return page_info_run_named("EXPOSURE_FUSION_CL", running);
}

int page_info_venc_run(volatile sig_atomic_t *running) {
    return page_info_run_named("VENC", running);
}

int page_info_vdec_run(volatile sig_atomic_t *running) {
    return page_info_run_named("VDEC", running);
}

int page_info_rtsp_send_run(volatile sig_atomic_t *running) {
    return page_info_run_named("RTSP_SEND", running);
}

int page_info_rtsp_recv_run(volatile sig_atomic_t *running) {
    return page_info_run_named("RTSP_RECV", running);
}

int page_info_pic_io_run(volatile sig_atomic_t *running) {
    return page_info_run_named("PIC_IO", running);
}
