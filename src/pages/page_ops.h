#ifndef ALLDEMO_PAGE_OPS_H
#define ALLDEMO_PAGE_OPS_H

#include <signal.h>

typedef enum {
    PAGE_IMPL_LEGACY_MAIN = 0,
    PAGE_IMPL_STANDALONE = 1,
    PAGE_IMPL_LEGACY_PROXY = 2,
} page_impl_kind_t;

typedef struct {
    const char *name;
    page_impl_kind_t impl;
    int (*run)(volatile sig_atomic_t *running);
} page_ops_t;

const page_ops_t *page_ops_find(const char *name);

#endif
