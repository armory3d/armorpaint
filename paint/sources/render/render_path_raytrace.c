
#include "../global.h"

f32           render_path_raytrace_uv_scale = 1.0;
mat4_t        render_path_raytrace_transform;
gpu_buffer_t *render_path_raytrace_vb;
gpu_buffer_t *render_path_raytrace_ib;

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
		gpu_texture_init_from_bytes(render_path_raytrace_env_cdf, data, ENV_CDF_W, ENV_CDF_H, GPU_TEXTURE_FORMAT_RGBA128);
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

void render_path_raytrace_commands(bool use_live_layer) {
	if (!render_path_raytrace_ready || render_path_raytrace_is_bake) {
		render_path_raytrace_ready = true;
		if (render_path_raytrace_is_bake) {
			render_path_raytrace_is_bake     = false;
			render_path_raytrace_init_shader = true;
		}
		char *ext = "";
		if (config_is_raytrace_multi()) {
			ext = "multi_";
		}
		char *mode = config_is_raytrace_fast() ? "core" : "full";
		render_path_raytrace_raytrace_init(string("raytrace_brute_%s%s%s", ext, mode, render_path_raytrace_ext), true);
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
		g_context->rtdirty = 0;
	}

	camera_object_t *cam                 = scene_camera;
	transform_t     *ct                  = cam->base->transform;
	render_path_raytrace_help_mat        = cam->v;
	render_path_raytrace_help_mat        = mat4_mult_mat(render_path_raytrace_help_mat, cam->p);
	render_path_raytrace_help_mat        = mat4_inv(render_path_raytrace_help_mat);
	render_path_raytrace_f32a->buffer[0] = transform_world_x(ct);
	render_path_raytrace_f32a->buffer[1] = transform_world_y(ct);
	render_path_raytrace_f32a->buffer[2] = transform_world_z(ct);
	render_path_raytrace_f32a->buffer[3] = render_path_raytrace_frame;
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

void render_path_raytrace_build_data() {
	if (g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}

	mesh_object_t *mo = !context_layer_filter_used() ? g_context->merged_object : g_context->paint_object;

	if (config_is_raytrace_multi()) {
		render_path_raytrace_transform = mo->base->transform->world_unpack;
	}
	else {
		render_path_raytrace_transform = mat4_identity();
	}

	f32 sc = mo->base->transform->scale.x * mo->data->scale_pos;
	if (mo->base->parent != NULL) {
		sc *= mo->base->parent->transform->scale.x;
	}
	render_path_raytrace_transform = mat4_scale(render_path_raytrace_transform, (vec4_t){sc, sc, sc, 1.0});

	render_path_raytrace_vb = mo->data->_->vertex_buffer;
	render_path_raytrace_ib = mo->data->_->index_buffer;
}

void render_path_raytrace_raytrace_init(char *shader_name, bool build) {
	if (render_path_raytrace_init_shader) {
		render_path_raytrace_init_shader = false;
		buffer_t *shader                 = data_get_blob(shader_name);
		_gpu_raytrace_init(shader);
	}

	if (build) {
		render_path_raytrace_build_data();
	}

	{
		config_apply_raytrace_multi();
		_gpu_raytrace_as_init();

		if (config_is_raytrace_multi()) {
			for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
				mesh_object_t *po = g_project->_->paint_objects->buffer[i];
				if (!po->base->visible) {
					continue;
				}
				_gpu_raytrace_as_add(po->data->_->vertex_buffer, po->data->_->index_buffer, po->base->transform->world_unpack);
			}
		}
		else {
			_gpu_raytrace_as_add(render_path_raytrace_vb, render_path_raytrace_ib, render_path_raytrace_transform);
		}

		gpu_buffer_t *vb_full = g_context->merged_object->data->_->vertex_buffer;
		gpu_buffer_t *ib_full = g_context->merged_object->data->_->index_buffer;

		_gpu_raytrace_as_build(vb_full, ib_full);
	}
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
