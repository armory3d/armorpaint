
#include "global.h"

#define NODES_MESH_SOCKET_COL 0xff00d6a3
#define NODES_MESH_MAX_DEPTH  64

static any_array_t *nodes_mesh_slots           = NULL;
static bool         nodes_mesh_refresh_pending = false;
static i32          nodes_mesh_eval_depth;

static mesh_geo_t *nodes_mesh_eval_node(ui_node_t *node);

// Node definitions

static ui_node_socket_t *nodes_mesh_socket(char *name, char *type, f32_array_t *value, f32 min, f32 max, f32 precision) {
	bool is_mesh = string_equals(type, "MESH");
	return ALLOC_INIT(ui_node_socket_t, {.name          = name,
	                                     .type          = type,
	                                     .color         = is_mesh ? NODES_MESH_SOCKET_COL : nodes_material_get_socket_color(type),
	                                     .default_value = value,
	                                     .min           = min,
	                                     .max           = max,
	                                     .precision     = precision,
	                                     .display       = string_equals(type, "VECTOR") ? 1 : 0});
}

ui_node_t *nodes_mesh_def(ui_node_t_array_t *category, char *name, char *type, u32 color, char *output_type) {
	ui_node_t *n =
	    ALLOC_INIT(ui_node_t,
	               {.name = name, .type = type, .color = color, .inputs = any_array_create(0), .outputs = any_array_create(0), .buttons = any_array_create(0)});
	if (output_type != NULL) {
		char        *output_name = string_equals(output_type, "MESH") ? _tr("Mesh") : string_equals(output_type, "VECTOR") ? _tr("Vector") : _tr("Value");
		f32_array_t *value       = string_equals(output_type, "VECTOR") ? f32_array_create_xyz(0.0, 0.0, 0.0) : f32_array_create_x(0.0);
		any_array_push(n->outputs, nodes_mesh_socket(output_name, output_type, value, 0.0, 1.0, 100));
		n->outputs->buffer[0]->display = 0;
	}
	if (category != NULL) {
		any_array_push(category, n);
	}
	return n;
}

void nodes_mesh_def_mesh(ui_node_t *n) {
	any_array_push(n->inputs, nodes_mesh_socket(_tr("Mesh"), "MESH", f32_array_create_x(0.0), 0.0, 1.0, 100));
}

void nodes_mesh_def_value(ui_node_t *n, char *name, f32 value, f32 min, f32 max, f32 precision) {
	any_array_push(n->inputs, nodes_mesh_socket(name, "VALUE", f32_array_create_x(value), min, max, precision));
}

void nodes_mesh_def_vector(ui_node_t *n, char *name, f32 x, f32 y, f32 z, f32 min, f32 max) {
	any_array_push(n->inputs, nodes_mesh_socket(name, "VECTOR", f32_array_create_xyz(x, y, z), min, max, 100));
}

void nodes_mesh_def_button(ui_node_t *n, char *name, char *type, f32 value, char *texts) {
	ui_node_button_t *b = ALLOC_INIT(ui_node_button_t, {.name          = name,
	                                                    .type          = type,
	                                                    .output        = 0,
	                                                    .default_value = f32_array_create_x(value),
	                                                    .data          = texts != NULL ? u8_array_create_from_string(texts) : NULL,
	                                                    .min           = 0.0,
	                                                    .max           = 1.0,
	                                                    .precision     = 100,
	                                                    .height        = 0});
	any_array_push(n->buttons, b);
}

void nodes_mesh_init() {
	if (nodes_mesh_list != NULL) {
		return;
	}

	nodes_mesh_node_evals   = any_map_create();
	nodes_mesh_node_vectors = any_map_create();

	nodes_mesh_input = any_array_create(0);
	mesh_input_node_init();
	mesh_shape_node_init();
	mesh_object_node_init();
	mesh_image_node_init();
	mesh_layer_mask_node_init();

	nodes_mesh_modifier = any_array_create(0);
	mesh_transform_node_init();
	mesh_array_node_init();
	mesh_mirror_node_init();
	mesh_subdivide_node_init();
	mesh_smooth_node_init();
	mesh_decimate_node_init();
	mesh_bevel_node_init();
	mesh_displace_node_init();
	mesh_script_node_init();

	nodes_mesh_combine = any_array_create(0);
	mesh_join_node_init();
	mesh_boolean_node_init();

	nodes_mesh_utilities = any_array_create(0);
	mesh_value_node_init();
	mesh_math_node_init();
	mesh_vector_node_init();

	mesh_output_node_init();

	nodes_mesh_categories = any_array_create_from_raw(
	    (void *[]){
	        _tr("Input"),
	        _tr("Modifier"),
	        _tr("Combine"),
	        _tr("Utilities"),
	    },
	    4);

	nodes_mesh_list = any_array_create_from_raw(
	    (void *[]){
	        nodes_mesh_input,
	        nodes_mesh_modifier,
	        nodes_mesh_combine,
	        nodes_mesh_utilities,
	    },
	    4);
}

// Geometry

mesh_geo_t *util_geo_create(i32 vertex_count, i32 index_count) {
	mesh_geo_t *g      = calloc(1, sizeof(mesh_geo_t));
	g->vertex_count    = vertex_count;
	g->index_count     = index_count;
	g->vertex_capacity = vertex_count;
	g->index_capacity  = index_count;
	g->pos             = calloc(vertex_count * 3 + 1, sizeof(f32));
	g->nor             = calloc(vertex_count * 3 + 1, sizeof(f32));
	g->tex             = calloc(vertex_count * 2 + 1, sizeof(f32));
	g->ind             = calloc(index_count + 1, sizeof(u32));
	return g;
}

void util_geo_free(mesh_geo_t *g) {
	free(g->pos);
	free(g->nor);
	free(g->tex);
	free(g->ind);
	free(g);
}

