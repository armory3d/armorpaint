#pragma once

#include <stdint.h>

typedef struct i8_array {
	int8_t  *buffer;
	uint32_t length;
	uint32_t capacity;
} i8_array_t;

typedef struct u8_array {
	uint8_t *buffer;
	uint32_t length;
	uint32_t capacity;
} u8_array_t;

typedef struct i16_array {
	int16_t *buffer;
	uint32_t length;
	uint32_t capacity;
} i16_array_t;

typedef struct u16_array {
	uint16_t *buffer;
	uint32_t  length;
	uint32_t  capacity;
} u16_array_t;

typedef struct i32_array {
	int32_t *buffer;
	uint32_t length;
	uint32_t capacity;
} i32_array_t;

typedef struct u32_array {
	uint32_t *buffer;
	uint32_t  length;
	uint32_t  capacity;
} u32_array_t;

typedef struct f32_array {
	float   *buffer;
	uint32_t length;
	uint32_t capacity;
} f32_array_t;

typedef struct any_array {
	void   **buffer;
	uint32_t length;
	uint32_t capacity;
} any_array_t;

typedef struct string_array {
	char   **buffer;
	uint32_t length;
	uint32_t capacity;
} string_array_t;

typedef u8_array_t buffer_t;

void array_free(void *a);   // Releases the buffer, keeps the struct
void array_delete(void *a); // Releases the buffer and the struct, not the elements
void i8_array_push(i8_array_t *a, int8_t e);
void u8_array_push(u8_array_t *a, uint8_t e);
void i16_array_push(i16_array_t *a, int16_t e);
void u16_array_push(u16_array_t *a, uint16_t e);
void i32_array_push(i32_array_t *a, int32_t e);
void u32_array_push(u32_array_t *a, uint32_t e);
void f32_array_push(f32_array_t *a, float e);
void any_array_push(any_array_t *a, void *e);
void string_array_push(string_array_t *a, void *e);

void i8_array_resize(i8_array_t *a, uint32_t size);
void u8_array_resize(u8_array_t *a, uint32_t size);
void i16_array_resize(i16_array_t *a, uint32_t size);
void u16_array_resize(u16_array_t *a, uint32_t size);
void i32_array_resize(i32_array_t *a, uint32_t size);
void u32_array_resize(u32_array_t *a, uint32_t size);
void f32_array_resize(f32_array_t *a, uint32_t size);
void any_array_resize(any_array_t *a, uint32_t size);
void string_array_resize(string_array_t *a, uint32_t size);
void buffer_resize(buffer_t *b, uint32_t size);

void         array_sort(any_array_t *ar, int (*compare)(const void *, const void *));
void         i32_array_sort(i32_array_t *ar, int (*compare)(const void *, const void *));
void        *array_pop(any_array_t *ar);
int          i32_array_pop(i32_array_t *ar);
void        *array_shift(any_array_t *ar);
void         array_splice(any_array_t *ar, uint32_t start, uint32_t delete_count);
void         i32_array_splice(i32_array_t *ar, uint32_t start, uint32_t delete_count);
any_array_t *array_concat(any_array_t *a, any_array_t *b);
any_array_t *array_slice(any_array_t *a, uint32_t begin, uint32_t end);
void         array_insert(any_array_t *a, uint32_t at, void *e);
void         array_remove(any_array_t *ar, void *e);
void         string_array_remove(string_array_t *ar, char *e);
void         i32_array_remove(i32_array_t *ar, int e);
int32_t      array_index_of(any_array_t *ar, void *e);
int32_t      string_array_index_of(string_array_t *ar, char *e);
int32_t      i32_array_index_of(i32_array_t *ar, int e);
void         array_reverse(any_array_t *ar);

