#include "iron_array.h"

#include "iron_gc.h"
#include "iron_string.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void array_free(void *a) {
	u8_array_t *tmp = (u8_array_t *)a;
	gc_free(tmp->buffer);
	tmp->buffer = NULL;
	tmp->length = tmp->capacity = 0;
}

static void *gc_realloc_no_free(void *ptr, size_t old_size, size_t new_size) {
	void *buffer = gc_alloc(new_size);
	memcpy(buffer, ptr, old_size);
	return buffer;
}

static void array_alloc(void *a, uint8_t element_size) {
	u8_array_t *tmp = (u8_array_t *)a;
	if (tmp->length >= tmp->capacity) {
		if (tmp->capacity == 0) {
			// If the array was created in armpack, length can already be > 0
			tmp->capacity   = tmp->length + 1;
			size_t old_size = tmp->length * element_size;
			size_t new_size = tmp->capacity * element_size;
			tmp->buffer     = gc_realloc_no_free(tmp->buffer, old_size, new_size);
		}
		else {
			tmp->capacity *= 2;
			tmp->buffer = gc_realloc(tmp->buffer, tmp->capacity * element_size);
		}
		if (element_size < 8) {
			gc_leaf(tmp->buffer);
		}
	}
}

void i8_array_push(i8_array_t *a, int8_t e) {
	array_alloc(a, sizeof(int8_t));
	a->buffer[a->length++] = e;
}

void u8_array_push(u8_array_t *a, uint8_t e) {
	array_alloc(a, sizeof(uint8_t));
	a->buffer[a->length++] = e;
}

void i16_array_push(i16_array_t *a, int16_t e) {
	array_alloc(a, sizeof(int16_t));
	a->buffer[a->length++] = e;
}

void u16_array_push(u16_array_t *a, uint16_t e) {
	array_alloc(a, sizeof(uint16_t));
	a->buffer[a->length++] = e;
}

void i32_array_push(i32_array_t *a, int32_t e) {
	array_alloc(a, sizeof(int32_t));
	a->buffer[a->length++] = e;
}

void u32_array_push(u32_array_t *a, uint32_t e) {
	array_alloc(a, sizeof(uint32_t));
	a->buffer[a->length++] = e;
}

void f32_array_push(f32_array_t *a, float e) {
	array_alloc(a, sizeof(float));
	a->buffer[a->length++] = e;
}

void any_array_push(any_array_t *a, void *e) {
	array_alloc(a, sizeof(uintptr_t));
	a->buffer[a->length++] = e;
}

void string_array_push(string_array_t *a, void *e) {
	array_alloc(a, sizeof(uintptr_t));
	a->buffer[a->length++] = e;
}

void i8_array_resize(i8_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(int8_t));
	gc_leaf(a->buffer);
}

void u8_array_resize(u8_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(uint8_t));
	gc_leaf(a->buffer);
}

void i16_array_resize(i16_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(int16_t));
	gc_leaf(a->buffer);
}

void u16_array_resize(u16_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(uint16_t));
	gc_leaf(a->buffer);
}

void i32_array_resize(i32_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(int32_t));
	gc_leaf(a->buffer);
}

void u32_array_resize(u32_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(uint32_t));
	gc_leaf(a->buffer);
}

void f32_array_resize(f32_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(float));
	gc_leaf(a->buffer);
}

void any_array_resize(any_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(void *));
}

void string_array_resize(string_array_t *a, uint32_t size) {
	a->capacity = size;
	a->buffer   = gc_realloc(a->buffer, a->capacity * sizeof(void *));
}

void buffer_resize(buffer_t *b, uint32_t size) {
	b->length = size;
	b->buffer = gc_realloc(b->buffer, b->length * sizeof(uint8_t));
	gc_leaf(b->buffer);
}

static int _array_sort_alpha(const void *a, const void *b) {
	return strcmp(*(const char **)a, *(const char **)b);
}

void array_sort(any_array_t *ar, int (*compare)(const void *, const void *)) {
	if (compare == NULL) {
		compare = _array_sort_alpha;
	}
	qsort(ar->buffer, ar->length, sizeof(ar->buffer[0]), compare);
}

static int _array_sort_num(const void *a, const void *b) {
	return (*(int *)a - *(int *)b);
}