mesh_geo_t *nodes_mesh_geo_from_data(mesh_data_t *md) {
	i16_array_t *va0 = md->vertex_arrays->buffer[0]->values;
	i16_array_t *va1 = md->vertex_arrays->buffer[1]->values;
	i16_array_t *va2 = md->vertex_arrays->length > 2 ? md->vertex_arrays->buffer[2]->values : NULL;
	mesh_geo_t  *g   = util_geo_create(va0->length / 4, md->index_array->length);
	f32          sp  = md->scale_pos / 32767.0;
	f32          st  = md->scale_tex / 32767.0;
	for (i32 i = 0; i < g->vertex_count; ++i) {
		g->pos[i * 3]     = va0->buffer[i * 4] * sp;
		g->pos[i * 3 + 1] = va0->buffer[i * 4 + 1] * sp;
		g->pos[i * 3 + 2] = va0->buffer[i * 4 + 2] * sp;
		g->nor[i * 3]     = va1->buffer[i * 2] / 32767.0;
		g->nor[i * 3 + 1] = va1->buffer[i * 2 + 1] / 32767.0;
		g->nor[i * 3 + 2] = va0->buffer[i * 4 + 3] / 32767.0;
		if (va2 != NULL) {
			g->tex[i * 2]     = va2->buffer[i * 2] * st;
			g->tex[i * 2 + 1] = va2->buffer[i * 2 + 1] * st;
		}
	}
	memcpy(g->ind, md->index_array->buffer, g->index_count * sizeof(u32));
	return g;
}

static i16 nodes_mesh_pack(f32 f) {
	f     = f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
	f32 x = f * 32767.0;
	i32 t = (i32)x;
	f32 r = x - (f32)t;
	return (i16)(t + (r >= 0.5f) - (r <= -0.5f));
}

mesh_data_t *nodes_mesh_geo_to_data(mesh_geo_t *g, char *name) {
	f32 scale_pos = 0.0;
	f32 scale_tex = 1.0;
	for (i32 i = 0; i < g->vertex_count; ++i) {
		scale_pos = math_max(scale_pos, math_abs(g->pos[i * 3]));
		scale_pos = math_max(scale_pos, math_abs(g->pos[i * 3 + 1]));
		scale_pos = math_max(scale_pos, math_abs(g->pos[i * 3 + 2]));
		scale_tex = math_max(scale_tex, math_abs(g->tex[i * 2]));
		scale_tex = math_max(scale_tex, math_abs(g->tex[i * 2 + 1]));
	}
	if (scale_pos <= 0.0) {
		scale_pos = 1.0;
	}

	i16_array_t *va0  = i16_array_create(g->vertex_count * 4);
	i16_array_t *va1  = i16_array_create(g->vertex_count * 2);
	i16_array_t *va2  = i16_array_create(g->vertex_count * 2);
	u32_array_t *inda = u32_array_create(g->index_count);
	for (i32 i = 0; i < g->vertex_count; ++i) {
		va0->buffer[i * 4]     = nodes_mesh_pack(g->pos[i * 3] / scale_pos);
		va0->buffer[i * 4 + 1] = nodes_mesh_pack(g->pos[i * 3 + 1] / scale_pos);
		va0->buffer[i * 4 + 2] = nodes_mesh_pack(g->pos[i * 3 + 2] / scale_pos);
		va0->buffer[i * 4 + 3] = nodes_mesh_pack(g->nor[i * 3 + 2]);
		va1->buffer[i * 2]     = nodes_mesh_pack(g->nor[i * 3]);
		va1->buffer[i * 2 + 1] = nodes_mesh_pack(g->nor[i * 3 + 1]);
		va2->buffer[i * 2]     = nodes_mesh_pack(g->tex[i * 2] / scale_tex);
		va2->buffer[i * 2 + 1] = nodes_mesh_pack(g->tex[i * 2 + 1] / scale_tex);
	}
	memcpy(inda->buffer, g->ind, g->index_count * sizeof(u32));

	mesh_data_t *raw   = ALLOC_INIT(mesh_data_t, {.name          = string_copy(name),
	                                              .vertex_arrays = any_array_create_from_raw(
                                                    (void *[]){
                                                        ALLOC_INIT(vertex_array_t, {.values = va0, .attrib = "pos", .data = "short4norm"}),
                                                        ALLOC_INIT(vertex_array_t, {.values = va1, .attrib = "nor", .data = "short2norm"}),
                                                        ALLOC_INIT(vertex_array_t, {.values = va2, .attrib = "tex", .data = "short2norm"}),
                                                    },
                                                    3),
	                                              .index_array = inda,
	                                              .scale_pos   = scale_pos,
	                                              .scale_tex   = scale_tex});
	mesh_data_t *md    = mesh_data_create(raw);
	md->_->owns_arrays = true;
	return md;
}

void nodes_mesh_geo_flip(mesh_geo_t *g) {
	for (i32 i = 0; i + 2 < g->index_count; i += 3) {
		u32 t         = g->ind[i + 1];
		g->ind[i + 1] = g->ind[i + 2];
		g->ind[i + 2] = t;
	}
}

void nodes_mesh_geo_copy_into(mesh_geo_t *dst, mesh_geo_t *src, i32 vertex_offset, i32 index_offset) {
	memcpy(dst->pos + vertex_offset * 3, src->pos, src->vertex_count * 3 * sizeof(f32));
	memcpy(dst->nor + vertex_offset * 3, src->nor, src->vertex_count * 3 * sizeof(f32));
	memcpy(dst->tex + vertex_offset * 2, src->tex, src->vertex_count * 2 * sizeof(f32));
	for (i32 i = 0; i < src->index_count; ++i) {
		dst->ind[index_offset + i] = src->ind[i] + vertex_offset;
	}
}

void nodes_mesh_geo_transform(mesh_geo_t *g, mat4_t m) {
	mat4_t nm = mat4_transpose3(mat4_inv(m));
	for (i32 i = 0; i < g->vertex_count; ++i) {
		vec4_t p          = vec4_apply_mat4((vec4_t){g->pos[i * 3], g->pos[i * 3 + 1], g->pos[i * 3 + 2], 1.0}, m);
		vec4_t n          = vec4_norm(vec4_apply_mat4((vec4_t){g->nor[i * 3], g->nor[i * 3 + 1], g->nor[i * 3 + 2], 0.0}, nm));
		g->pos[i * 3]     = p.x;
		g->pos[i * 3 + 1] = p.y;
		g->pos[i * 3 + 2] = p.z;
		g->nor[i * 3]     = n.x;
		g->nor[i * 3 + 1] = n.y;
		g->nor[i * 3 + 2] = n.z;
	}
	if (mat4_determinant(m) < 0.0) {
		nodes_mesh_geo_flip(g);
	}
}

