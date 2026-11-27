
#include "../global.h"

#define TERRAIN_NODE_MAX_RES 1024

enum {
	TERRAIN_INIT,
	TERRAIN_FILL,
	TERRAIN_FLOW,
	TERRAIN_THERMAL,
	TERRAIN_PACK,
	TERRAIN_PIPES
};

static gpu_compute_pipeline_t terrain_node_pipes[TERRAIN_PIPES];
static bool                   terrain_node_loaded = false;
static bool                   terrain_node_failed = false;
static any_map_t             *terrain_node_baked  = NULL; // Preview id -> hash of what the image was baked from

char *terrain_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *height = parser_material_parse_value_input(node->inputs->buffer[0], false);
	if (parser_material_terrain_passthrough) {
		return height;
	}
	char *store = parser_material_store_var_name(node);
	char *key   = string_tmp("%s_terrain", store);
	if (string_array_index_of(parser_material_parsed, key) == -1) {
		any_array_push(parser_material_parsed, string_copy(key));
		char *tex_name = string_tmp("texbake_%s", make_material_node_preview_id(node, NULL));
		node_shader_add_texture(parser_material_kong, tex_name, string_tmp("_%s", tex_name));
		node_shader_add_constant(parser_material_kong, string_tmp("float2 %s_size", tex_name), string_tmp("_size(_%s)", tex_name));
		// The terrain does not repeat, the border does not blend with the opposite side
		parser_material_write(parser_material_kong, string_tmp("float2 %s_half = float2(0.5, 0.5) / constants.%s_size;", store, tex_name));
		parser_material_write(parser_material_kong, string_tmp("float2 %s_uv = clamp(frac(%s), %s_half, float2(1.0, 1.0) - %s_half);", store,
		                                                       parser_material_atlas_uv ? "tex_coord_atlas" : "tex_coord", store, store));
		parser_material_write(parser_material_kong, string_tmp("float4 %s_t = sample(%s, sampler_linear, %s_uv);", store, tex_name, store));
	}
	// Change in height, flow, slope and lake depth, lakes are flat water surfaces
	if (socket == node->outputs->buffer[1]) {
		return string_tmp("%s_t.g", store);
	}
	if (socket == node->outputs->buffer[2]) {
		return string_tmp("clamp(%s_t.a * 1000.0, 0.0, 1.0)", store);
	}
	if (socket == node->outputs->buffer[3]) {
		return string_tmp("clamp(0.0 - %s_t.r * 20.0, 0.0, 1.0)", store);
	}
	if (socket == node->outputs->buffer[4]) {
		return string_tmp("clamp(%s_t.r * 20.0, 0.0, 1.0)", store);
	}
	if (socket == node->outputs->buffer[5]) {
		return string_tmp("%s_t.b", store);
	}
	return string_tmp("(%s + %s_t.r + %s_t.a)", height, store, store); // Input at full resolution keeps its detail
}

static u32 terrain_node_hash_node(u32 hash, ui_node_t *node) {
	hash = nodes_mesh_hash_bytes(hash, node->type, string_length(node->type));
	hash = nodes_mesh_hash_bytes(hash, node->name, string_length(node->name));
	for (i32 i = 0; i < node->inputs->length; ++i) {
		f32_array_t *v = node->inputs->buffer[i]->default_value;
		if (v != NULL) {
			hash = nodes_mesh_hash_bytes(hash, v->buffer, v->length * sizeof(f32));
		}
	}
	for (i32 i = 0; i < node->buttons->length; ++i) {
		ui_node_button_t *b = node->buttons->buffer[i];
		if (b->default_value != NULL) {
			hash = nodes_mesh_hash_bytes(hash, b->default_value->buffer, b->default_value->length * sizeof(f32));
		}
		if (b->data != NULL) {
			hash = nodes_mesh_hash_bytes(hash, b->data->buffer, b->data->length);
		}
	}
	return hash;
}

