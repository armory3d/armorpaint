#include "../global.h"

void mesh_layer_mask_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_input, _tr("Layer Mask"), "LAYER_MASK", 0xff4982a0, "VALUE");
	nodes_mesh_def_button(n, _tr("Layer"), "ENUM", 0, "");
	n->buttons->buffer[0]->output = -1;
	any_map_set(nodes_mesh_node_vectors, "LAYER_MASK", mesh_image_node_vector);
}
