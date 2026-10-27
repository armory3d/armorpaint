
#include "../global.h"

// Script node state, valid while its script runs
static mesh_geo_t  *nodes_mesh_script_in    = NULL;
static mesh_geo_t  *nodes_mesh_script_out   = NULL;
static mesh_geo_t **nodes_mesh_script_geos  = NULL;
static i32          nodes_mesh_script_count = 0;
static i32          nodes_mesh_script_cap   = 0;
static mesh_geo_t **nodes_mesh_script_inputs = NULL; // Meshes linked to the added mesh inputs, by socket

static ui_node_t        *nodes_mesh_script_node  = NULL;
static ui_node_button_t *nodes_mesh_script_dirty = NULL;
static ui_node_t        *nodes_mesh_socket_node  = NULL;

static char *nodes_mesh_script_text(ui_node_t *node) {
	ui_node_button_t *but = node->buttons->buffer[0];
	return but->data != NULL ? (char *)but->data->buffer : "";
}

static void nodes_mesh_script_button(i32 node_id) {
	ui_node_t        *node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	ui_node_button_t *but  = node->buttons->buffer[0];
	char             *text = nodes_mesh_script_text(node);
	if (node->buttons->length < 2) {
		// Node saved before inputs could be added
		nodes_mesh_def_button(node, "nodes_mesh_script_add_button", "CUSTOM", 0, NULL);
		node->buttons->buffer[1]->output = -1;
		node->buttons->buffer[1]->height = 1;
	}
	if (!ui_nodes_code_panel(node)) {
		// Apply the edit of a text field hidden while selected
		if (nodes_mesh_script_dirty == but) {
			nodes_mesh_script_dirty = NULL;
			g_ui->changed           = true;
		}
		but->height = 1;
		return;
	}
	ui_set_next_id((ui_id_t)&but->data);
	bool selected = g_ui->text_selected_id == ui_widget_id(NULL, UI_ID_TEXT);
	bool changed  = g_ui->changed;
	ui_nodes_code_begin();
	ui_text_area(&text, &ui_nodes_editor_state(node)->line, UI_ALIGN_LEFT, true, "", false);
	ui_nodes_code_end();
	if (ui_item_changed()) {
		but->data = u8_array_create_from_string(text);
		if (selected) {
			nodes_mesh_script_dirty = but;
		}
	}
	// The mesh is rebuilt once the text field is left
	if (selected) {
		g_ui->changed = changed;
	}
	else if (nodes_mesh_script_dirty == but) {
		nodes_mesh_script_dirty = NULL;
		g_ui->changed           = true;
	}
	but->height = ui_line_count(text) * UI_TEXT_AREA_LINE_H() / UI_LINE_H() + 2;
}

// Inputs added to a script node are read by name with mesh_geo_input_value(), mesh_geo_input_vector() and mesh_geo_input_mesh()
ui_node_socket_t *nodes_mesh_script_add_socket(ui_node_canvas_t *canvas, ui_node_t *node, char *type, char *name) {
	bool is_mesh = string_equals(type, "MESH") && string_equals(node->type, "MESH_SCRIPT");
	if (!string_equals(type, "RGBA") && !string_equals(type, "VECTOR") && !is_mesh) {
		type = "VALUE";
	}
	if (name == NULL || string_equals(name, "")) {
		// First free name, sockets are looked up by it
		char *base = is_mesh ? _tr("Mesh") : nodes_material_get_socket_name(type);
		name       = base;
		for (i32 i = 2; i < 999; ++i) {
			bool found = false;
			for (i32 j = 0; j < node->inputs->length; ++j) {
				if (string_equals(node->inputs->buffer[j]->name, name)) {
					found = true;
					break;
				}
			}
			if (!found) {
				break;
			}
			name = string("%s %s", base, i32_to_string(i));
		}
	}
	ui_node_socket_t *soc = nodes_material_create_socket(NULL, node, string_copy(name), type, canvas, 0.0, 1.0, NULL);
	soc->display          = string_equals(type, "VECTOR") ? 1 : 0;
	if (is_mesh) {
		soc->color = node->inputs->buffer[0]->color;
	}
	any_array_push(node->inputs, soc);
	return soc;
}

static void nodes_mesh_script_add_menu_draw() {
	ui_node_t *node    = nodes_mesh_socket_node;
	char      *names[] = {tr("RGBA"), tr("Vector"), tr("Value"), tr("Mesh")};
	char      *types[] = {"RGBA", "VECTOR", "VALUE", "MESH"};
	for (i32 i = 0; i < 4; ++i) {
		if (ui_menu_button(names[i], "", ICON_NONE)) {
			ui_nodes_push_undo(NULL);
			nodes_mesh_script_add_socket(ui_nodes_get_canvas(true), node, types[i], NULL);
			ui_nodes_canvas_changed();
		}
	}
}

