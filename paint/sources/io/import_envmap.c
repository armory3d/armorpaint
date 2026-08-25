
#include "../global.h"

gpu_pipeline_t        *import_envmap_pipeline = NULL;
i32                    import_envmap_params_loc;
i32                    import_envmap_radiance_loc;
gpu_texture_t         *import_envmap_radiance = NULL;
i32                    import_envmap_noise_loc;
gpu_texture_t_array_t *import_envmap_mips   = NULL;
vec4_t                 import_envmap_params = (vec4_t){0.0, 0.0, 0.0, 1.0};

void import_envmap_get_radiance_mip(gpu_texture_t *mip, i32 level, gpu_texture_t *radiance) {
#ifdef IRON_METAL
	i32 pass_count         = 8; // 32;
	import_envmap_params.y = 512;
#else
	i32 pass_count         = 1;
	import_envmap_params.y = 1024 * 16;
#endif
	import_envmap_params.z = 1.0 / (float)pass_count;
	import_envmap_params.x = (level + 1) / 10.0;

	for (i32 i = 0; i < pass_count; ++i) {
		_gpu_begin(mip, NULL, NULL, i == 0 ? GPU_CLEAR_COLOR : GPU_CLEAR_NONE, 0x00000000, 0.0);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_set_pipeline(import_envmap_pipeline);
		import_envmap_params.w = i;
		gpu_set_float4(import_envmap_params_loc, import_envmap_params.x, import_envmap_params.y, import_envmap_params.z, import_envmap_params.w);
		gpu_set_texture(import_envmap_radiance_loc, radiance);
		gpu_texture_t *noise = data_get_texture("bnoise256.k");
		gpu_set_texture(import_envmap_noise_loc, noise);
		gpu_draw();
		gpu_end();
	}
}

f32_array_t *import_envmap_get_spherical_harmonics(buffer_t *source, i32 source_width, i32 source_height) {
	f32_array_t *sh = f32_array_create(9 * 3 + 1); // Align to mult of 4 - 27->28
	for (i32 i = 0; i < sh->length; ++i) {
		sh->buffer[i] = 0.0f;
	}

	f32 dtheta = math_pi() * 2.0f / (f32)source_width;
	f32 dphi   = math_pi() / (f32)source_height;

	for (i32 y = 0; y < source_height; ++y) {
		f32 phi     = (y + 0.5f) * dphi;
		f32 sin_phi = math_sin(phi);
		f32 cos_phi = math_cos(phi);
		f32 domega  = sin_phi * dtheta * dphi;

		for (i32 x = 0; x < source_width; ++x) {
			f32 theta = (x + 0.5f) * dtheta - math_pi();
			f32 nx    = sin_phi * math_cos(theta);
			f32 ny    = -(sin_phi * math_sin(theta));
			f32 nz    = cos_phi;

			// Y00, Y1-1, Y10, Y11, Y2-2, Y2-1, Y20, Y21, Y22
			f32 basis[9];
			basis[0] = 0.282095f;
			basis[1] = 0.488603f * ny;
			basis[2] = 0.488603f * nz;
			basis[3] = 0.488603f * nx;
			basis[4] = 1.092548f * nx * ny;
			basis[5] = 1.092548f * ny * nz;
			basis[6] = 0.315392f * (3.0f * nz * nz - 1.0f);
			basis[7] = 1.092548f * nx * nz;
			basis[8] = 0.546274f * (nx * nx - ny * ny);

			for (i32 i = 0; i < 3; ++i) {
				f32 value = buffer_get_f16(source, ((x + y * source_width) * 8 + i * 2)) * domega;
				for (i32 j = 0; j < 9; ++j) {
					sh->buffer[j * 3 + i] += value * basis[j];
				}
			}
		}
	}

	return sh;
}

void import_envmap_run(char *path, gpu_texture_t *image) {
	// Init
	if (import_envmap_pipeline == NULL) {
		import_envmap_pipeline                    = gpu_create_pipeline();
		import_envmap_pipeline->vertex_shader     = sys_get_shader("prefilter_envmap.vert");
		import_envmap_pipeline->fragment_shader   = sys_get_shader("prefilter_envmap.frag");
		import_envmap_pipeline->blend_source      = GPU_BLEND_SOURCE_ALPHA;
		import_envmap_pipeline->blend_destination = GPU_BLEND_ONE;
		gpu_vertex_structure_t *vs                = ALLOC_INIT(gpu_vertex_structure_t, {0});
		gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_2X);
		import_envmap_pipeline->input_layout           = vs;
		import_envmap_pipeline->color_attachment_count = 1;
		import_envmap_pipeline->color_attachment[0]    = GPU_TEXTURE_FORMAT_RGBA64;

		gpu_pipeline_compile(import_envmap_pipeline);
		import_envmap_params_loc   = 0;
		import_envmap_radiance_loc = 0;
		import_envmap_noise_loc    = 1;

		import_envmap_radiance = gpu_create_render_target(1024, 512, GPU_TEXTURE_FORMAT_RGBA64);

		import_envmap_mips = any_array_create_from_raw((void *[]){}, 0);
		i32 w              = 512;
		for (i32 i = 0; i < 5; ++i) {
			any_array_push(import_envmap_mips, gpu_create_render_target(w, w > 1 ? math_floor(w / 2.0) : 1, GPU_TEXTURE_FORMAT_RGBA64));
			w = math_floor(w / 2.0);
		}
	}

	// Down-scale to 1024x512
	draw_begin(import_envmap_radiance, false, 0);
	draw_set_pipeline(pipes_copy64);
	draw_scaled_image(image, 0, 0, 1024, 512);
	draw_set_pipeline(NULL);
	draw_end();

	// Radiance
	for (i32 i = 0; i < import_envmap_mips->length; ++i) {
		import_envmap_get_radiance_mip(import_envmap_mips->buffer[i], i, import_envmap_radiance);
	}

	// Irradiance
	buffer_t *radiance_pixels  = gpu_get_texture_pixels(import_envmap_radiance);
	scene_world->_->irradiance = import_envmap_get_spherical_harmonics(radiance_pixels, import_envmap_radiance->width, import_envmap_radiance->height);

	// World
	scene_world->strength            = 1.0;
	scene_world->radiance_mipmaps    = import_envmap_mips->length - 2;
	scene_world->_->envmap           = image;
	scene_world->envmap              = string_copy(path);
	scene_world->_->radiance         = import_envmap_radiance;
	scene_world->_->radiance_mipmaps = import_envmap_mips;
	g_context->saved_envmap          = image;
	g_context->show_envmap           = true;
	if (g_context->show_envmap_blur) {
		scene_world->_->envmap = scene_world->_->radiance_mipmaps->buffer[0];
	}
	g_context->ddirty = 2;
	g_project->envmap = string_copy(path);
}