// Evaluation

ui_node_link_t *nodes_mesh_link(ui_node_canvas_t *c, ui_node_t *node, i32 socket) {
	for (i32 i = 0; i < c->links->length; ++i) {
		ui_node_link_t *l = c->links->buffer[i];
		if (l->to_id == node->id && l->to_socket == socket) {
			return l;
		}
	}
	return NULL;
}

ui_node_t *nodes_mesh_linked_node(ui_node_canvas_t *c, ui_node_t *node, i32 socket) {
	ui_node_link_t *l = nodes_mesh_link(c, node, socket);
	return l != NULL ? ui_get_node(c->nodes, l->from_id) : NULL;
}

// Socket value, taken from the linked value, math or vector node when there is one
vec4_t nodes_mesh_vector(ui_node_t *node, i32 socket) {
	ui_node_socket_t *s      = node->inputs->buffer[socket];
	f32              *v      = s->default_value->buffer;
	vec4_t            result = s->default_value->length > 2 ? (vec4_t){v[0], v[1], v[2], 1.0} : (vec4_t){v[0], v[0], v[0], 1.0};
	ui_node_link_t   *link   = nodes_mesh_link(nodes_mesh_eval_canvas, node, socket);
	ui_node_t        *from   = link != NULL ? ui_get_node(nodes_mesh_eval_canvas->nodes, link->from_id) : NULL;
	if (from == NULL || nodes_mesh_eval_depth >= NODES_MESH_MAX_DEPTH) {
		return result;
	}
	vec4_t (*node_vector)(ui_node_t *) = any_map_get(nodes_mesh_node_vectors, from->type);
	if (node_vector != NULL) {
		nodes_mesh_vector_socket = link->from_socket;
		nodes_mesh_eval_depth++;
		result = node_vector(from);
		nodes_mesh_eval_depth--;
	}
	return result;
}

f32 nodes_mesh_value(ui_node_t *node, i32 socket) {
	return nodes_mesh_vector(node, socket).x;
}

i32 nodes_mesh_button(ui_node_t *node, i32 button) {
	return (i32)node->buttons->buffer[button]->default_value->buffer[0];
}

static mesh_object_t *nodes_mesh_object(i32 uid);

static bool nodes_mesh_has_node(ui_node_canvas_t *c, char *type) {
	for (i32 i = 0; i < c->nodes->length; ++i) {
		if (string_equals(c->nodes->buffer[i]->type, type)) {
			return true;
		}
	}
	return false;
}

static mesh_nodes_t *nodes_mesh_find(i32 uid) {
	for (i32 i = 0; nodes_mesh_slots != NULL && i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t *s = nodes_mesh_slots->buffer[i];
		if (s->uid == uid) {
			return s;
		}
	}
	return NULL;
}

mesh_object_t *nodes_mesh_node_object(ui_node_t *n) {
	f32_array_t *v = n->buttons->buffer[0]->default_value;
	return v->length > 2 && v->buffer[1] >= 0 ? nodes_mesh_object((i32)v->buffer[1]) : NULL;
}

// Scene mesh nodes follow their object when meshes are reordered
static void nodes_mesh_sync_objects(ui_node_canvas_t *c) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		if (!string_equals(n->type, "MESH_OBJECT")) {
			continue;
		}
		ui_node_button_t *b = n->buttons->buffer[0];
		if (b->default_value->length < 3) {
			b->default_value = f32_array_create_xyz(b->default_value->buffer[0], -1, b->default_value->buffer[0]);
		}
		f32 *v     = b->default_value->buffer;
		i32  index = (i32)v[0];
		if (v[1] < 0 || index != (i32)v[2]) {
			// Loaded from a file or picked by the user
			v[1] = index >= 0 && index < objects->length ? objects->buffer[index]->base->uid : -1;
		}
		else {
			mesh_object_t *o = nodes_mesh_object((i32)v[1]);
			if (o != NULL) {
				index = array_index_of(objects, o);
			}
		}
		v[0] = index;
		v[2] = index;
	}
}

// Whether the nodes of an object use the mesh of the target object
static bool nodes_mesh_reaches(mesh_object_t *o, i32 target_uid, i32 depth) {
	if (o->base->uid == target_uid) {
		return true;
	}
	mesh_nodes_t *slot = nodes_mesh_find(o->base->uid);
	if (slot == NULL || depth > NODES_MESH_MAX_DEPTH) {
		return false;
	}
	for (i32 i = 0; i < slot->canvas->nodes->length; ++i) {
		ui_node_t *n = slot->canvas->nodes->buffer[i];
		if (!string_equals(n->type, "MESH_OBJECT")) {
			continue;
		}
		mesh_object_t *ref = nodes_mesh_node_object(n);
		if (ref != NULL && nodes_mesh_reaches(ref, target_uid, depth + 1)) {
			return true;
		}
	}
	return false;
}

// Meshes which depend on each other use the original mesh instead of the output
mesh_data_t *nodes_mesh_object_data(mesh_object_t *self, mesh_object_t *ref) {
	mesh_nodes_t *slot = nodes_mesh_find(ref->base->uid);
	if (slot != NULL && slot->output != slot->input && nodes_mesh_reaches(ref, self->base->uid, 0)) {
		return slot->input;
	}
	return ref->data;
}

mat4_t nodes_mesh_object_matrix(mesh_object_t *self, mesh_object_t *ref) {
	return mat4_mult_mat(ref->base->transform->world, mat4_inv(self->base->transform->world));
}

mesh_geo_t *nodes_mesh_eval_input(ui_node_t *node, i32 socket) {
	ui_node_t *from = nodes_mesh_linked_node(nodes_mesh_eval_canvas, node, socket);
	if (from == NULL || from->outputs->length == 0 || !string_equals(from->outputs->buffer[0]->type, "MESH")) {
		return util_geo_create(0, 0);
	}
	return nodes_mesh_eval_node(from);
}

