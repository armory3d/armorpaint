#pragma once

#include "iron_string.h"
#include <stdlib.h>
#include <string.h>

// point_t *p = ALLOC_INIT(point_t, {x: 1.5, y: 3.5});
#define ALLOC_INIT(type, ...) (type *)memcpy(malloc(sizeof(type)), (type[]){__VA_ARGS__}, sizeof(type))

#define TMP_ALLOC_INIT(type, ...) (type *)memcpy(string_tmp_alloc(sizeof(type)), (type[]){__VA_ARGS__}, sizeof(type))
