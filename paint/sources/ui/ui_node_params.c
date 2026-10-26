
#include "../global.h"

static ui_node_canvas_t *ui_node_params_canvas   = NULL;  // Canvas before the edit, for undo
static i32               ui_node_params_type     = 0;     // Canvas type of the pending edit
static mesh_object_t    *ui_node_params_object   = NULL;  // Object of the pending mesh edit
static bool              ui_node_params_pending  = false; // Edited, recompile once the input is released
static bool              ui_node_params_live     = false; // Edited this frame, refresh the preview
static bool              ui_node_params_hooked   = false;
static i32               ui_node_params_node     = -1; // Color popup target, node values can move in memory
static i32               ui_node_params_socket   = -1;
static ui_id_t           ui_node_params_combo_id = 0; // Texture combo of the socket buttons
static ui_color_state_t  ui_node_params_color_state;
static f32               ui_node_params_heights[3]; // Parameters height of the last frame per canvas type

static ui_node_canvas_t *ui_node_params_get_canvas(i32 canvas_type) {
	if (canvas_type == CANVAS_TYPE_MESH) {
		mesh_nodes_t *slot = nodes_mesh_get(g_context->paint_object, true);
		return slot != NULL ? slot->canvas : NULL;
	}
	return canvas_type == CANVAS_TYPE_BRUSH ? g_context->brush->canvas : g_context->material->canvas;
}

// Edited node, first match in order of priority
static ui_node_t *ui_node_params_source(i32 canvas_type) {
	char  *material_types[] = {"GROUP", "SHADER_GPU", "OUTPUT_MATERIAL_PBR"};
	char  *brush_types[]    = {"brush_output_node"};
	char  *mesh_types[]     = {"MESH_SCRIPT", "MESH_OUTPUT"};
	char **types            = canvas_type == CANVAS_TYPE_BRUSH ? brush_types : canvas_type == CANVAS_TYPE_MESH ? mesh_types : material_types;
	i32    count            = canvas_type == CANVAS_TYPE_BRUSH ? 1 : canvas_type == CANVAS_TYPE_MESH ? 2 : 3;

	ui_node_canvas_t *canvas = ui_node_params_get_canvas(canvas_type);
	if (canvas == NULL) {
		return NULL;
	}
	for (i32 t = 0; t < count; ++t) {
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *n = canvas->nodes->buffer[i];
			if (string_equals(n->type, types[t])) {
				return n;
			}
		}
	}
	return NULL;
}

static ui_node_link_t *ui_node_params_link(ui_node_canvas_t *canvas, ui_node_t *node, i32 socket) {
	for (i32 i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (l->to_id == node->id && l->to_socket == socket) {
			return l;
		}
	}
	return NULL;
}

static void ui_node_params_begin(i32 canvas_type) {
	if (g_ui->input_started || g_ui->is_key_pressed) {
		ui_node_params_canvas = util_clone_canvas(ui_node_params_get_canvas(canvas_type));
		ui_node_params_type   = canvas_type;
		ui_node_params_object = g_context->paint_object;
	}
	g_ui->changed = false;
}

static void ui_node_params_update_brush() {
	make_material_parse_brush();
	brush_output_node_parse_inputs();
	util_render_make_brush_preview();
	if (slot_layer_is_path(g_context->layer)) {
		util_layer_repaint_path(g_context->layer);
	}
}

