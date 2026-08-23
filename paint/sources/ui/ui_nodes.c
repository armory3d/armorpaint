
#include "../global.h"

bool                     ui_nodes_show_menu       = false;
bool                     ui_nodes_show_menu_first = true;
bool                     ui_nodes_hide_menu       = false;
i32                      ui_nodes_menu_category   = 0;
f32                      ui_nodes_popup_x         = 0.0;
f32                      ui_nodes_popup_y         = 0.0;
i32                      ui_nodes_node_search_x;
i32                      ui_nodes_node_search_y;
bool                     ui_nodes_uichanged_last        = false;
bool                     ui_nodes_recompile_mat         = false; // Mat preview
bool                     ui_nodes_recompile_mat_final   = false;
ui_node_t               *ui_nodes_node_search_spawn     = NULL;
i32                      ui_nodes_node_search_offset    = 0;
ui_node_canvas_t        *ui_nodes_last_canvas           = NULL;
i32                      ui_nodes_last_node_selected_id = -1;
bool                     ui_nodes_release_link          = false;
bool                     ui_nodes_is_node_menu_op       = false;
gpu_texture_t           *ui_nodes_grid                  = NULL;
bool                     ui_nodes_controls_down         = false;
slot_material_t_array_t *ui_nodes_tabs                  = NULL;
ui_node_link_t          *_ui_nodes_on_link_drag_link_drag;
ui_node_t               *_ui_nodes_on_link_drag_node;
ui_node_socket_t        *_ui_nodes_on_socket_released_socket;
ui_node_t               *_ui_nodes_on_socket_released_node;
ui_node_t               *_ui_nodes_on_canvas_released_selected;
bool                     _ui_nodes_node_search_first;
void (*_ui_nodes_node_search_done)(void);
ui_node_t *ui_nodes_node_changed = NULL;
void (*_ui_nodes_render_tmp)(i32);

void ui_viewnodes_on_link_drag_on_node_search_done() {
	ui_nodes_t *ui_nodes = ui_nodes_get_nodes();

	i32 node_selected_id = _ui_nodes_on_link_drag_node->id;
	if (ui_nodes->nodes_selected_id->length > 0) {
		node_selected_id = ui_nodes->nodes_selected_id->buffer[0];
	}

	ui_node_link_t    *link_drag = _ui_nodes_on_link_drag_link_drag;
	ui_node_t_array_t *nodes     = ui_nodes_get_canvas(true)->nodes;
	ui_node_t         *n         = ui_get_node(nodes, node_selected_id);
	if (link_drag->to_id == -1 && n->inputs->length > 0) {
		link_drag->to_id = n->id;
		char *from_type  = _ui_nodes_on_link_drag_node->outputs->buffer[link_drag->from_socket]->type;
		// Connect to the first socket
		link_drag->to_socket = 0;
		// Try to find the first type-matching socket and use it if present
		for (i32 i = 0; i < n->inputs->length; ++i) {
			ui_node_socket_t *socket = n->inputs->buffer[i];
			if (string_equals(socket->type, from_type)) {
				link_drag->to_socket = array_index_of(n->inputs, socket);
				break;
			}
		}
		any_array_push(ui_nodes_get_canvas(true)->links, link_drag);
	}
	else if (link_drag->from_id == -1 && n->outputs->length > 0) {
		link_drag->from_id     = n->id;
		link_drag->from_socket = 0;
		any_array_push(ui_nodes_get_canvas(true)->links, link_drag);
	}
}

void ui_nodes_node_search_menu() {
	ui_menu_h                  = UI_ELEMENT_H() * 8;
	ui_handle_t *search_handle = ui_handle(__ID__);
	char        *search        = to_lower_case(ui_text_input(search_handle, "", UI_ALIGN_LEFT, true, true));
	g_ui->changed              = false;
	if (_ui_nodes_node_search_first) {
		_ui_nodes_node_search_first = false;
		search_handle->text         = "";
		ui_start_text_edit(search_handle, UI_ALIGN_LEFT); // Focus search bar
	}

	if (search_handle->changed) {
		ui_nodes_node_search_offset = 0;
	}

	if (g_ui->is_key_pressed) { // Move selection
		if (g_ui->key_code == KEY_CODE_DOWN && ui_nodes_node_search_offset < 6) {
			ui_nodes_node_search_offset++;
		}
		if (g_ui->key_code == KEY_CODE_UP && ui_nodes_node_search_offset > 0) {
			ui_nodes_node_search_offset--;
		}
	}
	bool enter              = keyboard_down("enter");
	i32  count              = 0;
	i32  _BUTTON_COL        = g_theme->BUTTON_COL;
	bool _FILL_BUTTON_BG    = g_theme->FILL_BUTTON_BG;
	g_theme->FILL_BUTTON_BG = true;
	bool _SHADOWS           = g_theme->SHADOWS;
	g_theme->SHADOWS        = false;

	node_list_t_array_t *node_list = ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL ? nodes_material_list : nodes_brush_list;

	for (i32 i = 0; i < node_list->length; ++i) {
		ui_node_t_array_t *list = node_list->buffer[i];
		for (i32 i = 0; i < list->length; ++i) {
			ui_node_t *n = list->buffer[i];
			if (string_index_of(to_lower_case(tr(n->name)), search) >= 0) {
				g_theme->BUTTON_COL = count == ui_nodes_node_search_offset ? g_theme->HIGHLIGHT_COL : g_theme->SEPARATOR_COL;

				if (ui_button(tr(n->name), UI_ALIGN_LEFT, "") || (enter && count == ui_nodes_node_search_offset)) {
					ui_nodes_push_undo(NULL);
					ui_nodes_t       *nodes    = ui_nodes_get_nodes();
					ui_node_canvas_t *canvas   = ui_nodes_get_canvas(true);
					ui_nodes_node_search_spawn = ui_nodes_make_node(n, nodes, canvas); // Spawn selected node
					any_array_push(canvas->nodes, ui_nodes_node_search_spawn);
					nodes->nodes_selected_id = i32_array_create_from_raw(
					    (i32[]){
					        ui_nodes_node_search_spawn->id,
					    },
					    1);
					nodes->nodes_drag = true;

					ui_nodes_hwnd->redraws = 2;
					if (enter) {
						g_ui->changed = true;
						count         = 6; // Trigger break
					}
					if (_ui_nodes_node_search_done != NULL) {
						_ui_nodes_node_search_done();
					}
				}
				if (++count > 6) {
					break;
				}
			}
		}
		if (count > 6) {
			break;
		}
	}
	if (enter && count == 0) { // Hide popup on enter when node is not found
		g_ui->changed       = true;
		search_handle->text = "";
	}
	g_theme->BUTTON_COL     = _BUTTON_COL;
	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
	g_theme->SHADOWS        = _SHADOWS;
}

