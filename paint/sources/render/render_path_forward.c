
#include "../global.h"

void render_path_forward_init() {}

void render_path_forward_draw_forward() {
	render_path_set_target("gbuffer1", NULL, "main", GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_skydome("Scene/world_pass/world_pass");

	render_path_set_target("buf", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("gbuffer1", "tex");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");

	render_path_set_target("buf", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_compass();
	render_envsphere();
	render_path_draw_meshes("overlay");

	render_path_base_draw_taa("buf", "gbuffer1");
}

void render_path_forward_commands() {
	render_path_base_commands(render_path_forward_draw_forward);
}
