
#include "../global.h"

#define TAB_TIMELINE_CAMERA -1

typedef struct {
	i32            frame;
	i32            layer_index;
	slot_layer_t  *layer;
	gpu_texture_t *texpaint;
	gpu_texture_t *texpaint_nor;
	gpu_texture_t *texpaint_pack;
	f32_array_t   *path_points;
	f32_array_t   *path_points_world;
	f32_array_t   *path_points_camera;
	i32_array_t   *path_points_parent;
	bool           tween;
	bool           scripted; // Written by a script
} tab_timeline_keyframe_t;

typedef struct {
	i32            frame;
	i32            mesh_index;
	stage_t       *stage;
	mesh_object_t *mesh;
	mat4_t         transform;
	bool           tween;
	bool           scripted; // Written by a script
} tab_timeline_mesh_keyframe_t;

typedef tab_timeline_keyframe_t      tab_timeline_origin_t;
typedef tab_timeline_mesh_keyframe_t tab_timeline_mesh_origin_t;

i32  tab_timeline_selected_frame = 0;
i32  tab_timeline_selected_row   = 0;
bool tab_timeline_playing        = false;
f64  tab_timeline_play_time      = 0.0;
i32  tab_timeline_scroll         = 0;
bool tab_timeline_scrolling      = false;
f32  tab_timeline_scroll_drag_x  = 0.0f;
i32  tab_timeline_scroll_drag_v  = 0;

static i32          tab_timeline_max_frames = 200;
static i32          tab_timeline_frame_rate = 24;
static any_array_t *tab_timeline_keyframes  = NULL;
static any_array_t *tab_timeline_origins    = NULL;
static i32          tab_timeline_last_frame = 0;

static any_array_t *tab_timeline_mesh_keyframes  = NULL;
static any_array_t *tab_timeline_mesh_origins    = NULL;
static i32          tab_timeline_loop_frames     = 0;
static i32          tab_timeline_last_skin_frame = -1;

static i32 tab_timeline_pending_from           = -1;
static i32 tab_timeline_pending_to             = -1;
static i32 tab_timeline_pending_kf_frame       = -1;
static i32 tab_timeline_pending_kf_layer       = -1;
static i32 tab_timeline_pending_rm_frame       = -1;
static i32 tab_timeline_pending_rm_layer       = -1;
static i32 tab_timeline_pending_mesh_add_frame = -1;
static i32 tab_timeline_pending_mesh_add_index = -1;
static i32 tab_timeline_pending_mesh_rm_frame  = -1;
static i32 tab_timeline_pending_mesh_rm_index  = -1;

static f64 tab_timeline_last_click_time  = 0.0;
static i32 tab_timeline_last_click_frame = -1;
static i32 tab_timeline_last_click_row   = -1;
static i32 tab_timeline_pressed_id       = -1;

static gpu_pipeline_t *tab_timeline_tween_pipe   = NULL;
static i32             tab_timeline_tween_tex0   = 0;
static i32             tab_timeline_tween_tex1   = 0;
static i32             tab_timeline_tween_factor = 0;

static bool tab_timeline_stage_edit_init = false;

static stage_t *tab_timeline_edit_stage     = NULL;
static f64      tab_timeline_nested_frame   = 0.0f;
static bool     tab_timeline_nested_enabled = true;
static stage_t *tab_timeline_root_stage     = NULL;

static bool tab_timeline_mesh_in_edit(i32 mi) {
	return tab_timeline_edit_stage == NULL ||
	       (mi >= 0 && string_array_index_of(tab_timeline_edit_stage->objects, g_project->_->paint_objects->buffer[mi]->base->name) >= 0);
}

static stage_t *tab_timeline_get_root_stage() {
	if (tab_timeline_root_stage == NULL) {
		stage_t *s              = tab_stages_get_stage();
		tab_timeline_root_stage = s != NULL && s->nested_mesh == NULL ? s : NULL;
	}
	return tab_timeline_root_stage;
}

static stage_t *tab_timeline_key_stage(i32 mi) {
	return mi == TAB_TIMELINE_CAMERA ? tab_timeline_get_root_stage() : tab_timeline_edit_stage;
}

static char *tab_timeline_mesh_name(i32 mi) {
	return mi == TAB_TIMELINE_CAMERA ? scene_camera->base->name : g_project->_->paint_objects->buffer[mi]->base->name;
}

static bool tab_timeline_camera_editor_enabled = false;
static bool tab_timeline_camera_player_enabled = true;

static bool *tab_timeline_camera_enabled_handle() {
	return g_config->workspace == WORKSPACE_PLAYER ? &tab_timeline_camera_player_enabled : &tab_timeline_camera_editor_enabled;
}

static bool tab_timeline_camera_enabled() {
	return *tab_timeline_camera_enabled_handle();
}

static bool tab_timeline_clip_matches(stage_t *stage, mesh_object_t *mesh) {
	if (stage->nested_mesh == NULL)
		return false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *source = g_project->_->paint_objects->buffer[i];
		if (string_array_index_of(stage->objects, source->base->name) >= 0)
			return source->data == mesh->data;
	}
	return false;
}

static mat4_t tab_timeline_nested_transform(i32 mi) {
	mat4_t result = mat4_identity();
	if (!tab_timeline_nested_enabled || tab_timeline_edit_stage != NULL || mi < 0 || g_project->stages == NULL) {
		return result;
	}
	mesh_object_t *mesh = g_project->_->paint_objects->buffer[mi];
	for (i32 si = 0; si < g_project->stages->length; ++si) {
		stage_t *stage = g_project->stages->buffer[si];
		if (!tab_timeline_clip_matches(stage, mesh)) {
			continue;
		}
		i32 end = 0;
		for (i32 i = 0; i < tab_timeline_mesh_keyframes->length; ++i) {
			tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[i];
			if (kf->stage == stage && kf->frame > end)
				end = kf->frame;
		}
		f32                           frame = (f32)fmod(tab_timeline_nested_frame, (f64)(end + 1));
		tab_timeline_mesh_keyframe_t *prev  = NULL;
		tab_timeline_mesh_keyframe_t *next  = NULL;
		for (i32 i = 0; i < tab_timeline_mesh_origins->length; ++i) {
			tab_timeline_mesh_origin_t *o = tab_timeline_mesh_origins->buffer[i];
			if (o->stage == stage)
				result = o->transform;
		}
		for (i32 i = 0; i < tab_timeline_mesh_keyframes->length; ++i) {
			tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[i];
			if (kf->stage != stage)
				continue;
			if (kf->frame <= frame && (prev == NULL || kf->frame > prev->frame))
				prev = kf;
			if (kf->frame > frame && (next == NULL || kf->frame < next->frame))
				next = kf;
		}
		if (prev != NULL)
			result = prev->transform;
		if (next != NULL && next->tween) {
			i32 start = prev == NULL ? 0 : prev->frame;
			result    = mat4_tween(result, next->transform, (frame - start) / (next->frame - start));
		}
		break;
	}
	return result;
}

typedef struct {
	mesh_object_t *mesh;
	mat4_t         delta;
} tab_timeline_instance_t;

static any_array_t *tab_timeline_instances = NULL;

static tab_timeline_instance_t *tab_timeline_instance(i32 mi) {
	if (tab_timeline_instances == NULL)
		tab_timeline_instances = any_array_create_from_raw((void *[]){}, 0);
	mesh_object_t *mesh = g_project->_->paint_objects->buffer[mi];
	for (i32 i = 0; i < tab_timeline_instances->length; ++i) {
		tab_timeline_instance_t *instance = tab_timeline_instances->buffer[i];
		if (instance->mesh == mesh)
			return instance;
	}
	tab_timeline_instance_t *instance = ALLOC_INIT(tab_timeline_instance_t, {0});
	instance->mesh                    = mesh;
	instance->delta                   = mat4_identity();
	any_array_push(tab_timeline_instances, instance);
	return instance;
}

static mat4_t tab_timeline_capture_mesh(i32 mi) {
	transform_t *t = mi < 0 ? scene_camera->base->transform : g_project->_->paint_objects->buffer[mi]->base->transform;
	return mi < 0 ? t->local : mat4_mult_mat(mat4_inv(tab_timeline_instance(mi)->delta), t->local);
}

static void tab_timeline_copy_tex(gpu_texture_t *dst, gpu_texture_t *src) {
	draw_begin(dst, true, 0x00000000);
	draw_set_pipeline(pipes_copy);
	draw_scaled_image(src, 0, 0, dst->width, dst->height);
	draw_set_pipeline(NULL);
	draw_end();
}

static void tab_timeline_free_path_array(void *a) {
	if (a != NULL) {
		array_free(a);
		free(a);
	}
}

static void tab_timeline_copy_path_points_from_layer(slot_layer_t *l, f32_array_t **pp, f32_array_t **ppw, f32_array_t **ppc, i32_array_t **ppp) {
	f32_array_t *old_pp  = *pp;
	f32_array_t *old_ppw = *ppw;
	f32_array_t *old_ppc = *ppc;
	i32_array_t *old_ppp = *ppp;
	*pp                  = l->path_points != NULL ? f32_array_create_from_array(l->path_points) : NULL;
	*ppw                 = l->path_points_world != NULL ? f32_array_create_from_array(l->path_points_world) : NULL;
	*ppc                 = l->path_points_camera != NULL ? f32_array_create_from_array(l->path_points_camera) : NULL;
	*ppp                 = l->path_points_parent != NULL ? i32_array_create_from_array(l->path_points_parent) : NULL;
	tab_timeline_free_path_array(old_pp);
	tab_timeline_free_path_array(old_ppw);
	tab_timeline_free_path_array(old_ppc);
	tab_timeline_free_path_array(old_ppp);
}

static void tab_timeline_copy_path_points_to_layer(slot_layer_t *l, f32_array_t *pp, f32_array_t *ppw, f32_array_t *ppc, i32_array_t *ppp) {
	f32_array_t *old_pp   = l->path_points;
	f32_array_t *old_ppw  = l->path_points_world;
	f32_array_t *old_ppc  = l->path_points_camera;
	i32_array_t *old_ppp  = l->path_points_parent;
	l->path_points        = pp != NULL ? f32_array_create_from_array(pp) : NULL;
	l->path_points_world  = ppw != NULL ? f32_array_create_from_array(ppw) : NULL;
	l->path_points_camera = ppc != NULL ? f32_array_create_from_array(ppc) : NULL;
	l->path_points_parent = ppp != NULL ? i32_array_create_from_array(ppp) : NULL;
	tab_timeline_free_path_array(old_pp);
	tab_timeline_free_path_array(old_ppw);
	tab_timeline_free_path_array(old_ppc);
	tab_timeline_free_path_array(old_ppp);
}

static gpu_texture_format_t tab_timeline_tex_format() {
	return base_bits == TEXTURE_BITS_BITS8    ? GPU_TEXTURE_FORMAT_RGBA32
	       : base_bits == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64
	                                          : GPU_TEXTURE_FORMAT_RGBA128;
}

static i32 tab_timeline_find_keyframe(i32 frame, i32 layer_index) {
	for (i32 i = 0; i < tab_timeline_keyframes->length; i++) {
		tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[i];
		if (kf->frame == frame && kf->layer_index == layer_index) {
			return i;
		}
	}
	return -1;
}

static i32 tab_timeline_find_active_keyframe(i32 frame, i32 layer_index) {
	i32 best       = -1;
	i32 best_frame = -1;
	for (i32 i = 0; i < tab_timeline_keyframes->length; i++) {
		tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[i];
		if (kf->layer_index == layer_index && kf->frame <= frame && kf->frame > best_frame) {
			best_frame = kf->frame;
			best       = i;
		}
	}
	return best;
}