void ui_nodes_node_search(i32 x, i32 y, void (*done)(void)) {
	_ui_nodes_node_search_first = true;
	_ui_nodes_node_search_done  = done;
	ui_menu_draw(&ui_nodes_node_search_menu, x, y);
}

void ui_viewnodes_on_link_drag(i32 link_drag_id, bool is_new_link) {
	if (!is_new_link) {
		return;
	}

	ui_node_link_t_array_t *links     = ui_nodes_get_canvas(true)->links;
	ui_node_link_t         *link_drag = ui_get_link(links, link_drag_id);
	ui_node_t_array_t      *nodes     = ui_nodes_get_canvas(true)->nodes;
	ui_node_t              *node      = ui_get_node(nodes, link_drag->from_id > -1 ? link_drag->from_id : link_drag->to_id);
	i32                     link_x    = g_ui->_window_x + UI_NODE_X(node);
	i32                     link_y    = g_ui->_window_y + UI_NODE_Y(node);
	if (link_drag->from_id > -1) {
		link_x += UI_NODE_W(node);
		link_y += UI_OUTPUT_Y(node, link_drag->from_socket);
	}
	else {
		link_y += ui_nodes_INPUT_Y(ui_nodes_get_canvas(true), node, link_drag->to_socket) + UI_OUTPUTS_H(node, -1) + UI_BUTTONS_H(node);
	}

	if (math_abs(mouse_x - link_x) > 5 || math_abs(mouse_y - link_y) > 5) { // Link length

		_ui_nodes_on_link_drag_link_drag = link_drag;
		_ui_nodes_on_link_drag_node      = node;
		ui_nodes_node_search(-1, -1, &ui_viewnodes_on_link_drag_on_node_search_done);
	}
	// Selecting which node socket to preview
	else if (link_drag->from_id > -1) {
		i32_imap_set(g_context->node_preview_socket_map, node->id, link_drag->from_socket);
		ui_nodes_node_changed = node;
	}
}

void ui_viewnodes_on_socket_released_group_edit_box() {
	ui_node_socket_t *socket     = _ui_nodes_on_socket_released_socket;
	ui_node_t        *node       = _ui_nodes_on_socket_released_node;
	string_array_t   *type_combo = any_array_create_from_raw_tmp(
        (void *[]){
            tr("Color"),
            tr("Vector"),
            tr("Value"),
        },
        3);
	i32 type = ui_combo(_ui_nodes_htype, type_combo, tr("Type"), true, UI_ALIGN_LEFT, true);
	if (_ui_nodes_htype->changed) {
		_ui_nodes_hname->text = type == 0 ? tr("Color") : type == 1 ? tr("Vector") : tr("Value");
	}
	char        *name          = ui_text_input(_ui_nodes_hname, tr("Name"), UI_ALIGN_LEFT, true, false);
	f32          min           = ui_float_input(_ui_nodes_hmin, tr("Min"), UI_ALIGN_LEFT, 1000.0);
	f32          max           = ui_float_input(_ui_nodes_hmax, tr("Max"), UI_ALIGN_LEFT, 1000.0);
	f32_array_t *default_value = NULL;
	if (type == 0) {
		ui_row4();
		ui_float_input(_ui_nodes_hval0, tr("R"), UI_ALIGN_LEFT, 1000.0);
		ui_float_input(_ui_nodes_hval1, tr("G"), UI_ALIGN_LEFT, 1000.0);
		ui_float_input(_ui_nodes_hval2, tr("B"), UI_ALIGN_LEFT, 1000.0);
		ui_float_input(_ui_nodes_hval3, tr("A"), UI_ALIGN_LEFT, 1000.0);
		default_value = f32_array_create_xyzw(_ui_nodes_hval0->f, _ui_nodes_hval1->f, _ui_nodes_hval2->f, _ui_nodes_hval3->f);
	}
	else if (type == 1) {
		ui_row3();
		_ui_nodes_hval0->f = ui_float_input(_ui_nodes_hval0, tr("X"), UI_ALIGN_LEFT, 1000.0);
		_ui_nodes_hval1->f = ui_float_input(_ui_nodes_hval1, tr("Y"), UI_ALIGN_LEFT, 1000.0);
		_ui_nodes_hval2->f = ui_float_input(_ui_nodes_hval2, tr("Z"), UI_ALIGN_LEFT, 1000.0);
		default_value      = f32_array_create_xyz(_ui_nodes_hval0->f, _ui_nodes_hval1->f, _ui_nodes_hval2->f);
	}
	else {
		f32 f         = ui_float_input(_ui_nodes_hval0, tr("Value"), UI_ALIGN_LEFT, 1000.0);
		default_value = f32_array_create_x(f);
	}
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER)) {
		socket->name          = string_copy(name);
		socket->type          = type == 0 ? "RGBA" : type == 1 ? "VECTOR" : "VALUE";
		socket->color         = nodes_material_get_socket_color(socket->type);
		socket->min           = min;
		socket->max           = max;
		socket->default_value = default_value;
		ui_box_hide();
		nodes_material_sync_sockets(node);
		ui_nodes_hwnd->redraws = 2;
	}
}

void ui_viewnodes_on_socket_released_group_edit(void *_) {
	ui_end_input();
	ui_box_show_custom(&ui_viewnodes_on_socket_released_group_edit_box, 400, 250, NULL, true, tr("Socket"));
}

void ui_viewnodes_on_socket_released_group_menu_draw() {
	ui_node_socket_t *socket = _ui_nodes_on_socket_released_socket;
	ui_node_t        *node   = _ui_nodes_on_socket_released_node;
	if (ui_menu_button(tr("Edit"), "", ICON_EDIT)) {
		_ui_nodes_htype->i    = string_equals(socket->type, "RGBA") ? 0 : string_equals(socket->type, "VECTOR") ? 1 : 2;
		_ui_nodes_hname->text = string_copy(socket->name);
		_ui_nodes_hmin->f     = socket->min;
		_ui_nodes_hmax->f     = socket->max;
		if (string_equals(socket->type, "RGBA") || string_equals(socket->type, "VECTOR")) {
			_ui_nodes_hval0->f = socket->default_value->buffer[0];
			_ui_nodes_hval1->f = socket->default_value->buffer[1];
			_ui_nodes_hval2->f = socket->default_value->buffer[2];
			if (string_equals(socket->type, "RGBA")) {
				_ui_nodes_hval3->f = socket->default_value->buffer[3];
			}
		}
		else {
			_ui_nodes_hval0->f = socket->default_value->buffer[0];
		}
		sys_notify_on_next_frame(&ui_viewnodes_on_socket_released_group_edit, NULL);
	}
	if (ui_menu_button(tr("Delete"), "", ICON_DELETE)) {
		i32               i      = 0;
		ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
		// Remove links connected to the socket
		while (i < canvas->links->length) {
			ui_node_link_t *l = canvas->links->buffer[i];
			if ((l->from_id == node->id && l->from_socket == array_index_of(node->outputs, socket)) ||
			    (l->to_id == node->id && l->to_socket == array_index_of(node->inputs, socket))) {
				array_splice(canvas->links, i, 1);
			}
			else {
				i++;
			}
		}
		// Remove socket
		array_remove(node->inputs, socket);
		array_remove(node->outputs, socket);
		nodes_material_sync_sockets(node);
	}
}

