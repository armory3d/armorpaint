
#include "iron_json.h"
#include "iron_armpack.h"
#include "iron_string.h"
#include <jsmn.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void *gc_alloc(size_t size);

#ifdef IRON_WASM
#define PTR_SIZE 4
#else
#define PTR_SIZE 8
#endif

static char      *source;
static jsmntok_t *tokens;
static int        num_tokens;
static uint32_t   ti; // token index
static uint8_t   *decoded;
static uint32_t   wi; // write index
static uint32_t   bottom;
static uint32_t   array_count;

static inline uint64_t pad(uint32_t di, int n) {
	return (n - (di % n)) % n;
}

static void store_u8(uint8_t u8) {
	*(uint8_t *)(decoded + wi) = u8;
	wi += 1;
}

static void store_i32(int32_t i32) {
	// TODO: signed overflow is UB
	// if (i32 > INT32_MAX)
	// 	i32 = (int32_t)(i32 - INT32_MAX - 1) - INT32_MAX - 1;
	wi += pad(wi, 4);
	*(int32_t *)(decoded + wi) = i32;
	wi += 4;
}

static void store_u32(uint32_t u32) {
	wi += pad(wi, 4);
	*(uint32_t *)(decoded + wi) = u32;
	wi += 4;
}

static void store_f32(float f32) {
	wi += pad(wi, 4);
	*(float *)(decoded + wi) = f32;
	wi += 4;
}

static void store_ptr(uint32_t ptr) {
	wi += pad(wi, PTR_SIZE);
	*(uintptr_t *)(decoded + wi) = (uintptr_t)decoded + (uintptr_t)ptr;
	wi += PTR_SIZE;
}

static void store_ptr_abs(void *ptr) {
	wi += pad(wi, PTR_SIZE);
	*(uintptr_t *)(decoded + wi) = (uintptr_t)ptr;
	wi += PTR_SIZE;
}

static void store_string_bytes(char *str, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		store_u8(str[i]);
	}
	store_u8('\0');
}

static bool is_key(char *s, jsmntok_t *t) {
	return t->type == JSMN_STRING && s[t->end + 1] == ':';
}

static jsmntok_t get_token() {
	jsmntok_t t = tokens[ti];
	while (is_key(source, &t)) {
		ti++;
		t = tokens[ti];
	}
	return t;
}

static int traverse(uint32_t wi) {
	jsmntok_t t = get_token();
	if (t.type == JSMN_OBJECT) {
		ti++;
		uint32_t size = 0;
		for (uint32_t i = 0; i < t.size; ++i) {
			size += traverse(wi + size);
		}
		return pad(wi, PTR_SIZE) + size;
	}
	else if (t.type == JSMN_PRIMITIVE) {
		ti++;
		if (source[t.start] == 't' || source[t.start] == 'f') { // bool
			return 1;
		}
		else if (source[t.start] == 'n') { // null
			return pad(wi, PTR_SIZE) + PTR_SIZE;
		}
		else { // number
			return pad(wi, 4) + 4;
		}
	}
	else if (t.type == JSMN_ARRAY) {
		ti++;
		for (uint32_t i = 0; i < t.size; ++i) {
			traverse(0);
		}
		return pad(wi, PTR_SIZE) + PTR_SIZE;
	}
	else if (t.type == JSMN_STRING) {
		ti++;
		return pad(wi, PTR_SIZE) + PTR_SIZE;
	}

	return 0;
}

static int token_size() {
	uint32_t _ti = ti;
	uint32_t len = traverse(0);
	ti           = _ti;
	return len;
}

static bool has_dot(char *str, uint32_t len) {
	for (uint32_t i = 0; i < len; ++i) {
		if (str[i] == '.' || str[i] == ',') {
			return true;
		}
	}
	return false;
}

