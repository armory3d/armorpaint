
#include "../global.h"

void make_clone_run(node_shader_t *kong, char *tex_src, char *tex_nor_src, char *tex_pack_src) {
	node_shader_add_constant(kong, "float2 clone_delta", "_clone_delta");
	// Screen pixels are coarser than texels, extend the source texcoord across the pixel with its screen derivative
	// Pick the one-sided difference with the smaller step so uv seams and background do not leak in
	node_shader_write_frag(kong, "float2 clone_px = (sp.xy + constants.clone_delta) * constants.gbuffer_size;");
	node_shader_write_frag(kong, "float2 clone_max = constants.gbuffer_size - float2(1.0, 1.0);");
	node_shader_write_frag(kong, "float2 clone_c = clamp(floor(clone_px), float2(0.0, 0.0), clone_max);");
	node_shader_write_frag(kong, "float2 clone_f = clone_px - clone_c - float2(0.5, 0.5);");
	node_shader_write_frag(kong, "float2 clone_l = max(clone_c - float2(1.0, 1.0), float2(0.0, 0.0));");
	node_shader_write_frag(kong, "float2 clone_h = min(clone_c + float2(1.0, 1.0), clone_max);");
	node_shader_write_frag(kong, "float4 clone_g0 = gbuffer2[uint2(uint(clone_c.x), uint(clone_c.y))];");
	node_shader_write_frag(kong, "float4 clone_gxl = gbuffer2[uint2(uint(clone_l.x), uint(clone_c.y))];");
	node_shader_write_frag(kong, "float4 clone_gxh = gbuffer2[uint2(uint(clone_h.x), uint(clone_c.y))];");
	node_shader_write_frag(kong, "float4 clone_gyl = gbuffer2[uint2(uint(clone_c.x), uint(clone_l.y))];");
	node_shader_write_frag(kong, "float4 clone_gyh = gbuffer2[uint2(uint(clone_c.x), uint(clone_h.y))];");
	node_shader_write_frag(kong, "float2 tex_coord_inp = clone_g0.ba + clone_g0.rg;");
	node_shader_write_frag(kong, "float2 clone_dxl = tex_coord_inp - (clone_gxl.ba + clone_gxl.rg);");
	node_shader_write_frag(kong, "float2 clone_dxh = (clone_gxh.ba + clone_gxh.rg) - tex_coord_inp;");
	node_shader_write_frag(kong, "float2 clone_dyl = tex_coord_inp - (clone_gyl.ba + clone_gyl.rg);");
	node_shader_write_frag(kong, "float2 clone_dyh = (clone_gyh.ba + clone_gyh.rg) - tex_coord_inp;");
	node_shader_write_frag(kong, "float2 clone_dx = clone_dxh;");
	node_shader_write_frag(kong, "if (length(clone_dxl) < length(clone_dxh)) { clone_dx = clone_dxl; }");
	node_shader_write_frag(kong, "float2 clone_dy = clone_dyh;");
	node_shader_write_frag(kong, "if (length(clone_dyl) < length(clone_dyh)) { clone_dy = clone_dyl; }");
	node_shader_write_frag(kong, "tex_coord_inp += clone_dx * clone_f.x + clone_dy * clone_f.y;");

	node_shader_write_frag(kong, string_tmp("float4 texpaint_undo_sample = sample_lod(%s, sampler_linear, tex_coord_inp, 0.0);", tex_src));
	node_shader_write_frag(kong, string_tmp("float4 texpaint_pack_undo_sample = sample_lod(%s, sampler_linear, tex_coord_inp, 0.0);", tex_pack_src));
	node_shader_write_frag(kong, string_tmp("float4 texpaint_nor_undo_sample = sample_lod(%s, sampler_linear, tex_coord_inp, 0.0);", tex_nor_src));
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