mesh_geo_t *nodes_mesh_geo_modify(mesh_geo_t *g, char *type, f32 value) {
	mesh_object_t *o = nodes_mesh_eval_object != NULL ? nodes_mesh_eval_object : g_context->paint_object;
	if (g->index_count == 0 || o == NULL) {
		return g;
	}

	mesh_object_t temp = *o;
	temp.data          = nodes_mesh_geo_to_data(g, o->base->name);
	util_geo_free(g);

	// Modifiers swap the data of the object and free the previous one on the next frame
	bool smooth = true;
	if (string_equals(type, "MESH_SUBDIVIDE")) {
		i32 levels = math_floor(value);
		for (i32 i = 0; i < levels && i < 6; ++i) {
			_util_mesh_subdivide(&temp);
		}
	}
	else if (string_equals(type, "MESH_SMOOTH")) {
		i32 iterations = math_floor(value);
		for (i32 i = 0; i < iterations && i < 32; ++i) {
			_util_mesh_smooth(&temp);
		}
	}
	else if (string_equals(type, "MESH_DECIMATE")) {
		if (value < 1.0) {
			_util_mesh_decimate(&temp, math_max(value, 0.01));
			nodes_mesh_eval_unwrap = true;
		}
	}
	else if (string_equals(type, "MESH_BEVEL")) {
		if (value > 0.0) {
			_util_mesh_bevel(&temp, value);
			nodes_mesh_eval_unwrap = true;
		}
	}
	else {
		smooth = value > 0.0; // Normals only
	}
	util_mesh_calc_normals(any_array_create_from_raw_tmp((void *[]){&temp}, 1), smooth);

	g = nodes_mesh_geo_from_data(temp.data);
	util_mesh_delete_data_uncache(temp.data);
	return g;
}

// Subdivide, smooth, decimate and bevel nodes
mesh_geo_t *nodes_mesh_eval_modifier(ui_node_t *node) {
	mesh_geo_t *g = nodes_mesh_eval_input(node, 0);
	if (g->index_count > 0) {
		g = nodes_mesh_geo_modify(g, node->type, nodes_mesh_value(node, 1));
	}
	return g;
}

static mesh_geo_t *nodes_mesh_eval_node(ui_node_t *node) {
	if (++nodes_mesh_eval_depth > NODES_MESH_MAX_DEPTH) {
		return util_geo_create(0, 0); // Links form a loop
	}

	mesh_geo_t *(*node_eval)(ui_node_t *) = any_map_get(nodes_mesh_node_evals, node->type);
	mesh_geo_t *g                         = node_eval != NULL ? node_eval(node) : util_geo_create(0, 0);

	nodes_mesh_eval_depth--;
	return g;
}

// Returns NULL when the original mesh is used as is
static mesh_data_t *nodes_mesh_eval(mesh_nodes_t *slot, mesh_object_t *o) {
	ui_node_t *output = NULL;
	for (i32 i = 0; i < slot->canvas->nodes->length; ++i) {
		ui_node_t *n = slot->canvas->nodes->buffer[i];
		if (string_equals(n->type, "MESH_OUTPUT")) {
			output = n;
			break;
		}
	}
	if (output == NULL) {
		return NULL;
	}
	ui_node_t *from = nodes_mesh_linked_node(slot->canvas, output, 0);
	if (from == NULL || from->outputs->length == 0 || string_equals(from->type, "MESH_INPUT")) {
		return NULL;
	}

	nodes_mesh_eval_canvas      = slot->canvas;
	nodes_mesh_eval_slot        = slot;
	nodes_mesh_eval_object      = o;
	nodes_mesh_eval_unwrap      = false;
	nodes_mesh_eval_depth       = 0;
	nodes_mesh_eval_reads_frame = false;
	mesh_geo_t *g               = nodes_mesh_eval_node(from);
	nodes_mesh_eval_object      = NULL;
	slot->reads_frame           = nodes_mesh_eval_reads_frame;
	mesh_image_node_clear();

	if (g->index_count == 0) {
		// Keep the vertex buffers valid
		util_geo_free(g);
		g         = util_geo_create(3, 3);
		g->ind[1] = 1;
		g->ind[2] = 2;
		g->nor[2] = g->nor[5] = g->nor[8] = 1.0;
		nodes_mesh_eval_unwrap            = false;
	}

	mesh_data_t *md = nodes_mesh_geo_to_data(g, o->base->name);
	util_geo_free(g);
	return md;
}

static u32 nodes_mesh_hash_bytes(u32 hash, void *data, i32 length) {
	for (i32 i = 0; i < length; ++i) {
		hash = (hash ^ ((unsigned char *)data)[i]) * 16777619u;
	}
	return hash;
}

