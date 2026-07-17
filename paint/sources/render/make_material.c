
#include "../global.h"

shader_context_t          *make_material_default_scon = NULL;
material_context_t        *make_material_default_mcon = NULL;
static shader_context_t   *make_material_saved_scon   = NULL;
static material_context_t *make_material_saved_mcon   = NULL;

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
}

void make_material_delete_context(shader_context_t *c) {
	// Ensure pipeline is no longer in use
	sys_notify_on_next_frame(&make_material_delete_context_on_next_frame, c);
}

void make_material_parse_mesh_material() {
	material_data_t *m = g_project->_->materials->buffer[0]->data;

	for (i32 i = 0; i < m->_->shader->contexts->length; ++i) {
		shader_context_t *c = m->_->shader->contexts->buffer[i];
		if (string_equals(c->name, "mesh")) {
			array_remove(m->_->shader->contexts, c);
			make_material_delete_context(c);
			break;
		}
	}

	if (make_mesh_layer_pass_count > 1) {
		i32 i = 0;
		while (i < m->_->shader->contexts->length) {
			shader_context_t *c = m->_->shader->contexts->buffer[i];
			for (i32 j = 1; j < make_mesh_layer_pass_count; ++j) {
				char *name = string("mesh%s", i32_to_string(j));
				if (string_equals(c->name, name)) {
					array_remove(m->_->shader->contexts, c);
					make_material_delete_context(c);
					i--;
					break;
				}
			}
			i++;
		}

		i = 0;
		while (i < m->contexts->length) {
			material_context_t *c = m->contexts->buffer[i];
			for (i32 j = 1; j < make_mesh_layer_pass_count; ++j) {
				char *name = string("mesh%s", i32_to_string(j));
				if (string_equals(c->name, name)) {
					array_remove(m->contexts, c);
					i--;
					break;
				}
			}
			i++;
		}
	}

	material_t *mm = GC_ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});

	node_shader_context_t *con = make_mesh_run(mm, 0);
	shader_context_load(con->data);
	any_array_push(m->_->shader->contexts, con->data);

	for (i32 i = 1; i < make_mesh_layer_pass_count; ++i) {
		material_t            *mm  = GC_ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
		node_shader_context_t *con = make_mesh_run(mm, i);
		shader_context_load(con->data);
		any_array_push(m->_->shader->contexts, con->data);
		material_context_t *mcon =
		    GC_ALLOC_INIT(material_context_t, {.name = string("mesh%s", i32_to_string(i)), .bind_textures = any_array_create_from_raw((void *[]){}, 0)});
		material_context_load(mcon);
		any_array_push(m->contexts, mcon);
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

	material_data_t  *m    = g_project->_->materials->buffer[0]->data;
	shader_context_t *scon = NULL;
	for (i32 i = 0; i < m->_->shader->contexts->length; ++i) {
		shader_context_t *c = m->_->shader->contexts->buffer[i];
		if (string_equals(c->name, "mesh")) {
			scon = c;
			break;
		}
	}

	array_remove(m->_->shader->contexts, scon);

	material_context_t    *mcon = GC_ALLOC_INIT(material_context_t, {.name = "mesh", .bind_textures = any_array_create_from_raw((void *[]){}, 0)});
	material_t            *sd   = GC_ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
	node_shader_context_t *con  = make_mesh_preview_run(sd, mcon, false);

	for (i32 i = 0; i < m->contexts->length; ++i) {
		if (string_equals(m->contexts->buffer[i]->name, "mesh")) {
			material_context_load(mcon);
			m->contexts->buffer[i] = mcon;
			break;
		}
	}

	if (scon != NULL) {
		make_material_delete_context(scon);
	}

	bool compile_error = false;
	shader_context_load(con->data);
	if (con->data == NULL) {
		compile_error = true;
	}
	scon = con->data;
	if (compile_error) {
		return;
	}

	any_array_push(m->_->shader->contexts, scon);
}

