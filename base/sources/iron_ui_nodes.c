#include "iron_ui.h"

#include "iron_armpack.h"
#include "iron_array.h"
#include "iron_draw.h"
#include "iron_gc.h"
#include "iron_gpu.h"
#include "iron_json.h"
#include "iron_string.h"
#include "iron_system.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static ui_nodes_t   *current_nodes           = NULL;
static bool          ui_nodes_elements_baked = false;
static gpu_texture_t ui_socket_image;
static bool          ui_touch_drag   = false;
static bool          ui_box_select   = false;
static int           ui_box_select_x = 0;
static int           ui_box_select_y = 0;
static const int     ui_max_buttons  = 9;
static int           ui_node_id      = -1;
static ui_node_t    *node_resize     = NULL;

char           *ui_clipboard                             = "";
string_array_t *ui_nodes_exclude_remove                  = NULL; // No removal for listed node types
bool            ui_nodes_socket_released                 = false;
string_array_t *(*ui_nodes_enum_texts)(char *)           = NULL; // Retrieve combo items for buttons of type ENUM
any_array_t *(*ui_nodes_enum_textures)(char *)           = NULL;
gpu_texture_t *(*ui_nodes_preview_texture)(ui_node_t *)  = NULL; // Retrieve preview image
void (*ui_nodes_on_custom_button)(int, char *)           = NULL; // Call external function
ui_canvas_control_t *(*ui_nodes_on_canvas_control)(void) = NULL;
void (*ui_nodes_on_canvas_released)(void)                = NULL;
void (*ui_nodes_on_socket_released)(int)                 = NULL;
void (*ui_nodes_on_link_drag)(int, bool)                 = NULL;
void (*ui_nodes_on_node_remove)(ui_node_t *)             = NULL;
void (*ui_nodes_on_node_changed)(ui_node_t *)            = NULL;
bool ui_nodes_grid_snap                                  = true;
int  ui_nodes_grid_snap_w                                = 40;

int ui_popup_x                                    = 0;
int ui_popup_y                                    = 0;
int ui_popup_w                                    = 0;
int ui_popup_h                                    = 0;
void (*ui_popup_commands)(ui_t *, void *, void *) = NULL;
void *ui_popup_data;
void *ui_popup_data2;
int   ui_popup_handle_node_id        = -1;
int   ui_popup_handle_node_socket_id = -1;

char *tr(char *id); // translator.c

char *ui_tr(char *id) {
	return tr(id);
}

void ui_nodes_init(ui_nodes_t *nodes) {
	if (current_nodes == NULL) {
		current_nodes = nodes;
	}
	memset(nodes, 0, sizeof(ui_nodes_t));
	nodes->zoom              = 1.0;
	nodes->scale_factor      = 1.0;
	nodes->ELEMENT_H         = 25.0;
	nodes->snap_from_id      = -1;
	nodes->snap_to_id        = -1;
	nodes->link_drag_id      = -1;
	nodes->nodes_selected_id = gc_alloc(sizeof(i32_array_t));
	nodes->handle            = ui_handle_create();
}

float UI_NODES_SCALE() {
	return current_nodes->scale_factor * current_nodes->zoom;
}

float UI_NODES_PAN_X() {
	float zoom_pan = (1.0 - current_nodes->zoom) * current_nodes->uiw / 2.5;
	return current_nodes->pan_x * UI_NODES_SCALE() + zoom_pan;
}

float UI_NODES_PAN_Y() {
	float zoom_pan = (1.0 - current_nodes->zoom) * current_nodes->uih / 2.5;
	return current_nodes->pan_y * UI_NODES_SCALE() + zoom_pan;
}

float UI_LINE_H() {
	return current_nodes->ELEMENT_H * UI_NODES_SCALE();
}

float UI_BUTTONS_H(ui_node_t *node) {
	if (node->flags & UI_NODE_FLAG_COLLAPSED)
		return 0.0;
	float h = 0.0;
	for (int i = 0; i < node->buttons->length; ++i) {
		ui_node_button_t *but = node->buttons->buffer[i];
		if (strcmp(but->type, "RGBA") == 0) {
			float ratio = node->width > 0 ? node->width / 140.0 : 1.0; // Node resized
			h += 104.0 * ratio * UI_NODES_SCALE() + UI_LINE_H() * 5.0; // Color wheel + controls
		}
		else if (strcmp(but->type, "VECTOR") == 0) {
			h += UI_LINE_H() * 4.0;
		}
		else if (strcmp(but->type, "CUSTOM") == 0) {
			h += UI_LINE_H() * but->height;
		}
		else {
			h += UI_LINE_H();
		}
	}
	return h;
}

float UI_OUTPUTS_H(ui_node_t *node, int length) {
	if (node->flags & UI_NODE_FLAG_COLLAPSED)
		return 0.0;
	float h = 0.0;
	for (int i = 0; i < (length < 0 ? node->outputs->length : length); ++i) {
		h += UI_LINE_H();
	}
	return h;
}

bool ui_input_linked(ui_node_canvas_t *canvas, int node_id, int i) {
	for (int x = 0; x < canvas->links->length; ++x) {
		ui_node_link_t *l = canvas->links->buffer[x];
		if (l->to_id == node_id && l->to_socket == i) {
			return true;
		}
	}
	return false;
}

float UI_INPUTS_H(ui_node_canvas_t *canvas, ui_node_socket_t **sockets, int sockets_count, int length) {
	float h = 0.0;
	for (int i = 0; i < (length < 0 ? sockets_count : length); ++i) {
		if (strcmp(sockets[i]->type, "VECTOR") == 0 && sockets[i]->display == 1 && !ui_input_linked(canvas, sockets[i]->node_id, i)) {
			h += UI_LINE_H() * 4;
		}
		else {
			h += UI_LINE_H();
		}
	}
	return h;
}

float UI_NODE_H(ui_node_canvas_t *canvas, ui_node_t *node) {
	if (node->flags & UI_NODE_FLAG_COLLAPSED)
		return UI_LINE_H() * 1.2;
	return UI_LINE_H() * 1.2 + UI_INPUTS_H(canvas, node->inputs->buffer, node->inputs->length, -1) + UI_OUTPUTS_H(node, -1) + UI_BUTTONS_H(node);
}

float UI_NODE_W(ui_node_t *node) {
	return (node->width != 0 ? node->width : 140.0) * UI_NODES_SCALE();
}

float UI_NODE_X(ui_node_t *node) {
	return node->x * UI_NODES_SCALE() + UI_NODES_PAN_X();
}

float UI_NODE_Y(ui_node_t *node) {
	return node->y * UI_NODES_SCALE() + UI_NODES_PAN_Y();
}

float UI_INPUT_Y(ui_node_canvas_t *canvas, ui_node_t *node, int pos) {
	if (node->flags & UI_NODE_FLAG_COLLAPSED)
		return UI_LINE_H() * 0.5;
	return UI_LINE_H() * 1.62 + UI_INPUTS_H(canvas, node->inputs->buffer, node->inputs->length, pos);
}

float UI_OUTPUT_Y(ui_node_t *node, int pos) {
	if (node->flags & UI_NODE_FLAG_COLLAPSED)
		return UI_LINE_H() * 0.5;
	return UI_LINE_H() * 1.62 + UI_OUTPUTS_H(node, pos);
}

float ui_p(float f) {
	return f * UI_NODES_SCALE();
}

ui_node_t *ui_get_node(ui_node_array_t *nodes, int id) {
	for (int i = 0; i < nodes->length; ++i) {
		if (nodes->buffer[i]->id == id) {
			return nodes->buffer[i];
		}
	}
	return NULL;
}

int ui_get_node_index(ui_node_t **nodes, int nodes_count, int id) {
	for (int i = 0; i < nodes_count; ++i) {
		if (nodes[i]->id == id) {
			return i;
		}
	}
	return -1;
}

int ui_next_node_id(ui_node_array_t *nodes) {
	if (ui_node_id == -1) {
		for (int i = 0; i < nodes->length; ++i) {
			if (ui_node_id < nodes->buffer[i]->id) {
				ui_node_id = nodes->buffer[i]->id;
			}
		}
	}
	return ++ui_node_id;
}

ui_node_link_t *ui_get_link(ui_node_link_array_t *links, int id) {
	for (int i = 0; i < links->length; ++i) {
		if (links->buffer[i]->id == id) {
			return links->buffer[i];
		}
	}
	return NULL;
}

int ui_get_link_index(ui_node_link_array_t *links, int id) {
	for (int i = 0; i < links->length; ++i) {
		if (links->buffer[i]->id == id) {
			return i;
		}
	}
	return -1;
}

int ui_next_link_id(ui_node_link_array_t *links) {
	int id = 0;
	for (int i = 0; i < links->length; ++i) {
		if (links->buffer[i]->id >= id) {
			id = links->buffer[i]->id + 1;
		}
	}
	return id;
}

int ui_get_socket_id(ui_node_array_t *nodes) {
	int id = 0;
	for (int i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		for (int j = 0; j < n->inputs->length; ++j) {
			if (n->inputs->buffer[j]->id >= id) {
				id = n->inputs->buffer[j]->id + 1;
			}
		}
		for (int j = 0; j < n->outputs->length; ++j) {
			if (n->outputs->buffer[j]->id >= id) {
				id = n->outputs->buffer[j]->id + 1;
			}
		}
	}
	return id;
}

