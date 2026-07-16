
#include "global.h"

void context_init() {
	g_context = GC_ALLOC_INIT(context_t, {0});
	gc_root(g_context);

	g_context->merged_object_is_atlas       = false; // Only objects referenced by atlas are merged
	g_context->ddirty                       = 0;     // depth
	g_context->pdirty                       = 0;     // paint
	g_context->rdirty                       = 0;     // render
	g_context->brush_blend_dirty            = true;
	g_context->split_view                   = false;
	g_context->view_index                   = -1;
	g_context->view_index_last              = -1;
	g_context->picked_color                 = project_make_swatch(0xffffffff);
	g_context->envmap_loaded                = false;
	g_context->show_envmap                  = false;
	g_context->show_envmap_blur             = false;
	g_context->show_envmap_spheres          = false;
	g_context->capturing_screenshot         = false;
	g_context->capture_background           = false;
	g_context->envmap_angle                 = 0.0;
	g_context->light_angle                  = 0.0;
	g_context->cull_backfaces               = true;
	g_context->format_type                  = TEXTURE_LDR_FORMAT_PNG;
	g_context->format_quality               = 100.0;
	g_context->layers_destination           = EXPORT_DESTINATION_DISK;
	g_context->split_by                     = SPLIT_TYPE_OBJECT;
	g_context->select_time                  = 0.0;
	g_context->viewport_mode                = g_config->viewport_mode;
	g_context->hscale_was_changed           = false;
	g_context->export_mesh_format           = MESH_FORMAT_OBJ;
	g_context->export_mesh_index            = 0;
	g_context->pack_assets_on_export        = true;
	g_context->pack_assets_on_save          = false;
	g_context->paint_vec                    = (vec4_t){0.0, 0.0, 0.0, 1.0};
	g_context->last_paint_x                 = -1.0;
	g_context->last_paint_y                 = -1.0;
	g_context->foreground_event             = false;
	g_context->painted                      = 0;
	g_context->brush_time                   = 0.0;
	g_context->clone_start_x                = -1.0;
	g_context->clone_start_y                = -1.0;
	g_context->clone_delta_x                = 0.0;
	g_context->clone_delta_y                = 0.0;
	g_context->show_compass                 = true;
	g_context->last_paint_vec_x             = -1.0;
	g_context->last_paint_vec_y             = -1.0;
	g_context->prev_paint_vec_x             = -1.0;
	g_context->prev_paint_vec_y             = -1.0;
	g_context->frame                        = 0;
	g_context->paint2d_view                 = false;
	g_context->brush_locked                 = false;
	g_context->camera_type                  = CAMERA_TYPE_PERSPECTIVE;
	g_context->texture_export_path          = "";
	g_context->last_status_position         = 0;
	g_context->camera_pivot                 = CAMERA_PIVOT_CURSOR;
	g_context->camera_controls              = CAMERA_CONTROLS_ORBIT;
	g_context->pen_painting_only            = false; // Reject painting with finger when using pen
	g_context->layer_preview_dirty          = true;
	g_context->layers_preview_dirty         = false;
	g_context->node_preview_name            = "";
	g_context->node_preview_socket_map      = any_map_create();
	g_context->node_preview_map             = any_map_create();
	g_context->selected_node_preview        = true;
	g_context->material_preview             = false; // Drawing material previews
	g_context->saved_camera                 = mat4_identity();
	g_context->materialid_picked            = 0;
	g_context->uvx_picked                   = 0.0;
	g_context->uvy_picked                   = 0.0;
	g_context->picker_select_material       = false;
	g_context->picker_paint_mask            = false;
	g_context->picker_viewport_mask         = false;
	g_context->pick_pos_nor_tex             = false;
	g_context->pick_object_id               = false;
	g_context->posx_picked                  = 0.0;
	g_context->posy_picked                  = 0.0;
	g_context->posz_picked                  = 0.0;
	g_context->norx_picked                  = 0.0;
	g_context->nory_picked                  = 0.0;
	g_context->norz_picked                  = 0.0;
	g_context->draw_wireframe               = false;
	g_context->draw_texels                  = false;
	g_context->colorid                      = 0;
	g_context->colorid_picked               = false;
	g_context->layers_export                = EXPORT_MODE_VISIBLE;
	g_context->decal_preview                = false;
	g_context->decal_x                      = 0.0;
	g_context->decal_y                      = 0.0;
	g_context->write_icon_on_export         = false;
	g_context->particle_hit_x               = 0.0;
	g_context->particle_hit_y               = 0.0;
	g_context->particle_hit_z               = 0.0;
	g_context->last_particle_hit_x          = 0.0;
	g_context->last_particle_hit_y          = 0.0;
	g_context->last_particle_hit_z          = 0.0;
	g_context->particle_friction            = 0.1;
	g_context->particle_bounciness          = 0.0;
	g_context->particle_gravity_x           = 0.0;
	g_context->particle_gravity_y           = 0.0;
	g_context->particle_gravity_z           = -9.81;
	g_context->particle_lifetime            = 5.0;
	g_context->particle_mass                = 1.0;
	g_context->particle_random              = 0.1;
	g_context->particle_spawn_distance      = 0.3;
	g_context->layer_filter                 = 0;
	g_context->gizmo_started                = false;
	g_context->gizmo_offset                 = 0.0;
	g_context->gizmo_drag                   = 0.0;
	g_context->gizmo_drag_last              = 0.0;
	g_context->translate_x                  = false;
	g_context->translate_y                  = false;
	g_context->translate_z                  = false;
	g_context->scale_x                      = false;
	g_context->scale_y                      = false;
	g_context->scale_z                      = false;
	g_context->rotate_x                     = false;
	g_context->rotate_y                     = false;
	g_context->rotate_z                     = false;
	g_context->brush_nodes_radius           = 1.0;
	g_context->brush_nodes_opacity          = 1.0;
	g_context->brush_nodes_uses_random      = false;
	g_context->brush_mask_image_is_alpha    = false;
	g_context->brush_stencil_image_is_alpha = false;
	g_context->brush_stencil_x              = 0.02;
	g_context->brush_stencil_y              = 0.02;
	g_context->brush_stencil_scale          = 0.9;
	g_context->brush_stencil_scaling        = false;
	g_context->brush_stencil_angle          = 0.0;
	g_context->brush_stencil_rotating       = false;
	g_context->brush_nodes_scale            = 1.0;
	g_context->brush_nodes_angle            = 0.0;
	g_context->brush_nodes_hardness         = 1.0;
	g_context->brush_directional            = false;
	g_context->brush_scale_x                = 1.0;
	g_context->brush_decal_mask_radius      = 0.5;
	g_context->brush_blending               = BLEND_TYPE_MIX;
	g_context->brush_opacity                = 1.0;
	g_context->brush_scale                  = 1.0;
	g_context->brush_angle                  = 0.0;
	g_context->brush_lazy_radius            = 0.0;
	g_context->brush_lazy_step              = 0.0;
	g_context->brush_lazy_x                 = 0.0;
	g_context->brush_lazy_y                 = 0.0;
	g_context->brush_paint                  = UV_TYPE_UVMAP;
	g_context->brush_angle_reject_dot       = 0.5;
	g_context->bake_type                    = BAKE_TYPE_CURVATURE;
	g_context->bake_axis                    = BAKE_AXIS_XYZ;
	g_context->bake_up_axis                 = BAKE_UP_AXIS_Z;
	g_context->bake_samples                 = 128;
	g_context->bake_ao_strength             = 1.0;
	g_context->bake_ao_radius               = 1.0;
	g_context->bake_ao_offset               = 1.0;
	g_context->bake_curv_strength           = 1.0;
	g_context->bake_curv_radius             = 1.0;
	g_context->bake_curv_offset             = 0.0;
	g_context->bake_curv_smooth             = 1;
	g_context->bake_high_poly               = 0;
	g_context->xray                         = false;
	g_context->sym_x                        = false;
	g_context->sym_y                        = false;
	g_context->sym_z                        = false;
	g_context->fill_type                    = FILL_TYPE_OBJECT;
	g_context->blur_type                    = BLUR_TYPE_BLUR;
	g_context->paint2d                      = false;
	g_context->maximized_sidebar_width      = 0;
	g_context->drag_dest                    = 0;
	g_context->tool                         = TOOL_TYPE_BRUSH;
	g_context->color_picker_previous_tool   = TOOL_TYPE_BRUSH;
	g_context->brush_radius                 = 0.5;
	g_context->brush_hardness               = 1.0;
}

