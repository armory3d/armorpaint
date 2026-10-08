#pragma once

#include "iron_array.h"
#include "iron_map.h"
#include <stdbool.h>

void      *json_parse(char *s);
any_map_t *json_parse_to_map(char *s);

void      json_encode_begin();
char     *json_encode_end();
void      json_encode_string(char *k, char *v);
void      json_encode_string_array(char *k, string_array_t *a);
void      json_encode_f32(char *k, float f);
void      json_encode_i32(char *k, int i);
void      json_encode_null(char *k);
void      json_encode_f32_array(char *k, f32_array_t *a);
void      json_encode_i32_array(char *k, i32_array_t *a);
void      json_encode_bool(char *k, bool b);
void      json_encode_begin_array(char *k);
void      json_encode_end_array();
void      json_encode_begin_object();
void      json_encode_begin_object_key(char *k);
void      json_encode_end_object();
void      json_encode_map(any_map_t *m);
buffer_t *json_encode_to_armpack(char *json);
