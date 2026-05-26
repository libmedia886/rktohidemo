#include "page_ops.h"

#include "app/tile_state.h"
#include "media_api.h"
#include "page_surface.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#define VO_PAGE_W 1080
#define VO_PAGE_H 1920
#define VO_PAGE_STRIDE 1088
#define VO_PAGE_POOL 1
#define VO_PAGE_FPS 30
#define LICENSE_PATH "/root/licence.dat"

typedef enum {
    VO_PAGE_NORMAL = 0,
    VO_PAGE_FREEZE_MAIN,
    VO_PAGE_FREEZE_PLANE,
    VO_PAGE_HIDE_WARN,
    VO_PAGE_HIDE_PLANE,
} vo_page_state_t;

typedef struct {
    vo_page_state_t state;
    vo_page_state_t prev_state;
    const char *api;
    const char *status;
} vo_page_ctx_t;

static vo_page_state_t vo_state_for_frame(int frame) {
    int t = frame % (VO_PAGE_FPS * 18);
    if (t < VO_PAGE_FPS * 4) return VO_PAGE_NORMAL;
    if (t < VO_PAGE_FPS * 7) return VO_PAGE_FREEZE_MAIN;
    if (t < VO_PAGE_FPS * 10) return VO_PAGE_NORMAL;
    if (t < VO_PAGE_FPS * 13) return VO_PAGE_FREEZE_PLANE;
    if (t < VO_PAGE_FPS * 16) return VO_PAGE_NORMAL;
    if (t < VO_PAGE_FPS * 17) return VO_PAGE_HIDE_WARN;
    return VO_PAGE_HIDE_PLANE;
}

static void vo_restore_output(void) {
    (void)MEDIA_VO_FreezeMain(0);
    (void)MEDIA_VO_UnfreezeMain();
    (void)MEDIA_VO_FreezePlane(0, 0, 0);
    (void)MEDIA_VO_HidePlane(0, 0, 0);
}

static int vo_prepare(vo_page_ctx_t *ctx, int frame) {
    if (!ctx) return 0;
    vo_page_state_t state = vo_state_for_frame(frame);
    int changed = state != ctx->prev_state;
    if (changed &&
        (ctx->prev_state == VO_PAGE_FREEZE_MAIN ||
         ctx->prev_state == VO_PAGE_FREEZE_PLANE ||
         ctx->prev_state == VO_PAGE_HIDE_PLANE)) {
        vo_restore_output();
    }

    ctx->prev_state = state;
    ctx->state = state;
    switch (state) {
    case VO_PAGE_FREEZE_MAIN:
        ctx->api = "MEDIA VO FREEZE MAIN";
        ctx->status = "FREEZE MAIN";
        break;
    case VO_PAGE_FREEZE_PLANE:
        ctx->api = "MEDIA VO FREEZE PLANE";
        ctx->status = "FREEZE PLANE";
        break;
    case VO_PAGE_HIDE_WARN:
        ctx->api = "MEDIA VO HIDE PLANE";
        ctx->status = "HIDE WARNING";
        break;
    case VO_PAGE_HIDE_PLANE:
        ctx->api = "MEDIA VO HIDE PLANE";
        ctx->status = "HIDE PLANE";
        break;
    case VO_PAGE_NORMAL:
    default:
        ctx->api = "MEDIA SYS SEND FRAME";
        ctx->status = "NORMAL REFRESH";
        break;
    }
    return changed;
}

static void vo_apply_after_send(const vo_page_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->state == VO_PAGE_FREEZE_MAIN) {
        (void)MEDIA_VO_FreezeMain(1);
    } else if (ctx->state == VO_PAGE_FREEZE_PLANE) {
        (void)MEDIA_VO_FreezePlane(0, 0, 1);
    } else if (ctx->state == VO_PAGE_HIDE_PLANE) {
        (void)MEDIA_VO_HidePlane(0, 0, 1);
    }
}

static void draw_step(uint8_t *dst, int stride, int width, int height,
                      int x, int y, const char *label, int active) {
    page_surface_fill_rect_nv12(dst, stride, width, height, x, y, 190, 74,
                                active ? 30 : 12, active ? 84 : 128, active ? 76 : 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, x, y, 190, 4,
                                active ? 210 : 90, active ? 88 : 128, active ? 64 : 160);
    page_surface_draw_text(dst, stride, width, height, x + 18, y + 26, label, 1,
                           active ? 245 : 170, active ? 112 : 128, active ? 92 : 168);
}

