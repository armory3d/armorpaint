
#include "../global.h"

f32            render_path_raytrace_bake_rays_timer      = 0.0;
i32            render_path_raytrace_bake_rays_counter    = 0;
gpu_texture_t *render_path_raytrace_bake_last_layer      = NULL;
i32            render_path_raytrace_bake_last_bake_type  = 0;
i32            render_path_raytrace_bake_last_bake_type2 = 0;
static bool    render_path_raytrace_bake_env_sampling    = false;

void render_path_raytrace_bake_commands_parse_paint_material(void (*parse_paint_material)(bool)) {
	parse_paint_material(true);
}

char *render_path_raytrace_bake_get_bake_shader_name() {
	return g_context->bake_type == BAKE_TYPE_OCCLUSION     ? string("raytrace_bake_ao%s", render_path_raytrace_ext)
	       : g_context->bake_type == BAKE_TYPE_LIGHTMAP    ? string("raytrace_bake_light%s", render_path_raytrace_ext)
	       : g_context->bake_type == BAKE_TYPE_BENT_NORMAL ? string("raytrace_bake_bent%s", render_path_raytrace_ext)
	                                                       : string("raytrace_bake_thick%s", render_path_raytrace_ext);
}

bool render_path_raytrace_bake_commands(void (*parse_paint_material)(bool)) {

	if (!render_path_raytrace_ready || !render_path_raytrace_is_bake || render_path_raytrace_bake_last_bake_type != g_context->bake_type) {

		bool rebuild = !(render_path_raytrace_ready && render_path_raytrace_is_bake && render_path_raytrace_bake_last_bake_type != g_context->bake_type);
		render_path_raytrace_bake_last_bake_type = g_context->bake_type;
		render_path_raytrace_ready               = true;
		render_path_raytrace_is_bake             = true;
		render_path_raytrace_last_envmap         = NULL;
		render_path_raytrace_bake_last_layer     = NULL;

		if (any_map_get(render_path_render_targets, "baketex0") != NULL) {
			render_target_t *baketex0 = any_map_get(render_path_render_targets, "baketex0");
			render_target_t *baketex1 = any_map_get(render_path_render_targets, "baketex1");
			render_target_t *baketex2 = any_map_get(render_path_render_targets, "baketex2");
			gpu_delete_texture(baketex0->_image);
			gpu_delete_texture(baketex1->_image);
			gpu_delete_texture(baketex2->_image);
		}

		{
			render_target_t *t = render_target_create();
			t->name            = "baketex0";
			t->width           = config_get_texture_res_x();
			t->height          = config_get_texture_res_y();
			t->format          = "RGBA64";
			render_path_create_render_target(t);
		}
		{
			render_target_t *t = render_target_create();
			t->name            = "baketex1";
			t->width           = config_get_texture_res_x();
			t->height          = config_get_texture_res_y();
			t->format          = "RGBA64";
			render_path_create_render_target(t);
		}
		{
			render_target_t *t = render_target_create();
			t->name            = "baketex2";
			t->width           = config_get_texture_res_x();
			t->height          = config_get_texture_res_y();
			t->format          = "RGBA64"; // Match raytrace_target format
			render_path_create_render_target(t);
		}

		bake_type_t _bake_type = g_context->bake_type;
		g_context->bake_type   = BAKE_TYPE_INIT;
		parse_paint_material(false);
		render_path_set_target("baketex0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		// Pixels with alpha of 0.0 are skipped during raytracing
		string_array_t *additional = any_array_create_from_raw(
		    (void *[]){
		        "baketex1",
		    },
		    1);
		render_path_set_target("baketex0", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_draw_meshes("paint");
		g_context->bake_type = _bake_type;
		sys_notify_on_next_frame(&render_path_raytrace_bake_commands_parse_paint_material, parse_paint_material);

		render_path_raytrace_init_shader = true;
		render_path_raytrace_raytrace_init(render_path_raytrace_bake_get_bake_shader_name(), rebuild);

		return false;
	}

	if (!g_context->envmap_loaded) {
		context_load_envmap();
		context_update_envmap();
	}

	world_data_t  *probe        = scene_world;
	gpu_texture_t *saved_envmap = g_context->show_envmap_blur ? probe->_->radiance_mipmaps->buffer[0] : g_context->saved_envmap;

	if (render_path_raytrace_last_envmap != saved_envmap || render_path_raytrace_bake_last_layer != g_context->layer->texpaint ||
	    render_path_raytrace_bake_last_bake_type2 != g_context->bake_type || g_context->rtdirty > 0) {

		if (g_context->rtdirty > 0) {
			render_path_raytrace_draw_overrides(true);
		}
		g_context->rtdirty = 0;

		render_path_raytrace_last_envmap          = saved_envmap;
		render_path_raytrace_bake_last_layer      = g_context->layer->texpaint;
		render_path_raytrace_bake_last_bake_type2 = g_context->bake_type;

		gpu_texture_t *bnoise_sobol    = data_get_texture("bnoise_sobol.k");
		gpu_texture_t *bnoise_scramble = data_get_texture("bnoise_scramble.k");
		gpu_texture_t *bnoise_rank     = data_get_texture("bnoise_rank.k");

		render_target_t *baketex0 = any_map_get(render_path_render_targets, "baketex0");
		render_target_t *baketex1 = any_map_get(render_path_render_targets, "baketex1");

		gpu_texture_t *tex2 = NULL;
		if (g_context->bake_type == BAKE_TYPE_LIGHTMAP) {
			gpu_texture_t *_texpaint   = g_context->layer->texpaint;
			g_context->layer->texpaint = bake_texture_node_texpaint;
			slot_layer_t *flat         = layers_flatten(true, NULL);
			tex2                       = flat->texpaint;
			g_context->layer->texpaint = _texpaint;
		}
		else {
			render_target_t *texpaint_undo = any_map_get(render_path_render_targets, string("texpaint_undo%d", history_undo_i));
			if (texpaint_undo == NULL) {
				texpaint_undo = any_map_get(render_path_render_targets, "empty_black");
			}
			tex2 = texpaint_undo->_image;
		}

		gpu_texture_t *env_cdf                 = g_context->bake_type == BAKE_TYPE_LIGHTMAP ? render_path_raytrace_update_env_cdf() : NULL;
		render_path_raytrace_bake_env_sampling = env_cdf != NULL;
		gpu_raytrace_set_textures(baketex0->_image, baketex1->_image, tex2, saved_envmap, bnoise_sobol, bnoise_scramble, bnoise_rank, env_cdf);
	}

	if (g_context->brush_time > 0) {
		g_context->pdirty = 2;
	}

	if (g_context->pdirty > 0) {
		f32_array_t *f32a = render_path_raytrace_f32a;
		f32a->buffer[0]   = render_path_raytrace_frame++;
		f32a->buffer[1]   = g_context->bake_ao_strength;
		f32a->buffer[2]   = g_context->bake_ao_radius;
		f32a->buffer[3]   = g_context->bake_ao_offset;
		f32a->buffer[4]   = scene_world->strength;
		f32a->buffer[5]   = g_context->bake_up_axis;
		f32a->buffer[6]   = g_context->envmap_angle;
		f32a->buffer[7]   = render_path_raytrace_bake_env_sampling ? 1.0f : 0.0f;
		f32a->buffer[8]   = 0.0f; // Multiply by layer base

		render_target_t *framebuffer = any_map_get(render_path_render_targets, "baketex2");
		_gpu_raytrace_dispatch_rays(framebuffer->_image, f32a);

		i32   id          = g_context->layer->id;
		char *texpaint_id = string("texpaint%d", id);
		render_path_set_target(texpaint_id, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("baketex2", "tex");
		render_path_draw_shader("Scene/copy_pass/copy_pass");

#if defined(IRON_METAL) || defined(IRON_WEBGPU)
		i32 samples_per_frame = 4;
#else
		i32 samples_per_frame = 64;
#endif

		render_path_raytrace_bake_rays_pix = render_path_raytrace_frame * samples_per_frame;
		render_path_raytrace_bake_rays_counter += samples_per_frame;
		render_path_raytrace_bake_rays_timer += sys_real_delta();
		if (render_path_raytrace_bake_rays_timer >= 1) {
			render_path_raytrace_bake_rays_sec     = render_path_raytrace_bake_rays_counter;
			render_path_raytrace_bake_rays_timer   = 0;
			render_path_raytrace_bake_rays_counter = 0;
		}
		render_path_raytrace_bake_current_sample++;
		iron_delay_idle_sleep();
		return true;
	}
	else {
		render_path_raytrace_frame               = 0;
		render_path_raytrace_bake_rays_timer     = 0;
		render_path_raytrace_bake_rays_counter   = 0;
		render_path_raytrace_bake_current_sample = 0;
		return false;
	}
}

typedef struct lightmap_job {
	i32            object_uid;
	i32            res;
	i32            samples;
	f32            range;
	char          *path;
	void (*done)(void *);
	void *done_data;
} lightmap_job_t;

static lightmap_job_t **lightmap_jobs       = NULL;
static i32              lightmap_jobs_count = 0;
static lightmap_job_t  *lightmap_job        = NULL;
static i32              lightmap_frame      = 0;
static i32              lightmap_frames     = 0;
static i32              lightmap_viewport_mode;
static gpu_texture_t   *lightmap_pos     = NULL;
static gpu_texture_t   *lightmap_nor     = NULL;
static gpu_texture_t   *lightmap_base    = NULL;
static gpu_texture_t   *lightmap_target  = NULL;
static u8              *lightmap_mask    = NULL;
static f32_array_t     *lightmap_f32a    = NULL;
static gpu_texture_t   *lightmap_env     = NULL;
static gpu_texture_t   *lightmap_env_cdf = NULL;

#define LIGHTMAP_SAMPLES_PER_DISPATCH 8 // Matches SAMPLES in raytrace_bake_light.shader
#define LIGHTMAP_MARGIN               16

static vec4_t lightmap_vertex(i16 *a, i32 i, i32 stride, i32 count) {
	i16 *p = a + i * stride;
	return (vec4_t){p[0] / 32767.0f, p[1] / 32767.0f, count > 2 ? p[2] / 32767.0f : 0.0f, 0.0f};
}

// Rasterize world positions and normals of the object into its uv space
static void lightmap_raster(mesh_object_t *o, i32 res, f32 *pos, f32 *nor, u8 *mask) {
	mesh_data_t    *md   = o->data;
	vertex_array_t *vpos = mesh_data_get_vertex_array(md, "pos");
	vertex_array_t *vnor = mesh_data_get_vertex_array(md, "nor");
	vertex_array_t *vtex = mesh_data_get_vertex_array(md, "tex");
	if (vpos == NULL || vnor == NULL || vtex == NULL) {
		return;
	}
	i16         *pa  = vpos->values->buffer;
	i16         *na  = vnor->values->buffer;
	i16         *ta  = vtex->values->buffer;
	u32_array_t *ind = md->index_array;
	mat4_t       m   = o->base->transform->world_unpack;
	mat4_t       mn  = o->base->transform->world;

	for (i32 t = 0; t + 2 < ind->length; t += 3) {
		vec4_t p[3];
		vec4_t n[3];
		f32    x[3];
		f32    y[3];
		for (i32 k = 0; k < 3; ++k) {
			i32    i = ind->buffer[t + k];
			vec4_t v = lightmap_vertex(pa, i, 4, 3);
			v.w      = 1.0;
			p[k]     = vec4_apply_mat4(v, m);
			vec4_t w = lightmap_vertex(na, i, 2, 2);
			w.z      = pa[i * 4 + 3] / 32767.0f;
			w.w      = 0.0;
			n[k]     = vec4_apply_mat4(w, mn);
			x[k]     = ta[i * 2] / 32767.0f * md->scale_tex * res;
			y[k]     = ta[i * 2 + 1] / 32767.0f * md->scale_tex * res;
		}
		f32 area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
		if (fabsf(area) < 1e-12f) {
			continue;
		}
		i32 x0 = (i32)floorf(fminf(x[0], fminf(x[1], x[2])));
		i32 x1 = (i32)ceilf(fmaxf(x[0], fmaxf(x[1], x[2])));
		i32 y0 = (i32)floorf(fminf(y[0], fminf(y[1], y[2])));
		i32 y1 = (i32)ceilf(fmaxf(y[0], fmaxf(y[1], y[2])));
		x0     = x0 < 0 ? 0 : x0;
		y0     = y0 < 0 ? 0 : y0;
		x1     = x1 > res - 1 ? res - 1 : x1;
		y1     = y1 > res - 1 ? res - 1 : y1;
		for (i32 py = y0; py <= y1; ++py) {
			for (i32 px = x0; px <= x1; ++px) {
				f32 cx = px + 0.5f;
				f32 cy = py + 0.5f;
				f32 b0 = ((x[1] - cx) * (y[2] - cy) - (x[2] - cx) * (y[1] - cy)) / area;
				f32 b1 = ((x[2] - cx) * (y[0] - cy) - (x[0] - cx) * (y[2] - cy)) / area;
				f32 b2 = 1.0f - b0 - b1;
				if (b0 < -1e-4f || b1 < -1e-4f || b2 < -1e-4f) {
					continue;
				}
				i32 o4      = (py * res + px) * 4;
				pos[o4 + 0] = p[0].x * b0 + p[1].x * b1 + p[2].x * b2;
				pos[o4 + 1] = p[0].y * b0 + p[1].y * b1 + p[2].y * b2;
				pos[o4 + 2] = p[0].z * b0 + p[1].z * b1 + p[2].z * b2;
				pos[o4 + 3] = 1.0f;
				vec4_t nn   = vec4_norm(
                    (vec4_t){n[0].x * b0 + n[1].x * b1 + n[2].x * b2, n[0].y * b0 + n[1].y * b1 + n[2].y * b2, n[0].z * b0 + n[1].z * b1 + n[2].z * b2, 0.0});
				nor[o4 + 0]         = nn.x;
				nor[o4 + 1]         = nn.y;
				nor[o4 + 2]         = nn.z;
				nor[o4 + 3]         = 1.0f;
				mask[py * res + px] = 1;
			}
		}
	}
}

static void lightmap_delete_textures() {
	gpu_texture_t **ts[] = {&lightmap_pos, &lightmap_nor, &lightmap_target};
	for (i32 i = 0; i < 3; ++i) {
		if (*ts[i] != NULL) {
			gpu_delete_texture(*ts[i]);
			*ts[i] = NULL;
		}
	}
	free(lightmap_mask);
	lightmap_mask = NULL;
}

static mesh_object_t *lightmap_find_object(i32 uid) {
	for (i32 i = 0; i < scene_meshes->length; ++i) {
		mesh_object_t *o = scene_meshes->buffer[i];
		if (o->base->uid == uid) {
			return o;
		}
	}
	return NULL;
}

static void lightmap_begin(lightmap_job_t *job, mesh_object_t *object) {
	i32  res      = job->res;
	f32 *pos      = calloc((size_t)res * res * 4, sizeof(f32));
	f32 *nor      = calloc((size_t)res * res * 4, sizeof(f32));
	lightmap_mask = calloc((size_t)res * res, 1);
	lightmap_raster(object, res, pos, nor, lightmap_mask);
	lightmap_pos         = malloc(sizeof(gpu_texture_t));
	lightmap_nor         = malloc(sizeof(gpu_texture_t));
	lightmap_pos->buffer = NULL;
	lightmap_nor->buffer = NULL;
	gpu_texture_init_from_bytes(lightmap_pos, pos, res, res, GPU_TEXTURE_FORMAT_RGBA128, false);
	gpu_texture_init_from_bytes(lightmap_nor, nor, res, res, GPU_TEXTURE_FORMAT_RGBA128, false);
	free(pos);
	free(nor);
	lightmap_target = gpu_create_render_target(res, res, GPU_TEXTURE_FORMAT_RGBA64);

	if (lightmap_base == NULL) {
		// Albedo of geometry without a material, light only output
		u8 grey[4]            = {231, 231, 231, 255}; // 0.8 linear
		lightmap_base         = malloc(sizeof(gpu_texture_t));
		lightmap_base->buffer = NULL;
		gpu_texture_init_from_bytes(lightmap_base, grey, 1, 1, GPU_TEXTURE_FORMAT_RGBA32, false);
		lightmap_f32a = f32_array_create(24);
	}

	if (!g_context->envmap_loaded) {
		context_load_envmap();
		context_update_envmap();
	}
	lightmap_env     = g_context->show_envmap_blur ? scene_world->_->radiance_mipmaps->buffer[0] : g_context->saved_envmap;
	lightmap_env_cdf = render_path_raytrace_update_env_cdf();

	// Scene of all visible objects, hit surfaces use their material base color
	render_path_raytrace_ready       = true;
	render_path_raytrace_is_bake     = true;
	render_path_raytrace_init_shader = true;
	render_path_raytrace_raytrace_init(string("raytrace_bake_light%s", render_path_raytrace_ext), false);

	lightmap_frame  = 0;
	lightmap_frames = (job->samples + LIGHTMAP_SAMPLES_PER_DISPATCH - 1) / LIGHTMAP_SAMPLES_PER_DISPATCH;
	if (lightmap_frames < 1) {
		lightmap_frames = 1;
	}
}

static void lightmap_dispatch() {
	gpu_raytrace_set_textures(lightmap_pos, lightmap_nor, lightmap_base, lightmap_env, data_get_texture("bnoise_sobol.k"),
	                          data_get_texture("bnoise_scramble.k"), data_get_texture("bnoise_rank.k"), lightmap_env_cdf);
	f32 *f = lightmap_f32a->buffer;
	f[0]   = lightmap_frame;
	f[4]   = scene_world->strength;
	f[5]   = g_context->bake_up_axis;
	f[6]   = g_context->envmap_angle;
	f[7]   = lightmap_env_cdf != NULL ? 1.0f : 0.0f;
	f[8]   = 1.0f; // Light only
	_gpu_raytrace_dispatch_rays(lightmap_target, lightmap_f32a);
	lightmap_frame++;
}

static void lightmap_blur(f32 *px, u8 *m, i32 res) {
	f32 *tmp = malloc(sizeof(f32) * 4 * res * res);
	for (i32 pass = 0; pass < 2; ++pass) {
		memcpy(tmp, px, sizeof(f32) * 4 * res * res);
		for (i32 y = 0; y < res; ++y) {
			for (i32 x = 0; x < res; ++x) {
				if (!m[y * res + x]) {
					continue;
				}
				f32 r = 0.0, g = 0.0, b = 0.0;
				i32 n = 0;
				for (i32 dy = -1; dy <= 1; ++dy) {
					for (i32 dx = -1; dx <= 1; ++dx) {
						i32 sx = x + dx;
						i32 sy = y + dy;
						if (sx < 0 || sy < 0 || sx >= res || sy >= res || !m[sy * res + sx]) {
							continue;
						}
						f32 *s = tmp + (sy * res + sx) * 4;
						r += s[0];
						g += s[1];
						b += s[2];
						n++;
					}
				}
				f32 *d = px + (y * res + x) * 4;
				d[0]   = r / n;
				d[1]   = g / n;
				d[2]   = b / n;
			}
		}
	}
	free(tmp);
}

// Extend uv islands into the empty texels, then store (light / range)^(1/2.2)
static void lightmap_write(lightmap_job_t *job) {
	i32       res = job->res;
	buffer_t *b   = gpu_get_texture_pixels(lightmap_target);
	u16      *h   = (u16 *)b->buffer;
	f32      *px  = malloc(sizeof(f32) * 4 * res * res);
	for (i32 i = 0; i < res * res * 4; ++i) {
		px[i] = render_path_raytrace_half_to_float(h[i]);
	}
	u8       *m   = lightmap_mask;
	lightmap_blur(px, m, res);
	u8 *m2 = malloc((size_t)res * res);
	for (i32 pass = 0; pass < LIGHTMAP_MARGIN; ++pass) {
		memcpy(m2, m, (size_t)res * res);
		for (i32 y = 0; y < res; ++y) {
			for (i32 x = 0; x < res; ++x) {
				if (m[y * res + x]) {
					continue;
				}
				f32 r = 0.0, g = 0.0, bl = 0.0;
				i32 n = 0;
				for (i32 dy = -1; dy <= 1; ++dy) {
					for (i32 dx = -1; dx <= 1; ++dx) {
						i32 sx = x + dx;
						i32 sy = y + dy;
						if (sx < 0 || sy < 0 || sx >= res || sy >= res || !m[sy * res + sx]) {
							continue;
						}
						f32 *s = px + (sy * res + sx) * 4;
						r += s[0];
						g += s[1];
						bl += s[2];
						n++;
					}
				}
				if (n > 0) {
					f32 *d          = px + (y * res + x) * 4;
					d[0]            = r / n;
					d[1]            = g / n;
					d[2]            = bl / n;
					m2[y * res + x] = 1;
				}
			}
		}
		memcpy(m, m2, (size_t)res * res);
	}
	free(m2);

	buffer_t *out = buffer_create(res * res * 4);
	for (i32 i = 0; i < res * res; ++i) {
		for (i32 c = 0; c < 3; ++c) {
			f32 v = m[i] ? px[i * 4 + c] / job->range : 0.0f;
			v     = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
#ifdef IRON_BGRA
			i32 oc = 2 - c; // Png writer expects the render target byte order
#else
			i32 oc = c;
#endif
			out->buffer[i * 4 + oc] = (u8)(powf(v, 1.0f / 2.2f) * 255.0f + 0.5f);
		}
		out->buffer[i * 4 + 3] = 255;
	}
	if (ends_with(job->path, ".jpg")) {
		iron_write_jpg(job->path, out, res, res, 2, 95); // RGB
	}
	else {
		iron_write_png(job->path, out, res, res, 2); // RGB
	}
	free(px);
}

static void lightmap_update(void *_) {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}

	if (lightmap_job == NULL) {
		lightmap_job = lightmap_jobs[0];
		for (i32 i = 1; i < lightmap_jobs_count; ++i) {
			lightmap_jobs[i - 1] = lightmap_jobs[i];
		}
		lightmap_jobs_count--;
		mesh_object_t *object = lightmap_find_object(lightmap_job->object_uid);
		if (object != NULL) {
			lightmap_begin(lightmap_job, object);
		}
		else {
			free(lightmap_job->path);
			free(lightmap_job);
			lightmap_job = NULL;
			if (lightmap_jobs_count == 0) {
				sys_remove_update(lightmap_update);
				g_context->viewport_mode = lightmap_viewport_mode;
			}
		}
	}
	else if (lightmap_frame < lightmap_frames) {
		lightmap_dispatch();
	}
	else {
		lightmap_job_t *job = lightmap_job;
		lightmap_write(job);
		lightmap_delete_textures();
		lightmap_job               = NULL;
		render_path_raytrace_ready = false; // Rebuild for the viewport
		if (lightmap_jobs_count == 0) {
			sys_remove_update(lightmap_update);
			g_context->viewport_mode = lightmap_viewport_mode;
		}
		if (job->done != NULL) {
			job->done(job->done_data);
		}
		free(job->path);
		free(job);
	}

	if (in_use) {
		draw_begin(current, false, 0);
	}
}

void render_path_raytrace_bake_lightmap(mesh_object_t *object, i32 res, i32 samples, f32 range, char *path, void (*done)(void *), void *done_data) {
	lightmap_job_t *job = calloc(1, sizeof(lightmap_job_t));
	job->object_uid     = object->base->uid;
	job->res            = res;
	job->samples        = samples;
	job->range          = range > 0.0f ? range : 1.0f;
	job->path           = strdup(path);
	job->done           = done;
	job->done_data      = done_data;

	if (lightmap_job == NULL && lightmap_jobs_count == 0) {
		sys_notify_on_update(lightmap_update, NULL);
		lightmap_viewport_mode = g_context->viewport_mode;
		if (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE) {
			g_context->viewport_mode = VIEWPORT_MODE_LIT; // Path tracer shares the raytrace pipeline
		}
	}
	lightmap_jobs                        = realloc(lightmap_jobs, sizeof(lightmap_job_t *) * (lightmap_jobs_count + 1));
	lightmap_jobs[lightmap_jobs_count++] = job;
}
