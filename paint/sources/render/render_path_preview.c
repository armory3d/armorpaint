
#include "../global.h"

void render_path_preview_init() {

	{
		render_target_t *t = render_target_create();
		t->name            = "texpreview";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA64";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpreview_icon";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA64";
		render_path_create_render_target(t);
	}

	i32 size = math_floor(util_render_material_preview_size * 2.0);
	if (size < util_render_decal_preview_size) {
		size = util_render_decal_preview_size;
	}

	{
		render_target_t *t = render_target_create();
		t->name            = "mmain";
		t->width           = size;
		t->height          = size;
		t->format          = "D32";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}

	{
		render_target_t *t = render_target_create();
		t->name            = "mtex";
		t->width           = size;
		t->height          = size;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}

	{
		render_target_t *t = render_target_create();
		t->name            = "mgbuffer0";
		t->width           = size;
		t->height          = size;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}

	{
		render_target_t *t = render_target_create();
		t->name            = "mgbuffer1";
		t->width           = size;
		t->height          = size;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
}

void render_path_preview_commands_preview() {
	render_path_set_target("mgbuffer0", NULL, "mmain", GPU_CLEAR_COLOR | GPU_CLEAR_DEPTH, 0xffffffff, 1.0);
	string_array_t *additional = any_array_create_from_raw(
	    (void *[]){
	        "mgbuffer1",
	    },
	    1);
	render_path_set_target("mgbuffer0", additional, "mmain", GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_meshes("mesh");

	// Deferred light
	render_path_set_target("mtex", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("mmain", "gbufferD");
	render_path_bind_target("mgbuffer0", "gbuffer0");
	render_path_bind_target("mgbuffer1", "gbuffer1");
	render_path_bind_target("empty_white", "ssaotex");
	render_path_draw_shader("Scene/deferred_light/deferred_light");

	render_path_set_target("mtex", NULL, "mmain", GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_skydome("Scene/world_pass/world_pass");

	char            *framebuffer     = "texpreview";
	slot_material_t *selected_mat    = g_context->material;
	render_target_t *texpreview      = any_map_get(render_path_render_targets, "texpreview");
	render_target_t *texpreview_icon = any_map_get(render_path_render_targets, "texpreview_icon");
	texpreview->_image               = selected_mat->image;
	texpreview_icon->_image          = selected_mat->image_icon;

	render_path_set_target(framebuffer, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("mtex", "tex");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");

	render_path_set_target("texpreview_icon", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("texpreview", "tex");
	render_path_draw_shader("Scene/supersample_resolve/supersample_resolveRGBA64");
}

void render_path_preview_commands_decal() {
	render_path_set_target("mgbuffer0", NULL, "mmain", GPU_CLEAR_COLOR | GPU_CLEAR_DEPTH, 0xffffffff, 1.0);
	string_array_t *additional = any_array_create_from_raw(
	    (void *[]){
	        "mgbuffer1",
	    },
	    1);
	render_path_set_target("mgbuffer0", additional, "mmain", GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_meshes("mesh");

	// Deferred light
	char *output = "mtex";
	render_path_set_target(output, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("mmain", "gbufferD");
	render_path_bind_target("mgbuffer0", "gbuffer0");
	render_path_bind_target("mgbuffer1", "gbuffer1");
	render_path_bind_target("empty_white", "ssaotex");
	render_path_draw_shader("Scene/deferred_light/deferred_light");

	render_path_set_target(output, NULL, "mmain", GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_skydome("Scene/world_pass/world_pass");

	char            *framebuffer = "texpreview";
	render_target_t *texpreview  = any_map_get(render_path_render_targets, "texpreview");
	texpreview->_image           = g_context->decal_image;

	render_path_set_target(framebuffer, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);

	render_path_bind_target(output, "tex");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");
}
