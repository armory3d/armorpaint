
#include "../global.h"

void upscale_image_node_button(i32 node_id) {
	ui_node_canvas_t *canvas    = ui_nodes_get_canvas(true);
	ui_node_t        *node      = ui_get_node(canvas->nodes, node_id);
	char             *node_name = string_copy(parser_material_node_name(node, NULL));
	ui_handle_t      *h         = ui_handle(node_name);
	string_array_t   *models    = any_array_create_from_raw(
        (void *[]){
            "Real-ESRGAN",
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

			string_array_t *argv = any_array_create_from_raw(
			    (void *[]){
			        string("%s/%s", dir, neural_node_iris_bin()),
			        "-d",
			        string("%s", dir),
			        "--upscale",
			        "-i",
			        string("%s/input.png", dir),
			        "-o",
			        string("%s/output.png", dir),
			    },
			    8);

			if (node->buttons->buffer[1]->default_value->buffer[0] > 0.0) {
				string_array_push(argv, "--tile");
			}
			string_array_push(argv, NULL);

			iron_exec_async(argv->buffer[0], argv->buffer);
			sys_notify_on_update(neural_node_check_result, node);
		}
	}
}

void upscale_image_node_init() {

	ui_node_t *upscale_image_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Upscale Image"),
	                              .type   = "NEURAL_UPSCALE_IMAGE",
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
	                                      ALLOC_INIT(ui_node_button_t, {.name          = "upscale_image_node_button",
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

	any_array_push(nodes_material_neural, upscale_image_node_def);
	any_map_set(parser_material_node_vectors, "NEURAL_UPSCALE_IMAGE", neural_node_vector);
	any_map_set(ui_nodes_custom_buttons, "upscale_image_node_button", upscale_image_node_button);
}
