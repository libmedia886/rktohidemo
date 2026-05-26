#ifndef ALLDEMO_LOOP_PLAYER_H
#define ALLDEMO_LOOP_PLAYER_H

#include <signal.h>

#include "page_registry.h"

int run_default_only_loop(const char *argv0, int rotate_main,
                          const demo_loop_t *loop,
                          volatile sig_atomic_t *running);

#endif