static void token_write() {
	jsmntok_t t = get_token();

	if (t.type == JSMN_OBJECT) {
		// TODO: Object containing another object
		// Write object contents
		uint32_t size = token_size();
		size += pad(size, PTR_SIZE);
		bottom += size * array_count;
		ti++;
		for (uint32_t i = 0; i < t.size; ++i) {
			token_write();
		}
	}
	else if (t.type == JSMN_PRIMITIVE) {
		ti++;
		if (source[t.start] == 't' || source[t.start] == 'f') { // bool
			store_u8(source[t.start] == 't' ? 1 : 0);
		}
		else if (source[t.start] == 'n') { // null
			store_ptr_abs(NULL);
		}
		else {
			// Replace comma with dot for locale-independent parsing
			char *num_str = string_copy(source + t.start);
			for (int i = 0; i < (t.end - t.start); i++) {
				if (num_str[i] == ',') num_str[i] = '.';
			}
			has_dot(source + t.start, t.end - t.start) ? store_f32(strtof(num_str, NULL)) :
#ifdef _WIN32
			                                           store_i32(_strtoi64(num_str, NULL, 10));
#else
			                                           store_i32(strtol(num_str, NULL, 10));
#endif
		}
	}
	else if (t.type == JSMN_ARRAY) {
		ti++;
		store_ptr(bottom);

		uint32_t _wi = wi;
		wi           = bottom;
		store_ptr(bottom + PTR_SIZE + 4 + 4); // Pointer to buffer contents
		store_u32(t.size);                    // Element count
		store_u32(0);                         // Capacity = 0 -> do not free on first realloc
		bottom = wi;

		if (t.size == 0) {
			wi = _wi;
			return;
		}

		uint32_t count = t.size;
		array_count    = count;
		t              = get_token();

		if (t.type == JSMN_OBJECT) {
			// Struct pointers
			uint32_t size = token_size();
			size += pad(size, PTR_SIZE);

			for (uint32_t i = 0; i < count; ++i) {
				store_ptr(bottom + count * PTR_SIZE + i * size);
			}

			// Struct contents
			bottom = pad(wi, PTR_SIZE) + wi;
			for (uint32_t i = 0; i < count; ++i) {
				wi = pad(wi, PTR_SIZE) + wi;
				token_write();
			}
		}
		else if (t.type == JSMN_STRING) {
			// String pointers
			uint32_t _ti            = ti;
			uint32_t strings_length = 0;
			for (uint32_t i = 0; i < count; ++i) {
				store_ptr(bottom + count * PTR_SIZE + strings_length);
				uint32_t length = t.end - t.start; // String length
				strings_length += length;
				strings_length += 1; // '\0'
				ti++;
				t = get_token();
			}
			ti = _ti;
			t  = get_token();

			// String bytes
			for (uint32_t i = 0; i < count; ++i) {
				store_string_bytes(source + t.start, t.end - t.start);
				ti++;
				t = get_token();
			}
			bottom = pad(wi, PTR_SIZE) + wi;
		}
		else {
			// Array contents
			for (uint32_t i = 0; i < count; ++i) {
				token_write();
			}
			bottom = pad(wi, PTR_SIZE) + wi;
		}

		wi          = _wi;
		array_count = 1;
	}
	else if (t.type == JSMN_STRING) {
		ti++;
		store_ptr(bottom);

		uint32_t _wi = wi;
		wi           = bottom;
		store_string_bytes(source + t.start, t.end - t.start);
		bottom = pad(wi, PTR_SIZE) + wi;
		wi     = _wi;
	}
}

static void load_tokens(char *s) {
	jsmn_parser parser;
	jsmn_init(&parser);
	num_tokens = jsmn_parse(&parser, s, strlen(s), NULL, 0);

	tokens = malloc(sizeof(jsmntok_t) * num_tokens);
	jsmn_init(&parser);
	jsmn_parse(&parser, s, strlen(s), tokens, num_tokens);

	source = s;
	ti     = 0;
}

void *json_parse(char *s) {
	load_tokens(s);

	uint32_t out_size = strlen(s) * 2;
	decoded           = gc_alloc(out_size);
	wi                = 0;
	bottom            = 0;
	array_count       = 1;
	token_write();

	free(tokens);
	return decoded;
}

