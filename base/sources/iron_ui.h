
// Immediate Mode UI Library

#pragma once

#include "iron_armpack.h"
#include "iron_draw.h"
#include <stdbool.h>
#include <stdint.h>

// #define UI_HANDLE(name)                                                                       \
// 	ui_handle_t  _##name##_h = {.redraws = 2, .color = 0xffffffff, .text = "", .init = true}; \
// 	ui_handle_t *name        = &_##name##_h

typedef enum {
	UI_LAYOUT_VERTICAL,
	UI_LAYOUT_HORIZONTAL
} ui_layout_t;

typedef enum {
	UI_ALIGN_LEFT,
	UI_ALIGN_CENTER,
	UI_ALIGN_RIGHT
} ui_align_t;

typedef enum {
	UI_STATE_IDLE,
	UI_STATE_STARTED,
	UI_STATE_DOWN,
	UI_STATE_RELEASED,
	UI_STATE_HOVERED
} ui_state_t;

typedef enum {
	UI_LINK_STYLE_LINE,
	UI_LINK_STYLE_CUBIC_BEZIER
} ui_link_style_t;

typedef struct ui_theme {
	int          WINDOW_BG_COL;
	int          HOVER_COL;
	int          BUTTON_COL;
	int          PRESSED_COL;
	int          TEXT_COL;
	int          LABEL_COL;
	int          SEPARATOR_COL;
	int          HIGHLIGHT_COL;
	int          FONT_SIZE;
	int          ELEMENT_W;
	int          ELEMENT_H;
	int          ELEMENT_OFFSET;
	int          ARROW_SIZE;
	int          BUTTON_H;
	int          CHECK_SIZE;
	int          CHECK_SELECT_SIZE;
	int          SCROLL_W;
	int          SCROLL_MINI_W;
	int          TEXT_OFFSET;
	int          TAB_W; // Indentation
	/*bool*/ int FILL_BUTTON_BG;
	/*bool*/ int FULL_TABS; // Make tabs take full window width
	/*bool*/ int SHADOWS;
	int          LINK_STYLE;
	int          VIEWPORT_COL;
} ui_theme_t;

typedef struct ui_options {
	draw_font_t   *font;
	ui_theme_t    *theme;
	float          scale_factor;
	gpu_texture_t *color_wheel;
	gpu_texture_t *black_white_gradient;
} ui_options_t;

typedef struct ui_handle_array {
	struct ui_handle **buffer;
	int                length;
	int                capacity;
} ui_handle_array_t;

typedef struct ui_handle {
	union {
		float f;
		int   i;
		bool  b;
	};
	union {
		struct { // window
			int   layout;
			int   drag_x;
			int   drag_y;
			float last_max_x;
			float last_max_y;
			float scroll_offset;
		};
		struct { // color_wheel
			float hue;
			float sat;
			float val;
			float red;
			float green;
			float blue;
		};
	};
	uint32_t           color;
	int                redraws;
	char              *text;
	bool               scroll_enabled;
	bool               drag_enabled;
	bool               changed;
	bool               init;
	gpu_texture_t      texture;
	ui_handle_array_t *children;
} ui_handle_t;

typedef struct ui_text_extract {
	char colored[1024];
	char uncolored[1024];
} ui_text_extract_t;

typedef struct ui_coloring {
	uint32_t        color;
	string_array_t *start;
	char           *end;
	bool            separated;
} ui_coloring_t;

typedef struct ui_coloring_array {
	ui_coloring_t **buffer;
	int             length;
	int             capacity;
} ui_coloring_array_t;

typedef struct ui_text_coloring {
	ui_coloring_array_t *colorings;
	uint32_t             default_color;
} ui_text_coloring_t;