void ui_viewnodes_on_socket_released_group_menu(void *_) {
	ui_menu_draw(&ui_viewnodes_on_socket_released_group_menu_draw, -1, -1);
}

void ui_viewnodes_on_canvas_context_menu() {
	ui_node_t *selected = _ui_nodes_on_canvas_released_selected;
	g_ui->_y += 1;
	bool is_protected = selected == NULL || string_equals(selected->type, "OUTPUT_MATERIAL_PBR") || string_equals(selected->type, "GROUP_INPUT") ||
	                    string_equals(selected->type, "GROUP_OUTPUT") || string_equals(selected->type, "brush_output_node");
	g_ui->enabled = !is_protected;
	if (ui_menu_button(tr("Cut"), "ctrl+x", ICON_CUT)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_cut, NULL);
	}
	if (ui_menu_button(tr("Copy"), "ctrl+c", ICON_COPY)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_copy, NULL);
	}
	g_ui->enabled = !string_equals(ui_clipboard, "");
	if (ui_menu_button(tr("Paste"), "ctrl+v", ICON_PASTE)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_paste, NULL);
	}
	g_ui->enabled = !is_protected;
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_delete, NULL);
	}
	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_released_duplicate, NULL);
	}
	if (selected != NULL && string_equals(selected->type, "RGB")) {
		if (ui_menu_button(tr("Add Swatch"), "", ICON_PALETTE)) {
			f32_array_t    *color      = selected->outputs->buffer[0]->default_value;
			swatch_color_t *new_swatch = project_make_swatch(color_from_floats(color->buffer[0], color->buffer[1], color->buffer[2], color->buffer[3]));
			g_context->swatch          = new_swatch;
			any_array_push(g_project->swatches, new_swatch);
			ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 1;
		}
	}
	if (ui_menu_button(tr("Capture Output"), "", ICON_PHOTO)) {
		sys_notify_on_next_frame(&ui_viewnodes_on_canvas_capture_output, NULL);
	}
	if (ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL) {
		ui_menu_separator();
		if (ui_menu_button(tr("2D View"), "", ICON_NONE)) {
			ui_base_show_2d_view(VIEW_2D_TYPE_NODE);
		}
	}
	g_ui->enabled = true;
}

void ui_viewnodes_on_canvas_released() {
	if (g_ui->input_released_r && context_in_nodes() && math_abs(g_ui->input_x - g_ui->input_started_x) < 2 &&
	    math_abs(g_ui->input_y - g_ui->input_started_y) < 2) {

		// Node selection
		ui_nodes_t       *nodes    = ui_nodes_get_nodes();
		ui_node_canvas_t *canvas   = ui_nodes_get_canvas(true);
		ui_node_t        *selected = NULL;
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *node = canvas->nodes->buffer[i];
			if (ui_input_in_rect(g_ui->_window_x + UI_NODE_X(node), g_ui->_window_y + UI_NODE_Y(node), UI_NODE_W(node), UI_NODE_H(canvas, node))) {
				selected = node;
				break;
			}
		}
		if (selected == NULL) {
			nodes->nodes_selected_id = i32_array_create_from_raw((i32[]){}, 0);
		}
		else if (i32_array_index_of(nodes->nodes_selected_id, selected->id) == -1) {
			nodes->nodes_selected_id = i32_array_create_from_raw(
			    (i32[]){
			        selected->id,
			    },
			    1);
		}

		// Node context menu
		if (!ui_nodes_socket_released) {
			_ui_nodes_on_canvas_released_selected = selected;
			ui_menu_draw(&ui_viewnodes_on_canvas_context_menu, -1, -1);
		}
	}

	if (g_ui->input_released) {
		ui_nodes_t       *nodes  = ui_nodes_get_nodes();
		ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *node = canvas->nodes->buffer[i];
			if (ui_input_in_rect(g_ui->_window_x + UI_NODE_X(node), g_ui->_window_y + UI_NODE_Y(node), UI_NODE_W(node), UI_NODE_H(canvas, node))) {
				if (nodes->nodes_selected_id->length > 0 && node->id == nodes->nodes_selected_id->buffer[0]) {
					ui_view2d_hwnd->redraws = 2;
					if (sys_time() - g_context->select_time < 0.2)
						ui_base_show_2d_view(VIEW_2D_TYPE_NODE);
					g_context->select_time = sys_time();
				}
				break;
			}
		}
	}
}

void ui_viewnodes_on_socket_released(i32 socket_id) {
	ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
	ui_node_socket_t *socket = ui_get_socket(canvas->nodes, socket_id);
	ui_node_t        *node   = ui_get_node(canvas->nodes, socket->node_id);
	if (g_ui->input_released_r) {
		if (string_equals(node->type, "GROUP_INPUT") || string_equals(node->type, "GROUP_OUTPUT")) {

			_ui_nodes_on_socket_released_socket = socket;
			_ui_nodes_on_socket_released_node   = node;
			sys_notify_on_next_frame(&ui_viewnodes_on_socket_released_group_menu, NULL);
		}
		else {
			ui_viewnodes_on_canvas_released();
		}
	}
	// Selecting which node socket to preview
	// else {
	// 	let i: i32 = array_index_of(node.outputs, socket);
	// 	if (i > -1) {
	// 		i32_imap_set(g_context.node_preview_socket_map, node.id, i);
	// 		ui_nodes_node_changed = node;
	// 	}
	// }
}

ui_canvas_control_t *ui_viewnodes_on_canvas_control() {
	ui_canvas_control_t *control = ui_nodes_get_canvas_control(ui_nodes_controls_down, true);
	ui_nodes_controls_down       = control->controls_down;
	return control;
}

f32 ui_nodes_get_zoom_delta() {
	return g_config->zoom_direction == ZOOM_DIRECTION_VERTICAL              ? -g_ui->input_dy
	       : g_config->zoom_direction == ZOOM_DIRECTION_VERTICAL_INVERTED   ? -g_ui->input_dy
	       : g_config->zoom_direction == ZOOM_DIRECTION_HORIZONTAL          ? g_ui->input_dx
	       : g_config->zoom_direction == ZOOM_DIRECTION_HORIZONTAL_INVERTED ? g_ui->input_dx
	                                                                        : -(g_ui->input_dy - g_ui->input_dx);
}

