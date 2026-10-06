
#include "global.h"

void player_update() {
	tab_timeline_update();
}

void player_run_scripts() {
	g_context->tool = TOOL_TYPE_CURSOR;
	if (g_project->script_datas != NULL && g_project->script_datas->length > 0) {
		minic_eval(g_project->script_datas->buffer[0]);
	}
	tab_timeline_play();
}

static i32_array_t     *player_base_objects         = NULL;
static camera_object_t *player_base_camera          = NULL;
static i32              player_base_camera_index    = 0;
static i32              player_base_camera_controls = 0;
static i32              player_base_viewport_mode   = 0;
static void            *player_base_viewport_shader = NULL;
static i32              player_base_tool            = 0;

static void player_collect_objects(object_t *o, any_array_t *out) {
	for (i32 i = 0; i < o->children->length; ++i) {
		object_t *c = o->children->buffer[i];
		any_array_push(out, c);
		player_collect_objects(c, out);
	}
}

void player_runtime_capture_objects() {
	any_array_t *objects = any_array_create(0);
	player_collect_objects(_scene_root, objects);
	player_base_objects = i32_array_create(0);
	for (i32 i = 0; i < objects->length; ++i) {
		i32_array_push(player_base_objects, ((object_t *)objects->buffer[i])->uid);
	}
}

void player_runtime_capture() {
	player_runtime_capture_objects();
	player_base_camera          = scene_camera;
	player_base_camera_index    = array_index_of(_scene_root->children, scene_camera->base);
	player_base_camera_controls = g_context->camera_controls;
	player_base_viewport_mode   = g_context->viewport_mode;
	player_base_viewport_shader = g_context->viewport_shader;
	player_base_tool            = g_context->tool;
}

static bool player_keep_object(object_t *o) {
	if (o == scene_camera->base || i32_array_index_of(player_base_objects, o->uid) >= 0) {
		return true;
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (g_project->_->paint_objects->buffer[i]->base == o) {
			return true; // Removed by project_cleanup()
		}
	}
	// Created by the editor on demand
	if (g_context->merged_object != NULL && g_context->merged_object->base == o) {
		return true;
	}
	for (i32 i = 0; i < 32; ++i) {
		if (g_context->particles[i].bullet == o) {
			return true;
		}
	}
	if (render_compass_is_hitbox(o)) {
		return true;
	}
	return false;
}

// Objects added to the scene directly are not part of the project and survive the reload
static void player_remove_new_objects() {
	any_array_t *objects = any_array_create(0);
	player_collect_objects(_scene_root, objects);
	// Move the kept objects out of the removed ones first
	for (i32 i = 0; i < objects->length; ++i) {
		object_t *o = objects->buffer[i];
		if (player_keep_object(o) && o->parent != _scene_root && !player_keep_object(o->parent)) {
			object_set_parent(o, NULL);
		}
	}
	// Removing an object frees its children, collect the topmost ones before removing
	any_array_t *removed = any_array_create(0);
	for (i32 i = 0; i < objects->length; ++i) {
		object_t *o = objects->buffer[i];
		if (!player_keep_object(o) && (o->parent == _scene_root || player_keep_object(o->parent))) {
			any_array_push(removed, o);
		}
	}
	for (i32 i = 0; i < removed->length; ++i) {
		object_remove(removed->buffer[i]);
	}
}

static bool player_workspace_on   = false;
static i32  player_workspace_last = 0;
static bool player_ui_show_last   = true;
static bool player_lock_last      = false;

void player_set_workspace(bool on) {
	if (on) {
		player_workspace_last = g_config->workspace;
		player_ui_show_last   = ui_base_show;
		player_lock_last      = base_player_lock;
		g_config->workspace   = WORKSPACE_PLAYER;
		ui_base_show          = false;
		ui_menu_show          = false;
		base_player_lock      = true;
	}
	else {
		g_config->workspace = player_workspace_last;
		ui_base_show        = player_ui_show_last;
		base_player_lock    = player_lock_last;
	}
	player_workspace_on = on;
	render_path_resize();
	base_resize();
}

void player_runtime_reset() {
	script_reset_runtime();
	trait_reset();
	if (player_workspace_on) {
		player_set_workspace(false);
	}
	scene_camera = player_base_camera;
	if (scene_camera->base->parent != _scene_root) {
		object_set_parent(scene_camera->base, NULL);
		// Back to its place among the root objects
		array_remove(_scene_root->children, scene_camera->base);
		i32 i = player_base_camera_index;
		array_insert(_scene_root->children, i >= 0 && i <= _scene_root->children->length ? i : _scene_root->children->length, scene_camera->base);
	}
	player_remove_new_objects();
}

void player_runtime_restore_context() {
	g_context->camera_controls = player_base_camera_controls;
	context_set_viewport_mode(player_base_viewport_mode);
	if (g_context->viewport_shader != player_base_viewport_shader) {
		context_set_viewport_shader(player_base_viewport_shader);
	}
	context_select_tool(player_base_tool);
}

static buffer_t *player_snapshot          = NULL;
static char     *player_snapshot_path     = NULL;
static char     *player_snapshot_filepath = NULL;
static char     *player_snapshot_filename = NULL;
static char     *player_snapshot_title    = NULL;
static bool      player_stopping          = false;
static i32       player_reload_frames     = 0;

void player_start(void *_) {
	if (player_in_editor || agent_running) {
		return;
	}
	char *filepath           = g_project->_->filepath;
	player_snapshot_filepath = string_copy(filepath);
	player_snapshot_path     = string_equals(filepath, "") ? string_copy(string("%splayer.arm", iron_internal_save_path())) : string_copy(filepath);
	player_snapshot_filename = string_copy(ui_files_filename);
	player_snapshot_title    = string_copy(sys_title());
	player_snapshot          = export_arm_encode_project(player_snapshot_path);

	player_runtime_capture();
	player_in_editor = true;
	player_set_workspace(true);
	player_run_scripts();
}

static void player_stop_reload(void *_) {
	iron_delay_idle_sleep();
	if (--player_reload_frames > 0) {
		sys_notify_on_next_frame(player_stop_reload, NULL);
		return;
	}

	if (data_cached_blobs == NULL) {
		data_cached_blobs = any_map_create();
	}
	any_map_set(data_cached_blobs, player_snapshot_path, player_snapshot);
	player_snapshot = NULL;
	import_arm_run_project(player_snapshot_path);

	if (string_equals(player_snapshot_filepath, "")) {
		g_project->_->filepath = "";
		char *recent           = player_snapshot_path;
#ifdef IRON_WINDOWS
		recent = string_replace_all(recent, "\\", "/");
#endif
		string_array_remove(g_config->recent_projects, recent);
		config_save();
	}
	ui_files_filename = player_snapshot_filename;
	sys_title_set(player_snapshot_title);

	player_runtime_capture_objects();
	player_runtime_restore_context();
	player_in_editor = false;
	player_stopping  = false;
}

void player_stop() {
	if (!player_in_editor || player_stopping) {
		return;
	}
	player_stopping = true;
	player_runtime_reset();
	player_reload_frames = GPU_FRAMEBUFFER_COUNT + 1;
	sys_notify_on_next_frame(player_stop_reload, NULL);
}
