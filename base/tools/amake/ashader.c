
// kong wrapper

#include "../../sources/kong/kong.h"
#include "iron_string.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <D3Dcompiler.h>
#include <d3d11.h>

char *hlsl_to_bin(char *source, char *shader_type, char *to) {
	char *type;
	if (string_equals(shader_type, "vert")) {
		type = "vs_5_0";
	}
	else {
		type = "ps_5_0";
	}

	ID3DBlob *error_message;
	ID3DBlob *shader_buffer;
	UINT      flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	HRESULT   hr    = D3DCompile(source, strlen(source) + 1, NULL, NULL, NULL, "main", type, flags, 0, &shader_buffer, &error_message);
	if (hr != S_OK) {
		printf("%s\n", (char *)error_message->lpVtbl->GetBufferPointer(error_message));
		return NULL;
	}

	FILE *fp  = fopen(to, "wb");
	int   len = shader_buffer->lpVtbl->GetBufferSize(shader_buffer);
	fwrite((char *)shader_buffer->lpVtbl->GetBufferPointer(shader_buffer), 1, len, fp);
	fclose(fp);
	shader_buffer->lpVtbl->Release(shader_buffer);
}
#endif

extern uint64_t    next_variable_id;
extern size_t      allocated_globals_size;
extern function_id next_function_index;
extern global_id   globals_size;
extern name_id     names_index;
extern size_t      sets_count;
extern type_id     next_type_index;
void               hlsl_export2(char **vs, char **fs, api_kind d3d, bool debug);
void               spirv_export2(char **vs, char **fs, int *vs_size, int *fs_size, bool debug);
void               wgsl_export2(char **vs, char **fs);

void kong_compile(char *shader_lang, const char *from, const char *to) {
	FILE *fp = fopen(from, "rb");
	fseek(fp, 0, SEEK_END);
	int size = ftell(fp);
	rewind(fp);
	char *data = malloc(size + 1);
	data[size] = 0;
	fread(data, size, 1, fp);
	fclose(fp);

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

#ifdef _WIN32

	char *vs;
	char *fs;
	hlsl_export2(&vs, &fs, API_DIRECT3D11, false);

	////
	// int i = string_last_index_of(to, "\\");
	// char filename[512];
	// strcpy(filename, to);
	// char *filebase = &filename[i + 1];
	// int j = string_index_of(filebase, ".");
	// filebase[j] = '\0';

	// char tmp[512];
	// strcpy(tmp, to);
	// tmp[i] = '\0';
	// strcat(tmp, "\\..\\..\\temp\\");
	// strcat(tmp, filebase);
	// strcat(tmp, ".vert.hlsl");
	// fp = fopen(tmp, "wb");
	// fwrite(vs, 1, strlen(vs), fp);
	// fclose(fp);
	// tmp[i] = '\0';
	// strcat(tmp, "\\..\\..\\temp\\");
	// strcat(tmp, filebase);
	// strcat(tmp, ".frag.hlsl");
	// fp = fopen(tmp, "wb");
	// fwrite(fs, 1, strlen(fs), fp);
	// fclose(fp);
	////

	char to_[512];
	strcpy(to_, to);
	to_[strlen(to_) - 4] = '\0';
	strcat(to_, "vert.d3d11");
	hlsl_to_bin(vs, "vert", to_);

	strcpy(to_, to);
	to_[strlen(to_) - 4] = '\0';
	strcat(to_, "frag.d3d11");
	hlsl_to_bin(fs, "frag", to_);

#elif defined(__APPLE__)

	char *metal = metal_export("");

	int  i = string_last_index_of(to, "/");
	char filename[512];
	strcpy(filename, to);
	char *filebase = &filename[i + 1];
	int   j        = string_index_of(filebase, ".");
	filebase[j]    = '\0';

	char to_[512];
	strcpy(to_, to);
	to_[strlen(to_) - 5] = '\0';
	strcat(to_, "vert.metal");

	fp = fopen(to_, "wb");
	fwrite("//>", 1, 3, fp);
	fwrite(filebase, 1, strlen(filebase), fp);
	fwrite("_vert\n", 1, 6, fp);
	fwrite(metal, 1, strlen(metal), fp);
	fclose(fp);

	strcpy(to_, to);
	to_[strlen(to_) - 5] = '\0';
	strcat(to_, "frag.metal");

	fp = fopen(to_, "wb");
	fwrite("//>", 1, 3, fp);
	fwrite(filebase, 1, strlen(filebase), fp);
	fwrite("_frag\n", 1, 6, fp);
	fclose(fp);

#else

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

#endif
}

int ashader(char *shader_lang, char *from, char *to) {
	// shader_lang == hlsl || metal || spirv || wgsl
	kong_compile(shader_lang, from, to);
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