// Covers what changes the result, node positions are left out
static u32 nodes_mesh_hash(ui_node_canvas_t *c, mesh_object_t *o) {
	u32 hash = 2166136261u;
	for (i32 i = 0; i < c->nodes->length; ++i) {
		ui_node_t *n = c->nodes->buffer[i];
		hash         = nodes_mesh_hash_bytes(hash, &n->id, sizeof(n->id));
		hash         = nodes_mesh_hash_bytes(hash, n->type, string_length(n->type));
		if (string_equals(n->type, "MESH_OBJECT")) {
			// Rebuild when the other mesh changes or either of the two moves
			mesh_object_t *ref = nodes_mesh_node_object(n);
			if (ref != NULL) {
				mesh_data_t *data  = nodes_mesh_object_data(o, ref);
				mat4_t       m     = nodes_mesh_object_matrix(o, ref);
				i32          count = data->index_array->length;
				hash               = nodes_mesh_hash_bytes(hash, &data, sizeof(data));
				hash               = nodes_mesh_hash_bytes(hash, &count, sizeof(count));
				hash               = nodes_mesh_hash_bytes(hash, m.m, sizeof(f32) * 16);
			}
			hash = nodes_mesh_hash_bytes(hash, &ref, sizeof(ref));
			continue;
		}
		if (string_equals(n->type, "TEX_IMAGE") || string_equals(n->type, "LAYER_MASK")) {
			gpu_texture_t *texture = mesh_image_node_texture(n); // Reimported, replaced or swapped by undo
			hash                   = nodes_mesh_hash_bytes(hash, &texture, sizeof(texture));
		}
		if (string_equals(n->type, "MESH_DISPLACE")) {
			gpu_texture_t *texture = mesh_displace_node_texture(n);
			hash                   = nodes_mesh_hash_bytes(hash, &texture, sizeof(texture));
		}
		for (i32 j = 0; j < n->inputs->length; ++j) {
			f32_array_t *v = n->inputs->buffer[j]->default_value;
			if (j > 0 && string_equals(n->type, "MESH_SCRIPT")) {
				char *name = n->inputs->buffer[j]->name; // Scripts read their inputs by name
				hash       = nodes_mesh_hash_bytes(hash, name, string_length(name));
			}
			if (v != NULL) {
				hash = nodes_mesh_hash_bytes(hash, v->buffer, v->length * sizeof(f32));
			}
		}
		for (i32 j = 0; j < n->buttons->length; ++j) {
			f32_array_t *v = n->buttons->buffer[j]->default_value;
			if (v != NULL && v->length > 0) {
				hash = nodes_mesh_hash_bytes(hash, v->buffer, sizeof(f32));
			}
			u8_array_t *data = n->buttons->buffer[j]->data;
			if (data != NULL) {
				hash = nodes_mesh_hash_bytes(hash, data->buffer, data->length);
			}
		}
	}
	for (i32 i = 0; i < c->links->length; ++i) {
		ui_node_link_t *l = c->links->buffer[i];
		hash              = nodes_mesh_hash_bytes(hash, &l->from_id, sizeof(i32) * 4);
	}
	return hash;
}

// Slots

static mesh_object_t *nodes_mesh_object(i32 uid) {
	if (g_project->_->paint_objects == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (g_project->_->paint_objects->buffer[i]->base->uid == uid) {
			return g_project->_->paint_objects->buffer[i];
		}
	}
	return NULL;
}

static bool nodes_mesh_is_default(ui_node_canvas_t *c) {
	return c->nodes->length == 2 && c->links->length == 1;
}

static ui_node_canvas_t *nodes_mesh_default_canvas(ui_nodes_t *nodes) {
	nodes_mesh_init();
	ui_node_canvas_t *c  = ALLOC_INIT(ui_node_canvas_t, {.name = "", .nodes = any_array_create(0), .links = any_array_create(0)});
	ui_node_t        *in = ui_nodes_make_node(nodes_mesh_input_def, nodes, c);
	in->x                = 100;
	in->y                = 200;
	any_array_push(c->nodes, in);
	ui_node_t *out = ui_nodes_make_node(nodes_mesh_output_def, nodes, c);
	out->x         = 420;
	out->y         = 200;
	any_array_push(c->nodes, out);
	ui_node_link_t *l = ALLOC_INIT(ui_node_link_t, {.id = 0, .from_id = in->id, .from_socket = 0, .to_id = out->id, .to_socket = 0});
	any_array_push(c->links, l);
	ui_nodes_node_changed = NULL;
	return c;
}

mesh_nodes_t *nodes_mesh_get(mesh_object_t *o, bool create) {
	if (o == NULL) {
		return NULL;
	}
	mesh_nodes_t *slot = NULL;
	if (nodes_mesh_slots != NULL) {
		for (i32 i = 0; i < nodes_mesh_slots->length; ++i) {
			mesh_nodes_t *s = nodes_mesh_slots->buffer[i];
			if (s->uid == o->base->uid) {
				slot = s;
				break;
			}
		}
	}
	if (slot == NULL) {
		if (!create) {
			return NULL;
		}
		if (nodes_mesh_slots == NULL) {
			nodes_mesh_slots = any_array_create(0);
		}
		slot         = ALLOC_INIT(mesh_nodes_t, {.uid = o->base->uid, .input = o->data, .output = o->data});
		slot->nodes  = ui_nodes_create();
		slot->canvas = nodes_mesh_default_canvas(slot->nodes);
		slot->hash   = nodes_mesh_hash(slot->canvas, o);
		any_array_push(nodes_mesh_slots, slot);
	}
	if (slot->output == slot->input) {
		// The original mesh is in use, mesh tab operations may have replaced it
		slot->input  = o->data;
		slot->output = o->data;
	}
	slot->canvas->name = o->base->name;
	nodes_mesh_sync_objects(slot->canvas);
	return slot;
}

// The original mesh is kept in memory while the object shows the output mesh
bool nodes_mesh_holds(mesh_data_t *data) {
	if (nodes_mesh_slots == NULL) {
		return false;
	}
	for (i32 i = 0; i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t *s = nodes_mesh_slots->buffer[i];
		if (s->input == data && s->output != s->input) {
			return true;
		}
	}
	return false;
}

// Replaces the mesh of an object, the previous one is left to the caller
static void nodes_mesh_set_data(mesh_object_t *o, mesh_data_t *data, bool unwrap) {
	mesh_object_set_data(o, data);
	transform_build_matrix(o->base->transform);
	nodes_mesh_refresh_pending = true; // Meshes using this one

	if (unwrap) {
		util_mesh_uv_unwrap(any_array_create_from_raw_tmp((void *[]){o}, 1));
	}
	else if (g_context->merged_object != NULL || g_project->_->paint_objects->length > 1) {
		util_mesh_merge(NULL);
	}

	char          *uid_key = i32_to_string(o->base->uid);
	gpu_texture_t *preview = tab_meshes_preview_map != NULL ? any_map_get(tab_meshes_preview_map, uid_key) : NULL;
	if (preview != NULL) {
		gpu_delete_texture(preview);
		map_delete(tab_meshes_preview_map, uid_key);
	}

	util_uv_uvmap_cached       = false;
	util_uv_trianglemap_cached = false;
	util_uv_seammap_cached     = false;
	util_uv_dilatemap_cached   = false;
	render_path_raytrace_ready = false;
	g_context->ddirty          = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

void nodes_mesh_update(mesh_object_t *o, bool force) {
	mesh_nodes_t *slot = nodes_mesh_get(o, false);
	if (slot == NULL || nodes_mesh_script_running) {
		return;
	}
	force       = force || slot->dirty || (slot->reads_frame && slot->frame != tab_timeline_selected_frame);
	slot->dirty = false;
	slot->frame = tab_timeline_selected_frame;
	u32 hash    = nodes_mesh_hash(slot->canvas, o);
	if (!force && hash == slot->hash) {
		return;
	}
	slot->hash = hash;

	mesh_data_t *out = nodes_mesh_eval(slot, o);
	if (out == NULL) {
		out               = slot->input;
		slot->reads_frame = false;
	}
	mesh_data_t *old = o->data;
	if (out == old) {
		return;
	}

	if (old != slot->input && !util_mesh_data_is_shared(old)) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, old);
	}
	slot->output = out;
	nodes_mesh_set_data(o, out, nodes_mesh_eval_unwrap && out != slot->input);
}

