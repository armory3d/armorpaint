
#include "../global.h"

static tool_type_t      _bake_texture_node_tool;
static render_target_t *_bake_texture_node_rt;
static int              _bake_texture_node_bits;

static void bake_texture_node_clear(gpu_texture_t *t) {
	draw_begin(t, true, 0xff000000);
	draw_end();
}

static void bake_texture_node_check_result(ui_node_t *node) {
	ui_nodes_hwnd->redraws = 2;

	if (g_context->pdirty > 0 || render_path_paint_baking) {
		return;
	}

	bool rgba128 = g_context->bake_type == BAKE_TYPE_NORMAL || g_context->bake_type == BAKE_TYPE_HEIGHT || g_context->bake_type == BAKE_TYPE_DERIVATIVE;
	if (rgba128) {
		char            *rt_name = string("bake_texture_node_%d", node->id);
		render_target_t *rt      = any_map_get(render_path_render_targets, rt_name);
		gpu_texture_t   *_image  = rt->_image;
		rt->format               = "RGBA32";
		rt->_image               = gpu_create_render_target(rt->width, rt->height, GPU_TEXTURE_FORMAT_RGBA32);
		draw_begin(rt->_image, false, 0);
		draw_scaled_image(_image, 0, 0, _image->width, _image->height);
		draw_end();
		gpu_texture_destroy(_image);
	}

	any_map_set(render_path_render_targets, string("texpaint%d", g_context->layer->id), _bake_texture_node_rt);
	g_context->layer->texpaint = bake_texture_node_texpaint;
	g_context->tool            = _bake_texture_node_tool;
	ui_view2d_hwnd->redraws    = 2;
	bake_texture_node_baking   = false;
	make_material_parse_paint_material(true);

	if (rgba128) {
		base_bits_handle->i = _bake_texture_node_bits;
		layers_set_bits();
	}

	layers_update_fill_layers();

	sys_remove_update(bake_texture_node_check_result);
}

static void bake_texture_node_run(ui_node_t *node, int bake_type, bool rt_bake) {
	_bake_texture_node_tool = g_context->tool;

	// Create dedicated RGBA32 render target
	char            *rt_name = string("bake_texture_node_%d", node->id);
	render_target_t *rt      = any_map_get(render_path_render_targets, rt_name);
	if (rt != NULL && rt->width != config_get_texture_res_x()) {
		gpu_texture_destroy(rt->_image);
		rt->width  = config_get_texture_res_x();
		rt->height = config_get_texture_res_y();
		rt->_image = gpu_create_render_target(rt->width, rt->height, GPU_TEXTURE_FORMAT_RGBA32);
	}
	if (rt == NULL) {
		rt         = render_target_create();
		rt->name   = rt_name;
		rt->width  = config_get_texture_res_x();
		rt->height = config_get_texture_res_y();
		rt->format = "RGBA32";
		render_path_create_render_target(rt);
	}

	// Bake in lit mode for now
	if (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE) {
		g_context->viewport_mode = VIEWPORT_MODE_LIT;
	}

	g_context->tool      = TOOL_TYPE_BAKE;
	g_context->bake_type = bake_type;

	if (g_context->bake_type == BAKE_TYPE_NORMAL || g_context->bake_type == BAKE_TYPE_HEIGHT || g_context->bake_type == BAKE_TYPE_DERIVATIVE) {
		// Use RGBA128 texture format for high poly to low poly baking to prevent artifacts
		gpu_texture_destroy(rt->_image);
		rt->format = "RGBA128";
		rt->_image = gpu_create_render_target(rt->width, rt->height, GPU_TEXTURE_FORMAT_RGBA128);

		_bake_texture_node_bits = base_bits_handle->i;
		base_bits_handle->i     = TEXTURE_BITS_BITS32;
		gpu_texture_t *current  = _draw_current;
		draw_end();
		layers_set_bits();
		draw_begin(current, false, 0);

		// Requires 2 steps
		history_push_undo = true;
	}

	// Set as current layer texpaint
	i32 lid               = g_context->layer->id;
	_bake_texture_node_rt = any_map_get(render_path_render_targets, string("texpaint%d", lid));
	any_map_set(render_path_render_targets, string("texpaint%d", lid), rt);
	bake_texture_node_texpaint = g_context->layer->texpaint;

	g_context->layer->texpaint               = rt->_image;
	g_context->pdirty                        = rt_bake ? g_context->bake_samples : 1;
	g_context->rdirty                        = 3;
	render_path_raytrace_bake_current_sample = 0;
	render_path_raytrace_frame               = 0;
	bake_texture_node_baking                 = true;
	make_material_parse_paint_material(false);
	sys_notify_on_next_frame(bake_texture_node_clear, rt->_image);
	sys_notify_on_update(bake_texture_node_check_result, node);
}

