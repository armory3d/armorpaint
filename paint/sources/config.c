
#include "global.h"

typedef struct version {
	char *sha;
	char *date;
} version_t;

bool config_loaded = false;

#ifdef IRON_MACOS
float iron_display_backing_scale(void);
#endif

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
			free(path);
			config_load();
			return;
		}
	}
#endif

	if (blob != NULL) {
		char *config_string = sys_buffer_to_string(blob);
		g_config            = config_from_json(config_string);
		config_loaded       = g_config != NULL;
		free(config_string);
	}
	free(path);
}

void config_save() {
	if (base_player) {
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
	json_encode_f32("rp_ssao", g_config->rp_ssao);
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
	json_encode_bool("material_params", g_config->material_params);
	json_encode_bool("brush_params", g_config->brush_params);
	json_encode_bool("mesh_params", g_config->mesh_params);
	json_encode_bool("touch_ui", g_config->touch_ui);
	json_encode_bool("splash_screen", g_config->splash_screen);
	json_encode_bool("header_pinned", g_config->header_pinned);
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
	json_encode_i32("pathtrace_frames", g_config->pathtrace_frames);
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
	json_encode_bool("grid_snap", g_config->grid_snap);
	json_encode_bool("auto_run_scripts", g_config->auto_run_scripts);
	json_encode_bool("experimental", g_config->experimental);
	json_encode_i32("neural_res", g_config->neural_res);
	json_encode_i32("console_model", g_config->console_model);
	json_encode_i32("tools", g_config->tools);
	json_encode_bool("view2d_grid_show", g_config->view2d_grid_show);
	json_encode_i32("view2d_grid_cell", g_config->view2d_grid_cell);
	json_encode_bool("view2d_grid_snap", g_config->view2d_grid_snap);
	json_encode_bool("pixel_snap", g_config->pixel_snap);
	json_encode_bool("large_asset_previews", g_config->large_asset_previews);
	char *config_json = json_encode_end();

	buffer_t *buffer = sys_string_to_buffer(config_json);
	iron_file_save_bytes(path, buffer, 0);
	array_free(buffer);
	free(buffer);
	free(config_json);
}

config_t *config_create_default() {
	config_t *c           = ALLOC_INIT(config_t, {0});
	c->version            = string_copy(manifest_version_config);
	c->sha                = string_copy(config_get_sha());
	c->locale             = "en"; // "system";
	c->window_mode        = 0;
	c->window_resizable   = true;
	c->window_minimizable = true;
	c->window_maximizable = true;
	c->window_w           = 1720;
	c->window_h           = 960;
#ifdef IRON_MACOS
	c->window_w *= iron_display_backing_scale();
	c->window_h *= iron_display_backing_scale();
#endif
	c->window_x     = -1;
	c->window_y     = -1;
	c->window_scale = 1.0;
	if (sys_display_width() >= 2560 && sys_display_height() >= 1600) {
		c->window_scale = 2.0;
	}
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	c->window_scale = 2.0;
#endif
#ifdef IRON_MACOS
	c->window_scale = iron_display_backing_scale();
#endif
#ifdef IRON_WASM
	c->window_scale = sys_display_ppi() / 96.0;
	if (c->window_scale < 1.0) {
		c->window_scale = 1.0;
	}
#endif
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	if (sys_display_ppi() > 330) {
		c->window_scale = 2.5;
	}
	if (sys_display_ppi() > 400) {
		c->window_scale = 3.0;
	}
#endif
	c->window_vsync     = true;
	c->window_frequency = sys_display_frequency();
	c->rp_bloom         = 0.0;
	c->rp_vignette      = 1.0;
	c->rp_grain         = 0.09;
	c->rp_contrast      = 1.0;
	c->rp_gamma         = 1.0;
	c->lut_path         = "";
	c->texture_filter   = true;
	c->rp_ssao          = 1.0;
	c->rp_supersample   = 1.0;
#ifdef IRON_ANDROID
	if (sys_display_width() >= 3200 && sys_display_height() >= 2136) {
		c->window_scale   = 2.5;
		c->rp_supersample = 0.5;
	}
#endif
	c->recent_projects = any_array_create_from_raw((void *[]){}, 0);
	c->bookmarks       = any_array_create_from_raw((void *[]){}, 0);
	c->plugins         = any_array_create_from_raw((void *[]){}, 0);
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	c->keymap = "touch.json";
#else
	c->keymap = "default.json";
#endif
	c->theme  = "default.json";
	c->server = "https://cloud.armory3d.com";
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	c->undo_steps = 4;
#else
	c->undo_steps = 6;
#endif
	c->pressure_radius      = true;
	c->pressure_sensitivity = 2.0;
	c->camera_fov           = 0.69;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	c->camera_zoom_speed     = 0.5;
	c->camera_pan_speed      = 0.5;
	c->camera_rotation_speed = 0.5;
#else
	c->camera_zoom_speed     = 1.0;
	c->camera_pan_speed      = 1.0;
	c->camera_rotation_speed = 1.0;
#endif
	c->camera_upside_down = false;
	c->zoom_direction     = ZOOM_DIRECTION_VERTICAL;
	c->wrap_mouse         = false;
	c->camera_pivot       = CAMERA_PIVOT_CENTER;
	c->camera_controls    = CAMERA_CONTROLS_ORBIT;
	c->layer_res          = TEXTURE_RES_RES2048;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	c->touch_ui      = true;
	c->header_pinned = false;
	c->splash_screen = true;
#else
	c->touch_ui      = false;
	c->header_pinned = true;
	c->splash_screen = false;
#endif
	c->node_previews     = false;
	c->pressure_hardness = false;
	c->pressure_angle    = false;
	c->pressure_opacity  = false;
#if defined(IRON_ANDROID) || defined(IRON_IOS) || defined(IRON_MACOS)
	c->material_live = false;
#else
	c->material_live = true;
#endif
	c->brush_depth_reject  = true;
	c->brush_angle_reject  = true;
	c->brush_alpha_discard = 0.1;
	c->brush_live          = false;
	c->show_asset_names    = false;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	c->material_params = false;
	c->brush_params    = false;
#else
	c->material_params = true;
	c->brush_params    = true;
#endif
	c->mesh_params      = false;
	c->dilate_radius    = 2;
	c->blender          = "";
	c->pathtrace_mode   = PATHTRACE_MODE_FAST;
	c->pathtrace_frames = 8;
	c->grid_snap        = false;
	c->view2d_grid_show = false;
	c->view2d_grid_cell = 64;
	c->view2d_grid_snap = false;
	c->pixel_snap       = false;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	c->large_asset_previews = false;
#else
	c->large_asset_previews = true;
#endif
#ifdef NDEBUG
	c->auto_run_scripts = false;
#else
	c->auto_run_scripts = true;
#endif
	c->experimental  = false;
	c->neural_res    = 512;
	c->console_model = CONSOLE_MODEL_QWEN;
	c->tools         = TOOLS_PAINT;
	return c;
}

void config_init() {
	if (!config_loaded || g_config == NULL) {
		g_config = config_create_default();
	}
	else {
		g_config->sha = string_copy(config_get_sha());
	}

	ui_touch_control = g_config->touch_ui;
	ui_touch_speed   = 1.0;
#if defined(IRON_ANDROID) || defined(IRON_IOS)
	if (sys_display_ppi() > 400) {
		ui_touch_speed = 0.5;
	}
#endif
	base_res = g_config->layer_res;
	config_set_texture_res(g_config->layer_res);
	keymap_load();
}

config_t *config_from_map_to_config(any_map_t *old) {
	config_t *c              = ALLOC_INIT(config_t, {0});
	c->version               = string_copy(manifest_version_config);
	c->sha                   = string_copy(config_get_sha());
	c->locale                = string_copy(any_map_get(old, "locale"));
	c->window_mode           = armpack_map_get_i32(old, "window_mode");
	c->window_w              = armpack_map_get_i32(old, "window_w");
	c->window_h              = armpack_map_get_i32(old, "window_h");
	c->window_x              = armpack_map_get_i32(old, "window_x");
	c->window_y              = armpack_map_get_i32(old, "window_y");
	c->window_resizable      = armpack_map_get_i32(old, "window_resizable") > 0;
	c->window_maximizable    = armpack_map_get_i32(old, "window_maximizable") > 0;
	c->window_minimizable    = armpack_map_get_i32(old, "window_minimizable") > 0;
	c->window_vsync          = armpack_map_get_i32(old, "window_vsync") > 0;
	c->window_frequency      = armpack_map_get_i32(old, "window_frequency");
	c->window_scale          = armpack_map_get_f32(old, "window_scale");
	c->rp_supersample        = armpack_map_get_f32(old, "rp_supersample");
	c->rp_ssao               = armpack_map_get_f32(old, "rp_ssao");
	c->rp_bloom              = armpack_map_get_f32(old, "rp_bloom");
	c->rp_vignette           = armpack_map_get_f32(old, "rp_vignette");
	c->rp_grain              = armpack_map_get_f32(old, "rp_grain");
	c->rp_contrast           = armpack_map_get_f32(old, "rp_contrast");
	c->rp_gamma              = armpack_map_get_f32(old, "rp_gamma");
	c->lut_path              = string_copy(any_map_get(old, "lut_path"));
	c->texture_filter        = armpack_map_get_i32(old, "texture_filter") > 0;
	c->recent_projects       = any_map_get(old, "recent_projects");
	c->bookmarks             = any_map_get(old, "bookmarks");
	c->plugins               = any_map_get(old, "plugins");
	c->keymap                = string_copy(any_map_get(old, "keymap"));
	c->theme                 = string_copy(any_map_get(old, "theme"));
	c->undo_steps            = armpack_map_get_i32(old, "undo_steps");
	c->camera_fov            = armpack_map_get_f32(old, "camera_fov");
	c->camera_pan_speed      = armpack_map_get_f32(old, "camera_pan_speed");
	c->camera_zoom_speed     = armpack_map_get_f32(old, "camera_zoom_speed");
	c->camera_rotation_speed = armpack_map_get_f32(old, "camera_rotation_speed");
	c->camera_upside_down    = armpack_map_get_i32(old, "camera_upside_down") > 0;
	c->zoom_direction        = armpack_map_get_i32(old, "zoom_direction");
	c->wrap_mouse            = armpack_map_get_i32(old, "wrap_mouse") > 0;
	c->show_asset_names      = armpack_map_get_i32(old, "show_asset_names") > 0;
	c->material_params       = armpack_map_get_i32(old, "material_params") > 0;
	c->brush_params          = armpack_map_get_i32(old, "brush_params") > 0;
	c->mesh_params           = armpack_map_get_i32(old, "mesh_params") > 0;
	c->touch_ui              = armpack_map_get_i32(old, "touch_ui") > 0;
	c->splash_screen         = armpack_map_get_i32(old, "splash_screen") > 0;
	c->header_pinned         = armpack_map_get_i32(old, "header_pinned") > 0;
	c->layout_tabs           = any_map_get(old, "layout_tabs");
	c->camera_pivot          = armpack_map_get_i32(old, "camera_pivot");
	c->camera_controls       = armpack_map_get_i32(old, "camera_controls");
	c->server                = string_copy(any_map_get(old, "server"));
	c->viewport_mode         = armpack_map_get_i32(old, "viewport_mode");
	c->pathtrace_mode        = armpack_map_get_i32(old, "pathtrace_mode");
	c->pathtrace_frames      = armpack_map_get_i32(old, "pathtrace_frames");
	c->pressure_radius       = armpack_map_get_i32(old, "pressure_radius") > 0;
	c->pressure_sensitivity  = armpack_map_get_f32(old, "pressure_sensitivity");
	c->layer_res             = armpack_map_get_i32(old, "layer_res");
	c->brush_live            = armpack_map_get_i32(old, "brush_live") > 0;
	c->node_previews         = armpack_map_get_i32(old, "node_previews") > 0;
	c->pressure_hardness     = armpack_map_get_i32(old, "pressure_hardness") > 0;
	c->pressure_angle        = armpack_map_get_i32(old, "pressure_angle") > 0;
	c->pressure_opacity      = armpack_map_get_i32(old, "pressure_opacity") > 0;
	c->material_live         = armpack_map_get_i32(old, "material_live") > 0;
	c->brush_depth_reject    = armpack_map_get_i32(old, "brush_depth_reject") > 0;
	c->brush_angle_reject    = armpack_map_get_i32(old, "brush_angle_reject") > 0;
	c->brush_alpha_discard   = armpack_map_get_f32(old, "brush_alpha_discard");
	c->dilate_radius         = armpack_map_get_i32(old, "dilate_radius");
	c->blender               = string_copy(any_map_get(old, "blender"));
	c->grid_snap             = armpack_map_get_i32(old, "grid_snap") > 0;
	c->auto_run_scripts      = armpack_map_get_i32(old, "auto_run_scripts") > 0;
	c->experimental          = armpack_map_get_i32(old, "experimental") > 0;
	c->neural_res            = armpack_map_get_i32(old, "neural_res");
	c->console_model         = armpack_map_get_i32(old, "console_model");
	c->tools                 = armpack_map_get_i32(old, "tools");
	c->view2d_grid_show      = armpack_map_get_i32(old, "view2d_grid_show") > 0;
	c->view2d_grid_cell      = armpack_map_get_i32(old, "view2d_grid_cell");
	c->view2d_grid_snap      = armpack_map_get_i32(old, "view2d_grid_snap") > 0;
	c->pixel_snap            = armpack_map_get_i32(old, "pixel_snap") > 0;
	c->large_asset_previews  = armpack_map_get_i32(old, "large_asset_previews") > 0;
	return c;
}

config_t *config_from_version_2(any_map_t *old) {
	config_t *def = config_create_default();
	armpack_map_set_i32(old, "large_asset_previews", def->large_asset_previews);
	return config_from_map_to_config(old);
}

config_t *config_from_version_1(any_map_t *old) {
	config_t *def = config_create_default();
	armpack_map_set_i32(old, "material_params", def->material_params);
	armpack_map_set_i32(old, "brush_params", def->brush_params);
	armpack_map_set_i32(old, "mesh_params", def->mesh_params);
	armpack_map_set_i32(old, "header_pinned", def->header_pinned);
	armpack_map_set_i32(old, "auto_run_scripts", def->auto_run_scripts);
	armpack_map_set_i32(old, "tools", def->tools);
	armpack_map_set_i32(old, "pixel_snap", def->pixel_snap);
	any_map_set(old, "layout_tabs", def->layout_tabs);
	armpack_map_set_f32(old, "rp_vignette", def->rp_vignette);
	armpack_map_set_i32(old, "viewport_mode", def->viewport_mode);
	armpack_map_set_i32(old, "pathtrace_mode", def->pathtrace_mode);
	return config_from_version_2(old);
}

static bool config_is_version(char *s, char *version) {
	char *prefix = string("{\"version\":\"%s\"", version);
	bool  r      = starts_with(s, prefix);
	free(prefix);
	return r;
}

config_t *config_from_old(char *s) {
	config_t *(*fns[])(any_map_t *) = {
	    config_from_version_1,
	    config_from_version_2,
	};
	for (i32 v = sizeof(fns) / sizeof(fns[0]) - 1; v >= 0; --v) {
		if (config_is_version(s, i32_to_string(v + 1))) {
			buffer_t *b = json_encode_to_armpack(s);
			return b != NULL ? fns[v](armpack_decode_to_map(b)) : NULL;
		}
	}
	return NULL;
}

config_t *config_from_json(char *s) {
	if (config_is_version(s, manifest_version_config)) {
		return json_parse(s);
	}
	return config_from_old(s);
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
	iron_window_options_t *ops   = ALLOC_INIT(iron_window_options_t, {.title     = title,
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
	config_loaded = false;
	config_init();
	ui_dock_reset();
	translator_load_translations(g_config->locale);
	config_apply();
	config_load_theme(g_config->theme, true);
}

void config_import_from(config_t *from) {
	char *_sha        = g_config->sha;
	char *_version    = g_config->version;
	g_config          = from;
	g_config->sha     = string_copy(_sha);
	g_config->version = string_copy(_version);
	keymap_load();
	base_set_workspace(WORKSPACE_PRESET_NONE);
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

void config_set_texture_res(i32 pos) {
	if (pos != TEXTURE_RES_CUSTOM) {
		f32 res    = (f32)config_texture_res_size(pos);
		base_res_x = res;
		base_res_y = res;
	}
}

i32 config_get_texture_res_x() {
	return (i32)base_res_x;
}

i32 config_get_texture_res_y() {
	return (i32)base_res_y;
}

i32 config_get_texture_res_pos(i32 i) {
	return i == 2048    ? TEXTURE_RES_RES2048
	       : i == 4096  ? TEXTURE_RES_RES4096
	       : i == 8192  ? TEXTURE_RES_RES8192
	       : i == 16384 ? TEXTURE_RES_RES16384
	                    : TEXTURE_RES_CUSTOM;
}

void config_load_theme(char *theme, bool tag_redraw) {
	g_theme = ui_theme_create();

	if (!string_equals(theme, "default.json")) {
		buffer_t   *b      = data_get_blob(string("themes/%s", theme));
		ui_theme_t *parsed = json_parse(sys_buffer_to_string(b));
		g_theme            = parsed;
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

bool config_is_raytrace_fast() {
	return g_config->pathtrace_mode == PATHTRACE_MODE_FAST;
}