// Rendering is not allowed while the sidebar window draws, the previews are updated here instead
static void ui_node_params_update(void *_) {
	// Instant preview while dragging
	if (ui_node_params_live) {
		ui_node_params_live = false;
		if (g_config->material_live && ui_node_params_type != CANVAS_TYPE_MESH) {
			if (ui_node_params_type == CANVAS_TYPE_BRUSH) {
				ui_node_params_update_brush();
			}
			else if (layers_is_fill_material()) {
				layers_update_fill_layers();
			}
			else if (layers_is_path_material()) {
				layers_update_path_layers();
			}
			else {
				util_render_make_material_preview();
			}
		}
	}

	// Recompile once the edit is done
	if (!ui_node_params_pending || g_ui->input_down) {
		return;
	}
	ui_node_params_pending = false;

	if (ui_node_params_canvas != NULL) {
		history_edit_nodes(ui_node_params_canvas, ui_node_params_type, -1);
	}
	if (ui_node_params_type == CANVAS_TYPE_MESH) {
		// Meshes are rebuilt once the change is done
		if (array_index_of(g_project->_->paint_objects, ui_node_params_object) != -1) {
			nodes_mesh_update(ui_node_params_object, false);
		}
	}
	else if (ui_node_params_type == CANVAS_TYPE_BRUSH) {
		ui_node_params_update_brush();
		make_material_parse_paint_material(false);
	}
	else {
		make_material_parse_paint_material(true);
		if (layers_is_fill_material()) {
			layers_update_fill_layers();
		}
		if (layers_is_path_material()) {
			layers_update_path_layers();
		}
		util_render_make_material_preview();
		tab_meshes_refresh_overrides(g_context->material);
		if (context_is_decal()) {
			util_render_make_decal_preview();
		}
	}
	ui_nodes_hwnd->redraws = 2;
	ui_dock_redraw(-1);
}

static void ui_node_params_end() {
	if (!g_ui->changed) {
		return;
	}
	ui_node_params_pending = true;
	ui_node_params_live    = true;
	ui_nodes_hwnd->redraws = 2;
	if (!ui_node_params_hooked) {
		ui_node_params_hooked = true;
		sys_notify_on_update(&ui_node_params_update, NULL);
	}
}

static void ui_node_params_color_menu() {
	ui_node_t *node = ui_node_params_source(ui_node_params_type);
	if (node == NULL || node->id != ui_node_params_node || ui_node_params_socket >= node->inputs->length) {
		return;
	}
	f32 *val = node->inputs->buffer[ui_node_params_socket]->default_value->buffer;
	ui_fill(0, 0, g_ui->_w / (float)UI_SCALE(), g_theme->ELEMENT_H * 9, g_theme->SEPARATOR_COL);
	ui_node_params_begin(ui_node_params_type);
	u32 color = color_from_floats(val[0], val[1], val[2], 1.0);
	ui_color_wheel(&color, &ui_node_params_color_state, false, -1, 10 * g_theme->ELEMENT_H * UI_SCALE(), false, NULL, NULL);
	if (g_ui->changed) {
		val[0]            = color_get_rb(color) / 255.0;
		val[1]            = color_get_gb(color) / 255.0;
		val[2]            = color_get_bb(color) / 255.0;
		ui_menu_keep_open = true;
	}
	ui_node_params_end();
}

// Space right of the entries for the input socket buttons
static f32 ui_node_params_socket_w() {
	return 20 * UI_SCALE();
}

// Image of the File button, NULL when the texture was deleted
static asset_t *ui_node_params_tex_image_asset(ui_node_t *node) {
	i32 i = node->buttons->buffer[0]->default_value->buffer[0];
	return i >= 0 && i < g_project->_->assets->length ? g_project->_->assets->buffer[i] : NULL;
}

static ui_node_t *ui_node_params_create_tex_image(i32 canvas_type, ui_node_canvas_t *canvas) {
	if (canvas_type == CANVAS_TYPE_MESH) {
		return nodes_mesh_create_node(g_context->paint_object, "TEX_IMAGE");
	}
	if (canvas_type == CANVAS_TYPE_BRUSH) {
		return nodes_brush_create_node("TEX_IMAGE");
	}
	// Into the material canvas, the node editor may show a group
	nodes_material_init();
	ui_node_t *n = ui_nodes_make_node(nodes_material_get_node_t("TEX_IMAGE"), g_context->material->nodes, canvas);
	any_array_push(canvas->nodes, n);
	return n;
}

