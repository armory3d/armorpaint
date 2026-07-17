
#include "../global.h"

bool                 ui_view2d_text_input_hover = false;
bool                 ui_view2d_uvmap_show       = false;
paint_tex_t          ui_view2d_tex_type         = PAINT_TEX_BASE;
view_2d_layer_mode_t ui_view2d_layer_mode       = VIEW_2D_LAYER_MODE_SELECTED;
bool                 ui_view2d_controls_down    = false;
gpu_texture_t       *_ui_view2d_render_tex;
f32                  _ui_view2d_render_x;
f32                  _ui_view2d_render_y;
f32                  _ui_view2d_render_tw;
f32                  _ui_view2d_render_th;
gpu_texture_t       *ui_view2d_grid          = NULL;
bool                 ui_view2d_layer_touched = false;
gpu_texture_t       *ui_view2d_tex           = NULL;

void ui_view2d_init() {
	ui_view2d_pipe = gpu_create_pipeline();
	gc_root(ui_view2d_pipe);

	ui_view2d_pipe->vertex_shader   = sys_get_shader("layer_view.vert");
	ui_view2d_pipe->fragment_shader = sys_get_shader("layer_view.frag");
	gpu_vertex_structure_t *vs      = GC_ALLOC_INIT(gpu_vertex_structure_t, {0});
	gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_2X);
	gpu_vertex_structure_add(vs, "tex", GPU_VERTEX_DATA_F32_2X);
	gpu_vertex_structure_add(vs, "col", GPU_VERTEX_DATA_F32_4X);
	ui_view2d_pipe->input_layout              = vs;
	ui_view2d_pipe->blend_source              = GPU_BLEND_ONE;
	ui_view2d_pipe->blend_destination         = GPU_BLEND_ZERO;
	ui_view2d_pipe->color_write_mask_alpha[0] = false;
	gpu_pipeline_compile(ui_view2d_pipe);
	pipes_offset = 0;
	pipes_get_constant_location("float4"); // empty
	ui_view2d_channel_loc = pipes_get_constant_location("int");
}

void ui_view2d_capture_output(void *_) {
	util_texture_capture_output(ui_view2d_tex, "tex_capture", false);
}

ui_node_t *ui_view2d_get_selected_node() {
	ui_nodes_t *nodes = ui_nodes_get_nodes();
	if (nodes->nodes_selected_id->length == 0) {
		return NULL;
	}
	ui_node_canvas_t *c = ui_nodes_get_canvas(true);
	return ui_get_node(c->nodes, nodes->nodes_selected_id->buffer[0]);
}

