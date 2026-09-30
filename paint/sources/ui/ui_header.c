
#include "../global.h"

ui_color_state_t ui_header_color_state;
i32              ui_header_cursor_mode = 0;

void ui_header_init() {
	ui_header_handle->layout = UI_LAYOUT_HORIZONTAL;
}

void ui_header_render_ui() {
	if (g_config->touch_ui) {
		ui_header_h = ui_header_default_h + 4;
	}
	else {
		ui_header_h = ui_header_default_h;
	}
	ui_header_h = math_floor(ui_header_h * UI_SCALE());

	if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 0) {
		return;
	}

	if (!base_view3d_show) {
		return;
	}

	i32 nodesw = (ui_nodes_show || ui_view2d_show) ? g_config->layout->buffer[LAYOUT_SIZE_NODES_W] : 0;
	i32 ww     = iron_window_width() - ui_toolbar_w(true) - g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] - nodesw;

	if (g_ui->is_typing) {
		ui_header_handle->redraws = 2;
	}

	if (ui_window(ui_header_handle, base_x(), ui_header_h, ww, ui_header_h, false)) {
		g_ui->_y += 2;
		ui_header_draw_tool_properties();
	}
}

void ui_header_particle_menu_draw() {
	ui_slider(&g_context->particle_lifetime, tr("Lifetime"), 0.0, 10.0, true, 1.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_context->particle_spawn_distance, tr("Distance"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_context->particle_mass, tr("Mass"), 0.0, 3.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	ui_slider(&g_context->particle_random, tr("Random"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);

	ui_slider(&g_context->particle_friction, tr("Friction"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_friction(g_context->particle_friction);
	}

	ui_slider(&g_context->particle_bounciness, tr("Bounce"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_bounciness(g_context->particle_bounciness);
	}

	ui_slider(&g_context->particle_gravity_x, tr("Gravity X"), -10.0, 10.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_gravity(g_context->particle_gravity_x, g_context->particle_gravity_y, g_context->particle_gravity_z);
	}

	ui_slider(&g_context->particle_gravity_y, tr("Gravity Y"), -10.0, 10.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_gravity(g_context->particle_gravity_x, g_context->particle_gravity_y, g_context->particle_gravity_z);
	}

	ui_slider(&g_context->particle_gravity_z, tr("Gravity Z"), -10.0, 10.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		physics_set_gravity(g_context->particle_gravity_x, g_context->particle_gravity_y, g_context->particle_gravity_z);
	}

	if (g_ui->changed || g_ui->is_typing) {
		ui_menu_keep_open = true;
	}
}

void ui_header_draw_tool_properties_layer_preview_dirty(void *_) {
	g_context->layer_preview_dirty = true;
}

void ui_header_draw_tool_properties_color_picker_normal() {
	ui_fill(0, 0, g_ui->_w / (float)UI_SCALE(), g_theme->ELEMENT_H * 9, g_theme->SEPARATOR_COL);
	g_ui->changed = false;
	ui_color_wheel((u32 *)&g_context->picked_color->normal, &ui_header_color_state, false, -1, 10 * g_theme->ELEMENT_H * UI_SCALE(), false, NULL, NULL);
	if (g_ui->changed) {
		ui_header_handle->redraws = 2;
		ui_menu_keep_open         = true;
	}
}

void ui_header_draw_tool_properties_color_picker_base() {
	ui_fill(0, 0, g_ui->_w / (float)UI_SCALE(), g_theme->ELEMENT_H * 9, g_theme->SEPARATOR_COL);
	g_ui->changed = false;
	ui_color_wheel((u32 *)&g_context->picked_color->base, &ui_header_color_state, false, -1, 10 * g_theme->ELEMENT_H * UI_SCALE(), false, NULL, NULL);
	if (g_ui->changed) {
		g_context->picked_color->base = color_set_ab(g_context->picked_color->base, 255);
		ui_header_handle->redraws     = 2;
		ui_menu_keep_open             = true;
	}
}

void ui_header_draw_tool_properties_to_mask(slot_layer_t *m) {
	_gpu_begin(m->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_colorid_to_mask);
	render_target_t *rt = any_map_get(render_path_render_targets, "texpaint_colorid");
	gpu_set_texture(pipes_texpaint_colorid, rt->_image);
	gpu_set_texture(pipes_tex_colorid, project_get_image(g_project->_->assets->buffer[g_context->colorid]));
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
	g_context->colorid_picked      = false;
	ui_toolbar_handle->redraws     = 1;
	ui_header_handle->redraws      = 1;
	g_context->layer_preview_dirty = true;
	layers_update_fill_layers();
}

void ui_header_draw_tool_properties_import(char *path) {
	import_asset_run(path, -1.0, -1.0, true, false, NULL);
	g_context->colorid = g_project->_->assets->length - 1;
	for (i32 i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *a = g_project->_->assets->buffer[i];
		// Already imported
		if (string_equals(a->file, path)) {
			g_context->colorid = array_index_of(g_project->_->assets, a);
		}
	}
	g_context->ddirty                 = 2;
	g_context->colorid_picked         = false;
	ui_toolbar_handle->redraws        = 1;
	ui_base_hwnds->buffer[2]->redraws = 2;
}

void ui_header_draw_tool_properties() {
	if (g_context->tool == TOOL_TYPE_COLORID) {
		ui_text(tr("Picked Color"), UI_ALIGN_LEFT, 0x00000000);
		if (g_context->colorid_picked) {
			render_target_t *rt = any_map_get(render_path_render_targets, "texpaint_colorid");
			ui_image(rt->_image, 0xffffffff, 64);
		}
		g_ui->enabled = g_context->colorid_picked;
		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
			g_context->colorid_picked        = false;
			g_context->colorid_viewport_mask = false;
			ui_toolbar_handle->redraws       = 1;
		}
		g_ui->enabled = true;
		ui_text(tr("Color ID Map"), UI_ALIGN_LEFT, 0x00000000);
		if (g_project->_->assets->length > 0) {
			ui_combo(&g_context->colorid, base_combo_enum_texts("TEX_IMAGE"), tr("Color ID"), false, UI_ALIGN_LEFT, true);
			bool colorid_changed = ui_item_changed();
			if (ui_widget_id(&g_context->colorid, UI_ID_COMBO) == g_ui->combo_selected_id) {
				g_ui->combo_selected_images = base_combo_enum_textures("TEX_IMAGE");
			}
			if (colorid_changed) {
				g_context->ddirty          = 2;
				g_context->colorid_picked  = false;
				ui_toolbar_handle->redraws = 1;
			}
			ui_image(project_get_image(g_project->_->assets->buffer[g_context->colorid]), 0xffffffff, -1.0);
			if (g_ui->is_hovered) {
				ui_tooltip_image(project_get_image(g_project->_->assets->buffer[g_context->colorid]), 256);
			}
		}
		if (ui_icon_button(tr("Import"), ICON_FOLDER_OPEN, UI_ALIGN_CENTER)) {
			ui_files_show(string_array_join(path_texture_formats(), ","), false, true, &ui_header_draw_tool_properties_import);
		}
		g_ui->enabled = g_context->colorid_picked;
		if (ui_icon_button(tr("To Mask"), ICON_MASK, UI_ALIGN_CENTER)) {
			if (slot_layer_is_mask(g_context->layer)) {
				context_set_layer(g_context->layer->parent);
			}
			slot_layer_t *m = layers_new_mask(false, g_context->layer, -1);
			sys_notify_on_next_frame(&ui_header_draw_tool_properties_to_mask, m);
			history_new_white_mask();
		}
		g_ui->enabled = true;

		ui_check(&g_context->colorid_viewport_mask, tr("Viewport Mask"), "");
		if (ui_item_changed()) {
			make_material_parse_mesh_material();
		}
	}
	else if (g_context->tool == TOOL_TYPE_PICKER || g_context->tool == TOOL_TYPE_MATERIAL) {

		ui_state_t state = ui_text("", 0, color_set_ab(g_context->picked_color->base, 255));
		if (state == UI_STATE_STARTED) {
			base_drag_off_x  = -(mouse_x - g_ui->_x - g_ui->_window_x - 3);
			base_drag_off_y  = -(mouse_y - g_ui->_y - g_ui->_window_y + 1);
			base_drag_swatch = project_clone_swatch(g_context->picked_color);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Drag and drop picked color to swatches, materials, layers or to the node editor"));
		}
		if (g_ui->is_hovered && g_ui->input_released && g_ui->combo_selected_id == 0) {
			ui_menu_draw(&ui_header_draw_tool_properties_color_picker_base, -1, -1);
		}
		if (ui_icon_button(tr("Add Swatch"), ICON_PLUS, UI_ALIGN_CENTER)) {
			swatch_color_t *new_swatch = project_clone_swatch(g_context->picked_color);
			new_swatch->base           = color_set_ab(new_swatch->base, 255);
			g_context->swatch          = new_swatch;
			any_array_push(g_project->swatches, new_swatch);
			history_new_swatch();
			ui_base_hwnds->buffer[2]->redraws = 1;
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Add picked color to swatches"));
		}

		if (g_config->workflow == WORKFLOW_PBR) {

			i32 _w = g_ui->_w;
			g_ui->_w /= 2;

			ui_text("", 0, g_context->picked_color->normal);
			if (g_ui->is_hovered && g_ui->input_released && g_ui->combo_selected_id == 0) {
				ui_menu_draw(&ui_header_draw_tool_properties_color_picker_normal, -1, -1);
			}
			ui_text(tr("Normal"), UI_ALIGN_LEFT, 0x00000000);
			g_ui->_w = _w;

			ui_slider(&g_context->picked_color->occlusion, tr("Occlusion"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			ui_slider(&g_context->picked_color->roughness, tr("Roughness"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			ui_slider(&g_context->picked_color->metallic, tr("Metallic"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			ui_slider(&g_context->picked_color->height, tr("Height"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		}

		ui_slider(&g_context->picked_color->opacity, tr("Opacity"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);

		ui_check(&g_context->picker_select_material, tr("Select Material"), "");

		ui_check(&g_context->picker_paint_mask, tr("Paint Mask"), "");
		if (ui_item_changed()) {
			make_material_parse_paint_material(false);
		}

		ui_check(&g_context->picker_viewport_mask, tr("Viewport Mask"), "");
		if (ui_item_changed()) {
			make_material_parse_mesh_material();
		}

		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
			g_context->picker_viewport_mask = false;
			g_context->picker_paint_mask    = false;
			make_material_parse_mesh_material();
			make_material_parse_paint_material(false);
		}
	}
	else if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_FILL ||
	         g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT || g_context->tool == TOOL_TYPE_CLONE ||
	         g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE) {
		bool decal_mask = context_is_decal_mask();
		if (g_context->tool != TOOL_TYPE_FILL) {
			if (decal_mask) {
				ui_slider(&g_context->brush_decal_mask_radius, tr("Radius"), 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
				if (g_ui->is_hovered) {
					any_map_t *vars = any_map_create();
					any_map_set(vars, "brush_radius", any_map_get(g_keymap, "brush_radius"));
					any_map_set(vars, "brush_radius_decrease", any_map_get(g_keymap, "brush_radius_decrease"));
					any_map_set(vars, "brush_radius_increase", any_map_get(g_keymap, "brush_radius_increase"));
					ui_tooltip(
					    vtr("Hold {brush_radius} and move mouse to the left or press {brush_radius_decrease} to decrease the radius\nHold {brush_radius} "
					        "and move mouse to the right or press {brush_radius_increase} to increase the radius",
					        vars));
					map_free(vars);
				}
			}
			else {
				ui_slider(&g_context->brush_radius, tr("Radius"), 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
				if (g_ui->is_hovered) {
					any_map_t *vars = any_map_create();
					any_map_set(vars, "brush_radius", any_map_get(g_keymap, "brush_radius"));
					any_map_set(vars, "brush_radius_decrease", any_map_get(g_keymap, "brush_radius_decrease"));
					any_map_set(vars, "brush_radius_increase", any_map_get(g_keymap, "brush_radius_increase"));
					ui_tooltip(
					    vtr("Hold {brush_radius} and move mouse to the left or press {brush_radius_decrease} to decrease the radius\nHold {brush_radius} "
					        "and move mouse to the right or press {brush_radius_increase} to increase the radius",
					        vars));
					map_free(vars);
				}
			}
		}

		if (g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT) {
			ui_slider(&g_context->brush_scale_x, tr("Scale X"), 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		}

		if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_FILL || g_context->tool == TOOL_TYPE_DECAL ||
		    g_context->tool == TOOL_TYPE_TEXT) {
			ui_slider(&g_context->brush_scale, tr("UV Scale"), 0.01, 5.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
			if (ui_item_changed()) {
				if (g_context->tool == TOOL_TYPE_DECAL || g_context->tool == TOOL_TYPE_TEXT) {
					gpu_texture_t *current = _draw_current;
					draw_end();
					util_render_make_decal_preview();
					draw_begin(current, false, 0);
				}
			}

			ui_slider(&g_context->brush_angle, tr("Angle"), 0.0, 360.0, true, 1, true, UI_ALIGN_RIGHT, true);
			bool angle_changed = ui_item_changed();
			if (g_ui->is_hovered) {
				any_map_t *vars = any_map_create();
				any_map_set(vars, "brush_angle", any_map_get(g_keymap, "brush_angle"));
				ui_tooltip(vtr(
				    "Hold {brush_angle} and move mouse to the left to decrease the angle\nHold {brush_angle} and move mouse to the right to increase the angle",
				    vars));
				map_free(vars);
			}

			if (angle_changed) {
				make_material_parse_paint_material(true);
			}
		}

		ui_slider(&g_context->brush_opacity, tr("Opacity"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		if (g_ui->is_hovered) {
			any_map_t *vars = any_map_create();
			any_map_set(vars, "brush_opacity", any_map_get(g_keymap, "brush_opacity"));
			ui_tooltip(vtr("Hold {brush_opacity} and move mouse to the left to decrease the opacity\nHold {brush_opacity} and move mouse to the right to "
			               "increase the opacity",
			               vars));
			map_free(vars);
		}

		if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE || decal_mask ||
		    g_context->tool == TOOL_TYPE_PARTICLE) {
			ui_slider(&g_context->brush_hardness, tr("Hardness"), 0.0, 1.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		}

		if (g_context->tool != TOOL_TYPE_ERASER && g_config->workflow != WORKFLOW_SCULPT) {
			string_array_t *brush_blending_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("Mix"),
			        tr("Darken"),
			        tr("Multiply"),
			        tr("Burn"),
			        tr("Lighten"),
			        tr("Screen"),
			        tr("Dodge"),
			        tr("Add"),
			        tr("Overlay"),
			        tr("Soft Light"),
			        tr("Linear Light"),
			        tr("Difference"),
			        tr("Subtract"),
			        tr("Divide"),
			        tr("Hue"),
			        tr("Saturation"),
			        tr("Color"),
			        tr("Value"),
			    },
			    18);
			ui_combo((int *)&g_context->brush_blending, brush_blending_combo, tr("Blending"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
		}

		if ((g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_FILL) && g_config->workflow != WORKFLOW_SCULPT) {
			string_array_t *texcoord_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("UV Map"),
			        tr("Triplanar"),
			        tr("Project"),
			    },
			    3);
			ui_combo((int *)&g_context->brush_paint, texcoord_combo, tr("TexCoord"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
		}

		if (g_context->tool == TOOL_TYPE_BRUSH && g_config->workflow == WORKFLOW_SCULPT) {
			string_array_t *mode_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("Draw"),
			        tr("Grab"),
			    },
			    2);
			ui_combo((int *)&g_context->brush_sculpt, mode_combo, tr("Mode"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
		}

		if (g_context->tool == TOOL_TYPE_TEXT) {
			if (g_context->text_tool_text == NULL) {
				g_context->text_tool_text = "";
			}
			ui_id_t text_id = ui_widget_id(&g_context->text_tool_text, UI_ID_TEXT);
			i32     w       = g_ui->_w;
			if (g_ui->text_selected_id == text_id || g_ui->submit_text_id == text_id) {
				g_ui->_w *= 3;
			}
			ui_text_input(&g_context->text_tool_text, "", UI_ALIGN_LEFT, true, true);
			g_ui->_w = w;
			if (ui_item_changed()) {
				gpu_texture_t *current = _draw_current;
				draw_end();
				util_render_make_text_preview();
				util_render_make_decal_preview();
				draw_begin(current, false, 0);
			}
		}

		if (g_context->tool == TOOL_TYPE_PARTICLE) {
			if (ui_button(tr("Particle"), UI_ALIGN_CENTER, "")) {
				ui_menu_draw(&ui_header_particle_menu_draw, -1, -1);
			}
		}

		if (g_context->tool == TOOL_TYPE_CLONE) {
			if (ui_button(g_context->clone_set_source ? tr("Setting Source") : tr("Set Source"), UI_ALIGN_CENTER, "")) {
				g_context->clone_set_source = !g_context->clone_set_source;
			}
		}

		if (g_context->tool == TOOL_TYPE_BLUR) {
			string_array_t *blur_type_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("Blur"),
			        tr("Smudge"),
			    },
			    2);
			ui_combo(&g_context->blur_type, blur_type_combo, tr("Blur Type"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}
		}

		if (g_context->tool == TOOL_TYPE_FILL) {
			string_array_t *fill_mode_combo = any_array_create_from_raw_tmp(
			    (void *[]){
			        tr("Object"),
			        tr("Face"),
			        tr("Angle"),
			        tr("UV Island"),
			    },
			    4);
			ui_combo(&g_context->fill_type, fill_mode_combo, tr("Fill Mode"), false, UI_ALIGN_LEFT, true);
			if (ui_item_changed()) {
				if (g_context->fill_type == FILL_TYPE_FACE) {
					gpu_texture_t *current = _draw_current;
					draw_end();
					// cache_uv_map();
					util_uv_cache_triangle_map();
					draw_begin(current, false, 0);
					// draw_wireframe = true;
				}
				make_material_parse_paint_material(true);
				make_material_parse_mesh_material();
			}
		}
		else {
			i32  _w           = g_ui->_w;
			f32  sc           = UI_SCALE();
			bool touch_header = (g_config->touch_ui && g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1);
			if (touch_header) {
				g_ui->_x -= 4 * sc;
			}
			g_ui->_w = math_floor((touch_header ? 54 : 60) * sc);

			ui_check(&g_context->xray, tr("X-Ray"), "");
			if (ui_item_changed()) {
				make_material_parse_paint_material(true);
			}

			bool sym_changed = false;

			if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1) {
				if (g_config->touch_ui) {
					g_ui->_w = math_floor(19 * sc);
					ui_check(&g_context->sym_x, "", "");
					sym_changed |= ui_item_changed();
					g_ui->_x -= 4 * sc;
					ui_check(&g_context->sym_y, "", "");
					sym_changed |= ui_item_changed();
					g_ui->_x -= 4 * sc;
					ui_check(&g_context->sym_z, "", "");
					sym_changed |= ui_item_changed();
					g_ui->_x -= 4 * sc;
					g_ui->_w = math_floor(40 * sc);
					char *x  = tr("X");
					char *y  = tr("Y");
					char *z  = tr("Z");
					ui_text(string_tmp("%s%s%s", x, y, z), UI_ALIGN_LEFT, 0x00000000);
				}
				else {
					g_ui->_w = math_floor(56 * sc);
					ui_text(tr("Symmetry"), UI_ALIGN_LEFT, 0x00000000);
					g_ui->_w = math_floor(25 * sc);
					ui_check(&g_context->sym_x, tr("X"), "");
					sym_changed |= ui_item_changed();
					ui_check(&g_context->sym_y, tr("Y"), "");
					sym_changed |= ui_item_changed();
					ui_check(&g_context->sym_z, tr("Z"), "");
					sym_changed |= ui_item_changed();
				}
				g_ui->_w = _w;
			}
			else {
				// Popup
				g_ui->_w = _w;
				ui_check(&g_context->sym_x, string_tmp("%s %s", tr("Symmetry"), tr("X")), "");
				sym_changed |= ui_item_changed();
				ui_check(&g_context->sym_y, string_tmp("%s %s", tr("Symmetry"), tr("Y")), "");
				sym_changed |= ui_item_changed();
				ui_check(&g_context->sym_z, string_tmp("%s %s", tr("Symmetry"), tr("Z")), "");
				sym_changed |= ui_item_changed();
			}

			if (sym_changed) {
				make_material_parse_paint_material(true);
			}
		}
	}
	else if (g_context->tool == TOOL_TYPE_SELECT) {
		g_ui->enabled = g_context->select_active;
		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
			g_context->select_active = false;
			make_material_parse_paint_material(false);
		}
		g_ui->enabled = true;
	}

	if (g_context->tool == TOOL_TYPE_CURSOR) {
		string_array_t *cursor_mode_combo = any_array_create_from_raw_tmp(
		    (void *[]){
		        tr("Object"),
		    },
		    1);
		ui_combo(&ui_header_cursor_mode, cursor_mode_combo, tr("Mode"), false, UI_ALIGN_LEFT, true);

		mesh_object_t *o = g_context->paint_object != NULL ? g_context->paint_object : context_main_object();
		if (o != NULL && o->base != NULL && o->base->transform != NULL) {
			i32 _w = g_ui->_w;
			f32 sc = UI_SCALE();

			g_ui->_w = math_floor(34 * sc);
			ui_text("Loc", UI_ALIGN_LEFT, 0x00000000);
			g_ui->_w = math_floor(48 * sc);
			tab_meshes_draw_transform_loc(o, "header");

			g_ui->_w = math_floor(34 * sc);
			ui_text("Rot", UI_ALIGN_LEFT, 0x00000000);
			g_ui->_w = math_floor(48 * sc);
			tab_meshes_draw_transform_rot(o, "header");

			g_ui->_w = math_floor(40 * sc);
			ui_text("Scale", UI_ALIGN_LEFT, 0x00000000);
			g_ui->_w = math_floor(48 * sc);
			tab_meshes_draw_transform_scale(o, "header");

			g_ui->_w = _w;
		}
	}
}
