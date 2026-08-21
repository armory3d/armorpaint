
#include "../global.h"

#define TEXTURE_MESH_NODE_NUM_VIEWS 6
#define TEXTURE_MESH_NODE_GRID_COLS 3
#define TEXTURE_MESH_NODE_GRID_ROWS 2
#define TEXTURE_MESH_NODE_CELL      512
#define TEXTURE_MESH_NODE_GRID_W    (TEXTURE_MESH_NODE_CELL * TEXTURE_MESH_NODE_GRID_COLS)
#define TEXTURE_MESH_NODE_GRID_H    (TEXTURE_MESH_NODE_CELL * TEXTURE_MESH_NODE_GRID_ROWS)

static ui_node_t *texture_mesh_node_current = NULL;
static i32        texture_mesh_node_step    = 0;
static i32        texture_mesh_node_model   = 0;

static void texture_mesh_node_run_sd(ui_node_t *node);
static void texture_mesh_node_project(ui_node_t *node);

static void texture_mesh_node_set_view(i32 view) {
	f32 h = math_pi() / 2.0;
	f32 p = math_pi();
	switch (view) {
	case 0:
		viewport_set_view(0, 0, -1, p, 0, p);
		break; // bottom
	case 1:
		viewport_set_view(0, 0, 1, 0, 0, 0);
		break; // top
	case 2:
		viewport_set_view(0, 1, 0, h, 0, p);
		break; // back
	case 3:
		viewport_set_view(-1, 0, 0, h, 0, -h);
		break; // left
	case 4:
		viewport_set_view(1, 0, 0, h, 0, h);
		break; // right
	case 5:
		viewport_set_view(0, -1, 0, h, 0, 0);
		break; // front
	}
}

static void texture_mesh_node_restore_camera() {
	camera_reset(g_context->view_index_last);
}

static void texture_mesh_node_grab_view(i32 view_idx) {
	char            *dir = neural_node_dir();
	render_target_t *rt  = any_map_get(render_path_render_targets, "last");
	gpu_texture_t   *tex = rt->_image;

	iron_write_png(string("%s%sinput_%d.png", dir, PATH_SEP, view_idx), buffer_half_to_u8(gpu_get_texture_pixels(tex)), tex->width, tex->height, 0);
}

static void texture_mesh_node_capture(void *_) {
	if (texture_mesh_node_step == 0) {
		texture_mesh_node_set_view(0);
		texture_mesh_node_step = 1;
		sys_notify_on_next_frame(texture_mesh_node_capture, NULL);
		return;
	}

	i32 view = texture_mesh_node_step - 1;
	texture_mesh_node_grab_view(view);

	if (texture_mesh_node_step < TEXTURE_MESH_NODE_NUM_VIEWS) {
		texture_mesh_node_set_view(texture_mesh_node_step);
		texture_mesh_node_step++;
		sys_notify_on_next_frame(texture_mesh_node_capture, NULL);
	}
	else {
		texture_mesh_node_restore_camera();
		texture_mesh_node_run_sd(texture_mesh_node_current);
	}
}

