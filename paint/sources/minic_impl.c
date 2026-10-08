
#include "global.h"

static i32 script_stage_index(char *name) {
	if (g_project->stages == NULL || name == NULL) {
		return -1;
	}
	for (i32 i = 0; i < g_project->stages->length; ++i) {
		if (string_equals(g_project->stages->buffer[i]->name, name)) {
			return i;
		}
	}
	return -1;
}

void script_set_stage(char *name) {
	i32 i = script_stage_index(name);
	if (i >= 0) {
		tab_stages_selected = i;
		tab_stages_apply(g_project->stages->buffer[i]);
	}
}

void script_stage_create(char *name) {
	if (name == NULL || script_stage_index(name) >= 0) {
		return;
	}
	if (g_project->stages == NULL) {
		g_project->stages = any_array_create_from_raw((void *[]){}, 0);
	}
	stage_t *s = tab_stages_create_stage(string_copy(name));
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		string_array_push(s->layers, g_project->_->layers->buffer[i]->name);
	}
	any_array_push(g_project->stages, s);
}

void script_stage_add_object(char *stage, char *object) {
	i32 i = script_stage_index(stage);
	if (i < 0 || object == NULL) {
		return;
	}
	stage_t *s = g_project->stages->buffer[i];
	if (string_array_index_of(s->objects, object) < 0) {
		string_array_push(s->objects, string_copy(object));
	}
}

void script_stage_remove_object(char *stage, char *object) {
	i32 i = script_stage_index(stage);
	if (i < 0 || object == NULL) {
		return;
	}
	stage_t *s   = g_project->stages->buffer[i];
	i32      idx = string_array_index_of(s->objects, object);
	if (idx >= 0) {
		array_splice(s->objects, idx, 1);
	}
	tab_stages_set_hidden(s, object, false);
}

void script_stage_set_hidden(char *stage, char *object, bool hidden) {
	i32 i = script_stage_index(stage);
	if (i >= 0 && object != NULL) {
		tab_stages_set_hidden(g_project->stages->buffer[i], string_copy(object), hidden);
	}
}

void script_show_envmap(bool b) {
	g_context->show_envmap = b;
	context_load_envmap();
	g_context->ddirty = 2;
}

char *script_get_stage() {
	stage_t *s = tab_stages_get_stage();
	return s != NULL ? s->name : NULL;
}

void script_set_tilesheet_anim(object_t *o, char *anim) {
	mesh_object_t *mo = (mesh_object_t *)o->ext;

	// Locate the material slot
	i32 slot_index = tab_meshes_get_override(mo);
	if (slot_index < 0) {
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			if (g_project->_->materials->buffer[i]->data == mo->material) {
				slot_index = i;
				break;
			}
		}
	}

	slot_material_t *slot = g_project->_->materials->buffer[slot_index];

	// Locate the tilesheet animation node
	for (i32 i = 0; i < slot->canvas->nodes->length; ++i) {
		ui_node_t *node = slot->canvas->nodes->buffer[i];
		if (!string_equals(node->type, "TILESHEET_ANIM")) {
			continue;
		}

		ui_node_button_t *enum_but = node->buttons->buffer[4];
		char             *texts    = u8_array_to_string(enum_but->data);
		string_array_t   *names    = string_split(texts, "\n");
		i32               match    = -1;
		for (i32 j = 0; j < (i32)names->length; ++j) {
			if (string_equals(names->buffer[j], anim)) {
				match = j;
				break;
			}
		}
		string_split_free(names);
		free(texts);
		if (match == -1) {
			continue;
		}

		enum_but->default_value->buffer[0] = (f32)match;
		make_material_parse_paint_material(true);

		// Material override
		for (i32 k = 0; k < g_project->_->paint_objects->length; ++k) {
			mesh_object_t *po = g_project->_->paint_objects->buffer[k];
			if (tab_meshes_get_override(po) == slot_index) {
				tab_meshes_set_override(po, slot_index);
				g_context->ddirty = 2;
			}
		}
		return;
	}
}

static transform_t *_script_tween_transform = NULL;

static void script_tween_done(void) {
	_script_tween_transform = NULL;
}

static void script_tween_tick(void) {
	_script_tween_transform->dirty = true;
}

void script_tween_to(object_t *o, vec4_t to, f32 speed) {
	if (_script_tween_transform != NULL) {
		return;
	}

	transform_t *t          = o->transform;
	_script_tween_transform = t;
	f32    duration         = vec4_dist(t->loc, to) / speed;
	ease_t ease             = EASE_LINEAR;
	tween_to(
	    ALLOC_INIT(tween_anim_t, {.target = &t->loc.x, .to = to.x, .duration = duration, .ease = ease, .tick = script_tween_tick, .done = script_tween_done}));
	tween_to(ALLOC_INIT(tween_anim_t, {.target = &t->loc.y, .to = to.y, .duration = duration, .ease = ease}));
	tween_to(ALLOC_INIT(tween_anim_t, {.target = &t->loc.z, .to = to.z, .duration = duration, .ease = ease}));
}

static void script_timer_done(void *fn) {
	minic_call_fn(fn, NULL, 0);
}

void script_timer(f32 delay, void *fn) {
	tween_timer(delay, script_timer_done, fn);
}

void *script_update_fn = NULL;
void  script_on_update(void *_) {
    iron_delay_idle_sleep();
    minic_call_fn(script_update_fn, NULL, 0);
}
void script_notify_on_update(void *fn) {
	if (script_update_fn == NULL) {
		sys_notify_on_update(script_on_update, NULL);
	}
	script_update_fn = fn;
}

void *script_next_frame_fn = NULL;
void  script_on_next_frame(void *_) {
    void *fn             = script_next_frame_fn;
    script_next_frame_fn = NULL;
    if (fn != NULL) {
        minic_call_fn(fn, NULL, 0);
    }
}
void script_notify_on_next_frame(void *fn) {
	sys_notify_on_next_frame(script_on_next_frame, NULL);
	script_next_frame_fn = fn;
}

bool script_is_running(void) {
	return script_update_fn != NULL || script_next_frame_fn != NULL;
}

void script_stop(void) {
	if (script_update_fn != NULL) {
		sys_remove_update(script_on_update);
		script_update_fn = NULL;
	}
	script_next_frame_fn = NULL;
}

void *_ui_files_done;
void  _ui_files_show_done(char *path) {
    minic_val_t args[1] = {minic_val_ptr(path)};
    minic_call_fn(_ui_files_done, args, 1);
}
void ui_files_show2(char *filters, bool is_save, bool open_multiple, void *files_done) {
	_ui_files_done = files_done;
	ui_files_show(filters, is_save, open_multiple, _ui_files_show_done);
}

static any_array_t *script_cloud_init_fns  = NULL;
static any_array_t *script_cloud_dl_paths  = NULL;
static any_array_t *script_cloud_dl_fns    = NULL;
static bool         script_cloud_dl_active = false;
static void         script_cloud_dl_next(void);

static bool script_cloud_ready(void) {
	return file_cloud != NULL && any_map_get(file_cloud, "cloud") != NULL;
}

static void script_cloud_on_init_done(void) {
	ui_files_file_browser_on_init_cloud_done();
}

