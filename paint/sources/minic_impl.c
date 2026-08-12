
#include "global.h"

void script_set_stage(char *name) {
	if (g_project->stages == NULL) {
		return;
	}
	for (i32 i = 0; i < g_project->stages->length; ++i) {
		stage_t *s = g_project->stages->buffer[i];
		if (string_equals(s->name, name)) {
			tab_stages_selected = i;
			tab_stages_apply(s);
			return;
		}
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
		string_array_t   *names    = string_split(u8_array_to_string(enum_but->data), "\n");
		for (i32 j = 0; j < (i32)names->length; ++j) {
			if (!string_equals(names->buffer[j], anim)) {
				continue;
			}

			enum_but->default_value->buffer[0] = (f32)j;
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
	tween_to(GC_ALLOC_INIT(tween_anim_t,
	                       {.target = &t->loc.x, .to = to.x, .duration = duration, .ease = ease, .tick = script_tween_tick, .done = script_tween_done}));
	tween_to(GC_ALLOC_INIT(tween_anim_t, {.target = &t->loc.y, .to = to.y, .duration = duration, .ease = ease}));
	tween_to(GC_ALLOC_INIT(tween_anim_t, {.target = &t->loc.z, .to = to.z, .duration = duration, .ease = ease}));
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
    minic_call_fn(script_next_frame_fn, NULL, 0);
}
void script_notify_on_next_frame(void *fn) {
	sys_notify_on_next_frame(script_on_next_frame, NULL);
	script_next_frame_fn = fn;
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

char *project_filepath_get() {
	return g_project->_->filepath;
}
char *project_basepath_get() {
	return substring(g_project->_->filepath, 0, string_last_index_of(g_project->_->filepath, PATH_SEP));
}
void project_filepath_set(char *s) {
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
	g_context->rdirty              = 2;
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
	g_context->rdirty              = 2;
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

slot_material_t *script_material_create(char *name) {
	if (g_project == NULL || g_project->_ == NULL || g_project->_->materials == NULL || g_project->_->materials->length == 0) {
		return NULL;
	}
	material_data_t *data = g_project->_->materials->buffer[0]->data;
	slot_material_t *m    = slot_material_create(data, NULL);
	if (name != NULL && name[0] != '\0') {
		m->canvas->name = string_copy(name);
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
static material_data_t *script_object_material_data = NULL;

static void script_object_material_reset(void) {
	gc_unroot(script_object_material_data);
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
		gc_root(script_object_material_data);
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
	if (type == NULL || g_context == NULL || g_context->material == NULL) {
		return NULL;
	}
	nodes_material_init();
	ui_node_t *node = nodes_material_create_node(type, NULL);
	if (node != NULL && ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
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
	mesh_object_t *dup = sim_duplicate_object(o->ext);
	script_gpu_end(current, in_use);

	g_context->ddirty                                 = 2;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	return dup->base;
}

static physics_body_t *script_physics_body(object_t *o) {
	return o != NULL && o->_ != NULL ? o->_->body : NULL;
}

void script_physics_set_shape(object_t *o, i32 shape) {
	if (o == NULL) {
		return;
	}

	physics_body_t *body = script_physics_body(o);
	if (body != NULL) {
		physics_body_remove(body);
	}
	g_project->mesh_physics_shapes = i32_array_create(0);
	if (shape < 0) {
		return;
	}

	bool dynamic = shape == PHYSICS_SHAPE_BOX || shape == PHYSICS_SHAPE_SPHERE;
	sim_add_body(o, (physics_shape_t)shape, dynamic ? 1.0 : 0.0);
}

void script_physics_set_mass(object_t *o, f32 mass) {
	physics_body_t *body = script_physics_body(o);
	if (body != NULL) {
		physics_body_set_mass(body, mass);
		g_project->mesh_physics_shapes = i32_array_create(0);
	}
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

extern string_array_t *_path_texture_formats;
extern string_array_t *_path_mesh_formats;
extern string_array_t *_path_text_formats;

static any_map_t      *custom_texture_importers = NULL;
static any_map_t      *custom_mesh_importers    = NULL;
static any_map_t      *custom_text_importers    = NULL;
static string_array_t *custom_text_formats      = NULL;

gpu_texture_t *plugin_import_custom_texture(char *path) {
	char       *format  = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
	void       *fn      = any_map_get(custom_texture_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_val_t r       = minic_call_fn(fn, args, 1);
	return r.p;
}

raw_mesh_t *plugin_import_custom_mesh(char *path) {
	char       *format  = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
	void       *fn      = any_map_get(custom_mesh_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_val_t r       = minic_call_fn(fn, args, 1);
	return r.p;
}

void plugin_import_custom_text(char *path) {
	char       *format  = substring(path, string_last_index_of(path, ".") + 1, string_length(path));
	void       *fn      = any_map_get(custom_text_importers, format);
	minic_val_t args[1] = {minic_val_ptr(path)};
	minic_call_fn(fn, args, 1);
}

void plugin_register_text(char *format, void *fn) {
	any_map_set(import_text_importers, format, plugin_import_custom_text);

	if (custom_text_formats == NULL) {
		custom_text_formats = string_array_create(0);
		gc_root(custom_text_formats);
	}
	if (string_array_index_of(path_text_formats(), format) < 0) {
		any_array_push((any_array_t *)_path_text_formats, format);
		string_array_push(custom_text_formats, format);
	}

	if (custom_text_importers == NULL) {
		custom_text_importers = any_map_create();
		gc_root(custom_text_importers);
	}
	any_map_set(custom_text_importers, format, fn);
}

void plugin_unregister_text(char *format) {
	map_delete(import_text_importers, format);
	map_delete(custom_text_importers, format);

	i32 i = custom_text_formats != NULL ? string_array_index_of(custom_text_formats, format) : -1;
	if (i >= 0) {
		array_splice((any_array_t *)custom_text_formats, i, 1);
		array_splice((any_array_t *)_path_text_formats, string_array_index_of(_path_text_formats, format), 1);
	}
}

void plugin_register_texture(char *format, void *fn) {
	any_map_set(import_texture_importers, format, plugin_import_custom_texture);
	any_array_push((any_array_t *)_path_texture_formats, format);

	if (custom_texture_importers == NULL) {
		custom_texture_importers = any_map_create();
		gc_root(custom_texture_importers);
	}
	any_map_set(custom_texture_importers, format, fn);
}

void plugin_unregister_texture(char *format) {
	map_delete(import_texture_importers, format);
	array_splice((any_array_t *)_path_texture_formats, string_array_index_of(_path_texture_formats, format), 1);
}

void plugin_register_mesh(char *format, void *fn) {
	any_map_set(import_mesh_importers, format, plugin_import_custom_mesh);
	any_array_push((any_array_t *)_path_mesh_formats, format);

	if (custom_mesh_importers == NULL) {
		custom_mesh_importers = any_map_create();
		gc_root(custom_mesh_importers);
	}
	any_map_set(custom_mesh_importers, format, fn);
}

void plugin_unregister_mesh(char *format) {
	map_delete(import_mesh_importers, format);
	array_splice((any_array_t *)_path_mesh_formats, string_array_index_of(_path_mesh_formats, format), 1);
}

raw_mesh_t *plugin_make_raw_mesh(char *name, i16_array_t *posa, i16_array_t *nora, u32_array_t *inda, float scale_pos) {
	raw_mesh_t *mesh = gc_alloc(sizeof(raw_mesh_t));
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

void plugin_material_category_add(char *category_name, any_array_t *node_list) {
	any_array_push(nodes_material_categories, category_name);
	nodes_material_init();
	any_array_push(nodes_material_list, node_list);
}

void plugin_brush_category_add(char *category_name, any_array_t *node_list) {
	any_array_push(nodes_brush_categories, category_name);
	nodes_brush_init();
	any_array_push(nodes_brush_list, node_list);
}

void plugin_material_category_remove(char *category_name) {
	int i = array_index_of(nodes_material_categories, category_name);
	array_splice(nodes_material_list, i, 1);
	array_splice(nodes_material_categories, i, 1);
}

void plugin_brush_category_remove(char *category_name) {
	int i = array_index_of(nodes_brush_categories, category_name);
	array_splice(nodes_brush_list, i, 1);
	array_splice(nodes_brush_categories, i, 1);
}

void plugin_material_custom_nodes_set(char *node_type, void *fn) {
	any_map_set(parser_material_custom_nodes, node_type, fn);
}

void plugin_brush_custom_nodes_set(char *node_type, void *fn) {
	any_map_set(parser_logic_custom_nodes, node_type, fn);
}

void plugin_material_custom_nodes_remove(char *node_type) {
	map_delete(parser_material_custom_nodes, node_type);
}

void plugin_brush_custom_nodes_remove(char *node_type) {
	map_delete(parser_logic_custom_nodes, node_type);
}

void *plugin_material_kong_get() {
	return parser_material_kong;
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
	gc_unroot(_script_fade_stage);
	_script_fade_stage = NULL;
}

static void script_fade_out_done(void *_) {
	script_set_stage(_script_fade_stage);
	tween_reset();
	tween_to(GC_ALLOC_INIT(tween_anim_t, {.target = &_script_fade_opacity, .to = 0.0f, .duration = 1.0f, .ease = EASE_LINEAR, .done = script_fade_in_done}));
}

void script_fade_to_stage(char *stage) {
	if (_script_fade_stage != NULL) {
		return; // Fade in progress
	}
	_script_fade_stage = string_copy(stage);
	gc_root(_script_fade_stage);

	_script_fade_opacity = 0.0f;
	sys_notify_on_update(script_fade_draw, NULL);

	// Fade to black, set the stage, then fade back in
	tween_to(GC_ALLOC_INIT(tween_anim_t, {.target = &_script_fade_opacity, .to = 1.0f, .duration = 1.0f, .ease = EASE_LINEAR, .done = script_fade_out_done}));
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
