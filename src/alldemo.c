#include "app/page_registry.h"
#include "app/tile_state.h"
#include "legacy/alldemo_legacy.h"
#include "pages/page_ops.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static const char *parse_only_tile(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            return canonical_tile_name(argv[i + 1]);
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGALRM, on_signal);

    const char *only_tile = parse_only_tile(argc, argv);
    if (!only_tile) {
        return alldemo_legacy_main(argc, argv);
    }

    const page_ops_t *ops = page_ops_find(only_tile);
    if (!ops || find_tile_index(only_tile) < 0) {
        fprintf(stderr, "unknown tile for --only: %s\n", only_tile);
        return 1;
    }
    if (ops->run) {
        return ops->run(&g_running);
    }
    return alldemo_legacy_main(argc, argv);
}
