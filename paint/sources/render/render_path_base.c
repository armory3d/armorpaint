
#include "../global.h"

f32                      render_path_base_super_sample = 1.0;
f32                      render_path_base_last_x       = -1.0;
f32                      render_path_base_last_y       = -1.0;
render_target_t_array_t *render_path_base_bloom_mipmaps;

void render_path_base_init() {
	pipes_init();
	const_data_create_screen_aligned_data();
	render_path_base_super_sample = g_config->rp_supersample;
}

void render_path_base_apply_config() {
	if (render_path_base_super_sample != g_config->rp_supersample) {
		render_path_base_super_sample = g_config->rp_supersample;
		string_array_t *keys          = map_keys(render_path_render_targets);
		for (i32 i = 0; i < keys->length; ++i) {
			render_target_t *rt = any_map_get(render_path_render_targets, keys->buffer[i]);
			if (rt->width == 0) {
				rt->scale = render_path_base_super_sample;
			}
		}
		render_path_resize();
	}
}

f32 render_path_base_get_super_sampling() {
	return render_path_base_super_sample;
}

void render_path_base_begin() {
	// Begin split
	if (g_context->split_view && !g_context->paint2d_view) {
		if (g_context->view_index_last == -1 && g_context->view_index == -1) {
			// Begin split, draw right viewport first
			g_context->view_index = 1;
		}
		else {
			// Set current viewport
			g_context->view_index = mouse_view_x() > base_w() / 2.0 ? 1 : 0;
		}

		camera_object_t *cam = scene_camera;
		if (g_context->view_index_last > -1) {
			// Save current viewport camera
			camera_views[g_context->view_index_last] = cam->base->transform->local;
		}

		bool decal = context_is_decal();

		if (g_context->view_index_last != g_context->view_index || decal) {
			// Redraw on current viewport change
			g_context->ddirty = 1;
		}

		transform_set_matrix(cam->base->transform, camera_views[g_context->view_index]);
		camera_object_build_mat(cam);
		camera_object_build_proj(cam, -1.0);
	}

	// Match projection matrix jitter
	bool skip_taa = g_context->split_view || (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE && g_context->tool != TOOL_TYPE_COLORID) ||
	                g_context->camera_type == CAMERA_TYPE_ORTHOGRAPHIC ||
	                ((g_context->tool == TOOL_TYPE_CLONE || g_context->tool == TOOL_TYPE_BLUR) && g_context->pdirty > 0);

	if (skip_taa || g_config->brush_live) {
		scene_camera->frame = 0;
	}

	camera_object_proj_jitter(scene_camera);

	if (skip_taa) {
		scene_camera->p = scene_camera->no_jitter_p;
	}

	camera_object_build_mat(scene_camera);
}

void render_path_base_end() {
	// End split
	g_context->view_index_last = g_context->view_index;
	g_context->view_index      = -1;

	if (g_context->foreground_event && !mouse_down("left")) {
		g_context->foreground_event = false;
		g_context->pdirty           = 0;
	}
}

bool render_path_base_ssaa4() {
	return g_config->rp_supersample == 4;
}

bool render_path_base_is_cached() {
	if (iron_window_width() == 0 || iron_window_height() == 0) {
		return true;
	}

	f32 mx                  = render_path_base_last_x;
	f32 my                  = render_path_base_last_y;
	render_path_base_last_x = mouse_view_x();
	render_path_base_last_y = mouse_view_y();

	if (g_context->ddirty <= 0 && g_context->rdirty <= 0 && g_context->pdirty <= 0) {
		if (mx != render_path_base_last_x || my != render_path_base_last_y || iron_mouse_is_locked()) {
			g_context->ddirty = 0;
		}

		if (g_context->ddirty > -6) {
			// Accumulate taa frames
			g_context->ddirty--;
			return false;
		}

		if (g_context->ddirty > -12) {
			render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("last", "tex");
			if (render_path_base_ssaa4()) {
				render_path_draw_shader("Scene/supersample_resolve/supersample_resolveRGBA64");
			}
			else {
				render_path_draw_shader("Scene/copy_pass/copy_pass");
			}
			render_path_paint_commands_cursor();
			g_context->ddirty--;
		}

		render_path_base_end();
		return true;
	}
	return false;
}

