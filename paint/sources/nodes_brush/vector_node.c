
#include "../global.h"

typedef struct vector_node {
	struct logic_node  *base;
	vec4_t              value;
	struct gpu_texture *image;
} vector_node_t;

logic_node_value_t *vector_node_get(vector_node_t *self, i32 from) {
	f32 x                 = logic_node_input_get(self->base->inputs->buffer[0])->_f32;
	f32 y                 = logic_node_input_get(self->base->inputs->buffer[1])->_f32;
	f32 z                 = logic_node_input_get(self->base->inputs->buffer[2])->_f32;
	self->value.x         = x;
	self->value.y         = y;
	self->value.z         = z;
	logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._vec4 = self->value});
	return v;
}

gpu_texture_t *vector_node_get_as_image(vector_node_t *self, i32 from) {
	// let x: f32 = logic_node_input_get(self.base.inputs[0]);
	// let y: f32 = logic_node_input_get(self.base.inputs[1]);
	// let z: f32 = logic_node_input_get(self.base.inputs[2]);
	if (self->image != NULL) {
		gpu_delete_texture(self->image);
	}
	buffer_t     *b  = buffer_create(16);
	float_node_t *n0 = self->base->inputs->buffer[0]->node;
	float_node_t *n1 = self->base->inputs->buffer[1]->node;
	float_node_t *n2 = self->base->inputs->buffer[2]->node;
	buffer_set_f32(b, 0, n0->value);
	buffer_set_f32(b, 4, n1->value);
	buffer_set_f32(b, 8, n2->value);
	buffer_set_f32(b, 12, 1.0);
	self->image = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA128);
	return self->image;
}

void vector_node_set(vector_node_t *self, f32_array_t *value) {
	logic_node_input_set(self->base->inputs->buffer[0], f32_array_create_x(value->buffer[0]));
	logic_node_input_set(self->base->inputs->buffer[1], f32_array_create_x(value->buffer[1]));
	logic_node_input_set(self->base->inputs->buffer[2], f32_array_create_x(value->buffer[2]));
}

void *vector_node_create(ui_node_t *raw, f32_array_t *args) {
	vector_node_t *n      = GC_ALLOC_INIT(vector_node_t, {0});
	n->base               = logic_node_create(n);
	n->base->get          = vector_node_get;
	n->base->get_as_image = vector_node_get_as_image;
	n->base->set          = vector_node_set;
	n->value              = (vec4_t){0.0, 0.0, 0.0, 1.0};

	if (args != NULL) {
		logic_node_add_input(n->base, float_node_create(NULL, f32_array_create_x(args->buffer[0])), 0);
		logic_node_add_input(n->base, float_node_create(NULL, f32_array_create_x(args->buffer[1])), 0);
		logic_node_add_input(n->base, float_node_create(NULL, f32_array_create_x(args->buffer[2])), 0);
	}

	return n;
}

void vector_node_init() {
	ui_node_t *vector_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Vector"),
	                              .type   = "vector_node",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff4982a0,
	                              .inputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("X"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Y"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Z"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  3),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Vector"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = 0xff6363c7,
	                                                                       .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw((void *[]){}, 0),
	                              .width   = 0,
	                              .flags   = 0});

	any_array_push(nodes_brush_category0, vector_node_def);
	any_map_set(nodes_brush_creates, "vector_node", vector_node_create);
}
