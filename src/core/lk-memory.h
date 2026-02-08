#ifndef LK_MEMORY_H
#define LK_MEMORY_H

#include "lk-data.h"

void* lk_sys_alloc(void *ud, lk_u32 bytes);
void lk_sys_dealloc(void *ud, void *ptr);
void* lk_alloc(lk_tree* t, lk_u32 bytes);
void lk_dealloc(lk_tree* t, void* ptr);

#endif
