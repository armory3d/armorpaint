
#include "../global.h"

i32 sculpt_object_vertex_offset(mesh_object_t *o) {
	i32 offset = 0;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		if (p == o) {
			break;
		}
		offset += p->data->index_array->length;
	}
	return offset;
}

static void sculpt_ensure_texture_res() {
	i32 required = 0;
	for (i32 o = 0; o < g_project->_->paint_objects->length; ++o) {
		required += g_project->_->paint_objects->buffer[o]->data->index_array->length;
	}
	bool changed = false;
	while (config_get_texture_res_x() * config_get_texture_res_y() < required && config_get_texture_res_x() < 8192) {
		i32 next = config_get_texture_res_x() < 2048 ? 2048 : config_get_texture_res_x() < 4096 ? 4096 : 8192;
		i32 pos  = config_get_texture_res_pos(next);
		config_set_texture_res(pos);
		base_res_handle->i = pos;
		changed            = true;
	}
	if (changed) {
		layers_resize();
	}
}

void sculpt_import_mesh_pack_to_texture(gpu_texture_t *target) {
	// Pack positions and normals into texture
	u32       capacity      = config_get_texture_res_x() * config_get_texture_res_y();
	buffer_t *b             = buffer_create(capacity * 4 * 4);
	i32       vertex_offset = 0;
	for (i32 o = 0; o < g_project->_->paint_objects->length; ++o) {
		mesh_data_t *mesh = g_project->_->paint_objects->buffer[o]->data;
		for (i32 i = 0; i < math_floor(mesh->index_array->length); ++i) {
			i32 index = mesh->index_array->buffer[i];
			i32 t     = i + vertex_offset;
			if (t >= capacity) {
				break;
			}
			buffer_set_f32(b, 4 * t * 4, mesh->vertex_arrays->buffer[0]->values->buffer[index * 4] / 32767.0);
			buffer_set_f32(b, 4 * t * 4 + 1 * 4, mesh->vertex_arrays->buffer[0]->values->buffer[index * 4 + 1] / 32767.0);
			buffer_set_f32(b, 4 * t * 4 + 2 * 4, mesh->vertex_arrays->buffer[0]->values->buffer[index * 4 + 2] / 32767.0);
			f32 nor_x = mesh->vertex_arrays->buffer[1]->values->buffer[index * 2] / 32767.0f;
			f32 nor_y = mesh->vertex_arrays->buffer[1]->values->buffer[index * 2 + 1] / 32767.0f;
			f32 nor_z = mesh->vertex_arrays->buffer[0]->values->buffer[index * 4 + 3] / 32767.0f;
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
		vertex_offset += mesh->index_array->length;
	}

	gpu_texture_t *imgmesh = gpu_create_texture_from_bytes(b, config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA128);
	draw_begin(target, false, 0);
	draw_set_pipeline(pipes_copy128);
	draw_scaled_image(imgmesh, 0, 0, config_get_texture_res_x(), config_get_texture_res_y());
	draw_set_pipeline(NULL);
	draw_end();
	gpu_texture_destroy(imgmesh);
}

static char *sculpt_blend_mode(node_shader_t *kong, i32 blending, char *cola, char *colb, char *opac) {
	// mix/add/screen/lighten/difference (raise), subtract/darken (carve) and linear light (bidirectional)
	if (blending == BLEND_TYPE_DARKEN) {
		return string("lerp(%s, min(%s, %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_MULTIPLY) {
		return string("lerp(%s, %s * %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_BURN) {
		return string("lerp(%s, 1.0 - (1.0 - %s) / %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_LIGHTEN) {
		return string("max(%s, %s * %s)", cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SCREEN) {
		return string("(1.0 - (1.0 - %s * %s) * (1.0 - %s))", colb, opac, cola);
	}
	else if (blending == BLEND_TYPE_DODGE) {
		return string("lerp(%s, %s / (1.0 - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_ADD) {
		return string("lerp(%s, %s + %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_OVERLAY) {
		char *res = "sculpt_overlay_res";
		node_shader_write_frag(kong, string("var %s: float;", res));
		node_shader_write_frag(kong, string("if (%s < 0.5) { %s = 2.0 * %s * %s; } else { %s = 1.0 - 2.0 * (1.0 - %s) * (1.0 - %s); }", cola, res, cola, colb,
		                                    res, cola, colb));
		return string("lerp(%s, %s, %s)", cola, res, opac);
	}
	else if (blending == BLEND_TYPE_SOFT_LIGHT) {
		return string("((1.0 - %s) * %s + %s * ((1.0 - %s) * %s * %s + %s * (1.0 - (1.0 - %s) * (1.0 - %s))))", opac, cola, opac, cola, colb, cola, cola, colb,
		              cola);
	}
	else if (blending == BLEND_TYPE_LINEAR_LIGHT) {
		return string("(%s + %s * (2.0 * (%s - 0.5)))", cola, opac, colb);
	}
	else if (blending == BLEND_TYPE_DIFFERENCE) {
		return string("lerp(%s, abs(%s - %s), %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_SUBTRACT) {
		return string("lerp(%s, %s - %s, %s)", cola, cola, colb, opac);
	}
	else if (blending == BLEND_TYPE_DIVIDE) {
		return string("((1.0 - %s) * %s + %s * %s / %s)", opac, cola, opac, cola, colb);
	}
	else { // BLEND_TYPE_MIX, hue / saturation / color / value
		return string("lerp(%s, %s, %s)", cola, colb, opac);
	}
}

node_shader_context_t *sculpt_make_sculpt_run(material_t *data, material_context_t *matcon) {
	char                  *context_id = "paint";
	shader_context_t      *props      = GC_ALLOC_INIT(shader_context_t, {.name            = context_id,
	                                                                     .depth_write     = false,
	                                                                     .compare_mode    = "always",
	                                                                     .cull_mode       = "none",
	                                                                     .vertex_elements = any_array_create_from_raw(
                                                                   (void *[]){
                                                                       GC_ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "float2"}),
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
	// Grab drags the vertices along with the cursor instead of displacing along the normal
	bool grab = g_context->tool == TOOL_TYPE_BRUSH && g_context->brush_sculpt == SCULPT_TYPE_GRAB;
	// Decal fill layer: displacement must be confined to the decal box projection
	bool decal_layer = g_context->layer->fill_material != NULL && g_context->layer->uv_type == UV_TYPE_PROJECT && g_context->tool == TOOL_TYPE_FILL;

	bool has_wposition = g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE ||
	                     g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE || g_context->tool == TOOL_TYPE_FILL || decal;
	bool sculpt_triplanar = has_wposition && !decal && !decal_layer && g_context->tool != TOOL_TYPE_BLUR;
	node_shader_add_out(kong, "tex_coord: float2");
	node_shader_write_vert(kong, "var madd: float2 = float2(0.5, 0.5);");
	node_shader_write_vert(kong, "output.tex_coord = input.pos.xy * madd + madd;");
	node_shader_write_vert(kong, "output.tex_coord.y = 1.0 - output.tex_coord.y;");
	node_shader_write_vert(kong, "output.pos = float4(input.pos.xy, 0.0, 1.0);");
	node_shader_write_attrib_frag(kong, "var tex_coord: float2 = input.tex_coord;");
	node_shader_write_attrib_frag(kong, "var sculpt_uv: float2 = tex_coord;");

	// Restrict displacement to the object selected in the layer's object combo
	if (g_project->_->paint_objects->length > 1) {
		node_shader_add_texture(kong, "texpaint_sculpt_undo", "_texpaint_sculpt_undo");
		node_shader_add_constant(kong, "texpaint_undo_size: float2", "_size(_texpaint_sculpt_undo)");
		node_shader_add_constant(kong, "sculpt_mask_offset: float", "_sculpt_mask_offset");
		node_shader_add_constant(kong, "sculpt_mask_count: float", "_sculpt_mask_count");
		node_shader_write_frag(kong, "var sculpt_mask_lin: float = floor(sculpt_uv.y * constants.texpaint_undo_size.y) * "
		                             "constants.texpaint_undo_size.x + floor(sculpt_uv.x * constants.texpaint_undo_size.x);");
		node_shader_write_frag(kong, "if (sculpt_mask_lin < constants.sculpt_mask_offset || sculpt_mask_lin >= "
		                             "constants.sculpt_mask_offset + constants.sculpt_mask_count) { discard; }");
	}

	node_shader_add_constant(kong, "inp: float4", "_input_brush");
	node_shader_add_constant(kong, "inplast: float4", "_input_brush_last");
	node_shader_add_texture(kong, "gbufferD", NULL);
	kong->frag_out = "float4[2]";
	node_shader_add_constant(kong, "brush_radius: float", "_brush_radius");
	node_shader_add_constant(kong, "brush_opacity: float", "_brush_opacity");
	node_shader_add_constant(kong, "brush_hardness: float", "_brush_hardness");
	node_shader_write_frag(kong, "var dist: float = 0.0;");

	if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE || g_context->tool == TOOL_TYPE_BLUR ||
	    g_context->tool == TOOL_TYPE_PARTICLE || decal) {
		node_shader_add_constant(kong, "invVP: float4x4", "_inv_view_proj_matrix");
		if (grab) {
			// Anchor a camera-facing plane at the grab_start surface point
			node_shader_add_constant(kong, "grab_start: float2", "_grab_start");
			node_shader_write_frag(kong, "var grab_ndc: float2 = float2(constants.grab_start.x, 1.0 - constants.grab_start.y) * 2.0 - 1.0;");
			node_shader_write_frag(kong, "var grab_adepth: float = sample_lod(gbufferD, sampler_linear, constants.grab_start, 0.0).r;");
			node_shader_write_frag(kong, "var grab_anchor4: float4 = constants.invVP * float4(grab_ndc, grab_adepth, 1.0);");
			node_shader_write_frag(kong, "var grab_anchor: float3 = grab_anchor4.xyz / grab_anchor4.w;");
			node_shader_write_frag(kong, "var grab_near4: float4 = constants.invVP * float4(grab_ndc, 0.0, 1.0);");
			node_shader_write_frag(kong, "var grab_far4: float4 = constants.invVP * float4(grab_ndc, 1.0, 1.0);");
			node_shader_write_frag(kong, "var grab_plane_n: float3 = normalize(grab_far4.xyz / grab_far4.w - grab_near4.xyz / grab_near4.w);");
			// Intersect the cursor ray with the grab plane
			node_shader_write_frag(kong, "var winp_ndc: float2 = float2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0;");
			node_shader_write_frag(kong, "var winp_o4: float4 = constants.invVP * float4(winp_ndc, 0.0, 1.0);");
			node_shader_write_frag(kong, "var winp_f4: float4 = constants.invVP * float4(winp_ndc, 1.0, 1.0);");
			node_shader_write_frag(kong, "var winp_o: float3 = winp_o4.xyz / winp_o4.w;");
			node_shader_write_frag(kong, "var winp_d: float3 = normalize(winp_f4.xyz / winp_f4.w - winp_o);");
			node_shader_write_frag(kong,
			                       "var winp: float4 = float4(winp_o + winp_d * (dot(grab_anchor - winp_o, grab_plane_n) / dot(winp_d, grab_plane_n)), 1.0);");
		}
		else {
			node_shader_write_frag(kong, "var depth: float = sample_lod(gbufferD, sampler_linear, constants.inp.xy, 0.0).r;");
			node_shader_write_frag(kong, "var winp: float4 = float4(float2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0, depth, 1.0);");
			node_shader_write_frag(kong, "winp = constants.invVP * winp;");
			node_shader_write_frag(kong, "winp.xyz = winp.xyz / winp.w;");
		}
		node_shader_add_constant(kong, "W: float4x4", "_world_matrix");
		node_shader_write_attrib_frag(kong, "var read_undo: float4 = texpaint_sculpt_undo[uint2(uint(tex_coord.x * constants.texpaint_undo_size.x), "
		                                    "uint(tex_coord.y * constants.texpaint_undo_size.y))];");
		node_shader_write_attrib_frag(kong, "var wposition: float3 = (constants.W * float4(read_undo.xyz, 1.0)).xyz;");
		if (grab) {
			node_shader_write_frag(kong, "var winpl_ndc: float2 = float2(constants.inplast.x, 1.0 - constants.inplast.y) * 2.0 - 1.0;");
			node_shader_write_frag(kong, "var winpl_o4: float4 = constants.invVP * float4(winpl_ndc, 0.0, 1.0);");
			node_shader_write_frag(kong, "var winpl_f4: float4 = constants.invVP * float4(winpl_ndc, 1.0, 1.0);");
			node_shader_write_frag(kong, "var winpl_o: float3 = winpl_o4.xyz / winpl_o4.w;");
			node_shader_write_frag(kong, "var winpl_d: float3 = normalize(winpl_f4.xyz / winpl_f4.w - winpl_o);");
			node_shader_write_frag(
			    kong, "var winplast: float4 = float4(winpl_o + winpl_d * (dot(grab_anchor - winpl_o, grab_plane_n) / dot(winpl_d, grab_plane_n)), 1.0);");
		}
		else {
			node_shader_write_frag(kong, "var depthlast: float = sample_lod(gbufferD, sampler_linear, constants.inplast.xy, 0.0).r;");
			node_shader_write_frag(kong, "var winplast: float4 = float4(float2(constants.inplast.x, 1.0 - constants.inplast.y) * 2.0 - 1.0, depthlast, 1.0);");
			node_shader_write_frag(kong, "winplast = constants.invVP * winplast;");
			node_shader_write_frag(kong, "winplast.xyz = winplast.xyz / winplast.w;");
		}

		if (particle) {
			node_shader_add_constant(kong, "particle_hit: float3", "_particle_hit");
			node_shader_add_constant(kong, "particle_hit_last: float3", "_particle_hit_last");
			node_shader_add_constant(kong, "particle_radius: float", "_particle_radius");
			node_shader_write_frag(kong, "var ppa: float3 = wposition.xyz - constants.particle_hit;");
			node_shader_write_frag(kong, "var pba: float3 = constants.particle_hit_last - constants.particle_hit;");
			node_shader_write_frag(kong, "var pph: float = clamp(dot(ppa, pba) / max(dot(pba, pba), 0.00000001), 0.0, 1.0);");
			node_shader_write_frag(kong, "dist = length(ppa - pba * pph) * (5.0 / constants.particle_radius);");
			node_shader_write_frag(kong, "if (dist > 1.0) { discard; }");
		}
		else if (!decal) {
			if (g_context->xray) {
				node_shader_write_frag(kong, "var xray_ndc: float2 = float2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0;");
				node_shader_write_frag(kong, "var xray_near: float4 = constants.invVP * float4(xray_ndc, 0.0, 1.0);");
				node_shader_write_frag(kong, "var xray_far: float4 = constants.invVP * float4(xray_ndc, 1.0, 1.0);");
				node_shader_write_frag(kong, "var xray_axis: float3 = normalize(xray_far.xyz / xray_far.w - xray_near.xyz / xray_near.w);");
			}
			if (g_context->brush_lazy_radius > 0 && g_context->brush_lazy_step > 0) { // Sphere
				if (g_context->xray) {
					node_shader_write_frag(kong, "var pa: float3 = wposition.xyz - winp.xyz;");
					node_shader_write_frag(kong, "dist = length(pa - xray_axis * dot(xray_axis, pa));");
				}
				else {
					node_shader_write_frag(kong, "dist = distance(wposition.xyz, winp.xyz);");
				}
			}
			else { // Capsule
				node_shader_write_frag(kong, "var pa: float3 = wposition.xyz - winp.xyz;");
				node_shader_write_frag(kong, "var ba: float3 = winplast.xyz - winp.xyz;");
				if (g_context->xray) {
					node_shader_write_frag(kong, "pa = pa - xray_axis * dot(xray_axis, pa);");
					node_shader_write_frag(kong, "ba = ba - xray_axis * dot(xray_axis, ba);");
				}
				node_shader_write_frag(kong, "var h: float = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);");
				node_shader_write_frag(kong, "dist = length(pa - ba * h);");
			}
			node_shader_write_frag(kong, "if (dist > constants.brush_radius) { discard; }");
		}
		else { // decal
			node_shader_add_constant(kong, "decal_mask: float4", "_decal_mask");
			node_shader_write_frag(kong, "if (constants.decal_mask.z > 0.0) {");
			if (g_context->brush_lazy_radius > 0 && g_context->brush_lazy_step > 0) {
				node_shader_write_frag(kong, "dist = distance(wposition.xyz, winp.xyz);");
			}
			else {
				node_shader_write_frag(kong, "var pa: float3 = wposition.xyz - winp.xyz;");
				node_shader_write_frag(kong, "var ba: float3 = winplast.xyz - winp.xyz;");
				node_shader_write_frag(kong, "var h: float = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);");
				node_shader_write_frag(kong, "dist = length(pa - ba * h);");
			}
			node_shader_write_frag(kong, "if (dist > constants.brush_radius) { discard; }");
			node_shader_write_frag(kong, "}");
		}
	}
	else if (g_context->tool == TOOL_TYPE_FILL) {
		node_shader_add_constant(kong, "W: float4x4", "_world_matrix");
		node_shader_write_attrib_frag(kong, "var read_undo: float4 = texpaint_sculpt_undo[uint2(uint(tex_coord.x * constants.texpaint_undo_size.x), "
		                                    "uint(tex_coord.y * constants.texpaint_undo_size.y))];");
		node_shader_write_attrib_frag(kong, "var wposition: float3 = (constants.W * float4(read_undo.xyz, 1.0)).xyz;");
	}

	if (decal_layer) {
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_constant(kong, "decal_layer_matrix: float4x4", "_decal_layer_matrix");
		node_shader_write_frag(kong, "var decal_proj: float4 = constants.decal_layer_matrix * float4(read_undo.xyz, 1.0);");
		node_shader_write_frag(kong, "var uvsp: float2 = decal_proj.xy / decal_proj.w;");
		node_shader_write_frag(kong, "uvsp.x = uvsp.x * 0.5 + 0.5;");
		node_shader_write_frag(kong, "uvsp.y = 1.0 - (uvsp.y * 0.5 + 0.5);");
		node_shader_write_frag(kong, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");

		f32 uv_angle = g_context->layer->angle;
		if (uv_angle != 0.0) {
			node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
			node_shader_write_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
			                             "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
		}

		// Reject surfaces not facing the decal direction
		node_shader_write_frag(kong, "var dproj_undo: float4 = texpaint_sculpt_undo[uint2(uint(sculpt_uv.x * constants.texpaint_undo_size.x), uint(sculpt_uv.y "
		                             "* constants.texpaint_undo_size.y))];");
		node_shader_write_frag(kong, "var dproj_nv: float = floor(dproj_undo.a) / 255.0;");
		node_shader_write_frag(kong, "var dproj_oct: float2 = float2(dproj_undo.a - floor(dproj_undo.a), dproj_nv) * 2.0 - 1.0;");
		node_shader_write_frag(kong, "var dproj_nz: float = 1.0 - abs(dproj_oct.x) - abs(dproj_oct.y);");
		node_shader_write_frag(kong, "var dproj_nor: float3 = float3(dproj_oct.xy, dproj_nz);");
		node_shader_write_frag(kong, "if (dproj_nz < 0.0) { dproj_nor.xy = octahedron_wrap(dproj_oct.xy); }");
		node_shader_write_frag(kong, "dproj_nor = normalize((constants.W * float4(normalize(dproj_nor), 0.0)).xyz);");
		node_shader_add_constant(kong, "decal_layer_nor: float3", "_decal_layer_nor");
		f32 dot_angle = g_context->brush_angle_reject_dot;
		node_shader_write_frag(kong, string("if (abs(dot(dproj_nor, constants.decal_layer_nor) - 1.0) > %s) { discard; }", f32_to_string(dot_angle)));

		// Reject surfaces outside the decal box
		node_shader_add_constant(kong, "decal_layer_loc: float3", "_decal_layer_loc");
		node_shader_add_constant(kong, "decal_layer_dim: float", "_decal_layer_dim");
		node_shader_write_frag(kong,
		                       "if (abs(dot(constants.decal_layer_nor, constants.decal_layer_loc - wposition.xyz)) > constants.decal_layer_dim) { discard; }");

		node_shader_add_constant(kong, "brush_scale: float", "_brush_scale");
		node_shader_write_frag(kong, "tex_coord = uvsp * constants.brush_scale;");
	}

	if (decal) {
		// Tangent-space decal projection
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_texture(kong, "gbuffer0_undo", NULL);
		node_shader_add_constant(kong, "decal_mask: float4", "_decal_mask");
		node_shader_add_constant(kong, "VP: float4x4", "_view_proj_matrix");
		node_shader_add_constant(kong, "aspect_ratio: float", "_aspect_ratio_window");
		node_shader_add_constant(kong, "camera_right: float3", "_camera_right");
		node_shader_add_constant(kong, "camera_up: float3", "_camera_up");
		node_shader_add_constant(kong, "camera_align: float", "_brush_camera_align");

		node_shader_write_attrib_frag(kong, "var uvsp: float2 = float2(0.0, 0.0);");

		node_shader_write_attrib_frag(kong, "if (constants.camera_align > 0.0) {");
		// Project the surface point to screen-space for the planar, camera-aligned decal
		node_shader_write_attrib_frag(kong, "var sp4: float4 = constants.VP * float4(wposition.xyz, 1.0);");
		node_shader_write_attrib_frag(kong, "var sp: float2 = sp4.xy / sp4.w;");
		node_shader_write_attrib_frag(kong, "sp.x = sp.x * 0.5 + 0.5;");
		node_shader_write_attrib_frag(kong, "sp.y = 1.0 - (sp.y * 0.5 + 0.5);");
		node_shader_write_attrib_frag(kong, "var ca_depth: float = sample_lod(gbufferD, sampler_linear, constants.decal_mask.xy, 0.0).r;");
		node_shader_write_attrib_frag(kong, "var ca_coord: float2 = float2(constants.decal_mask.x * 2.0 - 1.0, 1.0 - constants.decal_mask.y * 2.0);");
		node_shader_write_attrib_frag(kong, "var ca_homog: float4 = constants.invVP * float4(ca_coord.x, ca_coord.y, ca_depth, 1.0);");
		node_shader_write_attrib_frag(kong, "var ca_clip_w: float = 1.0 / ca_homog.w;");
		node_shader_write_attrib_frag(kong, "var vp_up_y: float = (constants.VP * float4(constants.camera_up, 0.0)).y;");
		node_shader_write_attrib_frag(kong, "uvsp = sp.xy - constants.decal_mask.xy;");
		node_shader_write_attrib_frag(kong, "uvsp.x *= constants.aspect_ratio;");
		node_shader_write_attrib_frag(kong, "uvsp = uvsp * (ca_clip_w / (vp_up_y * constants.brush_radius));");
		node_shader_write_attrib_frag(kong, "}");

		node_shader_write_attrib_frag(kong, "else {");
		// When mask is active, anchor the decal at the frozen position
		node_shader_write_attrib_frag(kong, "var decal_xy: float2 = constants.inp.xy;");
		node_shader_write_attrib_frag(kong, "if (constants.decal_mask.z > 0.0) { decal_xy = constants.decal_mask.xy; }");

		// Unproject the decal anchor point from the depth buffer
		node_shader_write_attrib_frag(kong, "var decal_depth: float = sample_lod(gbufferD, sampler_linear, decal_xy, 0.0).r;");
		node_shader_write_attrib_frag(kong, "var decal_wpos4: float4 = float4(float2(decal_xy.x, 1.0 - decal_xy.y) * 2.0 - 1.0, decal_depth, 1.0);");
		node_shader_write_attrib_frag(kong, "decal_wpos4 = constants.invVP * decal_wpos4;");
		node_shader_write_attrib_frag(kong, "var decal_wpos: float3 = decal_wpos4.xyz / decal_wpos4.w;");

		// Decode face normal at anchor point
		node_shader_write_attrib_frag(kong, "var dg0: float2 = sample_lod(gbuffer0_undo, sampler_linear, decal_xy, 0.0).rg;");
		node_shader_write_attrib_frag(kong, "var dn: float3;");
		node_shader_write_attrib_frag(kong, "dn.z = 1.0 - abs(dg0.x) - abs(dg0.y);");
		node_shader_write_attrib_frag(
		    kong, "if (dn.z >= 0.0) { dn.x = dg0.x; dn.y = dg0.y; } else { var fw: float2 = octahedron_wrap(dg0.xy); dn.x = fw.x; dn.y = fw.y; }");
		node_shader_write_attrib_frag(kong, "dn = normalize(dn);");

		// Build tangent basis
		node_shader_write_attrib_frag(kong, "var d_right: float3 = constants.camera_right;");
		node_shader_write_attrib_frag(kong, "if (abs(dot(dn, d_right)) > 0.999) { d_right = float3(0.0, 0.0, 1.0); }");
		node_shader_write_attrib_frag(kong, "var d_tan: float3 = normalize(d_right - dn * dot(d_right, dn));");
		node_shader_write_attrib_frag(kong, "var d_bin: float3 = cross(d_tan, dn);");

		node_shader_write_attrib_frag(kong, "var d_offset: float3 = wposition.xyz - decal_wpos;");
		node_shader_write_attrib_frag(kong, "var decal_radius: float = constants.brush_radius;");
		node_shader_write_attrib_frag(kong, "if (constants.decal_mask.z > 0.0) { decal_radius = constants.decal_mask.w; }");
		node_shader_write_attrib_frag(kong, "if (abs(dot(d_offset, dn)) > decal_radius) { discard; }");
		node_shader_write_attrib_frag(kong, "uvsp = float2(dot(d_offset, d_tan), dot(d_offset, d_bin));");
		node_shader_write_attrib_frag(kong, "uvsp = uvsp / decal_radius * 0.5;");

		node_shader_write_attrib_frag(kong, "}");

		if (g_context->brush_directional) {
			node_shader_add_constant(kong, "brush_direction: float3", "_brush_direction");
			node_shader_write_attrib_frag(kong, "if (constants.brush_direction.z == 0.0) { discard; }");
			node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_direction.x - uvsp.y * constants.brush_direction.y, uvsp.x * "
			                                    "constants.brush_direction.y + uvsp.y * constants.brush_direction.x);");
		}

		f32 angle = g_context->brush_angle + g_context->brush_nodes_angle;
		if (angle != 0.0) {
			node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
			node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
			                                    "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
		}

		node_shader_add_constant(kong, "brush_scale_x: float", "_brush_scale_x");
		node_shader_write_attrib_frag(kong, "uvsp.x *= constants.brush_scale_x;");
		node_shader_write_attrib_frag(kong, "uvsp += float2(0.5, 0.5);");
		node_shader_write_attrib_frag(kong, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");
		node_shader_add_constant(kong, "brush_scale: float", "_brush_scale");
		node_shader_write_attrib_frag(kong, "tex_coord = uvsp * constants.brush_scale;");
	}

	if (sculpt_triplanar) {
		// Project the world position onto the three axis planes
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_constant(kong, "brush_scale: float", "_brush_scale");
		// Decode this vertexs world-space surface normal for blending
		node_shader_write_frag(kong, "var tri_nv: float = floor(read_undo.a) / 255.0;");
		node_shader_write_frag(kong, "var tri_oct: float2 = float2(read_undo.a - floor(read_undo.a), tri_nv) * 2.0 - 1.0;");
		node_shader_write_frag(kong, "var tri_nz: float = 1.0 - abs(tri_oct.x) - abs(tri_oct.y);");
		node_shader_write_frag(kong, "var tri_nor: float3 = float3(tri_oct.xy, tri_nz);");
		node_shader_write_frag(kong, "if (tri_nz < 0.0) { tri_nor.xy = octahedron_wrap(tri_oct.xy); }");
		node_shader_write_frag(kong, "tri_nor = normalize((constants.W * float4(normalize(tri_nor), 0.0)).xyz);");
		node_shader_write_frag(kong, "var tri_weight: float3 = tri_nor * tri_nor;");
		node_shader_write_frag(kong, "var tri_max: float = max(tri_weight.x, max(tri_weight.y, tri_weight.z));");
		node_shader_write_frag(kong, "tri_weight = max3(tri_weight - float3(tri_max * 0.75, tri_max * 0.75, tri_max * 0.75), float3(0.0, 0.0, 0.0));");
		node_shader_write_frag(kong, "var tex_coord_blend: float3 = tri_weight * (1.0 / (tri_weight.x + tri_weight.y + tri_weight.z));");
		node_shader_write_frag(kong, "tex_coord = wposition.yz * constants.brush_scale * 0.5;");
		node_shader_write_frag(kong, "var tex_coord1: float2 = wposition.xz * constants.brush_scale * 0.5;");
		node_shader_write_frag(kong, "var tex_coord2: float2 = wposition.xy * constants.brush_scale * 0.5;");
		f32 sculpt_uv_angle = g_context->layer->fill_material != NULL ? g_context->layer->angle : g_context->brush_angle + g_context->brush_nodes_angle;
		if (sculpt_uv_angle != 0.0) {
			node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
			node_shader_write_frag(kong, "tex_coord = float2(tex_coord.x * constants.brush_angle.x - tex_coord.y * constants.brush_angle.y, tex_coord.x * "
			                             "constants.brush_angle.y + tex_coord.y * constants.brush_angle.x);");
			node_shader_write_frag(kong, "tex_coord1 = float2(tex_coord1.x * constants.brush_angle.x - tex_coord1.y * constants.brush_angle.y, tex_coord1.x * "
			                             "constants.brush_angle.y + tex_coord1.y * constants.brush_angle.x);");
			node_shader_write_frag(kong, "tex_coord2 = float2(tex_coord2.x * constants.brush_angle.x - tex_coord2.y * constants.brush_angle.y, tex_coord2.x * "
			                             "constants.brush_angle.y + tex_coord2.y * constants.brush_angle.x);");
		}
		parser_material_triplanar = true;
	}

	// parser_material_parse may add vertex elements
	i32 velen = con_paint->data->vertex_elements->length;
	// parser_material_parse_height             = true;
	// parser_material_parse_height_as_channel  = true;
	shader_out_t *sout                       = parser_material_parse(g_context->material->canvas, con_paint, kong, matcon);
	con_paint->data->vertex_elements->length = velen;
	parser_material_triplanar                = false;
	// node_shader_write_frag(kong, string("var height: float = %s.r;", sout->out_basecol));
	node_shader_write_frag(kong, string("var disp: float3 = %s;", sout->out_basecol));

	if (kong->frag_bposition) {
		kong->frag_bposition = false;
		node_shader_write_attrib_frag(kong, "var bposition: float3 = wposition.xyz;");
	}

	node_shader_write_frag(kong, "var basecol: float3 = float3(1.0, 1.0, 1.0);");
	node_shader_write_frag(kong, string("var opacity: float = %s;", sout->out_opacity));
	if (g_context->layer->fill_material == NULL) {
		node_shader_write_frag(kong, "opacity *= constants.brush_opacity;");
	}
	else {
		node_shader_write_frag(kong, "opacity *= 20.0;");
	}

	if (g_context->brush_mask_image != NULL && g_context->tool == TOOL_TYPE_DECAL) {
		node_shader_add_texture(kong, "texbrushmask", "_texbrushmask");
		node_shader_write_frag(kong, "var mask_sample: float4 = sample_lod(texbrushmask, sampler_linear, uvsp, 0.0);");
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
		node_shader_add_constant(kong, "camera_right: float3", "_camera_right");
		node_shader_write_frag(kong, "var mn0: float2 = sample_lod(gbuffer0_undo, sampler_linear, constants.inp.xy, 0.0).rg;");
		node_shader_write_frag(kong, "var mn: float3;");
		node_shader_write_frag(kong, "mn.z = 1.0 - abs(mn0.x) - abs(mn0.y);");
		node_shader_write_frag(
		    kong, "if (mn.z >= 0.0) { mn.x = mn0.x; mn.y = mn0.y; } else { var mfw: float2 = octahedron_wrap(mn0.xy); mn.x = mfw.x; mn.y = mfw.y; }");
		node_shader_write_frag(kong, "mn = normalize(mn);");
		node_shader_write_frag(kong, "var mr: float3 = constants.camera_right;");
		node_shader_write_frag(kong, "if (abs(dot(mn, mr)) > 0.999) { mr = float3(0.0, 0.0, 1.0); }");
		node_shader_write_frag(kong, "var mt: float3 = normalize(mr - mn * dot(mr, mn));");
		node_shader_write_frag(kong, "var mb: float3 = cross(mt, mn);");
		node_shader_write_frag(kong, "var pa_mask_3d: float3 = wposition.xyz - winp.xyz;");
		node_shader_write_frag(kong, "var pa_mask: float2 = float2(dot(pa_mask_3d, mt), dot(pa_mask_3d, mb));");
		if (g_context->brush_directional) {
			node_shader_add_constant(kong, "brush_direction: float3", "_brush_direction");
			node_shader_write_frag(kong, "if (constants.brush_direction.z == 0.0) { discard; }");
			node_shader_write_frag(kong, "pa_mask = float2(pa_mask.x * constants.brush_direction.x - pa_mask.y * constants.brush_direction.y, pa_mask.x * "
			                             "constants.brush_direction.y + pa_mask.y * constants.brush_direction.x);");
		}
		f32 angle = g_context->brush_angle + g_context->brush_nodes_angle;
		if (angle != 0.0) {
			node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
			node_shader_write_frag(kong, "pa_mask.xy = float2(pa_mask.x * constants.brush_angle.x - pa_mask.y * constants.brush_angle.y, pa_mask.x * "
			                             "constants.brush_angle.y + pa_mask.y * constants.brush_angle.x);");
		}
		node_shader_write_frag(kong, "pa_mask = pa_mask / constants.brush_radius * 0.5 + 0.5;");
		node_shader_write_frag(kong, "var mask_sample: float4 = sample_lod(texbrushmask, sampler_linear, pa_mask, 0.0);");
		if (g_context->brush_mask_image_is_alpha) {
			node_shader_write_frag(kong, "opacity *= mask_sample.a;");
		}
		else {
			node_shader_write_frag(kong, "opacity *= mask_sample.r * mask_sample.a;");
		}
	}

	if (g_context->select_active && has_wposition) {
		node_shader_add_constant(kong, "VP: float4x4", "_view_proj_matrix");
		node_shader_add_constant(kong, "select_mask: float4", "_select_mask");
		node_shader_write_frag(kong, "var select_sp4: float4 = constants.VP * float4(wposition.xyz, 1.0);");
		node_shader_write_frag(kong, "var select_sp: float2 = select_sp4.xy / select_sp4.w;");
		node_shader_write_frag(kong, "select_sp.x = select_sp.x * 0.5 + 0.5;");
		node_shader_write_frag(kong, "select_sp.y = 1.0 - (select_sp.y * 0.5 + 0.5);");
		node_shader_write_frag(kong, "if (select_sp.x < constants.select_mask.x || select_sp.x > constants.select_mask.z || select_sp.y < "
		                             "constants.select_mask.y || select_sp.y > constants.select_mask.w) { discard; }");
	}

	if (g_context->brush_stencil_image != NULL && has_wposition) {
		node_shader_add_constant(kong, "VP: float4x4", "_view_proj_matrix");
		node_shader_add_constant(kong, "aspect_ratio: float", "_aspect_ratio_window");
		node_shader_add_texture(kong, "texbrushstencil", "_texbrushstencil");
		node_shader_add_constant(kong, "texbrushstencil_size: float2", "_size(_texbrushstencil)");
		node_shader_add_constant(kong, "stencil_transform: float4", "_stencil_transform");
		node_shader_write_frag(kong, "var stencil_sp4: float4 = constants.VP * float4(wposition.xyz, 1.0);");
		node_shader_write_frag(kong, "var stencil_sp: float2 = stencil_sp4.xy / stencil_sp4.w;");
		node_shader_write_frag(kong, "stencil_sp.x = stencil_sp.x * 0.5 + 0.5;");
		node_shader_write_frag(kong, "stencil_sp.y = 1.0 - (stencil_sp.y * 0.5 + 0.5);");
		node_shader_write_frag(
		    kong,
		    "var stencil_uv: float2 = (stencil_sp.xy - constants.stencil_transform.xy) / constants.stencil_transform.z * float2(constants.aspect_ratio, 1.0);");
		node_shader_write_frag(kong, "var stencil_size: float2 = constants.texbrushstencil_size;");
		node_shader_write_frag(kong, "var stencil_ratio: float = stencil_size.y / stencil_size.x;");
		node_shader_write_frag(kong, "stencil_uv -= float2(0.5 / stencil_ratio, 0.5);");
		node_shader_write_frag(kong,
		                       "stencil_uv = float2(stencil_uv.x * cos(constants.stencil_transform.w) - stencil_uv.y * sin(constants.stencil_transform.w),\
												   stencil_uv.x * sin(constants.stencil_transform.w) + stencil_uv.y * cos(constants.stencil_transform.w));");
		node_shader_write_frag(kong, "stencil_uv += float2(0.5 / stencil_ratio, 0.5);");
		node_shader_write_frag(kong, "stencil_uv.x *= stencil_ratio;");
		node_shader_write_frag(kong, "if (stencil_uv.x < 0.0 || stencil_uv.x > 1.0 || stencil_uv.y < 0.0 || stencil_uv.y > 1.0) { discard; }");
		node_shader_write_frag(kong, "var texbrushstencil_sample: float4 = sample_lod(texbrushstencil, sampler_linear, stencil_uv, 0.0);");
		if (g_context->brush_stencil_image_is_alpha) {
			node_shader_write_frag(kong, "opacity *= texbrushstencil_sample.a;");
		}
		else {
			node_shader_write_frag(kong, "opacity *= texbrushstencil_sample.r * texbrushstencil_sample.a;");
		}
	}

	node_shader_write_frag(kong, "if (opacity == 0.0) { discard; }");
	if (particle) {
		node_shader_write_frag(kong, "var str: float = clamp(pow(dist * constants.brush_hardness * 0.2, 2.0) / 10.0, 0.0, 1.0) * opacity;");
	}
	else {
		node_shader_write_frag(kong, "var t: float = clamp(dist / constants.brush_radius, 0.0, 1.0);");
		node_shader_write_frag(kong, "var t2: float = clamp((t - constants.brush_hardness) / max(1.0 - constants.brush_hardness, 0.001), 0.0, 1.0);");
		node_shader_write_frag(kong, "var falloff: float = 1.0 - t2 * t2 * (3.0 - 2.0 * t2);");
		node_shader_write_frag(kong, "var str: float = falloff * constants.brush_radius * 0.05 * opacity;");
	}
	if (decal) {
		node_shader_write_frag(kong, "str = str * 0.2;");
	}

	node_shader_add_texture(kong, "texpaint_sculpt_undo", "_texpaint_sculpt_undo");
	node_shader_add_constant(kong, "texpaint_undo_size: float2", "_size(_texpaint_sculpt_undo)");
	node_shader_write_frag(kong, "var sample_undo: float4 = sample_lod(texpaint_sculpt_undo, sampler_linear, sculpt_uv, 0.0);");
	node_shader_write_frag(kong, "var raw_undo: float4 = texpaint_sculpt_undo[uint2(uint(sculpt_uv.x * constants.texpaint_undo_size.x), uint(sculpt_uv.y * "
	                             "constants.texpaint_undo_size.y))];");

	if (g_context->layer->fill_material != NULL || g_context->tool == TOOL_TYPE_FILL) {
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_write_frag(kong, "var nor_v: float = floor(raw_undo.a) / 255.0;");
		node_shader_write_frag(kong, "var nor_oct: float2 = float2(raw_undo.a - floor(raw_undo.a), nor_v) * 2.0 - 1.0;");
		node_shader_write_frag(kong, "var nor_z: float = 1.0 - abs(nor_oct.x) - abs(nor_oct.y);");
		node_shader_write_frag(kong, "var nor_xyz: float3 = float3(nor_oct.xy, nor_z);");
		node_shader_write_frag(kong, "if (nor_z < 0.0) { nor_xyz.xy = octahedron_wrap(nor_oct.xy); }");
		node_shader_write_frag(kong, "var n: float3 = normalize((constants.W * float4(normalize(nor_xyz), 0.0)).xyz);");
	}
	else {
		node_shader_write_frag(kong, "if (sample_undo.r == 0.0 && sample_undo.g == 0.0 && sample_undo.b == 0.0) { discard; }");
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_texture(kong, "gbuffer0_undo", NULL);
		if (particle) {
			node_shader_add_constant(kong, "VP: float4x4", "_view_proj_matrix");
			node_shader_add_constant(kong, "particle_hit: float3", "_particle_hit");
			node_shader_write_frag(kong, "var hit_ndc: float4 = constants.VP * float4(constants.particle_hit, 1.0);");
			node_shader_write_frag(kong, "var hit_uv: float2 = hit_ndc.xy / hit_ndc.w;");
			node_shader_write_frag(kong, "hit_uv.x = hit_uv.x * 0.5 + 0.5;");
			node_shader_write_frag(kong, "hit_uv.y = 1.0 - (hit_uv.y * 0.5 + 0.5);");
			node_shader_write_frag(kong, "var g0_undo: float2 = sample_lod(gbuffer0_undo, sampler_linear, hit_uv, 0.0).rg;");
		}
		else {
			node_shader_write_frag(kong, "var g0_undo: float2 = sample_lod(gbuffer0_undo, sampler_linear, constants.inp.xy, 0.0).rg;");
		}
		node_shader_write_frag(kong, "var wn: float3;");
		node_shader_write_frag(kong, "wn.z = 1.0 - abs(g0_undo.x) - abs(g0_undo.y);");
		node_shader_write_frag(kong, "if (wn.z >= 0.0) { wn.xy = g0_undo.xy; } else { wn.xy = octahedron_wrap(g0_undo.xy); }");
		node_shader_write_frag(kong, "var n: float3 = normalize(wn);");
		node_shader_add_constant(kong, "sculpt_symmetry_reflect: float4x4", "_sculpt_symmetry_reflect");
		node_shader_write_frag(kong, "n = normalize((constants.sculpt_symmetry_reflect * float4(n, 0.0)).xyz);");
		if (g_context->xray) {
			node_shader_write_frag(kong, "var xray_nnv: float = floor(raw_undo.a) / 255.0;");
			node_shader_write_frag(kong, "var xray_noct: float2 = float2(raw_undo.a - floor(raw_undo.a), xray_nnv) * 2.0 - 1.0;");
			node_shader_write_frag(kong, "var xray_nnz: float = 1.0 - abs(xray_noct.x) - abs(xray_noct.y);");
			node_shader_write_frag(kong, "var xray_fnor: float3 = float3(xray_noct.xy, xray_nnz);");
			node_shader_write_frag(kong, "if (xray_nnz < 0.0) { xray_fnor.xy = octahedron_wrap(xray_noct.xy); }");
			node_shader_write_frag(kong, "n = normalize((constants.W * float4(normalize(xray_fnor), 0.0)).xyz);");
		}
	}
	if (g_context->tool == TOOL_TYPE_BLUR) {
		// Even out the surface by relaxing each vertex toward the cursors tangent plane
		node_shader_write_frag(kong, "var plane_dist: float = dot(wposition.xyz - winp.xyz, n);");
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb - n * plane_dist * str, raw_undo.a);");
	}
	else if (g_context->tool == TOOL_TYPE_ERASER) {
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb - n * disp * str, raw_undo.a);");
	}
	else if (grab) {
		node_shader_write_frag(kong, "var grab_delta: float3 = (winp.xyz - winplast.xyz) * falloff * opacity;");
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb + grab_delta, raw_undo.a);");
	}
	else {
		node_shader_write_frag(kong, string("var sculpt_disp: float = %s;", sculpt_blend_mode(kong, g_context->brush_blending, "0.0", "disp.x", "str")));
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
	sculpt_mesh_write(kong, attrib, string("var %s: float = 1.0;", out_var));
	for (i32 i = 0; i < masks->length; ++i) {
		slot_layer_t *m = masks->buffer[i];
		if (!slot_layer_is_visible(m) || m == skip) {
			continue;
		}
		node_shader_add_texture(kong, string("texpaint_vert%s", i32_to_string(m->id)), string("_texpaint_vert%s", i32_to_string(m->id)));
		f32 opac = slot_layer_get_opacity(m);
		sculpt_mesh_write(kong, attrib,
		                  string("%s *= lerp(1.0, sample_lod(texpaint_vert%s, sampler_linear, input.tex, 0.0).r, float(%s));", out_var, i32_to_string(m->id),
		                         f32_to_string(opac)));
	}
	sculpt_mesh_write(kong, attrib, string("%s = clamp(%s, 0.0, 1.0);", out_var, out_var));
	return true;
}

void sculpt_make_mesh_run(node_shader_t *kong, slot_layer_t_array_t *sculpt_layers, i32_array_t *sculpt_indices) {
	i32 count = sculpt_layers->length;
	if (count == 0) {
		return;
	}

	i32 idx0 = sculpt_indices->buffer[0];

	node_shader_add_constant(kong, "WVP: float4x4", "_world_view_proj_matrix");
	node_shader_add_constant(kong, "W: float4x4", "_world_matrix");
	node_shader_add_constant(kong, "N: float3x3", "_normal_matrix");
	// Per-object start index into the shared sculpt grid
	node_shader_add_constant(kong, "sculpt_vertex_offset: int", "_sculpt_vertex_offset");
	node_shader_add_out(kong, "wnormal: float3");
	kong->frag_n = false;

	node_shader_add_constant(kong, string("texpaint_sculpt_size%d: float2", idx0), string("_size(_texpaint_sculpt%d)", idx0));

	for (i32 i = 0; i < count; ++i) {
		i32 idx = sculpt_indices->buffer[i];
		node_shader_add_texture(kong, string("texpaint_sculpt%d", idx), string("_texpaint_sculpt%d", idx));
	}

	// The base layer holds the absolute mesh position
	bool base_masked = sculpt_layer_has_visible_masks(sculpt_layers->buffer[0]);
	if (count > 1 || base_masked) {
		node_shader_add_texture(kong, "texpaint_sculpt_base", "_texpaint_sculpt_base");
	}

	// Position
	node_shader_write_vert(kong, "var sculpt_vid: uint = uint(vertex_id()) + uint(constants.sculpt_vertex_offset);");
	node_shader_write_vert(kong, string("var sculpt_grid_w: uint = uint(constants.texpaint_sculpt_size%d.x);", idx0));
	node_shader_write_vert(kong, "var sculpt_uv: uint2 = uint2(sculpt_vid % sculpt_grid_w, sculpt_vid / sculpt_grid_w);");
	node_shader_write_vert(kong, string("var texpaint_sculpt_sample: float4 = texpaint_sculpt%d[sculpt_uv];", idx0));
	node_shader_write_vert(kong, "var sculpt_pos: float3 = texpaint_sculpt_sample.xyz;");
	if (sculpt_mask_value(kong, sculpt_layers->buffer[0], "sculpt_pmask0", false, NULL)) {
		// Blend the base layers deformation back toward the rest pose where the mask is dark
		node_shader_write_vert(kong, "var sculpt_pbase0: float4 = texpaint_sculpt_base[sculpt_uv];");
		node_shader_write_vert(kong, "sculpt_pos = sculpt_pbase0.xyz + (sculpt_pos - sculpt_pbase0.xyz) * sculpt_pmask0;");
	}
	for (i32 i = 1; i < count; ++i) {
		i32 idx = sculpt_indices->buffer[i];
		node_shader_write_vert(kong, string("var texpaint_sculpt_sample_%d: float4 = texpaint_sculpt%d[sculpt_uv];", idx, idx));
		node_shader_write_vert(kong, string("var texpaint_sculpt_base_sample_%d: float4 = texpaint_sculpt_base[sculpt_uv];", idx));
		if (sculpt_mask_value(kong, sculpt_layers->buffer[i], string("sculpt_pmask_%d", idx), false, NULL)) {
			node_shader_write_vert(kong,
			                       string("sculpt_pos = sculpt_pos + (texpaint_sculpt_sample_%d.xyz - texpaint_sculpt_base_sample_%d.xyz) * sculpt_pmask_%d;",
			                              idx, idx, idx));
		}
		else {
			node_shader_write_vert(kong, string("sculpt_pos = sculpt_pos + texpaint_sculpt_sample_%d.xyz - texpaint_sculpt_base_sample_%d.xyz;", idx, idx));
		}
	}
	node_shader_write_vert(kong, "output.pos = constants.WVP * float4(sculpt_pos, 1.0);");
	node_shader_write_vert(kong, "output.wposition = (constants.W * float4(sculpt_pos, 1.0)).xyz;");

	// Normal
	node_shader_write_attrib_vert(kong, "var sculpt_nvid: uint = uint(vertex_id()) + uint(constants.sculpt_vertex_offset);");
	node_shader_write_attrib_vert(kong, "var base_vertex0: uint = sculpt_nvid - (sculpt_nvid % uint(3));");
	node_shader_write_attrib_vert(kong, "var base_vertex1: uint = base_vertex0 + uint(1);");
	node_shader_write_attrib_vert(kong, "var base_vertex2: uint = base_vertex0 + uint(2);");
	node_shader_write_attrib_vert(kong, string("var sculpt_ngrid_w: uint = uint(constants.texpaint_sculpt_size%d.x);", idx0));
	node_shader_write_attrib_vert(kong, "var uv0: uint2 = uint2(base_vertex0 % sculpt_ngrid_w, base_vertex0 / sculpt_ngrid_w);");
	node_shader_write_attrib_vert(kong, "var uv1: uint2 = uint2(base_vertex1 % sculpt_ngrid_w, base_vertex1 / sculpt_ngrid_w);");
	node_shader_write_attrib_vert(kong, "var uv2: uint2 = uint2(base_vertex2 % sculpt_ngrid_w, base_vertex2 / sculpt_ngrid_w);");

	node_shader_write_attrib_vert(kong, string("var meshpos0: float4 = texpaint_sculpt%d[uv0];", idx0));
	node_shader_write_attrib_vert(kong, string("var meshpos1: float4 = texpaint_sculpt%d[uv1];", idx0));
	node_shader_write_attrib_vert(kong, string("var meshpos2: float4 = texpaint_sculpt%d[uv2];", idx0));
	for (i32 i = 1; i < count; ++i) {
		i32 idx = sculpt_indices->buffer[i];
		node_shader_write_attrib_vert(kong, string("meshpos0 = meshpos0 + texpaint_sculpt%d[uv0] - texpaint_sculpt_base[uv0];", idx));
		node_shader_write_attrib_vert(kong, string("meshpos1 = meshpos1 + texpaint_sculpt%d[uv1] - texpaint_sculpt_base[uv1];", idx));
		node_shader_write_attrib_vert(kong, string("meshpos2 = meshpos2 + texpaint_sculpt%d[uv2] - texpaint_sculpt_base[uv2];", idx));
	}

	// Unmasked sculpt face normal
	node_shader_write_attrib_vert(kong, "var meshnor: float3 = normalize(cross(meshpos2.xyz - meshpos1.xyz, meshpos0.xyz - meshpos1.xyz));");
	node_shader_write_attrib_vert(kong, "output.wnormal = constants.N * meshnor;");
	// Reconstruct the face normal from the masked world position
	node_shader_write_attrib_frag(kong, "var n: float3 = normalize(cross(ddx3(input.wposition), ddy3(input.wposition)));");
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
		node_shader_add_constant(kong, string("texpaint_sculpt_size%d: float2", idx0), string("_size(_texpaint_sculpt%d)", idx0));
		for (i32 i = 0; i < scount; ++i) {
			i32 idx = sculpt_indices->buffer[i];
			node_shader_add_texture(kong, string("texpaint_sculpt%d", idx), string("_texpaint_sculpt%d", idx));
		}

		bool base_masked = sculpt_layer_has_visible_masks(sculpt_layers->buffer[0]);
		if (scount > 1 || base_masked) {
			node_shader_add_texture(kong, "texpaint_sculpt_base", "_texpaint_sculpt_base");
		}

		node_shader_write_attrib_vert(kong, string("var sculpt_paint_w: uint = uint(constants.texpaint_sculpt_size%d.x);", idx0));
		node_shader_write_attrib_vert(kong, "var sculpt_uv_paint: uint2 = uint2(uint(vertex_id()) % sculpt_paint_w, uint(vertex_id()) / sculpt_paint_w);");

		node_shader_write_attrib_vert(kong, string("var sculpt_pos_paint: float4 = texpaint_sculpt%d[sculpt_uv_paint];", idx0));
		if (sculpt_mask_value(kong, sculpt_layers->buffer[0], "sculpt_pmask0", true, g_context->layer)) {
			node_shader_write_attrib_vert(kong, "var sculpt_pbase0: float4 = texpaint_sculpt_base[sculpt_uv_paint];");
			node_shader_write_attrib_vert(kong, "sculpt_pos_paint = sculpt_pbase0 + (sculpt_pos_paint - sculpt_pbase0) * sculpt_pmask0;");
		}
		for (i32 i = 1; i < scount; ++i) {
			i32 idx = sculpt_indices->buffer[i];
			if (sculpt_mask_value(kong, sculpt_layers->buffer[i], string("sculpt_pmask_%d", idx), true, g_context->layer)) {
				node_shader_write_attrib_vert(kong, string("var psamp_%d: float4 = texpaint_sculpt%d[sculpt_uv_paint];", idx, idx));
				node_shader_write_attrib_vert(kong, string("var pbasel_%d: float4 = texpaint_sculpt_base[sculpt_uv_paint];", idx));
				node_shader_write_attrib_vert(kong, string("sculpt_pos_paint = sculpt_pos_paint + (psamp_%d - pbasel_%d) * sculpt_pmask_%d;", idx, idx, idx));
			}
			else {
				node_shader_write_attrib_vert(
				    kong, string("sculpt_pos_paint = sculpt_pos_paint + texpaint_sculpt%d[sculpt_uv_paint] - texpaint_sculpt_base[sculpt_uv_paint];", idx));
			}
		}
		node_shader_write_attrib_vert(kong, "output.ndc = constants.WVP * float4(sculpt_pos_paint.xyz, 1.0);");
		if (kong->frag_wposition) {
			node_shader_write_attrib_vert(kong, "output.wposition = (constants.W * float4(sculpt_pos_paint.xyz, 1.0)).xyz;");
		}
	}
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
		render_path_bind_target(string("texpaint_sculpt%s", i32_to_string(id)), "tex");
		render_path_draw_shader("Scene/copy_pass/copyRGBA128_pass");
	}
}

void sculpt_init_meshes() {
	sculpt_ensure_texture_res();

	mesh_object_t_array_t *objects   = g_project->_->paint_objects;
	f32                    max_scale = 0.0;
	for (i32 o = 0; o < objects->length; ++o) {
		if (objects->buffer[o]->data->scale_pos > max_scale) {
			max_scale = objects->buffer[o]->data->scale_pos;
		}
	}

	for (i32 o = 0; o < objects->length; ++o) {
		mesh_object_t *object = objects->buffer[o];
		mesh_data_t   *md     = object->data;
		f32            ratio  = objects->length > 1 ? md->scale_pos / max_scale : 1.0;

		i16_array_t *posa = i16_array_create(md->index_array->length * 4);
		i16_array_t *nora = i16_array_create(md->index_array->length * 2);
		i16_array_t *texa = i16_array_create(md->index_array->length * 2);
		for (i32 i = 0; i < posa->length; ++i) {
			posa->buffer[i] = 32767;
		}
		for (i32 i = 0; i < md->index_array->length; ++i) {
			i32 index               = md->index_array->buffer[i];
			posa->buffer[i * 4]     = math_floor(md->vertex_arrays->buffer[0]->values->buffer[index * 4] * ratio);
			posa->buffer[i * 4 + 1] = math_floor(md->vertex_arrays->buffer[0]->values->buffer[index * 4 + 1] * ratio);
			posa->buffer[i * 4 + 2] = math_floor(md->vertex_arrays->buffer[0]->values->buffer[index * 4 + 2] * ratio);
			posa->buffer[i * 4 + 3] = md->vertex_arrays->buffer[0]->values->buffer[index * 4 + 3];
			nora->buffer[i * 2]     = md->vertex_arrays->buffer[1]->values->buffer[index * 2];
			nora->buffer[i * 2 + 1] = md->vertex_arrays->buffer[1]->values->buffer[index * 2 + 1];
			texa->buffer[i * 2]     = md->vertex_arrays->buffer[2]->values->buffer[index * 2];
			texa->buffer[i * 2 + 1] = md->vertex_arrays->buffer[2]->values->buffer[index * 2 + 1];
		}
		u32_array_t *inda = u32_array_create(md->index_array->length);
		for (i32 i = 0; i < inda->length; ++i) {
			inda->buffer[i] = i;
		}
		mesh_data_t *raw = GC_ALLOC_INIT(mesh_data_t, {.name          = md->name,
		                                               .vertex_arrays = any_array_create_from_raw(
		                                                   (void *[]){
		                                                       GC_ALLOC_INIT(vertex_array_t, {.values = posa, .attrib = "pos", .data = "short4norm"}),
		                                                       GC_ALLOC_INIT(vertex_array_t, {.values = nora, .attrib = "nor", .data = "short2norm"}),
		                                                       GC_ALLOC_INIT(vertex_array_t, {.values = texa, .attrib = "tex", .data = "short2norm"}),
		                                                   },
		                                                   3),
		                                               .index_array = inda,
		                                               .scale_pos   = 1.0,
		                                               .scale_tex   = 1.0});
		mesh_object_set_data(object, mesh_data_create(raw));
	}

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
		char            *ext = string("_undo%s", i32_to_string(i));
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
	slot_layer_t *l = layers_new_layer(true, -1, NULL);
	l->name         = string("Sculpt %d", l->id + 1);
	sculpt_init_meshes();
	sculpt_init_sculpt_texture(l);
	sculpt_init();
}

void render_path_sculpt_displace_pass(char *texpaint_sculpt) {
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

	material_context_t *material_context = NULL;
	shader_context_t   *shader_context   = NULL;
	material_data_t    *mat              = g_project->_->paint_objects->buffer[0]->material;
	for (i32 j = 0; j < mat->contexts->length; ++j) {
		if (string_equals(mat->contexts->buffer[j]->name, "paint")) {
			shader_context   = shader_data_get_context(mat->_->shader, "paint");
			material_context = mat->contexts->buffer[j];
			break;
		}
	}

	gpu_set_pipeline(shader_context->_->pipe);
	uniforms_set_context_consts(shader_context, _render_path_bind_params);
	uniforms_set_obj_consts(shader_context, g_project->_->paint_objects->buffer[0]->base);
	uniforms_set_material_consts(shader_context, material_context);
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
	char *texpaint_sculpt = string("texpaint_sculpt%s", i32_to_string(tid));

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

	f32_array_t *pos     = f32_array_create(nv * 3);
	f32          max_abs = 1.0;
	for (i32 v = 0; v < nv; ++v) {
		f32 x = buffer_get_f32(base_pixels, v * 16);
		f32 y = buffer_get_f32(base_pixels, v * 16 + 4);
		f32 z = buffer_get_f32(base_pixels, v * 16 + 8);
		for (i32 i = 1; i < count; ++i) {
			x += buffer_get_f32(layer_pixels[i], v * 16) - buffer_get_f32(rest_pixels, v * 16);
			y += buffer_get_f32(layer_pixels[i], v * 16 + 4) - buffer_get_f32(rest_pixels, v * 16 + 4);
			z += buffer_get_f32(layer_pixels[i], v * 16 + 8) - buffer_get_f32(rest_pixels, v * 16 + 8);
		}
		pos->buffer[v * 3]     = x;
		pos->buffer[v * 3 + 1] = y;
		pos->buffer[v * 3 + 2] = z;
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

	f32 inv = 1.0 / max_abs;
	for (i32 v = 0; v < nv; ++v) {
		va0->buffer[v * 4]     = math_floor(pos->buffer[v * 3] * inv * 32767.0);
		va0->buffer[v * 4 + 1] = math_floor(pos->buffer[v * 3 + 1] * inv * 32767.0);
		va0->buffer[v * 4 + 2] = math_floor(pos->buffer[v * 3 + 2] * inv * 32767.0);
	}

	for (i32 f = 0; f < math_floor(ia->length / 3.0); ++f) {
		i32    i1               = ia->buffer[f * 3];
		i32    i2               = ia->buffer[f * 3 + 1];
		i32    i3               = ia->buffer[f * 3 + 2];
		vec4_t va               = (vec4_t){pos->buffer[i1 * 3], pos->buffer[i1 * 3 + 1], pos->buffer[i1 * 3 + 2], 1.0};
		vec4_t vb               = (vec4_t){pos->buffer[i2 * 3], pos->buffer[i2 * 3 + 1], pos->buffer[i2 * 3 + 2], 1.0};
		vec4_t vc               = (vec4_t){pos->buffer[i3 * 3], pos->buffer[i3 * 3 + 1], pos->buffer[i3 * 3 + 2], 1.0};
		vec4_t cb               = vec4_norm(vec4_cross(vec4_sub(vc, vb), vec4_sub(va, vb)));
		i32    nx               = math_floor(cb.x * 32767);
		i32    ny               = math_floor(cb.y * 32767);
		i32    nz               = math_floor(cb.z * 32767);
		va1->buffer[i1 * 2]     = nx;
		va1->buffer[i1 * 2 + 1] = ny;
		va0->buffer[i1 * 4 + 3] = nz;
		va1->buffer[i2 * 2]     = nx;
		va1->buffer[i2 * 2 + 1] = ny;
		va0->buffer[i2 * 4 + 3] = nz;
		va1->buffer[i3 * 2]     = nx;
		va1->buffer[i3 * 2 + 1] = ny;
		va0->buffer[i3 * 4 + 3] = nz;
	}

	g->scale_pos = max_abs;
	mesh_data_build_vertices(g->_->vertex_buffer, g->vertex_arrays);
	render_path_raytrace_ready = false;
}

void slot_layer_apply_sculpt(slot_layer_t *raw) {
	if (raw->texpaint_sculpt == NULL) {
		return;
	}

	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	buffer_t              *pixels  = gpu_get_texture_pixels(raw->texpaint_sculpt);

	// Each object occupies a contiguous slice of the shared sculpt grid
	f32 max_abs = 1.0;
	i32 offset  = 0;
	for (i32 o = 0; o < objects->length; ++o) {
		mesh_data_t *g  = objects->buffer[o]->data;
		i32          nv = math_floor(g->vertex_arrays->buffer[0]->values->length / 4.0);
		for (i32 i = 0; i < nv; ++i) {
			i32 t = i + offset;
			f32 x = math_abs(buffer_get_f32(pixels, t * 16));
			f32 y = math_abs(buffer_get_f32(pixels, t * 16 + 4));
			f32 z = math_abs(buffer_get_f32(pixels, t * 16 + 8));
			if (x > max_abs)
				max_abs = x;
			if (y > max_abs)
				max_abs = y;
			if (z > max_abs)
				max_abs = z;
		}
		offset += g->index_array->length;
	}

	offset = 0;
	for (i32 o = 0; o < objects->length; ++o) {
		mesh_object_t *ob  = objects->buffer[o];
		mesh_data_t   *g   = ob->data;
		i16_array_t   *va0 = g->vertex_arrays->buffer[0]->values;
		i32            nv  = math_floor(va0->length / 4.0);

		if (max_abs > 1.0) {
			ob->base->transform->scale_world = g->scale_pos = g->scale_pos * max_abs;
			transform_build_matrix(ob->base->transform);
		}

		for (i32 i = 0; i < nv; ++i) {
			i32 t                  = i + offset;
			va0->buffer[i * 4]     = math_floor(buffer_get_f32(pixels, t * 16) / max_abs * 32767.0);
			va0->buffer[i * 4 + 1] = math_floor(buffer_get_f32(pixels, t * 16 + 4) / max_abs * 32767.0);
			va0->buffer[i * 4 + 2] = math_floor(buffer_get_f32(pixels, t * 16 + 8) / max_abs * 32767.0);
		}

		mesh_data_build_vertices(g->_->vertex_buffer, g->vertex_arrays);
		offset += g->index_array->length;
	}

	util_mesh_calc_normals(true);
	render_path_raytrace_ready = false;

	slot_layer_delete(raw);
}
