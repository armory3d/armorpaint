
#include "global.h"

typedef struct version {
	char *sha;
	char *date;
} version_t;

bool config_loaded = false;

void config_load() {
	char *path = "";
	if (path_is_protected()) {
		path = iron_internal_save_path();
	}
	path           = string("%sconfig.json", path);
	buffer_t *blob = data_get_blob(path);

#ifdef IRON_LINUX
	if (blob == NULL) { // Detect protected path
		config_init();
		config_save();
		blob = data_get_blob(path);
		if (blob == NULL) {
			path_is_protected_linux = true;
			config_load();
			return;
		}
	}
#endif

	if (blob != NULL) {
		char *config_string = sys_buffer_to_string(blob);
		if (starts_with(config_string, "{\"version\":")) { // Ensure valid config
			config_loaded = true;
			gc_unroot(g_config);
			g_config = json_parse(config_string);
			gc_root(g_config);
		}
	}
}

void config_save() {
	if (g_config->workspace == WORKSPACE_PLAYER) {
		return;
	}

	// Use system application data folder
	// when running from protected path like "Program Files"
	char *path;
	if (path_is_protected()) {
		path = string("%sconfig.json", iron_internal_save_path());
	}
	else {
		path = string("%s%sconfig.json", path_data(), PATH_SEP);
	}

	json_encode_begin();
	json_encode_string("version", g_config->version);
	json_encode_string("sha", g_config->sha);
	json_encode_string("locale", g_config->locale);
	json_encode_i32("window_mode", g_config->window_mode);
	json_encode_i32("window_w", g_config->window_w);
	json_encode_i32("window_h", g_config->window_h);
	json_encode_i32("window_x", g_config->window_x);
	json_encode_i32("window_y", g_config->window_y);
	json_encode_bool("window_resizable", g_config->window_resizable);
	json_encode_bool("window_maximizable", g_config->window_maximizable);
	json_encode_bool("window_minimizable", g_config->window_minimizable);
	json_encode_bool("window_vsync", g_config->window_vsync);
	json_encode_i32("window_frequency", g_config->window_frequency);
	json_encode_f32("window_scale", g_config->window_scale);
	json_encode_f32("rp_supersample", g_config->rp_supersample);
	json_encode_bool("rp_ssao", g_config->rp_ssao);
	json_encode_f32("rp_bloom", g_config->rp_bloom);
	json_encode_f32("rp_vignette", g_config->rp_vignette);
	json_encode_f32("rp_grain", g_config->rp_grain);
	json_encode_f32("rp_contrast", g_config->rp_contrast);
	json_encode_f32("rp_gamma", g_config->rp_gamma);
	json_encode_string("lut_path", g_config->lut_path);
	json_encode_bool("texture_filter", g_config->texture_filter);
	json_encode_string_array("recent_projects", g_config->recent_projects);
	json_encode_string_array("bookmarks", g_config->bookmarks);
	json_encode_string_array("plugins", g_config->plugins);
	json_encode_string("keymap", g_config->keymap);
	json_encode_string("theme", g_config->theme);
	json_encode_i32("undo_steps", g_config->undo_steps);
	json_encode_f32("camera_fov", g_config->camera_fov);
	json_encode_f32("camera_pan_speed", g_config->camera_pan_speed);
	json_encode_f32("camera_zoom_speed", g_config->camera_zoom_speed);
	json_encode_f32("camera_rotation_speed", g_config->camera_rotation_speed);
	json_encode_bool("camera_upside_down", g_config->camera_upside_down);
	json_encode_i32("zoom_direction", g_config->zoom_direction);
	json_encode_bool("wrap_mouse", g_config->wrap_mouse);
	json_encode_bool("show_asset_names", g_config->show_asset_names);
	json_encode_bool("touch_ui", g_config->touch_ui);
	json_encode_bool("splash_screen", g_config->splash_screen);
	if (g_config->layout != NULL) {
		json_encode_i32_array("layout", g_config->layout);
	}
	else {
		json_encode_null("layout");
	}
	if (g_config->layout_tabs != NULL) {
		json_encode_i32_array("layout_tabs", g_config->layout_tabs);
	}
	else {
		json_encode_null("layout_tabs");
	}
	json_encode_i32("camera_pivot", g_config->camera_pivot);
	json_encode_i32("camera_controls", g_config->camera_controls);
	json_encode_string("server", g_config->server);
	json_encode_i32("viewport_mode", g_config->viewport_mode);
	json_encode_i32("pathtrace_mode", g_config->pathtrace_mode);
	json_encode_bool("pressure_radius", g_config->pressure_radius);
	json_encode_f32("pressure_sensitivity", g_config->pressure_sensitivity);
	json_encode_i32("layer_res", g_config->layer_res);
	json_encode_bool("brush_live", g_config->brush_live);
	json_encode_bool("node_previews", g_config->node_previews);
	json_encode_bool("pressure_hardness", g_config->pressure_hardness);
	json_encode_bool("pressure_angle", g_config->pressure_angle);
	json_encode_bool("pressure_opacity", g_config->pressure_opacity);
	json_encode_bool("material_live", g_config->material_live);
	json_encode_bool("brush_depth_reject", g_config->brush_depth_reject);
	json_encode_bool("brush_angle_reject", g_config->brush_angle_reject);
	json_encode_f32("brush_alpha_discard", g_config->brush_alpha_discard);
	json_encode_i32("dilate_radius", g_config->dilate_radius);
	json_encode_string("blender", g_config->blender);
	json_encode_i32("scene_atlas_res", g_config->scene_atlas_res);
	json_encode_bool("grid_snap", g_config->grid_snap);
	json_encode_bool("experimental", g_config->experimental);
	json_encode_i32("neural_res", g_config->neural_res);
	json_encode_i32("render_mode", g_config->render_mode);
	json_encode_i32("workspace", g_config->workspace);
	json_encode_i32("workflow", g_config->workflow);
	json_encode_bool("view2d_grid_show", g_config->view2d_grid_show);
	json_encode_i32("view2d_grid_cell", g_config->view2d_grid_cell);
	json_encode_bool("view2d_grid_snap", g_config->view2d_grid_snap);
	char *config_json = json_encode_end();

	buffer_t *buffer = sys_string_to_buffer(config_json);
	iron_file_save_bytes(path, buffer, 0);
}

