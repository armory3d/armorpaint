
#include "../global.h"

void make_texcoord_run(node_shader_t *kong, bool is_atlas) {

	bool      fill_layer = g_context->layer->fill_material != NULL;
	uv_type_t uv_type    = fill_layer ? g_context->layer->uv_type : g_context->brush_paint;
	bool      decal      = context_is_decal();
	f32       angle      = g_context->brush_angle + g_context->brush_nodes_angle;
	f32       uv_angle   = fill_layer ? g_context->layer->angle : angle;

	char *scale = "1.0";
	if (!is_atlas) {
		node_shader_add_constant(kong, "float brush_scale", "_brush_scale");
		scale = "constants.brush_scale";
	}

	if (uv_type == UV_TYPE_PROJECT || decal) { // TexCoords - project
		node_shader_write_attrib_frag(kong, "float2 uvsp = sp.xy;");

		if (fill_layer) { // Decal layer
			node_shader_write_attrib_frag(kong, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");

			if (uv_angle != 0.0) {
				node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
				node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
				                                    "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
			}

			kong->frag_n = true;
			node_shader_add_constant(kong, "float3 decal_layer_nor", "_decal_layer_nor");
			f32 dot_angle = g_context->brush_angle_reject_dot;
			node_shader_write_frag(kong, string_tmp("if (abs(dot(n, constants.decal_layer_nor) - 1.0) > %s) { discard; }", f32_to_string(dot_angle)));

			kong->frag_wposition = true;
			node_shader_add_constant(kong, "float3 decal_layer_loc", "_decal_layer_loc");
			node_shader_add_constant(kong, "float decal_layer_dim", "_decal_layer_dim");
			node_shader_write_attrib_frag(
			    kong, "if (abs(dot(constants.decal_layer_nor, constants.decal_layer_loc - input.wposition)) > constants.decal_layer_dim) { discard; }");
		}
		else if (decal) {
			kong->frag_wposition = true;
			node_shader_add_function(kong, str_octahedron_wrap);
			node_shader_add_texture(kong, "gbuffer0", NULL);
			node_shader_add_constant(kong, "float4 decal_mask", "_decal_mask");
			node_shader_add_constant(kong, "float4x4 invVP", "_inv_view_proj_matrix");
			node_shader_add_constant(kong, "float4x4 VP", "_view_proj_matrix");
			node_shader_add_constant(kong, "float3 camera_right", "_camera_right");
			node_shader_add_constant(kong, "float3 camera_up", "_camera_up");
			node_shader_add_constant(kong, "float camera_align", "_brush_camera_align");

			node_shader_write_attrib_frag(kong, "if (constants.camera_align > 0.0) {");
			node_shader_write_attrib_frag(kong, "float ca_depth = sample_lod(gbufferD, sampler_linear, constants.decal_mask.xy, 0.0).r;");
			node_shader_write_attrib_frag(kong, "float2 ca_coord = float2(constants.decal_mask.x * 2.0 - 1.0, 1.0 - constants.decal_mask.y * 2.0);");
			node_shader_write_attrib_frag(kong, "float4 ca_homog = constants.invVP * float4(ca_coord.x, ca_coord.y, ca_depth, 1.0);");
			node_shader_write_attrib_frag(kong, "float ca_clip_w = 1.0 / ca_homog.w;");
			node_shader_write_attrib_frag(kong, "float vp_up_y = (constants.VP * float4(constants.camera_up, 0.0)).y;");
			node_shader_write_attrib_frag(kong, "uvsp = sp.xy - constants.decal_mask.xy;");
			node_shader_write_attrib_frag(kong, "uvsp.x *= constants.aspect_ratio;");
			node_shader_write_attrib_frag(kong, "uvsp = uvsp * (ca_clip_w / (vp_up_y * constants.brush_radius));");
			node_shader_write_attrib_frag(kong, "}");

			node_shader_write_attrib_frag(kong, "else {");

			// When mask is active, anchor the decal at the frozen position; otherwise follow the mouse
			node_shader_write_attrib_frag(kong, "float2 decal_xy = constants.inp.xy;");
			node_shader_write_attrib_frag(kong, "if (constants.decal_mask.z > 0.0) { decal_xy = constants.decal_mask.xy; }");

			// Unproject the decal anchor point from the depth buffer
			node_shader_write_attrib_frag(kong, "float decal_depth = sample_lod(gbufferD, sampler_linear, decal_xy, 0.0).r;");
			node_shader_write_attrib_frag(kong, "float4 decal_wpos4 = float4(float2(decal_xy.x, 1.0 - decal_xy.y) * 2.0 - 1.0, decal_depth, 1.0);");
			node_shader_write_attrib_frag(kong, "decal_wpos4 = constants.invVP * decal_wpos4;");
			node_shader_write_attrib_frag(kong, "float3 decal_wpos = decal_wpos4.xyz / decal_wpos4.w;");

			// Decode face normal at anchor point
			node_shader_write_attrib_frag(kong, "float2 dg0 = sample_lod(gbuffer0, sampler_linear, decal_xy, 0.0).rg;");
			node_shader_write_attrib_frag(kong, "float3 dn;");
			node_shader_write_attrib_frag(kong, "dn.z = 1.0 - abs(dg0.x) - abs(dg0.y);");
			node_shader_write_attrib_frag(
			    kong, "if (dn.z >= 0.0) { dn.x = dg0.x; dn.y = dg0.y; } else { float2 fw = octahedron_wrap(dg0.xy); dn.x = fw.x; dn.y = fw.y; }");
			node_shader_write_attrib_frag(kong, "dn = normalize(dn);");

			// Build tangent basis
			node_shader_write_attrib_frag(kong, "float3 d_right = constants.camera_right;");
			node_shader_write_attrib_frag(kong, "if (abs(dot(dn, d_right)) > 0.999) { d_right = float3(0.0, 0.0, 1.0); }");
			node_shader_write_attrib_frag(kong, "float3 d_tan = normalize(d_right - dn * dot(d_right, dn));");
			node_shader_write_attrib_frag(kong, "float3 d_bin = cross(d_tan, dn);");

			node_shader_write_attrib_frag(kong, "float3 d_offset = input.wposition - decal_wpos;");
			node_shader_write_attrib_frag(kong, "float decal_radius = constants.brush_radius;");
			node_shader_write_attrib_frag(kong, "if (constants.decal_mask.z > 0.0) { decal_radius = constants.decal_mask.w; }");
			node_shader_write_attrib_frag(kong, "if (abs(dot(d_offset, dn)) > decal_radius) { discard; }");
			node_shader_write_attrib_frag(kong, "uvsp = float2(dot(d_offset, d_tan), dot(d_offset, d_bin));");
			node_shader_write_attrib_frag(kong, "uvsp = uvsp / decal_radius * 0.5;");

			node_shader_write_attrib_frag(kong, "}");

			if (g_context->brush_directional) {
				node_shader_add_constant(kong, "float3 brush_direction", "_brush_direction");
				node_shader_write_attrib_frag(kong, "if (constants.brush_direction.z == 0.0) { discard; }");
				node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_direction.x - uvsp.y * constants.brush_direction.y, uvsp.x * "
				                                    "constants.brush_direction.y + uvsp.y * constants.brush_direction.x);");
			}

			if (uv_angle != 0.0) {
				node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
				node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
				                                    "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
			}

			node_shader_add_constant(kong, "float brush_scale_x", "_brush_scale_x");
			node_shader_write_attrib_frag(kong, "uvsp.x *= constants.brush_scale_x;");
			node_shader_write_attrib_frag(kong, "uvsp += float2(0.5, 0.5);");
			node_shader_write_attrib_frag(kong, "if (uvsp.x < 0.0 || uvsp.y < 0.0 || uvsp.x > 1.0 || uvsp.y > 1.0) { discard; }");
		}
		else {
			node_shader_write_attrib_frag(kong, "uvsp.x *= constants.aspect_ratio;");
			if (uv_angle != 0.0) {
				node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
				node_shader_write_attrib_frag(kong, "uvsp = float2(uvsp.x * constants.brush_angle.x - uvsp.y * constants.brush_angle.y, uvsp.x * "
				                                    "constants.brush_angle.y + uvsp.y * constants.brush_angle.x);");
			}
		}

		node_shader_write_attrib_frag(kong, string_tmp("float2 tex_coord = uvsp * %s;", scale));
	}
	else if (uv_type == UV_TYPE_UVMAP) { // TexCoords - uvmap
		node_shader_add_out(kong, "float2 tex_coord");

		if (g_context->layer->uv_map == 1) {
			node_shader_write_vert(kong, string_tmp("output.tex_coord = input.tex1 * %s;", scale));
		}
		else if (!is_atlas && util_mesh_udim_layer(g_context->layer)) {
			// Materials repeat per tile, bake reads layer textures in atlas space
			char *uv = "atlas_uv";
			if (g_context->tool != TOOL_TYPE_BAKE) {
				node_shader_add_constant(kong, "float atlas_stride", "_atlas_stride");
				uv = "(atlas_uv * constants.atlas_stride)";
			}
			node_shader_write_vert(kong, string_tmp("output.tex_coord = %s * %s;", uv, scale));
		}
		else {
			node_shader_write_vert(kong, string_tmp("output.tex_coord = input.tex * %s;", scale));
		}

		if (uv_angle > 0.0) {
			node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
			node_shader_write_vert(kong,
			                       "output.tex_coord = float2(output.tex_coord.x * constants.brush_angle.x - output.tex_coord.y * constants.brush_angle.y, "
			                       "output.tex_coord.x * constants.brush_angle.y + output.tex_coord.y * constants.brush_angle.x);");
		}
		node_shader_write_attrib_frag(kong, "float2 tex_coord = input.tex_coord;");
	}
	else { // UV_TYPE_TRIPLANAR, TexCoords - triplanar
		kong->frag_wposition = true;
		kong->frag_n         = true;
		node_shader_write_attrib_frag(kong, "float3 tri_weight = input.wnormal * input.wnormal;");
		node_shader_write_attrib_frag(kong, "float tri_max = max(tri_weight.x, max(tri_weight.y, tri_weight.z));");
		node_shader_write_attrib_frag(kong, "tri_weight = max(tri_weight - float3(tri_max * 0.75, tri_max * 0.75, tri_max * 0.75), float3(0.0, 0.0, 0.0));");
		node_shader_write_attrib_frag(kong, "float3 tex_coord_blend = tri_weight * (1.0 / (tri_weight.x + tri_weight.y + tri_weight.z));");
		node_shader_write_attrib_frag(kong, string_tmp("float2 tex_coord = input.wposition.yz * %s * 0.5;", scale));
		node_shader_write_attrib_frag(kong, string_tmp("float2 tex_coord1 = input.wposition.xz * %s * 0.5;", scale));
		node_shader_write_attrib_frag(kong, string_tmp("float2 tex_coord2 = input.wposition.xy * %s * 0.5;", scale));
		if (uv_angle != 0.0) {
			node_shader_add_constant(kong, "float2 brush_angle", "_brush_angle");
			node_shader_write_attrib_frag(kong, "tex_coord = float2(tex_coord.x * constants.brush_angle.x - tex_coord.y * constants.brush_angle.y, tex_coord.x "
			                                    "* constants.brush_angle.y + tex_coord.y * constants.brush_angle.x);");
			node_shader_write_attrib_frag(kong, "tex_coord1 = float2(tex_coord1.x * constants.brush_angle.x - tex_coord1.y * constants.brush_angle.y, "
			                                    "tex_coord1.x * constants.brush_angle.y + tex_coord1.y * constants.brush_angle.x);");
			node_shader_write_attrib_frag(kong, "tex_coord2 = float2(tex_coord2.x * constants.brush_angle.x - tex_coord2.y * constants.brush_angle.y, "
			                                    "tex_coord2.x * constants.brush_angle.y + tex_coord2.y * constants.brush_angle.x);");
		}
	}
}
