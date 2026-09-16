
#include "../global.h"

i32 ui_sidebar_tabx     = 0;
i32 ui_sidebar_last_tab = 0;

void ui_sidebar_render_ui() {

	// Expand button
	if (g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] == 0) {
		i32 width = math_floor(draw_string_width(g_font, g_ui->font_size, "<") + 25 * UI_SCALE());
		if (ui_window(ui_sidebar_hminimized, iron_window_width() - width, -1, width, math_floor(UI_ELEMENT_H() + 4 * UI_SCALE()), false)) {
			ui_fill(0, 0, g_ui->_window_w, g_ui->_window_h + 1, g_theme->SEPARATOR_COL);
			g_ui->_w            = width;
			i32 _BUTTON_H       = g_theme->BUTTON_H;
			i32 _BUTTON_COL     = g_theme->BUTTON_COL;
			i32 _TEXT_COL       = g_theme->TEXT_COL;
			g_theme->BUTTON_H   = g_theme->ELEMENT_H;
			g_theme->BUTTON_COL = g_theme->SEPARATOR_COL;
			g_theme->TEXT_COL   = g_theme->HOVER_COL;

			if (ui_button("<", UI_ALIGN_CENTER, "")) {
				ui_sidebar_show(true);
			}
			g_theme->BUTTON_H   = _BUTTON_H;
			g_theme->BUTTON_COL = _BUTTON_COL;
			g_theme->TEXT_COL   = _TEXT_COL;
		}
		return;
	}

	// Tabs
	bool mini                 = g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] <= ui_sidebar_w_mini;
	i32  expand_button_offset = g_config->touch_ui ? math_floor(UI_ELEMENT_H() + UI_ELEMENT_OFFSET()) : 0;
	ui_sidebar_tabx           = iron_window_width() - g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W];

	i32 _SCROLL_W = g_theme->SCROLL_W;
	if (mini) {
		g_theme->SCROLL_W = g_theme->SCROLL_MINI_W;
	}

	i32 sidebar_y = 0;

#ifdef IRON_IOS
	if (config_is_iphone()) {
		sidebar_y += UI_ELEMENT_H() + UI_ELEMENT_OFFSET();
		ui_end();
		draw_begin(NULL, false, 0);
		draw_set_color(g_theme->PRESSED_COL);
		draw_filled_rect(ui_sidebar_tabx, 0, g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W], sidebar_y);
		draw_end();
		ui_begin(g_ui);
	}
#endif

	if (ui_window(ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0], ui_sidebar_tabx, sidebar_y, g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W],
	              g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H0] - sidebar_y, false)) {
		tab_draw_t_array_t *tabs = ui_base_hwnd_tabs->buffer[TAB_AREA_SIDEBAR0];
		for (i32 i = 0; i < (mini ? 1 : tabs->length); ++i) {
			tabs->buffer[i]->f(&ui_base_tabs->buffer[TAB_AREA_SIDEBAR0]);
		}

		if (ui_base_tabs->buffer[TAB_AREA_SIDEBAR0] < tabs->length) {
			ui_sidebar_last_tab = ui_base_tabs->buffer[TAB_AREA_SIDEBAR0];
		}

		if (!g_config->touch_ui && !mini) {
			if (ui_tab(&ui_base_tabs->buffer[TAB_AREA_SIDEBAR0], ">", false, -2, false)) {
				ui_sidebar_show(false);
			}
		}
	}
	if (ui_window(ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1], ui_sidebar_tabx, g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H0],
	              g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W], g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_H1] - expand_button_offset, false)) {
		tab_draw_t_array_t *tabs = ui_base_hwnd_tabs->buffer[TAB_AREA_SIDEBAR1];
		for (i32 i = 0; i < (mini ? 1 : tabs->length); ++i) {
			tabs->buffer[i]->f(&ui_base_tabs->buffer[TAB_AREA_SIDEBAR1]);
		}
	}

	ui_end_window();
	g_theme->SCROLL_W = _SCROLL_W;

	// Collapse / expand button for mini sidebar
	if (g_config->touch_ui) {
		i32                width  = g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W];
		i32                height = math_floor(UI_ELEMENT_H() + UI_ELEMENT_OFFSET());
		static ui_window_t window = {0};
		if (ui_window(&window, iron_window_width() - width, iron_window_height() - height, width, height + 1, false)) {
			g_ui->_w            = width;
			i32 _BUTTON_H       = g_theme->BUTTON_H;
			i32 _BUTTON_COL     = g_theme->BUTTON_COL;
			g_theme->BUTTON_H   = g_theme->ELEMENT_H;
			g_theme->BUTTON_COL = g_theme->WINDOW_BG_COL;
			if (ui_button(mini ? "<" : ">", UI_ALIGN_CENTER, "")) {
				g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] = mini ? ui_sidebar_default_w_full : ui_sidebar_default_w_mini;
				g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] = math_floor(g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] * UI_SCALE());
			}
			g_theme->BUTTON_H   = _BUTTON_H;
			g_theme->BUTTON_COL = _BUTTON_COL;
		}
	}
}

void ui_sidebar_show(bool b) {
	if (b) {
		g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W] =
		    g_context->maximized_sidebar_width != 0 ? g_context->maximized_sidebar_width : math_floor(ui_sidebar_default_w * g_config->window_scale);
	}
	else {
		ui_base_tabs->buffer[TAB_AREA_SIDEBAR0]          = ui_sidebar_last_tab;
		g_config->layout_tabs->buffer[TAB_AREA_SIDEBAR0] = ui_sidebar_last_tab;
		g_context->maximized_sidebar_width               = g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W];
		g_config->layout->buffer[LAYOUT_SIZE_SIDEBAR_W]  = 0;
	}
}
