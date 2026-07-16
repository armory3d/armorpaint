
#include "../global.h"

bool make_paint_is_raytraced_bake() {
	return g_context->bake_type == BAKE_TYPE_INIT;
}

bool make_paint_is_zero_constant(char *s) {
	char *end;
	f32   f = strtof(s, &end);
	return end != s && *end == '\0' && f == 0.0;
}

string_array_t *make_paint_color_attachments() {
	if (g_context->tool == TOOL_TYPE_COLORID || g_context->tool == TOOL_TYPE_CURSOR) {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "RGBA32",
		    },
		    1);
		return res;
	}
	if (g_context->tool == TOOL_TYPE_PICKER && g_context->pick_pos_nor_tex) {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "RGBA128",
		        "RGBA128",
		    },
		    2);
		return res;
	}
	if (g_context->tool == TOOL_TYPE_PICKER && slot_layer_is_mask(g_context->layer)) {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "RGBA32",
		        "RGBA32",
		    },
		    2);
		return res;
	}
	if (g_context->tool == TOOL_TYPE_PICKER || g_context->tool == TOOL_TYPE_MATERIAL) {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "RGBA32",
		        "RGBA32",
		        "RGBA32",
		        "RGBA32",
		    },
		    4);
		return res;
	}
	if (g_context->tool == TOOL_TYPE_BAKE && make_paint_is_raytraced_bake()) {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "RGBA64",
		        "RGBA64",
		    },
		    2);
		return res;
	}

	gpu_texture_format_t format = base_bits_handle->i == TEXTURE_BITS_BITS8    ? GPU_TEXTURE_FORMAT_RGBA32
	                              : base_bits_handle->i == TEXTURE_BITS_BITS16 ? GPU_TEXTURE_FORMAT_RGBA64
	                                                                           : GPU_TEXTURE_FORMAT_RGBA128;

	if (format == GPU_TEXTURE_FORMAT_RGBA64) {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "RGBA64",
		        "RGBA64",
		        "RGBA64",
		        "R8",
		    },
		    4);
		return res;
	}
	if (format == GPU_TEXTURE_FORMAT_RGBA128) {
		string_array_t *res = any_array_create_from_raw(
		    (void *[]){
		        "RGBA128",
		        "RGBA128",
		        "RGBA128",
		        "R8",
		    },
		    4);
		return res;
	}

	string_array_t *res = any_array_create_from_raw(
	    (void *[]){
	        "RGBA32",
	        "RGBA32",
	        "RGBA32",
	        "R8",
	    },
	    4);
	return res;
}

