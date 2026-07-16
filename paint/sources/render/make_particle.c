
#include "../global.h"

static material_data_t *particle_bullet_material = NULL;

static node_shader_context_t *make_particle_bullet_run() {
	material_t            *mat   = GC_ALLOC_INIT(material_t, {.name = "particle_bullet", .canvas = NULL});
	shader_context_t      *props = GC_ALLOC_INIT(shader_context_t, {
	                                                                   .name            = "mesh",
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
	                                                                   .color_attachments = any_array_create_from_raw((void *[]){"RGBA64", "RGBA64", "RGBA64"}, 3),
	                                                                   .depth_attachment = "D32",
                                                              });
	node_shader_context_t *con   = node_shader_context_create(mat, props);
	node_shader_t         *kong  = node_shader_context_make_kong(con);

	kong->frag_n   = true;
	kong->frag_out = "float4[3]";

	node_shader_add_constant(kong, "WVP: float4x4", "_world_view_proj_matrix");
	node_shader_write_vert(kong, "output.pos = constants.WVP * float4(input.pos.xyz, 1.0);");

	node_shader_add_out(kong, "tex_coord: float2");
	node_shader_write_vert(kong, "output.tex_coord = input.tex;");

	node_shader_add_function(kong, str_octahedron_wrap);
	node_shader_add_function(kong, str_pack_float_int16);

	node_shader_write_frag(kong, "var basecol: float3 = float3(0.8, 0.0, 0.0);");
	node_shader_write_frag(kong, "var roughness: float = 0.5;");
	node_shader_write_frag(kong, "var metallic: float = 0.0;");
	node_shader_write_frag(kong, "var occlusion: float = 1.0;");
	node_shader_write_frag(kong, "n = n / (abs(n.x) + abs(n.y) + abs(n.z));");
	node_shader_write_frag(kong, "if (n.z < 0.0) { n.xy = octahedron_wrap(n.xy); }");
	node_shader_write_frag(kong, "output[0] = float4(n.xy, roughness, pack_f32_i16(metallic, uint(0)));");
	node_shader_write_frag(kong, "output[1] = float4(basecol, occlusion);");
	node_shader_write_frag(kong, "output[2] = float4(0.0, 0.0, input.tex_coord.xy);");

	parser_material_finalize(con);
	con->data->shader_from_source = true;
	gpu_create_shaders_from_kong(node_shader_get(kong), &con->data->vertex_shader, &con->data->fragment_shader, &con->data->_->vertex_shader_size,
	                             &con->data->_->fragment_shader_size);
	return con;
}

material_data_t *make_particle_get_bullet_material() {
	if (particle_bullet_material != NULL) {
		return particle_bullet_material;
	}

	node_shader_context_t *con = make_particle_bullet_run();
	shader_context_load(con->data);

	shader_data_t      *sd   = GC_ALLOC_INIT(shader_data_t, {
	                                                            .name     = "particle_bullet",
	                                                            .contexts = any_array_create_from_raw((void *[]){con->data}, 1),
                                                     });
	material_context_t *mcon = GC_ALLOC_INIT(material_context_t, {
	                                                                 .name           = "mesh",
	                                                                 .bind_constants = any_array_create_from_raw((void *[]){}, 0),
	                                                                 .bind_textures  = any_array_create_from_raw((void *[]){}, 0),
	                                                             });
	material_context_load(mcon);
	particle_bullet_material = GC_ALLOC_INIT(material_data_t, {
	                                                              .name     = "particle_bullet",
	                                                              .shader   = "",
	                                                              .contexts = any_array_create_from_raw((void *[]){mcon}, 1),
	                                                              ._        = GC_ALLOC_INIT(material_data_runtime_t, {.uid = 0.0, .shader = sd}),
	                                                          });
	gc_root(particle_bullet_material);
	return particle_bullet_material;
}

void make_particle_mask(node_shader_t *kong) {
	node_shader_add_out(kong, "wpos: float4");
	node_shader_add_constant(kong, "W: float4x4", "_world_matrix");
	node_shader_write_attrib_vert(kong, "output.wpos = constants.W * float4(input.pos.xyz, 1.0);");
	node_shader_add_constant(kong, "particle_hit: float3", "_particle_hit");
	node_shader_add_constant(kong, "particle_hit_last: float3", "_particle_hit_last");
	node_shader_add_constant(kong, "particle_radius: float", "_particle_radius");

	node_shader_write_frag(kong, "var pa: float3 = input.wpos.xyz - constants.particle_hit;");
	node_shader_write_frag(kong, "var ba: float3 = constants.particle_hit_last - constants.particle_hit;");
	node_shader_write_frag(kong, "var h: float = clamp(dot(pa, ba) / max(dot(ba, ba), 0.00000001), 0.0, 1.0);");
	node_shader_write_frag(kong, "dist = length(pa - ba * h) * (5.0 / constants.particle_radius);");
	node_shader_write_frag(kong, "if (dist > 1.0) { discard; }");
	node_shader_write_frag(kong, "var str: float = clamp(pow(1.0 / dist * constants.brush_hardness * 0.2, 4.0), 0.0, 1.0) * opacity;");
	node_shader_write_frag(kong, "if (str == 0.0) { discard; }");
}
