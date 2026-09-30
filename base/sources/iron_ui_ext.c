#include "iron_alloc.h"
#include "iron_draw.h"
#include "iron_string.h"
#include "iron_system.h"
#include "iron_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int          _ELEMENT_OFFSET               = 0;
static int          _BUTTON_COL                   = 0;
static bool         _SHADOWS                      = false;
static int          text_area_selection_start     = -1;
static int          text_area_selection_start_col = 0;
static double       text_area_last_click_time     = 0.0;
static int          text_area_last_click_line     = -1;
bool                ui_text_area_line_numbers     = false;
bool                ui_text_area_scroll_past_end  = false;
ui_text_coloring_t *ui_text_area_coloring         = NULL;
char               *ui_text_area_search           = NULL;
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

float ui_float_input(float *value, char *label, int align, float precision) {
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "%f", round(*value * precision) / precision);
	char   *text = buffer;
	ui_id_t id   = ui_widget_id(value, UI_ID_FLOAT_INPUT);
	ui_push_id(id);
	ui_set_next_id(1);
	ui_text_input(&text, label, align, true, false);
	if (ui_item_changed())
		*value = ui_parse_float(text);
	if (text != buffer)
		free(text);
	ui_pop_id();
	return *value;
}

int ui_slider_int(int *value, char *text, float from, float to, bool filled, bool display_value, int align, bool text_edit) {
	ui_t *current = ui_get_current();
	float f       = *value;
	if (current->next_id == 0)
		ui_set_next_id((ui_id_t)value); // Keep the id of the int instead of the temporary float
	ui_slider(&f, text, from, to, filled, 1, display_value, align, text_edit);
	*value = (int)f;
	return *value;
}

