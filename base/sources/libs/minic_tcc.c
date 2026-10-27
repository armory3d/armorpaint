
// Scripts compiled in memory with libtcc

#include "minic_tcc.h"
#include "iron_array.h"
#include "iron_string.h"
#include "libtcc.h"
#include "minic.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void console_log(char *s);

static int minic_tcc_printf(char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);
	char *buf = malloc(len + 1);
	va_start(args, fmt);
	vsnprintf(buf, len + 1, fmt, args);
	va_end(args);
	console_log(buf);
	free(buf);
	return len;
}

static int minic_tcc_sprintf(char *dst, char *fmt, ...) {
	va_list args;
	va_start(args, fmt);
	int n = vsprintf(dst, fmt, args);
	va_end(args);
	return n;
}

typedef struct {
	const char *name;
	const char *sig;
	void       *fn;
} minic_tcc_fn_t;

static minic_tcc_fn_t *minic_tcc_fns      = NULL;
static int             minic_tcc_count    = 0;
static int             minic_tcc_capacity = 0;

void minic_tcc_register(const char *name, const char *sig, void *fn) {
	if (minic_tcc_count == minic_tcc_capacity) {
		minic_tcc_capacity = minic_tcc_capacity > 0 ? minic_tcc_capacity * 2 : 1024;
		minic_tcc_fns      = realloc(minic_tcc_fns, minic_tcc_capacity * sizeof(minic_tcc_fn_t));
	}
	minic_tcc_fns[minic_tcc_count] = (minic_tcc_fn_t){name, sig, fn};
	minic_tcc_count++;
}

static void minic_tcc_init(void) {
	static bool done = false;
	if (done) {
		return;
	}
	done = true;
	minic_tcc_register("printf", "i(p:char fmt,...)", (void *)minic_tcc_printf);
	minic_tcc_register("sprintf", "i(p:char dst,p:char fmt,...)", (void *)minic_tcc_sprintf);
	minic_tcc_register_builtins();
}

static const char *minic_tcc_sig_type(char c) {
	switch (c) {
	case 'f':
		return "float";
	case 'd':
		return "double";
	case 'p':
		return "void *";
	case 'b':
		return "bool";
	case 'c':
		return "char";
	case 'v':
		return "void";
	default:
		return "int";
	}
}

// One type of a sig: "f", "p:name" (pointer) or "s:name" (struct by value)
static const char *minic_tcc_sig_read_type(const char **p, char *buf, int buf_size) {
	char c = **p;
	(*p)++;
	if (**p != ':') {
		return minic_tcc_sig_type(c);
	}
	(*p)++;
	const char *type = *p;
	while (**p != '\0' && **p != ' ' && **p != '(' && **p != ',' && **p != ')') {
		(*p)++;
	}
	snprintf(buf, buf_size, c == 's' ? "%.*s" : "%.*s *", (int)(*p - type), type);
	return buf;
}

static const char *minic_tcc_prelude =
    "typedef _Bool bool;\n"
    "#define true 1\n"
    "#define false 0\n"
    "#define NULL ((void *)0)\n"
    "typedef signed char i8;\n"
    "typedef unsigned char u8;\n"
    "typedef short i16;\n"
    "typedef unsigned short u16;\n"
    "typedef int i32;\n"
    "typedef unsigned int u32;\n"
    "typedef long long i64;\n"
    "typedef unsigned long long u64;\n"
    "typedef float f32;\n"
    "typedef double f64;\n"
    "typedef i8 int8_t;\n"
    "typedef u8 uint8_t;\n"
    "typedef i16 int16_t;\n"
    "typedef u16 uint16_t;\n"
    "typedef i32 int32_t;\n"
    "typedef u32 uint32_t;\n"
    "typedef i64 int64_t;\n"
    "typedef u64 uint64_t;\n"
    "typedef unsigned long size_t;\n"
    "typedef union mat3_t { float m[9]; struct { float m00, m01, m02, m10, m11, m12, m20, m21, m22; }; } mat3_t;\n"
    "typedef union mat4_t { float m[16]; struct { float m00, m01, m02, m03, m10, m11, m12, m13, m20, m21, m22, m23, m30, m31, m32, m33; }; } mat4_t;\n";

// Struct names declared up front, so the structs and functions can point to each other in any order
static void minic_tcc_forward(buffer_t *sb, any_array_t *done, const char *name, int len) {
	if (len < 3 || strncmp(name + len - 2, "_t", 2) != 0 || strncmp(name, "int", 3) == 0 || strncmp(name, "uint", 4) == 0 ||
	    (len == 6 && (strncmp(name, "mat3_t", 6) == 0 || strncmp(name, "mat4_t", 6) == 0 || strncmp(name, "size_t", 6) == 0))) {
		return;
	}
	char *s = string("%.*s", len, name);
	for (int i = 0; i < done->length; ++i) {
		if (strcmp(done->buffer[i], s) == 0) {
			return;
		}
	}
	any_array_push(done, s);
	string_buffer_append(sb, string("typedef struct %s %s;\n", s, s));
}