static void script_cloud_wait(void *_) {
	if (!script_cloud_ready()) {
		sys_notify_on_next_frame(script_cloud_wait, NULL);
		return;
	}
	any_array_t *fns      = script_cloud_init_fns;
	script_cloud_init_fns = NULL;
	for (i32 i = 0; i < fns->length; ++i) {
		minic_call_fn(fns->buffer[i], NULL, 0);
	}
	script_cloud_dl_next();
}

static void script_cloud_ensure(void *fn) {
	if (script_cloud_init_fns == NULL) {
		script_cloud_init_fns = any_array_create(0);
		if (file_cloud == NULL) {
			file_init_cloud(script_cloud_on_init_done, g_config->server);
		}
		sys_notify_on_next_frame(script_cloud_wait, NULL);
	}
	if (fn != NULL) {
		any_array_push(script_cloud_init_fns, fn);
	}
}

void script_cloud_init(void *done) {
	if (script_cloud_ready()) {
		minic_call_fn(done, NULL, 0);
		return;
	}
	script_cloud_ensure(done);
}

static void script_cloud_dl_done(char *dest) {
	void *fn = array_shift(script_cloud_dl_fns);
	array_shift(script_cloud_dl_paths);
	script_cloud_dl_active = false;
	if (fn != NULL) {
		minic_val_t args[1] = {minic_val_ptr(dest != NULL ? string_copy(dest) : NULL)};
		minic_call_fn(fn, args, 1);
	}
	script_cloud_dl_next();
}

static void script_cloud_dl_next(void) {
	if (script_cloud_dl_active || script_cloud_dl_paths == NULL || script_cloud_dl_paths->length == 0) {
		return;
	}
	if (!script_cloud_ready()) {
		script_cloud_ensure(NULL);
		return;
	}
	char *path = script_cloud_dl_paths->buffer[0];
	script_cloud_dl_active = true;
	if (i32_map_get(file_cloud_sizes, path) < 0) {
		console_error(string("Cloud file not found: %s", path));
		script_cloud_dl_done(NULL);
		return;
	}
	file_cache_cloud(path, script_cloud_dl_done, g_config->server);
}

void script_cloud_download(char *path, void *done) {
	if (script_cloud_dl_paths == NULL) {
		script_cloud_dl_paths = any_array_create(0);
		script_cloud_dl_fns   = any_array_create(0);
	}
	any_array_push(script_cloud_dl_paths, string_copy(path));
	any_array_push(script_cloud_dl_fns, done);
	script_cloud_dl_next();
}

static void script_cloud_reset(void) {
	if (script_cloud_init_fns != NULL) {
		script_cloud_init_fns->length = 0;
	}
	if (script_cloud_dl_paths != NULL) {
		i32 keep                      = script_cloud_dl_active ? 1 : 0;
		script_cloud_dl_paths->length = keep;
		script_cloud_dl_fns->length   = keep;
		if (keep) {
			script_cloud_dl_fns->buffer[0] = NULL;
		}
	}
}

char *project_filepath_get() {
#ifdef IRON_WINDOWS
	return string_replace_all(g_project->_->filepath, "\\", "/");
#else
	return g_project->_->filepath;
#endif
}
char *project_basepath_get() {
	char *path = project_filepath_get();
	return substring(path, 0, string_last_index_of(path, "/"));
}
void project_filepath_set(char *s) {
#ifdef IRON_WINDOWS
	s = string_replace_all(s, "/", "\\");
#endif
	g_project->_->filepath = string_copy(s);
}
context_t *script_get_context() {
	return g_context;
}
config_t *script_get_config() {
	return g_config;
}
project_t *script_get_project() {
	return g_project;
}

object_t *script_get_object(char *s) {
	for (int i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (string_equals(g_project->_->paint_objects->buffer[i]->base->name, s)) {
			return g_project->_->paint_objects->buffer[i]->base;
		}
	}
	return NULL;
}

slot_material_t *script_get_material(char *s) {
	for (int i = 0; i < g_project->_->materials->length; ++i) {
		if (string_equals(g_project->_->materials->buffer[i]->canvas->name, s)) {
			return g_project->_->materials->buffer[i];
		}
	}
	return NULL;
}

sound_t *script_get_sound(char *s) {
	for (int i = 0; i < g_project->_->sounds->length; ++i) {
		if (string_equals(g_project->_->sounds->buffer[i]->name, s)) {
			return g_project->_->sounds->buffer[i]->sound;
		}
	}
	return NULL;
}

i32 script_sound_remove(char *s) {
	for (int i = 0; i < g_project->_->sounds->length; ++i) {
		slot_sound_t *sound = g_project->_->sounds->buffer[i];
		if (string_equals(sound->name, s)) {
			data_delete_sound(sound->file);
			array_splice(g_project->_->sounds, i, 1);
			return 1;
		}
	}
	return 0;
}

gpu_texture_t *script_get_texture(char *s) {
	for (int i = 0; i < g_project->_->assets->length; ++i) {
		if (string_equals(g_project->_->assets->buffer[i]->name, s)) {
			return g_project->_->assets->buffer[i]->image;
		}
	}
	return NULL;
}

void script_texture_delete(char *s) {
	if (s == NULL || g_project == NULL || g_project->_ == NULL || g_project->_->assets == NULL) {
		return;
	}
	for (int i = 0; i < g_project->_->assets->length; ++i) {
		asset_t *asset = g_project->_->assets->buffer[i];
		if (string_equals(asset->name, s)) {
			tab_textures_delete_texture(asset);
			return;
		}
	}
}

static bool script_paint_active = false;
static bool script_paint_first  = true;

static bool script_paint_allowed(void) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return false;
	}
	if (slot_layer_is_group(g_context->layer)) {
		return false;
	}
	if (g_context->layer->fill_material != NULL && g_context->tool != TOOL_TYPE_PICKER && g_context->tool != TOOL_TYPE_MATERIAL &&
	    g_context->tool != TOOL_TYPE_COLORID) {
		return false;
	}
	return true;
}

static void script_paint_begin_stroke(void) {
	if (script_paint_active) {
		return;
	}
	make_material_parse_paint_material(false);
	render_path_base_draw_gbuffer();

	history_push_undo = true;
	sculpt_push_undo  = true;
	if (history_undo_layers != NULL) {
		history_paint();
	}

	script_paint_active         = true;
	script_paint_first          = true;
	g_context->brush_time       = sys_delta();
	g_context->prev_paint_vec_x = -1.0f;
	g_context->prev_paint_vec_y = -1.0f;
}

static void script_paint_at(f32 x, f32 y) {
	if (!script_paint_allowed()) {
		return;
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}

	script_paint_begin_stroke();

	f32 prev_x = script_paint_first ? x : g_context->paint_vec.x;
	f32 prev_y = script_paint_first ? y : g_context->paint_vec.y;

	g_context->decal_x          = x;
	g_context->decal_y          = y;
	g_context->paint_vec.x      = x;
	g_context->paint_vec.y      = y;
	g_context->last_paint_vec_x = prev_x;
	g_context->last_paint_vec_y = prev_y;
	g_context->prev_paint_vec_x = prev_x;
	g_context->prev_paint_vec_y = prev_y;
	g_context->pdirty           = 1;

	render_path_paint_commands_paint(false);

	script_paint_first = false;

	if (in_use) {
		draw_begin(current, false, 0);
	}
}

void script_paint(f32 x, f32 y) {
	script_paint_at(x, y);
}

