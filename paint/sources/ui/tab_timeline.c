
#include "../global.h"

typedef struct {
	i32            frame;
	i32            layer_index;
	gpu_texture_t *texpaint;
	gpu_texture_t *texpaint_nor;
	gpu_texture_t *texpaint_pack;
	f32_array_t   *path_points;
	f32_array_t   *path_points_world;
	f32_array_t   *path_points_camera;
	i32_array_t   *path_points_parent;
	bool           tween;
} tab_timeline_keyframe_t;

typedef struct {
	i32    frame;
	i32    mesh_index;
	mat4_t transform;
	bool   tween;
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

static any_array_t *tab_timeline_mesh_keyframes = NULL;
static any_array_t *tab_timeline_mesh_origins   = NULL;

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

static gpu_pipeline_t *tab_timeline_tween_pipe   = NULL;
static i32             tab_timeline_tween_tex0   = 0;
static i32             tab_timeline_tween_tex1   = 0;
static i32             tab_timeline_tween_factor = 0;

static bool tab_timeline_stage_rename_init = false;

static void tab_timeline_copy_tex(gpu_texture_t *dst, gpu_texture_t *src) {
	draw_begin(dst, true, 0x00000000);
	draw_set_pipeline(pipes_copy);
	draw_scaled_image(src, 0, 0, dst->width, dst->height);
	draw_set_pipeline(NULL);
	draw_end();
}

static void tab_timeline_copy_path_points_from_layer(slot_layer_t *l, f32_array_t **pp, f32_array_t **ppw, f32_array_t **ppc, i32_array_t **ppp) {
	*pp  = l->path_points != NULL ? f32_array_create_from_array(l->path_points) : NULL;
	*ppw = l->path_points_world != NULL ? f32_array_create_from_array(l->path_points_world) : NULL;
	*ppc = l->path_points_camera != NULL ? f32_array_create_from_array(l->path_points_camera) : NULL;
	*ppp = l->path_points_parent != NULL ? i32_array_create_from_array(l->path_points_parent) : NULL;
}

static void tab_timeline_copy_path_points_to_layer(slot_layer_t *l, f32_array_t *pp, f32_array_t *ppw, f32_array_t *ppc, i32_array_t *ppp) {
	l->path_points        = pp != NULL ? f32_array_create_from_array(pp) : NULL;
	l->path_points_world  = ppw != NULL ? f32_array_create_from_array(ppw) : NULL;
	l->path_points_camera = ppc != NULL ? f32_array_create_from_array(ppc) : NULL;
	l->path_points_parent = ppp != NULL ? i32_array_create_from_array(ppp) : NULL;
}

static gpu_texture_format_t tab_timeline_tex_format() {
	return base_bits_handle->i == TEXTURE_BITS_BITS8    ? GPU_TEXTURE_FORMAT_RGBA32
	       : base_bits_handle->i == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64
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
	tab_timeline_tween_pipe = gpu_create_pipeline();
	gc_root(tab_timeline_tween_pipe);
	tab_timeline_tween_pipe->vertex_shader   = sys_get_shader("layer_tween.vert");
	tab_timeline_tween_pipe->fragment_shader = sys_get_shader("layer_tween.frag");
	gpu_vertex_structure_t *vs               = GC_ALLOC_INIT(gpu_vertex_structure_t, {0});
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

static void tab_timeline_save_origins() {
	gpu_texture_format_t fmt = tab_timeline_tex_format();
	i32                  w   = config_get_texture_res_x();
	i32                  h   = config_get_texture_res_y();
	for (i32 li = 0; li < g_project->_->layers->length; li++) {
		slot_layer_t *l = g_project->_->layers->buffer[li];
		if (!slot_layer_is_layer(l)) {
			continue;
		}
		i32                    oi = tab_timeline_find_origin(li);
		tab_timeline_origin_t *o;
		if (oi < 0) {
			o                = GC_ALLOC_INIT(tab_timeline_origin_t, {0});
			o->layer_index   = li;
			o->texpaint      = gpu_create_render_target(w, h, fmt);
			o->texpaint_nor  = gpu_create_render_target(w, h, fmt);
			o->texpaint_pack = gpu_create_render_target(w, h, fmt);
			any_array_push(tab_timeline_origins, o);
		}
		else {
			o = tab_timeline_origins->buffer[oi];
		}
		tab_timeline_copy_tex(o->texpaint, l->texpaint);
		tab_timeline_copy_tex(o->texpaint_nor, l->texpaint_nor);
		tab_timeline_copy_tex(o->texpaint_pack, l->texpaint_pack);
		tab_timeline_copy_path_points_from_layer(l, &o->path_points, &o->path_points_world, &o->path_points_camera, &o->path_points_parent);
	}
}

static void tab_timeline_load_origins() {
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
	}
	g_context->ddirty               = 2;
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
		g_context->layers_preview_dirty = true;
	}
}