static void minic_tcc_forward_sig(buffer_t *sb, any_array_t *done, const char *sig) {
	for (const char *p = sig; *p != '\0'; ++p) {
		if ((*p == 'p' || *p == 's') && p[1] == ':') {
			const char *name = p + 2;
			int         len  = 0;
			while (name[len] != '\0' && name[len] != ' ' && name[len] != '(' && name[len] != ',' && name[len] != ')') {
				len++;
			}
			minic_tcc_forward(sb, done, name, len);
		}
	}
}

static int minic_tcc_type_size(minic_type_t t, const char *struct_name) {
	switch (t) {
	case MINIC_T_DOUBLE:
	case MINIC_T_PTR:
		return 8;
	case MINIC_T_BOOL:
	case MINIC_T_CHAR:
		return 1;
	case MINIC_T_I16:
	case MINIC_T_U16:
		return 2;
	case MINIC_T_EMBED:
		for (int i = 0; i < minic_struct_count; ++i) {
			if (strcmp(minic_structs[i].name, struct_name) == 0) {
				return minic_structs[i].size;
			}
		}
		return 0;
	default:
		return 4;
	}
}

static const char *minic_tcc_scalar_name(minic_type_t t) {
	switch (t) {
	case MINIC_T_FLOAT:
		return "float";
	case MINIC_T_DOUBLE:
		return "double";
	case MINIC_T_BOOL:
		return "bool";
	case MINIC_T_CHAR:
		return "char";
	case MINIC_T_I16:
		return "int16_t";
	case MINIC_T_U16:
		return "uint16_t";
	case MINIC_T_PTR:
	case MINIC_T_VOID:
		return "void";
	default:
		return "int";
	}
}

// Registered fields at their native offsets, the gaps between them are padding
static void minic_tcc_struct(buffer_t *sb, minic_struct_t *s) {
	int order[MINIC_MAX_STRUCT_FIELDS];
	for (int i = 0; i < s->field_count; ++i) {
		int j = i;
		while (j > 0 && s->offsets[order[j - 1]] > s->offsets[i]) {
			order[j] = order[j - 1];
			j--;
		}
		order[j] = i;
	}
	string_buffer_append(sb, string("struct %s {\n", s->name));
	int pos = 0;
	for (int k = 0; k < s->field_count; ++k) {
		int          f     = order[k];
		minic_type_t t     = s->types[f];
		const char  *sname = s->field_structs[f];
		int          count = s->counts[f] > 0 ? s->counts[f] : 1;
		int          size  = minic_tcc_type_size(t, sname) * count;
		if (s->offsets[f] < pos || size == 0 || s->offsets[f] + size > s->size) {
			continue;
		}
		if (s->offsets[f] > pos) {
			string_buffer_append(sb, string("    char _pad%d[%d];\n", pos, s->offsets[f] - pos));
		}
		const char *type     = t == MINIC_T_EMBED                     ? sname
		                       : t == MINIC_T_PTR && sname[0] != '\0' ? sname
		                                                              : minic_tcc_scalar_name(t == MINIC_T_PTR ? s->deref_types[f] : t);
		char        stars[8] = "";
		for (int d = 0; d < s->pointer_depths[f] && d < 7; ++d) {
			stars[d]     = '*';
			stars[d + 1] = '\0';
		}
		string_buffer_append(sb, string(s->counts[f] > 0 ? "    %s %s%s[%d];\n" : "    %s %s%s;\n", type, stars, s->fields[f], s->counts[f]));
		pos = s->offsets[f] + size;
	}
	if (s->size > pos) {
		string_buffer_append(sb, string("    char _pad%d[%d];\n", pos, s->size - pos));
	}
	string_buffer_append(sb, "};\n");
}

// Prototype from a sig, like "float name(char *, int);"
static void minic_tcc_sig_proto(buffer_t *sb, const char *name, const char *sig) {
	const char *p = sig;
	char        buf[MINIC_MAX_NAME + 4];
	string_buffer_append(sb, string("%s %s(", minic_tcc_sig_read_type(&p, buf, sizeof(buf)), name));
	if (*p == '(') {
		p++;
	}
	int arg = 0;
	while (*p != '\0' && *p != ')') {
		if (*p == ',' || *p == ' ') {
			p++;
			continue;
		}
		if (arg > 0) {
			string_buffer_append(sb, ", ");
		}
		if (strncmp(p, "...", 3) == 0) {
			string_buffer_append(sb, "...");
			p += 3;
		}
		else {
			string_buffer_append(sb, minic_tcc_sig_read_type(&p, buf, sizeof(buf)));
			while (*p != '\0' && *p != ',' && *p != ')') {
				p++; // Argument name
			}
		}
		arg++;
	}
	string_buffer_append(sb, arg == 0 ? "void);\n" : ");\n");
}

