
#include "../global.h"

i32 ui_toolbar_last_tool = 0;
i32 _ui_toolbar_i;
i32 _ui_toolbar_y      = 0; // Top of the view the toolbar is drawn in
i32 _ui_toolbar_bottom = 0;

void ui_toolbar_init() {}

// From TOOL_TYPE_EMPTY to TOOL_TYPE_CURVE
static i32 ui_toolbar_object_tool_icons[] = {ICON_EMPTY,   ICON_PLANE, ICON_CUBE,   ICON_UVSPHERE, ICON_CONE, ICON_CYLINDER,
                                             ICON_CAPSULE, ICON_TORUS, ICON_TEXT3D, ICON_PATH,     ICON_CURVE};

bool ui_toolbar_tool_visible(i32 tool) {
	if (tool == TOOL_TYPE_BAKE) {
		return false; // Used by the Bake Texture node only
	}
	if (tool >= TOOL_TYPE_PLUGIN) {
		return g_config->tools == TOOLS_PAINT;
	}
	if (g_config->tools == TOOLS_SCULPT) {
		// Brush is replaced by the sculpt mode icons
		return tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_PARTICLE && tool != TOOL_TYPE_COLORID && tool != TOOL_TYPE_PICKER && tool != TOOL_TYPE_MATERIAL &&
		       !util_tool_is_object_tool(tool);
	}
	if (g_config->tools == TOOLS_OBJECT) {
		return tool == TOOL_TYPE_CURSOR || util_tool_is_object_tool(tool);
	}
	return !util_tool_is_object_tool(tool);
}

static i32 ui_toolbar_item_count() {
	i32 count = 0;
	for (i32 i = 0; i < ui_toolbar_tool_names->length; ++i) {
		if (ui_toolbar_tool_visible(i)) {
			count++;
		}
	}
	if (g_config->tools == TOOLS_SCULPT) {
		count += SCULPT_TYPE_PLATEAU + 1;
	}
	return count;
}

void ui_toolbar_draw_tool_select_tool(void *_) {
	context_select_tool(_ui_toolbar_i);
}

void ui_toolbar_draw_tool_select_sculpt(void *_) {
	g_context->brush_sculpt = _ui_toolbar_i;
	context_select_tool(TOOL_TYPE_BRUSH);
}

void ui_toolbar_tool_properties_menu_draw() {
	g_ui->changed = false;
	ui_header_draw_tool_properties();
	if (g_ui->changed || g_ui->is_typing) {
		ui_menu_keep_open = true;
	}
	if (base_view3d_show && ui_button(tr("Pin to Header"), UI_ALIGN_LEFT, "")) {
		g_config->header_pinned = true;
	}
}

void ui_toolbar_tool_properties_menu() {
	i32 y = _ui_toolbar_y + g_ui->_y - 2 * UI_SCALE();
	if (!base_view3d_show) {
		y += ui_toolbar_w(false);
	}

#ifdef IRON_IOS
	if (config_is_iphone() && base_view3d_show) {
		y += ui_toolbar_w(false);
	}
#endif

	i32 x = g_ui->_window_x - ui_toolbar_x();
	ui_menu_draw(&ui_toolbar_tool_properties_menu_draw, x + g_ui->_x + g_ui->_w + 6 * UI_SCALE(), y);
}

static bool _ui_toolbar_right_click = false;

void ui_toolbar_update() {
	if (mouse_started("right")) {
		_ui_toolbar_right_click = context_is_floating_toolbar() && context_in_paint_area() && !g_config->touch_ui && !base_player && !ui_menu_show &&
		                          !ui_box_show && !g_ui->is_typing && g_context->tool != TOOL_TYPE_PATH && g_context->tool != TOOL_TYPE_CURVE;
	}
	else if (mouse_down("right") && mouse_moved) {
		_ui_toolbar_right_click = false;
	}
	else if (mouse_released("right")) {
		if (_ui_toolbar_right_click && !mouse_moved && context_in_paint_area()) {
			ui_menu_draw(&ui_toolbar_tool_properties_menu_draw, -1, -1);
		}
		_ui_toolbar_right_click = false;
	}
}

void ui_toolbar_draw_highlight(u32 col) {
	i32 size = ui_toolbar_w(false) - 4;
	draw_set_color(col);
	ui_draw_rect(true, true, g_ui->_x + -1, g_ui->_y + 2, size + 2, size + 2);
}