buffer_t *buffer_slice(buffer_t *a, uint32_t begin, uint32_t end);
uint8_t   buffer_get_u8(buffer_t *b, uint32_t p);
int8_t    buffer_get_i8(buffer_t *b, uint32_t p);
uint16_t  buffer_get_u16(buffer_t *b, uint32_t p);
int16_t   buffer_get_i16(buffer_t *b, uint32_t p);
float     buffer_get_f16(buffer_t *b, uint32_t p);
uint32_t  buffer_get_u32(buffer_t *b, uint32_t p);
int32_t   buffer_get_i32(buffer_t *b, uint32_t p);
float     buffer_get_f32(buffer_t *b, uint32_t p);
double    buffer_get_f64(buffer_t *b, uint32_t p);
int64_t   buffer_get_i64(buffer_t *b, uint32_t p);
void      buffer_set_u8(buffer_t *b, uint32_t p, uint8_t n);
void      buffer_set_i8(buffer_t *b, uint32_t p, int8_t n);
void      buffer_set_u16(buffer_t *b, uint32_t p, uint16_t n);
void      buffer_set_i16(buffer_t *b, uint32_t p, uint16_t n);
void      buffer_set_u32(buffer_t *b, uint32_t p, uint32_t n);
void      buffer_set_i32(buffer_t *b, uint32_t p, int32_t n);
void      buffer_set_f32(buffer_t *b, uint32_t p, float n);

buffer_t       *buffer_create(uint32_t length);
buffer_t       *buffer_create_from_raw(uint8_t *raw, uint32_t length);
f32_array_t    *f32_array_create(uint32_t length);
f32_array_t    *f32_array_create_from_buffer(buffer_t *b);
f32_array_t    *f32_array_create_from_array(f32_array_t *from);
f32_array_t    *f32_array_create_from_raw(float *raw, uint32_t length);
f32_array_t    *f32_array_create_from_raw_tmp(float *raw, uint32_t length);
f32_array_t    *f32_array_create_x(float x);
f32_array_t    *f32_array_create_xy(float x, float y);
f32_array_t    *f32_array_create_xyz(float x, float y, float z);
f32_array_t    *f32_array_create_xyzw(float x, float y, float z, float w);
f32_array_t    *f32_array_create_xyzwv(float x, float y, float z, float w, float v);
u32_array_t    *u32_array_create(uint32_t length);
u32_array_t    *u32_array_create_from_array(u32_array_t *from);
u32_array_t    *u32_array_create_from_raw(uint32_t *raw, uint32_t length);
i32_array_t    *i32_array_create(uint32_t length);
i32_array_t    *i32_array_create_from_array(i32_array_t *from);
i32_array_t    *i32_array_create_from_raw(int32_t *raw, uint32_t length);
u16_array_t    *u16_array_create(uint32_t length);
u16_array_t    *u16_array_create_from_raw(uint16_t *raw, uint32_t length);
i16_array_t    *i16_array_create(uint32_t length);
i16_array_t    *i16_array_create_from_array(i16_array_t *from);
i16_array_t    *i16_array_create_from_raw(int16_t *raw, uint32_t length);
u8_array_t     *u8_array_create(uint32_t length);
u8_array_t     *u8_array_create_from_array(u8_array_t *from);
u8_array_t     *u8_array_create_from_raw(uint8_t *raw, uint32_t length);
u8_array_t     *u8_array_create_from_string(char *s);
char           *u8_array_to_string(u8_array_t *a);
i8_array_t     *i8_array_create(uint32_t length);
i8_array_t     *i8_array_create_from_raw(int8_t *raw, uint32_t length);
any_array_t    *any_array_create(uint32_t length);
any_array_t    *any_array_create_from_raw(void **raw, uint32_t length);
any_array_t    *any_array_create_from_raw_tmp(void **raw, uint32_t length);
string_array_t *string_array_create(uint32_t length);
uint16_t        float_to_half_fast(float value);
uint8_t         half_to_u8_fast(uint16_t h);
buffer_t       *buffer_half_to_u8(buffer_t *buffer);
buffer_t       *buffer_bgra_swap(buffer_t *buffer);
buffer_t       *buffer_bgra64_swap(buffer_t *buffer);
