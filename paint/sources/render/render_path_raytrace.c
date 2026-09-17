
#include "../global.h"

f32 render_path_raytrace_uv_scale = 1.0;

#define OVERRIDE_MAX 64

static mesh_data_t *render_path_raytrace_override_data[OVERRIDE_MAX];
static i32          render_path_raytrace_override_material[OVERRIDE_MAX];
static bool         render_path_raytrace_override_dirty[OVERRIDE_MAX];
static i32          render_path_raytrace_override_count     = 0;
static i32          render_path_raytrace_override_allocated = 0;
static char        *render_path_raytrace_override_format    = NULL;
static i32          render_path_raytrace_override_width     = 0;
static i32          render_path_raytrace_override_height    = 0;

#define ENV_CDF_W 256
#define ENV_CDF_H 128
#define ENV_CDF_N (ENV_CDF_W * ENV_CDF_H)

gpu_texture_t *render_path_raytrace_env_cdf      = NULL;
static char   *render_path_raytrace_env_cdf_file = NULL;

static float render_path_raytrace_half_to_float(uint16_t h) {
	uint32_t sign = (uint32_t)(h >> 15) << 31;
	uint32_t exp  = (h >> 10) & 0x1f;
	uint32_t man  = h & 0x3ff;
	uint32_t bits;
	if (exp == 0) {
		if (man == 0) {
			bits = sign;
		}
		else {
			exp = 127 - 15 + 1;
			while ((man & 0x400) == 0) {
				man <<= 1;
				exp--;
			}
			man &= 0x3ff;
			bits = sign | (exp << 23) | (man << 13);
		}
	}
	else if (exp == 31) {
		bits = sign | 0x7f800000u | (man << 13);
	}
	else {
		bits = sign | ((exp + 127 - 15) << 23) | (man << 13);
	}
	float f;
	memcpy(&f, &bits, sizeof(f));
	return f;
}

static bool render_path_raytrace_env_luma(char *file, float *out) {
	buffer_t *blob = iron_load_blob(data_resolve_path(file));
	if (blob == NULL) {
		return false;
	}
	int32_t  w    = iron_read_s32le(blob->buffer);
	int32_t  h    = iron_read_s32le(blob->buffer + 4);
	bool     f16  = blob->buffer[11] == 'F';
	int      bpp  = f16 ? 8 : 4;
	size_t   size = (size_t)w * h * bpp;
	uint8_t *px   = malloc(size);
	if (px == NULL) {
		iron_delete_blob(blob);
		return false;
	}
	int decoded = LZ4_decompress_safe((char *)blob->buffer + 12, (char *)px, blob->length - 12, (int)size);
	iron_delete_blob(blob);
	if (decoded != (int)size) {
		free(px);
		return false;
	}

	for (int cy = 0; cy < ENV_CDF_H; ++cy) {
		int y0 = cy * h / ENV_CDF_H;
		int y1 = (cy + 1) * h / ENV_CDF_H;
		if (y1 <= y0) {
			y1 = y0 + 1;
		}
		float sin_theta = (float)sin(3.14159265358979 * ((double)cy + 0.5) / ENV_CDF_H);
		for (int cx = 0; cx < ENV_CDF_W; ++cx) {
			int x0 = cx * w / ENV_CDF_W;
			int x1 = (cx + 1) * w / ENV_CDF_W;
			if (x1 <= x0) {
				x1 = x0 + 1;
			}
			double sum = 0.0;
			int    n   = 0;
			for (int y = y0; y < y1 && y < h; ++y) {
				for (int x = x0; x < x1 && x < w; ++x) {
					size_t o = ((size_t)y * w + x) * bpp;
					float  r, g, b;
					if (f16) {
						uint16_t *p = (uint16_t *)(px + o);
						r           = render_path_raytrace_half_to_float(p[0]);
						g           = render_path_raytrace_half_to_float(p[1]);
						b           = render_path_raytrace_half_to_float(p[2]);
					}
					else {
						r = px[o + 0] / 255.0f;
						g = px[o + 1] / 255.0f;
						b = px[o + 2] / 255.0f;
					}
					sum += 0.2126 * r + 0.7152 * g + 0.0722 * b;
					++n;
				}
			}
			out[cy * ENV_CDF_W + cx] = n > 0 ? (float)(sum / n) * sin_theta : 0.0f;
		}
	}
	free(px);
	return true;
}

