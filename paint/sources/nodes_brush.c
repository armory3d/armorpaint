
#include "global.h"

void nodes_brush_init() {
	if (nodes_brush_list != NULL) {
		return;
	}

	gc_unroot(nodes_brush_creates);
	nodes_brush_creates = any_map_create();
	gc_root(nodes_brush_creates);

	gc_unroot(nodes_brush_category0);
	nodes_brush_category0 = any_array_create(0);
	gc_root(nodes_brush_category0);

	brush_output_node_init();
	tex_image_node_init();
	input_node_init();
	math_node_init();
	random_node_init();
	separate_vector_node_init();
	time_node_init();
	float_node_init();
	vector_node_init();
	vector_math_node_init();

	gc_unroot(nodes_brush_list);
	nodes_brush_list = any_array_create_from_raw(
	    (void *[]){
	        nodes_brush_category0,
	    },
	    1);
	gc_root(nodes_brush_list);
}

ui_node_t *nodes_brush_create_node(char *node_type) {
	for (i32 i = 0; i < nodes_brush_list->length; ++i) {
		ui_node_t_array_t *c = nodes_brush_list->buffer[i];
		for (i32 i = 0; i < c->length; ++i) {
			ui_node_t *n = c->buffer[i];
			if (string_equals(n->type, node_type)) {
				ui_node_canvas_t *canvas = g_context->brush->canvas;
				ui_nodes_t       *nodes  = g_context->brush->nodes;
				ui_node_t        *node   = ui_nodes_make_node(n, nodes, canvas);
				any_array_push(canvas->nodes, node);
				return node;
			}
		}
	}
	return NULL;
}
