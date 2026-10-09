
#include "../global.h"

#define SCULPT_ADJ_SLOTS        8
#define SCULPT_CLOTH_AREA       2.5
#define SCULPT_CLOTH_ITERATIONS 4
#define SCULPT_STRETCH_AREA     5.0

static i32            sculpt_vertex_count         = 0;
static i32            sculpt_texel_count          = 0;
static i32           *sculpt_remap                = NULL; // Vertex -> texel
static i32           *sculpt_rep                  = NULL; // Texel -> first vertex
static i32           *sculpt_object_texel_offsets = NULL; // Per object, plus total
static i32            sculpt_object_count         = 0;
static gpu_texture_t *sculpt_remap_texture        = NULL;
static gpu_texture_t *sculpt_adj_texture0         = NULL;
static gpu_texture_t *sculpt_adj_texture1         = NULL;
static f32            sculpt_cloth_drag           = 1.0;

i32 sculpt_object_vertex_offset(mesh_object_t *o) {
	i32 offset = 0;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p == o) {
			break;
		}
		offset += p->data->vertex_arrays->buffer[0]->values->length / 4;
	}
	return offset;
}

i32 sculpt_object_texel_offset(i32 object_index) {
	return object_index < sculpt_object_count ? sculpt_object_texel_offsets[object_index] : 0;
}

i32 sculpt_object_texel_count(i32 object_index) {
	return object_index < sculpt_object_count ? sculpt_object_texel_offsets[object_index + 1] - sculpt_object_texel_offsets[object_index] : 0;
}

i32 sculpt_vertex_texel(i32 vertex) {
	return vertex < sculpt_vertex_count ? sculpt_remap[vertex] : 0;
}

i32 sculpt_texel_vertex(i32 texel) {
	return texel < sculpt_texel_count ? sculpt_rep[texel] : 0;
}

gpu_texture_t *sculpt_get_remap_texture() {
	return sculpt_remap_texture;
}

gpu_texture_t *sculpt_get_adj_texture(i32 i) {
	return i == 0 ? sculpt_adj_texture0 : sculpt_adj_texture1;
}

f32 sculpt_get_cloth_drag() {
	return sculpt_cloth_drag;
}

static i16 *sculpt_weld_keys = NULL;

static int sculpt_weld_compare(const void *a, const void *b) {
	i32  ia = *(const i32 *)a;
	i32  ib = *(const i32 *)b;
	i16 *ka = sculpt_weld_keys + ia * 4;
	i16 *kb = sculpt_weld_keys + ib * 4;
	for (i32 i = 0; i < 3; ++i) {
		if (ka[i] != kb[i]) {
			return ka[i] < kb[i] ? -1 : 1;
		}
	}
	return ia - ib;
}

static gpu_texture_t *sculpt_create_index_texture(buffer_t *b, i32 width, i32 height, gpu_texture_format_t format, gpu_texture_t *old) {
	if (old != NULL) {
		gpu_delete_texture(old);
	}
	gpu_texture_t *t = gpu_create_texture_from_bytes(b, width, height, format);
	array_free(b);
	free(b);
	return t;
}

// Fan-order the neighbors of texel a, so consecutive slots form the triangles around it
// The list ends with -1 for a closed fan and -2 for an open one
static void sculpt_build_fan(i32 a, i32 *pairs, i32 pair_count, f32 *out) {
	for (i32 s = 0; s < SCULPT_ADJ_SLOTS; ++s) {
		out[s] = -1.0f;
	}
	if (pair_count == 0) {
		return;
	}
	// Start at a neighbor that never follows another, so open fans are walked from their border
	i32 start = pairs[0];
	for (i32 i = 0; i < pair_count; ++i) {
		bool follows = false;
		for (i32 j = 0; j < pair_count; ++j) {
			if (pairs[j * 2 + 1] == pairs[i * 2]) {
				follows = true;
				break;
			}
		}
		if (!follows) {
			start = pairs[i * 2];
			break;
		}
	}
	i32  cur    = start;
	i32  count  = 0;
	bool closed = false;
	while (count < SCULPT_ADJ_SLOTS) {
		out[count++] = (f32)cur;
		i32 next     = -1;
		for (i32 i = 0; i < pair_count; ++i) {
			if (pairs[i * 2] == cur) {
				next = pairs[i * 2 + 1];
				break;
			}
		}
		if (next < 0) {
			break;
		}
		if (next == start) {
			closed = true;
			break;
		}
		cur = next;
	}
	if (count < SCULPT_ADJ_SLOTS && !closed) {
		out[count] = -2.0f;
	}
}

static void sculpt_build_weld() {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	i32                    vcount  = 0;
	i32                    icount  = 0;
	for (i32 o = 0; o < objects->length; ++o) {
		vcount += objects->buffer[o]->data->vertex_arrays->buffer[0]->values->length / 4;
		icount += objects->buffer[o]->data->index_array->length;
	}

	free(sculpt_remap);
	free(sculpt_rep);
	free(sculpt_object_texel_offsets);
	sculpt_vertex_count         = vcount;
	sculpt_object_count         = objects->length;
	sculpt_remap                = malloc(sizeof(i32) * (vcount > 0 ? vcount : 1));
	sculpt_rep                  = malloc(sizeof(i32) * (vcount > 0 ? vcount : 1));
	sculpt_object_texel_offsets = malloc(sizeof(i32) * (objects->length + 1));

	// Group the vertices of each object by position, the lowest vertex index represents the group
	i32 *order  = malloc(sizeof(i32) * (vcount > 0 ? vcount : 1));
	i32 *leader = malloc(sizeof(i32) * (vcount > 0 ? vcount : 1));
	i32  voff   = 0;
	i32  texels = 0;
	for (i32 o = 0; o < objects->length; ++o) {
		i16_array_t *pos = objects->buffer[o]->data->vertex_arrays->buffer[0]->values;
		i32          n   = pos->length / 4;
		sculpt_weld_keys = pos->buffer;
		for (i32 i = 0; i < n; ++i) {
			order[i] = i;
		}
		qsort(order, n, sizeof(i32), sculpt_weld_compare);
		i32 start = 0;
		while (start < n) {
			i32 end = start + 1;
			while (end < n && memcmp(pos->buffer + order[end] * 4, pos->buffer + order[start] * 4, sizeof(i16) * 3) == 0) {
				end++;
			}
			for (i32 i = start; i < end; ++i) {
				leader[voff + order[i]] = voff + order[start];
			}
			start = end;
		}
		sculpt_object_texel_offsets[o] = texels;
		for (i32 i = 0; i < n; ++i) {
			i32 v = voff + i;
			if (leader[v] == v) {
				sculpt_rep[texels] = v;
				sculpt_remap[v]    = texels++;
			}
			else {
				sculpt_remap[v] = sculpt_remap[leader[v]];
			}
		}
		voff += n;
	}
	sculpt_object_texel_offsets[objects->length] = texels;
	sculpt_texel_count                           = texels;
	sculpt_weld_keys                             = NULL;
	free(order);
	free(leader);

	// Collect the (next, previous) corner pairs of every triangle around each texel
	i32 *pair_start = calloc(texels + 1, sizeof(i32));
	i32 *tri        = malloc(sizeof(i32) * (icount > 0 ? icount : 1));
	i32  tcount     = 0;
	voff            = 0;
	for (i32 o = 0; o < objects->length; ++o) {
		mesh_data_t *md = objects->buffer[o]->data;
		for (i32 i = 0; i + 2 < md->index_array->length; i += 3) {
			i32 a = sculpt_remap[voff + md->index_array->buffer[i]];
			i32 b = sculpt_remap[voff + md->index_array->buffer[i + 1]];
			i32 c = sculpt_remap[voff + md->index_array->buffer[i + 2]];
			if (a == b || b == c || a == c) {
				continue;
			}
			tri[tcount * 3]     = a;
			tri[tcount * 3 + 1] = b;
			tri[tcount * 3 + 2] = c;
			pair_start[a + 1]++;
			pair_start[b + 1]++;
			pair_start[c + 1]++;
			tcount++;
		}
		voff += md->vertex_arrays->buffer[0]->values->length / 4;
	}
	for (i32 t = 0; t < texels; ++t) {
		pair_start[t + 1] += pair_start[t];
	}
	i32 *pairs = malloc(sizeof(i32) * 2 * (tcount * 3 > 0 ? tcount * 3 : 1));
	i32 *fill  = calloc(texels > 0 ? texels : 1, sizeof(i32));
	for (i32 t = 0; t < tcount; ++t) {
		for (i32 k = 0; k < 3; ++k) {
			i32 a               = tri[t * 3 + k];
			i32 slot            = pair_start[a] + fill[a]++;
			pairs[slot * 2]     = tri[t * 3 + (k + 1) % 3];
			pairs[slot * 2 + 1] = tri[t * 3 + (k + 2) % 3];
		}
	}
	free(tri);
	free(fill);

	i32       w  = config_get_texture_res_x();
	i32       h  = texels > 0 ? (texels + w - 1) / w : 1;
	buffer_t *a0 = buffer_create(w * h * 16);
	buffer_t *a1 = buffer_create(w * h * 16);
	f32       fan[SCULPT_ADJ_SLOTS];
	for (i32 t = 0; t < w * h; ++t) {
		if (t < texels) {
			sculpt_build_fan(t, pairs + pair_start[t] * 2, pair_start[t + 1] - pair_start[t], fan);
		}
		else {
			for (i32 s = 0; s < SCULPT_ADJ_SLOTS; ++s) {
				fan[s] = -1.0f;
			}
		}
		for (i32 s = 0; s < 4; ++s) {
			buffer_set_f32(a0, t * 16 + s * 4, fan[s]);
			buffer_set_f32(a1, t * 16 + s * 4, fan[s + 4]);
		}
	}
	free(pairs);
	free(pair_start);
	sculpt_adj_texture0 = sculpt_create_index_texture(a0, w, h, GPU_TEXTURE_FORMAT_RGBA128, sculpt_adj_texture0);
	sculpt_adj_texture1 = sculpt_create_index_texture(a1, w, h, GPU_TEXTURE_FORMAT_RGBA128, sculpt_adj_texture1);

	// Indices are stored as floats, exact up to 2^24
	i32       rh = vcount > 0 ? (vcount + w - 1) / w : 1;
	buffer_t *r  = buffer_create(w * rh * 4);
	for (i32 v = 0; v < vcount; ++v) {
		buffer_set_f32(r, v * 4, (f32)sculpt_remap[v]);
	}
	sculpt_remap_texture = sculpt_create_index_texture(r, w, rh, GPU_TEXTURE_FORMAT_R32, sculpt_remap_texture);
}

static void sculpt_ensure_texture_res() {
	i32  required = sculpt_texel_count;
	bool changed  = false;
	while (config_get_texture_res_x() * config_get_texture_res_y() < required && config_get_texture_res_x() < 8192) {
		i32 next = config_get_texture_res_x() < 2048 ? 2048 : config_get_texture_res_x() < 4096 ? 4096 : 8192;
		i32 pos  = config_get_texture_res_pos(next);
		config_set_texture_res(pos);
		base_res = pos;
		changed  = true;
	}
	if (changed) {
		layers_resize();
	}
}

static i32 sculpt_vertex_object(i32 *vertex) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	for (i32 o = 0; o < objects->length; ++o) {
		i32 n = objects->buffer[o]->data->vertex_arrays->buffer[0]->values->length / 4;
		if (*vertex < n) {
			return o;
		}
		*vertex -= n;
	}
	return 0;
}

