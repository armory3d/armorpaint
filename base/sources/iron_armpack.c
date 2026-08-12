
#include "iron_armpack.h"

#include "iron_gc.h"
#include "iron_json.h"
#include "iron_string.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef IRON_WASM
#define PTR_SIZE 4
#else
#define PTR_SIZE 8
#endif
static uint32_t di;     // Decoded index
static uint32_t ei;     // Encoded index
static uint32_t bottom; // Decoded bottom
static uint8_t *decoded;
static uint8_t *encoded;
static uint32_t capacity;
static uint32_t array_count;
static uint32_t str_len;
static void     read_store();

static inline uint64_t pad(int di, int n) {
	return (n - (di % n)) % n;
}

static void store_u8(uint8_t u8) {
	*(uint8_t *)(decoded + di) = u8;
	di += 1;
}

/*static void store_i16(int16_t i16) {
    di += pad(di, 2);
    *(int16_t *)(decoded + di) = i16;
    di += 2;
}*/

static void store_u32(uint32_t u32) {
	di += pad(di, 4);
	*(uint32_t *)(decoded + di) = u32;
	di += 4;
}

static void store_i32(int32_t i32) {
	di += pad(di, 4);
	*(int32_t *)(decoded + di) = i32;
	di += 4;
}

static void store_f32(float f32) {
	di += pad(di, 4);
	*(float *)(decoded + di) = f32;
	di += 4;
}

static void store_ptr(uint32_t ptr) {
	di += pad(di, PTR_SIZE);
	*(uintptr_t *)(decoded + di) = (uintptr_t)decoded + (uintptr_t)ptr;
	di += PTR_SIZE;
}

static void store_ptr_abs(void *ptr) {
	di += pad(di, PTR_SIZE);
	*(uintptr_t *)(decoded + di) = (uintptr_t)ptr;
	di += PTR_SIZE;
}

static void store_string_bytes(char *str) {
	for (int i = 0; i < str_len; ++i) {
		store_u8(str[i]);
	}
	store_u8('\0');
}

static void store_string(char *str) {
	// Put string at the bottom and store a pointer to it
	store_ptr(bottom);
	uint32_t _di = di;
	di           = bottom;
	store_string_bytes(str);
	bottom = pad(di, PTR_SIZE) + di;
	di     = _di;
}

static uint8_t read_u8() {
	uint8_t u8 = *(uint8_t *)(encoded + ei);
	ei += 1;
	return u8;
}

static int16_t read_i16() {
	int16_t i16 = *(int16_t *)(encoded + ei);
	ei += 2;
	return i16;
}

static int32_t read_i32() {
	int32_t i32 = *(int32_t *)(encoded + ei);
	ei += 4;
	return i32;
}

static uint32_t read_u32() {
	uint32_t u32 = *(uint32_t *)(encoded + ei);
	ei += 4;
	return u32;
}

static float read_f32() {
	float f32 = *(float *)(encoded + ei);
	ei += 4;
	return f32;
}

static char *read_string() {
	str_len   = read_u32();
	char *str = (char *)(encoded + ei);
	ei += str_len;
	return str;
}