static i32 tab_timeline_find_origin(i32 layer_index) {
	for (i32 i = 0; i < tab_timeline_origins->length; i++) {
		if (((tab_timeline_origin_t *)tab_timeline_origins->buffer[i])->layer_index == layer_index) {
			return i;
		}
	}
	return -1;
}

static i32 tab_timeline_find_next_keyframe(i32 frame, i32 layer_index) {
	i32 best       = -1;
	i32 best_frame = tab_timeline_max_frames + 1;
	for (i32 i = 0; i < tab_timeline_keyframes->length; i++) {
		tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[i];
		if (kf->layer_index == layer_index && kf->frame > frame && kf->frame < best_frame) {
			best_frame = kf->frame;
			best       = i;
		}
	}
	return best;
}

static void tab_timeline_init_tween_pipe() {
	if (tab_timeline_tween_pipe != NULL) {
		return;
	}
	tab_timeline_tween_pipe                  = gpu_create_pipeline();
	tab_timeline_tween_pipe->vertex_shader   = sys_get_shader("layer_tween.vert");
	tab_timeline_tween_pipe->fragment_shader = sys_get_shader("layer_tween.frag");
	gpu_vertex_structure_t *vs               = ALLOC_INIT(gpu_vertex_structure_t, {0});
	gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_2X);
	tab_timeline_tween_pipe->input_layout = vs;
	gpu_pipeline_compile(tab_timeline_tween_pipe);
	tab_timeline_tween_tex0   = 0;
	tab_timeline_tween_tex1   = 1;
	pipes_offset              = 0;
	tab_timeline_tween_factor = pipes_get_constant_location("float");
}