static void tab_timeline_tween_from_keyframes(f32 frame_f) {
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
		g_context->layers_preview_dirty = true;
	}
}

static i32 tab_timeline_find_mesh_keyframe(i32 frame, i32 mesh_index) {
	for (i32 i = 0; i < tab_timeline_mesh_keyframes->length; i++) {
		tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[i];
		if (kf->frame == frame && kf->mesh_index == mesh_index) {
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
		if (kf->mesh_index == mesh_index && kf->frame <= frame && kf->frame > best_frame) {
			best_frame = kf->frame;
			best       = i;
		}
	}
	return best;
}

static i32 tab_timeline_find_mesh_origin(i32 mesh_index) {
	for (i32 i = 0; i < tab_timeline_mesh_origins->length; i++) {
		if (((tab_timeline_mesh_origin_t *)tab_timeline_mesh_origins->buffer[i])->mesh_index == mesh_index) {
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
		if (kf->mesh_index == mesh_index && kf->frame > frame && kf->frame < best_frame) {
			best_frame = kf->frame;
			best       = i;
		}
	}
	return best;
}

static void tab_timeline_save_mesh_origins() {
	for (i32 mi = 0; mi < g_project->_->paint_objects->length; mi++) {
		mesh_object_t              *o  = g_project->_->paint_objects->buffer[mi];
		i32                         oi = tab_timeline_find_mesh_origin(mi);
		tab_timeline_mesh_origin_t *orig;
		if (oi < 0) {
			orig             = GC_ALLOC_INIT(tab_timeline_mesh_origin_t, {0});
			orig->mesh_index = mi;
			any_array_push(tab_timeline_mesh_origins, orig);
		}
		else {
			orig = tab_timeline_mesh_origins->buffer[oi];
		}
		orig->transform = o->base->transform->local;
	}
}

static void tab_timeline_sync_mesh_body(mesh_object_t *o) {
	asim_body_t *body = o->base->_->body;
	if (body == NULL) {
		return;
	}
	asim_body_set_velocity(body->_body, 0.0f, 0.0f, 0.0f);
	asim_body_sync_transform(body);
}

static void tab_timeline_load_mesh_origins() {
	for (i32 mi = 0; mi < g_project->_->paint_objects->length; mi++) {
		i32 oi = tab_timeline_find_mesh_origin(mi);
		if (oi < 0) {
			continue;
		}
		tab_timeline_mesh_origin_t *orig = tab_timeline_mesh_origins->buffer[oi];
		mesh_object_t              *o    = g_project->_->paint_objects->buffer[mi];
		transform_set_matrix(o->base->transform, orig->transform);
		tab_timeline_sync_mesh_body(o);
	}
	g_context->ddirty = 2;
}

static void tab_timeline_save_mesh_to_keyframes(i32 frame) {
	for (i32 mi = 0; mi < g_project->_->paint_objects->length; mi++) {
		i32 kfi = tab_timeline_find_mesh_keyframe(frame, mi);
		if (kfi < 0) {
			continue;
		}
		tab_timeline_mesh_keyframe_t *kf = tab_timeline_mesh_keyframes->buffer[kfi];
		mesh_object_t                *o  = g_project->_->paint_objects->buffer[mi];
		kf->transform                    = o->base->transform->local;
	}
}

static void tab_timeline_load_mesh_from_keyframes(float frame_f) {
	bool any     = false;
	i32  frame_i = (i32)frame_f;
	for (i32 mi = 0; mi < g_project->_->paint_objects->length; mi++) {
		mesh_object_t *o       = g_project->_->paint_objects->buffer[mi];
		i32            act_kfi = tab_timeline_find_active_mesh_keyframe(frame_i, mi);
		i32            nxt_kfi = tab_timeline_find_next_mesh_keyframe(frame_i, mi);

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
					transform_set_matrix(o->base->transform, result);
					tab_timeline_sync_mesh_body(o);
					any = true;
					continue;
				}
			}
		}

		if (act_kfi >= 0) {
			transform_set_matrix(o->base->transform, ((tab_timeline_mesh_keyframe_t *)tab_timeline_mesh_keyframes->buffer[act_kfi])->transform);
			tab_timeline_sync_mesh_body(o);
			any = true;
		}
		else {
			i32 oi = tab_timeline_find_mesh_origin(mi);
			if (oi >= 0) {
				transform_set_matrix(o->base->transform, ((tab_timeline_mesh_origin_t *)tab_timeline_mesh_origins->buffer[oi])->transform);
				if (nxt_kfi >= 0) {
					tab_timeline_sync_mesh_body(o);
				}
				any = true;
			}
		}
	}
	if (any) {
		g_context->ddirty = 2;
	}
}

