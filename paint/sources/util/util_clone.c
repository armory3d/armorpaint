
#include "../global.h"

f32_array_t *util_clone_f32_array(f32_array_t *f32a) {
	if (f32a == NULL) {
		return NULL;
	}
	return f32_array_create_from_array(f32a);
}

u8_array_t *util_clone_u8_array(u8_array_t *u8a) {
	if (u8a == NULL) {
		return NULL;
	}
	return u8_array_create_from_array(u8a);
}

ui_node_socket_t_array_t *util_clone_canvas_sockets(ui_node_socket_t_array_t *sockets) {
	if (sockets == NULL) {
		return NULL;
	}
	ui_node_socket_t_array_t *r = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < sockets->length; ++i) {
		ui_node_socket_t *s = ALLOC_INIT(ui_node_socket_t, {0});
		s->id               = sockets->buffer[i]->id;
		s->node_id          = sockets->buffer[i]->node_id;
		s->name             = string_copy(sockets->buffer[i]->name);
		s->type             = string_copy(sockets->buffer[i]->type);
		s->color            = sockets->buffer[i]->color;
		s->default_value    = util_clone_f32_array(sockets->buffer[i]->default_value);
		s->min              = sockets->buffer[i]->min;
		s->max              = sockets->buffer[i]->max;
		s->precision        = sockets->buffer[i]->precision;
		s->display          = sockets->buffer[i]->display;
		any_array_push(r, s);
	}
	return r;
}

ui_node_button_t_array_t *util_clone_canvas_buttons(ui_node_button_t_array_t *buttons) {
	if (buttons == NULL) {
		return NULL;
	}
	ui_node_button_t_array_t *r = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < buttons->length; ++i) {
		ui_node_button_t *b = ALLOC_INIT(ui_node_button_t, {0});
		b->name             = string_copy(buttons->buffer[i]->name);
		b->type             = string_copy(buttons->buffer[i]->type);
		b->output           = buttons->buffer[i]->output;
		b->default_value    = util_clone_f32_array(buttons->buffer[i]->default_value);
		b->data             = util_clone_u8_array(buttons->buffer[i]->data);
		b->min              = buttons->buffer[i]->min;
		b->max              = buttons->buffer[i]->max;
		b->precision        = buttons->buffer[i]->precision;
		b->height           = buttons->buffer[i]->height;
		any_array_push(r, b);
	}
	return r;
}

ui_node_t *util_clone_canvas_node(ui_node_t *n) {
	if (n == NULL) {
		return NULL;
	}
	ui_node_t *r = ALLOC_INIT(ui_node_t, {0});
	r->id        = n->id;
	r->name      = string_copy(n->name);
	r->type      = string_copy(n->type);
	r->x         = n->x;
	r->y         = n->y;
	r->color     = n->color;

	u32 _length = 0;
	if (n->inputs != NULL && n->inputs->length < 9 && string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
		// Base workflow
		_length           = n->inputs->length;
		n->inputs->length = 9;
	}
	r->inputs = util_clone_canvas_sockets(n->inputs);
	if (_length != 0) {
		n->inputs->length = _length;
	}

	r->outputs = util_clone_canvas_sockets(n->outputs);
	r->buttons = util_clone_canvas_buttons(n->buttons);
	r->width   = n->width;
	r->flags   = n->flags;
	return r;
}

ui_node_t_array_t *util_clone_canvas_nodes(ui_node_t_array_t *nodes) {
	if (nodes == NULL) {
		return NULL;
	}
	ui_node_t_array_t *r = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = util_clone_canvas_node(nodes->buffer[i]);
		any_array_push(r, n);
	}
	return r;
}

ui_node_link_t_array_t *util_clone_canvas_links(ui_node_link_t_array_t *links) {
	if (links == NULL) {
		return NULL;
	}
	ui_node_link_t_array_t *r = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < links->length; ++i) {
		ui_node_link_t *l = ALLOC_INIT(ui_node_link_t, {0});
		l->id             = links->buffer[i]->id;
		l->from_id        = links->buffer[i]->from_id;
		l->from_socket    = links->buffer[i]->from_socket;
		l->to_id          = links->buffer[i]->to_id;
		l->to_socket      = links->buffer[i]->to_socket;
		any_array_push(r, l);
	}
	return r;
}

ui_node_canvas_t *util_clone_canvas(ui_node_canvas_t *c) {
	if (c == NULL) {
		return NULL;
	}
	ui_node_canvas_t *r = ALLOC_INIT(ui_node_canvas_t, {0});
	r->name             = string_copy(c->name);
	r->nodes            = util_clone_canvas_nodes(c->nodes);
	r->links            = util_clone_canvas_links(c->links);
	return r;
}

obj_t *util_clone_obj(obj_t *o) {
	if (o == NULL) {
		return NULL;
	}
	obj_t *r        = ALLOC_INIT(obj_t, {0});
	r->name         = string_copy(o->name);
	r->type         = string_copy(o->type);
	r->data_ref     = string_copy(o->data_ref);
	r->transform    = util_clone_f32_array(o->transform);
	r->dimensions   = util_clone_f32_array(o->dimensions);
	r->visible      = o->visible;
	r->spawn        = o->spawn;
	r->material_ref = string_copy(o->material_ref);
	if (o->children != NULL) {
		r->children = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < o->children->length; ++i) {
			obj_t *c = util_clone_obj(o->children->buffer[i]);
			any_array_push(r->children, c);
		}
	}
	return r;
}

swatch_color_t *util_clone_swatch_color(swatch_color_t *s) {
	swatch_color_t *r = ALLOC_INIT(swatch_color_t, {0});
	r->base           = s->base;
	r->opacity        = s->opacity;
	r->occlusion      = s->occlusion;
	r->roughness      = s->roughness;
	r->metallic       = s->metallic;
	r->normal         = s->normal;
	r->emission       = s->emission;
	r->height         = s->height;
	r->subsurface     = s->subsurface;
	return r;
}
