#include "iron_ui.h"
#include "iron_draw.h"
#include "iron_file.h"
#include "iron_gc.h"
#include "iron_gpu.h"
#include "iron_string.h"
#include "iron_system.h"
#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ui_t        *current = NULL;
static ui_theme_t  *theme;
static bool         ui_key_repeat         = true; // Emulate key repeat for non-character keys
static bool         ui_dynamic_glyph_load = true; // Allow text input fields to push new glyphs into the font atlas
static float        ui_key_repeat_time    = 0.0;
char                ui_text_to_paste[8192];
char                ui_text_to_copy[8192];
static bool         ui_combo_first         = true;
static ui_handle_t *ui_combo_search_handle = NULL;
static int          touch_hold_x           = -1;
static int          touch_hold_y           = -1;
// Pan with finger to scroll
// Touch and hold finger for right click
// Show extra tooltips above finger / on-screen keyboard
bool  ui_touch_control                                         = false;
float ui_touch_speed                                           = 1.0;
bool  ui_is_cut                                                = false;
bool  ui_is_copy                                               = false;
bool  ui_is_paste                                              = false;
void (*ui_on_border_hover)(ui_handle_t *, int)                 = NULL; // Mouse over window border, use for resizing
void (*ui_on_tab_drop)(ui_handle_t *, int, ui_handle_t *, int) = NULL; // Tab reorder via drag and drop
#ifdef WITH_EVAL
#include "libs/minic.h"
#endif

f32_array_t *_ui_row2 = NULL;
f32_array_t *_ui_row3 = NULL;
f32_array_t *_ui_row4 = NULL;
f32_array_t *_ui_row5 = NULL;
f32_array_t *_ui_row6 = NULL;
f32_array_t *_ui_row7 = NULL;

float UI_SCALE() {
	return current->ops->scale_factor;
}

float UI_ELEMENT_W() {
	return theme->ELEMENT_W * UI_SCALE();
}

float UI_ELEMENT_H() {
	return theme->ELEMENT_H * UI_SCALE();
}

float UI_ELEMENT_OFFSET() {
	return theme->ELEMENT_OFFSET * UI_SCALE();
}

float UI_ARROW_SIZE() {
	return theme->ARROW_SIZE * UI_SCALE();
}

float UI_BUTTON_H() {
	return theme->BUTTON_H * UI_SCALE();
}

float UI_CHECK_SIZE() {
	return theme->CHECK_SIZE * UI_SCALE();
}

float UI_CHECK_SELECT_SIZE() {
	return theme->CHECK_SELECT_SIZE * UI_SCALE();
}

float UI_FONT_SIZE() {
	return theme->FONT_SIZE * UI_SCALE();
}

float UI_SCROLL_W() {
	return theme->SCROLL_W * UI_SCALE();
}

float UI_SCROLL_MINI_W() {
	return theme->SCROLL_MINI_W * UI_SCALE();
}

float UI_TEXT_OFFSET() {
	return theme->TEXT_OFFSET * UI_SCALE();
}

float UI_TAB_W() {
	return theme->TAB_W * UI_SCALE();
}

float UI_HEADER_DRAG_H() {
	return 30.0 * UI_SCALE();
}

float UI_TOOLTIP_DELAY() {
	return 0.7;
}

ui_t *ui_get_current() {
	return current;
}

void ui_set_current(ui_t *_current) {
	current = _current;
	theme   = current->ops->theme;
}

ui_handle_t *ui_handle_create() {
	ui_handle_t *h = (ui_handle_t *)gc_alloc(sizeof(ui_handle_t));
	memset(h, 0, sizeof(ui_handle_t));
	h->redraws = 2;
	h->color   = 0xffffffff;
	h->text    = "";
	h->init    = true;
	return h;
}

ui_handle_t *ui_nest(ui_handle_t *handle, int pos) {
	if (handle->children == NULL) {
		handle->children = any_array_create(0);
	}
	while (pos >= handle->children->length) {
		ui_handle_t *h = ui_handle_create();
		any_array_push(handle->children, h);
		if (pos == handle->children->length - 1) {
			// Return now so init stays true
			return h;
		}
	}
	// This handle already exists, set init to false
	handle->children->buffer[pos]->init = false;
	return handle->children->buffer[pos];
}

void ui_fade_color(float alpha) {
	uint32_t color = draw_get_color();
	uint8_t  r     = (color & 0x00ff0000) >> 16;
	uint8_t  g     = (color & 0x0000ff00) >> 8;
	uint8_t  b     = (color & 0x000000ff);
	uint8_t  a     = (uint8_t)(255.0 * alpha);
	draw_set_color((a << 24) | (r << 16) | (g << 8) | b);
}

void ui_fill(float x, float y, float w, float h, uint32_t color) {
	draw_set_color(color);
	if (!current->enabled) {
		ui_fade_color(0.25);
	}
	draw_filled_rect(current->_x + x * UI_SCALE(), current->_y + y * UI_SCALE() - 1, w * UI_SCALE(), h * UI_SCALE());
	draw_set_color(0xffffffff);
}

void ui_rect(float x, float y, float w, float h, uint32_t color, float strength) {
	draw_set_color(color);
	if (!current->enabled) {
		ui_fade_color(0.25);
	}
	draw_rect(current->_x + x * UI_SCALE(), current->_y + y * UI_SCALE(), w * UI_SCALE(), h * UI_SCALE(), strength);
	draw_set_color(0xffffffff);
}

void ui_draw_shadow(float x, float y, float w, float h) {
	float max_offset = 16.0 * UI_SCALE();
	for (int i = 0; i < 6; i++) {
		float offset = (max_offset / 6) * (i + 1);
		float a      = 0.04 - (0.04 / 6) * i;
		draw_set_color(((uint8_t)(a * 255) << 24) | (0 << 16) | (0 << 8) | 0);
		ui_draw_rect(true, false, x - offset, y, w + offset * 2, h + offset);
	}
}

void ui_draw_rect(bool fill, bool shadows, float x, float y, float w, float h) {
	if (!current->enabled) {
		ui_fade_color(0.25);
	}
	x = (int)x;
	y = (int)y;
	w = (int)w;
	h = (int)h;

	if (fill) {
		int r = current->filled_round_corner_image.width;
		if (current->enabled && r > 0 && w >= r * 2.0) {
			draw_scaled_image(&current->filled_round_corner_image, x, y, r, r);
			draw_scaled_image(&current->filled_round_corner_image, x, y + h, r, -r);
			draw_scaled_image(&current->filled_round_corner_image, x + w, y, -r, r);
			draw_scaled_image(&current->filled_round_corner_image, x + w, y + h, -r, -r);
			draw_filled_rect(x + r, y, w - r * 2.0, h);
			draw_filled_rect(x, y + r, w, h - r * 2.0);
			if (shadows) {
				uint32_t color = draw_get_color();
				draw_set_color(color + 0x00030303);
				draw_filled_rect(x + r, y + h - 1.0, w - r * 2.0, 1.0);
				draw_filled_rect(x, y + r, 1.0, h - r * 2.0);
				draw_filled_rect(x + w - 1.0, y + r, 1.0, h - r * 2.0);
				draw_set_color(color + 0x00080808);
				draw_filled_rect(x + r, y, w - r * 2.0, 1.0);
			}
		}
		else {
			draw_filled_rect(x, y - 1, w, h + 1);
		}
	}
	else {
		int   r        = current->round_corner_image.width;
		float strength = 1.0;
		if (current->enabled && r > 0) {
			draw_scaled_image(&current->round_corner_image, x, y, r, r);
			draw_scaled_image(&current->round_corner_image, x, y + h, r, -r);
			draw_scaled_image(&current->round_corner_image, x + w, y, -r, r);
			draw_scaled_image(&current->round_corner_image, x + w, y + h, -r, -r);
			draw_filled_rect(x + r, y, w - r * 2.0, strength);
			draw_filled_rect(x + r, y + h - 1.0, w - r * 2.0, strength);
			draw_filled_rect(x, y + r, strength, h - r * 2.0);
			draw_filled_rect(x + w - 1.0, y + r, strength, h - r * 2.0);
		}
		else {
			draw_rect(x, y, w, h, strength);
		}
	}
}

void ui_draw_rect_round_top(bool shadows, float x, float y, float w, float h) {
	int r = current->filled_round_corner_image.width;
	draw_scaled_image(&current->filled_round_corner_image, x, y, r, r);
	draw_scaled_image(&current->filled_round_corner_image, x + w, y, -r, r);
	draw_filled_rect(x + r, y, w - r * 2.0, h);
	draw_filled_rect(x, y + r, w, h - r);
	if (shadows) {
		uint32_t color = draw_get_color();
		draw_set_color(color + 0x00030303);
		draw_filled_rect(x, y + r, 1, h - r);
		draw_filled_rect(x + w - 1, y + r, 1, h - r);
		draw_set_color(color + 0x00080808);
		draw_filled_rect(x + r, y, w - r * 2.0, 1);
	}
}

void ui_draw_round_bottom(float x, float y, float w) {
	int r = current->filled_round_corner_image.width;
	int h = 4;
	draw_set_color(theme->SEPARATOR_COL);
	draw_scaled_image(&current->filled_round_corner_image, x, y + h, r, -r);
	draw_scaled_image(&current->filled_round_corner_image, x + w, y + h, -r, -r);
	draw_filled_rect(x + r, y, w - r * 2.0, h);
}

bool ui_is_char(int code) {
	return (code >= 65 && code <= 90) || (code >= 97 && code <= 122);
}

int ui_check_start(int i, char *text, char **start, int start_count) {
	for (int x = 0; x < start_count; ++x) {
		if (strncmp(text + i, start[x], strlen(start[x])) == 0) {
			return strlen(start[x]);
		}
	}
	return 0;
}

ui_text_extract_t ui_extract_coloring(char *text, ui_coloring_t *col) {
	ui_text_extract_t res;
	res.colored[0]    = '\0';
	res.uncolored[0]  = '\0';
	bool coloring     = false;
	int  start_from   = 0;
	int  start_length = 0;
	for (int i = 0; i < strlen(text); ++i) {
		bool skip_first = false;
		// Check if upcoming text should be colored
		int length = ui_check_start(i, text, col->start->buffer, col->start->length);
		// Not touching another character
		bool separated_left  = i == 0 || !ui_is_char(text[i - 1]);
		bool separated_right = i + length >= strlen(text) || !ui_is_char(text[i + length]);
		bool is_separated    = separated_left && separated_right;
		// Start coloring
		if (length > 0 && (!coloring || col->end[0] == '\0') && (!col->separated || is_separated)) {
			coloring     = true;
			start_from   = i;
			start_length = length;
			if (col->end[0] != '\0' && col->end[0] != '\n')
				skip_first = true;
		}
		// End coloring
		else if (col->end[0] == '\0') {
			if (i == start_from + start_length)
				coloring = false;
		}
		else if (strncmp(text + i, col->end, strlen(col->end)) == 0) {
			coloring = false;
		}
		// If true, add current character to colored string
		int len_c  = strlen(res.colored);
		int len_uc = strlen(res.uncolored);
		if (coloring && !skip_first) {
			res.colored[len_c]        = text[i];
			res.colored[len_c + 1]    = '\0';
			res.uncolored[len_uc]     = ' ';
			res.uncolored[len_uc + 1] = '\0';
		}
		else {
			res.colored[len_c]        = ' ';
			res.colored[len_c + 1]    = '\0';
			res.uncolored[len_uc]     = text[i];
			res.uncolored[len_uc + 1] = '\0';
		}
	}
	return res;
}

void ui_draw_string(char *text, float x_offset, float y_offset, int align, bool truncation) {
	static char temp[1024];
	static char truncated[1024];
	if (text == NULL) {
		return;
	}
	if (truncation) {
		assert(strlen(text) < 1024 - 2);
		char *full_text = text;
		strcpy(truncated, text);
		text = &truncated[0];
		while (strlen(text) > 0 && draw_string_width(current->ops->font, current->font_size, text) > current->_w - 6.0 * UI_SCALE()) {
			text[strlen(text) - 1] = 0;
		}
		if (strlen(text) < strlen(full_text)) {
			strcat(text, "..");
			// Strip more to fit ".."
			while (strlen(text) > 2 && draw_string_width(current->ops->font, current->font_size, text) > current->_w - 10.0 * UI_SCALE()) {
				text[strlen(text) - 3] = 0;
				strcat(text, "..");
			}
			if (current->is_hovered) {
				ui_tooltip(full_text);
			}
		}
	}

	if (ui_dynamic_glyph_load) {
		int len = strlen(text);
		for (int i = 0; i < len; ++i) {
			if (text[i] > 126 && !draw_font_has_glyph((int)text[i])) {
				int glyph = text[i];
				draw_font_add_glyph(glyph);
			}
		}
	}

	if (x_offset < 0) {
		x_offset = theme->TEXT_OFFSET;
	}
	x_offset *= UI_SCALE();
	draw_set_font(current->ops->font, current->font_size);
	if (align == UI_ALIGN_CENTER) {
		x_offset = current->_w / 2.0 - draw_string_width(current->ops->font, current->font_size, text) / 2.0;
	}
	else if (align == UI_ALIGN_RIGHT) {
		x_offset = current->_w - draw_string_width(current->ops->font, current->font_size, text) - UI_TEXT_OFFSET();
	}

	if (!current->enabled) {
		ui_fade_color(0.25);
	}

	if (current->text_coloring == NULL) {
		draw_string(text, current->_x + x_offset, current->_y + current->font_offset_y + y_offset);
	}
	else {
		// Monospace fonts only for now
		strcpy(temp, text);
		for (int i = 0; i < current->text_coloring->colorings->length; ++i) {
			ui_coloring_t    *coloring = current->text_coloring->colorings->buffer[i];
			ui_text_extract_t result   = ui_extract_coloring(temp, coloring);
			if (result.colored[0] != '\0') {
				draw_set_color(coloring->color);
				draw_string(result.colored, current->_x + x_offset, current->_y + current->font_offset_y + y_offset);
			}
			strcpy(temp, result.uncolored);
		}
		draw_set_color(current->text_coloring->default_color);
		draw_string(temp, current->_x + x_offset, current->_y + current->font_offset_y + y_offset);
	}
}