// Drops the nodes of an object, the output mesh stays with the object
void nodes_mesh_remove(mesh_object_t *o) {
	if (nodes_mesh_slots == NULL) {
		return;
	}
	nodes_mesh_refresh_pending = true;
	for (i32 i = 0; i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t *slot = nodes_mesh_slots->buffer[i];
		if (slot->uid != o->base->uid) {
			continue;
		}
		array_splice(nodes_mesh_slots, i, 1);
		mesh_script_node_state_free(slot->uid);
		mesh_data_t *input = slot->output != slot->input ? slot->input : NULL;
		if (input != NULL && util_mesh_data_owner(input) == -1 && !nodes_mesh_holds(input)) {
			sys_notify_on_next_frame(&util_mesh_delete_data_uncache, input);
		}
		return;
	}
}

// Canvases of all objects with nodes, for asset index updates
ui_node_canvas_t_array_t *nodes_mesh_canvases() {
	ui_node_canvas_t_array_t *canvases = any_array_create(0);
	for (i32 i = 0; nodes_mesh_slots != NULL && i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t *slot = nodes_mesh_slots->buffer[i];
		any_array_push(canvases, slot->canvas);
	}
	return canvases;
}

bool nodes_mesh_has_nodes(mesh_object_t *o) {
	mesh_nodes_t *slot = nodes_mesh_get(o, false);
	return slot != NULL && !nodes_mesh_is_default(slot->canvas);
}

// The output mesh becomes the original mesh, the next edit starts from a default canvas
void nodes_mesh_apply(mesh_object_t *o) {
	nodes_mesh_remove(o);
	ui_nodes_hwnd->redraws = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
}

// Modifiers of the mesh tab, a single node run on the mesh of an object with the result kept as its mesh

static ui_node_canvas_t *nodes_mesh_modifier_canvas = NULL;

// Node with its default values on a canvas of its own, linked in between a mesh input and output
ui_node_t *nodes_mesh_modifier_node(ui_node_t *def) {
	ui_node_canvas_t *c    = nodes_mesh_default_canvas(NULL);
	ui_node_t        *in   = c->nodes->buffer[0];
	ui_node_t        *out  = c->nodes->buffer[1];
	ui_node_t        *node = ui_nodes_make_node(def, NULL, c);
	any_array_push(c->nodes, node);
	c->links->buffer[0]->to_id = node->id;
	ui_node_link_t *l          = ALLOC_INIT(ui_node_link_t, {.id = 1, .from_id = node->id, .from_socket = 0, .to_id = out->id, .to_socket = 0});
	any_array_push(c->links, l);
	ui_nodes_node_changed      = NULL;
	nodes_mesh_modifier_canvas = c;
	return node;
}

void nodes_mesh_apply_modifier(mesh_object_t *o) {
	if (nodes_mesh_modifier_canvas == NULL || o->data->index_array->length == 0) {
		return;
	}
	mesh_data_t *old  = o->data;
	mesh_nodes_t temp = {.uid = o->base->uid, .canvas = nodes_mesh_modifier_canvas, .input = old, .output = old};
	mesh_data_t *out  = nodes_mesh_eval(&temp, o);
	if (out == NULL) {
		return;
	}
	if (!util_mesh_data_is_shared(old)) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, old);
	}
	// Nodes of the object are kept, they run again on their next change
	mesh_nodes_t *slot = nodes_mesh_find(o->base->uid);
	if (slot != NULL) {
		if (slot->output == slot->input) {
			slot->input = out;
		}
		slot->output = out;
	}
	nodes_mesh_set_data(o, out, nodes_mesh_eval_unwrap);
}

// Replaces the original mesh of an object whose nodes change it, the nodes run again once the mouse is up
bool nodes_mesh_set_input(mesh_object_t *o, mesh_data_t *data) {
	mesh_nodes_t *slot = nodes_mesh_find(o->base->uid);
	if (slot == NULL || slot->output == slot->input) {
		return false;
	}
	mesh_data_t *old = slot->input;
	slot->input      = data;
	if (old != data && util_mesh_data_owner(old) == -1 && !nodes_mesh_holds(old)) {
		sys_notify_on_next_frame(&util_mesh_delete_data_uncache, old);
	}
	slot->dirty                = true;
	nodes_mesh_refresh_pending = true;
	return true;
}

void nodes_mesh_layer_changed(slot_layer_t *l) {
	i32 index = g_project->_->layers != NULL ? array_index_of(g_project->_->layers, l) : -1;
	for (i32 i = 0; index >= 0 && nodes_mesh_slots != NULL && i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t *slot = nodes_mesh_slots->buffer[i];
		for (i32 j = 0; j < slot->canvas->nodes->length; ++j) {
			ui_node_t *n = slot->canvas->nodes->buffer[j];
			if ((string_equals(n->type, "LAYER_MASK") || string_equals(n->type, "MESH_DISPLACE")) && nodes_mesh_button(n, 0) == index) {
				slot->dirty                = true;
				nodes_mesh_refresh_pending = true;
				break;
			}
		}
	}
}

// Rebuilds meshes with scene mesh nodes after other meshes changed or moved
void nodes_mesh_request_refresh() {
	nodes_mesh_refresh_pending = true;
}

