
#include "../global.h"

buffer_t *image_to_3d_mesh_node_remove_background(buffer_t *buffer) {
	for (i32 i = 0; i < math_floor((buffer->length) / 4.0); ++i) {
		if (buffer->buffer[i * 4] <= 16 && buffer->buffer[i * 4 + 1] <= 16 && buffer->buffer[i * 4 + 2] <= 16) {
			buffer->buffer[i * 4 + 3] = 0;
		}
	}
	return buffer;
}

void image_to_3d_mesh_node_check_result(ui_node_t *node) {
	neural_node_current = node;
	iron_delay_idle_sleep();
	if (iron_exec_async_done == 1) {
		char *file = string("%s%soutput.obj", neural_node_dir(), PATH_SEP);
		if (iron_file_exists(file)) {
			// let result: gpu_texture_t = ;
			// map_set(neural_node_results, node.id, result);
			ui_nodes_hwnd->redraws  = 2;
			ui_view2d_hwnd->redraws = 2;
			project_import_mesh_box(file, true, true, false, NULL);
		}
		sys_remove_update(image_to_3d_mesh_node_check_result);
	}
}

void image_to_3d_mesh_node_button(i32 node_id) {
	ui_node_canvas_t *canvas    = ui_nodes_get_canvas(true);
	ui_node_t        *node      = ui_get_node(canvas->nodes, node_id);
	char             *node_name = string_copy(parser_material_node_name(node, NULL));
	ui_handle_t      *h         = ui_handle(node_name);
	string_array_t   *models    = any_array_create_from_raw(
        (void *[]){
            "Hunyuan3D",
        },
        1);
	i32 model = ui_combo(ui_nest(h, 0), models, tr("Model"), false, UI_ALIGN_LEFT, true);
	if (neural_node_button(node, models->buffer[model])) {
		ui_node_t     *from_node = neural_from_node(node->inputs->buffer[0], 0);
		gpu_texture_t *input     = ui_nodes_get_node_preview_image(from_node);
		if (input != NULL) {
			char *dir = neural_node_dir();

#ifdef IRON_BGRA
			buffer_t *input_buf = image_to_3d_mesh_node_remove_background(buffer_bgra_swap(gpu_get_texture_pixels(input)));
#else
			buffer_t *input_buf = image_to_3d_mesh_node_remove_background(gpu_get_texture_pixels(input));
#endif
			iron_write_png(string("%s%sinput.png", dir, PATH_SEP), input_buf, input->width, input->height, 0);

			dir                  = string_copy(string_replace_all(dir, "\\", "/"));
			string_array_t *argv = any_array_create_from_raw(
			    (void *[]){
			        string("%s/Hunyuan3D_win64/python/python.exe", dir),
			        "-s",
			        "-B",
			        "-c",
			        string("from hy3dshape.pipelines import Hunyuan3DDiTFlowMatchingPipeline; "
			               "shape_pipeline = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained('%s/Hunyuan3D_win64/Hunyuan3D-2.1'); "
			               "mesh = shape_pipeline(image='%s/input.png', octree_resolution=512, num_chunks=64000)[0]; "
			               "mesh.export('%s/output.obj')",
			               dir, dir, dir),
			        NULL,
			    },
			    6);

			iron_exec_async(argv->buffer[0], argv->buffer);
			sys_notify_on_update(image_to_3d_mesh_node_check_result, node);
		}
	}
}

void image_to_3d_mesh_node_init() {

	ui_node_t *image_to_3d_mesh_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Image to 3D Mesh"),
	                           .type   = "NEURAL_IMAGE_TO_3D_MESH",
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
	                                                                 .name          = _tr("Mesh"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = "image_to_3d_mesh_node_button",
	                                                                 .type          = "CUSTOM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 2}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_neural, image_to_3d_mesh_node_def);
	any_map_set(parser_material_node_values, "NEURAL_IMAGE_TO_3D_MESH", neural_node_value);
	any_map_set(ui_nodes_custom_buttons, "image_to_3d_mesh_node_button", image_to_3d_mesh_node_button);
}