// Covers the nodes linked into the terrain node, groups as a whole
static u32 terrain_node_hash(u32 hash, ui_node_canvas_t *canvas, ui_node_t *node, i32 depth) {
	hash = terrain_node_hash_node(hash, node);
	if (string_equals(node->type, "GROUP")) {
		node_group_t *g = project_get_material_group_by_name(node->name);
		for (i32 i = 0; g != NULL && i < g->canvas->nodes->length; ++i) {
			hash = terrain_node_hash_node(hash, g->canvas->nodes->buffer[i]);
		}
		for (i32 i = 0; g != NULL && i < g->canvas->links->length; ++i) {
			hash = nodes_mesh_hash_bytes(hash, &g->canvas->links->buffer[i]->from_id, sizeof(i32) * 4);
		}
	}
	for (i32 i = 0; i < node->inputs->length && depth < 64; ++i) {
		ui_node_link_t *l    = nodes_mesh_link(canvas, node, i);
		ui_node_t      *from = l != NULL ? ui_get_node(canvas->nodes, l->from_id) : NULL;
		if (from != NULL) {
			hash = nodes_mesh_hash_bytes(hash, &l->from_socket, sizeof(i32));
			hash = terrain_node_hash(hash, canvas, from, depth + 1);
		}
	}
	return hash;
}

static void terrain_node_load() {
	char *names[] = {"terrain_init", "terrain_fill", "terrain_flow", "terrain_thermal", "terrain_pack"};
	for (i32 i = 0; i < TERRAIN_PIPES; ++i) {
		char     *file   = string_tmp("%s%s", names[i], sys_shader_ext());
		buffer_t *shader = data_get_blob(file);
		if (shader == NULL || !gpu_compute_pipeline_init(&terrain_node_pipes[i], shader->buffer, shader->length)) {
			iron_log("Could not load compute shader %s", file);
			terrain_node_failed = true;
		}
	}
	terrain_node_loaded = true;
}

static void terrain_node_dispatch(i32 pipe, gpu_texture_t *target, gpu_texture_t *a, gpu_texture_t *b, f32 *params) {
	gpu_compute_set_pipeline(&terrain_node_pipes[pipe]);
	gpu_compute_set_texture(0, target);
	gpu_compute_set_texture(1, a);
	gpu_compute_set_texture(2, b);
	gpu_compute_set_constants(params, sizeof(f32) * 12);
	gpu_compute_dispatch(target->width, target->height);
}

