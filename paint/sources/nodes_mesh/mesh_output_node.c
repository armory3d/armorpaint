
#include "../global.h"

void mesh_output_node_init() {
	// Placed once per canvas
	nodes_mesh_output_def = nodes_mesh_def(NULL, _tr("Mesh Output"), "MESH_OUTPUT", 0xffb34f5a, NULL);
	nodes_mesh_def_mesh(nodes_mesh_output_def);
}
