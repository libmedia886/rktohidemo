#include "tile_state.h"

#include "page_registry.h"

#include <string.h>
#include <strings.h>

module_tile_t g_tiles[] = {
    {"VI", 0, 0, TILE_OFFLINE}, {"VPSS", 0, 0, TILE_OFFLINE},
    {"VO", 0, 0, TILE_OFFLINE}, {"WBC", 0, 0, TILE_OFFLINE},
    {"RGA", 0, 0, TILE_OFFLINE},
    {"RESIZE_RGA", 0, 0, TILE_OFFLINE}, {"CSC_RGA", 0, 0, TILE_OFFLINE},
    {"CSC_CL", 0, 0, TILE_OFFLINE}, {"OSD", 0, 0, TILE_OFFLINE},
    {"CLAHE", 0, 0, TILE_OFFLINE}, {"RETINEX", 0, 0, TILE_OFFLINE},
    {"RETINEX_OFFLINE", 0, 0, TILE_OFFLINE}, {"TNR_CL", 0, 0, TILE_OFFLINE},
    {"HIGHLIGHT_SUPPRESS", 0, 0, TILE_OFFLINE},
    {"HIGHLIGHT_SUPPRESS_VI", 0, 0, TILE_OFFLINE},
    {"EIS", 0, 0, TILE_OFFLINE}, {"EIS_VI", 0, 0, TILE_OFFLINE},
    {"CAP_DEHAZE", 0, 0, TILE_OFFLINE}, {"CAP_DEHAZE_OFFLINE", 0, 0, TILE_OFFLINE},
    {"DCP_FAST_DEHAZE", 0, 0, TILE_OFFLINE},
    {"THERMAL", 0, 0, TILE_OFFLINE}, {"THERMAL_SR_NPU", 0, 0, TILE_OFFLINE},
    {"CONV_CL", 0, 0, TILE_OFFLINE},
    {"TRANSFORM", 0, 0, TILE_OFFLINE}, {"BLEND_PYR", 0, 0, TILE_OFFLINE},
    {"EDOF_CL", 0, 0, TILE_OFFLINE}, {"EXPOSURE_FUSION_CL", 0, 0, TILE_OFFLINE},
    {"MCF_FUSION_CL", 0, 0, TILE_OFFLINE},
    {"DUALVIEW", 0, 0, TILE_OFFLINE}, {"STEREO_3D", 0, 0, TILE_OFFLINE},
    {"VMIX", 0, 0, TILE_OFFLINE}, {"VMIX_RGA", 0, 0, TILE_OFFLINE},
    {"PANO", 0, 0, TILE_OFFLINE}, {"AVM", 0, 0, TILE_OFFLINE},
    {"AVM2D", 0, 0, TILE_OFFLINE},
    {"SVM3D", 0, 0, TILE_OFFLINE}, {"DETECT_NPU", 0, 0, TILE_OFFLINE},
    {"VENC", 0, 0, TILE_OFFLINE}, {"VDEC", 0, 0, TILE_OFFLINE},
    {"RTSP_SEND", 0, 0, TILE_OFFLINE}, {"RTSP_RECV", 0, 0, TILE_OFFLINE},
    {"PIC_IO", 0, 0, TILE_OFFLINE}, {"LICENSE", 0, 0, TILE_OFFLINE},
};

const size_t g_tile_count = sizeof(g_tiles) / sizeof(g_tiles[0]);

const char *tile_status_text(int status) {
    switch (status) {
    case TILE_LIVE: return "LIVE";
    case TILE_PROBED: return "PROBED";
    case TILE_LOOP: return "LOOP";
    case TILE_SYNTH: return "SYNTH";
    default: return "OFFLINE";
    }
}

void tile_status_color(int status, uint8_t *r, uint8_t *g, uint8_t *b) {
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

void set_tile_status(const char *name, int status) {
    name = canonical_tile_name(name);
    for (size_t i = 0; i < g_tile_count; ++i) {
        if (strcmp(g_tiles[i].name, name) == 0) {
            g_tiles[i].status = status;
            g_tiles[i].active = status != TILE_OFFLINE;
            return;
        }
    }
}

int find_tile_index(const char *name) {
    if (!name || !*name) return -1;
    name = canonical_tile_name(name);
    for (size_t i = 0; i < g_tile_count; ++i) {
        if (strcasecmp(g_tiles[i].name, name) == 0) return (int)i;
    }
    return -1;
}

void mark_showcase_modules(void) {
    for (size_t i = 0; i < g_tile_count; ++i) {
        int in_default = default_page_index(g_tiles[i].name) >= 0;
        g_tiles[i].active = in_default;
        g_tiles[i].status = in_default ? TILE_SYNTH : TILE_OFFLINE;
    }
}

int active_module_count(void) {
    int total = 0;
    for (size_t i = 0; i < g_tile_count; ++i) {
        if (g_tiles[i].active) total++;
    }
    return total;
}