static void render_path_raytrace_build_env_cdf(char *file) {
	if (render_path_raytrace_env_cdf_file != NULL && strcmp(render_path_raytrace_env_cdf_file, file) == 0) {
		return;
	}

	float *weight = malloc(sizeof(float) * ENV_CDF_N);
	if (weight == NULL) {
		return;
	}
	if (!render_path_raytrace_env_luma(file, weight)) {
		free(weight);
		return;
	}

	double total = 0.0;
	for (int i = 0; i < ENV_CDF_N; ++i) {
		total += weight[i];
	}
	if (total <= 0.0) {
		free(weight);
		return;
	}

	float *prob  = malloc(sizeof(float) * ENV_CDF_N);
	int   *alias = malloc(sizeof(int) * ENV_CDF_N);
	int   *stack = malloc(sizeof(int) * ENV_CDF_N);
	float *pdf   = malloc(sizeof(float) * ENV_CDF_N);
	if (prob == NULL || alias == NULL || stack == NULL || pdf == NULL) {
		free(weight);
		free(prob);
		free(alias);
		free(stack);
		free(pdf);
		return;
	}

	for (int i = 0; i < ENV_CDF_N; ++i) {
		double p = weight[i] / total;
		pdf[i]   = (float)(p * ENV_CDF_N);
		prob[i]  = (float)(p * ENV_CDF_N);
		alias[i] = i;
	}

	int small_top = 0;
	int large_top = ENV_CDF_N;
	for (int i = 0; i < ENV_CDF_N; ++i) {
		if (prob[i] < 1.0f) {
			stack[small_top++] = i;
		}
		else {
			stack[--large_top] = i;
		}
	}
	while (small_top > 0 && large_top < ENV_CDF_N) {
		int s    = stack[--small_top];
		int l    = stack[large_top++];
		alias[s] = l;
		prob[l]  = (prob[l] + prob[s]) - 1.0f;
		if (prob[l] < 1.0f) {
			stack[small_top++] = l;
		}
		else {
			stack[--large_top] = l;
		}
	}
	while (large_top < ENV_CDF_N) {
		prob[stack[large_top++]] = 1.0f;
	}
	while (small_top > 0) {
		prob[stack[--small_top]] = 1.0f;
	}

	float *data = malloc(sizeof(float) * 4 * ENV_CDF_N);
	if (data != NULL) {
		for (int i = 0; i < ENV_CDF_N; ++i) {
			data[i * 4 + 0] = prob[i];
			data[i * 4 + 1] = (float)alias[i];
			data[i * 4 + 2] = pdf[i];
			data[i * 4 + 3] = pdf[alias[i]];
		}
		if (render_path_raytrace_env_cdf == NULL) {
			render_path_raytrace_env_cdf         = malloc(sizeof(gpu_texture_t));
			render_path_raytrace_env_cdf->buffer = NULL;
		}
		else {
			gpu_texture_destroy(render_path_raytrace_env_cdf);
		}
		gpu_texture_init_from_bytes(render_path_raytrace_env_cdf, data, ENV_CDF_W, ENV_CDF_H, GPU_TEXTURE_FORMAT_RGBA128, false);
		free(data);

		free(render_path_raytrace_env_cdf_file);
		render_path_raytrace_env_cdf_file = string_copy(file);
	}

	free(weight);
	free(prob);
	free(alias);
	free(stack);
	free(pdf);
}

void render_path_raytrace_init() {}

static bool render_path_raytrace_sculpt_visible() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->texpaint_sculpt != NULL && slot_layer_is_visible(l)) {
			return true;
		}
	}
	return false;
}

