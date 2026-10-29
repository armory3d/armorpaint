
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
	bool         use_merged = merged != NULL && (g_context->layer_filter == 0 || util_mesh_udim_layer(g_context->layer));
	mesh_data_t *mesh       = use_merged ? merged->data : g_context->paint_object->data;

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
	bool selected_paint_object = context_layer_filter_used() || context_object_mask_used();
	mesh_data_t *merged = !selected_paint_object && g_context->merged_object != NULL ? g_context->merged_object->data : g_context->paint_object->data;
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
	bool         use_merged = g_context->merged_object != NULL && (mask == 0 || util_mesh_udim_layer(g_context->layer));
	mesh_data_t *geom       = use_merged ? g_context->merged_object->data : g_context->paint_object->data;
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

static gpu_texture_t *util_uv_seammap0     = NULL;
static gpu_texture_t *util_uv_seammap1     = NULL;
static mesh_data_t   *util_uv_seammap_mesh = NULL;
static i32            util_uv_seam_dx[4]   = {-1, 1, 0, 0};
static i32            util_uv_seam_dy[4]   = {0, 0, -1, 1};
static i16           *util_uv_seam_pos;

static int util_uv_seam_pos_compare(const void *a, const void *b) {
	return memcmp(util_uv_seam_pos + *(i32 *)a * 4, util_uv_seam_pos + *(i32 *)b * 4, sizeof(i16) * 3);
}

typedef struct util_uv_seam_edge {
	i32 g0;
	i32 g1;
	i32 tri_edge;
} util_uv_seam_edge_t;

static int util_uv_seam_edge_compare(const void *a, const void *b) {
	util_uv_seam_edge_t *ea = (util_uv_seam_edge_t *)a;
	util_uv_seam_edge_t *eb = (util_uv_seam_edge_t *)b;
	if (ea->g0 != eb->g0) {
		return ea->g0 < eb->g0 ? -1 : 1;
	}
	return ea->g1 < eb->g1 ? -1 : ea->g1 > eb->g1;
}

static i32 util_uv_seam_find(i32 *parent, i32 i) {
	while (parent[i] != i) {
		parent[i] = parent[parent[i]];
		i         = parent[i];
	}
	return i;
}

static f32 util_uv_seam_segment_dist(f32 px, f32 py, f32 *seg) {
	f32 dx = seg[2] - seg[0];
	f32 dy = seg[3] - seg[1];
	f32 l  = dx * dx + dy * dy;
	f32 t  = l > 0.0 ? ((px - seg[0]) * dx + (py - seg[1]) * dy) / l : 0.0;
	t      = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	f32 ex = seg[0] + dx * t - px;
	f32 ey = seg[1] + dy * t - py;
	return sqrtf(ex * ex + ey * ey);
}

// Marks the texels of island c covered by triangle (x, y) in pixels
static void util_uv_seam_raster_tri(i32 *chart, i32 w, i32 h, f32 *x, f32 *y, i32 c) {
	f32 area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
	if (fabsf(area) < 1e-12) {
		return;
	}
	i32 x0 = (i32)floorf(fminf(x[0], fminf(x[1], x[2])));
	i32 x1 = (i32)ceilf(fmaxf(x[0], fmaxf(x[1], x[2])));
	i32 y0 = (i32)floorf(fminf(y[0], fminf(y[1], y[2])));
	i32 y1 = (i32)ceilf(fmaxf(y[0], fmaxf(y[1], y[2])));
	x0     = x0 < 0 ? 0 : x0;
	y0     = y0 < 0 ? 0 : y0;
	x1     = x1 > w - 1 ? w - 1 : x1;
	y1     = y1 > h - 1 ? h - 1 : y1;
	for (i32 py = y0; py <= y1; ++py) {
		for (i32 px = x0; px <= x1; ++px) {
			f32 cx = px + 0.5;
			f32 cy = py + 0.5;
			f32 b0 = ((x[1] - cx) * (y[2] - cy) - (x[2] - cx) * (y[1] - cy)) / area;
			f32 b1 = ((x[2] - cx) * (y[0] - cy) - (x[0] - cx) * (y[2] - cy)) / area;
			if (b0 >= 0.0 && b1 >= 0.0 && b0 + b1 <= 1.0) {
				chart[py * w + px] = c;
			}
		}
	}
}

