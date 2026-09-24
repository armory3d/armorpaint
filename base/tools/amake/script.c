// General purpose natives for minic scripts, and `amake --c <file.c> [args]`

#include "../../sources/libs/minic.h"
#include "make.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ███╗   ███╗ █████╗ ██████╗
// ████╗ ████║██╔══██╗██╔══██╗
// ██╔████╔██║███████║██████╔╝
// ██║╚██╔╝██║██╔══██║██╔═══╝
// ██║ ╚═╝ ██║██║  ██║██║
// ╚═╝     ╚═╝╚═╝  ╚═╝╚═╝

// String to string map, keeps insertion order
typedef struct {
	int          count;
	any_array_t *keys;
	any_array_t *values;
} map_t;

static map_t *map_create(void) {
	map_t *m  = calloc(1, sizeof(map_t));
	m->keys   = list_create();
	m->values = list_create();
	return m;
}

static int map_index(map_t *m, char *key) {
	for (int i = 0; i < m->keys->length; ++i) {
		if (strcmp(m->keys->buffer[i], key) == 0) {
			return i;
		}
	}
	return -1;
}

static void map_set(map_t *m, char *key, char *value) {
	int i = map_index(m, key);
	if (i >= 0) {
		m->values->buffer[i] = value;
		return;
	}
	any_array_push(m->keys, key);
	any_array_push(m->values, value);
	m->count = m->keys->length;
}

static char *map_get(map_t *m, char *key) {
	int i = map_index(m, key);
	return i >= 0 ? m->values->buffer[i] : NULL;
}

//      ██╗███████╗ ██████╗ ███╗   ██╗
//      ██║██╔════╝██╔═══██╗████╗  ██║
//      ██║███████╗██║   ██║██╔██╗ ██║
// ██   ██║╚════██║██║   ██║██║╚██╗██║
// ╚█████╔╝███████║╚██████╔╝██║ ╚████║
//  ╚════╝ ╚══════╝ ╚═════╝ ╚═╝  ╚═══╝