static void tab_timeline_save_current(i32 frame) {
	frame == 0 ? tab_timeline_save_origins() : tab_timeline_save_to_keyframes(frame);
	frame == 0 ? tab_timeline_save_mesh_origins() : tab_timeline_save_mesh_to_keyframes(frame);
}

static void tab_timeline_frame_change_on_next_frame(void *_) {
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
	tab_timeline_save_current(tab_timeline_selected_frame);
	tab_timeline_playing   = true;
	tab_timeline_play_time = sys_time() - (f64)tab_timeline_selected_frame / tab_timeline_frame_rate;
}

static void tab_timeline_add_keyframe_on_next_frame(void *_) {
	i32 fr                        = tab_timeline_pending_kf_frame;
	i32 li                        = tab_timeline_pending_kf_layer;
	tab_timeline_pending_kf_frame = -1;
	tab_timeline_pending_kf_layer = -1;

	if (fr <= 0 || li < 0 || li >= g_project->_->layers->length) {
		return;
	}
	slot_layer_t *l = g_project->_->layers->buffer[li];
	if (!slot_layer_is_layer(l)) {
		return;
	}
	gpu_texture_format_t     fmt = tab_timeline_tex_format();
	i32                      w   = config_get_texture_res_x();
	i32                      h   = config_get_texture_res_y();
	i32                      kfi = tab_timeline_find_keyframe(fr, li);
	tab_timeline_keyframe_t *kf;
	if (kfi < 0) {
		kf                = GC_ALLOC_INIT(tab_timeline_keyframe_t, {0});
		kf->frame         = fr;
		kf->layer_index   = li;
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

static void tab_timeline_add_mesh_keyframe_on_next_frame(void *_) {
	i32 fr                              = tab_timeline_pending_mesh_add_frame;
	i32 mi                              = tab_timeline_pending_mesh_add_index;
	tab_timeline_pending_mesh_add_frame = -1;
	tab_timeline_pending_mesh_add_index = -1;

	if (fr <= 0 || mi < 0 || mi >= g_project->_->paint_objects->length) {
		return;
	}
	mesh_object_t                *o   = g_project->_->paint_objects->buffer[mi];
	i32                           kfi = tab_timeline_find_mesh_keyframe(fr, mi);
	tab_timeline_mesh_keyframe_t *kf;
	if (kfi < 0) {
		kf             = GC_ALLOC_INIT(tab_timeline_mesh_keyframe_t, {0});
		kf->frame      = fr;
		kf->mesh_index = mi;
		any_array_push(tab_timeline_mesh_keyframes, kf);
	}
	else {
		kf = tab_timeline_mesh_keyframes->buffer[kfi];
	}
	kf->transform = o->base->transform->local;
}

static void tab_timeline_remove_mesh_keyframe_on_next_frame(void *_) {
	i32 fr                             = tab_timeline_pending_mesh_rm_frame;
	i32 mi                             = tab_timeline_pending_mesh_rm_index;
	tab_timeline_pending_mesh_rm_frame = -1;
	tab_timeline_pending_mesh_rm_index = -1;

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
	gc_unroot(tab_timeline_keyframes);
	tab_timeline_keyframes = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_keyframes);
	gc_unroot(tab_timeline_mesh_keyframes);
	tab_timeline_mesh_keyframes = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_mesh_keyframes);
	tab_timeline_load_origins();
	tab_timeline_load_mesh_origins();
	tab_timeline_last_frame = 0;
}