bool context_use_deferred() {
	return g_config->render_mode != RENDER_MODE_FORWARD &&
	       (g_context->viewport_mode == VIEWPORT_MODE_LIT || g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE) && g_context->tool != TOOL_TYPE_COLORID;
}

void context_select_material(i32 i) {
	if (g_project->_->materials->length <= i) {
		return;
	}
	context_set_material(g_project->_->materials->buffer[i]);
}

void context_set_material_on_next_frame(void *_) {
	util_render_make_decal_preview();
}

void context_set_material(slot_material_t *m) {
	if (array_index_of(g_project->_->materials, m) == -1) {
		return;
	}
	g_context->material = m;
	make_material_parse_paint_material(true);
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
	ui_header_handle->redraws                         = 2;
	ui_nodes_hwnd->redraws                            = 2;
	gc_unroot(ui_nodes_group_stack);
	ui_nodes_group_stack = any_array_create_from_raw((void *[]){}, 0);
	gc_root(ui_nodes_group_stack);

	bool decal = context_is_decal();
	if (decal) {
		sys_notify_on_next_frame(&context_set_material_on_next_frame, NULL);
	}
}

void context_select_brush(i32 i) {
	if (g_project->_->brushes->length <= i) {
		return;
	}
	context_set_brush(g_project->_->brushes->buffer[i]);
}