typedef struct ui {
	bool                is_scrolling; // Use to limit other activities
	bool                is_typing;
	bool                enabled; // Current element state
	bool                is_started;
	bool                is_pushed;
	bool                is_hovered;
	bool                is_released;
	bool                changed; // Global elements change check
	bool                scroll_enabled;
	bool                always_redraw;       // Hurts performance
	bool                highlight_on_select; // Highlight text edit contents on selection
	bool                tab_switch_enabled;  // Allow switching focus to the next element by pressing tab
	ui_text_coloring_t *text_coloring;       // Set coloring scheme for ui_draw_string() calls
	int                 window_border_top;
	int                 window_border_bottom;
	int                 window_border_left;
	int                 window_border_right;
	char                hovered_tab_name[256];
	float               hovered_tab_x;
	float               hovered_tab_y;
	float               hovered_tab_w;
	float               hovered_tab_h;
	bool                touch_hold_activated;
	bool                slider_tooltip;
	float               slider_tooltip_x;
	float               slider_tooltip_y;
	float               slider_tooltip_w;
	bool                input_enabled;
	float               input_x; // Input position
	float               input_y;
	float               input_started_x;
	float               input_started_y;
	float               input_dx; // Delta
	float               input_dy;
	float               input_wheel_delta;
	bool                input_started; // Buttons
	bool                input_started_r;
	bool                input_released;
	bool                input_released_r;
	bool                input_down;
	bool                input_down_r;
	bool                pen_in_use;
	bool                is_key_pressed; // Keys
	bool                is_key_down;
	bool                is_shift_down;
	bool                is_ctrl_down;
	bool                is_alt_down;
	bool                is_a_down;
	bool                is_backspace_down;
	bool                is_delete_down;
	bool                is_escape_down;
	bool                is_return_down;
	bool                is_tab_down;
	int                 key_code;
	int                 key_char;
	float               input_started_time;
	int                 cursor_x; // Text input
	int                 highlight_anchor;
	int                 cursor_sticky_x; // Remember column for vertical navigation
	float               cursor_screen_x; // Caret screen position, updated while editing
	float               cursor_screen_y;
	f32_array_t        *ratios; // Splitting rows
	int                 current_ratio;
	float               x_before_split;
	int                 w_before_split;

	ui_options_t *ops;
	int           font_size;
	float         font_offset_y; // Precalculated offsets
	float         arrow_offset_x;
	float         arrow_offset_y;
	float         title_offset_x;
	float         button_offset_y;
	float         check_offset_x;
	float         check_offset_y;
	float         check_select_offset_x;
	float         check_select_offset_y;
	float         radio_offset_x;
	float         radio_offset_y;
	float         radio_select_offset_x;
	float         radio_select_offset_y;
	float         scroll_align;
	bool          image_scroll_align;

	float _x; // Cursor(stack) position
	float _y;
	int   _w;
	int   _h;

	float        _window_x; // Window state
	float        _window_y;
	float        _window_w;
	float        _window_h;
	ui_handle_t *current_window;
	bool         window_ended;
	ui_handle_t *scroll_handle;   // Window or slider being scrolled
	ui_handle_t *drag_handle;     // Window being dragged
	ui_handle_t *drag_tab_handle; // Tab being dragged
	int          drag_tab_position;
	float        window_header_w;
	float        window_header_h;
	float        restore_x;
	float        restore_y;

	ui_handle_t    *text_selected_handle;
	char            text_selected[1024];
	ui_handle_t    *submit_text_handle;
	char            text_to_submit[1024];
	bool            tab_pressed;
	ui_handle_t    *tab_pressed_handle;
	ui_handle_t    *combo_selected_handle;
	ui_handle_t    *combo_selected_window;
	int             combo_selected_align;
	string_array_t *combo_selected_texts;
	any_array_t    *combo_selected_images;
	char           *combo_selected_label;
	int             combo_selected_x;
	int             combo_selected_y;
	int             combo_selected_w;
	int             combo_selected_texts_filtered;
	bool            combo_search_bar;
	ui_handle_t    *submit_combo_handle;
	int             combo_to_submit;
	int             combo_initial_value;
	char            tooltip_text[512];
	gpu_texture_t  *tooltip_img;
	int             tooltip_img_max_width;
	float           tooltip_x;
	float           tooltip_y;
	bool            tooltip_shown;
	bool            tooltip_wait;
	double          tooltip_time;
	char            tab_names[16][256];
	uint32_t        tab_colors[16];
	bool            tab_enabled[16];
	int             tab_count; // Number of tab calls since window begin
	ui_handle_t    *tab_handle;
	float           tab_scroll;
	bool            tab_vertical;
	bool            tab_align_right;
	bool            sticky;
	bool            scissor;

	bool          elements_baked;
	gpu_texture_t check_select_image;
	gpu_texture_t radio_image;
	gpu_texture_t radio_select_image;
	gpu_texture_t round_corner_image;
	gpu_texture_t filled_round_corner_image;
} ui_t;

