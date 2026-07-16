
#include "../global.h"

typedef struct separate_vector_node {
	struct logic_node *base;
} separate_vector_node_t;

logic_node_value_t *separate_vector_node_get(separate_vector_node_t *self, i32 from) {
	vec4_t vector = logic_node_input_get(self->base->inputs->buffer[0])->_vec4;
	if (from == 0) {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = vector.x});
		return v;
	}
	else if (from == 1) {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = vector.y});
		return v;
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = vector.z});
		return v;
	}
}

void *separate_vector_node_create(ui_node_t *raw, f32_array_t *args) {
	separate_vector_node_t *n = GC_ALLOC_INIT(separate_vector_node_t, {0});
	n->base                   = logic_node_create(n);
	n->base->get              = separate_vector_node_get;
	return n;
}

void separate_vector_node_init() {
	ui_node_t *separate_vector_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Separate Vector"),
	                              .type   = "separate_vector_node",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff4982a0,
	                              .inputs = any_array_create_from_raw(
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
	                              .outputs = any_array_create_from_raw(
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
	                              .buttons = any_array_create_from_raw((void *[]){}, 0),
	                              .width   = 0,
	                              .flags   = 0});

	any_array_push(nodes_brush_category0, separate_vector_node_def);
	any_map_set(nodes_brush_creates, "separate_vector_node", separate_vector_node_create);
}
