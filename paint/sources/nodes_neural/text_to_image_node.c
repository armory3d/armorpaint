
#include "../global.h"

string_array_t *text_to_image_node_flux_klein_args(char *dir, char *prompt) {
	string_array_t *argv = any_array_create_from_raw(
	    (void *[]){
	        string("%s/%s", dir, neural_node_iris_bin()),
	        "-d",
	        string("%s", dir),
	    },
	    3);
	return argv;
}

void text_to_image_node_run(ui_node_t *node, void (*callback)(ui_node_t *)) {
	char        *node_name = string_copy(parser_material_node_name(node, NULL));
	ui_handle_t *h         = ui_handle(node_name);
	i32          model     = ui_nest(h, 0)->i;
	char        *prompt    = ui_nest(h, 1)->text;
	char        *dir       = neural_node_dir();

	string_array_t *argv;
	if (model == 0) {
		argv = text_to_image_node_flux_klein_args(dir, prompt);
	}

	if (g_config->neural_res >= 2048) {
		string_array_push(argv, "--vae-tiling");
	}
	string_array_push(argv, "-W");
	string_array_push(argv, string("%d", g_config->neural_res));
	string_array_push(argv, "-H");
	string_array_push(argv, string("%d", g_config->neural_res));
	string_array_push(argv, "--seed");
	string_array_push(argv, "-1");
	string_array_push(argv, "-o");
	string_array_push(argv, string("%s/output.png", dir));
	string_array_push(argv, "-p");
	string_array_push(argv, string("'%s'", prompt));
	if (node->buttons->buffer[1]->default_value->buffer[0] > 0.0) {
		string_array_push(argv, "--tile");
	}
	string_array_push(argv, NULL);

	iron_exec_async(argv->buffer[0], argv->buffer);
	sys_notify_on_update(callback, node);
}

void text_to_image_node_button(i32 node_id) {
	ui_node_t      *node      = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	char           *node_name = string_copy(parser_material_node_name(node, NULL));
	ui_handle_t    *h         = ui_handle(node_name);
	string_array_t *models    = any_array_create_from_raw(
        (void *[]){
            "FLUX 2 klein",
        },
        1);
	i32   model                      = ui_combo(ui_nest(h, 0), models, tr("Model"), false, UI_ALIGN_LEFT, true);
	char *prompt                     = ui_text_area(ui_nest(h, 1), UI_ALIGN_LEFT, true, tr("prompt"), true);
	node->buttons->buffer[0]->height = string_split(prompt, "\n")->length + 2;

	if (neural_node_button(node, models->buffer[model])) {
		text_to_image_node_run(node, neural_node_check_result);
	}
}

void text_to_image_node_init() {

	ui_node_t *text_to_image_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                              .name   = _tr("Text to Image"),
	                              .type   = "NEURAL_TEXT_TO_IMAGE",
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
	                                      ALLOC_INIT(ui_node_button_t, {.name          = "text_to_image_node_button",
	                                                                       .type          = "CUSTOM",
	                                                                       .output        = -1,
	                                                                       .default_value = f32_array_create_x(0),
	                                                                       .data          = NULL,
	                                                                       .min           = 0.0,
	                                                                       .max           = 1.0,
	                                                                       .precision     = 100,
	                                                                       .height        = 1}),
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

	any_array_push(nodes_material_neural, text_to_image_node_def);
	any_map_set(parser_material_node_vectors, "NEURAL_TEXT_TO_IMAGE", neural_node_vector);
	any_map_set(ui_nodes_custom_buttons, "text_to_image_node_button", text_to_image_node_button);
}