void render_path_base_draw_split(void (*draw_commands)(void)) {
	if (g_context->split_view && !g_context->paint2d_view) {
		g_context->ddirty    = 2;
		camera_object_t *cam = scene_camera;

		g_context->view_index = g_context->view_index == 0 ? 1 : 0;
		transform_set_matrix(cam->base->transform, camera_views[g_context->view_index]);
		camera_object_build_mat(cam);
		camera_object_build_proj(cam, -1.0);

		render_path_base_draw_gbuffer();

		bool use_live_layer = g_context->tool == TOOL_TYPE_MATERIAL;
		bool path_trace     = g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE && g_context->tool != TOOL_TYPE_COLORID;
        path_trace ? render_path_raytrace_draw(use_live_layer) : draw_commands();

		g_context->view_index = g_context->view_index == 0 ? 1 : 0;
		transform_set_matrix(cam->base->transform, camera_views[g_context->view_index]);
		camera_object_build_mat(cam);
		camera_object_build_proj(cam, -1.0);
	}
}

void render_path_base_commands(void (*draw_commands)(void)) {
	if (render_path_base_is_cached()) {
		return;
	}

	render_path_base_begin();
	render_path_paint_begin();
	render_path_base_draw_split(draw_commands);
	render_path_base_draw_gbuffer();
	render_path_paint_draw();

	if (g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE && g_context->tool != TOOL_TYPE_COLORID) {
		bool use_live_layer = g_context->tool == TOOL_TYPE_MATERIAL;
		render_path_raytrace_draw(use_live_layer);
		g_context->last_paint_vec_x = g_context->paint_vec.x;
		g_context->last_paint_vec_y = g_context->paint_vec.y;
		g_context->foreground_event = false;
		render_path_base_end();
		return;
	}

	draw_commands();
	render_path_paint_commands_cursor();
	render_path_paint_end();
	render_path_base_end();
}

