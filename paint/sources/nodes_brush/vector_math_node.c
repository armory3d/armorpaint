
#include "../global.h"

typedef struct vector_math_node {
	struct logic_node *base;
	char              *operation;
	vec4_t             v;
} vector_math_node_t;

logic_node_value_t *vector_math_node_get(vector_math_node_t *self, i32 from) {
	vec4_t v1 = logic_node_input_get(self->base->inputs->buffer[0])->_vec4;
	vec4_t v2 = logic_node_input_get(self->base->inputs->buffer[1])->_vec4;
	self->v   = v1;
	f32   f   = 0.0;
	char *op  = self->operation;
	if (string_equals(op, "Add")) {
		self->v = vec4_add(self->v, v2);
	}
	else if (string_equals(op, "Subtract")) {
		self->v = vec4_sub(self->v, v2);
	}
	else if (string_equals(op, "Average")) {
		self->v = vec4_add(self->v, v2);
		self->v.x *= 0.5;
		self->v.y *= 0.5;
		self->v.z *= 0.5;
	}
	else if (string_equals(op, "Dot Product")) {
		f       = vec4_dot(self->v, v2);
		self->v = (vec4_t){f, f, f, 1.0};
	}
	else if (string_equals(op, "Cross Product")) {
		self->v = vec4_cross(self->v, v2);
	}
	else if (string_equals(op, "Normalize")) {
		self->v = vec4_norm(self->v);
	}
	else if (string_equals(op, "Multiply")) {
		self->v.x *= v2.x;
		self->v.y *= v2.y;
		self->v.z *= v2.z;
	}
	else if (string_equals(op, "Divide")) {
		self->v.x /= v2.x == 0.0 ? 0.000001 : v2.x;
		self->v.y /= v2.y == 0.0 ? 0.000001 : v2.y;
		self->v.z /= v2.z == 0.0 ? 0.000001 : v2.z;
	}
	else if (string_equals(op, "Length")) {
		f       = vec4_len(self->v);
		self->v = (vec4_t){f, f, f, 1.0};
	}
	else if (string_equals(op, "Distance")) {
		f       = vec4_dist(self->v, v2);
		self->v = (vec4_t){f, f, f, 1.0};
	}
	else if (string_equals(op, "Project")) {
		self->v = v2;
		self->v = vec4_mult(self->v, vec4_dot(v1, v2) / (float)vec4_dot(v2, v2));
	}
	else if (string_equals(op, "Reflect")) {
		vec4_t tmp = (vec4_t){0.0, 0.0, 0.0, 1.0};
		tmp        = v2;
		tmp        = vec4_norm(tmp);
		self->v    = vec4_reflect(self->v, tmp);
	}
	else if (string_equals(op, "Scale")) {
		self->v.x *= v2.x;
		self->v.y *= v2.x;
		self->v.z *= v2.x;
	}
	else if (string_equals(op, "Absolute")) {
		self->v.x = math_abs(self->v.x);
		self->v.y = math_abs(self->v.y);
		self->v.z = math_abs(self->v.z);
	}
	else if (string_equals(op, "Minimum")) {
		self->v.x = math_min(v1.x, v2.x);
		self->v.y = math_min(v1.y, v2.y);
		self->v.z = math_min(v1.z, v2.z);
	}
	else if (string_equals(op, "Maximum")) {
		self->v.x = math_max(v1.x, v2.x);
		self->v.y = math_max(v1.y, v2.y);
		self->v.z = math_max(v1.z, v2.z);
	}
	else if (string_equals(op, "Floor")) {
		self->v.x = math_floor(v1.x);
		self->v.y = math_floor(v1.y);
		self->v.z = math_floor(v1.z);
	}
	else if (string_equals(op, "Ceil")) {
		self->v.x = math_ceil(v1.x);
		self->v.y = math_ceil(v1.y);
		self->v.z = math_ceil(v1.z);
	}
	else if (string_equals(op, "Fraction")) {
		self->v.x = v1.x - math_floor(v1.x);
		self->v.y = v1.y - math_floor(v1.y);
		self->v.z = v1.z - math_floor(v1.z);
	}
	else if (string_equals(op, "Modulo")) {
		self->v.x = math_fmod(v1.x, v2.x);
		self->v.y = math_fmod(v1.y, v2.y);
		self->v.z = math_fmod(v1.z, v2.z);
	}
	else if (string_equals(op, "Snap")) {
		self->v.x = math_floor(v1.x / (float)v2.x) * v2.x;
		self->v.y = math_floor(v1.y / (float)v2.y) * v2.y;
		self->v.z = math_floor(v1.z / (float)v2.z) * v2.z;
	}
	else if (string_equals(op, "Sine")) {
		self->v.x = math_sin(v1.x);
		self->v.y = math_sin(v1.y);
		self->v.z = math_sin(v1.z);
	}
	else if (string_equals(op, "Cosine")) {
		self->v.x = math_cos(v1.x);
		self->v.y = math_cos(v1.y);
		self->v.z = math_cos(v1.z);
	}
	else if (string_equals(op, "Tangent")) {
		self->v.x = math_tan(v1.x);
		self->v.y = math_tan(v1.y);
		self->v.z = math_tan(v1.z);
	}

	if (from == 0) {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._vec4 = self->v});
		return v;
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = f});
		return v;
	}
}

void *vector_math_node_create(ui_node_t *raw, f32_array_t *args) {
	vector_math_node_t *n = GC_ALLOC_INIT(vector_math_node_t, {0});
	n->base               = logic_node_create(n);
	n->base->get          = vector_math_node_get;
	n->v                  = (vec4_t){0.0, 0.0, 0.0, 1.0};
	return n;
}

void vector_math_node_init() {
	ui_node_t *vector_math_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Vector Math"),
	                              .type   = "vector_math_node",
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
	                                  2),
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
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Value"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(0.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  2),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_button_t, {.name          = _tr("operation"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(
                                                                               "Add\nSubtract\nMultiply\nDivide\nAverage\nCross Product\nProject\nReflect\nDot "
	                                                                                    "Product\nDistance\nLength\nScale\nNormalize\nAbsolute\nMinimum\nMaximum\nFloor"
	                                                                                    "\nCeil\nFraction\nModulo\nSnap\nSine\nCosine\nTangent"),
	                                                                       .min       = 0.0,
	                                                                       .max       = 1.0,
	                                                                       .precision = 100,
	                                                                       .height    = 0}),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_brush_category0, vector_math_node_def);
	any_map_set(nodes_brush_creates, "vector_math_node", vector_math_node_create);
}
