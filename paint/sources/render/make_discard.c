
#include "../global.h"

void make_discard_color_id(node_shader_t *kong, char *tex_coord) {
	node_shader_add_texture(kong, "texpaint_colorid", NULL);    // 1x1 picker
	node_shader_add_texture(kong, "texcolorid", "_texcolorid"); // color map
	// node_shader_write_frag(kong, "var colorid_c1: float3 = texpaint_colorid[uint2(0, 0)].rgb;");
	node_shader_write_frag(kong, "var colorid_c14: float4 = texpaint_colorid[uint2(uint(0), uint(0))];");
	node_shader_write_frag(kong, "var colorid_c1: float3 = colorid_c14.rgb;");
	node_shader_write_frag(kong, string("var colorid_c2: float3 = sample_lod(texcolorid, sampler_linear, input.%s, 0.0).rgb;", tex_coord));
	// node_shader_write_frag(kong, "if (any(colorid_c1 != colorid_c2)) { discard };");
	node_shader_write_frag(kong, "if (colorid_c1.r != colorid_c2.r || colorid_c1.g != colorid_c2.g || colorid_c1.b != colorid_c2.b) { discard; }");
}

void make_discard_face(node_shader_t *kong) {
	node_shader_add_texture(kong, "gbuffer2", NULL);
	node_shader_add_texture(kong, "textrianglemap", "_textrianglemap");
	node_shader_add_constant(kong, "textrianglemap_size: float2", "_texpaint_size");
	node_shader_add_constant(kong, "gbuffer_size: float2", "_gbuffer_size");
	// node_shader_write_frag(kong, "var tex_coord_inp: float2 = gbuffer2[uint2(constants.inp.x * constants.gbuffer_size.x, constants.inp.y *
	// constants.gbuffer_size.y)].ba;");
	node_shader_write_frag(
	    kong,
	    "var tex_coord_inp4: float4 = gbuffer2[uint2(uint(constants.inp.x * constants.gbuffer_size.x), uint(constants.inp.y * constants.gbuffer_size.y))];");
	node_shader_write_frag(kong, "var tex_coord_inp: float2 = tex_coord_inp4.ba;");
	node_shader_write_frag(kong, "var face_c1: float4 = textrianglemap[uint2(uint(tex_coord_inp.x * constants.textrianglemap_size.x), uint(tex_coord_inp.y * "
	                             "constants.textrianglemap_size.y))];");
	node_shader_write_frag(kong, "var face_c2: float4 = sample_lod(textrianglemap, sampler_linear, input.tex_coord_pick, 0.0);");
	node_shader_write_frag(kong, "var face_match: bool = face_c1.x == face_c2.x && face_c1.y == face_c2.y && face_c1.z == face_c2.z && face_c1.w == face_c2.w;");

	bool sym_x = g_context->sym_x && g_context->fill_sym_x_valid;
	bool sym_y = g_context->sym_y && g_context->fill_sym_y_valid;
	bool sym_z = g_context->sym_z && g_context->fill_sym_z_valid;

	if (sym_x) {
		node_shader_add_constant(kong, "fill_sym_x_uv: float2", "_fill_sym_x_uv");
		node_shader_write_frag(kong, "var face_symx: float4 = sample_lod(textrianglemap, sampler_linear, constants.fill_sym_x_uv, 0.0);");
		node_shader_write_frag(
		    kong, "if (face_symx.x == face_c2.x && face_symx.y == face_c2.y && face_symx.z == face_c2.z && face_symx.w == face_c2.w) { face_match = true; }");
	}
	if (sym_y) {
		node_shader_add_constant(kong, "fill_sym_y_uv: float2", "_fill_sym_y_uv");
		node_shader_write_frag(kong, "var face_symy: float4 = sample_lod(textrianglemap, sampler_linear, constants.fill_sym_y_uv, 0.0);");
		node_shader_write_frag(
		    kong, "if (face_symy.x == face_c2.x && face_symy.y == face_c2.y && face_symy.z == face_c2.z && face_symy.w == face_c2.w) { face_match = true; }");
	}
	if (sym_z) {
		node_shader_add_constant(kong, "fill_sym_z_uv: float2", "_fill_sym_z_uv");
		node_shader_write_frag(kong, "var face_symz: float4 = sample_lod(textrianglemap, sampler_linear, constants.fill_sym_z_uv, 0.0);");
		node_shader_write_frag(
		    kong, "if (face_symz.x == face_c2.x && face_symz.y == face_c2.y && face_symz.z == face_c2.z && face_symz.w == face_c2.w) { face_match = true; }");
	}

	node_shader_write_frag(kong, "if (!face_match) { discard; }");
}

void make_discard_uv_island(node_shader_t *kong) {
	node_shader_add_texture(kong, "texuvislandmap", "_texuvislandmap");
	node_shader_write_frag(kong, "if (sample_lod(texuvislandmap, sampler_linear, input.tex_coord_pick, 0.0).r == 0.0) { discard; }");
}

void make_discard_material_id(node_shader_t *kong, char *tex_coord) {
	char *tc;
	if (tex_coord == NULL) {
		kong->frag_wvpposition = true;
		node_shader_write_frag(
		    kong, "var picker_sample_tc: float2 = float2(input.wvpposition.x / input.wvpposition.w, input.wvpposition.y / input.wvpposition.w) * 0.5 + 0.5;");
		node_shader_write_frag(kong, "picker_sample_tc.y = 1.0 - picker_sample_tc.y;");
		tc = "picker_sample_tc";
	}
	else {
		tc = string("input.%s", tex_coord);
	}
	node_shader_add_texture(kong, "texpaint_nor_undo", "_texpaint_nor_undo");
	node_shader_write_frag(kong, string("var picker_sample_a: float = sample_lod(texpaint_nor_undo, sampler_linear, %s, 0.0).a;", tc));
	// material_id * 3 + (0 - normal, 1 - emission, 2 - subsurface)
	node_shader_write_frag(kong, "var picker_sample_id: float = floor(floor(picker_sample_a * 255.0 + 0.5) / 3.0);");
	node_shader_write_frag(kong, string("if (abs(picker_sample_id - %s) > 0.5) { discard; }", f32_to_string_with_zeros((f32)g_context->materialid_picked)));
}