void i32_array_sort(i32_array_t *ar, int (*compare)(const void *, const void *)) {
	if (compare == NULL) {
		compare = _array_sort_num;
	}
	qsort(ar->buffer, ar->length, sizeof(ar->buffer[0]), compare);
}

void *array_pop(any_array_t *ar) {
	ar->length--;
	return ar->buffer[ar->length];
}

int i32_array_pop(i32_array_t *ar) {
	ar->length--;
	return ar->buffer[ar->length];
}

void *array_shift(any_array_t *ar) {
	void *first = ar->buffer[0];
	for (uint32_t i = 0; i < ar->length - 1; ++i) {
		ar->buffer[i] = ar->buffer[i + 1];
	}
	ar->length--;
	return first;
}

void array_splice(any_array_t *ar, uint32_t start, uint32_t delete_count) {
	for (uint32_t i = start; i < ar->length; ++i) {
		if (i + delete_count >= ar->length) {
			ar->buffer[i] = NULL;
		}
		else {
			ar->buffer[i] = ar->buffer[i + delete_count];
		}
	}
	ar->length -= delete_count;
}

void i32_array_splice(i32_array_t *ar, uint32_t start, uint32_t delete_count) {
	for (uint32_t i = start; i < ar->length; ++i) {
		if (i + delete_count >= ar->length) {
			ar->buffer[i] = 0;
		}
		else {
			ar->buffer[i] = ar->buffer[i + delete_count];
		}
	}
	ar->length -= delete_count;
}

any_array_t *array_concat(any_array_t *a, any_array_t *b) {
	any_array_t *ar = gc_alloc(sizeof(any_array_t));
	ar->length      = a->length + b->length;
	any_array_resize(ar, ar->length);
	for (uint32_t i = 0; i < a->length; ++i) {
		ar->buffer[i] = a->buffer[i];
	}
	for (uint32_t i = 0; i < b->length; ++i) {
		ar->buffer[a->length + i] = b->buffer[i];
	}
	return ar;
}

any_array_t *array_slice(any_array_t *a, uint32_t begin, uint32_t end) {
	any_array_t *ar = gc_alloc(sizeof(any_array_t));
	ar->length      = end - begin;
	any_array_resize(ar, ar->length);
	for (uint32_t i = 0; i < ar->length; ++i) {
		ar->buffer[i] = a->buffer[begin + i];
	}
	return ar;
}

void array_insert(any_array_t *a, uint32_t at, void *e) {
	array_alloc(a, sizeof(uintptr_t));
	for (uint32_t i = a->length; i > at; --i) {
		a->buffer[i] = a->buffer[i - 1];
	}
	a->length++;
	a->buffer[at] = e;
}

void array_remove(any_array_t *ar, void *e) {
	int32_t i = array_index_of(ar, e);
	if (i > -1) {
		array_splice(ar, i, 1);
	}
}

void string_array_remove(string_array_t *ar, char *e) {
	int32_t i = string_array_index_of(ar, e);
	if (i > -1) {
		array_splice(ar, i, 1);
	}
}

void i32_array_remove(i32_array_t *ar, int e) {
	int32_t i = i32_array_index_of(ar, e);
	if (i > -1) {
		i32_array_splice(ar, i, 1);
	}
}

int32_t array_index_of(any_array_t *ar, void *e) {
	for (uint32_t i = 0; i < ar->length; ++i) {
		if (ar->buffer[i] == e) {
			return i;
		}
	}
	return -1;
}

int32_t string_array_index_of(string_array_t *ar, char *e) {
	for (uint32_t i = 0; i < ar->length; ++i) {
		if (string_equals(ar->buffer[i], e)) {
			return i;
		}
	}
	return -1;
}

int32_t i32_array_index_of(i32_array_t *ar, int e) {
	for (uint32_t i = 0; i < ar->length; ++i) {
		if (ar->buffer[i] == e) {
			return i;
		}
	}
	return -1;
}

void array_reverse(any_array_t *ar) {
	for (uint32_t i = 0; i < ar->length / 2; ++i) {
		void *tmp                      = ar->buffer[i];
		ar->buffer[i]                  = ar->buffer[ar->length - 1 - i];
		ar->buffer[ar->length - 1 - i] = tmp;
	}
}

