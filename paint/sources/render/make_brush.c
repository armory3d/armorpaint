
#include "../global.h"

void make_brush_run(node_shader_t *kong) {

	node_shader_write_frag(kong, "float dist = 0.0;");

	if (g_context->tool == TOOL_TYPE_PARTICLE) {
		return;
	}

	bool fill_layer = g_context->layer->fill_material != NULL;
	bool decal      = context_is_decal();
	if (decal && !fill_layer) {
		node_shader_write_frag(kong, "if (constants.decal_mask.z > 0.0) {");
	}

	node_shader_write_frag(kong, "float depth = sample_lod(gbufferD, sampler_linear, constants.inp.xy, 0.0).r;");

	node_shader_add_constant(kong, "float4x4 invVP", "_inv_view_proj_matrix");
	node_shader_write_frag(kong, "float4 winp = float4(float2(constants.inp.x, 1.0 - constants.inp.y) * 2.0 - 1.0, depth, 1.0);");
	node_shader_write_frag(kong, "winp = constants.invVP * winp;");
	node_shader_write_frag(kong, "winp.xyz = winp.xyz / winp.w;");
	kong->frag_wposition = true;

	if (g_config->brush_angle_reject || g_context->xray) {
		node_shader_add_function(kong, str_octahedron_wrap);
		node_shader_add_texture(kong, "gbuffer0", NULL);
		node_shader_write_frag(kong, "float2 g0 = sample_lod(gbuffer0, sampler_linear, constants.inp.xy, 0.0).rg;");
		node_shader_write_frag(kong, "float3 wn;");
		node_shader_write_frag(kong, "wn.z = 1.0 - abs(g0.x) - abs(g0.y);");
		// node_shader_write_frag(kong, "wn.xy = wn.z >= 0.0 ? g0.xy : octahedron_wrap(g0.xy);");
		node_shader_write_frag(kong,
		                       "if (wn.z >= 0.0) { wn.x = g0.x; wn.y = g0.y; } else { float2 f2 = octahedron_wrap(g0.xy); wn.x = f2.x; wn.y = f2.y; }");
		node_shader_write_frag(kong, "wn = normalize(wn);");
		node_shader_write_frag(kong, "float plane_dist = dot(wn, winp.xyz - input.wposition);");

		if (g_config->brush_angle_reject && !g_context->xray && !make_material_transluc_used) {
			// constants.inp.w = paint2d ? 0.0 : 1.0
			node_shader_write_frag(kong, "if (plane_dist < -0.03 && constants.inp.w == 0.0) { discard; }");
			kong->frag_n = true;
			f32 angle    = g_context->brush_angle_reject_dot;
			node_shader_write_frag(kong, string_tmp("if (dot(wn, n) < %s && constants.inp.w == 0.0) { discard; }", f32_to_string(angle)));
		}
	}

	node_shader_write_frag(kong, "float depthlast = sample_lod(gbufferD, sampler_linear, constants.inplast.xy, 0.0).r;");

	node_shader_write_frag(kong, "float4 winplast = float4(float2(constants.inplast.x, 1.0 - constants.inplast.y) * 2.0 - 1.0, depthlast, 1.0);");
	node_shader_write_frag(kong, "winplast = constants.invVP * winplast;");
	node_shader_write_frag(kong, "winplast.xyz = winplast.xyz / winplast.w;");

	node_shader_write_frag(kong, "float3 pa = input.wposition - winp.xyz;");
	if (g_context->xray) {
		node_shader_write_frag(kong, "pa += wn * float3(plane_dist, plane_dist, plane_dist);");
	}
	node_shader_write_frag(kong, "float3 ba = winplast.xyz - winp.xyz;");

	node_shader_add_constant(kong, "float4x4 VP", "_view_proj_matrix");
	node_shader_add_constant(kong, "float3 camera_up", "_camera_up");
	node_shader_add_constant(kong, "float aspect_ratio", "_aspect_ratio_window");
	node_shader_add_constant(kong, "float camera_align", "_brush_camera_align");

	node_shader_write_frag(kong, "if (constants.camera_align > 0.0) {");
	node_shader_write_frag(kong, "float vp_up_y = (constants.VP * float4(constants.camera_up, 0.0)).y;");
	node_shader_write_frag(kong, "float ca_scale = (1.0 / winp.w) / (vp_up_y * constants.brush_radius);");
	node_shader_write_frag(kong, "float2 sa = sp.xy - constants.inp.xy;");
	node_shader_write_frag(kong, "sa.x *= constants.aspect_ratio;");
	node_shader_write_frag(kong, "sa = sa * ca_scale;");
	// Capsule
	node_shader_write_frag(kong, "float2 sb = constants.inplast.xy - constants.inp.xy;");
	node_shader_write_frag(kong, "sb.x *= constants.aspect_ratio;");
	node_shader_write_frag(kong, "sb = sb * ca_scale;");
	node_shader_write_frag(kong, "float sh = clamp(dot(sa, sb) / dot(sb, sb), 0.0, 1.0);");
	node_shader_write_frag(kong, "dist = length(sa - sb * sh) * 2.0 * constants.brush_radius;");
	node_shader_write_frag(kong, "}");

	node_shader_write_frag(kong, "else {");
	// Capsule
	node_shader_write_frag(kong, "float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);");
	node_shader_write_frag(kong, "dist = length(pa - ba * h);");
	node_shader_write_frag(kong, "}");

	node_shader_write_frag(kong, "if (dist > constants.brush_radius) { discard; }");

	if (decal && !fill_layer) {
		node_shader_write_frag(kong, "}");
	}
}