static void tab_timeline_init() {
	if (tab_timeline_keyframes != NULL) {
		return;
	}
	tab_timeline_keyframes = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_keyframes);
	tab_timeline_origins = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_origins);
	tab_timeline_mesh_keyframes = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_mesh_keyframes);
	tab_timeline_mesh_origins = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_mesh_origins);
}

void tab_timeline_reset() {
	tab_timeline_init();

	gc_unroot(tab_timeline_keyframes);
	tab_timeline_keyframes = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_keyframes);
	gc_unroot(tab_timeline_origins);
	tab_timeline_origins = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_origins);
	gc_unroot(tab_timeline_mesh_keyframes);
	tab_timeline_mesh_keyframes = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_mesh_keyframes);
	gc_unroot(tab_timeline_mesh_origins);
	tab_timeline_mesh_origins = any_array_create_from_raw((void *[]){}, 0);
	gc_root(tab_timeline_mesh_origins);

	tab_timeline_selected_frame = 0;
	tab_timeline_selected_row   = 0;
	tab_timeline_last_frame     = 0;
	tab_timeline_playing        = false;
	tab_timeline_scroll         = 0;
}

static i32 _tab_timeline_frame = 0;

void tab_timeline_prepare_save() {
	_tab_timeline_frame = tab_timeline_last_frame;
	if (tab_timeline_keyframes == NULL || tab_timeline_last_frame == 0) {
		return;
	}
	tab_timeline_save_to_keyframes(tab_timeline_last_frame);
	tab_timeline_save_mesh_to_keyframes(tab_timeline_last_frame);
	tab_timeline_load_origins();
	tab_timeline_load_mesh_origins();
}