static void bake_texture_node_button(i32 node_id) {
	ui_node_t   *node      = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	char        *node_name = parser_material_node_name(node, NULL);
	ui_handle_t *h         = ui_handle(node_name);

	string_array_t *bakes = any_array_create_from_raw(
	    (void *[]){
	        tr("Curvature"),
	        tr("Normal"),
	        tr("Object Normal"),
	        tr("Height"),
	        tr("Derivative"),
	        tr("Position"),
	        tr("TexCoord"),
	        tr("Material ID"),
	        tr("Object ID"),
	        tr("Vertex Color"),
	    },
	    10);
	if (gpu_raytrace_supported()) {
		any_array_push(bakes, tr("Occlusion"));
		any_array_push(bakes, tr("Lightmap"));
		any_array_push(bakes, tr("Bent Normal"));
		any_array_push(bakes, tr("Thickness"));
	}

	i32 bake_type = ui_combo(ui_nest(h, 0), bakes, tr("Bake"), false, UI_ALIGN_LEFT, true);
	i32 height    = 1;

	bool rt_bake =
	    bake_type == BAKE_TYPE_OCCLUSION || bake_type == BAKE_TYPE_LIGHTMAP || bake_type == BAKE_TYPE_BENT_NORMAL || bake_type == BAKE_TYPE_THICKNESS;

	bool baking = g_context->tool == TOOL_TYPE_BAKE && g_context->pdirty > 0;
	if (baking) {
		f32 progress = render_path_raytrace_bake_current_sample / (float)g_context->bake_samples;
		if (progress > 1.0) {
			progress = 1.0;
		}

		i32 _BUTTON_COL     = g_theme->BUTTON_COL;
		g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;

		ui_handle_t *bake_h = ui_nest(h, 12);
		bake_h->f           = progress;
		char *label         = string("%d%%", (i32)(progress * 100));
		ui_slider(bake_h, label, 0.0, 1.0, true, 100, false, UI_ALIGN_CENTER, true);

		g_theme->BUTTON_COL = _BUTTON_COL;

		if (g_ui->is_hovered && g_ui->input_released) {
			g_context->pdirty = 0; // Stop baking
		}
	}
	else if (ui_icon_button(tr("Bake"), ICON_PLAY, UI_ALIGN_CENTER)) {
		bake_texture_node_run(node, bake_type, rt_bake);
	}
	height++;

	if (rt_bake) {
		ui_handle_t *samples_handle = ui_nest(h, 1);
		samples_handle->f           = g_context->bake_samples;
		g_context->bake_samples     = math_floor(ui_slider(samples_handle, tr("Samples"), 1, 512, true, 1, true, UI_ALIGN_RIGHT, true));
		height++;
	}

	if (bake_type == BAKE_TYPE_NORMAL_OBJECT || bake_type == BAKE_TYPE_POSITION || bake_type == BAKE_TYPE_BENT_NORMAL) {
		ui_handle_t *up_axis_handle   = ui_nest(h, 2);
		up_axis_handle->i             = g_context->bake_up_axis;
		string_array_t *up_axis_combo = any_array_create_from_raw(
		    (void *[]){
		        tr("Z"),
		        tr("Y"),
		    },
		    2);
		g_context->bake_up_axis = ui_combo(up_axis_handle, up_axis_combo, tr("Up Axis"), true, UI_ALIGN_LEFT, true);
		height++;
	}

	if (bake_type == BAKE_TYPE_OCCLUSION || bake_type == BAKE_TYPE_CURVATURE) {
		ui_handle_t *axis_handle   = ui_nest(h, 3);
		axis_handle->i             = g_context->bake_axis;
		string_array_t *axis_combo = any_array_create_from_raw(
		    (void *[]){
		        tr("XYZ"),
		        tr("X"),
		        tr("Y"),
		        tr("Z"),
		        tr("-X"),
		        tr("-Y"),
		        tr("-Z"),
		    },
		    7);
		g_context->bake_axis = ui_combo(axis_handle, axis_combo, tr("Axis"), true, UI_ALIGN_LEFT, true);
		height++;
	}

	if (bake_type == BAKE_TYPE_OCCLUSION) {
		ui_handle_t *strength_handle = ui_nest(h, 4);
		strength_handle->f           = g_context->bake_ao_strength;
		g_context->bake_ao_strength  = ui_slider(strength_handle, tr("Strength"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_handle_t *radius_handle   = ui_nest(h, 5);
		radius_handle->f             = g_context->bake_ao_radius;
		g_context->bake_ao_radius    = ui_slider(radius_handle, tr("Radius"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_handle_t *offset_handle   = ui_nest(h, 6);
		offset_handle->f             = g_context->bake_ao_offset;
		g_context->bake_ao_offset    = ui_slider(offset_handle, tr("Offset"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		height += 3;
	}

	if (bake_type == BAKE_TYPE_CURVATURE) {
		ui_handle_t *strength_handle  = ui_nest(h, 7);
		strength_handle->f            = g_context->bake_curv_strength;
		g_context->bake_curv_strength = ui_slider(strength_handle, tr("Strength"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_handle_t *radius_handle    = ui_nest(h, 8);
		radius_handle->f              = g_context->bake_curv_radius;
		g_context->bake_curv_radius   = ui_slider(radius_handle, tr("Radius"), 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_handle_t *offset_handle    = ui_nest(h, 9);
		offset_handle->f              = g_context->bake_curv_offset;
		g_context->bake_curv_offset   = ui_slider(offset_handle, tr("Offset"), -2.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_handle_t *smooth_handle    = ui_nest(h, 10);
		smooth_handle->f              = g_context->bake_curv_smooth;
		g_context->bake_curv_smooth   = math_floor(ui_slider(smooth_handle, tr("Smooth"), 0, 5, false, 1, true, UI_ALIGN_RIGHT, true));
		height += 4;
	}

	if (bake_type == BAKE_TYPE_NORMAL || bake_type == BAKE_TYPE_HEIGHT || bake_type == BAKE_TYPE_DERIVATIVE) {
		string_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p = g_project->_->paint_objects->buffer[i];
			any_array_push(ar, p->base->name);
		}
		ui_handle_t *poly_handle  = ui_nest(h, 11);
		poly_handle->i            = g_context->bake_high_poly;
		g_context->bake_high_poly = ui_combo(poly_handle, ar, tr("High Poly"), false, UI_ALIGN_LEFT, true);
		height++;
	}

	node->buttons->buffer[0]->height = height + 0.5;
}

static char *bake_texture_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char            *rt_name = string("bake_texture_node_%d", node->id);
	render_target_t *rt      = any_map_get(render_path_render_targets, rt_name);
	if (rt == NULL || bake_texture_node_baking) {
		return "float3(0.0, 0.0, 0.0)";
	}
	any_map_set(data_cached_textures, rt_name, rt->_image);
	bind_tex_t *tex      = parser_material_make_bind_tex(rt_name, rt_name);
	char       *texstore = parser_material_texture_store(node, tex, rt_name, COLOR_SPACE_AUTO);
	return string("%s.rgb", texstore);
}

void bake_texture_node_init() {
	ui_node_t *bake_texture_node_def =
	    GC_ALLOC_INIT(ui_node_t, {.id      = 0,
	                              .name    = _tr("Bake Texture"),
	                              .type    = "TEX_BAKE",
	                              .x       = 0,
	                              .y       = 0,
	                              .color   = 0xff4982a0,
	                              .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Color"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  1),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      GC_ALLOC_INIT(ui_node_button_t, {.name          = "bake_texture_node_button",
	                                                                       .type          = "CUSTOM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 1}),
	                                  },
	                                  1),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_texture, bake_texture_node_def);
	any_map_set(parser_material_node_vectors, "TEX_BAKE", bake_texture_node_vector);
	any_map_set(ui_nodes_custom_buttons, "bake_texture_node_button", bake_texture_node_button);
}
