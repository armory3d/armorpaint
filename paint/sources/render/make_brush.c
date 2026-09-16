
#include "../global.h"

void make_brush_run(node_shader_t *kong) {

	node_shader_write_frag(kong, "var dist: float = 0.0;");

	if (g_context->tool == TOOL_TYPE_PARTICLE) {
		return;
	}

	bool fill_layer = g_context->layer->fill_material != NULL;
	bool decal      = context_is_decal();
	if (decal && !fill_layer) {
		node_shader_write_frag(kong, "if (constants.decal_mask.z > 0.0) {");
	}

	node_shader_write_frag(kong, "var depth: float = sample_lod(gbufferD, sampler_linear, constants.inp.xy, 0.0).r;");

	node_shader_add_constant(kong, "invVP: float4x4", "_inv_view_proj_matrix");
	node_shader_write_frag(kong, "var winp: float4 = float4(float2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0, depth, 1.0);");
	node_shader_write_frag(kong, "winp = constants.invVP * winp;");
	node_shader_write_frag(kong, "winp.xyz = winp.xyz / winp.w;");
	kong->frag_wposition = true;

	if (g_config->brush_angle_reject || g_context->xray) {
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_texture(kong, "gbuffer0", NULL);
		node_shader_write_frag(kong, "var g0: float2 = sample_lod(gbuffer0, sampler_linear, constants.inp.xy, 0.0).rg;");
		node_shader_write_frag(kong, "var wn: float3;");
		node_shader_write_frag(kong, "wn.z = 1.0 - abs(g0.x) - abs(g0.y);");
		// node_shader_write_frag(kong, "wn.xy = wn.z >= 0.0 ? g0.xy : octahedron_wrap(g0.xy);");
		node_shader_write_frag(kong,
		                       "if (wn.z >= 0.0) { wn.x = g0.x; wn.y = g0.y; } else { var f2: float2 = octahedron_wrap(g0.xy); wn.x = f2.x; wn.y = f2.y; }");
		node_shader_write_frag(kong, "wn = normalize(wn);");
		node_shader_write_frag(kong, "var plane_dist: float = dot(wn, winp.xyz - input.wposition);");

		if (g_config->brush_angle_reject && !g_context->xray && !make_material_transluc_used) {
			// constants.inp.w = paint2d ? 0.0 : 1.0
			node_shader_write_frag(kong, "if (plane_dist < -0.03 && constants.inp.w == 0.0) { discard; }");
			kong->frag_n = true;
			f32 angle    = g_context->brush_angle_reject_dot;
			node_shader_write_frag(kong, string_tmp("if (dot(wn, n) < %s && constants.inp.w == 0.0) { discard; }", f32_to_string(angle)));
		}
	}

	node_shader_write_frag(kong, "var depthlast: float = sample_lod(gbufferD, sampler_linear, constants.inplast.xy, 0.0).r;");

	node_shader_write_frag(kong, "var winplast: float4 = float4(float2(constants.inplast.x, 1.0 - constants.inplast.y) * 2.0 - 1.0, depthlast, 1.0);");
	node_shader_write_frag(kong, "winplast = constants.invVP * winplast;");
	node_shader_write_frag(kong, "winplast.xyz = winplast.xyz / winplast.w;");

	node_shader_write_frag(kong, "var pa: float3 = input.wposition - winp.xyz;");
	if (g_context->xray) {
		node_shader_write_frag(kong, "pa += wn * float3(plane_dist, plane_dist, plane_dist);");
	}
	node_shader_write_frag(kong, "var ba: float3 = winplast.xyz - winp.xyz;");

	node_shader_add_constant(kong, "VP: float4x4", "_view_proj_matrix");
	node_shader_add_constant(kong, "camera_up: float3", "_camera_up");
	node_shader_add_constant(kong, "aspect_ratio: float", "_aspect_ratio_window");
	node_shader_add_constant(kong, "camera_align: float", "_brush_camera_align");

	node_shader_write_frag(kong, "if (constants.camera_align > 0.0) {");
	node_shader_write_frag(kong, "var vp_up_y: float = (constants.VP * float4(constants.camera_up, 0.0)).y;");
	node_shader_write_frag(kong, "var ca_scale: float = (1.0 / winp.w) / (vp_up_y * constants.brush_radius);");
	node_shader_write_frag(kong, "var sa: float2 = sp.xy - constants.inp.xy;");
	node_shader_write_frag(kong, "sa.x *= constants.aspect_ratio;");
	node_shader_write_frag(kong, "sa = sa * ca_scale;");
	// Capsule
	node_shader_write_frag(kong, "var sb: float2 = constants.inplast.xy - constants.inp.xy;");
	node_shader_write_frag(kong, "sb.x *= constants.aspect_ratio;");
	node_shader_write_frag(kong, "sb = sb * ca_scale;");
	node_shader_write_frag(kong, "var sh: float = clamp(dot(sa, sb) / dot(sb, sb), 0.0, 1.0);");
	node_shader_write_frag(kong, "dist = length(sa - sb * sh) * 2.0 * constants.brush_radius;");
	node_shader_write_frag(kong, "}");

	node_shader_write_frag(kong, "else {");
	// Capsule
	node_shader_write_frag(kong, "var h: float = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);");
	node_shader_write_frag(kong, "dist = length(pa - ba * h);");
	node_shader_write_frag(kong, "}");

	node_shader_write_frag(kong, "if (dist > constants.brush_radius) { discard; }");

	if (decal && !fill_layer) {
		node_shader_write_frag(kong, "}");
	}
}