void nodes_mesh_refresh() {
	if (!nodes_mesh_refresh_pending || nodes_mesh_slots == NULL || mouse_down("left") || base_player) {
		return;
	}
	nodes_mesh_refresh_pending = false;
	for (i32 i = 0; i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t  *slot = nodes_mesh_slots->buffer[i];
		mesh_object_t *o    = nodes_mesh_object(slot->uid);
		// Undo swaps the textures of layers, which changes the hash of layer mask nodes
		if (o != NULL && (slot->dirty || nodes_mesh_has_node(slot->canvas, "MESH_OBJECT") || nodes_mesh_has_node(slot->canvas, "LAYER_MASK") ||
		                  nodes_mesh_has_node(slot->canvas, "MESH_DISPLACE"))) {
			nodes_mesh_update(o, false);
		}
	}
}

// Rebuilds meshes whose scripts read the timeline frame
void nodes_mesh_frame_changed() {
	for (i32 i = 0; nodes_mesh_slots != NULL && i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t *slot = nodes_mesh_slots->buffer[i];
		if (slot->reads_frame && slot->frame != tab_timeline_selected_frame) {
			mesh_object_t *o = nodes_mesh_object(slot->uid);
			if (o != NULL) {
				nodes_mesh_update(o, false);
			}
		}
	}
}

void nodes_mesh_reset() {
	mesh_script_node_state_free(-1);
	if (nodes_mesh_slots == NULL) {
		return;
	}
	while (nodes_mesh_slots->length > 0) {
		mesh_nodes_t *slot  = array_pop(nodes_mesh_slots);
		mesh_data_t  *input = slot->output != slot->input ? slot->input : NULL;
		if (input != NULL && g_project->_->paint_objects != NULL && util_mesh_data_owner(input) == -1 && !nodes_mesh_holds(input)) {
			util_mesh_delete_data_uncache(input);
		}
	}
}

// Project files store the original meshes and the nodes, outputs are rebuilt on load
void nodes_mesh_export_begin() {
	g_project->mesh_nodes         = NULL;
	g_project->mesh_nodes_objects = NULL;
	if (nodes_mesh_slots == NULL) {
		return;
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o    = g_project->_->paint_objects->buffer[i];
		mesh_nodes_t  *slot = nodes_mesh_get(o, false);
		if (slot == NULL || nodes_mesh_is_default(slot->canvas)) {
			continue;
		}
		if (g_project->mesh_nodes == NULL) {
			g_project->mesh_nodes         = any_array_create(0);
			g_project->mesh_nodes_objects = i32_array_create(0);
		}
		// Images are stored by name like in material nodes
		ui_node_canvas_t *c = util_clone_canvas(slot->canvas);
		for (i32 j = 0; j < c->nodes->length; ++j) {
			export_arm_export_node(c->nodes->buffer[j], NULL);
		}
		any_array_push(g_project->mesh_nodes, c);
		i32_array_push(g_project->mesh_nodes_objects, i);
		if (slot->output != slot->input) {
			slot->output = o->data;
			o->data      = slot->input;
		}
	}
}

void nodes_mesh_export_end() {
	if (nodes_mesh_slots == NULL) {
		return;
	}
	for (i32 i = 0; i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t  *slot = nodes_mesh_slots->buffer[i];
		mesh_object_t *o    = nodes_mesh_object(slot->uid);
		if (o != NULL && slot->output != slot->input && o->data == slot->input) {
			o->data = slot->output;
		}
	}
}

void nodes_mesh_import() {
	if (g_project->mesh_nodes == NULL || g_project->mesh_nodes_objects == NULL) {
		return;
	}
	for (i32 i = 0; i < g_project->mesh_nodes->length && i < g_project->mesh_nodes_objects->length; ++i) {
		i32 index = g_project->mesh_nodes_objects->buffer[i];
		if (index < 0 || index >= g_project->_->paint_objects->length) {
			continue;
		}
		mesh_object_t *o    = g_project->_->paint_objects->buffer[index];
		mesh_nodes_t  *slot = nodes_mesh_get(o, true);
		slot->canvas        = util_clone_canvas(g_project->mesh_nodes->buffer[i]); // Project data is temporary
		import_arm_init_nodes(slot->canvas->nodes);
		for (i32 j = 0; j < slot->canvas->nodes->length; ++j) {
			ui_node_t *n = slot->canvas->nodes->buffer[j];
			if (string_equals(n->type, "MESH_OBJECT") && n->buttons->buffer[0]->default_value->length > 2) {
				n->buttons->buffer[0]->default_value->buffer[1] = -1; // Object uids are not kept in files
			}
		}
	}
	// Scene mesh nodes need all the canvases in place
	for (i32 i = 0; i < g_project->mesh_nodes->length && i < g_project->mesh_nodes_objects->length; ++i) {
		i32 index = g_project->mesh_nodes_objects->buffer[i];
		if (index >= 0 && index < g_project->_->paint_objects->length) {
			nodes_mesh_update(g_project->_->paint_objects->buffer[index], true);
		}
	}
}

// Script nodes are skipped until the scripts of a loaded project are trusted
bool nodes_mesh_canvases_have_script(ui_node_canvas_t_array_t *canvases) {
	for (i32 i = 0; canvases != NULL && i < canvases->length; ++i) {
		ui_node_canvas_t *c = canvases->buffer[i];
		for (i32 j = 0; c != NULL && j < c->nodes->length; ++j) {
			if (string_equals(c->nodes->buffer[j]->type, "MESH_SCRIPT")) {
				return true;
			}
		}
	}
	return false;
}

void nodes_mesh_update_scripts() {
	for (i32 i = 0; nodes_mesh_slots != NULL && i < nodes_mesh_slots->length; ++i) {
		mesh_nodes_t  *slot = nodes_mesh_slots->buffer[i];
		mesh_object_t *o    = nodes_mesh_object(slot->uid);
		if (o != NULL && nodes_mesh_canvases_have_script(any_array_create_from_raw_tmp((void *[]){slot->canvas}, 1))) {
			nodes_mesh_update(o, true);
		}
	}
}