void sculpt_import_mesh_pack_to_texture(gpu_texture_t *target) {
	// Pack positions and normals into texture
	u32       capacity = config_get_texture_res_x() * config_get_texture_res_y();
	buffer_t *b        = buffer_create(capacity * 4 * 4);
	f32      *nor      = calloc(sculpt_texel_count * 3 + 1, sizeof(f32));
	i32       v        = 0;
	for (i32 o = 0; o < g_project->_->paint_objects->length; ++o) {
		mesh_data_t *mesh = g_project->_->paint_objects->buffer[o]->data;
		i16_array_t *pos  = mesh->vertex_arrays->buffer[0]->values;
		i16_array_t *nr   = mesh->vertex_arrays->buffer[1]->values;
		for (i32 i = 0; i < pos->length / 4; ++i, ++v) {
			// Average the normals of all vertices welded into the texel
			i32 t = sculpt_remap[v];
			nor[t * 3] += nr->buffer[i * 2] / 32767.0f;
			nor[t * 3 + 1] += nr->buffer[i * 2 + 1] / 32767.0f;
			nor[t * 3 + 2] += pos->buffer[i * 4 + 3] / 32767.0f;
		}
	}
	for (i32 t = 0; t < sculpt_texel_count && t < capacity; ++t) {
		i32          vi   = sculpt_rep[t];
		mesh_data_t *mesh = g_project->_->paint_objects->buffer[sculpt_vertex_object(&vi)]->data;
		i16_array_t *pos  = mesh->vertex_arrays->buffer[0]->values;
		buffer_set_f32(b, 4 * t * 4, pos->buffer[vi * 4] / 32767.0);
		buffer_set_f32(b, 4 * t * 4 + 1 * 4, pos->buffer[vi * 4 + 1] / 32767.0);
		buffer_set_f32(b, 4 * t * 4 + 2 * 4, pos->buffer[vi * 4 + 2] / 32767.0);
		f32 nor_x = nor[t * 3];
		f32 nor_y = nor[t * 3 + 1];
		f32 nor_z = nor[t * 3 + 2];
		f32 l1    = math_abs(nor_x) + math_abs(nor_y) + math_abs(nor_z);
		f32 oct_x = l1 > 0.0f ? nor_x / l1 : 0.0f;
		f32 oct_y = l1 > 0.0f ? nor_y / l1 : 0.0f;
		if (nor_z < 0.0f) {
			f32 ox = oct_x;
			f32 oy = oct_y;
			oct_x  = (1.0f - math_abs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
			oct_y  = (1.0f - math_abs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
		}
		buffer_set_f32(b, 4 * t * 4 + 3 * 4, (oct_x + 1.0f) * 0.5f + math_floor((oct_y + 1.0f) * 0.5f * 255.0f + 0.5f));
	}
	free(nor);

	gpu_texture_t *imgmesh = gpu_create_texture_from_bytes(b, config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA128);
	array_free(b);
	free(b);
	draw_begin(target, false, 0);
	draw_set_pipeline(pipes_copy128);
	draw_scaled_image(imgmesh, 0, 0, config_get_texture_res_x(), config_get_texture_res_y());
	draw_set_pipeline(NULL);
	draw_end();
	gpu_delete_texture(imgmesh);
}

bool sculpt_mode_uses_adjacency() {
	return g_context->tool == TOOL_TYPE_BRUSH &&
	       (g_context->brush_sculpt == SCULPT_TYPE_SMOOTH || g_context->brush_sculpt == SCULPT_TYPE_INFLATE || g_context->brush_sculpt == SCULPT_TYPE_CLOTH);
}

static char *sculpt_blend_mode(node_shader_t *kong, i32 blending, char *cola, char *colb, char *opac) {
	// mix/add/screen/lighten/difference (raise), subtract/darken (carve) and linear light (bidirectional)
	if (blending == BLEND_TYPE_DARKEN) {
		return string_tmp("lerp(%s, min(%s, %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_MULTIPLY) {
		return string_tmp("lerp(%s, %s * %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_BURN) {
		return string_tmp("lerp(%s, 1.0 - (1.0 - %s) / %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_LIGHTEN) {
		return string_tmp("max(%s, %s * %s)", cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SCREEN) {
		return string_tmp("(1.0 - (1.0 - %s * %s) * (1.0 - %s))", colb, opac, cola);
	}
	else if (blending == BLEND_TYPE_DODGE) {
		return string_tmp("lerp(%s, %s / (1.0 - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_ADD) {
		return string_tmp("lerp(%s, %s + %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_OVERLAY) {
		char *res = "sculpt_overlay_res";
		node_shader_write_frag(kong, string_tmp("float %s;", res));
		node_shader_write_frag(kong, string_tmp("if (%s < 0.5) { %s = 2.0 * %s * %s; } else { %s = 1.0 - 2.0 * (1.0 - %s) * (1.0 - %s); }", cola, res, cola,
		                                        colb, res, cola, colb));
		return string_tmp("lerp(%s, %s, %s)", cola, res, opac);
	}
	else if (blending == BLEND_TYPE_SOFT_LIGHT) {
		return string_tmp("((1.0 - %s) * %s + %s * ((1.0 - %s) * %s * %s + %s * (1.0 - (1.0 - %s) * (1.0 - %s))))", opac, cola, opac, cola, colb, cola, cola,
		                  colb, cola);
	}
	else if (blending == BLEND_TYPE_LINEAR_LIGHT) {
		return string_tmp("(%s + %s * (2.0 * (%s - 0.5)))", cola, opac, colb);
	}
	else if (blending == BLEND_TYPE_DIFFERENCE) {
		return string_tmp("lerp(%s, abs(%s - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SUBTRACT) {
		return string_tmp("lerp(%s, %s - %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_DIVIDE) {
		return string_tmp("((1.0 - %s) * %s + %s * %s / %s)", opac, cola, opac, cola, colb);
	}
	else { // BLEND_TYPE_MIX, hue / saturation / color / value
		return string_tmp("lerp(%s, %s, %s)", cola, colb, opac);
	}
}

node_shader_context_t *sculpt_make_sculpt_run(material_t *data) {
	char                  *context_id = "paint";
	shader_context_t      *props      = ALLOC_INIT(shader_context_t, {.name            = context_id,
	                                                                  .depth_write     = false,
	                                                                  .compare_mode    = "always",
	                                                                  .cull_mode       = "none",
	                                                                  .vertex_elements = any_array_create_from_raw(
                                                                (void *[]){
                                                                    ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "float2"}),
                                                                },
                                                                1),
	                                                                  .color_attachments = any_array_create_from_raw(
                                                                (void *[]){
                                                                    "RGBA128",
                                                                    "R8",
                                                                },
                                                                2)});
	node_shader_context_t *con_paint  = node_shader_context_create(data, props);
	con_paint->data->color_writes_red = u8_array_create_from_raw(
	    (u8[]){
	        true,
	        true,
	        true,
	        true,
	    },
	    4);
	con_paint->data->color_writes_green = u8_array_create_from_raw(
	    (u8[]){
	        true,
	        true,
	        true,
	        true,
	    },
	    4);
	con_paint->data->color_writes_blue = u8_array_create_from_raw(
	    (u8[]){
	        true,
	        true,
	        true,
	        true,
	    },
	    4);
	con_paint->data->color_writes_alpha = u8_array_create_from_raw(
	    (u8[]){
	        true,
	        true,
	        true,
	        true,
	    },
	    4);

	node_shader_t *kong     = node_shader_context_make_kong(con_paint);
	bool           decal    = context_is_decal();
	bool           particle = g_context->tool == TOOL_TYPE_PARTICLE;
	sculpt_type_t  mode     = g_context->tool == TOOL_TYPE_BRUSH ? g_context->brush_sculpt : SCULPT_TYPE_DRAW;
	// Grab drags the vertices along with the cursor instead of displacing along the normal
	bool grab    = mode == SCULPT_TYPE_GRAB;
	bool stretch = mode == SCULPT_TYPE_STRETCH;
	bool cloth   = mode == SCULPT_TYPE_CLOTH;
	// Cloth and stretch drag like grab, so they all unproject the cursor onto the grab plane
	bool drag = grab || stretch || cloth;
	// Subtract and darken invert the brush direction, as they carve in draw mode
	bool invert = g_context->brush_blending == BLEND_TYPE_SUBTRACT || g_context->brush_blending == BLEND_TYPE_DARKEN;
	// Decal fill layer: displacement must be confined to the decal box projection
	bool decal_layer = g_context->layer->fill_material != NULL && g_context->layer->uv_type == UV_TYPE_PROJECT && g_context->tool == TOOL_TYPE_FILL;

	bool has_wposition = g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE ||
	                     g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE || g_context->tool == TOOL_TYPE_FILL || decal;
	bool sculpt_triplanar = has_wposition && !decal && !decal_layer && g_context->tool != TOOL_TYPE_BLUR;
	node_shader_add_out(kong, "float2 tex_coord");
	node_shader_write_vert(kong, "float2 madd = float2(0.5, 0.5);");
	node_shader_write_vert(kong, "output.tex_coord = input.pos.xy * madd + madd;");
	node_shader_write_vert(kong, "output.tex_coord.y = 1.0 - output.tex_coord.y;");
	node_shader_write_vert(kong, "output.pos = float4(input.pos.xy, 0.0, 1.0);");
	node_shader_write_attrib_frag(kong, "float2 tex_coord = input.tex_coord;");
	node_shader_write_attrib_frag(kong, "float2 sculpt_uv = tex_coord;");

	// Restrict displacement to the object selected in the layer's object combo
	if (g_project->_->paint_objects->length > 1) {
		node_shader_add_texture(kong, "texpaint_sculpt_undo", "_texpaint_sculpt_undo");
		node_shader_add_constant(kong, "float2 texpaint_undo_size", "_size(_texpaint_sculpt_undo)");
		node_shader_add_constant(kong, "float sculpt_mask_offset", "_sculpt_mask_offset");
		node_shader_add_constant(kong, "float sculpt_mask_count", "_sculpt_mask_count");
		node_shader_write_frag(kong, "float sculpt_mask_lin = floor(sculpt_uv.y * constants.texpaint_undo_size.y) * "
		                             "constants.texpaint_undo_size.x + floor(sculpt_uv.x * constants.texpaint_undo_size.x);");
		node_shader_write_frag(kong, "if (sculpt_mask_lin < constants.sculpt_mask_offset || sculpt_mask_lin >= "
		                             "constants.sculpt_mask_offset + constants.sculpt_mask_count) { discard; }");
	}

	node_shader_add_constant(kong, "float4 inp", "_input_brush");
	node_shader_add_constant(kong, "float4 inplast", "_input_brush_last");
	node_shader_add_texture(kong, "gbufferD", NULL);
	kong->frag_out = "float4[2]";
	node_shader_add_constant(kong, "float brush_radius", "_brush_radius");
	node_shader_add_constant(kong, "float brush_opacity", "_brush_opacity");
	node_shader_add_constant(kong, "float brush_hardness", "_brush_hardness");
	node_shader_write_frag(kong, "float dist = 0.0;");

	if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE || g_context->tool == TOOL_TYPE_BLUR ||
	    g_context->tool == TOOL_TYPE_PARTICLE || decal) {
		node_shader_add_constant(kong, "float4x4 invVP", "_inv_view_proj_matrix");
		if (drag) {
			// Anchor a camera-facing plane at the grab_start surface point
			node_shader_add_constant(kong, "float2 grab_start", "_grab_start");
			node_shader_write_frag(kong, "float2 grab_ndc = float2(constants.grab_start.x, 1.0 - constants.grab_start.y) * 2.0 - 1.0;");
			node_shader_write_frag(kong, "float grab_adepth = sample_lod(gbufferD, sampler_linear, constants.grab_start, 0.0).r;");
			node_shader_write_frag(kong, "float4 grab_anchor4 = constants.invVP * float4(grab_ndc, grab_adepth, 1.0);");
			node_shader_write_frag(kong, "float3 grab_anchor = grab_anchor4.xyz / grab_anchor4.w;");
			node_shader_write_frag(kong, "float4 grab_near4 = constants.invVP * float4(grab_ndc, 0.0, 1.0);");
			node_shader_write_frag(kong, "float4 grab_far4 = constants.invVP * float4(grab_ndc, 1.0, 1.0);");
			node_shader_write_frag(kong, "float3 grab_plane_n = normalize(grab_far4.xyz / grab_far4.w - grab_near4.xyz / grab_near4.w);");
			// Intersect the cursor ray with the grab plane
			node_shader_write_frag(kong, "float2 winp_ndc = float2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0;");
			node_shader_write_frag(kong, "float4 winp_o4 = constants.invVP * float4(winp_ndc, 0.0, 1.0);");
			node_shader_write_frag(kong, "float4 winp_f4 = constants.invVP * float4(winp_ndc, 1.0, 1.0);");
			node_shader_write_frag(kong, "float3 winp_o = winp_o4.xyz / winp_o4.w;");
			node_shader_write_frag(kong, "float3 winp_d = normalize(winp_f4.xyz / winp_f4.w - winp_o);");
			node_shader_write_frag(kong, "float4 winp = float4(winp_o + winp_d * (dot(grab_anchor - winp_o, grab_plane_n) / dot(winp_d, grab_plane_n)), 1.0);");
		}
		else {
			node_shader_write_frag(kong, "float depth = sample_lod(gbufferD, sampler_linear, constants.inp.xy, 0.0).r;");
			node_shader_write_frag(kong, "float4 winp = float4(float2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0, depth, 1.0);");
			node_shader_write_frag(kong, "winp = constants.invVP * winp;");
			node_shader_write_frag(kong, "winp.xyz = winp.xyz / winp.w;");
		}
		node_shader_add_constant(kong, "float4x4 W", "_world_matrix");
		node_shader_write_attrib_frag(kong, "float4 read_undo = texpaint_sculpt_undo[uint2(uint(tex_coord.x * constants.texpaint_undo_size.x), "
		                                    "uint(tex_coord.y * constants.texpaint_undo_size.y))];");
		node_shader_write_attrib_frag(kong, "float3 wposition = (constants.W * float4(read_undo.xyz, 1.0)).xyz;");
		if (drag) {
			node_shader_write_frag(kong, "float2 winpl_ndc = float2(constants.inplast.x, 1.0 - constants.inplast.y) * 2.0 - 1.0;");
			node_shader_write_frag(kong, "float4 winpl_o4 = constants.invVP * float4(winpl_ndc, 0.0, 1.0);");
			node_shader_write_frag(kong, "float4 winpl_f4 = constants.invVP * float4(winpl_ndc, 1.0, 1.0);");
			node_shader_write_frag(kong, "float3 winpl_o = winpl_o4.xyz / winpl_o4.w;");
			node_shader_write_frag(kong, "float3 winpl_d = normalize(winpl_f4.xyz / winpl_f4.w - winpl_o);");
			node_shader_write_frag(
			    kong, "float4 winplast = float4(winpl_o + winpl_d * (dot(grab_anchor - winpl_o, grab_plane_n) / dot(winpl_d, grab_plane_n)), 1.0);");
		}
		else {
			node_shader_write_frag(kong, "float depthlast = sample_lod(gbufferD, sampler_linear, constants.inplast.xy, 0.0).r;");
			node_shader_write_frag(kong, "float4 winplast = float4(float2(constants.inplast.x, 1.0 - constants.inplast.y) * 2.0 - 1.0, depthlast, 1.0);");
			node_shader_write_frag(kong, "winplast = constants.invVP * winplast;");
			node_shader_write_frag(kong, "winplast.xyz = winplast.xyz / winplast.w;");
		}

		if (particle) {
			node_shader_add_constant(kong, "float3 particle_hit", "_particle_hit");
			node_shader_add_constant(kong, "float3 particle_hit_last", "_particle_hit_last");
			node_shader_add_constant(kong, "float particle_radius", "_particle_radius");
			node_shader_write_frag(kong, "float3 ppa = wposition.xyz - constants.particle_hit;");
			node_shader_write_frag(kong, "float3 pba = constants.particle_hit_last - constants.particle_hit;");
			node_shader_write_frag(kong, "float pph = clamp(dot(ppa, pba) / max(dot(pba, pba), 0.00000001), 0.0, 1.0);");
			node_shader_write_frag(kong, "dist = length(ppa - pba * pph) * (5.0 / constants.particle_radius);");
			node_shader_write_frag(kong, "if (dist > 1.0) { discard; }");
		}
		else if (grab || stretch) {
			// Pick the vertices around the anchor once, by their stroke-start positions, so they stay attached however fast the cursor moves
			node_shader_add_texture(kong, "texpaint_sculpt_stroke", "_texpaint_sculpt_stroke");
			node_shader_add_constant(kong, "float2 texpaint_undo_size", "_size(_texpaint_sculpt_undo)");
			node_shader_write_frag(kong,
			                       "float4 grab_rest = texpaint_sculpt_stroke[uint2(uint(sculpt_uv.x * constants.texpaint_undo_size.x), uint(sculpt_uv.y * "
			                       "constants.texpaint_undo_size.y))];");
			node_shader_write_frag(kong, "float3 grab_pa = (constants.W * float4(grab_rest.xyz, 1.0)).xyz - grab_anchor;");
			if (g_context->xray) {
				node_shader_write_frag(kong, "grab_pa -= grab_plane_n * dot(grab_plane_n, grab_pa);");
			}
			node_shader_write_frag(kong, "dist = length(grab_pa);");
			if (stretch) {
				node_shader_write_frag(kong, string_tmp("if (dist > constants.brush_radius * %s) { discard; }", f32_to_string(SCULPT_STRETCH_AREA)));
			}
			else {
				node_shader_write_frag(kong, "if (dist > constants.brush_radius) { discard; }");
			}
		}
		else if (!decal) {
			if (g_context->xray) {
				node_shader_write_frag(kong, "float2 xray_ndc = float2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0;");
				node_shader_write_frag(kong, "float4 xray_near = constants.invVP * float4(xray_ndc, 0.0, 1.0);");
				node_shader_write_frag(kong, "float4 xray_far = constants.invVP * float4(xray_ndc, 1.0, 1.0);");
				node_shader_write_frag(kong, "float3 xray_axis = normalize(xray_far.xyz / xray_far.w - xray_near.xyz / xray_near.w);");
			}
			if (g_context->brush_lazy_radius > 0 && g_context->brush_lazy_step > 0) { // Sphere
				if (g_context->xray) {
					node_shader_write_frag(kong, "float3 pa = wposition.xyz - winp.xyz;");
					node_shader_write_frag(kong, "dist = length(pa - xray_axis * dot(xray_axis, pa));");
				}
				else {
					node_shader_write_frag(kong, "dist = distance(wposition.xyz, winp.xyz);");
				}
			}
			else { // Capsule
				node_shader_write_frag(kong, "float3 pa = wposition.xyz - winp.xyz;");
				node_shader_write_frag(kong, "float3 ba = winplast.xyz - winp.xyz;");
				if (g_context->xray) {
					node_shader_write_frag(kong, "pa = pa - xray_axis * dot(xray_axis, pa);");
					node_shader_write_frag(kong, "ba = ba - xray_axis * dot(xray_axis, ba);");
				}
				node_shader_write_frag(kong, "float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);");
				node_shader_write_frag(kong, "dist = length(pa - ba * h);");
			}
			if (cloth) {
				node_shader_write_frag(kong, string_tmp("if (dist > constants.brush_radius * %s) { discard; }", f32_to_string(SCULPT_CLOTH_AREA)));
			}
			else {
				node_shader_write_frag(kong, "if (dist > constants.brush_radius) { discard; }");
			}
		}
		else { // decal
			node_shader_add_constant(kong, "float4 decal_mask", "_decal_mask");
			node_shader_write_frag(kong, "if (constants.decal_mask.z > 0.0) {");
			if (g_context->brush_lazy_radius > 0 && g_context->brush_lazy_step > 0) {
				node_shader_write_frag(kong, "dist = distance(wposition.xyz, winp.xyz);");
			}
			else {
				node_shader_write_frag(kong, "float3 pa = wposition.xyz - winp.xyz;");
				node_shader_write_frag(kong, "float3 ba = winplast.xyz - winp.xyz;");
				node_shader_write_frag(kong, "float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);");
				node_shader_write_frag(kong, "dist = length(pa - ba * h);");
			}
			node_shader_write_frag(kong, "if (dist > constants.brush_radius) { discard; }");
			node_shader_write_frag(kong, "}");
		}
	}
	else if (g_context->tool == TOOL_TYPE_FILL) {
		node_shader_add_constant(kong, "float4x4 W", "_world_matrix");
		node_shader_write_attrib_frag(kong, "float4 read_undo = texpaint_sculpt_undo[uint2(uint(tex_coord.x * constants.texpaint_undo_size.x), "
		                                    "uint(tex_coord.y * constants.texpaint_undo_size.y))];");
		node_shader_write_attrib_frag(kong, "float3 wposition = (constants.W * float4(read_undo.xyz, 1.0)).xyz;");
	}

	if (decal_layer) {
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_constant(kong, "float4x4 decal_layer_matrix", "_decal_layer_matrix");
		node_shader_write_frag(kong, "float4 decal_proj = constants.decal_layer_matrix * float4(read_undo.xyz, 1.0);");
		node_shader_write_frag(kong, "float2 uvsp = decal_proj.xy / decal_proj.w;");
		node_shader_write_frag(kong, "uvsp.x = uvsp.x * 0.5 + 0.5;");
		node_shader_write_frag(kong, "uvsp.y = 1.0 - (uvsp.y * 0.5 + 0.5);");
		node_shader_write_frag(kong, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");

		f32 uv_angle = g_context->layer->angle;
		if (uv_angle != 0.0) {
			node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
			node_shader_write_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
			                             "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
		}

		// Reject surfaces not facing the decal direction
		node_shader_write_frag(kong, "float4 dproj_undo = texpaint_sculpt_undo[uint2(uint(sculpt_uv.x * constants.texpaint_undo_size.x), uint(sculpt_uv.y "
		                             "* constants.texpaint_undo_size.y))];");
		node_shader_write_frag(kong, "float dproj_nv = floor(dproj_undo.a) / 255.0;");
		node_shader_write_frag(kong, "float2 dproj_oct = float2(dproj_undo.a - floor(dproj_undo.a), dproj_nv) * 2.0 - 1.0;");
		node_shader_write_frag(kong, "float dproj_nz = 1.0 - abs(dproj_oct.x) - abs(dproj_oct.y);");
		node_shader_write_frag(kong, "float3 dproj_nor = float3(dproj_oct.xy, dproj_nz);");
		node_shader_write_frag(kong, "if (dproj_nz < 0.0) { dproj_nor.xy = octahedron_wrap(dproj_oct.xy); }");
		node_shader_write_frag(kong, "dproj_nor = normalize((constants.W * float4(normalize(dproj_nor), 0.0)).xyz);");
		node_shader_add_constant(kong, "float3 decal_layer_nor", "_decal_layer_nor");
		f32 dot_angle = g_context->brush_angle_reject_dot;
		node_shader_write_frag(kong, string_tmp("if (abs(dot(dproj_nor, constants.decal_layer_nor) - 1.0) > %s) { discard; }", f32_to_string(dot_angle)));

		// Reject surfaces outside the decal box
		node_shader_add_constant(kong, "float3 decal_layer_loc", "_decal_layer_loc");
		node_shader_add_constant(kong, "float decal_layer_dim", "_decal_layer_dim");
		node_shader_write_frag(kong,
		                       "if (abs(dot(constants.decal_layer_nor, constants.decal_layer_loc - wposition.xyz)) > constants.decal_layer_dim) { discard; }");

		node_shader_add_constant(kong, "float brush_scale", "_brush_scale");
		node_shader_write_frag(kong, "tex_coord = uvsp * constants.brush_scale;");
	}

	if (decal) {
		// Tangent-space decal projection
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_texture(kong, "gbuffer0_undo", NULL);
		node_shader_add_constant(kong, "float4 decal_mask", "_decal_mask");
		node_shader_add_constant(kong, "float4x4 VP", "_view_proj_matrix");
		node_shader_add_constant(kong, "float aspect_ratio", "_aspect_ratio_window");
		node_shader_add_constant(kong, "float3 camera_right", "_camera_right");
		node_shader_add_constant(kong, "float3 camera_up", "_camera_up");
		node_shader_add_constant(kong, "float camera_align", "_brush_camera_align");

		node_shader_write_attrib_frag(kong, "float2 uvsp = float2(0.0, 0.0);");

		node_shader_write_attrib_frag(kong, "if (constants.camera_align > 0.0) {");
		// Project the surface point to screen-space for the planar, camera-aligned decal
		node_shader_write_attrib_frag(kong, "float4 sp4 = constants.VP * float4(wposition.xyz, 1.0);");
		node_shader_write_attrib_frag(kong, "float2 sp = sp4.xy / sp4.w;");
		node_shader_write_attrib_frag(kong, "sp.x = sp.x * 0.5 + 0.5;");
		node_shader_write_attrib_frag(kong, "sp.y = 1.0 - (sp.y * 0.5 + 0.5);");
		node_shader_write_attrib_frag(kong, "float ca_depth = sample_lod(gbufferD, sampler_linear, constants.decal_mask.xy, 0.0).r;");
		node_shader_write_attrib_frag(kong, "float2 ca_coord = float2(constants.decal_mask.x * 2.0 - 1.0, 1.0 - constants.decal_mask.y * 2.0);");
		node_shader_write_attrib_frag(kong, "float4 ca_homog = constants.invVP * float4(ca_coord.x, ca_coord.y, ca_depth, 1.0);");
		node_shader_write_attrib_frag(kong, "float ca_clip_w = 1.0 / ca_homog.w;");
		node_shader_write_attrib_frag(kong, "float vp_up_y = (constants.VP * float4(constants.camera_up, 0.0)).y;");
		node_shader_write_attrib_frag(kong, "uvsp = sp.xy - constants.decal_mask.xy;");
		node_shader_write_attrib_frag(kong, "uvsp.x *= constants.aspect_ratio;");
		node_shader_write_attrib_frag(kong, "uvsp = uvsp * (ca_clip_w / (vp_up_y * constants.brush_radius));");
		node_shader_write_attrib_frag(kong, "}");

		node_shader_write_attrib_frag(kong, "else {");
		// When mask is active, anchor the decal at the frozen position
		node_shader_write_attrib_frag(kong, "float2 decal_xy = constants.inp.xy;");
		node_shader_write_attrib_frag(kong, "if (constants.decal_mask.z > 0.0) { decal_xy = constants.decal_mask.xy; }");

		// Unproject the decal anchor point from the depth buffer
		node_shader_write_attrib_frag(kong, "float decal_depth = sample_lod(gbufferD, sampler_linear, decal_xy, 0.0).r;");
		node_shader_write_attrib_frag(kong, "float4 decal_wpos4 = float4(float2(decal_xy.x, 1.0 - decal_xy.y) * 2.0 - 1.0, decal_depth, 1.0);");
		node_shader_write_attrib_frag(kong, "decal_wpos4 = constants.invVP * decal_wpos4;");
		node_shader_write_attrib_frag(kong, "float3 decal_wpos = decal_wpos4.xyz / decal_wpos4.w;");

		// Decode face normal at anchor point
		node_shader_write_attrib_frag(kong, "float2 dg0 = sample_lod(gbuffer0_undo, sampler_linear, decal_xy, 0.0).rg;");
		node_shader_write_attrib_frag(kong, "float3 dn;");
		node_shader_write_attrib_frag(kong, "dn.z = 1.0 - abs(dg0.x) - abs(dg0.y);");
		node_shader_write_attrib_frag(
		    kong, "if (dn.z >= 0.0) { dn.x = dg0.x; dn.y = dg0.y; } else { float2 fw = octahedron_wrap(dg0.xy); dn.x = fw.x; dn.y = fw.y; }");
		node_shader_write_attrib_frag(kong, "dn = normalize(dn);");

		// Build tangent basis
		node_shader_write_attrib_frag(kong, "float3 d_right = constants.camera_right;");
		node_shader_write_attrib_frag(kong, "if (abs(dot(dn, d_right)) > 0.999) { d_right = float3(0.0, 0.0, 1.0); }");
		node_shader_write_attrib_frag(kong, "float3 d_tan = normalize(d_right - dn * dot(d_right, dn));");
		node_shader_write_attrib_frag(kong, "float3 d_bin = cross(d_tan, dn);");

		node_shader_write_attrib_frag(kong, "float3 d_offset = wposition.xyz - decal_wpos;");
		node_shader_write_attrib_frag(kong, "float decal_radius = constants.brush_radius;");
		node_shader_write_attrib_frag(kong, "if (constants.decal_mask.z > 0.0) { decal_radius = constants.decal_mask.w; }");
		node_shader_write_attrib_frag(kong, "if (abs(dot(d_offset, dn)) > decal_radius) { discard; }");
		node_shader_write_attrib_frag(kong, "uvsp = float2(dot(d_offset, d_tan), dot(d_offset, d_bin));");
		node_shader_write_attrib_frag(kong, "uvsp = uvsp / decal_radius * 0.5;");

		node_shader_write_attrib_frag(kong, "}");

		if (g_context->brush_directional) {
			node_shader_add_constant(kong, "float3 brush_direction", "_brush_direction");
			node_shader_write_attrib_frag(kong, "if (constants.brush_direction.z == 0.0) { discard; }");
			node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_direction.x - uvsp.y * constants.brush_direction.y, uvsp.x * "
			                                    "constants.brush_direction.y + uvsp.y * constants.brush_direction.x);");
		}

		f32 angle = g_context->brush_angle + g_context->brush_nodes_angle;
		if (angle != 0.0) {
			node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
			node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
			                                    "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
		}

		node_shader_add_constant(kong, "float brush_scale_x", "_brush_scale_x");
		node_shader_write_attrib_frag(kong, "uvsp.x *= constants.brush_scale_x;");
		node_shader_write_attrib_frag(kong, "uvsp += float2(0.5, 0.5);");
		node_shader_write_attrib_frag(kong, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");
		node_shader_add_constant(kong, "float brush_scale", "_brush_scale");
		node_shader_write_attrib_frag(kong, "tex_coord = uvsp * constants.brush_scale;");
	}

	if (sculpt_triplanar) {
		// Project the world position onto the three axis planes
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_constant(kong, "float brush_scale", "_brush_scale");
		// Decode this vertexs world-space surface normal for blending
		node_shader_write_attrib_frag(kong, "float tri_nv = floor(read_undo.a) / 255.0;");
		node_shader_write_attrib_frag(kong, "float2 tri_oct = float2(read_undo.a - floor(read_undo.a), tri_nv) * 2.0 - 1.0;");
		node_shader_write_attrib_frag(kong, "float tri_nz = 1.0 - abs(tri_oct.x) - abs(tri_oct.y);");
		node_shader_write_attrib_frag(kong, "float3 tri_nor = float3(tri_oct.xy, tri_nz);");
		node_shader_write_attrib_frag(kong, "if (tri_nz < 0.0) { tri_nor.xy = octahedron_wrap(tri_oct.xy); }");
		node_shader_write_attrib_frag(kong, "tri_nor = normalize((constants.W * float4(normalize(tri_nor), 0.0)).xyz);");
		node_shader_write_attrib_frag(kong, "float3 tri_weight = tri_nor * tri_nor;");
		node_shader_write_attrib_frag(kong, "float tri_max = max(tri_weight.x, max(tri_weight.y, tri_weight.z));");
		node_shader_write_attrib_frag(kong, "tri_weight = max(tri_weight - float3(tri_max * 0.75, tri_max * 0.75, tri_max * 0.75), float3(0.0, 0.0, 0.0));");
		node_shader_write_attrib_frag(kong, "float3 tex_coord_blend = tri_weight * (1.0 / (tri_weight.x + tri_weight.y + tri_weight.z));");
		node_shader_write_attrib_frag(kong, "tex_coord = wposition.yz * constants.brush_scale * 0.5;");
		node_shader_write_attrib_frag(kong, "float2 tex_coord1 = wposition.xz * constants.brush_scale * 0.5;");
		node_shader_write_attrib_frag(kong, "float2 tex_coord2 = wposition.xy * constants.brush_scale * 0.5;");
		f32 sculpt_uv_angle = g_context->layer->fill_material != NULL ? g_context->layer->angle : g_context->brush_angle + g_context->brush_nodes_angle;
		if (sculpt_uv_angle != 0.0) {
			node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
			node_shader_write_attrib_frag(kong,
			                              "tex_coord = float2(tex_coord.x * constants.brush_angle.x - tex_coord.y * constants.brush_angle.y, tex_coord.x * "
			                              "constants.brush_angle.y + tex_coord.y * constants.brush_angle.x);");
			node_shader_write_attrib_frag(kong,
			                              "tex_coord1 = float2(tex_coord1.x * constants.brush_angle.x - tex_coord1.y * constants.brush_angle.y, tex_coord1.x * "
			                              "constants.brush_angle.y + tex_coord1.y * constants.brush_angle.x);");
			node_shader_write_attrib_frag(kong,
			                              "tex_coord2 = float2(tex_coord2.x * constants.brush_angle.x - tex_coord2.y * constants.brush_angle.y, tex_coord2.x * "
			                              "constants.brush_angle.y + tex_coord2.y * constants.brush_angle.x);");
		}
		parser_material_triplanar = true;
	}

	// parser_material_parse may add vertex elements
	i32           velen                      = con_paint->data->vertex_elements->length;
	shader_out_t *sout                       = parser_material_parse(g_context->material->canvas, con_paint, kong);
	con_paint->data->vertex_elements->length = velen;
	parser_material_triplanar                = false;
	node_shader_write_frag(kong, string_tmp("float3 disp = %s;", sout->out_basecol));

	if (kong->frag_bposition) {
		kong->frag_bposition = false;
		node_shader_write_attrib_frag(kong, "float3 bposition = wposition.xyz;");
	}

	node_shader_write_frag(kong, "float3 basecol = float3(1.0, 1.0, 1.0);");
	node_shader_write_frag(kong, string_tmp("float opacity = %s;", sout->out_opacity));
	if (g_context->layer->fill_material == NULL) {
		node_shader_write_frag(kong, "opacity *= constants.brush_opacity;");
	}
	else {
		node_shader_write_frag(kong, "opacity *= 20.0;");
	}

	if (g_context->brush_mask_image != NULL && g_context->tool == TOOL_TYPE_DECAL) {
		node_shader_add_texture(kong, "texbrushmask", "_texbrushmask");
		node_shader_write_frag(kong, "float4 mask_sample = sample_lod(texbrushmask, sampler_linear, uvsp, 0.0);");
		if (g_context->brush_mask_image_is_alpha) {
			node_shader_write_frag(kong, "opacity *= mask_sample.a;");
		}
		else {
			node_shader_write_frag(kong, "opacity *= mask_sample.r * mask_sample.a;");
		}
	}
	else if (g_context->tool == TOOL_TYPE_TEXT) {
		node_shader_add_texture(kong, "textexttool", "_textexttool");
		node_shader_write_frag(kong, "opacity *= sample_lod(textexttool, sampler_linear, uvsp, 0.0).r;");
	}

	if (g_context->brush_mask_image != NULL && (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER)) {
		node_shader_add_texture(kong, "texbrushmask", "_texbrushmask");
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_texture(kong, "gbuffer0_undo", NULL);
		node_shader_add_constant(kong, "float3 camera_right", "_camera_right");
		node_shader_write_frag(kong, "float2 mn0 = sample_lod(gbuffer0_undo, sampler_linear, constants.inp.xy, 0.0).rg;");
		node_shader_write_frag(kong, "float3 mn;");
		node_shader_write_frag(kong, "mn.z = 1.0 - abs(mn0.x) - abs(mn0.y);");
		node_shader_write_frag(kong,
		                       "if (mn.z >= 0.0) { mn.x = mn0.x; mn.y = mn0.y; } else { float2 mfw = octahedron_wrap(mn0.xy); mn.x = mfw.x; mn.y = mfw.y; }");
		node_shader_write_frag(kong, "mn = normalize(mn);");
		node_shader_write_frag(kong, "float3 mr = constants.camera_right;");
		node_shader_write_frag(kong, "if (abs(dot(mn, mr)) > 0.999) { mr = float3(0.0, 0.0, 1.0); }");
		node_shader_write_frag(kong, "float3 mt = normalize(mr - mn * dot(mr, mn));");
		node_shader_write_frag(kong, "float3 mb = cross(mt, mn);");
		node_shader_write_frag(kong, "float3 pa_mask_3d = wposition.xyz - winp.xyz;");
		node_shader_write_frag(kong, "float2 pa_mask = float2(dot(pa_mask_3d, mt), dot(pa_mask_3d, mb));");
		if (g_context->brush_directional) {
			node_shader_add_constant(kong, "float3 brush_direction", "_brush_direction");
			node_shader_write_frag(kong, "if (constants.brush_direction.z == 0.0) { discard; }");
			node_shader_write_frag(kong, "pa_mask = float2(pa_mask.x * constants.brush_direction.x - pa_mask.y * constants.brush_direction.y, pa_mask.x * "
			                             "constants.brush_direction.y + pa_mask.y * constants.brush_direction.x);");
		}
		f32 angle = g_context->brush_angle + g_context->brush_nodes_angle;
		if (angle != 0.0) {
			node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
			node_shader_write_frag(kong, "pa_mask.xy = float2(pa_mask.x * constants.brush_angle.x - pa_mask.y * constants.brush_angle.y, pa_mask.x * "
			                             "constants.brush_angle.y + pa_mask.y * constants.brush_angle.x);");
		}
		node_shader_write_frag(kong, "pa_mask = pa_mask / constants.brush_radius * 0.5 + 0.5;");
		node_shader_write_frag(kong, "float4 mask_sample = sample_lod(texbrushmask, sampler_linear, pa_mask, 0.0);");
		if (g_context->brush_mask_image_is_alpha) {
			node_shader_write_frag(kong, "opacity *= mask_sample.a;");
		}
		else {
			node_shader_write_frag(kong, "opacity *= mask_sample.r * mask_sample.a;");
		}
	}

	if (g_context->select_active && has_wposition) {
		node_shader_add_constant(kong, "float4x4 VP", "_view_proj_matrix");
		node_shader_add_constant(kong, "float4 select_mask", "_select_mask");
		node_shader_write_frag(kong, "float4 select_sp4 = constants.VP * float4(wposition.xyz, 1.0);");
		node_shader_write_frag(kong, "float2 select_sp = select_sp4.xy / select_sp4.w;");
		node_shader_write_frag(kong, "select_sp.x = select_sp.x * 0.5 + 0.5;");
		node_shader_write_frag(kong, "select_sp.y = 1.0 - (select_sp.y * 0.5 + 0.5);");
		node_shader_write_frag(kong, "if (select_sp.x < constants.select_mask.x || select_sp.x > constants.select_mask.z || select_sp.y < "
		                             "constants.select_mask.y || select_sp.y > constants.select_mask.w) { discard; }");
	}

	if (g_context->brush_stencil_image != NULL && has_wposition) {
		node_shader_add_constant(kong, "float4x4 VP", "_view_proj_matrix");
		node_shader_add_constant(kong, "float aspect_ratio", "_aspect_ratio_window");
		node_shader_add_texture(kong, "texbrushstencil", "_texbrushstencil");
		node_shader_add_constant(kong, "float2 texbrushstencil_size", "_size(_texbrushstencil)");
		node_shader_add_constant(kong, "float4 stencil_transform", "_stencil_transform");
		node_shader_write_frag(kong, "float4 stencil_sp4 = constants.VP * float4(wposition.xyz, 1.0);");
		node_shader_write_frag(kong, "float2 stencil_sp = stencil_sp4.xy / stencil_sp4.w;");
		node_shader_write_frag(kong, "stencil_sp.x = stencil_sp.x * 0.5 + 0.5;");
		node_shader_write_frag(kong, "stencil_sp.y = 1.0 - (stencil_sp.y * 0.5 + 0.5);");
		node_shader_write_frag(
		    kong,
		    "float2 stencil_uv = (stencil_sp.xy - constants.stencil_transform.xy) / constants.stencil_transform.z * float2(constants.aspect_ratio, 1.0);");
		node_shader_write_frag(kong, "float2 stencil_size = constants.texbrushstencil_size;");
		node_shader_write_frag(kong, "float stencil_ratio = stencil_size.y / stencil_size.x;");
		node_shader_write_frag(kong, "stencil_uv -= float2(0.5 / stencil_ratio, 0.5);");
		node_shader_write_frag(kong,
		                       "stencil_uv = float2(stencil_uv.x * cos(constants.stencil_transform.w) - stencil_uv.y * sin(constants.stencil_transform.w),\
												   stencil_uv.x * sin(constants.stencil_transform.w) + stencil_uv.y * cos(constants.stencil_transform.w));");
		node_shader_write_frag(kong, "stencil_uv += float2(0.5 / stencil_ratio, 0.5);");
		node_shader_write_frag(kong, "stencil_uv.x *= stencil_ratio;");
		node_shader_write_frag(kong, "if (stencil_uv.x < 0.0 || stencil_uv.x > 1.0 || stencil_uv.y < 0.0 || stencil_uv.y > 1.0) { discard; }");
		node_shader_write_frag(kong, "float4 texbrushstencil_sample = sample_lod(texbrushstencil, sampler_linear, stencil_uv, 0.0);");
		if (g_context->brush_stencil_image_is_alpha) {
			node_shader_write_frag(kong, "opacity *= texbrushstencil_sample.a;");
		}
		else {
			node_shader_write_frag(kong, "opacity *= texbrushstencil_sample.r * texbrushstencil_sample.a;");
		}
	}

	node_shader_write_frag(kong, "if (opacity == 0.0) { discard; }");
	if (particle) {
		node_shader_write_frag(kong, "float str = clamp(pow(dist * constants.brush_hardness * 0.2, 2.0) / 10.0, 0.0, 1.0) * opacity;");
	}
	else {
		node_shader_write_frag(kong, "float t = clamp(dist / constants.brush_radius, 0.0, 1.0);");
		node_shader_write_frag(kong, "float t2 = clamp((t - constants.brush_hardness) / max(1.0 - constants.brush_hardness, 0.001), 0.0, 1.0);");
		node_shader_write_frag(kong, "float falloff = 1.0 - t2 * t2 * (3.0 - 2.0 * t2);");
		node_shader_write_frag(kong, "float str = falloff * constants.brush_radius * 0.05 * opacity;");
	}
	if (decal) {
		node_shader_write_frag(kong, "str = str * 0.2;");
	}

	node_shader_add_texture(kong, "texpaint_sculpt_undo", "_texpaint_sculpt_undo");
	node_shader_add_constant(kong, "float2 texpaint_undo_size", "_size(_texpaint_sculpt_undo)");
	node_shader_write_frag(kong, "float4 sample_undo = sample_lod(texpaint_sculpt_undo, sampler_linear, sculpt_uv, 0.0);");
	node_shader_write_frag(kong, "float4 raw_undo = texpaint_sculpt_undo[uint2(uint(sculpt_uv.x * constants.texpaint_undo_size.x), uint(sculpt_uv.y * "
	                             "constants.texpaint_undo_size.y))];");

	if (g_context->layer->fill_material != NULL || g_context->tool == TOOL_TYPE_FILL) {
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_write_frag(kong, "float nor_v = floor(raw_undo.a) / 255.0;");
		node_shader_write_frag(kong, "float2 nor_oct = float2(raw_undo.a - floor(raw_undo.a), nor_v) * 2.0 - 1.0;");
		node_shader_write_frag(kong, "float nor_z = 1.0 - abs(nor_oct.x) - abs(nor_oct.y);");
		node_shader_write_frag(kong, "float3 nor_xyz = float3(nor_oct.xy, nor_z);");
		node_shader_write_frag(kong, "if (nor_z < 0.0) { nor_xyz.xy = octahedron_wrap(nor_oct.xy); }");
		node_shader_write_frag(kong, "float3 n = normalize((constants.W * float4(normalize(nor_xyz), 0.0)).xyz);");
	}
	else {
		node_shader_write_frag(kong, "if (sample_undo.r == 0.0 && sample_undo.g == 0.0 && sample_undo.b == 0.0) { discard; }");
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_texture(kong, "gbuffer0_undo", NULL);
		if (particle) {
			node_shader_add_constant(kong, "float4x4 VP", "_view_proj_matrix");
			node_shader_add_constant(kong, "float3 particle_hit", "_particle_hit");
			node_shader_write_frag(kong, "float4 hit_ndc = constants.VP * float4(constants.particle_hit, 1.0);");
			node_shader_write_frag(kong, "float2 hit_uv = hit_ndc.xy / hit_ndc.w;");
			node_shader_write_frag(kong, "hit_uv.x = hit_uv.x * 0.5 + 0.5;");
			node_shader_write_frag(kong, "hit_uv.y = 1.0 - (hit_uv.y * 0.5 + 0.5);");
			node_shader_write_frag(kong, "float2 g0_undo = sample_lod(gbuffer0_undo, sampler_linear, hit_uv, 0.0).rg;");
		}
		else {
			node_shader_write_frag(kong, "float2 g0_undo = sample_lod(gbuffer0_undo, sampler_linear, constants.inp.xy, 0.0).rg;");
		}
		node_shader_write_frag(kong, "float3 wn;");
		node_shader_write_frag(kong, "wn.z = 1.0 - abs(g0_undo.x) - abs(g0_undo.y);");
		node_shader_write_frag(kong, "if (wn.z >= 0.0) { wn.xy = g0_undo.xy; } else { wn.xy = octahedron_wrap(g0_undo.xy); }");
		node_shader_write_frag(kong, "float3 n = normalize(wn);");
		node_shader_add_constant(kong, "float4x4 sculpt_symmetry_reflect", "_sculpt_symmetry_reflect");
		node_shader_write_frag(kong, "n = normalize((constants.sculpt_symmetry_reflect * float4(n, 0.0)).xyz);");
		if (g_context->xray) {
			node_shader_write_frag(kong, "float xray_nnv = floor(raw_undo.a) / 255.0;");
			node_shader_write_frag(kong, "float2 xray_noct = float2(raw_undo.a - floor(raw_undo.a), xray_nnv) * 2.0 - 1.0;");
			node_shader_write_frag(kong, "float xray_nnz = 1.0 - abs(xray_noct.x) - abs(xray_noct.y);");
			node_shader_write_frag(kong, "float3 xray_fnor = float3(xray_noct.xy, xray_nnz);");
			node_shader_write_frag(kong, "if (xray_nnz < 0.0) { xray_fnor.xy = octahedron_wrap(xray_noct.xy); }");
			node_shader_write_frag(kong, "n = normalize((constants.W * float4(normalize(xray_fnor), 0.0)).xyz);");
		}
	}
	if (g_context->tool == TOOL_TYPE_BLUR) {
		// Even out the surface by relaxing each vertex toward the cursors tangent plane
		node_shader_write_frag(kong, "float plane_dist = dot(wposition.xyz - winp.xyz, n);");
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb - n * plane_dist * str, raw_undo.a);");
	}
	else if (g_context->tool == TOOL_TYPE_ERASER) {
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb - n * disp * str, raw_undo.a);");
	}
	else if (grab || stretch) {
		// Absolute offset from the stroke start, the grabbed area follows the cursor
		node_shader_add_constant(kong, "float4x4 invW", "_inv_world_matrix");
		if (stretch) {
			node_shader_write_frag(kong,
			                       string_tmp("float grab_t = clamp(dist / (constants.brush_radius * %s), 0.0, 1.0);", f32_to_string(SCULPT_STRETCH_AREA)));
			node_shader_write_frag(kong, "float grab_w = 1.0 - grab_t * grab_t * (3.0 - 2.0 * grab_t);");
		}
		else {
			node_shader_write_frag(kong, "float grab_w = falloff;");
		}
		node_shader_write_frag(kong, "float3 grab_delta = (winp.xyz - grab_anchor) * grab_w * clamp(opacity, 0.0, 1.0);");
		node_shader_write_frag(kong, "output[0] = float4(grab_rest.xyz + (constants.invW * float4(grab_delta, 0.0)).xyz, raw_undo.a);");
	}
	else if (mode == SCULPT_TYPE_SMOOTH || mode == SCULPT_TYPE_INFLATE) {
		// Gather the fan-ordered neighbors of this vertex
		node_shader_add_texture(kong, "sculpt_adj0", "_sculpt_adj0");
		node_shader_add_texture(kong, "sculpt_adj1", "_sculpt_adj1");
		node_shader_write_frag(kong, "uint adj_w = uint(constants.texpaint_undo_size.x);");
		node_shader_write_frag(kong,
		                       "uint2 adj_uv = uint2(uint(sculpt_uv.x * constants.texpaint_undo_size.x), uint(sculpt_uv.y * constants.texpaint_undo_size.y));");
		node_shader_write_frag(kong, "float4 adj0 = sculpt_adj0[adj_uv];");
		node_shader_write_frag(kong, "float4 adj1 = sculpt_adj1[adj_uv];");
		char *slots[SCULPT_ADJ_SLOTS] = {"adj0.x", "adj0.y", "adj0.z", "adj0.w", "adj1.x", "adj1.y", "adj1.z", "adj1.w"};
		for (i32 i = 0; i < SCULPT_ADJ_SLOTS; ++i) {
			node_shader_write_frag(kong, string_tmp("float3 adj_p%d = raw_undo.xyz;", i));
			node_shader_write_frag(kong, string_tmp("float adj_v%d = step(0.0, %s);", i, slots[i]));
			node_shader_write_frag(kong, string_tmp("if (%s >= 0.0) {", slots[i]));
			node_shader_write_frag(kong, string_tmp("uint adj_i%d = uint(%s);", i, slots[i]));
			node_shader_write_frag(kong, string_tmp("float4 adj_q%d = texpaint_sculpt_undo[uint2(adj_i%d %% adj_w, adj_i%d / adj_w)];", i, i, i));
			node_shader_write_frag(kong, string_tmp("adj_p%d = adj_q%d.xyz;", i, i));
			node_shader_write_frag(kong, "}");
		}
		node_shader_write_frag(kong, "float adj_count = 0.0;");
		node_shader_write_frag(kong, "float3 adj_sum = float3(0.0, 0.0, 0.0);");
		node_shader_write_frag(kong, "float3 adj_nor = float3(0.0, 0.0, 0.0);");
		for (i32 i = 0; i < SCULPT_ADJ_SLOTS; ++i) {
			node_shader_write_frag(kong, string_tmp("adj_sum += adj_p%d * adj_v%d;", i, i));
			node_shader_write_frag(kong, string_tmp("adj_count += adj_v%d;", i));
			if (i + 1 < SCULPT_ADJ_SLOTS) {
				// Consecutive slots span a triangle, the last slot of a closed fan (-1 terminator) wraps to the first
				node_shader_write_frag(kong,
				                       string_tmp("adj_nor += cross(adj_p%d - raw_undo.xyz, adj_p%d - raw_undo.xyz) * adj_v%d * adj_v%d;", i, i + 1, i, i + 1));
				node_shader_write_frag(
				    kong, string_tmp("adj_nor += cross(adj_p%d - raw_undo.xyz, adj_p0 - raw_undo.xyz) * adj_v%d * (1.0 - adj_v%d) * step(-1.5, %s);", i, i,
				                     i + 1, slots[i + 1]));
			}
			else {
				node_shader_write_frag(kong, string_tmp("adj_nor += cross(adj_p%d - raw_undo.xyz, adj_p0 - raw_undo.xyz) * adj_v%d;", i, i));
			}
		}
		if (mode == SCULPT_TYPE_SMOOTH) {
			node_shader_write_frag(kong, "float3 adj_avg = adj_sum / max(adj_count, 1.0);");
			node_shader_write_frag(kong, "float smooth_str = clamp(falloff * opacity * 0.5, 0.0, 1.0) * step(0.5, adj_count);");
			node_shader_write_frag(kong, "output[0] = float4(lerp(sample_undo.rgb, adj_avg, smooth_str), raw_undo.a);");
		}
		else {
			// Swell along the current vertex normal rather than the cursor normal
			node_shader_write_frag(kong, "float3 inflate_n = adj_nor / max(length(adj_nor), 0.00000001);");
			node_shader_write_frag(kong, string_tmp("float sculpt_disp = %s;", sculpt_blend_mode(kong, g_context->brush_blending, "0.0", "disp.x", "str")));
			node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb + inflate_n * sculpt_disp, raw_undo.a);");
		}
	}
	else if (mode == SCULPT_TYPE_FLATTEN || mode == SCULPT_TYPE_CLAY || mode == SCULPT_TYPE_TRIM) {
		node_shader_add_constant(kong, "float4x4 invW", "_inv_world_matrix");
		node_shader_add_constant(kong, "float4x4 VP", "_view_proj_matrix");
		node_shader_add_constant(kong, "float3 camera_right", "_camera_right");
		node_shader_add_constant(kong, "float3 camera_up", "_camera_up");
		// Average the stroke-start surface around the cursor, so the plane cuts through curved areas instead of touching them
		node_shader_write_frag(kong, "float3 area_c = winp.xyz;");
		node_shader_write_frag(kong, "float area_count = 1.0;");
		node_shader_write_frag(kong, "for (int i = 0; i < 8; i += 1) {");
		node_shader_write_frag(kong, "float area_a = float(i) * 0.785398;");
		node_shader_write_frag(kong, "float3 area_w = winp.xyz + (constants.camera_right * cos(area_a) + constants.camera_up * sin(area_a)) * "
		                             "constants.brush_radius * 0.5;");
		node_shader_write_frag(kong, "float4 area_s4 = constants.VP * float4(area_w, 1.0);");
		node_shader_write_frag(kong, "float2 area_s = area_s4.xy / area_s4.w;");
		node_shader_write_frag(kong, "float2 area_uv = float2(area_s.x * 0.5 + 0.5, 1.0 - (area_s.y * 0.5 + 0.5));");
		node_shader_write_frag(kong, "float area_depth = sample_lod(gbufferD, sampler_linear, area_uv, 0.0).r;");
		node_shader_write_frag(kong, "float4 area_p4 = constants.invVP * float4(area_s, area_depth, 1.0);");
		node_shader_write_frag(kong, "float3 area_p = area_p4.xyz / area_p4.w;");
		// Skip background and depth discontinuities
		node_shader_write_frag(kong, "if (distance(area_p, winp.xyz) < constants.brush_radius) { area_c += area_p; area_count += 1.0; }");
		node_shader_write_frag(kong, "}");
		node_shader_write_frag(kong, "area_c = area_c / area_count;");
		// Slide the plane along the stroke segment so the whole capsule is not pulled toward its end
		node_shader_write_frag(kong, "float3 area_ba = winplast.xyz - winp.xyz;");
		node_shader_write_frag(kong, "area_c += area_ba * clamp(dot(wposition.xyz - winp.xyz, area_ba) / max(dot(area_ba, area_ba), 0.00000001), 0.0, 1.0);");
		if (mode == SCULPT_TYPE_FLATTEN) {
			// Pull toward the area plane from both sides
			node_shader_write_frag(kong, "float3 plane_delta = -n * dot(wposition.xyz - area_c, n) * clamp(falloff * opacity * 0.25, 0.0, 1.0);");
		}
		else if (mode == SCULPT_TYPE_TRIM) {
			// Shave off whatever rises above a plane sunk slightly below the area, so it cuts a flat facet; inverted modes fill the dips below a raised plane
			node_shader_write_frag(kong, invert ? "float3 trim_n = -n;" : "float3 trim_n = n;");
			node_shader_write_frag(kong, "float trim_d = max(dot(wposition.xyz - area_c, trim_n) + constants.brush_radius * 0.1 * disp.x, 0.0);");
			node_shader_write_frag(kong, "float3 plane_delta = -trim_n * trim_d * clamp(falloff * opacity * 0.5, 0.0, 1.0);");
		}
		else {
			// Build up toward a plane floating above the surface, filling dips first and capping the height
			node_shader_write_frag(kong, invert ? "float3 clay_n = -n;" : "float3 clay_n = n;");
			node_shader_write_frag(kong, "float clay_d = dot(area_c + clay_n * constants.brush_radius * 0.15 - wposition.xyz, clay_n);");
			node_shader_write_frag(kong, "float3 plane_delta = clay_n * clamp(clay_d, 0.0, str * disp.x);");
		}
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb + (constants.invW * float4(plane_delta, 0.0)).xyz, raw_undo.a);");
	}
	else if (cloth) {
		// Drag the brush area with the cursor, then relax the edges toward their stroke-start lengths so the surrounding area follows like cloth
		node_shader_add_constant(kong, "float4x4 invW", "_inv_world_matrix");
		node_shader_add_constant(kong, "float sculpt_cloth_drag", "_sculpt_cloth_drag");
		node_shader_add_texture(kong, "texpaint_sculpt_stroke", "_texpaint_sculpt_stroke");
		node_shader_add_texture(kong, "sculpt_adj0", "_sculpt_adj0");
		node_shader_add_texture(kong, "sculpt_adj1", "_sculpt_adj1");
		node_shader_write_frag(kong, "uint adj_w = uint(constants.texpaint_undo_size.x);");
		node_shader_write_frag(kong,
		                       "uint2 adj_uv = uint2(uint(sculpt_uv.x * constants.texpaint_undo_size.x), uint(sculpt_uv.y * constants.texpaint_undo_size.y));");
		node_shader_write_frag(kong, "float4 adj0 = sculpt_adj0[adj_uv];");
		node_shader_write_frag(kong, "float4 adj1 = sculpt_adj1[adj_uv];");
		node_shader_write_frag(kong, "float4 cloth_rest = texpaint_sculpt_stroke[adj_uv];");
		node_shader_write_frag(kong, "float3 cloth_corr = float3(0.0, 0.0, 0.0);");
		node_shader_write_frag(kong, "float cloth_count = 0.0;");
		char *slots[SCULPT_ADJ_SLOTS] = {"adj0.x", "adj0.y", "adj0.z", "adj0.w", "adj1.x", "adj1.y", "adj1.z", "adj1.w"};
		for (i32 i = 0; i < SCULPT_ADJ_SLOTS; ++i) {
			node_shader_write_frag(kong, string_tmp("if (%s >= 0.0) {", slots[i]));
			node_shader_write_frag(kong, string_tmp("uint cloth_i%d = uint(%s);", i, slots[i]));
			node_shader_write_frag(kong, string_tmp("uint2 cloth_uv%d = uint2(cloth_i%d %% adj_w, cloth_i%d / adj_w);", i, i, i));
			node_shader_write_frag(kong, string_tmp("float4 cloth_q%d = texpaint_sculpt_undo[cloth_uv%d];", i, i));
			node_shader_write_frag(kong, string_tmp("float4 cloth_r%d = texpaint_sculpt_stroke[cloth_uv%d];", i, i));
			node_shader_write_frag(kong, string_tmp("float3 cloth_d%d = cloth_q%d.xyz - raw_undo.xyz;", i, i));
			node_shader_write_frag(kong, string_tmp("float cloth_l%d = length(cloth_d%d);", i, i));
			node_shader_write_frag(kong, string_tmp("float cloth_l0%d = length(cloth_r%d.xyz - cloth_rest.xyz);", i, i));
			// Move halfway, the neighbor takes the other half
			node_shader_write_frag(kong, string_tmp("cloth_corr += cloth_d%d * (0.5 - 0.5 * cloth_l0%d / max(cloth_l%d, 0.00000001));", i, i, i));
			node_shader_write_frag(kong, "cloth_count += 1.0;");
			node_shader_write_frag(kong, "}");
		}
		node_shader_write_frag(kong, "cloth_corr = cloth_corr / max(cloth_count, 1.0);");
		// Fade the relaxation out toward the area edge, vertices outside stay pinned
		node_shader_write_frag(kong, string_tmp("float cloth_t = clamp((dist / (constants.brush_radius * %s) - 0.7) / 0.3, 0.0, 1.0);",
		                                        f32_to_string(SCULPT_CLOTH_AREA)));
		node_shader_write_frag(kong, "float cloth_pin = 1.0 - cloth_t * cloth_t * (3.0 - 2.0 * cloth_t);");
		node_shader_write_frag(kong, "float3 cloth_drag = (winp.xyz - winplast.xyz) * falloff * opacity * constants.sculpt_cloth_drag;");
		node_shader_write_frag(kong,
		                       "output[0] = float4(sample_undo.rgb + cloth_corr * cloth_pin + (constants.invW * float4(cloth_drag, 0.0)).xyz, raw_undo.a);");
	}
	else if (mode == SCULPT_TYPE_TWIST) {
		// Rotate around the surface normal axis through the cursor, inverted modes twist the other way
		node_shader_add_constant(kong, "float4x4 invW", "_inv_world_matrix");
		node_shader_write_frag(kong, string_tmp("float twist_a = falloff * opacity * disp.x * %s;", invert ? "-0.1" : "0.1"));
		node_shader_write_frag(kong, "float3 twist_p = wposition.xyz - winp.xyz;");
		node_shader_write_frag(kong, "float twist_c = cos(twist_a);");
		node_shader_write_frag(kong, "float twist_s = sin(twist_a);");
		node_shader_write_frag(kong, "float3 twist_r = twist_p * twist_c + cross(n, twist_p) * twist_s + n * dot(n, twist_p) * (1.0 - twist_c);");
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb + (constants.invW * float4(twist_r - twist_p, 0.0)).xyz, raw_undo.a);");
	}
	else if (mode == SCULPT_TYPE_PLATEAU) {
		// Level everything to the plane of the stroke-start point, so the whole stroke ends up at one height
		node_shader_add_constant(kong, "float4x4 invW", "_inv_world_matrix");
		node_shader_add_constant(kong, "float2 grab_start", "_grab_start");
		node_shader_write_frag(kong, "float plat_depth = sample_lod(gbufferD, sampler_linear, constants.grab_start, 0.0).r;");
		node_shader_write_frag(kong, "float3 plat_c = winp.xyz;");
		node_shader_write_frag(kong, "float3 plat_n = n;");
		// Strokes starting off the mesh fall back to the cursor plane
		node_shader_write_frag(kong, "if (plat_depth < 1.0) {");
		node_shader_write_frag(
		    kong, "float4 plat_c4 = constants.invVP * float4(float2(constants.grab_start.x, 1.0 - constants.grab_start.y) * 2.0 - 1.0, plat_depth, 1.0);");
		node_shader_write_frag(kong, "plat_c = plat_c4.xyz / plat_c4.w;");
		node_shader_write_frag(kong, "float2 plat_g0 = sample_lod(gbuffer0_undo, sampler_linear, constants.grab_start, 0.0).rg;");
		node_shader_write_frag(kong, "plat_n.z = 1.0 - abs(plat_g0.x) - abs(plat_g0.y);");
		node_shader_write_frag(kong, "if (plat_n.z >= 0.0) { plat_n.xy = plat_g0.xy; } else { plat_n.xy = octahedron_wrap(plat_g0.xy); }");
		node_shader_write_frag(kong, "plat_n = normalize(plat_n);");
		node_shader_write_frag(kong, "}");
		node_shader_write_frag(kong, "float3 plat_delta = -plat_n * dot(wposition.xyz - plat_c, plat_n) * clamp(falloff * opacity * 0.5, 0.0, 1.0);");
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb + (constants.invW * float4(plat_delta, 0.0)).xyz, raw_undo.a);");
	}
	else if (mode == SCULPT_TYPE_PINCH || mode == SCULPT_TYPE_CREASE) {
		// Pull vertices sideways toward the stroke line
		node_shader_add_constant(kong, "float4x4 invW", "_inv_world_matrix");
		node_shader_write_frag(kong, "float3 pinch_ba = winplast.xyz - winp.xyz;");
		node_shader_write_frag(kong, "float pinch_h = clamp(dot(wposition.xyz - winp.xyz, pinch_ba) / max(dot(pinch_ba, pinch_ba), 0.00000001), 0.0, 1.0);");
		node_shader_write_frag(kong, "float3 pinch_d = winp.xyz + pinch_ba * pinch_h - wposition.xyz;");
		node_shader_write_frag(kong, "pinch_d -= n * dot(pinch_d, n);");
		node_shader_write_frag(kong, string_tmp("float3 pinch_delta = pinch_d * clamp(falloff * opacity * 0.25, 0.0, 1.0) * %s;", invert ? "-1.0" : "1.0"));
		node_shader_write_frag(kong, "float3 pinch_out = sample_undo.rgb + (constants.invW * float4(pinch_delta, 0.0)).xyz;");
		if (mode == SCULPT_TYPE_CREASE) {
			// Carve a groove into the pinched area, inverted modes raise a sharp ridge
			node_shader_write_frag(kong, string_tmp("pinch_out += n * str * disp.x * %s;", invert ? "0.5" : "-0.5"));
		}
		node_shader_write_frag(kong, "output[0] = float4(pinch_out, raw_undo.a);");
	}
	else {
		node_shader_write_frag(kong, string_tmp("float sculpt_disp = %s;", sculpt_blend_mode(kong, g_context->brush_blending, "0.0", "disp.x", "str")));
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb + n * sculpt_disp, raw_undo.a);");
	}
	node_shader_write_frag(kong, "output[1] = float4(str, 0.0, 0.0, 1.0);");
	parser_material_finalize(con_paint);
	con_paint->data->shader_from_source = true;
	gpu_create_shaders_from_kong(node_shader_get(kong), &con_paint->data->vertex_shader, &con_paint->data->fragment_shader,
	                             &con_paint->data->_->vertex_shader_size, &con_paint->data->_->fragment_shader_size);
	return con_paint;
}

static void sculpt_mesh_write(node_shader_t *kong, bool attrib, char *s) {
	if (attrib) {
		node_shader_write_attrib_vert(kong, s);
	}
	else {
		node_shader_write_vert(kong, s);
	}
}

bool sculpt_layer_has_visible_masks(slot_layer_t *l) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	if (masks == NULL) {
		return false;
	}
	for (i32 i = 0; i < masks->length; ++i) {
		if (slot_layer_is_visible(masks->buffer[i])) {
			return true;
		}
	}
	return false;
}