static void nodes_mesh_script_add_button(i32 node_id) {
	if (ui_icon_button(tr("Add"), ICON_PLUS, UI_ALIGN_CENTER)) {
		nodes_mesh_socket_node = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
		ui_menu_draw(&nodes_mesh_script_add_menu_draw, -1, -1);
	}
}

static mesh_geo_t *nodes_mesh_script_track(mesh_geo_t *g) {
	if (!nodes_mesh_script_running) {
		return g; // Called from another script, left to the caller
	}
	if (nodes_mesh_script_count == nodes_mesh_script_cap) {
		nodes_mesh_script_cap  = nodes_mesh_script_cap == 0 ? 16 : nodes_mesh_script_cap * 2;
		nodes_mesh_script_geos = realloc(nodes_mesh_script_geos, nodes_mesh_script_cap * sizeof(mesh_geo_t *));
	}
	nodes_mesh_script_geos[nodes_mesh_script_count++] = g;
	return g;
}

// Mesh handed over by a script with out of range counts, indices and values dropped, copied or fixed in place
static mesh_geo_t *nodes_mesh_script_clean(mesh_geo_t *g, bool in_place) {
	if (g == NULL || g->pos == NULL || g->nor == NULL || g->tex == NULL || g->ind == NULL) {
		return util_geo_create(0, 0);
	}
	i32         vertex_count = g->vertex_count < 0 ? 0 : (g->vertex_count > g->vertex_capacity ? g->vertex_capacity : g->vertex_count);
	i32         index_count  = g->index_count < 0 ? 0 : (g->index_count > g->index_capacity ? g->index_capacity : g->index_count);
	mesh_geo_t *c            = in_place ? g : util_geo_create(vertex_count, index_count - index_count % 3);
	for (i32 i = 0; i < vertex_count * 3; ++i) {
		c->pos[i] = isfinite(g->pos[i]) ? g->pos[i] : 0.0;
		c->nor[i] = isfinite(g->nor[i]) ? g->nor[i] : 0.0;
	}
	for (i32 i = 0; i < vertex_count * 2; ++i) {
		c->tex[i] = isfinite(g->tex[i]) ? g->tex[i] : 0.0;
	}
	i32 count = 0; // Stays at or behind i, so packing in place is safe
	for (i32 i = 0; i + 2 < index_count; i += 3) {
		if (g->ind[i] < (u32)vertex_count && g->ind[i + 1] < (u32)vertex_count && g->ind[i + 2] < (u32)vertex_count) {
			c->ind[count++] = g->ind[i];
			c->ind[count++] = g->ind[i + 1];
			c->ind[count++] = g->ind[i + 2];
		}
	}
	c->vertex_count = vertex_count;
	c->index_count  = count;
	return c;
}

static mesh_geo_t *nodes_mesh_script_arg(mesh_geo_t *g) {
	return nodes_mesh_script_clean(g, false);
}

// Mesh the script node received, edited in place or replaced with mesh_geo_output()
mesh_geo_t *mesh_geo_input() {
	return nodes_mesh_script_in != NULL ? nodes_mesh_script_in : util_geo_create(0, 0);
}

void mesh_geo_output(mesh_geo_t *g) {
	if (nodes_mesh_script_running && g != NULL) {
		nodes_mesh_script_out = g;
	}
}

// The functions below leave their arguments as they are and return a new mesh
mesh_geo_t *mesh_geo_create(i32 vertex_count, i32 index_count) {
	return nodes_mesh_script_track(util_geo_create(vertex_count < 0 ? 0 : vertex_count, index_count < 0 ? 0 : index_count));
}

mesh_geo_t *mesh_geo_copy(mesh_geo_t *g) {
	return nodes_mesh_script_track(nodes_mesh_script_arg(g));
}

mesh_geo_t *mesh_geo_shape(i32 shape, f32 size_x, f32 size_y, f32 size_z, i32 segments) {
	f32 size[] = {size_x, size_y, size_z};
	return nodes_mesh_script_track(nodes_mesh_shape(shape, size, segments));
}

