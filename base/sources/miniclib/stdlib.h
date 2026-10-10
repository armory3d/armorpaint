#pragma once

#include <stddef.h>

typedef unsigned long size_t;
#define EXIT_FAILURE 1
#define alloca       __builtin_alloca

void *malloc(size_t size);
void *calloc(size_t num, size_t size);
void *realloc(void *mem, size_t size);
void  free(void *mem);
void *aligned_alloc(size_t align, size_t size);

int  system(const char *string);
void exit(int code);

long int      strtol(const char *str, char **endptr, int base);
float         strtof(const char *str, char **endptr);
double        strtod(const char *str, char **endptr);
double        atof(const char *str);
int           abs(int n);
long long int llabs(long long int n);
void          qsort(void *base, size_t num, size_t size, int (*compar)(const void *, const void *));
int           rand();

#define RAND_MAX 2147483647