static void texture_mesh_node_build_grid(char *dir) {
	gpu_texture_t *grid = gpu_create_render_target(TEXTURE_MESH_NODE_GRID_W, TEXTURE_MESH_NODE_GRID_H, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(grid, true, 0xff000000);
	for (i32 v = 0; v < TEXTURE_MESH_NODE_NUM_VIEWS; v++) {
		char          *input = string("%s%sinput_%d.png", dir, PATH_SEP, v);
		gpu_texture_t *tex   = iron_load_texture(input);
		f32            dx    = (v % TEXTURE_MESH_NODE_GRID_COLS) * TEXTURE_MESH_NODE_CELL;
		f32            dy    = (v / TEXTURE_MESH_NODE_GRID_COLS) * TEXTURE_MESH_NODE_CELL;
		draw_scaled_image(tex, dx, dy, TEXTURE_MESH_NODE_CELL, TEXTURE_MESH_NODE_CELL);
	}
	draw_end();
	iron_write_png(string("%s%sinput_grid.png", dir, PATH_SEP), gpu_get_texture_pixels(grid), grid->width, grid->height, 0);
}

static void texture_mesh_node_split_grid(char *dir) {
	char          *grid_path = string("%s%soutput_grid.png", dir, PATH_SEP);
	gpu_texture_t *grid      = iron_load_texture(grid_path);
	f32            cw        = grid->width / (f32)TEXTURE_MESH_NODE_GRID_COLS;
	f32            ch        = grid->height / (f32)TEXTURE_MESH_NODE_GRID_ROWS;
	for (i32 v = 0; v < TEXTURE_MESH_NODE_NUM_VIEWS; v++) {
		f32            sx   = (v % TEXTURE_MESH_NODE_GRID_COLS) * cw;
		f32            sy   = (v / TEXTURE_MESH_NODE_GRID_COLS) * ch;
		gpu_texture_t *cell = gpu_create_render_target((i32)cw, (i32)ch, GPU_TEXTURE_FORMAT_RGBA32);
		draw_begin(cell, false, 0);
		draw_scaled_sub_image(grid, sx, sy, cw, ch, 0, 0, cw, ch);
		draw_end();
		iron_write_png(string("%s%soutput_%d.png", dir, PATH_SEP, v), gpu_get_texture_pixels(cell), cell->width, cell->height, 0);
	}
}

static char *texture_mesh_node_grid_prompt(char *prompt) {
	return string("%s Keep the grid layout unchanged.", prompt, TEXTURE_MESH_NODE_GRID_COLS, TEXTURE_MESH_NODE_GRID_ROWS, TEXTURE_MESH_NODE_NUM_VIEWS);
}

static string_array_t *texture_mesh_node_flux_klein_args(char *dir, char *prompt) {
	any_array_t *argv = any_array_create(0);
	any_array_push(argv, string("%s/%s", dir, neural_node_iris_bin()));
	any_array_push(argv, "--diffusion-model");
	any_array_push(argv, string("%s/flux-2-klein-4b-Q8_0.gguf", dir));
	any_array_push(argv, "--vae");
	any_array_push(argv, string("%s/flux_ae.safetensors", dir));
	any_array_push(argv, "--llm");
	any_array_push(argv, string("%s/Qwen3-4B-Q8_0.gguf", dir));
	any_array_push(argv, "--cfg-scale");
	any_array_push(argv, "1.0");
	any_array_push(argv, "--sampling-method");
	any_array_push(argv, "euler");
	any_array_push(argv, "--diffusion-fa");
	any_array_push(argv, "--offload-to-cpu");
	any_array_push(argv, "--steps");
	any_array_push(argv, "4");
	any_array_push(argv, "-s");
	any_array_push(argv, "-1");
	any_array_push(argv, "-W");
	any_array_push(argv, string("%d", TEXTURE_MESH_NODE_GRID_W));
	any_array_push(argv, "-H");
	any_array_push(argv, string("%d", TEXTURE_MESH_NODE_GRID_H));
	any_array_push(argv, "-r");
	any_array_push(argv, string("%s%sinput_grid.png", dir, PATH_SEP));
	any_array_push(argv, "-p");
	any_array_push(argv, texture_mesh_node_grid_prompt(prompt));
	any_array_push(argv, "-o");
	any_array_push(argv, string("%s%soutput_grid.png", dir, PATH_SEP));
	any_array_push(argv, NULL);
	return argv;
}

static string_array_t *texture_mesh_node_qwen_args(char *dir, char *prompt) {
	any_array_t *argv = any_array_create(0);
	any_array_push(argv, string("%s/%s", dir, neural_node_iris_bin()));
	any_array_push(argv, "--diffusion-model");
	any_array_push(argv, string("%s/qwen-image-edit-2511-Q4_K_S.gguf", dir));
	any_array_push(argv, "--vae");
	any_array_push(argv, string("%s/Qwen_Image-VAE.safetensors", dir));
	any_array_push(argv, "--llm");
	any_array_push(argv, string("%s/Qwen2.5-VL-7B-Instruct-Q4_K_S.gguf", dir));
	any_array_push(argv, "--llm_vision");
	any_array_push(argv, string("%s/mmproj-F16.gguf", dir));
	any_array_push(argv, "--offload-to-cpu");
	any_array_push(argv, "--diffusion-fa");
	any_array_push(argv, "--steps");
	any_array_push(argv, "30");
	any_array_push(argv, "-s");
	any_array_push(argv, "-1");
	any_array_push(argv, "--cfg-scale");
	any_array_push(argv, "2.5");
	any_array_push(argv, "--flow-shift");
	any_array_push(argv, "3");
	any_array_push(argv, "--qwen-image-zero-cond-t");
	any_array_push(argv, "-W");
	any_array_push(argv, string("%d", TEXTURE_MESH_NODE_GRID_W / 2));
	any_array_push(argv, "-H");
	any_array_push(argv, string("%d", TEXTURE_MESH_NODE_GRID_H / 2));
	any_array_push(argv, "-p");
	any_array_push(argv, texture_mesh_node_grid_prompt(prompt));
	any_array_push(argv, "-r");
	any_array_push(argv, string("%s%sinput_grid.png", dir, PATH_SEP));
	any_array_push(argv, "-o");
	any_array_push(argv, string("%s%soutput_grid.png", dir, PATH_SEP));
	any_array_push(argv, NULL);
	return argv;
}

static void texture_mesh_node_run_upscale(char *dir) {
	char           *grid = string("%s%soutput_grid.png", dir, PATH_SEP);
	string_array_t *argv = any_array_create_from_raw(
	    (void *[]){
	        string("%s/%s", dir, neural_node_iris_bin()),
	        "-M",
	        "upscale",
	        "--upscale-model",
	        string("%s/RealESRGAN_x4plus.pth", dir),
	        "-i",
	        grid,
	        "-o",
	        grid,
	        NULL,
	    },
	    10);
	iron_exec_async(argv->buffer[0], argv->buffer);
}

static void texture_mesh_node_check_upscale(ui_node_t *node) {
	neural_node_current = node;
	iron_delay_idle_sleep();
	if (iron_exec_async_done == 1) {
		sys_remove_update(texture_mesh_node_check_upscale);
		texture_mesh_node_split_grid(neural_node_dir());
		texture_mesh_node_project(node);
	}
}

static void texture_mesh_node_check_sd(ui_node_t *node) {
	neural_node_current = node;
	iron_delay_idle_sleep();
	if (iron_exec_async_done == 1) {
		sys_remove_update(texture_mesh_node_check_sd);
		texture_mesh_node_run_upscale(neural_node_dir());
		sys_notify_on_update(texture_mesh_node_check_upscale, node);
	}
}

static void texture_mesh_node_run_sd(ui_node_t *node) {
	char        *node_name = string_copy(parser_material_node_name(node, NULL));
	ui_handle_t *h         = ui_handle(node_name);
	char        *prompt    = ui_nest(h, 1)->text;
	char        *dir       = neural_node_dir();

	if (prompt == NULL || string_equals(prompt, "")) {
		prompt = ".";
	}

	texture_mesh_node_build_grid(dir);

	string_array_t *argv = texture_mesh_node_model == 0 ? texture_mesh_node_qwen_args(dir, prompt) : texture_mesh_node_flux_klein_args(dir, prompt);
	iron_exec_async(argv->buffer[0], argv->buffer);
	sys_notify_on_update(texture_mesh_node_check_sd, node);
}

static void texture_mesh_node_project(ui_node_t *node) {
	char *dir       = neural_node_dir();
	char *node_name = string_copy(parser_material_node_name(node, NULL));

	// Save state
	tool_type_t _tool          = g_context->tool;
	f32         _brush_radius  = g_context->brush_radius;
	f32         _brush_opacity = g_context->brush_opacity;
	f32         _brush_angle   = g_context->brush_angle;
	f32         _brush_scale   = g_context->brush_scale;
	f32         _brush_scale_x = g_context->brush_scale_x;

	// Project a camera-aligned decal
	g_context->tool               = TOOL_TYPE_DECAL;
	g_context->brush_camera_align = true;
	g_context->brush_opacity      = 1.0;
	g_context->brush_angle        = 0.0;
	g_context->brush_scale        = 1.0;
	g_context->brush_scale_x      = (f32)sys_w() / (f32)sys_h();
	g_context->brush_directional  = false;
	g_context->decal_x            = 0.5;
	g_context->decal_y            = 0.5;

	// viewport_update_camera_type(CAMERA_TYPE_ORTHOGRAPHIC);

	for (i32 v = 0; v < TEXTURE_MESH_NODE_NUM_VIEWS; v++) {
		char *output = string("%s%soutput_%d.png", dir, PATH_SEP, v);
		if (!iron_file_exists(output)) {
			continue;
		}

		gpu_texture_t *sd_tex = iron_load_texture(output);
		any_imap_set(neural_node_results, node->outputs->buffer[0]->id, sd_tex);
		any_map_set(data_cached_textures, node_name, sd_tex);

		texture_mesh_node_set_view(v);
		camera_object_build_mat(scene_camera);
		render_path_base_draw_gbuffer();

		mat4_t vp               = scene_camera->vp;
		vec4_t clip_c           = vec4_apply_mat4((vec4_t){0.0, 0.0, 0.0, 1.0}, vp);
		vec4_t cam_up           = camera_object_up_world(scene_camera);
		vec4_t up_clip          = vec4_apply_mat4((vec4_t){cam_up.x, cam_up.y, cam_up.z, 0.0}, vp);
		f32    m                = math_abs(up_clip.y) > 0.00001 ? clip_c.w / up_clip.y : 1.0;
		f32    scale2d          = (900.0 / (f32)base_h()) * g_config->window_scale;
		g_context->brush_radius = (math_abs(m) * 15.0) / (scale2d * 2.0);

		make_material_parse_paint_material(false);

		g_context->paint_vec.x      = 0.5;
		g_context->paint_vec.y      = 0.5;
		g_context->last_paint_vec_x = 0.5;
		g_context->last_paint_vec_y = 0.5;
		g_context->pdirty           = 1;
		render_path_paint_commands_paint(false);
	}

	render_path_paint_dilate(true, true);

	// Restore
	texture_mesh_node_restore_camera();
	camera_object_build_mat(scene_camera);

	g_context->pdirty             = 0;
	g_context->tool               = _tool;
	g_context->brush_camera_align = false;
	g_context->brush_radius       = _brush_radius;
	g_context->brush_opacity      = _brush_opacity;
	g_context->brush_angle        = _brush_angle;
	g_context->brush_scale        = _brush_scale;
	g_context->brush_scale_x      = _brush_scale_x;

	make_material_parse_paint_material(false);
	render_path_base_draw_gbuffer();

	g_context->rtdirty      = 1;
	g_context->rdirty       = 2;
	ui_nodes_hwnd->redraws  = 2;
	ui_view2d_hwnd->redraws = 2;

	texture_mesh_node_step    = 0;
	texture_mesh_node_current = NULL;
}

void texture_mesh_node_button(i32 node_id) {
	ui_node_t      *node      = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	char           *node_name = string_copy(parser_material_node_name(node, NULL));
	ui_handle_t    *h         = ui_handle(node_name);
	string_array_t *models    = any_array_create_from_raw(
        (void *[]){
            "Qwen Image Edit",
            "FLUX 2 klein",
        },
        2);
	i32   model                      = ui_combo(ui_nest(h, 0), models, tr("Model"), false, UI_ALIGN_LEFT, true);
	char *prompt                     = ui_text_area(ui_nest(h, 1), UI_ALIGN_LEFT, true, tr("prompt"), true);
	node->buttons->buffer[0]->height = string_split(prompt, "\n")->length + 2;

	if (neural_node_button(node, models->buffer[model])) {
		texture_mesh_node_model         = model;
		g_context->capturing_screenshot = true;
		texture_mesh_node_step          = 0;

		texture_mesh_node_current = node;

		sys_notify_on_next_frame(texture_mesh_node_capture, NULL);
	}
}

void texture_mesh_node_init() {

	ui_node_t *texture_mesh_node_def = ALLOC_INIT(ui_node_t, {.id     = 0,
	                                                          .name   = _tr("Texture Mesh"),
	                                                          .type   = "NEURAL_TEXTURE_MESH",
	                                                          .x      = 0,
	                                                          .y      = 0,
	                                                          .color  = 0xff4982a0,
	                                                          .inputs = any_array_create_from_raw(
	                                                              (void *[]){
	                                                                  ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                .node_id       = 0,
	                                                                                                .name          = _tr("In"),
	                                                                                                .type          = "BOOL",
	                                                                                                .color         = 0xff6363c7,
	                                                                                                .default_value = f32_array_create_xyz(0.0, 0.0, 0.0),
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .display       = 0}),
	                                                              },
	                                                              1),
	                                                          .outputs = any_array_create_from_raw(
	                                                              (void *[]){
	                                                                  ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                                                .node_id       = 0,
	                                                                                                .name          = _tr("Color"),
	                                                                                                .type          = "RGBA",
	                                                                                                .color         = 0xffc7c729,
	                                                                                                .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .display       = 0}),
	                                                              },
	                                                              1),
	                                                          .buttons = any_array_create_from_raw(
	                                                              (void *[]){
	                                                                  ALLOC_INIT(ui_node_button_t, {.name          = "texture_mesh_node_button",
	                                                                                                .type          = "CUSTOM",
	                                                                                                .output        = -1,
	                                                                                                .default_value = f32_array_create_x(0),
	                                                                                                .data          = NULL,
	                                                                                                .min           = 0.0,
	                                                                                                .max           = 1.0,
	                                                                                                .precision     = 100,
	                                                                                                .height        = 1}),
	                                                              },
	                                                              1),
	                                                          .width = 0,
	                                                          .flags = 0});

	any_array_push(nodes_material_neural, texture_mesh_node_def);
	any_map_set(parser_material_node_vectors, "NEURAL_TEXTURE_MESH", neural_node_vector);
	any_map_set(ui_nodes_custom_buttons, "texture_mesh_node_button", texture_mesh_node_button);
}