static void token_write_to_map(any_map_t *m) {
	jsmntok_t t = get_token();

	if (t.type == JSMN_OBJECT) {
		// TODO: Object containing another object
		ti++;
		for (uint32_t i = 0; i < t.size; ++i) {
			token_write_to_map(m);
		}
	}
	else if (t.type == JSMN_PRIMITIVE) {
		jsmntok_t tkey = tokens[ti - 1];
		ti++;
		any_map_set(m, substring(source, tkey.start, tkey.end), substring(source, t.start, t.end));
	}
	else if (t.type == JSMN_ARRAY) {
		ti++;
	}
	else if (t.type == JSMN_STRING) {
		jsmntok_t tkey = tokens[ti - 1];
		ti++;
		any_map_set(m, substring(source, tkey.start, tkey.end), substring(source, t.start, t.end));
	}
}

any_map_t *json_parse_to_map(char *s) {
	load_tokens(s);

	any_map_t *m = any_map_create();
	token_write_to_map(m);

	free(tokens);
	return m;
}

static char *encoded;
static int   keys;
static int   array_nest = -1;
static int   array_length[16];

void json_encode_begin() {
	encoded = "{";
	keys    = 0;
}

char *json_encode_end() {
	encoded = string("%s}", encoded);
	return encoded;
}

void json_encode_key(char *k) {
	if (keys > 0) {
		encoded = string("%s,", encoded);
	}
	encoded = string("%s\"%s\":", encoded, k);
	keys++;
}

void json_encode_null(char *k) {
	json_encode_key(k);
	encoded = string("%snull", encoded);
}

void json_encode_string_value(char *v) {
	encoded = string("%s\"%s\"", encoded, v);
}

void json_encode_string(char *k, char *v) {
	json_encode_key(k);
	json_encode_string_value(v);
}

void json_encode_string_array(char *k, string_array_t *a) {
	if (a == NULL) {
		json_encode_null(k);
		return;
	}
	json_encode_begin_array(k);
	for (uint32_t i = 0; i < a->length; ++i) {
		if (i > 0) {
			encoded = string("%s,", encoded);
		}
		json_encode_string_value(a->buffer[i]);
	}
	json_encode_end_array();
}

void json_encode_f32(char *k, float f) {
	json_encode_key(k);
	encoded = string("%s%s", encoded, f32_to_string_with_zeros(f));
}

void json_encode_i32(char *k, int i) {
	json_encode_key(k);
	encoded = string("%s%s", encoded, i32_to_string(i));
}

void json_encode_f32_array(char *k, f32_array_t *a) {
	if (a == NULL) {
		json_encode_null(k);
		return;
	}
	json_encode_begin_array(k);
	for (uint32_t i = 0; i < a->length; ++i) {
		if (i > 0) {
			encoded = string("%s,", encoded);
		}
		encoded = string("%s%s", encoded, f32_to_string_with_zeros(a->buffer[i]));
	}
	json_encode_end_array();
}

void json_encode_i32_array(char *k, i32_array_t *a) {
	if (a == NULL) {
		json_encode_null(k);
		return;
	}
	json_encode_begin_array(k);
	for (uint32_t i = 0; i < a->length; ++i) {
		if (i > 0) {
			encoded = string("%s,", encoded);
		}
		encoded = string("%s%s", encoded, i32_to_string(a->buffer[i]));
	}
	json_encode_end_array();
}

void json_encode_bool(char *k, bool b) {
	json_encode_key(k);
	encoded = string("%s%s", encoded, b ? "true" : "false");
}

void json_encode_begin_array(char *k) {
	array_nest++;
	array_length[array_nest] = 0;
	json_encode_key(k);
	encoded = string("%s[", encoded);
}

void json_encode_end_array() {
	array_nest--;
	encoded = string("%s]", encoded);
}

void json_encode_begin_object() {
	if (array_nest > -1) {
		if (array_length[array_nest] > 0) {
			encoded = string("%s,", encoded);
		}
		array_length[array_nest]++;
	}
	keys    = 0;
	encoded = string("%s{", encoded);
}

void json_encode_end_object() {
	encoded = string("%s}", encoded);
}

void json_encode_map(any_map_t *m) {
	any_array_t *keys = map_keys(m);
	for (uint32_t i = 0; i < keys->length; i++) {
		json_encode_string(keys->buffer[i], any_map_get(m, keys->buffer[i]));
	}
}

