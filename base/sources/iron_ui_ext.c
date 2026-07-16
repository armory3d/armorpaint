#include "iron_draw.h"
#include "iron_gc.h"
#include "iron_string.h"
#include "iron_system.h"
#include "iron_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ui_handle_t *wheel_selected_handle    = NULL;
static ui_handle_t *gradient_selected_handle = NULL;
static ui_handle_t  radio_handle;
static int          _ELEMENT_OFFSET               = 0;
static int          _BUTTON_COL                   = 0;
static bool         _SHADOWS                      = false;
static int          text_area_selection_start     = -1;
static int          text_area_selection_start_col = 0;
bool                ui_text_area_line_numbers     = false;
bool                ui_text_area_scroll_past_end  = false;
ui_text_coloring_t *ui_text_area_coloring         = NULL;
bool (*ui_picker_button)(void)                    = NULL;

float ui_dist(float x1, float y1, float x2, float y2) {
	float vx = x1 - x2;
	float vy = y1 - y2;
	return sqrtf(vx * vx + vy * vy);
}

float ui_fract(float f) {
	return f - (int)f;
}

float ui_mix(float x, float y, float a) {
	return x * (1.0 - a) + y * a;
}

float ui_clamp(float x, float min_val, float max_val) {
	return fmin(fmax(x, min_val), max_val);
}

float ui_step(float edge, float x) {
	return x < edge ? 0.0 : 1.0;
}

static const float kx = 1.0;
static const float ky = 2.0 / 3.0;
static const float kz = 1.0 / 3.0;
static const float kw = 3.0;
void               ui_hsv_to_rgb(float cr, float cg, float cb, float *out) {
    float px = fabs(ui_fract(cr + kx) * 6.0 - kw);
    float py = fabs(ui_fract(cr + ky) * 6.0 - kw);
    float pz = fabs(ui_fract(cr + kz) * 6.0 - kw);
    out[0]   = cb * ui_mix(kx, ui_clamp(px - kx, 0.0, 1.0), cg);
    out[1]   = cb * ui_mix(kx, ui_clamp(py - kx, 0.0, 1.0), cg);
    out[2]   = cb * ui_mix(kx, ui_clamp(pz - kx, 0.0, 1.0), cg);
}

static const float Kx = 0.0;
static const float Ky = -1.0 / 3.0;
static const float Kz = 2.0 / 3.0;
static const float Kw = -1.0;
static const float e  = 1.0e-10;
void               ui_rgb_to_hsv(float cr, float cg, float cb, float *out) {
    float px = ui_mix(cb, cg, ui_step(cb, cg));
    float py = ui_mix(cg, cb, ui_step(cb, cg));
    float pz = ui_mix(Kw, Kx, ui_step(cb, cg));
    float pw = ui_mix(Kz, Ky, ui_step(cb, cg));
    float qx = ui_mix(px, cr, ui_step(px, cr));
    float qy = ui_mix(py, py, ui_step(px, cr));
    float qz = ui_mix(pw, pz, ui_step(px, cr));
    float qw = ui_mix(cr, px, ui_step(px, cr));
    float d  = qx - fmin(qw, qy);
    out[0]   = fabs(qz + (qw - qy) / (6.0 * d + e));
    out[1]   = d / (qx + e);
    out[2]   = qx;
}

float ui_float_input(ui_handle_t *handle, char *label, int align, float precision) {
	char tmp[256];
	handle->text = tmp;
	sprintf(handle->text, "%f", round(handle->f * precision) / precision);
	char *text = ui_text_input(handle, label, align, true, false);
	handle->f  = atof(text);
	return handle->f;
}

int ui_inline_radio(ui_handle_t *handle, string_array_t *texts, int align) {
	ui_t *current = ui_get_current();

	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		return handle->i;
	}
	float step    = current->_w / texts->length;
	int   hovered = -1;
	if (ui_get_hover(UI_ELEMENT_H())) {
		int ix = current->input_x - current->_x - current->_window_x;
		for (int i = 0; i < texts->length; ++i) {
			if (ix < i * step + step) {
				hovered = i;
				break;
			}
		}
	}
	if (ui_get_released(UI_ELEMENT_H())) {
		handle->i       = hovered;
		handle->changed = current->changed = true;
	}
	else {
		handle->changed = false;
	}

	ui_theme_t *theme = ui_get_current()->ops->theme;
	for (int i = 0; i < texts->length; ++i) {
		if (handle->i == i) {
			draw_set_color(theme->HIGHLIGHT_COL);
			if (!current->enabled) {
				ui_fade_color(0.25);
			}
			ui_draw_rect(true, true, current->_x + step * i, current->_y + current->button_offset_y, step, UI_BUTTON_H());
		}
		else if (hovered == i) {
			draw_set_color(theme->BUTTON_COL);
			if (!current->enabled) {
				ui_fade_color(0.25);
			}
			ui_draw_rect(false, true, current->_x + step * i, current->_y + current->button_offset_y, step, UI_BUTTON_H());
		}
		draw_set_color(theme->TEXT_COL); // Text
		current->_x += step * i;
		float _w    = current->_w;
		current->_w = (int)step;
		ui_draw_string(texts->buffer[i], theme->TEXT_OFFSET, 0, align, true);
		current->_x -= step * i;
		current->_w = _w;
	}
	ui_end_element();
	return handle->i;
}

