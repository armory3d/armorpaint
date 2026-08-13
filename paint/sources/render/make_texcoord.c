
#include "../global.h"

void make_texcoord_run(node_shader_t *kong, bool is_atlas) {

	bool      fill_layer = g_context->layer->fill_material != NULL;
	uv_type_t uv_type    = fill_layer ? g_context->layer->uv_type : g_context->brush_paint;
	bool      decal      = context_is_decal();
	f32       angle      = g_context->brush_angle + g_context->brush_nodes_angle;
	f32       uv_angle   = fill_layer ? g_context->layer->angle : angle;

	char *scale = "1.0";
	if (!is_atlas) {
		node_shader_add_constant(kong, "brush_scale: float", "_brush_scale");
		scale = "constants.brush_scale";
	}

	if (uv_type == UV_TYPE_PROJECT || decal) { // TexCoords - project
		node_shader_write_attrib_frag(kong, "var uvsp: float2 = sp.xy;");

		if (fill_layer) { // Decal layer
			node_shader_write_attrib_frag(kong, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");

			if (uv_angle != 0.0) {
				node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
				node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
				                                    "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
			}

			kong->frag_n = true;
			node_shader_add_constant(kong, "decal_layer_nor: float3", "_decal_layer_nor");
			f32 dot_angle = g_context->brush_angle_reject_dot;
			node_shader_write_frag(kong, string("if (abs(dot(n, constants.decal_layer_nor) - 1.0) > %s) { discard; }", f32_to_string(dot_angle)));

			kong->frag_wposition = true;
			node_shader_add_constant(kong, "decal_layer_loc: float3", "_decal_layer_loc");
			node_shader_add_constant(kong, "decal_layer_dim: float", "_decal_layer_dim");
			node_shader_write_attrib_frag(
			    kong, "if (abs(dot(constants.decal_layer_nor, constants.decal_layer_loc - input.wposition)) > constants.decal_layer_dim) { discard; }");
		}
		else if (decal) {
			kong->frag_wposition = true;
			node_shader_add_function(kong, str_octahedron_wrap);
			node_shader_add_texture(kong, "gbuffer0", NULL);
			node_shader_add_constant(kong, "decal_mask: float4", "_decal_mask");
			node_shader_add_constant(kong, "invVP: float4x4", "_inv_view_proj_matrix");
			node_shader_add_constant(kong, "VP: float4x4", "_view_proj_matrix");
			node_shader_add_constant(kong, "camera_right: float3", "_camera_right");
			node_shader_add_constant(kong, "camera_up: float3", "_camera_up");
			node_shader_add_constant(kong, "camera_align: float", "_brush_camera_align");

			node_shader_write_attrib_frag(kong, "if (constants.camera_align > 0.0) {");
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

			// When mask is active, anchor the decal at the frozen position; otherwise follow the mouse
			node_shader_write_attrib_frag(kong, "var decal_xy: float2 = constants.inp.xy;");
			node_shader_write_attrib_frag(kong, "if (constants.decal_mask.z > 0.0) { decal_xy = constants.decal_mask.xy; }");

			// Unproject the decal anchor point from the depth buffer
			node_shader_write_attrib_frag(kong, "var decal_depth: float = sample_lod(gbufferD, sampler_linear, decal_xy, 0.0).r;");
			node_shader_write_attrib_frag(kong, "var decal_wpos4: float4 = float4(float2(decal_xy.x, 1.0 - decal_xy.y) * 2.0 - 1.0, decal_depth, 1.0);");
			node_shader_write_attrib_frag(kong, "decal_wpos4 = constants.invVP * decal_wpos4;");
			node_shader_write_attrib_frag(kong, "var decal_wpos: float3 = decal_wpos4.xyz / decal_wpos4.w;");

			// Decode face normal at anchor point
			node_shader_write_attrib_frag(kong, "var dg0: float2 = sample_lod(gbuffer0, sampler_linear, decal_xy, 0.0).rg;");
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

			node_shader_write_attrib_frag(kong, "var d_offset: float3 = input.wposition - decal_wpos;");
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

			if (uv_angle != 0.0) {
				node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
				node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
				                                    "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
			}

			node_shader_add_constant(kong, "brush_scale_x: float", "_brush_scale_x");
			node_shader_write_attrib_frag(kong, "uvsp.x *= constants.brush_scale_x;");
			node_shader_write_attrib_frag(kong, "uvsp += float2(0.5, 0.5);");
			node_shader_write_attrib_frag(kong, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");
		}
		else {
			node_shader_write_attrib_frag(kong, "uvsp.x *= constants.aspect_ratio;");
			if (uv_angle != 0.0) {
				node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
				node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
				                                    "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
			}
		}

		node_shader_write_attrib_frag(kong, string("var tex_coord: float2 = uvsp * %s;", scale));
	}
	else if (uv_type == UV_TYPE_UVMAP) { // TexCoords - uvmap
		node_shader_add_out(kong, "tex_coord: float2");

		if (g_context->layer->uv_map == 1) {
			node_shader_write_vert(kong, string("output.tex_coord = input.tex1 * %s;", scale));
		}
		else {
			node_shader_write_vert(kong, string("output.tex_coord = input.tex * %s;", scale));
		}

		if (uv_angle > 0.0) {
			node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
			node_shader_write_vert(kong,
			                       "output.tex_coord = float2(output.tex_coord.x * constants.brush_angle.x - output.tex_coord.y * constants.brush_angle.y, "
			                       "output.tex_coord.x * constants.brush_angle.y + output.tex_coord.y * constants.brush_angle.x);");
		}
		node_shader_write_attrib_frag(kong, "var tex_coord: float2 = input.tex_coord;");
	}
	else { // UV_TYPE_TRIPLANAR, TexCoords - triplanar
		kong->frag_wposition = true;
		kong->frag_n         = true;
		node_shader_write_attrib_frag(kong, "var tri_weight: float3 = input.wnormal * input.wnormal;");
		node_shader_write_attrib_frag(kong, "var tri_max: float = max(tri_weight.x, max(tri_weight.y, tri_weight.z));");
		node_shader_write_attrib_frag(kong, "tri_weight = max3(tri_weight - float3(tri_max * 0.75, tri_max * 0.75, tri_max * 0.75), float3(0.0, 0.0, 0.0));");
		node_shader_write_attrib_frag(kong, "var tex_coord_blend: float3 = tri_weight * (1.0 / (tri_weight.x + tri_weight.y + tri_weight.z));");
		node_shader_write_attrib_frag(kong, string("var tex_coord: float2 = input.wposition.yz * %s * 0.5;", scale));
		node_shader_write_attrib_frag(kong, string("var tex_coord1: float2 = input.wposition.xz * %s * 0.5;", scale));
		node_shader_write_attrib_frag(kong, string("var tex_coord2: float2 = input.wposition.xy * %s * 0.5;", scale));
		if (uv_angle != 0.0) {
			node_shader_add_constant(kong, "brush_angle: float2", "_brush_angle");
			node_shader_write_attrib_frag(kong, "tex_coord = float2(tex_coord.x * constants.brush_angle.x - tex_coord.y * constants.brush_angle.y, tex_coord.x "
			                                    "* constants.brush_angle.y + tex_coord.y * constants.brush_angle.x);");
			node_shader_write_attrib_frag(kong, "tex_coord1 = float2(tex_coord1.x * constants.brush_angle.x - tex_coord1.y * constants.brush_angle.y, "
			                                    "tex_coord1.x * constants.brush_angle.y + tex_coord1.y * constants.brush_angle.x);");
			node_shader_write_attrib_frag(kong, "tex_coord2 = float2(tex_coord2.x * constants.brush_angle.x - tex_coord2.y * constants.brush_angle.y, "
			                                    "tex_coord2.x * constants.brush_angle.y + tex_coord2.y * constants.brush_angle.x);");
		}
	}
}
