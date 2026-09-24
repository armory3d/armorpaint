
#include "../global.h"

void make_clone_run(node_shader_t *kong) {
	node_shader_add_constant(kong, "float2 clone_delta", "_clone_delta");
	// node_shader_write_frag(kong, "float2 tex_coord_inp = gbuffer2[uint2((sp.xy + constants.clone_delta) * constants.gbuffer_size)].ba;");
	node_shader_write_frag(kong, "uint2 tex_coord_coord = uint2(uint((sp.x + constants.clone_delta.x) * constants.gbuffer_size.x), uint((sp.y + "
	                             "constants.clone_delta.y) * constants.gbuffer_size.y));");
	node_shader_write_frag(kong, "float4 tex_coord_inp4 = gbuffer2[tex_coord_coord];");
	node_shader_write_frag(kong, "float2 tex_coord_inp = tex_coord_inp4.ba;");

	node_shader_write_frag(kong, "float4 texpaint_undo_sample = sample_lod(texpaint_undo, sampler_linear, tex_coord_inp, 0.0);");
	node_shader_write_frag(kong, "float4 texpaint_pack_undo_sample = sample_lod(texpaint_pack_undo, sampler_linear, tex_coord_inp, 0.0);");
	node_shader_write_frag(kong, "float4 texpaint_nor_undo_sample = sample_lod(texpaint_nor_undo, sampler_linear, tex_coord_inp, 0.0);");
	node_shader_write_frag(kong, "float clone_src_alpha = texpaint_undo_sample.a;");
	node_shader_write_frag(kong, "float3 basecol = texpaint_undo_sample.rgb;");
	node_shader_write_frag(kong, "float roughness = texpaint_pack_undo_sample.g;");
	node_shader_write_frag(kong, "float metallic = texpaint_pack_undo_sample.b;");
	node_shader_write_frag(kong, "float occlusion = texpaint_pack_undo_sample.r;");
	node_shader_write_frag(kong, "float3 nortan = texpaint_nor_undo_sample.rgb;");
	node_shader_write_frag(kong, "float height = texpaint_pack_undo_sample.a;");
	node_shader_write_frag(kong, "float mat_opacity = texpaint_undo_sample.a;");
	node_shader_write_frag(kong, "float opacity = mat_opacity * constants.brush_opacity;");
	if (g_context->material->paint_emis || g_context->material->paint_subs) {
		node_shader_write_frag(kong, "float clone_matid_mod = float(int(texpaint_nor_undo_sample.a * 255.0)) % float(3);");
	}
	if (g_context->material->paint_emis) {
		node_shader_write_frag(kong, "float emis = 0.0; if (clone_matid_mod == 1.0) { emis = 1.0; }");
	}
	if (g_context->material->paint_subs) {
		node_shader_write_frag(kong, "float subs = 0.0; if (clone_matid_mod == 2.0) { subs = 1.0; }");
	}
}
