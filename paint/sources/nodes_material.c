
#include "global.h"

void nodes_material_init() {
	if (nodes_material_list != NULL) {
		return;
	}

	nodes_material_input = any_array_create_from_raw((void *[]){}, 0);
	attribute_node_init();
	rgb_node_init(); // color_node_init
	geometry_node_init();
	layer_node_init();
	layer_mask_node_init();
	material_node_init();
	object_info_node_init();
	picker_node_init();
	script_node_init();
	shader_node_init();
	texture_coordinate_node_init();
	tilesheet_animation_node_init();
	tilesheet_node_init();
	uv_map_node_init();
	value_node_init();
	wireframe_node_init();

	nodes_material_texture = any_array_create_from_raw((void *[]){}, 0);
	bake_texture_node_init();
	brick_texture_node_init();
	camera_texture_node_init();
	checker_texture_node_init();
	curvature_bake_node_init();
	gabor_texture_node_init();
	gradient_texture_node_init();
	image_texture_node_init();
	magic_texture_node_init();
	noise_texture_node_init();
	text_texture_node_init();
	voronoi_texture_node_init();
	wave_texture_node_init();

	nodes_material_color = any_array_create_from_raw((void *[]){}, 0);
	blur_node_init();
	brightness_contrast_node_init();
	color_mask_node_init();
	color_ramp_node_init();
	combine_color_node_init();
	gamma_node_init();
	hue_saturation_value_node_init();
	invert_color_node_init();
	mix_color_node_init();
	quantize_node_init();
	rgb_curves_node_init();
	rgb_to_bw_node_init();
	replace_color_node_init();
	separate_color_node_init();
	warp_node_init();

	nodes_material_utilities = any_array_create_from_raw((void *[]){}, 0);
	bump_node_init();
	clamp_node_init();
	combine_xyz_node_init();
	float_curve_node_init();
	map_range_node_init();
	mapping_node_init();
	math2_node_init();
	mix_normal_map_node_init();
	normal_node_init();
	normal_map_node_init();
	separate_xyz_node_init();
	vector_curves_node_init();
	vector_math2_node_init();
	vector_rotate_node_init();
	vector_transform_node_init();

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)

	nodes_material_neural = any_array_create_from_raw((void *[]){}, 0);
	edit_image_node_init();
#ifdef IRON_WINDOWS
	image_to_3d_mesh_node_init();
#endif
	image_to_pbr_node_init();
	repeat_node_init();
	save_image_node_init();
	text_to_image_node_init();
	if (g_config->experimental) {
		texture_mesh_node_init();
	}
	upscale_image_node_init();

#endif

	nodes_material_group = any_array_create_from_raw((void *[]){}, 0);
	group_node_init();

#if defined(IRON_WINDOWS) || defined(IRON_LINUX) || defined(IRON_MACOS)

	nodes_material_list = any_array_create_from_raw(
	    (void *[]){
	        nodes_material_input,
	        nodes_material_texture,
	        nodes_material_color,
	        nodes_material_utilities,
	        nodes_material_neural,
	        nodes_material_group,
	    },
	    6);

#else

	nodes_material_list = any_array_create_from_raw(
	    (void *[]){
	        nodes_material_input,
	        nodes_material_texture,
	        nodes_material_color,
	        nodes_material_utilities,
	        nodes_material_group,
	    },
	    5);

#endif
}

ui_node_t *nodes_material_get_node_t(char *node_type) {
	for (i32 i = 0; i < nodes_material_list->length; ++i) {
		ui_node_t_array_t *c = nodes_material_list->buffer[i];
		for (i32 i = 0; i < c->length; ++i) {
			ui_node_t *n = c->buffer[i];
			if (string_equals(n->type, node_type)) {
				return n;
			}
		}
	}
	return NULL;
}

ui_node_t *nodes_material_create_node(char *node_type, node_group_t *group) {
	ui_node_t *n = nodes_material_get_node_t(node_type);
	if (n == NULL) {
		return NULL;
	}
	ui_node_canvas_t *canvas = group != NULL ? group->canvas : ui_nodes_get_canvas(false);
	ui_nodes_t       *nodes  = group != NULL ? group->nodes : g_context->material->nodes;
	ui_node_t        *node   = ui_nodes_make_node(n, nodes, canvas);
	any_array_push(canvas->nodes, node);
	return node;
}