void ui_nodes_bake_elements() {
	if (ui_socket_image.width != 0) {
		gpu_texture_destroy(&ui_socket_image);
	}
	gpu_render_target_init(&ui_socket_image, 24, 24, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(&ui_socket_image, true, 0x00000000);
	draw_set_color(0xff111111);
	draw_filled_circle(12, 12, 11, 0);
	draw_set_color(0xffffffff);
	draw_filled_circle(12, 12, 9, 0);
	draw_end();
	ui_nodes_elements_baked = true;
}

ui_canvas_control_t *ui_on_default_canvas_control() {
	ui_t                      *current = ui_get_current();
	static ui_canvas_control_t c;
	c.pan_x = current->input_down_r ? current->input_dx : 0.0;
	c.pan_y = current->input_down_r ? current->input_dy : 0.0;
	c.zoom  = -current->input_wheel_delta / 10.0;
	return &c;
}

void ui_draw_link(float x1, float y1, float x2, float y2, bool highlight) {
	ui_t *current = ui_get_current();
	int   c1      = current->ops->theme->LABEL_COL;
	int   c2      = current->ops->theme->HOVER_COL + 0X00222222;
	int   c       = highlight ? c1 : c2;
	draw_set_color(ui_color(ui_color_r(c), ui_color_g(c), ui_color_b(c), 210));
	if (current->ops->theme->LINK_STYLE == UI_LINK_STYLE_LINE) {
		draw_line_aa(x1, y1, x2, y2, 1.0);
	}
	else if (current->ops->theme->LINK_STYLE == UI_LINK_STYLE_CUBIC_BEZIER) {
		f32_array_t xa;
		f32_array_t ya;
		float       x[] = {x1, x1 + fabs(x1 - x2) / 2.0, x2 - fabs(x1 - x2) / 2.0, x2};
		float       y[] = {y1, y1, y2, y2};
		xa.buffer       = x;
		ya.buffer       = y;
		draw_cubic_bezier(&xa, &ya, 30, highlight ? 2.0 : 1.0);
	}
}

static void ui_remove_link_at(ui_node_canvas_t *canvas, int at) {
	canvas->links->buffer[at] = NULL;
	for (int i = at; i < canvas->links->length - 1; ++i) {
		canvas->links->buffer[i] = canvas->links->buffer[i + 1];
	}
	canvas->links->length--;
}

static void ui_remove_node_at(ui_node_canvas_t *canvas, int at) {
	canvas->nodes->buffer[at] = NULL;
	for (int i = at; i < canvas->nodes->length - 1; ++i) {
		canvas->nodes->buffer[i] = canvas->nodes->buffer[i + 1];
	}
	canvas->nodes->length--;
}

void ui_remove_node(ui_node_t *n, ui_node_canvas_t *canvas) {
	if (n == NULL)
		return;
	int i = 0;
	while (i < canvas->links->length) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (l->from_id == n->id || l->to_id == n->id) {
			ui_remove_link_at(canvas, i);
			if (ui_nodes_on_node_changed != NULL && l->from_id == n->id) {
				ui_nodes_on_node_changed(ui_get_node(canvas->nodes, l->to_id));
			}
		}
		else {
			i++;
		}
	}
	ui_remove_node_at(canvas, ui_get_node_index(canvas->nodes->buffer, canvas->nodes->length, n->id));
	if (ui_nodes_on_node_remove != NULL) {
		(*ui_nodes_on_node_remove)(n);
	}
}

bool ui_is_selected(ui_node_t *node) {
	for (int i = 0; i < current_nodes->nodes_selected_id->length; ++i) {
		if (current_nodes->nodes_selected_id->buffer[i] == node->id) {
			return true;
		}
	}
	return false;
}

static void remove_from_selection(ui_node_t *node) {
	i32_array_remove(current_nodes->nodes_selected_id, node->id);
}

static void add_to_selection(ui_node_t *node) {
	i32_array_push(current_nodes->nodes_selected_id, node->id);
}

void ui_popup(int x, int y, int w, int h, void (*commands)(ui_t *, void *, void *), void *data, void *data2) {
	ui_popup_x        = x;
	ui_popup_y        = y;
	ui_popup_w        = w;
	ui_popup_h        = h;
	ui_popup_commands = commands;
	ui_popup_data     = data;
	ui_popup_data2    = data2;
}

static void color_picker_callback(uint32_t color) {
	ui_t  *current   = ui_get_current();
	float *val       = (float *)current_nodes->color_picker_callback_data;
	val[0]           = ui_color_r(color) / 255.0f;
	val[1]           = ui_color_g(color) / 255.0f;
	val[2]           = ui_color_b(color) / 255.0f;
	current->changed = true;
}

void ui_color_wheel_picker(void *data) {
	current_nodes->color_picker_callback_data = data;
	current_nodes->color_picker_callback      = &color_picker_callback;
}

static void rgba_popup_commands(ui_t *ui, void *data, void *data2) {
	ui_handle_t *nhandle = (ui_handle_t *)data;
	float       *val     = (float *)data2;
	nhandle->color       = ui_color(val[0] * 255.0, val[1] * 255.0, val[2] * 255.0, 255.0);
	ui_color_wheel(nhandle, false, -1, -1, true, &ui_color_wheel_picker, val);
	val[0] = ui_color_r(nhandle->color) / 255.0f;
	val[1] = ui_color_g(nhandle->color) / 255.0f;
	val[2] = ui_color_b(nhandle->color) / 255.0f;
}

void ui_nodes_rgba_popup(ui_handle_t *nhandle, float *val, int x, int y) {
	ui_t *current = ui_get_current();
	ui_popup(x, y, 140.0 * current_nodes->scale_factor, current->ops->theme->ELEMENT_H * 10.0, &rgba_popup_commands, nhandle, val);
}

static float ui_nodes_snap(float f) {
	float w = ui_nodes_grid_snap_w * UI_NODES_SCALE();
	f       = f * UI_NODES_SCALE();
	return roundf(f / w) * w;
}

static string_array_t enum_ar;
static char           enum_label[64];
static char           enum_texts_data[256][64];
static char          *enum_texts[256];

static string_array_t temp_ar; // Used by open combo
static char           temp_label[64];
static char           temp_texts_data[256][64];
static char          *temp_texts[256];

