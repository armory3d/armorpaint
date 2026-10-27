#pragma once

#include "iron_array.h"
#include "iron_global.h"

void      iron_write_jpg(char *path, buffer_t *bytes, i32 w, i32 h, i32 format, i32 quality);
void      iron_write_png(char *path, buffer_t *bytes, i32 w, i32 h, i32 format);
buffer_t *iron_encode_jpg(buffer_t *bytes, i32 w, i32 h, i32 format, i32 quality);
buffer_t *iron_encode_png(buffer_t *bytes, i32 w, i32 h, i32 format);

void iron_mp4_begin(char *path, i32 w, i32 h);
void iron_mp4_end();
void iron_mp4_encode(buffer_t *pixels, f64 time);