void make_material_bake_node_preview(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	if (string_equals(node->type, "BLUR")) {
		char          *id    = parser_material_node_name(node, parents);
		gpu_texture_t *image = any_map_get(g_context->node_previews, id);
		any_array_push(g_context->node_previews_used, id);
		i32 res_x = math_floor(config_get_texture_res_x() / 4.0);
		i32 res_y = math_floor(config_get_texture_res_y() / 4.0);
		if (image == NULL || image->width != res_x || image->height != res_y) {
			if (image != NULL) {
				gpu_delete_texture(image);
			}
			image = gpu_create_render_target(res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);
			any_map_set(g_context->node_previews, id, image);
		}

		parser_material_blur_passthrough = true;
		util_render_make_node_preview(g_context->material->canvas, node, image, group, parents);
		parser_material_blur_passthrough = false;
	}
	else if (string_equals(node->type, "DIRECT_WARP")) {
		char          *id    = parser_material_node_name(node, parents);
		gpu_texture_t *image = any_map_get(g_context->node_previews, id);
		any_array_push(g_context->node_previews_used, id);
		i32 res_x = math_floor(config_get_texture_res_x());
		i32 res_y = math_floor(config_get_texture_res_y());
		if (image == NULL || image->width != res_x || image->height != res_y) {
			if (image != NULL) {
				gpu_delete_texture(image);
			}
			image = gpu_create_render_target(res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);
			any_map_set(g_context->node_previews, id, image);
		}

		parser_material_warp_passthrough = true;
		util_render_make_node_preview(g_context->material->canvas, node, image, group, parents);
		parser_material_warp_passthrough = false;
	}
	else if (string_equals(node->type, "BAKE_CURVATURE")) {
		char          *id    = parser_material_node_name(node, parents);
		gpu_texture_t *image = any_map_get(g_context->node_previews, id);
		any_array_push(g_context->node_previews_used, id);
		i32 res_x = math_floor(config_get_texture_res_x());
		i32 res_y = math_floor(config_get_texture_res_y());
		if (image == NULL || image->width != res_x || image->height != res_y) {
			if (image != NULL) {
				gpu_delete_texture(image);
			}
			image = gpu_create_render_target(res_x, res_y, GPU_TEXTURE_FORMAT_R8);
			any_map_set(g_context->node_previews, id, image);
		}

		if (render_path_paint_live_layer == NULL) {
			gc_unroot(render_path_paint_live_layer);
			render_path_paint_live_layer = slot_layer_create("_live", LAYER_SLOT_TYPE_LAYER, NULL);
			gc_root(render_path_paint_live_layer);
		}

		tool_type_t _tool      = g_context->tool;
		bake_type_t _bake_type = g_context->bake_type;
		g_context->tool        = TOOL_TYPE_BAKE;
		g_context->bake_type   = BAKE_TYPE_CURVATURE;

		parser_material_bake_passthrough = true;
		gc_unroot(parser_material_start_node);
		parser_material_start_node = node;
		gc_root(parser_material_start_node);
		gc_unroot(parser_material_start_group);
		parser_material_start_group = group;
		gc_root(parser_material_start_group);
		gc_unroot(parser_material_start_parents);
		parser_material_start_parents = parents;
		gc_root(parser_material_start_parents);
		make_material_parse_paint_material(false);
		parser_material_bake_passthrough = false;
		gc_unroot(parser_material_start_node);
		parser_material_start_node = NULL;
		gc_unroot(parser_material_start_group);
		parser_material_start_group = NULL;
		gc_unroot(parser_material_start_parents);
		parser_material_start_parents = NULL;
		g_context->pdirty             = 1;
		render_path_paint_use_live_layer(true);
		render_path_paint_commands_paint(false);
		render_path_paint_dilate(true, false);
		render_path_paint_use_live_layer(false);
		g_context->pdirty = 0;

		g_context->tool      = _tool;
		g_context->bake_type = _bake_type;

		any_map_t       *rts           = render_path_render_targets;
		render_target_t *texpaint_live = any_map_get(rts, "texpaint_live");
		draw_begin(image, false, 0);
		draw_image(texpaint_live->_image, 0, 0);
		draw_end();
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

void make_material_bake_node_previews() {
	g_context->node_previews_used = any_array_create_from_raw((void *[]){}, 0);
	if (g_context->node_previews == NULL) {
		g_context->node_previews = any_map_create();
	}
	ui_node_t_array_t *empty = any_array_create_from_raw((void *[]){}, 0);
	make_material_traverse_nodes(g_context->material->canvas->nodes, NULL, empty);

	string_array_t *keys = map_keys(g_context->node_previews);
	for (i32 i = 0; i < keys->length; ++i) {
		char *key = keys->buffer[i];
		if (string_array_index_of(g_context->node_previews_used, key) == -1) {
			gpu_texture_t *image = any_map_get(g_context->node_previews, key);
			gpu_delete_texture(image);
			map_delete(g_context->node_previews, key);
		}
	}
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

	material_data_t *m = g_project->_->materials->buffer[0]->data;
	for (i32 i = 0; i < m->_->shader->contexts->length; ++i) {
		shader_context_t *c = m->_->shader->contexts->buffer[i];
		if (string_equals(c->name, "paint")) {
			array_remove(m->_->shader->contexts, c);
			if (c != make_material_default_scon && c != make_material_saved_scon) {
				make_material_delete_context(c);
			}
			break;
		}
	}
	for (i32 i = 0; i < m->contexts->length; ++i) {
		material_context_t *c = m->contexts->buffer[i];
		if (string_equals(c->name, "paint")) {
			array_remove(m->contexts, c);
			break;
		}
	}

	material_t            *sdata = GC_ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	material_context_t    *tmcon = GC_ALLOC_INIT(material_context_t, {.name = "paint", .bind_textures = any_array_create_from_raw((void *[]){}, 0)});
	node_shader_context_t *con   = make_paint_run(sdata, tmcon);

	bool              compile_error = false;
	shader_context_t *scon;
	shader_context_load(con->data);
	if (con->data == NULL) {
		compile_error = true;
	}
	scon = con->data;
	if (compile_error) {
		return;
	}
	material_context_load(tmcon);
	material_context_t *mcon = tmcon;

	any_array_push(m->_->shader->contexts, scon);
	any_array_push(m->contexts, mcon);

	if (make_material_default_scon == NULL) {
		gc_unroot(make_material_default_scon);
		make_material_default_scon = scon;
		gc_root(make_material_default_scon);
	}
	if (make_material_default_mcon == NULL) {
		gc_unroot(make_material_default_mcon);
		make_material_default_mcon = mcon;
		gc_root(make_material_default_mcon);
	}
}

void make_material_save_paint_material() {
	material_data_t *m = g_project->_->materials->buffer[0]->data;
	for (i32 i = 0; i < m->_->shader->contexts->length; ++i) {
		shader_context_t *c = m->_->shader->contexts->buffer[i];
		if (string_equals(c->name, "paint")) {
			gc_unroot(make_material_saved_scon);
			make_material_saved_scon = c;
			gc_root(make_material_saved_scon);
			break;
		}
	}
	for (i32 i = 0; i < m->contexts->length; ++i) {
		material_context_t *c = m->contexts->buffer[i];
		if (string_equals(c->name, "paint")) {
			gc_unroot(make_material_saved_mcon);
			make_material_saved_mcon = c;
			gc_root(make_material_saved_mcon);
			break;
		}
	}
}

void make_material_restore_paint_material() {
	if (make_material_saved_scon == NULL) {
		return;
	}
	material_data_t *m = g_project->_->materials->buffer[0]->data;
	for (i32 i = 0; i < m->_->shader->contexts->length; ++i) {
		shader_context_t *c = m->_->shader->contexts->buffer[i];
		if (string_equals(c->name, "paint")) {
			array_remove(m->_->shader->contexts, c);
			if (c != make_material_default_scon && c != make_material_saved_scon) {
				make_material_delete_context(c);
			}
			break;
		}
	}
	for (i32 i = 0; i < m->contexts->length; ++i) {
		material_context_t *c = m->contexts->buffer[i];
		if (string_equals(c->name, "paint")) {
			array_remove(m->contexts, c);
			break;
		}
	}
	any_array_push(m->_->shader->contexts, make_material_saved_scon);
	any_array_push(m->contexts, make_material_saved_mcon);
	gc_unroot(make_material_saved_scon);
	gc_unroot(make_material_saved_mcon);
	make_material_saved_scon = NULL;
	make_material_saved_mcon = NULL;
}

parse_node_preview_result_t *make_material_parse_node_preview_material(ui_node_t *node, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	if (node->outputs->length == 0) {
		return NULL;
	}

	material_t            *sdata         = GC_ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	material_context_t    *mcon_raw      = GC_ALLOC_INIT(material_context_t, {.name = "mesh", .bind_textures = any_array_create_from_raw((void *[]){}, 0)});
	node_shader_context_t *con           = make_node_preview_run(sdata, mcon_raw, node, group, parents);
	bool                   compile_error = false;
	shader_context_t      *scon;
	shader_context_load(con->data);

	if (con->data == NULL) {
		compile_error = true;
	}
	scon = con->data;
	if (compile_error) {
		return NULL;
	}

	material_context_load(mcon_raw);
	material_context_t          *mcon   = mcon_raw;
	parse_node_preview_result_t *result = GC_ALLOC_INIT(parse_node_preview_result_t, {.scon = scon, .mcon = mcon});

	return result;
}

void make_material_parse_brush() {
	parser_logic_parse(g_context->brush->canvas);
}

char *make_material_blend_mode(node_shader_t *kong, i32 blending, char *cola, char *colb, char *opac) {
	if (blending == BLEND_TYPE_MIX) {
		return string("lerp3(%s, %s, %s)", cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_DARKEN) {
		return string("lerp3(%s, min3(%s, %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_MULTIPLY) {
		return string("lerp3(%s, %s * %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_BURN) {
		return string("lerp3(%s, float3(1.0, 1.0, 1.0) - (float3(1.0, 1.0, 1.0) - %s) / %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_LIGHTEN) {
		return string("max3(%s, %s * %s)", cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SCREEN) {
		return string("(float3(1.0, 1.0, 1.0) - (float3(1.0 - %s, 1.0 - %s, 1.0 - %s) + %s * (float3(1.0, 1.0, 1.0) - %s)) * (float3(1.0, 1.0, 1.0) - %s))",
		              opac, opac, opac, opac, colb, cola);
	}
	else if (blending == BLEND_TYPE_DODGE) {
		return string("lerp3(%s, %s / (float3(1.0, 1.0, 1.0) - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_ADD) {
		return string("lerp3(%s, %s + %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_OVERLAY) {
		// return "lerp3(" + cola + ", float3( \
		// 	" + cola + ".r < 0.5 ? 2.0 * " + cola + ".r * " + colb + ".r : 1.0 - 2.0 * (1.0 - " + cola + ".r) * (1.0 - " + colb + ".r), \
		// 	" + cola + ".g < 0.5 ? 2.0 * " + cola + ".g * " + colb + ".g : 1.0 - 2.0 * (1.0 - " + cola + ".g) * (1.0 - " + colb + ".g), \
		// 	" + cola + ".b < 0.5 ? 2.0 * " + cola + ".b * " + colb + ".b : 1.0 - 2.0 * (1.0 - " + cola + ".b) * (1.0 - " + colb + ".b) \
		// ), " + opac + ")";
		char *cola_rgb = string("%s_rgb", string_replace_all(cola, ".", "_"));
		char *colb_rgb = string("%s_rgb", string_replace_all(colb, ".", "_"));
		char *res_r    = string("%s_res_r", string_replace_all(cola, ".", "_"));
		char *res_g    = string("%s_res_g", string_replace_all(cola, ".", "_"));
		char *res_b    = string("%s_res_b", string_replace_all(cola, ".", "_"));
		node_shader_write_frag(kong, string("var %s: float;", res_r));
		node_shader_write_frag(kong, string("var %s: float;", res_g));
		node_shader_write_frag(kong, string("var %s: float;", res_b));
		node_shader_write_frag(kong, string("var %s: float3 = %s;", cola_rgb, cola)); // cola_rgb = cola.rgb
		node_shader_write_frag(kong, string("var %s: float3 = %s;", colb_rgb, colb));
		node_shader_write_frag(kong, string("if (%s.r < 0.5) { %s = 2.0 * %s.r * %s.r; } else { %s = 1.0 - 2.0 * (1.0 - %s.r) * (1.0 - %s.r); }", cola_rgb,
		                                    res_r, cola_rgb, colb_rgb, res_r, cola_rgb, colb_rgb));
		node_shader_write_frag(kong, string("if (%s.g < 0.5) { %s = 2.0 * %s.g * %s.g; } else { %s = 1.0 - 2.0 * (1.0 - %s.g) * (1.0 - %s.g); }", cola_rgb,
		                                    res_g, cola_rgb, colb_rgb, res_g, cola_rgb, colb_rgb));
		node_shader_write_frag(kong, string("if (%s.b < 0.5) { %s = 2.0 * %s.b * %s.b; } else { %s = 1.0 - 2.0 * (1.0 - %s.b) * (1.0 - %s.b); }", cola_rgb,
		                                    res_b, cola_rgb, colb_rgb, res_b, cola_rgb, colb_rgb));
		return string("lerp3(%s, float3(%s, %s, %s), %s)", cola, res_r, res_g, res_b, opac);
	}
	else if (blending == BLEND_TYPE_SOFT_LIGHT) {
		return string("((1.0 - %s) * %s + %s * ((float3(1.0, 1.0, 1.0) - %s) * %s * %s + %s * (float3(1.0, 1.0, 1.0) - (float3(1.0, 1.0, 1.0) - %s) * "
		              "(float3(1.0, 1.0, 1.0) - %s))))",
		              opac, cola, opac, cola, colb, cola, cola, colb, cola);
	}
	else if (blending == BLEND_TYPE_LINEAR_LIGHT) {
		return string("(%s + %s * (float3(2.0, 2.0, 2.0) * (%s - float3(0.5, 0.5, 0.5))))", cola, opac, colb);
	}
	else if (blending == BLEND_TYPE_DIFFERENCE) {
		return string("lerp3(%s, abs3(%s - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SUBTRACT) {
		return string("lerp3(%s, %s - %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_DIVIDE) {
		return string("float3(1.0 - %s, 1.0 - %s, 1.0 - %s) * %s + float3(%s, %s, %s) * %s / %s", opac, opac, opac, cola, opac, opac, opac, cola, colb);
	}
	else if (blending == BLEND_TYPE_HUE) {
		node_shader_add_function(kong, str_hue_sat);
		return string("lerp3(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, colb, cola, cola, opac);
	}
	else if (blending == BLEND_TYPE_SATURATION) {
		node_shader_add_function(kong, str_hue_sat);
		return string("lerp3(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, cola, colb, cola, opac);
	}
	else if (blending == BLEND_TYPE_COLOR) {
		node_shader_add_function(kong, str_hue_sat);
		return string("lerp3(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, colb, colb, cola, opac);
	}
	else { // BlendValue
		node_shader_add_function(kong, str_hue_sat);
		return string("lerp3(%s, hsv_to_rgb(float3(rgb_to_hsv(%s).r, rgb_to_hsv(%s).g, rgb_to_hsv(%s).b)), %s)", cola, cola, cola, colb, opac);
	}
}

void make_material_parse_depth_material() {
	material_data_t *m = g_project->_->materials->buffer[0]->data;

	for (i32 i = 0; i < m->_->shader->contexts->length; ++i) {
		shader_context_t *c = m->_->shader->contexts->buffer[i];
		if (string_equals(c->name, "depth")) {
			array_remove(m->_->shader->contexts, c);
			make_material_delete_context(c);
			break;
		}
	}
	for (i32 i = 0; i < m->contexts->length; ++i) {
		material_context_t *c = m->contexts->buffer[i];
		if (string_equals(c->name, "depth")) {
			array_remove(m->contexts, c);
			break;
		}
	}

	material_t            *sdata = GC_ALLOC_INIT(material_t, {.name = "Material", .canvas = g_context->material->canvas});
	material_context_t    *tmcon = GC_ALLOC_INIT(material_context_t, {.name = "depth", .bind_textures = any_array_create_from_raw((void *[]){}, 0)});
	node_shader_context_t *con   = make_depth_run(sdata, tmcon);

	bool              compile_error = false;
	shader_context_t *scon;
	shader_context_load(con->data);
	if (con->data == NULL) {
		compile_error = true;
	}
	scon = con->data;
	if (compile_error) {
		return;
	}
	material_context_load(tmcon);
	material_context_t *mcon = tmcon;

	any_array_push(m->_->shader->contexts, scon);
	any_array_push(m->contexts, mcon);
}