void ui_node_draw_body(ui_node_t *node, ui_node_canvas_t *canvas, float nx, float ny) {
	ui_t *current = ui_get_current();
	float wx      = current->_window_x;
	float wy      = current->_window_y;
	float w       = UI_NODE_W(node);
	// float h       = UI_NODE_H(canvas, node);
	float lineh = UI_LINE_H();

	// Outputs
	for (int i = 0; i < node->outputs->length; ++i) {
		ui_node_socket_t *out = node->outputs->buffer[i];
		ny += lineh;
		draw_set_color(out->color);
		draw_scaled_image(&ui_socket_image, nx + w - ui_p(6), ny - ui_p(3), ui_p(12), ui_p(12));
	}
	ny -= lineh * node->outputs->length;
	draw_set_color(current->ops->theme->LABEL_COL);
	for (int i = 0; i < node->outputs->length; ++i) {
		ui_node_socket_t *out = node->outputs->buffer[i];
		ny += lineh;
		float strw = draw_string_width(current->ops->font, current->font_size, ui_tr(out->name));
		draw_string(ui_tr(out->name), nx + w - strw - ui_p(12), ny - ui_p(3));

		if (ui_nodes_on_socket_released != NULL && current->input_enabled && (current->input_released || current->input_released_r)) {
			if (current->input_x > wx + nx && current->input_x < wx + nx + w && current->input_y > wy + ny && current->input_y < wy + ny + lineh) {
				ui_nodes_on_socket_released(out->id);
				ui_nodes_socket_released = true;
			}
		}
	}

	// Buttons
	ui_handle_t *nhandle = ui_nest(current_nodes->handle, node->id);
	ny -= lineh / 3.0; // Fix align
	for (int buti = 0; buti < node->buttons->length; ++buti) {
		ui_node_button_t *but = node->buttons->buffer[buti];
		float            *val = but->output == -1 ? but->default_value->buffer : node->outputs->buffer[but->output]->default_value->buffer;

		if (strcmp(but->type, "RGBA") == 0) {
			ny += lineh;             // 18 + 2 separator
			current->_x    = nx + 1; // Offset for node selection border
			current->_y    = ny;
			current->_w    = w;
			nhandle->color = ui_color(val[0] * 255.0, val[1] * 255.0, val[2] * 255.0, 255.0);
			ui_color_wheel(nhandle, false, -1, -1, true, &ui_color_wheel_picker, val);
			val[0] = ui_color_r(nhandle->color) / 255.0f;
			val[1] = ui_color_g(nhandle->color) / 255.0f;
			val[2] = ui_color_b(nhandle->color) / 255.0f;
		}
		else if (strcmp(but->type, "VECTOR") == 0) {
			ny += lineh;
			current->_x                      = nx;
			current->_y                      = ny;
			current->_w                      = w;
			float min                        = but->min;
			float max                        = but->max;
			float text_off                   = current->ops->theme->TEXT_OFFSET;
			current->ops->theme->TEXT_OFFSET = 6;
			ui_text(ui_tr(but->name), UI_ALIGN_LEFT, 0);

			ui_handle_t *h  = ui_nest(nhandle, buti);
			ui_handle_t *h0 = ui_nest(h, 0);
			if (h0->init) {
				h0->f = val[0];
			}
			ui_handle_t *h1 = ui_nest(h, 1);
			if (h1->init) {
				h1->f = val[1];
			}
			ui_handle_t *h2 = ui_nest(h, 2);
			if (h2->init) {
				h2->f = val[2];
			}

			val[0]                           = ui_slider(h0, "X", min, max, true, but->precision, true, UI_ALIGN_LEFT, true);
			val[1]                           = ui_slider(h1, "Y", min, max, true, but->precision, true, UI_ALIGN_LEFT, true);
			val[2]                           = ui_slider(h2, "Z", min, max, true, but->precision, true, UI_ALIGN_LEFT, true);
			current->ops->theme->TEXT_OFFSET = text_off;
			ny += lineh * 3.0;
		}
		else if (strcmp(but->type, "VALUE") == 0) {
			ny += lineh;
			current->_x                      = nx;
			current->_y                      = ny;
			current->_w                      = w;
			float min                        = but->min;
			float max                        = but->max;
			float prec                       = but->precision;
			float text_off                   = current->ops->theme->TEXT_OFFSET;
			current->ops->theme->TEXT_OFFSET = 6;
			ui_handle_t *soc_handle          = ui_nest(nhandle, buti);
			if (soc_handle->init) {
				soc_handle->f = val[0];
			}
			val[0]                           = ui_slider(soc_handle, ui_tr(but->name), min, max, true, prec, true, UI_ALIGN_LEFT, true);
			current->ops->theme->TEXT_OFFSET = text_off;
		}
		else if (strcmp(but->type, "STRING") == 0) {
			ny += lineh;
			current->_x                = nx;
			current->_y                = ny;
			current->_w                = w;
			ui_handle_t *h             = ui_nest(nhandle, buti);
			h->text                    = val != NULL ? (char *)val : "";
			but->default_value->buffer = ui_text_input(h, ui_tr(but->name), UI_ALIGN_LEFT, true, false);
			but->default_value->length = strlen(but->default_value->buffer) + 1;
		}
		else if (strcmp(but->type, "ENUM") == 0) {
			ny += lineh;
			current->_x = nx;
			current->_y = ny;
			current->_w = w;

			ui_handle_t *but_handle = ui_nest(nhandle, buti);
			but_handle->i           = ((float *)but->default_value->buffer)[0];

			bool  combo_select      = current->combo_selected_handle == NULL && ui_get_released(UI_ELEMENT_H());
			char *label             = combo_select ? temp_label : enum_label;
			char (*texts_data)[256] = combo_select ? temp_texts_data : enum_texts_data;
			char          **texts   = combo_select ? temp_texts : enum_texts;
			string_array_t *ar      = combo_select ? &temp_ar : &enum_ar;
			any_array_t    *images  = NULL;

			int  texts_count  = 0;
			bool has_but_data = but->data != NULL && but->data->length > 1;
			if (has_but_data) {
				int wi = 0;
				for (int i = 0; i < but->data->length; ++i) {
					if (texts_count == 255) {
						break;
					}
					char c = ((char *)but->data->buffer)[i];
					if (c == '\0') {
						texts_data[texts_count][wi] = '\0';
						texts_count++;
						break;
					}
					if (c == '\n') {
						texts_data[texts_count][wi] = '\0';
						texts_count++;
						wi = 0;
						continue;
					}
					texts_data[texts_count][wi] = c;
					wi++;
				}
				for (int i = 0; i < texts_count; ++i) {
					strcpy(texts_data[i], ui_tr(texts_data[i]));
					texts[i] = texts_data[i];
				}

				ar->buffer = texts;
				ar->length = texts_count;
			}
			else {
				gc_unroot(ar);
				ar = (*ui_nodes_enum_texts)(node->type);
				gc_root(ar);
			}

			strcpy(label, ui_tr(but->name));

			((float *)but->default_value->buffer)[0] = ui_combo(but_handle, ar, label, false, UI_ALIGN_LEFT, true);
			if (current->combo_selected_handle == but_handle && !has_but_data && ui_nodes_enum_textures != NULL) {
				current->combo_selected_images = (*ui_nodes_enum_textures)(node->type);
			}
		}
		else if (strcmp(but->type, "BOOL") == 0) {
			ny += lineh;
			current->_x    = nx;
			current->_y    = ny;
			current->_w    = w;
			ui_handle_t *h = ui_nest(nhandle, buti);
			if (h->init) {
				h->b = ((float *)but->default_value->buffer)[0];
			}
			((float *)but->default_value->buffer)[0] = ui_check(h, ui_tr(but->name), "");
		}
		else if (strcmp(but->type, "CUSTOM") == 0) { // Calls external function for custom button drawing
			ny += lineh;
			current->_x = nx;
			current->_y = ny;
			current->_w = w;
			ui_nodes_on_custom_button(node->id, but->name);
			ny += lineh * (but->height - 1); // but->height specifies vertical button size
		}
	}
	ny += lineh / 3.0; // Fix align

	// Inputs
	for (int i = 0; i < node->inputs->length; ++i) {
		ui_node_socket_t *inp = node->inputs->buffer[i];
		ny += lineh;
		draw_set_color(inp->color);
		draw_scaled_image(&ui_socket_image, nx - ui_p(6), ny - ui_p(3), ui_p(12), ui_p(12));
		bool is_linked = ui_input_linked(canvas, node->id, i);
		if (!is_linked && strcmp(inp->type, "VALUE") == 0) {
			current->_x                      = nx + ui_p(6);
			current->_y                      = ny - ui_p(current_nodes->ELEMENT_H / 3.0);
			current->_w                      = w - ui_p(6);
			ui_node_socket_t *soc            = inp;
			float             min            = soc->min;
			float             max            = soc->max;
			float             prec           = soc->precision;
			float             text_off       = current->ops->theme->TEXT_OFFSET;
			current->ops->theme->TEXT_OFFSET = 6;

			ui_handle_t *_handle    = ui_nest(nhandle, ui_max_buttons);
			ui_handle_t *soc_handle = ui_nest(_handle, i);
			if (soc_handle->init) {
				soc_handle->f = ((float *)soc->default_value->buffer)[0];
			}
			((float *)soc->default_value->buffer)[0] = ui_slider(soc_handle, ui_tr(inp->name), min, max, true, prec, true, UI_ALIGN_LEFT, true);
			current->ops->theme->TEXT_OFFSET         = text_off;
		}
		else if (!is_linked && strcmp(inp->type, "STRING") == 0) {
			current->_x                      = nx + ui_p(6);
			current->_y                      = ny - ui_p(9);
			current->_w                      = w - ui_p(6);
			ui_node_socket_t *soc            = inp;
			float             text_off       = current->ops->theme->TEXT_OFFSET;
			current->ops->theme->TEXT_OFFSET = 6;
			ui_handle_t *_handle             = ui_nest(nhandle, ui_max_buttons);
			ui_handle_t *h                   = ui_nest(_handle, i);
			if (h->init) {
				strcpy(h->text, soc->default_value->buffer);
			}
			soc->default_value->buffer       = ui_text_input(h, ui_tr(inp->name), UI_ALIGN_LEFT, true, false);
			current->ops->theme->TEXT_OFFSET = text_off;
		}
		else if (!is_linked && strcmp(inp->type, "RGBA") == 0) {
			draw_set_color(current->ops->theme->LABEL_COL);
			draw_string(ui_tr(inp->name), nx + ui_p(12), ny - ui_p(3));
			ui_node_socket_t *soc = inp;
			draw_set_color(0xff000000);
			draw_filled_rect(nx + w - ui_p(38), ny - ui_p(6), ui_p(36), ui_p(18));
			float *val = (float *)soc->default_value->buffer;
			draw_set_color(ui_color(val[0] * 255, val[1] * 255, val[2] * 255, 255));
			float rx = nx + w - ui_p(37);
			float ry = ny - ui_p(5);
			float rw = ui_p(34);
			float rh = ui_p(16);
			draw_filled_rect(rx, ry, rw, rh);
			float ix = current->input_x - wx;
			float iy = current->input_y - wy;
			if (current->input_started && ix > rx && iy > ry && ix < rx + rw && iy < ry + rh) {
				current_nodes->_input_started = current->input_started = false;
				ui_nodes_rgba_popup(nhandle, soc->default_value->buffer, (int)(rx), (int)(ry + UI_ELEMENT_H()));
				ui_popup_handle_node_id        = node->id;
				ui_popup_handle_node_socket_id = soc->id;
			}
			if (ui_popup_commands != NULL && ui_popup_handle_node_id == node->id && ui_popup_handle_node_socket_id == soc->id) {
				// armpack data may have been moved in memory
				ui_popup_data  = nhandle;
				ui_popup_data2 = soc->default_value->buffer;
			}
		}
		else if (!is_linked && strcmp(inp->type, "VECTOR") == 0 && inp->display == 1) {
			draw_set_color(current->ops->theme->LABEL_COL);
			draw_string(ui_tr(inp->name), nx + ui_p(12), ny - ui_p(3));
			ny += lineh / 2;
			current->_x                      = nx;
			current->_y                      = ny;
			current->_w                      = w;
			float min                        = inp->min;
			float max                        = inp->max;
			float text_off                   = current->ops->theme->TEXT_OFFSET;
			current->ops->theme->TEXT_OFFSET = 6;
			float       *val                 = (float *)inp->default_value->buffer;
			ui_handle_t *h                   = ui_nest(nhandle, ui_max_buttons);
			ui_handle_t *hi                  = ui_nest(h, i);
			ui_handle_t *h0                  = ui_nest(hi, 0);
			if (h0->init) {
				h0->f = val[0];
			}
			ui_handle_t *h1 = ui_nest(hi, 1);
			if (h1->init) {
				h1->f = val[1];
			}
			ui_handle_t *h2 = ui_nest(hi, 2);
			if (h2->init) {
				h2->f = val[2];
			}
			val[0]                           = ui_slider(h0, "X", min, max, true, inp->precision, true, UI_ALIGN_LEFT, true);
			val[1]                           = ui_slider(h1, "Y", min, max, true, inp->precision, true, UI_ALIGN_LEFT, true);
			val[2]                           = ui_slider(h2, "Z", min, max, true, inp->precision, true, UI_ALIGN_LEFT, true);
			current->ops->theme->TEXT_OFFSET = text_off;
			ny += lineh * 2.5;
		}
		else {
			draw_set_color(current->ops->theme->LABEL_COL);
			draw_string(ui_tr(inp->name), nx + ui_p(12), ny - ui_p(3));
		}
		if (ui_nodes_on_socket_released != NULL && current->input_enabled && (current->input_released || current->input_released_r)) {
			if (current->input_x > wx + nx && current->input_x < wx + nx + w && current->input_y > wy + ny && current->input_y < wy + ny + lineh) {
				ui_nodes_on_socket_released(inp->id);
				ui_nodes_socket_released = true;
			}
		}
	}
}

