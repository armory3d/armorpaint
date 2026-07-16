
#include "../global.h"

typedef struct boolean_node {
	struct logic_node *base;
	bool               value;
} boolean_node_t;

logic_node_value_t *boolean_node_get(boolean_node_t *self, i32 from) {
	if (self->base->inputs->length > 0) {
		return logic_node_input_get(self->base->inputs->buffer[0]);
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = self->value ? 1.0 : 0.0});
		return v;
	}
}

void boolean_node_set(boolean_node_t *self, f32_array_t *value) {
	if (self->base->inputs->length > 0) {
		logic_node_input_set(self->base->inputs->buffer[0], value);
	}
	else {
		self->value = value->buffer[0] > 0.0;
	}
}

void *boolean_node_create(ui_node_t *raw, f32_array_t *args) {
	boolean_node_t *n = GC_ALLOC_INIT(boolean_node_t, {0});
	n->base           = logic_node_create(n);
	n->base->get      = boolean_node_get;
	n->base->set      = boolean_node_set;
	n->value          = args == NULL ? false : args->buffer[0] > 0.0;
	return n;
}

void boolean_node_init() {}