bool ui_get_initial_hover(float elem_h) {
	if (current->scissor && current->input_y < current->_window_y + current->window_header_h) {
		return false;
	}
	if (current->current_window != NULL && !ui_input_in_rect(current->_window_x, current->_window_y, current->_window_w, current->_window_h)) {
		return false;
	}
	return current->enabled && current->input_enabled && current->input_started_x >= current->_window_x + current->_x &&
	       current->input_started_x < (current->_window_x + current->_x + current->_w) && current->input_started_y >= current->_window_y + current->_y &&
	       current->input_started_y < (current->_window_y + current->_y + elem_h);
}

bool ui_get_hover(float elem_h) {
	if (current->scissor && current->input_y < current->_window_y + current->window_header_h) {
		return false;
	}
	if (current->current_window != NULL && !ui_input_in_rect(current->_window_x, current->_window_y, current->_window_w, current->_window_h)) {
		return false;
	}
	current->is_hovered = current->enabled && current->input_enabled && current->input_x >= current->_window_x + current->_x &&
	                      current->input_x < (current->_window_x + current->_x + current->_w) && current->input_y >= current->_window_y + current->_y &&
	                      current->input_y < (current->_window_y + current->_y + elem_h);
	return current->is_hovered;
}

bool ui_get_released(float elem_h) { // Input selection
	current->is_released = current->enabled && current->input_enabled && current->input_released && ui_get_hover(elem_h) && ui_get_initial_hover(elem_h);
	return current->is_released;
}

bool ui_get_pushed(float elem_h) {
	current->is_pushed = current->enabled && current->input_enabled && current->input_down && ui_get_hover(elem_h) && ui_get_initial_hover(elem_h);
	return current->is_pushed;
}

bool ui_get_started(float elem_h) {
	current->is_started = current->enabled && current->input_enabled && current->input_started && ui_get_hover(elem_h);
	return current->is_started;
}

bool ui_is_visible(float elem_h) {
	if (current->current_window == NULL)
		return true;
	return (current->_y + elem_h > current->window_header_h && current->_y < current->current_window->texture.height);
}

float ui_get_ratio(f32_array_t *ratios, int current, float w) {
	float ratio = ratios->buffer[current];
	if (ratio < 0) {
		float t = 0.0;
		for (int i = 0; i < ratios->length; ++i) {
			t += -ratios->buffer[i] * UI_SCALE();
		}
		float f = t > w ? w / t : 1.0;
		return -ratio * f * UI_SCALE();
	}
	else {
		return ratio * w;
	}
}

// Draw the upcoming elements in the same row
// Negative values will be treated as absolute, positive values as ratio to `window width`
void ui_row(f32_array_t *ratios) {
	if (ratios->length == 0) {
		current->ratios = NULL;
		return;
	}
	current->ratios         = ratios;
	current->current_ratio  = 0;
	current->x_before_split = current->_x;
	current->w_before_split = current->_w;
	current->_w             = ui_get_ratio(ratios, current->current_ratio, current->_w);
}

void ui_row2() {
	ui_row(_ui_row2);
}

void ui_row3() {
	ui_row(_ui_row3);
}

void ui_row4() {
	ui_row(_ui_row4);
}

void ui_row5() {
	ui_row(_ui_row5);
}

void ui_row6() {
	ui_row(_ui_row6);
}

void ui_row7() {
	ui_row(_ui_row7);
}

void ui_indent() {
	current->_x += UI_TAB_W();
	current->_w -= UI_TAB_W();
}

void ui_unindent() {
	current->_x -= UI_TAB_W();
	current->_w += UI_TAB_W();
}

void ui_end_element_of_size(float element_size) {
	if (current->current_window == NULL || current->current_window->layout == UI_LAYOUT_VERTICAL) {
		if (current->current_ratio == -1 || (current->ratios != NULL && current->current_ratio == current->ratios->length - 1)) { // New line
			current->_y += element_size;

			if ((current->ratios != NULL && current->current_ratio == current->ratios->length - 1)) { // Last row element
				current->current_ratio = -1;
				current->ratios        = NULL;
				current->_x            = current->x_before_split;
				current->_w            = current->w_before_split;
			}
		}
		else { // Row
			current->current_ratio++;
			current->_x += current->_w; // More row elements to place
			current->_w = ui_get_ratio(current->ratios, current->current_ratio, current->w_before_split);
		}
	}
	else { // Horizontal
		current->_x += current->_w + UI_ELEMENT_OFFSET();
	}
}

void ui_end_element() {
	ui_end_element_of_size(UI_ELEMENT_H() + UI_ELEMENT_OFFSET());
}

void ui_resize(ui_handle_t *handle, int w, int h) {
	handle->redraws = 2;
	if (handle->texture.width != 0) {
		gpu_texture_destroy(&handle->texture);
	}
	if (w < 1) {
		w = 1;
	}
	if (h < 1) {
		h = 1;
	}
	gpu_render_target_init(&handle->texture, w, h, GPU_TEXTURE_FORMAT_RGBA32);
}

bool ui_input_in_rect(float x, float y, float w, float h) {
	return current->enabled && current->input_enabled && current->input_x >= x && current->input_x < (x + w) && current->input_y >= y &&
	       current->input_y < (y + h);
}

bool ui_input_changed() {
	return current->input_dx != 0 || current->input_dy != 0 || current->input_wheel_delta != 0 || current->input_started || current->input_started_r ||
	       current->input_released || current->input_released_r || current->input_down || current->input_down_r || current->is_key_pressed;
}

void ui_end_input() {
	if (ui_on_tab_drop != NULL && current->drag_tab_handle != NULL) {
		if (current->input_dx != 0 || current->input_dy != 0) {
			iron_mouse_set_cursor(IRON_CURSOR_HAND);
		}
		if (current->input_released) {
			iron_mouse_set_cursor(IRON_CURSOR_ARROW);
			current->drag_tab_handle = NULL;
		}
	}

	current->is_key_pressed    = false;
	current->input_started     = false;
	current->input_started_r   = false;
	current->input_released    = false;
	current->input_released_r  = false;
	current->input_dx          = 0;
	current->input_dy          = 0;
	current->input_wheel_delta = 0;
	current->pen_in_use        = false;
	if (ui_key_repeat && current->is_key_down && iron_time() - ui_key_repeat_time > 0.05) {
		if (current->key_code == KEY_CODE_BACKSPACE || current->key_code == KEY_CODE_DELETE || current->key_code == KEY_CODE_LEFT ||
		    current->key_code == KEY_CODE_RIGHT || current->key_code == KEY_CODE_UP || current->key_code == KEY_CODE_DOWN) {
			ui_key_repeat_time      = iron_time();
			current->is_key_pressed = true;
		}
	}
	if (touch_hold_x > -1) {
		current->input_x          = touch_hold_x;
		current->input_y          = touch_hold_y;
		touch_hold_x              = -1;
		touch_hold_y              = -1;
		current->input_released_r = true;
	}
	if (ui_touch_control && current->input_down && current->input_x == current->input_started_x && current->input_y == current->input_started_y &&
	    current->input_started_time > 0 && iron_time() - current->input_started_time > 0.5) {
		current->touch_hold_activated = true;
		current->input_released       = true;
		touch_hold_x                  = current->input_x;
		touch_hold_y                  = current->input_y;
		current->input_x              = -1;
		current->input_y              = -1;
		current->input_started_time   = 0;
	}
}

void ui_scroll(float delta) {
	current->current_window->scroll_offset -= delta;
}

int ui_line_count(char *str) {
	if (str == NULL)
		return 0;
	int i     = 0;
	int count = 1;
	while (str[i] != '\0') {
		if (str[i] == '\n')
			count++;
		i++;
	}
	return count;
}

char *ui_extract_line_off(char *str, int line, int *off) {
	str += *off;
	static char temp[1024];
	int         pos    = 0;
	int         len    = strlen(str);
	int         line_i = 0;
	int         i      = 0;
	for (i = 0; i < len; ++i) {
		if (str[i] == '\n') {
			line_i++;
			if (line_i > line) {
				i++;
				break;
			}
			continue;
		}
		if (line_i < line) {
			continue;
		}
		if (line_i > line) {
			break;
		}
		temp[pos++] = str[i];
	}
	temp[pos] = 0;
	*off += i;
	return temp;
}

char *ui_extract_line(char *str, int line) {
	int off = 0;
	return ui_extract_line_off(str, line, &off);
}

char *ui_lower_case(char *dest, char *src) {
	int len = strlen(src);
	assert(len < 1024);
	for (int i = 0; i < len; ++i) {
		dest[i] = tolower(src[i]);
	}
	return dest;
}

void ui_draw_tooltip_text() {
	int   line_count = ui_line_count(current->tooltip_text);
	float tooltip_w  = 0.0;
	for (int i = 0; i < line_count; ++i) {
		float line_tooltip_w = draw_string_width(current->ops->font, current->font_size, ui_extract_line(current->tooltip_text, i));
		if (line_tooltip_w > tooltip_w) {
			tooltip_w = line_tooltip_w;
		}
	}
	current->tooltip_x = fmin(current->tooltip_x, iron_window_width() - tooltip_w - 20);
	draw_begin(NULL, false, 0);
	float font_height = draw_font_height(current->ops->font, current->font_size + 4);
	float off         = 0;
	if (current->tooltip_img != NULL) {
		float w = current->tooltip_img->width;
		if (current->tooltip_img_max_width != 0 && w > current->tooltip_img_max_width) {
			w = current->tooltip_img_max_width;
		}
		off = current->tooltip_img->height * (w / current->tooltip_img->width);
	}

	int x = current->tooltip_x - 5;
	int y = current->tooltip_y + off - 5;
	int w = tooltip_w + 20 + 10;
	int h = font_height * line_count + 6;
	ui_draw_shadow(x, y, w, h);
	draw_set_color(theme->SEPARATOR_COL);
	draw_filled_rect(x, y, w, h);

	draw_set_font(current->ops->font, current->font_size);
	draw_set_color(theme->TEXT_COL);
	for (int i = 0; i < line_count; ++i) {
		draw_string(ui_extract_line(current->tooltip_text, i), current->tooltip_x + 5, current->tooltip_y + off + i * (current->font_size + 4));
	}
	draw_end();
}

void ui_draw_tooltip_image() {
	float w = current->tooltip_img->width;
	if (current->tooltip_img_max_width != 0 && w > current->tooltip_img_max_width) {
		w = current->tooltip_img_max_width;
	}
	float h            = current->tooltip_img->height * (w / current->tooltip_img->width);
	current->tooltip_x = fmin(current->tooltip_x, iron_window_width() - w - 20);
	current->tooltip_y = fmin(current->tooltip_y, iron_window_height() - h - 20);
	draw_begin(NULL, false, 0);
	draw_set_color(0xff000000);
	draw_filled_rect(current->tooltip_x, current->tooltip_y, w, h);
	draw_set_color(0xffffffff);
	draw_scaled_image(current->tooltip_img, current->tooltip_x, current->tooltip_y, w, h);
	draw_end();
}

void ui_draw_tooltip() {
	static char temp[1024];
	if (current->slider_tooltip) {
		draw_begin(NULL, false, 0);
		draw_set_font(current->ops->font, current->font_size * 2);
		sprintf(temp, "%f", round(current->scroll_handle->f * 100.0) / 100.0);
		string_strip_trailing_zeros(temp);
		char *text  = temp;
		float x_off = draw_string_width(current->ops->font, current->font_size * 2.0, text) / 2.0;
		float y_off = draw_font_height(current->ops->font, current->font_size * 2.0);
		float x     = fmin(fmax(current->slider_tooltip_x, current->input_x), current->slider_tooltip_x + current->slider_tooltip_w);
		draw_set_color(theme->BUTTON_COL);
		draw_filled_rect(x - x_off, current->slider_tooltip_y - y_off, x_off * 2.0, y_off);
		draw_set_color(theme->TEXT_COL);
		draw_string(text, x - x_off, current->slider_tooltip_y - y_off);
		draw_end();
	}
	if (ui_touch_control && current->text_selected_handle != NULL) {
		draw_begin(NULL, false, 0);
		draw_set_font(current->ops->font, current->font_size * 2.0);
		float x_off = draw_string_width(current->ops->font, current->font_size * 2.0, current->text_selected) / 2.0;
		float y_off = draw_font_height(current->ops->font, current->font_size * 2.0) / 2.0;
		float x     = iron_window_width() / 2.0;
		float y     = iron_window_height() / 3.0;
		draw_set_color(theme->BUTTON_COL);
		draw_filled_rect(x - x_off, y - y_off, x_off * 2.0, y_off * 2.0);
		draw_set_color(theme->TEXT_COL);
		draw_string(current->text_selected, x - x_off, y - y_off);
		draw_end();
	}

	if (current->tooltip_text[0] != '\0' || current->tooltip_img != NULL) {
		if (ui_input_changed()) {
			current->tooltip_shown = false;
			current->tooltip_wait  = current->input_dx == 0 && current->input_dy == 0; // Wait for movement before showing up again
		}
		if (!current->tooltip_shown) {
			current->tooltip_shown = true;
			current->tooltip_x     = current->input_x;
			current->tooltip_time  = iron_time();
		}
		if (!current->tooltip_wait && iron_time() - current->tooltip_time > UI_TOOLTIP_DELAY()) {
			if (current->tooltip_img != NULL) {
				ui_draw_tooltip_image();
			}
			if (current->tooltip_text[0] != '\0') {
				ui_draw_tooltip_text();
			}
		}
	}
	else
		current->tooltip_shown = false;
}