ui_node_t *nodes_mesh_create_node(mesh_object_t *o, char *type) {
	nodes_mesh_init();
	if (string_equals(type, "MESH_OUTPUT")) {
		return NULL;
	}
	mesh_nodes_t *slot = nodes_mesh_get(o, true);
	for (i32 i = 0; i < nodes_mesh_list->length; ++i) {
		ui_node_t_array_t *category = nodes_mesh_list->buffer[i];
		for (i32 j = 0; j < category->length; ++j) {
			if (string_equals(category->buffer[j]->type, type)) {
				ui_node_t *node = ui_nodes_make_node(category->buffer[j], slot->nodes, slot->canvas);
				any_array_push(slot->canvas->nodes, node);
				return node;
			}
		}
	}
	return NULL;
}

char *nodes_mesh_reference() {
	return "//\n"
	       "// Mesh nodes build the mesh of an object from its original mesh, each object has its own canvas with a\n"
	       "// pre-created MESH_INPUT (original mesh) and MESH_OUTPUT node. Build it with script_mesh_create_node_at(),\n"
	       "// script_mesh_get_node(), script_mesh_connect() and apply with script_mesh_update(). Node values are set with\n"
	       "// script_material_set_float/set_vector/set_button/set_text, which work on any node.\n"
	       "//\n"
	       "// MESH_SCRIPT runs a minic script on the mesh linked to its input, the script text is button 0:\n"
	       "//     mesh_geo_t *g = mesh_geo_input(); // Editable copy of the input, float g->pos[vertex_count * 3],\n"
	       "//                                       // g->nor[vertex_count * 3], g->tex[vertex_count * 2], int g->ind[index_count]\n"
	       "//     mesh_geo_output(g);               // Mesh passed on by the node, the input mesh when not called\n"
	       "//     Arrays are not bounds checked. Counts can be lowered, to grow a mesh fill a new mesh_geo_create(vertex_count, index_count).\n"
	       "//     Triangles are counter clockwise, a mesh fits the default view at a size of about 1.\n"
	       "//     mesh_geo_* functions return a new mesh and leave their arguments unchanged, meshes are freed when the script ends.\n"
	       "//     Except mesh_geo_append(dst, src, m), which adds src to dst in place (dst counts grow, false when its capacity is\n"
	       "//     full), transformed by float m[12]: the x, y, z axes then the translation. Use it to place many copies of a mesh.\n"
	       "//     mesh_geo_shape: 0 Box, 1 Sphere, 2 Cylinder, 3 Cone, 4 Torus, 5 Plane. mesh_geo_mirror axis: 0 X, 1 Y, 2 Z.\n"
	       "//     mesh_geo_boolean op: MESH_BOOLEAN_UNION, MESH_BOOLEAN_DIFFERENCE, MESH_BOOLEAN_INTERSECT. Rotations are in degrees.\n"
	       "//     Call mesh_geo_normals(g, smooth) after moving vertices. The input passes through when the script has an error.\n"
	       "//     Inputs: script_mesh_add_input(o, sc, \"VALUE\", \"Height\") adds a VALUE, VECTOR, RGBA or MESH socket after the mesh input\n"
	       "//     (socket 1, 2, ..), the Add button of the node does the same. The script reads them by name with\n"
	       "//     mesh_geo_input_value(\"Height\") and mesh_geo_input_vector(\"Offset\", i), i is 0 - 2 for x y z, 0 - 3 for r g b a.\n"
	       "//     Value, Math and Vector nodes can be linked into them.\n"
	       "//     script_timeline_get_frame() returns the timeline frame, a script that calls it is rerun when the frame changes.\n"
	       "//     Globals start over on every run. void *mesh_geo_state(int size) returns a zeroed block of size bytes that the\n"
	       "//     node keeps between its runs, to cache work such as simulated frames. A different size gives a new zeroed block,\n"
	       "//     store the inputs it was built from in it to tell when to start over.\n"
	       "//     A \"MESH\" input takes a second mesh, such as a MESH_OBJECT node (button 0 is the scene mesh index), read with\n"
	       "//     mesh_geo_t *path = mesh_geo_input_mesh(\"Path\"); it is in the space of the edited object, empty when not linked.\n"
	       "//     TEX_IMAGE (Image Texture) node: button 0 is the index of a project texture (Textures tab), outputs Color (0) and\n"
	       "//     Alpha (1). Link one into an RGBA or VALUE input and read its pixels with mesh_geo_image_t *img =\n"
	       "//     mesh_geo_input_image(\"Mask\"); img->width, img->height, mesh_geo_image_sample(img, u, v,\n"
	       "//     channel) samples the nearest pixel at a mesh uv (0 - 1, beyond for 16 and 32 bit layers).\n"
	       "//     The Alpha output gives alpha in all channels. An input without an image gives a 1x1 image of its value.\n"
	       "//     LAYER_MASK (Layer Mask) node: button 0 is the index of a project layer, reads like an image with the red channel\n"
	       "//     in all four, mesh_geo_input_image() of a VALUE input linked to it.\n"
	       "//     Example:\n"
	       "//     mesh_object_t *o = script_get_context()->paint_object;\n"
	       "//     ui_node_t *sc = script_mesh_create_node_at(o, \"MESH_SCRIPT\", 300.0, 100.0);\n"
	       "//     script_material_set_text(sc, 0, \"void main() {\\n"
	       "mesh_geo_t *g = mesh_geo_subdivide(mesh_geo_input(), 2);\\n"
	       "for (int i = 0; i < g->vertex_count; i++) {\\n"
	       "g->pos[i * 3 + 2] += sin(g->pos[i * 3] * 8.0) * 0.05;\\n"
	       "}\\n"
	       "g = mesh_geo_boolean(mesh_geo_normals(g, true), mesh_geo_shape(1, 0.8, 0.8, 0.8, 32), MESH_BOOLEAN_DIFFERENCE);\\n"
	       "mesh_geo_output(g);\\n"
	       "}\");\n"
	       "//     script_mesh_connect(o, script_mesh_get_node(o, \"MESH_INPUT\"), 0, sc, 0);\n"
	       "//     script_mesh_connect(o, sc, 0, script_mesh_get_node(o, \"MESH_OUTPUT\"), 0);\n"
	       "//     script_mesh_update(o);\n";
}