void tab_timeline_finish_save() {
	if (tab_timeline_keyframes == NULL || _tab_timeline_frame == 0) {
		return;
	}
	tab_timeline_load_from_keyframes(_tab_timeline_frame);
	tab_timeline_load_mesh_from_keyframes((float)_tab_timeline_frame);
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
		    GC_ALLOC_INIT(timeline_layer_keyframe_data_t, {
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
	for (i32 i = 0; i < tab_timeline_mesh_keyframes->length; ++i) {
		tab_timeline_mesh_keyframe_t  *kf = tab_timeline_mesh_keyframes->buffer[i];
		timeline_mesh_keyframe_data_t *d  = GC_ALLOC_INIT(timeline_mesh_keyframe_data_t, {
		                                                                                     .frame      = kf->frame,
		                                                                                     .mesh_index = kf->mesh_index,
		                                                                                     .transform  = mat4_to_f32_array(kf->transform),
		                                                                                     .tween      = kf->tween,
                                                                                        });
		any_array_push(meshes, d);
	}
	raw->timeline_meshes = meshes;
}

static gpu_texture_t *tab_timeline_tex_from_buffer(buffer_t *buf, bool is_bgra) {
	gpu_texture_format_t fmt               = tab_timeline_tex_format();
	i32                  w                 = config_get_texture_res_x();
	i32                  h                 = config_get_texture_res_y();
	i32                  bytes_per_channel = base_bits_handle->i == TEXTURE_BITS_BITS8 ? 1 : base_bits_handle->i == TEXTURE_BITS_BITS16 ? 2 : 4;
	gpu_texture_t       *tmp               = gpu_create_texture_from_bytes(lz4_decode(buf, w * h * 4 * bytes_per_channel), w, h, fmt);
	gpu_texture_t       *rt                = gpu_create_render_target(w, h, fmt);
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
			tab_timeline_keyframe_t        *kf = GC_ALLOC_INIT(tab_timeline_keyframe_t, {0});
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
			tab_timeline_mesh_keyframe_t  *kf = GC_ALLOC_INIT(tab_timeline_mesh_keyframe_t, {0});
			kf->frame                         = d->frame;
			kf->mesh_index                    = d->mesh_index;
			kf->transform                     = mat4_from_f32_array(d->transform, 0);
			kf->tween                         = d->tween;
			any_array_push(tab_timeline_mesh_keyframes, kf);
		}
	}
}

static char *tab_timeline_row_name(i32 row) {
	i32 layer_count = g_project->_->layers->length;
	if (row >= layer_count) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[row - layer_count];
		return o->base->name;
	}
	slot_layer_t *l = g_project->_->layers->buffer[row];
	return l->name;
}

static char *tab_timeline_script_name(i32 row, i32 frame) {
	char *row_name = string_replace_all(tab_timeline_row_name(row), " ", "");
	char *name     = string("%s_%s.frame", row_name, i32_to_string(frame));
	return name;
}

static bool tab_timeline_has_script(i32 row, i32 frame) {
	if (g_project->script_names == NULL) {
		return false;
	}
	return string_array_index_of(g_project->script_names, tab_timeline_script_name(row, frame)) >= 0;
}