void script_paint_world(f32 x, f32 y, f32 z) {
	vec4_t clip = vec4_apply_mat4((vec4_t){x, y, z, 1.0f}, scene_camera->vp);
	if (clip.w <= 0.0f) {
		return;
	}
	f32 sx = (clip.x / clip.w + 1.0f) * 0.5f;
	f32 sy = (-clip.y / clip.w + 1.0f) * 0.5f;
	script_paint_at(sx, sy);
}

void script_paint_end(void) {
	if (!script_paint_active) {
		return;
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}

	render_path_paint_dilate(true, true);
	layers_update_linked_layers();

	g_context->pdirty              = 0;
	g_context->rtdirty             = 1;
	g_context->ddirty              = 2;
	g_context->brush_time          = 0.0f;
	g_context->brush_blend_dirty   = true;
	g_context->layer_preview_dirty = true;
	g_context->prev_paint_vec_x    = -1.0f;
	g_context->prev_paint_vec_y    = -1.0f;

	script_paint_active = false;
	script_paint_first  = true;

	if (in_use) {
		draw_begin(current, false, 0);
	}
}

void script_fill_layer(void) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return;
	}
	if (slot_layer_is_group(g_context->layer)) {
		return;
	}

	history_push_undo = true;
	if (history_undo_layers != NULL) {
		history_paint();
	}
	layers_update_fill_layer(true);
	g_context->layer_preview_dirty = true;
	g_context->rtdirty             = 1;
	g_context->ddirty              = 2;
}

static ui_node_canvas_t *script_material_canvas(void) {
	if (g_context == NULL || g_context->material == NULL) {
		return NULL;
	}
	return g_context->material->canvas;
}

static void script_gpu_begin(gpu_texture_t **out_current, bool *out_in_use) {
	*out_current = _draw_current;
	*out_in_use  = gpu_in_use;
	if (*out_in_use) {
		draw_end();
	}
}

static void script_gpu_end(gpu_texture_t *current, bool in_use) {
	if (in_use) {
		draw_begin(current, false, 0);
	}
}

buffer_t *script_get_texture_pixels(gpu_texture_t *texture) {
	if (texture == NULL || texture->format == GPU_TEXTURE_FORMAT_D32) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	buffer_t *b = gpu_get_texture_pixels(texture);
	script_gpu_end(current, in_use);
	return b;
}

bool tab_layers_can_delete(slot_layer_t *l);
void tab_layers_delete_layer(slot_layer_t *l);
void base_init_undo_layers();

static bool script_layer_valid(slot_layer_t *l) {
	return l != NULL && g_project != NULL && g_project->_ != NULL && array_index_of(g_project->_->layers, l) >= 0;
}

slot_layer_t *script_layer_get(char *name) {
	if (name == NULL || g_project == NULL || g_project->_ == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (string_equals(g_project->_->layers->buffer[i]->name, name)) {
			return g_project->_->layers->buffer[i];
		}
	}
	return NULL;
}

void script_layer_set(slot_layer_t *l) {
	if (!script_layer_valid(l)) {
		return;
	}
	context_set_layer(l);
	g_context->ddirty = 2;
}

static slot_layer_t *script_layer_new_cleared(i32 base_color, f32 occlusion, f32 roughness, f32 metallic) {
	if (g_context == NULL || g_context->layer == NULL) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t *l = layers_new_layer(false, -1, NULL);
	if (l != NULL) {
		history_new_layer();
		slot_layer_clear(l, base_color, NULL, occlusion, roughness, metallic);
	}
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
	return l;
}