void ui_draw_combo() {
	if (current->combo_selected_handle == NULL) {
		return;
	}
	draw_set_color(theme->SEPARATOR_COL);
	draw_begin(NULL, false, 0);

	float combo_h =
	    (current->combo_selected_texts->length + (current->combo_selected_label != NULL ? 1 : 0) + (current->combo_search_bar ? 1 : 0)) * UI_ELEMENT_H();
	float dist_top    = current->combo_selected_y - combo_h - UI_ELEMENT_H() - current->window_border_top;
	float dist_bottom = iron_window_height() - current->window_border_bottom - (current->combo_selected_y + combo_h);
	bool  unroll_up   = dist_bottom < 0 && dist_bottom < dist_top;

	ui_begin_region(current, current->combo_selected_x, current->combo_selected_y, current->combo_selected_w);

	float shadow_h =
	    (current->combo_selected_texts_filtered + (current->combo_selected_label != NULL ? 1 : 0) + (current->combo_search_bar ? 1 : 0)) * UI_ELEMENT_H();
	if (unroll_up) {
		float off = current->combo_selected_label != NULL ? UI_ELEMENT_H() / UI_SCALE() : 0.0;
		ui_draw_shadow(current->_x, current->_y - shadow_h - off, current->_w, shadow_h);
	}
	else {
		ui_draw_shadow(current->_x, current->_y, current->_w, shadow_h);
	}

	if (current->is_key_pressed || current->input_wheel_delta != 0) {
		int arrow_up   = current->is_key_pressed && current->key_code == (unroll_up ? KEY_CODE_DOWN : KEY_CODE_UP);
		int arrow_down = current->is_key_pressed && current->key_code == (unroll_up ? KEY_CODE_UP : KEY_CODE_DOWN);
		int wheel_up   = (unroll_up && current->input_wheel_delta > 0) || (!unroll_up && current->input_wheel_delta < 0);
		int wheel_down = (unroll_up && current->input_wheel_delta < 0) || (!unroll_up && current->input_wheel_delta > 0);
		if ((arrow_up || wheel_up) && current->combo_to_submit > 0) {
			int step = 1;
			if (current->combo_search_bar && strlen(current->text_selected) > 0) {
				char search[512];
				char str[512];
				ui_lower_case(search, current->text_selected);
				while (true) {
					ui_lower_case(str, current->combo_selected_texts->buffer[current->combo_to_submit - step]);
					if (strstr(str, search) == NULL && current->combo_to_submit - step > 0) {
						++step;
					}
					else {
						break;
					}
				}
				// Corner case: current position is the top one according to the search pattern
				ui_lower_case(str, current->combo_selected_texts->buffer[current->combo_to_submit - step]);
				if (strstr(str, search) == NULL) {
					step = 0;
				}
			}
			current->combo_to_submit -= step;
			current->submit_combo_handle = current->combo_selected_handle;
		}
		else if ((arrow_down || wheel_down) && current->combo_to_submit < current->combo_selected_texts->length - 1) {
			int step = 1;
			if (current->combo_search_bar && strlen(current->text_selected) > 0) {
				char search[512];
				char str[512];
				ui_lower_case(search, current->text_selected);
				while (true) {
					ui_lower_case(str, current->combo_selected_texts->buffer[current->combo_to_submit + step]);
					if (strstr(str, search) == NULL && current->combo_to_submit + step > 0) {
						++step;
					}
					else {
						break;
					}
				}
				// Corner case: current position is the top one according to the search pattern
				ui_lower_case(str, current->combo_selected_texts->buffer[current->combo_to_submit + step]);
				if (strstr(str, search) == NULL) {
					step = 0;
				}
			}

			current->combo_to_submit += step;
			current->submit_combo_handle = current->combo_selected_handle;
		}
		if (current->combo_selected_window != NULL) {
			current->combo_selected_window->redraws = 2;
		}
	}

	current->input_enabled = true;
	int  _BUTTON_COL       = theme->BUTTON_COL;
	int  _ELEMENT_OFFSET   = theme->ELEMENT_OFFSET;
	bool _SHADOWS          = theme->SHADOWS;
	theme->ELEMENT_OFFSET  = 0;
	theme->SHADOWS         = false;
	float unroll_right     = current->_x + current->combo_selected_w * 2.0 < iron_window_width() - current->window_border_right ? 1 : -1;
	bool  reset_position   = false;
	char  search[512];
	search[0] = '\0';
	if (current->combo_search_bar) {
		if (unroll_up) {
			current->_y -= UI_ELEMENT_H() * 2.0;
		}
		if (ui_combo_first) {
			ui_combo_search_handle->text = "";
		}
		ui_fill(0, 0, current->_w / UI_SCALE(), UI_ELEMENT_H() / UI_SCALE(), theme->SEPARATOR_COL);
		strcpy(search, ui_text_input(ui_combo_search_handle, "", UI_ALIGN_LEFT, true, true));
		ui_lower_case(search, search);
		if (current->is_released) {
			ui_combo_first = true; // Keep combo open
		}
		if (ui_combo_first) {
#if !defined(IRON_ANDROID) && !defined(IRON_IOS)
			ui_start_text_edit(ui_combo_search_handle, UI_ALIGN_LEFT); // Focus search bar
#endif
		}
		reset_position = ui_combo_search_handle->changed;
	}

	current->combo_selected_texts_filtered = 0;
	for (int i = 0; i < current->combo_selected_texts->length; ++i) {
		char str[512];
		ui_lower_case(str, current->combo_selected_texts->buffer[i]);
		if (strlen(search) > 0 && strstr(str, search) == NULL) {
			continue; // Don't show items that don't fit the current search pattern
		}
		current->combo_selected_texts_filtered++;

		if (reset_position) { // The search has changed, select first entry that matches
			current->combo_to_submit = current->combo_selected_handle->i = i;
			current->submit_combo_handle                                 = current->combo_selected_handle;
			reset_position                                               = false;
		}
		if (unroll_up) {
			current->_y -= UI_ELEMENT_H() * 2.0;
		}
		theme->BUTTON_COL = i == current->combo_selected_handle->i ? theme->HIGHLIGHT_COL : theme->SEPARATOR_COL;
		ui_fill(0, 0, current->_w / UI_SCALE(), UI_ELEMENT_H() / UI_SCALE(), theme->SEPARATOR_COL);
		if (ui_button(current->combo_selected_texts->buffer[i], current->combo_selected_align, "")) {
			current->combo_to_submit     = i;
			current->submit_combo_handle = current->combo_selected_handle;
			if (current->combo_selected_window != NULL) {
				current->combo_selected_window->redraws = 2;
			}
			break;
		}
		if (current->combo_selected_images != NULL && current->combo_selected_images->length > i) {
			draw_scaled_image(current->combo_selected_images->buffer[i], current->_x - UI_ELEMENT_H(), current->_y - UI_ELEMENT_H() + 1, UI_ELEMENT_H(),
			                  UI_ELEMENT_H());
		}
		if (current->_y + UI_ELEMENT_H() > iron_window_height() - current->window_border_bottom ||
		    current->_y - UI_ELEMENT_H() * 2 < current->window_border_top) {
			current->_x += current->combo_selected_w * unroll_right; // Next column
			current->_y = current->combo_selected_y;
		}
	}
	theme->BUTTON_COL     = _BUTTON_COL;
	theme->ELEMENT_OFFSET = _ELEMENT_OFFSET;
	theme->SHADOWS        = _SHADOWS;

	if (current->combo_selected_label != NULL) { // Unroll down
		if (unroll_up) {
			current->_y -= UI_ELEMENT_H() * 2.0;
			ui_fill(0, 0, current->_w / UI_SCALE(), UI_ELEMENT_H() / UI_SCALE(), theme->SEPARATOR_COL);
			draw_set_color(theme->LABEL_COL);
			ui_draw_string(current->combo_selected_label, theme->TEXT_OFFSET, 0, UI_ALIGN_RIGHT, true);
			current->_y += UI_ELEMENT_H();
			ui_fill(0, 0, current->_w / UI_SCALE(), 1.0 * UI_SCALE(), theme->HOVER_COL); // Separator
		}
		else {
			ui_fill(0, 0, current->_w / UI_SCALE(), UI_ELEMENT_H() / UI_SCALE(), theme->SEPARATOR_COL);
			ui_fill(0, 0, current->_w / UI_SCALE(), 1.0 * UI_SCALE(), theme->HOVER_COL); // Separator
			draw_set_color(theme->LABEL_COL);
			ui_draw_string(current->combo_selected_label, theme->TEXT_OFFSET, 0, UI_ALIGN_RIGHT, true);
			current->_y += UI_ELEMENT_H();
			ui_draw_round_bottom(current->_x, current->_y - 1, current->_w);
		}
	}

	if ((current->input_released || current->input_released_r || current->is_escape_down || current->is_return_down) && !ui_combo_first) {
		if (current->input_released_r || current->is_escape_down) {
			current->submit_combo_handle = current->combo_selected_handle;
			current->combo_to_submit     = current->combo_initial_value;
		}
		current->combo_selected_handle = NULL;
		ui_combo_first                 = true;
	}
	else {
		ui_combo_first = false;
	}
	current->input_enabled = current->combo_selected_handle == NULL;
	ui_end_region();
	draw_end();
}

void ui_end_frame() {
	ui_draw_combo(); // Handle active combo
	ui_draw_tooltip();
	ui_end_input();
}