void ui_view2d_draw_edit() {
	if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
		ui_handle_t *h_uvmap_show = ui_handle(__ID__);
		h_uvmap_show->b           = ui_view2d_uvmap_show;
		ui_check(h_uvmap_show, tr("UV Map"), "");
		if (h_uvmap_show->changed) {
			ui_view2d_uvmap_show    = h_uvmap_show->b;
			ui_view2d_hwnd->redraws = 2;
			ui_menu_keep_open       = true;
		}
	}

	ui_handle_t *h_tiled_show = ui_handle(__ID__);
	h_tiled_show->b           = ui_view2d_tiled_show;
	ui_check(h_tiled_show, tr("Tiled"), "");
	if (h_tiled_show->changed) {
		ui_view2d_tiled_show    = h_tiled_show->b;
		ui_view2d_hwnd->redraws = 2;
		ui_menu_keep_open       = true;
	}

	ui_menu_separator();

	ui_handle_t *h_view2d_grid_snap = ui_handle(__ID__);
	h_view2d_grid_snap->b           = g_config->view2d_grid_snap;
	g_config->view2d_grid_snap      = ui_check(h_view2d_grid_snap, tr("Grid Snap"), any_map_get(g_keymap, "grid_snap"));
	if (h_view2d_grid_snap->changed) {
		ui_menu_keep_open = true;
	}

	ui_handle_t *h_view2d_grid_show = ui_handle(__ID__);
	h_view2d_grid_show->b           = g_config->view2d_grid_show;
	g_config->view2d_grid_show      = ui_check(h_view2d_grid_show, tr("Show Grid"), "");
	if (h_view2d_grid_show->changed) {
		ui_view2d_hwnd->redraws = 2;
		ui_menu_keep_open       = true;
	}

	ui_handle_t *h_view2d_grid_cell = ui_handle(__ID__);
	h_view2d_grid_cell->f           = g_config->view2d_grid_cell;
	g_config->view2d_grid_cell      = ui_slider(h_view2d_grid_cell, tr("Grid Cell"), 1.0, 256.0, true, 1, true, UI_ALIGN_RIGHT, true);
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Cell size in pixels"));
	}
	if (h_view2d_grid_cell->changed) {
		ui_view2d_hwnd->redraws = 2;
		ui_menu_keep_open       = true;
	}

	ui_menu_separator();

	if (ui_menu_button(tr("Zoom to Fit"), "", ICON_NONE)) {
		ui_view2d_pan_x         = 0.0;
		ui_view2d_pan_y         = 0.0;
		ui_view2d_pan_scale     = 1.0;
		ui_view2d_hwnd->redraws = 2;
	}
	if (ui_menu_button(tr("Zoom to 100%"), "", ICON_NONE)) {
		if (ui_view2d_tex != NULL) {
			f32 wm              = fmin(ui_view2d_ww, ui_view2d_wh);
			ui_view2d_pan_scale = ui_view2d_tex->width / (wm * 0.9);
		}
		ui_view2d_hwnd->redraws = 2;
	}

	g_ui->enabled = ui_view2d_tex != NULL;
	if (ui_menu_button(tr("Capture Output"), "", ICON_PHOTO)) {
		sys_notify_on_next_frame(&ui_view2d_capture_output, NULL);
	}
	g_ui->enabled = true;

	if (ui_view2d_tex != NULL) {
		ui_menu_separator();
		g_ui->enabled = false;
		ui_text(string("%dx%d", ui_view2d_tex->width, ui_view2d_tex->height), UI_ALIGN_LEFT, 0x00000000);
		if (ui_view2d_type == VIEW_2D_TYPE_ASSET) {
			asset_t *asset     = g_context->texture;
			bool     is_packed = g_project->packed_assets != NULL && project_packed_asset_exists(g_project->packed_assets, asset->file);
			if (is_packed) {
				ui_text(tr("packed"), UI_ALIGN_LEFT, 0x00000000);
			}
		}
		g_ui->enabled = true;
	}

	char *view_type = ui_view2d_type == VIEW_2D_TYPE_ASSET   ? tr("Asset")
	                  : ui_view2d_type == VIEW_2D_TYPE_NODE  ? tr("Node")
	                  : ui_view2d_type == VIEW_2D_TYPE_FONT  ? tr("Font")
	                  : ui_view2d_type == VIEW_2D_TYPE_UVMAP ? tr("UVMap")
	                                                         : tr("Layer");

	if (ui_view2d_type == VIEW_2D_TYPE_NODE) {
		ui_node_t *sel = ui_view2d_get_selected_node();
		if (sel != NULL) {
			view_type = string("%s %s", sel->type, view_type);
		}
	}

	g_ui->enabled = false;
	ui_text(view_type, UI_ALIGN_LEFT, 0x00000000);
	g_ui->enabled = true;
}

void ui_view2d_draw_image(gpu_texture_t *image, f32 dx, f32 dy, f32 dw, f32 dh, i32 channel) {
	if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
		gpu_set_int(ui_view2d_channel_loc, channel);
	}
	draw_scaled_image(image, dx, dy, dw, dh);
}

