
#include "../global.h"

typedef struct color_node {
	struct logic_node  *base;
	vec4_t              value;
	struct gpu_texture *image;
} color_node_t;

logic_node_value_t *color_node_get(color_node_t *self, i32 from) {
	if (self->base->inputs->length > 0) {
		return logic_node_input_get(self->base->inputs->buffer[0]);
	}
	else {
		logic_node_value_t *v = GC_ALLOC_INIT(logic_node_value_t, {._vec4 = self->value});
		return v;
	}
}

gpu_texture_t *color_node_get_as_image(color_node_t *self, i32 from) {
	if (self->base->inputs->length > 0) {
		return logic_node_input_get_as_image(self->base->inputs->buffer[0]);
	}
	if (self->image != NULL) {
		gpu_delete_texture(self->image);
	}
	buffer_t *b = buffer_create(16);
	buffer_set_f32(b, 0, self->value.x);
	buffer_set_f32(b, 4, self->value.y);
	buffer_set_f32(b, 8, self->value.z);
	buffer_set_f32(b, 12, self->value.w);
	self->image = gpu_create_texture_from_bytes(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA128);
	return self->image;
}

void color_node_set(color_node_t *self, f32_array_t *value) {
	if (self->base->inputs->length > 0) {
		logic_node_input_set(self->base->inputs->buffer[0], value);
	}
	else {
		self->value.x = value->buffer[0];
		self->value.y = value->buffer[1];
		self->value.z = value->buffer[2];
		self->value.w = value->buffer[3];
	}
}

void *color_node_create(ui_node_t *raw, f32_array_t *args) {
	f32           r       = args == NULL ? 0.8 : args->buffer[0];
	f32           g       = args == NULL ? 0.8 : args->buffer[1];
	f32           b       = args == NULL ? 0.8 : args->buffer[2];
	f32           a       = args == NULL ? 1.0 : args->buffer[3];
	color_node_t *n       = GC_ALLOC_INIT(color_node_t, {0});
	n->base               = logic_node_create(n);
	n->base->get          = color_node_get;
	n->base->get_as_image = color_node_get_as_image;
	n->base->set          = color_node_set;
	n->value              = (vec4_t){r, g, b, a};
	return n;
}

void color_node_init() {

}