void context_set_brush(slot_brush_t *b) {
	if (array_index_of(g_project->_->brushes, b) == -1) {
		return;
	}
	g_context->brush = b;
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	make_material_parse_paint_material(false);
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
	ui_nodes_hwnd->redraws                            = 2;
}

void context_set_font(slot_font_t *f) {
	if (array_index_of(g_project->_->fonts, f) == -1) {
		return;
	}
	g_context->font = f;
	util_render_make_text_preview();
	util_render_make_decal_preview();
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
	ui_view2d_hwnd->redraws                         = 2;
}

void context_select_font(i32 i) {
	if (g_project->_->fonts->length <= i) {
		return;
	}
	context_set_font(g_project->_->fonts->buffer[i]);
}

void context_set_sound(slot_sound_t *s) {
	if (array_index_of(g_project->_->sounds, s) == -1) {
		return;
	}
	g_context->sound                                = s;
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
	ui_view2d_hwnd->redraws                         = 2;
}

void context_select_sound(i32 i) {
	if (g_project->_->sounds->length <= i) {
		return;
	}
	context_set_sound(g_project->_->sounds->buffer[i]);
}

void context_select_layer(i32 i) {
	if (g_project->_->layers->length <= i) {
		return;
	}
	context_set_layer(g_project->_->layers->buffer[i]);
}

void context_set_layer(slot_layer_t *l) {
	if (l == g_context->layer) {
		return;
	}
	g_context->layer          = l;
	ui_header_handle->redraws = 2;

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	layers_set_object_mask();
	make_material_parse_mesh_material();
	make_material_parse_paint_material(true);

	if (in_use)
		draw_begin(current, false, 0);

	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	ui_view2d_hwnd->redraws                           = 2;
}

void context_select_tool(i32 i) {
	g_context->tool = i;
	make_material_parse_paint_material(true);
	make_material_parse_mesh_material();
	g_context->ddirty              = 3;
	viewport_mode_t _viewport_mode = g_context->viewport_mode;
	g_context->viewport_mode       = VIEWPORT_MODE_NONE;
	context_set_viewport_mode(_viewport_mode);

	context_init_tool();
	ui_header_handle->redraws  = 2;
	ui_toolbar_handle->redraws = 2;
}

