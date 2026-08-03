
#include "../global.h"

node_shader_context_t *make_mesh_preview_run(material_t *data, material_context_t *matcon, bool viewport) {
	char             *context_id = "mesh";
	shader_context_t *props      = GC_ALLOC_INIT(shader_context_t, {.name            = context_id,
	                                                                .depth_write     = true,
	                                                                .compare_mode    = "less",
	                                                                .cull_mode       = "clockwise",
	                                                                .vertex_elements = any_array_create_from_raw(
                                                                   (void *[]){
                                                                       GC_ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
                                                                       GC_ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
                                                                       GC_ALLOC_INIT(vertex_element_t, {.name = "tex", .data = "short2norm"}),
                                                                   },
                                                                   3),
	                                                                .color_attachments = any_array_create_from_raw(
                                                                   (void *[]){
                                                                       "RGBA64",
                                                                       "RGBA64",
                                                                   },
                                                                   2),
	                                                                .depth_attachment = "D32"});

	if (viewport) {
		string_array_push(props->color_attachments, "RGBA64");
	}

	node_shader_context_t *con_mesh = node_shader_context_create(data, props);

	node_shader_t *kong = node_shader_context_make_kong(con_mesh);

	char *pos = "input.pos";

	node_shader_add_constant(kong, "WVP: float4x4", "_world_view_proj_matrix");
	node_shader_write_attrib_vert(kong, string("output.pos = constants.WVP * float4(%s.xyz, 1.0);", pos));
	f32   sc          = g_context->brush_scale * g_context->brush_nodes_scale;
	char *brush_scale = f32_to_string(sc);
	node_shader_add_out(kong, "tex_coord: float2");
	node_shader_write_attrib_vert(kong, string("output.tex_coord = input.tex * float(%s);", brush_scale));
	node_shader_write_attrib_frag(kong, "var tex_coord: float2 = input.tex_coord;");

	bool decal                         = g_context->decal_preview;
	parser_material_sample_keep_aspect = decal;
	gc_unroot(parser_material_sample_uv_scale);
	parser_material_sample_uv_scale = string_copy(brush_scale);
	gc_root(parser_material_sample_uv_scale);
	parser_material_parse_height            = make_material_height_used;
	shader_out_t *sout                      = parser_material_parse(g_context->material->canvas, con_mesh, kong, matcon);
	parser_material_parse_height            = false;
	parser_material_sample_keep_aspect      = false;
	char *base                              = sout->out_basecol;
	char *rough                             = sout->out_roughness;
	char *met                               = sout->out_metallic;
	char *occ                               = sout->out_occlusion;
	char *opac                              = sout->out_opacity;
	char *height                            = sout->out_height;
	char *nortan                            = parser_material_out_normaltan;
	node_shader_write_frag(kong, string("var basecol: float3 = pow3(%s, float3(2.2, 2.2, 2.2));", base));
	node_shader_write_frag(kong, string("var roughness: float = %s;", rough));
	node_shader_write_frag(kong, string("var metallic: float = %s;", met));
	node_shader_write_frag(kong, string("var occlusion: float = %s;", occ));
	node_shader_write_frag(kong, string("var opacity: float = %s;", opac));
	node_shader_write_frag(kong, string("var nortan: float3 = %s;", nortan));
	node_shader_write_frag(kong, string("var height: float = %s;", height));

	if (decal) {
		if (g_context->tool == TOOL_TYPE_TEXT) {
			node_shader_add_texture(kong, "textexttool", "_textexttool");
			node_shader_write_frag(kong, string("opacity *= sample_lod(textexttool, sampler_linear, tex_coord / float(%s), 0.0).r;", brush_scale));
		}
		f32 opac = g_config->brush_alpha_discard;
		node_shader_write_frag(kong, string("if (opacity <= float(%s)) { discard; }", f32_to_string(opac)));
	}

	kong->frag_out = viewport ? "float4[3]" : "float4[2]";
	kong->frag_n   = true;

	node_shader_add_function(kong, str_pack_float_int16);
	node_shader_add_function(kong, str_cotangent_frame);
	node_shader_add_function(kong, str_octahedron_wrap);

	if (g_context->material->paint_opac_mode == OPACITY_MODE_TRANSLUC) {
		kong->frag_wvpposition = true;
		node_shader_add_function(kong, str_dither_bayer);
		node_shader_write_frag(
		    kong, "var fragcoord1: float2 = float2(input.wvpposition.x / input.wvpposition.w, input.wvpposition.y / input.wvpposition.w) * 0.5 + 0.5;");
		node_shader_write_frag(kong, "var dither: float = dither_bayer(fragcoord1 * float2(256.0, 256.0));");
		node_shader_write_frag(kong, "if (opacity <= dither) { discard; }");
	}

	if (make_material_height_used) {
		node_shader_write_frag(kong, "if (height > 0.0) {");
		node_shader_write_frag(kong, "var height_dx: float = ddx(height * 2.0);");
		node_shader_write_frag(kong, "var height_dy: float = ddy(height * 2.0);");
		// Whiteout blend
		node_shader_write_frag(kong, "var n1: float3 = nortan * float3(2.0, 2.0, 2.0) - float3(1.0, 1.0, 1.0);");
		node_shader_write_frag(kong, "var n2: float3 = normalize(float3(height_dx * 16.0, height_dy * 16.0, 1.0));");
		node_shader_write_frag(kong, "nortan = normalize(float3(n1.xy + n2.xy, n1.z * n2.z)) * float3(0.5, 0.5, 0.5) + float3(0.5, 0.5, 0.5);");
		node_shader_write_frag(kong, "}");
	}

	// Apply normal channel
	if (decal) {
		// TODO
	}
	else {
		kong->frag_vvec = true;
		node_shader_write_frag(kong, "var TBN: float3x3 = cotangent_frame(n, vvec, tex_coord);");
		node_shader_write_frag(kong, "n = nortan * 2.0 - 1.0;");
		node_shader_write_frag(kong, "n.y = -n.y;");
		node_shader_write_frag(kong, "n = normalize(TBN * n);");
	}

	node_shader_write_frag(kong, "n = n / (abs(n.x) + abs(n.y) + abs(n.z));");
	// node_shader_write_frag(kong, "n.xy = n.z >= 0.0 ? n.xy : octahedron_wrap(n.xy);");
	node_shader_write_frag(kong, "if (n.z < 0.0) { n.xy = octahedron_wrap(n.xy); }");
	// uint matid = uint(0);

	if (decal) {
		node_shader_write_frag(kong, "output[0] = float4(n.x, n.y, roughness, pack_f32_i16(metallic, uint(0)));"); // metallic/matid
		node_shader_write_frag(kong, "output[1] = float4(basecol, occlusion);");
	}
	else {
		node_shader_write_frag(
		    kong, "output[0] = float4(n.x, n.y, lerp(1.0, roughness, opacity), pack_f32_i16(lerp(1.0, metallic, opacity), uint(0)));"); // metallic/matid
		node_shader_write_frag(kong, "output[1] = float4(lerp3(float3(0.0, 0.0, 0.0), basecol, opacity), occlusion);");
	}

	if (viewport) {
		node_shader_write_frag(kong, "output[2] = float4(0.0, 0.0, 0.0, 0.0);");
	}

	parser_material_finalize(con_mesh);

	con_mesh->data->shader_from_source = true;
	gpu_create_shaders_from_kong(node_shader_get(kong), &con_mesh->data->vertex_shader, &con_mesh->data->fragment_shader,
	                             &con_mesh->data->_->vertex_shader_size, &con_mesh->data->_->fragment_shader_size);
	return con_mesh;
}

material_data_t *make_mesh_preview_viewport(slot_material_t *slot) {
	slot_material_t *_material = g_context->material;
	g_context->material        = slot;

	material_context_t    *mcon = GC_ALLOC_INIT(material_context_t, {.name = "mesh", .bind_textures = any_array_create_from_raw((void *[]){}, 0)});
	material_t            *mm   = GC_ALLOC_INIT(material_t, {.name = "Material", .canvas = NULL});
	node_shader_context_t *con  = make_mesh_preview_run(mm, mcon, true);
	shader_context_load(con->data);
	material_context_load(mcon);

	shader_data_t *sd = GC_ALLOC_INIT(shader_data_t, {0});
	sd->name          = string("_material_%s", i32_to_string(slot->id));
	sd->contexts      = any_array_create_from_raw((void *[]){con->data}, 1);

	material_data_t *md = GC_ALLOC_INIT(material_data_t, {0});
	md->name            = sd->name;
	md->contexts        = any_array_create_from_raw((void *[]){mcon}, 1);
	md->_               = GC_ALLOC_INIT(material_data_runtime_t, {0});
	md->_->shader       = sd;
	md->_->uid          = 0;

	g_context->material = _material;
	return md;
}
