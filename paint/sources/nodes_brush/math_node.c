
#include "../global.h"

typedef struct math_node {
	struct logic_node *base;
	char              *operation;
	bool               use_clamp;
} math_node_t;

logic_node_value_t *math_node_get(math_node_t *self, i32 from) {
	f32   v1 = logic_node_input_get(self->base->inputs->buffer[0])->_f32;
	f32   v2 = logic_node_input_get(self->base->inputs->buffer[1])->_f32;
	f32   f  = 0.0;
	char *op = self->operation;
	if (string_equals(op, "Add")) {
		f = v1 + v2;
	}
	else if (string_equals(op, "Multiply")) {
		f = v1 * v2;
	}
	else if (string_equals(op, "Sine")) {
		f = math_sin(v1);
	}
	else if (string_equals(op, "Cosine")) {
		f = math_cos(v1);
	}
	else if (string_equals(op, "Max")) {
		f = math_max(v1, v2);
	}
	else if (string_equals(op, "Min")) {
		f = math_min(v1, v2);
	}
	else if (string_equals(op, "Absolute")) {
		f = math_abs(v1);
	}
	else if (string_equals(op, "Subtract")) {
		f = v1 - v2;
	}
	else if (string_equals(op, "Divide")) {
		f = v1 / (float)(v2 == 0.0 ? 0.000001 : v2);
	}
	else if (string_equals(op, "Tangent")) {
		f = math_tan(v1);
	}
	else if (string_equals(op, "Arcsine")) {
		f = math_asin(v1);
	}
	else if (string_equals(op, "Arccosine")) {
		f = math_acos(v1);
	}
	else if (string_equals(op, "Arctangent")) {
		f = math_atan(v1);
	}
	else if (string_equals(op, "Arctan2")) {
		f = math_atan2(v2, v1);
	}
	else if (string_equals(op, "Power")) {
		f = math_pow(v1, v2);
	}
	else if (string_equals(op, "Logarithm")) {
		f = math_log(v1);
	}
	else if (string_equals(op, "Round")) {
		f = math_round(v1);
	}
	else if (string_equals(op, "Floor")) {
		f = math_floor(v1);
	}
	else if (string_equals(op, "Ceil")) {
		f = math_ceil(v1);
	}
	else if (string_equals(op, "Truncate")) {
		f = math_floor(v1);
	}
	else if (string_equals(op, "Fraction")) {
		f = v1 - math_floor(v1);
	}
	else if (string_equals(op, "Less Than")) {
		f = v1 < v2 ? 1.0 : 0.0;
	}
	else if (string_equals(op, "Greater Than")) {
		f = v1 > v2 ? 1.0 : 0.0;
	}
	else if (string_equals(op, "Modulo")) {
		f = math_fmod(v1, v2);
	}
	else if (string_equals(op, "Snap")) {
		f = math_floor(v1 / (float)v2) * v2;
	}
	else if (string_equals(op, "Square Root")) {
		f = math_sqrt(v1);
	}
	else if (string_equals(op, "Inverse Square Root")) {
		f = 1.0 / (float)math_sqrt(v1);
	}
	else if (string_equals(op, "Exponent")) {
		f = math_exp(v1);
	}
	else if (string_equals(op, "Sign")) {
		f = v1 > 0 ? 1.0 : (v1 < 0 ? -1.0 : 0);
	}
	else if (string_equals(op, "Ping-Pong")) {
		f = (v2 != 0.0) ? v2 - math_abs(math_fmod(math_abs(v1), (2 * v2)) - v2) : 0.0;
	}
	else if (string_equals(op, "Hyperbolic Sine")) {
		f = (math_exp(v1) - math_exp(-v1)) / 2.0;
	}
	else if (string_equals(op, "Hyperbolic Cosine")) {
		f = (math_exp(v1) + math_exp(-v1)) / 2.0;
	}
	else if (string_equals(op, "Hyperbolic Tangent")) {
		f = 1.0 - (2.0 / (float)(math_exp(2 * v1) + 1));
	}
	else if (string_equals(op, "To Radians")) {
		f = v1 / 180.0 * math_pi();
	}
	else if (string_equals(op, "To Degrees")) {
		f = v1 / (float)math_pi() * 180.0;
	}

	if (self->use_clamp) {
		f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
	}

	logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._f32 = f});
	return v;
}

void *math_node_create(ui_node_t *raw, f32_array_t *args) {
	math_node_t *n = GC_ALLOC_INIT(math_node_t, {0});
	n->base        = logic_node_create(n);
	n->base->get   = math_node_get;
	return n;
}

void math_node_init() {
	ui_node_t *math_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Math"),
	                              .type   = "math_node",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff4982a0,
	                              .inputs = any_array_create_from_raw(
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
	                                  2),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
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
	                                  1),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_button_t, {.name          = _tr("operation"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(
                                                                               "Add\nSubtract\nMultiply\nDivide\nPower\nLogarithm\nSquare Root\nInverse "
	                                                                                    "Square Root\nAbsolute\nExponent\nMinimum\nMaximum\nLess Than\nGreater "
	                                                                                    "Than\nSign\nRound\nFloor\nCeil\nTruncate\nFraction\nModulo\nSnap\nPing-"
	                                                                                    "Pong\nSine\nCosine\nTangent\nArcsine\nArccosine\nArctangent\nArctan2\nHyperb"
	                                                                                    "olic Sine\nHyperbolic Cosine\nHyperbolic Tangent\nTo Radians\nTo Degrees"),
	                                                                       .min       = 0.0,
	                                                                       .max       = 1.0,
	                                                                       .precision = 100,
	                                                                       .height    = 0}),
	                                      GC_ALLOC_INIT(ui_node_button_t, {.name          = _tr("Clamp"),
	                                                                       .type          = "BOOL",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  2),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_brush_category0, math_node_def);
	any_map_set(nodes_brush_creates, "math_node", math_node_create);
}