int ui_inline_radio(int *value, string_array_t *texts, int align) {
	ui_t *current         = ui_get_current();
	current->item_changed = false;

	if (!ui_is_visible(UI_ELEMENT_H())) {
		ui_end_element();
		ui_record_change();
		return (*value);
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
		(*value)              = hovered;
		current->item_changed = current->changed = true;
	}
	else {
		current->item_changed = false;
	}

	ui_theme_t *theme = ui_get_current()->ops->theme;
	for (int i = 0; i < texts->length; ++i) {
		if ((*value) == i) {
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
	ui_record_change();
	return (*value);
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

int ui_color_wheel(uint32_t *value, ui_color_state_t *state, bool alpha, float w, float h, bool color_preview, void (*picker)(void *), void *data) {
	ui_t   *current  = ui_get_current();
	ui_id_t id       = ui_widget_id(value, UI_ID_COLOR);
	current->next_id = 0;
	ui_push_id(id);
	bool wheel_changed   = false;
	bool channel_changed = false;
	if (w < 0) {
		w = current->_w;
	}

	float r = ui_color_r((*value)) / 255.0f;
	float g = ui_color_g((*value)) / 255.0f;
	float b = ui_color_b((*value)) / 255.0f;
	if (fabs(state->red - r) > 0.01 || fabs(state->green - g) > 0.01 || fabs(state->blue - b) > 0.01) {
		state->red   = r;
		state->green = g;
		state->blue  = b;
		float hsv[3];
		ui_rgb_to_hsv(r, g, b, hsv);
		if (hsv[1] > 0)
			state->hue = hsv[0];
		state->sat = hsv[1];
		state->val = hsv[2];
	}

	// Wheel
	float px     = current->_x;
	float py     = current->_y;
	bool  scroll = current->current_window != 0 ? current->current_window->scroll_enabled : false;
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
	if (picker != 0 && ui_picker_button()) {
		(*picker)(data);
		current->changed = false;
		wheel_changed    = false;
		ui_pop_id();
		current->item_changed = wheel_changed;
		ui_record_change();
		return (*value);
	}
	current->_x = _x;
	current->_y = _y;
	current->_w = _w;

	ui_theme_t *theme   = current->ops->theme;
	uint32_t    col     = ui_color(round(state->val * 255.0f), round(state->val * 255.0f), round(state->val * 255.0f), 255);
	float       wheel_h = current->ops->color_wheel->height * (w / current->ops->color_wheel->width);
	ui_image(current->ops->color_wheel, col, wheel_h - 2);

	// Picker
	float ph      = current->_y - py;
	float ox      = px + w / 2.0;
	float oy      = py + ph / 2.0;
	float cw      = w * 0.7;
	float cwh     = cw / 2.0;
	float cx      = ox;
	float cy      = oy + state->sat * cwh; // Sat is distance from center
	float grad_tx = px + 0.897 * w;
	float grad_ty = oy - cwh;
	float grad_w  = 0.0777 * w;
	float grad_h  = cw;
	// Rotate around origin by hue
	float theta = state->hue * (IRON_PI * 2.0);
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
	draw_filled_rect(grad_tx + grad_w / 2.0 - 3.0 * UI_SCALE(), grad_ty + (1.0 - state->val) * grad_h - 3.0 * UI_SCALE(), 6.0 * UI_SCALE(), 6.0 * UI_SCALE());
	draw_set_color(0xffffffff);
	draw_filled_rect(grad_tx + grad_w / 2.0 - 2.0 * UI_SCALE(), grad_ty + (1.0 - state->val) * grad_h - 2.0 * UI_SCALE(), 4.0 * UI_SCALE(), 4.0 * UI_SCALE());

	float a = ui_color_a((*value)) / 255.0f;
	if (alpha) {
		state->alpha = a;
		a            = ui_slider(&state->alpha, "Alpha", 0.0, 1.0, true, 100, true, UI_ALIGN_LEFT, true);
		wheel_changed |= ui_item_changed();
	}

	// Mouse picking for color wheel
	float gx = ox + current->_window_x;
	float gy = oy + current->_window_y;
	if (current->input_started && ui_input_in_rect(gx - cwh, gy - cwh, cw, cw)) {
		current->color_wheel_id = id;
	}
	if (current->input_released && current->color_wheel_id == id) {
		current->color_wheel_id = 0;
		wheel_changed = current->changed = true;
	}
	if (current->input_down && current->color_wheel_id == id) {
		state->sat  = fmin(ui_dist(gx, gy, current->input_x, current->input_y), cwh) / cwh;
		float angle = atan2(current->input_x - gx, current->input_y - gy);
		if (angle < 0) {
			angle = IRON_PI + (IRON_PI - fabs(angle));
		}
		angle         = IRON_PI * 2.0 - angle;
		state->hue    = angle / (IRON_PI * 2.0);
		wheel_changed = current->changed = true;
	}
	// Mouse picking for val
	if (current->input_started && ui_input_in_rect(grad_tx + current->_window_x, grad_ty + current->_window_y, grad_w, grad_h)) {
		current->color_gradient_id = id;
	}
	if (current->input_released && current->color_gradient_id == id) {
		current->color_gradient_id = 0;
		wheel_changed = current->changed = true;
	}
	if (current->input_down && current->color_gradient_id == id) {
		state->val    = fmax(0.01, fmin(1.0, 1.0 - (current->input_y - grad_ty - current->_window_y) / grad_h));
		wheel_changed = current->changed = true;
	}

	// Save as rgb
	ui_hsv_to_rgb(state->hue, state->sat, state->val, &state->red);
	(*value) = ui_color(round(state->red * 255.0), round(state->green * 255.0), round(state->blue * 255.0), round(a * 255.0));

	if (color_preview) {
		ui_text("", UI_ALIGN_RIGHT, (*value));
	}

	char          *strings[] = {"RGB", "HSV", "Hex"};
	string_array_t car;
	car.buffer = strings;
	car.length = 3;
	int pos    = ui_inline_radio(&state->mode, &car, UI_ALIGN_LEFT);

	if (pos == 0) {
		ui_slider(&state->red, "R", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		channel_changed |= ui_item_changed();
		ui_slider(&state->green, "G", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		channel_changed |= ui_item_changed();
		ui_slider(&state->blue, "B", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		channel_changed |= ui_item_changed();
		if (channel_changed) {
			float hsv[3];
			ui_rgb_to_hsv(state->red, state->green, state->blue, hsv);
			if (hsv[1] > 0)
				state->hue = hsv[0];
			state->sat = hsv[1];
			state->val = hsv[2];
		}
		(*value) = ui_color(round(state->red * 255.0), round(state->green * 255.0), round(state->blue * 255.0), round(a * 255.0));
	}
	else if (pos == 1) {
		ui_slider(&state->hue, "H", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		channel_changed |= ui_item_changed();
		ui_slider(&state->sat, "S", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		channel_changed |= ui_item_changed();
		ui_slider(&state->val, "V", 0, 1, true, 100, true, UI_ALIGN_LEFT, true);
		channel_changed |= ui_item_changed();
		ui_hsv_to_rgb(state->hue, state->sat, state->val, &state->red);
		(*value) = ui_color(round(state->red * 255.0), round(state->green * 255.0), round(state->blue * 255.0), round(a * 255.0));
	}
	else if (pos == 2) {
		char  tmp[16];
		char *text = tmp;
		sprintf(text, "%x", (*value));
		ui_set_next_id(1);
		char *edited = ui_text_input(&text, "#", UI_ALIGN_LEFT, true, false);
		wheel_changed |= ui_item_changed();
		char hex_code[UI_TEXT_MAX];
		snprintf(hex_code, sizeof(hex_code), "%s", edited);
		if (edited != tmp)
			free(edited);
		if (strlen(hex_code) >= 1 && hex_code[0] == '#') { // Allow # at the beginning
			memmove(hex_code, hex_code + 1, strlen(hex_code));
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
			hex_code[8] = '\0';
		}
#ifdef _WIN32
		(*value) = _strtoi64(hex_code, NULL, 16);
#else
		(*value) = strtol(hex_code, NULL, 16);
#endif
	}
	if (channel_changed) {
		wheel_changed = current->changed = true;
	}

	// Do not close if user clicks
	if (current->input_released && ui_input_in_rect(current->_window_x + px, current->_window_y + py, w, h < 0 ? (current->_y - py) : h) &&
	    current->input_released) {
		current->changed = true;
	}

	ui_pop_id();
	current->item_changed = wheel_changed;
	ui_record_change();
	return (*value);
}

static void scroll_align(ui_t *current, int *line_index) {
	if (current->current_window == NULL)
		return;
	// Scroll down
	if (((*line_index) + 1) * UI_ELEMENT_H() + current->current_window->scroll_offset > current->_h - current->window_header_h) {
		current->current_window->scroll_offset -= UI_ELEMENT_H();
	}
	// Scroll up
	else if (((*line_index) + 1) * UI_ELEMENT_H() + current->current_window->scroll_offset < current->window_header_h) {
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

static bool ui_text_area_is_word_char(char c) {
	return c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static void ui_text_area_select_word(ui_t *current) {
	char *text = current->text_selected;
	int   len  = (int)strlen(text);
	int   pos  = current->cursor_x > len ? len : current->cursor_x;
	if (!(pos < len && ui_text_area_is_word_char(text[pos])) && !(pos > 0 && ui_text_area_is_word_char(text[pos - 1]))) {
		return;
	}
	int start = pos;
	while (start > 0 && ui_text_area_is_word_char(text[start - 1])) {
		start--;
	}
	int end = pos;
	while (end < len && ui_text_area_is_word_char(text[end])) {
		end++;
	}
	current->highlight_anchor = start;
	current->cursor_x         = end;
	current->cursor_sticky_x  = end;
}

void ui_text_area_clear_selection() {
	text_area_selection_start = -1;
}

static void handle_line_select(ui_t *current, int *line_index) {
	if (current->is_shift_down) {
		if (text_area_selection_start == -1) {
			text_area_selection_start     = (*line_index);
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
		if (pos < (int)sizeof(temp) - 1) {
			temp[pos++] = str[i];
		}
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

static void ui_append_capped(char *dst, char *src, int cap) {
	int len   = strlen(dst);
	int count = strlen(src);
	if (len + count > cap - 1) {
		count = cap - 1 - len;
	}
	if (count <= 0) {
		return;
	}
	memcpy(dst + len, src, count);
	dst[len + count] = '\0';
}

void ui_text_area_word_wrap(char *lines, char **value, int *line_index, bool selected) {
	ui_t *current    = ui_get_current();
	bool  cursor_set = false;
	int   cursor_pos = current->cursor_x;
	for (int i = 0; i < (*line_index); ++i) {
		cursor_pos += strlen(ui_extract_line(lines, i)) + 1; // + '\n'
	}
	bool anchor_set = false;
	int  anchor_pos = current->highlight_anchor;
	for (int i = 0; i < (*line_index); ++i) {
		anchor_pos += strlen(ui_extract_line(lines, i)) + 1;
	}
	int  word_count = ui_word_count(lines);
	char line[UI_TEXT_MAX];
	line[0] = '\0';
	char new_lines[UI_TEXT_MAX];
	new_lines[0] = '\0';

	for (int i = 0; i < word_count; ++i) {
		char *w      = ui_extract_word(lines, i);
		float spacew = draw_string_width(current->ops->font, current->font_size, " ");
		float wordw  = spacew + draw_string_width(current->ops->font, current->font_size, w);
		float linew  = wordw + draw_string_width(current->ops->font, current->font_size, line);
		if (linew > current->_w - 10 && linew > wordw) {
			if (new_lines[0] != '\0') {
				ui_append_capped(new_lines, "\n", UI_TEXT_MAX);
			}
			ui_append_capped(new_lines, line, UI_TEXT_MAX);
			line[0] = '\0';
		}

		if (line[0] == '\0') {
			ui_append_capped(line, w, UI_TEXT_MAX);
		}
		else {
			ui_append_capped(line, " ", UI_TEXT_MAX);
			ui_append_capped(line, w, UI_TEXT_MAX);
		}

		int new_line_count = new_lines[0] == '\0' ? 0 : ui_line_count(new_lines);
		int lines_len      = new_line_count;
		for (int i = 0; i < new_line_count; ++i) {
			lines_len += strlen(ui_extract_line(new_lines, i));
		}

		if (selected && !cursor_set && cursor_pos <= lines_len + strlen(line)) {
			cursor_set        = true;
			(*line_index)     = new_line_count;
			current->cursor_x = cursor_pos - lines_len;
		}
		if (selected && !anchor_set && anchor_pos <= lines_len + strlen(line)) {
			anchor_set                = true;
			current->highlight_anchor = anchor_pos - lines_len;
		}
	}
	if (new_lines[0] != '\0') {
		ui_append_capped(new_lines, "\n", UI_TEXT_MAX);
	}
	ui_append_capped(new_lines, line, UI_TEXT_MAX);
	if (selected) {
		(*value) = string_copy(ui_extract_line(new_lines, (*line_index)));
		strcpy(current->text_selected, (*value));
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

static char ui_text_area_lower(char c) {
	return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int ui_text_area_search_next(char *line, char *search, int from) {
	int len = strlen(search);
	if (len == 0) {
		return -1;
	}
	for (int i = from; line[i] != '\0'; ++i) {
		int j = 0;
		while (j < len && ui_text_area_lower(line[i + j]) == ui_text_area_lower(search[j])) {
			j++;
		}
		if (j == len) {
			return i;
		}
	}
	return -1;
}

static void ui_text_area_draw_search(char *line) {
	// Highlight the occurrences of ui_text_area_search
	if (ui_text_area_search == NULL || ui_text_area_search[0] == '\0') {
		return;
	}
	ui_t *current = ui_get_current();
	if (!ui_is_visible(UI_ELEMENT_H())) {
		return;
	}
	int   len = strlen(ui_text_area_search);
	float off = UI_TEXT_OFFSET();
	float h   = UI_ELEMENT_H() - current->button_offset_y * 3.0;
	int   i   = ui_text_area_search_next(line, ui_text_area_search, 0);
	while (i >= 0) {
		float x = current->_x + off + draw_sub_string_width(current->ops->font, current->font_size, line, 0, i);
		float w = draw_sub_string_width(current->ops->font, current->font_size, line, i, i + len);
		draw_set_color(0x77ffee00);
		draw_filled_rect(x, current->_y + current->button_offset_y * 1.5, w, h);
		i = ui_text_area_search_next(line, ui_text_area_search, i + len);
	}
}

char *ui_text_area(char **value, int *line_index, int align, bool editable, char *label, bool word_wrap) {
	ui_t   *current       = ui_get_current();
	ui_id_t text_id       = current->next_id != 0 ? current->next_id : (ui_id_t)value;
	char   *original_text = string_copy(*value);
	current->item_changed = false;
	(*value)              = string_replace_all((*value), "\t", "    ");
	bool selected         = current->text_selected_id == ui_widget_id(value, UI_ID_TEXT); // Text being edited
	current->next_id      = 0;                                                            // Internal labels must not consume the text areas identity

	int text_size = strlen((*value)) + 1 + UI_TEXT_MAX;
	if (lines_size < text_size) {
		if (lines_buffer != NULL) {
			free(lines_buffer);
		}
		lines_buffer = malloc(text_size);
		lines_size   = text_size;
	}

	char *lines = lines_buffer;
	strcpy(lines, (*value));
	bool show_label              = (lines[0] == '\0');
	bool key_pressed             = selected && current->is_key_pressed;
	current->highlight_on_select = false;
	current->tab_switch_enabled  = false;
	if (word_wrap && (*value)[0] != '\0') {
		ui_text_area_word_wrap(lines, value, line_index, selected);
	}
	int line_count = ui_line_count(lines);
	if (ui_text_area_line_numbers) {
		ui_text_area_draw_line_numbers(line_count);
	}
	int cursor_start_x  = current->cursor_x;
	int active_line_len = selected ? (int)strlen(ui_extract_line(lines, (*line_index))) : 0;

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
	char edit_new_text[UI_TEXT_MAX];
	edit_new_text[0] = '\0';
	for (int i = 0; i < line_count; ++i) { // Draw lines
		char *line = ui_extract_line_off(lines, 0, &lines_off);
		ui_text_area_draw_search(line);
		// Text input
		if ((!selected && ui_get_hover(UI_ELEMENT_H())) || (selected && i == (*line_index))) {
			(*line_index)      = i; // Set active line
			bool line_released = ui_get_released(UI_ELEMENT_H());
			strcpy((*value), line);
			current->submit_text_id = 0;
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
			ui_set_next_id(text_id);
			ui_text_input(value, show_label ? label : "", align, editable, false);
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
			if (line_released) { // Double click word select
				double now = iron_time();
				if (now - text_area_last_click_time < 0.3 && text_area_last_click_line == i) {
					text_area_selection_start = -1;
					ui_text_area_select_word(current);
					text_area_last_click_time = 0.0;
				}
				else {
					text_area_last_click_time = now;
				}
				text_area_last_click_line = i;
			}
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
				    ((i >= text_area_selection_start && i < (*line_index)) || (i <= text_area_selection_start && i > (*line_index)))) {
					int   line_height   = UI_ELEMENT_H();
					int   cursor_height = line_height - current->button_offset_y * 3.0;
					float off           = UI_TEXT_OFFSET();
					float hl_x, hl_w;
					if (i == text_area_selection_start && i > (*line_index)) {
						// Start line is below active line: highlight from 0 to start col
						hl_x = current->_x + off;
						hl_w = draw_sub_string_width(current->ops->font, current->font_size, line, 0, text_area_selection_start_col);
					}
					else if (i == text_area_selection_start && i < (*line_index)) {
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
	if (selected && paste_is_multiline_out && (text_area_selection_start == -1 || text_area_selection_start == (*line_index))) {
		int pos = ui_line_pos(lines, (*line_index)) + current->cursor_x;
		ui_insert_chars_at(lines, pos, ui_text_to_paste);
		int paste_lines = ui_line_count(ui_text_to_paste) - 1;
		(*line_index) += paste_lines;
		char *last_pasted_line = ui_extract_line(ui_text_to_paste, paste_lines);
		current->cursor_x = current->highlight_anchor = (int)strlen(last_pasted_line);
		text_area_selection_start                     = -1;
		ui_text_to_paste[0]                           = '\0';
		ui_is_paste                                   = false;
		strcpy(current->text_selected, ui_extract_line(lines, (*line_index)));
		(*value) = string_copy(current->text_selected);
	}

	// Multi-line copy/cut/paste
	if (selected && text_area_selection_start != -1 && text_area_selection_start != (*line_index)) {
		// Determine ordered selection bounds
		int sel_top_line, sel_bot_line, sel_top_col, sel_bot_col;
		if (text_area_selection_start < (*line_index)) {
			sel_top_line = text_area_selection_start;
			sel_bot_line = (*line_index);
			sel_top_col  = text_area_selection_start_col;
			sel_bot_col  = current->cursor_x;
		}
		else {
			sel_top_line = (*line_index);
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
			(*line_index)     = sel_top_line;
			current->cursor_x = current->highlight_anchor = sel_top_col;
			text_area_selection_start                     = -1;
			strcpy(current->text_selected, ui_extract_line(lines, (*line_index)));
			(*value) = string_copy(current->text_selected);
		}
		if (editable && ui_is_paste) {
			// Delete selected range then insert clipboard
			int pos_start = ui_line_pos(lines, sel_top_line) + sel_top_col;
			int pos_end   = ui_line_pos(lines, sel_bot_line) + sel_bot_col;
			ui_remove_chars_at(lines, pos_start, pos_end - pos_start);
			ui_insert_chars_at(lines, pos_start, ui_text_to_paste);
			// Reposition cursor: count newlines in pasted text
			int paste_lines        = ui_line_count(ui_text_to_paste) - 1;
			(*line_index)          = sel_top_line + paste_lines;
			char *last_pasted_line = ui_extract_line(ui_text_to_paste, paste_lines);
			current->cursor_x = current->highlight_anchor = sel_top_col + (int)strlen(last_pasted_line);
			text_area_selection_start                     = -1;
			ui_text_to_paste[0]                           = '\0';
			ui_is_paste                                   = false;
			strcpy(current->text_selected, ui_extract_line(lines, (*line_index)));
			(*value) = string_copy(current->text_selected);
		}
	}

	if (ui_text_area_scroll_past_end) {
		current->_y += current->_h - current->window_header_h - UI_ELEMENT_H() - UI_ELEMENT_OFFSET();
	}

	if (key_pressed) {
		// Move cursor vertically
		if (current->key_code == KEY_CODE_DOWN && (*line_index) < line_count - 1) {
			handle_line_select(current, line_index);
			current->cursor_sticky_x = current->cursor_sticky_x > current->cursor_x ? current->cursor_sticky_x : current->cursor_x;
			(*line_index)++;
			int next_len      = (int)strlen(ui_extract_line(lines, (*line_index)));
			current->cursor_x = current->cursor_sticky_x < next_len ? current->cursor_sticky_x : next_len;
			if (!current->is_shift_down) {
				current->highlight_anchor = current->cursor_x;
			}
			scroll_align(current, line_index);
		}
		else if (current->key_code == KEY_CODE_UP && (*line_index) > 0) {
			handle_line_select(current, line_index);
			current->cursor_sticky_x = current->cursor_sticky_x > current->cursor_x ? current->cursor_sticky_x : current->cursor_x;
			(*line_index)--;
			int prev_len      = (int)strlen(ui_extract_line(lines, (*line_index)));
			current->cursor_x = current->cursor_sticky_x < prev_len ? current->cursor_sticky_x : prev_len;
			if (!current->is_shift_down) {
				current->highlight_anchor = current->cursor_x;
			}
			scroll_align(current, line_index);
		}
		else if (current->key_code == KEY_CODE_RIGHT && cursor_start_x == active_line_len && (*line_index) < line_count - 1 && !current->is_ctrl_down) {
			handle_line_select(current, line_index);
			(*line_index)++;
			current->cursor_x         = 0;
			current->highlight_anchor = 0;
			current->cursor_sticky_x  = 0;
			strcpy(current->text_selected, ui_extract_line(lines, (*line_index)));
			(*value) = string_copy(current->text_selected);
			scroll_align(current, line_index);
		}
		else if (current->key_code == KEY_CODE_LEFT && cursor_start_x == 0 && (*line_index) > 0 && !current->is_ctrl_down) {
			handle_line_select(current, line_index);
			(*line_index)--;
			int prev_len              = (int)strlen(ui_extract_line(lines, (*line_index)));
			current->cursor_x         = prev_len;
			current->highlight_anchor = prev_len;
			current->cursor_sticky_x  = prev_len;
			strcpy(current->text_selected, ui_extract_line(lines, (*line_index)));
			(*value) = string_copy(current->text_selected);
			scroll_align(current, line_index);
		}
		else if (current->key_code == KEY_CODE_HOME || current->key_code == KEY_CODE_END) {
			text_area_selection_start = -1; // Home/End are single-line operations, cancel multi-line selection
		}
		else if (current->is_ctrl_down && current->is_a_down) { // Select all lines
			text_area_selection_start     = 0;
			text_area_selection_start_col = 0;
			(*line_index)                 = line_count - 1;
			current->highlight_anchor     = 0;
			current->cursor_x             = (int)strlen(ui_extract_line(lines, (*line_index)));
			current->cursor_sticky_x      = current->cursor_x;
			strcpy(current->text_selected, ui_extract_line(lines, (*line_index)));
			(*value) = string_copy(current->text_selected);
			scroll_align(current, line_index);
		}
		else {
			current->cursor_sticky_x = current->cursor_x; // Reset sticky column on any non-vertical move
			if (!current->is_shift_down && !current->is_ctrl_down) {
				text_area_selection_start = -1; // Cancel multi-line selection on non-shift, non-ctrl key
			}
		}
		// New line
		if (editable && current->key_code == KEY_CODE_RETURN && !word_wrap) {
			(*line_index)++;
			ui_insert_char_at(lines, ui_line_pos(lines, (*line_index) - 1) + current->cursor_x, '\n');
			// Auto indent
			char *prev_line = ui_extract_line(lines, (*line_index) - 1);
			int   indent    = 0;
			while (prev_line[indent] == ' ') {
				indent++;
			}
			int new_line_pos = ui_line_pos(lines, (*line_index));
			for (int s = 0; s < indent; ++s) {
				ui_insert_char_at(lines, new_line_pos, ' ');
			}
			ui_set_next_id(text_id);
			ui_start_text_edit(value, UI_ALIGN_LEFT);
			current->next_id  = 0;
			current->cursor_x = current->highlight_anchor = indent;
			scroll_align(current, line_index);
		}
		// Delete line
		if (editable && current->key_code == KEY_CODE_BACKSPACE && cursor_start_x == 0 && (*line_index) > 0 && text_area_selection_start == -1) {
			(*line_index)--;
			current->cursor_x = current->highlight_anchor = strlen(ui_extract_line(lines, (*line_index)));
			ui_remove_chars_at(lines, ui_line_pos(lines, (*line_index) + 1) - 1, 1); // Remove '\n' of the previous line
			scroll_align(current, line_index);
		}
		// Delete line
		if (editable && current->key_code == KEY_CODE_DELETE && (*line_index) < line_count - 1 && cursor_start_x == active_line_len &&
		    text_area_selection_start == -1) {
			current->highlight_anchor = current->cursor_x;
			ui_remove_chars_at(lines, ui_line_pos(lines, (*line_index) + 1) - 1, 1); // Remove '\n' at end of current line
		}
		// Tab indent
		if (editable && current->key_code == KEY_CODE_TAB) {
			int pos = ui_line_pos(lines, (*line_index)) + current->cursor_x;
			ui_insert_chars_at(lines, pos, "    ");
			current->cursor_x += 4;
			current->highlight_anchor = current->cursor_x;
			current->cursor_sticky_x  = current->cursor_x;
		}
		strcpy(current->text_selected, ui_extract_line(lines, (*line_index)));
	}

	current->highlight_on_select = true;
	current->tab_switch_enabled  = true;
	(*value)                     = string_copy(lines);
	current->item_changed        = strcmp(original_text, *value) != 0;
	free(original_text);
	ui_record_change();
	return (*value);
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

ui_t *ui_create(ui_options_t *ops) {
	ui_t *raw = ALLOC_INIT(ui_t, {0});
	ui_init(raw, ops);
	return raw;
}

ui_theme_t *ui_theme_create() {
	ui_theme_t *raw = ALLOC_INIT(ui_theme_t, {0});
	ui_theme_default(raw);
	return raw;
}

void ui_set_font(ui_t *ui, draw_font_t *font) {
	draw_font_init(font);
	ui->ops->font = font;
}