void  ui_init(ui_t *ui, ui_options_t *ops);
void  ui_begin(ui_t *ui);
void  ui_begin_sticky();
void  ui_end_sticky();
void  ui_begin_region(ui_t *ui, int x, int y, int w);
void  ui_end_region();
bool  ui_window(ui_handle_t *handle, int x, int y, int w, int h, bool drag); // Returns true if redraw is needed
bool  ui_button(char *text, int align, char *label);
int   ui_text(char *text, int align, int bg);
bool  ui_tab(ui_handle_t *handle, char *text, bool vertical, uint32_t color, bool align_right);
bool  ui_panel(ui_handle_t *handle, char *text, bool is_tree, bool filled, bool align_right);
int   ui_sub_image(gpu_texture_t *image, uint32_t tint, int h, int sx, int sy, int sw, int sh);
int   ui_image(gpu_texture_t *image, uint32_t tint, int h);
char *ui_text_input(ui_handle_t *handle, char *label, int align, bool editable, bool live_update);
bool  ui_check(ui_handle_t *handle, char *text, char *label);
bool  ui_radio(ui_handle_t *handle, int position, char *text, char *label);
int   ui_combo(ui_handle_t *handle, string_array_t *texts, char *label, bool show_label, int align, bool search_bar);
float ui_slider(ui_handle_t *handle, char *text, float from, float to, bool filled, float precision, bool display_value, int align, bool text_edit);
void  ui_row(f32_array_t *ratios);
void  ui_row2();
void  ui_row3();
void  ui_row4();
void  ui_row5();
void  ui_row6();
void  ui_row7();
void  ui_separator(int h, bool fill);
void  ui_tooltip(char *text);
void  ui_tooltip_image(gpu_texture_t *image, int max_width);
void  ui_end();
void  ui_end_window();
char *ui_hovered_tab_name();
void  ui_set_hovered_tab_name(char *name);
void  ui_mouse_down(ui_t *ui, int button, int x, int y); // Input events
void  ui_mouse_move(ui_t *ui, int x, int y, int movement_x, int movement_y);
void  ui_mouse_up(ui_t *ui, int button, int x, int y);
void  ui_mouse_wheel(ui_t *ui, float delta);
void  ui_pen_down(ui_t *ui, int x, int y, float pressure);
void  ui_pen_up(ui_t *ui, int x, int y, float pressure);
void  ui_pen_move(ui_t *ui, int x, int y, float pressure);
void  ui_key_down(ui_t *ui, int key_code);
void  ui_key_up(ui_t *ui, int key_code);
void  ui_key_press(ui_t *ui, unsigned character);
#if defined(IRON_ANDROID) || defined(IRON_IOS)
void ui_touch_down(ui_t *ui, int index, int x, int y);
void ui_touch_up(ui_t *ui, int index, int x, int y);
void ui_touch_move(ui_t *ui, int index, int x, int y);
#endif
void         ui_copy();
void         ui_cut();
void         ui_paste(char *s);
void         ui_theme_default(ui_theme_t *t);
ui_t        *ui_get_current();
void         ui_set_current(ui_t *current);
ui_handle_t *ui_handle_create();
ui_handle_t *ui_nest(ui_handle_t *handle, int pos);
void         ui_set_scale(float factor);

