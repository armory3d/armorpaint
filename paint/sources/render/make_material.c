
#include "../global.h"

static shader_context_t *make_material_default_scon = NULL;
static shader_context_t *make_material_saved_scon   = NULL;

// Objects without an override material draw with the pass shaders
bool make_material_is_default(shader_data_t *m) {
	return m == g_project->_->materials->buffer[0]->data;
}

bool make_material_get_mout() {
	for (i32 i = 0; i < g_context->material->canvas->nodes->length; ++i) {
		ui_node_t *n = g_context->material->canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			return true;
		}
	}
	return false;
}

void make_material_delete_context_on_next_frame(shader_context_t *c) {
	shader_context_delete(c);

	if (c->bind_textures != NULL) {
		for (i32 i = 0; i < c->bind_textures->length; ++i) {
			bind_tex_t *tex = c->bind_textures->buffer[i];
			free(tex->name);
			free(tex->file);
			free(tex);
		}
		array_free(c->bind_textures);
		free(c->bind_textures);
		c->bind_textures = NULL;
	}
	if (c->_ != NULL && c->_->textures != NULL) {
		array_free(c->_->textures);
		free(c->_->textures);
		c->_->textures = NULL;
	}

	if (!c->shader_from_source) {
		return;
	}

	if (c->constants != NULL) {
		for (i32 i = 0; i < c->constants->length; ++i) {
			shader_const_t *sc = c->constants->buffer[i];
			free(sc->name);
			free(sc->type);
			free(sc->link);
			free(sc);
		}
		array_free(c->constants);
		free(c->constants);
	}
	if (c->texture_units != NULL) {
		for (i32 i = 0; i < c->texture_units->length; ++i) {
			tex_unit_t *tu = c->texture_units->buffer[i];
			free(tu->name);
			free(tu->link);
			free(tu);
		}
		array_free(c->texture_units);
		free(c->texture_units);
	}
	if (c->vertex_elements != NULL) {
		for (i32 i = 0; i < c->vertex_elements->length; ++i) {
			free(c->vertex_elements->buffer[i]);
		}
		array_free(c->vertex_elements);
		free(c->vertex_elements);
	}
	array_delete(c->color_writes_red);
	array_delete(c->color_writes_green);
	array_delete(c->color_writes_blue);
	array_delete(c->color_writes_alpha);
	array_delete(c->color_attachments);
	if (c->_ != NULL) {
		if (c->_->constants != NULL) {
			array_free(c->_->constants);
			free(c->_->constants);
		}
		if (c->_->tex_units != NULL) {
			array_free(c->_->tex_units);
			free(c->_->tex_units);
		}
		free(c->_);
	}
	free(c);
}

void make_material_delete_context(shader_context_t *c) {
	// Ensure pipeline is no longer in use
	sys_notify_on_next_frame(&make_material_delete_context_on_next_frame, c);
}

void make_material_parse_mesh_material() {
	if (make_material_mesh_scons == NULL) {
		make_material_mesh_scons = any_array_create(0);
	}
	for (i32 i = 0; i < make_material_mesh_scons->length; ++i) {
		if (make_material_mesh_scons->buffer[i] != NULL) {
			make_material_delete_context(make_material_mesh_scons->buffer[i]);
		}
	}
	make_material_mesh_scons->length = 0;

	// The first pass decides the layer pass count
	for (i32 i = 0; i == 0 || i < make_mesh_layer_pass_count; ++i) {
		material_t            *mm  = ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
		node_shader_context_t *con = make_mesh_run(mm, i);
		shader_context_load(con->data);
		any_array_push(make_material_mesh_scons, con->data);
		node_shader_context_free(con);
		free(mm);
	}

	g_context->ddirty  = 2;
	g_context->rtdirty = 1;

	if (make_material_transluc_used) {
		make_material_parse_depth_material();
	}
}

void make_material_parse_mesh_preview_material() {
	if (!make_material_get_mout()) {
		return;
	}

	// Replaces the first mesh pass shader, make_material_parse_mesh_material() restores it
	material_t            *sd  = ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
	node_shader_context_t *con = make_mesh_preview_run(sd, false);
	shader_context_load(con->data);
	if (make_material_mesh_scons == NULL) {
		make_material_mesh_scons = any_array_create(0);
	}
	if (make_material_mesh_scons->length == 0) {
		any_array_push(make_material_mesh_scons, NULL);
	}
	if (make_material_mesh_scons->buffer[0] != NULL) {
		make_material_delete_context(make_material_mesh_scons->buffer[0]);
	}
	make_material_mesh_scons->buffer[0] = con->data;
	node_shader_context_free(con);
	free(sd);
}