uint8_t ui_color_r(uint32_t color) {
	return (color & 0x00ff0000) >> 16;
}

uint8_t ui_color_g(uint32_t color) {
	return (color & 0x0000ff00) >> 8;
}

uint8_t ui_color_b(uint32_t color) {
	return (color & 0x000000ff);
}

uint8_t ui_color_a(uint32_t color) {
	return (color) >> 24;
}

uint32_t ui_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return (a << 24) | (r << 16) | (g << 8) | b;
}

bool _ui_picker_button() {
	return ui_button("P", UI_ALIGN_CENTER, "");
}

int ui_color_wheel(ui_handle_t *handle, bool alpha, float w, float h, bool color_preview, void (*picker)(void *), void *data) {
	ui_t *current = ui_get_current();
	if (w < 0) {
		w = current->_w;
	}

	float r = ui_color_r(handle->color) / 255.0f;
	float g = ui_color_g(handle->color) / 255.0f;
	float b = ui_color_b(handle->color) / 255.0f;
	if (fabs(handle->red - r) > 0.01 || fabs(handle->green - g) > 0.01 || fabs(handle->blue - b) > 0.01) {
		handle->red   = r;
		handle->green = g;
		handle->blue  = b;
		ui_rgb_to_hsv(r, g, b, &handle->hue);
	}

	// Wheel
	float px     = current->_x;
	float py     = current->_y;
	bool  scroll = current->current_window != NULL ? current->current_window->scroll_enabled : false;
	if (!scroll) {
		w -= UI_SCROLL_W();
		px += UI_SCROLL_W() / 2.0;
	}
	float _x    = current->_x;
	float _y    = current->_y;
	float _w    = current->_w;
	current->_w = (int)(28.0 * UI_SCALE());
	if (ui_picker_button == NULL) {
		ui_picker_button = &_ui_picker_button;
	}
	if (picker != NULL && ui_picker_button()) {
		(*picker)(data);
		current->changed = false;
		handle->changed  = false;
		return handle->color;
	}
	current->_x = _x;
	current->_y = _y;
	current->_w = _w;

	ui_theme_t *theme   = current->ops->theme;
	uint32_t    col     = ui_color(round(handle->val * 255.0f), round(handle->val * 255.0f), round(handle->val * 255.0f), 255);
	float       wheel_h = current->ops->color_wheel->height * (w / current->ops->color_wheel->width);
	ui_image(current->ops->color_wheel, col, wheel_h - 2);

	// Picker
	float ph      = current->_y - py;
	float ox      = px + w / 2.0;
	float oy      = py + ph / 2.0;
	float cw      = w * 0.7;
	float cwh     = cw / 2.0;
	float cx      = ox;
	float cy      = oy + handle->sat * cwh; // Sat is distance from center
	float grad_tx = px + 0.897 * w;
	float grad_ty = oy - cwh;
	float grad_w  = 0.0777 * w;
	float grad_h  = cw;
	// Rotate around origin by hue
	float theta = handle->hue * (IRON_PI * 2.0);
	float cx2   = cos(theta) * (cx - ox) - sin(theta) * (cy - oy) + ox;
	float cy2   = sin(theta) * (cx - ox) + cos(theta) * (cy - oy) + oy;
	cx          = cx2;
	cy          = cy2;

	current->_x = px - (scroll ? 0 : UI_SCROLL_W() / 2.0);
	current->_y = py;
	ui_image(current->ops->black_white_gradient, 0xffffffff, wheel_h);

	draw_set_color(0xff000000);
	draw_filled_rect(cx - 3.0 * UI_SCALE(), cy - 3.0 * UI_SCALE(), 6.0 * UI_SCALE(), 6.0 * UI_SCALE());
	draw_set_color(0xffffffff);
	draw_filled_rect(cx - 2.0 * UI_SCALE(), cy - 2.0 * UI_SCALE(), 4.0 * UI_SCALE(), 4.0 * UI_SCALE());

	draw_set_color(0xff000000);
	draw_filled_rect(grad_tx + grad_w / 2.0 - 3.0 * UI_SCALE(), grad_ty + (1.0 - handle->val) * grad_h - 3.0 * UI_SCALE(), 6.0 * UI_SCALE(), 6.0 * UI_SCALE());
	draw_set_color(0xffffffff);
	draw_filled_rect(grad_tx + grad_w / 2.0 - 2.0 * UI_SCALE(), grad_ty + (1.0 - handle->val) * grad_h - 2.0 * UI_SCALE(), 4.0 * UI_SCALE(), 4.0 * UI_SCALE());

	float a = ui_color_a(handle->color) / 255.0f;
	if (alpha) {
		ui_handle_t *alpha_handle = ui_nest(handle, 1);
		if (alpha_handle->init) {
			alpha_handle->f = round(a * 100.0) / 100.0;
		}
		a = ui_slider(alpha_handle, "Alpha", 0.0, 1.0, true, 100, true, UI_ALIGN_LEFT, true);
		if (alpha_handle->changed) {
			handle->changed = current->changed = true;
		}
	}

	// Mouse picking for color wheel
	float gx = ox + current->_window_x;
	float gy = oy + current->_window_y;
	if (current->input_started && ui_input_in_rect(gx - cwh, gy - cwh, cw, cw)) {
		wheel_selected_handle = handle;
	}
	if (current->input_released && wheel_selected_handle != NULL) {
		wheel_selected_handle = NULL;
		handle->changed = current->changed = true;
	}
	if (current->input_down && wheel_selected_handle == handle) {
		handle->sat = fmin(ui_dist(gx, gy, current->input_x, current->input_y), cwh) / cwh;
		float angle = atan2(current->input_x - gx, current->input_y - gy);
		if (angle < 0) {
			angle = IRON_PI + (IRON_PI - fabs(angle));
		}
		angle           = IRON_PI * 2.0 - angle;
		handle->hue     = angle / (IRON_PI * 2.0);
		handle->changed = current->changed = true;
	}
	// Mouse picking for val
	if (current->input_started && ui_input_in_rect(grad_tx + current->_window_x, grad_ty + current->_window_y, grad_w, grad_h)) {
		gradient_selected_handle = handle;
	}
	if (current->input_released && gradient_selected_handle != NULL) {
		gradient_selected_handle = NULL;
		handle->changed = current->changed = true;
	}
	if (current->input_down && gradient_selected_handle == handle) {
		handle->val     = fmax(0.01, fmin(1.0, 1.0 - (current->input_y - grad_ty - current->_window_y) / grad_h));
		handle->changed = current->changed = true;
	}

	// Save as rgb
	ui_hsv_to_rgb(handle->hue, handle->sat, handle->val, &handle->red);
	handle->color = ui_color(round(handle->red * 255.0), round(handle->green * 255.0), round(handle->blue * 255.0), round(a * 255.0));

	if (color_preview) {
		ui_text("", UI_ALIGN_RIGHT, handle->color);
	}

	char          *strings[] = {"RGB", "HSV", "Hex"};
	string_array_t car;
	car.buffer = strings;
	car.length = 3;
	int pos    = ui_inline_radio(&radio_handle, &car, UI_ALIGN_LEFT);

	ui_handle_t *h0 = ui_nest(ui_nest(handle, 0), 0);
	ui_handle_t *h1 = ui_nest(ui_nest(handle, 0), 1);
	ui_handle_t *h2 = ui_nest(ui_nest(handle, 0), 2);
	if (pos == 0) {
		h0->f         = handle->red;
		h1->f         = handle->green;
		h2->f         = handle->blue;
		handle->red   = ui_slider(h0, "R", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		handle->green = ui_slider(h1, "G", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		handle->blue  = ui_slider(h2, "B", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		ui_rgb_to_hsv(handle->red, handle->green, handle->blue, &handle->hue);
		handle->color = ui_color(round(handle->red * 255.0), round(handle->green * 255.0), round(handle->blue * 255.0), round(a * 255.0));
	}
	else if (pos == 1) {
		h0->f       = handle->hue;
		h1->f       = handle->sat;
		h2->f       = handle->val;
		handle->hue = ui_slider(h0, "H", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		handle->sat = ui_slider(h1, "S", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		handle->val = ui_slider(h2, "V", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		ui_hsv_to_rgb(handle->hue, handle->sat, handle->val, &handle->red);
		handle->color = ui_color(round(handle->red * 255.0), round(handle->green * 255.0), round(handle->blue * 255.0), round(a * 255.0));
	}
	else if (pos == 2) {
		char tmp[16];
		handle->text = tmp;
		sprintf(handle->text, "%x", handle->color);
		char *hex_code = ui_text_input(handle, "#", UI_ALIGN_LEFT, true, false);
		if (strlen(hex_code) >= 1 && hex_code[0] == '#') { // Allow # at the beginning
			hex_code = strcpy(hex_code, hex_code + 1);
		}
		if (strlen(hex_code) == 3) { // 3 digit CSS style values like fa0 --> ffaa00
			hex_code[5] = hex_code[2];
			hex_code[4] = hex_code[2];
			hex_code[3] = hex_code[1];
			hex_code[2] = hex_code[1];
			hex_code[1] = hex_code[0];
			hex_code[6] = '\0';
		}
		if (strlen(hex_code) == 4) { // 4 digit CSS style values
			hex_code[7] = hex_code[3];
			hex_code[6] = hex_code[3];
			hex_code[5] = hex_code[2];
			hex_code[4] = hex_code[2];
			hex_code[3] = hex_code[1];
			hex_code[2] = hex_code[1];
			hex_code[1] = hex_code[0];
			hex_code[8] = '\0';
		}
		if (strlen(hex_code) == 6) { // Make the alpha channel optional
			hex_code[7] = hex_code[5];
			hex_code[6] = hex_code[4];
			hex_code[5] = hex_code[3];
			hex_code[4] = hex_code[2];
			hex_code[3] = hex_code[1];
			hex_code[2] = hex_code[0];
			hex_code[0] = 'f';
			hex_code[1] = 'f';
		}
#ifdef _WIN32
		handle->color = _strtoi64(hex_code, NULL, 16);
#else
		handle->color = strtol(hex_code, NULL, 16);
#endif
	}
	if (h0->changed || h1->changed || h2->changed) {
		handle->changed = current->changed = true;
	}

	// Do not close if user clicks
	if (current->input_released && ui_input_in_rect(current->_window_x + px, current->_window_y + py, w, h < 0 ? (current->_y - py) : h) &&
	    current->input_released) {
		current->changed = true;
	}

	return handle->color;
}

static void scroll_align(ui_t *current, ui_handle_t *handle) {
	// Scroll down
	if ((handle->i + 1) * UI_ELEMENT_H() + current->current_window->scroll_offset > current->_h - current->window_header_h) {
		current->current_window->scroll_offset -= UI_ELEMENT_H();
	}
	// Scroll up
	else if ((handle->i + 1) * UI_ELEMENT_H() + current->current_window->scroll_offset < current->window_header_h) {
		current->current_window->scroll_offset += UI_ELEMENT_H();
	}
}

static char *right_align_number(char *s, int number, int length) {
	sprintf(s, "%d", number);
	while (strlen(s) < length) {
		for (int i = strlen(s) + 1; i > 0; --i) {
			s[i] = s[i - 1];
		}
		s[0] = ' ';
	}
	return s;
}

static void handle_line_select(ui_t *current, ui_handle_t *handle) {
	if (current->is_shift_down) {
		if (text_area_selection_start == -1) {
			text_area_selection_start     = handle->i;
			text_area_selection_start_col = current->cursor_x;
		}
		current->highlight_anchor = 0;
	}
	else
		text_area_selection_start = -1;
}

static int ui_word_count(char *str) {
	if (str == NULL || str[0] == '\0') {
		return 0;
	}
	int i     = 0;
	int count = 1;
	while (str[i] != '\0') {
		if (str[i] == ' ' || str[i] == '\n') {
			count++;
		}
		i++;
	}
	return count;
}

static char temp[128];

static char *ui_extract_word(char *str, int word) {
	int pos    = 0;
	int len    = strlen(str);
	int word_i = 0;
	for (int i = 0; i < len; ++i) {
		if (str[i] == ' ' || str[i] == '\n') {
			word_i++;
			continue;
		}
		if (word_i < word) {
			continue;
		}
		if (word_i > word) {
			break;
		}
		temp[pos++] = str[i];
	}
	temp[pos] = 0;
	return temp;
}

static int ui_line_pos(char *str, int line) {
	int i            = 0;
	int current_line = 0;
	while (str[i] != '\0' && current_line < line) {
		if (str[i] == '\n') {
			current_line++;
		}
		i++;
	}
	return i;
}

static char *lines_buffer = NULL;
static int   lines_size   = 0;

void ui_text_area_word_wrap(char *lines, ui_handle_t *handle, bool selected) {
	ui_t *current    = ui_get_current();
	bool  cursor_set = false;
	int   cursor_pos = current->cursor_x;
	for (int i = 0; i < handle->i; ++i) {
		cursor_pos += strlen(ui_extract_line(lines, i)) + 1; // + '\n'
	}
	bool anchor_set = false;
	int  anchor_pos = current->highlight_anchor;
	for (int i = 0; i < handle->i; ++i) {
		anchor_pos += strlen(ui_extract_line(lines, i)) + 1;
	}
	int  word_count = ui_word_count(lines);
	char line[1024];
	line[0] = '\0';
	char new_lines[4096];
	new_lines[0] = '\0';

	for (int i = 0; i < word_count; ++i) {
		char *w      = ui_extract_word(lines, i);
		float spacew = draw_string_width(current->ops->font, current->font_size, " ");
		float wordw  = spacew + draw_string_width(current->ops->font, current->font_size, w);
		float linew  = wordw + draw_string_width(current->ops->font, current->font_size, line);
		if (linew > current->_w - 10 && linew > wordw) {
			if (new_lines[0] != '\0') {
				strcat(new_lines, "\n");
			}
			strcat(new_lines, line);
			line[0] = '\0';
		}

		if (line[0] == '\0') {
			strcpy(line, w);
		}
		else {
			strcat(line, " ");
			strcat(line, w);
		}

		int new_line_count = new_lines[0] == '\0' ? 0 : ui_line_count(new_lines);
		int lines_len      = new_line_count;
		for (int i = 0; i < new_line_count; ++i) {
			lines_len += strlen(ui_extract_line(new_lines, i));
		}

		if (selected && !cursor_set && cursor_pos <= lines_len + strlen(line)) {
			cursor_set        = true;
			handle->i         = new_line_count;
			current->cursor_x = cursor_pos - lines_len;
		}
		if (selected && !anchor_set && anchor_pos <= lines_len + strlen(line)) {
			anchor_set                = true;
			current->highlight_anchor = anchor_pos - lines_len;
		}
	}
	if (new_lines[0] != '\0') {
		strcat(new_lines, "\n");
	}
	strcat(new_lines, line);
	if (selected) {
		strcpy(handle->text, ui_extract_line(new_lines, handle->i));
		strcpy(current->text_selected, handle->text);
	}
	strcpy(lines, new_lines);
}

void ui_text_area_draw_line_numbers(int line_count) {
	ui_t       *current   = ui_get_current();
	ui_theme_t *theme     = current->ops->theme;
	float       _y        = current->_y;
	int         _TEXT_COL = theme->TEXT_COL;
	theme->TEXT_COL       = theme->HOVER_COL;
	int  max_length       = ceil(log(line_count + 0.5) / log(10)); // Express log_10 with natural log
	char s[64];
	for (int i = 0; i < line_count; ++i) {
		ui_text(right_align_number(&s[0], i + 1, max_length), UI_ALIGN_LEFT, 0x00000000);
		current->_y -= UI_ELEMENT_OFFSET();
	}
	theme->TEXT_COL = _TEXT_COL;
	current->_y     = _y;

	sprintf(s, "%d", line_count);
	float numbers_w = (strlen(s) * 16 + 4) * UI_SCALE();
	current->_x += numbers_w;
	current->_w -= numbers_w - UI_SCROLL_W();
}

char *ui_text_area(ui_handle_t *handle, int align, bool editable, char *label, bool word_wrap) {
	ui_t *current = ui_get_current();
	handle->text  = string_replace_all(handle->text, "\t", "    ");
	bool selected = current->text_selected_handle == handle; // Text being edited

	int text_size = strlen(handle->text) + 1 + 1024;
	if (lines_size < text_size) {
		if (lines_buffer != NULL) {
			free(lines_buffer);
		}
		lines_buffer = malloc(text_size);
		lines_size   = text_size;
	}

	char *lines = lines_buffer;
	strcpy(lines, handle->text);
	int  line_count              = ui_line_count(lines);
	bool show_label              = (line_count == 1 && lines[0] == '\0');
	bool key_pressed             = selected && current->is_key_pressed;
	current->highlight_on_select = false;
	current->tab_switch_enabled  = false;
	if (word_wrap && handle->text[0] != '\0') {
		ui_text_area_word_wrap(lines, handle, selected);
	}
	if (ui_text_area_line_numbers) {
		ui_text_area_draw_line_numbers(line_count);
	}
	int cursor_start_x  = current->cursor_x;
	int active_line_len = selected ? (int)strlen(ui_extract_line(lines, handle->i)) : 0;

	ui_theme_t *theme = current->ops->theme;
	draw_set_color(theme->SEPARATOR_COL); // Background
	ui_draw_rect(true, true, current->_x + current->button_offset_y, current->_y + current->button_offset_y, current->_w - current->button_offset_y * 2,
	             line_count * UI_ELEMENT_H() - current->button_offset_y * 2);

	ui_text_coloring_t *_text_coloring = current->text_coloring;
	current->text_coloring             = ui_text_area_coloring;

	if (current->input_started) {
		text_area_selection_start = -1;
	}

	int  lines_off         = 0;
	int  edit_line_pos     = -1;
	int  edit_line_old_len = 0;
	char edit_new_text[1024];
	edit_new_text[0] = '\0';
	for (int i = 0; i < line_count; ++i) { // Draw lines
		char *line = ui_extract_line_off(lines, 0, &lines_off);
		// Text input
		if ((!selected && ui_get_hover(UI_ELEMENT_H())) || (selected && i == handle->i)) {
			handle->i = i; // Set active line
			strcpy(handle->text, line);
			current->submit_text_handle = NULL;
			// Suppress cut / paste / select-all in ui_update_text_edit for multi-line handling
			bool _is_cut            = ui_is_cut;
			bool _is_copy           = ui_is_copy;
			bool _is_paste          = ui_is_paste;
			bool _is_a_down         = current->is_a_down;
			int  _key_char          = current->key_char;
			int  _key_code          = current->key_code;
			int  _highlight_anchor  = current->highlight_anchor;
			bool _is_key_pressed    = current->is_key_pressed;
			bool paste_is_multiline = ui_is_paste && strchr(ui_text_to_paste, '\n') != NULL;
			if ((text_area_selection_start != -1 && text_area_selection_start != i) || paste_is_multiline) {
				ui_is_cut   = false;
				ui_is_copy  = false;
				ui_is_paste = false;
				// Suppress editing keys for multi-line selection, keep navigation keys active
				if (current->key_code == KEY_CODE_BACKSPACE || current->key_code == KEY_CODE_DELETE || current->key_code == KEY_CODE_RETURN ||
				    current->key_code == KEY_CODE_ESCAPE) {
					current->is_key_pressed = false;
				}
				// Fix highlight for active line: anchor at end-of-line when selection comes from below
				if (text_area_selection_start > i) {
					current->highlight_anchor = (int)strlen(line);
				}
			}
			// When back on the start line during an active shift-selection, restore the original column as anchor
			if (text_area_selection_start == i && current->is_shift_down) {
				current->highlight_anchor = text_area_selection_start_col;
			}
			if (current->is_ctrl_down && current->is_a_down) {
				current->key_char = 0; // Prevent
			}
			ui_text_input(handle, show_label ? label : "", align, editable, false);
			if ((text_area_selection_start != -1 && text_area_selection_start != i) || paste_is_multiline) {
				// Restore flags that were suppressed for multi-line handling
				ui_is_cut                 = _is_cut;
				ui_is_copy                = _is_copy;
				ui_is_paste               = _is_paste;
				current->key_code         = _key_code;
				current->highlight_anchor = _highlight_anchor;
				current->is_key_pressed   = _is_key_pressed;
			}
			current->is_a_down = _is_a_down;
			current->key_char  = _key_char;
			if (selected && current->key_code != KEY_CODE_RETURN && current->key_code != KEY_CODE_ESCAPE &&
			    strcmp(line, current->text_selected) != 0) { // Edit text - defer to after loop
				edit_line_pos     = ui_line_pos(lines, i);
				edit_line_old_len = strlen(line);
				strcpy(edit_new_text, current->text_selected);
			}
		}
		// Text
		else {
			if (show_label) {
				int TEXT_COL    = theme->TEXT_COL;
				theme->TEXT_COL = theme->LABEL_COL;
				ui_text(label, UI_ALIGN_RIGHT, 0x00000000);
				theme->TEXT_COL = TEXT_COL;
			}
			else {
				// Multi-line selection highlight
				if (text_area_selection_start > -1 &&
				    ((i >= text_area_selection_start && i < handle->i) || (i <= text_area_selection_start && i > handle->i))) {
					int   line_height   = UI_ELEMENT_H();
					int   cursor_height = line_height - current->button_offset_y * 3.0;
					float off           = UI_TEXT_OFFSET();
					float hl_x, hl_w;
					if (i == text_area_selection_start && i > handle->i) {
						// Start line is below active line: highlight from 0 to start col
						hl_x = current->_x + off;
						hl_w = draw_sub_string_width(current->ops->font, current->font_size, line, 0, text_area_selection_start_col);
					}
					else if (i == text_area_selection_start && i < handle->i) {
						// Start line is above active line: highlight from start col to end
						float start_off = draw_sub_string_width(current->ops->font, current->font_size, line, 0, text_area_selection_start_col);
						hl_x            = current->_x + off + start_off;
						hl_w            = draw_string_width(current->ops->font, current->font_size, line) - start_off;
					}
					else {
						// Middle lines: highlight full line
						hl_x = current->_x + off;
						hl_w = draw_string_width(current->ops->font, current->font_size, line);
					}
					if (hl_w > 0) {
						draw_set_color(theme->HOVER_COL + 0x00202020);
						draw_filled_rect(hl_x, current->_y + current->button_offset_y * 1.5, hl_w, cursor_height);
					}
				}
				ui_text(line, align, 0x00000000);
			}
		}
		current->_y -= UI_ELEMENT_OFFSET();
	}
	current->_y += UI_ELEMENT_OFFSET();
	current->text_coloring = _text_coloring;

	if (edit_line_pos >= 0) { // Apply edit after loop to avoid flickering
		ui_remove_chars_at(lines, edit_line_pos, edit_line_old_len);
		ui_insert_chars_at(lines, edit_line_pos, edit_new_text);
	}

	// Multi-line paste with no selection
	bool paste_is_multiline_out = ui_is_paste && strchr(ui_text_to_paste, '\n') != NULL;
	if (selected && paste_is_multiline_out && (text_area_selection_start == -1 || text_area_selection_start == handle->i)) {
		int pos = ui_line_pos(lines, handle->i) + current->cursor_x;
		ui_insert_chars_at(lines, pos, ui_text_to_paste);
		int paste_lines = ui_line_count(ui_text_to_paste) - 1;
		handle->i += paste_lines;
		char *last_pasted_line = ui_extract_line(ui_text_to_paste, paste_lines);
		current->cursor_x = current->highlight_anchor = (int)strlen(last_pasted_line);
		text_area_selection_start                     = -1;
		ui_text_to_paste[0]                           = '\0';
		ui_is_paste                                   = false;
		strcpy(current->text_selected, ui_extract_line(lines, handle->i));
		strcpy(handle->text, current->text_selected);
	}

	// Multi-line copy/cut/paste
	if (selected && text_area_selection_start != -1 && text_area_selection_start != handle->i) {
		// Determine ordered selection bounds
		int sel_top_line, sel_bot_line, sel_top_col, sel_bot_col;
		if (text_area_selection_start < handle->i) {
			sel_top_line = text_area_selection_start;
			sel_bot_line = handle->i;
			sel_top_col  = text_area_selection_start_col;
			sel_bot_col  = current->cursor_x;
		}
		else {
			sel_top_line = handle->i;
			sel_bot_line = text_area_selection_start;
			sel_top_col  = current->cursor_x;
			sel_bot_col  = text_area_selection_start_col;
		}

		if (ui_is_copy || ui_is_cut) {
			// Build the selected text spanning multiple lines
			ui_text_to_copy[0] = '\0';
			for (int i = sel_top_line; i <= sel_bot_line; ++i) {
				char *ln        = ui_extract_line(lines, i);
				int   col_start = (i == sel_top_line) ? sel_top_col : 0;
				int   col_end   = (i == sel_bot_line) ? sel_bot_col : (int)strlen(ln);
				int   len       = col_end - col_start;
				if (len > 0) {
					strncat(ui_text_to_copy, ln + col_start, len);
				}
				if (i < sel_bot_line) {
					strcat(ui_text_to_copy, "\n");
				}
			}
			iron_copy_to_clipboard(ui_text_to_copy);
			ui_is_copy = false;
		}
		if (editable && (ui_is_cut || current->key_code == KEY_CODE_BACKSPACE || current->key_code == KEY_CODE_DELETE)) {
			// Delete from sel_top_col on sel_top_line to sel_bot_col on sel_bot_line
			int pos_start = ui_line_pos(lines, sel_top_line) + sel_top_col;
			int pos_end   = ui_line_pos(lines, sel_bot_line) + sel_bot_col;
			ui_remove_chars_at(lines, pos_start, pos_end - pos_start);
			handle->i         = sel_top_line;
			current->cursor_x = current->highlight_anchor = sel_top_col;
			text_area_selection_start                     = -1;
			strcpy(current->text_selected, ui_extract_line(lines, handle->i));
			strcpy(handle->text, current->text_selected);
		}
		if (editable && ui_is_paste) {
			// Delete selected range then insert clipboard
			int pos_start = ui_line_pos(lines, sel_top_line) + sel_top_col;
			int pos_end   = ui_line_pos(lines, sel_bot_line) + sel_bot_col;
			ui_remove_chars_at(lines, pos_start, pos_end - pos_start);
			ui_insert_chars_at(lines, pos_start, ui_text_to_paste);
			// Reposition cursor: count newlines in pasted text
			int paste_lines        = ui_line_count(ui_text_to_paste) - 1;
			handle->i              = sel_top_line + paste_lines;
			char *last_pasted_line = ui_extract_line(ui_text_to_paste, paste_lines);
			current->cursor_x = current->highlight_anchor = sel_top_col + (int)strlen(last_pasted_line);
			text_area_selection_start                     = -1;
			ui_text_to_paste[0]                           = '\0';
			ui_is_paste                                   = false;
			strcpy(current->text_selected, ui_extract_line(lines, handle->i));
			strcpy(handle->text, current->text_selected);
		}
	}

	if (ui_text_area_scroll_past_end) {
		current->_y += current->_h - current->window_header_h - UI_ELEMENT_H() - UI_ELEMENT_OFFSET();
	}

	if (key_pressed) {
		// Move cursor vertically
		if (current->key_code == KEY_CODE_DOWN && handle->i < line_count - 1) {
			handle_line_select(current, handle);
			current->cursor_sticky_x = current->cursor_sticky_x > current->cursor_x ? current->cursor_sticky_x : current->cursor_x;
			handle->i++;
			int next_len      = (int)strlen(ui_extract_line(lines, handle->i));
			current->cursor_x = current->cursor_sticky_x < next_len ? current->cursor_sticky_x : next_len;
			if (!current->is_shift_down) {
				current->highlight_anchor = current->cursor_x;
			}
			scroll_align(current, handle);
		}
		else if (current->key_code == KEY_CODE_UP && handle->i > 0) {
			handle_line_select(current, handle);
			current->cursor_sticky_x = current->cursor_sticky_x > current->cursor_x ? current->cursor_sticky_x : current->cursor_x;
			handle->i--;
			int prev_len      = (int)strlen(ui_extract_line(lines, handle->i));
			current->cursor_x = current->cursor_sticky_x < prev_len ? current->cursor_sticky_x : prev_len;
			if (!current->is_shift_down) {
				current->highlight_anchor = current->cursor_x;
			}
			scroll_align(current, handle);
		}
		else if (current->key_code == KEY_CODE_RIGHT && cursor_start_x == active_line_len && handle->i < line_count - 1 && !current->is_ctrl_down) {
			handle_line_select(current, handle);
			handle->i++;
			current->cursor_x         = 0;
			current->highlight_anchor = 0;
			current->cursor_sticky_x  = 0;
			strcpy(current->text_selected, ui_extract_line(lines, handle->i));
			strcpy(handle->text, current->text_selected);
			scroll_align(current, handle);
		}
		else if (current->key_code == KEY_CODE_LEFT && cursor_start_x == 0 && handle->i > 0 && !current->is_ctrl_down) {
			handle_line_select(current, handle);
			handle->i--;
			int prev_len              = (int)strlen(ui_extract_line(lines, handle->i));
			current->cursor_x         = prev_len;
			current->highlight_anchor = prev_len;
			current->cursor_sticky_x  = prev_len;
			strcpy(current->text_selected, ui_extract_line(lines, handle->i));
			strcpy(handle->text, current->text_selected);
			scroll_align(current, handle);
		}
		else if (current->key_code == KEY_CODE_HOME || current->key_code == KEY_CODE_END) {
			text_area_selection_start = -1; // Home/End are single-line operations, cancel multi-line selection
		}
		else if (current->is_ctrl_down && current->is_a_down) { // Select all lines
			text_area_selection_start     = 0;
			text_area_selection_start_col = 0;
			handle->i                     = line_count - 1;
			current->highlight_anchor     = 0;
			current->cursor_x             = (int)strlen(ui_extract_line(lines, handle->i));
			current->cursor_sticky_x      = current->cursor_x;
			strcpy(current->text_selected, ui_extract_line(lines, handle->i));
			strcpy(handle->text, current->text_selected);
			scroll_align(current, handle);
		}
		else {
			current->cursor_sticky_x = current->cursor_x; // Reset sticky column on any non-vertical move
			if (!current->is_shift_down && !current->is_ctrl_down) {
				text_area_selection_start = -1; // Cancel multi-line selection on non-shift, non-ctrl key
			}
		}
		// New line
		if (editable && current->key_code == KEY_CODE_RETURN && !word_wrap) {
			handle->i++;
			ui_insert_char_at(lines, ui_line_pos(lines, handle->i - 1) + current->cursor_x, '\n');
			// Auto indent
			char *prev_line = ui_extract_line(lines, handle->i - 1);
			int   indent    = 0;
			while (prev_line[indent] == ' ') {
				indent++;
			}
			int new_line_pos = ui_line_pos(lines, handle->i);
			for (int s = 0; s < indent; ++s) {
				ui_insert_char_at(lines, new_line_pos, ' ');
			}
			ui_start_text_edit(handle, UI_ALIGN_LEFT);
			current->cursor_x = current->highlight_anchor = indent;
			scroll_align(current, handle);
		}
		// Delete line
		if (editable && current->key_code == KEY_CODE_BACKSPACE && cursor_start_x == 0 && handle->i > 0 && text_area_selection_start == -1) {
			handle->i--;
			current->cursor_x = current->highlight_anchor = strlen(ui_extract_line(lines, handle->i));
			ui_remove_chars_at(lines, ui_line_pos(lines, handle->i + 1) - 1, 1); // Remove '\n' of the previous line
			scroll_align(current, handle);
		}
		// Delete line
		if (editable && current->key_code == KEY_CODE_DELETE && handle->i < line_count - 1 && cursor_start_x == active_line_len &&
		    text_area_selection_start == -1) {
			current->highlight_anchor = current->cursor_x;
			ui_remove_chars_at(lines, ui_line_pos(lines, handle->i + 1) - 1, 1); // Remove '\n' at end of current line
		}
		// Tab indent
		if (editable && current->key_code == KEY_CODE_TAB) {
			int pos = ui_line_pos(lines, handle->i) + current->cursor_x;
			ui_insert_chars_at(lines, pos, "    ");
			current->cursor_x += 4;
			current->highlight_anchor = current->cursor_x;
			current->cursor_sticky_x  = current->cursor_x;
		}
		strcpy(current->text_selected, ui_extract_line(lines, handle->i));
	}

	current->highlight_on_select = true;
	current->tab_switch_enabled  = true;
	handle->text                 = string_copy(lines);
	return handle->text;
}

float UI_MENUBAR_H() {
	ui_t *current = ui_get_current();
	return UI_BUTTON_H() * 1.1 + 2.0 + current->button_offset_y;
}

void ui_begin_menu() {
	ui_t       *current   = ui_get_current();
	ui_theme_t *theme     = current->ops->theme;
	_ELEMENT_OFFSET       = theme->ELEMENT_OFFSET;
	_BUTTON_COL           = theme->BUTTON_COL;
	_SHADOWS              = theme->SHADOWS;
	theme->ELEMENT_OFFSET = 0;
	theme->BUTTON_COL     = theme->SEPARATOR_COL;
	theme->SHADOWS        = false;
	draw_set_color(theme->SEPARATOR_COL);
	draw_filled_rect(0, 0, current->_window_w, UI_MENUBAR_H());
}

void ui_end_menu() {
	ui_theme_t *theme     = ui_get_current()->ops->theme;
	theme->ELEMENT_OFFSET = _ELEMENT_OFFSET;
	theme->BUTTON_COL     = _BUTTON_COL;
	theme->SHADOWS        = _SHADOWS;
}

bool ui_menubar_button(char *text) {
	ui_t *current = ui_get_current();
	current->_w   = draw_string_width(current->ops->font, current->font_size, text) + 25.0 * UI_SCALE();
	return ui_button(text, UI_ALIGN_CENTER, "");
}

const char *ui_theme_keys[] = {"WINDOW_BG_COL", "HOVER_COL",         "BUTTON_COL", "PRESSED_COL",   "TEXT_COL",       "LABEL_COL",  "SEPARATOR_COL",
                               "HIGHLIGHT_COL", "FONT_SIZE",         "ELEMENT_W",  "ELEMENT_H",     "ELEMENT_OFFSET", "ARROW_SIZE", "BUTTON_H",
                               "CHECK_SIZE",    "CHECK_SELECT_SIZE", "SCROLL_W",   "SCROLL_MINI_W", "TEXT_OFFSET",    "TAB_W",      "FILL_BUTTON_BG",
                               "FULL_TABS",     "SHADOWS",           "LINK_STYLE", "VIEWPORT_COL"};

int ui_theme_keys_count = sizeof(ui_theme_keys) / sizeof(ui_theme_keys[0]);

////

f32 ui_MENUBAR_H(ui_t *ui) {
	f32 button_offset_y = (ui->ops->theme->ELEMENT_H * UI_SCALE() - ui->ops->theme->BUTTON_H * UI_SCALE()) / (float)2;
	return ui->ops->theme->BUTTON_H * UI_SCALE() * 1.1 + 2 + button_offset_y;
}

extern any_map_t *ui_children;

ui_handle_t *ui_handle(char *s) {
	ui_handle_t *h = any_map_get(ui_children, s);
	if (h == NULL) {
		h = ui_handle_create();
		any_map_set(ui_children, s, h);
		return h;
	}
	h->init = false;
	return h;
}

ui_t *ui_create(ui_options_t *ops) {
	ui_t *raw = GC_ALLOC_INIT(ui_t, {0});
	ui_init(raw, ops);
	return raw;
}

ui_theme_t *ui_theme_create() {
	ui_theme_t *raw = GC_ALLOC_INIT(ui_theme_t, {0});
	ui_theme_default(raw);
	return raw;
}

void ui_set_font(ui_t *ui, draw_font_t *font) {
	draw_font_init(font);
	ui->ops->font = font;
}
