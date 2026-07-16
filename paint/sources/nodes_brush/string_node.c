
#include "../global.h"

typedef struct string_node {
	struct logic_node *base;
	char              *value;
} string_node_t;

logic_node_value_t *string_node_get(string_node_t *self, i32 from) {
	if (self->base->inputs->length > 0) {
		return logic_node_input_get(self->base->inputs->buffer[0]);
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._str = self->value});
		return v;
	}
}

void string_node_set(string_node_t *self, void *value) {
	if (self->base->inputs->length > 0) {
		logic_node_input_set(self->base->inputs->buffer[0], value);
	}
	else {
		self->value = string_copy(value);
	}
}

void *string_node_create(ui_node_t *raw, f32_array_t *args) {
	string_node_t *n = GC_ALLOC_INIT(string_node_t, {0});
	n->base          = logic_node_create(n);
	n->base->get     = string_node_get;
	n->base->set     = string_node_set;
	n->value         = args == NULL ? "" : sys_buffer_to_string(args->buffer);
	return n;
}

void string_node_init() {

}
