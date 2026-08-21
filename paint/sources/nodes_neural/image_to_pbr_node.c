
#include "../global.h"

gpu_texture_t *image_to_pbr_node_result_base      = NULL;
gpu_texture_t *image_to_pbr_node_result_normal    = NULL;
gpu_texture_t *image_to_pbr_node_result_occlusion = NULL;
gpu_texture_t *image_to_pbr_node_result_height    = NULL;
gpu_texture_t *image_to_pbr_node_result_roughness = NULL;
i32            image_to_pbr_node_node_id          = -1;

char *image_to_pbr_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	gpu_texture_t *result = NULL;
	if (socket == node->outputs->buffer[0]) { // base color
		result = image_to_pbr_node_result_base;
	}
	else if (socket == node->outputs->buffer[3]) { // normal map
		result = image_to_pbr_node_result_normal;
	}

	if (result == NULL) {
		return "float3(0.0, 0.0, 0.0)";
	}
	char *tex_name = string("%s%s", parser_material_node_name(node, NULL), i32_to_string(socket->id));
	any_map_set(data_cached_textures, tex_name, result);
	bind_tex_t *tex      = parser_material_make_bind_tex(tex_name, tex_name);
	char       *texstore = parser_material_texture_store(node, tex, tex_name, COLOR_SPACE_AUTO);
	return string("%s.rgb", texstore);
}

char *image_to_pbr_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	gpu_texture_t *result = NULL;
	if (socket == node->outputs->buffer[1]) { // occlusion
		result = image_to_pbr_node_result_occlusion;
	}
	else if (socket == node->outputs->buffer[2]) { // roughness
		result = image_to_pbr_node_result_roughness;
	}
	else if (socket == node->outputs->buffer[4]) { // height
		result = image_to_pbr_node_result_height;
	}

	if (result == NULL) {
		return "0.0";
	}

	char *tex_name = string("%s%s", parser_material_node_name(node, NULL), i32_to_string(socket->id));
	any_map_set(data_cached_textures, tex_name, result);
	bind_tex_t *tex      = parser_material_make_bind_tex(tex_name, tex_name);
	char       *texstore = parser_material_texture_store(node, tex, tex_name, COLOR_SPACE_AUTO);
	return string("%s.r", texstore);
}

void image_to_pbr_node_check_result(void (*done)(gpu_texture_t *)) {
	iron_delay_idle_sleep();
	if (iron_exec_async_done == 1) {
		char *dir  = neural_node_dir();
		char *file = string("%s%soutput.png", dir, PATH_SEP);
		if (iron_file_exists(file)) {
			gpu_texture_t *tex = iron_load_texture(file);
			done(tex);
		}
		sys_remove_update(image_to_pbr_node_check_result);
	}
}

void image_to_pbr_node_run_da3(bool tile, int width, int height, void (*done)(gpu_texture_t *)) {
	char           *dir  = neural_node_dir();
	string_array_t *argv = any_array_create_from_raw(
	    (void *[]){
	        string("%s/%s", dir, neural_node_iris_bin()),
	        "-d",
	        string("%s", dir),
	        "--depth",
	        "-W",
	        string("%d", width),
	        "-H",
	        string("%d", height),
	        "-i",
	        string("%s/input.png", dir),
	        "-o",
	        string("%s/output.png", dir),
	    },
	    12);
	if (tile) {
		string_array_push(argv, "--tile");
	}
	string_array_push(argv, NULL);

	iron_exec_async(argv->buffer[0], argv->buffer);
	sys_notify_on_update(image_to_pbr_node_check_result, done);
}