static char      *jenc_src;
static jsmntok_t *jenc_tokens;
static int        jenc_ti;
static uint8_t    jenc_forced_array_type;

static bool jenc_has_dot(int start, int end) {
	for (int i = start; i < end; i++) {
		if (jenc_src[i] == '.' || jenc_src[i] == ',')
			return true;
	}
	return false;
}

static int jenc_skip(int ti) {
	jsmntok_t t = jenc_tokens[ti++];
	if (t.type == JSMN_OBJECT) {
		for (int i = 0; i < t.size; i++) {
			ti = jenc_skip(ti); // key
			ti = jenc_skip(ti); // value
		}
	}
	else if (t.type == JSMN_ARRAY) {
		for (int i = 0; i < t.size; i++) {
			ti = jenc_skip(ti);
		}
	}
	return ti;
}

static uint8_t jenc_elem_type(int ti) {
	jsmntok_t t = jenc_tokens[ti];
	if (t.type == JSMN_STRING)
		return 0xdb;
	if (t.type == JSMN_OBJECT)
		return 0xdf;
	if (t.type == JSMN_ARRAY)
		return 0xdd;
	char c = jenc_src[t.start];
	if (c == 't' || c == 'f')
		return 0xc2;
	if (c == 'n')
		return 0xc0;
	if (jenc_has_dot(t.start, t.end))
		return 0xca;
	return 0xd2;
}

static uint8_t jenc_array_type(int count) {
	if (count == 0)
		return 0x0;
	int     ti         = jenc_ti;
	uint8_t first_type = jenc_elem_type(ti);
	if (first_type != 0xca && first_type != 0xd2 && first_type != 0xdb) {
		return 0x0; // non-scalar types -> dynamic
	}
	for (int i = 0; i < count; i++) {
		if (jenc_elem_type(ti) != first_type)
			return 0x0;
		ti = jenc_skip(ti);
	}
	return first_type;
}

static void jenc_write_string(int start, int len) {
	armpack_write_u8(0xdb);
	armpack_write_u32(len);
	for (int i = 0; i < len; i++) {
		armpack_write_u8(jenc_src[start + i]);
	}
}

static void jenc_value();

static void jenc_object(int count) {
	armpack_write_u8(0xdf);
	armpack_write_i32(count);
	for (int i = 0; i < count; i++) {
		jsmntok_t key     = jenc_tokens[jenc_ti++];
		int       key_len = key.end - key.start;
		// Check for [type] suffix in key name
		uint8_t forced_type = 0;
		int     actual_len  = key_len;
		if (key_len >= 4 && jenc_src[key.end - 1] == ']') {
			const char *ks = jenc_src + key.start;
			if (key_len >= 5 && strncmp(ks + key_len - 5, "[f32]", 5) == 0) {
				forced_type = 0xca;
				actual_len  = key_len - 5;
			}
			else if (key_len >= 5 && strncmp(ks + key_len - 5, "[i32]", 5) == 0) {
				forced_type = 0xd2;
				actual_len  = key_len - 5;
			}
			else if (key_len >= 5 && strncmp(ks + key_len - 5, "[i16]", 5) == 0) {
				forced_type = 0xd1;
				actual_len  = key_len - 5;
			}
			else if (key_len >= 4 && strncmp(ks + key_len - 4, "[u8]", 4) == 0) {
				forced_type = 0xc4;
				actual_len  = key_len - 4;
			}
		}
		jenc_write_string(key.start, actual_len);
		jenc_forced_array_type = forced_type;
		jenc_value();
		jenc_forced_array_type = 0;
	}
}

