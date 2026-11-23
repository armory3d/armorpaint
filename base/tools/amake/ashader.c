
// kong wrapper

#include "../../sources/kong/kong.h"
#include "iron_string.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <Windows.h>
#endif

extern uint64_t    next_variable_id;
extern size_t      allocated_globals_size;
extern function_id next_function_index;
extern global_id   globals_size;
extern name_id     names_index;
extern size_t      sets_count;
extern type_id     next_type_index;
void               spirv_export2(char **vs, char **fs, int *vs_size, int *fs_size, bool debug);
void               wgsl_export2(char **vs, char **fs);

static bool has_compute_function(void) {
	for (function_id i = 0; get_function(i) != NULL; ++i) {
		if (has_attribute(&get_function(i)->attributes, add_name("compute"))) {
			return true;
		}
	}
	return false;
}

static void kong_compile_compute(char *shader_lang, const char *to) {
	char *code;
	int   size;
	if (strcmp(shader_lang, "spirv") == 0) {
		transform(TRANSFORM_FLAG_ONE_COMPONENT_SWIZZLE | TRANSFORM_FLAG_BINARY_UNIFY_LENGTH);
		code = spirv_export_compute(&size);
	}
	else if (strcmp(shader_lang, "metal") == 0) {
		code = metal_export_compute();
		size = (int)strlen(code);
	}
	else if (strcmp(shader_lang, "wgsl") == 0) {
		code = wgsl_export_compute();
		size = (int)strlen(code);
	}
	else {
		printf("Compute shaders are not supported for %s\n", shader_lang);
		return;
	}

	FILE *fp = fopen(to, "wb");
	fwrite(code, 1, size, fp);
	fclose(fp);
}

void kong_compile(char *shader_lang, const char *from, const char *to, char **defines, int defines_count) {
	FILE *fp = fopen(from, "rb");
	fseek(fp, 0, SEEK_END);
	int size = ftell(fp);
	rewind(fp);
	char *data = malloc(size + 1);
	data[size] = 0;
	fread(data, size, 1, fp);
	fclose(fp);
	data = kong_preprocess(data, defines, defines_count);

	next_variable_id       = 1;
	allocated_globals_size = 0;
	next_function_index    = 0;
	globals_size           = 0;
	names_index            = 1;
	sets_count             = 0;
	next_type_index        = 0;
	names_init();
	types_init();
	functions_init();
	tokens tokens = tokenize(from, data);
	parse(from, &tokens);
	resolve_types();
	allocate_globals();
	for (function_id i = 0; get_function(i) != NULL; ++i) {
		compile_function_block(&get_function(i)->code, get_function(i)->block);
	}
	analyze();

	if (has_compute_function()) {
		kong_compile_compute(shader_lang, to);
		return;
	}

	if (strcmp(shader_lang, "wgsl") == 0) {
		transform(TRANSFORM_FLAG_ONE_COMPONENT_SWIZZLE);
		char *vs;
		char *fs;
		wgsl_export2(&vs, &fs);

		char to_[512];
		strcpy(to_, to);
		to_[strlen(to_) - 4] = '\0';
		strcat(to_, "vert.wgsl");

		FILE *fp = fopen(to_, "wb");
		fwrite(vs, 1, strlen(vs), fp);
		fclose(fp);

		strcpy(to_, to);
		to_[strlen(to_) - 4] = '\0';
		strcat(to_, "frag.wgsl");

		fp = fopen(to_, "wb");
		fwrite(fs, 1, strlen(fs), fp);
		fclose(fp);

		return;
	}

	if (strcmp(shader_lang, "metal") == 0) {
		char *metal = metal_export("");

		char to_[512];
		strcpy(to_, to);
		to_[strlen(to_) - 5] = '\0';
		strcat(to_, "vert.metal");

		fp = fopen(to_, "wb");
		fwrite("//>vert\n", 1, 8, fp);
		fwrite(metal, 1, strlen(metal), fp);
		fclose(fp);

		strcpy(to_, to);
		to_[strlen(to_) - 5] = '\0';
		strcat(to_, "frag.metal");

		fp = fopen(to_, "wb");
		fwrite("//>frag\n", 1, 8, fp);
		fclose(fp);

		return;
	}

	transform(TRANSFORM_FLAG_ONE_COMPONENT_SWIZZLE | TRANSFORM_FLAG_BINARY_UNIFY_LENGTH);
	char *vs;
	char *fs;
	int   vs_size;
	int   fs_size;
	spirv_export2(&vs, &fs, &vs_size, &fs_size, false);

	char to_[512];
	strcpy(to_, to);
	to_[strlen(to_) - 5] = '\0';
	strcat(to_, "vert.spirv");

	fp = fopen(to_, "wb");
	fwrite(vs, 1, vs_size, fp);
	fclose(fp);

	strcpy(to_, to);
	to_[strlen(to_) - 5] = '\0';
	strcat(to_, "frag.spirv");

	fp = fopen(to_, "wb");
	fwrite(fs, 1, fs_size, fp);
	fclose(fp);
}

int ashader(char *shader_lang, char *from, char *to, char **defines, int defines_count) {
	// shader_lang == metal || spirv || wgsl
	kong_compile(shader_lang, from, to, defines, defines_count);
	return 0;
}

#ifdef IRON_WINDOWS
void iron_microsoft_format(const char *format, va_list args, wchar_t *buffer) {
	char cbuffer[4096];
	vsprintf(cbuffer, format, args);
	MultiByteToWideChar(CP_UTF8, 0, cbuffer, -1, buffer, 4096);
}
#endif

void iron_log_args(iron_log_level_t level, const char *format, va_list args) {
#ifdef IRON_WINDOWS
	wchar_t buffer[4096];
	iron_microsoft_format(format, args, buffer);
	wcscat(buffer, L"\r\n");
	OutputDebugStringW(buffer);
	DWORD written;
	WriteConsoleW(GetStdHandle(level == IRON_LOG_LEVEL_INFO ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE), buffer, (DWORD)wcslen(buffer), &written, NULL);
#else
	char buffer[4096];
	vsnprintf(buffer, 4090, format, args);
	strcat(buffer, "\n");
	fprintf(level == IRON_LOG_LEVEL_INFO ? stdout : stderr, "%s", buffer);
#endif
}

void iron_log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	iron_log_args(IRON_LOG_LEVEL_INFO, format == NULL ? "null" : format, args);
	va_end(args);
}
