
#include "../global.h"

typedef struct tex_image_node {
	struct logic_node *base;
	struct ui_node    *raw;
} tex_image_node_t;

logic_node_value_t *tex_image_node_get(tex_image_node_t *self, i32 from) {
	string_array_t *ar   = ui_nodes_enum_texts(self->raw->type);
	i32             i    = self->raw->buttons->buffer[0]->default_value->buffer[0];
	char           *file = ar->buffer[i];

	if (from == 0) {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._str = string("%s.rgb", file)});
		return v;
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._str = string("%s.a", file)});
		return v;
	}
}

void *tex_image_node_create(ui_node_t *raw, f32_array_t *args) {
	tex_image_node_t *n = GC_ALLOC_INIT(tex_image_node_t, {0});
	n->base             = logic_node_create(n);
	n->base->get        = tex_image_node_get;
	n->raw              = raw;
	return n;
}

void tex_image_node_init() {
	ui_node_t *tex_image_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id   = 0,
	                              .name = _tr("Image Texture"),
	                              // type: "tex_image_node",
	                              .type   = "TEX_IMAGE",
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
	                                                                       .name          = _tr("Color"),
	                                                                       .type          = "VALUE", // Match brush output socket type
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Alpha"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  2),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_button_t, {.name          = _tr("file"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string(""),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                      GC_ALLOC_INIT(ui_node_button_t, {.name          = _tr("color_space"),
	                                                                       .type          = "ENUM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = u8_array_create_from_string("linear\nsrgb"),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  2),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_brush_category0, tex_image_node_def);
	// any_map_set(nodes_brush_creates, "tex_image_node", tex_image_node_create);
	any_map_set(nodes_brush_creates, "TEX_IMAGE", tex_image_node_create);
}