ui_canvas_control_t *ui_nodes_get_canvas_control(bool controls_down, bool is_node_view) {
	if (g_config->wrap_mouse && controls_down) {
		if (g_ui->input_x < g_ui->_window_x) {
			g_ui->input_x = g_ui->_window_x + g_ui->_window_w;
			iron_mouse_set_position(math_floor(g_ui->input_x), math_floor(g_ui->input_y));
		}
		else if (g_ui->input_x > g_ui->_window_x + g_ui->_window_w) {
			g_ui->input_x = g_ui->_window_x;
			iron_mouse_set_position(math_floor(g_ui->input_x), math_floor(g_ui->input_y));
		}
		else if (g_ui->input_y < g_ui->_window_y) {
			g_ui->input_y = g_ui->_window_y + g_ui->_window_h;
			iron_mouse_set_position(math_floor(g_ui->input_x), math_floor(g_ui->input_y));
		}
		else if (g_ui->input_y > g_ui->_window_y + g_ui->_window_h) {
			g_ui->input_y = g_ui->_window_y;
			iron_mouse_set_position(math_floor(g_ui->input_x), math_floor(g_ui->input_y));
		}
	}
	if (keymap_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_STARTED) ||
	    keymap_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_STARTED) || g_ui->input_started_r || g_ui->input_wheel_delta != 0.0) {
		controls_down = true;
	}
	else if (!keymap_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_DOWN) &&
	         !keymap_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_DOWN) && !g_ui->input_down_r && g_ui->input_wheel_delta == 0.0) {
		controls_down = false;
	}

	if (!controls_down) {
		ui_canvas_control_t *cc = ALLOC_INIT(ui_canvas_control_t, {.pan_x = 0, .pan_y = 0, .zoom = 0, .controls_down = controls_down});
		return cc;
	}

	bool                 pan        = g_ui->input_down_r || keymap_shortcut(any_map_get(g_keymap, "action_pan"), SHORTCUT_TYPE_DOWN);
	f32                  zoom_delta = keymap_shortcut(any_map_get(g_keymap, "action_zoom"), SHORTCUT_TYPE_DOWN) ? ui_nodes_get_zoom_delta() / 100.0 : 0.0;
	ui_canvas_control_t *control    = ALLOC_INIT(ui_canvas_control_t, {.pan_x = pan ? g_ui->input_dx : 0.0,
	                                                                   .pan_y = pan ? g_ui->input_dy : 0.0,
	                                                                   .zoom  = g_ui->input_wheel_delta != 0.0 ? -g_ui->input_wheel_delta / 10 : zoom_delta,
	                                                                   .controls_down = controls_down});

	if (is_node_view && g_ui->input_x < g_ui->_window_x) {
		control->pan_x = 0.0;
		control->pan_y = 0.0;
		control->zoom  = 0.0;
	}

	if (g_ui->combo_selected_handle != NULL) {
		control->zoom = 0.0;
	}
	if (control->zoom != 0.0) {
		ui_nodes_grid_redraw = true;
	}

	return control;
}

gpu_texture_t *ui_nodes_draw_grid(f32 zoom) {
	i32 ww = g_config->layout->buffer[LAYOUT_SIZE_NODES_W];
	if (!ui_base_show) {
		ww += g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] + ui_toolbar_w(true);
	}
	if (!base_view3d_show) {
		ww += base_view3d_w();
	}
	i32 wh   = sys_h();
	f32 step = ui_nodes_grid_cell_w * zoom;
	i32 mult = 5 * UI_SCALE();
	i32 w    = math_floor(ww + step * mult);
	i32 h    = math_floor(wh + step * mult);
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	gpu_texture_t *grid = gpu_create_render_target(w, h, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(grid, true, g_theme->SEPARATOR_COL);

	i32 sep_col      = g_theme->SEPARATOR_COL;
	i32 line_primary = base_darker(sep_col, 0x00050505);
	if (line_primary < 0xff000000) {
		line_primary = sep_col + 0x00050505;
	}

	i32 line_secondary = base_darker(sep_col, 0x00090909);
	if (line_secondary < 0xff000000) {
		line_secondary = sep_col + 0x00090909;
	}

	draw_set_color(line_primary);
	step = ui_nodes_grid_small_cell_w * zoom;
	for (i32 i = 0; i < math_floor(h / (float)step) + 1; ++i) {
		draw_line(0, i * step, w, i * step, 1.0);
	}
	for (i32 i = 0; i < math_floor(w / (float)step) + 1; ++i) {
		draw_line(i * step, 0, i * step, h, 1.0);
	}
	draw_set_color(line_secondary);
	step = ui_nodes_grid_cell_w * zoom;
	for (i32 i = 0; i < math_floor(h / (float)step) + 1; ++i) {
		draw_line(0, i * step, w, i * step, 1.0);
	}
	for (i32 i = 0; i < math_floor(w / (float)step) + 1; ++i) {
		draw_line(i * step, 0, i * step, h, 1.0);
	}
	draw_end();
	return grid;
}

void ui_nodes_render_color_picker_callback(swatch_color_t *color) {
	_ui_nodes_render_tmp(color->base);
	ui_nodes_hwnd->redraws = 2;
	bool material_live     = g_config->material_live;
	if (material_live) {
		ui_nodes_canvas_changed();
	}
}

void ui_nodes_draw_menubar_search(void *_) {
	ui_nodes_node_search(math_floor(ui_nodes_node_search_x), math_floor(ui_nodes_node_search_y), NULL);
}