static i32 render_path_raytrace_override_slot(mesh_data_t *data) {
	for (i32 i = 0; i < render_path_raytrace_override_count; ++i) {
		if (render_path_raytrace_override_data[i] == data) {
			return i;
		}
	}
	return -1;
}

static char *render_path_raytrace_override_target(i32 slot, char channel) {
	return string_tmp("raytrace_override%d_%c", slot, channel);
}

static bool render_path_raytrace_override_targets(i32 count) {
	if (count == 0) {
		return false;
	}
	char *format    = base_bits == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
	i32   width     = config_get_texture_res_x();
	i32   height    = config_get_texture_res_y();
	bool  recreated = false;
	if (render_path_raytrace_override_format != NULL && (!string_equals(render_path_raytrace_override_format, format) ||
	                                                     render_path_raytrace_override_width != width || render_path_raytrace_override_height != height)) {
		for (i32 i = 0; i < render_path_raytrace_override_allocated; ++i) {
			for (char *c = "abc"; *c != '\0'; ++c) {
				char            *name = render_path_raytrace_override_target(i, *c);
				render_target_t *rt   = any_map_get(render_path_render_targets, name);
				gpu_delete_texture(rt->_image);
				map_delete(render_path_render_targets, name);
			}
		}
		render_target_t *mask = any_map_get(render_path_render_targets, "raytrace_override_mask");
		if (mask != NULL) {
			gpu_delete_texture(mask->_image);
			map_delete(render_path_render_targets, "raytrace_override_mask");
		}
		render_path_raytrace_override_allocated = 0;
		recreated                               = true;
	}
	render_path_raytrace_override_format = format;
	render_path_raytrace_override_width  = width;
	render_path_raytrace_override_height = height;

	if (any_map_get(render_path_render_targets, "raytrace_override_mask") == NULL) {
		render_target_t *t = render_target_create();
		t->name            = "raytrace_override_mask";
		t->width           = width;
		t->height          = height;
		t->format          = "R8";
		render_path_create_render_target(t);
	}
	while (render_path_raytrace_override_allocated < count) {
		for (char *c = "abc"; *c != '\0'; ++c) {
			render_target_t *t = render_target_create();
			t->name            = string_copy(render_path_raytrace_override_target(render_path_raytrace_override_allocated, *c));
			t->width           = width;
			t->height          = height;
			t->format          = format;
			render_path_create_render_target(t);
		}
		render_path_raytrace_override_allocated++;
	}
	return recreated;
}

static gpu_texture_t *render_path_raytrace_override_texture(i32 slot, char channel) {
	render_target_t *rt = any_map_get(render_path_render_targets, render_path_raytrace_override_target(slot, channel));
	return rt->_image;
}

static void render_path_raytrace_draw_override(i32 slot) {
	mesh_data_t           *data    = render_path_raytrace_override_data[slot];
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	u8_array_t            *visible = u8_array_create(objects->length);
	u8_array_t            *culling = u8_array_create(objects->length);
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *p   = objects->buffer[i];
		visible->buffer[i] = p->base->visible;
		culling->buffer[i] = p->frustum_culling;
		p->base->visible   = p->data == data;
		p->frustum_culling = false;
	}

	string_array_t *additional = any_array_create_from_raw(
	    (void *[]){
	        render_path_raytrace_override_target(slot, 'b'),
	        render_path_raytrace_override_target(slot, 'c'),
	        "raytrace_override_mask",
	    },
	    3);
	render_path_raytrace_override_pass = true;
	render_path_set_target(render_path_raytrace_override_target(slot, 'a'), additional, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	render_path_bind_target("main", "gbufferD");
	render_path_bind_target("texpaint_blend1", "paintmask");
	render_path_draw_meshes("atlas");
	render_path_raytrace_override_pass = false;

	for (i32 i = 0; i < objects->length; ++i) {
		objects->buffer[i]->base->visible   = visible->buffer[i];
		objects->buffer[i]->frustum_culling = culling->buffer[i];
	}
	array_delete(visible);
	array_delete(culling);
	array_delete(additional);
	render_path_raytrace_override_dirty[slot] = false;
}