static uint32_t traverse(int di, bool count_arrays) {
	uint8_t flag = read_u8();
	switch (flag) {
	case 0xc0: // NULL
		return pad(di, PTR_SIZE) + PTR_SIZE;
	case 0xc2: // false
		return 1;
	case 0xc3: // true
		return 1;
	case 0xca: // f32
		ei += 4;
		return pad(di, 4) + 4;
	case 0xd2: // i32
		ei += 4;
		return pad(di, 4) + 4;
	case 0xdf: { // map
		uint32_t len   = 0;
		int      count = read_i32();
		for (int i = 0; i < count; ++i) {
			read_u8();                               // 0xdb string
			read_string();                           // key
			len += traverse(di + len, count_arrays); // value
		}
		len += pad(di + len, PTR_SIZE) + PTR_SIZE; // void *_
		return len;
	}
	case 0xdd: { // array
		uint32_t len   = 0;
		uint32_t count = read_u32();
		uint8_t  flag2 = count > 0 ? read_u8() : 0x0;
		switch (flag2) {
		case 0x0: // Empty array
			break;
		case 0xca: // Typed f32
			ei += 4 * count;
			if (4 * count <= 4096)
				len += 4 * count;
			break;
		case 0xd2: // Typed i32
			ei += 4 * count;
			if (4 * count <= 4096)
				len += 4 * count;
			break;
		case 0xd1: // Typed i16
			ei += 2 * count;
			if (2 * count <= 4096)
				len += 2 * count;
			break;
		case 0xc4: // Typed u8
			ei += count;
			if (count <= 4096)
				len += count;
			break;
		default:     // Dynamic (type (array / map / string) - value)
			ei -= 1; // Undo flag2 read
			for (uint32_t j = 0; j < count; ++j) {
				len += traverse(0, count_arrays);
			}
		}
		len += PTR_SIZE + 4 + 4 + 16; // buffer ptr, length, capacity + align
		if (!count_arrays) {
			len = 0;
		}
		// ptr (to array_t)
		len += pad(di, PTR_SIZE) + PTR_SIZE;
		return len;
	}
	case 0xdb: {                   // string
		uint32_t len = read_u32(); // string_length
		ei += len;
		len += 1; // '\0'
		if (!count_arrays) {
			len = 0;
		}
		len += pad(di, PTR_SIZE) + PTR_SIZE;
		return len;
	}
	default:
		return 0;
	}
}

static uint32_t get_struct_length() {
	uint32_t _ei = ei;
	uint32_t len = traverse(0, false);
	ei           = _ei;
	return len;
}

static void read_store_map(uint32_t count) {
	ei -= 5; // u8 map, i32 count
	uint32_t size = get_struct_length();
	size += pad(size, PTR_SIZE);
	bottom += size * array_count;
	array_count = 0;
	ei += 5;

	for (uint32_t i = 0; i < count; ++i) {
		read_u8();     // 0xdb string
		read_string(); // key
		read_store();  // value
	}
	di += pad(di, PTR_SIZE);
	di += PTR_SIZE; // void *_ for runtime storage
}

static bool is_typed_array(uint8_t flag) {
	return flag == 0xca || flag == 0xd2 || flag == 0xd1 || flag == 0xc4 || flag == 0xc2 || flag == 0xc3;
}

static uint8_t flag_to_byte_size(uint8_t flag) {
	if (flag == 0xca)
		return 4; // f32
	if (flag == 0xd2)
		return 4; // i32
	if (flag == 0xd1)
		return 2; // i16
	if (flag == 0xc4)
		return 1; // u8
	if (flag == 0xc2)
		return 1; // u8 (true)
	if (flag == 0xc3)
		return 1; // u8 (false)
	return 0;
}

static void store_typed_array(uint8_t flag, uint32_t count) {
	uint32_t size = flag_to_byte_size(flag) * count;
	if (size > 4096) {
		void *data = gc_alloc(size);
		memcpy(data, encoded + ei, size);
		gc_leaf(data);
		*(uintptr_t *)(decoded + di - 4 - 4 - PTR_SIZE) = (uintptr_t)data; // Set buffer ptr
		*(uint32_t *)(decoded + di - 4)                 = count;           // Set capacity
	}
	else {
		memcpy(decoded + di, encoded + ei, size);
		di += size;
	}
	ei += size;
}