static bool ui_toolbar_input_visible() {
	if (context_is_floating_toolbar()) {
		return g_ui->input_y < _ui_toolbar_bottom;
	}
	return true;
}

static ui_state_t ui_toolbar_draw_icon(bool selected, bool visible, i32 tile_i, gpu_texture_t *img, i32 icon_accent) {
	i32 size = ui_toolbar_w(false) - 4;
	if (selected) {
		ui_toolbar_draw_highlight(g_theme->HIGHLIGHT_COL);
	}
	else if (visible && ui_input_in_rect(g_ui->_window_x + g_ui->_x - 1, g_ui->_window_y + g_ui->_y + 2, size + 2, size + 2)) {
		ui_toolbar_draw_highlight(g_theme->HOVER_COL);
	}

	rect_t *rect = resource_tile50(img, tile_i);
	i32     _y   = g_ui->_y;
	g_ui->_x -= 2;
	ui_state_t image_state = ui_sub_image(img, icon_accent, -1.0, rect->x, rect->y, rect->w, rect->h);
	g_ui->_x += 2;

	// Clicking the selected tool again opens its properties
	if (selected && context_is_floating_toolbar()) {
		float aw = 4 * UI_SCALE();
		float ah = 8 * UI_SCALE();
		float ax = g_ui->_x + size - aw;
		float ay = _y + 4 + size - 2 * UI_SCALE() - ah;
		draw_set_color(g_theme->LABEL_COL);
		draw_filled_triangle(ax, ay, ax, ay + ah, ax + aw, ay + ah / 2.0);
	}
	return image_state;
}

static void ui_toolbar_handle_release(ui_state_t image_state, bool visible, i32 id) {
	if (image_state == UI_STATE_RELEASED && context_is_floating_toolbar() && visible) {
		if (ui_toolbar_last_tool == id) {
			ui_toolbar_tool_properties_menu();
		}
		ui_toolbar_last_tool = id;
	}
}

void ui_toolbar_draw_sculpt_tool(sculpt_type_t mode, gpu_texture_t *img, i32 icon_accent) {
	g_ui->_x += 2;
	bool       visible     = ui_toolbar_input_visible();
	bool       selected    = g_context->tool == TOOL_TYPE_BRUSH && g_context->brush_sculpt == mode;
	ui_state_t image_state = ui_toolbar_draw_icon(selected, visible, ICON_SCULPT_DRAW + mode, img, icon_accent);

	if (image_state == UI_STATE_STARTED && visible) {
		_ui_toolbar_i = mode;
		sys_notify_on_next_frame(&ui_toolbar_draw_tool_select_sculpt, NULL);
	}
	// Sculpt modes get their own negative ids, so switching modes does not open the properties menu
	ui_toolbar_handle_release(image_state, visible, -1 - mode);

	if (g_ui->is_hovered) {
		string_array_t *names = any_array_create_from_raw_tmp(
		    (void *[]){
		        tr("Draw"),
		        tr("Grab"),
		        tr("Inflate"),
		        tr("Flatten"),
		        tr("Clay"),
		        tr("Pinch"),
		        tr("Crease"),
		        tr("Cloth"),
		        tr("Twist"),
		        tr("Stretch"),
		        tr("Trim"),
		        tr("Plateau"),
		    },
		    12);
		ui_tooltip(names->buffer[mode]);
	}
	g_ui->_x -= 2;
	g_ui->_y += 2;
}