void tab_timeline_edit_script(i32 row, i32 frame) {
	tab_scripts_create(tab_timeline_script_name(row, frame));
	ui_base_htabs->buffer[TAB_AREA_SIDEBAR0]->i       = 2; // Scripts tab
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

static bool tab_timeline_can_delete() {
	i32  layer_count = g_project->_->layers->length;
	bool is_mesh     = tab_timeline_selected_row >= layer_count;
	bool has_kf;
	if (!is_mesh) {
		has_kf = tab_timeline_keyframes != NULL && tab_timeline_selected_frame > 0 &&
		         tab_timeline_find_keyframe(tab_timeline_selected_frame, tab_timeline_selected_row) >= 0;
	}
	else {
		i32 mi = tab_timeline_selected_row - layer_count;
		has_kf =
		    tab_timeline_mesh_keyframes != NULL && tab_timeline_selected_frame > 0 && tab_timeline_find_mesh_keyframe(tab_timeline_selected_frame, mi) >= 0;
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
		tab_timeline_pending_mesh_rm_index = tab_timeline_selected_row - layer_count;
		sys_notify_on_next_frame(&tab_timeline_remove_mesh_keyframe_on_next_frame, NULL);
	}

	tab_timeline_delete_script(tab_timeline_selected_row, tab_timeline_selected_frame);
}

static void tab_timeline_run_frame_scripts(i32 frame) {
	if (g_project->script_names == NULL) {
		return;
	}
	i32 row_count = g_project->_->layers->length + g_project->_->paint_objects->length;
	for (i32 ri = 0; ri < row_count; ri++) {
		i32 i = string_array_index_of(g_project->script_names, tab_timeline_script_name(ri, frame));
		if (i >= 0) {
			minic_ctx_free(minic_eval(string("void main() {\n%s\n}", g_project->script_datas->buffer[i])));
		}
	}
}

void tab_timeline_play() {
	tab_timeline_init();
	tab_timeline_playing    = true;
	tab_timeline_play_time  = sys_time();
	tab_timeline_last_frame = -1; // Ensure frame 0 scripts run
}

void tab_timeline_update() {
	tab_timeline_init();
	if (!tab_timeline_playing) {
		return;
	}
	iron_delay_idle_sleep();

	if (tab_timeline_last_frame < 0) {
		tab_timeline_save_current(0);
	}

	f64   elapsed = sys_time() - tab_timeline_play_time;
	float frame_f = (float)fmod(elapsed * tab_timeline_frame_rate, tab_timeline_max_frames);
	i32   frame_i = (i32)frame_f;

	tab_timeline_selected_frame = frame_i;

	if (g_config->workspace != WORKSPACE_PLAYER) {
		ui_base_hwnds->buffer[TAB_AREA_STATUS]->redraws = 2;
	}

	if (tab_timeline_mesh_keyframes != NULL) {
		tab_timeline_load_mesh_from_keyframes(frame_f);
	}

	if (frame_i != tab_timeline_last_frame) {
		tab_timeline_last_frame = frame_i;
		tab_timeline_load_from_keyframes(frame_i);
		tab_timeline_run_frame_scripts(frame_i);
		project_reskin_mesh(frame_i);
	}

	if (tab_timeline_keyframes != NULL) {
		tab_timeline_tween_from_keyframes(frame_f);
	}
}

void tab_timeline_draw_frame_context_menu() {
	i32  layer_count = g_project->_->layers->length;
	bool is_mesh     = tab_timeline_selected_row >= layer_count;
	bool has_kf;
	i32  mesh_kfi  = -1;
	i32  layer_kfi = -1;
	if (!is_mesh) {
		layer_kfi = tab_timeline_keyframes != NULL && tab_timeline_selected_frame > 0
		                ? tab_timeline_find_keyframe(tab_timeline_selected_frame, tab_timeline_selected_row)
		                : -1;
		has_kf    = layer_kfi >= 0;
	}
	else {
		i32 mi = tab_timeline_selected_row - layer_count;
		mesh_kfi =
		    tab_timeline_mesh_keyframes != NULL && tab_timeline_selected_frame > 0 ? tab_timeline_find_mesh_keyframe(tab_timeline_selected_frame, mi) : -1;
		has_kf = mesh_kfi >= 0;
	}

	if (ui_menu_button(tr("Edit Script"), "", ICON_EDIT)) {
		tab_timeline_edit_script(tab_timeline_selected_row, tab_timeline_selected_frame);
	}

	g_ui->enabled        = has_kf;
	ui_handle_t *h_tween = ui_handle(__ID__);
	h_tween->b           = false;
	if (g_ui->enabled) {
		h_tween->b = is_mesh ? ((tab_timeline_mesh_keyframe_t *)tab_timeline_mesh_keyframes->buffer[mesh_kfi])->tween
		                     : ((tab_timeline_keyframe_t *)tab_timeline_keyframes->buffer[layer_kfi])->tween;
	}
	ui_check(h_tween, tr("Tween"), "");
	if (g_ui->enabled && h_tween->changed) {
		if (is_mesh) {
			((tab_timeline_mesh_keyframe_t *)tab_timeline_mesh_keyframes->buffer[mesh_kfi])->tween = h_tween->b;
		}
		else {
			((tab_timeline_keyframe_t *)tab_timeline_keyframes->buffer[layer_kfi])->tween = h_tween->b;
		}
		ui_menu_keep_open = true;
	}

	bool has_script = tab_timeline_has_script(tab_timeline_selected_row, tab_timeline_selected_frame);
	g_ui->enabled   = has_kf || has_script;
	if (ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		tab_timeline_delete_selected();
	}
	g_ui->enabled = true;
}

void tab_timeline_draw_edit() {
	ui_menu_align();
	ui_handle_t *hfps = ui_handle(__ID__);
	hfps->f           = (f32)tab_timeline_frame_rate;
	ui_slider(hfps, tr("Frame Rate"), 1, 60, false, 1, true, UI_ALIGN_RIGHT, true);
	if (hfps->changed) {
		tab_timeline_frame_rate = (i32)hfps->f;
		ui_menu_keep_open       = true;
	}

	ui_menu_align();
	ui_handle_t *hframes = ui_handle(__ID__);
	hframes->f           = (f32)tab_timeline_max_frames;
	ui_slider(hframes, tr("Frame Count"), 1, 200, false, 1, true, UI_ALIGN_RIGHT, true);
	if (hframes->changed) {
		tab_timeline_max_frames = (i32)hframes->f;
		ui_menu_keep_open       = true;
	}

	if (ui_menu_button(tr("Clear"), "", ICON_ERASE)) {
		sys_notify_on_next_frame(&tab_timeline_clear_on_next_frame, NULL);
	}
}

void tab_timeline_stage_rename_box_draw() {
	stage_t     *s = tab_stages_get_stage();
	ui_handle_t *h = ui_handle(__ID__);

	if (tab_timeline_stage_rename_init) {
		h->text = string_copy(s->name);
		ui_start_text_edit(h, UI_ALIGN_LEFT);
		tab_timeline_stage_rename_init = false;
	}

	char *name = ui_text_input(h, tr("Name"), UI_ALIGN_LEFT, true, false);
	ui_end_element();
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
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
		tab_stages_apply(s);
	}

	g_ui->enabled = g_project->stages != NULL && g_project->stages->length > 1;
	if (ui_menu_button(tr("Remove"), "", ICON_DELETE)) {
		array_splice(g_project->stages, tab_stages_selected, 1);
		if (tab_stages_selected >= g_project->stages->length) {
			tab_stages_selected = g_project->stages->length - 1;
		}
		tab_stages_apply(g_project->stages->buffer[tab_stages_selected]);
	}
	g_ui->enabled = true;

	if (ui_menu_button(tr("Rename"), "", ICON_EDIT)) {
		tab_timeline_stage_rename_init = true;
		ui_box_show_custom(&tab_timeline_stage_rename_box_draw, 400, 130, NULL, true, tr("Rename Stage"));
	}
}