void ui_nodes_draw_menubar() {
	ui_node_canvas_t *c     = ui_nodes_get_canvas(true);
	i32               ew    = math_floor(UI_ELEMENT_W() * 0.7);
	i32               top_y = ui_menu_top_y();
	draw_set_color(g_theme->WINDOW_BG_COL);
	draw_filled_rect(0, top_y, ui_nodes_ww, UI_ELEMENT_H() + UI_ELEMENT_OFFSET() * 2);
	draw_set_color(0xffffffff);

	i32 start_y = top_y + UI_ELEMENT_OFFSET();
	g_ui->_x    = 0;
	g_ui->_y    = 2 + start_y;

	// Back button
	if (g_config->touch_ui && !base_view3d_show && !ui_view2d_show) {
		g_ui->_w = math_floor(ew + 3);
		if (ui_icon_button("Back", ICON_ARROW_LEFT, UI_ALIGN_CENTER)) {
			g_ui->input_released = false;
			g_config->workspace  = WORKSPACE_PAINT_3D;
			config_save();
			base_update_workspace();
		}
		g_ui->_x += ew + 3;
		g_ui->_y = 2 + start_y;
	}

	bool full = true;
#ifdef IRON_IOS
	if (config_is_iphone()) {
		full = false;
	}
#endif

	// Editable canvas name
	if (full) {
		g_ui->_w       = ew;
		ui_handle_t *h = ui_handle(__ID__);
		h->text        = string_copy(c->name);
		g_ui->_w       = math_floor(math_min(draw_string_width(g_font, g_ui->font_size, h->text) + 15 * UI_SCALE(), 100 * UI_SCALE()));
		char *new_name = ui_text_input(h, "", UI_ALIGN_LEFT, true, false);
		g_ui->_x += g_ui->_w + 3;
		g_ui->_y = 2 + start_y;
		g_ui->_w = ew;
		if (h->changed) { // Check whether renaming is possible and update group links
			if (ui_nodes_group_stack->length > 0) {
				bool can_rename = true;
				for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
					node_group_t *m = g_project->_->material_groups->buffer[i];
					if (string_equals(m->canvas->name, new_name)) {
						can_rename = false; // Name already used
					}
				}
				if (can_rename) {
					char *old_name                     = c->name;
					c->name                            = string_copy(new_name);
					ui_node_canvas_t_array_t *canvases = any_array_create_from_raw((void *[]){}, 0);
					for (i32 i = 0; i < g_project->_->materials->length; ++i) {
						slot_material_t *m = g_project->_->materials->buffer[i];
						any_array_push(canvases, m->canvas);
					}
					for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
						node_group_t *m = g_project->_->material_groups->buffer[i];
						any_array_push(canvases, m->canvas);
					}
					for (i32 i = 0; i < canvases->length; ++i) {
						ui_node_canvas_t *canvas = canvases->buffer[i];
						for (i32 i = 0; i < canvas->nodes->length; ++i) {
							ui_node_t *n = canvas->nodes->buffer[i];
							if (string_equals(n->type, "GROUP") && string_equals(n->name, old_name)) {
								n->name = string_copy(c->name);
							}
						}
					}
				}
			}
			else {
				c->name = string_copy(new_name);
			}
		}
	}

	// Category buttons
	i32  _BUTTON_COL     = g_theme->BUTTON_COL;
	bool _SHADOWS        = g_theme->SHADOWS;
	g_theme->BUTTON_COL  = g_theme->WINDOW_BG_COL;
	g_theme->SHADOWS     = false;
	string_array_t *cats = ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL ? nodes_material_categories : nodes_brush_categories;
	for (i32 i = 0; i < cats->length; ++i) {

		if (!full && i == 4) { // MATERIAL_NODE_CATEGORY_GROUP
			break;
		}

		if ((ui_menubar_button(tr(cats->buffer[i]))) || (g_ui->is_hovered && ui_nodes_show_menu)) {
			ui_nodes_show_menu     = true;
			ui_nodes_menu_category = i;
			ui_nodes_popup_x       = ui_nodes_wx + g_ui->_x;
			ui_nodes_popup_y       = ui_nodes_wy + g_ui->_y;
			if (g_config->touch_ui) {
				ui_nodes_show_menu_first = true;
				i32 menuw                = math_floor(ew * 2.3);
				ui_nodes_popup_x -= menuw / 2.0;
				ui_nodes_popup_x += g_ui->_w / 2.0;
			}
		}
		g_ui->_x += g_ui->_w + 3;
		g_ui->_y = 2 + start_y;
	}
	if (g_config->touch_ui) {
		i32 _w   = g_ui->_w;
		g_ui->_w = math_floor(36 * UI_SCALE());
		g_ui->_y = 4 * UI_SCALE() + start_y;
		if (ui_menubar_icon_button(ICON_SEARCH)) {
			ui_nodes_node_search(math_floor(g_ui->_window_x + g_ui->_x), math_floor(g_ui->_window_y + g_ui->_y), NULL);
		}
		g_ui->_w = _w;
	}
	else {
		if (ui_menubar_button(tr("Search"))) {
			ui_nodes_node_search_x = g_ui->_window_x + g_ui->_x;
			ui_nodes_node_search_y = g_ui->_window_y + g_ui->_y;
			// Allow for node menu to be closed first
			sys_notify_on_next_frame(&ui_nodes_draw_menubar_search, NULL);
		}
	}
	if (g_ui->is_hovered) {
		ui_tooltip(string_tmp("%s (%s)", tr("Search for nodes"), any_map_get(g_keymap, "node_search")));
	}

	g_ui->_x += g_ui->_w + 3;
	g_ui->_y            = 2 + start_y;
	g_theme->BUTTON_COL = _BUTTON_COL;
	g_theme->SHADOWS    = _SHADOWS;

	// Close node group
	g_ui->_w = ew;
	if (ui_nodes_group_stack->length > 0 && ui_icon_button(tr("Back"), ICON_ARROW_LEFT, UI_ALIGN_CENTER)) {
		array_pop(ui_nodes_group_stack);
	}
}