void ui_toolbar_draw_tool(i32 tool, gpu_texture_t *img, i32 icon_accent) {
	g_ui->_x += 2;

	bool visible = ui_toolbar_input_visible();
	i32  tile_y  = math_floor(tool / 12.0);
	i32  tile_x  = tile_y % 2 == 0 ? tool % 12 : (11 - (tool % 12));
	i32  _y      = g_ui->_y;
	i32  tile_i  = util_tool_is_object_tool(tool) ? ui_toolbar_object_tool_icons[tool - TOOL_TYPE_EMPTY] : tile_y * 12 + tile_x;
	if (tool >= TOOL_TYPE_PLUGIN) {
		tile_i = plugin_tool_get(tool)->icon;
	}

	ui_state_t image_state = ui_toolbar_draw_icon(g_context->tool == tool, visible, tile_i, img, icon_accent);

	if (image_state == UI_STATE_STARTED && visible) {
		_ui_toolbar_i = tool;
		sys_notify_on_next_frame(&ui_toolbar_draw_tool_select_tool, NULL);
	}
	ui_toolbar_handle_release(image_state, visible, tool);

	if (tool == TOOL_TYPE_COLORID && g_context->colorid_picked) {
		render_target_t *rt = any_map_get(render_path_render_targets, "texpaint_colorid");
		draw_scaled_sub_image(rt->_image, 0, 0, 1, 1, 0, _y + 1.5 * UI_SCALE(), 5 * UI_SCALE(), 34 * UI_SCALE());
	}

	if (g_ui->is_hovered) {
		char *tooltip  = tr(ui_toolbar_tool_names->buffer[tool]);
		char *key_name = string_tmp("tool_%s", ui_toolbar_tool_names->buffer[tool]);
		for (char *c = key_name; *c != '\0'; ++c) {
			*c = tolower(*c);
		}
		char *key = any_map_get(g_keymap, key_name);
		// Object tools have no shortcuts, the text one shares its name with the paint tool
		if (key != NULL && !string_equals(key, "") && !util_tool_is_object_tool(tool)) {
			tooltip = string_tmp("%s (%s)", tooltip, key);
		}
		char *extra = ui_toolbar_tooltip_extras->buffer[tool];
		if (!string_equals(extra, "")) {
			tooltip = string_tmp("%s - %s", tooltip, vtr(extra, g_keymap));
		}
		ui_tooltip(tooltip);
	}
	g_ui->_x -= 2;
	g_ui->_y += 2;
}

i32 ui_toolbar_w(bool screen_size_request) {
	if (screen_size_request && context_is_floating_toolbar()) {
		return 0;
	}
	if (!base_view3d_show && !ui_view2d_show) {
		return 0;
	}

	i32 w = ui_toolbar_default_w;
	if (g_config->touch_ui) {
		w = ui_toolbar_default_w + 6;
	}
	w = math_floor(w * g_config->window_scale);
	return w;
}

i32 ui_toolbar_x() {
	return 5 * UI_SCALE();
}

void ui_toolbar_draw_show_3d_view() {
	if (g_config->touch_ui) {
		i32 toolbar_w = ui_toolbar_default_w * UI_SCALE() + 14 * UI_SCALE();
		i32 y         = ui_header_h + 8 * UI_SCALE();

		if ((ui_view2d_show || ui_nodes_show) && !g_config->touch_ui) {
			y += toolbar_w;
		}

		if (ui_window(ui_toolbar_handle, ui_toolbar_x(), y, toolbar_w, toolbar_w, false)) {
			i32 _ELEMENT_H      = g_theme->ELEMENT_H;
			i32 _BUTTON_H       = g_theme->BUTTON_H;
			i32 _BUTTON_COL     = g_theme->BUTTON_COL;
			i32 _fontOffsetY    = g_ui->font_offset_y;
			g_theme->ELEMENT_H  = math_floor(g_theme->ELEMENT_H * 1.5);
			g_theme->BUTTON_H   = g_theme->ELEMENT_H;
			g_theme->BUTTON_COL = g_theme->WINDOW_BG_COL;
			i32 font_height     = draw_font_height(g_font, g_ui->font_size);
			g_ui->font_offset_y = (UI_ELEMENT_H() - font_height) / 2.0;
			i32 _w              = g_ui->_w;
			g_ui->_w            = toolbar_w;
			if (ui_icon_button("", ICON_CUBE, UI_ALIGN_CENTER)) {
				ui_base_show_3d_view();
			}
			g_ui->_w            = _w;
			g_theme->ELEMENT_H  = _ELEMENT_H;
			g_theme->BUTTON_H   = _BUTTON_H;
			g_theme->BUTTON_COL = _BUTTON_COL;
			g_ui->font_offset_y = _fontOffsetY;
		}
	}
}

void ui_toolbar_header_menu_draw() {
	if (ui_menu_button(tr("Toggle Header"), "", ICON_PROPERTIES)) {
		g_config->header_pinned = false;
	}
}