void render_path_raytrace_draw_overrides(bool all) {
	for (i32 i = 0; i < render_path_raytrace_override_count; ++i) {
		if (all || render_path_raytrace_override_dirty[i]) {
			render_path_raytrace_draw_override(i);
		}
	}
}

static void render_path_raytrace_update_overrides() {
	mesh_data_t *data[OVERRIDE_MAX];
	i32          material[OVERRIDE_MAX];
	i32          count = 0;
	for (i32 i = 0; i < g_project->_->paint_objects->length && count < OVERRIDE_MAX; ++i) {
		mesh_object_t *po  = g_project->_->paint_objects->buffer[i];
		i32            mat = tab_meshes_get_linked_override(po);
		if (!po->base->visible || mat < 0) {
			continue;
		}
		bool seen = false;
		for (i32 j = 0; j < count && !seen; ++j) {
			seen = data[j] == po->data;
		}
		if (!seen) {
			data[count]     = po->data;
			material[count] = mat;
			count++;
		}
	}
	i32  allocated = render_path_raytrace_override_allocated;
	bool recreated = render_path_raytrace_override_targets(count);
	for (i32 i = 0; i < count; ++i) {
		bool changed = recreated || i >= allocated || i >= render_path_raytrace_override_count || render_path_raytrace_override_data[i] != data[i] ||
		               render_path_raytrace_override_material[i] != material[i];
		render_path_raytrace_override_data[i]     = data[i];
		render_path_raytrace_override_material[i] = material[i];
		render_path_raytrace_override_dirty[i]    = changed;
	}
	render_path_raytrace_override_count = count;
}

static bool render_path_raytrace_overrides_visible() {
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *po = g_project->_->paint_objects->buffer[i];
		if (po->base->visible && tab_meshes_get_linked_override(po) >= 0) {
			return true;
		}
	}
	return false;
}

static bool render_path_raytrace_overrides_changed() {
	if (render_path_raytrace_sculpt_visible()) {
		return false;
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *po = g_project->_->paint_objects->buffer[i];
		if (!po->base->visible) {
			continue;
		}
		i32 mat  = tab_meshes_get_linked_override(po);
		i32 slot = render_path_raytrace_override_slot(po->data);
		if (mat < 0 ? slot >= 0 : (slot < 0 || render_path_raytrace_override_material[slot] != mat)) {
			return true;
		}
	}
	return false;
}

