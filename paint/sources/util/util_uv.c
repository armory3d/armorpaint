
#include "../global.h"

buffer_t       *util_uv_dilate_bytes = NULL;
gpu_pipeline_t *util_uv_pipe_dilate  = NULL;

void util_uv_cache_uv_map() {
	if (util_uv_uvmap != NULL && (util_uv_uvmap->width != config_get_texture_res_x() || util_uv_uvmap->height != config_get_texture_res_y())) {
		gpu_delete_texture(util_uv_uvmap);
		util_uv_uvmap        = NULL;
		util_uv_uvmap_cached = false;
	}

	if (util_uv_uvmap_cached) {
		return;
	}

	i32 res_x = config_get_texture_res_x();
	i32 res_y = config_get_texture_res_y();
	if (util_uv_uvmap == NULL) {
		util_uv_uvmap = gpu_create_render_target(res_x, res_y, GPU_TEXTURE_FORMAT_RGBA32);
	}

	util_uv_uvmap_cached = true;
	i32            mask  = slot_layer_get_object_mask(g_context->layer);
	mesh_object_t *merged =
	    (mask > 0 && mask <= g_project->_->paint_objects->length) ? g_project->_->paint_objects->buffer[mask - 1] : g_context->merged_object;
	mesh_data_t *mesh = (g_context->layer_filter == 0 && merged != NULL) ? merged->data : g_context->paint_object->data;

	i16_array_t *texa = mesh->vertex_arrays->buffer[2]->values;
	u32_array_t *inda = mesh->index_array;
	draw_begin(util_uv_uvmap, true, 0x00000000);
	draw_set_color(0xffffffff);
	f32 strength = res_x > 2048 ? 2.0 : 1.0;
	f32 f        = (1 / 32767.0) * util_uv_uvmap->width;
	for (i32 i = 0; i < math_floor(inda->length / 3.0); ++i) {
		f32 x1 = (texa->buffer[inda->buffer[i * 3] * 2]) * f;
		f32 x2 = (texa->buffer[inda->buffer[i * 3 + 1] * 2]) * f;
		f32 x3 = (texa->buffer[inda->buffer[i * 3 + 2] * 2]) * f;
		f32 y1 = (texa->buffer[inda->buffer[i * 3] * 2 + 1]) * f;
		f32 y2 = (texa->buffer[inda->buffer[i * 3 + 1] * 2 + 1]) * f;
		f32 y3 = (texa->buffer[inda->buffer[i * 3 + 2] * 2 + 1]) * f;
		draw_line_aa(x1, y1, x2, y2, strength);
		draw_line_aa(x2, y2, x3, y3, strength);
		draw_line_aa(x3, y3, x1, y1, strength);
	}
	draw_end();
}

void util_uv_cache_triangle_map() {
	if (util_uv_trianglemap != NULL &&
	    (util_uv_trianglemap->width != config_get_texture_res_x() || util_uv_trianglemap->height != config_get_texture_res_y())) {
		gpu_delete_texture(util_uv_trianglemap);
		util_uv_trianglemap        = NULL;
		util_uv_trianglemap_cached = false;
	}

	if (util_uv_trianglemap_cached) {
		return;
	}

	if (util_uv_trianglemap == NULL) {
		util_uv_trianglemap = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA32);
	}

	util_uv_trianglemap_cached = true;
	mesh_data_t *merged        = g_context->merged_object != NULL ? g_context->merged_object->data : g_context->paint_object->data;
	mesh_data_t *mesh          = merged;
	i16_array_t *texa          = mesh->vertex_arrays->buffer[2]->values;
	u32_array_t *inda          = mesh->index_array;
	draw_begin(util_uv_trianglemap, true, 0xff000000);
	f32 f     = (1 / 32767.0) * util_uv_trianglemap->width;
	i32 color = 0xff000001;
	for (i32 i = 0; i < math_floor(inda->length / 3.0); ++i) {
		if (color == 0xffffffff)
			color = 0xff000001;
		color++;
		draw_set_color(color);
		f32 x1 = (texa->buffer[inda->buffer[i * 3] * 2]) * f;
		f32 x2 = (texa->buffer[inda->buffer[i * 3 + 1] * 2]) * f;
		f32 x3 = (texa->buffer[inda->buffer[i * 3 + 2] * 2]) * f;
		f32 y1 = (texa->buffer[inda->buffer[i * 3] * 2 + 1]) * f;
		f32 y2 = (texa->buffer[inda->buffer[i * 3 + 1] * 2 + 1]) * f;
		f32 y3 = (texa->buffer[inda->buffer[i * 3 + 2] * 2 + 1]) * f;
		draw_filled_triangle(x1, y1, x2, y2, x3, y3);
	}
	draw_end();
}