void ui_view2d_render_color_pick(void *_) {
	render_target_t *rt              = any_map_get(render_path_render_targets, "texpaint_picker");
	gpu_texture_t   *texpaint_picker = rt->_image;
	draw_begin(texpaint_picker, false, 0);
	draw_scaled_image(_ui_view2d_render_tex, -_ui_view2d_render_x, -_ui_view2d_render_y, _ui_view2d_render_tw, _ui_view2d_render_th);
	draw_end();
	buffer_t *a = gpu_get_texture_pixels(texpaint_picker);
#ifdef IRON_BGRA
	i32 i0 = 2;
	i32 i1 = 1;
	i32 i2 = 0;
#else
	i32 i0 = 0;
	i32 i1 = 1;
	i32 i2 = 2;
#endif

	g_context->picked_color->base = color_set_rb(g_context->picked_color->base, buffer_get_u8(a, i0));
	g_context->picked_color->base = color_set_gb(g_context->picked_color->base, buffer_get_u8(a, i1));
	g_context->picked_color->base = color_set_bb(g_context->picked_color->base, buffer_get_u8(a, i2));
	ui_header_handle->redraws     = 2;

	if (g_context->color_picker_callback != NULL) {
		g_context->color_picker_callback(g_context->picked_color);
	}
}

