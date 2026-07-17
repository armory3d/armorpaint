
#include "../global.h"

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