bool  ui_get_hover(float elem_h);
bool  ui_get_released(float elem_h);
bool  ui_input_in_rect(float x, float y, float w, float h);
void  ui_fill(float x, float y, float w, float h, uint32_t color);
void  ui_rect(float x, float y, float w, float h, uint32_t color, float strength);
int   ui_line_count(char *str);
char *ui_extract_line(char *str, int line);
char *ui_extract_line_off(char *str, int line, int *off);
bool  ui_is_visible(float elem_h);
void  ui_end_element();
void  ui_end_element_of_size(float element_size);
void  ui_end_input();
void  ui_end_frame();
void  ui_fade_color(float alpha);
void  ui_draw_string(char *text, float x_offset, float y_offset, int align, bool truncation);
void  ui_draw_shadow(float x, float y, float w, float h);
void  ui_draw_rect(bool fill, bool shadows, float x, float y, float w, float h);
void  ui_draw_round_bottom(float x, float y, float w);
void  ui_start_text_edit(ui_handle_t *handle, int align);
void  ui_remove_char_at(char *str, int at);
void  ui_remove_chars_at(char *str, int at, int count);
void  ui_insert_char_at(char *str, int at, char c);
void  ui_insert_chars_at(char *str, int at, char *cs);

float UI_SCALE();
float UI_ELEMENT_W();
float UI_ELEMENT_H();
float UI_ELEMENT_OFFSET();
float UI_ARROW_SIZE();
float UI_BUTTON_H();
float UI_CHECK_SIZE();
float UI_CHECK_SELECT_SIZE();
float UI_FONT_SIZE();
float UI_SCROLL_W();
float UI_TEXT_OFFSET();
float UI_TAB_W();
float UI_HEADER_DRAG_H();
float UI_TOOLTIP_DELAY();

extern bool  ui_touch_control;
extern float ui_touch_speed;
extern bool  ui_is_cut;
extern bool  ui_is_copy;
extern bool  ui_is_paste;
extern char  ui_text_to_paste[8192];
extern char  ui_text_to_copy[8192];
extern void (*ui_on_border_hover)(ui_handle_t *, int);
extern void (*ui_on_tab_drop)(ui_handle_t *, int, ui_handle_t *, int);
extern bool (*ui_picker_button)(void);
extern const char *ui_theme_keys[];
extern int         ui_theme_keys_count;

float ui_float_input(ui_handle_t *handle, char *label, int align, float precision);
int   ui_inline_radio(ui_handle_t *handle, string_array_t *texts, int align);
int   ui_color_wheel(ui_handle_t *handle, bool alpha, float w, float h, bool color_preview, void (*picker)(void *), void *data);
char *ui_text_area(ui_handle_t *handle, int align, bool editable, char *label, bool word_wrap);
void  ui_begin_menu();
void  ui_end_menu();
bool  ui_menubar_button(char *text);
void  ui_hsv_to_rgb(float cr, float cg, float cb, float *out);
void  ui_rgb_to_hsv(float cr, float cg, float cb, float *out);

uint8_t  ui_color_r(uint32_t color);
uint8_t  ui_color_g(uint32_t color);
uint8_t  ui_color_b(uint32_t color);
uint8_t  ui_color_a(uint32_t color);
uint32_t ui_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

extern bool                ui_text_area_line_numbers;
extern bool                ui_text_area_scroll_past_end;
extern ui_text_coloring_t *ui_text_area_coloring;

typedef struct ui_canvas_control {
	float pan_x;
	float pan_y;
	float zoom;
	bool  controls_down;
} ui_canvas_control_t;

typedef struct ui_node_socket {
	int          id;
	int          node_id;
	char        *name;
	char        *type;
	uint32_t     color;
	f32_array_t *default_value;
	float        min;
	float        max;
	float        precision;
	int          display;
} ui_node_socket_t;

typedef struct ui_node_button {
	char        *name;
	char        *type;
	int          output;
	f32_array_t *default_value;
	u8_array_t  *data;
	float        min;
	float        max;
	float        precision;
	float        height;
} ui_node_button_t;

typedef struct ui_node_socket_array {
	ui_node_socket_t **buffer;
	int                length;
	int                capacity;
} ui_node_socket_array_t;

typedef struct ui_node_button_array {
	ui_node_button_t **buffer;
	int                length;
	int                capacity;
} ui_node_button_array_t;

typedef enum ui_node_flag {
	UI_NODE_FLAG_NONE      = 0,
	UI_NODE_FLAG_COLLAPSED = 1,
	UI_NODE_FLAG_PREVIEW   = 2,
} ui_node_flag_t;