buffer_t *buffer_slice(buffer_t *a, uint32_t begin, uint32_t end) {
	buffer_t *b = gc_alloc(sizeof(buffer_t));
	buffer_resize(b, end - begin);
	for (uint32_t i = 0; i < b->length; ++i) {
		b->buffer[i] = a->buffer[begin + i];
	}
	return b;
}

uint8_t buffer_get_u8(buffer_t *b, uint32_t p) {
	return *(uint8_t *)(b->buffer + p);
}

int8_t buffer_get_i8(buffer_t *b, uint32_t p) {
	return *(int8_t *)(b->buffer + p);
}

uint16_t buffer_get_u16(buffer_t *b, uint32_t p) {
	return *(uint16_t *)(b->buffer + p);
}

int16_t buffer_get_i16(buffer_t *b, uint32_t p) {
	return *(int16_t *)(b->buffer + p);
}

float buffer_get_f16(buffer_t *b, uint32_t p) {
	uint16_t half     = *(uint16_t *)(b->buffer + p);
	uint32_t sign     = (half & 0x8000) << 16;
	uint32_t exp_frac = half & 0x7FFF;
	if (exp_frac == 0)
		return 0.0;
	if ((half & 0x7C00) == 0x7C00)
		return NAN;
	union {
		float    f;
		uint32_t u;
	} result;
	result.u = sign | ((exp_frac << 13) + 0x38000000);
	return result.f;
}

uint32_t buffer_get_u32(buffer_t *b, uint32_t p) {
	return *(uint32_t *)(b->buffer + p);
}

int32_t buffer_get_i32(buffer_t *b, uint32_t p) {
	return *(int32_t *)(b->buffer + p);
}

float buffer_get_f32(buffer_t *b, uint32_t p) {
	return *(float *)(b->buffer + p);
}

double buffer_get_f64(buffer_t *b, uint32_t p) {
	return *(double *)(b->buffer + p);
}

int64_t buffer_get_i64(buffer_t *b, uint32_t p) {
	return *(int64_t *)(b->buffer + p);
}

void buffer_set_u8(buffer_t *b, uint32_t p, uint8_t n) {
	*(uint8_t *)(b->buffer + p) = n;
}

void buffer_set_i8(buffer_t *b, uint32_t p, int8_t n) {
	*(int8_t *)(b->buffer + p) = n;
}

void buffer_set_u16(buffer_t *b, uint32_t p, uint16_t n) {
	*(uint16_t *)(b->buffer + p) = n;
}

void buffer_set_i16(buffer_t *b, uint32_t p, uint16_t n) {
	*(int16_t *)(b->buffer + p) = n;
}

void buffer_set_u32(buffer_t *b, uint32_t p, uint32_t n) {
	*(uint32_t *)(b->buffer + p) = n;
}

void buffer_set_i32(buffer_t *b, uint32_t p, int32_t n) {
	*(int32_t *)(b->buffer + p) = n;
}

void buffer_set_f32(buffer_t *b, uint32_t p, float n) {
	*(float *)(b->buffer + p) = n;
}

buffer_t *buffer_create(uint32_t length) {
	buffer_t *b = gc_alloc(sizeof(buffer_t));
	buffer_resize(b, length);
	return b;
}

buffer_t *buffer_create_from_raw(uint8_t *raw, uint32_t length) {
	buffer_t *b = gc_alloc(sizeof(buffer_t));
	b->buffer   = raw;
	b->length   = length;
	b->capacity = length;
	return b;
}

f32_array_t *f32_array_create(uint32_t length) {
	f32_array_t *a = gc_alloc(sizeof(f32_array_t));
	if (length > 0) {
		f32_array_resize(a, length);
		a->length = length;
	}
	return a;
}

f32_array_t *f32_array_create_from_buffer(buffer_t *b) {
	f32_array_t *a = gc_alloc(sizeof(f32_array_t));
	a->buffer      = b->buffer;
	a->length      = b->length / 4;
	a->capacity    = b->length / 4;
	return a;
}

f32_array_t *f32_array_create_from_array(f32_array_t *from) {
	f32_array_t *a = f32_array_create(from->length);
	for (uint32_t i = 0; i < from->length; ++i) {
		a->buffer[i] = from->buffer[i];
	}
	return a;
}

f32_array_t *f32_array_create_from_raw(float *raw, uint32_t length) {
	f32_array_t *a = f32_array_create(length);
	for (uint32_t i = 0; i < length; ++i) {
		a->buffer[i] = raw[i];
	}
	return a;
}