static void read_store_array(uint32_t count) { // Store in any/i32/../_array_t format
	// Store pointer to array_t struct
	// Put array contents at the bottom
	// - pointer to array_t buffer
	// - element count
	// - capacity (same as count)
	// - buffer
	store_ptr(bottom);

	uint32_t _di = di;
	di           = bottom;

	if (count > 0) {
		store_ptr(di + PTR_SIZE + 4 + 4); // Pointer to buffer contents
	}
	else {
		store_ptr_abs(NULL);
	}
	store_u32(count); // Element count
	store_u32(0);     // Capacity = 0 -> do not free on first realloc
	bottom = di;

	if (count == 0) {
		di = _di;
		return;
	}

	uint8_t flag = read_u8();
	if (is_typed_array(flag)) {
		if (flag == 0xc2 || flag == 0xc3) {
			ei--; // Bool array
		}
		store_typed_array(flag, count);
		bottom = pad(di, PTR_SIZE) + di;
	}
	// Dynamic (type (array / map / string) - value)
	else {
		ei -= 1; // Undo flag read

		// Strings
		if (flag == 0xdb) {
			// String pointers
			uint32_t _ei            = ei;
			uint32_t strings_length = 0;
			for (uint32_t i = 0; i < count; ++i) {
				store_ptr(bottom + count * PTR_SIZE + strings_length);
				if (i < count - 1) {
					ei += 1;                      // String flag
					uint32_t length = read_u32(); // String length
					ei += length;
					strings_length += length;
					strings_length += 1; // '\0'
				}
			}
			ei = _ei;

			// String bytes
			for (uint32_t i = 0; i < count; ++i) {
				ei += 1; // String flag
				store_string_bytes(read_string());
			}
			bottom = pad(di, PTR_SIZE) + di;
		}
		// Arrays
		else if (flag == 0xdd) {
			uint32_t outer_start = di;
			bottom               = outer_start + count * PTR_SIZE;
			for (uint32_t i = 0; i < count; ++i) {
				di = outer_start + i * PTR_SIZE;
				/*uint8_t flag =*/read_u8();
				read_store_array(read_u32());
			}
		}
		// Structs
		else {
			uint32_t size = get_struct_length();
			size += pad(size, PTR_SIZE);

			// Struct pointers
			for (uint32_t i = 0; i < count; ++i) {
				store_ptr(bottom + count * PTR_SIZE + i * size);
			}

			// Struct contents
			bottom = pad(di, PTR_SIZE) + di;

			array_count = count;

			for (uint32_t i = 0; i < count; ++i) {
				di = pad(di, PTR_SIZE) + di;
				/*uint8_t flag =*/read_u8();
				read_store_map(read_u32());
			}
		}
	}

	di = _di;
}

static void read_store() {
	uint8_t flag = read_u8();
	switch (flag) {
	case 0xc0:
		store_ptr_abs(NULL);
		break;
	case 0xc2:
		store_u8(false);
		break;
	case 0xc3:
		store_u8(true);
		break;
	case 0xca:
		store_f32(read_f32());
		break;
	case 0xd2:
		store_i32(read_i32());
		break;
	case 0xdf: {
		array_count = 1;
		read_store_map(read_i32());
		break;
	}
	case 0xdd:
		read_store_array(read_i32());
		break;
	case 0xdb:
		store_string(read_string());
		break;
	}
}

static void reset() {
	di     = 0;
	ei     = 0;
	bottom = 0;
}

void *armpack_decode(buffer_t *b) {
	reset();
	encoded  = b->buffer;
	capacity = traverse(0, true);
	reset();
	decoded = gc_alloc(capacity);
	read_store();
	return decoded;
}

void armpack_encode_start(void *_encoded) {
	encoded = _encoded;
	ei      = 0;
}

int armpack_encode_end() {
	return ei;
}

void armpack_write_u8(uint8_t i) {
	*(uint8_t *)(encoded + ei) = i;
	ei += 1;
}

void armpack_write_i16(int16_t i) {
	*(int16_t *)(encoded + ei) = i;
	ei += 2;
}

void armpack_write_u32(uint32_t i) {
	*(uint32_t *)(encoded + ei) = i;
	ei += 4;
}

void armpack_write_i32(int32_t i) {
	*(int32_t *)(encoded + ei) = i;
	ei += 4;
}

void armpack_write_f32(float i) {
	*(float *)(encoded + ei) = i;
	ei += 4;
}

void armpack_encode_map(uint32_t count) {
	armpack_write_u8(0xdf);
	armpack_write_i32(count);
}

void armpack_encode_array(uint32_t count) { // array of arrays / array of maps
	armpack_write_u8(0xdd);
	armpack_write_u32(count);
}

void armpack_encode_array_f32(f32_array_t *f32a) {
	if (f32a == NULL) {
		armpack_write_u8(0xc0); // NULL
		return;
	}
	armpack_write_u8(0xdd);
	armpack_write_u32(f32a->length);
	if (f32a->length > 0) {
		armpack_write_u8(0xca);
		for (uint32_t i = 0; i < f32a->length; ++i) {
			armpack_write_f32(f32a->buffer[i]);
		}
	}
}