static void tab_timeline_tween_tex(gpu_texture_t *dst, gpu_texture_t *from, gpu_texture_t *to, f32 t) {
	tab_timeline_init_tween_pipe();
	_gpu_begin(dst, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(tab_timeline_tween_pipe);
	gpu_set_texture(tab_timeline_tween_tex0, from);
	gpu_set_texture(tab_timeline_tween_tex1, to);
	gpu_set_float(tab_timeline_tween_factor, t);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
}

static void tab_timeline_save_origin(i32 li, bool scripted) {
	slot_layer_t *l = g_project->_->layers->buffer[li];
	if (!slot_layer_is_layer(l)) {
		return;
	}
	i32                    oi = tab_timeline_find_origin(li);
	tab_timeline_origin_t *o;
	if (oi < 0) {
		gpu_texture_format_t fmt = tab_timeline_tex_format();
		i32                  w   = config_get_texture_res_x();
		i32                  h   = config_get_texture_res_y();
		o                        = ALLOC_INIT(tab_timeline_origin_t, {0});
		o->layer_index           = li;
		o->layer                 = l;
		o->texpaint              = gpu_create_render_target(w, h, fmt);
		o->texpaint_nor          = gpu_create_render_target(w, h, fmt);
		o->texpaint_pack         = gpu_create_render_target(w, h, fmt);
		any_array_push(tab_timeline_origins, o);
	}
	else {
		o = tab_timeline_origins->buffer[oi];
	}
	tab_timeline_copy_tex(o->texpaint, l->texpaint);
	tab_timeline_copy_tex(o->texpaint_nor, l->texpaint_nor);
	tab_timeline_copy_tex(o->texpaint_pack, l->texpaint_pack);
	tab_timeline_copy_path_points_from_layer(l, &o->path_points, &o->path_points_world, &o->path_points_camera, &o->path_points_parent);
	o->scripted = scripted;
}

static void tab_timeline_save_origins() {
	for (i32 li = 0; li < g_project->_->layers->length; li++) {
		i32 oi = tab_timeline_find_origin(li);
		if (oi >= 0 && ((tab_timeline_origin_t *)tab_timeline_origins->buffer[oi])->scripted) {
			continue;
		}
		tab_timeline_save_origin(li, false);
	}
}

static void tab_timeline_load_origins() {
	if (tab_timeline_edit_stage != NULL)
		return;
	for (i32 li = 0; li < g_project->_->layers->length; li++) {
		i32 oi = tab_timeline_find_origin(li);
		if (oi < 0) {
			continue;
		}
		slot_layer_t          *l = g_project->_->layers->buffer[li];
		tab_timeline_origin_t *o = tab_timeline_origins->buffer[oi];
		if (slot_layer_is_layer(l)) {
			tab_timeline_copy_tex(l->texpaint, o->texpaint);
			tab_timeline_copy_tex(l->texpaint_nor, o->texpaint_nor);
			tab_timeline_copy_tex(l->texpaint_pack, o->texpaint_pack);
			tab_timeline_copy_path_points_to_layer(l, o->path_points, o->path_points_world, o->path_points_camera, o->path_points_parent);
		}
		o->scripted = false;
	}
	g_context->ddirty               = 2;
	g_context->rtdirty              = 1;
	g_context->layers_preview_dirty = true;
}

static void tab_timeline_save_to_keyframes(i32 frame) {
	for (i32 li = 0; li < g_project->_->layers->length; li++) {
		i32 kfi = tab_timeline_find_keyframe(frame, li);
		if (kfi < 0) {
			continue;
		}
		slot_layer_t            *l  = g_project->_->layers->buffer[li];
		tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[kfi];
		if (slot_layer_is_layer(l)) {
			tab_timeline_copy_tex(kf->texpaint, l->texpaint);
			tab_timeline_copy_tex(kf->texpaint_nor, l->texpaint_nor);
			tab_timeline_copy_tex(kf->texpaint_pack, l->texpaint_pack);
			tab_timeline_copy_path_points_from_layer(l, &kf->path_points, &kf->path_points_world, &kf->path_points_camera, &kf->path_points_parent);
		}
	}
}

static void tab_timeline_load_from_keyframes(i32 frame) {
	if (tab_timeline_edit_stage != NULL)
		return;
	if (tab_timeline_keyframes == NULL || tab_timeline_keyframes->length == 0) {
		return;
	}
	bool any = false;
	for (i32 li = 0; li < g_project->_->layers->length; li++) {
		slot_layer_t *l = g_project->_->layers->buffer[li];
		if (!slot_layer_is_layer(l)) {
			continue;
		}
		i32 kfi = tab_timeline_find_active_keyframe(frame, li);
		if (kfi >= 0) {
			tab_timeline_keyframe_t *kf = tab_timeline_keyframes->buffer[kfi];
			tab_timeline_copy_tex(l->texpaint, kf->texpaint);
			tab_timeline_copy_tex(l->texpaint_nor, kf->texpaint_nor);
			tab_timeline_copy_tex(l->texpaint_pack, kf->texpaint_pack);
			tab_timeline_copy_path_points_to_layer(l, kf->path_points, kf->path_points_world, kf->path_points_camera, kf->path_points_parent);
			any = true;
		}
		else {
			i32 oi = tab_timeline_find_origin(li);
			if (oi >= 0) {
				tab_timeline_origin_t *o = tab_timeline_origins->buffer[oi];
				tab_timeline_copy_tex(l->texpaint, o->texpaint);
				tab_timeline_copy_tex(l->texpaint_nor, o->texpaint_nor);
				tab_timeline_copy_tex(l->texpaint_pack, o->texpaint_pack);
				tab_timeline_copy_path_points_to_layer(l, o->path_points, o->path_points_world, o->path_points_camera, o->path_points_parent);
				any = true;
			}
		}
	}
	if (any) {
		g_context->ddirty               = 2;
		g_context->rtdirty              = 1;
		g_context->layers_preview_dirty = true;
	}
}

static void tab_timeline_tween_from_keyframes(f32 frame_f) {
	if (tab_timeline_edit_stage != NULL)
		return;
	bool any     = false;
	i32  frame_i = (i32)frame_f;
	for (i32 li = 0; li < g_project->_->layers->length; li++) {
		slot_layer_t *l = g_project->_->layers->buffer[li];
		if (!slot_layer_is_layer(l)) {
			continue;
		}
		i32 nxt_kfi = tab_timeline_find_next_keyframe(frame_i, li);
		if (nxt_kfi < 0) {
			continue;
		}
		tab_timeline_keyframe_t *nxt = tab_timeline_keyframes->buffer[nxt_kfi];
		if (!nxt->tween) {
			continue;
		}

		gpu_texture_t *from_texpaint      = NULL;
		gpu_texture_t *from_texpaint_nor  = NULL;
		gpu_texture_t *from_texpaint_pack = NULL;
		i32            from_frame         = 0;
		i32            act_kfi            = tab_timeline_find_active_keyframe(frame_i, li);
		if (act_kfi >= 0) {
			tab_timeline_keyframe_t *act = tab_timeline_keyframes->buffer[act_kfi];
			from_texpaint                = act->texpaint;
			from_texpaint_nor            = act->texpaint_nor;
			from_texpaint_pack           = act->texpaint_pack;
			from_frame                   = act->frame;
		}
		else {
			i32 oi = tab_timeline_find_origin(li);
			if (oi < 0) {
				continue;
			}
			tab_timeline_origin_t *o = tab_timeline_origins->buffer[oi];
			from_texpaint            = o->texpaint;
			from_texpaint_nor        = o->texpaint_nor;
			from_texpaint_pack       = o->texpaint_pack;
			from_frame               = 0;
		}

		f32 t = (frame_f - (f32)from_frame) / (f32)(nxt->frame - from_frame);
		tab_timeline_tween_tex(l->texpaint, from_texpaint, nxt->texpaint, t);
		tab_timeline_tween_tex(l->texpaint_nor, from_texpaint_nor, nxt->texpaint_nor, t);
		tab_timeline_tween_tex(l->texpaint_pack, from_texpaint_pack, nxt->texpaint_pack, t);
		any = true;
	}
	if (any) {
		g_context->ddirty               = 2;
		g_context->rtdirty              = 1;
		g_context->layers_preview_dirty = true;
	}
}

static i32 tab_timeline_find_mesh_keyframe(i32 frame, i32 mesh_index) {
	for (i32 i = 0; i < tab_timeline_mesh_keyframes->length; i++) {
		tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[i];
		if (kf->stage == tab_timeline_key_stage(mesh_index) && kf->frame == frame && kf->mesh_index == mesh_index) {
			return i;
		}
	}
	return -1;
}

static i32 tab_timeline_find_active_mesh_keyframe(i32 frame, i32 mesh_index) {
	i32 best       = -1;
	i32 best_frame = -1;
	for (i32 i = 0; i < tab_timeline_mesh_keyframes->length; i++) {
		tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[i];
		if (kf->stage == tab_timeline_key_stage(mesh_index) && kf->mesh_index == mesh_index && kf->frame <= frame && kf->frame > best_frame) {
			best_frame = kf->frame;
			best       = i;
		}
	}
	return best;
}

static i32 tab_timeline_find_mesh_origin(i32 mesh_index) {
	for (i32 i = 0; i < tab_timeline_mesh_origins->length; i++) {
		if (((tab_timeline_mesh_origin_t *)tab_timeline_mesh_origins->buffer[i])->stage == tab_timeline_key_stage(mesh_index) &&
		    ((tab_timeline_mesh_origin_t *)tab_timeline_mesh_origins->buffer[i])->mesh_index == mesh_index) {
			return i;
		}
	}
	return -1;
}

static i32 tab_timeline_find_next_mesh_keyframe(i32 frame, i32 mesh_index) {
	i32 best       = -1;
	i32 best_frame = tab_timeline_max_frames + 1;
	for (i32 i = 0; i < tab_timeline_mesh_keyframes->length; i++) {
		tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[i];
		if (kf->stage == tab_timeline_key_stage(mesh_index) && kf->mesh_index == mesh_index && kf->frame > frame && kf->frame < best_frame) {
			best_frame = kf->frame;
			best       = i;
		}
	}
	return best;
}

static bool tab_timeline_camera_keyed() {
	if (tab_timeline_find_mesh_origin(TAB_TIMELINE_CAMERA) >= 0) {
		return true;
	}
	for (i32 i = 0; i < tab_timeline_mesh_keyframes->length; i++) {
		tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[i];
		if (kf->mesh_index == TAB_TIMELINE_CAMERA && kf->stage == tab_timeline_key_stage(TAB_TIMELINE_CAMERA)) {
			return true;
		}
	}
	return false;
}

static transform_t *tab_timeline_mesh_transform(i32 mi) {
	return mi == TAB_TIMELINE_CAMERA ? scene_camera->base->transform : g_project->_->paint_objects->buffer[mi]->base->transform;
}

static void tab_timeline_save_mesh_origin_at(i32 mi, mat4_t transform, bool scripted) {
	i32                         oi = tab_timeline_find_mesh_origin(mi);
	tab_timeline_mesh_origin_t *orig;
	if (oi < 0) {
		orig             = ALLOC_INIT(tab_timeline_mesh_origin_t, {0});
		orig->mesh_index = mi;
		orig->stage      = tab_timeline_key_stage(mi);
		orig->mesh       = mi == TAB_TIMELINE_CAMERA ? NULL : g_project->_->paint_objects->buffer[mi];
		any_array_push(tab_timeline_mesh_origins, orig);
	}
	else {
		orig = tab_timeline_mesh_origins->buffer[oi];
	}
	orig->transform = transform;
	orig->scripted  = scripted;
}

static void tab_timeline_save_mesh_origin(i32 mi) {
	tab_timeline_save_mesh_origin_at(mi, tab_timeline_capture_mesh(mi), false);
}

static void tab_timeline_save_mesh_origins() {
	for (i32 mi = TAB_TIMELINE_CAMERA; mi < g_project->_->paint_objects->length; mi++) {
		if (!tab_timeline_mesh_in_edit(mi) || (mi == TAB_TIMELINE_CAMERA && !tab_timeline_camera_enabled()))
			continue;
		i32 oi = tab_timeline_find_mesh_origin(mi);
		if (mi == TAB_TIMELINE_CAMERA && oi < 0)
			continue;
		if (oi >= 0 && ((tab_timeline_mesh_origin_t *)tab_timeline_mesh_origins->buffer[oi])->scripted)
			continue;
		tab_timeline_save_mesh_origin(mi);
	}
}

static void tab_timeline_sync_mesh_body(i32 mi) {
	if (mi == TAB_TIMELINE_CAMERA) {
		return;
	}
	physics_body_t *body = g_project->_->paint_objects->buffer[mi]->base->_->body;
	if (body == NULL) {
		return;
	}
	physics_body_set_velocity(body->_body, 0.0f, 0.0f, 0.0f);
	physics_body_sync_transform(body);
}

static bool tab_timeline_skip_mesh(i32 mi) {
	if (tab_timeline_edit_stage != NULL)
		return !tab_timeline_mesh_in_edit(mi);
	if (mi == TAB_TIMELINE_CAMERA) {
		return !tab_timeline_camera_enabled() || !tab_timeline_camera_keyed();
	}
	physics_body_t *body = g_project->_->paint_objects->buffer[mi]->base->_->body;
	return body != NULL && body->mass > 0.0;
}

static bool tab_timeline_set_mesh_transform(i32 mi, mat4_t mat) {
	if (mi == TAB_TIMELINE_CAMERA) {
		transform_set_matrix(scene_camera->base->transform, mat);
		camera_object_build_mat(scene_camera);
		if (g_context->camera_type == CAMERA_TYPE_ORTHOGRAPHIC) {
			viewport_update_camera_type(g_context->camera_type);
		}
		return false; // Do not rebuild raytrace geometry
	}
	transform_t             *t        = tab_timeline_mesh_transform(mi);
	mat4_t                   before   = t->world_unpack;
	tab_timeline_instance_t *instance = tab_timeline_instance(mi);
	instance->delta                   = tab_timeline_nested_transform(mi);
	mat                               = mat4_mult_mat(instance->delta, mat);
	transform_set_matrix(t, mat);
	return memcmp(&before, &t->world_unpack, sizeof(mat4_t)) != 0;
}

static bool tab_timeline_mesh_refresh_pending = false;

static void tab_timeline_mesh_moved() {
	if (tab_timeline_playing) {
		tab_timeline_mesh_refresh_pending = true;
		render_path_raytrace_ready        = false;
		return;
	}
	tab_timeline_mesh_refresh_pending = false;
	util_mesh_transform_changed();
}

static void tab_timeline_load_mesh_origins() {
	tab_timeline_nested_frame = 0.0f;
	bool moved                = false;
	for (i32 mi = TAB_TIMELINE_CAMERA; mi < g_project->_->paint_objects->length; mi++) {
		if (!tab_timeline_mesh_in_edit(mi))
			continue;
		i32 oi = tab_timeline_find_mesh_origin(mi);
		if (oi < 0 || tab_timeline_skip_mesh(mi)) {
			continue;
		}
		tab_timeline_mesh_origin_t *orig = tab_timeline_mesh_origins->buffer[oi];
		if (tab_timeline_set_mesh_transform(mi, orig->transform)) {
			moved = true;
		}
		tab_timeline_sync_mesh_body(mi);
		orig->scripted = false;
	}
	g_context->ddirty = 2;
	if (moved) {
		tab_timeline_mesh_moved();
	}
}

static void tab_timeline_save_mesh_to_keyframes(i32 frame) {
	for (i32 mi = TAB_TIMELINE_CAMERA; mi < g_project->_->paint_objects->length; mi++) {
		if (!tab_timeline_mesh_in_edit(mi) || (mi == TAB_TIMELINE_CAMERA && !tab_timeline_camera_enabled()))
			continue;
		i32 kfi = tab_timeline_find_mesh_keyframe(frame, mi);
		if (kfi < 0) {
			continue;
		}
		tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[kfi];
		kf->transform                    = tab_timeline_capture_mesh(mi);
	}
}

static void tab_timeline_load_mesh_keyframes(float frame_f, bool camera_only, f64 nested_frame) {
	if (!camera_only)
		tab_timeline_nested_frame = nested_frame;
	bool any     = false;
	bool moved   = false;
	i32  frame_i = (i32)frame_f;
	for (i32 mi = TAB_TIMELINE_CAMERA; mi < (camera_only ? 0 : g_project->_->paint_objects->length); mi++) {
		if (!tab_timeline_mesh_in_edit(mi))
			continue;
		if (tab_timeline_skip_mesh(mi)) {
			continue;
		}
		i32 act_kfi = tab_timeline_find_active_mesh_keyframe(frame_i, mi);
		i32 nxt_kfi = tab_timeline_find_next_mesh_keyframe(frame_i, mi);

		if (nxt_kfi >= 0) {
			tab_timeline_mesh_keyframe_t *nxt = tab_timeline_mesh_keyframes->buffer[nxt_kfi];
			if (nxt->tween) {
				mat4_t from_mat;
				i32    from_frame;
				bool   has_from = false;
				if (act_kfi >= 0) {
					tab_timeline_mesh_keyframe_t *act = tab_timeline_mesh_keyframes->buffer[act_kfi];
					from_mat                          = act->transform;
					from_frame                        = act->frame;
					has_from                          = true;
				}
				else {
					i32 oi = tab_timeline_find_mesh_origin(mi);
					if (oi >= 0) {
						from_mat   = ((tab_timeline_mesh_origin_t *)tab_timeline_mesh_origins->buffer[oi])->transform;
						from_frame = 0;
						has_from   = true;
					}
				}
				if (has_from) {
					float  t      = (frame_f - (float)from_frame) / (float)(nxt->frame - from_frame);
					mat4_t result = mat4_tween(from_mat, nxt->transform, t);
					if (tab_timeline_set_mesh_transform(mi, result)) {
						moved = true;
					}
					tab_timeline_sync_mesh_body(mi);
					any = true;
					continue;
				}
			}
		}

		if (act_kfi >= 0) {
			if (tab_timeline_set_mesh_transform(mi, ((tab_timeline_mesh_keyframe_t *)tab_timeline_mesh_keyframes->buffer[act_kfi])->transform)) {
				moved = true;
			}
			tab_timeline_sync_mesh_body(mi);
			any = true;
		}
		else {
			i32 oi = tab_timeline_find_mesh_origin(mi);
			if (oi >= 0) {
				if (tab_timeline_set_mesh_transform(mi, ((tab_timeline_mesh_origin_t *)tab_timeline_mesh_origins->buffer[oi])->transform)) {
					moved = true;
				}
				if (nxt_kfi >= 0) {
					tab_timeline_sync_mesh_body(mi);
				}
				any = true;
			}
		}
	}
	if (any) {
		g_context->ddirty = 2;
	}
	if (moved) {
		tab_timeline_mesh_moved();
	}
}

static void tab_timeline_load_mesh_from_keyframes(float frame_f) {
	tab_timeline_load_mesh_keyframes(frame_f, false, frame_f);
}

static void tab_timeline_save_current(i32 frame) {
	if (tab_timeline_edit_stage == NULL) {
		frame == 0 ? tab_timeline_save_origins() : tab_timeline_save_to_keyframes(frame);
	}
	frame == 0 ? tab_timeline_save_mesh_origins() : tab_timeline_save_mesh_to_keyframes(frame);
}

static void tab_timeline_frame_change_on_next_frame(void *_) {
	if (tab_timeline_pending_to < 0)
		return;
	i32 from                  = tab_timeline_pending_from;
	i32 to                    = tab_timeline_pending_to;
	tab_timeline_pending_from = -1;
	tab_timeline_pending_to   = -1;

	if (!tab_timeline_playing) {
		tab_timeline_save_current(from);
	}
	to == 0 ? tab_timeline_load_origins() : tab_timeline_load_from_keyframes(to);
	tab_timeline_tween_from_keyframes((f32)to);
	if (!tab_timeline_playing) {
		to == 0 ? tab_timeline_load_mesh_origins() : tab_timeline_load_mesh_from_keyframes((float)to);
	}

	project_reskin_mesh(to);
}

static void tab_timeline_play_on_next_frame(void *_) {
	if (tab_timeline_playing)
		return;
	tab_timeline_frame_change_on_next_frame(NULL);
	tab_timeline_save_current(tab_timeline_selected_frame);
	tab_timeline_playing         = true;
	tab_timeline_play_time       = sys_time() - (f64)tab_timeline_selected_frame / tab_timeline_frame_rate;
	tab_timeline_loop_frames     = 0;
	tab_timeline_last_skin_frame = -1;
}

static void tab_timeline_add_keyframe(i32 fr, i32 li, bool scripted) {
	if (fr < 0 || li < 0 || li >= g_project->_->layers->length) {
		return;
	}
	slot_layer_t *l = g_project->_->layers->buffer[li];
	if (!slot_layer_is_layer(l)) {
		return;
	}
	if (fr == 0) {
		tab_timeline_save_origin(li, scripted);
		return;
	}
	gpu_texture_format_t     fmt = tab_timeline_tex_format();
	i32                      w   = config_get_texture_res_x();
	i32                      h   = config_get_texture_res_y();
	i32                      kfi = tab_timeline_find_keyframe(fr, li);
	tab_timeline_keyframe_t *kf;
	if (kfi < 0) {
		kf                = ALLOC_INIT(tab_timeline_keyframe_t, {0});
		kf->frame         = fr;
		kf->layer_index   = li;
		kf->layer         = l;
		kf->texpaint      = gpu_create_render_target(w, h, fmt);
		kf->texpaint_nor  = gpu_create_render_target(w, h, fmt);
		kf->texpaint_pack = gpu_create_render_target(w, h, fmt);
		any_array_push(tab_timeline_keyframes, kf);
	}
	else {
		kf = tab_timeline_keyframes->buffer[kfi];
	}
	tab_timeline_copy_tex(kf->texpaint, l->texpaint);
	tab_timeline_copy_tex(kf->texpaint_nor, l->texpaint_nor);
	tab_timeline_copy_tex(kf->texpaint_pack, l->texpaint_pack);
	tab_timeline_copy_path_points_from_layer(l, &kf->path_points, &kf->path_points_world, &kf->path_points_camera, &kf->path_points_parent);
}

static void tab_timeline_add_keyframe_on_next_frame(void *_) {
	i32 fr                        = tab_timeline_pending_kf_frame;
	i32 li                        = tab_timeline_pending_kf_layer;
	tab_timeline_pending_kf_frame = -1;
	tab_timeline_pending_kf_layer = -1;
	tab_timeline_add_keyframe(fr, li, false);
}

static void tab_timeline_remove_keyframe_on_next_frame(void *_) {
	i32 fr                        = tab_timeline_pending_rm_frame;
	i32 li                        = tab_timeline_pending_rm_layer;
	tab_timeline_pending_rm_frame = -1;
	tab_timeline_pending_rm_layer = -1;

	i32 kfi = tab_timeline_find_keyframe(fr, li);
	if (kfi < 0) {
		return;
	}
	array_remove(tab_timeline_keyframes, tab_timeline_keyframes->buffer[kfi]);
	if (fr == tab_timeline_last_frame) {
		tab_timeline_load_from_keyframes(fr);
	}
}

static void tab_timeline_add_mesh_keyframe_at(i32 fr, i32 mi, mat4_t transform, bool scripted) {
	if (fr < 0 || mi < TAB_TIMELINE_CAMERA || mi >= g_project->_->paint_objects->length || !tab_timeline_mesh_in_edit(mi)) {
		return;
	}
	if (fr == 0) {
		tab_timeline_save_mesh_origin_at(mi, transform, scripted);
		return;
	}
	i32                           kfi = tab_timeline_find_mesh_keyframe(fr, mi);
	tab_timeline_mesh_keyframe_t *kf;
	if (kfi < 0) {
		kf             = ALLOC_INIT(tab_timeline_mesh_keyframe_t, {0});
		kf->frame      = fr;
		kf->mesh_index = mi;
		kf->stage      = tab_timeline_key_stage(mi);
		kf->tween      = true;
		kf->mesh       = mi == TAB_TIMELINE_CAMERA ? NULL : g_project->_->paint_objects->buffer[mi];
		any_array_push(tab_timeline_mesh_keyframes, kf);
	}
	else {
		kf = tab_timeline_mesh_keyframes->buffer[kfi];
	}
	kf->transform = transform;
}

static void tab_timeline_add_mesh_keyframe(i32 fr, i32 mi) {
	tab_timeline_add_mesh_keyframe_at(fr, mi, tab_timeline_capture_mesh(mi), false);
}

static void tab_timeline_add_mesh_keyframe_on_next_frame(void *_) {
	i32 fr                              = tab_timeline_pending_mesh_add_frame;
	i32 mi                              = tab_timeline_pending_mesh_add_index;
	tab_timeline_pending_mesh_add_frame = -1;
	tab_timeline_pending_mesh_add_index = -1;
	tab_timeline_add_mesh_keyframe(fr, mi);
}

static void tab_timeline_remove_mesh_keyframe_on_next_frame(void *_) {
	i32 fr                             = tab_timeline_pending_mesh_rm_frame;
	i32 mi                             = tab_timeline_pending_mesh_rm_index;
	tab_timeline_pending_mesh_rm_frame = -1;
	tab_timeline_pending_mesh_rm_index = -1;

	if (fr == 0) {
		i32 oi = mi == TAB_TIMELINE_CAMERA ? tab_timeline_find_mesh_origin(mi) : -1;
		if (oi >= 0) {
			array_remove(tab_timeline_mesh_origins, tab_timeline_mesh_origins->buffer[oi]);
		}
		return;
	}
	i32 kfi = tab_timeline_find_mesh_keyframe(fr, mi);
	if (kfi < 0) {
		return;
	}
	array_remove(tab_timeline_mesh_keyframes, tab_timeline_mesh_keyframes->buffer[kfi]);
	if (fr == tab_timeline_last_frame) {
		tab_timeline_load_mesh_from_keyframes((float)fr);
	}
}

static void tab_timeline_clear_on_next_frame(void *_) {
	if (tab_timeline_edit_stage == NULL)
		tab_timeline_keyframes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = tab_timeline_mesh_keyframes->length - 1; i >= 0; --i) {
		tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[i];
		if (tab_timeline_mesh_in_edit(kf->mesh_index) && kf->stage == tab_timeline_key_stage(kf->mesh_index))
			array_splice(tab_timeline_mesh_keyframes, i, 1);
	}
	tab_timeline_selected_frame = 0;
	tab_timeline_load_origins();
	tab_timeline_load_mesh_origins();
	tab_timeline_last_frame = 0;
}