void ui_nodes_update(void *_) {
	if (ui_nodes_show && base_ui_enabled) {

		ui_nodes_wx = math_floor(sys_w()) + ui_toolbar_w(true);
		ui_nodes_wy = ui_header_h * 2;

		if (ui_view2d_show && base_view3d_show) {
			ui_nodes_wy += sys_h() - g_config->layout->buffer[LAYOUT_SIZE_NODES_H];
		}

		i32 ww = g_config->layout->buffer[LAYOUT_SIZE_NODES_W];
		if (!ui_base_show) {
			ww += g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] + ui_toolbar_w(true);
			ui_nodes_wx -= ui_toolbar_w(true);
			ui_nodes_wy = 0;
		}
		if (!base_view3d_show) {
			ww += base_view3d_w();
		}

		if (!base_view3d_show && ui_view2d_show) {
			ui_nodes_wx = base_view3d_w();
			ui_nodes_wy = 0;
			ui_nodes_ww = g_config->layout->buffer[LAYOUT_SIZE_NODES_W];
			ui_nodes_wh = sys_h();
			if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1) {
				ui_nodes_wh += ui_header_h * 2;
			}
		}

		if (!base_view3d_show) {
			ui_nodes_wh -= ui_header_h * 4;
		}

		i32  mx      = mouse_x;
		i32  my      = mouse_y;
		bool enabled = true;
		if (mx < ui_nodes_wx || mx > ui_nodes_wx + ww || my < ui_nodes_wy) {
			enabled = false;
		}
		if (g_ui->is_typing || !g_ui->input_enabled) {
			enabled = false;
		}

		if (enabled) {
			ui_nodes_t *nodes = ui_nodes_get_nodes();
			if (nodes->nodes_selected_id->length > 0 && g_ui->is_key_pressed) {
				if (g_ui->key_code == KEY_CODE_LEFT) {
					for (i32 i = 0; i < nodes->nodes_selected_id->length; ++i) {
						i32                n      = nodes->nodes_selected_id->buffer[i];
						ui_node_t_array_t *_nodes = ui_nodes_get_canvas(true)->nodes;
						ui_get_node(_nodes, n)->x -= 1;
					}
				}
				else if (g_ui->key_code == KEY_CODE_RIGHT) {
					for (i32 i = 0; i < nodes->nodes_selected_id->length; ++i) {
						i32                n      = nodes->nodes_selected_id->buffer[i];
						ui_node_t_array_t *_nodes = ui_nodes_get_canvas(true)->nodes;
						ui_get_node(_nodes, n)->x += 1;
					}
				}
				if (g_ui->key_code == KEY_CODE_UP) {
					for (i32 i = 0; i < nodes->nodes_selected_id->length; ++i) {
						i32                n      = nodes->nodes_selected_id->buffer[i];
						ui_node_t_array_t *_nodes = ui_nodes_get_canvas(true)->nodes;
						ui_get_node(_nodes, n)->y -= 1;
					}
				}
				else if (g_ui->key_code == KEY_CODE_DOWN) {
					for (i32 i = 0; i < nodes->nodes_selected_id->length; ++i) {
						i32                n      = nodes->nodes_selected_id->buffer[i];
						ui_node_t_array_t *_nodes = ui_nodes_get_canvas(true)->nodes;
						ui_get_node(_nodes, n)->y += 1;
					}
				}
			}

			// Node search popup
			if (keymap_shortcut(any_map_get(g_keymap, "node_search"), SHORTCUT_TYPE_STARTED)) {
				ui_nodes_node_search(-1, -1, NULL);
			}
			if (ui_nodes_node_search_spawn != NULL) {
				g_ui->input_x              = mouse_x; // Fix input_dx after popup removal
				g_ui->input_y              = mouse_y;
				ui_nodes_node_search_spawn = NULL;
			}

			if (keymap_shortcut(any_map_get(g_keymap, "view_reset"), SHORTCUT_TYPE_STARTED)) {
				nodes->pan_x = 0.0;
				nodes->pan_y = 0.0;
				nodes->zoom  = 1.0;
			}

			if (keymap_shortcut(any_map_get(g_keymap, "node_overview"), SHORTCUT_TYPE_STARTED)) {
				nodes->zoom = nodes->zoom == 1.0 ? 0.2 : 1.0;
				nodes->uiw  = ui_nodes_ww;
				nodes->uih  = ui_nodes_wh;
			}
		}
	}

	// Render
	ui_nodes_recompile();

	ui_nodes_t *ui_nodes = ui_nodes_get_nodes();

	// Remove dragged link when mouse is released out of the node viewport
	ui_node_canvas_t *c = ui_nodes_get_canvas(true);
	if (ui_nodes_release_link && ui_nodes->link_drag_id != -1) {
		array_remove(c->links, ui_get_link(c->links, ui_nodes->link_drag_id));
		ui_nodes->link_drag_id = -1;
	}
	ui_nodes_release_link = g_ui->input_released;

	if (!ui_nodes_show) {
		return;
	}

	g_ui->input_enabled = base_ui_enabled;

	if (ui_nodes_last_zoom != ui_nodes->zoom) {
		ui_nodes_last_zoom   = ui_nodes->zoom;
		ui_nodes_grid_redraw = true;
	}

	if (ui_nodes_grid_redraw) {
		if (ui_nodes_grid != NULL) {
			gpu_delete_texture(ui_nodes_grid);
		}
		ui_nodes_grid        = ui_nodes_draw_grid(ui_nodes->zoom);
		ui_nodes_grid_redraw = false;
	}

	// Selected node preview
	if (ui_nodes->nodes_selected_id->length > 0 && ui_nodes->nodes_selected_id->buffer[0] != ui_nodes_last_node_selected_id) {
		ui_nodes_last_node_selected_id = ui_nodes->nodes_selected_id->buffer[0];
		ui_node_t *sel                 = ui_get_node(c->nodes, ui_nodes->nodes_selected_id->buffer[0]);
		ui_nodes_make_node_preview(sel);

		// If script node is present, update selected node preview on every frame
		bool              has_script_node = false;
		ui_node_canvas_t *canvas          = g_context->material->canvas;
		for (i32 i = 0; i < canvas->nodes->length; ++i) {
			ui_node_t *n = canvas->nodes->buffer[i];
			if (string_equals(n->type, "SCRIPT_CPU")) {
				has_script_node = true;
				break;
			}
		}
		if (has_script_node) {
			ui_nodes_last_node_selected_id = -1;
			ui_view2d_hwnd->redraws        = 2;
			iron_delay_idle_sleep();
		}
	}
	else if (ui_nodes->nodes_selected_id->length == 0) {
		ui_nodes_last_node_selected_id = -1;
	}

	// Update node previews
	if (ui_nodes_node_changed != NULL && !g_ui->input_down) {
		ui_node_t_array_t *linked_nodes = any_array_create_from_raw((void *[]){}, 0);
		ui_nodes_get_linked_nodes(linked_nodes, ui_nodes_node_changed, c);

		ui_node_t *sel = ui_nodes->nodes_selected_id->length > 0 ? ui_get_node(c->nodes, ui_nodes->nodes_selected_id->buffer[0]) : NULL;
		if (sel != NULL && array_index_of(linked_nodes, sel) == -1) {
			any_array_push(linked_nodes, sel);
		}

		for (i32 i = 0; i < linked_nodes->length; ++i) {
			ui_nodes_make_node_preview(linked_nodes->buffer[i]);
		}
		ui_nodes_node_changed = NULL;
	}

	// Start with UI
	ui_begin(g_ui);

	// Make window
	ui_nodes_ww = g_config->layout->buffer[LAYOUT_SIZE_NODES_W];
	ui_nodes_wx = math_floor(sys_w()) + ui_toolbar_w(true);
	ui_nodes_wy = 0;

	if (!ui_base_show) {
		ui_nodes_ww += g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] + ui_toolbar_w(true);
		ui_nodes_wx -= ui_toolbar_w(true);
	}
	if (!base_view3d_show) {
		ui_nodes_ww += base_view3d_w();
	}

	i32 ew      = math_floor(UI_ELEMENT_W() * 0.7);
	ui_nodes_wh = sys_h();
	if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1) {
		ui_nodes_wh += ui_header_h * 2;
	}

	if (ui_view2d_show) {
		ui_nodes_wh = g_config->layout->buffer[LAYOUT_SIZE_NODES_H];
		ui_nodes_wy = sys_h() - g_config->layout->buffer[LAYOUT_SIZE_NODES_H] + ui_header_h;
		if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1) {
			ui_nodes_wy += ui_header_h;
		}
		else {
			ui_nodes_wy -= ui_header_h;
		}
		if (!ui_base_show) {
			ui_nodes_wy -= ui_header_h * 2;
		}
	}

	if (!base_view3d_show && ui_view2d_show) {
		ui_nodes_wx = base_view3d_w();
		ui_nodes_wy = 0;
		ui_nodes_ww = g_config->layout->buffer[LAYOUT_SIZE_NODES_W];
		ui_nodes_wh = sys_h();
		if (g_config->layout->buffer[LAYOUT_SIZE_HEADER] == 1) {
			ui_nodes_wh += ui_header_h * 2;
		}
	}

	if (!base_view3d_show) {
		ui_nodes_wh -= ui_header_h * 4;
	}

	if (ui_window(ui_nodes_hwnd, ui_nodes_wx, ui_nodes_wy, ui_nodes_ww, ui_nodes_wh, false)) {

		if (!g_config->touch_ui) {
			bool expand = !base_view3d_show && g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] == 0;
			ui_tab(ui_nodes_htab, expand ? string_tmp("%s          ", tr("Nodes")) : tr("Nodes"), false, -1, !base_view3d_show);

			// Additional tabs
			if (ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL) {
				if (ui_nodes_tabs == NULL) {
					ui_nodes_tabs = any_array_create_from_raw((void *[]){}, 0);
				}

				for (i32 i = 0; i < ui_nodes_tabs->length; ++i) {
					ui_tab(ui_nodes_htab, ui_nodes_tabs->buffer[i]->canvas->name, false, -1, false);
					if (ui_tab(ui_nodes_htab, tr("x"), false, -1, false)) {
						array_splice(ui_nodes_tabs, i, 1);
						ui_nodes_htab->i = 0;
					}
				}

				if (ui_tab(ui_nodes_htab, tr("+"), false, -1, false)) {
					any_array_push(ui_nodes_tabs, g_context->material);
				}
			}
		}

		// Grid
		draw_set_color(0xffffffff);
		f32                  step            = ui_nodes_grid_cell_w * ui_nodes->zoom;
		ui_canvas_control_t *pending_control = ui_nodes_on_canvas_control();
		f32                  x               = math_fmod(UI_NODES_PAN_X() + pending_control->pan_x, step) - step;
		f32                  y               = math_fmod(UI_NODES_PAN_Y() + pending_control->pan_y, step) - step;
		draw_image(ui_nodes_grid, x, y);

		// Undo
		if (g_ui->input_started || g_ui->is_key_pressed) {
			ui_nodes_last_canvas = util_clone_canvas(ui_nodes_get_canvas(true));
		}

		// Nodes
		bool _input_enabled        = g_ui->input_enabled;
		i32  header_h              = UI_ELEMENT_H() * 2 + UI_ELEMENT_OFFSET() * 2;
		bool header_hover          = g_ui->input_y < g_ui->_window_y + header_h;
		g_ui->input_enabled        = _input_enabled && !ui_nodes_show_menu && !header_hover;
		g_ui->window_border_right  = g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W];
		g_ui->window_border_top    = ui_header_h * 2;
		g_ui->window_border_bottom = g_config->layout->buffer[LAYOUT_SIZE_STATUS_H];

		ui_node_canvas(ui_nodes, c);
		g_ui->input_enabled = _input_enabled;

		if (ui_nodes->color_picker_callback != NULL) {
			g_context->color_picker_previous_tool = g_context->tool;
			context_select_tool(TOOL_TYPE_PICKER);
			_ui_nodes_render_tmp             = ui_nodes->color_picker_callback;
			g_context->color_picker_callback = &ui_nodes_render_color_picker_callback;
			ui_nodes->color_picker_callback  = NULL;
		}

		// Remove nodes with unknown id for this canvas type
		if (ui_is_paste) {
			node_list_t_array_t *node_list = ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL ? nodes_material_list : nodes_brush_list;

			i32 i = 0;
			while (i++ < c->nodes->length) {
				ui_node_t *canvas_node = c->nodes->buffer[i - 1];
				if (string_array_index_of(ui_nodes_exclude_remove, canvas_node->type) >= 0) {
					continue;
				}
				bool found = false;
				for (i32 i = 0; i < node_list->length; ++i) {
					ui_node_t_array_t *list = node_list->buffer[i];
					for (i32 i = 0; i < list->length; ++i) {
						ui_node_t *list_node = list->buffer[i];
						if (string_equals(canvas_node->type, list_node->type)) {
							found = true;
							break;
						}
					}
					if (found) {
						break;
					}
				}
				if (string_equals(canvas_node->type, "GROUP") && !ui_nodes_can_place_group(canvas_node->name)) {
					found = false;
				}
				if (!found) {
					ui_remove_node(canvas_node, c);
					i32_array_remove(ui_nodes->nodes_selected_id, canvas_node->id);
					i--;
				}
			}
		}

		if (ui_nodes_is_node_menu_op) {
			ui_is_copy           = false;
			ui_is_cut            = false;
			ui_is_paste          = false;
			g_ui->is_delete_down = false;
		}

		// Recompile material on change
		if (g_ui->changed) {
			ui_nodes_recompile_mat = (g_ui->input_dx != 0 || g_ui->input_dy != 0 || !ui_nodes_uichanged_last) && g_config->material_live; // Instant preview
		}
		else if (ui_nodes_uichanged_last) {
			ui_nodes_canvas_changed();
			ui_nodes_push_undo(ui_nodes_last_canvas);
		}
		ui_nodes_uichanged_last = g_ui->changed;

		// Node previews
		if (g_context->selected_node_preview && ui_nodes->nodes_selected_id->length > 0) {
			ui_node_t     *sel = ui_get_node(c->nodes, ui_nodes->nodes_selected_id->buffer[0]);
			gpu_texture_t *img = ui_nodes_get_node_preview_image(sel);
			if (img != NULL && !(sel->flags & UI_NODE_FLAG_PREVIEW)) {
				f32 tw = 128 * UI_SCALE();
				f32 th = tw * (img->height / (float)img->width);
				f32 tx = ui_nodes_ww - tw - 8 * UI_SCALE();
				f32 ty = ui_nodes_wh - th - 8 * UI_SCALE();
				if (img == any_imap_get(g_context->node_preview_map, sel->id)) {
					ui_draw_shadow(tx, ty, tw, th);
				}
				bool single_channel = string_equals(sel->type, "LAYER_MASK");
				if (single_channel) {
					draw_set_pipeline(ui_view2d_pipe);
					gpu_set_int(ui_view2d_channel_loc, 1);
				}
				draw_set_color(0xffffffff);
				draw_scaled_image(img, tx, ty, tw, th);
				if (single_channel) {
					draw_set_pipeline(NULL);
				}
			}
		}

		ui_nodes_draw_menubar();

		g_ui->window_border_right  = 0;
		g_ui->window_border_top    = 0;
		g_ui->window_border_bottom = 0;
	}

	ui_end();

	if (ui_nodes_show_menu) {
		node_list_t_array_t *list     = ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL ? nodes_material_list : nodes_brush_list;
		ui_node_t_array_t   *category = list->buffer[ui_nodes_menu_category];
		bool                 is_group_category =
		    ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL && string_equals(nodes_material_categories->buffer[ui_nodes_menu_category], "Group");
		i32 py    = ui_nodes_popup_y;
		i32 menuw = math_floor(ew * 2.3);
		draw_begin(NULL, false, 0);
		ui_begin_region(g_ui, math_floor(ui_nodes_popup_x), math_floor(py), menuw);
		i32 _FILL_BUTTON_BG     = g_theme->FILL_BUTTON_BG;
		g_theme->FILL_BUTTON_BG = false;
		i32 _ELEMENT_OFFSET     = g_theme->ELEMENT_OFFSET;
		g_theme->ELEMENT_OFFSET = 0;
		i32 _ELEMENT_H          = g_theme->ELEMENT_H;
		g_theme->ELEMENT_H      = g_config->touch_ui ? (28 + 2) : 28;
		ui_menu_h               = category->length * UI_ELEMENT_H();
		if (is_group_category) {
			ui_menu_h += g_project->_->material_groups->length * UI_ELEMENT_H();
		}
		ui_menu_begin();
		for (i32 i = 0; i < category->length; ++i) {
			ui_node_t *n = category->buffer[i];
			if (ui_menu_button(tr(n->name), "", ICON_NONE)) {
				ui_nodes_push_undo(NULL);
				ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
				ui_nodes_t       *nodes  = ui_nodes_get_nodes();
				ui_node_t        *node   = ui_nodes_make_node(n, nodes, canvas);
				any_array_push(canvas->nodes, node);
				nodes->nodes_selected_id = i32_array_create_from_raw(
				    (i32[]){
				        node->id,
				    },
				    1);
				nodes->nodes_drag = true;
			}
			// Next column
			if (g_ui->_y - ui_nodes_wy + UI_ELEMENT_H() / 2.0 > ui_nodes_wh) {
				g_ui->_x += menuw;
				g_ui->_y = py;
			}
		}
		if (is_group_category) {
			for (i32 i = 0; i < g_project->_->material_groups->length; ++i) {
				node_group_t *g  = g_project->_->material_groups->buffer[i];
				g_ui->enabled    = ui_nodes_can_place_group(g->canvas->name);
				f32_array_t *row = f32_array_create_from_raw_tmp(
				    (f32[]){
				        5 / 6.0,
				        1 / 6.0,
				    },
				    2);
				ui_row(row);
				if (ui_button(string_tmp("%s%s", config_button_spacing, g->canvas->name), UI_ALIGN_LEFT, "")) {
					ui_nodes_push_undo(NULL);
					ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
					ui_nodes_t       *nodes  = ui_nodes_get_nodes();
					ui_node_t        *node   = ui_nodes_make_group_node(g->canvas, nodes, canvas);
					any_array_push(canvas->nodes, node);
					nodes->nodes_selected_id = i32_array_create_from_raw(
					    (i32[]){
					        node->id,
					    },
					    1);
					nodes->nodes_drag = true;
				}
				g_ui->enabled = !project_is_material_group_in_use(g);
				if (ui_button("x", UI_ALIGN_CENTER, "")) {
					history_delete_material_group(g);
					array_remove(g_project->_->material_groups, g);
				}
				g_ui->enabled = true;
			}
		}
		ui_nodes_hide_menu = g_ui->combo_selected_handle == NULL && !ui_nodes_show_menu_first &&
		                     (g_ui->changed || g_ui->input_released || g_ui->input_released_r || g_ui->is_escape_down);
		ui_nodes_show_menu_first = false;
		g_theme->FILL_BUTTON_BG  = _FILL_BUTTON_BG;
		g_theme->ELEMENT_OFFSET  = _ELEMENT_OFFSET;
		g_theme->ELEMENT_H       = _ELEMENT_H;
		ui_menu_end();
		ui_end_region();
		draw_end();
	}

	if (ui_nodes_hide_menu) {
		ui_nodes_show_menu       = false;
		ui_nodes_show_menu_first = true;
	}

	g_ui->input_enabled = true;
}