mesh_geo_t *mesh_geo_transform(mesh_geo_t *g, f32 loc_x, f32 loc_y, f32 loc_z, f32 rot_x, f32 rot_y, f32 rot_z, f32 scale_x, f32 scale_y, f32 scale_z) {
	vec4_t loc   = {loc_x, loc_y, loc_z, 1.0};
	vec4_t rot   = {rot_x, rot_y, rot_z, 1.0};
	vec4_t scale = {scale_x, scale_y, scale_z, 1.0};
	return nodes_mesh_script_track(nodes_mesh_geo_transform_euler(nodes_mesh_script_arg(g), loc, rot, scale));
}

mesh_geo_t *mesh_geo_array(mesh_geo_t *g, i32 count, f32 offset_x, f32 offset_y, f32 offset_z, bool relative) {
	vec4_t offset = {offset_x, offset_y, offset_z, 1.0};
	return nodes_mesh_script_track(nodes_mesh_geo_array(nodes_mesh_script_arg(g), count, offset, relative));
}

mesh_geo_t *mesh_geo_mirror(mesh_geo_t *g, i32 axis) {
	return nodes_mesh_script_track(nodes_mesh_geo_mirror(nodes_mesh_script_arg(g), axis));
}

mesh_geo_t *mesh_geo_join(mesh_geo_t *a, mesh_geo_t *b) {
	mesh_geo_t *ca = nodes_mesh_script_arg(a);
	return nodes_mesh_script_track(nodes_mesh_geo_join(ca, nodes_mesh_script_arg(b)));
}

// Appends src to dst in place, transformed by m: the x, y, z axes then the translation (12 floats).
// False when dst has no room left.
bool mesh_geo_append(mesh_geo_t *dst, mesh_geo_t *src, f32 *m) {
	if (dst == NULL || src == NULL || m == NULL || src->pos == NULL || src->nor == NULL || src->tex == NULL || src->ind == NULL) {
		return false;
	}
	i32 base  = dst->vertex_count < 0 ? 0 : dst->vertex_count;
	i32 first = dst->index_count < 0 ? 0 : dst->index_count;
	i32 vc    = src->vertex_count < 0 ? 0 : (src->vertex_count > src->vertex_capacity ? src->vertex_capacity : src->vertex_count);
	i32 ic    = src->index_count < 0 ? 0 : (src->index_count > src->index_capacity ? src->index_capacity : src->index_count);
	if (dst->pos == NULL || dst->nor == NULL || dst->tex == NULL || dst->ind == NULL || vc > dst->vertex_capacity - base || ic > dst->index_capacity - first) {
		return false;
	}
	// Normals by the cofactor matrix, which keeps them perpendicular under non-uniform scale
	f32  c[9]   = {m[4] * m[8] - m[5] * m[7], m[5] * m[6] - m[3] * m[8], m[3] * m[7] - m[4] * m[6], m[7] * m[2] - m[8] * m[1], m[8] * m[0] - m[6] * m[2],
	               m[6] * m[1] - m[7] * m[0], m[1] * m[5] - m[2] * m[4], m[2] * m[3] - m[0] * m[5], m[0] * m[4] - m[1] * m[3]};
	bool mirror = m[0] * c[0] + m[1] * c[1] + m[2] * c[2] < 0.0;
	for (i32 i = 0; i < vc; ++i) {
		f32 *p = src->pos + i * 3;
		f32 *n = src->nor + i * 3;
		f32 *q = dst->pos + (base + i) * 3;
		f32 *r = dst->nor + (base + i) * 3;
		for (i32 k = 0; k < 3; ++k) {
			q[k] = m[9 + k] + m[k] * p[0] + m[3 + k] * p[1] + m[6 + k] * p[2];
			r[k] = c[k] * n[0] + c[3 + k] * n[1] + c[6 + k] * n[2];
		}
		f32 l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
		l     = l > 0.0 ? (mirror ? -1.0 : 1.0) / l : 0.0;
		r[0] *= l;
		r[1] *= l;
		r[2] *= l;
		dst->tex[(base + i) * 2]     = src->tex[i * 2];
		dst->tex[(base + i) * 2 + 1] = src->tex[i * 2 + 1];
	}
	i32 count = first;
	for (i32 i = 0; i + 2 < ic; i += 3) {
		u32 *t = src->ind + i;
		if (t[0] < (u32)vc && t[1] < (u32)vc && t[2] < (u32)vc) {
			dst->ind[count++] = base + t[0];
			dst->ind[count++] = base + t[mirror ? 2 : 1]; // Mirroring flips the winding, keep it counter clockwise
			dst->ind[count++] = base + t[mirror ? 1 : 2];
		}
	}
	dst->vertex_count = base + vc;
	dst->index_count  = count;
	return true;
}