f32_array_t *f32_array_create_x(float x) {
	f32_array_t *a = f32_array_create(1);
	a->buffer[0]   = x;
	return a;
}

f32_array_t *f32_array_create_xy(float x, float y) {
	f32_array_t *a = f32_array_create(2);
	a->buffer[0]   = x;
	a->buffer[1]   = y;
	return a;
}

f32_array_t *f32_array_create_xyz(float x, float y, float z) {
	f32_array_t *a = f32_array_create(3);
	a->buffer[0]   = x;
	a->buffer[1]   = y;
	a->buffer[2]   = z;
	return a;
}

f32_array_t *f32_array_create_xyzw(float x, float y, float z, float w) {
	f32_array_t *a = f32_array_create(4);
	a->buffer[0]   = x;
	a->buffer[1]   = y;
	a->buffer[2]   = z;
	a->buffer[3]   = w;
	return a;
}

f32_array_t *f32_array_create_xyzwv(float x, float y, float z, float w, float v) {
	f32_array_t *a = f32_array_create(5);
	a->buffer[0]   = x;
	a->buffer[1]   = y;
	a->buffer[2]   = z;
	a->buffer[3]   = w;
	a->buffer[4]   = v;
	return a;
}

u32_array_t *u32_array_create(uint32_t length) {
	u32_array_t *a = gc_alloc(sizeof(u32_array_t));
	if (length > 0) {
		u32_array_resize(a, length);
		a->length = length;
	}
	return a;
}

u32_array_t *u32_array_create_from_array(u32_array_t *from) {
	u32_array_t *a = u32_array_create(from->length);
	for (uint32_t i = 0; i < from->length; ++i) {
		a->buffer[i] = from->buffer[i];
	}
	return a;
}

u32_array_t *u32_array_create_from_raw(uint32_t *raw, uint32_t length) {
	u32_array_t *a = u32_array_create(length);
	for (uint32_t i = 0; i < length; ++i) {
		a->buffer[i] = raw[i];
	}
	return a;
}

i32_array_t *i32_array_create(uint32_t length) {
	i32_array_t *a = gc_alloc(sizeof(i32_array_t));
	if (length > 0) {
		i32_array_resize(a, length);
		a->length = length;
	}
	return a;
}

i32_array_t *i32_array_create_from_array(i32_array_t *from) {
	i32_array_t *a = i32_array_create(from->length);
	for (uint32_t i = 0; i < from->length; ++i) {
		a->buffer[i] = from->buffer[i];
	}
	return a;
}

i32_array_t *i32_array_create_from_raw(int32_t *raw, uint32_t length) {
	i32_array_t *a = i32_array_create(length);
	for (uint32_t i = 0; i < length; ++i) {
		a->buffer[i] = raw[i];
	}
	return a;
}

u16_array_t *u16_array_create(uint32_t length) {
	u16_array_t *a = gc_alloc(sizeof(u16_array_t));
	if (length > 0) {
		u16_array_resize(a, length);
		a->length = length;
	}
	return a;
}

u16_array_t *u16_array_create_from_raw(uint16_t *raw, uint32_t length) {
	u16_array_t *a = u16_array_create(length);
	for (uint32_t i = 0; i < length; ++i) {
		a->buffer[i] = raw[i];
	}
	return a;
}

i16_array_t *i16_array_create(uint32_t length) {
	i16_array_t *a = gc_alloc(sizeof(i16_array_t));
	if (length > 0) {
		i16_array_resize(a, length);
		a->length = length;
	}
	return a;
}

i16_array_t *i16_array_create_from_array(i16_array_t *from) {
	i16_array_t *a = i16_array_create(from->length);
	for (uint32_t i = 0; i < from->length; ++i) {
		a->buffer[i] = from->buffer[i];
	}
	return a;
}

i16_array_t *i16_array_create_from_raw(int16_t *raw, uint32_t length) {
	i16_array_t *a = i16_array_create(length);
	for (uint32_t i = 0; i < length; ++i) {
		a->buffer[i] = raw[i];
	}
	return a;
}

u8_array_t *u8_array_create(uint32_t length) {
	u8_array_t *a = gc_alloc(sizeof(u8_array_t));
	if (length > 0) {
		u8_array_resize(a, length);
		a->length = length;
	}
	return a;
}