static bool ui_node_params_is_used(ui_node_canvas_t *canvas, ui_node_t *node) {
	for (i32 i = 0; i < canvas->links->length; ++i) {
		if (canvas->links->buffer[i]->from_id == node->id) {
			return true;
		}
	}
	return false;
}

// Texture picked in the socket combo, sets the File of the connected Image Texture or connects a new one
// None (-1) disconnects the texture, the Image Texture is removed when its outputs are not used elsewhere
static void ui_node_params_set_texture(i32 index) {
	ui_node_canvas_t *canvas = ui_node_params_get_canvas(ui_node_params_type);
	ui_node_t        *node   = ui_node_params_source(ui_node_params_type);
	if (canvas == NULL || node == NULL || node->id != ui_node_params_node || ui_node_params_socket >= node->inputs->length || index < -1 ||
	    index >= g_project->_->assets->length) {
		return;
	}
	ui_node_link_t *link = ui_node_params_link(canvas, node, ui_node_params_socket);
	ui_node_t      *from = link != NULL ? ui_get_node(canvas->nodes, link->from_id) : NULL;
	if (index == -1 && link == NULL) {
		return;
	}
	ui_node_params_canvas = util_clone_canvas(canvas);
	ui_node_params_object = g_context->paint_object;

	if (index == -1) {
		array_remove(canvas->links, link);
		if (from != NULL && string_equals(from->type, "TEX_IMAGE") && !ui_node_params_is_used(canvas, from)) {
			ui_remove_node(from, canvas);
		}
	}
	else if (from == NULL || !string_equals(from->type, "TEX_IMAGE")) {
		if (link != NULL) {
			array_remove(canvas->links, link);
		}
		from    = ui_node_params_create_tex_image(ui_node_params_type, canvas);
		from->x = node->x - 220;
		from->y = node->y + ui_node_params_socket * 40;
		any_array_push(canvas->links, project_create_node_link(canvas->links, from->id, 0, node->id, ui_node_params_socket));
	}
	if (index != -1) {
		from->buttons->buffer[0]->default_value->buffer[0] = index;
	}
	g_ui->changed = true;
	ui_node_params_end();
}

// Button with a down arrow drawn in the space right of the entry, keeps the layout position
// Opens the texture list of the Image Texture File combo
static void ui_node_params_draw_socket(i32 canvas_type, ui_node_t *node, i32 socket) {
	f32 _x   = g_ui->_x;
	f32 _y   = g_ui->_y;
	f32 _w   = g_ui->_w;
	g_ui->_x = _x + _w + 2 * UI_SCALE();
	g_ui->_w = ui_node_params_socket_w() - 2 * UI_SCALE();
	if (ui_button("", UI_ALIGN_CENTER, "")) {
		ui_node_canvas_t *canvas = ui_node_params_get_canvas(canvas_type);
		ui_node_link_t   *link   = ui_node_params_link(canvas, node, socket);
		ui_node_t        *from   = link != NULL ? ui_get_node(canvas->nodes, link->from_id) : NULL;
		asset_t          *asset  = from != NULL && string_equals(from->type, "TEX_IMAGE") ? ui_node_params_tex_image_asset(from) : NULL;
		ui_node_params_node      = node->id;
		ui_node_params_socket    = socket;
		ui_node_params_type      = canvas_type;
		ui_node_params_combo_id  = ui_widget_id(&ui_node_params_combo_id, UI_ID_COMBO);
		// List spans the entry and the button
		g_ui->_x = _x;
		g_ui->_y = _y;
		g_ui->_w = _w + ui_node_params_socket_w();
		// None first, then the textures
		string_array_t *texts  = string_array_create(0);
		any_array_t    *images = any_array_create(0);
		string_array_push(texts, tr("None"));
		any_array_push(images, NULL);
		for (i32 i = 0; i < g_project->_->assets->length; ++i) {
			string_array_push(texts, g_project->_->assets->buffer[i]->name);
			any_array_push(images, project_get_image(g_project->_->assets->buffer[i]));
		}
		ui_combo_open(ui_node_params_combo_id, asset != NULL ? array_index_of(g_project->_->assets, asset) + 1 : 0, texts, tr("File"), UI_ALIGN_LEFT, true);
		g_ui->combo_selected_images = images;
		array_free(texts); // Copied by the combo
		free(texts);
	}
	f32 w = 6 * UI_SCALE();
	f32 h = 4 * UI_SCALE();
	f32 x = _x + _w + 2 * UI_SCALE() + (ui_node_params_socket_w() - 2 * UI_SCALE() - w) / 2.0;
	f32 y = _y + (UI_ELEMENT_H() - h) / 2.0;
	draw_set_color(g_theme->LABEL_COL);
	draw_filled_triangle(x, y, x + w, y, x + w / 2.0, y + h);
	draw_set_color(0xffffffff);
	g_ui->_x = _x;
	g_ui->_y = _y;
	g_ui->_w = _w;
}