void util_uv_cache_seam_map(i32 res_x, i32 res_y) {
	mesh_data_t *mesh = g_context->merged_object != NULL ? g_context->merged_object->data : g_context->paint_object->data;
	if (util_uv_seammap0 != NULL && (util_uv_seammap0->width != res_x || util_uv_seammap0->height != res_y || util_uv_seammap_mesh != mesh)) {
		util_uv_seammap_cached = false;
	}

	if (util_uv_seammap_cached) {
		return;
	}
	util_uv_seammap_cached = true;
	util_uv_seammap_mesh   = mesh;
	if (util_uv_seammap0 != NULL) {
		gpu_delete_texture(util_uv_seammap0);
		gpu_delete_texture(util_uv_seammap1);
	}

	i16_array_t *posa     = mesh->vertex_arrays->buffer[0]->values;
	i16_array_t *texa     = mesh->vertex_arrays->buffer[2]->values;
	u32_array_t *inda     = mesh->index_array;
	i32          nv       = posa->length / 4;
	i32          nt       = inda->length / 3;
	i32          w        = res_x;
	i32          h        = res_y;
	f32          tf       = 1.0 / 32767.0;
	f32          max_dist = 1.5; // Pixels, the neighbors of island texels are enough

	// Weld vertices by position
	i32 *order = malloc(sizeof(i32) * nv);
	i32 *group = malloc(sizeof(i32) * nv);
	for (i32 i = 0; i < nv; ++i) {
		order[i] = i;
	}
	util_uv_seam_pos = posa->buffer;
	qsort(order, nv, sizeof(i32), util_uv_seam_pos_compare);
	for (i32 i = 0; i < nv; ++i) {
		bool same       = i > 0 && util_uv_seam_pos_compare(&order[i], &order[i - 1]) == 0;
		group[order[i]] = same ? group[order[i - 1]] : order[i];
	}
	free(order);

	// Pair triangle edges sharing a welded edge, adj[t * 3 + k] is the neighbor edge
	util_uv_seam_edge_t *edges = malloc(sizeof(util_uv_seam_edge_t) * nt * 3);
	for (i32 t = 0; t < nt; ++t) {
		for (i32 k = 0; k < 3; ++k) {
			i32 g0                    = group[inda->buffer[t * 3 + k]];
			i32 g1                    = group[inda->buffer[t * 3 + (k + 1) % 3]];
			edges[t * 3 + k].g0       = g0 < g1 ? g0 : g1;
			edges[t * 3 + k].g1       = g0 < g1 ? g1 : g0;
			edges[t * 3 + k].tri_edge = t * 3 + k;
		}
	}
	qsort(edges, nt * 3, sizeof(util_uv_seam_edge_t), util_uv_seam_edge_compare);
	i32  *adj    = malloc(sizeof(i32) * nt * 3);
	bool *seam   = calloc(nt * 3, sizeof(bool));
	i32  *parent = malloc(sizeof(i32) * nt);
	for (i32 t = 0; t < nt; ++t) {
		parent[t] = t;
	}
	for (i32 i = 0; i < nt * 3;) {
		i32 j = i + 1;
		while (j < nt * 3 && edges[j].g0 == edges[i].g0 && edges[j].g1 == edges[i].g1) {
			j++;
		}
		if (j - i == 2 && edges[i].g0 != edges[i].g1) { // Manifold edge
			i32 e0  = edges[i].tri_edge;
			i32 e1  = edges[i + 1].tri_edge;
			adj[e0] = e1;
			adj[e1] = e0;
			i32 a0  = inda->buffer[e0];
			i32 b0  = inda->buffer[(e0 / 3) * 3 + (e0 % 3 + 1) % 3];
			i32 a1  = inda->buffer[e1];
			i32 b1  = inda->buffer[(e1 / 3) * 3 + (e1 % 3 + 1) % 3];
			if (group[a1] != group[a0]) {
				i32 tmp = a1;
				a1      = b1;
				b1      = tmp;
			}
			bool same_uv = texa->buffer[a0 * 2] == texa->buffer[a1 * 2] && texa->buffer[a0 * 2 + 1] == texa->buffer[a1 * 2 + 1] &&
			               texa->buffer[b0 * 2] == texa->buffer[b1 * 2] && texa->buffer[b0 * 2 + 1] == texa->buffer[b1 * 2 + 1];
			seam[e0] = !same_uv;
			seam[e1] = !same_uv;
			if (same_uv) {
				parent[util_uv_seam_find(parent, e0 / 3)] = util_uv_seam_find(parent, e1 / 3);
			}
		}
		i = j;
	}
	free(edges);

	// Island per texel, -1 when empty
	i32 *chart = malloc(sizeof(i32) * w * h);
	for (i32 i = 0; i < w * h; ++i) {
		chart[i] = -1;
	}
	for (i32 t = 0; t < nt; ++t) {
		f32 x[3], y[3];
		for (i32 k = 0; k < 3; ++k) {
			i32 vi = inda->buffer[t * 3 + k];
			x[k]   = texa->buffer[vi * 2] * tf * w;
			y[k]   = texa->buffer[vi * 2 + 1] * tf * h;
		}
		util_uv_seam_raster_tri(chart, w, h, x, y, util_uv_seam_find(parent, t));
	}

	// Uv across the seam for each of the 4 neighbors, slot 4 is the portal of an empty texel, with the distance to the seam it was unfolded from
	f32 *slot   = malloc(sizeof(f32) * w * h * 10);
	f32 *slot_d = malloc(sizeof(f32) * w * h * 5);
	for (i32 i = 0; i < w * h * 5; ++i) {
		slot_d[i] = 1e30;
	}

	// Texels just across each seam edge map into the island on the other side, unfolded around the edge
	for (i32 e = 0; e < nt * 3; ++e) {
		if (!seam[e]) {
			continue;
		}
		i32 t  = e / 3;
		i32 k  = e % 3;
		i32 a  = inda->buffer[t * 3 + k];
		i32 b  = inda->buffer[t * 3 + (k + 1) % 3];
		i32 c  = inda->buffer[t * 3 + (k + 2) % 3];
		i32 e2 = adj[e];
		i32 t2 = e2 / 3;
		i32 a2 = inda->buffer[e2];
		i32 b2 = inda->buffer[t2 * 3 + (e2 % 3 + 1) % 3];
		i32 c2 = inda->buffer[t2 * 3 + (e2 % 3 + 2) % 3];
		if (group[a2] != group[a]) {
			i32 tmp = a2;
			a2      = b2;
			b2      = tmp;
		}

		// Edge in pixels on this side, in uvs on the other side
		f32 ax = texa->buffer[a * 2] * tf * w, ay = texa->buffer[a * 2 + 1] * tf * h;
		f32 bx = texa->buffer[b * 2] * tf * w, by = texa->buffer[b * 2 + 1] * tf * h;
		f32 cx = texa->buffer[c * 2] * tf * w, cy = texa->buffer[c * 2 + 1] * tf * h;
		f32 a2x = texa->buffer[a2 * 2] * tf, a2y = texa->buffer[a2 * 2 + 1] * tf;
		f32 b2x = texa->buffer[b2 * 2] * tf, b2y = texa->buffer[b2 * 2 + 1] * tf;
		f32 c2x = texa->buffer[c2 * 2] * tf, c2y = texa->buffer[c2 * 2 + 1] * tf;

		// Similarity z -> m * z + o, or m * conj(z) + o for mirrored islands, taking a2 to a and b2 to b
		f32 dx = bx - ax, dy = by - ay;
		f32 d2x = b2x - a2x, d2y = b2y - a2y;
		f32 l2 = d2x * d2x + d2y * d2y;
		f32 l  = dx * dx + dy * dy;
		if (l2 <= 0.0 || l < 1e-8) {
			continue;
		}
		f32  side   = dx * (cy - ay) - dy * (cx - ax);
		bool mirror = false;
		f32  mx = 0.0, my = 0.0, ox = 0.0, oy = 0.0;
		for (i32 pass = 0; pass < 2; ++pass) {
			f32 qy = mirror ? -d2y : d2y;
			mx     = (dx * d2x + dy * qy) / l2;
			my     = (dy * d2x - dx * qy) / l2;
			f32 zy = mirror ? -a2y : a2y;
			ox     = ax - (mx * a2x - my * zy);
			oy     = ay - (mx * zy + my * a2x);
			// The unfolded third vertex has to land on the far side of the edge
			f32 wy = mirror ? -c2y : c2y;
			f32 ux = mx * c2x - my * wy + ox;
			f32 uy = mx * wy + my * c2x + oy;
			if (side * (dx * (uy - ay) - dy * (ux - ax)) <= 0.0) {
				break;
			}
			mirror = true;
		}
		f32 mm = mx * mx + my * my;

		i32 chart1 = util_uv_seam_find(parent, t);
		i32 chart2 = util_uv_seam_find(parent, t2);
		f32 seg[4] = {ax, ay, bx, by};
		// Reaches one texel past the texture border, where the texels are neighbors of border texels
		i32 x0 = (i32)floorf(fminf(ax, bx) - max_dist), x1 = (i32)ceilf(fmaxf(ax, bx) + max_dist);
		i32 y0 = (i32)floorf(fminf(ay, by) - max_dist), y1 = (i32)ceilf(fmaxf(ay, by) + max_dist);
		x0 = x0 < -1 ? -1 : x0;
		y0 = y0 < -1 ? -1 : y0;
		x1 = x1 > w ? w : x1;
		y1 = y1 > h ? h : y1;
		for (i32 py = y0; py <= y1; ++py) {
			for (i32 px = x0; px <= x1; ++px) {
				f32  pcx    = px + 0.5;
				f32  pcy    = py + 0.5;
				bool inside = px >= 0 && px < w && py >= 0 && py < h;
				i32  i      = py * w + px;
				if ((inside && chart[i] == chart1) || side * (dx * (pcy - ay) - dy * (pcx - ax)) > 0.0) {
					continue;
				}
				f32 d = util_uv_seam_segment_dist(pcx, pcy, seg);
				if (d > max_dist) {
					continue;
				}
				// Inverse similarity into the other island, rejected when it lands in a third island
				f32 zx = ((pcx - ox) * mx + (pcy - oy) * my) / mm;
				f32 zy = ((pcy - oy) * mx - (pcx - ox) * my) / mm;
				zy     = mirror ? -zy : zy;
				i32 qx = (i32)floorf(zx * w);
				i32 qy = (i32)floorf(zy * h);
				if (qx < 0 || qx >= w || qy < 0 || qy >= h || (chart[qy * w + qx] != chart2 && chart[qy * w + qx] != -1)) {
					continue;
				}
				if (inside && chart[i] == -1 && d < slot_d[i * 5 + 4]) {
					slot[(i * 5 + 4) * 2]     = zx;
					slot[(i * 5 + 4) * 2 + 1] = zy;
					slot_d[i * 5 + 4]         = d;
				}
				// Texels of this island whose neighbor in direction kk is this texel
				for (i32 kk = 0; kk < 4; ++kk) {
					i32 nx = px - util_uv_seam_dx[kk];
					i32 ny = py - util_uv_seam_dy[kk];
					if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
						continue;
					}
					i32 n = ny * w + nx;
					if (chart[n] == chart1 && d < slot_d[n * 5 + kk]) {
						slot[(n * 5 + kk) * 2]     = zx;
						slot[(n * 5 + kk) * 2 + 1] = zy;
						slot_d[n * 5 + kk]         = d;
					}
				}
			}
		}
	}
	free(adj);
	free(seam);
	free(parent);
	free(group);

	// Island texels step to texels of the same island or across a seam, portals copy the other side, the rest blurs freely
	buffer_t *bytes0 = buffer_create(w * h * 16);
	buffer_t *bytes1 = buffer_create(w * h * 16);
	f32      *nbr0   = (f32 *)bytes0->buffer;
	f32      *nbr1   = (f32 *)bytes1->buffer;
	for (i32 py = 0; py < h; ++py) {
		for (i32 px = 0; px < w; ++px) {
			i32 i = py * w + px;
			for (i32 k = 0; k < 4; ++k) {
				f32 *out = (k < 2 ? nbr0 : nbr1) + i * 4 + (k % 2) * 2;
				i32  nx  = px + util_uv_seam_dx[k];
				i32  ny  = py + util_uv_seam_dy[k];
				bool in  = nx >= 0 && nx < w && ny >= 0 && ny < h;
				i32  s   = chart[i] == -1 && slot_d[i * 5 + 4] < 1e30 ? i * 5 + 4 : i * 5 + k;
				out[0]   = -1.0;
				out[1]   = -1.0;
				if (s % 5 != 4 && in && (chart[i] == -1 || chart[ny * w + nx] == chart[i])) {
					out[0] = (nx + 0.5) / w;
					out[1] = (ny + 0.5) / h;
				}
				else if (slot_d[s] < 1e30) {
					out[0] = slot[s * 2];
					out[1] = slot[s * 2 + 1];
				}
			}
		}
	}
	free(chart);
	free(slot);
	free(slot_d);

	util_uv_seammap0 = gpu_create_texture_from_bytes_raw(bytes0, w, h, GPU_TEXTURE_FORMAT_RGBA128);
	util_uv_seammap1 = gpu_create_texture_from_bytes_raw(bytes1, w, h, GPU_TEXTURE_FORMAT_RGBA128);
	array_free(bytes0);
	free(bytes0);
	array_free(bytes1);
	free(bytes1);
}

