
#include "global.h"

logic_node_t *logic_node_create(logic_node_ext_t *ext) {
	logic_node_t *n = ALLOC_INIT(logic_node_t, {0});
	n->inputs       = any_array_create_from_raw((void *[]){}, 0);
	n->outputs      = any_array_create_from_raw((void *[]){}, 0);
	n->ext          = ext;
	return n;
}

logic_node_input_t *logic_node_input_create(logic_node_ext_t *node, i32 from) {
	logic_node_input_t *inp = ALLOC_INIT(logic_node_input_t, {0});
	inp->node               = node;
	inp->from               = from;
	return inp;
}

void logic_node_add_input(logic_node_t *self, logic_node_ext_t *node, i32 from) {
	any_array_push(self->inputs, logic_node_input_create(node, from));
}

void logic_node_add_outputs(logic_node_t *self, logic_node_t_array_t *nodes) {
	any_array_push(self->outputs, nodes);
}

void *logic_node_get(logic_node_t *self, i32 from) {
	if (self->get != NULL) {
		return self->get(self->ext, from);
	}
	return NULL;
}

gpu_texture_t *logic_node_get_as_image(logic_node_t *self, i32 from) {
	if (self->get_as_image != NULL) {
		return self->get_as_image(self->ext, from);
	}
	return NULL;
}

void logic_node_set(logic_node_t *self, void *value) {
	if (self->set != NULL) {
		self->set(self->ext, value);
	}
}

logic_node_value_t *logic_node_input_get(logic_node_input_t *self) {
	return logic_node_get(self->node->base, self->from);
}

gpu_texture_t *logic_node_input_get_as_image(logic_node_input_t *self) {
	return logic_node_get_as_image(self->node->base, self->from);
}

void logic_node_input_set(logic_node_input_t *self, f32_array_t *value) {
	logic_node_set(self->node->base, value);
}