void ui_view2d_update(void *_) {
	f32 headerh = UI_ELEMENT_H() * 1.4;

	g_context->paint2d = false;

	if (!base_ui_enabled || !ui_view2d_show || mouse_x < ui_view2d_wx || mouse_x > ui_view2d_wx + ui_view2d_ww || mouse_y < ui_view2d_wy + headerh ||
	    mouse_y > ui_view2d_wy + ui_view2d_wh) {
		if (ui_view2d_controls_down) {
			ui_canvas_control_t *control = ui_nodes_get_canvas_control(ui_view2d_controls_down, false);
			ui_view2d_controls_down      = control->controls_down;
		}
	}
	else {

		ui_canvas_control_t *control = ui_nodes_get_canvas_control(ui_view2d_controls_down, false);
		ui_view2d_pan_x += control->pan_x;
		ui_view2d_pan_y += control->pan_y;
		ui_view2d_controls_down = control->controls_down;
		if (control->zoom != 0.0) {
			f32 _pan_x = ui_view2d_pan_x / (float)ui_view2d_pan_scale;
			f32 _pan_y = ui_view2d_pan_y / (float)ui_view2d_pan_scale;
			ui_view2d_pan_scale += control->zoom;
			if (ui_view2d_pan_scale < 0.1) {
				ui_view2d_pan_scale = 0.1;
			}
			if (ui_view2d_pan_scale > 6.0) {
				ui_view2d_pan_scale = 6.0;
			}
			ui_view2d_pan_x = _pan_x * ui_view2d_pan_scale;
			ui_view2d_pan_y = _pan_y * ui_view2d_pan_scale;

			if (ui_touch_control) {
				// Zoom to finger location
				ui_view2d_pan_x -= (g_ui->input_x - g_ui->_window_x - g_ui->_window_w / 2.0) * control->zoom;
				ui_view2d_pan_y -= (g_ui->input_y - g_ui->_window_y - g_ui->_window_h / 2.0) * control->zoom;
			}
			ui_view2d_grid_redraw = true;
		}

		bool decal_mask = context_is_decal_mask_paint();
		bool set_clone_source =
		    g_context->tool == TOOL_TYPE_CLONE &&
		    operator_shortcut(string("%s+%s", any_map_get(g_keymap, "set_clone_source"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN);

		if (!g_ui->input_down) {
			ui_view2d_layer_touched = false;
		}

		if (ui_view2d_type == VIEW_2D_TYPE_LAYER && !ui_view2d_text_input_hover &&
		    (operator_shortcut(any_map_get(g_keymap, "action_paint"), SHORTCUT_TYPE_DOWN) ||
		     operator_shortcut(string("%s+%s", any_map_get(g_keymap, "brush_ruler"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN) ||
		     decal_mask || set_clone_source || g_config->brush_live)) {

			if (g_config->touch_ui) {
				// Paint only when clicking on the layer rect
				slot_layer_t  *layer   = g_context->layer;
				gpu_texture_t *tex     = layer->texpaint;
				f32            ratio   = tex->height / (float)tex->width;
				f32            wm      = fmin(ui_view2d_ww, ui_view2d_wh);
				f32            tw      = wm * 0.9 * ui_view2d_pan_scale;
				f32            th      = tw * ratio;
				f32            tx      = ui_view2d_ww / 2.0 - tw / 2.0 + ui_view2d_pan_x;
				i32            headerh = g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1 ? ui_header_h * 2 : ui_header_h;
				i32            apph    = iron_window_height() - g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] + headerh;
				f32            ty      = apph / 2.0 - th / 2.0 + ui_view2d_pan_y;
				f32            mx      = mouse_x - ui_view2d_wx;
				f32            my      = mouse_y - ui_view2d_wy;
				if (mx > tx && mx < tx + tw && my > ty && my < ty + th) {
					ui_view2d_layer_touched = true;
				}
				if (ui_view2d_layer_touched) {
					g_context->paint2d = true;
				}
			}
			else {
				g_context->paint2d = true;
			}
		}

		if (!g_ui->is_typing) {
			if (keyboard_started("left")) {
				ui_view2d_pan_x -= 5;
			}
			else if (keyboard_started("right")) {
				ui_view2d_pan_x += 5;
			}
			if (keyboard_started("up")) {
				ui_view2d_pan_y -= 5;
			}
			else if (keyboard_started("down")) {
				ui_view2d_pan_y += 5;
			}

			if (!g_context->paint2d && g_config->touch_ui && g_ui->input_down && ui_view2d_type != VIEW_2D_TYPE_UVMAP) {
				ui_view2d_pan_x += g_ui->input_dx;
				ui_view2d_pan_y += g_ui->input_dy;
			}

			// Limit panning to keep texture in viewport
			i32 border = 32;
			f32 wm     = fmin(ui_view2d_ww, ui_view2d_wh);
			f32 tw     = ui_view2d_ww * 0.9 * ui_view2d_pan_scale;
			f32 tx     = ui_view2d_ww / 2.0 - tw / 2.0 + ui_view2d_pan_x;
			f32 hh     = sys_h();
			f32 ty     = hh / 2.0 - tw / 2.0 + ui_view2d_pan_y;

			if (tx + border > ui_view2d_ww) {
				ui_view2d_pan_x = ui_view2d_ww / 2.0 + tw / 2.0 - border;
			}
			else if (tx - border < -tw) {
				ui_view2d_pan_x = -tw / 2.0 - ui_view2d_ww / 2.0 + border;
			}
			if (ty + border > hh) {
				ui_view2d_pan_y = hh / 2.0 + tw / 2.0 - border;
			}
			else if (ty - border < -tw) {
				ui_view2d_pan_y = -tw / 2.0 - hh / 2.0 + border;
			}

			if (operator_shortcut(any_map_get(g_keymap, "view_reset"), SHORTCUT_TYPE_STARTED)) {
				ui_view2d_pan_x     = 0.0;
				ui_view2d_pan_y     = 0.0;
				ui_view2d_pan_scale = 1.0;
			}
		}
	}

	// Render
	ui_view2d_ww = g_config->layout->buffer[LAYOUT_SIZE_NODES_W];
	ui_view2d_wx = math_floor(sys_w()) + ui_toolbar_w(true);
	ui_view2d_wy = 0;

	if (!ui_base_show) {
		ui_view2d_ww += g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] + ui_toolbar_w(true);
		ui_view2d_wx -= ui_toolbar_w(true);
	}
	if (!base_view3d_show) {
		ui_view2d_ww += base_view3d_w();
	}

	if (!ui_view2d_show) {
		return;
	}

	if (g_context->pdirty >= 0) {
		ui_view2d_hwnd->redraws = 2; // Paint was active
	}

	// Cache grid
	if (ui_view2d_grid_redraw) {
		if (ui_view2d_grid != NULL) {
			gpu_delete_texture(ui_view2d_grid);
		}
		gc_unroot(ui_view2d_grid);
		ui_view2d_grid = ui_nodes_draw_grid(ui_view2d_pan_scale);
		gc_root(ui_view2d_grid);
		ui_view2d_grid_redraw = false;
	}

	// Ensure UV map is drawn
	if (ui_view2d_uvmap_show || ui_view2d_type == VIEW_2D_TYPE_UVMAP) {
		util_uv_cache_uv_map();
	}

	// Ensure font image is drawn
	if (g_context->font->image == NULL) {
		util_render_make_font_preview();
	}

	g_ui->input_enabled = base_ui_enabled;

	ui_begin(g_ui);

	headerh  = g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1 ? ui_header_h * 2 : ui_header_h;
	i32 apph = iron_window_height() - g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] + headerh;
	if (!base_view3d_show) {
		apph = base_h();
	}
	ui_view2d_wh = iron_window_height() - g_config->layout->buffer[LAYOUT_SIZE_STATUS_H];

	if (ui_nodes_show) {
		ui_view2d_wh -= g_config->layout->buffer[LAYOUT_SIZE_NODES_H];
		if (g_config->touch_ui) {
			ui_view2d_wh += ui_header_h;
		}
	}

	if (!base_view3d_show && ui_nodes_show) {
		ui_view2d_wx = 0;
		ui_view2d_ww = base_view3d_w();
		ui_view2d_wh = iron_window_height() - g_config->layout->buffer[LAYOUT_SIZE_STATUS_H];
	}

	if (ui_window(ui_view2d_hwnd, ui_view2d_wx, ui_view2d_wy, ui_view2d_ww, ui_view2d_wh, false)) {

		if (!g_config->touch_ui) {
			bool expand = !base_view3d_show && !ui_nodes_show && g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] == 0;
			ui_tab(ui_view2d_htab, expand ? string("%s          ", tr("2D View")) : tr("2D View"), false, -1, !base_view3d_show);
			if (ui_tab(ui_view2d_htab, tr("+"), false, -1, false)) {
				ui_view2d_htab->i = 0;
			}
		}

		// Grid
		// draw_set_color(0xffffffff);
		// let step: f32 = ui_nodes_grid_cell_w * ui_view2d_pan_scale;
		// let x: f32    = math_fmod(ui_view2d_pan_x, step) - step;
		// let y: f32    = math_fmod(ui_view2d_pan_y, step) - step;
		// draw_image(ui_view2d_grid, x, y);
		draw_set_color(g_theme->SEPARATOR_COL + 0x00020202);
		draw_filled_rect(0, 0, ui_view2d_ww, ui_view2d_wh);
		draw_set_color(0xffffffff);

		// Texture
		gpu_texture_t *tex     = NULL;
		slot_layer_t  *l       = g_context->layer;
		i32            channel = 0;

		i32 wm = fmin(ui_view2d_ww, ui_view2d_wh);
		i32 tw = wm * 0.9 * ui_view2d_pan_scale;
		i32 tx = ui_view2d_ww / 2.0 - tw / 2.0 + ui_view2d_pan_x;
		i32 ty = apph / 2.0 - tw / 2.0 + ui_view2d_pan_y;

		if (ui_view2d_type == VIEW_2D_TYPE_ASSET) {
			tex = project_get_image(g_context->texture);
		}
		else if (ui_view2d_type == VIEW_2D_TYPE_NODE) {
			ui_node_t *sel = ui_view2d_get_selected_node();
			if (sel != NULL) {
				gpu_texture_t *img = ui_nodes_get_node_preview_image(sel);
				if (img != NULL) {
					tex = img;
				}
			}
		}
		else if (ui_view2d_type == VIEW_2D_TYPE_LAYER || ui_view2d_type == VIEW_2D_TYPE_UVMAP) {
			slot_layer_t *layer = l;

			if (g_config->brush_live && render_path_paint_live_layer_drawn > 0) {
				layer = render_path_paint_live_layer;
			}
			if (g_context->tool == TOOL_TYPE_MATERIAL) {
				layer = render_path_paint_live_layer;
			}

			if (ui_view2d_layer_mode == VIEW_2D_LAYER_MODE_VISIBLE) {
				gpu_texture_t *current = _draw_current;
				bool           in_use  = gpu_in_use;
				if (in_use)
					draw_end();
				layer = layers_flatten(false, NULL);
				if (in_use)
					draw_begin(current, false, 0);
			}
			else if (slot_layer_is_group(layer)) {
				gpu_texture_t *current = _draw_current;
				bool           in_use  = gpu_in_use;
				if (in_use)
					draw_end();
				layer = layers_flatten(false, slot_layer_get_children(layer));
				if (in_use)
					draw_begin(current, false, 0);
			}
			else {
				draw_set_color(color_from_floats(layer->mask_opacity, layer->mask_opacity, layer->mask_opacity, 1.0));
			}

			tex = slot_layer_is_mask(g_context->layer)      ? layer->texpaint
			      : ui_view2d_tex_type == PAINT_TEX_BASE    ? layer->texpaint
			      : ui_view2d_tex_type == PAINT_TEX_OPACITY ? layer->texpaint
			      : ui_view2d_tex_type == PAINT_TEX_NORMAL  ? layer->texpaint_nor
			                                                : layer->texpaint_pack;

			channel = slot_layer_is_mask(g_context->layer)        ? 1
			          : ui_view2d_tex_type == PAINT_TEX_OCCLUSION ? 1
			          : ui_view2d_tex_type == PAINT_TEX_ROUGHNESS ? 2
			          : ui_view2d_tex_type == PAINT_TEX_METALLIC  ? 3
			          : ui_view2d_tex_type == PAINT_TEX_OPACITY   ? 4
			          : ui_view2d_tex_type == PAINT_TEX_HEIGHT    ? 4
			          : ui_view2d_tex_type == PAINT_TEX_NORMAL    ? 5
			                                                      : 0;
		}
		else if (ui_view2d_type == VIEW_2D_TYPE_FONT) {
			tex = g_context->font->image;
		}

		i32 th = tw;
		if (tex != NULL) {
			th = tw * (tex->height / (float)tex->width);
			ty = apph / 2.0 - th / 2.0 + ui_view2d_pan_y;
			if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
				draw_set_pipeline(ui_view2d_pipe);
			}

			ui_view2d_draw_image(tex, tx, ty, tw, th, channel);
			if (ui_view2d_tiled_show) {
				ui_view2d_draw_image(tex, tx - tw, ty, tw, th, channel);
				ui_view2d_draw_image(tex, tx - tw, ty - th, tw, th, channel);
				ui_view2d_draw_image(tex, tx - tw, ty + th, tw, th, channel);
				ui_view2d_draw_image(tex, tx + tw, ty, tw, th, channel);
				ui_view2d_draw_image(tex, tx + tw, ty - th, tw, th, channel);
				ui_view2d_draw_image(tex, tx + tw, ty + th, tw, th, channel);
				ui_view2d_draw_image(tex, tx, ty - th, tw, th, channel);
				ui_view2d_draw_image(tex, tx, ty + th, tw, th, channel);
			}

			if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
				draw_set_pipeline(NULL);
			}
			draw_set_color(0xffffffff);

			// Texture and node preview color picking
			if ((context_in_2d_view(VIEW_2D_TYPE_ASSET) || context_in_2d_view(VIEW_2D_TYPE_NODE)) && g_context->tool == TOOL_TYPE_PICKER && g_ui->input_down) {
				gc_unroot(_ui_view2d_render_tex);
				_ui_view2d_render_tex = tex;
				gc_root(_ui_view2d_render_tex);
				_ui_view2d_render_x = g_ui->input_x - tx - ui_view2d_wx;
				;
				_ui_view2d_render_y  = g_ui->input_y - ty - ui_view2d_wy;
				_ui_view2d_render_tw = tw;
				_ui_view2d_render_th = th;
				sys_notify_on_next_frame(&ui_view2d_render_color_pick, NULL);
			}
		}

		// UV map
		if (ui_view2d_type == VIEW_2D_TYPE_LAYER && ui_view2d_uvmap_show) {
			draw_scaled_image(util_uv_uvmap, tx, ty, tw, th);
		}

		if (ui_view2d_type == VIEW_2D_TYPE_UVMAP) {
			draw_scaled_image(util_uv_uvmap, tx, ty, tw, th);
			edit_uvmap_update();
		}

		// Pixel grid
		if (g_config->view2d_grid_show && ui_view2d_type == VIEW_2D_TYPE_LAYER && tex != NULL) {
			i32 cell  = g_config->view2d_grid_cell;
			f32 stepx = cell / (float)tex->width * tw;
			f32 stepy = cell / (float)tex->height * th;
			draw_set_color(0x55ffffff);
			for (f32 gx = tx; gx <= tx + tw + 0.5; gx += stepx) {
				draw_filled_rect(math_floor(gx), ty, 1, th);
			}
			for (f32 gy = ty; gy <= ty + th + 0.5; gy += stepy) {
				draw_filled_rect(tx, math_floor(gy), tw, 1);
			}
			draw_set_color(0xffffffff);
		}

		// Menu
		i32 top_y = ui_menu_top_y();
		i32 ew    = math_floor(UI_ELEMENT_W());
		draw_set_color(g_theme->WINDOW_BG_COL);
		draw_filled_rect(0, top_y, ui_view2d_ww, UI_ELEMENT_H() + UI_ELEMENT_OFFSET() * 2);
		draw_set_color(0xffffffff);

		f32 start_y = top_y + UI_ELEMENT_OFFSET();
		g_ui->_x    = 2;
		g_ui->_y    = 2 + start_y;

		// Back button
		if (g_config->touch_ui && !base_view3d_show) {
			g_ui->_w = math_floor(ew * 0.7 + 3);
			if (ui_icon_button(tr("Back"), ICON_ARROW_LEFT, UI_ALIGN_CENTER)) {
				g_ui->input_released = false;
				g_config->workspace  = WORKSPACE_PAINT_3D;
				config_save();
				base_update_workspace();
			}
			g_ui->_x += ew * 0.7 + 3;
			g_ui->_y = 2 + start_y;
		}

		bool full = true;
