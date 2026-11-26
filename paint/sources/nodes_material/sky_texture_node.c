
#include "../global.h"

enum {
	SKY_TRANSMITTANCE,
	SKY_MULTISCATTER,
	SKY_ENVMAP,
	SKY_PIPES
};

static gpu_compute_pipeline_t sky_node_pipes[SKY_PIPES];
static bool                   sky_node_loaded = false;
static bool                   sky_node_failed = false;
static any_map_t             *sky_node_baked  = NULL; // Preview id -> hash of parameters used to bake

static char *sky_texture_node_sample(ui_node_t *node) {
	char *store = parser_material_store_var_name(node);
	char *key   = string_tmp("%s_sky", store);
	if (string_array_index_of(parser_material_parsed, key) == -1) {
		any_array_push(parser_material_parsed, string_copy(key));
		char *tex_name = string_tmp("texbake_%s", make_material_node_preview_id(node, NULL));
		node_shader_add_texture(parser_material_kong, tex_name, string_tmp("_%s", tex_name));
		node_shader_add_constant(parser_material_kong, string_tmp("float2 %s_size", tex_name), string_tmp("_size(_%s)", tex_name));

		char *uv_src;
		if (node->inputs->length > 0 && parser_material_get_input_link(node->inputs->buffer[0]) != NULL) {
			uv_src = string_tmp("(%s).xy", parser_material_parse_vector_input(node->inputs->buffer[0]));
		}
		else {
			uv_src = parser_material_atlas_uv ? "tex_coord_atlas" : "tex_coord";
		}

		// Equirectangular map wraps horizontally and clamps vertically to avoid pole bleeding
		parser_material_write(parser_material_kong, string_tmp("float2 %s_uv_in = %s;", store, uv_src));
		parser_material_write(parser_material_kong, string_tmp("float %s_hy = 0.5 / constants.%s_size.y;", store, tex_name));
		parser_material_write(parser_material_kong,
		                      string_tmp("float2 %s_uv = float2(frac(%s_uv_in.x), clamp(%s_uv_in.y, %s_hy, 1.0 - %s_hy));", store, store, store, store, store));
		if (parser_material_is_frag) {
			parser_material_write(parser_material_kong, string_tmp("float4 %s_t = sample(%s, sampler_linear, %s_uv);", store, tex_name, store));
		}
		else {
			parser_material_write(parser_material_kong, string_tmp("float4 %s_t = sample_lod(%s, sampler_linear, %s_uv, 0.0);", store, tex_name, store));
		}
	}
	return store;
}

char *sky_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *store = sky_texture_node_sample(node);
	return string_tmp("%s_t.rgb", store);
}

char *sky_texture_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *store = sky_texture_node_sample(node);
	return string_tmp("%s_t.a", store);
}

static f32 sky_texture_node_input_val(ui_node_canvas_t *canvas, ui_node_t *node, i32 idx) {
	ui_node_link_t *l = nodes_mesh_link(canvas, node, idx);
	if (l != NULL) {
		ui_node_t *from = ui_get_node(canvas->nodes, l->from_id);
		if (from != NULL && string_equals(from->type, "VALUE") && from->outputs->length > 0 && from->outputs->buffer[0]->default_value != NULL) {
			return from->outputs->buffer[0]->default_value->buffer[0];
		}
	}
	return node->inputs->buffer[idx]->default_value->buffer[0];
}

static u32 sky_texture_node_hash(ui_node_canvas_t *canvas, ui_node_t *node, i32 res_x, i32 res_y) {
	u32 hash = 2166136261u;
	hash     = nodes_mesh_hash_bytes(hash, &res_x, sizeof(i32));
	hash     = nodes_mesh_hash_bytes(hash, &res_y, sizeof(i32));
	for (i32 i = 1; i < node->inputs->length; ++i) {
		f32 v = sky_texture_node_input_val(canvas, node, i);
		hash  = nodes_mesh_hash_bytes(hash, &v, sizeof(f32));
	}
	for (i32 i = 0; i < node->buttons->length; ++i) {
		ui_node_button_t *b = node->buttons->buffer[i];
		if (b->default_value != NULL) {
			hash = nodes_mesh_hash_bytes(hash, b->default_value->buffer, b->default_value->length * sizeof(f32));
		}
	}
	return hash;
}

