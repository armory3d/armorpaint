
#include "../global.h"

logic_node_value_t *float_node_get(float_node_t *self, i32 from) {
	if (self->base->inputs->length > 0) {
		return logic_node_input_get(self->base->inputs->buffer[0]);
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = self->value});
		return v;
	}
}

gpu_texture_t *float_node_get_as_image(float_node_t *self, i32 from) {
	if (self->base->inputs->length > 0) {
		return logic_node_input_get_as_image(self->base->inputs->buffer[0]);
	}
	if (self->image != NULL) {
		gpu_delete_texture(self->image);
	}
	buffer_t *b = buffer_create(16);
	buffer_set_f32(b, 0, self->value);
	buffer_set_f32(b, 4, self->value);
	buffer_set_f32(b, 8, self->value);
	buffer_set_f32(b, 12, 1.0);
	self->image = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA128);
	return self->image;
}

void float_node_set(float_node_t *self, f32_array_t *value) {
	if (self->base->inputs->length > 0) {
		logic_node_input_set(self->base->inputs->buffer[0], value);
	}
	else {
		self->value = value->buffer[0];
	}
}

void *float_node_create(ui_node_t *raw, f32_array_t *args) {
	float_node_t *n       = GC_ALLOC_INIT(float_node_t, {0});
	n->base               = logic_node_create(n);
	n->base->get          = float_node_get;
	n->base->get_as_image = float_node_get_as_image;
	n->base->set          = float_node_set;
	n->value              = args == NULL ? 0.5 : args->buffer[0];
	return n;
}

void float_node_init() {
	ui_node_t *float_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Value"),
	                              .type   = "float_node",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xffb34f5a,
	                              .inputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Value"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.5),
	                                                                       .min           = 0.0,
	                                                                       .max           = 10.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Value"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.5),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw((void *[]){}, 0),
	                              .width   = 0,
	                              .flags   = 0});

	any_array_push(nodes_brush_category0, float_node_def);
	any_map_set(nodes_brush_creates, "float_node", float_node_create);
}