void util_uv_cache_dilate_map() {
	if (util_uv_dilatemap != NULL && (util_uv_dilatemap->width != config_get_texture_res_x() || util_uv_dilatemap->height != config_get_texture_res_y())) {
		gpu_delete_texture(util_uv_dilatemap);
		util_uv_dilatemap        = NULL;
		util_uv_dilatemap_cached = false;
	}

	if (util_uv_dilatemap_cached)
		return;

	if (util_uv_dilatemap == NULL) {
		util_uv_dilatemap = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);
	}

	if (util_uv_pipe_dilate == NULL) {
		util_uv_pipe_dilate                  = gpu_create_pipeline();
		util_uv_pipe_dilate->vertex_shader   = sys_get_shader("dilate_map.vert");
		util_uv_pipe_dilate->fragment_shader = sys_get_shader("dilate_map.frag");
		gpu_vertex_structure_t *vs           = ALLOC_INIT(gpu_vertex_structure_t, {0});
		gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_I16_4X_NORM);
		gpu_vertex_structure_add(vs, "nor", GPU_VERTEX_DATA_I16_2X_NORM);
		gpu_vertex_structure_add(vs, "tex", GPU_VERTEX_DATA_I16_2X_NORM);
		util_uv_pipe_dilate->input_layout        = vs;
		util_uv_pipe_dilate->depth_write         = false;
		util_uv_pipe_dilate->depth_mode          = GPU_COMPARE_MODE_ALWAYS;
		util_uv_pipe_dilate->color_attachment[0] = GPU_TEXTURE_FORMAT_R8;
		gpu_pipeline_compile(util_uv_pipe_dilate);
		// dilate_tex_unpack = getConstantLocation(pipeDilate, "tex_unpack");
	}

	i32 mask = context_object_mask_used() ? slot_layer_get_object_mask(g_context->layer) : 0;
	if (context_layer_filter_used()) {
		mask = g_context->layer_filter;
	}
	mesh_data_t *geom = mask == 0 && g_context->merged_object != NULL ? g_context->merged_object->data : g_context->paint_object->data;
	_gpu_begin(util_uv_dilatemap, NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
	gpu_set_pipeline(util_uv_pipe_dilate);
	gpu_set_vertex_buffer(geom->_->vertex_buffer);
	gpu_set_index_buffer(geom->_->index_buffer);
	gpu_draw();
	gpu_end();
	util_uv_dilatemap_cached = true;
	util_uv_dilate_bytes     = NULL;
}

void _util_uv_check(i32 cx, i32 cy, i32 w, i32 h, i32 r, buffer_t *view, i32_array_t *coords_x, i32_array_t *coords_y) {
	if (cx < 0 || cx >= w || cy < 0 || cy >= h) {
		return;
	}
	if (buffer_get_u8(view, cy * w + cx) == 255) {
		return;
	}
	if (buffer_get_u8(util_uv_dilate_bytes, cy * r * util_uv_dilatemap->width + cx * r) == 0) {
		return;
	}
	buffer_set_u8(view, cy * w + cx, 255);

	i32_array_push(coords_x, cx + 1);
	i32_array_push(coords_y, cy);

	i32_array_push(coords_x, cx - 1);
	i32_array_push(coords_y, cy);

	i32_array_push(coords_x, cx);
	i32_array_push(coords_y, cy + 1);

	i32_array_push(coords_x, cx);
	i32_array_push(coords_y, cy - 1);
}

void util_uv_cache_uv_island_map() {
	util_uv_cache_dilate_map();
	if (util_uv_dilate_bytes == NULL) {
		util_uv_dilate_bytes = gpu_get_texture_pixels(util_uv_dilatemap);
	}
	util_render_pick_pos_nor_tex();
	i32          w        = 2048; // config_get_texture_res_x()
	i32          h        = 2048; // config_get_texture_res_y()
	i32          x        = math_floor(g_context->uvx_picked * w);
	i32          y        = math_floor(g_context->uvy_picked * h);
	buffer_t    *bytes    = buffer_create(w * h);
	i32_array_t *coords_x = i32_array_create_from_raw(
	    (i32[]){
	        x,
	    },
	    1);
	i32_array_t *coords_y = i32_array_create_from_raw(
	    (i32[]){
	        y,
	    },
	    1);
	i32 r = math_floor(util_uv_dilatemap->width / (float)w);

	while (coords_x->length > 0) {
		i32 cx = i32_array_pop(coords_x);
		i32 cy = i32_array_pop(coords_y);
		_util_uv_check(cx, cy, w, h, r, bytes, coords_x, coords_y);
	}

	if (util_uv_uvislandmap != NULL) {
		gpu_delete_texture(util_uv_uvislandmap);
	}
	util_uv_uvislandmap        = gpu_create_texture_from_bytes(bytes, w, h, GPU_TEXTURE_FORMAT_R8);
	util_uv_uvislandmap_cached = true;

	array_free(bytes);
	free(bytes);
	array_free(coords_x);
	free(coords_x);
	array_free(coords_y);
	free(coords_y);
}
