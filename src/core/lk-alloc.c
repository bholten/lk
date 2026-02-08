#include <stdlib.h>

#include "lk-data.h"
#include "lk-memory.h"

void* lk_sys_alloc(void *ud, lk_u32 bytes) {
    (void)ud;
    return malloc((size_t)bytes);
}

void lk_sys_dealloc(void *ud, void *ptr) {
    (void)ud;
    free(ptr);
}

void* lk_alloc(lk_tree* t, lk_u32 bytes) {
    return t->alloc ? t->alloc(t->alloc_ud, bytes) : lk_sys_alloc(NULL, bytes);
}

void lk_dealloc(lk_tree* t, void* ptr) {
    if (!ptr) {
      return;
    }

    if (t->dealloc) {
      t->dealloc(t->alloc_ud, ptr);
    } else {
      lk_sys_dealloc(NULL, ptr);
    }
}

