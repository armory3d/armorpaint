
#include "../global.h"

void render_path_lit_draw() {
	render_path_base_draw_ssao();
	render_path_base_draw_deferred_light();
	render_path_base_draw_bloom("buf", "buf");
	// draw_histogram();

	render_path_set_target("gbuffer1", NULL, NULL, GPU_CLEAR_DONT_LOAD_COLOR, 0, 0.0);
	render_path_bind_target("buf", "tex");
	// render_path_bind_target("histogram", "histogram");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");
	render_pass_draw(RENDER_PASS_POST, "gbuffer1");

	if (!args_player) {
		render_path_set_target("gbuffer1", NULL, "main", GPU_CLEAR_NONE, 0, 0.0);
		render_grid();
		render_path_set_target("gbuffer1", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_compass();
		render_envsphere();
		render_pathsphere_tool();
		render_gizmo_draw();
	}
	render_path_base_draw_taa("gbuffer1", "buf");
}

void render_path_lit_commands() {
	render_path_base_commands(render_path_lit_draw);
}
