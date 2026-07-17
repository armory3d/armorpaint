
#include "../global.h"

bool  ui_box_draggable             = true;
char *ui_box_title                 = "";
char *ui_box_text                  = "";
void (*ui_box_commands)(void)      = NULL;
void (*ui_box_modal_on_hide)(void) = NULL;
i32         ui_box_draws           = 0;
bool        ui_box_copyable        = false;
f32         ui_box_tween_alpha     = 0.0;
static bool ui_box_ignore_release  = false;

void ui_box_init() {
	ui_box_hwnd->redraws = 2;
	ui_box_hwnd->drag_x  = 0;
	ui_box_hwnd->drag_y  = 0;
	ui_box_show          = true;
	ui_box_draws         = 0;
	ui_box_click_to_hide = true;
	ui_box_ignore_release = g_ui->input_down; // Box may open on mouse down
}

void ui_box_render() {
	if (g_ui->is_key_pressed) {
		ui_box_hwnd->redraws = 2;
	}

	if (!ui_menu_show) {
		bool in_use    = g_ui->combo_selected_handle != NULL;
		bool is_escape = g_ui->is_escape_down;
		bool released  = g_ui->input_released;
		if (released && ui_box_ignore_release) {
			ui_box_ignore_release = false;
			released              = false;
		}
		if (ui_box_draws > 2 && (released || is_escape) && !in_use && !g_ui->is_typing) {
			i32 appw   = iron_window_width();
			i32 apph   = iron_window_height();
			i32 mw     = math_floor(ui_box_modalw * UI_SCALE());
			i32 mh     = math_floor(ui_box_modalh * UI_SCALE());
			f32 left   = (appw / 2.0 - mw / 2.0) + ui_box_hwnd->drag_x;
			f32 right  = (appw / 2.0 + mw / 2.0) + ui_box_hwnd->drag_x;
			f32 top    = (apph / 2.0 - mh / 2.0) + ui_box_hwnd->drag_y;
			f32 bottom = (apph / 2.0 + mh / 2.0) + ui_box_hwnd->drag_y;
			i32 mx     = mouse_x;
			i32 my     = mouse_y;
			if ((ui_box_click_to_hide && (mx < left || mx > right || my < top || my > bottom)) || is_escape) {
				ui_box_hide();
			}
		}
	}

	if (g_config->touch_ui) { // Darken bg
		draw_begin(NULL, false, 0);
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		draw_set_color(color_from_floats(0, 0, 0, ui_box_tween_alpha));
#else
		draw_set_color(color_from_floats(0, 0, 0, 0.5));
#endif
		draw_filled_rect(0, 0, iron_window_width(), iron_window_height());
		draw_end();
	}

	i32 appw = iron_window_width();
	i32 apph = iron_window_height();
	i32 mw   = math_floor(ui_box_modalw * UI_SCALE());
	i32 mh   = math_floor(ui_box_modalh * UI_SCALE());
	if (mw > appw) {
		mw = appw;
	}
	if (mh > apph) {
		mh = apph;
	}
	i32 left          = math_floor(appw / 2.0 - mw / 2.0);
	i32 top           = math_floor(apph / 2.0 - mh / 2.0);
	ui_box_hwnd->text = string_copy(ui_box_title);

	if (ui_box_commands == NULL) {
		ui_begin(g_ui);
		if (ui_window(ui_box_hwnd, left, top, mw, mh, ui_box_draggable)) {
			g_ui->_y += 10;
			ui_handle_t *htext = ui_handle(__ID__);
			htext->text        = string_copy(ui_box_text);
			if (ui_box_copyable) {
				draw_font_t *_font      = g_font;
				i32          _font_size = g_ui->font_size;
				ui_set_font(g_ui, data_get_font("font_mono.ttf"));
				g_ui->font_size = math_floor(15 * UI_SCALE());
				ui_text_area(htext, UI_ALIGN_LEFT, false, "", false);
				ui_set_font(g_ui, _font);
				g_ui->font_size = _font_size;
			}
			else {
				ui_text(ui_box_text, UI_ALIGN_LEFT, 0x00000000);
			}
			ui_end_element();

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
			if (ui_box_copyable) {
				ui_row3();
			}
			else {
				f32_array_t *row = f32_array_create_from_raw(
				    (f32[]){
				        2 / 3.0,
				        1 / 3.0,
				    },
				    2);
				ui_row(row);
			}
#else
			f32_array_t *row = f32_array_create_from_raw(
			    (f32[]){
			        2 / 3.0,
			        1 / 3.0,
			    },
			    2);
			ui_row(row);
#endif

			ui_end_element();

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)
			if (ui_box_copyable && ui_icon_button(tr("Copy"), ICON_COPY, UI_ALIGN_CENTER)) {
				iron_copy_to_clipboard(ui_box_text);
			}
#endif
			if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER)) {
				ui_box_hide();
			}
		}
		ui_end();
	}
	else {
		ui_begin(g_ui);
		g_ui->input_enabled = !ui_menu_show && g_ui->combo_selected_handle == NULL;
		if (ui_window(ui_box_hwnd, left, top, mw, mh, ui_box_draggable)) {
			g_ui->_y += 10;
			ui_box_commands();
		}
		g_ui->input_enabled = true;
		ui_end();
	}

	ui_box_draws++;
}

