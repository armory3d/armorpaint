
#include "../global.h"

bool         ui_menu_hide_flag  = false;
i32          ui_menu_sub_x      = 0;
i32          ui_menu_sub_y      = 0;
ui_handle_t *ui_menu_sub_handle = NULL;
char        *_ui_menu_render_msg;

void ui_menu_hide() {
	ui_menu_show = false;
	base_redraw_ui();
}

void ui_menu_fit_to_screen() {
	// Prevent the menu going out of screen
	f32 menu_w = base_default_element_w * UI_SCALE() * 2.3;
	if (ui_menu_x + menu_w > iron_window_width()) {
		if (ui_menu_x - menu_w > 0) {
			ui_menu_x = math_floor(ui_menu_x - menu_w);
		}
		else {
			ui_menu_x = math_floor(iron_window_width() - menu_w);
		}
	}
	if (ui_menu_y + ui_menu_h > iron_window_height()) {
		if (ui_menu_y - ui_menu_h > 0) {
			ui_menu_y = math_floor(ui_menu_y - ui_menu_h);
		}
		else {
			ui_menu_y = iron_window_height() - ui_menu_h;
		}
		ui_menu_x += 1; // Move out of mouse focus
	}
}

void ui_menu_render() {
	i32 menu_w = ui_menu_commands != NULL ? math_floor(base_default_element_w * UI_SCALE() * 2.3) : math_floor(UI_ELEMENT_W() * 2.3);

	i32 _FILL_BUTTON_BG     = g_theme->FILL_BUTTON_BG;
	g_theme->FILL_BUTTON_BG = false;
	i32 _ELEMENT_OFFSET     = g_theme->ELEMENT_OFFSET;
	g_theme->ELEMENT_OFFSET = 0;
	i32 _ELEMENT_H          = g_theme->ELEMENT_H;
	g_theme->ELEMENT_H      = g_config->touch_ui ? (28 + 2) : 28;

	if (ui_menu_nested) {
		ui_menu_show_first = true;
		ui_menu_nested     = false;
	}

	// First draw out of screen, then align the menu based on menu height
	bool out_of_screen = ui_menu_show_first;
	if (out_of_screen) {
		ui_menu_x -= iron_window_width() * 2;
		ui_menu_y -= iron_window_height() * 2;
	}

	draw_begin(NULL, false, 0);
	ui_begin_region(g_ui, ui_menu_x, ui_menu_y, menu_w);
	g_ui->input_enabled = g_ui->combo_selected_handle == NULL;
	ui_menu_begin();

	if (ui_menu_commands != NULL) {
		ui_menu_commands();
	}

	ui_menu_hide_flag = g_ui->combo_selected_handle == NULL && !ui_menu_keep_open && !ui_menu_show_first &&
	                    (g_ui->changed || g_ui->input_released || g_ui->input_released_r || g_ui->is_escape_down);
	ui_menu_keep_open = false;

	g_theme->FILL_BUTTON_BG = _FILL_BUTTON_BG;
	g_theme->ELEMENT_OFFSET = _ELEMENT_OFFSET;
	g_theme->ELEMENT_H      = _ELEMENT_H;
	ui_menu_end();
	ui_end_region();
	g_ui->input_enabled = true;
	draw_end();

	if (ui_menu_show_first) {
		ui_menu_show_first = false;
		ui_menu_keep_open  = true;
		ui_menu_h          = g_ui->_y - ui_menu_y;
		if (out_of_screen) {
			ui_menu_x += iron_window_width() * 2;
			ui_menu_y += iron_window_height() * 2;
		}
		ui_menu_fit_to_screen();
		ui_menu_render(); // Render at correct position now
	}

	if (ui_menu_hide_flag) {
		ui_menu_hide();
		ui_menu_show_first = true;
		ui_menu_commands   = NULL;
	}
}

void ui_menu_draw(void (*commands)(void), i32 x, i32 y) {
	ui_end_input();
	if (ui_menu_show) {
		ui_menu_nested    = true;
		ui_menu_keep_open = true;
	}
	ui_menu_show     = true;
	ui_menu_commands = commands;
	ui_menu_x        = x > -1 ? x : math_floor(mouse_x + 1);
	ui_menu_y        = y > -1 ? y : math_floor(mouse_y + 1);
	ui_menu_h        = 0;
}

void ui_menu_separator() {
	g_ui->_y++;
	ui_fill(26, 0, g_ui->_w / (float)UI_SCALE() - 26, 1, g_theme->BUTTON_COL);
}

bool ui_menu_button(char *text, char *label, icon_t icon) {
	if (g_config->touch_ui && !string_equals(label, ">")) {
		label = "";
	}
	i32  _x_left = g_ui->_x;
	i32  _y_top  = g_ui->_y;
	bool result  = ui_button(string_tmp("%s%s", config_button_spacing, text), config_button_align, label);
	if (string_equals(label, ">") && result) {
		ui_menu_keep_open = true;
	}

	if (icon != ICON_NONE) {
		i32            _y_bottom = g_ui->_y;
		gpu_texture_t *icons     = resource_get("icons05x.k");
		rect_t        *rect      = resource_tile50(icons, icon);
		i32            icon_h    = 25 * UI_SCALE();
		g_ui->_x                 = _x_left - 5 * UI_SCALE();
		g_ui->_y                 = _y_top - 1;
		if (g_config->touch_ui) {
			g_ui->_x = _x_left - 2 * UI_SCALE();
			g_ui->_y = _y_top + 2 * UI_SCALE();
		}
		ui_sub_image(icons, base_darker(g_theme->LABEL_COL, 0x00222222), icon_h, rect->x / 2.0, rect->y / 2.0, rect->w / 2.0, rect->h / 2.0);
		g_ui->_x = _x_left;
		g_ui->_y = _y_bottom;
	}

	return result;
}