mesh_geo_t *mesh_geo_boolean(mesh_geo_t *a, mesh_geo_t *b, i32 op) {
	mesh_geo_t *ca = nodes_mesh_script_arg(a);
	return nodes_mesh_script_track(nodes_mesh_geo_boolean(ca, nodes_mesh_script_arg(b), op));
}

static mesh_geo_t *nodes_mesh_script_modify(mesh_geo_t *g, char *type, f32 value) {
	return nodes_mesh_script_track(nodes_mesh_geo_modify(nodes_mesh_script_arg(g), type, value));
}

mesh_geo_t *mesh_geo_subdivide(mesh_geo_t *g, i32 levels) {
	return nodes_mesh_script_modify(g, "MESH_SUBDIVIDE", levels);
}

mesh_geo_t *mesh_geo_smooth(mesh_geo_t *g, i32 iterations) {
	return nodes_mesh_script_modify(g, "MESH_SMOOTH", iterations);
}

mesh_geo_t *mesh_geo_decimate(mesh_geo_t *g, f32 ratio) {
	return nodes_mesh_script_modify(g, "MESH_DECIMATE", ratio);
}

mesh_geo_t *mesh_geo_bevel(mesh_geo_t *g, f32 amount) {
	return nodes_mesh_script_modify(g, "MESH_BEVEL", amount);
}

mesh_geo_t *mesh_geo_normals(mesh_geo_t *g, bool smooth) {
	return nodes_mesh_script_modify(g, "MESH_NORMALS", smooth ? 1.0 : 0.0);
}

// Input added to the script node, linked value nodes are followed
static bool nodes_mesh_script_socket(char *name, i32 i, f32 *result) {
	ui_node_t *node = nodes_mesh_script_node;
	if (node == NULL || name == NULL || i < 0 || i > 3) {
		return false;
	}
	for (i32 j = 1; j < node->inputs->length; ++j) {
		ui_node_socket_t *s = node->inputs->buffer[j];
		if (!string_equals(s->name, name) || string_equals(s->type, "MESH")) {
			continue;
		}
		vec4_t v = nodes_mesh_vector(node, j);
		if (s->default_value->length > 3 && nodes_mesh_linked_node(nodes_mesh_eval_canvas, node, j) == NULL) {
			v.w = s->default_value->buffer[3]; // Alpha
		}
		*result = i == 0 ? v.x : i == 1 ? v.y : i == 2 ? v.z : v.w;
		return true;
	}
	return false;
}

f32 mesh_geo_input_value(char *name) {
	f32 f = 0.0;
	nodes_mesh_script_socket(name, 0, &f);
	return f;
}

// Component of a vector (0 - 2) or color (0 - 3) input
f32 mesh_geo_input_vector(char *name, i32 i) {
	f32 f = 0.0;
	nodes_mesh_script_socket(name, i, &f);
	return f;
}

// Mesh linked to a mesh input added to the script node, empty when there is none
mesh_geo_t *mesh_geo_input_mesh(char *name) {
	ui_node_t *node = nodes_mesh_script_node;
	if (node != NULL && name != NULL) {
		for (i32 j = 1; j < node->inputs->length; ++j) {
			ui_node_socket_t *s = node->inputs->buffer[j];
			if (string_equals(s->name, name) && string_equals(s->type, "MESH")) {
				return nodes_mesh_script_inputs[j];
			}
		}
	}
	return nodes_mesh_script_track(util_geo_create(0, 0));
}

// Pixels of the image or layer mask node linked to an input added to the script node, read during the script.
// An input without one gives a 1x1 image of its value. NULL when there is no input of that name.
mesh_geo_image_t *mesh_geo_input_image(char *name) {
	ui_node_t *node = nodes_mesh_script_node;
	if (node == NULL || name == NULL) {
		return NULL;
	}
	for (i32 j = 1; j < node->inputs->length; ++j) {
		ui_node_socket_t *s = node->inputs->buffer[j];
		if (!string_equals(s->name, name) || string_equals(s->type, "MESH")) {
			continue;
		}
		ui_node_link_t *link = nodes_mesh_link(nodes_mesh_eval_canvas, node, j);
		ui_node_t      *from = link != NULL ? ui_get_node(nodes_mesh_eval_canvas->nodes, link->from_id) : NULL;
		if (from != NULL && (string_equals(from->type, "TEX_IMAGE") || string_equals(from->type, "LAYER_MASK"))) {
			mesh_geo_image_t *img = mesh_image_node_get(from, link->from_socket);
			if (img != NULL) {
				return img;
			}
		}
		mesh_geo_image_t *img = mesh_image_create(1, 1);
		for (i32 i = 0; i < 4; ++i) {
			nodes_mesh_script_socket(name, i, (f32 *)img->pixels + i);
		}
		mesh_image_node_track(img);
		return img;
	}
	return NULL;
}

