#include "stdio.h"
#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

#ifdef IRON_WASM
__attribute__((import_module("imports"), import_name("js_printf"))) void    js_printf(const char *format);
__attribute__((import_module("imports"), import_name("js_fopen"))) FILE    *js_fopen(const char *filename, const char *mode);
__attribute__((import_module("imports"), import_name("js_ftell"))) long int js_ftell(FILE *stream);
__attribute__((import_module("imports"), import_name("js_fseek"))) int      js_fseek(FILE *stream, long int offset, int origin);
__attribute__((import_module("imports"), import_name("js_fread"))) size_t   js_fread(void *ptr, size_t size, size_t count, FILE *stream);
__attribute__((import_module("imports"), import_name("js_fwrite"))) size_t  js_fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
__attribute__((import_module("imports"), import_name("js_fclose"))) int     js_fclose(FILE *stream);
__attribute__((import_module("imports"), import_name("js_rename"))) int     js_rename(const char *oldpath, const char *newpath);
#endif

FILE *stdout = NULL, *stderr = NULL;

int printf(const char *format, ...) {
	char    buf[1024];
	va_list args;
	va_start(args, format);
	int ret = stbsp_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
#ifdef IRON_WASM
	js_printf(buf);
#endif
	return ret;
}

int fprintf(FILE *stream, const char *format, ...) {
	char    buf[1024];
	va_list args;
	va_start(args, format);
	int ret = stbsp_vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
#ifdef IRON_WASM
	js_printf(buf);
#endif
	return ret;
}

int sprintf(char *s, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int ret = stbsp_vsprintf(s, format, args);
	va_end(args);
	return ret;
}

int snprintf(char *s, size_t n, const char *format, ...) {
	va_list args;
	va_start(args, format);
	int ret = stbsp_vsnprintf(s, (int)n, format, args);
	va_end(args);
	return ret;
}

int vsnprintf(char *s, size_t n, const char *format, va_list arg) {
	return stbsp_vsnprintf(s, (int)n, format, arg);
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream) {
#ifdef IRON_WASM
	return js_fwrite(ptr, size, count, stream);
#endif
	return 0;
}

FILE *fopen(const char *filename, const char *mode) {
#ifdef IRON_WASM
	return js_fopen(filename, mode);
#endif
	return NULL;
}

int fclose(FILE *stream) {
#ifdef IRON_WASM
	return js_fclose(stream);
#endif
	return 0;
}

long int ftell(FILE *stream) {
#ifdef IRON_WASM
	return js_ftell(stream);
#endif
	return 0;
}

int fseek(FILE *stream, long int offset, int origin) {
#ifdef IRON_WASM
	return js_fseek(stream, offset, origin);
#endif
	return 0;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream) {
#ifdef IRON_WASM
	return js_fread(ptr, size, count, stream);
#endif
	return 0;
}

int fputs(const char *str, FILE *stream) {
	return 0;
}

int puts(char *str) {
	return 0;
}

int rename(const char *oldpath, const char *newpath) {
#ifdef IRON_WASM
	return js_rename(oldpath, newpath);
#else
	(void)oldpath;
	(void)newpath;
	return -1;
#endif
}