typedef struct ui_node {
	int                     id;
	char                   *name;
	char                   *type;
	float                   x;
	float                   y;
	uint32_t                color;
	ui_node_socket_array_t *inputs;
	ui_node_socket_array_t *outputs;
	ui_node_button_array_t *buttons;
	float                   width;
	int                     flags; // ui_node_flag_t
} ui_node_t;

typedef struct ui_node_link {
	int id;
	int from_id;
	int from_socket;
	int to_id;
	int to_socket;
} ui_node_link_t;

typedef struct ui_node_array {
	ui_node_t **buffer;
	int         length;
	int         capacity;
} ui_node_array_t;

typedef struct ui_node_link_array {
	ui_node_link_t **buffer;
	int              length;
	int              capacity;
} ui_node_link_array_t;

typedef struct ui_node_canvas {
	char                 *name;
	ui_node_array_t      *nodes;
	ui_node_link_array_t *links;
} ui_node_canvas_t;

typedef struct ui_nodes {
	bool         nodes_drag;
	i32_array_t *nodes_selected_id;
	float        pan_x;
	float        pan_y;
	float        zoom;
	int          uiw;
	int          uih;
	bool         _input_started;
	void (*color_picker_callback)(uint32_t);
	void        *color_picker_callback_data;
	float        scale_factor;
	float        ELEMENT_H;
	bool         dragged;
	ui_node_t   *move_on_top;
	int          link_drag_id;
	bool         is_new_link;
	int          snap_from_id;
	int          snap_to_id;
	int          snap_socket;
	float        snap_x;
	float        snap_y;
	ui_handle_t *handle;
} ui_nodes_t;

void ui_nodes_init(ui_nodes_t *nodes);
void ui_node_canvas(ui_nodes_t *nodes, ui_node_canvas_t *canvas);
void ui_nodes_rgba_popup(ui_handle_t *nhandle, float *val, int x, int y);

void ui_remove_node(ui_node_t *n, ui_node_canvas_t *canvas);

float                  UI_NODES_SCALE();
float                  UI_NODES_PAN_X();
float                  UI_NODES_PAN_Y();
extern char           *ui_clipboard;
extern string_array_t *ui_nodes_exclude_remove;
extern bool            ui_nodes_socket_released;
extern string_array_t *(*ui_nodes_enum_texts)(char *);
extern any_array_t *(*ui_nodes_enum_textures)(char *);
extern gpu_texture_t *(*ui_nodes_preview_texture)(ui_node_t *);
extern void (*ui_nodes_on_custom_button)(int, char *);
extern ui_canvas_control_t *(*ui_nodes_on_canvas_control)(void);
extern void (*ui_nodes_on_canvas_released)(void);
extern void (*ui_nodes_on_socket_released)(int);
extern void (*ui_nodes_on_link_drag)(int, bool);
extern void (*ui_nodes_on_node_remove)(ui_node_t *);
extern void (*ui_nodes_on_node_changed)(ui_node_t *);
extern bool ui_nodes_grid_snap;

void     ui_node_canvas_encode(ui_node_canvas_t *canvas);
uint32_t ui_node_canvas_encoded_size(ui_node_canvas_t *canvas);
char    *ui_node_canvas_to_json(ui_node_canvas_t *canvas);

float UI_NODE_X(ui_node_t *node);
float UI_NODE_Y(ui_node_t *node);
float UI_NODE_W(ui_node_t *node);
float UI_NODE_H(ui_node_canvas_t *canvas, ui_node_t *node);
float UI_OUTPUT_Y(ui_node_t *node, int pos);
float UI_INPUT_Y(ui_node_canvas_t *canvas, ui_node_t *node, int pos);
float UI_OUTPUTS_H(ui_node_t *node, int length);
float UI_BUTTONS_H(ui_node_t *node);
float UI_LINE_H();
float UI_NODES_PAN_X();
float UI_NODES_PAN_Y();

float           ui_p(float f);
int             ui_get_socket_id(ui_node_array_t *nodes);
ui_node_link_t *ui_get_link(ui_node_link_array_t *links, int id);
int             ui_next_link_id(ui_node_link_array_t *links);
ui_node_t      *ui_get_node(ui_node_array_t *nodes, int id);
int             ui_next_node_id(ui_node_array_t *nodes);