static void tab_timeline_init() {
	if (tab_timeline_keyframes != NULL) {
		return;
	}
	tab_timeline_keyframes      = any_array_create_from_raw((void *[]){}, 0);
	tab_timeline_origins        = any_array_create_from_raw((void *[]){}, 0);
	tab_timeline_mesh_keyframes = any_array_create_from_raw((void *[]){}, 0);
	tab_timeline_mesh_origins   = any_array_create_from_raw((void *[]){}, 0);
}

void tab_timeline_reset() {
	tab_timeline_camera_editor_enabled = false;
	tab_timeline_camera_player_enabled = true;
	tab_timeline_edit_stage            = NULL;
	tab_timeline_root_stage            = NULL;
	tab_timeline_instances             = NULL;
	tab_timeline_nested_frame          = 0.0f;
	tab_timeline_nested_enabled        = true;
	tab_timeline_init();

	tab_timeline_keyframes      = any_array_create_from_raw((void *[]){}, 0);
	tab_timeline_origins        = any_array_create_from_raw((void *[]){}, 0);
	tab_timeline_mesh_keyframes = any_array_create_from_raw((void *[]){}, 0);
	tab_timeline_mesh_origins   = any_array_create_from_raw((void *[]){}, 0);

	tab_timeline_selected_frame  = 0;
	tab_timeline_selected_row    = 0;
	tab_timeline_last_frame      = 0;
	tab_timeline_playing         = false;
	tab_timeline_scroll          = 0;
	tab_timeline_loop_frames     = 0;
	tab_timeline_last_skin_frame = -1;
}

static void tab_timeline_load_camera(i32 frame) {
	if (!tab_timeline_camera_enabled())
		return;
	i32 oi = tab_timeline_find_mesh_origin(TAB_TIMELINE_CAMERA);
	if (oi >= 0) {
		tab_timeline_set_mesh_transform(TAB_TIMELINE_CAMERA, ((tab_timeline_mesh_origin_t *)tab_timeline_mesh_origins->buffer[oi])->transform);
		g_context->ddirty = 2;
	}
	if (frame > 0) {
		tab_timeline_load_mesh_keyframes((f32)frame, true, frame);
	}
}

void tab_timeline_set_stage(stage_t *stage) {
	tab_timeline_init();
	stage_t *next        = stage != NULL && stage->nested_mesh != NULL ? stage : NULL;
	bool     root_change = next == NULL && stage != tab_timeline_root_stage;
	if (next == tab_timeline_edit_stage) {
		if (root_change) {
			if (!tab_timeline_playing && tab_timeline_root_stage != NULL)
				tab_timeline_save_current(tab_timeline_last_frame);
			tab_timeline_root_stage = stage;
			tab_timeline_load_camera(tab_timeline_last_frame);
		}
		return;
	}
	if (!tab_timeline_playing)
		tab_timeline_save_current(tab_timeline_last_frame);
	mat4_t camera               = scene_camera->base->transform->local;
	tab_timeline_playing        = false;
	tab_timeline_edit_stage     = NULL;
	tab_timeline_nested_enabled = false;
	tab_timeline_load_origins();
	tab_timeline_load_mesh_origins();
	tab_timeline_nested_enabled = true;
	tab_timeline_edit_stage     = next;
	tab_timeline_selected_frame = tab_timeline_last_frame = 0;
	tab_timeline_pending_from = tab_timeline_pending_to = -1;
	tab_timeline_scroll                                 = 0;
	if (next != NULL) {
		for (i32 mi = 0; mi < g_project->_->paint_objects->length; ++mi) {
			if (!tab_timeline_mesh_in_edit(mi))
				continue;
			if (tab_timeline_find_mesh_origin(mi) < 0) {
				tab_timeline_mesh_origin_t *o = ALLOC_INIT(tab_timeline_mesh_origin_t, {0});
				o->mesh_index                 = mi;
				o->mesh                       = g_project->_->paint_objects->buffer[mi];
				o->stage                      = next;
				o->transform                  = mat4_identity();
				any_array_push(tab_timeline_mesh_origins, o);
			}
			tab_timeline_selected_row = g_project->_->layers->length + mi;
			context_select_paint_object(g_project->_->paint_objects->buffer[mi]);
		}
	}
	tab_timeline_load_mesh_origins();
	tab_timeline_set_mesh_transform(TAB_TIMELINE_CAMERA, camera);
	if (root_change) {
		tab_timeline_root_stage = stage;
		tab_timeline_load_camera(0);
	}
}

static void tab_timeline_apply_stage_on_next_frame(void *stage) {
	if (g_project->stages == NULL || array_index_of(g_project->stages, stage) < 0)
		return;
	tab_stages_selected = array_index_of(g_project->stages, stage);
	tab_stages_apply(stage);
}

static void tab_timeline_open_mesh_on_next_frame(void *mesh_ptr) {
	mesh_object_t *mesh = mesh_ptr;
	if (array_index_of(g_project->_->paint_objects, mesh) < 0)
		return;
	stage_t *clip = NULL;
	for (i32 i = 0; i < g_project->stages->length; ++i) {
		stage_t *s = g_project->stages->buffer[i];
		if (tab_timeline_clip_matches(s, mesh)) {
			clip = s;
			break;
		}
	}
	if (clip == NULL) {
		clip              = tab_stages_create_stage(string("%s.mesh", mesh->base->name));
		clip->nested_mesh = string_copy(mesh->data->name);
		string_array_push(clip->objects, string_copy(mesh->base->name));
		any_array_push(g_project->stages, clip);
	}
	tab_timeline_apply_stage_on_next_frame(clip);
}

void tab_timeline_edit_mesh(mesh_object_t *mesh) {
	sys_notify_on_next_frame(&tab_timeline_open_mesh_on_next_frame, mesh);
	ui_base_tabs->buffer[TAB_AREA_STATUS]          = 5; // Timeline
	g_config->layout_tabs->buffer[TAB_AREA_STATUS] = 5;
	if (g_config->layout->buffer[LAYOUT_SIZE_STATUS_H] <= ui_statusbar_default_h * g_config->window_scale) {
		ui_base_toggle_browser();
	}
	ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
}

static i32    _tab_timeline_frame = 0;
static mat4_t _tab_timeline_camera;

void tab_timeline_prepare_save() {
	tab_timeline_init();
	_tab_timeline_frame = tab_timeline_last_frame;
	if (!tab_timeline_playing)
		tab_timeline_save_current(_tab_timeline_frame);
	_tab_timeline_camera        = scene_camera->base->transform->local;
	stage_t *edit               = tab_timeline_edit_stage;
	tab_timeline_edit_stage     = NULL;
	tab_timeline_nested_enabled = false;
	tab_timeline_load_origins();
	tab_timeline_load_mesh_origins();
	tab_timeline_nested_enabled = true;
	tab_timeline_edit_stage     = edit;
}