bool sculpt_mask_value(node_shader_t *kong, slot_layer_t *l, char *out_var, bool attrib, slot_layer_t *skip) {
	if (!sculpt_layer_has_visible_masks(l)) {
		return false;
	}
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	sculpt_mesh_write(kong, attrib, string_tmp("float %s = 1.0;", out_var));
	for (i32 i = 0; i < masks->length; ++i) {
		slot_layer_t *m = masks->buffer[i];
		if (!slot_layer_is_visible(m) || m == skip) {
			continue;
		}
		node_shader_add_texture(kong, string_tmp("texpaint_vert%s", i32_to_string(m->id)), string_tmp("_texpaint_vert%s", i32_to_string(m->id)));
		f32 opac = slot_layer_get_opacity(m);
		sculpt_mesh_write(kong, attrib,
		                  string_tmp("%s *= lerp(1.0, sample_lod(texpaint_vert%s, sampler_linear, input.tex, 0.0).r, float(%s));", out_var,
		                             i32_to_string(m->id), f32_to_string(opac)));
	}
	sculpt_mesh_write(kong, attrib, string_tmp("%s = clamp(%s, 0.0, 1.0);", out_var, out_var));
	return true;
}

// Look up the texel of a mesh vertex in the remap texture
static void sculpt_write_texel_uv(node_shader_t *kong, bool attrib, char *out, char *vertex, char *size) {
	node_shader_add_texture(kong, "sculpt_remap", "_sculpt_remap");
	node_shader_add_constant(kong, "float2 sculpt_remap_size", "_size(_sculpt_remap)");
	sculpt_mesh_write(kong, attrib, string_tmp("uint %s_vid = %s;", out, vertex));
	sculpt_mesh_write(kong, attrib, string_tmp("uint %s_rw = uint(constants.sculpt_remap_size.x);", out));
	sculpt_mesh_write(kong, attrib, string_tmp("float4 %s_remap = sculpt_remap[uint2(%s_vid %% %s_rw, %s_vid / %s_rw)];", out, out, out, out, out));
	sculpt_mesh_write(kong, attrib, string_tmp("uint %s_texel = uint(%s_remap.r);", out, out));
	sculpt_mesh_write(kong, attrib, string_tmp("uint %s_w = uint(constants.%s.x);", out, size));
	sculpt_mesh_write(kong, attrib, string_tmp("uint2 %s = uint2(%s_texel %% %s_w, %s_texel / %s_w);", out, out, out, out, out));
}