void config_init() {
	if (!config_loaded || g_config == NULL) {
		gc_unroot(g_config);
		g_config = GC_ALLOC_INIT(config_t, {0});
		gc_root(g_config);
		g_config->version            = string_copy(manifest_version_config);
		g_config->sha                = string_copy(config_get_sha());
		g_config->locale             = "en"; // "system";
		g_config->window_mode        = 0;
		g_config->window_resizable   = true;
		g_config->window_minimizable = true;
		g_config->window_maximizable = true;
		g_config->window_w           = 1720;
		g_config->window_h           = 960;
#ifdef IRON_MACOS
		g_config->window_w *= 2;
		g_config->window_h *= 2;
#endif
		g_config->window_x     = -1;
		g_config->window_y     = -1;
		g_config->window_scale = 1.0;
		if (sys_display_width() >= 2560 && sys_display_height() >= 1600) {
			g_config->window_scale = 2.0;
		}
#if defined(IRON_ANDROID) || defined(IRON_IOS) || defined(IRON_MACOS)
		g_config->window_scale = 2.0;
#endif
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		if (sys_display_ppi() > 330) {
			g_config->window_scale = 2.5;
		}
		if (sys_display_ppi() > 400) {
			g_config->window_scale = 3.0;
		}
#endif
		g_config->window_vsync     = true;
		g_config->window_frequency = sys_display_frequency();
		g_config->rp_bloom         = 0.0;
		g_config->rp_vignette      = 0.2;
		g_config->rp_grain         = 0.09;
		g_config->rp_contrast      = 1.0;
		g_config->rp_gamma         = 1.0;
		g_config->lut_path         = "";
		g_config->texture_filter   = true;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		g_config->rp_ssao = false;
#else
		g_config->rp_ssao = true;
#endif
		g_config->rp_supersample = 1.0;
#ifdef IRON_ANDROID
		if (sys_display_width() >= 3200 && sys_display_height() >= 2136) {
			g_config->window_scale   = 2.5;
			g_config->rp_supersample = 0.5;
		}
#endif
		g_config->recent_projects = any_array_create_from_raw((void *[]){}, 0);
		g_config->bookmarks       = any_array_create_from_raw((void *[]){}, 0);
		g_config->plugins         = any_array_create_from_raw((void *[]){}, 0);
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		g_config->keymap = "touch.json";
#else
		g_config->keymap = "default.json";
#endif
		g_config->theme  = "default.json";
		g_config->server = "https://cloud.armory3d.com";
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		g_config->undo_steps = 4;
#else
		g_config->undo_steps = 6;
#endif
		g_config->pressure_radius      = true;
		g_config->pressure_sensitivity = 2.0;
		g_config->camera_fov           = 0.69;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		g_config->camera_zoom_speed     = 0.5;
		g_config->camera_pan_speed      = 0.5;
		g_config->camera_rotation_speed = 0.5;
#else
		g_config->camera_zoom_speed     = 1.0;
		g_config->camera_pan_speed      = 1.0;
		g_config->camera_rotation_speed = 1.0;
#endif
		g_config->camera_upside_down = false;
		g_config->zoom_direction     = ZOOM_DIRECTION_VERTICAL;
		g_config->wrap_mouse         = false;
		g_config->camera_pivot       = CAMERA_PIVOT_CENTER;
		g_config->camera_controls    = CAMERA_CONTROLS_ORBIT;
		g_config->layer_res          = TEXTURE_RES_RES2048;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		g_config->touch_ui      = true;
		g_config->splash_screen = true;
#else
		g_config->touch_ui      = false;
		g_config->splash_screen = false;
#endif
		g_config->node_previews     = false;
		g_config->pressure_hardness = false;
		g_config->pressure_angle    = false;
		g_config->pressure_opacity  = false;
#if defined(IRON_ANDROID) || defined(IRON_IOS) || defined(IRON_MACOS)
		g_config->material_live = false;
#else
		g_config->material_live = true;
#endif
		g_config->brush_depth_reject  = true;
		g_config->brush_angle_reject  = true;
		g_config->brush_alpha_discard = 0.1;
		g_config->brush_live          = false;
		g_config->show_asset_names    = false;
		g_config->dilate_radius       = 2;
		g_config->blender             = "";
		g_config->scene_atlas_res     = TEXTURE_RES_RES8192;
		g_config->pathtrace_mode      = PATHTRACE_MODE_FAST;
		g_config->grid_snap           = false;
		g_config->view2d_grid_show    = false;
		g_config->view2d_grid_cell    = 64;
		g_config->view2d_grid_snap    = false;
		g_config->experimental        = false;
		g_config->neural_res          = 512;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
		g_config->render_mode = RENDER_MODE_FORWARD;
#else
		g_config->render_mode = RENDER_MODE_DEFERRED;
#endif
		g_config->workspace = WORKSPACE_PAINT_3D;
		g_config->workflow  = WORKFLOW_PBR;
	}
	else {
		// Discard old config
		if (!string_equals(g_config->sha, config_get_sha())) {
			config_loaded = false;
			config_init();
			return;
		}
	}

	ui_touch_control = g_config->touch_ui;
	ui_touch_speed   = 1.0;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	if (sys_display_ppi() > 400) {
		ui_touch_speed = 0.5;
	}
#endif
	base_res_handle->i = g_config->layer_res;
	config_set_texture_res(g_config->layer_res);
	keymap_load();
}