static bool make_material_bake_linked = false;

char *make_material_node_preview_id(ui_node_t *node, ui_node_t_array_t *parents) {
	return string_tmp("m%d_%s", g_context->material->id, parser_material_node_name(node, parents));
}

static gpu_texture_t *make_material_node_preview_target(char *id, i32 res_x, i32 res_y, gpu_texture_format_t format) {
	gpu_texture_t *image = any_map_get(g_context->node_previews, id);
	any_array_push(g_context->node_previews_used, id);
	if (image == NULL || image->width != res_x || image->height != res_y || image->format != format) {
		if (image != NULL) {
			gpu_delete_texture(image);
		}
		image = gpu_create_render_target(res_x, res_y, format);
		any_map_set(g_context->node_previews, string_copy(id), image);
	}
	return image;
}

void make_material_bake_node_preview(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	if (string_equals(node->type, "BLUR")) {
		char *id     = make_material_node_preview_id(node, parents);
		i32   passes = blur_node_seamless_passes(node);
		i32   res_x  = math_floor(config_get_texture_res_x() / 4.0);
		i32   res_y  = math_floor(config_get_texture_res_y() / 4.0);
		// Seamless blur renders the input to _src, then diffuses it into the sampled texture
		gpu_texture_t *image = make_material_node_preview_target(passes > 0 ? string_tmp("%s_src", id) : id, res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);

		parser_material_blur_passthrough = true;
		util_render_make_node_preview(g_context->material->canvas, node, image, group, parents);
		parser_material_blur_passthrough = false;

		if (passes > 0) {
			util_uv_cache_seam_map(res_x, res_y);
			gpu_texture_t *dst = make_material_node_preview_target(id, res_x, res_y, GPU_TEXTURE_FORMAT_RGBA64);
			gpu_texture_t *tmp = make_material_node_preview_target(string_tmp("%s_tmp", id), res_x, res_y, GPU_TEXTURE_FORMAT_RGBA64);
			util_uv_blur_seamless(dst, tmp, image, passes);
		}
	}
	else if (string_equals(node->type, "DIRECT_WARP")) {
		char          *id    = make_material_node_preview_id(node, parents);
		gpu_texture_t *image = any_map_get(g_context->node_previews, id);
		any_array_push(g_context->node_previews_used, id);
		i32 res_x = math_floor(config_get_texture_res_x());
		i32 res_y = math_floor(config_get_texture_res_y());
		if (image == NULL || image->width != res_x || image->height != res_y) {
			if (image != NULL) {
				gpu_delete_texture(image);
			}
			image = gpu_create_render_target(res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);
			any_map_set(g_context->node_previews, string_copy(id), image);
		}

		parser_material_warp_passthrough = true;
		util_render_make_node_preview(g_context->material->canvas, node, image, group, parents);
		parser_material_warp_passthrough = false;
	}
	else if (string_equals(node->type, "BAKE_CURVATURE")) {
		make_curvature_bake(node, group, parents, make_material_node_preview_id(node, parents));
	}
}

void make_material_traverse_nodes(ui_node_t_array_t *nodes, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *node = nodes->buffer[i];
		make_material_bake_node_preview(node, group, parents);
		if (string_equals(node->type, "GROUP")) {
			for (i32 j = 0; j < g_project->_->material_groups->length; ++j) {
				node_group_t *g     = g_project->_->material_groups->buffer[j];
				char         *cname = g->canvas->name;
				if (string_equals(cname, node->name)) {
					any_array_push(parents, node);
					make_material_traverse_nodes(g->canvas->nodes, g->canvas, parents);
					array_pop(parents);
					break;
				}
			}
		}
	}
}

// Deletes previews of material m, except the ones listed in keep
void make_material_delete_node_previews(slot_material_t *m, string_array_t *keep) {
	if (g_context->node_previews == NULL) {
		return;
	}
	char           *prefix = string_tmp("m%d_", m->id);
	string_array_t *keys   = map_keys(g_context->node_previews);
	for (i32 i = 0; i < keys->length; ++i) {
		char *key = keys->buffer[i];
		if (starts_with(key, prefix) && (keep == NULL || string_array_index_of(keep, key) == -1)) {
			gpu_texture_t *image = any_map_get(g_context->node_previews, key);
			gpu_delete_texture(image);
			map_delete(g_context->node_previews, key);
			free(key);
		}
	}
	array_free(keys);
	free(keys);
}

