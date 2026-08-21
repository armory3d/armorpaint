
#include "../global.h"

void render_path_deferred_init() {

	{
		render_target_t *t = render_target_create();
		t->name            = "main";
		t->width           = 0;
		t->height          = 0;
		t->format          = "D32";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "gbuffer0";
		t->width           = 0;
		t->height          = 0;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "gbuffer1";
		t->width           = 0;
		t->height          = 0;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "gbuffer2";
		t->width           = 0;
		t->height          = 0;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "buf";
		t->width           = 0;
		t->height          = 0;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "last";
		t->width           = 0;
		t->height          = 0;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "empty_white";
		t->width           = 1;
		t->height          = 1;
		t->format          = "R8";
		buffer_t *b        = buffer_create(1);
		buffer_set_u8(b, 0, 255);
		t->_image = gpu_create_texture_from_bytes(b, t->width, t->height, GPU_TEXTURE_FORMAT_R8);
		array_free(b);
		free(b);
		any_map_set(render_path_render_targets, t->name, t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "empty_black";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		buffer_t *b        = buffer_create(4);
		buffer_set_u8(b, 0, 0);
		buffer_set_u8(b, 1, 0);
		buffer_set_u8(b, 2, 0);
		buffer_set_u8(b, 3, 0);
		t->_image = gpu_create_texture_from_bytes(b, t->width, t->height, GPU_TEXTURE_FORMAT_RGBA32);
		array_free(b);
		free(b);
		any_map_set(render_path_render_targets, t->name, t);
	}

	if (g_config->rp_ssao) {
		render_path_base_init_ssao();
	}

	render_path_load_shader("Scene/world_pass/world_pass");
	render_path_load_shader("Scene/deferred_light/deferred_light");
	render_path_load_shader("Scene/compositor_pass/compositor_pass");
	render_path_load_shader("Scene/copy_pass/copy_pass");
	render_path_load_shader("Scene/copy_pass/copyR8_pass");
	render_path_load_shader("Scene/copy_pass/copyRGBA64_pass");
	render_path_load_shader("Scene/copy_pass/copyRGBA128_pass");
	render_path_load_shader("Scene/taa_pass/taa_pass");
	render_path_load_shader("Scene/supersample_resolve/supersample_resolve");
	render_path_load_shader("Scene/supersample_resolve/supersample_resolveRGBA64");

	render_path_paint_init();
	render_path_preview_init();
	render_path_raytrace_init();
}

void render_path_deferred_draw_deferred() {
	render_path_base_draw_ssao();
	render_path_base_draw_deferred_light();
	render_path_base_draw_bloom("buf", "buf");
	// draw_histogram();

	render_path_set_target("gbuffer1", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("buf", "tex");
	// render_path_bind_target("histogram", "histogram");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");

	render_path_set_target("gbuffer1", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_compass();
	render_envsphere();
	render_path_draw_meshes("overlay");
	render_path_base_draw_taa("gbuffer1", "buf");
}

void render_path_deferred_commands() {
	render_path_paint_live_brush_dirty();
	render_path_base_commands(render_path_deferred_draw_deferred);
}
