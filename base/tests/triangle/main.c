// Red triangle test
// ../../make --run

#include <iron.h>

gpu_pipeline_t *pipeline;
gpu_buffer_t   *vb;
gpu_buffer_t   *ib;

void render() {
	_gpu_begin(NULL, NULL, NULL, GPU_CLEAR_COLOR | GPU_CLEAR_DEPTH, 0xff000000, 1.0);
	gpu_set_pipeline(pipeline);
	gpu_set_vertex_buffer(vb);
	gpu_set_index_buffer(ib);
	gpu_draw();
	gpu_end();
}

void _kickstart() {
	iron_window_options_t *ops =
	    ALLOC_INIT(iron_window_options_t, {.title     = "Iron",
	                                          .width     = 640,
	                                          .height    = 480,
	                                          .x         = -1,
	                                          .y         = -1,
	                                          .mode      = IRON_WINDOW_MODE_WINDOW,
	                                          .features  = IRON_WINDOW_FEATURES_RESIZABLE | IRON_WINDOW_FEATURES_MINIMIZABLE | IRON_WINDOW_FEATURES_MAXIMIZABLE,
	                                          .vsync     = true,
	                                          .frequency = 60,
	                                          .depth_bits = 32});
	_iron_init(ops);

	pipeline = gpu_create_pipeline();

	gpu_vertex_structure_t *vs = ALLOC_INIT(gpu_vertex_structure_t, {0});
	gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_3X);
	buffer_t     *vs_buffer         = iron_load_blob(string("./data/test.vert%s", sys_shader_ext()));
	buffer_t     *fs_buffer         = iron_load_blob(string("./data/test.frag%s", sys_shader_ext()));
	gpu_shader_t *vert              = gpu_create_shader(vs_buffer, GPU_SHADER_TYPE_VERTEX);
	gpu_shader_t *frag              = gpu_create_shader(fs_buffer, GPU_SHADER_TYPE_FRAGMENT);
	pipeline->vertex_shader         = vert;
	pipeline->fragment_shader       = frag;
	pipeline->input_layout          = vs;
	pipeline->depth_attachment_bits = 32;
	gpu_pipeline_compile(pipeline);

	f32_array_t *vertices = f32_array_create_from_raw(
	    (f32[]){
	        -1.0,
	        -1.0,
	        0.0,
	        1.0,
	        -1.0,
	        0.0,
	        0.0,
	        1.0,
	        0.0,
	    },
	    9);

	i32_array_t *indices = i32_array_create_from_raw(
	    (i32[]){
	        0,
	        1,
	        2,
	    },
	    3);

	vb = gpu_create_vertex_buffer(vertices->length / (float)3, vs->elements);

	float *vb_data = gpu_vertex_buffer_lock(vb);
	for (i32 i = 0; i < vertices->length; i++) {
		vb_data[i] = vertices->buffer[i];
	}
	gpu_vertex_buffer_unlock(vb);

	ib = gpu_create_index_buffer(indices->length);

	uint32_t *ib_data = gpu_index_buffer_lock(ib);
	for (i32 i = 0; i < indices->length; i++) {
		ib_data[i] = indices->buffer[i];
	}
	gpu_index_buffer_unlock(ib);

	_iron_set_update_callback(render);

	iron_start();
}

////

any_map_t *ui_children;
any_map_t *ui_nodes_custom_buttons;
i32        pipes_offset;
char      *strings_check_internet_connection() {
    return "";
}
void  console_error(char *s) {}
void  console_info(char *s) {}
char *tr(char *id) {
	return id;
}
i32 pipes_get_constant_location(char *s) {
	return 0;
}