void make_material_bake_node_previews() {
	g_context->node_previews_used = any_array_create_from_raw((void *[]){}, 0);
	if (g_context->node_previews == NULL) {
		g_context->node_previews = any_map_create();
	}
	ui_node_t_array_t *empty = any_array_create_from_raw((void *[]){}, 0);
	make_material_traverse_nodes(g_context->material->canvas->nodes, NULL, empty);

	make_material_delete_node_previews(g_context->material, g_context->node_previews_used);
	array_free(empty);
	free(empty);
	array_free(g_context->node_previews_used);
	free(g_context->node_previews_used);
	g_context->node_previews_used = NULL;
}

// Refresh previews of a material that is not selected but fills linked layers,
// blur and warp sample other layers so they go stale on every stroke
void make_material_bake_node_previews_linked() {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();
	make_material_bake_linked = true;
	make_material_bake_node_previews();
	make_material_bake_linked = false;
	if (in_use)
		draw_begin(current, false, 0);
}

static void make_material_set_paint_scon(shader_context_t *scon) {
	shader_context_t *old = make_material_paint_scon;
	if (old != NULL && old != scon && old != make_material_default_scon && old != make_material_saved_scon) {
		make_material_delete_context(old);
	}
	make_material_paint_scon = scon;
}

void make_material_parse_paint_material(bool bake_previews) {
	if (!make_material_get_mout()) {
		return;
	}

	if (bake_previews) {
		gpu_texture_t *current = _draw_current;
		bool           in_use  = gpu_in_use;
		if (in_use)
			draw_end();
		make_material_bake_node_previews();
		if (in_use)
			draw_begin(current, false, 0);
	}

	material_t            *sdata = ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	node_shader_context_t *con   = make_paint_run(sdata);
	shader_context_load(con->data);
	make_material_set_paint_scon(con->data);
	node_shader_context_free(con);
	free(sdata);

	if (make_material_default_scon == NULL) {
		make_material_default_scon = make_material_paint_scon;
	}
}

void make_material_save_paint_material() {
	make_material_saved_scon = make_material_paint_scon;
}

void make_material_restore_paint_material() {
	if (make_material_saved_scon == NULL) {
		return;
	}
	shader_context_t *saved  = make_material_saved_scon;
	make_material_saved_scon = NULL;
	make_material_set_paint_scon(saved);
}

shader_context_t *make_material_parse_node_preview_material(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	if (node->outputs->length == 0) {
		return NULL;
	}

	material_t            *sdata         = ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	node_shader_context_t *con           = make_node_preview_run(sdata, node, group, parents);
	bool                   compile_error = false;
	shader_context_t      *scon;
	shader_context_load(con->data);

	if (con->data == NULL) {
		compile_error = true;
	}
	scon = con->data;
	node_shader_context_free(con);
	free(sdata);
	if (compile_error) {
		return NULL;
	}

	return scon;
}

void make_material_parse_brush() {
	parser_logic_parse(g_context->brush->canvas);
}

