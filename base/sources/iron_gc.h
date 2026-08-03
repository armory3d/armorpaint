#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// point_t *p = GC_ALLOC_INIT(point_t, {x: 1.5, y: 3.5});
#define GC_ALLOC_INIT(type, ...) (type *)memcpy(gc_alloc(sizeof(type)), (type[]){__VA_ARGS__}, sizeof(type))

void *gc_alloc(size_t size);
void  gc_leaf(void *ptr);
void  gc_root(void *ptr);
void  gc_unroot(void *ptr);
void  gc_resize(void *ptr, size_t size);
void *gc_realloc(void *ptr, size_t size);
void  gc_free(void *ptr);
void  gc_pause();
void  gc_resume();
void  gc_run();
void  gc_start(void *bos);
void  gc_stop();