void ui_bake_elements() {
	if (current->check_select_image.width != 0) {
		gpu_texture_destroy(&current->check_select_image);
	}
	float r = UI_CHECK_SELECT_SIZE();
	gpu_render_target_init(&current->check_select_image, r, r, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(&current->check_select_image, true, 0x00000000);
	draw_set_color(0xffffffff);
	draw_line(0, r / 2.0, r / 2.0 - 2.0 * UI_SCALE(), r - 2.0 * UI_SCALE(), 2.0 * UI_SCALE());
	draw_line(r / 2.0 - 3.0 * UI_SCALE(), r - 3.0 * UI_SCALE(), r / 2.0 + 5.0 * UI_SCALE(), r - 11.0 * UI_SCALE(), 2.0 * UI_SCALE());
	draw_end();

	if (current->radio_image.width != 0) {
		gpu_texture_destroy(&current->radio_image);
	}
	r = UI_CHECK_SIZE();
	gpu_render_target_init(&current->radio_image, r, r, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(&current->radio_image, true, 0x00000000);
	draw_set_color(0xffaaaaaa);
	draw_filled_circle(r / 2.0, r / 2.0, r / 2.0, 0);
	draw_set_color(0xffffffff);
	draw_circle(r / 2.0, r / 2.0, r / 2.0, 0, 1.0 * UI_SCALE());
	draw_end();

	if (current->radio_select_image.width != 0) {
		gpu_texture_destroy(&current->radio_select_image);
	}
	r = UI_CHECK_SELECT_SIZE();
	gpu_render_target_init(&current->radio_select_image, r, r, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(&current->radio_select_image, true, 0x00000000);
	draw_set_color(0xffaaaaaa);
	draw_filled_circle(r / 2.0, r / 2.0, 4.5 * UI_SCALE(), 0);
	draw_set_color(0xffffffff);
	draw_filled_circle(r / 2.0, r / 2.0, 4.0 * UI_SCALE(), 0);
	draw_end();

	if (current->filled_round_corner_image.width != 0) {
		gpu_texture_destroy(&current->filled_round_corner_image);
	}
	r = 4.0 * UI_SCALE();
	gpu_render_target_init(&current->filled_round_corner_image, r, r, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(&current->filled_round_corner_image, true, 0x00000000);
	draw_set_color(0xffffffff);
	draw_filled_circle(r, r, r, 0);
	draw_end();

	if (current->round_corner_image.width != 0) {
		gpu_texture_destroy(&current->round_corner_image);
	}
	gpu_render_target_init(&current->round_corner_image, r, r, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(&current->round_corner_image, true, 0x00000000);
	draw_set_color(0xffffffff);
	draw_circle(r, r, r, 0, 1);
	draw_end();

	current->elements_baked = true;
}

void ui_begin_region(ui_t *ui, int x, int y, int w) {
	ui_set_current(ui);
	if (!current->elements_baked) {
		draw_end();
		ui_bake_elements();
		draw_begin(NULL, false, 0);
	}
	current->changed         = false;
	current->current_window  = NULL;
	current->tooltip_text[0] = '\0';
	current->tooltip_img     = NULL;
	current->_window_x       = 0;
	current->_window_y       = 0;
	current->_window_w       = w;
	current->_x              = x;
	current->_y              = y;
	current->_w              = w;
	current->_h              = 0;
}

void ui_end_region() {
	current->tab_pressed_handle = NULL;
}

void ui_set_cursor_to_input(int align) {
	float off = align == UI_ALIGN_LEFT ? UI_TEXT_OFFSET() : current->_w - draw_string_width(current->ops->font, current->font_size, current->text_selected);
	float x   = current->input_x - (current->_window_x + current->_x + off);
	current->cursor_x = 0;
	while (current->cursor_x < strlen(current->text_selected) &&
	       draw_sub_string_width(current->ops->font, current->font_size, current->text_selected, 0, current->cursor_x) < x) {
		current->cursor_x++;
	}
	current->highlight_anchor = current->cursor_x;
}

void ui_start_text_edit(ui_handle_t *handle, int align) {
	current->is_typing          = true;
	current->submit_text_handle = current->text_selected_handle;
	strcpy(current->text_to_submit, current->text_selected);
	current->text_selected_handle = handle;
	strcpy(current->text_selected, handle->text);
	current->cursor_x = strlen(handle->text);
	if (current->tab_pressed) {
		current->tab_pressed    = false;
		current->is_key_pressed = false; // Prevent text deselect after tab press
	}
	else if (!current->highlight_on_select) { // Set cursor to click location
		ui_set_cursor_to_input(align);
	}
	current->tab_pressed_handle = handle;
	current->highlight_anchor   = current->highlight_on_select ? 0 : current->cursor_x;
	iron_keyboard_show();
}

void ui_submit_text_edit() {
	current->changed                     = strcmp(current->submit_text_handle->text, current->text_to_submit) != 0;
	current->submit_text_handle->changed = current->changed;
	current->submit_text_handle->text    = string_copy(current->text_to_submit);
	current->submit_text_handle          = NULL;
	current->text_to_submit[0]           = '\0';
	current->text_selected[0]            = '\0';
}

void keyboard_up_listener(int key_code);

void ui_deselect_text(ui_t *ui) {
	if (ui->text_selected_handle == NULL) {
		return;
	}
	ui->submit_text_handle = ui->text_selected_handle;
	strcpy(ui->text_to_submit, ui->text_selected);
	ui->text_selected_handle = NULL;
	ui->is_typing            = false;
	if (ui->current_window != NULL) {
		ui->current_window->redraws = 2;
	}
	iron_keyboard_hide();
	ui->highlight_anchor = ui->cursor_x;
#ifdef IRON_IOS
	keyboard_up_listener(KEY_CODE_SHIFT);
#endif
}

void ui_remove_char_at(char *str, int at) {
	int len = strlen(str);
	for (int i = at; i < len; ++i) {
		str[i] = str[i + 1];
	}
}

void ui_remove_chars_at(char *str, int at, int count) {
	for (int i = 0; i < count; ++i) {
		ui_remove_char_at(str, at);
	}
}

void ui_insert_char_at(char *str, int at, char c) {
	int len = strlen(str);
	for (int i = len + 1; i > at; --i) {
		str[i] = str[i - 1];
	}
	str[at] = c;
}

void ui_insert_chars_at(char *str, int at, char *cs) {
	int len = strlen(cs);
	for (int i = 0; i < len; ++i) {
		ui_insert_char_at(str, at + i, cs[i]);
	}
}

// Number of leading spaces to step over when moving/removing a tab backwards
static int ui_tab_dedent_count(char *str, int cursor_x) {
	int n = cursor_x % 4;
	if (n == 0) {
		n = 4;
	}
	if (cursor_x < n) {
		return 0;
	}
	for (int i = 1; i <= n; ++i) {
		if (str[cursor_x - i] != ' ') {
			return 0;
		}
	}
	return n;
}

// Number of trailing spaces to step over when moving/removing a tab forwards
static int ui_tab_indent_count(char *str, int cursor_x, int len) {
	int n = 4 - (cursor_x % 4);
	if (cursor_x + n > len) {
		return 0;
	}
	for (int i = 0; i < n; ++i) {
		if (str[cursor_x + i] != ' ') {
			return 0;
		}
	}
	return n;
}

void ui_update_text_edit(int align, bool editable, bool live_update) {
	char text[1024];
	strcpy(text, current->text_selected);
	if (current->is_key_pressed) {                // Process input
		if (current->key_code == KEY_CODE_LEFT) { // Move cursor
			if (current->is_ctrl_down) {          // Jump to previous word
				int i = current->cursor_x;
				while (i > 0 && text[i - 1] == ' ')
					i--;
				while (i > 0 && text[i - 1] != ' ')
					i--;
				current->cursor_x = i;
			}
			else if (!current->is_shift_down && current->highlight_anchor != current->cursor_x) { // Collapse selection to start
				current->cursor_x = current->highlight_anchor < current->cursor_x ? current->highlight_anchor : current->cursor_x;
			}
			else if (current->cursor_x > 0) {
				int n = current->tab_switch_enabled ? 0 : ui_tab_dedent_count(text, current->cursor_x);
				if (n > 0) {
					current->cursor_x -= n; // Tab
				}
				else {
					current->cursor_x--;
				}
			}
		}
		else if (current->key_code == KEY_CODE_RIGHT) {
			if (current->is_ctrl_down) { // Jump to next word
				int i   = current->cursor_x;
				int len = strlen(text);
				while (i < len && text[i] != ' ')
					i++;
				while (i < len && text[i] == ' ')
					i++;
				current->cursor_x = i;
			}
			else if (!current->is_shift_down && current->highlight_anchor != current->cursor_x) { // Collapse selection to end
				current->cursor_x = current->highlight_anchor > current->cursor_x ? current->highlight_anchor : current->cursor_x;
			}
			else if (current->cursor_x < strlen(text)) {
				int n = current->tab_switch_enabled ? 0 : ui_tab_indent_count(text, current->cursor_x, strlen(text));
				if (n > 0) {
					current->cursor_x += n; // Tab
				}
				else {
					current->cursor_x++;
				}
			}
		}
		else if (editable && current->key_code == KEY_CODE_BACKSPACE) { // Remove char
			if (current->is_ctrl_down) {                                // Remove previous word
				int i = current->cursor_x;
				while (i > 0 && text[i - 1] == ' ')
					i--;
				while (i > 0 && text[i - 1] != ' ')
					i--;
				ui_remove_chars_at(text, i, current->cursor_x - i);
				current->cursor_x         = i;
				current->highlight_anchor = i;
			}
			else if (current->cursor_x > 0 && current->highlight_anchor == current->cursor_x) {
				int n = current->tab_switch_enabled ? 0 : ui_tab_dedent_count(text, current->cursor_x);
				if (n > 0) {
					ui_remove_chars_at(text, current->cursor_x - n, n); // Tab
					current->cursor_x -= n;
				}
				else {
					ui_remove_char_at(text, current->cursor_x - 1);
					current->cursor_x--;
				}
			}
			else if (current->highlight_anchor < current->cursor_x) {
				int count = current->cursor_x - current->highlight_anchor;
				ui_remove_chars_at(text, current->highlight_anchor, count);
				current->cursor_x = current->highlight_anchor;
			}
			else {
				int count = current->highlight_anchor - current->cursor_x;
				ui_remove_chars_at(text, current->cursor_x, count);
			}
		}
		else if (editable && current->key_code == KEY_CODE_DELETE) {
			if (current->is_ctrl_down) { // Remove next word
				int i   = current->cursor_x;
				int len = strlen(text);
				while (i < len && text[i] != ' ')
					i++;
				while (i < len && text[i] == ' ')
					i++;
				ui_remove_chars_at(text, current->cursor_x, i - current->cursor_x);
				current->highlight_anchor = current->cursor_x;
			}
			else if (current->highlight_anchor == current->cursor_x) {
				int len = strlen(text);
				int n   = current->tab_switch_enabled ? 0 : ui_tab_indent_count(text, current->cursor_x, len);
				if (n > 0) {
					ui_remove_chars_at(text, current->cursor_x, n); // Tab
				}
				else {
					ui_remove_char_at(text, current->cursor_x);
				}
			}
			else if (current->highlight_anchor < current->cursor_x) {
				int count = current->cursor_x - current->highlight_anchor;
				ui_remove_chars_at(text, current->highlight_anchor, count);
				current->cursor_x = current->highlight_anchor;
			}
			else {
				int count = current->highlight_anchor - current->cursor_x;
				ui_remove_chars_at(text, current->cursor_x, count);
			}
		}
		else if (current->key_code == KEY_CODE_RETURN) { // Deselect
			ui_deselect_text(current);
		}
		else if (current->key_code == KEY_CODE_ESCAPE) { // Cancel
			strcpy(current->text_selected, current->text_selected_handle->text);
			ui_deselect_text(current);
		}
		else if (current->key_code == KEY_CODE_TAB && current->tab_switch_enabled && !current->is_ctrl_down) { // Next field
			current->tab_pressed = true;
			ui_deselect_text(current);
			current->key_code = 0;
		}
		else if (current->key_code == KEY_CODE_HOME) {
			current->cursor_x = 0;
		}
		else if (current->key_code == KEY_CODE_END) {
			current->cursor_x = strlen(text);
		}
		else if (current->is_ctrl_down && current->is_a_down) { // Select all
			current->cursor_x         = strlen(text);
			current->highlight_anchor = 0;
		}
		else if (editable && // Write
		         current->key_code != KEY_CODE_SHIFT && current->key_code != KEY_CODE_CAPS_LOCK && current->key_code != KEY_CODE_CONTROL &&
		         current->key_code != KEY_CODE_META && current->key_code != KEY_CODE_ALT && current->key_code != KEY_CODE_UP &&
		         current->key_code != KEY_CODE_DOWN && current->key_char >= 32 && (!current->is_ctrl_down || current->is_alt_down) && // AltGr = ctrl+alt
		         !(current->key_code >= KEY_CODE_F1 && current->key_code <= KEY_CODE_F24)) {
			ui_remove_chars_at(text, current->highlight_anchor, current->cursor_x - current->highlight_anchor);
			ui_insert_char_at(text, current->highlight_anchor, current->key_char);

			current->cursor_x = current->cursor_x + 1 > strlen(text) ? strlen(text) : current->cursor_x + 1;
		}
		bool selecting =
		    current->is_shift_down && (current->key_code == KEY_CODE_LEFT || current->key_code == KEY_CODE_RIGHT || current->key_code == KEY_CODE_SHIFT ||
		                               current->key_code == KEY_CODE_HOME || current->key_code == KEY_CODE_END);
		// isCtrlDown && isAltDown is the condition for AltGr was pressed
		// AltGr is part of the German keyboard layout and part of key combinations like AltGr + e -> €
		bool ctrl_nav = current->is_ctrl_down && !current->is_shift_down &&
		                (current->key_code == KEY_CODE_LEFT || current->key_code == KEY_CODE_RIGHT || current->key_code == KEY_CODE_HOME ||
		                 current->key_code == KEY_CODE_END);
		if (!selecting && (ctrl_nav || !current->is_ctrl_down || (current->is_ctrl_down && current->is_alt_down))) {
			current->highlight_anchor = current->cursor_x;
		}
	}

	if (editable && ui_is_paste) { // Process cut copy paste
		ui_remove_chars_at(text, current->highlight_anchor, current->cursor_x - current->highlight_anchor);
		ui_insert_chars_at(text, current->highlight_anchor, ui_text_to_paste);
		current->cursor_x += strlen(ui_text_to_paste);
		current->highlight_anchor = current->cursor_x;
		ui_text_to_paste[0]       = 0;
		ui_is_paste               = false;
	}

	if (ui_is_copy) {
		if (current->highlight_anchor == current->cursor_x) {
			strncpy(ui_text_to_copy, text, 8191); // Copy
			ui_text_to_copy[8191] = '\0';
		}
		else if (current->highlight_anchor < current->cursor_x) {
			int len = current->cursor_x - current->highlight_anchor;
			if (len > 8191)
				len = 8191;
			strncpy(ui_text_to_copy, text + current->highlight_anchor, len);
			ui_text_to_copy[len] = '\0';
		}
		else {
			int len = current->highlight_anchor - current->cursor_x;
			if (len > 8191)
				len = 8191;
			strncpy(ui_text_to_copy, text + current->cursor_x, len);
			ui_text_to_copy[len] = '\0';
		}
	}

	if (editable && ui_is_cut) { // Cut
		if (current->highlight_anchor == current->cursor_x) {
			// No selection - nothing to cut
		}
		else if (current->highlight_anchor < current->cursor_x) {
			ui_remove_chars_at(text, current->highlight_anchor, current->cursor_x - current->highlight_anchor);
			current->cursor_x = current->highlight_anchor;
		}
		else {
			ui_remove_chars_at(text, current->cursor_x, current->highlight_anchor - current->cursor_x);
		}
		ui_is_cut = false;
	}

	float off           = UI_TEXT_OFFSET();
	float line_height   = UI_ELEMENT_H();
	float cursor_height = line_height - current->button_offset_y * 3.0;
	// Draw highlight
	if (current->highlight_anchor != current->cursor_x) {
		float istart = current->cursor_x;
		float iend   = current->highlight_anchor;
		if (current->highlight_anchor < current->cursor_x) {
			istart = current->highlight_anchor;
			iend   = current->cursor_x;
		}

		float hlstrw    = draw_sub_string_width(current->ops->font, current->font_size, text, istart, iend);
		float start_off = draw_sub_string_width(current->ops->font, current->font_size, text, 0, istart);
		float hl_start  = align == UI_ALIGN_LEFT ? current->_x + start_off + off : current->_x + current->_w - hlstrw - off;
		if (align == UI_ALIGN_RIGHT) {
			hl_start -= draw_sub_string_width(current->ops->font, current->font_size, text, iend, strlen(text));
		}
		draw_set_color(theme->HOVER_COL + 0x00202020);
		draw_filled_rect(hl_start, current->_y + current->button_offset_y * 1.5, hlstrw, cursor_height);
	}

	// Draw cursor
	int   str_start  = align == UI_ALIGN_LEFT ? 0 : current->cursor_x;
	int   str_length = align == UI_ALIGN_LEFT ? current->cursor_x : (strlen(text) - current->cursor_x);
	float strw       = draw_sub_string_width(current->ops->font, current->font_size, text, str_start, str_length);
	float cursor_x   = align == UI_ALIGN_LEFT ? current->_x + strw + off : current->_x + current->_w - strw - off;
	draw_set_color(theme->TEXT_COL); // Cursor
	draw_filled_rect(cursor_x, current->_y + current->button_offset_y * 1.5, 1.0 * UI_SCALE(), cursor_height);
	current->cursor_screen_x = cursor_x;
	current->cursor_screen_y = current->_y;

	strcpy(current->text_selected, text);
	if (live_update && current->text_selected_handle != NULL) {
		current->text_selected_handle->changed = strcmp(current->text_selected_handle->text, current->text_selected) != 0;
		current->text_selected_handle->text    = string_copy(current->text_selected);
	}
}

void ui_set_hovered_tab_name(char *name) {
	if (ui_input_in_rect(current->_window_x, current->_window_y, current->_window_w, current->_window_h)) {
		strcpy(current->hovered_tab_name, name);
		current->hovered_tab_x = current->_window_x;
		current->hovered_tab_y = current->_window_y;
		current->hovered_tab_w = current->_window_w;
		current->hovered_tab_h = current->_window_h;
	}
}

void ui_draw_tabs() {
	current->input_x = current->restore_x;
	current->input_y = current->restore_y;
	if (current->current_window == NULL) {
		return;
	}
	float tab_x                  = 0.0;
	float tab_y                  = 0.0;
	float tab_h_min              = UI_BUTTON_H() * 1.1;
	float header_h               = current->current_window->drag_enabled ? UI_HEADER_DRAG_H() : 0;
	float tab_h                  = (theme->FULL_TABS && current->tab_vertical)
	                                   ? ((current->_window_h - header_h - 20 - current->tab_count * 6 * UI_SCALE()) / current->tab_count)
	                                   : tab_h_min;
	float orig_y                 = current->_y;
	current->_y                  = header_h;
	current->tab_handle->changed = false;

	if (current->is_ctrl_down && current->is_tab_down) { // Next tab
		current->tab_handle->i++;
		if (current->tab_handle->i >= current->tab_count) {
			current->tab_handle->i = 0;
		}
		current->tab_handle->changed = true;
		current->is_tab_down         = false;
	}

	if (current->tab_handle->i >= current->tab_count) {
		current->tab_handle->i = current->tab_count - 1;
	}

	draw_set_color(theme->SEPARATOR_COL); // Tab background
	if (current->tab_vertical) {
		ui_draw_rect(true, false, 5, current->_y + 5, UI_ELEMENT_W() * 1.2 - 10, current->_window_h - header_h - 10);
	}
	else {
		draw_filled_rect(0, current->_y, current->_window_w, current->button_offset_y + tab_h + 2);
	}

	draw_set_color(theme->BUTTON_COL); // Underline tab buttons
	if (!current->tab_vertical) {
		draw_filled_rect(current->button_offset_y, current->_y + current->button_offset_y + tab_h + 2, current->_window_w - current->button_offset_y * 2.0, 1);
	}

	float base_y   = current->tab_vertical ? current->_y + 10 : current->_y + 2;
	bool  _enabled = current->enabled;

	for (int i = 0; i < current->tab_count; ++i) {
		current->enabled = current->tab_enabled[i];
		current->_x      = current->tab_vertical ? tab_x + UI_ELEMENT_W() * 0.1 : tab_x;
		current->_y      = base_y + tab_y;
		current->_w =
		    current->tab_vertical ? UI_ELEMENT_W()
		    : theme->FULL_TABS
		        ? (current->_window_w / current->tab_count)
		        : (draw_string_width(current->ops->font, current->font_size, current->tab_names[i]) + current->button_offset_y * 2.0 + 18.0 * UI_SCALE());
		if (current->tab_align_right) {
			current->_x = current->_window_w - current->_w - current->_x;
		}
		bool released = ui_get_released(tab_h);
		bool started  = ui_get_started(tab_h);
		bool pushed   = ui_get_pushed(tab_h);
		bool hover    = ui_get_hover(tab_h);
		if (ui_on_tab_drop != NULL) {
			if (started) {
				current->drag_tab_handle   = current->tab_handle;
				current->drag_tab_position = i;
			}
			if (current->drag_tab_handle != NULL && hover && current->input_released) {
				ui_on_tab_drop(current->tab_handle, i, current->drag_tab_handle, current->drag_tab_position);
				current->tab_handle->i = i;
			}
		}
		if (released) {
			ui_handle_t *h                   = ui_nest(current->tab_handle, current->tab_handle->i); // Restore tab scroll
			h->scroll_offset                 = current->current_window->scroll_offset;
			h                                = ui_nest(current->tab_handle, i);
			current->tab_scroll              = h->scroll_offset;
			current->tab_handle->i           = i; // Set new tab
			current->current_window->redraws = 3;
			current->tab_handle->changed     = true;
		}
		bool selected = current->tab_handle->i == i;

		draw_set_color((pushed || hover)                                                ? theme->HOVER_COL
		               : (current->tab_colors[i] != -1 && current->tab_colors[i] != -2) ? current->tab_colors[i]
		               : selected                                                       ? theme->WINDOW_BG_COL
		                                                                                : theme->SEPARATOR_COL);
		if (current->tab_vertical) {
			tab_y += tab_h + 6 * UI_SCALE();
		}
		else {
			tab_x += current->_w + 1;
		}

		int x = current->_x + current->button_offset_y;
		int y = current->_y + current->button_offset_y;
		int w = current->_w;
		int h = tab_h;

		int r = current->filled_round_corner_image.width;

		if (current->tab_vertical) {
			ui_draw_rect(true, selected, x, y, w, h);
		}
		else {
			draw_scaled_image(&current->filled_round_corner_image, x, y, r, r);
			draw_scaled_image(&current->filled_round_corner_image, x + w, y, -r, r);
			draw_filled_rect(x + r, y, w - r * 2.0, h);
			draw_filled_rect(x, y + r, w, h - r);
			if (selected) {
				uint32_t color = draw_get_color();
				draw_set_color(color + 0x00030303);
				draw_filled_rect(x, y + r, 1, h - r * 2.0);
				draw_filled_rect(x + w - 1, y + r, 1, h - r * 2.0);
				draw_set_color(color + 0x00080808);
				draw_filled_rect(x + r, y, w - r * 2.0, 1);
			}
		}

		draw_set_color(theme->TEXT_COL);
		if (current->tab_colors[i] == -2) { // Faded
			ui_fade_color(0.25);
		}
		else if (!selected) {
			ui_fade_color(0.65);
		}
		ui_draw_string(current->tab_names[i], theme->TEXT_OFFSET, (tab_h - tab_h_min) / 2.0,
		               (theme->FULL_TABS || !current->tab_vertical) ? UI_ALIGN_CENTER : UI_ALIGN_LEFT, true);

		if (selected) {
			if (!current->tab_vertical) {
				// Hide underline
				draw_set_color(theme->WINDOW_BG_COL);
				draw_filled_rect(current->_x + current->button_offset_y, current->_y + current->button_offset_y + tab_h, current->_w, 1);
			}
		}
	}

	current->enabled = _enabled;
	ui_set_hovered_tab_name(current->tab_names[current->tab_handle->i]);

	current->_x = 0; // Restore positions
	current->_y = orig_y;
	current->_w = !current->current_window->scroll_enabled ? current->_window_w : current->_window_w - UI_SCROLL_W();
}

void ui_draw_arrow(bool selected, bool align_right) {
	float x = current->_x + current->arrow_offset_x;
	float y = current->_y + current->arrow_offset_y;
	draw_set_color(theme->LABEL_COL);
	if (selected) {
		draw_filled_triangle(x, y, x + UI_ARROW_SIZE() + 0.1, y, x + UI_ARROW_SIZE() / 2.0, y + UI_ARROW_SIZE());
	}
	else {
		if (align_right) {
			draw_filled_triangle(x + UI_ARROW_SIZE(), y - 1, x + UI_ARROW_SIZE(), y + UI_ARROW_SIZE() + 1, x, y + UI_ARROW_SIZE() / 2.0);
		}
		else {
			draw_filled_triangle(x + UI_ARROW_SIZE(), y - 0.1, x, y + UI_ARROW_SIZE() + 0.1, x + UI_ARROW_SIZE(), y + UI_ARROW_SIZE() / 2.0);
		}
	}
}

void ui_draw_tree(bool selected) {
	float SIGN_W = 7.0 * UI_SCALE();
	float x      = current->_x + current->arrow_offset_x + 1;
	float y      = current->_y + current->arrow_offset_y + 1;
	draw_set_color(theme->TEXT_COL);
	if (selected) {
		draw_filled_rect(x, y + SIGN_W / 2.0 - 1, SIGN_W, SIGN_W / 7.0);
	}
	else {
		draw_filled_rect(x, y + SIGN_W / 2.0 - 1, SIGN_W, SIGN_W / 7.0);
		draw_filled_rect(x + SIGN_W / 2.0 - 1, y, SIGN_W / 7.0, SIGN_W);
	}
}

void ui_draw_check(bool selected, bool hover) {
	float x = current->_x + current->check_offset_x;
	float y = current->_y + current->check_offset_y;

	draw_set_color(selected ? theme->HIGHLIGHT_COL : theme->PRESSED_COL);
	ui_draw_rect(true, true, x, y, UI_CHECK_SIZE(), UI_CHECK_SIZE()); // Bg

	draw_set_color(hover ? theme->HOVER_COL : theme->BUTTON_COL);
	ui_draw_rect(false, true, x, y, UI_CHECK_SIZE(), UI_CHECK_SIZE()); // Bg

	if (selected) { // Check
		draw_set_color(hover ? theme->TEXT_COL : theme->LABEL_COL);
		if (!current->enabled) {
			ui_fade_color(0.25);
		}
		int size = UI_CHECK_SELECT_SIZE();
		draw_scaled_image(&current->check_select_image, x + current->check_select_offset_x, y + current->check_select_offset_y, size, size);
	}
}

void ui_draw_radio(bool selected, bool hover) {
	float x = current->_x + current->radio_offset_x;
	float y = current->_y + current->radio_offset_y;
	draw_set_color(selected ? theme->HIGHLIGHT_COL : hover ? theme->HOVER_COL : theme->BUTTON_COL);
	draw_image(&current->radio_image, x, y); // Circle bg

	if (selected) { // Check
		draw_set_color(theme->LABEL_COL);
		if (!current->enabled) {
			ui_fade_color(0.25);
		}
		draw_image(&current->radio_select_image, x + current->radio_select_offset_x, y + current->radio_select_offset_y); // Circle
	}
}

void ui_draw_slider(float value, float from, float to, bool filled, bool hover) {
	float x = current->_x + current->button_offset_y;
	float y = current->_y + current->button_offset_y;
	float w = current->_w - current->button_offset_y * 2.0;

	draw_set_color(theme->PRESSED_COL);
	ui_draw_rect(true, true, x, y, w, UI_BUTTON_H()); // Bg

	if (hover) {
		draw_set_color(theme->HOVER_COL);
		ui_draw_rect(false, true, x, y, w, UI_BUTTON_H()); // Bg
	}

	draw_set_color(hover ? theme->HOVER_COL : theme->BUTTON_COL);
	float offset   = (value - from) / (to - from);
	float bar_w    = 8.0 * UI_SCALE(); // Unfilled bar
	float slider_x = filled ? x : x + (w - bar_w) * offset;
	slider_x       = fmax(fmin(slider_x, x + (w - bar_w)), x);
	float slider_w = filled ? w * offset : bar_w;
	slider_w       = fmax(fmin(slider_w, w), 0);
	ui_draw_rect(true, true, slider_x, y, slider_w, UI_BUTTON_H());
}

void ui_set_scale(float factor) {
	current->ops->scale_factor     = factor;
	current->font_size             = UI_FONT_SIZE();
	float font_height              = draw_font_height(current->ops->font, current->font_size);
	current->font_offset_y         = (UI_ELEMENT_H() - font_height) / 2.0; // Precalculate offsets
	current->arrow_offset_y        = (UI_ELEMENT_H() - UI_ARROW_SIZE()) / 2.0;
	current->arrow_offset_x        = UI_ARROW_SIZE() * 1.5;
	current->title_offset_x        = (current->arrow_offset_y * 2.0 + UI_ARROW_SIZE()) / UI_SCALE();
	current->button_offset_y       = (UI_ELEMENT_H() - UI_BUTTON_H()) / 2.0;
	current->check_offset_y        = (UI_ELEMENT_H() - UI_CHECK_SIZE()) / 2.0;
	current->check_offset_x        = current->check_offset_y;
	current->check_select_offset_y = (UI_CHECK_SIZE() - UI_CHECK_SELECT_SIZE()) / 2.0;
	current->check_select_offset_x = current->check_select_offset_y;
	current->radio_offset_y        = (UI_ELEMENT_H() - UI_CHECK_SIZE()) / 2.0;
	current->radio_offset_x        = current->radio_offset_y;
	current->radio_select_offset_y = (UI_CHECK_SIZE() - UI_CHECK_SELECT_SIZE()) / 2.0;
	current->radio_select_offset_x = current->radio_select_offset_y;
	current->elements_baked        = false;
}

void ui_init(ui_t *ui, ui_options_t *ops) {
	memset(ui, 0, sizeof(ui_t));
	ui->ops = ops;
	ui_set_current(ui);
	ui_set_scale(ops->scale_factor);
	current->enabled             = true;
	current->scroll_enabled      = true;
	current->highlight_on_select = true;
	current->tab_switch_enabled  = true;
	current->input_enabled       = true;
	current->current_ratio       = -1;
	current->image_scroll_align  = true;
	current->window_ended        = true;
	current->restore_x           = -1;
	current->restore_y           = -1;
	current->input_x             = -1;
	current->input_y             = -1;
	if (ui_combo_search_handle == NULL) {
		ui_combo_search_handle = ui_handle_create();
		gc_root(ui_combo_search_handle);
	}
	if (_ui_row2 == NULL) {
		_ui_row2 = f32_array_create_from_raw((float[]){1.0 / 2.0, 1.0 / 2.0}, 2);
		_ui_row3 = f32_array_create_from_raw((float[]){1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0}, 3);
		_ui_row4 = f32_array_create_from_raw((float[]){1.0 / 4.0, 1.0 / 4.0, 1.0 / 4.0, 1.0 / 4.0}, 4);
		_ui_row5 = f32_array_create_from_raw((float[]){1.0 / 5.0, 1.0 / 5.0, 1.0 / 5.0, 1.0 / 5.0, 1.0 / 5.0}, 5);
		_ui_row6 = f32_array_create_from_raw((float[]){1.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0}, 6);
		_ui_row7 = f32_array_create_from_raw((float[]){1.0 / 7.0, 1.0 / 7.0, 1.0 / 7.0, 1.0 / 7.0, 1.0 / 7.0, 1.0 / 7.0, 1.0 / 7.0}, 7);
		gc_root(_ui_row2);
		gc_root(_ui_row3);
		gc_root(_ui_row4);
		gc_root(_ui_row5);
		gc_root(_ui_row6);
		gc_root(_ui_row7);
	}
}

void ui_begin(ui_t *ui) {
	ui_set_current(ui);
	if (!current->elements_baked) {
		ui_bake_elements();
	}
	current->changed = false;
	current->_x      = 0; // Reset cursor
	current->_y      = 0;
	current->_w      = 0;
	current->_h      = 0;
}

// Sticky region ignores window scrolling
void ui_begin_sticky() {
	current->sticky = true;
	current->_y -= current->current_window->scroll_offset;
	if (current->current_window->scroll_enabled) {
		current->_w += UI_SCROLL_W(); // Use full width since there is no scroll bar in sticky region
	}
}

void ui_end_sticky() {
	current->sticky  = false;
	current->scissor = true;
	draw_scissor(0, current->_y, current->_window_w, current->_window_h - current->_y);
	current->window_header_h += current->_y - current->window_header_h;
	current->_y += current->current_window->scroll_offset;
	current->is_hovered = false;
	if (current->current_window->scroll_enabled) {
		current->_w -= UI_SCROLL_W();
	}
}

void ui_end_window() {
	ui_handle_t *handle = current->current_window;
	if (handle == NULL)
		return;
	if (handle->redraws > 0 || current->is_scrolling) {
		if (current->scissor) {
			current->scissor = false;
			gpu_disable_scissor();
		}

		if (current->tab_count > 0) {
			ui_draw_tabs();
		}

		if (handle->drag_enabled) { // Draggable header
			draw_set_color(theme->SEPARATOR_COL);
			ui_draw_rect_round_top(true, 0, 0, current->_window_w, UI_HEADER_DRAG_H());

			if (handle->text != NULL) { // Window title
				draw_set_color(theme->TEXT_COL);
				draw_string(handle->text, current->_window_w / 2 - draw_string_width(current->ops->font, current->font_size, handle->text) / 2,
				            10 * UI_SCALE());
			}
		}

		float window_size = handle->layout == UI_LAYOUT_VERTICAL ? current->_window_h - current->window_header_h
		                                                         : current->_window_w - current->window_header_w; // Exclude header
		float full_size   = handle->layout == UI_LAYOUT_VERTICAL ? current->_y - current->window_header_h : current->_x - current->window_header_w;
		full_size -= handle->scroll_offset;

		if (full_size < window_size || !current->scroll_enabled) { // Disable scrollbar
			handle->scroll_enabled = false;
			handle->scroll_offset  = 0;
		}
		else { // Draw window scrollbar if necessary
			if (handle->layout == UI_LAYOUT_VERTICAL) {
				handle->scroll_enabled = true;
			}
			if (current->tab_scroll < 0) { // Restore tab
				handle->scroll_offset = current->tab_scroll;
				current->tab_scroll   = 0;
			}
			float wy               = current->_window_y + current->window_header_h;
			float amount_to_scroll = full_size - window_size;
			float amount_scrolled  = -handle->scroll_offset;
			float ratio            = amount_scrolled / amount_to_scroll;
			float bar_h            = window_size * fabs(window_size / full_size);
			bar_h                  = fmax(bar_h, UI_ELEMENT_H());

			float total_scrollable_area = window_size - bar_h;
			float e                     = amount_to_scroll / total_scrollable_area;
			float bar_y                 = total_scrollable_area * ratio + current->window_header_h;
			bool  bar_focus = ui_input_in_rect(current->_window_x + current->_window_w - UI_SCROLL_W(), bar_y + current->_window_y, UI_SCROLL_W(), bar_h);

			if (handle->scroll_enabled && current->input_started && bar_focus) { // Start scrolling
				current->scroll_handle = handle;
				current->is_scrolling  = true;
			}

			float scroll_delta = current->input_wheel_delta;
			if (handle->scroll_enabled && ui_touch_control && current->input_down && current->input_dy != 0 &&
			    current->input_x > current->_window_x + current->window_header_w && current->input_y > current->_window_y + current->window_header_h) {
				current->is_scrolling = true;
				scroll_delta          = -(current->input_dy / 20.0) * ui_touch_speed;
			}
			if (handle == current->scroll_handle) { // Scroll
				ui_scroll(current->input_dy * e);
			}
			else if (scroll_delta != 0 && current->combo_selected_handle == NULL &&
			         ui_input_in_rect(current->_window_x, wy, current->_window_w, window_size)) { // Wheel
				ui_scroll(scroll_delta * UI_ELEMENT_H());
			}

			// Stay in bounds
			if (handle->scroll_offset > 0) {
				handle->scroll_offset = 0;
			}
			else if (full_size + handle->scroll_offset < window_size) {
				handle->scroll_offset = window_size - full_size;
			}

			if (handle->layout == UI_LAYOUT_VERTICAL) {
				draw_set_color(theme->BUTTON_COL); // Bar
				bool  scrollbar_focus = ui_input_in_rect(current->_window_x + current->_window_w - UI_SCROLL_W(), wy, UI_SCROLL_W(), window_size);
				float bar_w           = (scrollbar_focus || handle == current->scroll_handle) ? UI_SCROLL_W() : UI_SCROLL_MINI_W();
				ui_draw_rect(true, true, current->_window_w - bar_w - current->scroll_align, bar_y, bar_w, bar_h);
			}
		}

		handle->last_max_x = current->_x;
		handle->last_max_y = current->_y;
		if (handle->layout == UI_LAYOUT_VERTICAL) {
			handle->last_max_x += current->_window_w;
		}
		else {
			handle->last_max_y += current->_window_h;
		}
		handle->redraws--;
		draw_end();
	}

	current->window_ended = true;

	// Draw window texture
	draw_begin(NULL, false, 0);
	draw_set_color(0xffffffff);
	draw_image(&handle->texture, current->_window_x, current->_window_y);
	if (handle->redraws <= 0) {
		handle->redraws--;
	}
	draw_end();
}

bool ui_window_dirty(ui_handle_t *handle, int x, int y, int w, int h) {
	float wx            = x + handle->drag_x;
	float wy            = y + handle->drag_y;
	float input_changed = ui_input_in_rect(wx, wy, w, h) && ui_input_changed();
	return current->always_redraw || current->is_scrolling || input_changed;
}

bool ui_window(ui_handle_t *handle, int x, int y, int w, int h, bool drag) {
	if (handle->texture.width == 0 || w != handle->texture.width || h != handle->texture.height) {
		ui_resize(handle, w, h);
	}

	if (!current->window_ended) {
		ui_end_window(); // End previous window if necessary
	}
	current->window_ended    = false;
	current->current_window  = handle;
	current->_window_x       = x + handle->drag_x;
	current->_window_y       = y + handle->drag_y;
	current->_window_w       = w;
	current->_window_h       = h;
	current->window_header_w = 0;
	current->window_header_h = 0;

	if (ui_window_dirty(handle, x, y, w, h)) {
		handle->redraws = 2;
	}

	if (ui_on_border_hover != NULL) {
		if (ui_input_in_rect(current->_window_x - 4, current->_window_y, 8, current->_window_h)) {
			ui_on_border_hover(handle, 0);
		}
		else if (ui_input_in_rect(current->_window_x + current->_window_w - 4, current->_window_y, 8, current->_window_h)) {
			ui_on_border_hover(handle, 1);
		}
		else if (ui_input_in_rect(current->_window_x, current->_window_y - 4, current->_window_w, 8)) {
			ui_on_border_hover(handle, 2);
		}
		else if (ui_input_in_rect(current->_window_x, current->_window_y + current->_window_h - 4, current->_window_w, 8)) {
			ui_on_border_hover(handle, 3);
		}
	}

	if (handle->redraws <= 0) {
		return false;
	}

	if (handle->layout == UI_LAYOUT_VERTICAL) {
		current->_x = 0;
		current->_y = handle->scroll_offset;
	}
	else {
		current->_x = handle->scroll_offset;
		current->_y = 0;
	}
	if (handle->layout == UI_LAYOUT_HORIZONTAL) {
		w = UI_ELEMENT_W();
	}
	current->_w              = !handle->scroll_enabled ? w : w - UI_SCROLL_W(); // Exclude scrollbar if present
	current->_h              = h;
	current->tooltip_text[0] = 0;
	current->tooltip_img     = NULL;
	current->tab_count       = 0;

	// Fill window bg
	if (drag) {
		draw_begin(&handle->texture, true, 0x00000000);
		draw_set_color(theme->WINDOW_BG_COL);
		ui_draw_rect(true, true, 0, 0, w, h);
	}
	else {
		draw_begin(&handle->texture, true, theme->WINDOW_BG_COL);
	}

	handle->drag_enabled = drag;
	if (drag) {
		if (current->input_started && ui_input_in_rect(current->_window_x, current->_window_y, current->_window_w, UI_HEADER_DRAG_H())) {
			current->drag_handle = handle;
		}
		else if (current->input_released) {
			current->drag_handle = NULL;
		}
		if (handle == current->drag_handle) {
			handle->redraws = 2;
			handle->drag_x += current->input_dx;
			handle->drag_y += current->input_dy;
		}
		current->_y += UI_HEADER_DRAG_H(); // Header offset
		current->window_header_h += UI_HEADER_DRAG_H();
	}

	return true;
}

bool ui_button(char *text, int align, char *label) {
	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		return false;
	}
	bool released = ui_get_released(UI_ELEMENT_H());
	bool pushed   = ui_get_pushed(UI_ELEMENT_H());
	bool hover    = ui_get_hover(UI_ELEMENT_H());
	if (released) {
		current->changed = true;
	}

	if (theme->FILL_BUTTON_BG || pushed || hover) {
		draw_set_color(pushed ? theme->PRESSED_COL : (!theme->FILL_BUTTON_BG && hover) ? theme->HIGHLIGHT_COL : hover ? theme->HOVER_COL : theme->BUTTON_COL);
		ui_draw_rect(true, theme->SHADOWS, current->_x + current->button_offset_y, current->_y + current->button_offset_y,
		             current->_w - current->button_offset_y * 2, UI_BUTTON_H());
	}

	draw_set_color(theme->TEXT_COL);
	ui_draw_string(text, theme->TEXT_OFFSET, 0, align, true);
	if (label != NULL) {
		draw_set_color(theme->LABEL_COL);
		ui_draw_string(label, theme->TEXT_OFFSET, 0, align == UI_ALIGN_RIGHT ? UI_ALIGN_LEFT : UI_ALIGN_RIGHT, true);
	}

	ui_end_element();
	return released;
}

void ui_split_text(char *lines, int align, int bg) {
	int count = ui_line_count(lines);
	for (int i = 0; i < count; ++i) {
		ui_text(ui_extract_line(lines, i), align, bg);
	}
}

int ui_text(char *text, int align, int bg) {
	if (ui_line_count(text) > 1) {
		ui_split_text(text, align, bg);
		return UI_STATE_IDLE;
	}
	float h = fmax(UI_ELEMENT_H(), draw_font_height(current->ops->font, current->font_size));
	if (!ui_is_visible(h)) {
		ui_end_element_of_size(h + UI_ELEMENT_OFFSET());
		return UI_STATE_IDLE;
	}
	bool started  = ui_get_started(h);
	bool down     = ui_get_pushed(h);
	bool released = ui_get_released(h);
	/*bool hover    =*/ui_get_hover(h);
	if (bg != 0x0000000) {
		draw_set_color(bg);
		draw_filled_rect(current->_x + current->button_offset_y, current->_y + current->button_offset_y, current->_w - current->button_offset_y * 2,
		                 UI_BUTTON_H());
	}
	draw_set_color(theme->TEXT_COL);
	ui_draw_string(text, theme->TEXT_OFFSET, 0, align, true);

	ui_end_element_of_size(h + UI_ELEMENT_OFFSET());
	return started ? UI_STATE_STARTED : released ? UI_STATE_RELEASED : down ? UI_STATE_DOWN : UI_STATE_IDLE;
}

bool ui_tab(ui_handle_t *handle, char *text, bool vertical, uint32_t color, bool align_right) {
	if (current->tab_count == 0) { // First tab
		current->tab_handle      = handle;
		current->tab_vertical    = vertical;
		current->tab_align_right = align_right;
		if (vertical) {
			current->_w -= UI_ELEMENT_OFFSET() + UI_ELEMENT_W() * 1.2; // Shrink window area by width of vertical tabs
			current->window_header_w += UI_ELEMENT_W() * 1.2;
		}
		else {
			current->window_header_h += UI_BUTTON_H() + current->button_offset_y + UI_ELEMENT_OFFSET();
		}
		current->restore_x = current->input_x; // Mouse in tab header, disable clicks for tab content
		current->restore_y = current->input_y;
		if (!vertical && ui_input_in_rect(current->_window_x, current->_window_y, current->_window_w, current->window_header_h)) {
			current->input_x = current->input_y = -1;
		}
		if (vertical) {
			current->_x += current->window_header_w + 6;
			current->_w -= 6;
		}
		else {
			current->_y += current->window_header_h + 3;
		}
	}
	assert(current->tab_count < 16);
	strcpy(current->tab_names[current->tab_count], text);
	current->tab_colors[current->tab_count]  = color;
	current->tab_enabled[current->tab_count] = current->enabled;
	current->tab_count++;
	return handle->i == current->tab_count - 1;
}

bool ui_panel(ui_handle_t *handle, char *text, bool is_tree, bool filled, bool align_right) {
	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		return handle->b;
	}
	if (ui_get_released(UI_ELEMENT_H())) {
		handle->b       = !handle->b;
		handle->changed = current->changed = true;
	}

	if (filled) {
		draw_set_color(theme->SEPARATOR_COL);
		draw_filled_rect(current->_x + current->button_offset_y, current->_y + current->button_offset_y, current->_w - current->button_offset_y * 2,
		                 UI_BUTTON_H());
	}

	if (is_tree) {
		ui_draw_tree(handle->b);
	}
	else {
		ui_draw_arrow(handle->b, align_right);
	}

	draw_set_color(theme->LABEL_COL); // Title
	ui_draw_string(text, current->title_offset_x, 0, UI_ALIGN_LEFT, true);

	ui_end_element();
	return handle->b;
}

int ui_sub_image(gpu_texture_t *image, uint32_t tint, int h, int sx, int sy, int sw, int sh) {
	float iw     = (sw > 0 ? sw : image->width) * UI_SCALE();
	float ih     = (sh > 0 ? sh : image->height) * UI_SCALE();
	float w      = fmin(iw, current->_w);
	float x      = current->_x;
	float scroll = current->current_window != NULL ? current->current_window->scroll_enabled : false;
	float r      = current->current_ratio == -1 ? 1.0 : ui_get_ratio(current->ratios, current->current_ratio, 1);
	if (current->image_scroll_align) { // Account for scrollbar size
		w = fmin(iw, current->_w - current->button_offset_y * 2.0);
		x += current->button_offset_y;
		if (!scroll) {
			w -= UI_SCROLL_W() * r;
			x += UI_SCROLL_W() * r / 2.0;
		}
	}
	else if (scroll) {
		w += UI_SCROLL_W() * r;
	}

	// Image size
	float ratio = h == -1 ? w / iw : h / ih;
	if (h == -1) {
		h = ih * ratio;
	}
	else {
		w = iw * ratio;
	}

	if (!ui_is_visible(h)) {
		ui_end_element_of_size(h);
		return UI_STATE_IDLE;
	}
	bool started  = ui_get_started(h);
	bool down     = ui_get_pushed(h);
	bool released = ui_get_released(h);
	bool hover    = ui_get_hover(h);

	draw_set_color(tint);
	if (!current->enabled) {
		ui_fade_color(0.25);
	}
	if (sw > 0) { // Source rect specified
		draw_scaled_sub_image(image, sx, sy, sw, sh, x, current->_y, w, h);
	}
	else {
		draw_scaled_image(image, x, current->_y, w, h);
	}

	ui_end_element_of_size(h);
	return started ? UI_STATE_STARTED : released ? UI_STATE_RELEASED : down ? UI_STATE_DOWN : hover ? UI_STATE_HOVERED : UI_STATE_IDLE;
}

int ui_image(gpu_texture_t *image, uint32_t tint, int h) {
	return ui_sub_image(image, tint, h, 0, 0, image->width, image->height);
}

char *ui_text_input(ui_handle_t *handle, char *label, int align, bool editable, bool live_update) {
	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		return handle->text;
	}

	bool hover = ui_get_hover(UI_ELEMENT_H());
	if (hover) {
		iron_mouse_set_cursor(IRON_CURSOR_IBEAM);
	}
	draw_set_color(hover ? theme->HOVER_COL : theme->BUTTON_COL); // Text bg
	ui_draw_rect(false, true, current->_x + current->button_offset_y, current->_y + current->button_offset_y, current->_w - current->button_offset_y * 2,
	             UI_BUTTON_H());

	bool released = ui_get_released(UI_ELEMENT_H());
	if (current->submit_text_handle == handle && released) { // Keep editing selected text
		current->is_typing            = true;
		current->text_selected_handle = current->submit_text_handle;
		current->submit_text_handle   = NULL;
		ui_set_cursor_to_input(align);
	}
	bool start_edit = released || current->tab_pressed;
	handle->changed = false;

	if (current->text_selected_handle != handle && start_edit) {
		ui_start_text_edit(handle, align);
	}
	if (current->text_selected_handle == handle) {
		ui_update_text_edit(align, editable, live_update);
		if (current->current_window != NULL) {
			current->current_window->redraws = 2; // Keep redrawing window while typing
		}
	}
	if (current->submit_text_handle == handle) {
		ui_submit_text_edit();
	}

	if (label[0] != '\0') {
		draw_set_color(theme->LABEL_COL); // Label
		int label_align = align == UI_ALIGN_RIGHT ? UI_ALIGN_LEFT : UI_ALIGN_RIGHT;
		ui_draw_string(label, label_align == UI_ALIGN_LEFT ? theme->TEXT_OFFSET : 0, 0, label_align, true);
	}

	draw_set_color(theme->TEXT_COL); // Text
	if (current->text_selected_handle != handle) {
		ui_draw_string(handle->text, theme->TEXT_OFFSET, 0, align, true);
	}
	else {
		ui_draw_string(current->text_selected, theme->TEXT_OFFSET, 0, align, false);
	}

	ui_end_element();
	return handle->text;
}

bool ui_check(ui_handle_t *handle, char *text, char *label) {
	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		return handle->b;
	}
	if (ui_get_released(UI_ELEMENT_H())) {
		handle->b       = !handle->b;
		handle->changed = current->changed = true;
	}
	else
		handle->changed = false;

	bool hover = ui_get_hover(UI_ELEMENT_H());
	ui_draw_check(handle->b, hover); // Check

	draw_set_color(theme->TEXT_COL); // Text
	ui_draw_string(text, current->title_offset_x, 0, UI_ALIGN_LEFT, true);

	if (label[0] != '\0') {
		draw_set_color(theme->LABEL_COL);
		ui_draw_string(label, theme->TEXT_OFFSET, 0, UI_ALIGN_RIGHT, true);
	}

	ui_end_element();

	return handle->b;
}

bool ui_radio(ui_handle_t *handle, int position, char *text, char *label) {
	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		return handle->i == position;
	}
	if (position == 0) {
		handle->changed = false;
	}
	if (ui_get_released(UI_ELEMENT_H())) {
		handle->i       = position;
		handle->changed = current->changed = true;
	}

	bool hover = ui_get_hover(UI_ELEMENT_H());
	ui_draw_radio(handle->i == position, hover); // Radio

	draw_set_color(theme->TEXT_COL); // Text
	ui_draw_string(text, current->title_offset_x, 0, UI_ALIGN_LEFT, true);

	if (label[0] != '\0') {
		draw_set_color(theme->LABEL_COL);
		ui_draw_string(label, theme->TEXT_OFFSET, 0, UI_ALIGN_RIGHT, true);
	}

	ui_end_element();

	return handle->i == position;
}

