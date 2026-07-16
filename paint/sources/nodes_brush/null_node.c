
#include "../global.h"

typedef struct null_node {
	struct logic_node *base;
} null_node_t;

logic_node_value_t *null_node_get(null_node_t *self, i32 from) {
	return NULL;
}

void *null_node_create(ui_node_t *raw, f32_array_t *args) {
	null_node_t *n = GC_ALLOC_INIT(null_node_t, {0});
	n->base        = logic_node_create(n);
	n->base->get   = float_node_get;
	return n;
}

void null_node_init() {

}
