
#include "../global.h"

typedef struct time_node {
	struct logic_node *base;
} time_node_t;

logic_node_value_t *time_node_get(time_node_t *self, i32 from) {
	if (from == 0) {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = sys_time()});
		return v;
	}
	else if (from == 1) {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = sys_delta()});
		return v;
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = g_context->brush_time});
		return v;
	}
}

void *time_node_create(ui_node_t *raw, f32_array_t *args) {
	time_node_t *n = GC_ALLOC_INIT(time_node_t, {0});
	n->base        = logic_node_create(n);
	n->base->get   = time_node_get;
	return n;
}

void time_node_init() {
	ui_node_t *time_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id      = 0,
	                              .name    = _tr("Time"),
	                              .type    = "time_node",
	                              .x       = 0,
	                              .y       = 0,
	                              .color   = 0xff4982a0,
	                              .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Time"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Delta"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Brush"),
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

	any_array_push(nodes_brush_category0, time_node_def);
	any_map_set(nodes_brush_creates, "time_node", time_node_create);
}