void tab_timeline_finish_save() {
	if (tab_timeline_keyframes == NULL)
		return;
	if (_tab_timeline_frame == 0)
		tab_timeline_load_origins();
	else
		tab_timeline_load_from_keyframes(_tab_timeline_frame);
	tab_timeline_tween_from_keyframes((f32)_tab_timeline_frame);
	tab_timeline_load_mesh_from_keyframes((f32)_tab_timeline_frame);
	tab_timeline_set_mesh_transform(TAB_TIMELINE_CAMERA, _tab_timeline_camera);
}

void tab_timeline_export(project_t *raw) {
	raw->timeline_frame_rate = tab_timeline_frame_rate;
	raw->timeline_max_frames = tab_timeline_max_frames;

	if (tab_timeline_keyframes == NULL) {
		raw->timeline_layers = NULL;
		raw->timeline_meshes = NULL;
		return;
	}

	timeline_layer_keyframe_data_t_array_t *layers = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < tab_timeline_keyframes->length; ++i) {
		tab_timeline_keyframe_t        *kf = tab_timeline_keyframes->buffer[i];
		timeline_layer_keyframe_data_t *d =
		    ALLOC_INIT(timeline_layer_keyframe_data_t, {
		                                                   .frame              = kf->frame,
		                                                   .layer_index        = kf->layer_index,
		                                                   .texpaint           = lz4_encode(gpu_get_texture_pixels(kf->texpaint)),
		                                                   .texpaint_nor       = lz4_encode(gpu_get_texture_pixels(kf->texpaint_nor)),
		                                                   .texpaint_pack      = lz4_encode(gpu_get_texture_pixels(kf->texpaint_pack)),
		                                                   .path_points        = kf->path_points,
		                                                   .path_points_world  = kf->path_points_world,
		                                                   .path_points_camera = kf->path_points_camera,
		                                                   .path_points_parent = kf->path_points_parent,
		                                                   .tween              = kf->tween,
		                                               });
		any_array_push(layers, d);
	}
	raw->timeline_layers = layers;

	timeline_mesh_keyframe_data_t_array_t *meshes = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < tab_timeline_mesh_keyframes->length + tab_timeline_mesh_origins->length; ++i) {
		tab_timeline_mesh_keyframe_t *kf = i < tab_timeline_mesh_keyframes->length ? tab_timeline_mesh_keyframes->buffer[i]
		                                                                           : tab_timeline_mesh_origins->buffer[i - tab_timeline_mesh_keyframes->length];
		if (i >= tab_timeline_mesh_keyframes->length && kf->stage == NULL)
			continue;
		if (kf->stage != NULL && array_index_of(g_project->stages, kf->stage) < 0)
			continue;
		timeline_mesh_keyframe_data_t *d =
		    ALLOC_INIT(timeline_mesh_keyframe_data_t, {
		                                                  .frame       = kf->frame,
		                                                  .mesh_index  = kf->mesh_index,
		                                                  .stage_index = kf->stage == NULL ? 0 : array_index_of(g_project->stages, kf->stage) + 1,
		                                                  .transform   = mat4_to_f32_array(kf->transform),
		                                                  .tween       = kf->tween,
		                                              });
		any_array_push(meshes, d);
	}
	raw->timeline_meshes = meshes;
}

void tab_timeline_export_free(project_t *raw) {
	if (raw->timeline_layers != NULL) {
		for (i32 i = 0; i < raw->timeline_layers->length; ++i) {
			timeline_layer_keyframe_data_t *d = raw->timeline_layers->buffer[i];
			array_free(d->texpaint);
			free(d->texpaint);
			array_free(d->texpaint_nor);
			free(d->texpaint_nor);
			array_free(d->texpaint_pack);
			free(d->texpaint_pack);
			free(d);
		}
		array_free(raw->timeline_layers);
		free(raw->timeline_layers);
		raw->timeline_layers = NULL;
	}
	if (raw->timeline_meshes != NULL) {
		for (i32 i = 0; i < raw->timeline_meshes->length; ++i) {
			timeline_mesh_keyframe_data_t *d = raw->timeline_meshes->buffer[i];
			array_free(d->transform);
			free(d->transform);
			free(d);
		}
		array_free(raw->timeline_meshes);
		free(raw->timeline_meshes);
		raw->timeline_meshes = NULL;
	}
}

