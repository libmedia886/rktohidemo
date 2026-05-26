#ifndef ALLDEMO_PAGE_REGISTRY_H
#define ALLDEMO_PAGE_REGISTRY_H

#define NAV_ENV_PAGE "ALLDEMO_LOOP_PAGE"
#define NAV_ENV_TOTAL "ALLDEMO_LOOP_TOTAL"
#define NAV_ENV_MODE "ALLDEMO_LOOP_MODE"
#define NAV_ENV_PROFILE "ALLDEMO_LOOP_PROFILE"

typedef struct {
    const char *name;
    const char *profile;
    const char *const *pages;
    int total_pages;
    int rotate_seconds;
} demo_loop_t;

typedef enum {
    PAGE_BIND_NONE = 0,
    PAGE_BIND_VI,
    PAGE_BIND_WBC,
    PAGE_BIND_VMIX,
    PAGE_BIND_VPSS,
    PAGE_BIND_OSD,
    PAGE_BIND_CLAHE,
    PAGE_BIND_RETINEX,
    PAGE_BIND_HIGHLIGHT_SUPPRESS_VI,
    PAGE_BIND_CAP_DEHAZE,
    PAGE_BIND_DCP_DEHAZE,
    PAGE_BIND_RGA,
    PAGE_BIND_RESIZE_RGA,
    PAGE_BIND_CSC_RGA,
    PAGE_BIND_CSC_CL,
    PAGE_BIND_TRANSFORM,
    PAGE_BIND_STEREO_3D,
} page_bind_display_t;

enum {
    PAGE_FLAG_CAMERA = 1u << 0,
    PAGE_FLAG_CAMERA_4K = 1u << 1,
    PAGE_FLAG_CAMERA_1080P = 1u << 2,
    PAGE_FLAG_VMIX_OSD_DISPLAY = 1u << 3,
};

typedef struct {
    const char *name;
    const char *flow_note;
    const char *showcase_note;
    unsigned flags;
    page_bind_display_t bind_display;
} page_desc_t;

const demo_loop_t *alldemo_customer_loop(void);
const demo_loop_t *alldemo_engineering_loop(void);

int loop_page_rotate_seconds(const demo_loop_t *loop, const char *module);

int module_page_number(const char *name);
int module_page_total(void);
const char *showcase_nav_mode(void);

int default_page_index(const char *name);
int alldemo_default_page_count(void);
const char *alldemo_default_page_name(int index);

const page_desc_t *page_desc_find(const char *name);
int page_has_flag(const char *name, unsigned flag);
page_bind_display_t page_bind_display(const char *name);
const char *module_flow_note(const char *name);
const char *module_showcase_note(const char *name);

const char *canonical_tile_name(const char *name);
int tile_needs_camera(const char *name);
int tile_uses_4k_camera_input(const char *name);
int tile_uses_1080p_camera_input(const char *name);

#endif