void armpack_encode_array_i32(i32_array_t *i32a) {
	if (i32a == NULL) {
		armpack_write_u8(0xc0); // NULL
		return;
	}
	armpack_write_u8(0xdd);
	armpack_write_u32(i32a->length);
	if (i32a->length > 0) {
		armpack_write_u8(0xd2);
		for (uint32_t i = 0; i < i32a->length; ++i) {
			armpack_write_i32(i32a->buffer[i]);
		}
	}
}

void armpack_encode_array_i16(i16_array_t *i16a) {
	if (i16a == NULL) {
		armpack_write_u8(0xc0); // NULL
		return;
	}
	armpack_write_u8(0xdd);
	armpack_write_u32(i16a->length);
	if (i16a->length > 0) {
		armpack_write_u8(0xd1);
		for (uint32_t i = 0; i < i16a->length; ++i) {
			armpack_write_i16(i16a->buffer[i]);
		}
	}
}

void armpack_encode_array_u8(u8_array_t *u8a) {
	if (u8a == NULL) {
		armpack_write_u8(0xc0); // NULL
		return;
	}
	armpack_write_u8(0xdd);
	armpack_write_u32(u8a->length);
	if (u8a->length > 0) {
		armpack_write_u8(0xc4);
		for (uint32_t i = 0; i < u8a->length; ++i) {
			armpack_write_u8(u8a->buffer[i]);
		}
	}
}

void armpack_encode_array_string(string_array_t *strings) {
	if (strings == NULL) {
		armpack_write_u8(0xc0); // NULL
		return;
	}
	armpack_write_u8(0xdd);
	armpack_write_u32(strings->length);
	for (uint32_t i = 0; i < strings->length; ++i) {
		armpack_encode_string(strings->buffer[i]);
	}
}

void armpack_encode_string(char *str) {
	if (str == NULL) {
		armpack_write_u8(0xc0); // NULL
		return;
	}
	armpack_write_u8(0xdb);
	size_t len = strlen(str);
	armpack_write_u32(len);
	for (uint32_t i = 0; i < len; ++i) {
		armpack_write_u8(str[i]);
	}
}

void armpack_encode_i32(int32_t i) {
	armpack_write_u8(0xd2);
	armpack_write_i32(i);
}

void armpack_encode_f32(float f) {
	armpack_write_u8(0xca);
	armpack_write_f32(f);
}

void armpack_encode_bool(bool b) {
	armpack_write_u8(b ? 0xc3 : 0xc2);
}

void armpack_encode_null() {
	armpack_write_u8(0xc0);
}

uint32_t armpack_size_map() {
	return 1 + 4; // u8 tag + u32 count
}

uint32_t armpack_size_array() {
	return 1 + 4; // u8 tag + u32 count
}

uint32_t armpack_size_array_f32(f32_array_t *f32a) {
	return 1 + 4 + 1 + f32a->length * 4; // u8 tag + u32 count + u8 flag + f32* contents
}

uint32_t armpack_size_array_u8(u8_array_t *u8a) {
	return 1 + 4 + 1 + u8a->length; // u8 tag + u32 count + u8 flag + u8* contents
}

uint32_t armpack_size_string(char *str) {
	return 1 + 4 + strlen(str); // u8 tag + u32 length + contents
}

uint32_t armpack_size_i32() {
	return 1 + 4; // u8 tag + i32
}

uint32_t armpack_size_f32() {
	return 1 + 4; // u8 tag + f32
}

uint32_t armpack_size_bool() {
	return 1; // u8 tag
}

static char *read_string_alloc() {
	char *s         = read_string();
	char *allocated = gc_alloc(str_len + 1);
	memcpy(allocated, s, str_len);
	allocated[str_len] = '\0';
	return allocated;
}

typedef union ptr_storage {
	void *p;
	struct {
		float f;
		int   type; // 0 - float, 1 - int
	};
	int i;
} ptr_storage_t;

any_map_t *_armpack_decode_to_map();