char *make_material_blend_mode(node_shader_t *kong, i32 blending, char *cola, char *colb, char *opac) {
	if (blending == BLEND_TYPE_MIX) {
		return string_tmp("lerp(%s, %s, %s)", cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_DARKEN) {
		return string_tmp("lerp(%s, min(%s, %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_MULTIPLY) {
		return string_tmp("lerp(%s, %s * %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_BURN) {
		return string_tmp("lerp(%s, float3(1.0, 1.0, 1.0) - (float3(1.0, 1.0, 1.0) - %s) / %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_LIGHTEN) {
		return string_tmp("max(%s, %s * %s)", cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SCREEN) {
		return string_tmp("(float3(1.0, 1.0, 1.0) - (float3(1.0 - %s, 1.0 - %s, 1.0 - %s) + %s * (float3(1.0, 1.0, 1.0) - %s)) * (float3(1.0, 1.0, 1.0) - %s))",
		                  opac, opac, opac, opac, colb, cola);
	}
	else if (blending == BLEND_TYPE_DODGE) {
		return string_tmp("lerp(%s, %s / (float3(1.0, 1.0, 1.0) - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_ADD) {
		return string_tmp("lerp(%s, %s + %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_OVERLAY) {
		// return "lerp(" + cola + ", float3( \
		// 	" + cola + ".r < 0.5 ? 2.0 * " + cola + ".r * " + colb + ".r : 1.0 - 2.0 * (1.0 - " + cola + ".r) * (1.0 - " + colb + ".r), \
		// 	" + cola + ".g < 0.5 ? 2.0 * " + cola + ".g * " + colb + ".g : 1.0 - 2.0 * (1.0 - " + cola + ".g) * (1.0 - " + colb + ".g), \
		// 	" + cola + ".b < 0.5 ? 2.0 * " + cola + ".b * " + colb + ".b : 1.0 - 2.0 * (1.0 - " + cola + ".b) * (1.0 - " + colb + ".b) \
		// ), " + opac + ")";
		char *cola_rgb = string_tmp("%s_rgb", string_replace_all(cola, ".", "_"));
		char *colb_rgb = string_tmp("%s_rgb", string_replace_all(colb, ".", "_"));
		char *res_r    = string_tmp("%s_res_r", string_replace_all(cola, ".", "_"));
		char *res_g    = string_tmp("%s_res_g", string_replace_all(cola, ".", "_"));
		char *res_b    = string_tmp("%s_res_b", string_replace_all(cola, ".", "_"));
		node_shader_write_frag(kong, string_tmp("float %s;", res_r));
		node_shader_write_frag(kong, string_tmp("float %s;", res_g));
		node_shader_write_frag(kong, string_tmp("float %s;", res_b));
		node_shader_write_frag(kong, string_tmp("float3 %s = %s;", cola_rgb, cola)); // cola_rgb = cola.rgb
		node_shader_write_frag(kong, string_tmp("float3 %s = %s;", colb_rgb, colb));
		node_shader_write_frag(kong, string_tmp("if (%s.r < 0.5) { %s = 2.0 * %s.r * %s.r; } else { %s = 1.0 - 2.0 * (1.0 - %s.r) * (1.0 - %s.r); }", cola_rgb,
		                                        res_r, cola_rgb, colb_rgb, res_r, cola_rgb, colb_rgb));
		node_shader_write_frag(kong, string_tmp("if (%s.g < 0.5) { %s = 2.0 * %s.g * %s.g; } else { %s = 1.0 - 2.0 * (1.0 - %s.g) * (1.0 - %s.g); }", cola_rgb,
		                                        res_g, cola_rgb, colb_rgb, res_g, cola_rgb, colb_rgb));
		node_shader_write_frag(kong, string_tmp("if (%s.b < 0.5) { %s = 2.0 * %s.b * %s.b; } else { %s = 1.0 - 2.0 * (1.0 - %s.b) * (1.0 - %s.b); }", cola_rgb,
		                                        res_b, cola_rgb, colb_rgb, res_b, cola_rgb, colb_rgb));
		return string_tmp("lerp(%s, float3(%s, %s, %s), %s)", cola, res_r, res_g, res_b, opac);
	}
	else if (blending == BLEND_TYPE_SOFT_LIGHT) {
		return string_tmp("((1.0 - %s) * %s + %s * ((float3(1.0, 1.0, 1.0) - %s) * %s * %s + %s * (float3(1.0, 1.0, 1.0) - (float3(1.0, 1.0, 1.0) - %s) * "
		                  "(float3(1.0, 1.0, 1.0) - %s))))",
		                  opac, cola, opac, cola, colb, cola, cola, colb, cola);
	}
	else if (blending == BLEND_TYPE_LINEAR_LIGHT) {
		return string_tmp("(%s + %s * (float3(2.0, 2.0, 2.0) * (%s - float3(0.5, 0.5, 0.5))))", cola, opac, colb);
	}
	else if (blending == BLEND_TYPE_DIFFERENCE) {
		return string_tmp("lerp(%s, abs(%s - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SUBTRACT) {
		return string_tmp("lerp(%s, %s - %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_DIVIDE) {
		return string_tmp("float3(1.0 - %s, 1.0 - %s, 1.0 - %s) * %s + float3(%s, %s, %s) * %s / %s", opac, opac, opac, cola, opac, opac, opac, cola, colb);
	}
	else if (blending == BLEND_TYPE_HUE) {
		node_shader_add_function(kong, str_hue_sat);
		return string_tmp("lerp(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, colb, cola, cola, opac);
	}
	else if (blending == BLEND_TYPE_SATURATION) {
		node_shader_add_function(kong, str_hue_sat);
		return string_tmp("lerp(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, cola, colb, cola, opac);
	}
	else if (blending == BLEND_TYPE_COLOR) {
		node_shader_add_function(kong, str_hue_sat);
		return string_tmp("lerp(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, colb, colb, cola, opac);
	}
	else { // BlendValue
		node_shader_add_function(kong, str_hue_sat);
		return string_tmp("lerp(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, cola, cola, colb, opac);
	}
}

void make_material_parse_depth_material() {
	if (make_material_depth_scon != NULL) {
		make_material_delete_context(make_material_depth_scon);
	}
	material_t            *sdata = ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	node_shader_context_t *con   = make_depth_run(sdata);
	shader_context_load(con->data);
	make_material_depth_scon = con->data;
	node_shader_context_free(con);
	free(sdata);
}