void sculpt_make_mesh_run(node_shader_t *kong, slot_layer_t_array_t *sculpt_layers, i32_array_t *sculpt_indices) {
	i32 count = sculpt_layers->length;
	if (count == 0) {
		return;
	}

	i32 idx0 = sculpt_indices->buffer[0];

	node_shader_add_constant(kong, "float4x4 WVP", "_world_view_proj_matrix");
	node_shader_add_constant(kong, "float4x4 W", "_world_matrix");
	node_shader_add_constant(kong, "float3x3 N", "_normal_matrix");
	// Per-object start index into the shared sculpt grid
	node_shader_add_constant(kong, "int sculpt_vertex_offset", "_sculpt_vertex_offset");
	node_shader_add_out(kong, "float3 wnormal");
	kong->frag_n = false;

	node_shader_add_constant(kong, string_tmp("float2 texpaint_sculpt_size%d", idx0), string_tmp("_size(_texpaint_sculpt%d)", idx0));

	for (i32 i = 0; i < count; ++i) {
		i32 idx = sculpt_indices->buffer[i];
		node_shader_add_texture(kong, string_tmp("texpaint_sculpt%d", idx), string_tmp("_texpaint_sculpt%d", idx));
	}

	// The base layer holds the absolute mesh position
	bool base_masked = sculpt_layer_has_visible_masks(sculpt_layers->buffer[0]);
	if (count > 1 || base_masked) {
		node_shader_add_texture(kong, "texpaint_sculpt_base", "_texpaint_sculpt_base");
	}

	// Position
	sculpt_write_texel_uv(kong, false, "sculpt_uv", "uint(vertex_id()) + uint(constants.sculpt_vertex_offset)", string_tmp("texpaint_sculpt_size%d", idx0));
	node_shader_write_vert(kong, string_tmp("float4 texpaint_sculpt_sample = texpaint_sculpt%d[sculpt_uv];", idx0));
	node_shader_write_vert(kong, "float3 sculpt_pos = texpaint_sculpt_sample.xyz;");
	if (sculpt_mask_value(kong, sculpt_layers->buffer[0], "sculpt_pmask0", false, NULL)) {
		// Blend the base layers deformation back toward the rest pose where the mask is dark
		node_shader_write_vert(kong, "float4 sculpt_pbase0 = texpaint_sculpt_base[sculpt_uv];");
		node_shader_write_vert(kong, "sculpt_pos = sculpt_pbase0.xyz + (sculpt_pos - sculpt_pbase0.xyz) * sculpt_pmask0;");
	}
	for (i32 i = 1; i < count; ++i) {
		i32 idx = sculpt_indices->buffer[i];
		node_shader_write_vert(kong, string_tmp("float4 texpaint_sculpt_sample_%d = texpaint_sculpt%d[sculpt_uv];", idx, idx));
		node_shader_write_vert(kong, string_tmp("float4 texpaint_sculpt_base_sample_%d = texpaint_sculpt_base[sculpt_uv];", idx));
		if (sculpt_mask_value(kong, sculpt_layers->buffer[i], string_tmp("sculpt_pmask_%d", idx), false, NULL)) {
			node_shader_write_vert(
			    kong,
			    string_tmp("sculpt_pos = sculpt_pos + (texpaint_sculpt_sample_%d.xyz - texpaint_sculpt_base_sample_%d.xyz) * sculpt_pmask_%d;", idx, idx, idx));
		}
		else {
			node_shader_write_vert(kong, string_tmp("sculpt_pos = sculpt_pos + texpaint_sculpt_sample_%d.xyz - texpaint_sculpt_base_sample_%d.xyz;", idx, idx));
		}
	}
	node_shader_write_vert(kong, "output.pos = constants.WVP * float4(sculpt_pos, 1.0);");
	node_shader_write_vert(kong, "output.wposition = (constants.W * float4(sculpt_pos, 1.0)).xyz;");

	// Faceted normal, the rest pose normal only decides which side faces out
	node_shader_write_attrib_vert(kong, "output.wnormal = constants.N * float3(input.nor.xy, input.pos.w);");
	// Reconstruct the face normal from the masked world position
	node_shader_write_attrib_frag(kong, "float3 n = normalize(cross(ddx(input.wposition), ddy(input.wposition)));");
	node_shader_write_attrib_frag(kong, "if (dot(n, normalize(input.wnormal)) < 0.0) { n = -n; }");
}