int ui_combo(ui_handle_t *handle, string_array_t *texts, char *label, bool show_label, int align, bool search_bar) {
	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		return handle->i;
	}
	if (ui_get_released(UI_ELEMENT_H())) {
		if (current->combo_selected_handle == NULL) {
			current->input_enabled                 = false;
			current->combo_selected_handle         = handle;
			current->combo_selected_window         = current->current_window;
			current->combo_selected_align          = align;
			current->combo_selected_texts          = texts;
			current->combo_selected_images         = NULL;
			current->combo_selected_label          = label;
			current->combo_selected_x              = current->_x + current->_window_x;
			current->combo_selected_y              = current->_y + current->_window_y + UI_ELEMENT_H();
			current->combo_selected_w              = current->_w;
			current->combo_selected_texts_filtered = 0;
			current->combo_search_bar              = search_bar;
			for (int i = 0; i < texts->length; ++i) { // Adapt combo list width to combo item width
				int w = (int)draw_string_width(current->ops->font, current->font_size, texts->buffer[i]) + 10;
				if (current->combo_selected_w < w) {
					current->combo_selected_w = w;
				}
			}
			if (current->combo_selected_w > current->_w * 2.0) {
				current->combo_selected_w = current->_w * 2.0;
			}
			if (current->combo_selected_w > current->_w) {
				current->combo_selected_w += UI_TEXT_OFFSET();
			}
			current->combo_to_submit     = handle->i;
			current->combo_initial_value = handle->i;
		}
	}
	if (handle == current->submit_combo_handle) {
		handle->i                    = current->combo_to_submit;
		current->submit_combo_handle = NULL;
		handle->changed = current->changed = true;
	}
	else {
		handle->changed = false;
	}

	draw_set_color(theme->PRESSED_COL); // Bg
	ui_draw_rect(true, true, current->_x + current->button_offset_y, current->_y + current->button_offset_y, current->_w - current->button_offset_y * 2,
	             UI_BUTTON_H());

	bool hover = ui_get_hover(UI_ELEMENT_H());
	draw_set_color(hover ? theme->HOVER_COL : theme->BUTTON_COL);
	ui_draw_rect(false, true, current->_x + current->button_offset_y, current->_y + current->button_offset_y, current->_w - current->button_offset_y * 2,
	             UI_BUTTON_H());

	int x = current->_x + current->_w - current->arrow_offset_x - 8;
	int y = current->_y + current->arrow_offset_y + 3;
	draw_set_color(theme->HOVER_COL);
	draw_filled_triangle(x, y, x + UI_ARROW_SIZE(), y, x + UI_ARROW_SIZE() / 2.0, y + UI_ARROW_SIZE() / 2.0);

	if (show_label && label[0] != '\0') {
		if (align == UI_ALIGN_LEFT) {
			current->_x -= 15;
		}
		draw_set_color(theme->LABEL_COL);
		ui_draw_string(label, theme->TEXT_OFFSET, 0, align == UI_ALIGN_LEFT ? UI_ALIGN_RIGHT : UI_ALIGN_LEFT, true);
		if (align == UI_ALIGN_LEFT) {
			current->_x += 15;
		}
	}

	if (align == UI_ALIGN_RIGHT) {
		current->_x -= 15;
	}
	draw_set_color(theme->TEXT_COL); // Value
	if (handle->i < texts->length) {
		ui_draw_string(texts->buffer[handle->i], theme->TEXT_OFFSET, 0, align, true);
	}
	if (align == UI_ALIGN_RIGHT) {
		current->_x += 15;
	}

	ui_end_element();
	return handle->i;
}

