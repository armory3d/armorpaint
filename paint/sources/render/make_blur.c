
#include "../global.h"

char *str_get_smudge_tool_weight = "\
fun get_smudge_tool_weight(i: int): float { \
	if (i == 0) { return 1.0 / 28.0; } \
	if (i == 1) { return 2.0 / 28.0; } \
	if (i == 2) { return 3.0 / 28.0; } \
	if (i == 3) { return 4.0 / 28.0; } \
	if (i == 4) { return 5.0 / 28.0; } \
	if (i == 5) { return 6.0 / 28.0; } \
	return 7.0 / 28.0; \
} \
";

char *str_get_blur_tool_weight = "\
fun get_blur_tool_weight(i: int): float { \
	if (i == 0) { return 0.034619 / 2.0; } \
	if (i == 1) { return 0.044859 / 2.0; } \
	if (i == 2) { return 0.055857 / 2.0; } \
	if (i == 3) { return 0.066833 / 2.0; } \
	if (i == 4) { return 0.076841 / 2.0; } \
	if (i == 5) { return 0.084894 / 2.0; } \
	if (i == 6) { return 0.090126 / 2.0; } \
	if (i == 7) { return 0.09194 / 2.0; } \
	if (i == 8) { return 0.090126 / 2.0; } \
	if (i == 9) { return 0.084894 / 2.0; } \
	if (i == 10) { return 0.076841 / 2.0; } \
	if (i == 11) { return 0.066833 / 2.0; } \
	if (i == 12) { return 0.055857 / 2.0; } \
	if (i == 13) { return 0.044859 / 2.0; } \
	return 0.034619 / 2.0; \
} \
";