void render_path_raytrace_commands(bool use_live_layer) {
	if (render_path_raytrace_ready && !render_path_raytrace_is_bake && render_path_raytrace_overrides_changed()) {
		render_path_raytrace_ready = false;
	}
	if (!render_path_raytrace_ready || render_path_raytrace_is_bake) {
		render_path_raytrace_ready = true;
		if (render_path_raytrace_is_bake) {
			render_path_raytrace_is_bake     = false;
			render_path_raytrace_init_shader = true;
		}
		char *mode = config_is_raytrace_fast() ? "core" : "full";
		render_path_raytrace_raytrace_init(string("raytrace_brute_%s%s", mode, render_path_raytrace_ext), true);
		render_path_raytrace_last_envmap = NULL;
	}

	if (!g_context->envmap_loaded) {
		context_load_envmap();
		context_update_envmap();
	}

	world_data_t  *probe        = scene_world;
	gpu_texture_t *saved_envmap = g_context->show_envmap_blur ? probe->_->radiance_mipmaps->buffer[0] : g_context->saved_envmap;

	////
	if (render_path_raytrace_last_envmap != saved_envmap) {
		render_path_raytrace_last_envmap = saved_envmap;

		gpu_texture_t *bnoise_sobol    = data_get_texture("bnoise_sobol.k");
		gpu_texture_t *bnoise_scramble = data_get_texture("bnoise_scramble.k");
		gpu_texture_t *bnoise_rank     = data_get_texture("bnoise_rank.k");

		if (scene_world->envmap != NULL) {
			render_path_raytrace_build_env_cdf(scene_world->envmap);
		}

		slot_layer_t *l = layers_flatten(true, NULL);
		gpu_raytrace_set_textures(l->texpaint, l->texpaint_nor, l->texpaint_pack, saved_envmap, bnoise_sobol, bnoise_scramble, bnoise_rank,
		                          render_path_raytrace_env_cdf);
	}
	////

	bool is_live = g_config->brush_live && render_path_paint_live_layer_drawn > 0 && render_path_paint_live_layer != NULL;
	if (g_context->pdirty > 0 || g_context->rtdirty > 0 || is_live) {
		slot_layer_t  *layer          = g_context->layer;
		gpu_texture_t *_texpaint      = layer->texpaint;
		gpu_texture_t *_texpaint_nor  = layer->texpaint_nor;
		gpu_texture_t *_texpaint_pack = layer->texpaint_pack;
		if (is_live) {
			layer->texpaint = render_path_paint_live_layer->texpaint;
			if (slot_layer_is_layer(layer)) {
				layer->texpaint_nor  = render_path_paint_live_layer->texpaint_nor;
				layer->texpaint_pack = render_path_paint_live_layer->texpaint_pack;
			}
		}
		layers_flatten(true, NULL);
		if (is_live) {
			layer->texpaint      = _texpaint;
			layer->texpaint_nor  = _texpaint_nor;
			layer->texpaint_pack = _texpaint_pack;
		}
		if (g_context->rtdirty > 0) {
			render_path_raytrace_draw_overrides(true);
		}
		g_context->rtdirty = 0;
	}

	camera_object_t *cam                  = scene_camera;
	transform_t     *ct                   = cam->base->transform;
	render_path_raytrace_help_mat         = cam->v;
	render_path_raytrace_help_mat         = mat4_mult_mat(render_path_raytrace_help_mat, cam->p);
	render_path_raytrace_help_mat         = mat4_inv(render_path_raytrace_help_mat);
	render_path_raytrace_f32a->buffer[0]  = transform_world_x(ct);
	render_path_raytrace_f32a->buffer[1]  = transform_world_y(ct);
	render_path_raytrace_f32a->buffer[2]  = transform_world_z(ct);
	render_path_raytrace_f32a->buffer[3]  = render_path_raytrace_frame;
	render_path_raytrace_f32a->buffer[4]  = render_path_raytrace_help_mat.m00;
	render_path_raytrace_f32a->buffer[5]  = render_path_raytrace_help_mat.m01;
	render_path_raytrace_f32a->buffer[6]  = render_path_raytrace_help_mat.m02;
	render_path_raytrace_f32a->buffer[7]  = render_path_raytrace_help_mat.m03;
	render_path_raytrace_f32a->buffer[8]  = render_path_raytrace_help_mat.m10;
	render_path_raytrace_f32a->buffer[9]  = render_path_raytrace_help_mat.m11;
	render_path_raytrace_f32a->buffer[10] = render_path_raytrace_help_mat.m12;
	render_path_raytrace_f32a->buffer[11] = render_path_raytrace_help_mat.m13;
	render_path_raytrace_f32a->buffer[12] = render_path_raytrace_help_mat.m20;
	render_path_raytrace_f32a->buffer[13] = render_path_raytrace_help_mat.m21;
	render_path_raytrace_f32a->buffer[14] = render_path_raytrace_help_mat.m22;
	render_path_raytrace_f32a->buffer[15] = render_path_raytrace_help_mat.m23;
	render_path_raytrace_f32a->buffer[16] = render_path_raytrace_help_mat.m30;
	render_path_raytrace_f32a->buffer[17] = render_path_raytrace_help_mat.m31;
	render_path_raytrace_f32a->buffer[18] = render_path_raytrace_help_mat.m32;
	render_path_raytrace_f32a->buffer[19] = render_path_raytrace_help_mat.m33;
	render_path_raytrace_f32a->buffer[20] = scene_world->strength;
	if (!g_context->show_envmap) {
		render_path_raytrace_f32a->buffer[20] = -render_path_raytrace_f32a->buffer[20];
	}
	render_path_raytrace_f32a->buffer[21] = g_context->envmap_angle;
	render_path_raytrace_f32a->buffer[22] = render_path_raytrace_uv_scale;
	render_path_raytrace_f32a->buffer[23] = render_path_raytrace_env_cdf != NULL ? 1.0f : 0.0f;

	if (render_path_base_buf_swapped) {
		render_path_base_swap_buf("buf");
	}

	if (render_path_raytrace_frame < g_config->pathtrace_frames) {
		render_target_t *framebuffer = any_map_get(render_path_render_targets, "buf");
		_gpu_raytrace_dispatch_rays(framebuffer->_image, render_path_raytrace_f32a);
		render_path_raytrace_frame++;
	}

	g_context->ddirty--;
	g_context->pdirty--;
}