float ui_slider(ui_handle_t *handle, char *text, float from, float to, bool filled, float precision, bool display_value, int align, bool text_edit) {
	static char temp[1024];
	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		return handle->f;
	}
	if (ui_get_started(UI_ELEMENT_H())) {
		current->scroll_handle = handle;
		current->is_scrolling  = true;
		current->changed = handle->changed = true;
		if (ui_touch_control) {
			current->slider_tooltip   = true;
			current->slider_tooltip_x = current->_x + current->_window_x;
			current->slider_tooltip_y = current->_y + current->_window_y;
			current->slider_tooltip_w = current->_w;
		}
	}
	else {
		handle->changed = false;
	}

#if !defined(IRON_ANDROID) && !defined(IRON_IOS)
	if (handle == current->scroll_handle && current->input_dx != 0) { // Scroll
#else
	if (handle == current->scroll_handle) { // Scroll
#endif
		float range    = to - from;
		float slider_x = current->_x + current->_window_x + current->button_offset_y;
		float slider_w = current->_w - current->button_offset_y * 2;
		float step     = range / slider_w;
		float value    = from + (current->input_x - slider_x) * step;
		handle->f      = round(value * precision) / precision;
		if (handle->f < from) {
			handle->f = from; // Stay in bounds
		}
		else if (handle->f > to) {
			handle->f = to;
		}
		handle->changed = current->changed = true;
	}

	bool hover = ui_get_hover(UI_ELEMENT_H());
	ui_draw_slider(handle->f, from, to, filled, hover); // Slider

	// Text edit
	bool start_edit = (ui_get_released(UI_ELEMENT_H()) || current->tab_pressed) && text_edit;
	if (start_edit) { // Mouse did not move
		char tmp[256];
		sprintf(tmp, "%.2f", handle->f);
		handle->text = string_copy(tmp);
		string_strip_trailing_zeros(handle->text);
		ui_start_text_edit(handle, UI_ALIGN_LEFT);
		handle->changed = current->changed = true;
	}
	int lalign = align == UI_ALIGN_LEFT ? UI_ALIGN_RIGHT : UI_ALIGN_LEFT;
	if (current->text_selected_handle == handle) {
		ui_update_text_edit(lalign, true, false);
	}
	if (current->submit_text_handle == handle) {
		ui_submit_text_edit();
#ifdef WITH_EVAL
		if (handle->text[0] == '.') {
			handle->text = string("0%s", handle->text);
		}
		minic_ctx_t *_ctx = minic_eval(string("float main() { return %s; }", handle->text));
		handle->f         = minic_ctx_result(_ctx);
		minic_ctx_free(_ctx);
#else
		handle->f = atof(handle->text);
#endif
		handle->changed = current->changed = true;
	}

	draw_set_color(theme->LABEL_COL); // Text
	ui_draw_string(text, theme->TEXT_OFFSET, 0, align, true);

	if (display_value) {
		draw_set_color(theme->TEXT_COL); // Value
		if (current->text_selected_handle != handle) {
			sprintf(temp, "%.2f", round(handle->f * precision) / precision);
			string_strip_trailing_zeros(temp);
			ui_draw_string(temp, theme->TEXT_OFFSET, 0, lalign, true);
		}
		else {
			ui_draw_string(current->text_selected, theme->TEXT_OFFSET, 0, lalign, true);
		}
	}

	ui_end_element();
	return handle->f;
}