static gpu_texture_t *tab_timeline_tex_from_buffer(buffer_t *buf, bool is_bgra) {
	gpu_texture_format_t fmt               = tab_timeline_tex_format();
	i32                  w                 = config_get_texture_res_x();
	i32                  h                 = config_get_texture_res_y();
	i32                  bytes_per_channel = base_bits == TEXTURE_BITS_BITS8 ? 1 : base_bits == TEXTURE_BITS_BITS16 ? 2 : 4;
	buffer_t            *pixels            = lz4_decode(buf, w * h * 4 * bytes_per_channel);
	gpu_texture_t       *tmp               = gpu_create_texture_from_bytes_raw(pixels, w, h, fmt);
	array_free(pixels);
	free(pixels);
	gpu_texture_t *rt = gpu_create_render_target(w, h, fmt);
	draw_begin(rt, false, 0);
	draw_set_pipeline(is_bgra ? pipes_copy_bgra : pipes_copy);
	draw_image(tmp, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
	gpu_delete_texture(tmp);
	return rt;
}

void tab_timeline_import(project_t *raw) {
	tab_timeline_reset();

	if (raw->timeline_max_frames > 0) {
		tab_timeline_frame_rate = raw->timeline_frame_rate;
		tab_timeline_max_frames = raw->timeline_max_frames;
	}

	if (raw->timeline_layers != NULL) {
		for (i32 i = 0; i < raw->timeline_layers->length; ++i) {
			timeline_layer_keyframe_data_t *d  = raw->timeline_layers->buffer[i];
			tab_timeline_keyframe_t        *kf = ALLOC_INIT(tab_timeline_keyframe_t, {0});
			kf->frame                          = d->frame;
			kf->layer_index                    = d->layer_index;
			kf->texpaint                       = tab_timeline_tex_from_buffer(d->texpaint, raw->is_bgra);
			kf->texpaint_nor                   = tab_timeline_tex_from_buffer(d->texpaint_nor, raw->is_bgra);
			kf->texpaint_pack                  = tab_timeline_tex_from_buffer(d->texpaint_pack, raw->is_bgra);
			kf->path_points                    = d->path_points;
			kf->path_points_world              = d->path_points_world;
			kf->path_points_camera             = d->path_points_camera;
			kf->path_points_parent             = d->path_points_parent;
			kf->tween                          = d->tween;
			any_array_push(tab_timeline_keyframes, kf);
		}
	}

	if (raw->timeline_meshes != NULL) {
		for (i32 i = 0; i < raw->timeline_meshes->length; ++i) {
			timeline_mesh_keyframe_data_t *d  = raw->timeline_meshes->buffer[i];
			tab_timeline_mesh_keyframe_t  *kf = ALLOC_INIT(tab_timeline_mesh_keyframe_t, {0});
			kf->stage = raw->stages != NULL && d->stage_index > 0 && d->stage_index <= raw->stages->length ? raw->stages->buffer[d->stage_index - 1] : NULL;
			kf->frame = d->frame;
			kf->mesh_index = d->mesh_index;
			kf->transform  = mat4_from_f32_array(d->transform, 0);
			kf->tween      = d->tween;
			any_array_push(d->frame == 0 && kf->stage != NULL ? tab_timeline_mesh_origins : tab_timeline_mesh_keyframes, kf);
		}
	}
}

static i32 tab_timeline_row_count() {
	// Layers, meshes, camera
	return g_project->_->layers->length + g_project->_->paint_objects->length + 1;
}

static i32 tab_timeline_row_to_mesh(i32 row) {
	i32 mi = row - g_project->_->layers->length;
	return mi == g_project->_->paint_objects->length ? TAB_TIMELINE_CAMERA : mi;
}

static char *tab_timeline_row_name(i32 row) {
	i32 layer_count = g_project->_->layers->length;
	if (row >= layer_count) {
		return tab_timeline_mesh_name(tab_timeline_row_to_mesh(row));
	}
	slot_layer_t *l = g_project->_->layers->buffer[row];
	return l->name;
}

static char *tab_timeline_script_name(i32 row, i32 frame) {
	char *row_name = string_replace_all_tmp(tab_timeline_row_name(row), " ", "");
	if (tab_timeline_edit_stage != NULL)
		return string_tmp("mesh_%s_%s.frame", row_name, i32_to_string(frame));
	return string_tmp("%s_%s.frame", row_name, i32_to_string(frame));
}

static bool tab_timeline_has_script(i32 row, i32 frame) {
	if (g_project->script_names == NULL || g_project->script_names->length == 0) {
		return false;
	}
	return string_array_index_of(g_project->script_names, tab_timeline_script_name(row, frame)) >= 0;
}

void tab_timeline_edit_script(i32 row, i32 frame) {
	tab_scripts_create(tab_timeline_script_name(row, frame));
	ui_base_tabs->buffer[TAB_AREA_SIDEBAR0]           = 2; // Scripts tab
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
	g_config->layout_tabs->buffer[TAB_AREA_SIDEBAR0]  = 2;
}

static void tab_timeline_delete_script(i32 row, i32 frame) {
	if (g_project->script_names == NULL) {
		return;
	}
	i32 i = string_array_index_of(g_project->script_names, tab_timeline_script_name(row, frame));
	if (i >= 0) {
		array_splice((any_array_t *)g_project->script_datas, i, 1);
		array_splice((any_array_t *)g_project->script_names, i, 1);
	}
}

static bool tab_timeline_is_frame_script(char *name, char *prefix) {
	if (!starts_with(name, prefix) || !ends_with(name, ".frame")) {
		return false;
	}
	i32 start = string_length(prefix);
	i32 end   = string_length(name) - string_length(".frame");
	if (end <= start) {
		return false;
	}
	for (i32 i = start; i < end; ++i) { // Only "<name>_<frame>.frame" belongs to this row
		if (name[i] < '0' || name[i] > '9') {
			return false;
		}
	}
	return true;
}

static void tab_timeline_delete_row_scripts(char *row_name) {
	if (g_project->script_names == NULL) {
		return;
	}
	char *prefix = string("%s_", string_replace_all(row_name, " ", ""));

	i32 row_count = tab_timeline_row_count();
	for (i32 ri = 0; ri < row_count; ++ri) {
		if (string_equals(string("%s_", string_replace_all(tab_timeline_row_name(ri), " ", "")), prefix)) {
			return;
		}
	}

	for (i32 i = g_project->script_names->length - 1; i >= 0; --i) {
		if (tab_timeline_is_frame_script(g_project->script_names->buffer[i], prefix)) {
			array_splice((any_array_t *)g_project->script_datas, i, 1);
			array_splice((any_array_t *)g_project->script_names, i, 1);
		}
	}
}

static void tab_timeline_sync_layer_rows(any_array_t *ar) {
	if (ar == NULL) {
		return;
	}
	for (i32 i = ar->length - 1; i >= 0; --i) {
		tab_timeline_keyframe_t *kf = ar->buffer[i];
		if (kf->layer == NULL && kf->layer_index >= 0 && kf->layer_index < g_project->_->layers->length) {
			kf->layer = g_project->_->layers->buffer[kf->layer_index];
		}
		i32 index = kf->layer != NULL ? array_index_of(g_project->_->layers, kf->layer) : -1;
		if (index < 0) {
			array_splice(ar, i, 1);
			continue;
		}
		kf->layer_index = index;
	}
}

static void tab_timeline_sync_mesh_rows(any_array_t *ar) {
	if (ar == NULL) {
		return;
	}
	for (i32 i = ar->length - 1; i >= 0; --i) {
		tab_timeline_mesh_keyframe_t *kf = ar->buffer[i];
		if (kf->mesh_index == TAB_TIMELINE_CAMERA) {
			continue;
		}
		if (kf->mesh == NULL && kf->mesh_index >= 0 && kf->mesh_index < g_project->_->paint_objects->length) {
			kf->mesh = g_project->_->paint_objects->buffer[kf->mesh_index];
		}
		i32 index = kf->mesh != NULL ? array_index_of(g_project->_->paint_objects, kf->mesh) : -1;
		if (index < 0) {
			array_splice(ar, i, 1);
			continue;
		}
		kf->mesh_index = index;
	}
}

void tab_timeline_sync() {
	if (g_project->_->layers == NULL || g_project->_->paint_objects == NULL) {
		return;
	}
	tab_timeline_sync_layer_rows(tab_timeline_keyframes);
	tab_timeline_sync_layer_rows(tab_timeline_origins);
	tab_timeline_sync_mesh_rows(tab_timeline_mesh_keyframes);
	tab_timeline_sync_mesh_rows(tab_timeline_mesh_origins);

	i32 row_count = tab_timeline_row_count();
	if (tab_timeline_selected_row >= row_count) {
		tab_timeline_selected_row = row_count > 0 ? row_count - 1 : 0;
	}
}

void tab_timeline_on_mesh_deleted(char *mesh_name) {
	if (mesh_name != NULL) {
		tab_timeline_delete_row_scripts(mesh_name);
	}
	tab_timeline_sync();
}

static bool tab_timeline_can_delete() {
	i32  layer_count = g_project->_->layers->length;
	bool is_mesh     = tab_timeline_selected_row >= layer_count;
	bool has_kf;
	if (!is_mesh) {
		has_kf = tab_timeline_keyframes != NULL && tab_timeline_selected_frame > 0 &&
		         tab_timeline_find_keyframe(tab_timeline_selected_frame, tab_timeline_selected_row) >= 0;
	}
	else {
		i32 mi = tab_timeline_row_to_mesh(tab_timeline_selected_row);
		has_kf = tab_timeline_mesh_keyframes != NULL && (tab_timeline_selected_frame > 0 ? tab_timeline_find_mesh_keyframe(tab_timeline_selected_frame, mi) >= 0
		                                                                                 : mi == TAB_TIMELINE_CAMERA && tab_timeline_find_mesh_origin(mi) >= 0);
	}
	return has_kf || tab_timeline_has_script(tab_timeline_selected_row, tab_timeline_selected_frame);
}

static void tab_timeline_delete_selected() {
	i32  layer_count = g_project->_->layers->length;
	bool is_mesh     = tab_timeline_selected_row >= layer_count;

	if (!is_mesh) {
		tab_timeline_pending_rm_frame = tab_timeline_selected_frame;
		tab_timeline_pending_rm_layer = tab_timeline_selected_row;
		sys_notify_on_next_frame(&tab_timeline_remove_keyframe_on_next_frame, NULL);
	}
	else {
		tab_timeline_pending_mesh_rm_frame = tab_timeline_selected_frame;
		tab_timeline_pending_mesh_rm_index = tab_timeline_row_to_mesh(tab_timeline_selected_row);
		sys_notify_on_next_frame(&tab_timeline_remove_mesh_keyframe_on_next_frame, NULL);
	}

	tab_timeline_delete_script(tab_timeline_selected_row, tab_timeline_selected_frame);
}

static void tab_timeline_run_frame_scripts(i32 frame) {
	if (g_project->script_names == NULL || g_project->script_names->length == 0) {
		return;
	}
	i32 row_count = tab_timeline_row_count();
	for (i32 ri = 0; ri < row_count; ri++) {
		if (tab_timeline_edit_stage != NULL && (ri < g_project->_->layers->length || !tab_timeline_mesh_in_edit(tab_timeline_row_to_mesh(ri))))
			continue;
		i32 i = string_array_index_of(g_project->script_names, tab_timeline_script_name(ri, frame));
		if (i >= 0) {
			minic_ctx_free(minic_eval(string("void main() {\n%s\n}", g_project->script_datas->buffer[i])));
		}
	}
}

typedef struct {
	char  *name;
	i32    frame;
	i32    mesh_index;
	mat4_t transform;
	bool   has_transform;
} tab_timeline_script_key_t;

static void tab_timeline_redraw_status() {
	if (g_config->workspace != WORKSPACE_PLAYER) {
		ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
	}
}

static void tab_timeline_add_named_keyframe_on_next_frame(void *data) {
	tab_timeline_script_key_t *key = data;
	if (key->has_transform && string_equals(tab_timeline_mesh_name(key->mesh_index), key->name)) {
		tab_timeline_add_mesh_keyframe_at(key->frame, key->mesh_index, key->transform, true);
		tab_timeline_redraw_status();
		return;
	}
	for (i32 row = 0; row < tab_timeline_row_count(); ++row) {
		if (!string_equals(tab_timeline_row_name(row), key->name)) {
			continue;
		}
		if (row < g_project->_->layers->length) {
			tab_timeline_add_keyframe(key->frame, row, true);
		}
		else {
			i32 mi = tab_timeline_row_to_mesh(row);
			tab_timeline_add_mesh_keyframe_at(key->frame, mi, tab_timeline_capture_mesh(mi), true);
		}
		tab_timeline_redraw_status();
		return;
	}
}

void tab_timeline_add_named_keyframe(char *name, i32 frame) {
	tab_timeline_init();
	if (name == NULL || frame < 0 || frame >= tab_timeline_max_frames) {
		return;
	}
	tab_timeline_script_key_t *key = ALLOC_INIT(tab_timeline_script_key_t, {.name = string_copy(name), .frame = frame});
	i32 layer_count = g_project->_->layers->length;
	i32 row         = 0;
	while (row < layer_count && !string_equals(tab_timeline_row_name(row), name)) {
		++row;
	}
	if (row == layer_count) {
		for (; row < tab_timeline_row_count(); ++row) {
			if (!string_equals(tab_timeline_row_name(row), name)) {
				continue;
			}
			i32 mi = tab_timeline_row_to_mesh(row);
			if (tab_timeline_mesh_in_edit(mi)) {
				key->mesh_index    = mi;
				key->transform     = tab_timeline_capture_mesh(mi);
				key->has_transform = true;
			}
			break;
		}
	}
	sys_notify_on_next_frame(&tab_timeline_add_named_keyframe_on_next_frame, key);
}

void tab_timeline_play() {
	tab_timeline_init();
	tab_timeline_playing         = true;
	tab_timeline_play_time       = sys_time();
	tab_timeline_last_frame      = -1; // Ensure frame 0 scripts run
	tab_timeline_loop_frames     = 0;
	tab_timeline_last_skin_frame = -1;
}

void tab_timeline_resume() {
	if (!tab_timeline_playing) {
		sys_notify_on_next_frame(&tab_timeline_play_on_next_frame, NULL);
	}
}

void tab_timeline_pause() {
	tab_timeline_playing = false;
	tab_timeline_set_frame(tab_timeline_selected_frame);
}

void tab_timeline_set_frame(i32 frame) {
	frame                       = (i32)math_min(math_max(frame, 0), tab_timeline_max_frames - 1);
	tab_timeline_selected_frame = frame;
	tab_timeline_play_time      = sys_time() - (f64)frame / tab_timeline_frame_rate;
	if (tab_timeline_playing || frame == tab_timeline_last_frame) {
		return;
	}
	if (tab_timeline_pending_to < 0) {
		tab_timeline_pending_from = tab_timeline_last_frame;
		sys_notify_on_next_frame(&tab_timeline_frame_change_on_next_frame, NULL);
	}
	tab_timeline_pending_to = frame;
	tab_timeline_last_frame = frame;
}

void tab_timeline_update() {
	tab_timeline_init();
	tab_timeline_sync();
	if (!tab_timeline_playing) {
		if (tab_timeline_mesh_refresh_pending) {
			tab_timeline_mesh_refresh_pending = false;
			util_mesh_transform_changed();
		}
		return;
	}
	iron_delay_idle_sleep();

	if (tab_timeline_last_frame < 0) {
		tab_timeline_save_current(0);
	}

	i32 loop_frames = g_config->workspace == WORKSPACE_PLAYER && tab_timeline_loop_frames > 0 ? tab_timeline_loop_frames : tab_timeline_max_frames;

	// Keep nested clips continuous when the main timeline wraps
	f64   playback_frame = (sys_time() - tab_timeline_play_time) * tab_timeline_frame_rate;
	float frame_f        = (float)fmod(playback_frame, loop_frames);
	i32   frame_i        = (i32)frame_f;
	i32   skin_frame     = (i32)playback_frame;

	tab_timeline_selected_frame = frame_i;

	if (tab_timeline_mesh_keyframes != NULL) {
		tab_timeline_load_mesh_keyframes(frame_f, false, playback_frame);
	}

	if (frame_i != tab_timeline_last_frame) {
		tab_timeline_last_frame = frame_i;
		if (g_config->workspace != WORKSPACE_PLAYER) {
			ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 1;
		}
		tab_timeline_load_from_keyframes(frame_i);
		tab_timeline_run_frame_scripts(frame_i);
	}

	if (skin_frame != tab_timeline_last_skin_frame) {
		tab_timeline_last_skin_frame = skin_frame;
		project_reskin_mesh(skin_frame);
		if (tab_timeline_loop_frames <= 0) {
			i32 frames               = project_skin_frames();
			tab_timeline_loop_frames = frames > 0 && frames < tab_timeline_max_frames ? frames : tab_timeline_max_frames;
		}
	}

	if (tab_timeline_keyframes != NULL) {
		tab_timeline_tween_from_keyframes(frame_f);
	}
}

void tab_timeline_draw_frame_context_menu() {
	i32  layer_count = g_project->_->layers->length;
	bool is_mesh     = tab_timeline_selected_row >= layer_count;
	bool has_kf;
	bool has_start = false; // Start camera at frame 0
	i32  mesh_kfi  = -1;
	i32  layer_kfi = -1;
	if (!is_mesh) {
		layer_kfi = tab_timeline_keyframes != NULL && tab_timeline_selected_frame > 0
		                ? tab_timeline_find_keyframe(tab_timeline_selected_frame, tab_timeline_selected_row)
		                : -1;
		has_kf    = layer_kfi >= 0;
	}
	else {
		i32 mi = tab_timeline_row_to_mesh(tab_timeline_selected_row);
		mesh_kfi =
		    tab_timeline_mesh_keyframes != NULL && tab_timeline_selected_frame > 0 ? tab_timeline_find_mesh_keyframe(tab_timeline_selected_frame, mi) : -1;
		has_kf = mesh_kfi >= 0;
		has_start =
		    tab_timeline_mesh_origins != NULL && tab_timeline_selected_frame == 0 && mi == TAB_TIMELINE_CAMERA && tab_timeline_find_mesh_origin(mi) >= 0;
	}

	if (ui_menu_button(tr("Edit Script"), "", ICON_EDIT)) {
		tab_timeline_edit_script(tab_timeline_selected_row, tab_timeline_selected_frame);
	}

	g_ui->enabled = has_kf;
	bool tween    = false;
	if (g_ui->enabled) {
		tween = is_mesh ? ((tab_timeline_mesh_keyframe_t *)tab_timeline_mesh_keyframes->buffer[mesh_kfi])->tween
		                : ((tab_timeline_keyframe_t *)tab_timeline_keyframes->buffer[layer_kfi])->tween;
	}
	ui_check(&tween, tr("Tween"), "");
	if (g_ui->enabled && ui_item_changed()) {
		if (is_mesh) {
			((tab_timeline_mesh_keyframe_t *)tab_timeline_mesh_keyframes->buffer[mesh_kfi])->tween = tween;
		}
		else {
			((tab_timeline_keyframe_t *)tab_timeline_keyframes->buffer[layer_kfi])->tween = tween;
		}
		ui_menu_keep_open = true;
	}

	bool has_script = tab_timeline_has_script(tab_timeline_selected_row, tab_timeline_selected_frame);
	g_ui->enabled   = has_kf || has_start || has_script;
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		tab_timeline_delete_selected();
	}
	g_ui->enabled = true;
}