void config_init_layout() {
	config_t    *raw        = g_config;
	bool         show2d     = (ui_nodes_show || ui_view2d_show) && raw->layout != NULL;
	i32_array_t *new_layout = i32_array_create_from_raw((i32[]){}, 0);

	i32_array_push(new_layout, math_floor(ui_sidebar_default_w * raw->window_scale)); // LayoutSidebarW
	i32_array_push(new_layout, math_floor(iron_window_height() / 2.0));               // LayoutSidebarH0
	i32_array_push(new_layout, math_floor(iron_window_height() / 2.0));               // LayoutSidebarH1

#ifdef IRON_IOS
	i32_array_push(new_layout, show2d ? math_floor((sys_w() + raw->layout->buffer[LAYOUT_SIZE_NODES_W]) * 0.473) : math_floor(sys_w() * 0.473)); // LayoutNodesW
#elif defined(IRON_ANDROID)
	i32_array_push(new_layout, show2d ? math_floor((sys_w() + raw->layout->buffer[LAYOUT_SIZE_NODES_W]) * 0.473) : math_floor(sys_w() * 0.473));
#else
	i32_array_push(new_layout,
	               show2d ? math_floor((sys_w() + raw->layout->buffer[LAYOUT_SIZE_NODES_W]) * 0.515)
	                      : math_floor(sys_w() * 0.515)); // Align with ui header controls
#endif

	i32_array_push(new_layout, math_floor(sys_h() / 2.0));                              // LayoutNodesH
	i32_array_push(new_layout, math_floor(ui_statusbar_default_h * raw->window_scale)); // LayoutStatusH

#if defined(IRON_ANDROID) || defined(IRON_IOS)
	i32_array_push(new_layout, 0); // LayoutHeader
#else
	i32_array_push(new_layout, 1);
#endif

	raw->layout_tabs = i32_array_create_from_raw(
	    (i32[]){
	        0,
	        0,
	        0,
	    },
	    3);

	raw->layout = new_layout;
}