// Small preview at the right end of the current element, the element shrinks to the left of it
static void ui_node_params_draw_preview(gpu_texture_t *img) {
	f32 size = UI_ELEMENT_H() - 4 * UI_SCALE();
	f32 x    = g_ui->_x + g_ui->_w - size - 2 * UI_SCALE();
	f32 y    = g_ui->_y + (UI_ELEMENT_H() - size) / 2.0;
	if (img != NULL) {
		// Fit inside the square, keep the aspect ratio
		f32 w = img->width >= img->height ? size : size * img->width / (float)img->height;
		f32 h = img->height >= img->width ? size : size * img->height / (float)img->width;
		draw_set_color(0xffffffff);
		draw_scaled_image(img, x + (size - w) / 2.0, y + (size - h) / 2.0, w, h);
	}
	g_ui->_w -= size + 2 * UI_SCALE();
}

// Stroke preview of the brush, spans the width of the panel
static void ui_node_params_draw_brush_preview() {
	gpu_texture_t *img = g_context->brush->image_wave;
	f32            w   = math_floor(g_ui->_window_w - 24 * UI_SCALE());
	f32            h   = math_floor(w * util_render_brush_wave_strip);
	if (img != NULL && g_context->brush->preview_ready) {
		draw_set_color(0xffffffff);
		draw_scaled_image(img, g_ui->_x + (g_ui->_w - w) / 2.0, g_ui->_y, w, h);
	}
	ui_end_element_of_size(h);
}

static void ui_node_params_draw_buttons(ui_node_t *node) {
	for (i32 i = 0; i < node->buttons->length; ++i) {
		ui_node_button_t *but = node->buttons->buffer[i];
		f32              *val = but->output == -1 ? but->default_value->buffer : node->outputs->buffer[but->output]->default_value->buffer;
		if (string_equals(but->type, "VALUE")) {
			ui_slider(val, tr(but->name), but->min, but->max, true, but->precision, true, UI_ALIGN_LEFT, true);
		}
		else if (string_equals(but->type, "BOOL")) {
			bool value = val[0] != 0.0;
			ui_check(&value, tr(but->name), "");
			val[0] = value ? 1.0 : 0.0;
		}
	}
}

