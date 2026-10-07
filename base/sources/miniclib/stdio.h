#pragma once

#include <stdarg.h>
#include <stddef.h>

#define SEEK_SET 0
#define SEEK_END 1

typedef int FILE;

extern FILE *stdout, *stderr;

int      printf(const char *format, ...);
int      fprintf(FILE *stream, const char *format, ...);
int      sprintf(char *s, const char *format, ...);
int      snprintf(char *s, size_t n, const char *format, ...);
int      vsnprintf(char *s, size_t n, const char *format, va_list arg);
size_t   fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
FILE    *fopen(const char *filename, const char *mode);
int      fclose(FILE *stream);
long int ftell(FILE *stream);
int      fseek(FILE *stream, long int offset, int origin);
size_t   fread(void *ptr, size_t size, size_t count, FILE *stream);
int      fputs(const char *str, FILE *stream);
int      puts(char *str);
int      rename(const char *oldpath, const char *newpath);