void ui_nodes_accept_asset_drop(i32 index) {
	ui_nodes_push_undo(NULL);
	node_group_t *g = ui_nodes_group_stack->length > 0 ? ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1] : NULL;
	ui_node_t    *n = ui_nodes_canvas_type == CANVAS_TYPE_MATERIAL ? nodes_material_create_node("TEX_IMAGE", g) : nodes_brush_create_node("TEX_IMAGE");
	n->buttons->buffer[0]->default_value->buffer[0] = index;
	ui_nodes_get_nodes()->nodes_selected_id         = i32_array_create_from_raw(
        (i32[]){
            n->id,
        },
        1);
}

void ui_nodes_accept_layer_drop(i32 index) {
	ui_nodes_push_undo(NULL);
	if (slot_layer_is_group(g_project->_->layers->buffer[index])) {
		return;
	}
	node_group_t *g                                 = ui_nodes_group_stack->length > 0 ? ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1] : NULL;
	ui_node_t    *n                                 = nodes_material_create_node(slot_layer_is_mask(g_context->layer) ? "LAYER_MASK" : "LAYER", g);
	n->buttons->buffer[0]->default_value->buffer[0] = index;
	ui_nodes_get_nodes()->nodes_selected_id         = i32_array_create_from_raw(
        (i32[]){
            n->id,
        },
        1);
}