void ui_box_tween_tick() {
	base_redraw_ui();
}

void ui_box_tween_in() {
	tween_reset();

	tween_anim_t *a = GC_ALLOC_INIT(tween_anim_t, {.target = &ui_box_tween_alpha, .to = 0.5, .duration = 0.2, .ease = EASE_EXPO_OUT});
	tween_to(a);

	ui_box_hwnd->drag_y = math_floor(iron_window_height() / 2.0);
	a = GC_ALLOC_INIT(tween_anim_t, {.target = &ui_box_hwnd->drag_y, .to = 0.0, .duration = 0.2, .ease = EASE_EXPO_OUT, .tick = ui_box_tween_tick});
	tween_to(a);
}

void ui_box_hide_internal() {
	if (ui_box_modal_on_hide != NULL) {
		ui_box_modal_on_hide();
	}
	ui_box_show = false;
	base_redraw_ui();
}

void ui_box_tween_out() {
	tween_anim_t *a =
	    GC_ALLOC_INIT(tween_anim_t, {.target = &ui_box_tween_alpha, .to = 0.0, .duration = 0.2, .ease = EASE_EXPO_IN, .done = ui_box_hide_internal});
	tween_to(a);

	a = GC_ALLOC_INIT(tween_anim_t, {.target = &ui_box_hwnd->drag_y, .to = iron_window_height() / 2, .duration = 0.2, .ease = EASE_EXPO_IN});
	tween_to(a);
}

void ui_box_show_message(char *title, char *text, bool copyable) {
	ui_box_init();
	ui_box_modalw = copyable ? 800 : 400;
	ui_box_modalh = copyable ? 600 : 180;
	gc_unroot(ui_box_title);
	ui_box_title = string_copy(title);
	gc_root(ui_box_title);
	gc_unroot(ui_box_text);
	ui_box_text = string_copy(text);
	gc_root(ui_box_text);
	gc_unroot(ui_box_commands);
	ui_box_commands  = NULL;
	ui_box_copyable  = copyable;
	ui_box_draggable = true;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	ui_box_tween_in();
#endif
}

void ui_box_show_custom(void (*commands)(void), i32 mw, i32 mh, void (*on_hide)(void), bool draggable, char *title) {
	ui_box_init();
	ui_box_modalw = mw;
	ui_box_modalh = mh;
	gc_unroot(ui_box_modal_on_hide);
	ui_box_modal_on_hide = on_hide;
	gc_root(ui_box_modal_on_hide);
	gc_unroot(ui_box_commands);
	ui_box_commands = commands;
	gc_root(ui_box_commands);
	ui_box_draggable = draggable;
	gc_unroot(ui_box_title);
	ui_box_title = string_copy(title);
	gc_root(ui_box_title);
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	ui_box_tween_in();
#endif
}

void ui_box_hide() {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	ui_box_tween_out();
#else
	ui_box_hide_internal();
#endif
}