void terrain_node_bake(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents, char *id) {
	i32 res = math_min(math_max(config_get_texture_res_x(), config_get_texture_res_y()), TERRAIN_NODE_MAX_RES);

	ui_node_canvas_t *canvas = group != NULL ? group : g_context->material->canvas;
	u32               hash   = terrain_node_hash(2166136261u, canvas, node, 0);
	for (i32 i = 0; parents != NULL && i < parents->length; ++i) {
		hash = terrain_node_hash_node(hash, parents->buffer[i]); // Values reaching the group inputs
	}
	hash = nodes_mesh_hash_bytes(hash, &res, sizeof(i32));

	gpu_texture_t *previous = any_map_get(g_context->node_previews, id);
	gpu_texture_t *out      = make_material_node_preview_target(id, res, res, GPU_TEXTURE_FORMAT_RGBA64);
	if (terrain_node_baked == NULL) {
		terrain_node_baked = any_map_create();
	}
	u32_array_t *baked = any_map_get(terrain_node_baked, id);
	if (baked == NULL) {
		baked = u32_array_create(1);
		any_map_set(terrain_node_baked, string_copy(id), baked);
	}
	else if (previous == out && baked->buffer[0] == hash) {
		return;
	}
	baked->buffer[0] = hash;

	// Reuse an identical terrain bake from another material
	if (!gpu_in_use) {
		string_array_t *keys = map_keys(terrain_node_baked);
		for (i32 i = 0; i < keys->length; ++i) {
			char *other_id = keys->buffer[i];
			if (string_equals(other_id, id)) {
				continue;
			}
			u32_array_t   *other_baked = any_map_get(terrain_node_baked, other_id);
			gpu_texture_t *other_tex   = other_baked != NULL && other_baked->buffer[0] == hash ? any_map_get(g_context->node_previews, other_id) : NULL;
			if (other_tex != NULL && other_tex != out && other_tex->width == res && other_tex->height == res &&
			    other_tex->format == GPU_TEXTURE_FORMAT_RGBA64) {
				draw_begin(out, false, 0);
				draw_set_pipeline(pipes_copy64);
				draw_image(other_tex, 0, 0);
				draw_set_pipeline(NULL);
				draw_end();
				array_free(keys);
				free(keys);
				return;
			}
		}
		array_free(keys);
		free(keys);
	}

	gpu_texture_t *src = make_material_node_preview_target(string_tmp("%s_src", id), res, res, GPU_TEXTURE_FORMAT_RGBA128);
	gpu_texture_t *a   = make_material_node_preview_target(string_tmp("%s_a", id), res, res, GPU_TEXTURE_FORMAT_RGBA128);
	gpu_texture_t *b   = make_material_node_preview_target(string_tmp("%s_b", id), res, res, GPU_TEXTURE_FORMAT_RGBA128);

	parser_material_terrain_passthrough = true;
	util_render_make_node_preview(canvas, node, src, group, parents);
	parser_material_terrain_passthrough = false;

	if (!terrain_node_loaded) {
		terrain_node_load();
	}
	if (terrain_node_failed || gpu_in_use) {
		draw_begin(out, true, 0x00000000); // Input height as is
		draw_end();
		return;
	}

	f32 relief     = math_max(node->inputs->buffer[1]->default_value->buffer[0], 0.001);
	i32 iterations = (i32)node->inputs->buffer[2]->default_value->buffer[0];
	f32 erosion    = node->inputs->buffer[3]->default_value->buffer[0];
	f32 deposition = node->inputs->buffer[4]->default_value->buffer[0];
	f32 talus      = node->inputs->buffer[5]->default_value->buffer[0];
	f32 angle      = (70.0 - 50.0 * math_min(math_max(talus, 0.0), 1.0)) * math_pi() / 180.0;
	f32 n          = (f32)res;
	f32 params[12] = {erosion * 0.002,
	                  deposition * 0.004,
	                  deposition * 0.02,
	                  1.0, // Erosion, deposition, deposition in lakes, transport capacity
	                  math_tan(angle) / (relief * n),
	                  talus > 0.0 ? 0.06 : 0.0,
	                  0.001,
	                  0.00001, // Talus per cell, thermal rate, edge drop, routing slope
	                  relief,
	                  1.1,
	                  0.5,
	                  n};

	terrain_node_dispatch(TERRAIN_INIT, a, src, NULL, params);
	// Fill the depressions of the routing surface before the water starts flowing
	for (i32 i = 0; i < res; ++i) {
		terrain_node_dispatch(TERRAIN_FILL, b, a, NULL, params);
		gpu_texture_t *t = a;
		a                = b;
		b                = t;
	}
	for (i32 i = 0; i < iterations; ++i) {
		terrain_node_dispatch(TERRAIN_FLOW, b, a, NULL, params);
		terrain_node_dispatch(TERRAIN_THERMAL, a, b, NULL, params);
	}
	terrain_node_dispatch(TERRAIN_PACK, out, a, src, params);
}

void terrain_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_material_texture, _tr("Terrain"), "TERRAIN", 0xff4982a0, NULL);
	nodes_mesh_def_value(n, _tr("Height"), 0.5, 0.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Relief"), 0.25, 0.0, 1.0, 100); // Height range over the width, as displaced
	nodes_mesh_def_value(n, _tr("Iterations"), 3000, 0, 10000, 1);
	nodes_mesh_def_value(n, _tr("Erosion"), 0.5, 0.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Deposition"), 0.35, 0.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Talus"), 0.5, 0.0, 1.0, 100);
	char *outputs[] = {_tr("Height"), _tr("Flow"), _tr("Water"), _tr("Erosion"), _tr("Deposition"), _tr("Slope")};
	for (i32 i = 0; i < 6; ++i) {
		any_array_push(n->outputs, ALLOC_INIT(ui_node_socket_t, {.name          = outputs[i],
		                                                         .type          = "VALUE",
		                                                         .color         = 0xffa1a1a1,
		                                                         .default_value = f32_array_create_x(0.0),
		                                                         .min           = 0.0,
		                                                         .max           = 1.0,
		                                                         .precision     = 100}));
	}
	any_map_set(parser_material_node_values, "TERRAIN", terrain_node_value);
}