slot_layer_t *script_layer_new(void) {
	return script_layer_new_cleared(0x00000000, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *script_layer_new_color(i32 base_color, f32 occlusion, f32 roughness, f32 metallic) {
	return script_layer_new_cleared(base_color, occlusion, roughness, metallic);
}

slot_layer_t *script_layer_new_fill(void) {
	if (g_context == NULL || g_context->layer == NULL || g_context->material == NULL) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t *l = layers_new_layer(false, -1, NULL);
	if (l != NULL) {
		history_new_layer();
		l->uv_type = UV_TYPE_UVMAP;
		if (g_config->workflow == WORKFLOW_SCULPT) {
			sculpt_init();
			sculpt_init_sculpt_texture(l);
		}
		history_to_fill_layer();
		slot_layer_to_fill_layer(l);
	}
	script_gpu_end(current, in_use);
	g_context->rtdirty = 1;
	g_context->ddirty  = 2;
	return l;
}

void script_layer_delete(slot_layer_t *l) {
	if (!script_layer_valid(l) || !tab_layers_can_delete(l)) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	tab_layers_delete_layer(l);
	script_gpu_end(current, in_use);
}

i32 script_layer_count(void) {
	if (g_project == NULL || g_project->_ == NULL) {
		return 0;
	}
	return g_project->_->layers->length;
}

slot_layer_t *script_layer_at(i32 i) {
	if (i < 0 || i >= script_layer_count()) {
		return NULL;
	}
	return g_project->_->layers->buffer[i];
}

i32 script_layer_index(slot_layer_t *l) {
	if (l == NULL || g_project == NULL || g_project->_ == NULL) {
		return -1;
	}
	return array_index_of(g_project->_->layers, l);
}

slot_layer_t *script_layer_new_mask(slot_layer_t *l, i32 type) {
	// type: 0 black, 1 white, 2 fill with the current material
	if (!script_layer_valid(l) || type < 0 || type > 2) {
		return NULL;
	}
	if (type == 2 && g_context->material == NULL) {
		return NULL;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	context_set_layer(l);
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *m        = layers_new_mask(false, l, -1);
	if (m != NULL) {
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *mat = g_project->_->materials->buffer[i];
			tab_layers_remap_layer_pointers(mat->canvas->nodes, tab_layers_fill_layer_map(pointers));
		}
		if (type == 0) {
			history_new_black_mask();
			slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		}
		else if (type == 1) {
			history_new_white_mask();
			slot_layer_clear(m, 0xffffffff, NULL, 1.0, layers_default_rough, 0.0);
		}
		else {
			history_new_fill_mask();
			slot_layer_clear(m, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
			slot_layer_to_fill_layer(m);
		}
		layers_update_fill_layers();
	}
	script_gpu_end(current, in_use);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty             = 1;
	g_context->ddirty              = 2;
	return m;
}

slot_layer_t *script_layer_new_group(slot_layer_t *l) {
	if (!script_layer_valid(l)) {
		return NULL;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	if (slot_layer_is_group(l) || slot_layer_is_in_group(l)) {
		return NULL;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	base_init_undo_layers();
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *group    = layers_new_group();
	if (group != NULL) {
		context_set_layer(l);
		array_remove(g_project->_->layers, group);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, l) + 1, group);
		l->parent = group;
		for (i32 i = 0; i < g_project->_->materials->length; ++i) {
			slot_material_t *m = g_project->_->materials->buffer[i];
			tab_layers_remap_layer_pointers(m->canvas->nodes, tab_layers_fill_layer_map(pointers));
		}
		context_set_layer(group);
		history_new_group();
	}
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
	return group;
}

static void script_layer_refill(slot_layer_t *l) {
	slot_material_t *material = g_context->material;
	g_context->material       = l->fill_material;
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	layers_update_fill_layers();
	context_set_material(material);
}

void script_layer_set_name(slot_layer_t *l, char *name) {
	if (!script_layer_valid(l) || name == NULL || name[0] == '\0') {
		return;
	}
	char *old_name = l->name;
	l->name        = string_copy(slot_layer_unique_name(l, name));
	tab_stages_rename_layer(old_name, l->name);
	if (ui_base_hwnds != NULL) {
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	}
}

void script_layer_update(slot_layer_t *l) {
	if (!script_layer_valid(l)) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	if (l->fill_material != NULL) {
		script_layer_refill(l);
	}
	make_material_parse_mesh_material();
	script_gpu_end(current, in_use);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
}

bool script_layer_move(slot_layer_t *l, i32 to) {
	if (!script_layer_valid(l) || to < 0 || to >= g_project->_->layers->length) {
		return false;
	}
	if (!slot_layer_can_move(l, to)) {
		return false;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	slot_layer_move(l, to);
	make_material_parse_mesh_material();
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
	return true;
}

bool script_layer_set_group(slot_layer_t *l, slot_layer_t *group) {
	if (!script_layer_valid(l) || !slot_layer_is_layer(l) || slot_layer_is_filter(l)) {
		return false;
	}
	if (group == NULL) {
		if (l->parent == NULL) {
			return true;
		}
		return script_layer_move(l, g_project->_->layers->length - 1);
	}
	if (!script_layer_valid(group) || !slot_layer_is_group(group)) {
		return false;
	}
	if (l->parent == group) {
		return true;
	}
	// Place directly below the group and its masks, slot_layer_move then parents it to the group
	i32 anchor = array_index_of(g_project->_->layers, group);
	while (anchor > 0 && slot_layer_is_mask(g_project->_->layers->buffer[anchor - 1]) && g_project->_->layers->buffer[anchor - 1]->parent == group) {
		--anchor;
	}
	i32  from       = array_index_of(g_project->_->layers, l);
	i32  to         = from < anchor ? anchor - 1 : anchor;
	bool show_panel = group->show_panel;
	group->show_panel = true; // A collapsed group would make the move skip past its children
	bool moved        = script_layer_move(l, to);
	group->show_panel = show_panel;
	return moved && l->parent == group;
}

void script_layer_set_object(slot_layer_t *l, object_t *object) {
	if (!script_layer_valid(l)) {
		return;
	}
	if (slot_layer_is_mask(l) || slot_layer_is_filter(l)) {
		l = l->parent;
	}
	i32 mask = 0;
	if (object != NULL) {
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			if (g_project->_->paint_objects->buffer[i]->base == object) {
				mask = i + 1;
				break;
			}
		}
		if (mask == 0) {
			return;
		}
	}
	if (l->object_mask == mask) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	context_set_layer(l);
	history_layer_object();
	l->object_mask = mask;
	make_material_parse_mesh_material();
	if (l->fill_material != NULL) {
		script_layer_refill(l);
	}
	else {
		layers_set_object_mask();
	}
	script_gpu_end(current, in_use);
	g_context->rtdirty = 1;
	g_context->ddirty  = 2;
}

static void script_bake_lightmap_done(void *fn) {
	if (fn != NULL) {
		minic_call_fn(fn, NULL, 0);
	}
}

void script_bake_lightmap(object_t *o, i32 res, i32 samples, f32 range, char *path, void *done) {
	if (o == NULL || o->ext == NULL || !string_equals(o->ext_type, "mesh_object_t") || path == NULL) {
		return;
	}
	render_path_raytrace_bake_lightmap(o->ext, res, samples, range, path, script_bake_lightmap_done, done);
}

void script_quit(void) {
	iron_stop();
}

void script_project_new(void) {
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	project_new(true);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

static char *script_resolve_path(char *path) {
	// iron_file_exists() checks relative paths against the cwd, data_get_blob() against data/
	if (data_is_abs(path) || data_is_up(path) || starts_with(path, "./")) {
		return path;
	}
	return string("./%s", path);
}

void script_project_open(char *path) {
	if (path == NULL || !iron_file_exists(path)) {
		return;
	}
	path = script_resolve_path(path);
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	import_arm_run_project(path);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

void script_import_asset(char *path, bool hdr_as_envmap) {
	if (path == NULL || !iron_file_exists(path)) {
		return;
	}
	path = script_resolve_path(path);
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	import_asset_run(path, -1, -1, false, hdr_as_envmap, NULL);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

extern bool import_mesh_clear_layers;
extern bool import_mesh_no_reset;
extern bool import_mesh_append;

static void script_append_mesh_finish(i32 first) {
	if (g_project->_->paint_objects->length > first) {
		import_mesh_finish_import(NULL);
	}
	import_mesh_append = false;
}

void script_append_mesh(char *path) {
	if (path == NULL || !iron_file_exists(path)) {
		return;
	}
	path = script_resolve_path(path);
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	i32 first = g_project->_->paint_objects->length;
	import_mesh_run(path, false, false, true);
	script_append_mesh_finish(first);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

extern int plugins_skinning_frame;

void script_append_mesh_skinned(char *glb_path) {
	plugins_skinning_frame = 0;
	script_append_mesh(glb_path);
	plugins_skinning_frame = -1;
}

void script_append_mesh_obj(char *data) {
	if (data == NULL || data[0] == '\0') {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	import_mesh_clear_layers = false;
	import_mesh_no_reset     = true;
	import_mesh_append       = true;
	g_context->layer_filter  = 0;
	buffer_t *b              = buffer_create_from_raw((u8 *)data, strlen(data));
	obj_parse_y_to_z_up      = false;
	i32 first                = g_project->_->paint_objects->length;
	import_obj_parse(b, false);
	obj_parse_y_to_z_up = true;
	free(b);
	script_append_mesh_finish(first);
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

bool script_packed_asset_save(char *suffix, char *path) {
	for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
		packed_asset_t *pa = g_project->packed_assets->buffer[i];
		if (ends_with(pa->name, suffix)) {
			iron_file_save_bytes(path, (buffer_t *)pa->bytes, ((buffer_t *)pa->bytes)->length);
			return true;
		}
	}
	return false;
}

i32 script_packed_assets_remove(char *search) {
	if (g_project->packed_assets == NULL || search == NULL) {
		return 0;
	}
	i32 removed = 0;
	i32 i       = 0;
	while (i < g_project->packed_assets->length) {
		if (string_index_of(g_project->packed_assets->buffer[i]->name, search) >= 0) {
			array_splice(g_project->packed_assets, i, 1);
			removed++;
		}
		else {
			i++;
		}
	}
	return removed;
}

i32 script_assets_remove(char *search) {
	if (search == NULL) {
		return 0;
	}
	i32 removed = 0;
	i32 i       = 0;
	while (i < g_project->_->assets->length) {
		asset_t *a = g_project->_->assets->buffer[i];
		if (string_index_of(a->file, search) >= 0) {
			tab_textures_delete_texture(a);
			removed++;
		}
		else {
			i++;
		}
	}
	return removed;
}

void script_set_pack_assets(i32 pack) {
	g_context->pack_assets_on_save = pack != 0;
}

void script_export_mesh(char *path) {
	if (path == NULL) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	export_mesh_run(path, NULL, true);
	script_gpu_end(current, in_use);
}

void script_export_material(char *path) {
	if (path == NULL || g_context == NULL || g_context->material == NULL) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	export_arm_run_material(path);
	script_gpu_end(current, in_use);
}

slot_material_t *script_material_create(char *name) {
	if (g_project == NULL || g_project->_ == NULL || g_project->_->materials == NULL || g_project->_->materials->length == 0) {
		return NULL;
	}
	shader_data_t   *data = g_project->_->materials->buffer[0]->data;
	slot_material_t *m    = slot_material_create(data, NULL);
	if (name != NULL && name[0] != '\0') {
		m->canvas->name = string_copy(slot_material_unique_name(m->canvas, name));
	}
	any_array_push(g_project->_->materials, m);

	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	context_set_material(m);
	util_render_make_material_preview();
	script_gpu_end(current, in_use);

	history_new_material();
	if (ui_base_hwnds != NULL && ui_base_hwnds->length > 1) {
		ui_base_hwnds->buffer[1]->redraws = 2;
	}
	return m;
}

void script_material_set(slot_material_t *m) {
	if (m == NULL) {
		return;
	}
	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	context_set_material(m);
	util_render_make_material_preview();
	script_gpu_end(current, in_use);
}

static slot_material_t *script_object_material_slot = NULL;
static shader_data_t   *script_object_material_data = NULL;

static void script_object_material_reset(void) {
	script_object_material_slot = NULL;
	script_object_material_data = NULL;
}

void script_object_set_material(object_t *o, slot_material_t *m) {
	if (o == NULL || g_project == NULL || g_project->_ == NULL || g_project->_->materials == NULL) {
		return;
	}
	if (!string_equals(o->ext_type, "mesh_object_t")) {
		return;
	}
	i32 index = m != NULL ? array_index_of(g_project->_->materials, m) : -1;
	if (index >= 0 && m != script_object_material_slot) {
		script_object_material_reset();
		script_object_material_slot = m;
		script_object_material_data = make_mesh_preview_viewport(m);
	}
	tab_meshes_set_override_data(o->ext, index, index >= 0 ? script_object_material_data : NULL);
	g_project->mesh_materials = i32_array_create(0);
	g_context->ddirty         = 2;
}

void script_material_delete(slot_material_t *m) {
	if (m == NULL || g_project == NULL || g_project->_ == NULL || g_project->_->materials == NULL) {
		return;
	}
	if (m == script_object_material_slot) {
		script_object_material_reset();
	}
	if (g_project->_->materials->length <= 1) {
		return;
	}
	if (array_index_of(g_project->_->materials, m) < 0) {
		return;
	}
	tab_materials_delete_material(m);
}

ui_node_t *script_material_create_node(char *type) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (type == NULL || canvas == NULL) {
		return NULL;
	}
	nodes_material_init();
	ui_node_t *n = nodes_material_get_node_t(type);
	if (n == NULL) {
		return NULL;
	}
	ui_node_t *node = ui_nodes_make_node(n, g_context->material->nodes, canvas);
	any_array_push(canvas->nodes, node);
	ui_nodes_hwnd->redraws = 2;
	return node;
}

ui_node_t *script_material_create_node_at(char *type, f32 x, f32 y) {
	ui_node_t *node = script_material_create_node(type);
	if (node != NULL) {
		node->x = x;
		node->y = y;
	}
	return node;
}

ui_node_t *script_material_get_node(char *type) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL || type == NULL) {
		return NULL;
	}
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, type)) {
			return n;
		}
	}
	return NULL;
}

ui_node_t *script_material_get_node_id(i32 id) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL) {
		return NULL;
	}
	return ui_get_node(canvas->nodes, id);
}