static void ui_toolbar_draw_window(ui_window_t *handle, i32 x, i32 y, i32 h) {
	if (ui_window(handle, x, y, ui_toolbar_w(false), h, false)) {
		g_ui->image_scroll_align   = false;
		gpu_texture_t *img         = resource_get("icons.k");
		u32            col         = g_theme->WINDOW_BG_COL;
		bool           light       = col > 0xff666666;
		i32            icon_accent = light ? 0xff666666 : -1;

		// Properties icon
		if (!context_is_floating_toolbar()) {
			g_ui->_y -= 4 * UI_SCALE();
			rect_t *rect = resource_tile50(img, ICON_PROPERTIES);
			if (ui_sub_image(img, light ? 0xff666666 : g_theme->BUTTON_COL, -1.0, rect->x, rect->y, rect->w, rect->h) == UI_STATE_RELEASED) {
				ui_menu_draw(&ui_toolbar_header_menu_draw, -1, -1);
			}
			g_ui->_y -= 4 * UI_SCALE();
		}

		if (g_config->tools == TOOLS_SCULPT) {
			for (i32 i = 0; i <= SCULPT_TYPE_PLATEAU; ++i) {
				ui_toolbar_draw_sculpt_tool(i, img, icon_accent);
			}
		}
		// The cursor leads the object tools, it is the last one of the paint and sculpt tools
		bool cursor_last = g_config->tools != TOOLS_OBJECT;
		for (i32 i = 0; i < ui_toolbar_tool_names->length; ++i) {
			if (ui_toolbar_tool_visible(i) && !(cursor_last && i == TOOL_TYPE_CURSOR)) {
				ui_toolbar_draw_tool(i, img, icon_accent);
			}
		}
		if (cursor_last) {
			ui_toolbar_draw_tool(TOOL_TYPE_CURSOR, img, icon_accent);
		}

		g_ui->image_scroll_align = true;
		// The window is not redrawn once the mouse leaves, the hover of the last tool would stay set
		g_ui->is_hovered = false;
	}

	if (g_config->touch_ui) {
		// Hide scrollbar
		i32 _SCROLL_W     = g_theme->SCROLL_W;
		g_theme->SCROLL_W = 0;
		ui_end_window();
		g_theme->SCROLL_W = _SCROLL_W;
	}
}

void ui_toolbar_render_ui() {
	i32 _WINDOW_BG_COL = g_theme->WINDOW_BG_COL;

	if (!base_view3d_show && !ui_view2d_show && !ui_nodes_show) {
		ui_toolbar_draw_show_3d_view();
		return;
	}

	if (!base_view3d_show && ui_view2d_show && ui_view2d_type != VIEW_2D_TYPE_LAYER) {
		return;
	}

	if (!base_view3d_show && ui_nodes_show && !ui_view2d_show) {
		return;
	}

	// Toolbar is docked on the side of the 3D view, it floats in the view when the header is hidden
	i32 x              = base_view_x(0) - ui_toolbar_w(false);
	i32 y              = base_y() - ui_header_h;
	i32 h              = base_h() + ui_header_h;
	_ui_toolbar_y      = base_y();
	_ui_toolbar_bottom = base_y() + base_h();

	if (context_is_floating_toolbar()) {
		// 2D view takes the toolbar when there is no 3D view
		if (!base_view3d_show) {
			x                  = ui_view2d_wx + ui_toolbar_x();
			_ui_toolbar_y      = ui_view2d_wy;
			_ui_toolbar_bottom = ui_view2d_wy + ui_view2d_wh;
		}
		else {
			x = base_view_x(0) + ui_toolbar_x();
		}
		y                      = _ui_toolbar_y + ui_header_h + ui_toolbar_x() + 3 * UI_SCALE();
		h                      = ui_toolbar_item_count() * (ui_toolbar_w(false) + 2);
		g_theme->WINDOW_BG_COL = g_theme->SEPARATOR_COL;
		if (!base_view3d_show && ui_view2d_show && !g_config->touch_ui) {
			y += ui_toolbar_w(false);
		}

#ifdef IRON_IOS
		if (config_is_iphone()) {
			y += ui_toolbar_w(false);
		}
#endif
	}

	// One toolbar per 3D view
	i32 views = base_is_split() ? 2 : 1;
	ui_window_sync_redraws(ui_toolbar_handle, ui_toolbar_split_handle, views == 2);
	for (i32 i = 0; i < views; ++i) {
		ui_toolbar_draw_window(i == 0 ? ui_toolbar_handle : ui_toolbar_split_handle, x + base_view_x(i) - base_view_x(0), y, h);
	}

	if (context_is_floating_toolbar()) {
		g_theme->WINDOW_BG_COL = _WINDOW_BG_COL;
	}
}