void tab_timeline_draw_edit() {
	ui_menu_align();
	ui_slider_int(&tab_timeline_frame_rate, tr("Frame Rate"), 1, 60, false, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		ui_menu_keep_open = true;
	}

	ui_menu_align();
	ui_slider_int(&tab_timeline_max_frames, tr("Frame Count"), 1, 200, false, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		ui_menu_keep_open = true;
	}

	if (ui_menu_button(tr("Clear"), "", ICON_ERASE)) {
		sys_notify_on_next_frame(&tab_timeline_clear_on_next_frame, NULL);
	}
}

static char *tab_timeline_stage_name   = "";
static char *tab_timeline_stage_search = "";

static int tab_timeline_sort_objects(const void *pa, const void *pb) {
	mesh_object_t *a = *(mesh_object_t **)pa;
	mesh_object_t *b = *(mesh_object_t **)pb;
	return strcmp(a->base->name, b->base->name);
}

static int tab_timeline_sort_layers(const void *pa, const void *pb) {
	slot_layer_t *a = *(slot_layer_t **)pa;
	slot_layer_t *b = *(slot_layer_t **)pb;
	return strcmp(a->name, b->name);
}

void tab_timeline_stage_edit_box_draw() {
	stage_t *s = tab_stages_get_stage();
	if (s == NULL) {
		return;
	}

	if (tab_timeline_stage_edit_init) {
		tab_timeline_stage_name   = string_copy(s->name);
		tab_timeline_stage_search = "";
		ui_start_text_edit(&tab_timeline_stage_name, UI_ALIGN_LEFT);
		tab_timeline_stage_edit_init = false;
	}

	char *name = ui_text_input(&tab_timeline_stage_name, tr("Name"), UI_ALIGN_LEFT, true, false);

	if (s->nested_mesh != NULL) {
		if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER)) {
			if (string_length(name) > 0)
				s->name = string_copy(name);
			ui_box_hide();
		}
		return;
	}

	ui_text_input(&tab_timeline_stage_search, tr("Search"), UI_ALIGN_LEFT, true, true);
	char *search = to_lower_case(tab_timeline_stage_search);

	ui_text(tr("Meshes"), UI_ALIGN_LEFT, 0x00000000);

	any_array_t *objects = array_slice((any_array_t *)g_project->_->paint_objects, 0, g_project->_->paint_objects->length);
	array_sort(objects, &tab_timeline_sort_objects);
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *o = objects->buffer[i];
		if (string_index_of(to_lower_case(o->base->name), search) < 0) {
			continue;
		}
		i32  idx      = string_array_index_of(s->objects, o->base->name);
		bool in_stage = idx >= 0;
		ui_check(&in_stage, o->base->name, "");
		if (ui_item_changed()) {
			if (in_stage) {
				string_array_push(s->objects, o->base->name);
			}
			else {
				array_splice(s->objects, idx, 1);
			}

			o->base->visible = in_stage;
			tab_stages_set_hidden(s, o->base->name, false);
			util_mesh_visibility_changed();
			util_physics_apply_stage(s);
			ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
		}
	}
	array_delete(objects);

	ui_text(tr("Layers"), UI_ALIGN_LEFT, 0x00000000);

	any_array_t *layers = array_slice((any_array_t *)g_project->_->layers, 0, g_project->_->layers->length);
	array_sort(layers, &tab_timeline_sort_layers);
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *l = layers->buffer[i];
		if (string_index_of(to_lower_case(l->name), search) < 0) {
			continue;
		}
		i32  idx      = string_array_index_of(s->layers, l->name);
		bool in_stage = idx >= 0;
		ui_check(&in_stage, l->name, "");
		if (ui_item_changed()) {
			if (in_stage) {
				string_array_push(s->layers, l->name);
			}
			else {
				array_splice(s->layers, idx, 1);
			}
			ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
		}
	}
	array_delete(layers);
	ui_end_element();

	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		if (string_length(name) > 0) {
			s->name = string_copy(name);
		}
		ui_box_hide();
	}
}

void tab_timeline_draw_stage_menu() {
	if (ui_menu_button(tr("New"), "", ICON_PLUS)) {
		stage_t *s = tab_stages_create_stage(string("%s %s", tr("Stage"), i32_to_string(g_project->stages->length + 1)));
		any_array_push(g_project->stages, s);
		tab_stages_selected = g_project->stages->length - 1;
		sys_notify_on_next_frame(&tab_timeline_apply_stage_on_next_frame, s);
	}

	g_ui->enabled = g_project->stages->length > 1;
	if (ui_menu_button(tr("Remove"), "", ICON_DELETE)) {
		array_splice(g_project->stages, tab_stages_selected, 1);
		if (tab_stages_selected >= g_project->stages->length) {
			tab_stages_selected = g_project->stages->length - 1;
		}
		sys_notify_on_next_frame(&tab_timeline_apply_stage_on_next_frame, g_project->stages->buffer[tab_stages_selected]);
	}
	g_ui->enabled = true;

	if (ui_menu_button(tr("Edit"), "", ICON_EDIT)) {
		tab_timeline_stage_edit_init = true;
		ui_box_show_custom(&tab_timeline_stage_edit_box_draw, 400, 430, NULL, true, tr("Edit Stage"));
	}
}

static bool tab_timeline_input_in_rect(f32 x, f32 y, f32 w, f32 h) {
	if (g_ui->scissor && g_ui->input_y < g_ui->_window_y + g_ui->window_header_h) {
		return false;
	}
	return g_ui->input_x > g_ui->_window_x + x && g_ui->input_x < g_ui->_window_x + x + w && g_ui->input_y > g_ui->_window_y + y &&
	       g_ui->input_y < g_ui->_window_y + y + h;
}

static bool tab_timeline_button(i32 id, bool hover) {
	if (hover && g_ui->input_started) {
		tab_timeline_pressed_id = id;
	}
	return hover && g_ui->input_released && tab_timeline_pressed_id == id;
}