static void script_material_remove_links_to(ui_node_canvas_t *canvas, i32 to_id, i32 to_socket) {
	i32 i = 0;
	while (i < canvas->links->length) {
		ui_node_link_t *l = canvas->links->buffer[i];
		if (l->to_id == to_id && l->to_socket == to_socket) {
			for (i32 j = i; j < canvas->links->length - 1; ++j) {
				canvas->links->buffer[j] = canvas->links->buffer[j + 1];
			}
			canvas->links->length--;
		}
		else {
			i++;
		}
	}
}

void script_material_connect(ui_node_t *from, i32 from_socket, ui_node_t *to, i32 to_socket) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL || from == NULL || to == NULL) {
		return;
	}
	if (from_socket < 0 || from_socket >= from->outputs->length) {
		return;
	}
	if (to_socket < 0 || to_socket >= to->inputs->length) {
		return;
	}

	script_material_remove_links_to(canvas, to->id, to_socket);

	ui_node_link_t *link = project_create_node_link(canvas->links, from->id, from_socket, to->id, to_socket);
	any_array_push(canvas->links, link);

	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

void script_material_disconnect(ui_node_t *to, i32 to_socket) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL || to == NULL) {
		return;
	}
	script_material_remove_links_to(canvas, to->id, to_socket);
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