#ifdef IRON_IOS
		if (config_is_iphone()) {
			full = false;
		}
#endif

		// Editable layer name
		if (full) {
			g_ui->_w          = ew;
			ui_handle_t *h    = ui_handle(__ID__);
			char        *text = ui_view2d_type == VIEW_2D_TYPE_NODE ? g_context->node_preview_name : h->text;

			g_ui->_w = math_floor(math_min(draw_string_width(g_font, g_ui->font_size, text) + 15 * UI_SCALE(), 100 * UI_SCALE()));

			if (ui_view2d_type == VIEW_2D_TYPE_ASSET) {
				asset_t *asset = g_context->texture;
				if (asset != NULL) {
					h->text     = string_copy(asset->name);
					asset->name = string_copy(ui_text_input(h, "", UI_ALIGN_LEFT, true, false));
				}
			}
			else if (ui_view2d_type == VIEW_2D_TYPE_NODE) {
				ui_node_t *sel = ui_view2d_get_selected_node();
				if (sel != NULL && !string_equals(sel->type, "GROUP")) {
					h->text                      = string_copy(sel->name);
					sel->name                    = string_copy(ui_text_input(h, "", UI_ALIGN_LEFT, true, false));
					g_context->node_preview_name = string_copy(sel->name);
					ui_view2d_text_input_hover   = g_ui->is_hovered;
				}
				else {
					ui_text(g_context->node_preview_name, UI_ALIGN_LEFT, 0x00000000);
				}
			}
			else if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
				h->text        = string_copy(l->name);
				char *new_name = string_copy(ui_text_input(h, "", UI_ALIGN_LEFT, true, false));
				tab_stages_rename_layer(l->name, new_name);
				l->name                    = new_name;
				ui_view2d_text_input_hover = g_ui->is_hovered;
			}
			else if (ui_view2d_type == VIEW_2D_TYPE_FONT) {
				h->text               = string_copy(g_context->font->name);
				g_context->font->name = ui_text_input(h, "", UI_ALIGN_LEFT, true, false);
			}

			if (h->changed) {
				ui_base_hwnds->buffer[0]->redraws = 2;
			}
			g_ui->_x += g_ui->_w + 3;
			g_ui->_y = 2 + start_y;
		}

		g_ui->_w = ew;

		if (ui_view2d_type == VIEW_2D_TYPE_LAYER) {
			ui_handle_t *h_layer_mode        = ui_handle(__ID__);
			h_layer_mode->i                  = ui_view2d_layer_mode;
			string_array_t *layer_mode_combo = any_array_create_from_raw(
			    (void *[]){
			        tr("Visible"),
			        tr("Selected"),
			    },
			    2);
			ui_view2d_layer_mode = ui_combo(h_layer_mode, layer_mode_combo, tr("Layers"), false, UI_ALIGN_LEFT, true);
			g_ui->_x += ew + 3;
			g_ui->_y = 2 + start_y;

			if (!slot_layer_is_mask(g_context->layer)) {
				ui_handle_t *h_tex_type        = ui_handle(__ID__);
				h_tex_type->i                  = ui_view2d_tex_type;
				string_array_t *tex_type_combo = any_array_create_from_raw(
				    (void *[]){
				        tr("Base Color"),
				        tr("Opacity"),
				        tr("Normal Map"),
				        tr("Occlusion"),
				        tr("Roughness"),
				        tr("Metallic"),
				        tr("Height"),
				    },
				    7);

				if (g_config->workflow == WORKFLOW_BASE) {
					array_splice(tex_type_combo, 6, 1);
					array_splice(tex_type_combo, 5, 1);
					array_splice(tex_type_combo, 4, 1);
					array_splice(tex_type_combo, 3, 1);
					array_splice(tex_type_combo, 2, 1);
				}

				ui_view2d_tex_type = ui_combo(h_tex_type, tex_type_combo, tr("Texture"), false, UI_ALIGN_LEFT, true);
				g_ui->_x += ew + 3;
				g_ui->_y = 2 + start_y;
			}
		}

		// Zoom slider
		if (full && tex != NULL) {
			ui_handle_t *h_zoom        = ui_handle(__ID__);
			i32          scale_percent = math_round((tw / (float)tex->width) * 100);
			h_zoom->f                  = scale_percent;
			g_ui->_w                   = math_floor(ew + 3);
			f32 new_percent            = ui_slider(h_zoom, string("%%", scale_percent), 1, 100, true, 1, true, UI_ALIGN_RIGHT, true);
			if (h_zoom->changed) {
				ui_view2d_pan_scale     = new_percent / 100.0 * tex->width / (wm * 0.9);
				ui_view2d_hwnd->redraws = 2;
			}
			g_ui->_x += ew + 3;
			g_ui->_y = 2 + start_y;
		}

		g_ui->_w = math_floor(ew * 0.7 + 3);
		if (ui_icon_button("Edit", ICON_EDIT, UI_ALIGN_CENTER)) {
			gc_unroot(ui_view2d_tex);
			ui_view2d_tex = tex;
			gc_root(ui_view2d_tex);
			ui_menu_draw(&ui_view2d_draw_edit, -1, -1);
		}
		g_ui->_x += ew * 0.7 + 3;
		g_ui->_y = 2 + start_y;

		// Picked position
		if (g_context->tool == TOOL_TYPE_PICKER && (ui_view2d_type == VIEW_2D_TYPE_LAYER || ui_view2d_type == VIEW_2D_TYPE_ASSET)) {
			gpu_texture_t *cursor_img = resource_get("cursor.k");
			f32            hsize      = 16 * UI_SCALE();
			f32            size       = hsize * 2;
			draw_scaled_image(cursor_img, tx + tw * g_context->uvx_picked - hsize, ty + th * g_context->uvy_picked - hsize, size, size);
		}
	}
	ui_end();

	g_ui->input_enabled = true;
}