void render_path_raytrace_raytrace_init(char *shader_name, bool build) {
	if (render_path_raytrace_init_shader) {
		render_path_raytrace_init_shader = false;
		buffer_t *shader                 = data_get_blob(shader_name);
		_gpu_raytrace_init(shader);
	}

	if (build && g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}

	_gpu_raytrace_as_init();

	mesh_object_t *merged = g_context->merged_object;
	bool           moving = render_path_raytrace_moving || tab_timeline_playing;
	if (merged != NULL && (render_path_raytrace_sculpt_visible() || (!render_path_raytrace_overrides_visible() && !moving))) {
		render_path_raytrace_override_count = 0;
		transform_t *t                      = merged->base->transform;
		t->scale_world                      = merged->data->scale_pos;
		transform_build_matrix(t);
		_gpu_raytrace_as_add(merged->data->_->vertex_buffer, merged->data->_->index_buffer, t->world_unpack, NULL);
	}
	else {
		render_path_raytrace_update_overrides();
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *po = g_project->_->paint_objects->buffer[i];
			if (!po->base->visible) {
				continue;
			}
			i32 slot = render_path_raytrace_override_slot(po->data);
			if (slot < 0) {
				_gpu_raytrace_as_add(po->data->_->vertex_buffer, po->data->_->index_buffer, po->base->transform->world_unpack, NULL);
				continue;
			}
			gpu_texture_t *a = render_path_raytrace_override_texture(slot, 'a');
			gpu_texture_t *textures[3];
			if (render_path_raytrace_is_bake) {
				textures[0] = NULL;
				textures[1] = NULL;
				textures[2] = a;
			}
			else {
				textures[0] = a;
				textures[1] = render_path_raytrace_override_texture(slot, 'b');
				textures[2] = render_path_raytrace_override_texture(slot, 'c');
			}
			_gpu_raytrace_as_add(po->data->_->vertex_buffer, po->data->_->index_buffer, po->base->transform->world_unpack, textures);
		}
	}

	_gpu_raytrace_as_build();
	render_path_raytrace_draw_overrides(false);
}

void render_path_raytrace_draw(bool use_live_layer) {
	bool is_live   = g_config->brush_live && render_path_paint_live_layer_drawn > 0;
	bool is_player = g_config->workspace == WORKSPACE_PLAYER;
	if (g_context->ddirty > 1 || g_context->pdirty > 0 || is_live || is_player) {
		render_path_raytrace_frame = 0;
	}

	render_path_raytrace_commands(use_live_layer);
	render_path_set_target("buf", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_meshes("overlay");
	render_path_set_target("buf", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_compass();
	render_path_set_target("last", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("buf", "tex");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");
	render_path_base_draw_bloom("buf", "last");
	render_path_set_target("last", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_envsphere();
	render_pathsphere();
	render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("last", "tex");
	render_path_draw_shader("Scene/copy_pass/copy_pass");
	render_path_paint_commands_cursor();
}
