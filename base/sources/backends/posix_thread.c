#include <assert.h>
#include <iron_thread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>

void iron_mutex_init(iron_mutex_t *mutex) {
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&mutex->impl.mutex, &attr);
}

void iron_mutex_destroy(iron_mutex_t *mutex) {
	pthread_mutex_destroy(&mutex->impl.mutex);
}

bool iron_mutex_try_to_lock(iron_mutex_t *mutex) {
	return pthread_mutex_trylock(&mutex->impl.mutex) == 0;
}

void iron_mutex_lock(iron_mutex_t *mutex) {
	pthread_mutex_lock(&mutex->impl.mutex);
}

void iron_mutex_unlock(iron_mutex_t *mutex) {
	pthread_mutex_unlock(&mutex->impl.mutex);
}

#if !defined(IRON_IOS) && !defined(IRON_MACOS)

struct thread_start {
	void (*thread)(void *param);
	void *param;
};

#define THREAD_STARTS 64
static struct thread_start starts[THREAD_STARTS];
static int                 thread_start_index = 0;

static void *ThreadProc(void *arg) {
	intptr_t start_index = (intptr_t)arg;
	starts[start_index].thread(starts[start_index].param);
	pthread_exit(NULL);
	return NULL;
}

void iron_thread_init(iron_thread_t *t, void (*thread)(void *param), void *param) {
	t->impl.param  = param;
	t->impl.thread = thread;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	// pthread_attr_setstacksize(&attr, 1024 * 64);
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = 0;
	pthread_attr_setschedparam(&attr, &sp);
	intptr_t start_index = thread_start_index++;
	if (thread_start_index >= THREAD_STARTS) {
		thread_start_index = 0;
	}
	starts[start_index].thread = thread;
	starts[start_index].param  = param;
	int ret                    = pthread_create(&t->impl.pthread, &attr, &ThreadProc, (void *)start_index);
	assert(ret == 0);
	pthread_attr_destroy(&attr);
}

void iron_thread_wait_and_destroy(iron_thread_t *thread) {
	int ret;
	do {
		ret = pthread_join(thread->impl.pthread, NULL);
	} while (ret != 0);
}

bool iron_thread_try_to_destroy(iron_thread_t *thread) {
	return pthread_join(thread->impl.pthread, NULL) == 0;
}

void iron_threads_init() {}

void iron_threads_quit() {}

// Alternatively _GNU_SOURCE can be defined to make
// the headers declare it but let's not make it too
// easy to write Linux-specific POSIX-code
int pthread_setname_np(pthread_t thread, const char *name);
#endif

void iron_thread_set_name(const char *name) {
#if !defined(IRON_IOS) && !defined(IRON_MACOS)
	pthread_setname_np(pthread_self(), name);
#else
	pthread_setname_np(name);
#endif
}