void ui_node_draw(ui_node_t *node, ui_node_canvas_t *canvas) {
	ui_t *current = ui_get_current();
	float wx      = current->_window_x;
	float wy      = current->_window_y;
	float ui_x    = current->_x;
	float ui_y    = current->_y;
	float ui_w    = current->_w;
	float w       = UI_NODE_W(node);
	float h       = UI_NODE_H(canvas, node);
	float nx      = UI_NODE_X(node);
	float ny      = UI_NODE_Y(node);
	char *text    = ui_tr(node->name);
	float lineh   = UI_LINE_H();

	// Disallow input if node is overlapped by another node
	current_nodes->_input_started = current->input_started;
	if (current->input_started) {
		for (int i = ui_get_node_index(canvas->nodes->buffer, canvas->nodes->length, node->id) + 1; i < canvas->nodes->length; ++i) {
			ui_node_t *n = canvas->nodes->buffer[i];
			if (UI_NODE_X(n) < current->input_x - current->_window_x && UI_NODE_X(n) + UI_NODE_W(n) > current->input_x - current->_window_x &&
			    UI_NODE_Y(n) < current->input_y - current->_window_y && UI_NODE_Y(n) + UI_NODE_H(canvas, n) > current->input_y - current->_window_y) {
				current->input_started = false;
				break;
			}
		}
	}

	// Grid snap preview
	if (ui_nodes_grid_snap && ui_is_selected(node) && current_nodes->nodes_drag) {
		draw_set_color(current->ops->theme->BUTTON_COL);
		ui_draw_rect(false, true, ui_nodes_snap(node->x) + UI_NODES_PAN_X(), ui_nodes_snap(node->y) + UI_NODES_PAN_Y(), w + 2, h + 2);
		// nx = ui_nodes_snap(node->x) + UI_NODES_PAN_X();
		// ny = ui_nodes_snap(node->y) + UI_NODES_PAN_Y();
	}

	// Node preview
	if ((node->flags & UI_NODE_FLAG_PREVIEW) && ui_nodes_preview_texture != NULL) {
		// ui_draw_shadow(nx, ny - w, w * 0.98, w * 0.98);
		gpu_texture_t *image = ui_nodes_preview_texture(node);
		if (image != NULL) {
			draw_set_color(0xffffffff);
			float ph = w * (image->height / (float)image->width);
			draw_scaled_image(image, nx, ny - ph, w, ph);
		}
	}

	// Shadow
	ui_draw_shadow(nx, ny, w, h);

	// Outline
	draw_set_color(ui_is_selected(node) ? current->ops->theme->HIGHLIGHT_COL : current->ops->theme->PRESSED_COL);
	ui_draw_rect(true, true, nx - 1, ny - 1, w + 2, h + 2);

	// Body
	draw_set_color(current->ops->theme->WINDOW_BG_COL);
	ui_draw_rect(true, true, nx, ny, w, h);

	// Header line
	draw_set_color(node->color);
	draw_filled_rect(nx, ny + lineh - ui_p(3), w, ui_p(3));

	// Collapse button
	bool hover = current->input_x > wx + nx && current->input_x < wx + nx + ui_p(20) && current->input_y > wy + ny && current->input_y < wy + ny + ui_p(20);
	if (hover && current->input_started) {
		node->flags ^= UI_NODE_FLAG_COLLAPSED;
	}
	draw_set_color(hover ? current->ops->theme->LABEL_COL : current->ops->theme->HOVER_COL);
	int bx = nx + ui_p(4);
	int by = ny + ui_p(8);
	draw_filled_triangle(bx, by, bx + UI_ARROW_SIZE() * 2, by, bx + UI_ARROW_SIZE(), by + UI_ARROW_SIZE());

	// Eye button
	hover = current->input_x > wx + nx + w - ui_p(20) && current->input_x < wx + nx + w && current->input_y > wy + ny && current->input_y < wy + ny + ui_p(20);
	if (hover && current->input_started) {
		node->flags ^= UI_NODE_FLAG_PREVIEW;
	}
	draw_set_color(hover ? current->ops->theme->LABEL_COL : current->ops->theme->HOVER_COL);
	float       ex         = nx + w - ui_p(10);
	float       ey         = ny + ui_p(10);
	float       hw         = ui_p(6);
	float       hh         = ui_p(3);
	float       x[4]       = {ex - hw, ex - hw * 0.5, ex + hw * 0.5, ex + hw};
	float       y_upper[4] = {ey, ey - hh * 1.5, ey - hh * 1.5, ey};
	f32_array_t xa;
	f32_array_t ya;
	xa.buffer = x;
	ya.buffer = y_upper;
	draw_cubic_bezier(&xa, &ya, 15, 1.0);
	float y_lower[4] = {ey, ey + hh * 1.5, ey + hh * 1.5, ey};
	ya.buffer        = y_lower;
	draw_cubic_bezier(&xa, &ya, 15, 1.0);
	draw_filled_circle(ex, ey, ui_p(2), 0);

	// Title
	draw_set_color(current->ops->theme->TEXT_COL);
	// float textw = draw_string_width(current->ops->font, current->font_size, text);
	draw_string(text, nx + ui_p(20), ny + ui_p(6));
	ny += lineh * 0.5;

	if (!(node->flags & UI_NODE_FLAG_COLLAPSED)) {
		bool _changed    = current->changed;
		current->changed = false;
		ui_node_draw_body(node, canvas, nx, ny);
		if (ui_nodes_on_node_changed != NULL && current->changed) {
			ui_nodes_on_node_changed(node);
		}
		current->changed = _changed || current->changed;
	}

	current->_x            = ui_x;
	current->_y            = ui_y;
	current->_w            = ui_w;
	current->input_started = current_nodes->_input_started;
}

bool ui_is_node_type_excluded(char *type) {
	for (int i = 0; i < ui_nodes_exclude_remove->length; ++i) {
		if (strcmp(ui_nodes_exclude_remove->buffer[i], type) == 0) {
			return true;
		}
	}
	return false;
}