static void append_utf8(buffer_t *sb, unsigned cp) {
	char buf[5] = {0};
	if (cp < 0x80) {
		buf[0] = (char)cp;
	}
	else if (cp < 0x800) {
		buf[0] = (char)(0xC0 | (cp >> 6));
		buf[1] = (char)(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000) {
		buf[0] = (char)(0xE0 | (cp >> 12));
		buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		buf[2] = (char)(0x80 | (cp & 0x3F));
	}
	else {
		buf[0] = (char)(0xF0 | (cp >> 18));
		buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		buf[3] = (char)(0x80 | (cp & 0x3F));
	}
	string_buffer_append(sb, buf);
}

static unsigned parse_hex4(char *s) {
	char hex[5] = {0};
	for (int i = 0; i < 4 && s[i] != '\0'; ++i) {
		hex[i] = s[i];
	}
	return (unsigned)strtoul(hex, NULL, 16);
}

// Parses a JSON string starting at the opening quote, returns the position after the closing quote
static char *parse_json_string(char *s, char **out) {
	buffer_t sb = {0};
	string_buffer_init(&sb);
	s++;
	while (*s != '\0' && *s != '"') {
		if (*s != '\\') {
			char c[2] = {*s++, '\0'};
			string_buffer_append(&sb, c);
			continue;
		}
		s++;
		switch (*s) {
		case 'b':
			string_buffer_append(&sb, "\b");
			break;
		case 'f':
			string_buffer_append(&sb, "\f");
			break;
		case 'n':
			string_buffer_append(&sb, "\n");
			break;
		case 'r':
			string_buffer_append(&sb, "\r");
			break;
		case 't':
			string_buffer_append(&sb, "\t");
			break;
		case 'u': {
			unsigned cp = parse_hex4(s + 1);
			s += 4;
			if (cp >= 0xD800 && cp < 0xDC00 && s[1] == '\\' && s[2] == 'u') {
				unsigned low = parse_hex4(s + 3);
				cp           = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
				s += 6;
			}
			append_utf8(&sb, cp);
			break;
		}
		default: {
			char c[2] = {*s, '\0'};
			string_buffer_append(&sb, c);
		}
		}
		if (*s != '\0') {
			s++;
		}
	}
	*out = string_buffer_get(&sb);
	return *s == '"' ? s + 1 : s;
}

// Reads a flat {"key": "value"} object
static map_t *json_parse_map(char *s) {
	map_t *m = map_create();
	while (s != NULL && *s != '\0') {
		while (*s != '\0' && *s != '"') {
			s++;
		}
		if (*s == '\0') {
			break;
		}
		char *key;
		char *value;
		s = parse_json_string(s, &key);
		while (*s != '\0' && *s != '"') {
			s++;
		}
		if (*s == '\0') {
			break;
		}
		s = parse_json_string(s, &value);
		map_set(m, key, value);
	}
	return m;
}

static int compare_strings(const void *a, const void *b) {
	return strcmp(*(char **)a, *(char **)b);
}

// Same output as JSON.stringify(map, sorted_keys, indent)
static char *json_stringify_map(map_t *m, int indent) {
	any_array_t *keys = list_create();
	for (int i = 0; i < m->keys->length; ++i) {
		any_array_push(keys, m->keys->buffer[i]);
	}
	qsort(keys->buffer, keys->length, sizeof(char *), compare_strings);
	char *pad = string("%*s", indent, "");
	char *nl  = indent > 0 ? "\n" : "";
	char *sep = indent > 0 ? ": " : ":";

	buffer_t sb = {0};
	string_buffer_init(&sb);
	string_buffer_append(&sb, "{");
	for (int i = 0; i < keys->length; ++i) {
		string_buffer_append(&sb, string("%s%s%s\"%s\"%s\"%s\"", i > 0 ? "," : "", nl, pad, json_escape(keys->buffer[i]), sep,
		                                 json_escape(map_get(m, keys->buffer[i]))));
	}
	string_buffer_append(&sb, keys->length > 0 ? nl : "");
	string_buffer_append(&sb, "}");
	return string_buffer_get(&sb);
}

// ███╗   ██╗ █████╗ ████████╗██╗██╗   ██╗███████╗███████╗
// ████╗  ██║██╔══██╗╚══██╔══╝██║██║   ██║██╔════╝██╔════╝
// ██╔██╗ ██║███████║   ██║   ██║██║   ██║█████╗  ███████╗
// ██║╚██╗██║██╔══██║   ██║   ██║╚██╗ ██╔╝██╔══╝  ╚════██║
// ██║ ╚████║██║  ██║   ██║   ██║ ╚████╔╝ ███████╗███████║
// ╚═╝  ╚═══╝╚═╝  ╚═╝   ╚═╝   ╚═╝  ╚═══╝  ╚══════╝╚══════╝

static int    script_argc = 0;
static char **script_argv = NULL;

static char *arg_str(minic_val_t *args, int argc, int i) {
	char *s = minic_arg_p(args, argc, i);
	return s != NULL ? s : "";
}

static minic_val_t native_script_arg(minic_val_t *args, int argc) {
	int i = minic_arg_i(args, argc, 0);
	return minic_val_ptr(i >= 0 && i < script_argc ? script_argv[i] : NULL);
}

static minic_val_t native_print(minic_val_t *args, int argc) {
	printf("%s\n", arg_str(args, argc, 0));
	return minic_val_void();
}

static minic_val_t native_fs_readdir(minic_val_t *args, int argc) {
	return minic_val_ptr(fs_readdir(script_path(arg_str(args, argc, 0))));
}

static minic_val_t native_fs_readfile(minic_val_t *args, int argc) {
	return minic_val_ptr(fs_readfile(script_path(arg_str(args, argc, 0)), NULL));
}

static minic_val_t native_string_index_of(minic_val_t *args, int argc) {
	char *s    = arg_str(args, argc, 0);
	int   from = minic_arg_i(args, argc, 2);
	int   len  = (int)strlen(s);
	if (from < 0) {
		from = 0;
	}
	if (from > len) {
		return minic_val_int(-1);
	}
	char *found = strstr(s + from, arg_str(args, argc, 1));
	return minic_val_int(found != NULL ? (int)(found - s) : -1);
}

static minic_val_t native_string_length(minic_val_t *args, int argc) {
	return minic_val_int((int)strlen(arg_str(args, argc, 0)));
}

static minic_val_t native_string_equals(minic_val_t *args, int argc) {
	return minic_val_int(strcmp(arg_str(args, argc, 0), arg_str(args, argc, 1)) == 0);
}

static minic_val_t native_starts_with(minic_val_t *args, int argc) {
	return minic_val_int(starts_with(arg_str(args, argc, 0), arg_str(args, argc, 1)));
}

static minic_val_t native_ends_with(minic_val_t *args, int argc) {
	return minic_val_int(ends_with(arg_str(args, argc, 0), arg_str(args, argc, 1)));
}

static minic_val_t native_substring(minic_val_t *args, int argc) {
	return minic_val_ptr(js_substring(arg_str(args, argc, 0), minic_arg_i(args, argc, 1), minic_arg_i(args, argc, 2)));
}

static minic_val_t native_map_create(minic_val_t *args, int argc) {
	return minic_val_ptr(map_create());
}

static minic_val_t native_map_set(minic_val_t *args, int argc) {
	map_set(minic_arg_p(args, argc, 0), string_copy(arg_str(args, argc, 1)), string_copy(arg_str(args, argc, 2)));
	return minic_val_void();
}

static minic_val_t native_map_get(minic_val_t *args, int argc) {
	return minic_val_ptr(map_get(minic_arg_p(args, argc, 0), arg_str(args, argc, 1)));
}

static minic_val_t native_json_parse_map(minic_val_t *args, int argc) {
	return minic_val_ptr(json_parse_map(minic_arg_p(args, argc, 0)));
}

static minic_val_t native_json_stringify_map(minic_val_t *args, int argc) {
	return minic_val_ptr(json_stringify_map(minic_arg_p(args, argc, 0), minic_arg_i(args, argc, 1)));
}

void script_register_natives(void) {
	minic_struct_begin("string_array_t", (int)sizeof(any_array_t), MINIC_ALIGNOF(any_array_t));
	minic_struct_field("buffer", (int)offsetof(any_array_t, buffer), MINIC_T_PTR, MINIC_T_PTR, NULL);
	minic_struct_field("length", (int)offsetof(any_array_t, length), MINIC_T_INT, MINIC_T_INT, NULL);

	MINIC_STRUCT(map_t);
	MINIC_I(count);
	MINIC_END();

	minic_register("script_arg", "p(i)", native_script_arg);
	minic_register("print", "v(p)", native_print);
	minic_register("fs_readdir", "p:string_array_t(p)", native_fs_readdir);
	minic_register("fs_readfile", "p(p)", native_fs_readfile);
	minic_register("string_index_of", "i(p,p,i)", native_string_index_of);
	minic_register("string_length", "i(p)", native_string_length);
	minic_register("string_equals", "b(p,p)", native_string_equals);
	minic_register("starts_with", "b(p,p)", native_starts_with);
	minic_register("ends_with", "b(p,p)", native_ends_with);
	minic_register("substring", "p(p,i,i)", native_substring);
	minic_register("map_create", "p:map_t()", native_map_create);
	minic_register("map_set", "v(p,p,p)", native_map_set);
	minic_register("map_get", "p(p,p)", native_map_get);
	minic_register("json_parse_map", "p:map_t(p)", native_json_parse_map);
	minic_register("json_stringify_map", "p(p,i)", native_json_stringify_map);
}

// amake --c <file.c> [args], relative paths in the script resolve against the working directory
int run_script(char *file, int argc, char **argv) {
	char *source = fs_readfile(file, NULL);
	if (source == NULL) {
		printf("Error: %s not found.\n", file);
		return 1;
	}
	script_argc = argc;
	script_argv = argv;
	script_push_dir(os_cwd());
	minic_ctx_t *ctx = minic_eval_named(source, file);
	script_pop_dir();
	float result = minic_ctx_result(ctx);
	if (result == -1.0f) {
		printf("Error: %s failed.\n", file);
		return 1;
	}
	return (int)result;
}