void ui_separator(int h, bool fill) {
	if (!ui_is_visible(UI_ELEMENT_H())) {
		current->_y += h * UI_SCALE();
		return;
	}
	if (fill) {
		draw_set_color(theme->SEPARATOR_COL);
		draw_filled_rect(current->_x, current->_y, current->_w, h * UI_SCALE());
	}
	current->_y += h * UI_SCALE();
}

void ui_tooltip(char *text) {
	assert(strlen(text) < 512);
	strcpy(current->tooltip_text, text);
	current->tooltip_y = current->_y + current->_window_y;
}

void ui_tooltip_image(gpu_texture_t *image, int max_width) {
	current->tooltip_img           = image;
	current->tooltip_img_max_width = max_width;
	current->tooltip_y             = current->_y + current->_window_y;
}

void ui_end() {
	if (!current->window_ended) {
		ui_end_window();
	}
	current->tab_pressed_handle = NULL;
}

void ui_set_input_position(ui_t *ui, int x, int y) {
	ui->input_dx += x - ui->input_x;
	ui->input_dy += y - ui->input_y;
	ui->input_x = x;
	ui->input_y = y;
}

// Useful for drag and drop operations
char *ui_hovered_tab_name() {
	return ui_input_in_rect(current->hovered_tab_x, current->hovered_tab_y, current->hovered_tab_w, current->hovered_tab_h) ? current->hovered_tab_name : "";
}