void context_init_tool() {
	if (context_is_decal()) {
		if (g_context->tool == TOOL_TYPE_TEXT) {
			util_render_make_text_preview();
		}
		util_render_make_decal_preview();
	}
	else if (g_context->tool == TOOL_TYPE_MATERIAL) {
		layers_update_fill_layers();
		context_main_object()->skip_context = NULL;
	}
}

void context_select_paint_object(mesh_object_t *o) {
	ui_header_handle->redraws = 2;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		p->skip_context  = "paint";
	}

	// #ifdef is_forge
	// g_context->paint_object->skip_context = "";
	// #endif

	g_context->paint_object = o;

	i32 mask = slot_layer_get_object_mask(g_context->layer);
	if (context_layer_filter_used()) {
		mask = g_context->layer_filter;
	}

	if (g_context->merged_object == NULL || mask > 0) {
		g_context->paint_object->skip_context = "";
	}
	util_uv_uvmap_cached       = false;
	util_uv_trianglemap_cached = false;
	util_uv_dilatemap_cached   = false;
}

mesh_object_t *context_main_object() {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *po = g_project->_->paint_objects->buffer[i];
		if (po->base->children->length > 0) {
			return po;
		}
	}
	return g_project->_->paint_objects->buffer[0];
}

bool context_layer_filter_used() {
	return g_context->layer_filter > 0 && g_context->layer_filter <= g_project->_->paint_objects->length;
}

bool context_object_mask_used() {
	return slot_layer_get_object_mask(g_context->layer) > 0 && slot_layer_get_object_mask(g_context->layer) <= g_project->_->paint_objects->length;
}

bool context_in_3d_view() {
	return g_context->paint_vec.x < 1 && g_context->paint_vec.x > 0 && g_context->paint_vec.y < 1 && g_context->paint_vec.y > 0;
}

bool context_in_paint_area() {
	return context_in_3d_view() || context_in_2d_view(VIEW_2D_TYPE_LAYER);
}

bool context_in_layers() {
	char *tab = ui_hovered_tab_name();
	return string_equals(tab, tr("Layers"));
}

bool context_in_materials() {
	char *tab = ui_hovered_tab_name();
	return string_equals(tab, tr("Materials"));
}

bool context_in_meshes() {
	char *tab = ui_hovered_tab_name();
	return string_equals(tab, tr("Meshes"));
}

bool context_in_swatches() {
	char *tab = ui_hovered_tab_name();
	return string_equals(tab, tr("Swatches"));
}

bool context_in_brushes() {
	char *tab = ui_hovered_tab_name();
	return string_equals(tab, tr("Brushes"));
}

bool context_in_fonts() {
	char *tab = ui_hovered_tab_name();
	return string_equals(tab, tr("Fonts"));
}

bool context_in_textures() {
	char *tab = ui_hovered_tab_name();
	return string_equals(tab, tr("Textures"));
}

bool context_in_browser() {
	char *tab = ui_hovered_tab_name();
	return string_equals(tab, tr("Browser"));
}

bool context_in_2d_view(view_2d_type_t type) {
	return ui_view2d_show && ui_view2d_type == type && mouse_x > ui_view2d_wx && mouse_x < ui_view2d_wx + ui_view2d_ww && mouse_y > ui_view2d_wy &&
	       mouse_y < ui_view2d_wy + ui_view2d_wh;
}

bool context_in_nodes() {
	return ui_nodes_show && mouse_x > ui_nodes_wx && mouse_x < ui_nodes_wx + ui_nodes_ww && mouse_y > ui_nodes_wy && mouse_y < ui_nodes_wy + ui_nodes_wh;
}

bool context_is_decal() {
	return g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT;
}

bool context_is_decal_mask() {
	return context_is_decal() && operator_shortcut(any_map_get(g_keymap, "decal_mask"), SHORTCUT_TYPE_DOWN);
}

bool context_is_brush_camera_align() {
	bool brush = g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER;
	return (context_is_decal() || brush) &&
	       (g_context->brush_camera_align || operator_shortcut(any_map_get(g_keymap, "brush_camera_align"), SHORTCUT_TYPE_DOWN));
}