void render_path_base_draw_bloom(char *source, char *target) {
	if (g_config->rp_bloom == 0.0) {
		return;
	}

	if (render_path_base_bloom_mipmaps == NULL) {
		gc_unroot(render_path_base_bloom_mipmaps);
		render_path_base_bloom_mipmaps = any_array_create_from_raw((void *[]){}, 0);
		gc_root(render_path_base_bloom_mipmaps);

		f32 prev_scale = 1.0;
		for (i32 i = 0; i < 10; ++i) {
			render_target_t *t = render_target_create();
			t->name            = string("bloom_mip_%d", i);
			t->width           = 0;
			t->height          = 0;
			prev_scale *= 0.5;
			t->scale  = prev_scale;
			t->format = "RGBA64";
			any_array_push(render_path_base_bloom_mipmaps, render_path_create_render_target(t));
		}

		render_path_load_shader("Scene/bloom_pass/bloom_downsample_pass");
		render_path_load_shader("Scene/bloom_pass/bloom_upsample_pass");
	}

	f32 bloom_radius                    = 6.5;
	f32 min_dim                         = math_min(render_path_current_w, render_path_current_h);
	f32 log_min_dim                     = math_max(1.0, math_log2(min_dim) + (bloom_radius - 8.0));
	i32 num_mips                        = math_floor(log_min_dim);
	render_path_base_bloom_sample_scale = 0.5 + log_min_dim - num_mips;

	for (i32 i = 0; i < num_mips; ++i) {
		render_path_base_bloom_current_mip = i;
		render_path_set_target(render_path_base_bloom_mipmaps->buffer[i]->name, NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_bind_target(i == 0 ? source : render_path_base_bloom_mipmaps->buffer[i - 1]->name, "tex");
		render_path_draw_shader("Scene/bloom_pass/bloom_downsample_pass");
	}

	for (i32 i = 0; i < num_mips; ++i) {
		i32 mip_level                      = num_mips - 1 - i;
		render_path_base_bloom_current_mip = mip_level;
		render_path_set_target(mip_level == 0 ? target : render_path_base_bloom_mipmaps->buffer[mip_level - 1]->name, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(render_path_base_bloom_mipmaps->buffer[mip_level]->name, "tex");
		render_path_draw_shader("Scene/bloom_pass/bloom_upsample_pass");
	}
}

void render_path_base_init_ssao() {
#if defined(IRON_MACOS) || defined(IRON_IOS) || defined(IRON_ANDROID)
	f32 scale = 0.5;
#else
	f32 scale = 1.0;
#endif

	{
		render_target_t *t = render_target_create();
		t->name            = "singlea";
		t->width           = 0;
		t->height          = 0;
		t->format          = "R8";
		t->scale           = scale * render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}

	{
		render_target_t *t = render_target_create();
		t->name            = "singleb";
		t->width           = 0;
		t->height          = 0;
		t->format          = "R8";
		t->scale           = scale * render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}

	render_path_load_shader("Scene/ssao_pass/ssao_pass");
	render_path_load_shader("Scene/ssao_blur_pass/ssao_blur_pass_x");
	render_path_load_shader("Scene/ssao_blur_pass/ssao_blur_pass_y");
}

void render_path_base_draw_ssao() {
	bool ssao = g_config->rp_ssao != false && g_context->camera_type == CAMERA_TYPE_PERSPECTIVE;
	if (ssao && g_context->ddirty > -6 && _render_path_frame > 0) {
		if (any_map_get(render_path_render_targets, "singlea") == NULL) {
			render_path_base_init_ssao();
		}

		render_path_set_target("singlea", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("main", "gbufferD");
		render_path_bind_target("gbuffer0", "gbuffer0");
		render_path_draw_shader("Scene/ssao_pass/ssao_pass");

		render_path_set_target("singleb", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("singlea", "tex");
		render_path_bind_target("gbuffer0", "gbuffer0");
		render_path_draw_shader("Scene/ssao_blur_pass/ssao_blur_pass_x");

		render_path_set_target("singlea", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("singleb", "tex");
		render_path_bind_target("gbuffer0", "gbuffer0");
		render_path_draw_shader("Scene/ssao_blur_pass/ssao_blur_pass_y");
	}
}

void render_path_base_draw_deferred_light() {
	render_path_set_target("buf", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("main", "gbufferD");
	render_path_bind_target("gbuffer0", "gbuffer0");
	render_path_bind_target("gbuffer1", "gbuffer1");
	bool ssao = g_config->rp_ssao != false && g_context->camera_type == CAMERA_TYPE_PERSPECTIVE;
	if (ssao && _render_path_frame > 0) {
		render_path_bind_target("singlea", "ssaotex");
	}
	else {
		render_path_bind_target("empty_white", "ssaotex");
	}

	render_path_draw_shader("Scene/deferred_light/deferred_light");

	render_path_set_target("buf", NULL, "main", GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_skydome("Scene/world_pass/world_pass");
}

// function render_path_base_draw_histogram() {
// 	{
// 		let t: render_target_t = RenderTarget.create();
// 		t.name = "histogram";
// 		t.width = 1;
// 		t.height = 1;
// 		t.format = "RGBA64";
// 		render_path_create_render_target(t);
// 		render_path_load_shader("Scene/histogram_pass/histogram_pass");
// 	}
// 	render_path_set_target("histogram");
// 	render_path_bind_target("last", "tex");
// 	render_path_draw_shader("Scene/histogram_pass/histogram_pass");
// }

void render_path_base_draw_taa(char *bufa, char *bufb) {
	render_path_set_target(bufb, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target(bufa, "tex");

	bool skip_taa = g_context->split_view;
	if (skip_taa) {
		render_path_draw_shader("Scene/copy_pass/copyRGBA64_pass");
	}
	else {
		render_path_bind_target("last", "tex2");
		render_path_draw_shader("Scene/taa_pass/taa_pass");
	}

	render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target(bufb, "tex");
	if (render_path_base_ssaa4()) {
		render_path_draw_shader("Scene/supersample_resolve/supersample_resolveRGBA64");
	}
	else {
		render_path_draw_shader("Scene/copy_pass/copy_pass");
	}

	render_path_base_swap_buf(bufa);
}

void render_path_base_swap_buf(char *bufa) {
	// Swap buf and last targets
	render_target_t *last_target = any_map_get(render_path_render_targets, "last");
	last_target->name            = string_copy(bufa);
	render_target_t *buf_target  = any_map_get(render_path_render_targets, bufa);
	buf_target->name             = "last";
	any_map_set(render_path_render_targets, bufa, last_target);
	any_map_set(render_path_render_targets, "last", buf_target);
	render_path_base_buf_swapped = !render_path_base_buf_swapped;
}

void render_path_base_make_gbuffer_copy_textures() {
	render_target_t *copy = any_map_get(render_path_render_targets, "gbuffer0_copy");
	render_target_t *g0   = any_map_get(render_path_render_targets, "gbuffer0");
	if (copy == NULL || copy->_image->width != g0->_image->width || copy->_image->height != g0->_image->height) {
		{
			render_target_t *t = render_target_create();
			t->name            = "gbuffer0_copy";
			t->width           = 0;
			t->height          = 0;
			t->format          = "RGBA64";
			t->scale           = render_path_base_get_super_sampling();
			render_path_create_render_target(t);
		}
		{
			render_target_t *t = render_target_create();
			t->name            = "gbuffer1_copy";
			t->width           = 0;
			t->height          = 0;
			t->format          = "RGBA64";
			t->scale           = render_path_base_get_super_sampling();
			render_path_create_render_target(t);
		}
		{
			render_target_t *t = render_target_create();
			t->name            = "gbuffer2_copy";
			t->width           = 0;
			t->height          = 0;
			t->format          = "RGBA64";
			t->scale           = render_path_base_get_super_sampling();
			render_path_create_render_target(t);
		}
	}
}

void render_path_base_copy_to_gbuffer() {
	string_array_t *additional = any_array_create_from_raw(
	    (void *[]){
	        "gbuffer1",
	        "gbuffer2",
	    },
	    2);
	render_path_set_target("gbuffer0", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("gbuffer0_copy", "tex0");
	render_path_bind_target("gbuffer1_copy", "tex1");
	render_path_bind_target("gbuffer2_copy", "tex2");
	render_path_draw_shader("Scene/copy_mrt3_pass/copy_mrt3RGBA64_pass");
}

void render_path_base_draw_gbuffer() {

	if (make_material_transluc_used) {
		render_path_set_target("gbuffer0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_set_target("gbuffer1", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	}

	render_path_set_target("gbuffer0", NULL, "main", GPU_CLEAR_DEPTH, 0, 1.0); // Only clear gbuffer0
	string_array_t *additional = any_array_create_from_raw(
	    (void *[]){
	        "gbuffer1",
	        "gbuffer2",
	    },
	    2);
	render_path_set_target("gbuffer0", additional, "main", GPU_CLEAR_NONE, 0, 0.0);
	render_path_paint_bind_layers();
	render_path_draw_meshes("mesh");
	render_path_paint_unbind_layers();
	if (make_mesh_layer_pass_count > 1) {
		render_path_base_make_gbuffer_copy_textures();
		for (i32 i = 1; i < make_mesh_layer_pass_count; ++i) {
			char *ping = i % 2 == 1 ? "_copy" : "";
			char *pong = i % 2 == 1 ? "" : "_copy";
			if (i == make_mesh_layer_pass_count - 1) {
				render_path_set_target(string("gbuffer2%s", ping), NULL, NULL, GPU_CLEAR_COLOR, 0xff000000, 0.0);
			}
			char           *g1ping     = string("gbuffer1%s", ping);
			char           *g2ping     = string("gbuffer2%s", ping);
			string_array_t *additional = any_array_create_from_raw(
			    (void *[]){
			        g1ping,
			        g2ping,
			    },
			    2);
			render_path_set_target(string("gbuffer0%s", ping), additional, "main", GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target(string("gbuffer0%s", pong), "gbuffer0");
			render_path_bind_target(string("gbuffer1%s", pong), "gbuffer1");
			render_path_bind_target(string("gbuffer2%s", pong), "gbuffer2");
			render_path_paint_bind_layers();
			render_path_draw_meshes(string("mesh%d", i));
			render_path_paint_unbind_layers();
		}
		if (make_mesh_layer_pass_count % 2 == 0) {
			render_path_base_copy_to_gbuffer();
		}
	}

	if (make_material_transluc_used) {
		render_path_set_target("gbuffer0", NULL, "main", GPU_CLEAR_NONE, 0, 1.0);
		render_path_draw_meshes("depth");
	}

	bool hide     = operator_shortcut(any_map_get(g_keymap, "stencil_hide"), SHORTCUT_TYPE_DOWN) || keyboard_down("control");
	bool is_decal = base_is_decal_layer();
	if (is_decal && !hide) {
		line_draw_color            = 0xff000000;
		line_draw_strength         = 0.002;
		string_array_t *additional = any_array_create_from_raw(
		    (void *[]){
		        "gbuffer1",
		    },
		    1);
		render_path_set_target("gbuffer0", additional, "main", GPU_CLEAR_NONE, 0, 0.0);
		line_draw_render(g_context->layer->decal_mat);
		render_path_end();
	}
}