static void sky_node_load() {
	char *names[] = {"sky_transmittance", "sky_multiscatter", "sky_envmap"};
	for (i32 i = 0; i < SKY_PIPES; ++i) {
		char     *file   = string_tmp("%s%s", names[i], sys_shader_ext());
		buffer_t *shader = data_get_blob(file);
		if (shader == NULL || !gpu_compute_pipeline_init(&sky_node_pipes[i], shader->buffer, shader->length)) {
			iron_log("Could not load compute shader %s", file);
			sky_node_failed = true;
		}
	}
	sky_node_loaded = true;
}

static void sky_node_dispatch(i32 pipe, gpu_texture_t *target, gpu_texture_t *a, gpu_texture_t *b, f32 *params) {
	gpu_compute_set_pipeline(&sky_node_pipes[pipe]);
	gpu_compute_set_texture(0, target);
	if (a != NULL) {
		gpu_compute_set_texture(1, a);
	}
	if (b != NULL) {
		gpu_compute_set_texture(2, b);
	}
	gpu_compute_set_constants(params, sizeof(f32) * 20);
	gpu_compute_dispatch(target->width, target->height);
}

gpu_texture_t *sky_texture_node_preview_image(ui_node_t *node, ui_node_t_array_t *parents) {
	if (g_context->node_previews == NULL) {
		return NULL;
	}
	char *id = make_material_node_preview_id(node, parents);
	return any_map_get(g_context->node_previews, id);
}

void sky_texture_node_bake(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents, char *id) {
	i32 res_x = config_get_texture_res_x();
	i32 res_y = math_max(config_get_texture_res_y() / 2, 1);

	ui_node_canvas_t *canvas = group != NULL ? group : g_context->material->canvas;
	u32               hash   = sky_texture_node_hash(canvas, node, res_x, res_y);
	for (i32 i = 0; parents != NULL && i < parents->length; ++i) {
		ui_node_t *p = parents->buffer[i];
		for (i32 j = 0; j < p->inputs->length; ++j) {
			f32_array_t *v = p->inputs->buffer[j]->default_value;
			if (v != NULL) {
				hash = nodes_mesh_hash_bytes(hash, v->buffer, v->length * sizeof(f32));
			}
		}
	}

	gpu_texture_t *previous = any_map_get(g_context->node_previews, id);
	gpu_texture_t *out      = make_material_node_preview_target(id, res_x, res_y, GPU_TEXTURE_FORMAT_RGBA64);
	gpu_texture_t *trans    = make_material_node_preview_target(string_tmp("%s_trans", id), 256, 64, GPU_TEXTURE_FORMAT_RGBA128);
	gpu_texture_t *mscat    = make_material_node_preview_target(string_tmp("%s_mscat", id), 32, 32, GPU_TEXTURE_FORMAT_RGBA128);

	if (sky_node_baked == NULL) {
		sky_node_baked = any_map_create();
	}
	u32_array_t *baked = any_map_get(sky_node_baked, id);
	if (baked == NULL) {
		baked = u32_array_create(1);
		any_map_set(sky_node_baked, string_copy(id), baked);
	}
	else if (previous == out && baked->buffer[0] == hash) {
		return;
	}
	baked->buffer[0] = hash;

	if (!sky_node_loaded) {
		sky_node_load();
	}
	if (sky_node_failed || gpu_in_use) {
		draw_begin(out, true, 0xff000000);
		draw_end();
		return;
	}

	f32 deg_to_rad    = math_pi() / 180.0;
	f32 sun_elevation = sky_texture_node_input_val(canvas, node, 1) * deg_to_rad;
	f32 sun_rotation  = sky_texture_node_input_val(canvas, node, 2) * deg_to_rad;
	f32 sun_size      = math_max(sky_texture_node_input_val(canvas, node, 3), 0.05) * 0.5 * deg_to_rad; // Angular radius
	f32 sun_intensity = math_max(sky_texture_node_input_val(canvas, node, 4), 0.0);
	f32 altitude      = math_min(math_max(sky_texture_node_input_val(canvas, node, 5), 0.0), 60.0);
	f32 air           = math_max(sky_texture_node_input_val(canvas, node, 6), 0.0);
	f32 dust          = math_max(sky_texture_node_input_val(canvas, node, 7), 0.0);
	f32 ozone         = math_max(sky_texture_node_input_val(canvas, node, 8), 0.0);
	f32 cloud_cov     = math_min(math_max(sky_texture_node_input_val(canvas, node, 9), 0.0), 1.0);
	f32 cloud_dens    = math_max(sky_texture_node_input_val(canvas, node, 10), 0.0);
	f32 cloud_scale   = math_max(sky_texture_node_input_val(canvas, node, 11), 0.05);
	f32 cloud_alt     = math_min(math_max(sky_texture_node_input_val(canvas, node, 12), 0.2), 15.0);
	f32 cloud_seed    = sky_texture_node_input_val(canvas, node, 13);
	f32 sun_disc      = (node->buttons->length > 0 && node->buttons->buffer[0]->default_value->buffer[0] > 0.5) ? 1.0 : 0.0;

	// Sun direction
	f32 cos_el = math_cos(sun_elevation);
	f32 sx     = cos_el * math_cos(sun_rotation);
	f32 sy     = -cos_el * math_sin(sun_rotation);
	f32 sz     = math_sin(sun_elevation);

	f32 params[20] = {
	    sx,
	    sy,
	    sz,
	    sun_size, // sun_dir_size
	    sun_intensity,
	    sun_disc,
	    altitude,
	    0.18, // sun_params (ground albedo = 0.18)
	    air,
	    dust,
	    ozone,
	    0.80, // atmo_params (Mie g = 0.80)
	    cloud_cov,
	    cloud_dens,
	    cloud_scale,
	    cloud_alt, // cloud_params0
	    cloud_seed,
	    3.2,  // cloud thickness (km)
	    0.55, // cirrus contribution
	    0.0,
	};

	sky_node_dispatch(SKY_TRANSMITTANCE, trans, NULL, NULL, params);
	sky_node_dispatch(SKY_MULTISCATTER, mscat, trans, NULL, params);
	sky_node_dispatch(SKY_ENVMAP, out, trans, mscat, params);
	ui_nodes_hwnd->redraws  = 2;
	ui_view2d_hwnd->redraws = 2;
}

