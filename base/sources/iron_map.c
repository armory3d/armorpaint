
#include "iron_map.h"
#include "iron_alloc.h"
#include "iron_string.h"

static size_t hash(const char *k) {
	// fnv1a
	size_t hash = 0x811c9dc5;
	while (*k) {
		hash ^= (unsigned char)*k++;
		hash *= 0x01000193;
	}
	return hash;
}

static size_t index_set(any_map_t *m, char *k) {
	size_t i = hash(k) & (m->keys->capacity - 1); // % m->keys->capacity
	while (true) {
		if (m->keys->buffer[i] == NULL) {
			m->keys->length++;
			break;
		}
		if (string_equals(k, m->keys->buffer[i])) {
			break;
		}
		++i;
		if (i > m->keys->capacity - 1) {
			i = 0;
		}
	}
	return i;
}

static size_t index_get(string_array_t *keys, char *k) {
	if (k == NULL || keys->capacity == 0) {
		return -1;
	}
	size_t i = hash(k) & (keys->capacity - 1);
	while (!string_equals(k, keys->buffer[i])) {
		if (keys->buffer[i] == NULL) {
			return -1;
		}
		++i;
		if (i > keys->capacity - 1) {
			i = 0;
		}
	}
	return i;
}

static void resize(any_map_t *m, int elem_size) {
	if (m->keys->length < m->keys->capacity * 0.5) {
		return;
	}

	int        cap        = m->keys->capacity == 0 ? 16 : m->keys->capacity * 2;
	any_map_t *tmp        = any_map_create();
	tmp->keys->capacity   = cap;
	tmp->keys->buffer     = calloc(1, cap * sizeof(void *));
	tmp->values->capacity = cap;
	tmp->values->buffer   = calloc(1, cap * elem_size);

	any_array_t *old_keys = map_keys(m);
	for (int i = 0; i < old_keys->length; ++i) {
		char  *k             = old_keys->buffer[i];
		size_t j             = index_set(tmp, k);
		tmp->keys->buffer[j] = k;
		if (elem_size == 8) {
			void **buf = tmp->values->buffer;
			buf[j]     = any_map_get(m, k);
		}
		else {
			int32_t *buf = tmp->values->buffer;
			buf[j]       = i32_map_get(m, k);
		}
	}

	array_free(old_keys);
	free(old_keys);

	array_free(m->keys);
	free(m->keys);
	array_free(m->values);
	free(m->values);
	m->keys   = tmp->keys;
	m->values = tmp->values;
	free(tmp);
}

void i32_map_set(i32_map_t *m, char *k, int v) {
	resize(m, 4);
	size_t i             = index_set(m, k);
	m->keys->buffer[i]   = k;
	m->values->buffer[i] = v;
}

void f32_map_set(f32_map_t *m, char *k, float v) {
	resize(m, 4);
	size_t i             = index_set(m, k);
	m->keys->buffer[i]   = k;
	m->values->buffer[i] = v;
}

void any_map_set(any_map_t *m, char *k, void *v) {
	resize(m, 8);
	size_t i             = index_set(m, k);
	m->keys->buffer[i]   = k;
	m->values->buffer[i] = v;
}

int32_t i32_map_get(i32_map_t *m, char *k) {
	size_t i = index_get(m->keys, k);
	return i == -1 ? -1 : m->values->buffer[i];
}

float f32_map_get(f32_map_t *m, char *k) {
	size_t i = index_get(m->keys, k);
	return i == -1 ? -1.0 : m->values->buffer[i];
}

void *any_map_get(any_map_t *m, char *k) {
	size_t i = index_get(m->keys, k);
	return i == -1 ? NULL : m->values->buffer[i];
}

static int next_pos(any_map_t *m, int i) {
	return i == m->keys->capacity - 1 ? 0 : i + 1;
}

void map_delete(any_map_t *m, char *k) {
	size_t i = index_get(m->keys, k);
	if (i != -1) {
		m->keys->buffer[i]   = NULL;
		m->values->buffer[i] = NULL;
		m->keys->length--;

		int next = next_pos(m, i);
		while (m->keys->buffer[next] != NULL && next != i) {
			int j = index_set(m, m->keys->buffer[next]);
			if (j != next) {
				m->keys->buffer[j]      = m->keys->buffer[next];
				m->values->buffer[j]    = m->values->buffer[next];
				m->keys->buffer[next]   = NULL;
				m->values->buffer[next] = NULL;
			}
			next = next_pos(m, next);
		}
	}
}

any_array_t *map_keys(any_map_t *m) {
	string_array_t *keys = calloc(1, sizeof(string_array_t));
	string_array_resize(keys, m->keys->length);
	for (int i = 0; i < m->keys->capacity; ++i) {
		if (m->keys->buffer[i] != NULL) {
			string_array_push(keys, m->keys->buffer[i]);
		}
	}
	return keys;
}

// Stored keys and values are not owned by the map
void map_free(any_map_t *m) {
	if (m == NULL) {
		return;
	}
	array_free(m->keys);
	free(m->keys);
	array_free(m->values);
	free(m->values);
	free(m);
}

i32_map_t *i32_map_create() {
	i32_map_t *r = calloc(1, sizeof(i32_map_t));
	r->keys      = calloc(1, sizeof(string_array_t));
	r->values    = calloc(1, sizeof(i32_array_t));
	return r;
}

any_map_t *any_map_create() {
	any_map_t *r = calloc(1, sizeof(any_map_t));
	r->keys      = calloc(1, sizeof(string_array_t));
	r->values    = calloc(1, sizeof(any_array_t));
	return r;
}

// imap

void i32_imap_set(i32_imap_t *m, int k, int v) {
	int i = i32_array_index_of(m->keys, k);
	if (i == -1) {
		i32_array_push(m->keys, k);
		i32_array_push(m->values, v);
	}
	else {
		m->keys->buffer[i]   = k;
		m->values->buffer[i] = v;
	}
}

void any_imap_set(any_imap_t *m, int k, void *v) {
	int i = i32_array_index_of(m->keys, k);
	if (i == -1) {
		i32_array_push(m->keys, k);
		any_array_push(m->values, v);
	}
	else {
		m->keys->buffer[i]   = k;
		m->values->buffer[i] = v;
	}
}

int32_t i32_imap_get(i32_imap_t *m, int k) {
	int i = i32_array_index_of(m->keys, k);
	if (i > -1) {
		return m->values->buffer[i];
	}
	return -1;
}

void *any_imap_get(any_imap_t *m, int k) {
	int i = i32_array_index_of(m->keys, k);
	if (i > -1) {
		return m->values->buffer[i];
	}
	return NULL;
}

void imap_delete(any_imap_t *m, int k) {
	int i = i32_array_index_of(m->keys, k);
	if (i > -1) {
		i32_array_splice(m->keys, i, 1);
		array_splice(m->values, i, 1);
	}
}

i32_array_t *imap_keys(any_imap_t *m) {
	return m->keys;
}

i32_imap_t *i32_imap_create() {
	i32_imap_t *r = calloc(1, sizeof(i32_imap_t));
	r->keys       = calloc(1, sizeof(i32_array_t));
	r->values     = calloc(1, sizeof(i32_array_t));
	return r;
}

any_imap_t *any_imap_create() {
	any_imap_t *r = calloc(1, sizeof(any_imap_t));
	r->keys       = calloc(1, sizeof(i32_array_t));
	r->values     = calloc(1, sizeof(any_array_t));
	return r;
}
