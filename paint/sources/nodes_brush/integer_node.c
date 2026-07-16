
#include "../global.h"

typedef struct integer_node {
	struct logic_node *base;
	i32                value;
} integer_node_t;

logic_node_value_t *integer_node_get(integer_node_t *self, i32 from) {
	if (self->base->inputs->length > 0) {
		return logic_node_input_get(self->base->inputs->buffer[0]);
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = self->value});
		return v;
	}
}

void integer_node_set(integer_node_t *self, f32_array_t *value) {
	if (self->base->inputs->length > 0) {
		logic_node_input_set(self->base->inputs->buffer[0], value);
	}
	else {
		self->value = value->buffer[0];
	}
}

void *integer_node_create(ui_node_t *raw, f32_array_t *args) {
	float_node_t *n = GC_ALLOC_INIT(float_node_t, {0});
	n->base         = logic_node_create(n);
	n->base->get    = integer_node_get;
	n->base->set    = integer_node_set;
	n->value        = args == NULL ? 0 : args->buffer[0];
	return n;
}

void integer_node() {

}