void sky_texture_node_init() {
	ui_node_t *n = nodes_mesh_def(nodes_material_texture, _tr("Sky Texture"), "TEX_SKY", 0xff4982a0, NULL);

	// Optional UV / equirectangular coordinate input
	any_array_push(n->inputs, ALLOC_INIT(ui_node_socket_t, {.name          = _tr("Vector"),
	                                                        .type          = "VECTOR",
	                                                        .color         = 0xff6363c7,
	                                                        .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                        .min           = 0.0,
	                                                        .max           = 1.0,
	                                                        .precision     = 100,
	                                                        .display       = 0}));

	// Sun and atmosphere parameters
	nodes_mesh_def_value(n, _tr("Sun Elevation"), 25.0, -10.0, 90.0, 10);
	nodes_mesh_def_value(n, _tr("Sun Rotation"), 0.0, 0.0, 360.0, 10);
	nodes_mesh_def_value(n, _tr("Sun Size"), 0.545, 0.1, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Sun Intensity"), 1.0, 0.0, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Altitude"), 0.0, 0.0, 60.0, 100);
	nodes_mesh_def_value(n, _tr("Air"), 1.0, 0.0, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Dust"), 1.0, 0.0, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Ozone"), 1.0, 0.0, 10.0, 100);

	// Volumetric cloud parameters
	nodes_mesh_def_value(n, _tr("Cloud Coverage"), 0.45, 0.0, 1.0, 100);
	nodes_mesh_def_value(n, _tr("Cloud Density"), 0.6, 0.0, 2.0, 100);
	nodes_mesh_def_value(n, _tr("Cloud Scale"), 1.0, 0.1, 5.0, 100);
	nodes_mesh_def_value(n, _tr("Cloud Altitude"), 2.0, 0.5, 10.0, 100);
	nodes_mesh_def_value(n, _tr("Cloud Seed"), 0.0, 0.0, 100.0, 100);

	// Outputs: HDRI Sky Color + Cloud Mask
	any_array_push(n->outputs, ALLOC_INIT(ui_node_socket_t, {.name          = _tr("Color"),
	                                                         .type          = "RGBA",
	                                                         .color         = 0xffc7c729,
	                                                         .default_value = f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0),
	                                                         .min           = 0.0,
	                                                         .max           = 1.0,
	                                                         .precision     = 100}));
	any_array_push(
	    n->outputs,
	    ALLOC_INIT(
	        ui_node_socket_t,
	        {.name = _tr("Clouds"), .type = "VALUE", .color = 0xffa1a1a1, .default_value = f32_array_create_x(0.0), .min = 0.0, .max = 1.0, .precision = 100}));

	// Toggle sun disc visibility
	nodes_mesh_def_button(n, _tr("Sun Disc"), "BOOL", 1.0, NULL);
	n->buttons->buffer[0]->output = -1;

	any_map_set(parser_material_node_vectors, "TEX_SKY", sky_texture_node_vector);
	any_map_set(parser_material_node_values, "TEX_SKY", sky_texture_node_value);
}