char *config_get_sha() {
	buffer_t *blob = data_get_blob("version.json");
	if (blob == NULL) {
		return "undefined";
	}
	version_t *v = json_parse(sys_buffer_to_string(blob));
	return v->sha;
}

char *config_get_date() {
	buffer_t *blob = data_get_blob("version.json");
	if (blob == NULL) {
		return "undefined";
	}
	version_t *v = json_parse(sys_buffer_to_string(blob));
	return v->date;
}

iron_window_options_t *config_get_options() {
	iron_window_mode_t     window_mode = g_config->window_mode == 0 ? IRON_WINDOW_MODE_WINDOW : IRON_WINDOW_MODE_FULLSCREEN;
	iron_window_features_t features    = IRON_WINDOW_FEATURES_NONE;
	if (g_config->window_resizable) {
		features |= IRON_WINDOW_FEATURES_RESIZABLE;
	}
	if (g_config->window_maximizable) {
		features |= IRON_WINDOW_FEATURES_MAXIMIZABLE;
	}
	if (g_config->window_minimizable) {
		features |= IRON_WINDOW_FEATURES_MINIMIZABLE;
	}
	char                  *title = string("untitled - %s", manifest_title);
	iron_window_options_t *ops   = GC_ALLOC_INIT(iron_window_options_t, {.title     = title,
	                                                                     .width     = g_config->window_w,
	                                                                     .height    = g_config->window_h,
	                                                                     .x         = g_config->window_x,
	                                                                     .y         = g_config->window_y,
	                                                                     .mode      = window_mode,
	                                                                     .features  = features,
	                                                                     .vsync     = g_config->window_vsync,
	                                                                     .frequency = g_config->window_frequency});
	return ops;
}

void config_restore() {
	gc_unroot(ui_children);
	ui_children = any_map_create(); // Reset ui handles
	gc_root(ui_children);
	config_loaded        = false;
	i32_array_t *_layout = g_config->layout;
	config_init();
	g_config->layout = _layout;
	config_init_layout();
	translator_load_translations(g_config->locale);
	config_apply();
	config_load_theme(g_config->theme, true);
}