void make_blur_run(node_shader_t *kong) {
	node_shader_write_frag(kong, "var basecol: float3 = float3(0.0, 0.0, 0.0);");
	node_shader_write_frag(kong, "var roughness: float = 0.0;");
	node_shader_write_frag(kong, "var metallic: float = 0.0;");
	node_shader_write_frag(kong, "var occlusion: float = 0.0;");
	node_shader_write_frag(kong, "var nortan: float3 = float3(0.0, 0.0, 0.0);");
	node_shader_write_frag(kong, "var height: float = 0.0;");
	node_shader_write_frag(kong, "var mat_opacity: float = 1.0;");
	bool is_mask = slot_layer_is_mask(g_context->layer);
	if (is_mask) {
		node_shader_write_frag(kong, "var opacity: float = 1.0;");
	}
	else {
		node_shader_write_frag(kong, "var opacity: float = 0.0;");
	}
	if (g_context->material->paint_emis) {
		node_shader_write_frag(kong, "var emis: float = 0.0;");
	}
	if (g_context->material->paint_subs) {
		node_shader_write_frag(kong, "var subs: float = 0.0;");
	}

	if (g_context->blur_type == BLUR_TYPE_SMUDGE) {
		node_shader_add_function(kong, str_get_smudge_tool_weight);
		node_shader_write_frag(kong, "var smudge_delta: float2 = constants.inp.xy - constants.inplast.xy;");
		node_shader_write_frag(kong, "for (var i: int = 0; i < 7; i += 1) {");
		node_shader_write_frag(kong, "var smudge_sp: float2 = sp.xy - smudge_delta * ((float(i) + 1.0) / 7.0);");
		node_shader_write_frag(kong, "smudge_sp = float2(clamp(smudge_sp.x, 0.0, 0.9999), clamp(smudge_sp.y, 0.0, 0.9999));");
		node_shader_write_frag(kong, "var tex_coord_inp24: float4 = gbuffer2[uint2(uint(smudge_sp.x * constants.gbuffer_size.x), uint(smudge_sp.y * "
		                             "constants.gbuffer_size.y))];");
		node_shader_write_frag(kong, "var tex_coord_inp2: float2 = tex_coord_inp24.ba;");
		node_shader_write_frag(kong, "var texpaint_sample: float4 = sample(texpaint_ref, sampler_linear, tex_coord_inp2);");
		node_shader_write_frag(kong, "var blur_weight_i: float = get_smudge_tool_weight(i);");
		node_shader_write_frag(kong, "opacity += texpaint_sample.a * blur_weight_i;");
		node_shader_write_frag(kong, "basecol += texpaint_sample.rgb * texpaint_sample.a * blur_weight_i;");
		node_shader_write_frag(kong, "var texpaint_pack_sample: float4 = sample(texpaint_pack_ref, sampler_linear, tex_coord_inp2) * blur_weight_i;");
		node_shader_write_frag(kong, "roughness += texpaint_pack_sample.g;");
		node_shader_write_frag(kong, "metallic += texpaint_pack_sample.b;");
		node_shader_write_frag(kong, "occlusion += texpaint_pack_sample.r;");
		node_shader_write_frag(kong, "height += texpaint_pack_sample.a;");
		node_shader_write_frag(kong, "nortan += sample(texpaint_nor_ref, sampler_linear, tex_coord_inp2).rgb * blur_weight_i;");
		node_shader_write_frag(kong, "}");
	}
	else {
		node_shader_write_frag(kong,
		                       "var tex_coord_inp4: float4 = gbuffer2[uint2(uint(sp.x * constants.gbuffer_size.x), uint(sp.y * constants.gbuffer_size.y))];");
		node_shader_write_frag(kong, "var tex_coord_inp: float2 = tex_coord_inp4.ba;");
		node_shader_add_constant(kong, "texpaint_size: float2", "_texpaint_size");
		node_shader_write_frag(kong, "var blur_step: float = 1.0 / constants.texpaint_size.x;");
		node_shader_add_function(kong, str_get_blur_tool_weight);
		// X
		node_shader_write_frag(kong, "for (var i: int = 0; i <= 14; i += 1) {");
		node_shader_write_frag(kong,
		                       "var texpaint_sample: float4 = sample(texpaint_ref, sampler_linear, tex_coord_inp + float2(blur_step * float(i - 7), 0.0));");
		node_shader_write_frag(kong, "var blur_weight_i: float = get_blur_tool_weight(i);");
		node_shader_write_frag(kong, "opacity += texpaint_sample.a * blur_weight_i;");
		node_shader_write_frag(kong, "basecol += texpaint_sample.rgb * texpaint_sample.a * blur_weight_i;");
		node_shader_write_frag(kong, "var texpaint_pack_sample: float4 = sample(texpaint_pack_ref, sampler_linear, tex_coord_inp + float2(blur_step * float(i "
		                             "- 7), 0.0)) * blur_weight_i;");
		node_shader_write_frag(kong, "roughness += texpaint_pack_sample.g;");
		node_shader_write_frag(kong, "metallic += texpaint_pack_sample.b;");
		node_shader_write_frag(kong, "occlusion += texpaint_pack_sample.r;");
		node_shader_write_frag(kong, "height += texpaint_pack_sample.a;");
		node_shader_write_frag(
		    kong, "nortan += sample(texpaint_nor_ref, sampler_linear, tex_coord_inp + float2(blur_step * float(i - 7), 0.0)).rgb * blur_weight_i;");
		node_shader_write_frag(kong, "}");
		// Y
		node_shader_write_frag(kong, "for (var j: int = 0; j <= 14; j += 1) {");
		node_shader_write_frag(kong,
		                       "var texpaint_sample: float4 = sample(texpaint_ref, sampler_linear, tex_coord_inp + float2(0.0, blur_step * float(j - 7)));");
		node_shader_write_frag(kong, "var blur_weight_j: float = get_blur_tool_weight(j);");
		node_shader_write_frag(kong, "opacity += texpaint_sample.a * blur_weight_j;");
		node_shader_write_frag(kong, "basecol += texpaint_sample.rgb * texpaint_sample.a * blur_weight_j;");
		node_shader_write_frag(kong, "var texpaint_pack_sample: float4 = sample(texpaint_pack_ref, sampler_linear, tex_coord_inp + float2(0.0, blur_step * "
		                             "float(j - 7))) * blur_weight_j;");
		node_shader_write_frag(kong, "roughness += texpaint_pack_sample.g;");
		node_shader_write_frag(kong, "metallic += texpaint_pack_sample.b;");
		node_shader_write_frag(kong, "occlusion += texpaint_pack_sample.r;");
		node_shader_write_frag(kong, "height += texpaint_pack_sample.a;");
		node_shader_write_frag(
		    kong, "nortan += sample(texpaint_nor_ref, sampler_linear, tex_coord_inp + float2(0.0, blur_step * float(j - 7))).rgb * blur_weight_j;");
		node_shader_write_frag(kong, "}");
	}
	node_shader_write_frag(kong, "var blur_src_alpha: float = opacity;");
	node_shader_write_frag(kong, "opacity *= constants.brush_opacity;");
}