void ui_nodes_accept_material_drop(i32 index) {
	ui_nodes_push_undo(NULL);
	node_group_t *g                                 = ui_nodes_group_stack->length > 0 ? ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1] : NULL;
	ui_node_t    *n                                 = nodes_material_create_node("MATERIAL", g);
	n->buttons->buffer[0]->default_value->buffer[0] = index;
	ui_nodes_get_nodes()->nodes_selected_id         = i32_array_create_from_raw(
        (i32[]){
            n->id,
        },
        1);
}

void ui_nodes_accept_swatch_drop(swatch_color_t *swatch) {
	ui_nodes_push_undo(NULL);
	node_group_t *g                         = ui_nodes_group_stack->length > 0 ? ui_nodes_group_stack->buffer[ui_nodes_group_stack->length - 1] : NULL;
	ui_node_t    *n                         = nodes_material_create_node("RGB", g);
	n->outputs->buffer[0]->default_value    = f32_array_create_xyzw(color_get_rb(swatch->base) / 255.0, color_get_gb(swatch->base) / 255.0,
	                                                                color_get_bb(swatch->base) / 255.0, color_get_ab(swatch->base) / 255.0);
	ui_nodes_get_nodes()->nodes_selected_id = i32_array_create_from_raw(
	    (i32[]){
	        n->id,
	    },
	    1);
}

void ui_viewnodes_init() {
	ui_nodes_preview_texture = ui_nodes_get_node_preview_image;

	ui_nodes_on_link_drag = ui_viewnodes_on_link_drag;

	ui_nodes_on_node_remove = ui_viewnodes_on_node_remove;

	ui_nodes_on_node_changed = ui_viewnodes_on_node_changed;

	ui_nodes_on_socket_released = ui_viewnodes_on_socket_released;

	ui_nodes_on_canvas_released = ui_viewnodes_on_canvas_released;

	ui_nodes_on_canvas_control = ui_viewnodes_on_canvas_control;

	ui_nodes_grid_snap = g_config->grid_snap;
	nodes_material_init();
}