void script_material_remove_node(ui_node_t *node) {
	ui_node_canvas_t *canvas = script_material_canvas();
	if (canvas == NULL || node == NULL) {
		return;
	}
	if (string_equals(node->type, "OUTPUT_MATERIAL_PBR")) {
		return;
	}
	ui_remove_node(node, canvas);
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

static ui_node_socket_t *script_material_socket(ui_node_t *node, bool is_input, i32 socket) {
	if (node == NULL) {
		return NULL;
	}
	if (is_input) {
		if (socket < 0 || socket >= node->inputs->length) {
			return NULL;
		}
		return node->inputs->buffer[socket];
	}
	if (socket < 0 || socket >= node->outputs->length) {
		return NULL;
	}
	return node->outputs->buffer[socket];
}

void script_material_set_float(ui_node_t *node, i32 is_input, i32 socket, f32 value) {
	ui_node_socket_t *soc = script_material_socket(node, is_input != 0, socket);
	if (soc == NULL || soc->default_value == NULL || soc->default_value->length < 1) {
		return;
	}
	soc->default_value->buffer[0] = value;
}

void script_material_set_color(ui_node_t *node, i32 is_input, i32 socket, f32 r, f32 g, f32 b, f32 a) {
	ui_node_socket_t *soc = script_material_socket(node, is_input != 0, socket);
	if (soc == NULL || soc->default_value == NULL || soc->default_value->length < 3) {
		return;
	}
	soc->default_value->buffer[0] = r;
	soc->default_value->buffer[1] = g;
	soc->default_value->buffer[2] = b;
	if (soc->default_value->length >= 4) {
		soc->default_value->buffer[3] = a;
	}
}

void script_material_set_vector(ui_node_t *node, i32 is_input, i32 socket, f32 x, f32 y, f32 z) {
	ui_node_socket_t *soc = script_material_socket(node, is_input != 0, socket);
	if (soc == NULL || soc->default_value == NULL || soc->default_value->length < 3) {
		return;
	}
	soc->default_value->buffer[0] = x;
	soc->default_value->buffer[1] = y;
	soc->default_value->buffer[2] = z;
}

void script_material_set_button(ui_node_t *node, i32 button, f32 value) {
	if (node == NULL || button < 0 || button >= node->buttons->length) {
		return;
	}
	ui_node_button_t *but = node->buttons->buffer[button];
	if (but->default_value == NULL || but->default_value->length < 1) {
		return;
	}
	but->default_value->buffer[0] = value;
}

void script_material_set_text(ui_node_t *node, i32 button, char *text) {
	if (node == NULL || text == NULL || button < 0 || button >= node->buttons->length) {
		return;
	}
	ui_node_button_t *but = node->buttons->buffer[button];
	but->data             = u8_array_create_from_string(text);
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

void script_material_update(void) {
	if (g_context == NULL || g_context->material == NULL) {
		return;
	}
	script_object_material_reset();

	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);

	make_material_parse_paint_material(true);
	util_render_make_material_preview();
	if (context_is_decal()) {
		util_render_make_decal_preview();
	}
	base_update_workflow_nodes();

	script_gpu_end(current, in_use);

	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
	if (ui_header_handle != NULL) {
		ui_header_handle->redraws = 2;
	}
	if (ui_base_hwnds != NULL && ui_base_hwnds->length > 1) {
		ui_base_hwnds->buffer[1]->redraws = 2;
	}
	g_context->ddirty  = 2;
	g_context->rtdirty = 1;
}

string_array_t *script_shape_list(void) {
	project_fetch_default_meshes();
	return project_default_mesh_list;
}

object_t *script_shape_add(char *name) {
	if (name == NULL || string_array_index_of(script_shape_list(), name) < 0) {
		return NULL;
	}
	if (g_project == NULL || g_project->_ == NULL || g_project->_->paint_objects == NULL || g_project->_->paint_objects->length == 0) {
		return NULL;
	}

	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	mesh_object_t *mo = tab_meshes_append_shape(name);
	script_gpu_end(current, in_use);

	if (mo == NULL) {
		return NULL;
	}

	tab_meshes_reset_preview_map();
	g_context->ddirty = 2;
	if (ui_base_hwnds != NULL && ui_base_hwnds->length > TAB_AREA_SIDEBAR0) {
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	}
	return mo->base;
}

object_t *script_object_duplicate(object_t *o) {
	if (o == NULL || !string_equals(o->ext_type, "mesh_object_t")) {
		return NULL;
	}

	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	mesh_object_t *dup = util_mesh_duplicate_object(o->ext);
	script_gpu_end(current, in_use);

	g_context->ddirty                                 = 2;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	return dup->base;
}

object_t *script_object_clone(char *name) {
	return script_object_duplicate(script_get_object(name));
}

void script_object_remove(object_t *o) {
	if (o == NULL || !string_equals(o->ext_type, "mesh_object_t")) {
		return;
	}
	if (g_project->_->paint_objects->length < 2 || array_index_of(g_project->_->paint_objects, o->ext) < 0) {
		return;
	}

	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	tab_meshes_draw_context_menu_delete(o->ext);
	script_gpu_end(current, in_use);

	tab_meshes_reset_preview_map();
	g_context->ddirty = 2;
	if (ui_base_hwnds != NULL && ui_base_hwnds->length > TAB_AREA_SIDEBAR0) {
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	}
}

void script_object_origin_to_geometry(object_t *o) {
	if (o == NULL || !string_equals(o->ext_type, "mesh_object_t") || array_index_of(g_project->_->paint_objects, o->ext) < 0) {
		return;
	}

	gpu_texture_t *current;
	bool           in_use;
	script_gpu_begin(&current, &in_use);
	util_mesh_origin_to_geometry(util_mesh_get_hierarchy(o->ext));
	script_gpu_end(current, in_use);
	g_context->ddirty = 2;
}

void script_object_set_name(object_t *o, char *name) {
	if (o == NULL || name == NULL) {
		return;
	}

	char *new_name = string_copy(name);
	tab_stages_rename_object(o->name, new_name);
	o->name = new_name;

	if (o->ext != NULL && string_equals(o->ext_type, "mesh_object_t")) {
		mesh_object_t *mo = o->ext;
		if (mo->data != NULL && util_mesh_data_owner(mo->data) == array_index_of(g_project->_->paint_objects, mo)) {
			mo->data->name = string_copy(new_name);
		}
	}

	if (ui_base_hwnds != NULL && ui_base_hwnds->length > TAB_AREA_SIDEBAR0) {
		ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	}
}

static physics_body_t *script_physics_body(object_t *o) {
	return o != NULL && o->_ != NULL ? o->_->body : NULL;
}

void script_physics_set_shape(object_t *o, i32 shape) {
	if (o == NULL) {
		return;
	}

	bool dynamic = shape == PHYSICS_SHAPE_BOX || shape == PHYSICS_SHAPE_SPHERE;
	util_physics_set(o, shape, shape < 0 ? 0.0 : (dynamic ? 1.0 : 0.0));
	g_project->mesh_physics_shapes = i32_array_create(0);
}

void script_physics_set_mass(object_t *o, f32 mass) {
	if (o == NULL) {
		return;
	}
	util_physics_set_mass(o, mass);
	g_project->mesh_physics_shapes = i32_array_create(0);
}

void script_physics_apply_impulse(object_t *o, f32 x, f32 y, f32 z) {
	physics_body_t *body = script_physics_body(o);
	if (body != NULL) {
		physics_body_apply_impulse(body->_body, (vec4_t){x, y, z, 0.0});
	}
}

void script_physics_set_velocity(object_t *o, f32 x, f32 y, f32 z) {
	physics_body_t *body = script_physics_body(o);
	if (body != NULL) {
		physics_body_set_velocity(body->_body, x, y, z);
	}
}

void script_physics_sync_transform(object_t *o) {
	physics_body_t *body = script_physics_body(o);
	if (body != NULL) {
		physics_body_sync_transform(body);
	}
}

static bool script_pick_hit(object_t *o, ray_t *ray, f32 margin, f32 *out_t) {
	if (o->ext == NULL || !string_equals(o->ext_type, "mesh_object_t")) {
		return false;
	}
	mesh_data_t *data = ((mesh_object_t *)o->ext)->data;
	if (data == NULL) {
		return false;
	}

	vec4_t min;
	vec4_t max;
	mesh_data_calculate_aabb_min_max(data, &min, &max);

	vec4_t scale = o->transform->scale;
	vec4_t grow  = (vec4_t){scale.x != 0.0 ? margin / math_abs(scale.x) : 0.0, scale.y != 0.0 ? margin / math_abs(scale.y) : 0.0,
                           scale.z != 0.0 ? margin / math_abs(scale.z) : 0.0, 0.0};
	min = (vec4_t){min.x - grow.x, min.y - grow.y, min.z - grow.z, 0.0};
	max = (vec4_t){max.x + grow.x, max.y + grow.y, max.z + grow.z, 0.0};

	mat4_t inv    = mat4_inv(o->transform->world);
	vec4_t origin = vec4_apply_mat4((vec4_t){ray->origin.x, ray->origin.y, ray->origin.z, 1.0}, inv);
	vec4_t dir    = vec4_apply_mat4((vec4_t){ray->dir.x, ray->dir.y, ray->dir.z, 0.0}, inv);

	f32 t0         = -10000.0;
	f32 t1         = 10000.0;
	f32 o_axis[3]  = {origin.x, origin.y, origin.z};
	f32 d_axis[3]  = {dir.x, dir.y, dir.z};
	f32 lo_axis[3] = {min.x, min.y, min.z};
	f32 hi_axis[3] = {max.x, max.y, max.z};

	for (i32 i = 0; i < 3; ++i) {
		if (math_abs(d_axis[i]) < 1e-9) {
			if (o_axis[i] < lo_axis[i] || o_axis[i] > hi_axis[i]) {
				return false;
			}
			continue;
		}

		f32 ta = (lo_axis[i] - o_axis[i]) / d_axis[i];
		f32 tb = (hi_axis[i] - o_axis[i]) / d_axis[i];
		if (ta > tb) {
			f32 tmp = ta;
			ta      = tb;
			tb      = tmp;
		}
		if (ta > t0) {
			t0 = ta;
		}
		if (tb < t1) {
			t1 = tb;
		}
		if (t0 > t1) {
			return false;
		}
	}

	if (t1 < 0.0) {
		return false;
	}
	*out_t = t0 >= 0.0 ? t0 : t1;
	return true;
}

object_t *script_pick_object() {
	if (g_project == NULL || g_project->_ == NULL || g_project->_->paint_objects == NULL) {
		return NULL;
	}

	ray_t *ray = raycast_get_ray(mouse_view_x(), mouse_view_y(), scene_camera);
	if (ray == NULL) {
		return NULL;
	}

	object_t *hit    = NULL;
	f32       best_t = 10000.0;

	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	for (i32 i = 0; i < objects->length; ++i) {
		object_t *o = objects->buffer[i]->base;
		f32       t;
		if (o->visible && script_pick_hit(o, ray, 0.02, &t) && t < best_t) {
			best_t = t;
			hit    = o;
		}
	}
	return hit;
}

extern string_array_t *_path_texture_formats;
extern string_array_t *_path_mesh_formats;
extern string_array_t *_path_text_formats;

static any_map_t      *custom_texture_importers = NULL;
static any_map_t      *custom_mesh_importers    = NULL;
static any_map_t      *custom_text_importers    = NULL;
static string_array_t *custom_text_formats      = NULL;

gpu_texture_t *script_import_custom_texture(char *path) {
	char       *format  = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
	void       *fn      = any_map_get(custom_texture_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_val_t r       = minic_call_fn(fn, args, 1);
	return r.p;
}

raw_mesh_t *script_import_custom_mesh(char *path) {
	char       *format  = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
	void       *fn      = any_map_get(custom_mesh_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_val_t r       = minic_call_fn(fn, args, 1);
	return r.p;
}

void script_import_custom_text(char *path) {
	char       *format  = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
	void       *fn      = any_map_get(custom_text_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_call_fn(fn, args, 1);
}

void script_register_text(char *format, void *fn) {
	any_map_set(import_text_importers, format, script_import_custom_text);

	if (custom_text_formats == NULL) {
		custom_text_formats = string_array_create(0);
	}
	if (string_array_index_of(path_text_formats(), format) < 0) {
		any_array_push((any_array_t *)_path_text_formats, format);
		string_array_push(custom_text_formats, format);
	}

	if (custom_text_importers == NULL) {
		custom_text_importers = any_map_create();
	}
	any_map_set(custom_text_importers, format, fn);
}

void script_unregister_text(char *format) {
	map_delete(import_text_importers, format);
	map_delete(custom_text_importers, format);

	i32 i = custom_text_formats != NULL ? string_array_index_of(custom_text_formats, format) : -1;
	if (i >= 0) {
		array_splice((any_array_t *)custom_text_formats, i, 1);
		array_splice((any_array_t *)_path_text_formats, string_array_index_of(_path_text_formats, format), 1);
	}
}

void script_register_texture(char *format, void *fn) {
	any_map_set(import_texture_importers, format, script_import_custom_texture);
	any_array_push((any_array_t *)_path_texture_formats, format);

	if (custom_texture_importers == NULL) {
		custom_texture_importers = any_map_create();
	}
	any_map_set(custom_texture_importers, format, fn);
}

void script_unregister_texture(char *format) {
	map_delete(import_texture_importers, format);
	array_splice((any_array_t *)_path_texture_formats, string_array_index_of(_path_texture_formats, format), 1);
}

void script_register_mesh(char *format, void *fn) {
	any_map_set(import_mesh_importers, format, script_import_custom_mesh);
	any_array_push((any_array_t *)_path_mesh_formats, format);

	if (custom_mesh_importers == NULL) {
		custom_mesh_importers = any_map_create();
	}
	any_map_set(custom_mesh_importers, format, fn);
}

void script_unregister_mesh(char *format) {
	map_delete(import_mesh_importers, format);
	array_splice((any_array_t *)_path_mesh_formats, string_array_index_of(_path_mesh_formats, format), 1);
}

raw_mesh_t *script_make_raw_mesh(char *name, i16_array_t *posa, i16_array_t *nora, u32_array_t *inda, float scale_pos) {
	raw_mesh_t *mesh = calloc(1, sizeof(raw_mesh_t));
	memset(mesh, 0, sizeof(raw_mesh_t));
	mesh->name         = name;
	mesh->posa         = posa;
	mesh->nora         = nora;
	mesh->inda         = inda;
	mesh->scale_pos    = scale_pos;
	mesh->scale_tex    = 1.0f;
	mesh->vertex_count = posa->length / 4;
	mesh->index_count  = inda->length;
	return mesh;
}

void script_material_category_add(char *category_name, any_array_t *node_list) {
	any_array_push(nodes_material_categories, category_name);
	nodes_material_init();
	any_array_push(nodes_material_list, node_list);
}

void script_brush_category_add(char *category_name, any_array_t *node_list) {
	any_array_push(nodes_brush_categories, category_name);
	nodes_brush_init();
	any_array_push(nodes_brush_list, node_list);
}

void script_material_category_remove(char *category_name) {
	int i = array_index_of(nodes_material_categories, category_name);
	array_splice(nodes_material_list, i, 1);
	array_splice(nodes_material_categories, i, 1);
}

void script_brush_category_remove(char *category_name) {
	int i = array_index_of(nodes_brush_categories, category_name);
	array_splice(nodes_brush_list, i, 1);
	array_splice(nodes_brush_categories, i, 1);
}

void script_material_custom_nodes_set(char *node_type, void *fn) {
	any_map_set(parser_material_custom_nodes, node_type, fn);
}

void script_brush_custom_nodes_set(char *node_type, void *fn) {
	any_map_set(parser_logic_custom_nodes, node_type, fn);
}

void script_material_custom_nodes_remove(char *node_type) {
	map_delete(parser_material_custom_nodes, node_type);
}

void script_brush_custom_nodes_remove(char *node_type) {
	map_delete(parser_logic_custom_nodes, node_type);
}

void *script_material_kong_get() {
	return parser_material_kong;
}

static char *_script_message    = NULL;
static i32   _script_message_id = 0;

static void script_message_draw(void *_) {
	if (_script_message == NULL) {
		return;
	}

	f32 scale = UI_SCALE();
	i32 size  = math_floor(22 * scale);

	draw_begin(NULL, false, 0);
	draw_set_font(g_font, size);

	f32 w = draw_string_width(draw_font, draw_font_size, _script_message);
	f32 h = draw_font_height(draw_font, draw_font_size);
	f32 x = (iron_window_width() - w) / 2.0;
	f32 y = iron_window_height() - 100 * scale;

	draw_set_color(0x99000000);
	draw_filled_rect(x - 12 * scale, y - 6 * scale, w + 24 * scale, h + 12 * scale);
	draw_set_color(0xffffffff);
	draw_string(_script_message, x, y);
	draw_end();
}

static void script_message_hide(void *data) {
	if ((i32)(intptr_t)data != _script_message_id) {
		return;
	}
	sys_remove_update(script_message_draw);
	_script_message = NULL;
}

void script_show_message(char *text, f32 seconds) {
	if (text == NULL) {
		return;
	}
	if (_script_message == NULL) {
		sys_notify_on_update(script_message_draw, NULL);
	}
	_script_message = string_copy(text);
	_script_message_id++;
	tween_timer(seconds, script_message_hide, (void *)(intptr_t)_script_message_id);
}

static f32   _script_fade_opacity = 0.0f;
static char *_script_fade_stage   = NULL;

static void script_fade_draw(void *_) {
	draw_begin(NULL, false, 0);
	draw_set_color((u32)(_script_fade_opacity * 255.0f) << 24);
	draw_filled_rect(0, 0, iron_window_width(), iron_window_height());
	draw_end();
}

static void script_fade_in_done(void *_) {
	sys_remove_update(script_fade_draw);
	_script_fade_stage = NULL;
}

static void script_fade_out_done(void *_) {
	script_set_stage(_script_fade_stage);
	tween_reset();
	tween_to(ALLOC_INIT(tween_anim_t, {.target = &_script_fade_opacity, .to = 0.0f, .duration = 1.0f, .ease = EASE_LINEAR, .done = script_fade_in_done}));
}

void script_fade_to_stage(char *stage) {
	if (_script_fade_stage != NULL) {
		return; // Fade in progress
	}
	_script_fade_stage = string_copy(stage);

	_script_fade_opacity = 0.0f;
	sys_notify_on_update(script_fade_draw, NULL);

	// Fade to black, set the stage, then fade back in
	tween_to(ALLOC_INIT(tween_anim_t, {.target = &_script_fade_opacity, .to = 1.0f, .duration = 1.0f, .ease = EASE_LINEAR, .done = script_fade_out_done}));
}

void script_timeline_resume(void) {
	tab_timeline_resume();
}

void script_timeline_pause(void) {
	tab_timeline_pause();
}

void script_timeline_set_frame(i32 frame) {
	tab_timeline_set_frame(frame);
}

void script_timeline_add_keyframe(char *name, i32 frame) {
	tab_timeline_add_named_keyframe(name, frame);
}

typedef struct particle {
	float frame;
	float x;
	float y;
	float vx;
	float vy;
	float sc;
	float sca;
	int   flag;
} particle_t;

#define NUM_PARTICLES 128
static particle_t particles[NUM_PARTICLES];

void script_draw_particles(gpu_texture_t *texture, float x, float y, float w, float h, int atlas_x, int atlas_frames) {
	float screen_w = iron_window_width();
	float screen_h = iron_window_height();
	float cell_w   = texture->width / atlas_x;

	for (int i = 0; i < NUM_PARTICLES; ++i) {
		particle_t *p = &particles[i];
		if ((p->vx == 0 && p->vy == 0) || (p->x > screen_w || p->y > screen_h)) {
			p->x     = iron_random_get_in(-screen_w / 3.0, screen_w);
			p->y     = iron_random_get_in(-screen_w / 3.0, -cell_w);
			p->vx    = iron_random_get_in(0, 200) / 100.0;
			p->vy    = iron_random_get_in(0, 200) / 100.0;
			p->frame = iron_random_get_in(0, atlas_frames - 1);
			p->sc    = iron_random_get_in(0, 100) / 100.0;
			p->sca   = iron_random_get_in(0, 100) / 100.0;
		}

		p->frame += 0.5 * p->vy;
		if (p->frame >= atlas_frames)
			p->frame = 0;
		int frame_x = (int)p->frame % atlas_x;
		int frame_y = (int)p->frame / atlas_x;
		p->x += p->vx;
		p->y += p->vy;

		int col = ((int)(255 * p->sca) << 24) | (255 << 16) | (255 << 8) | 255;
		draw_set_color(col);
		// draw_sub_image(texture, frame_x * cell_w, frame_y * cell_w, cell_w, cell_w, p->x, p->y);
		draw_scaled_sub_image(texture, frame_x * cell_w, frame_y * cell_w, cell_w, cell_w, p->x, p->y, cell_w * 2, cell_w * 2);
	}
}

void script_reset_runtime(void) {
	script_stop();
	tween_reset();
	sys_remove_update(script_message_draw);
	_script_message = NULL;
	sys_remove_update(script_fade_draw);
	_script_fade_stage   = NULL;
	_script_fade_opacity = 0.0f;
	memset(particles, 0, sizeof(particles));
	_script_tween_transform = NULL;
	script_cloud_reset();
#ifdef IRON_AUDIO
	if (data_cached_sounds != NULL) {
		string_array_t *keys = map_keys(data_cached_sounds);
		for (i32 i = 0; i < keys->length; ++i) {
			audio_stop(any_map_get(data_cached_sounds, keys->buffer[i]));
		}
	}
#endif
}

typedef struct script_screenshot_request {
	char *path;
	i32   frames;
	void (*done)(void);
} script_screenshot_request_t;

static any_array_t    *_script_screenshot_queue  = NULL;
static gpu_texture_t *_script_screenshot_target = NULL;
static i32            _script_screenshot_frames = 0;

static void script_screenshot_next(void);

static void script_screenshot_end_frame(void *_) {
	script_screenshot_request_t *r = _script_screenshot_queue->buffer[0];
	gpu_framebuffer_redirect       = NULL;

	draw_begin(NULL, false, 0);
	draw_image(_script_screenshot_target, 0, 0);
	if (agent_running) {
		agent_draw_overlay();
	}
	draw_end();

	buffer_t *pixels = gpu_get_texture_pixels(_script_screenshot_target);
	for (i32 i = 3; i < pixels->length; i += 4) {
		pixels->buffer[i] = 255;
	}
	iron_write_png(r->path, pixels, _script_screenshot_target->width, _script_screenshot_target->height, 0);
	gpu_delete_texture(_script_screenshot_target);
	_script_screenshot_target = NULL;

	array_shift(_script_screenshot_queue);
	if (r->done != NULL) {
		r->done();
	}
	script_screenshot_next();
}

static void script_screenshot_capture(void *_) {
	iron_delay_idle_sleep();
	script_screenshot_request_t *r = _script_screenshot_queue->buffer[0];
	if (++_script_screenshot_frames < r->frames) {
		g_context->ddirty = 2;
		base_redraw_ui();
		sys_notify_on_next_frame(script_screenshot_capture, NULL);
		return;
	}
	_script_screenshot_target = gpu_create_render_target(iron_window_width(), iron_window_height(), GPU_TEXTURE_FORMAT_RGBA32);
	gpu_framebuffer_redirect  = _script_screenshot_target;
	sys_notify_on_end_frame(script_screenshot_end_frame, NULL);
}

static void script_screenshot_next(void) {
	if (_script_screenshot_queue->length == 0) {
		return;
	}
	_script_screenshot_frames = 0;
	sys_notify_on_next_frame(script_screenshot_capture, NULL);
}

void script_screenshot_queue(char *path, i32 frames, void (*done)(void)) {
	if (_script_screenshot_queue == NULL) {
		_script_screenshot_queue = any_array_create(0);
	}
	script_screenshot_request_t *r = ALLOC_INIT(script_screenshot_request_t, {.path = string_copy(path), .frames = frames, .done = done});
	any_array_push(_script_screenshot_queue, r);
	if (_script_screenshot_queue->length == 1) {
		script_screenshot_next();
	}
}

void script_screenshot(char *path) {
	if (path != NULL) {
		script_screenshot_queue(path, 1, NULL);
	}
}
