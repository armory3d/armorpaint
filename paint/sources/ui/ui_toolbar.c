
#include "../global.h"

i32 ui_toolbar_last_tool = 0;
i32 _ui_toolbar_i;

void ui_toolbar_init() {}

void ui_toolbar_draw_tool_select_tool(void *_) {
	context_select_tool(_ui_toolbar_i);
}

void ui_toolbar_tool_properties_menu_draw() {
	g_ui->changed = false;
	ui_header_draw_tool_properties();
	if (g_ui->changed || g_ui->is_typing) {
		ui_menu_keep_open = true;
	}
	if (base_view3d_show && ui_button(tr("Pin to Header"), UI_ALIGN_LEFT, "")) {
		g_config->layout->buffer[LAYOUT_SIZE_HEADER] = 1;
	}
	if (base_view3d_show && ui_button(tr("Hide 3D View"), UI_ALIGN_LEFT, "")) {
		ui_base_show_3d_view();
	}
}

void ui_toolbar_tool_properties_menu() {
	i32 y = g_ui->_y - 2 * UI_SCALE();
	if (!base_view3d_show) {
		y += ui_toolbar_w(false);
	}

#ifdef IRON_IOS
	if (config_is_iphone() && base_view3d_show) {
		y += ui_toolbar_w(false);
	}
#endif

	ui_menu_draw(&ui_toolbar_tool_properties_menu_draw, g_ui->_x + g_ui->_w + 6 * UI_SCALE(), y);
}

void ui_toolbar_draw_highlight() {
	i32 size = ui_toolbar_w(false) - 4;
	draw_set_color(g_theme->HIGHLIGHT_COL);
	ui_draw_rect(true, true, g_ui->_x + -1, g_ui->_y + 2, size + 2, size + 2);
}

void ui_toolbar_draw_tool(i32 tool, gpu_texture_t *img, i32 icon_accent) {
	g_ui->_x += 2;
	if (g_context->tool == tool) {
		ui_toolbar_draw_highlight();
	}
	i32     tile_y = math_floor(tool / 12.0);
	i32     tile_x = tile_y % 2 == 0 ? tool % 12 : (11 - (tool % 12));
	i32     tile_i = tile_y * 12 + tile_x;
	rect_t *rect   = resource_tile50(img, tile_i);
	i32     _y     = g_ui->_y;

	bool visible = true;
	if (context_is_floating_toolbar()) {
		i32 statush = g_config->layout->buffer[LAYOUT_SIZE_STATUS_H];
		i32 statusy = iron_window_height() - statush;
		visible     = g_ui->input_y < statusy;
	}

	g_ui->_x -= 2;
	ui_state_t image_state = ui_sub_image(img, icon_accent, -1.0, rect->x, rect->y, rect->w, rect->h);
	g_ui->_x += 2;

	if (image_state == UI_STATE_STARTED && visible) {
		_ui_toolbar_i = tool;
		sys_notify_on_next_frame(&ui_toolbar_draw_tool_select_tool, NULL);
	}
	else if (image_state == UI_STATE_RELEASED && context_is_floating_toolbar() && visible) {
		if (ui_toolbar_last_tool == tool) {
			ui_toolbar_tool_properties_menu();
		}
		ui_toolbar_last_tool = tool;
	}

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
		if (!string_equals(key, "")) {
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

void ui_toolbar_render_ui() {
	i32 x              = 0;
	i32 y              = ui_header_h;
	i32 h              = iron_window_height() - ui_header_h - g_config->layout->buffer[LAYOUT_SIZE_STATUS_H];
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

	if (context_is_floating_toolbar()) {
		x += ui_toolbar_x();
		y += ui_toolbar_x() + 3 * UI_SCALE();
		h                      = (ui_toolbar_tool_names->length + 1) * (ui_toolbar_w(false) + 2);
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

	if (ui_window(ui_toolbar_handle, x, y, ui_toolbar_w(false), h, false)) {
		g_ui->_y -= 4 * UI_SCALE();
		g_ui->image_scroll_align   = false;
		gpu_texture_t *img         = resource_get("icons.k");
		u32            col         = g_theme->WINDOW_BG_COL;
		bool           light       = col > 0xff666666;
		i32            icon_accent = light ? 0xff666666 : -1;

		// Properties icon
		if (!context_is_floating_toolbar()) {
			rect_t *rect = resource_tile50(img, ICON_PROPERTIES);
			if (ui_sub_image(img, light ? 0xff666666 : g_theme->BUTTON_COL, -1.0, rect->x, rect->y, rect->w, rect->h) == UI_STATE_RELEASED) {
				g_config->layout->buffer[LAYOUT_SIZE_HEADER] = 0;
			}
		}
		// Draw ">" button if header is hidden
		else {
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
			g_ui->_w            = ui_toolbar_w(false);

			if (ui_button(">", UI_ALIGN_CENTER, "")) {
				ui_toolbar_tool_properties_menu();
			}

			g_ui->_w            = _w;
			g_theme->ELEMENT_H  = _ELEMENT_H;
			g_theme->BUTTON_H   = _BUTTON_H;
			g_theme->BUTTON_COL = _BUTTON_COL;
			g_ui->font_offset_y = _fontOffsetY;
		}
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Toggle header"));
		}
		g_ui->_y -= 4 * UI_SCALE();

		for (i32 i = 0; i < ui_toolbar_tool_names->length; ++i) {
			ui_toolbar_draw_tool(i, img, icon_accent);
		}

		g_ui->image_scroll_align = true;
	}

	if (context_is_floating_toolbar()) {
		g_theme->WINDOW_BG_COL = _WINDOW_BG_COL;
	}

	if (g_config->touch_ui) {
		// Hide scrollbar
		i32 _SCROLL_W     = g_theme->SCROLL_W;
		g_theme->SCROLL_W = 0;
		ui_end_window();
		g_theme->SCROLL_W = _SCROLL_W;
	}
}