static void draw_vo_page(uint8_t *dst, int stride, int width, int height,
                         int frame, void *opaque) {
    vo_page_ctx_t *ctx = (vo_page_ctx_t *)opaque;
    char frame_text[32];
    snprintf(frame_text, sizeof(frame_text), "FRAME %06d", frame);

    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, height, 6, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, 0, 0, width, 188, 26, 108, 160);
    page_surface_draw_text(dst, stride, width, height, 70, 58, "VO OUTPUT", 10, 235, 108, 176);

    int panel_x = 72;
    int panel_y = 292;
    int panel_w = width - 144;
    int panel_h = 1030;
    page_surface_fill_rect_nv12(dst, stride, width, height, panel_x, panel_y,
                                panel_w, panel_h, 14, 128, 128);
    page_surface_fill_rect_nv12(dst, stride, width, height, panel_x, panel_y,
                                panel_w, 14, 98, 96, 180);
    page_surface_fill_rect_nv12(dst, stride, width, height, panel_x, panel_y + panel_h - 14,
                                panel_w, 14, 98, 96, 180);

    page_surface_draw_text(dst, stride, width, height, panel_x + 48, panel_y + 70,
                           "CURRENT API", 3, 170, 108, 176);
    page_surface_draw_text(dst, stride, width, height, panel_x + 48, panel_y + 132,
                           ctx ? ctx->api : "MEDIA SYS SEND FRAME", 3, 220, 108, 176);
    page_surface_draw_text(dst, stride, width, height, panel_x + 48, panel_y + 230,
                           "STATE", 3, 170, 108, 176);
    page_surface_draw_text(dst, stride, width, height, panel_x + 48, panel_y + 292,
                           ctx ? ctx->status : "NORMAL REFRESH", 4, 230, 108, 176);

    const uint8_t bars[6][3] = {
        {220, 40, 50}, {40, 170, 90}, {50, 100, 220},
        {240, 220, 80}, {220, 80, 200}, {80, 230, 230},
    };
    int bar_y = panel_y + 420;
    int bar_w = panel_w / 6;
    for (int i = 0; i < 6; ++i) {
        page_surface_fill_rect_nv12(dst, stride, width, height,
                                    panel_x + i * bar_w, bar_y, bar_w, 118,
                                    bars[i][0], bars[i][1], bars[i][2]);
    }

    int scan_y = panel_y + 610;
    page_surface_fill_rect_nv12(dst, stride, width, height, panel_x + 54, scan_y,
                                panel_w - 108, 260, 10, 128, 128);
    for (int i = 0; i < 11; ++i) {
        int yy = scan_y + 24 + i * 20;
        page_surface_fill_rect_nv12(dst, stride, width, height, panel_x + 84, yy,
                                    panel_w - 168, 2, 38, 132, 150);
    }
    int scan_line = scan_y + 18 + ((frame * 8) % 218);
    page_surface_fill_rect_nv12(dst, stride, width, height, panel_x + 72, scan_line,
                                panel_w - 144, 10, 225, 64, 106);

    int step_y = panel_y + 930;
    draw_step(dst, stride, width, height, panel_x + 36, step_y, "NORMAL",
              ctx && ctx->state == VO_PAGE_NORMAL);
    draw_step(dst, stride, width, height, panel_x + 246, step_y, "FREEZE MAIN",
              ctx && ctx->state == VO_PAGE_FREEZE_MAIN);
    draw_step(dst, stride, width, height, panel_x + 456, step_y, "FREEZE PLANE",
              ctx && ctx->state == VO_PAGE_FREEZE_PLANE);
    draw_step(dst, stride, width, height, panel_x + 666, step_y, "HIDE PLANE",
              ctx && (ctx->state == VO_PAGE_HIDE_WARN || ctx->state == VO_PAGE_HIDE_PLANE));

    page_surface_draw_text(dst, stride, width, height, 80, 1600,
                           "VO CHN0 PLANE AUTO", 4, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 80, 1680,
                           frame_text, 4, 210, 108, 176);
    page_surface_draw_text(dst, stride, width, height, 80, 1780,
                           "AUTO RESTORE AFTER FREEZE OR HIDE", 2, 190, 144, 84);
}

int page_vo_run(volatile sig_atomic_t *running) {
    page_surface_t surface;
    vo_page_ctx_t ctx = {
        .state = VO_PAGE_NORMAL,
        .prev_state = (vo_page_state_t)-1,
        .api = "MEDIA SYS SEND FRAME",
        .status = "NORMAL REFRESH",
    };

    vo_restore_output();
    if (page_surface_open(&surface, VO_PAGE_POOL, VO_PAGE_W, VO_PAGE_H,
                          VO_PAGE_STRIDE, VO_PAGE_FPS, 4, LICENSE_PATH) != 0) {
        return 1;
    }

    set_tile_status("VO", TILE_LIVE);
    printf("VO standalone page. Ctrl+C to stop.\n");

    int frame = 0;
    while (!running || *running) {
        int changed = vo_prepare(&ctx, frame);
        if (page_surface_send_frame(&surface, draw_vo_page, &ctx, frame) != 0) {
            usleep(1000);
            continue;
        }
        if (changed) vo_apply_after_send(&ctx);
        frame++;
        if ((frame % VO_PAGE_FPS) == 0) {
            printf("VO frames=%d state=%s api=%s\n", frame, ctx.status, ctx.api);
        }
        usleep(1000000 / VO_PAGE_FPS);
    }

    vo_restore_output();
    page_surface_close(&surface);
    return 0;
}