static gpu_pipeline_t *util_uv_pipe_blur_seam = NULL;
static i32             util_uv_blur_seam_size;

// Blurs src into dst along the surface with passes diffusion steps, tmp is a scratch target of the dst size and format
void util_uv_blur_seamless(gpu_texture_t *dst, gpu_texture_t *tmp, gpu_texture_t *src, i32 passes) {
	if (util_uv_pipe_blur_seam == NULL) {
		util_uv_pipe_blur_seam                  = gpu_create_pipeline();
		util_uv_pipe_blur_seam->vertex_shader   = sys_get_shader("blur_seam.vert");
		util_uv_pipe_blur_seam->fragment_shader = sys_get_shader("blur_seam.frag");
		gpu_vertex_structure_t *vs              = ALLOC_INIT(gpu_vertex_structure_t, {0});
		gpu_vertex_structure_add(vs, "pos", GPU_VERTEX_DATA_F32_2X);
		util_uv_pipe_blur_seam->input_layout           = vs;
		util_uv_pipe_blur_seam->color_attachment_count = 1;
		util_uv_pipe_blur_seam->color_attachment[0]    = dst->format;
		gpu_pipeline_compile(util_uv_pipe_blur_seam);
		pipes_offset           = 0;
		util_uv_blur_seam_size = pipes_get_constant_location("float2");
	}

	// The last pass lands in dst
	for (i32 i = 0; i < passes; ++i) {
		gpu_texture_t *from = i == 0 ? src : ((passes - i) % 2 == 0 ? dst : tmp);
		gpu_texture_t *to   = (passes - 1 - i) % 2 == 0 ? dst : tmp;
		_gpu_begin(to, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(util_uv_pipe_blur_seam);
		gpu_set_texture(0, from);
		gpu_set_texture(1, util_uv_seammap0);
		gpu_set_texture(2, util_uv_seammap1);
		gpu_set_float2(util_uv_blur_seam_size, dst->width, dst->height);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}
}