void *_armpack_decode_to_array() {
	uint32_t array_count = read_u32();
	if (array_count == 0) {
		return any_array_create(0);
	}
	uint8_t element_flag = read_u8();
	if (element_flag == 0xca) { // f32
		f32_array_t *array = f32_array_create(array_count);
		for (uint32_t j = 0; j < array_count; j++) {
			array->buffer[j] = read_f32();
		}
		return array;
	}
	else if (element_flag == 0xd2) { // i32
		i32_array_t *array = i32_array_create(array_count);
		for (uint32_t j = 0; j < array_count; j++) {
			array->buffer[j] = read_i32();
		}
		return array;
	}
	else if (element_flag == 0xd1) { // i16
		i16_array_t *array = i16_array_create(array_count);
		for (uint32_t j = 0; j < array_count; j++) {
			array->buffer[j] = read_i16();
		}
		return array;
	}
	else if (element_flag == 0xc4) { // u8
		u8_array_t *array = u8_array_create(array_count);
		for (uint32_t j = 0; j < array_count; j++) {
			array->buffer[j] = read_u8();
		}
		return array;
	}
	else if (element_flag == 0xdb) { // string
		ei--;
		string_array_t *array = string_array_create(array_count);
		for (uint32_t j = 0; j < array_count; j++) {
			read_u8(); // flag
			array->buffer[j] = read_string_alloc();
		}
		return array;
	}
	else if (element_flag == 0xdf) { // map
		ei--;
		any_array_t *array = any_array_create(array_count);
		for (uint32_t j = 0; j < array_count; j++) {
			read_u8(); // flag
			array->buffer[j] = _armpack_decode_to_map();
		}
		return array;
	}
	else if (element_flag == 0xdd) { // array
		ei--;
		any_array_t *nested_array = any_array_create(array_count);
		for (uint32_t j = 0; j < array_count; j++) {
			read_u8(); // flag
			nested_array->buffer[j] = _armpack_decode_to_array();
		}
		return nested_array;
	}
	return NULL;
}

any_map_t *_armpack_decode_to_map() {
	any_map_t *result = any_map_create();
	uint32_t   count  = read_u32();

	for (uint32_t i = 0; i < count; i++) {
		read_u8(); // 0xdb string
		char *key = read_string_alloc();

		uint8_t flag = read_u8();
		switch (flag) {
		case 0xc0: { // NULL
			any_map_set(result, key, NULL);
			break;
		}
		case 0xc2: { // false
			ptr_storage_t s;
			s.i    = 0;
			s.type = 1;
			any_map_set(result, key, s.p);
			break;
		}
		case 0xc3: { // true
			ptr_storage_t s;
			s.i    = 1;
			s.type = 1;
			any_map_set(result, key, s.p);
			break;
		}
		case 0xca: { // f32
			ptr_storage_t s;
			s.f    = read_f32();
			s.type = 0;
			any_map_set(result, key, s.p);
			break;
		}
		case 0xd2: { // i32
			ptr_storage_t s;
			s.i    = read_i32();
			s.type = 1;
			any_map_set(result, key, s.p);
			break;
		}
		case 0xdb: { // string
			any_map_set(result, key, read_string_alloc());
			break;
		}
		case 0xdf: { // map
			any_map_t *nested_map = _armpack_decode_to_map();
			any_map_set(result, key, nested_map);
			break;
		}
		case 0xdd: { // array
			void *array = _armpack_decode_to_array();
			any_map_set(result, key, array);
			break;
		}
		}
	}

	return result;
}

any_map_t *armpack_decode_to_map(buffer_t *b) {
	encoded = b->buffer;
	ei      = 0;
	read_u8(); // Must be 0xdf for a map
	return _armpack_decode_to_map();
}

static char *armpack_to_json_value();
static bool  json_omit_large_arrays;

#define JSON_OMIT_LARGE_ARRAYS_MAX_ELEMENTS 64

static const char *peek_typed_array_suffix() {
	if (encoded[ei] != 0xdd)
		return "";
	uint32_t saved_ei = ei;
	ei++;
	uint32_t    count  = read_u32();
	const char *suffix = "";
	if (count > 0) {
		uint8_t flag2 = read_u8();
		if (flag2 == 0xca)
			suffix = "[f32]";
		else if (flag2 == 0xd2)
			suffix = "[i32]";
		else if (flag2 == 0xd1)
			suffix = "[i16]";
		else if (flag2 == 0xc4)
			suffix = "[u8]";
	}
	ei = saved_ei;
	return suffix;
}