static void ui_node_params_draw_inputs(i32 canvas_type, ui_node_t *node) {
	ui_node_canvas_t *canvas = ui_node_params_get_canvas(canvas_type);
	i32               count  = node->inputs->length;
	if (string_equals(node->type, "OUTPUT_MATERIAL_PBR")) {
		count = math_min(count, 6); // Base Color to Normal Map, skip Emission, Height and Subsurface
	}
	for (i32 i = 0; i < count; ++i) {
		ui_node_socket_t *inp  = node->inputs->buffer[i];
		ui_node_link_t   *link = ui_node_params_link(canvas, node, i);
		ui_node_params_draw_socket(canvas_type, node, i);
		if (link != NULL) {
			ui_node_t *from = ui_get_node(canvas->nodes, link->from_id);
			ui_row2();
			ui_text(tr(inp->name), UI_ALIGN_LEFT, 0x00000000);
			asset_t *asset = from != NULL && string_equals(from->type, "TEX_IMAGE") ? ui_node_params_tex_image_asset(from) : NULL;
			if (asset != NULL) {
				ui_node_params_draw_preview(project_get_image(asset));
			}
			ui_text(asset != NULL ? asset->name : from != NULL ? tr(from->name) : "", UI_ALIGN_RIGHT, 0x00000000);
		}
		else if (string_equals(inp->type, "VALUE")) {
			ui_slider(inp->default_value->buffer, tr(inp->name), inp->min, inp->max, true, inp->precision, true, UI_ALIGN_LEFT, true);
		}
		else if (string_equals(inp->type, "RGBA")) {
			f32 *val = inp->default_value->buffer;
			ui_row2();
			ui_text(tr(inp->name), UI_ALIGN_LEFT, 0x00000000);
			// Use the returned state, is_hovered is stale when the row is scrolled out of view
			ui_state_t state = ui_swatch(color_from_floats(val[0], val[1], val[2], 1.0));
			if (state == UI_STATE_RELEASED && g_ui->combo_selected_id == 0) {
				ui_node_params_node   = node->id;
				ui_node_params_socket = i;
				ui_node_params_type   = canvas_type;
				ui_menu_draw(&ui_node_params_color_menu, -1, -1);
			}
		}
		else if (string_equals(inp->type, "VECTOR") && inp->display == 1) {
			f32 *val = inp->default_value->buffer;
			ui_text(tr(inp->name), UI_ALIGN_LEFT, 0x00000000);
			ui_row3();
			ui_slider(&val[0], "X", inp->min, inp->max, true, inp->precision, true, UI_ALIGN_LEFT, true);
			ui_slider(&val[1], "Y", inp->min, inp->max, true, inp->precision, true, UI_ALIGN_LEFT, true);
			ui_slider(&val[2], "Z", inp->min, inp->max, true, inp->precision, true, UI_ALIGN_LEFT, true);
		}
		else {
			ui_text(tr(inp->name), UI_ALIGN_LEFT, 0x00000000);
		}
	}
}

// Clear background from y down to the bottom of the tab, the meshes tab draws alternating lines
static void ui_node_params_clear(f32 y) {
	if (y < g_ui->_window_h) {
		draw_set_color(g_theme->WINDOW_BG_COL);
		draw_filled_rect(0, y, g_ui->_window_w, g_ui->_window_h - y);
		draw_set_color(0xffffffff);
	}
}

// Lay out the parameters below the window, the elements are skipped there but still advance the layout
static f32 ui_node_params_measure(i32 canvas_type, ui_node_t *node) {
	f32 _x   = g_ui->_x;
	f32 _y   = g_ui->_y;
	f32 _w   = g_ui->_w;
	f32 y    = g_ui->current_window->texture.height + g_ui->current_window->scroll_offset + 1;
	g_ui->_y = y;
	g_ui->_w -= 8 * UI_SCALE();
	if (canvas_type == CANVAS_TYPE_BRUSH) {
		ui_node_params_draw_brush_preview();
	}
	g_ui->_w -= ui_node_params_socket_w();
	ui_node_params_draw_buttons(node);
	ui_node_params_draw_inputs(canvas_type, node);
	f32 h    = g_ui->_y - y;
	g_ui->_x = _x;
	g_ui->_y = _y;
	g_ui->_w = _w;
	return h;
}