static bool minic_tcc_is_ident(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// Whether the script defines a function with this name, it then replaces the native one
static bool minic_tcc_script_defines(const char *text, const char *name) {
	int len = (int)strlen(name);
	for (const char *p = strstr(text, name); p != NULL; p = strstr(p + len, name)) {
		if ((p > text && minic_tcc_is_ident(p[-1])) || minic_tcc_is_ident(p[len])) {
			continue;
		}
		const char *q = p + len;
		while (*q == ' ' || *q == '\t') {
			q++;
		}
		if (*q != '(') {
			continue;
		}
		int depth = 0;
		for (; *q != '\0'; ++q) {
			depth += *q == '(' ? 1 : *q == ')' ? -1 : 0;
			if (depth == 0) {
				break;
			}
		}
		if (*q == ')') {
			q++;
			while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') {
				q++;
			}
			if (*q == '{') {
				return true;
			}
		}
	}
	return false;
}

static char *minic_tcc_header(const char *text) {
	minic_register_builtins(); // Structs and enums
	minic_tcc_init();

	buffer_t sb;
	string_buffer_init(&sb);
	string_buffer_append(&sb, minic_tcc_prelude);

	any_array_t *done = any_array_create(0);
	for (int i = 0; i < minic_struct_count; ++i) {
		minic_tcc_forward(&sb, done, minic_structs[i].name, (int)strlen(minic_structs[i].name));
		for (int f = 0; f < minic_structs[i].field_count; ++f) {
			const char *sname = minic_structs[i].field_structs[f];
			minic_tcc_forward(&sb, done, sname, (int)strlen(sname));
		}
	}
	for (int i = 0; i < minic_tcc_count; ++i) {
		minic_tcc_forward_sig(&sb, done, minic_tcc_fns[i].sig);
	}

	// Embedded structs are defined before the structs embedding them
	bool written[MINIC_MAX_STRUCTS] = {0};
	for (int pass = 0; pass < minic_struct_count; ++pass) {
		for (int i = 0; i < minic_struct_count; ++i) {
			minic_struct_t *s     = &minic_structs[i];
			bool            ready = !written[i] && strcmp(s->name, "mat3_t") != 0 && strcmp(s->name, "mat4_t") != 0; // Unions, in the prelude
			for (int f = 0; f < s->field_count && ready; ++f) {
				if (s->types[f] != MINIC_T_EMBED) {
					continue;
				}
				for (int j = 0; j < minic_struct_count; ++j) {
					if (strcmp(minic_structs[j].name, s->field_structs[f]) == 0 && !written[j]) {
						ready = false;
					}
				}
			}
			if (ready) {
				minic_tcc_struct(&sb, s);
				written[i] = true;
			}
		}
	}

	for (int i = 0; i < minic_enum_const_count_get(); ++i) {
		string_buffer_append(&sb, string("#define %s %d\n", minic_enum_const_name_at(i), minic_enum_const_value_at(i)));
	}

	for (int i = 0; i < minic_global_count_get(); ++i) {
		minic_type_t t = minic_global_type_at(i);
		string_buffer_append(&sb, string("extern %s%s %s;\n", minic_tcc_scalar_name(t), t == MINIC_T_PTR ? " *" : "", minic_global_name_at(i)));
	}
	for (int i = 0; i < minic_tcc_count; ++i) {
		if (!minic_tcc_script_defines(text, minic_tcc_fns[i].name)) {
			minic_tcc_sig_proto(&sb, minic_tcc_fns[i].name, minic_tcc_fns[i].sig);
		}
	}

	char *result = string_copy(string_buffer_get(&sb));
	string_buffer_free(&sb);
	return result;
}

static void minic_tcc_error(void *opaque, const char *msg) {
	console_log((char *)msg);
}

// Compiles the script with tcc and runs its main(), errors are counted in minic_error_count
void minic_tcc_eval(char *text, char *name) {
	char     *code = string("%s#line 1 \"%s\"\n%s\n", minic_tcc_header(text), name, text);
	TCCState *s    = tcc_new();
	tcc_set_error_func(s, NULL, minic_tcc_error);
	tcc_set_options(s, "-nostdlib");
	tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
	for (int i = 0; i < minic_tcc_count; ++i) {
		if (!minic_tcc_script_defines(text, minic_tcc_fns[i].name)) {
			tcc_add_symbol(s, minic_tcc_fns[i].name, minic_tcc_fns[i].fn);
		}
	}
	for (int i = 0; i < minic_global_count_get(); ++i) {
		tcc_add_symbol(s, minic_global_name_at(i), minic_global_ptr_at(i));
	}
	void (*fn)(void) = NULL;
	if (tcc_compile_string(s, code) == 0 && tcc_relocate(s) == 0) {
		fn = (void (*)(void))tcc_get_symbol(s, "main");
		if (fn == NULL) {
			console_log(string("%s: main() not found", name));
		}
	}
	if (fn != NULL) {
		fn();
	}
	else {
		minic_error_count++;
	}
	tcc_delete(s);
}