static void jenc_array(int count) {
	armpack_write_u8(0xdd);
	armpack_write_u32(count);
	if (count == 0)
		return;

	uint8_t forced         = jenc_forced_array_type;
	jenc_forced_array_type = 0;
	uint8_t elem_type      = forced != 0 ? forced : jenc_array_type(count);

	if (elem_type == 0xca) { // typed f32
		armpack_write_u8(0xca);
		for (int i = 0; i < count; i++) {
			jsmntok_t t = jenc_tokens[jenc_ti++];
			// Replace comma with dot for locale-independent parsing
			char *num_str = string_copy(jenc_src + t.start);
			for (int j = 0; j < (t.end - t.start); j++) {
				if (num_str[j] == ',') num_str[j] = '.';
			}
			armpack_write_f32(strtof(num_str, NULL));
		}
	}
	else if (elem_type == 0xd2) { // typed i32
		armpack_write_u8(0xd2);
		for (int i = 0; i < count; i++) {
			jsmntok_t t = jenc_tokens[jenc_ti++];
#ifdef _WIN32
			armpack_write_i32((int32_t)_strtoi64(jenc_src + t.start, NULL, 10));
#else
			armpack_write_i32((int32_t)strtol(jenc_src + t.start, NULL, 10));
#endif
		}
	}
	else if (elem_type == 0xd1) { // typed i16
		armpack_write_u8(0xd1);
		for (int i = 0; i < count; i++) {
			jsmntok_t t = jenc_tokens[jenc_ti++];
#ifdef _WIN32
			armpack_write_i16((int16_t)_strtoi64(jenc_src + t.start, NULL, 10));
#else
			armpack_write_i16((int16_t)strtol(jenc_src + t.start, NULL, 10));
#endif
		}
	}
	else if (elem_type == 0xc4) { // typed u8
		armpack_write_u8(0xc4);
		for (int i = 0; i < count; i++) {
			jsmntok_t t = jenc_tokens[jenc_ti++];
#ifdef _WIN32
			armpack_write_u8((uint8_t)_strtoi64(jenc_src + t.start, NULL, 10));
#else
			armpack_write_u8((uint8_t)strtol(jenc_src + t.start, NULL, 10));
#endif
		}
	}
	else { // dynamic
		for (int i = 0; i < count; i++) {
			jenc_value();
		}
	}
}

static void jenc_value() {
	jsmntok_t t = jenc_tokens[jenc_ti++];
	if (t.type == JSMN_OBJECT) {
		jenc_object(t.size);
		return;
	}
	if (t.type == JSMN_ARRAY) {
		jenc_array(t.size);
		return;
	}
	if (t.type == JSMN_STRING) {
		jenc_write_string(t.start, t.end - t.start);
		return;
	}
	// JSMN_PRIMITIVE
	char c = jenc_src[t.start];
	if (c == 't') {
		armpack_write_u8(0xc3); // true
	}
	else if (c == 'f') {
		armpack_write_u8(0xc2); // false
	}
	else if (c == 'n') {
		armpack_write_u8(0xc0); // null
	}
	else if (jenc_has_dot(t.start, t.end)) {
		armpack_write_u8(0xca);
		// Replace comma with dot for locale-independent parsing
		char *num_str = string_copy(jenc_src + t.start);
		for (int j = 0; j < (t.end - t.start); j++) {
			if (num_str[j] == ',') num_str[j] = '.';
		}
		armpack_write_f32(strtof(num_str, NULL));
	}
	else {
		armpack_write_u8(0xd2);
#ifdef _WIN32
		armpack_write_i32((int32_t)_strtoi64(jenc_src + t.start, NULL, 10));
#else
		armpack_write_i32((int32_t)strtol(jenc_src + t.start, NULL, 10));
#endif
	}
}

buffer_t *json_encode_to_armpack(char *json) {
	jsmn_parser parser;
	jsmn_init(&parser);
	int n = jsmn_parse(&parser, json, strlen(json), NULL, 0);
	if (n <= 0)
		return NULL;

	jenc_tokens = malloc(sizeof(jsmntok_t) * n);
	jsmn_init(&parser);
	jsmn_parse(&parser, json, strlen(json), jenc_tokens, n);

	jenc_src = json;
	jenc_ti  = 0;

	int      max_size = strlen(json) * 3 + 64;
	uint8_t *buf      = malloc(max_size);
	armpack_encode_start(buf);
	jenc_value();
	int size = armpack_encode_end();

	free(jenc_tokens);

	buffer_t *b = buffer_create(size);
	memcpy(b->buffer, buf, size);
	free(buf);
	return b;
}