void sculpt_make_paint_run(node_shader_t *kong) {
	slot_layer_t_array_t *sculpt_layers  = any_array_create_from_raw((void *[]){}, 0);
	i32_array_t          *sculpt_indices = i32_array_create(0);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->texpaint_sculpt != NULL && slot_layer_is_visible(l)) {
			any_array_push(sculpt_layers, l);
			i32_array_push(sculpt_indices, i);
		}
	}
	i32 scount = sculpt_layers->length;
	if (scount > 0) {
		i32 idx0 = sculpt_indices->buffer[0];
		node_shader_add_constant(kong, string_tmp("float2 texpaint_sculpt_size%d", idx0), string_tmp("_size(_texpaint_sculpt%d)", idx0));
		for (i32 i = 0; i < scount; ++i) {
			i32 idx = sculpt_indices->buffer[i];
			node_shader_add_texture(kong, string_tmp("texpaint_sculpt%d", idx), string_tmp("_texpaint_sculpt%d", idx));
		}

		bool base_masked = sculpt_layer_has_visible_masks(sculpt_layers->buffer[0]);
		if (scount > 1 || base_masked) {
			node_shader_add_texture(kong, "texpaint_sculpt_base", "_texpaint_sculpt_base");
		}

		sculpt_write_texel_uv(kong, true, "sculpt_uv_paint", "uint(vertex_id())", string_tmp("texpaint_sculpt_size%d", idx0));

		node_shader_write_attrib_vert(kong, string_tmp("float4 sculpt_pos_paint = texpaint_sculpt%d[sculpt_uv_paint];", idx0));
		if (sculpt_mask_value(kong, sculpt_layers->buffer[0], "sculpt_pmask0", true, g_context->layer)) {
			node_shader_write_attrib_vert(kong, "float4 sculpt_pbase0 = texpaint_sculpt_base[sculpt_uv_paint];");
			node_shader_write_attrib_vert(kong, "sculpt_pos_paint = sculpt_pbase0 + (sculpt_pos_paint - sculpt_pbase0) * sculpt_pmask0;");
		}
		for (i32 i = 1; i < scount; ++i) {
			i32 idx = sculpt_indices->buffer[i];
			if (sculpt_mask_value(kong, sculpt_layers->buffer[i], string_tmp("sculpt_pmask_%d", idx), true, g_context->layer)) {
				node_shader_write_attrib_vert(kong, string_tmp("float4 psamp_%d = texpaint_sculpt%d[sculpt_uv_paint];", idx, idx));
				node_shader_write_attrib_vert(kong, string_tmp("float4 pbasel_%d = texpaint_sculpt_base[sculpt_uv_paint];", idx));
				node_shader_write_attrib_vert(kong,
				                              string_tmp("sculpt_pos_paint = sculpt_pos_paint + (psamp_%d - pbasel_%d) * sculpt_pmask_%d;", idx, idx, idx));
			}
			else {
				node_shader_write_attrib_vert(
				    kong, string_tmp("sculpt_pos_paint = sculpt_pos_paint + texpaint_sculpt%d[sculpt_uv_paint] - texpaint_sculpt_base[sculpt_uv_paint];", idx));
			}
		}
		node_shader_write_attrib_vert(kong, "output.ndc = constants.WVP * float4(sculpt_pos_paint.xyz, 1.0);");
		if (kong->frag_wposition) {
			node_shader_write_attrib_vert(kong, "output.wposition = (constants.W * float4(sculpt_pos_paint.xyz, 1.0)).xyz;");
		}
	}

	array_delete(sculpt_layers);
	array_delete(sculpt_indices);
}