void ui_node_canvas(ui_nodes_t *nodes, ui_node_canvas_t *canvas) {
	current_nodes = nodes;
	ui_t *current = ui_get_current();
	if (!ui_nodes_elements_baked) {
		gpu_texture_t *current = _draw_current;
		draw_end();
		ui_nodes_bake_elements();
		draw_begin(current, false, 0);
	}
	if (ui_nodes_exclude_remove == NULL) {
		ui_nodes_exclude_remove = gc_alloc(sizeof(string_array_t));
		gc_root(ui_nodes_exclude_remove);
	}

	float wx                      = current->_window_x;
	float wy                      = current->_window_y;
	bool  _input_enabled          = current->input_enabled;
	current->input_enabled        = _input_enabled && ui_popup_commands == NULL;
	ui_canvas_control_t *controls = ui_nodes_on_canvas_control != NULL ? ui_nodes_on_canvas_control() : ui_on_default_canvas_control();
	ui_nodes_socket_released      = false;

	// Pan canvas
	if (current->input_enabled && (controls->pan_x != 0.0 || controls->pan_y != 0.0)) {
		current_nodes->pan_x += controls->pan_x / UI_NODES_SCALE();
		current_nodes->pan_y += controls->pan_y / UI_NODES_SCALE();
	}

	// Zoom canvas
	if (current->input_enabled && controls->zoom != 0.0) {
		current_nodes->zoom += controls->zoom;
		if (current_nodes->zoom < 0.1) {
			current_nodes->zoom = 0.1;
		}
		else if (current_nodes->zoom > 1.0) {
			current_nodes->zoom = 1.0;
		}
		current_nodes->zoom = round(current_nodes->zoom * 100.0) / 100.0;
		current_nodes->uiw  = current->_w;
		current_nodes->uih  = current->_h;
		if (ui_touch_control) {
			// Zoom to finger location
			current_nodes->pan_x -= (current->input_x - current->_window_x - current->_window_w / 2.0) * controls->zoom * 5.0 * (1.0 - current_nodes->zoom);
			current_nodes->pan_y -= (current->input_y - current->_window_y - current->_window_h / 2.0) * controls->zoom * 5.0 * (1.0 - current_nodes->zoom);
		}
	}
	current_nodes->scale_factor = UI_SCALE();
	current_nodes->ELEMENT_H    = current->ops->theme->ELEMENT_H + 2;
	ui_set_scale(UI_NODES_SCALE()); // Apply zoomed scale
	current->elements_baked = true;
	draw_set_font(current->ops->font, current->font_size);

	for (int i = 0; i < canvas->links->length; ++i) {
		ui_node_link_t *link   = canvas->links->buffer[i];
		ui_node_t      *from   = ui_get_node(canvas->nodes, link->from_id);
		ui_node_t      *to     = ui_get_node(canvas->nodes, link->to_id);
		float           from_x = from == NULL ? current->input_x : wx + UI_NODE_X(from) + UI_NODE_W(from);
		float           from_y = from == NULL ? current->input_y : wy + UI_NODE_Y(from) + UI_OUTPUT_Y(from, link->from_socket);
		float           to_x   = to == NULL ? current->input_x : wx + UI_NODE_X(to);
		float to_y = to == NULL ? current->input_y : wy + UI_NODE_Y(to) + UI_INPUT_Y(canvas, to, link->to_socket) + UI_OUTPUTS_H(to, -1) + UI_BUTTONS_H(to);

		// Cull
		float left   = to_x > from_x ? from_x : to_x;
		float right  = to_x > from_x ? to_x : from_x;
		float top    = to_y > from_y ? from_y : to_y;
		float bottom = to_y > from_y ? to_y : from_y;
		if (right < 0 || left > wx + current->_window_w || bottom < 0 || top > wy + current->_window_h) {
			continue;
		}

		// Snap to nearest socket
		if (current_nodes->link_drag_id == link->id) {
			if (current_nodes->snap_from_id != -1) {
				from_x = current_nodes->snap_x;
				from_y = current_nodes->snap_y;
			}
			if (current_nodes->snap_to_id != -1) {
				to_x = current_nodes->snap_x;
				to_y = current_nodes->snap_y;
			}
			current_nodes->snap_from_id = current_nodes->snap_to_id = -1;

			for (int j = 0; j < canvas->nodes->length; ++j) {
				ui_node_t *node   = canvas->nodes->buffer[j];
				float      node_h = UI_NODE_H(canvas, node);
				float      rx     = wx + UI_NODE_X(node) - UI_LINE_H() / 2;
				float      ry     = wy + UI_NODE_Y(node) - UI_LINE_H() / 2;
				float      rw     = UI_NODE_W(node) + UI_LINE_H();
				float      rh     = node_h + UI_LINE_H();
				if (ui_input_in_rect(rx, ry, rw, rh)) {
					if (from == NULL && node->id != to->id) { // Snap to output
						for (int k = 0; k < node->outputs->length; ++k) {
							float sx = wx + UI_NODE_X(node) + UI_NODE_W(node);
							float sy = wy + UI_NODE_Y(node) + UI_OUTPUT_Y(node, k);
							float rx = sx - UI_LINE_H() / 2;
							float ry = sy - UI_LINE_H() / 2;
							if (ui_input_in_rect(rx, ry, UI_LINE_H(), UI_LINE_H())) {
								current_nodes->snap_x       = sx;
								current_nodes->snap_y       = sy;
								current_nodes->snap_from_id = node->id;
								current_nodes->snap_socket  = k;
								break;
							}
						}
					}
					else if (to == NULL && node->id != from->id) { // Snap to input
						for (int k = 0; k < node->inputs->length; ++k) {
							float sx = wx + UI_NODE_X(node);
							float sy = wy + UI_NODE_Y(node) + UI_INPUT_Y(canvas, node, k) + UI_OUTPUTS_H(node, -1) + UI_BUTTONS_H(node);
							float rx = sx - UI_LINE_H() / 2.0;
							float ry = sy - UI_LINE_H() / 2.0;
							if (ui_input_in_rect(rx, ry, UI_LINE_H(), UI_LINE_H())) {
								current_nodes->snap_x      = sx;
								current_nodes->snap_y      = sy;
								current_nodes->snap_to_id  = node->id;
								current_nodes->snap_socket = k;
								break;
							}
						}
					}
				}
			}
		}

		bool selected = false;
		for (int j = 0; j < current_nodes->nodes_selected_id->length; ++j) {
			int n_id = current_nodes->nodes_selected_id->buffer[j];
			if (link->from_id == n_id || link->to_id == n_id) {
				selected = true;
				break;
			}
		}

		ui_draw_link(from_x - wx, from_y - wy, to_x - wx, to_y - wy, selected);
	}

	for (int i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *node = canvas->nodes->buffer[i];

		// Cull
		float preview_h = (node->flags & UI_NODE_FLAG_PREVIEW) ? UI_NODE_W(node) : 0.0;
		if (UI_NODE_X(node) > current->_window_w || UI_NODE_X(node) + UI_NODE_W(node) < 0 || UI_NODE_Y(node) - preview_h > current->_window_h ||
		    UI_NODE_Y(node) + UI_NODE_H(canvas, node) < 0) {
			if (!ui_is_selected(node)) {
				continue;
			}
		}

		// Resize node
		float node_h = UI_NODE_H(canvas, node);
		float top_h  = UI_LINE_H() + UI_OUTPUTS_H(node, -1) + ui_p(5);
		if (current->input_enabled && (ui_input_in_rect(wx + UI_NODE_X(node) + UI_NODE_W(node), wy + UI_NODE_Y(node), 5, UI_LINE_H()) ||
		                               ui_input_in_rect(wx + UI_NODE_X(node) + UI_NODE_W(node), wy + UI_NODE_Y(node) + top_h, 5, node_h - top_h))) {

			iron_mouse_set_cursor(IRON_CURSOR_SIZEWE);
			if (current->input_started) {
				node_resize = node;
			}
		}

		// Drag node
		if (current->input_enabled && node_resize == NULL &&
		    ui_input_in_rect(wx + UI_NODE_X(node) - UI_LINE_H() / 2.0, wy + UI_NODE_Y(node), UI_NODE_W(node) + UI_LINE_H(), UI_LINE_H())) {
			if (current->input_started) {
				if (current->is_shift_down || current->is_ctrl_down) {
					// Add to selection or deselect
					if (ui_is_selected(node)) {
						remove_from_selection(node);
					}
					else {
						add_to_selection(node);
					}
				}
				else if (current_nodes->nodes_selected_id->length <= 1) {
					// Selecting single node, otherwise wait for input release
					current_nodes->nodes_selected_id->length = 0;
					i32_array_push(current_nodes->nodes_selected_id, node->id);
				}
				current_nodes->move_on_top = node; // Place selected node on top
				current_nodes->nodes_drag  = true;
				current_nodes->dragged     = false;
			}
			else if (current->input_released && !current->is_shift_down && !current->is_ctrl_down && !current_nodes->dragged) {
				// No drag performed, select single node
				current_nodes->nodes_selected_id->length = 0;
				i32_array_push(current_nodes->nodes_selected_id, node->id);
			}
		}

		if (current->input_started && !(node->flags & UI_NODE_FLAG_COLLAPSED) &&
		    ui_input_in_rect(wx + UI_NODE_X(node) - UI_LINE_H() / 2, wy + UI_NODE_Y(node) - UI_LINE_H() / 2, UI_NODE_W(node) + UI_LINE_H(),
		                     node_h + UI_LINE_H())) {
			// Check sockets
			if (current_nodes->link_drag_id == -1) {
				for (int j = 0; j < node->outputs->length; ++j) {
					float sx = wx + UI_NODE_X(node) + UI_NODE_W(node);
					float sy = wy + UI_NODE_Y(node) + UI_OUTPUT_Y(node, j);
					if (ui_input_in_rect(sx - UI_LINE_H() / 2.0, sy - UI_LINE_H() / 2.0, UI_LINE_H(), UI_LINE_H())) {
						// New link from output
						ui_node_link_t *l = (ui_node_link_t *)malloc(sizeof(ui_node_link_t)); // TODO: store at canvas->links without malloc
						l->id             = ui_next_link_id(canvas->links);
						l->from_id        = node->id;
						l->from_socket    = j;
						l->to_id          = -1;
						l->to_socket      = -1;
						any_array_push(canvas->links, l);
						current_nodes->link_drag_id = l->id;
						current_nodes->is_new_link  = true;
						break;
					}
				}
			}
			if (current_nodes->link_drag_id == -1) {
				for (int j = 0; j < node->inputs->length; ++j) {
					float sx = wx + UI_NODE_X(node);
					float sy = wy + UI_NODE_Y(node) + UI_INPUT_Y(canvas, node, j) + UI_OUTPUTS_H(node, -1) + UI_BUTTONS_H(node);
					if (ui_input_in_rect(sx - UI_LINE_H() / 2.0, sy - UI_LINE_H() / 2.0, UI_LINE_H(), UI_LINE_H())) {
						// Already has a link - disconnect
						for (int k = 0; k < canvas->links->length; ++k) {
							ui_node_link_t *l = canvas->links->buffer[k];
							if (l->to_id == node->id && l->to_socket == j) {
								l->to_id = l->to_socket     = -1;
								current_nodes->link_drag_id = l->id;
								current_nodes->is_new_link  = false;
								if (ui_nodes_on_node_changed != NULL) {
									ui_nodes_on_node_changed(node);
								}
								break;
							}
						}
						if (current_nodes->link_drag_id != -1) {
							break;
						}
						// New link from input
						ui_node_link_t *l = (ui_node_link_t *)malloc(sizeof(ui_node_link_t));
						l->id             = ui_next_link_id(canvas->links);
						l->from_id        = -1;
						l->from_socket    = -1;
						l->to_id          = node->id;
						l->to_socket      = j;
						any_array_push(canvas->links, l);
						current_nodes->link_drag_id = l->id;
						current_nodes->is_new_link  = true;
						break;
					}
				}
			}
		}
		else if (current->input_released) {
			if (current_nodes->snap_to_id != -1) { // Connect to input
				// Force single link per input
				for (int j = 0; j < canvas->links->length; ++j) {
					ui_node_link_t *l = canvas->links->buffer[j];
					if (l->to_id == current_nodes->snap_to_id && l->to_socket == current_nodes->snap_socket) {
						ui_remove_link_at(canvas, ui_get_link_index(canvas->links, l->id));
						break;
					}
				}
				ui_node_link_t *link_drag = ui_get_link(canvas->links, current_nodes->link_drag_id);
				link_drag->to_id          = current_nodes->snap_to_id;
				link_drag->to_socket      = current_nodes->snap_socket;
				current->changed          = true;
				if (ui_nodes_on_node_changed != NULL) {
					ui_nodes_on_node_changed(ui_get_node(canvas->nodes, link_drag->to_id));
				}
			}
			else if (current_nodes->snap_from_id != -1) { // Connect to output
				ui_node_link_t *link_drag = ui_get_link(canvas->links, current_nodes->link_drag_id);
				link_drag->from_id        = current_nodes->snap_from_id;
				link_drag->from_socket    = current_nodes->snap_socket;
				current->changed          = true;
				if (ui_nodes_on_node_changed != NULL) {
					ui_nodes_on_node_changed(ui_get_node(canvas->nodes, link_drag->to_id));
				}
			}
			else if (current_nodes->link_drag_id != -1) { // Remove dragged link
				current->changed = true;
				if (ui_nodes_on_link_drag != NULL) {
					ui_nodes_on_link_drag(current_nodes->link_drag_id, current_nodes->is_new_link);
				}
				ui_remove_link_at(canvas, ui_get_link_index(canvas->links, current_nodes->link_drag_id));
			}
			current_nodes->snap_to_id = current_nodes->snap_from_id = -1;
			current_nodes->link_drag_id                             = -1;
			current_nodes->nodes_drag                               = false;
		}

		if (current_nodes->nodes_drag && ui_is_selected(node) && !current->input_down_r) {
			if (current->input_dx != 0 || current->input_dy != 0) {
				current_nodes->dragged = true;
				node->x += current->input_dx / UI_NODES_SCALE();
				node->y += current->input_dy / UI_NODES_SCALE();
				// Absolute
				// node->x = (current->input_x - current->_window_x - UI_NODES_PAN_X()) / UI_NODES_SCALE();
				// node->y = (current->input_y - current->_window_y - UI_NODES_PAN_Y()) / UI_NODES_SCALE();
			}
		}
		if (ui_nodes_grid_snap && current->input_released && ui_is_selected(node)) {
			node->x = ui_nodes_snap(node->x) / UI_NODES_SCALE();
			node->y = ui_nodes_snap(node->y) / UI_NODES_SCALE();
		}

		ui_node_draw(node, canvas);
	}

	if (ui_nodes_on_canvas_released != NULL && current->input_enabled && (current->input_released || current->input_released_r) && !ui_nodes_socket_released) {
		ui_nodes_on_canvas_released();
	}

	if (node_resize != NULL) {
		iron_mouse_set_cursor(IRON_CURSOR_SIZEWE);
		node_resize->width += current->input_dx / UI_NODES_SCALE();
		if (node_resize->width < 140) {
			node_resize->width = 140;
		}
		if (current->input_released) {
			node_resize = NULL;
		}
	}
	else {
		if (ui_box_select) {
			draw_set_color(0x223333dd);
			draw_filled_rect(ui_box_select_x, ui_box_select_y, current->input_x - ui_box_select_x - current->_window_x,
			                 current->input_y - ui_box_select_y - current->_window_y);
			draw_set_color(0x773333dd);
			draw_rect(ui_box_select_x, ui_box_select_y, current->input_x - ui_box_select_x - current->_window_x,
			          current->input_y - ui_box_select_y - current->_window_y, 1);
			draw_set_color(0xffffffff);
		}
		if (current->input_enabled && current->input_started && !current->is_alt_down && current_nodes->link_drag_id == -1 && !current_nodes->nodes_drag &&
		    !current->changed && ui_input_in_rect(current->_window_x, current->_window_y, current->_window_w, current->_window_h)) {

			if (ui_touch_control) {
				ui_touch_drag = true;
			}
			else {
				ui_box_select   = true;
				ui_box_select_x = current->input_x - current->_window_x;
				ui_box_select_y = current->input_y - current->_window_y;
			}
		}
		if (ui_touch_drag) {
			if (!current->input_down) {
				ui_touch_drag = false;
			}
			current_nodes->pan_x += current->input_dx / UI_NODES_SCALE();
			current_nodes->pan_y += current->input_dy / UI_NODES_SCALE();
		}
		if (ui_box_select && !current->input_down) {
			ui_box_select = false;
			int left      = ui_box_select_x;
			int top       = ui_box_select_y;
			int right     = current->input_x - current->_window_x;
			int bottom    = current->input_y - current->_window_y;
			if (left > right) {
				int t = left;
				left  = right;
				right = t;
			}
			if (top > bottom) {
				int t  = top;
				top    = bottom;
				bottom = t;
			}
			ui_node_t *nodes[32];
			int        nodes_count = 0;
			for (int j = 0; j < canvas->nodes->length; ++j) {
				ui_node_t *n = canvas->nodes->buffer[j];
				if (UI_NODE_X(n) + UI_NODE_W(n) > left && UI_NODE_X(n) < right && UI_NODE_Y(n) + UI_NODE_H(canvas, n) > top && UI_NODE_Y(n) < bottom) {
					nodes[nodes_count] = n;
					nodes_count++;
				}
			}
			if (current->is_shift_down || current->is_ctrl_down) {
				for (int j = 0; j < nodes_count; ++j) {
					add_to_selection(nodes[j]);
				}
			}
			else {
				current_nodes->nodes_selected_id->length = 0;
				for (int j = 0; j < nodes_count; ++j) {
					add_to_selection(nodes[j]);
				}
			}
		}
	}

	// Place selected node on top
	if (current_nodes->move_on_top != NULL) {
		int index = ui_get_node_index(canvas->nodes->buffer, canvas->nodes->length, current_nodes->move_on_top->id);
		ui_remove_node_at(canvas, index);
		any_array_push(canvas->nodes, current_nodes->move_on_top);
		current_nodes->move_on_top = NULL;
	}

	// Node copy & paste
	bool cut_selected = false;
	if (ui_is_copy && !current->is_typing) {
		ui_node_t *copy_nodes[32];
		int        copy_nodes_count = 0;
		for (int i = 0; i < current_nodes->nodes_selected_id->length; ++i) {
			int        id = current_nodes->nodes_selected_id->buffer[i];
			ui_node_t *n  = ui_get_node(canvas->nodes, id);
			if (ui_is_node_type_excluded(n->type)) {
				continue;
			}
			copy_nodes[copy_nodes_count] = n;
			copy_nodes_count++;
		}
		ui_node_link_t *copy_links[64];
		int             copy_links_count = 0;
		for (int i = 0; i < canvas->links->length; ++i) {
			ui_node_link_t *l    = canvas->links->buffer[i];
			ui_node_t      *from = NULL;
			for (int j = 0; j < current_nodes->nodes_selected_id->length; ++j) {
				if (current_nodes->nodes_selected_id->buffer[j] == l->from_id) {
					from = ui_get_node(canvas->nodes, l->from_id);
					break;
				}
			}
			ui_node_t *to = NULL;
			for (int j = 0; j < current_nodes->nodes_selected_id->length; ++j) {
				if (current_nodes->nodes_selected_id->buffer[j] == l->to_id) {
					to = ui_get_node(canvas->nodes, l->to_id);
					break;
				}
			}

			if (from != NULL && !ui_is_node_type_excluded(from->type) && to != NULL && !ui_is_node_type_excluded(to->type)) {
				copy_links[copy_links_count] = l;
				copy_links_count++;
			}
		}
		if (copy_nodes_count > 0) {
			ui_node_canvas_t copy_canvas = {0};
			copy_canvas.name             = canvas->name;
			ui_node_array_t      nodes   = {.buffer = copy_nodes, .length = copy_nodes_count};
			ui_node_link_array_t links   = {.buffer = copy_links, .length = copy_links_count};
			copy_canvas.nodes            = &nodes;
			copy_canvas.links            = &links;
			gc_unroot(ui_clipboard);
			ui_clipboard = ui_node_canvas_to_json(&copy_canvas);
			gc_root(ui_clipboard);
			iron_copy_to_clipboard(ui_clipboard);
		}
		cut_selected = ui_is_cut;
		ui_is_copy   = false;
		ui_is_cut    = false;
	}

	if (ui_is_paste && !current->is_typing) {
		ui_is_paste = false;

		bool is_json = ui_clipboard[0] == '{';
		if (is_json) {

			ui_node_canvas_t *paste_canvas = json_parse(ui_clipboard);
			gc_root(paste_canvas); // TODO

			// Convert button data from string to u8 array
			for (int i = 0; i < paste_canvas->nodes->length; ++i) {
				for (int j = 0; j < paste_canvas->nodes->buffer[i]->buttons->length; ++j) {
					ui_node_button_t *but = paste_canvas->nodes->buffer[i]->buttons->buffer[j];
					if (but->data != NULL) {
						char *s   = string_replace_all(but->data, "\\n", "\n");
						but->data = u8_array_create_from_raw((uint8_t *)s, strlen(s) + 1);
					}
				}
			}

			for (int i = 0; i < paste_canvas->links->length; ++i) {
				ui_node_link_t *l = paste_canvas->links->buffer[i];
				// Assign unique link id
				l->id = ui_next_link_id(canvas->links);
				any_array_push(canvas->links, l);
			}
			int offset_x = (int)(((int)(current->input_x / UI_SCALE()) * UI_NODES_SCALE() - wx - UI_NODES_PAN_X()) / UI_NODES_SCALE()) -
			               paste_canvas->nodes->buffer[paste_canvas->nodes->length - 1]->x;
			int offset_y = (int)(((int)(current->input_y / UI_SCALE()) * UI_NODES_SCALE() - wy - UI_NODES_PAN_Y()) / UI_NODES_SCALE()) -
			               paste_canvas->nodes->buffer[paste_canvas->nodes->length - 1]->y;
			for (int i = 0; i < paste_canvas->nodes->length; ++i) {
				ui_node_t *n = paste_canvas->nodes->buffer[i];
				// Assign unique node id
				int old_id = n->id;
				n->id      = ui_next_node_id(canvas->nodes);

				for (int j = 0; j < n->inputs->length; ++j) {
					ui_node_socket_t *soc = n->inputs->buffer[j];
					soc->id               = ui_get_socket_id(canvas->nodes);
					soc->node_id          = n->id;
				}
				for (int j = 0; j < n->outputs->length; ++j) {
					ui_node_socket_t *soc = n->outputs->buffer[j];
					soc->id               = ui_get_socket_id(canvas->nodes);
					soc->node_id          = n->id;
				}
				for (int j = 0; j < paste_canvas->links->length; ++j) {
					ui_node_link_t *l = paste_canvas->links->buffer[j];
					if (l->from_id == old_id)
						l->from_id = n->id;
					else if (l->to_id == old_id)
						l->to_id = n->id;
				}
				n->x += offset_x;
				n->y += offset_y;
				any_array_push(canvas->nodes, n);
			}
			current_nodes->nodes_drag                = true;
			current_nodes->nodes_selected_id->length = 0;
			for (int i = 0; i < paste_canvas->nodes->length; ++i) {
				i32_array_push(current_nodes->nodes_selected_id, paste_canvas->nodes->buffer[i]->id);
			}
			current->changed = true;
		}
	}

	// Select all nodes
	if (current->is_ctrl_down && current->key_code == KEY_CODE_A && !current->is_typing) {
		current_nodes->nodes_selected_id->length = 0;
		for (int i = 0; i < canvas->nodes->length; ++i) {
			add_to_selection(canvas->nodes->buffer[i]);
		}
	}

	// Node removal
	bool node_removal = current->input_enabled && (current->is_backspace_down || current->is_delete_down) && !current->is_typing;
	if (node_removal || cut_selected) {
		int i = current_nodes->nodes_selected_id->length - 1;
		while (i >= 0) {
			int        nid = current_nodes->nodes_selected_id->buffer[i--];
			ui_node_t *n   = ui_get_node(canvas->nodes, nid);
			if (ui_is_node_type_excluded(n->type)) {
				n->flags ^= UI_NODE_FLAG_COLLAPSED;
				continue;
			}
			ui_remove_node(n, canvas);
			current->changed = true;
		}
		current_nodes->nodes_selected_id->length = 0;
	}

	// Node duplicate
	bool node_duplicate = current->input_enabled && current->is_ctrl_down && current->is_key_pressed && current->key_code == KEY_CODE_D && !current->is_typing;
	if (current_nodes->nodes_selected_id->length > 0 && node_duplicate) {
		ui_is_copy  = true;
		ui_is_paste = true;
	}

	ui_set_scale(current_nodes->scale_factor); // Restore non-zoomed scale
	current->elements_baked = true;
	current->input_enabled  = _input_enabled;

	if (ui_popup_commands != NULL) {
		current->_x = ui_popup_x;
		current->_y = ui_popup_y;
		current->_w = ui_popup_w;

		ui_draw_shadow(current->_x - 5, current->_y - 5, current->_w + 10, ui_popup_h * UI_SCALE() + 10);
		draw_set_color(current->ops->theme->SEPARATOR_COL);
		ui_draw_rect(true, true, current->_x - 5, current->_y - 5, current->_w + 10, ui_popup_h * UI_SCALE() + 10);
		(*ui_popup_commands)(current, ui_popup_data, ui_popup_data2);

		bool hide = (current->input_started || current->input_started_r) &&
		            (current->input_x - wx < ui_popup_x - 6 || current->input_x - wx > ui_popup_x + ui_popup_w + 6 || current->input_y - wy < ui_popup_y - 6 ||
		             current->input_y - wy > ui_popup_y + ui_popup_h * UI_SCALE() + 6);
		if (hide || current->is_escape_down) {
			ui_popup_commands = NULL;
			ui_nodes_on_node_changed(ui_get_node(canvas->nodes, ui_popup_handle_node_id));
			current->changed = false;
		}
	}
}