node_shader_context_t *make_paint_run(material_t *data, material_context_t *matcon) {
	bool picking_tool = g_context->tool == TOOL_TYPE_COLORID || g_context->tool == TOOL_TYPE_PICKER || g_context->tool == TOOL_TYPE_MATERIAL ||
	                    g_context->tool == TOOL_TYPE_CURSOR;
	if (g_context->layer->texpaint_sculpt != NULL && !picking_tool) {
		return sculpt_make_sculpt_run(data, matcon);
	}

	char                  *context_id = "paint";
	shader_context_t      *props      = GC_ALLOC_INIT(shader_context_t, {.name            = context_id,
	                                                                     .depth_write     = false,
	                                                                     .compare_mode    = "always",
	                                                                     .cull_mode       = "none",
	                                                                     .vertex_elements = any_array_create_from_raw(
                                                                   (void *[]){
                                                                       GC_ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
                                                                       GC_ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
                                                                       GC_ALLOC_INIT(vertex_element_t, {.name = "tex", .data = "short2norm"}),
                                                                   },
                                                                   3),
	                                                                     .color_attachments = make_paint_color_attachments()});
	node_shader_context_t *con_paint  = node_shader_context_create(data, props);

	if (mesh_data_get_vertex_array(g_context->paint_object->data, "col") != NULL) {
		node_shader_context_add_elem(con_paint, "col", "short4norm");
	}

	if (mesh_data_get_vertex_array(g_context->paint_object->data, "tex1") != NULL) {
		node_shader_context_add_elem(con_paint, "tex1", "short2norm");
	}

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
	con_paint->allow_vcols = mesh_data_get_vertex_array(g_context->paint_object->data, "col") != NULL;

	node_shader_t *kong = node_shader_context_make_kong(con_paint);

	if (g_context->tool == TOOL_TYPE_BAKE && g_context->bake_type == BAKE_TYPE_INIT) {
		// Init raytraced bake
		make_bake_position_normal(kong);
		con_paint->data->shader_from_source = true;
		gpu_create_shaders_from_kong(node_shader_get(kong), &con_paint->data->vertex_shader, &con_paint->data->fragment_shader,
		                             &con_paint->data->_->vertex_shader_size, &con_paint->data->_->fragment_shader_size);
		return con_paint;
	}

	if (g_context->tool == TOOL_TYPE_BAKE) {
		make_bake_set_color_writes(con_paint);
	}

	if (g_context->tool == TOOL_TYPE_COLORID || g_context->tool == TOOL_TYPE_PICKER || g_context->tool == TOOL_TYPE_MATERIAL ||
	    g_context->tool == TOOL_TYPE_CURSOR) {
		make_picking_run(kong);
		con_paint->data->shader_from_source = true;
		gpu_create_shaders_from_kong(node_shader_get(kong), &con_paint->data->vertex_shader, &con_paint->data->fragment_shader,
		                             &con_paint->data->_->vertex_shader_size, &con_paint->data->_->fragment_shader_size);
		return con_paint;
	}

	bool face_fill      = g_context->tool == TOOL_TYPE_FILL && g_context->fill_type == FILL_TYPE_FACE;
	bool uv_island_fill = g_context->tool == TOOL_TYPE_FILL && g_context->fill_type == FILL_TYPE_UV_ISLAND;
	bool decal          = context_is_decal();

	if (g_context->layer->uv_map == 1) {
		node_shader_write_vert(kong, "var tpos: float2 = float2(input.tex1.x * 2.0 - 1.0, (1.0 - input.tex1.y) * 2.0 - 1.0);");
	}
	else {
		node_shader_write_vert(kong, "var tpos: float2 = float2(input.tex.x * 2.0 - 1.0, (1.0 - input.tex.y) * 2.0 - 1.0);");
	}

	node_shader_write_vert(kong, "output.pos = float4(tpos, 0.0, 1.0);");

	bool decal_layer = g_context->layer->fill_material != NULL && g_context->layer->uv_type == UV_TYPE_PROJECT;
	if (decal_layer) {
		node_shader_add_constant(kong, "WVP: float4x4", "_decal_layer_matrix");
	}
	else {
		node_shader_add_constant(kong, "WVP: float4x4", "_world_view_proj_matrix");
	}

	node_shader_add_out(kong, "ndc: float4");
	node_shader_write_attrib_vert(kong, "output.ndc = constants.WVP * float4(input.pos.xyz, 1.0);");

	node_shader_write_attrib_frag(kong, "var sp: float3 = (input.ndc.xyz / input.ndc.w);");
	node_shader_write_attrib_frag(kong, "sp.x = sp.x * 0.5 + 0.5;");
	node_shader_write_attrib_frag(kong, "sp.y = sp.y * 0.5 + 0.5;");
	node_shader_write_attrib_frag(kong, "sp.y = 1.0 - sp.y;");
	node_shader_write_attrib_frag(kong, "sp.z -= 0.0001;"); // small bias

	uv_type_t uv_type = g_context->layer->fill_material != NULL ? g_context->layer->uv_type : g_context->brush_paint;
	if (uv_type == UV_TYPE_PROJECT) {
		kong->frag_ndcpos = true;
	}

	node_shader_add_constant(kong, "inp: float4", "_input_brush");
	node_shader_add_constant(kong, "inplast: float4", "_input_brush_last");
	node_shader_add_constant(kong, "aspect_ratio: float", "_aspect_ratio_window");
	node_shader_write_frag(kong, "var bsp: float2 = sp.xy * 2.0 - 1.0;");
	node_shader_write_frag(kong, "bsp.x *= constants.aspect_ratio;");
	node_shader_write_frag(kong, "bsp = bsp * 0.5 + 0.5;");

	// Select tool mask
	if (g_context->select_active) {
		node_shader_add_constant(kong, "select_mask: float4", "_select_mask");
		node_shader_write_frag(kong, "if (sp.x < constants.select_mask.x || sp.x > constants.select_mask.z || sp.y < constants.select_mask.y || sp.y > "
		                             "constants.select_mask.w) { discard; }");
	}

	node_shader_add_texture(kong, "gbufferD", NULL);

	kong->frag_out = "float4[4]";

	node_shader_add_constant(kong, "brush_radius: float", "_brush_radius");
	node_shader_add_constant(kong, "brush_opacity: float", "_brush_opacity");
	node_shader_add_constant(kong, "brush_hardness: float", "_brush_hardness");

	if (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_CLONE || g_context->tool == TOOL_TYPE_BLUR ||
	    g_context->tool == TOOL_TYPE_PARTICLE || decal) {

		bool depth_reject = !g_context->xray;
		if (!g_config->brush_depth_reject) {
			depth_reject = false;
		}

		// TODO: sp.z needs to take height channel into account
		bool particle = g_context->tool == TOOL_TYPE_PARTICLE;
		if (!decal && !particle) {
			if (make_material_height_used || g_context->sym_x || g_context->sym_y || g_context->sym_z) {
				depth_reject = false;
			}
		}

		if (depth_reject && !particle) {
			node_shader_write_frag(kong, "if (sp.z > sample_lod(gbufferD, sampler_linear, sp.xy, 0.0).r - 0.00008) { discard; }");
		}

		make_brush_run(kong);
	}
	else { // Fill, Bake
		node_shader_write_frag(kong, "var dist: float = 0.0;");
		bool angle_fill = g_context->tool == TOOL_TYPE_FILL && g_context->fill_type == FILL_TYPE_ANGLE;
		if (angle_fill) {
			node_shader_add_function(kong, str_octahedron_wrap);
			node_shader_add_texture(kong, "gbuffer0", NULL);
			node_shader_write_frag(kong, "var g0: float2 = sample_lod(gbuffer0, sampler_linear, constants.inp.xy, 0.0).rg;");
			node_shader_write_frag(kong, "var wn: float3;");
			node_shader_write_frag(kong, "wn.z = 1.0 - abs(g0.x) - abs(g0.y);");
			// node_shader_write_frag(kong, "wn.xy = wn.z >= 0.0 ? g0.xy : octahedron_wrap(g0.xy);");
			node_shader_write_frag(
			    kong, "if (wn.z >= 0.0) { wn.x = g0.x; wn.y = g0.y; } else { var f2: float2 = octahedron_wrap(g0.xy); wn.x = f2.x; wn.y = f2.y; }");
			node_shader_write_frag(kong, "wn = normalize(wn);");
			kong->frag_n = true;
			f32 angle    = g_context->brush_angle_reject_dot;
			node_shader_write_frag(kong, string("if (dot(wn, n) < %s) { discard; }", f32_to_string(angle)));
		}
		bool stencil_fill = g_context->tool == TOOL_TYPE_FILL && g_context->brush_stencil_image != NULL;
		if (stencil_fill) {
			node_shader_write_frag(kong, "if (sp.z > sample_lod(gbufferD, sampler_linear, sp.xy, 0.0).r - 0.00005) { discard; }");
		}
	}

	if (g_context->colorid_picked || face_fill || uv_island_fill) {
		node_shader_add_out(kong, "tex_coord_pick: float2");
		node_shader_write_vert(kong, "output.tex_coord_pick = input.tex;");
		if (g_context->colorid_picked) {
			make_discard_color_id(kong, "tex_coord_pick");
		}
		if (face_fill) {
			make_discard_face(kong);
		}
		else if (uv_island_fill) {
			make_discard_uv_island(kong);
		}
	}

	if (g_context->picker_paint_mask) {
		make_discard_material_id(kong, NULL);
	}

	make_texcoord_run(kong);

	if (g_context->tool == TOOL_TYPE_CLONE || g_context->tool == TOOL_TYPE_BLUR) {
		node_shader_add_texture(kong, "gbuffer2", NULL);
		node_shader_add_constant(kong, "gbuffer_size: float2", "_gbuffer_size");
		node_shader_add_texture(kong, "texpaint_undo", "_texpaint_undo");
		node_shader_add_texture(kong, "texpaint_nor_undo", "_texpaint_nor_undo");
		node_shader_add_texture(kong, "texpaint_pack_undo", "_texpaint_pack_undo");

		if (g_context->tool == TOOL_TYPE_CLONE) {
			make_clone_run(kong);
		}
		else { // Blur, Smudge
			make_blur_run(kong);
		}
	}
	else {
		parser_material_parse_emission          = g_context->material->paint_emis;
		parser_material_parse_subsurface        = g_context->material->paint_subs;
		parser_material_parse_height            = g_context->material->paint_height;
		uv_type_t uv_type                       = g_context->layer->fill_material != NULL ? g_context->layer->uv_type : g_context->brush_paint;
		parser_material_triplanar               = uv_type == UV_TYPE_TRIPLANAR && !decal;
		parser_material_sample_keep_aspect      = decal;
		gc_unroot(parser_material_sample_uv_scale);
		parser_material_sample_uv_scale = "constants.brush_scale";
		gc_root(parser_material_sample_uv_scale);
		shader_out_t *sout                      = parser_material_parse(g_context->material->canvas, con_paint, kong, matcon);
		parser_material_parse_emission          = false;
		parser_material_parse_subsurface        = false;
		parser_material_parse_height            = false;
		char *base                              = sout->out_basecol;
		char *rough                             = sout->out_roughness;
		char *met                               = sout->out_metallic;
		char *occ                               = sout->out_occlusion;
		char *nortan                            = parser_material_out_normaltan;
		char *height                            = sout->out_height;
		char *opac                              = sout->out_opacity;
		char *emis                              = sout->out_emission;
		char *subs                              = sout->out_subsurface;
		node_shader_write_frag(kong, string("var basecol: float3 = %s;", base));
		node_shader_write_frag(kong, string("var roughness: float = %s;", rough));
		node_shader_write_frag(kong, string("var metallic: float = %s;", met));
		node_shader_write_frag(kong, string("var occlusion: float = %s;", occ));
		node_shader_write_frag(kong, string("var nortan: float3 = %s;", nortan));
		node_shader_write_frag(kong, string("var height: float = %s;", height));
		node_shader_write_frag(kong, string("var mat_opacity: float = %s;", opac));
		if (g_context->material->paint_opac_mode == OPACITY_MODE_TRANSLUC) {
			make_material_transluc_used = true;
			node_shader_write_frag(kong, "var opacity: float = 1.0;");
		}
		else {
			node_shader_write_frag(kong, "var opacity: float = mat_opacity;");
		}

		if (g_context->layer->fill_material == NULL) {
			node_shader_write_frag(kong, "opacity *= constants.brush_opacity;");
		}

		if (g_context->material->paint_emis) {
			node_shader_write_frag(kong, string("var emis: float = %s;", emis));
		}
		if (g_context->material->paint_subs) {
			node_shader_write_frag(kong, string("var subs: float = %s;", subs));
		}
		if (!make_material_height_used && !make_paint_is_zero_constant(height)) {
			make_material_height_used = true;
			// Height used for the first time, also rebuild vertex shader
			return make_paint_run(data, matcon);
		}
		make_material_emis_used = parse_float(emis) != 0.0;
		make_material_subs_used = parse_float(subs) != 0.0;
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

	if (g_context->brush_stencil_image != NULL &&
	    (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER || g_context->tool == TOOL_TYPE_FILL || g_context->tool == TOOL_TYPE_CLONE ||
	     g_context->tool == TOOL_TYPE_BLUR || g_context->tool == TOOL_TYPE_PARTICLE || decal)) {
		node_shader_add_texture(kong, "texbrushstencil", "_texbrushstencil");
		node_shader_add_constant(kong, "texbrushstencil_size: float2", "_size(_texbrushstencil)");
		node_shader_add_constant(kong, "stencil_transform: float4", "_stencil_transform");
		node_shader_write_frag(
		    kong, "var stencil_uv: float2 = (sp.xy - constants.stencil_transform.xy) / constants.stencil_transform.z * float2(constants.aspect_ratio, 1.0);");
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

	if (g_context->brush_mask_image != NULL && (g_context->tool == TOOL_TYPE_BRUSH || g_context->tool == TOOL_TYPE_ERASER)) {
		// Project mask onto mesh surface
		node_shader_add_texture(kong, "texbrushmask", "_texbrushmask");
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_texture(kong, "gbuffer0", NULL);
		node_shader_add_constant(kong, "camera_right: float3", "_camera_right");
		node_shader_add_constant(kong, "camera_up: float3", "_camera_up");
		node_shader_add_constant(kong, "camera_align: float", "_brush_camera_align");
		node_shader_add_constant(kong, "VP: float4x4", "_view_proj_matrix");
		node_shader_write_frag(kong, "var mn0: float2 = sample_lod(gbuffer0, sampler_linear, constants.inp.xy, 0.0).rg;");
		node_shader_write_frag(kong, "var mn: float3;");
		node_shader_write_frag(kong, "mn.z = 1.0 - abs(mn0.x) - abs(mn0.y);");
		node_shader_write_frag(
		    kong, "if (mn.z >= 0.0) { mn.x = mn0.x; mn.y = mn0.y; } else { var mfw: float2 = octahedron_wrap(mn0.xy); mn.x = mfw.x; mn.y = mfw.y; }");
		node_shader_write_frag(kong, "mn = normalize(mn);");
		node_shader_write_frag(kong, "var mr: float3 = constants.camera_right;");
		node_shader_write_frag(kong, "if (abs(dot(mn, mr)) > 0.999) { mr = float3(0.0, 0.0, 1.0); }");
		node_shader_write_frag(kong, "var mt: float3 = normalize(mr - mn * dot(mr, mn));");
		node_shader_write_frag(kong, "var mb: float3 = cross(mt, mn);");
		node_shader_write_frag(kong, "var pa_mask_3d: float3 = input.wposition - winp.xyz;");
		node_shader_write_frag(kong, "var pa_mask: float2;");
		node_shader_write_frag(kong, "if (constants.camera_align > 0.0) {");
		node_shader_write_frag(kong, "var mvp_up_y: float = (constants.VP * float4(constants.camera_up, 0.0)).y;");
		node_shader_write_frag(kong, "var msa: float2 = sp.xy - constants.inp.xy;");
		node_shader_write_frag(kong, "msa.x *= constants.aspect_ratio;");
		node_shader_write_frag(kong, "pa_mask = msa * (2.0 / (mvp_up_y * winp.w));");
		node_shader_write_frag(kong, "}");
		node_shader_write_frag(kong, "else {");
		node_shader_write_frag(kong, "pa_mask = float2(dot(pa_mask_3d, mt), dot(pa_mask_3d, mb));");
		node_shader_write_frag(kong, "}");
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

	node_shader_write_frag(kong, string("if (opacity <= float(%s)) { discard; }", f32_to_string(g_config->brush_alpha_discard)));

	if (g_context->tool == TOOL_TYPE_PARTICLE) { // Particle mask
		make_particle_mask(kong);
	}
	else { // Brush cursor mask
		node_shader_write_frag(kong, "var t: float = dist / constants.brush_radius;");
		node_shader_write_frag(kong, "var t2: float = clamp((t - constants.brush_hardness) / max(1.0 - constants.brush_hardness, 0.001), 0.0, 1.0);");
		node_shader_write_frag(kong, "var str: float = (1.0 - t2 * t2 * (3.0 - 2.0 * t2)) * opacity;");
	}

	if (g_context->tool == TOOL_TYPE_CLONE) {
		node_shader_write_frag(kong, "str *= clone_src_alpha;");
	}

	// Manual blending to preserve memory
	kong->frag_wvpposition = true;
	node_shader_write_frag(kong,
	                       "var sample_tc: float2 = float2(input.wvpposition.x / input.wvpposition.w, input.wvpposition.y / input.wvpposition.w) * 0.5 + 0.5;");
	node_shader_write_frag(kong, "sample_tc.y = 1.0 - sample_tc.y;");
	node_shader_add_texture(kong, "paintmask", NULL);
	node_shader_write_frag(kong, "var sample_mask: float = sample_lod(paintmask, sampler_linear, sample_tc, 0.0).r;");
	node_shader_write_frag(kong, "var base_str: float = max(str, sample_mask);");
	node_shader_write_frag(kong, "str = base_str + str * (1.0 - base_str) * 0.1;");

	node_shader_add_texture(kong, "texpaint_undo", "_texpaint_undo");
	node_shader_write_frag(kong, "var sample_undo: float4 = sample_lod(texpaint_undo, sampler_linear, sample_tc, 0.0);");

	f32 matid = g_context->material->id / 255.0;
	if (g_context->picker_paint_mask) {
		matid = g_context->materialid_picked / 255.0; // Keep existing material id in place when mask is set
	}
	char *matid_string = parser_material_vec1(matid * 3.0);
	node_shader_write_frag(kong, string("var matid: float = %s;", matid_string));

	// matid % 3 == 0 - normal, 1 - emission, 2 - subsurface
	if (g_context->material->paint_emis && make_material_emis_used) {
		node_shader_write_frag(kong, "if (emis > 0.0) {");
		node_shader_write_frag(kong, "	matid += 1.0 / 255.0;");
		node_shader_write_frag(kong, "	if (str == 0.0) { discard; }");
		node_shader_write_frag(kong, "}");
	}
	else if (g_context->material->paint_subs && make_material_subs_used) {
		node_shader_write_frag(kong, "if (subs > 0.0) {");
		node_shader_write_frag(kong, "	matid += 2.0 / 255.0;");
		node_shader_write_frag(kong, "	if (str == 0.0) { discard; }");
		node_shader_write_frag(kong, "}");
	}

	if (g_context->tool == TOOL_TYPE_ERASER) {
		node_shader_write_frag(kong, "basecol = float3(0.0, 0.0, 0.0);");
		node_shader_write_frag(kong, "nortan = float3(0.5, 0.5, 1.0);");
		node_shader_write_frag(kong, "occlusion = 1.0;");
		node_shader_write_frag(kong, "roughness = 0.0;");
		node_shader_write_frag(kong, "metallic = 0.0;");
		node_shader_write_frag(kong, "height = 0.0;");
	}

	// Output
	node_shader_add_texture(kong, "texpaint_nor_undo", "_texpaint_nor_undo");
	node_shader_add_texture(kong, "texpaint_pack_undo", "_texpaint_pack_undo");
	node_shader_write_frag(kong, "var sample_nor_undo: float4 = sample_lod(texpaint_nor_undo, sampler_linear, sample_tc, 0.0);");
	node_shader_write_frag(kong, "var sample_pack_undo: float4 = sample_lod(texpaint_pack_undo, sampler_linear, sample_tc, 0.0);");

	bool is_mask = slot_layer_is_mask(g_context->layer);
	if (g_context->tool == TOOL_TYPE_ERASER && !is_mask) {
		node_shader_write_frag(kong, "var out_a: float = sample_undo.a * (1.0 - str);");
		node_shader_write_frag(kong, "output[0] = float4(sample_undo.rgb, out_a);");
	}
	else if (g_context->tool == TOOL_TYPE_BLUR && !is_mask) {
		node_shader_write_frag(kong, "var t_blur: float = str / max(blur_src_alpha, 0.0000001);");
		node_shader_write_frag(kong, "var out_a: float = str + sample_undo.a * (1.0 - t_blur);");
		node_shader_write_frag(kong,
		                       "output[0] = float4((basecol * t_blur + sample_undo.rgb * sample_undo.a * (1.0 - t_blur)) / max(out_a, 0.0000001), out_a);");
	}
	else if (g_context->material->paint_opac_mode == OPACITY_MODE_TRANSLUC || g_context->layer->fill_material != NULL) {
		node_shader_write_frag(kong, string("output[0] = float4(%s, %s);",
		                                    make_material_blend_mode(kong, g_context->brush_blending, "sample_undo.rgb", "basecol", "str"), "mat_opacity"));
	}
	else if (g_context->brush_blending == BLEND_TYPE_MIX && !is_mask) {
		node_shader_write_frag(kong, "var out_a: float = str + sample_undo.a * (1.0 - str);");
		node_shader_write_frag(kong, "output[0] = float4((basecol * str + sample_undo.rgb * sample_undo.a * (1.0 - str)) / max(out_a, 0.0000001), out_a);");
	}
	else {
		node_shader_write_frag(kong, "var out_a: float = str + sample_undo.a * (1.0 - str);");
		node_shader_write_frag(kong, string("output[0] = float4(%s, %s);",
		                                    make_material_blend_mode(kong, g_context->brush_blending, "sample_undo.rgb", "basecol", "str"), "out_a"));
	}
	node_shader_write_frag(kong, "output[1] = float4(lerp3(sample_nor_undo.rgb, nortan, str), matid);");
	node_shader_write_frag(kong, "output[2] = lerp4(sample_pack_undo, float4(occlusion, roughness, metallic, height), str);");
	node_shader_write_frag(kong, "output[3] = float4(str, 0.0, 0.0, 1.0);");

	if (!g_context->material->paint_base) {
		con_paint->data->color_writes_red->buffer[0]   = false;
		con_paint->data->color_writes_green->buffer[0] = false;
		con_paint->data->color_writes_blue->buffer[0]  = false;
	}
	if (!g_context->material->paint_opac) {
		con_paint->data->color_writes_alpha->buffer[0] = false;
	}
	if (!g_context->material->paint_nor) {
		con_paint->data->color_writes_red->buffer[1]   = false;
		con_paint->data->color_writes_green->buffer[1] = false;
		con_paint->data->color_writes_blue->buffer[1]  = false;
	}
	if (!g_context->material->paint_occ) {
		con_paint->data->color_writes_red->buffer[2] = false;
	}
	if (!g_context->material->paint_rough) {
		con_paint->data->color_writes_green->buffer[2] = false;
	}
	if (!g_context->material->paint_met) {
		con_paint->data->color_writes_blue->buffer[2] = false;
	}
	if (!g_context->material->paint_height) {
		con_paint->data->color_writes_alpha->buffer[2] = false;
	}

	if (is_mask) {
		// Base color only
		con_paint->data->color_writes_green->buffer[0] = false;
		con_paint->data->color_writes_blue->buffer[0]  = false;
		con_paint->data->color_writes_alpha->buffer[0] = false;
		con_paint->data->color_writes_red->buffer[1]   = false;
		con_paint->data->color_writes_green->buffer[1] = false;
		con_paint->data->color_writes_blue->buffer[1]  = false;
		con_paint->data->color_writes_alpha->buffer[1] = false;
		con_paint->data->color_writes_red->buffer[2]   = false;
		con_paint->data->color_writes_green->buffer[2] = false;
		con_paint->data->color_writes_blue->buffer[2]  = false;
		con_paint->data->color_writes_alpha->buffer[2] = false;
	}

	if (g_context->tool == TOOL_TYPE_BAKE) {
		make_bake_run(con_paint, kong);
	}

	parser_material_finalize(con_paint);
	parser_material_triplanar          = false;
	parser_material_sample_keep_aspect = false;

	sculpt_make_paint_run(kong);

	con_paint->data->shader_from_source = true;
	gpu_create_shaders_from_kong(node_shader_get(kong), &con_paint->data->vertex_shader, &con_paint->data->fragment_shader,
	                             &con_paint->data->_->vertex_shader_size, &con_paint->data->_->fragment_shader_size);

	return con_paint;
}