void tab_timeline_draw(i32 *htab) {
	if (ui_tab(htab, tr("Timeline"), false, -1, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		tab_timeline_init();

		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw_tmp((f32[]){-70, -110, -100, -100, -40, -40, -40, -40, -60}, 9);
		ui_row(row);

		// Stage
		string_array_t *stage_names = string_array_create(0);
		for (i32 i = 0; i < g_project->stages->length; ++i) {
			string_array_push(stage_names, g_project->stages->buffer[i]->name);
		}

		if (ui_button(tr("Stage"), UI_ALIGN_CENTER, "")) {
			ui_menu_draw(&tab_timeline_draw_stage_menu, -1, -1);
		}

		ui_combo(&tab_stages_selected, stage_names, tr("Stage"), false, UI_ALIGN_LEFT, true);
		if (ui_item_changed() && tab_stages_selected < g_project->stages->length) {
			sys_notify_on_next_frame(&tab_timeline_apply_stage_on_next_frame, g_project->stages->buffer[tab_stages_selected]);
		}

		if (ui_icon_button(tr("Keyframe"), ICON_PLUS, UI_ALIGN_CENTER)) {
			i32 li = tab_timeline_selected_row;
			if (li < g_project->_->layers->length && slot_layer_is_layer(g_project->_->layers->buffer[li])) {
				tab_timeline_pending_kf_frame = tab_timeline_selected_frame;
				tab_timeline_pending_kf_layer = li;
				sys_notify_on_next_frame(&tab_timeline_add_keyframe_on_next_frame, NULL);
			}
			else if (li >= g_project->_->layers->length && li < tab_timeline_row_count()) {
				tab_timeline_pending_mesh_add_frame = tab_timeline_selected_frame;
				tab_timeline_pending_mesh_add_index = tab_timeline_row_to_mesh(li);
				sys_notify_on_next_frame(&tab_timeline_add_mesh_keyframe_on_next_frame, NULL);
			}
		}
		if (ui_icon_button(tr("Edit"), ICON_EDIT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_timeline_draw_edit, -1, -1);
		}

		if (tab_timeline_playing) {
			if (ui_icon_button(tr("Pause"), ICON_PAUSE, UI_ALIGN_CENTER)) {
				tab_timeline_playing = false;
			}
		}
		else {
			if (ui_icon_button(tr("Play"), ICON_PLAY, UI_ALIGN_CENTER)) {
				sys_notify_on_next_frame(&tab_timeline_play_on_next_frame, NULL);
			}
		}
		if (ui_icon_button(tr("Stop"), ICON_STOP, UI_ALIGN_CENTER)) {
			tab_timeline_playing        = false;
			tab_timeline_selected_frame = 0;
		}
		if (ui_icon_button(tr("Previous"), ICON_CHEVRON_LEFT, UI_ALIGN_CENTER)) {
			if (tab_timeline_selected_frame > 0) {
				tab_timeline_selected_frame--;
				tab_timeline_play_time = sys_time() - (f64)tab_timeline_selected_frame / tab_timeline_frame_rate;
			}
		}
		if (ui_icon_button(tr("Next"), ICON_CHEVRON_RIGHT, UI_ALIGN_CENTER)) {
			if (tab_timeline_selected_frame < tab_timeline_max_frames - 1) {
				tab_timeline_selected_frame++;
				tab_timeline_play_time = sys_time() - (f64)tab_timeline_selected_frame / tab_timeline_frame_rate;
			}
		}
		g_ui->enabled = false;
		ui_text(i32_to_string(tab_timeline_selected_frame), UI_ALIGN_CENTER, 0x00000000);
		g_ui->enabled = true;
		ui_end_sticky();

		f32 layer_name_w    = 120.0f * UI_SCALE(); // Eye, icon, name
		f32 frame_w         = 16.0f * UI_SCALE();
		f32 start_x         = g_ui->_x + layer_name_w;
		f32 start_y         = g_ui->_y;
		i32 font_h          = draw_font_height(g_font, g_ui->font_size);
		i32 strip_h         = (i32)(g_theme->ELEMENT_H * UI_SCALE());
		f32 track_w         = g_ui->_window_w - start_x;
		i32 visible         = (i32)(track_w / frame_w);
		i32 max_scroll      = math_max(tab_timeline_max_frames - visible, 0);
		tab_timeline_scroll = (i32)math_min(math_max(tab_timeline_scroll, 0), max_scroll);

		// Frame number labels every 5 frames
		draw_set_color(g_theme->LABEL_COL);
		i32 label_start = (tab_timeline_scroll / 5) * 5;
		for (i32 i = label_start; i < tab_timeline_scroll + visible + 1 && i < tab_timeline_max_frames; i += 5) {
			f32 lx = start_x + (i - tab_timeline_scroll) * frame_w;
			if (lx < start_x) {
				continue;
			}
			char *label   = i32_to_string(i);
			f32   label_w = draw_string_width(g_font, g_ui->font_size, label);
			draw_string(label, lx + (frame_w - label_w) / 2.0f, start_y);
		}

		u32 base_col   = g_theme->BUTTON_COL;
		u32 bright_col = base_col + 0x00101010;
		u32 sel_col    = g_theme->HIGHLIGHT_COL;
		i32 row_count  = g_project->_->layers->length;

		stage_t *stage = tab_stages_get_stage();

		gpu_texture_t *icons     = resource_get("icons.k");
		f32            icon_size = strip_h - 2;
		f32            eye_size  = 18.0f * UI_SCALE();

		i32 vis_row = 0;
		for (i32 ri = 0; ri < row_count; ri++) {
			slot_layer_t *layer = g_project->_->layers->buffer[ri];
			if (stage != NULL && string_array_index_of(stage->layers, layer->name) < 0) {
				continue;
			}
			f32 row_y  = start_y + font_h + 2 + vis_row * strip_h;
			f32 icon_y = row_y + (strip_h - icon_size) / 2.0f;

			// Eye icon
			rect_t *eye   = resource_tile18(icons, layer->visible ? ICON18_EYE_ON : ICON18_EYE_OFF);
			f32     eye_y = row_y + (strip_h - eye_size) / 2.0f;
			draw_set_color(g_theme->HOVER_COL + 0x00282828);
			draw_scaled_sub_image(icons, eye->x, eye->y, eye->w, eye->h, g_ui->_x, eye_y, eye_size, eye_size);
			bool eye_hover = !tab_timeline_scrolling && tab_timeline_input_in_rect(g_ui->_x, row_y, eye_size, strip_h);
			if (tab_timeline_button(ri * 2, eye_hover)) {
				layer->visible = !layer->visible;
				make_material_parse_mesh_material();
				g_context->ddirty = 2;
			}

			rect_t *rect = resource_tile50(icons, ICON_LAYERS);
			draw_set_color(g_theme->LABEL_COL);
			draw_scaled_sub_image(icons, rect->x, rect->y, rect->w, rect->h, g_ui->_x + eye_size + 4, icon_y, icon_size, icon_size);
			draw_set_color(g_theme->LABEL_COL);
			draw_string(layer->name, g_ui->_x + eye_size + icon_size + 6, row_y + (strip_h - font_h) / 2.0f);

			for (i32 i = tab_timeline_scroll; i < tab_timeline_scroll + visible + 1 && i < tab_timeline_max_frames; i++) {
				f32 x = start_x + (i - tab_timeline_scroll) * frame_w;
				if (x < start_x) {
					continue;
				}
				bool selected = i == tab_timeline_selected_frame && ri == tab_timeline_selected_row;
				u32  col      = selected ? sel_col : (i % 5 == 0) ? bright_col : base_col;

				draw_set_color(col);
				draw_filled_rect(x, row_y, frame_w - 1, strip_h - 1);

				if (tab_timeline_has_script(ri, i)) {
					draw_set_color(sel_col);
					draw_rect(x + 1, row_y + 1, frame_w - 2, strip_h - 2, 1 * UI_SCALE());
				}

				if (i == 0 || (tab_timeline_keyframes != NULL && tab_timeline_find_keyframe(i, ri) >= 0)) {
					draw_set_color(g_theme->LABEL_COL);
					draw_filled_circle(x + frame_w / 2.0f, row_y + strip_h / 2.0f, 3.0f * UI_SCALE(), 12);
				}

				bool in_cell = !tab_timeline_scrolling && tab_timeline_input_in_rect(x, row_y, frame_w, strip_h);
				if (in_cell && g_ui->input_started) {
					f64  now          = sys_time();
					bool double_click = now - tab_timeline_last_click_time < 0.3 && tab_timeline_last_click_frame == i && tab_timeline_last_click_row == ri;
					tab_timeline_last_click_time  = now;
					tab_timeline_last_click_frame = i;
					tab_timeline_last_click_row   = ri;
					if (double_click) {
						tab_timeline_edit_script(ri, i);
					}
				}
				if (in_cell && g_ui->input_down) {
					tab_timeline_selected_frame = i;
					tab_timeline_selected_row   = ri;
					tab_timeline_play_time      = sys_time() - (f64)i / tab_timeline_frame_rate;
				}
				if (in_cell && g_ui->input_released_r) {
					tab_timeline_selected_frame = i;
					tab_timeline_selected_row   = ri;
					ui_menu_draw(&tab_timeline_draw_frame_context_menu, -1, -1);
				}
			}
			vis_row++;
		}

		i32 mesh_count = g_project->_->paint_objects->length;
		for (i32 mi = 0; mi <= mesh_count; mi++) { // Camera row last
			bool is_camera = mi == mesh_count;
			if (is_camera && tab_timeline_edit_stage != NULL)
				continue;
			mesh_object_t *mesh = is_camera ? NULL : g_project->_->paint_objects->buffer[mi];
			object_t      *obj  = is_camera ? scene_camera->base : mesh->base;
			i32            kmi  = is_camera ? TAB_TIMELINE_CAMERA : mi;
			i32            ri   = row_count + mi;
			if (!is_camera && stage != NULL && string_array_index_of(stage->objects, obj->name) < 0) {
				continue;
			}
			f32 row_y  = start_y + font_h + 2 + vis_row * strip_h;
			f32 icon_y = row_y + (strip_h - icon_size) / 2.0f;

			// Eye icon
			{
				bool    visible = is_camera ? tab_timeline_camera_enabled() : obj->visible;
				rect_t *eye     = resource_tile18(icons, visible ? ICON18_EYE_ON : ICON18_EYE_OFF);
				f32     eye_y   = row_y + (strip_h - eye_size) / 2.0f;
				draw_set_color(g_theme->HOVER_COL + 0x00282828);
				draw_scaled_sub_image(icons, eye->x, eye->y, eye->w, eye->h, g_ui->_x, eye_y, eye_size, eye_size);
				bool eye_hover = !tab_timeline_scrolling && tab_timeline_input_in_rect(g_ui->_x, row_y, eye_size, strip_h);
				if (tab_timeline_button(ri * 2, eye_hover)) {
					if (is_camera) {
						*tab_timeline_camera_enabled_handle() = !visible;
						if (!visible) {
							tab_timeline_load_camera(tab_timeline_selected_frame);
						}
					}
					else {
						obj->visible = !visible;
						tab_stages_apply_visible(mesh);
					}
				}
			}

			rect_t *rect = resource_tile50(icons, is_camera ? ICON_CAMERA : ICON_CUBE);
			draw_set_color(g_theme->LABEL_COL);
			draw_scaled_sub_image(icons, rect->x, rect->y, rect->w, rect->h, g_ui->_x + eye_size + 4, icon_y, icon_size, icon_size);
			draw_set_color(g_theme->LABEL_COL);
			draw_string(obj->name, g_ui->_x + eye_size + icon_size + 6, row_y + (strip_h - font_h) / 2.0f);
			f32 name_x = g_ui->_x + eye_size + icon_size + 6;
			if (!is_camera && tab_timeline_edit_stage == NULL && !tab_timeline_scrolling) {
				bool label_hover = tab_timeline_input_in_rect(name_x, row_y, start_x - name_x, strip_h);
				if (tab_timeline_button(ri * 2 + 1, label_hover)) {
					tab_timeline_edit_mesh(mesh);
				}
			}

			for (i32 i = tab_timeline_scroll; i < tab_timeline_scroll + visible + 1 && i < tab_timeline_max_frames; i++) {
				f32 x = start_x + (i - tab_timeline_scroll) * frame_w;
				if (x < start_x) {
					continue;
				}
				bool selected = i == tab_timeline_selected_frame && ri == tab_timeline_selected_row;
				u32  col      = selected ? sel_col : (i % 5 == 0) ? bright_col : base_col;

				draw_set_color(col);
				draw_filled_rect(x, row_y, frame_w - 1, strip_h - 1);

				if (tab_timeline_has_script(ri, i)) {
					draw_set_color(sel_col);
					draw_rect(x, row_y, frame_w - 1, strip_h - 1, 1 * UI_SCALE());
				}

				bool has_kf = i == 0 ? !is_camera || tab_timeline_find_mesh_origin(kmi) >= 0 : tab_timeline_find_mesh_keyframe(i, kmi) >= 0;
				if (has_kf) {
					draw_set_color(g_theme->LABEL_COL);
					draw_filled_circle(x + frame_w / 2.0f, row_y + strip_h / 2.0f, 3.0f * UI_SCALE(), 12);
				}

				bool in_cell = !tab_timeline_scrolling && tab_timeline_input_in_rect(x, row_y, frame_w, strip_h);
				if (in_cell && g_ui->input_started) {
					f64  now          = sys_time();
					bool double_click = now - tab_timeline_last_click_time < 0.3 && tab_timeline_last_click_frame == i && tab_timeline_last_click_row == ri;
					tab_timeline_last_click_time  = now;
					tab_timeline_last_click_frame = i;
					tab_timeline_last_click_row   = ri;
					if (double_click) {
						tab_timeline_edit_script(ri, i);
					}
				}
				if (in_cell && g_ui->input_down) {
					tab_timeline_selected_frame = i;
					tab_timeline_selected_row   = ri;
					tab_timeline_play_time      = sys_time() - (f64)i / tab_timeline_frame_rate;
				}
				if (in_cell && g_ui->input_released_r) {
					tab_timeline_selected_frame = i;
					tab_timeline_selected_row   = ri;
					ui_menu_draw(&tab_timeline_draw_frame_context_menu, -1, -1);
				}
			}
			vis_row++;
		}

		// Scrollbar
		f32 scrollbar_h = 8.0f * UI_SCALE();
		f32 scrollbar_y = start_y + font_h + 2 + vis_row * strip_h;
		f32 handle_w    = track_w * (f32)visible / tab_timeline_max_frames;
		f32 handle_x    = start_x + (max_scroll > 0 ? tab_timeline_scroll * (track_w - handle_w) / max_scroll : 0);

		draw_set_color(base_darker(g_theme->BUTTON_COL, 0x00101010));
		draw_filled_rect(start_x, scrollbar_y, track_w, scrollbar_h);
		draw_set_color(g_theme->BUTTON_COL + 0x00202020);
		draw_filled_rect(handle_x, scrollbar_y, handle_w, scrollbar_h);

		if (g_ui->input_started && tab_timeline_input_in_rect(start_x, scrollbar_y, track_w, scrollbar_h)) {
			tab_timeline_scrolling     = true;
			tab_timeline_scroll_drag_x = g_ui->input_x;
			tab_timeline_scroll_drag_v = tab_timeline_scroll;
		}
		if (g_ui->input_released) {
			tab_timeline_scrolling  = false;
			tab_timeline_pressed_id = -1;
		}
		if (tab_timeline_scrolling && g_ui->input_down && max_scroll > 0) {
			f32 delta           = g_ui->input_x - tab_timeline_scroll_drag_x;
			tab_timeline_scroll = (i32)(tab_timeline_scroll_drag_v + delta * max_scroll / (track_w - handle_w));
			tab_timeline_scroll = (i32)math_min(math_max(tab_timeline_scroll, 0), max_scroll);
		}

		// Delete keyframe / script
		bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
		if (in_focus && g_ui->is_delete_down && tab_timeline_can_delete()) {
			g_ui->is_delete_down = false;
			tab_timeline_delete_selected();
		}

		// Select frame
		if (tab_timeline_selected_frame != tab_timeline_last_frame) {
			tab_timeline_pending_from = tab_timeline_last_frame;
			tab_timeline_pending_to   = tab_timeline_selected_frame;
			tab_timeline_last_frame   = tab_timeline_selected_frame;
			sys_notify_on_next_frame(&tab_timeline_frame_change_on_next_frame, NULL);
		}

		draw_set_color(0xffffffff);
		g_ui->_y = scrollbar_y + scrollbar_h + 2;
	}
}
