
#include "../global.h"

node_shader_context_t *make_depth_run(material_t *data, material_context_t *matcon) {
	shader_context_t      *props     = ALLOC_INIT(shader_context_t, {.name            = "depth",
	                                                                    .depth_write     = true,
	                                                                    .compare_mode    = "less",
	                                                                    .cull_mode       = g_context->cull_backfaces ? "clockwise" : "none",
	                                                                    .vertex_elements = any_array_create_from_raw(
                                                                   (void *[]){
                                                                       ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
                                                                       ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
                                                                       ALLOC_INIT(vertex_element_t, {.name = "tex", .data = "short2norm"}),
                                                                   },
                                                                   3),
	                                                                    .color_attachments = any_array_create_from_raw(
                                                                   (void *[]){
                                                                       "RGBA64",
                                                                   },
                                                                   1),
	                                                                    .depth_attachment = "D32"});
	node_shader_context_t *con_depth = node_shader_context_create(data, props);

	node_shader_t *kong  = node_shader_context_make_kong(con_depth);
	kong->frag_wposition = true;

	node_shader_add_constant(kong, "VP: float4x4", "_view_proj_matrix");

	node_shader_write_vert(kong, "output.pos = constants.VP * float4(output.wposition.xyz, 1.0);");

	// {
	// 	slot_layer_t_array_t *sculpt_layers  = any_array_create_from_raw((void *[]){}, 0);
	// 	i32_array_t          *sculpt_indices = i32_array_create(0);
	// 	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
	// 		slot_layer_t *l = g_project->_->layers->buffer[i];
	// 		if (l->texpaint_sculpt != NULL && slot_layer_is_visible(l)) {
	// 			any_array_push(sculpt_layers, l);
	// 			i32_array_push(sculpt_indices, i);
	// 		}
	// 	}
	// 	sculpt_make_mesh_run(kong, sculpt_layers, sculpt_indices);
	// }

	con_depth->data->color_writes_red = u8_array_create_from_raw(
	    (u8[]){
	        false,
	    },
	    1);

	con_depth->data->color_writes_green = u8_array_create_from_raw(
	    (u8[]){
	        false,
	    },
	    1);

	con_depth->data->color_writes_blue = u8_array_create_from_raw(
	    (u8[]){
	        false,
	    },
	    1);

	con_depth->data->color_writes_alpha = u8_array_create_from_raw(
	    (u8[]){
	        false,
	    },
	    1);

	parser_material_finalize(con_depth);

	con_depth->data->shader_from_source = true;
	gpu_create_shaders_from_kong(node_shader_get(kong), &con_depth->data->vertex_shader, &con_depth->data->fragment_shader,
	                             &con_depth->data->_->vertex_shader_size, &con_depth->data->_->fragment_shader_size);
	return con_depth;
}