static bool tab_timeline_input_in_rect(f32 x, f32 y, f32 w, f32 h) {
	if (g_ui->scissor && g_ui->input_y < g_ui->_window_y + g_ui->window_header_h) {
		return false;
	}
	return g_ui->input_x > g_ui->_window_x + x && g_ui->input_x < g_ui->_window_x + x + w && g_ui->input_y > g_ui->_window_y + y &&
	       g_ui->input_y < g_ui->_window_y + y + h;
}

void tab_timeline_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Timeline"), false, -1, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		tab_timeline_init();

		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw((f32[]){-70, -110, -100, -100, -40, -40, -40, -40, -60}, 9);
		ui_row(row);

		// Stage
		if (g_project->stages == NULL) {
			g_project->stages = any_array_create_from_raw((void *[]){}, 0);
			stage_t *s        = tab_stages_create_stage("Stage 1");
			for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
				string_array_push(s->objects, g_project->_->paint_objects->buffer[i]->base->name);
			}
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				string_array_push(s->layers, g_project->_->layers->buffer[i]->name);
			}
			any_array_push(g_project->stages, s);
		}

		string_array_t *stage_names = string_array_create(0);
		if (g_project->stages != NULL) {
			for (i32 i = 0; i < g_project->stages->length; ++i) {
				string_array_push(stage_names, g_project->stages->buffer[i]->name);
			}
		}

		if (ui_button(tr("Stage"), UI_ALIGN_CENTER, "")) {
			ui_menu_draw(&tab_timeline_draw_stage_menu, -1, -1);
		}

		ui_handle_t *stage_handle = ui_handle(__ID__);
		stage_handle->i           = tab_stages_selected;
		tab_stages_selected       = ui_combo(stage_handle, stage_names, tr("Stage"), false, UI_ALIGN_LEFT, true);
		if (stage_handle->changed && g_project->stages != NULL && tab_stages_selected < g_project->stages->length) {
			tab_stages_apply(g_project->stages->buffer[tab_stages_selected]);
		}

		if (ui_icon_button(tr("Keyframe"), ICON_PLUS, UI_ALIGN_CENTER)) {
			i32 li = tab_timeline_selected_row;
			if (tab_timeline_selected_frame > 0 && li < g_project->_->layers->length && slot_layer_is_layer(g_project->_->layers->buffer[li])) {
				tab_timeline_pending_kf_frame = tab_timeline_selected_frame;
				tab_timeline_pending_kf_layer = li;
				sys_notify_on_next_frame(&tab_timeline_add_keyframe_on_next_frame, NULL);
			}
			else if (tab_timeline_selected_frame > 0 && li >= g_project->_->layers->length) {
				i32 mi = li - g_project->_->layers->length;
				if (mi < g_project->_->paint_objects->length) {
					tab_timeline_pending_mesh_add_frame = tab_timeline_selected_frame;
					tab_timeline_pending_mesh_add_index = mi;
					sys_notify_on_next_frame(&tab_timeline_add_mesh_keyframe_on_next_frame, NULL);
				}
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

		f32 layer_name_w    = 120.0f * UI_SCALE();
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
			if (eye_hover && g_ui->input_released) {
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
		for (i32 mi = 0; mi < mesh_count; mi++) {
			mesh_object_t *mesh = g_project->_->paint_objects->buffer[mi];
			i32            ri   = row_count + mi;
			if (stage != NULL && string_array_index_of(stage->objects, mesh->base->name) < 0) {
				continue;
			}
			f32 row_y  = start_y + font_h + 2 + vis_row * strip_h;
			f32 icon_y = row_y + (strip_h - icon_size) / 2.0f;

			// Eye icon
			rect_t *eye   = resource_tile18(icons, mesh->base->visible ? ICON18_EYE_ON : ICON18_EYE_OFF);
			f32     eye_y = row_y + (strip_h - eye_size) / 2.0f;
			draw_set_color(g_theme->HOVER_COL + 0x00282828);
			draw_scaled_sub_image(icons, eye->x, eye->y, eye->w, eye->h, g_ui->_x, eye_y, eye_size, eye_size);
			bool eye_hover = !tab_timeline_scrolling && tab_timeline_input_in_rect(g_ui->_x, row_y, eye_size, strip_h);
			if (eye_hover && g_ui->input_released) {
				mesh->base->visible = !mesh->base->visible;

				util_mesh_merge(util_mesh_get_visible());
				util_uv_uvmap_cached       = false;
				util_uv_trianglemap_cached = false;
				util_uv_dilatemap_cached   = false;
				g_context->ddirty          = 2;
			}

			rect_t *rect = resource_tile50(icons, ICON_CUBE);
			draw_set_color(g_theme->LABEL_COL);
			draw_scaled_sub_image(icons, rect->x, rect->y, rect->w, rect->h, g_ui->_x + eye_size + 4, icon_y, icon_size, icon_size);
			draw_set_color(g_theme->LABEL_COL);
			draw_string(mesh->base->name, g_ui->_x + eye_size + icon_size + 6, row_y + (strip_h - font_h) / 2.0f);

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

				if (i == 0 || (tab_timeline_mesh_keyframes != NULL && tab_timeline_find_mesh_keyframe(i, mi) >= 0)) {
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
			tab_timeline_scrolling = false;
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