void ui_node_canvas_encode(ui_node_canvas_t *canvas) {
	// armpack_encode_start(encoded);
	armpack_encode_map(3);
	armpack_encode_string("name");
	armpack_encode_string(canvas->name);

	armpack_encode_string("nodes");
	armpack_encode_array(canvas->nodes->length);
	for (int i = 0; i < canvas->nodes->length; ++i) {
		armpack_encode_map(11);
		armpack_encode_string("id");
		armpack_encode_i32(canvas->nodes->buffer[i]->id);
		armpack_encode_string("name");
		armpack_encode_string(canvas->nodes->buffer[i]->name);
		armpack_encode_string("type");
		armpack_encode_string(canvas->nodes->buffer[i]->type);
		armpack_encode_string("x");
		armpack_encode_f32(canvas->nodes->buffer[i]->x);
		armpack_encode_string("y");
		armpack_encode_f32(canvas->nodes->buffer[i]->y);
		armpack_encode_string("color");
		armpack_encode_i32(canvas->nodes->buffer[i]->color);

		armpack_encode_string("inputs");
		armpack_encode_array(canvas->nodes->buffer[i]->inputs->length);
		for (int j = 0; j < canvas->nodes->buffer[i]->inputs->length; ++j) {
			armpack_encode_map(10);
			armpack_encode_string("id");
			armpack_encode_i32(canvas->nodes->buffer[i]->inputs->buffer[j]->id);
			armpack_encode_string("node_id");
			armpack_encode_i32(canvas->nodes->buffer[i]->inputs->buffer[j]->node_id);
			armpack_encode_string("name");
			armpack_encode_string(canvas->nodes->buffer[i]->inputs->buffer[j]->name);
			armpack_encode_string("type");
			armpack_encode_string(canvas->nodes->buffer[i]->inputs->buffer[j]->type);
			armpack_encode_string("color");
			armpack_encode_i32(canvas->nodes->buffer[i]->inputs->buffer[j]->color);
			armpack_encode_string("default_value");
			armpack_encode_array_f32(canvas->nodes->buffer[i]->inputs->buffer[j]->default_value);
			armpack_encode_string("min");
			armpack_encode_f32(canvas->nodes->buffer[i]->inputs->buffer[j]->min);
			armpack_encode_string("max");
			armpack_encode_f32(canvas->nodes->buffer[i]->inputs->buffer[j]->max);
			armpack_encode_string("precision");
			armpack_encode_f32(canvas->nodes->buffer[i]->inputs->buffer[j]->precision);
			armpack_encode_string("display");
			armpack_encode_i32(canvas->nodes->buffer[i]->inputs->buffer[j]->display);
		}

		armpack_encode_string("outputs");
		armpack_encode_array(canvas->nodes->buffer[i]->outputs->length);
		for (int j = 0; j < canvas->nodes->buffer[i]->outputs->length; ++j) {
			armpack_encode_map(10);
			armpack_encode_string("id");
			armpack_encode_i32(canvas->nodes->buffer[i]->outputs->buffer[j]->id);
			armpack_encode_string("node_id");
			armpack_encode_i32(canvas->nodes->buffer[i]->outputs->buffer[j]->node_id);
			armpack_encode_string("name");
			armpack_encode_string(canvas->nodes->buffer[i]->outputs->buffer[j]->name);
			armpack_encode_string("type");
			armpack_encode_string(canvas->nodes->buffer[i]->outputs->buffer[j]->type);
			armpack_encode_string("color");
			armpack_encode_i32(canvas->nodes->buffer[i]->outputs->buffer[j]->color);
			armpack_encode_string("default_value");
			armpack_encode_array_f32(canvas->nodes->buffer[i]->outputs->buffer[j]->default_value);
			armpack_encode_string("min");
			armpack_encode_f32(canvas->nodes->buffer[i]->outputs->buffer[j]->min);
			armpack_encode_string("max");
			armpack_encode_f32(canvas->nodes->buffer[i]->outputs->buffer[j]->max);
			armpack_encode_string("precision");
			armpack_encode_f32(canvas->nodes->buffer[i]->outputs->buffer[j]->precision);
			armpack_encode_string("display");
			armpack_encode_i32(canvas->nodes->buffer[i]->outputs->buffer[j]->display);
		}

		armpack_encode_string("buttons");
		armpack_encode_array(canvas->nodes->buffer[i]->buttons->length);
		for (int j = 0; j < canvas->nodes->buffer[i]->buttons->length; ++j) {
			armpack_encode_map(9);
			armpack_encode_string("name");
			armpack_encode_string(canvas->nodes->buffer[i]->buttons->buffer[j]->name);
			armpack_encode_string("type");
			armpack_encode_string(canvas->nodes->buffer[i]->buttons->buffer[j]->type);
			armpack_encode_string("output");
			armpack_encode_i32(canvas->nodes->buffer[i]->buttons->buffer[j]->output);
			armpack_encode_string("default_value");
			armpack_encode_array_f32(canvas->nodes->buffer[i]->buttons->buffer[j]->default_value);
			armpack_encode_string("data");
			u8_array_t *u8 = canvas->nodes->buffer[i]->buttons->buffer[j]->data;
			armpack_encode_array_u8(u8);
			armpack_encode_string("min");
			armpack_encode_f32(canvas->nodes->buffer[i]->buttons->buffer[j]->min);
			armpack_encode_string("max");
			armpack_encode_f32(canvas->nodes->buffer[i]->buttons->buffer[j]->max);
			armpack_encode_string("precision");
			armpack_encode_f32(canvas->nodes->buffer[i]->buttons->buffer[j]->precision);
			armpack_encode_string("height");
			armpack_encode_f32(canvas->nodes->buffer[i]->buttons->buffer[j]->height);
		}

		armpack_encode_string("width");
		armpack_encode_f32(canvas->nodes->buffer[i]->width);
		armpack_encode_string("flags");
		armpack_encode_i32(canvas->nodes->buffer[i]->flags);
	}

	armpack_encode_string("links");
	armpack_encode_array(canvas->links->length);
	for (int i = 0; i < canvas->links->length; ++i) {
		armpack_encode_map(5);
		armpack_encode_string("id");
		armpack_encode_i32(canvas->links->buffer[i]->id);
		armpack_encode_string("from_id");
		armpack_encode_i32(canvas->links->buffer[i]->from_id);
		armpack_encode_string("from_socket");
		armpack_encode_i32(canvas->links->buffer[i]->from_socket);
		armpack_encode_string("to_id");
		armpack_encode_i32(canvas->links->buffer[i]->to_id);
		armpack_encode_string("to_socket");
		armpack_encode_i32(canvas->links->buffer[i]->to_socket);
	}
}