void config_import_from(config_t *from) {
	char *_sha     = g_config->sha;
	char *_version = g_config->version;
	gc_unroot(g_config);
	g_config = from;
	gc_root(g_config);
	g_config->sha     = string_copy(_sha);
	g_config->version = string_copy(_version);
	gc_unroot(ui_children);
	ui_children = any_map_create(); // Reset ui handles
	gc_root(ui_children);
	keymap_load();
	config_init_layout();
	translator_load_translations(g_config->locale);
	config_apply();
	config_load_theme(g_config->theme, true);
}

void config_apply() {
	config_save();
	g_context->ddirty      = 2;
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();
	render_path_base_apply_config();
	if (in_use)
		draw_begin(current, false, 0);
}

i32 config_get_super_sample_quality(f32 f) {
	return f == 0.25 ? 0 : f == 0.5 ? 1 : f == 1.0 ? 2 : f == 1.5 ? 3 : f == 2.0 ? 4 : 5;
}

f32 config_get_super_sample_size(i32 i) {
	return i == 0 ? 0.25 : i == 1 ? 0.5 : i == 2 ? 1.0 : i == 3 ? 1.5 : i == 4 ? 2.0 : 4.0;
}

i32 config_texture_res_size(i32 pos) {
	return pos == TEXTURE_RES_RES2048 ? 2048 : pos == TEXTURE_RES_RES4096 ? 4096 : pos == TEXTURE_RES_RES8192 ? 8192 : pos == TEXTURE_RES_RES16384 ? 16384 : 0;
}

i32 config_get_layer_res() {
	i32 res = g_config->layer_res;
	return config_texture_res_size(res);
}

i32 config_get_scene_atlas_res() {
	i32 res = g_config->scene_atlas_res;
	return config_texture_res_size(res);
}

void config_set_texture_res(i32 pos) {
	if (pos != TEXTURE_RES_CUSTOM) {
		f32 res              = (f32)config_texture_res_size(pos);
		base_res_x_handle->f = res;
		base_res_y_handle->f = res;
	}
}

i32 config_get_texture_res_x() {
	return (i32)base_res_x_handle->f;
}

i32 config_get_texture_res_y() {
	return (i32)base_res_y_handle->f;
}

i32 config_get_texture_res_pos(i32 i) {
	return i == 2048    ? TEXTURE_RES_RES2048
	       : i == 4096  ? TEXTURE_RES_RES4096
	       : i == 8192  ? TEXTURE_RES_RES8192
	       : i == 16384 ? TEXTURE_RES_RES16384
	                    : TEXTURE_RES_CUSTOM;
}

void config_load_theme(char *theme, bool tag_redraw) {
	gc_unroot(g_theme);
	g_theme = ui_theme_create();
	gc_root(g_theme);

	if (!string_equals(theme, "default.json")) {
		buffer_t   *b      = data_get_blob(string("themes/%s", theme));
		ui_theme_t *parsed = json_parse(sys_buffer_to_string(b));
		gc_unroot(g_theme);
		g_theme = parsed;
		gc_root(g_theme);
	}

	if (tag_redraw) {
		g_ui->ops->theme = g_theme;
		base_redraw_ui();
	}

	if (g_config->touch_ui) {
		// Enlarge elements
		g_theme->FULL_TABS         = true;
		g_theme->ELEMENT_H         = 24 + 6;
		g_theme->BUTTON_H          = 22 + 6;
		g_theme->FONT_SIZE         = 13 + 2;
		g_theme->ARROW_SIZE        = 5 + 2;
		g_theme->CHECK_SIZE        = 15 + 4;
		g_theme->CHECK_SELECT_SIZE = 8 + 2;
	}
	else {
		g_theme->FULL_TABS = false;
	}
}

void config_enable_plugin(char *f) {
	if (string_array_index_of(g_config->plugins, f) == -1) {
		any_array_push(g_config->plugins, f);
		plugin_start(f);
	}
}

void config_disable_plugin(char *f) {
	if (string_array_index_of(g_config->plugins, f) > -1) {
		string_array_remove(g_config->plugins, f);
		plugin_stop(f);
	}
}

#ifdef IRON_IOS
bool config_is_iphone() {
	return sys_display_ppi() > 330;
}
#endif
