#include "stdlib.h"
#include "ctype.h"
#include "stddef.h"
#include "stdio.h"
#include "string.h"

#define ALIGNMENT   8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

#define HEAP_SIZE 1024 * 1024 * 512
static unsigned char heap[HEAP_SIZE] __attribute__((aligned(ALIGNMENT)));
static size_t        heap_top = ALIGNMENT;

typedef struct block {
	size_t        size;
	struct block *next;
} block_t;
static block_t *free_list = NULL;

#ifdef IRON_WASM
__attribute__((export_name("wasm_malloc")))
#endif
void *malloc(size_t size) {
	size           = ALIGN(size);
	block_t **link = &free_list;
	block_t  *curr = free_list;
	while (curr) {
		if (curr->size >= size) {
			if (curr->size >= size + sizeof(block_t) + ALIGNMENT) {
				block_t *remainder = (block_t *)((unsigned char *)curr + sizeof(block_t) + size);
				remainder->size    = curr->size - size - sizeof(block_t);
				remainder->next    = curr->next;
				*link              = remainder;
				curr->size         = size;
			}
			else {
				*link = curr->next;
			}
			return (void *)(curr + 1);
		}
		link = &curr->next;
		curr = curr->next;
	}
	if (heap_top + sizeof(block_t) + size > HEAP_SIZE) {
		printf("malloc: out of memory");
		return NULL;
	}
	block_t *ptr = (block_t *)&heap[heap_top];
	ptr->size    = size;
	heap_top += sizeof(block_t) + size;
	return (void *)(ptr + 1);
}

#ifdef IRON_WASM
__attribute__((export_name("wasm_free")))
#endif
void free(void *mem) {
	if (!mem) {
		return;
	}
	block_t *block = (block_t *)mem - 1;
	block_t *curr  = free_list;
	block_t *prev  = NULL;
	while (curr && curr < block) {
		prev = curr;
		curr = curr->next;
	}
	if (prev) {
		prev->next = block;
	}
	else {
		free_list = block;
	}
	block->next = curr;
	if (curr && (unsigned char *)(block + 1) + block->size == (unsigned char *)curr) {
		block->size += sizeof(block_t) + curr->size;
		block->next = curr->next;
	}
	if (prev && (unsigned char *)(prev + 1) + prev->size == (unsigned char *)block) {
		prev->size += sizeof(block_t) + block->size;
		prev->next = block->next;
	}
}

void *calloc(size_t num, size_t size) {
	size_t total = num * size;
	void  *ptr   = malloc(total);
	if (ptr) {
		memset(ptr, 0, total);
	}
	return ptr;
}

void *realloc(void *mem, size_t size) {
	if (!mem) {
		return malloc(size);
	}
	block_t *b = (block_t *)mem - 1;
	if (b->size >= size) {
		return mem;
	}
	void *ptr = malloc(size);
	if (ptr) {
		memcpy(ptr, mem, b->size);
		free(mem);
	}
	return ptr;
}

void *aligned_alloc(size_t align, size_t size) {
	heap_top = (heap_top + align - 1) & ~(align - 1);
	return malloc(size);
}

int system(const char *string) {
	return 0;
}

void exit(int code) {
	exit(code);
}

long int strtol(const char *str, char **endptr, int base) {
	while (isspace(*str)) {
		str++;
	}
	int sign = (*str == '-') ? (str++, -1) : (*str == '+') ? (str++, 1) : 1;
	if (base == 0) {
		base = (*str == '0') ? (str[1] == 'x' || str[1] == 'X') ? (str += 2, 16) : (str++, 8) : 10;
	}
	if (base == 16 && *str == '0' && (str[1] == 'x' || str[1] == 'X')) {
		str += 2;
	}
	long val = 0;
	for (; *str; str++) {
		int digit = isdigit(*str) ? *str - '0' : (*str >= 'a' && *str <= 'z') ? *str - 'a' + 10 : (*str >= 'A' && *str <= 'Z') ? *str - 'A' + 10 : 999;
		if (digit >= base) {
			break;
		}
		val = val * base + digit;
	}
	if (endptr) {
		*endptr = (char *)str;
	}
	return sign * val;
}

float strtof(const char *str, char **endptr) {
	return (float)strtod(str, endptr);
}

double strtod(const char *str, char **endptr) {
	while (isspace(*str)) {
		str++;
	}
	int    sign = (*str == '-') ? (str++, -1) : (*str == '+') ? (str++, 1) : 1;
	double val  = 0;
	while (isdigit(*str)) {
		val = val * 10.0 + (*str++ - '0');
	}
	if (*str == '.') {
		double divisor = 10.0;
		for (str++; isdigit(*str); str++) {
			val += (*str - '0') / divisor;
			divisor *= 10.0;
		}
	}
	if (endptr) {
		*endptr = (char *)str;
	}
	return sign * val;
}

double atof(const char *str) {
	return strtod(str, NULL);
}

int abs(int n) {
	return n < 0 ? -n : n;
}

long long int llabs(long long int n) {
	return n < 0 ? -n : n;
}

void qsort(void *base, size_t num, size_t size, int (*compar)(const void *, const void *)) {
	if (num < 2) {
		return;
	}
	char  *b = base;
	char   tmp[size];
	char  *pivot = b + (num / 2) * size;
	size_t i     = 0;
	size_t j     = num - 1;
	memcpy(tmp, pivot, size);
	while (i <= j) {
		while (compar(b + i * size, tmp) < 0) {
			i++;
		}
		while (j > 0 && compar(b + j * size, tmp) > 0) {
			j--;
		}
		if (i <= j) {
			if (i != j) {
				char t[size];
				memcpy(t, b + i * size, size);
				memcpy(b + i * size, b + j * size, size);
				memcpy(b + j * size, t, size);
			}
			i++;
			if (j == 0) {
				break;
			}
			j--;
		}
	}
	if (j > 0) {
		qsort(b, j + 1, size, compar);
	}
	if (i < num) {
		qsort(b + i * size, num - i, size, compar);
	}
}

static unsigned int seed = 1;

void srand(unsigned int s) {
	seed = s;
}

int rand() {
	seed = seed * 1103515245 + 12345;
	return (seed >> 16) & 0x7FFF;
}