void sculpt_init_sculpt_texture(slot_layer_t *l) {
	i32 id = l->id;
	{
		render_target_t *t = render_target_create();
		t->name            = string("texpaint_sculpt%s", i32_to_string(id));
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = "RGBA128";
		l->texpaint_sculpt = render_path_create_render_target(t)->_image;
	}
	sculpt_import_mesh_pack_to_texture(l->texpaint_sculpt);

	i32 sculpt_layer_count = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		if (g_project->_->layers->buffer[i]->texpaint_sculpt != NULL) {
			sculpt_layer_count++;
		}
	}

	if (any_map_get(render_path_render_targets, "texpaint_sculpt_base") == NULL) {
		render_target_t *t = render_target_create();
		t->name            = "texpaint_sculpt_base";
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = "RGBA128";
		render_path_create_render_target(t);
	}

	if (sculpt_layer_count == 1) {
		render_path_set_target("texpaint_sculpt_base", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string_tmp("texpaint_sculpt%s", i32_to_string(id)), "tex");
		render_path_draw_shader("Scene/copy_pass/copyRGBA128_pass");
	}
}

void sculpt_init_meshes() {
	mesh_object_t_array_t *objects   = g_project->_->paint_objects;
	f32                    max_scale = 0.0;
	for (i32 o = 0; o < objects->length; ++o) {
		if (objects->buffer[o]->data->scale_pos > max_scale) {
			max_scale = objects->buffer[o]->data->scale_pos;
		}
	}

	// Keep the mesh indexed, only bring all objects to a shared position scale
	for (i32 o = 0; o < objects->length; ++o) {
		mesh_object_t *object = objects->buffer[o];
		mesh_data_t   *md     = object->data;
		f32            ratio  = objects->length > 1 ? md->scale_pos / max_scale : 1.0;
		i16_array_t   *pos    = md->vertex_arrays->buffer[0]->values;
		i32            n      = pos->length / 4;

		i16_array_t *posa = i16_array_create(n * 4);
		i16_array_t *nora = i16_array_create(n * 2);
		i16_array_t *texa = i16_array_create(n * 2);
		for (i32 i = 0; i < n; ++i) {
			posa->buffer[i * 4]     = math_floor(pos->buffer[i * 4] * ratio);
			posa->buffer[i * 4 + 1] = math_floor(pos->buffer[i * 4 + 1] * ratio);
			posa->buffer[i * 4 + 2] = math_floor(pos->buffer[i * 4 + 2] * ratio);
			posa->buffer[i * 4 + 3] = pos->buffer[i * 4 + 3];
			nora->buffer[i * 2]     = md->vertex_arrays->buffer[1]->values->buffer[i * 2];
			nora->buffer[i * 2 + 1] = md->vertex_arrays->buffer[1]->values->buffer[i * 2 + 1];
			texa->buffer[i * 2]     = md->vertex_arrays->buffer[2]->values->buffer[i * 2];
			texa->buffer[i * 2 + 1] = md->vertex_arrays->buffer[2]->values->buffer[i * 2 + 1];
		}
		u32_array_t *inda = u32_array_create(md->index_array->length);
		for (i32 i = 0; i < inda->length; ++i) {
			inda->buffer[i] = md->index_array->buffer[i];
		}
		mesh_data_t *raw = ALLOC_INIT(mesh_data_t, {.name          = md->name,
		                                            .vertex_arrays = any_array_create_from_raw(
		                                                (void *[]){
		                                                    ALLOC_INIT(vertex_array_t, {.values = posa, .attrib = "pos", .data = "short4norm"}),
		                                                    ALLOC_INIT(vertex_array_t, {.values = nora, .attrib = "nor", .data = "short2norm"}),
		                                                    ALLOC_INIT(vertex_array_t, {.values = texa, .attrib = "tex", .data = "short2norm"}),
		                                                },
		                                                3),
		                                            .index_array = inda,
		                                            .scale_pos   = 1.0,
		                                            .scale_tex   = 1.0});
		mesh_object_set_data(object, mesh_data_create(raw));
	}

	sculpt_build_weld();
	sculpt_ensure_texture_res();

	if (objects->length > 1) {
		f32 ex = 0.0, ey = 0.0, ez = 0.0;
		for (i32 o = 0; o < objects->length; ++o) {
			vec4_t aabb = mesh_data_calculate_aabb(objects->buffer[o]->data);
			ex          = fmaxf(ex, aabb.x);
			ey          = fmaxf(ey, aabb.y);
			ez          = fmaxf(ez, aabb.z);
		}
		f32            r                    = math_sqrt(ex * ex + ey * ey + ez * ez);
		mesh_object_t *main_object          = context_main_object();
		main_object->base->transform->loc   = (vec4_t){0, 0, 0, 1.0};
		main_object->base->transform->scale = (vec4_t){2.0 / r, 2.0 / r, 2.0 / r, 1.0};
		transform_build_matrix(main_object->base->transform);
	}
}

