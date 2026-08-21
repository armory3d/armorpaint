#include "iron_string.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *string_alloc(int size) {
	char *r = calloc(1, size);
	return r;
}

#define STRING_TMP_BUFFER_SIZE (4 * 1024 * 1024)
static char  *string_tmp_buffer = NULL;
static size_t string_tmp_pos    = 0;

char *string_tmp_alloc(int size) {
	if (string_tmp_buffer == NULL) {
		string_tmp_buffer = malloc(STRING_TMP_BUFFER_SIZE);
	}
	if (string_tmp_pos + size > STRING_TMP_BUFFER_SIZE) {
		return string_alloc(size);
	}
	char *r = string_tmp_buffer + string_tmp_pos;
	string_tmp_pos += ((size_t)size + 7) & ~(size_t)7;
	memset(r, 0, size);
	return r;
}

void string_tmp_reset() {
	string_tmp_pos = 0;
}

char *string_tmp(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);

	va_list args_copy;
	va_copy(args_copy, args);
	int len = vsnprintf(NULL, 0, fmt, args_copy);
	va_end(args_copy);

	char *str = string_tmp_alloc(len + 1);
	vsnprintf(str, len + 1, fmt, args);
	va_end(args);
	return str;
}

char *string(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);

	va_list args_copy;
	va_copy(args_copy, args);
	int len = vsnprintf(NULL, 0, fmt, args_copy);
	va_end(args_copy);

	char *str = string_alloc(len + 1);
	vsnprintf(str, len + 1, fmt, args);
	va_end(args);
	return str;
}

char *string_copy(char *a) {
	if (a == NULL) {
		return NULL;
	}
	char *r = string_alloc(strlen(a) + 1);
	strcpy(r, a);
	return r;
}

int string_length(char *str) {
	return strlen(str);
}

bool string_equals(char *a, char *b) {
	if (a == NULL || b == NULL) {
		return false;
	}
	return strcmp(a, b) == 0;
}

char *i32_to_string(int32_t i) {
	int   l = snprintf(NULL, 0, "%d", i);
	char *r = string_tmp_alloc(l + 1);
	sprintf(r, "%d", i);
	return r;
}

char *i32_to_string_hex(int32_t i) {
	int   l = snprintf(NULL, 0, "%X", i);
	char *r = string_tmp_alloc(l + 1);
	sprintf(r, "%X", i);
	return r;
}

#ifdef IRON_WINDOWS
#define PERCENT_LD "%lld"
#define PERCENT_LU "%llu"
#else
#define PERCENT_LD "%ld"
#define PERCENT_LU "%lu"
#endif

char *i64_to_string(int64_t i) {
	int   l = snprintf(NULL, 0, PERCENT_LD, i);
	char *r = string_tmp_alloc(l + 1);
	sprintf(r, PERCENT_LD, i);
	return r;
}

char *u64_to_string(uint64_t i) {
	int   l = snprintf(NULL, 0, PERCENT_LU, i);
	char *r = string_tmp_alloc(l + 1);
	sprintf(r, PERCENT_LU, i);
	return r;
}

char *f32_to_string_with_zeros(float f) {
	int   l = snprintf(NULL, 0, "%f", f);
	char *r = string_tmp_alloc(l + 1);
	sprintf(r, "%f", f);
	return r;
}

char *f32_to_string(float f) {
	char *r = f32_to_string_with_zeros(f);
	string_strip_trailing_zeros(r);
	return r;
}

void string_strip_trailing_zeros(char *str) {
	int len = strlen(str);
	while (str[--len] == '0') {
		str[len] = '\0';
	}
	if (str[len] == '.') {
		str[len] = '\0';
	}
}

int32_t string_index_of_pos(char *s, char *search, int pos) {
	char *found = strstr(s + pos, search);
	if (found != NULL) {
		return found - s;
	}
	return -1;
}

int32_t string_index_of(char *s, char *search) {
	return string_index_of_pos(s, search, 0);
}

int32_t string_last_index_of(char *str, char *search) {
	char *s     = str;
	char *found = NULL;
	while (1) {
		char *p = strstr(s, search);
		if (p == NULL) {
			break;
		}
		found = p;
		s     = p + 1;
	}
	if (found != NULL) {
		return found - str;
	}
	return -1;
}

any_array_t *string_split(char *s, char *sep) {
	any_array_t *a       = calloc(1, sizeof(any_array_t));
	int          sep_len = strlen(sep);
	char        *pos     = s;
	while (true) {
		char *next = strstr(pos, sep);
		if (next == NULL) {
			any_array_push(a, string_copy(pos));
			break;
		}
		int   part_len = next - pos;
		char *part     = string_alloc(part_len + 1);
		strncpy(part, pos, part_len);
		part[part_len] = '\0';
		any_array_push(a, part);
		pos = next + sep_len;
	}
	return a;
}

void string_split_free(any_array_t *a) {
	if (a == NULL) {
		return;
	}
	for (int i = 0; i < a->length; ++i) {
		free(a->buffer[i]);
	}
	array_free(a);
	free(a);
}

char *string_array_join(any_array_t *a, char *separator) {
	int len     = 0;
	int len_sep = strlen(separator);
	for (int i = 0; i < a->length; ++i) {
		len += strlen(a->buffer[i]);
		if (i < a->length - 1) {
			len += len_sep;
		}
	}

	char *r = string_alloc(len + 1);
	for (int i = 0; i < a->length; ++i) {
		strcat(r, a->buffer[i]);
		if (i < a->length - 1) {
			strcat(r, separator);
		}
	}
	return r;
}

