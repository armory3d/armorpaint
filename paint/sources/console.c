
#include "global.h"

char *console_progress_text = NULL;

void console_draw_toast(char *s) {
	draw_begin(NULL, false, 0);
	draw_set_color(0x55000000);
	draw_filled_rect(0, 0, iron_window_width(), iron_window_height());
	f32 scale = UI_SCALE();
	f32 x     = iron_window_width() / 2.0;
	f32 y     = iron_window_height() - 200 * scale;
	draw_filled_rect(x - 200 * scale, y, 400 * scale, 80 * scale);
	draw_set_font(g_font, math_floor(22 * scale));
	draw_set_color(0xffffffff);
	draw_string(s, x - draw_string_width(draw_font, draw_font_size, s) / 2.0, y + 40 * scale - draw_font_height(draw_font, draw_font_size) / 2.0);
	draw_end();
}

void console_toast(char *s) {
	// Show a popup message
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();
	console_log(s);
	console_draw_toast(s);
	if (in_use)
		draw_begin(current, false, 0);
}

void console_draw_progress(void *_) {
	console_draw_toast(console_progress_text);
}

void console_progress(char *s) {
	// Keep popup message displayed until s == NULL
	if (s == NULL) {
		sys_remove_update(console_draw_progress);
	}
	else if (console_progress_text == NULL) {
		sys_notify_on_update(console_draw_progress, NULL);
	}
	if (s != NULL) {
		console_log(s);
	}
	console_progress_text = string_copy(s);

	// Pass one frame to immediately show the message
	draw_end();
	sys_render();
	draw_begin(NULL, false, 0);
	gpu_present();
	ui_end_input();
}

void console_info(char *s) {
	console_message_timer = 5.0;
	console_message = string_copy(s);
	console_message_color = 0x00000000;
	base_redraw_status();
	console_log(s);
}

void console_error(char *s) {
	console_message_timer = 8.0;
	console_message = string_copy(s);
	console_message_color = 0xffaa0000;
	base_redraw_status();
	console_log(s);
}

void console_log(char *s) {
	iron_log(s);
	base_redraw_console();
	string_array_push(console_last_traces, string_copy(s));
	if (console_last_traces->length > 100) {
		array_shift(console_last_traces);
	}
}