u8_array_t *u8_array_create_from_array(u8_array_t *from) {
	u8_array_t *a = u8_array_create(from->length);
	for (uint32_t i = 0; i < from->length; ++i) {
		a->buffer[i] = from->buffer[i];
	}
	return a;
}

u8_array_t *u8_array_create_from_raw(uint8_t *raw, uint32_t length) {
	u8_array_t *a = u8_array_create(length);
	for (uint32_t i = 0; i < length; ++i) {
		a->buffer[i] = raw[i];
	}
	return a;
}

u8_array_t *u8_array_create_from_string(char *s) {
	uint32_t    length = (uint32_t)strlen(s);
	u8_array_t *a      = u8_array_create(length + 1);
	memcpy(a->buffer, s, length);
	a->buffer[length] = '\0';
	return a;
}

char *u8_array_to_string(u8_array_t *a) {
	char *r = gc_alloc(a->length + 1);
	memcpy(r, a->buffer, a->length);
	return r;
}

i8_array_t *i8_array_create(uint32_t length) {
	i8_array_t *a = gc_alloc(sizeof(i8_array_t));
	if (length > 0) {
		i8_array_resize(a, length);
		a->length = length;
	}
	return a;
}

i8_array_t *i8_array_create_from_raw(int8_t *raw, uint32_t length) {
	i8_array_t *a = i8_array_create(length);
	for (uint32_t i = 0; i < length; ++i) {
		a->buffer[i] = raw[i];
	}
	return a;
}

any_array_t *any_array_create(uint32_t length) {
	any_array_t *a = gc_alloc(sizeof(any_array_t));
	if (length > 0) {
		any_array_resize(a, length);
		a->length = length;
	}
	return a;
}

any_array_t *any_array_create_from_raw(void **raw, uint32_t length) {
	any_array_t *a = any_array_create(length);
	for (uint32_t i = 0; i < length; ++i) {
		a->buffer[i] = raw[i];
	}
	return a;
}

string_array_t *string_array_create(uint32_t length) {
	string_array_t *a = gc_alloc(sizeof(string_array_t));
	if (length > 0) {
		string_array_resize(a, length);
		a->length = length;
	}
	return a;
}

uint16_t float_to_half_fast(float value) {
	union {
		float    f;
		uint32_t u;
	} v           = {value};
	uint32_t sign = (v.u >> 16) & 0x8000;
	v.u &= 0x7FFFFFFF;
	if (v.u >= 0x47800000)
		return sign | 0x7C00;
	if (v.u < 0x38800000)
		return sign;
	return sign | ((v.u - 0x38000000) >> 13);
}

uint8_t half_to_u8_fast(uint16_t h) {
	uint32_t exp      = (h >> 10) & 0x1F;
	uint32_t mantissa = h & 0x03FF;
	if (exp == 0)
		return 0;
	if (exp >= 15)
		return 255;
	uint32_t val   = mantissa | 0x0400;
	int      shift = 25 - exp;
	uint32_t res   = (val * 255) >> shift;
	return (uint8_t)res;
}

buffer_t *buffer_half_to_u8(buffer_t *buffer) {
	for (int32_t i = 0; i < buffer->length / 2.0; ++i) {
		buffer->buffer[i] = half_to_u8_fast(buffer_get_u16(buffer, i * 2));
	}
	return buffer;
}

buffer_t *buffer_bgra_swap(buffer_t *buffer) {
	for (uint32_t i = 0; i < buffer->length / 4; ++i) {
		uint8_t r                 = buffer->buffer[i * 4];
		buffer->buffer[i * 4]     = buffer->buffer[i * 4 + 2];
		buffer->buffer[i * 4 + 2] = r;
	}
	return buffer;
}

buffer_t *buffer_bgra64_swap(buffer_t *buffer) {
	for (uint32_t i = 0; i < buffer->length / 8; ++i) {
		uint8_t r_low             = buffer->buffer[i * 8 + 4];
		uint8_t r_high            = buffer->buffer[i * 8 + 5];
		buffer->buffer[i * 8 + 4] = buffer->buffer[i * 8 + 0];
		buffer->buffer[i * 8 + 5] = buffer->buffer[i * 8 + 1];
		buffer->buffer[i * 8]     = r_low;
		buffer->buffer[i * 8 + 1] = r_high;
	}
	return buffer;
}
