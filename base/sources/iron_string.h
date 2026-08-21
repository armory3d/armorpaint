#pragma once

#include "iron_array.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

char *string_alloc(int size);
char *string(char *fmt, ...);
char *string_copy(char *a);

// Frame temporary
char *string_tmp_alloc(int size);
char *string_tmp(char *fmt, ...);
char *string_replace_all_tmp(char *s, char *search, char *replace);
void  string_tmp_reset();
int   string_length(char *str);
bool  string_equals(char *a, char *b);
char *i32_to_string(int32_t i);
char *i32_to_string_hex(int32_t i);
char *i64_to_string(int64_t i);
char *u64_to_string(uint64_t i);
char *f32_to_string(float f);
char *f32_to_string_with_zeros(float f);

void         string_strip_trailing_zeros(char *str);
int32_t      string_index_of(char *s, char *search);
int32_t      string_index_of_pos(char *s, char *search, int pos);
int32_t      string_last_index_of(char *s, char *search);
any_array_t *string_split(char *s, char *sep);
void         string_split_free(any_array_t *a);
char        *string_array_join(any_array_t *a, char *separator);
char        *string_replace_all(char *s, char *search, char *replace);
char        *substring(char *s, int32_t start, int32_t end);
char        *string_from_char_code(int32_t c);
int32_t      char_code_at(char *s, int32_t i);
char        *char_at(char *s, int32_t i);
bool         starts_with(char *s, char *start);
bool         ends_with(char *s, char *end);
char        *to_lower_case(char *s);
char        *to_upper_case(char *s);
char        *trim_end(char *str);
int          string_utf8_decode(const char *str, int *i);

void  string_buffer_init(buffer_t *sb);
void  string_buffer_reset(buffer_t *sb);
void  string_buffer_append(buffer_t *sb, char *s);
char *string_buffer_get(buffer_t *sb);
void  string_buffer_free(buffer_t *sb);