void image_to_pbr_node_all_done(void *_) {
	int              res_w = image_to_pbr_node_result_height->width;
	int              res_h = image_to_pbr_node_result_height->height;
	render_target_t *occmap;
	{
		render_target_t *t = render_target_create();
		t->name            = "occmap";
		t->width           = res_w;
		t->height          = res_h;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
		occmap = t;
	}
	render_target_t *normmap;
	{
		render_target_t *t = render_target_create();
		t->name            = "normmap";
		t->width           = res_w;
		t->height          = res_h;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
		normmap = t;
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "_height_map";
		t->width           = res_w;
		t->height          = res_h;
		t->format          = "RGBA32";
		t->_image          = image_to_pbr_node_result_height;
		any_map_set(render_path_render_targets, t->name, t);
	}
	render_target_t *normal_map_rt;
	{
		render_target_t *t = render_target_create();
		t->name            = "_normal_map";
		t->width           = res_w;
		t->height          = res_h;
		t->format          = "RGBA32";
		t->_image          = image_to_pbr_node_result_normal;
		any_map_set(render_path_render_targets, t->name, t);
		normal_map_rt = t;
	}

	render_path_load_shader("Scene/depth_to_normal_pass/depth_to_normal_pass");
	render_path_load_shader("Scene/depth_to_ao_pass/depth_to_ao_pass");
	render_path_load_shader("Scene/ssao_blur_pass/ssao_blur_pass_x");
	render_path_load_shader("Scene/ssao_blur_pass/ssao_blur_pass_y");

	// Normal map
	render_path_set_target("normmap", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("_height_map", "height_map");
	render_path_draw_shader("Scene/depth_to_normal_pass/depth_to_normal_pass");

	image_to_pbr_node_result_normal = normmap->_image;
	normal_map_rt->_image = normmap->_image;

	// Occlusion
	render_path_set_target("occmap", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("_height_map", "height_map");
	render_path_bind_target("_normal_map", "normal_map");
	render_path_draw_shader("Scene/depth_to_ao_pass/depth_to_ao_pass");

	image_to_pbr_node_result_occlusion = occmap->_image;

	// Base color
	char          *dir       = neural_node_dir();
	gpu_texture_t *input_tex = iron_load_texture(string("%s%sinput.png", dir, PATH_SEP));
	buffer_t      *in_px     = gpu_get_texture_pixels(input_tex);
	buffer_t      *occ_px    = gpu_get_texture_pixels(occmap->_image);

	int bw = occmap->_image->width;
	int bh = occmap->_image->height;

	const float linear_light_fac = 0.3f;
	const float subtract_fac     = 0.3f;
	const float bright           = 0.1f;
	const float contr            = 0.0f;
	const float bc_a             = 1.0f + contr;
	const float bc_b             = bright - contr * 0.5f;

	buffer_t *base_px = buffer_create(bw * bh * 4);
	for (int y = 0; y < bh; ++y) {
		for (int x = 0; x < bw; ++x) {
			int bi = (y * bw + x) * 4;

			float occ     = occ_px->buffer[bi] / 255.0f;
			float inv_occ = 1.0f - occ;
			for (int c = 0; c < 3; ++c) {
				float col = in_px->buffer[bi + c] / 255.0f;
				col += linear_light_fac * (2.0f * (inv_occ - 0.5f));
				col -= subtract_fac * inv_occ;
				col = bc_a * col + bc_b;
				if (col < 0.0f)
					col = 0.0f;
				if (col > 1.0f)
					col = 1.0f;
				base_px->buffer[bi + c] = (int)(col * 255.0f);
			}
			base_px->buffer[bi + 3] = 255;
		}
	}
	gpu_texture_t *base = gpu_create_texture_from_bytes(base_px, bw, bh, GPU_TEXTURE_FORMAT_RGBA32);

	image_to_pbr_node_result_base = base;

	// Roughness
	buffer_t *rough_px = buffer_create(bw * bh * 4);
	for (int y = 0; y < bh; ++y) {
		for (int x = 0; x < bw; ++x) {
			int   bi  = (y * bw + x) * 4;
			float lum = (base_px->buffer[bi] * 0.299f + base_px->buffer[bi + 1] * 0.587f + base_px->buffer[bi + 2] * 0.114f) / 255.0f;

			float detail = 0.0f;
			int   count  = 0;
			for (int oy = -1; oy <= 1; ++oy) {
				for (int ox = -1; ox <= 1; ++ox) {
					int sx = x + ox;
					int sy = y + oy;
					if (sx < 0 || sx >= bw || sy < 0 || sy >= bh) {
						continue;
					}
					int   si   = (sy * bw + sx) * 4;
					float slum = (base_px->buffer[si] * 0.299f + base_px->buffer[si + 1] * 0.587f + base_px->buffer[si + 2] * 0.114f) / 255.0f;
					float d    = slum - lum;
					detail += d < 0.0f ? -d : d;
					count += 1;
				}
			}
			detail = count > 0 ? detail / count : 0.0f;

			float rough = 0.5f + detail * 4.0f;
			if (rough > 1.0f) {
				rough = 1.0f;
			}
			int v                    = (int)(rough * 255.0f);
			rough_px->buffer[bi]     = v;
			rough_px->buffer[bi + 1] = v;
			rough_px->buffer[bi + 2] = v;
			rough_px->buffer[bi + 3] = 255;
		}
	}
	gpu_texture_t *rough = gpu_create_texture_from_bytes(rough_px, bw, bh, GPU_TEXTURE_FORMAT_RGBA32);

	image_to_pbr_node_result_roughness = rough;

	ui_node_canvas_t *canvas = ui_nodes_get_canvas(true);
	ui_node_t        *node   = ui_get_node(canvas->nodes, image_to_pbr_node_node_id);
	if (node != NULL) {
		any_imap_set(neural_node_results, node->outputs->buffer[0]->id, image_to_pbr_node_result_base);
		any_imap_set(neural_node_results, node->outputs->buffer[1]->id, image_to_pbr_node_result_occlusion);
		any_imap_set(neural_node_results, node->outputs->buffer[2]->id, image_to_pbr_node_result_roughness);
		any_imap_set(neural_node_results, node->outputs->buffer[3]->id, image_to_pbr_node_result_normal);
		any_imap_set(neural_node_results, node->outputs->buffer[4]->id, image_to_pbr_node_result_height);
	}
}

void image_to_pbr_node_depth_done(gpu_texture_t *tex) {
	image_to_pbr_node_result_height = tex;
	sys_notify_on_next_frame(&image_to_pbr_node_all_done, NULL);
}

void image_to_pbr_node_button(i32 node_id) {
	ui_node_canvas_t *canvas    = ui_nodes_get_canvas(true);
	ui_node_t        *node      = ui_get_node(canvas->nodes, node_id);
	char             *node_name = string_copy(parser_material_node_name(node, NULL));
	ui_handle_t      *h         = ui_handle(node_name);

	string_array_t *models = any_array_create_from_raw(
	    (void *[]){
	        "DA3MONO",
	    },
	    1);
	i32 model = ui_combo(ui_nest(h, 0), models, tr("Model"), false, UI_ALIGN_LEFT, true);

	if (neural_node_button(node, models->buffer[model])) {
		ui_node_t     *from_node = neural_from_node(node->inputs->buffer[0], 0);
		gpu_texture_t *input     = ui_nodes_get_node_preview_image(from_node);
		if (input != NULL) {
			char *dir = neural_node_dir();

#ifdef IRON_BGRA
			buffer_t *input_buf = buffer_bgra_swap(gpu_get_texture_pixels(input)); // Vulkan non-rt textures need a flip
#else
			buffer_t *input_buf = gpu_get_texture_pixels(input);
#endif
			iron_write_png(string("%s%sinput.png", dir, PATH_SEP), input_buf, input->width, input->height, 0);

			bool tile                 = node->buttons->buffer[1]->default_value->buffer[0] > 0.0;
			image_to_pbr_node_node_id = node_id;
			image_to_pbr_node_run_da3(tile, input->width, input->height, &image_to_pbr_node_depth_done);
		}
	}
}

void image_to_pbr_node_init() {

	ui_node_t *image_to_pbr_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Image to PBR"),
	                              .type   = "NEURAL_IMAGE_TO_PBR",
	                              .x      = 0,
	                              .y      = 0,
	                              .color  = 0xff4982a0,
	                              .inputs = any_array_create_from_raw(
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
	                              .outputs = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Base Color"),
	                                                                       .type          = "RGBA",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Occlusion"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Roughness"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Normal Map"),
	                                                                       .type          = "VECTOR",
	                                                                       .color         = 0xffc7c729,
	                                                                       .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                      ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                       .node_id       = 0,
	                                                                       .name          = _tr("Height"),
	                                                                       .type          = "VALUE",
	                                                                       .color         = 0xffa1a1a1,
	                                                                       .default_value = f32_array_create_x(1.0),
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .display       = 0}),
	                                  },
	                                  5),
	                              .buttons = any_array_create_from_raw(
	                                  (void *[]){
	                                      ALLOC_INIT(ui_node_button_t, {.name          = "image_to_pbr_node_button",
	                                                                       .type          = "CUSTOM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 2}),
	                                      ALLOC_INIT(ui_node_button_t, {.name          = _tr("Tile"),
	                                                                       .type          = "BOOL",
	                                                                       .output        = 0,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 0}),
	                                  },
	                                  2),
	                              .width = 0,
	                              .flags = 0});

	any_array_push(nodes_material_neural, image_to_pbr_node_def);
	any_map_set(parser_material_node_vectors, "NEURAL_IMAGE_TO_PBR", image_to_pbr_node_vector);
	any_map_set(parser_material_node_values, "NEURAL_IMAGE_TO_PBR", image_to_pbr_node_value);
	any_map_set(ui_nodes_custom_buttons, "image_to_pbr_node_button", image_to_pbr_node_button);
}
