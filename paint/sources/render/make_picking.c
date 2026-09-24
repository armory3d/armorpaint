
#include "../global.h"

char *str_get_pos_nor_from_depth = "\
float3 get_pos_from_depth(float2 uv, float4x4 invVP) { \
	float depth = sample_lod(gbufferD, sampler_linear, float2(uv.x, 1.0 - uv.y), 0.0).r; \
	float4 wpos = float4(uv * 2.0 - 1.0, depth, 1.0); \
	wpos = invVP * wpos; \
	return wpos.xyz / wpos.w; \
} \
float3 get_nor_from_depth(float3 p0, float2 uv, float4x4 invVP, float2 tex_step) { \
	float3 p1 = get_pos_from_depth(uv + float2(tex_step.x * 4.0, 0.0), invVP); \
	float3 p2 = get_pos_from_depth(uv + float2(0.0, tex_step.y * 4.0), invVP); \
	return normalize(cross(p2 - p0, p1 - p0)); \
} \
";

void make_picking_run(node_shader_t *kong) {
	// Mangle vertices to form full screen triangle
	node_shader_write_vert(kong, "output.pos = float4(-1.0 + float((vertex_id() & 1) << uint(2)), -1.0 + float((vertex_id() & 2) << uint(1)), 0.0, 1.0);");

	node_shader_add_texture(kong, "gbuffer2", NULL);
	node_shader_add_constant(kong, "float2 gbuffer_size", "_gbuffer_size");
	node_shader_add_constant(kong, "float4 inp", "_input_brush");

	// node_shader_write_frag(kong, "float2 tex_coord_inp = gbuffer2[uint2(constants.inp.x * constants.gbuffer_size.x, constants.inp.y *
	// constants.gbuffer_size.y)].ba;");
	node_shader_write_frag(
	    kong,
	    "float4 tex_coord_inp4 = gbuffer2[uint2(uint(constants.inp.x * constants.gbuffer_size.x), uint(constants.inp.y * constants.gbuffer_size.y))];");
	node_shader_write_frag(kong, "float2 tex_coord_inp = tex_coord_inp4.ba;");

	if (g_context->tool == TOOL_TYPE_COLORID) {
		kong->frag_out = "float4";
		node_shader_add_texture(kong, "texcolorid", "_texcolorid");
		node_shader_write_frag(kong, "float3 idcol = sample_lod(texcolorid, sampler_linear, tex_coord_inp, 0.0).rgb;");
		node_shader_write_frag(kong, "output = float4(idcol, 1.0);");
	}
	else if (g_context->tool == TOOL_TYPE_PICKER || g_context->tool == TOOL_TYPE_MATERIAL) {
		if (g_context->pick_pos_nor_tex) {
			kong->frag_out = "float4[2]";
			node_shader_add_texture(kong, "gbufferD", NULL);
			node_shader_add_constant(kong, "float4x4 invVP", "_inv_view_proj_matrix");
			node_shader_add_function(kong, str_get_pos_nor_from_depth);
			node_shader_write_frag(kong,
			                       "float3 out_pos_from_depth = get_pos_from_depth(float2(constants.inp.x, 1.0 - constants.inp.y), constants.invVP);");
			node_shader_write_frag(kong, "float3 out_nor_from_depth = get_nor_from_depth(out_pos_from_depth, float2(constants.inp.x, 1.0 - "
			                             "constants.inp.y), constants.invVP, float2(1.0, 1.0) / constants.gbuffer_size);");
			node_shader_write_frag(kong, "output[0] = float4(out_pos_from_depth, tex_coord_inp.x);");
			node_shader_write_frag(kong, "output[1] = float4(out_nor_from_depth, tex_coord_inp.y);");
		}
		else if (slot_layer_is_mask(g_context->layer)) {
			kong->frag_out = "float4[2]";
			node_shader_add_texture(kong, "texpaint", NULL);
			node_shader_write_frag(kong, "float texpaint_val = sample_lod(texpaint, sampler_linear, tex_coord_inp, 0.0).r;");
			node_shader_write_frag(kong, "output[0] = float4(texpaint_val, texpaint_val, texpaint_val, 1.0);");
			node_shader_write_frag(kong, "output[1].rg = tex_coord_inp.xy;");
		}
		else {
			kong->frag_out = "float4[4]";
			node_shader_add_texture(kong, "texpaint", NULL);
			node_shader_add_texture(kong, "texpaint_nor", NULL);
			node_shader_add_texture(kong, "texpaint_pack", NULL);
			node_shader_write_frag(kong, "output[0] = sample_lod(texpaint, sampler_linear, tex_coord_inp, 0.0);");
			node_shader_write_frag(kong, "output[1] = sample_lod(texpaint_nor, sampler_linear, tex_coord_inp, 0.0);");
			node_shader_write_frag(kong, "output[2] = sample_lod(texpaint_pack, sampler_linear, tex_coord_inp, 0.0);");
			node_shader_write_frag(kong, "output[3].rg = tex_coord_inp.xy;");
		}
	}
	else if (g_context->tool == TOOL_TYPE_CURSOR) {
		kong->frag_out = "float4";
		node_shader_add_texture(kong, "gbuffer1", NULL);
		node_shader_add_constant(kong, "float2 gbuffer_size", "_gbuffer_size");
		node_shader_add_constant(kong, "float4 inp", "_input_brush");
		node_shader_write_frag(
		    kong, "uint2 inp_co = uint2(uint(constants.inp.x * constants.gbuffer_size.x), uint(constants.inp.y * constants.gbuffer_size.y));");
		node_shader_write_frag(kong, "output = gbuffer1[inp_co];");
	}
}
