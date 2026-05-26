#ifndef ALLDEMO_TILE_STATE_H
#define ALLDEMO_TILE_STATE_H

#include <stddef.h>
#include <stdint.h>

enum {
    TILE_OFFLINE = 0,
    TILE_SYNTH = 1,
    TILE_LOOP = 2,
    TILE_PROBED = 3,
    TILE_LIVE = 4,
};

typedef struct {
    const char *name;
    int active;
    int frames;
    int status;
} module_tile_t;

extern module_tile_t g_tiles[];
extern const size_t g_tile_count;

const char *tile_status_text(int status);
void tile_status_color(int status, uint8_t *r, uint8_t *g, uint8_t *b);
void set_tile_status(const char *name, int status);
int find_tile_index(const char *name);
void mark_showcase_modules(void);
int active_module_count(void);

#endif