static char *string_replace_all_impl(char *s, char *search, char *replace, bool use_tmp) {
	size_t search_len  = strlen(search);
	size_t replace_len = strlen(replace);
	size_t buffer_size = strlen(s) + 1;
	if (replace_len > search_len) {
		char  *tmp   = s;
		size_t count = 0;
		while ((tmp = strstr(tmp, search)) != NULL) {
			count++;
			tmp += search_len;
		}
		buffer_size += count * (replace_len - search_len);
	}

	char *buffer     = use_tmp ? string_tmp_alloc(buffer_size) : string_alloc(buffer_size);
	char *buffer_pos = buffer;
	while (1) {
		char *p = strstr(s, search);
		if (p == NULL) {
			strcpy(buffer_pos, s);
			break;
		}
		memcpy(buffer_pos, s, p - s);
		buffer_pos += p - s;
		memcpy(buffer_pos, replace, replace_len);
		buffer_pos += replace_len;
		s = p + search_len;
	}
	return buffer;
}

char *string_replace_all(char *s, char *search, char *replace) {
	return string_replace_all_impl(s, search, replace, false);
}

char *string_replace_all_tmp(char *s, char *search, char *replace) {
	return string_replace_all_impl(s, search, replace, true);
}

char *substring(char *s, int32_t start, int32_t end) {
	char *buffer = string_alloc(end - start + 1);
	for (int i = 0; i < end - start; ++i) {
		buffer[i] = s[start + i];
	}
	return buffer;
}

char *string_from_char_code(int32_t c) {
	char *r = string_alloc(2);
	r[0]    = c;
	r[1]    = '\0';
	return r;
}

int32_t char_code_at(char *s, int32_t i) {
	return s[i];
}

char *char_at(char *s, int32_t i) {
	char *r = string_alloc(2);
	r[0]    = s[i];
	r[1]    = '\0';
	return r;
}

bool starts_with(char *s, char *start) {
	return strncmp(start, s, strlen(start)) == 0;
}

bool ends_with(char *s, char *end) {
	size_t len_s   = strlen(s);
	size_t len_end = strlen(end);
	if (len_end > len_s) {
		return false;
	}
	return strncmp(s + len_s - len_end, end, len_end) == 0;
}

char *to_lower_case(char *s) {
	char *r = string_alloc(strlen(s) + 1);
	strcpy(r, s);
	int len = string_length(r);
	for (int i = 0; i < len; ++i) {
		r[i] = tolower(r[i]);
	}
	return r;
}

char *to_upper_case(char *s) {
	char *r = string_alloc(strlen(s) + 1);
	strcpy(r, s);
	int len = string_length(r);
	for (int i = 0; i < len; ++i) {
		r[i] = toupper(r[i]);
	}
	return r;
}

char *trim_end(char *str) {
	int pos = string_length(str) - 1;
	while (pos >= 0 && (str[pos] == ' ' || str[pos] == '\n' || str[pos] == '\r')) {
		pos--;
	}
	return substring(str, 0, pos + 1);
}

// Per Lowgren, CC BY-SA 3.0
// https://stackoverflow.com/a/35332046
#define is_unicode(c) (((c) & 0xc0) == 0xc0)
int string_utf8_decode(const char *str, int *i) {
	const unsigned char *s = (const unsigned char *)str;
	int                  u = *s, l = 1;
	if (is_unicode(u)) {
		int a = (u & 0x20) ? ((u & 0x10) ? ((u & 0x08) ? ((u & 0x04) ? 6 : 5) : 4) : 3) : 2;
		if (a < 6 || !(u & 0x02)) {
			u = ((u << (a + 1)) & 0xff) >> (a + 1);
			for (int b = 1; b < a; ++b)
				u = (u << 6) | (s[l++] & 0x3f);
		}
	}
	if (i)
		*i += l;
	return u;
}

void string_buffer_init(buffer_t *sb) {
	sb->capacity = 128;
	sb->buffer   = malloc(sb->capacity);
	string_buffer_reset(sb);
}

void string_buffer_reset(buffer_t *sb) {
	sb->length    = 0;
	sb->buffer[0] = '\0';
}

static void string_buffer_ensure(buffer_t *sb, int extra) {
	uint32_t needed = sb->length + extra + 1;
	if (needed <= sb->capacity) {
		return;
	}
	uint32_t new_cap = sb->capacity * 2;
	if (new_cap < needed) {
		new_cap = needed;
	}
	sb->buffer   = realloc(sb->buffer, new_cap);
	sb->capacity = new_cap;
}

void string_buffer_append(buffer_t *sb, char *s) {
	int n = (int)strlen(s);
	string_buffer_ensure(sb, n);
	memcpy(sb->buffer + sb->length, s, n);
	sb->length += n;
	sb->buffer[sb->length] = '\0';
}

char *string_buffer_get(buffer_t *sb) {
	return (char *)sb->buffer;
}

void string_buffer_free(buffer_t *sb) {
	free(sb->buffer);
	sb->buffer   = NULL;
	sb->length   = 0;
	sb->capacity = 0;
}
