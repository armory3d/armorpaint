
#include "../global.h"

void save_image_node_run(ui_node_t *node, gpu_texture_t *result) {
	if (string_equals(g_project->_->filepath, "")) {
		return;
	}

	char *dir = substring(g_project->_->filepath, 0, string_last_index_of(g_project->_->filepath, PATH_SEP) + 1);
	i32   n   = 1;
	char *file;
	do {
		file = string("%soutput%03d.png", dir, n++);
	} while (iron_file_exists(file));

#ifdef IRON_BGRA
	buffer_t *buf = buffer_bgra_swap(gpu_get_texture_pixels(result));
#else
	buffer_t *buf = gpu_get_texture_pixels(result);
#endif

	iron_write_png(file, buf, result->width, result->height, 0);
}

void save_image_node_init() {

	ui_node_t *save_image_node_def = ALLOC_INIT(ui_node_t, {.id     = 0,
	                                                        .name   = _tr("Save Image"),
	                                                        .type   = "NEURAL_SAVE_IMAGE",
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
	                                                        .outputs = any_array_create_from_raw((void *[]){}, 0),
	                                                        .buttons = any_array_create_from_raw((void *[]){}, 0),
	                                                        .width   = 0,
	                                                        .flags   = 0});

	any_array_push(nodes_material_neural, save_image_node_def);
}
