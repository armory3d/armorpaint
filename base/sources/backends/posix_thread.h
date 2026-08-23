#pragma once

#include <iron_global.h>
#include <pthread.h>

typedef struct {
	pthread_mutex_t mutex;
} iron_mutex_impl_t;

typedef struct {
	void *param;
	void (*thread)(void *param);
	pthread_t pthread;
} iron_thread_impl_t;

#ifdef __TINYC__
#include <stdatomic.h>
#endif

static inline bool iron_atomic_compare_exchange(volatile int32_t *pointer, int32_t old_value, int32_t new_value) {
	return __atomic_compare_exchange(pointer, &old_value, &new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline bool iron_atomic_compare_exchange_pointer(void *volatile *pointer, void *old_value, void *new_value) {
	return __atomic_compare_exchange(pointer, &old_value, &new_value, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static inline int32_t iron_atomic_increment(volatile int32_t *pointer) {
	return __atomic_fetch_add(pointer, 1, __ATOMIC_SEQ_CST);
}

static inline int32_t iron_atomic_decrement(volatile int32_t *pointer) {
	return __atomic_fetch_sub(pointer, 1, __ATOMIC_SEQ_CST);
}

static inline void iron_atomic_exchange(volatile int32_t *pointer, int32_t value) {
	int32_t previous;
	__atomic_exchange(pointer, &value, &previous, __ATOMIC_SEQ_CST);
}

static inline void iron_atomic_exchange_float(volatile float *pointer, float value) {
	int32_t previous;
	__atomic_exchange((volatile int32_t *)pointer, (int32_t *)&value, &previous, __ATOMIC_SEQ_CST);
}

static inline void iron_atomic_exchange_double(volatile double *pointer, double value) {
	int64_t previous;
	__atomic_exchange((volatile int64_t *)pointer, (int64_t *)&value, &previous, __ATOMIC_SEQ_CST);
}

#define IRON_ATOMIC_COMPARE_EXCHANGE(pointer, oldValue, newValue) (iron_atomic_compare_exchange(pointer, oldValue, newValue))

#define IRON_ATOMIC_COMPARE_EXCHANGE_POINTER(pointer, oldValue, newValue) (iron_atomic_compare_exchange_pointer(pointer, oldValue, newValue))

#define IRON_ATOMIC_INCREMENT(pointer) (iron_atomic_increment(pointer))

#define IRON_ATOMIC_DECREMENT(pointer) (iron_atomic_decrement(pointer))

#define IRON_ATOMIC_EXCHANGE_32(pointer, value) (iron_atomic_exchange(pointer, value))

#define IRON_ATOMIC_EXCHANGE_FLOAT(pointer, value) (iron_atomic_exchange_float(pointer, value))

#define IRON_ATOMIC_EXCHANGE_DOUBLE(pointer, value) (iron_atomic_exchange_double(pointer, value))