void sculpt_init() {
	sculpt_init_meshes();

	if (g_context->merged_object != NULL) {
		util_mesh_merge(NULL);
	}

	make_material_parse_paint_material(true);
	make_material_parse_mesh_material();

	if (any_map_get(render_path_render_targets, "gbuffer0_undo") != NULL) {
		return;
	}

	{
		render_target_t *t = render_target_create();
		t->name            = "gbuffer0_undo";
		t->width           = 0;
		t->height          = 0;
		t->format          = "RGBA64";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "gbufferD_undo";
		t->width           = 0;
		t->height          = 0;
		t->format          = "R32";
		t->scale           = render_path_base_get_super_sampling();
		render_path_create_render_target(t);
	}
	{
		// Holds the previous-frame sculpt state during a stroke
		render_target_t *t = render_target_create();
		t->name            = "texpaint_sculpt_ref";
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = "RGBA128";
		render_path_create_render_target(t);
	}

	render_path_load_shader("Scene/copy_pass/copyR32_pass");

	for (i32 i = 0; i < history_undo_layers->length; ++i) {
		char            *ext = string_tmp("_undo%s", i32_to_string(i));
		slot_layer_t    *ul  = history_undo_layers->buffer[i];
		render_target_t *t   = render_target_create();
		t->name              = string("texpaint_sculpt%s", ext);
		t->width             = config_get_texture_res_x();
		t->height            = config_get_texture_res_y();
		t->format            = "RGBA128";
		ul->texpaint_sculpt  = render_path_create_render_target(t)->_image;
	}
}

void sculpt_layers_create_sculpt_layer() {
	slot_layer_t *l    = layers_new_layer(true, -1, NULL);
	char         *name = string_copy(slot_layer_unique_name(l, string_tmp("Sculpt %d", l->id + 1)));
	tab_stages_rename_layer(l->name, name);
	l->name = name;
	sculpt_init_meshes();
	sculpt_init_sculpt_texture(l);
	sculpt_init();
}