u32 ui_menu_color_sub(u32 c, u32 s) {
	return c - (s + 0xff000000) > c ? 0xff000000 : c - s;
}

bool ui_icon_button(char *text, icon_t icon, ui_align_t align) {
	i32 _x_left = g_ui->_x;
	i32 _y_top  = g_ui->_y;
	i32 _w      = g_ui->_w;
	if (!string_equals(text, "")) {
		text = align == UI_ALIGN_LEFT ? string_tmp("        %s", text) : string_tmp("      %s", text);
	}

	char *tooltip = "";
	i32   textw   = draw_string_width(g_font, g_ui->font_size, text);
	f32   wmax    = g_config->touch_ui ? 0.9 : 0.8;
	if (textw > _w * wmax) {
		tooltip = text;
		text    = "";
		textw   = 0;
	}

	bool result = ui_button(text, align, "");

	if (g_ui->is_hovered && !string_equals(tooltip, "")) {
		ui_tooltip(tooltip);
	}

	if (icon != ICON_NONE) {
		i32            _x_right  = g_ui->_x;
		i32            _y_bottom = g_ui->_y;
		gpu_texture_t *icons     = resource_get("icons05x.k");
		rect_t        *rect      = resource_tile50(icons, icon);
		i32            icon_h    = 25 * UI_SCALE();
		g_ui->_x                 = align == UI_ALIGN_LEFT ? _x_left : _x_left + _w / 2.0 - textw / 2.0 - icon_h / 2.0;
		g_ui->_y                 = _y_top;

		if (g_config->touch_ui) {
			g_ui->_x += 1 * UI_SCALE();
			if (!string_equals(text, "")) {
				g_ui->_x += 5 * UI_SCALE();
			}
			g_ui->_y = _y_top + 2 * UI_SCALE();
		}
		if (g_ui->current_ratio > -1) {
			g_ui->current_ratio--;
		}

		g_ui->image_scroll_align = false;
		ui_sub_image(icons, g_ui->enabled ? ui_menu_color_sub(g_theme->LABEL_COL, 0x00333333) : 0xffffffff, icon_h, rect->x / 2.0, rect->y / 2.0, rect->w / 2.0,
		             rect->h / 2.0);
		g_ui->image_scroll_align = true;

		g_ui->_x = _x_right;
		g_ui->_y = _y_bottom;
	}
	return result;
}

bool ui_menu_sub_button(ui_handle_t *handle, char *text) {
	g_ui->is_hovered = false;
	ui_menu_button(text, ">", ICON_NONE);
	if (g_ui->is_hovered) {
		ui_menu_sub_handle = handle;
	}
	else if (math_abs(g_ui->input_dy) > g_ui->input_dx && g_ui->input_x < g_ui->_x + g_ui->_w) {
		ui_menu_sub_handle = NULL;
	}
	return ui_menu_sub_handle == handle;
}

void ui_menu_label(char *text, char *shortcut) {
	i32 _y            = g_ui->_y;
	i32 _TEXT_COL     = g_theme->TEXT_COL;
	g_theme->TEXT_COL = g_theme->LABEL_COL;
	ui_text(text, UI_ALIGN_LEFT, 0x00000000);
	if (shortcut != NULL) {
		g_ui->_y = _y;
		ui_text(shortcut, UI_ALIGN_RIGHT, 0x00000000);
	}
	g_theme->TEXT_COL = _TEXT_COL;
}

void ui_menu_align() {
	if (!g_config->touch_ui) {
		f32_array_t *row = f32_array_create_from_raw_tmp(
		    (f32[]){
		        12 / 100.0,
		        88 / 100.0,
		    },
		    2);
		ui_row(row);
		ui_end_element();
	}
}

void ui_menu_begin() {
	ui_draw_shadow(g_ui->_x, g_ui->_y, g_ui->_w, ui_menu_h);
	draw_set_color(g_theme->SEPARATOR_COL);
	ui_draw_rect(true, true, g_ui->_x, g_ui->_y, g_ui->_w, ui_menu_h);
	draw_set_color(0xffffffff);
}

void ui_menu_end() {}

void ui_menu_sub_begin(i32 items) {
	ui_menu_sub_x = g_ui->_x;
	ui_menu_sub_y = g_ui->_y;
	g_ui->_x += g_ui->_w + 2;
	g_ui->_y -= UI_ELEMENT_H();
	ui_draw_shadow(g_ui->_x, g_ui->_y, g_ui->_w, UI_ELEMENT_H() * items);
	draw_set_color(g_theme->SEPARATOR_COL);
	ui_draw_rect(true, true, g_ui->_x, g_ui->_y, g_ui->_w, UI_ELEMENT_H() * items);
	draw_set_color(0xffffffff);
}

void ui_menu_sub_end() {
	g_ui->_x = ui_menu_sub_x;
	g_ui->_y = ui_menu_sub_y;
}
