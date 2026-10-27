
#include "../global.h"

static f32 mesh_math_node_op(i32 op, f32 a, f32 b) {
	switch (op) {
	case 1:
		return a - b;
	case 2:
		return a * b;
	case 3:
		return a / (b == 0.0 ? 0.000001 : b);
	case 4:
		return math_pow(a, b);
	case 5:
		return math_min(a, b);
	case 6:
		return math_max(a, b);
	case 7:
		return b == 0.0 ? 0.0 : math_fmod(a, b);
	case 8:
		return math_sin(a);
	case 9:
		return math_cos(a);
	case 10:
		return math_abs(a);
	case 11:
		return math_round(a);
	case 12:
		return math_floor(a);
	default:
		return a + b;
	}
}

vec4_t mesh_math_node_vector(ui_node_t *node) {
	f32 f = mesh_math_node_op(nodes_mesh_button(node, 0), nodes_mesh_value(node, 0), nodes_mesh_value(node, 1));
	return (vec4_t){f, f, f, 1.0};
}

void mesh_math_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_mesh_utilities, _tr("Math"), "MESH_MATH", 0xff62676d, "VALUE");
	nodes_mesh_def_button(n, _tr("Operation"), "ENUM", 0,
	                      "Add\nSubtract\nMultiply\nDivide\nPower\nMinimum\nMaximum\nModulo\nSine\nCosine\nAbsolute\nRound\nFloor");
	nodes_mesh_def_value(n, _tr("Value"), 0.5, -10.0, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Value"), 0.5, -10.0, 10.0, 100);
	any_map_set(nodes_mesh_node_vectors, "MESH_MATH", mesh_math_node_vector);
}