uint32_t ui_node_canvas_encoded_size(ui_node_canvas_t *canvas) {
	uint32_t size = 0;
	size += armpack_size_map();
	size += armpack_size_string("name");
	size += armpack_size_string(canvas->name);

	size += armpack_size_string("nodes");
	size += armpack_size_array();
	for (int i = 0; i < canvas->nodes->length; ++i) {
		size += armpack_size_map();
		size += armpack_size_string("id");
		size += armpack_size_i32();
		size += armpack_size_string("name");
		size += armpack_size_string(canvas->nodes->buffer[i]->name);
		size += armpack_size_string("type");
		size += armpack_size_string(canvas->nodes->buffer[i]->type);
		size += armpack_size_string("x");
		size += armpack_size_f32();
		size += armpack_size_string("y");
		size += armpack_size_f32();
		size += armpack_size_string("color");
		size += armpack_size_i32();

		size += armpack_size_string("inputs");
		size += armpack_size_array();
		for (int j = 0; j < canvas->nodes->buffer[i]->inputs->length; ++j) {
			size += armpack_size_map();
			size += armpack_size_string("id");
			size += armpack_size_i32();
			size += armpack_size_string("node_id");
			size += armpack_size_i32();
			size += armpack_size_string("name");
			size += armpack_size_string(canvas->nodes->buffer[i]->inputs->buffer[j]->name);
			size += armpack_size_string("type");
			size += armpack_size_string(canvas->nodes->buffer[i]->inputs->buffer[j]->type);
			size += armpack_size_string("color");
			size += armpack_size_i32();
			size += armpack_size_string("default_value");
			size += armpack_size_array_f32(canvas->nodes->buffer[i]->inputs->buffer[j]->default_value);
			size += armpack_size_string("min");
			size += armpack_size_f32();
			size += armpack_size_string("max");
			size += armpack_size_f32();
			size += armpack_size_string("precision");
			size += armpack_size_f32();
			size += armpack_size_string("display");
			size += armpack_size_i32();
		}

		size += armpack_size_string("outputs");
		size += armpack_size_array();
		for (int j = 0; j < canvas->nodes->buffer[i]->outputs->length; ++j) {
			size += armpack_size_map();
			size += armpack_size_string("id");
			size += armpack_size_i32();
			size += armpack_size_string("node_id");
			size += armpack_size_i32();
			size += armpack_size_string("name");
			size += armpack_size_string(canvas->nodes->buffer[i]->outputs->buffer[j]->name);
			size += armpack_size_string("type");
			size += armpack_size_string(canvas->nodes->buffer[i]->outputs->buffer[j]->type);
			size += armpack_size_string("color");
			size += armpack_size_i32();
			size += armpack_size_string("default_value");
			size += armpack_size_array_f32(canvas->nodes->buffer[i]->outputs->buffer[j]->default_value);
			size += armpack_size_string("min");
			size += armpack_size_f32();
			size += armpack_size_string("max");
			size += armpack_size_f32();
			size += armpack_size_string("precision");
			size += armpack_size_f32();
			size += armpack_size_string("display");
			size += armpack_size_i32();
		}

		size += armpack_size_string("buttons");
		size += armpack_size_array();
		for (int j = 0; j < canvas->nodes->buffer[i]->buttons->length; ++j) {
			size += armpack_size_map();
			size += armpack_size_string("name");
			size += armpack_size_string(canvas->nodes->buffer[i]->buttons->buffer[j]->name);
			size += armpack_size_string("type");
			size += armpack_size_string(canvas->nodes->buffer[i]->buttons->buffer[j]->type);
			size += armpack_size_string("output");
			size += armpack_size_i32();
			size += armpack_size_string("default_value");
			size += armpack_size_array_f32(canvas->nodes->buffer[i]->buttons->buffer[j]->default_value);
			size += armpack_size_string("data");
			u8_array_t *u8 = canvas->nodes->buffer[i]->buttons->buffer[j]->data;
			size += armpack_size_array_u8(u8);
			size += armpack_size_string("min");
			size += armpack_size_f32();
			size += armpack_size_string("max");
			size += armpack_size_f32();
			size += armpack_size_string("precision");
			size += armpack_size_f32();
			size += armpack_size_string("height");
			size += armpack_size_f32();
		}

		size += armpack_size_string("width");
		size += armpack_size_f32();
		size += armpack_size_string("flags");
		size += armpack_size_i32();
	}

	size += armpack_size_string("links");
	size += armpack_size_array();
	for (int i = 0; i < canvas->links->length; ++i) {
		size += armpack_size_map();
		size += armpack_size_string("id");
		size += armpack_size_i32();
		size += armpack_size_string("from_id");
		size += armpack_size_i32();
		size += armpack_size_string("from_socket");
		size += armpack_size_i32();
		size += armpack_size_string("to_id");
		size += armpack_size_i32();
		size += armpack_size_string("to_socket");
		size += armpack_size_i32();
	}

	return size;
}