// Channel 0 - 3 (r g b a) at a mesh uv, nearest pixel and repeating
f32 mesh_geo_image_sample(mesh_geo_image_t *img, f32 u, f32 v, i32 channel) {
	vec4_t c = mesh_image_sample(img, u, v);
	return channel == 0 ? c.x : channel == 1 ? c.y : channel == 2 ? c.z : channel == 3 ? c.w : 0.0;
}

mesh_geo_t *mesh_script_node_eval(ui_node_t *node) {
	mesh_geo_t *g    = nodes_mesh_eval_input(node, 0);
	char       *text = nodes_mesh_script_text(node);
	if (!project_scripts_trusted || string_equals(text, "")) {
		return g;
	}

	// Linked nodes are evaluated before the script starts, they may be script nodes too
	mesh_geo_t **inputs = calloc(node->inputs->length, sizeof(mesh_geo_t *));
	for (i32 i = 1; i < node->inputs->length; ++i) {
		if (string_equals(node->inputs->buffer[i]->type, "MESH")) {
			inputs[i] = nodes_mesh_eval_input(node, i);
		}
	}

	// The script works on a copy, the input passes through when it fails
	nodes_mesh_script_running = true;
	nodes_mesh_script_count   = 0;
	nodes_mesh_script_in      = nodes_mesh_script_track(nodes_mesh_script_arg(g));
	nodes_mesh_script_out     = nodes_mesh_script_in;
	nodes_mesh_script_node    = node;
	nodes_mesh_script_inputs  = inputs;
	for (i32 i = 1; i < node->inputs->length; ++i) {
		if (inputs[i] != NULL) {
			mesh_geo_t *raw = inputs[i];
			inputs[i]       = nodes_mesh_script_track(nodes_mesh_script_arg(raw));
			util_geo_free(raw);
		}
	}

	i32 errors = minic_error_count;
#ifdef WITH_TCC
	minic_tcc_eval(text, "mesh script");
#else
	minic_ctx_free(minic_eval_named(text, "mesh script"));
#endif
	if (minic_error_count == errors) {
		util_geo_free(g);
		g = NULL;
		for (i32 i = 0; i < nodes_mesh_script_count; ++i) {
			if (nodes_mesh_script_geos[i] == nodes_mesh_script_out) {
				// Created by this script, taken over instead of copied
				nodes_mesh_script_geos[i] = nodes_mesh_script_geos[--nodes_mesh_script_count];
				g                         = nodes_mesh_script_clean(nodes_mesh_script_out, true);
				if (g != nodes_mesh_script_out) {
					util_geo_free(nodes_mesh_script_out);
				}
				break;
			}
		}
		if (g == NULL) {
			g = nodes_mesh_script_arg(nodes_mesh_script_out);
		}
	}

	for (i32 i = 0; i < nodes_mesh_script_count; ++i) {
		util_geo_free(nodes_mesh_script_geos[i]);
	}
	nodes_mesh_script_count   = 0;
	nodes_mesh_script_in      = NULL;
	nodes_mesh_script_out     = NULL;
	nodes_mesh_script_node    = NULL;
	nodes_mesh_script_inputs  = NULL;
	nodes_mesh_script_running = false;
	free(inputs);
	return g;
}

void mesh_script_node_init() {
	// Runs a minic script on the mesh
	ui_node_t *n = nodes_mesh_def(nodes_mesh_modifier, _tr("Script"), "MESH_SCRIPT", 0xff4982a0, "MESH");
	nodes_mesh_def_button(n, "nodes_mesh_script_button", "CUSTOM", 0, "void main() {\n    mesh_geo_t *g = mesh_geo_input();\n    mesh_geo_output(g);\n}");
	n->buttons->buffer[0]->output = -1;
	n->buttons->buffer[0]->height = 6;
	n->width                      = 300;
	nodes_mesh_def_button(n, "nodes_mesh_script_add_button", "CUSTOM", 0, NULL);
	n->buttons->buffer[1]->output = -1;
	n->buttons->buffer[1]->height = 1;
	nodes_mesh_def_mesh(n);
	any_map_set(ui_nodes_custom_buttons, "nodes_mesh_script_button", nodes_mesh_script_button);
	any_map_set(ui_nodes_custom_buttons, "nodes_mesh_script_add_button", nodes_mesh_script_add_button);
	any_map_set(nodes_mesh_node_evals, "MESH_SCRIPT", mesh_script_node_eval);
}