void ui_node_params_draw(i32 canvas_type, bool *show) {
	// Apply the picked texture once the combo is closed, escape submits the initial value
	if (ui_node_params_combo_id != 0 && g_ui->submit_combo_id == ui_node_params_combo_id && g_ui->combo_selected_id == 0) {
		g_ui->submit_combo_id = 0;
		if (g_ui->combo_to_submit != g_ui->combo_initial_value) {
			ui_node_params_set_texture(g_ui->combo_to_submit - 1); // Skip None
		}
	}

	ui_node_t *node = ui_node_params_source(canvas_type);
	if (node == NULL) {
		return;
	}

	// Outlined section with the toggle button at the bottom of the tab, the parameters expand it upwards
	// Toggle button sticks to the bottom of the tab, in line with the collapsed status bar
	f32 bottom = g_ui->_window_h - UI_ELEMENT_H() - UI_ELEMENT_OFFSET() + g_ui->current_window->scroll_offset;
	f32 _x     = g_ui->_x;
	f32 _w     = g_ui->_w;
	f32 pad    = UI_ELEMENT_OFFSET() * 2;
	g_ui->_y += pad;
	// Parameters rest on the button when there is space, placed by their height in the last frame
	// Measured up front when shown for the first time, the misplaced frame would reserve a scrollbar
	if (*show && ui_node_params_heights[canvas_type] == 0) {
		ui_node_params_heights[canvas_type] = ui_node_params_measure(canvas_type, node);
	}
	f32 params_h = ui_node_params_heights[canvas_type];
	if (*show && g_ui->_y + pad + params_h < bottom) {
		g_ui->_y = bottom - pad - params_h;
	}
	f32 section_y = g_ui->_y;
	g_ui->_y += pad;
	g_ui->_x += 4 * UI_SCALE(); // Keep the content inside the outline
	g_ui->_w -= 8 * UI_SCALE();

	if (*show) {
		ui_node_params_clear(section_y);
		f32 params_y = g_ui->_y;
		ui_node_params_begin(canvas_type);
		if (canvas_type == CANVAS_TYPE_BRUSH) {
			ui_node_params_draw_brush_preview();
		}
		g_ui->_w -= ui_node_params_socket_w();
		ui_node_params_draw_buttons(node);
		ui_node_params_draw_inputs(canvas_type, node);
		g_ui->_w += ui_node_params_socket_w();
		ui_node_params_end();
		if (g_ui->_y - params_y != params_h) {
			// Lay out again, one more frame to drop the scrollbar width a misplaced frame may have reserved
			ui_node_params_heights[canvas_type] = g_ui->_y - params_y;
			g_ui->current_window->redraws       = 3;
		}
	}

	if (g_ui->_y < bottom) {
		g_ui->_y = bottom;
	}
	if (!*show) {
		// Outline only the button, slightly taller than it
		section_y = g_ui->_y - 2 * UI_SCALE();
		ui_node_params_clear(section_y);
	}

	// Highlighted while the parameters are shown, only outlined otherwise
	i32 _BUTTON_COL     = g_theme->BUTTON_COL;
	i32 _HOVER_COL      = g_theme->HOVER_COL;
	i32 _HIGHLIGHT_COL  = g_theme->HIGHLIGHT_COL;
	i32 _FILL_BUTTON_BG = g_theme->FILL_BUTTON_BG;
	if (*show) {
		g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;
		g_theme->HOVER_COL  = g_theme->HIGHLIGHT_COL;
	}
	else {
		g_theme->FILL_BUTTON_BG = false;
		g_theme->HIGHLIGHT_COL  = g_theme->HOVER_COL; // Unfilled buttons hover with the highlight color
	}
	if (ui_button(tr("Parameters"), UI_ALIGN_CENTER, "")) {
		*show = !*show;
		// Lay out again next frame, the parameters are drawn above the button
		g_ui->current_window->redraws = 2;
	}
	g_theme->BUTTON_COL     = _BUTTON_COL;
	g_theme->HOVER_COL      = _HOVER_COL;
	g_theme->HIGHLIGHT_COL  = _HIGHLIGHT_COL;
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;

	// Drop the offset after the button, the content has to end inside the window or a scrollbar is reserved
	g_ui->_y -= UI_ELEMENT_OFFSET();
	g_ui->_x = _x;
	g_ui->_w = _w;
	f32 h    = g_ui->_y - section_y;
	ui_rect_round(1, -h / UI_SCALE(), g_ui->_w / (float)UI_SCALE() - 2, h / UI_SCALE(), g_theme->BUTTON_COL, 2);
}