char *ui_node_canvas_to_json(ui_node_canvas_t *canvas) {
	json_encode_begin();
	json_encode_string("name", canvas->name);

	json_encode_begin_array("nodes");
	for (int i = 0; i < canvas->nodes->length; ++i) {
		json_encode_begin_object();
		json_encode_i32("id", canvas->nodes->buffer[i]->id);
		json_encode_string("name", canvas->nodes->buffer[i]->name);
		json_encode_string("type", canvas->nodes->buffer[i]->type);
		json_encode_f32("x", canvas->nodes->buffer[i]->x);
		json_encode_f32("y", canvas->nodes->buffer[i]->y);
		json_encode_i32("color", canvas->nodes->buffer[i]->color);

		json_encode_begin_array("inputs");
		for (int j = 0; j < canvas->nodes->buffer[i]->inputs->length; ++j) {
			json_encode_begin_object();
			json_encode_i32("id", canvas->nodes->buffer[i]->inputs->buffer[j]->id);
			json_encode_i32("node_id", canvas->nodes->buffer[i]->inputs->buffer[j]->node_id);
			json_encode_string("name", canvas->nodes->buffer[i]->inputs->buffer[j]->name);
			json_encode_string("type", canvas->nodes->buffer[i]->inputs->buffer[j]->type);
			json_encode_i32("color", canvas->nodes->buffer[i]->inputs->buffer[j]->color);
			json_encode_f32_array("default_value", canvas->nodes->buffer[i]->inputs->buffer[j]->default_value);
			json_encode_f32("min", canvas->nodes->buffer[i]->inputs->buffer[j]->min);
			json_encode_f32("max", canvas->nodes->buffer[i]->inputs->buffer[j]->max);
			json_encode_f32("precision", canvas->nodes->buffer[i]->inputs->buffer[j]->precision);
			json_encode_i32("display", canvas->nodes->buffer[i]->inputs->buffer[j]->display);
			json_encode_end_object();
		}
		json_encode_end_array();

		json_encode_begin_array("outputs");
		for (int j = 0; j < canvas->nodes->buffer[i]->outputs->length; ++j) {
			json_encode_begin_object();
			json_encode_i32("id", canvas->nodes->buffer[i]->outputs->buffer[j]->id);
			json_encode_i32("node_id", canvas->nodes->buffer[i]->outputs->buffer[j]->node_id);
			json_encode_string("name", canvas->nodes->buffer[i]->outputs->buffer[j]->name);
			json_encode_string("type", canvas->nodes->buffer[i]->outputs->buffer[j]->type);
			json_encode_i32("color", canvas->nodes->buffer[i]->outputs->buffer[j]->color);
			json_encode_f32_array("default_value", canvas->nodes->buffer[i]->outputs->buffer[j]->default_value);
			json_encode_f32("min", canvas->nodes->buffer[i]->outputs->buffer[j]->min);
			json_encode_f32("max", canvas->nodes->buffer[i]->outputs->buffer[j]->max);
			json_encode_f32("precision", canvas->nodes->buffer[i]->outputs->buffer[j]->precision);
			json_encode_i32("display", canvas->nodes->buffer[i]->outputs->buffer[j]->display);
			json_encode_end_object();
		}
		json_encode_end_array();

		json_encode_begin_array("buttons");
		for (int j = 0; j < canvas->nodes->buffer[i]->buttons->length; ++j) {
			json_encode_begin_object();
			json_encode_string("name", canvas->nodes->buffer[i]->buttons->buffer[j]->name);
			json_encode_string("type", canvas->nodes->buffer[i]->buttons->buffer[j]->type);
			json_encode_i32("output", canvas->nodes->buffer[i]->buttons->buffer[j]->output);
			json_encode_f32_array("default_value", canvas->nodes->buffer[i]->buttons->buffer[j]->default_value);
			u8_array_t *data = canvas->nodes->buffer[i]->buttons->buffer[j]->data;
			if (data != NULL) {
				char *s = string_replace_all((char *)data->buffer, "\n", "\\n");
				json_encode_string("data", s);
			}
			else {
				json_encode_null("data");
			}
			json_encode_f32("min", canvas->nodes->buffer[i]->buttons->buffer[j]->min);
			json_encode_f32("max", canvas->nodes->buffer[i]->buttons->buffer[j]->max);
			json_encode_f32("precision", canvas->nodes->buffer[i]->buttons->buffer[j]->precision);
			json_encode_f32("height", canvas->nodes->buffer[i]->buttons->buffer[j]->height);
			json_encode_end_object();
		}
		json_encode_end_array();

		json_encode_f32("width", canvas->nodes->buffer[i]->width);
		json_encode_i32("flags", canvas->nodes->buffer[i]->flags);
		json_encode_end_object();
	}
	json_encode_end_array();

	json_encode_begin_array("links");
	for (int i = 0; i < canvas->links->length; ++i) {
		json_encode_begin_object();
		json_encode_i32("id", canvas->links->buffer[i]->id);
		json_encode_i32("from_id", canvas->links->buffer[i]->from_id);
		json_encode_i32("from_socket", canvas->links->buffer[i]->from_socket);
		json_encode_i32("to_id", canvas->links->buffer[i]->to_id);
		json_encode_i32("to_socket", canvas->links->buffer[i]->to_socket);
		json_encode_end_object();
	}
	json_encode_end_array();

	return json_encode_end();
}

////

f32 ui_nodes_INPUT_Y(ui_node_canvas_t *canvas, ui_node_t *node, i32 pos) {
	return UI_INPUT_Y(canvas, node, pos);
}

extern any_map_t *ui_nodes_custom_buttons;

void nodes_on_custom_button(i32 node_id, char *button_name) {
	void (*f)(i32) = any_map_get(ui_nodes_custom_buttons, button_name);
	f(node_id);
}

ui_nodes_t *ui_nodes_create() {
	ui_nodes_t *raw = GC_ALLOC_INIT(ui_nodes_t, {0});
	ui_nodes_init(raw);
	gc_unroot(ui_nodes_exclude_remove);
	ui_nodes_exclude_remove = any_array_create_from_raw(
	    (void *[]){
	        "OUTPUT_MATERIAL_PBR",
	        "GROUP_OUTPUT",
	        "GROUP_INPUT",
	        "brush_output_node",
	    },
	    4);
	gc_root(ui_nodes_exclude_remove);
	ui_nodes_on_custom_button = nodes_on_custom_button;
	return raw;
}

typedef struct ui_node_t_array {
	ui_node_t **buffer;
	int         length;
	int         capacity;
} ui_node_t_array_t;

ui_node_socket_t *ui_get_socket(ui_node_t_array_t *nodes, i32 id) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		for (i32 j = 0; j < n->inputs->length; ++j) {
			ui_node_socket_t *s = n->inputs->buffer[j];
			if (s->id == id) {
				return s;
			}
		}
		for (i32 j = 0; j < n->outputs->length; ++j) {
			ui_node_socket_t *s = n->outputs->buffer[j];
			if (s->id == id) {
				return s;
			}
		}
	}
	return NULL;
}