void ui_mouse_down(ui_t *ui, int button, int x, int y) {
	if (ui->pen_in_use) {
		return;
	}
	if (button == 0) {
		ui->input_started = ui->input_down = true;
	}
	else {
		ui->input_started_r = ui->input_down_r = true;
	}
	ui->input_started_time = iron_time();
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	ui_set_input_position(ui, x, y);
#endif
	ui->input_started_x = x;
	ui->input_started_y = y;
}

void ui_mouse_move(ui_t *ui, int x, int y, int movement_x, int movement_y) {
#if !defined(IRON_ANDROID) && !defined(IRON_IOS)
	ui_set_input_position(ui, x, y);
#endif
}

void ui_mouse_up(ui_t *ui, int button, int x, int y) {
	if (ui->pen_in_use) {
		return;
	}

	if (ui->touch_hold_activated) {
		ui->touch_hold_activated = false;
		return;
	}

	if (ui->is_scrolling) { // Prevent action when scrolling is active
		ui->is_scrolling   = false;
		ui->scroll_handle  = NULL;
		ui->slider_tooltip = false;
		if (x == ui->input_started_x && y == ui->input_started_y) { // Mouse not moved
			if (button == 0)
				ui->input_released = true;
			else
				ui->input_released_r = true;
		}
	}
	else {
		if (button == 0) {
			ui->input_released = true;
		}
		else {
			ui->input_released_r = true;
		}
	}

	if (button == 0) {
		ui->input_down = false;
	}
	else {
		ui->input_down_r = false;
	}
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	ui_set_input_position(ui, x, y);
#endif
	ui_deselect_text(ui);
}

void ui_mouse_wheel(ui_t *ui, float delta) {
	ui->input_wheel_delta = delta;
}

void ui_pen_down(ui_t *ui, int x, int y, float pressure) {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	return;
#endif

	ui_mouse_down(ui, 0, x, y);
}

void ui_pen_up(ui_t *ui, int x, int y, float pressure) {
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	return;
#endif

	if (ui->input_started) {
		ui->input_started = false;
		ui->pen_in_use    = true;
		return;
	}
	ui_mouse_up(ui, 0, x, y);
	ui->pen_in_use = true; // On pen release, additional mouse down & up events are fired at once - filter those out
}

void ui_pen_move(ui_t *ui, int x, int y, float pressure) {
#if defined(IRON_IOS)
	// Listen to pen hover if no other input is active
	if (pressure == 0.0) {
		if (!ui->input_down && !ui->input_down_r) {
			ui_set_input_position(ui, x, y);
		}
		return;
	}
#endif

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	return;
#endif

	ui_mouse_move(ui, x, y, 0, 0);
}

void ui_key_down(ui_t *ui, int key_code) {
	ui->key_code       = key_code;
	ui->is_key_pressed = true;
	ui->is_key_down    = true;
	ui_key_repeat_time = iron_time() + 0.4;
	switch (key_code) {
	case KEY_CODE_SHIFT:
		ui->is_shift_down = true;
		break;
	case KEY_CODE_CONTROL:
		ui->is_ctrl_down = true;
		break;
#ifdef IRON_DARWIN
	case KEY_CODE_META:
		ui->is_ctrl_down = true;
		break;
#endif
	case KEY_CODE_ALT:
		ui->is_alt_down = true;
		break;
	case KEY_CODE_BACKSPACE:
		ui->is_backspace_down = true;
		break;
	case KEY_CODE_DELETE:
		ui->is_delete_down = true;
		break;
	case KEY_CODE_ESCAPE:
		ui->is_escape_down = true;
		break;
	case KEY_CODE_RETURN:
		ui->is_return_down = true;
		break;
	case KEY_CODE_TAB:
		ui->is_tab_down = true;
		break;
	case KEY_CODE_A:
		ui->is_a_down = true;
		break;
	case KEY_CODE_SPACE:
		ui->key_char = ' ';
		break;
#ifdef IRON_ANDROID // Detect right mouse button on Android..
	case KEY_CODE_BACK:
		if (!ui->input_down_r)
			ui_mouse_down(ui, 1, ui->input_x, ui->input_y);
		break;
#endif
	}
}

void ui_key_up(ui_t *ui, int key_code) {
	ui->is_key_down = false;
	switch (key_code) {
	case KEY_CODE_SHIFT:
		ui->is_shift_down = false;
		break;
	case KEY_CODE_CONTROL:
		ui->is_ctrl_down = false;
		break;
#ifdef IRON_DARWIN
	case KEY_CODE_META:
		ui->is_ctrl_down = false;
		break;
#endif
	case KEY_CODE_ALT:
		ui->is_alt_down = false;
		break;
	case KEY_CODE_BACKSPACE:
		ui->is_backspace_down = false;
		break;
	case KEY_CODE_DELETE:
		ui->is_delete_down = false;
		break;
	case KEY_CODE_ESCAPE:
		ui->is_escape_down = false;
		break;
	case KEY_CODE_RETURN:
		ui->is_return_down = false;
		break;
	case KEY_CODE_TAB:
		ui->is_tab_down = false;
		break;
	case KEY_CODE_A:
		ui->is_a_down = false;
		break;
#ifdef IRON_ANDROID
	case KEY_CODE_BACK:
		ui_mouse_down(ui, 1, ui->input_x, ui->input_y);
		break;
#endif
	}
}

void ui_key_press(ui_t *ui, unsigned key_char) {
	ui->key_char       = key_char;
	ui->is_key_pressed = true;
}

#if defined(IRON_ANDROID) || defined(IRON_IOS)
static float ui_pinch_distance = 0.0;
static float ui_pinch_total    = 0.0;
static float ui_pinch_smooth   = 0.0;

void ui_touch_down(ui_t *ui, int index, int x, int y) {
	// Reset movement delta on touch start
	if (index == 0) {
		ui->input_dx = 0;
		ui->input_dy = 0;
		ui->input_x  = x;
		ui->input_y  = y;
	}
	// Two fingers down - right mouse button
	else if (index == 1) {
		ui->input_down = false;
		ui_mouse_down(ui, 1, ui->input_x, ui->input_y);
		ui_pinch_total    = 0.0;
		ui_pinch_distance = 0.0;
	}
	// Three fingers down - middle mouse button
	else if (index == 2) {
		ui->input_down_r = false;
		ui_mouse_down(ui, 2, ui->input_x, ui->input_y);
	}
}

void ui_touch_up(ui_t *ui, int index, int x, int y) {
	if (index == 1) {
		ui_mouse_up(ui, 1, ui->input_x, ui->input_y);
		ui_pinch_distance = 0.0;
		ui_pinch_total    = 0.0;
		ui_pinch_smooth   = 0.0;
	}
}

void ui_touch_move(ui_t *ui, int index, int x, int y) {
	if (index == 0) {
		ui_set_input_position(ui, x, y);
	}

	// Pinch to zoom - mouse wheel
	if (index == 1) {
		float dx           = ui->input_x - x;
		float dy           = ui->input_y - y;
		float new_distance = sqrtf(dx * dx + dy * dy);
		if (ui_pinch_distance == 0.0) {
			ui_pinch_distance = new_distance;
			return;
		}
		float delta            = ui_pinch_distance - new_distance;
		ui_pinch_distance      = new_distance;
		float smoothing_factor = 0.3;
		ui_pinch_smooth        = ui_pinch_smooth * smoothing_factor + delta * (1.0 - smoothing_factor);
		ui_pinch_total += ui_pinch_smooth;
		float deadzone = 2.0;
		if (fabs(ui_pinch_total) > deadzone) {
			ui->input_wheel_delta = (ui_pinch_total / 50.0) * ui_touch_speed;
			ui_pinch_total        = 0.0;
		}
	}
}
#endif

char *ui_copy() {
	ui_is_copy = true;
	return &ui_text_to_copy[0];
}

char *ui_cut() {
	ui_is_cut = true;
	return ui_copy();
}

void ui_paste(char *s) {
	ui_is_paste = true;
	strncpy(ui_text_to_paste, s, 8192);
	ui_text_to_paste[8191] = 0;
}

void ui_theme_default(ui_theme_t *t) {
	t->WINDOW_BG_COL     = 0xff262626;
	t->HOVER_COL         = 0xff383838;
	t->BUTTON_COL        = 0xff323232;
	t->PRESSED_COL       = 0xff212121;
	t->TEXT_COL          = 0xfff7f7f7;
	t->LABEL_COL         = 0xffcfcfcf;
	t->SEPARATOR_COL     = 0xff202020;
	t->HIGHLIGHT_COL     = 0xff205d9c;
	t->FONT_SIZE         = 13;
	t->ELEMENT_W         = 100;
	t->ELEMENT_H         = 26;
	t->ELEMENT_OFFSET    = 4;
	t->ARROW_SIZE        = 5;
	t->BUTTON_H          = 24;
	t->CHECK_SIZE        = 16;
	t->CHECK_SELECT_SIZE = 12;
	t->SCROLL_W          = 9;
	t->SCROLL_MINI_W     = 3;
	t->TEXT_OFFSET       = 8;
	t->TAB_W             = 6;
	t->FILL_BUTTON_BG    = true;
	t->FULL_TABS         = false;
	t->SHADOWS           = true;
	t->LINK_STYLE        = UI_LINK_STYLE_LINE;
	t->VIEWPORT_COL      = 0xff070707;
}