bool context_is_decal_mask_paint() {
	return context_is_decal() &&
	       operator_shortcut(string("%s+%s", any_map_get(g_keymap, "decal_mask"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN);
}

bool context_is_decal_mask_paint_pass() {
	return context_is_decal_mask() && (mouse_down("left") || g_context->brush_time > 0);
}

bool context_is_floating_toolbar() {
	// Header is off -> floating toolbar
	return g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 0 || (!base_view3d_show && ui_view2d_show);
}

void context_set_viewport_mode(viewport_mode_t mode) {
	if (mode == g_context->viewport_mode) {
		return;
	}

	viewport_mode_t prev_viewport_mode = g_context->viewport_mode;
	g_context->viewport_mode           = mode;

	if (context_use_deferred()) {
		gc_unroot(render_path_commands);
		render_path_commands = render_path_deferred_commands;
		gc_root(render_path_commands);
	}
	else {
		gc_unroot(render_path_commands);
		render_path_commands = render_path_forward_commands;
		gc_root(render_path_commands);
	}
	make_material_parse_mesh_material();

	// Rotate mode is not supported for path tracing yet
	if (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE && g_context->camera_controls == CAMERA_CONTROLS_ROTATE) {
		g_context->camera_controls = CAMERA_CONTROLS_ORBIT;
		viewport_reset();
	}

	// Bake in lit mode for now
	if (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE && g_context->tool == TOOL_TYPE_BAKE) {
		g_context->viewport_mode = VIEWPORT_MODE_LIT;
	}

	bool has_sculpt = false;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (g_project->_->layers->buffer[i]->texpaint_sculpt != NULL && slot_layer_is_visible(g_project->_->layers->buffer[i])) {
			has_sculpt = true;
			break;
		}
	}
	if (has_sculpt) {
		if (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE) {
			sculpt_bake_to_mesh();
		}
		else if (prev_viewport_mode == VIEWPORT_MODE_PATH_TRACE) {
			// Restore un-sculpted mesh
			util_mesh_merge(NULL);
		}
	}
}

void context_set_camera_controls(int i) {
	g_context->camera_controls = i;
}

void context_load_envmap() {
	if (!g_context->envmap_loaded) {
		// TODO: Unable to share texture for both radiance and envmap - reload image
		g_context->envmap_loaded = true;
		map_delete(data_cached_textures, "World_radiance.k");
	}
	world_data_load_envmap(scene_world);
	if (g_context->saved_envmap == NULL) {
		g_context->saved_envmap = scene_world->_->envmap;
	}
}

void context_update_envmap() {
	if (g_context->show_envmap) {
		scene_world->_->envmap = g_context->show_envmap_blur ? scene_world->_->radiance_mipmaps->buffer[0] : g_context->saved_envmap;
	}
	else {
		scene_world->_->envmap = g_context->empty_envmap;
	}
}

void context_set_viewport_shader(void *viewport_shader) {
	g_context->viewport_shader = viewport_shader;
	context_set_render_path();
}

void context_set_render_path_on_next_frame(void *_) {
	make_material_parse_mesh_material();
}

void context_set_render_path() {
	if (g_config->render_mode == RENDER_MODE_FORWARD || g_context->viewport_shader != NULL) {
		gc_unroot(render_path_commands);
		render_path_commands = render_path_forward_commands;
		gc_root(render_path_commands);
	}
	else {
		gc_unroot(render_path_commands);
		render_path_commands = render_path_deferred_commands;
		gc_root(render_path_commands);
	}
	sys_notify_on_next_frame(&context_set_render_path_on_next_frame, NULL);
}

bool context_enable_import_plugin(char *file) {
	// Return plugin name suitable for importing the specified file
	if (box_preferences_files_plugin == NULL) {
		box_preferences_fetch_plugins();
	}
	char *ext = substring(file, string_last_index_of(file, ".") + 1, string_length(file));
	for (i32 i = 0; i < box_preferences_files_plugin->length; ++i) {
		char *f = box_preferences_files_plugin->buffer[i];
		if (starts_with(f, "import_") && string_index_of(f, ext) >= 0) {
			config_enable_plugin(f);
			console_info(string("%s %s", f, tr("plugin enabled")));
			return true;
		}
	}
	return false;
}