static char *armpack_to_json_map(uint32_t count) {
	char *result = "{";
	for (uint32_t i = 0; i < count; i++) {
		read_u8(); // 0xdb string flag
		char       *key    = read_string_alloc();
		const char *suffix = peek_typed_array_suffix();
		char       *value  = armpack_to_json_value();
		if (i > 0) {
			result = string("%s,", result);
		}
		result = string("%s\"%s%s\":%s", result, key, suffix, value);
	}
	return string("%s}", result);
}

static char *armpack_to_json_value() {
	uint8_t flag = read_u8();
	switch (flag) {
	case 0xc0:
		return "null";
	case 0xc2:
		return "false";
	case 0xc3:
		return "true";
	case 0xca:
		return f32_to_string_with_zeros(read_f32());
	case 0xd2:
		return i32_to_string(read_i32());
	case 0xdb: {
		char *s = read_string_alloc();
		return string("\"%s\"", s);
	}
	case 0xdf:
		return armpack_to_json_map(read_i32());
	case 0xdd: {
		uint32_t count = read_u32();
		if (count == 0) {
			return "[]";
		}
		uint8_t flag2 = read_u8();
		if (json_omit_large_arrays && count > JSON_OMIT_LARGE_ARRAYS_MAX_ELEMENTS) {
			uint32_t elem_size = flag2 == 0xca || flag2 == 0xd2 ? 4 : flag2 == 0xd1 ? 2 : flag2 == 0xc4 ? 1 : 0;
			if (elem_size > 0) {
				ei += count * elem_size;
				return string("\"<%d values omitted>\"", count);
			}
		}
		char *result = "[";
		if (flag2 == 0xca) { // f32
			for (uint32_t i = 0; i < count; i++) {
				if (i > 0)
					result = string("%s,", result);
				result = string("%s%s", result, f32_to_string_with_zeros(read_f32()));
			}
		}
		else if (flag2 == 0xd2) { // i32
			for (uint32_t i = 0; i < count; i++) {
				if (i > 0)
					result = string("%s,", result);
				result = string("%s%s", result, i32_to_string(read_i32()));
			}
		}
		else if (flag2 == 0xd1) { // i16
			for (uint32_t i = 0; i < count; i++) {
				if (i > 0)
					result = string("%s,", result);
				result = string("%s%s", result, i32_to_string(read_i16()));
			}
		}
		else if (flag2 == 0xc4) { // u8
			for (uint32_t i = 0; i < count; i++) {
				if (i > 0)
					result = string("%s,", result);
				result = string("%s%s", result, i32_to_string(read_u8()));
			}
		}
		else {    // dynamic (string, array, map)
			ei--; // undo flag2 read
			for (uint32_t i = 0; i < count; i++) {
				if (i > 0)
					result = string("%s,", result);
				result = string("%s%s", result, armpack_to_json_value());
			}
		}
		return string("%s]", result);
	}
	default:
		return "null";
	}
}

char *armpack_decode_to_json(buffer_t *b) {
	encoded = b->buffer;
	ei      = 0;
	read_u8(); // Must be 0xdf for a map
	return armpack_to_json_map(read_i32());
}

char *armpack_decode_to_json_omit_large_arrays(buffer_t *b) {
	encoded                = b->buffer;
	ei                     = 0;
	json_omit_large_arrays = true;
	read_u8(); // Must be 0xdf for a map
	char *result           = armpack_to_json_map(read_i32());
	json_omit_large_arrays = false;
	return result;
}

float armpack_map_get_f32(any_map_t *map, char *key) {
	ptr_storage_t ps;
	ps.p = any_map_get(map, key);
	if (ps.p == NULL) {
		return 0.0;
	}
	return ps.type == 0 ? ps.f : (float)ps.i;
}

int armpack_map_get_i32(any_map_t *map, char *key) {
	ptr_storage_t ps;
	ps.p = any_map_get(map, key);
	if (ps.p == NULL) {
		return 0;
	}
	return ps.type == 1 ? ps.i : (int)ps.f;
}

void armpack_map_set_i32(any_map_t *map, char *key, int value) {
	ptr_storage_t s;
	s.i    = value;
	s.type = 1;
	any_map_set(map, key, s.p);
}