void render_path_sculpt_displace_pass(char *texpaint_sculpt) {
	// The adjacency texture is addressed with the sculpt texture width
	if (sculpt_mode_uses_adjacency() && (sculpt_adj_texture0 == NULL || sculpt_adj_texture0->width != config_get_texture_res_x())) {
		sculpt_build_weld();
	}

	// Snapshot the current state so the displacement pass accumulates on top of the previous one
	render_path_set_target("texpaint_sculpt_ref", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target(texpaint_sculpt, "tex");
	render_path_draw_shader("Scene/copy_pass/copyRGBA128_pass");

	render_path_set_target("texpaint_blend1", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("texpaint_blend0", "tex");
	render_path_draw_shader("Scene/copy_pass/copyR8_pass");
	string_array_t *additional = any_array_create_from_raw(
	    (void *[]){
	        "texpaint_blend0",
	    },
	    1);
	render_path_set_target(texpaint_sculpt, additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("gbufferD_undo", "gbufferD");
	if (g_context->xray || g_config->brush_angle_reject) {
		render_path_bind_target("gbuffer0", "gbuffer0");
	}
	render_path_bind_target("texpaint_blend1", "paintmask");
	render_path_bind_target("gbuffer0_undo", "gbuffer0_undo");

	shader_data_t    *mat            = g_project->_->paint_objects->buffer[0]->material;
	shader_context_t *shader_context = shader_data_get_context(mat, "paint");

	gpu_set_pipeline(shader_context->_->pipe);
	uniforms_set_context_consts(shader_context, _render_path_bind_params);
	uniforms_set_obj_consts(shader_context, g_project->_->paint_objects->buffer[0]->base);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	render_path_end();
}

void render_path_sculpt_commands() {
	if (g_context->pdirty <= 0) {
		return;
	}

	i32   tid             = g_context->layer->id;
	char *texpaint_sculpt = string_tmp("texpaint_sculpt%s", i32_to_string(tid));

	if (g_context->tool == TOOL_TYPE_PARTICLE) {
		// Accumulate one displacement pass per active particle impact
		for (i32 pi = 0; pi < 32; ++pi) {
			if (g_context->particles[pi].timer == NULL || g_context->particles[pi].hit_x == 0) {
				continue;
			}
			g_context->particle_index      = pi;
			g_context->particle_hit_x      = g_context->particles[pi].hit_x;
			g_context->particle_hit_y      = g_context->particles[pi].hit_y;
			g_context->particle_hit_z      = g_context->particles[pi].hit_z;
			g_context->last_particle_hit_x = g_context->particles[pi].hit_last_x;
			g_context->last_particle_hit_y = g_context->particles[pi].hit_last_y;
			g_context->last_particle_hit_z = g_context->particles[pi].hit_last_z;
			render_path_sculpt_displace_pass(texpaint_sculpt);
		}
		return;
	}

	if (g_context->tool == TOOL_TYPE_BRUSH && g_context->brush_sculpt == SCULPT_TYPE_CLOTH) {
		// Drag once, then keep relaxing the edge constraints so the pull spreads through the cloth area
		for (i32 i = 0; i < SCULPT_CLOTH_ITERATIONS; ++i) {
			sculpt_cloth_drag = i == 0 ? 1.0 : 0.0;
			render_path_sculpt_displace_pass(texpaint_sculpt);
		}
		sculpt_cloth_drag = 1.0;
		return;
	}

	render_path_sculpt_displace_pass(texpaint_sculpt);
}

void render_path_sculpt_snapshot_gbuffer() {
	render_path_set_target("gbuffer0_undo", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("gbuffer0", "tex");
	render_path_draw_shader("Scene/copy_pass/copyRGBA64_pass");
	render_path_set_target("gbufferD_undo", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("main", "tex");
	render_path_draw_shader("Scene/copy_pass/copyR32_pass");
}

void render_path_sculpt_begin() {
	if (!render_path_paint_paint_enabled()) {
		return;
	}
	render_path_paint_push_undo_last = history_push_undo;
	if (history_push_undo && history_undo_layers != NULL) {
		history_paint();
		render_path_sculpt_snapshot_gbuffer();
		g_context->grab_start_x = g_context->paint_vec.x;
		g_context->grab_start_y = g_context->paint_vec.y;
	}
	sculpt_push_undo = false;
}

void sculpt_bake_to_mesh() {
	slot_layer_t_array_t *sculpt_layers = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->texpaint_sculpt != NULL && slot_layer_is_visible(l)) {
			any_array_push(sculpt_layers, l);
		}
	}
	i32 count = sculpt_layers->length;
	if (count == 0) {
		array_delete(sculpt_layers);
		return;
	}

	if (g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}
	mesh_data_t *g   = g_context->merged_object->data;
	i16_array_t *va0 = g->vertex_arrays->buffer[0]->values;
	i16_array_t *va1 = g->vertex_arrays->buffer[1]->values;
	u32_array_t *ia  = g->index_array;
	i32          nv  = math_floor(va0->length / 4.0);
	i32          nt  = sculpt_texel_count;

	// The base layer stores absolute positions, higher layers store deltas from the rest pose
	buffer_t  *base_pixels  = gpu_get_texture_pixels(sculpt_layers->buffer[0]->texpaint_sculpt);
	buffer_t  *rest_pixels  = NULL;
	buffer_t **layer_pixels = NULL;
	if (count > 1) {
		render_target_t *rest = any_map_get(render_path_render_targets, "texpaint_sculpt_base");
		rest_pixels           = gpu_get_texture_pixels(rest->_image);
		layer_pixels          = malloc(sizeof(buffer_t *) * count);
		for (i32 i = 1; i < count; ++i) {
			layer_pixels[i] = gpu_get_texture_pixels(sculpt_layers->buffer[i]->texpaint_sculpt);
		}
	}

	f32_array_t *pos     = f32_array_create(nt * 3);
	f32          max_abs = 1.0;
	for (i32 t = 0; t < nt; ++t) {
		f32 x = buffer_get_f32(base_pixels, t * 16);
		f32 y = buffer_get_f32(base_pixels, t * 16 + 4);
		f32 z = buffer_get_f32(base_pixels, t * 16 + 8);
		for (i32 i = 1; i < count; ++i) {
			x += buffer_get_f32(layer_pixels[i], t * 16) - buffer_get_f32(rest_pixels, t * 16);
			y += buffer_get_f32(layer_pixels[i], t * 16 + 4) - buffer_get_f32(rest_pixels, t * 16 + 4);
			z += buffer_get_f32(layer_pixels[i], t * 16 + 8) - buffer_get_f32(rest_pixels, t * 16 + 8);
		}
		pos->buffer[t * 3]     = x;
		pos->buffer[t * 3 + 1] = y;
		pos->buffer[t * 3 + 2] = z;
		if (math_abs(x) > max_abs)
			max_abs = math_abs(x);
		if (math_abs(y) > max_abs)
			max_abs = math_abs(y);
		if (math_abs(z) > max_abs)
			max_abs = math_abs(z);
	}
	if (count > 1) {
		free(layer_pixels);
	}

	// The merged mesh lists the vertices of all paint objects in order
	f32 inv = 1.0 / max_abs;
	for (i32 v = 0; v < nv; ++v) {
		i32 t                  = sculpt_vertex_texel(v);
		va0->buffer[v * 4]     = math_floor(pos->buffer[t * 3] * inv * 32767.0);
		va0->buffer[v * 4 + 1] = math_floor(pos->buffer[t * 3 + 1] * inv * 32767.0);
		va0->buffer[v * 4 + 2] = math_floor(pos->buffer[t * 3 + 2] * inv * 32767.0);
	}

	// Area weighted normals accumulated per welded vertex, so seams shade smoothly
	f32_array_t *nor = f32_array_create(nt * 3);
	for (i32 i = 0; i < nor->length; ++i) {
		nor->buffer[i] = 0.0;
	}
	for (i32 f = 0; f < math_floor(ia->length / 3.0); ++f) {
		i32    t1    = sculpt_vertex_texel(ia->buffer[f * 3]);
		i32    t2    = sculpt_vertex_texel(ia->buffer[f * 3 + 1]);
		i32    t3    = sculpt_vertex_texel(ia->buffer[f * 3 + 2]);
		vec4_t va    = (vec4_t){pos->buffer[t1 * 3], pos->buffer[t1 * 3 + 1], pos->buffer[t1 * 3 + 2], 1.0};
		vec4_t vb    = (vec4_t){pos->buffer[t2 * 3], pos->buffer[t2 * 3 + 1], pos->buffer[t2 * 3 + 2], 1.0};
		vec4_t vc    = (vec4_t){pos->buffer[t3 * 3], pos->buffer[t3 * 3 + 1], pos->buffer[t3 * 3 + 2], 1.0};
		vec4_t cb    = vec4_cross(vec4_sub(vc, vb), vec4_sub(va, vb));
		i32    ts[3] = {t1, t2, t3};
		for (i32 k = 0; k < 3; ++k) {
			nor->buffer[ts[k] * 3] += cb.x;
			nor->buffer[ts[k] * 3 + 1] += cb.y;
			nor->buffer[ts[k] * 3 + 2] += cb.z;
		}
	}
	for (i32 v = 0; v < nv; ++v) {
		i32    t               = sculpt_vertex_texel(v);
		vec4_t n               = vec4_norm((vec4_t){nor->buffer[t * 3], nor->buffer[t * 3 + 1], nor->buffer[t * 3 + 2], 0.0});
		va1->buffer[v * 2]     = math_floor(n.x * 32767);
		va1->buffer[v * 2 + 1] = math_floor(n.y * 32767);
		va0->buffer[v * 4 + 3] = math_floor(n.z * 32767);
	}
	array_delete(nor);

	g->scale_pos = max_abs;
	mesh_data_build_vertices(g->_->vertex_buffer, g->vertex_arrays);
	render_path_raytrace_ready = false;

	array_delete(pos);
	array_delete(sculpt_layers);
}

// Per texel mask of a sculpt layer, sampled at the uv of the first vertex welded into each texel
bool sculpt_layer_mask_texels(slot_layer_t *l, f32 *out, i32 len) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	if (masks == NULL) {
		return false;
	}
#ifdef IRON_BGRA
	i32 r_off = 2;
#else
	i32 r_off = 0;
#endif
	bool any = false;
	for (i32 mi = 0; mi < masks->length; ++mi) {
		slot_layer_t *m = masks->buffer[mi];
		if (!slot_layer_is_visible(m) || m->texpaint == NULL) {
			continue;
		}
		if (!any) {
			for (i32 t = 0; t < len; ++t) {
				out[t] = 1.0;
			}
			any = true;
		}
		buffer_t *mp   = gpu_get_texture_pixels(m->texpaint);
		i32       mw   = m->texpaint->width;
		i32       mh   = m->texpaint->height;
		f32       opac = slot_layer_get_opacity(m);
		for (i32 t = 0; t < len; ++t) {
			i32          v    = sculpt_texel_vertex(t);
			i16_array_t *texa = g_project->_->paint_objects->buffer[sculpt_vertex_object(&v)]->data->vertex_arrays->buffer[2]->values;
			i32          x    = math_floor(texa->buffer[v * 2] / 32767.0 * mw);
			i32          y    = math_floor(texa->buffer[v * 2 + 1] / 32767.0 * mh);
			x                 = x < 0 ? 0 : (x >= mw ? mw - 1 : x);
			y                 = y < 0 ? 0 : (y >= mh ? mh - 1 : y);
			f32 r             = buffer_get_u8(mp, (y * mw + x) * 4 + r_off) / 255.0;
			out[t] *= (1.0 - opac) + r * opac;
		}
	}
	if (any) {
		for (i32 t = 0; t < len; ++t) {
			out[t] = out[t] < 0.0 ? 0.0 : (out[t] > 1.0 ? 1.0 : out[t]);
		}
	}
	return any;
}

static void sculpt_upload_pixels(gpu_texture_t *target, buffer_t *b) {
	gpu_texture_t *img = gpu_create_texture_from_bytes(b, target->width, target->height, GPU_TEXTURE_FORMAT_RGBA128);
	draw_begin(target, false, 0);
	draw_set_pipeline(pipes_copy128);
	draw_scaled_image(img, 0, 0, target->width, target->height);
	draw_set_pipeline(NULL);
	draw_end();
	gpu_delete_texture(img);
}

// Shift an absolute sculpt state by the applied delta, then bring it to the new mesh scale
static void sculpt_shift_texture(gpu_texture_t *target, f32 *delta, i32 len, f32 scale) {
	if (target == NULL) {
		return;
	}
	buffer_t *b = gpu_get_texture_pixels(target);
	for (i32 t = 0; t < len; ++t) {
		for (i32 c = 0; c < 3; ++c) {
			buffer_set_f32(b, t * 16 + c * 4, (buffer_get_f32(b, t * 16 + c * 4) + delta[t * 3 + c]) * scale);
		}
	}
	sculpt_upload_pixels(target, b);
}

void slot_layer_apply_sculpt(slot_layer_t *raw) {
	if (raw->texpaint_sculpt == NULL) {
		return;
	}

	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	i32                    nt      = sculpt_texel_count;
	render_target_t       *rest_rt = any_map_get(render_path_render_targets, "texpaint_sculpt_base");
	buffer_t              *pixels  = gpu_get_texture_pixels(raw->texpaint_sculpt);
	buffer_t              *rest    = gpu_get_texture_pixels(rest_rt->_image);

	// Every sculpt layer stores rest pose + its own delta, bake the delta the viewport shows (masks included) into the rest pose
	f32 *mask    = malloc(sizeof(f32) * (nt > 0 ? nt : 1));
	bool masked  = sculpt_layer_mask_texels(raw, mask, nt);
	f32 *delta   = malloc(sizeof(f32) * 3 * (nt > 0 ? nt : 1));
	f32  max_abs = 1.0;
	for (i32 t = 0; t < nt; ++t) {
		for (i32 c = 0; c < 3; ++c) {
			f32 r            = buffer_get_f32(rest, t * 16 + c * 4);
			f32 d            = (buffer_get_f32(pixels, t * 16 + c * 4) - r) * (masked ? mask[t] : 1.0);
			delta[t * 3 + c] = d;
			if (math_abs(r + d) > max_abs) {
				max_abs = math_abs(r + d);
			}
		}
	}
	free(mask);

	// The remaining layers and the undo snapshots keep their own strokes on top of the new rest pose
	f32 scale = 1.0 / max_abs;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->texpaint_sculpt != NULL && l != raw) {
			sculpt_shift_texture(l->texpaint_sculpt, delta, nt, scale);
		}
	}
	for (i32 i = 0; history_undo_layers != NULL && i < history_undo_layers->length; ++i) {
		sculpt_shift_texture(history_undo_layers->buffer[i]->texpaint_sculpt, delta, nt, scale);
	}
	sculpt_shift_texture(rest_rt->_image, delta, nt, scale);
	free(delta);

	i32 offset = 0;
	for (i32 o = 0; o < objects->length; ++o) {
		mesh_object_t *ob  = objects->buffer[o];
		mesh_data_t   *g   = ob->data;
		i16_array_t   *va0 = g->vertex_arrays->buffer[0]->values;
		i32            nv  = math_floor(va0->length / 4.0);

		if (max_abs > 1.0) {
			ob->base->transform->scale_world = g->scale_pos = g->scale_pos * max_abs;
			transform_build_matrix(ob->base->transform);
		}

		// The rest buffer now holds the shifted and rescaled positions
		for (i32 i = 0; i < nv; ++i) {
			i32 t                  = sculpt_vertex_texel(i + offset);
			va0->buffer[i * 4]     = math_floor(buffer_get_f32(rest, t * 16) * 32767.0);
			va0->buffer[i * 4 + 1] = math_floor(buffer_get_f32(rest, t * 16 + 4) * 32767.0);
			va0->buffer[i * 4 + 2] = math_floor(buffer_get_f32(rest, t * 16 + 8) * 32767.0);
		}

		mesh_data_build_vertices(g->_->vertex_buffer, g->vertex_arrays);
		offset += nv;
	}

	util_mesh_calc_normals(NULL, true);
	render_path_raytrace_ready = false;

	slot_layer_delete(raw);
}
