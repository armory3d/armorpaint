
#include "../global.h"

extern char *str_sh_irradiance;
extern char *str_envmap_equirect;
extern char *str_envmap_sample;
extern char *str_env_brdf_approx;

static mesh_object_t *render_envsphere_metallic = NULL;
static mesh_object_t *render_envsphere_diffuse  = NULL;

static node_shader_context_t *make_envsphere_shader(char *name, f32 roughness, f32 metallic) {
	material_t            *mat   = ALLOC_INIT(material_t, {.name = name, .canvas = NULL});
	shader_context_t      *props = ALLOC_INIT(shader_context_t, {
	                                                                .name            = "overlay",
	                                                                .depth_write     = false,
	                                                                .compare_mode    = "always",
	                                                                .cull_mode       = "clockwise",
	                                                                .vertex_elements = any_array_create_from_raw(
                                                                   (void *[]){
                                                                       ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
                                                                       ALLOC_INIT(vertex_element_t, {.name = "nor", .data = "short2norm"}),
                                                                       ALLOC_INIT(vertex_element_t, {.name = "tex", .data = "short2norm"}),
                                                                   },
                                                                   3),
	                                                                .color_attachments = any_array_create_from_raw((void *[]){"RGBA64"}, 1),
	                                                                .depth_attachment  = "NONE",
                                                           });
	node_shader_context_t *con   = node_shader_context_create(mat, props);
	node_shader_t         *kong  = node_shader_context_make_kong(con);

	kong->frag_n = true;

	node_shader_add_constant(kong, "WVP: float4x4", "_world_view_proj_matrix");
	node_shader_write_vert(kong, "output.pos = constants.WVP * float4(input.pos.xyz, 1.0);");

	node_shader_add_constant(kong, "cam_look: float3", "_camera_look");
	node_shader_write_attrib_frag(kong, "var vvec: float3 = -constants.cam_look;");

	node_shader_add_texture(kong, "senvmap_radiance", "_envmap_radiance");
	node_shader_add_texture(kong, "senvmap_radiance0", "_envmap_radiance0");
	node_shader_add_texture(kong, "senvmap_radiance1", "_envmap_radiance1");
	node_shader_add_texture(kong, "senvmap_radiance2", "_envmap_radiance2");
	node_shader_add_texture(kong, "senvmap_radiance3", "_envmap_radiance3");
	node_shader_add_texture(kong, "senvmap_radiance4", "_envmap_radiance4");

	node_shader_add_constant(kong, "envmap_data: float4", "_envmap_data"); // (angle, sin, cos, strength)
	node_shader_add_constant(kong, "shirr0: float4", "_envmap_irradiance0");
	node_shader_add_constant(kong, "shirr1: float4", "_envmap_irradiance1");
	node_shader_add_constant(kong, "shirr2: float4", "_envmap_irradiance2");
	node_shader_add_constant(kong, "shirr3: float4", "_envmap_irradiance3");
	node_shader_add_constant(kong, "shirr4: float4", "_envmap_irradiance4");
	node_shader_add_constant(kong, "shirr5: float4", "_envmap_irradiance5");
	node_shader_add_constant(kong, "shirr6: float4", "_envmap_irradiance6");

	node_shader_add_function(kong, str_sh_irradiance);
	node_shader_add_function(kong, str_envmap_equirect);
	node_shader_add_function(kong, str_envmap_sample);
	node_shader_add_function(kong, str_env_brdf_approx);

	node_shader_write_frag(kong, "var basecol: float3 = float3(0.8, 0.8, 0.8);");
	node_shader_write_frag(kong, string_tmp("var roughness: float = %s;", f32_to_string_with_zeros(roughness)));
	node_shader_write_frag(kong, string_tmp("var metallic: float = %s;", f32_to_string_with_zeros(metallic)));

	// Indirect lighting
	node_shader_write_frag(kong, "var albedo: float3 = lerp3(basecol, float3(0.0, 0.0, 0.0), metallic);");
	node_shader_write_frag(kong, "var f0: float3 = lerp3(float3(0.04, 0.04, 0.04), basecol, metallic);");
	node_shader_write_frag(kong, "var dotnv: float = max(0.0, dot(n, vvec));");
	node_shader_write_frag(kong, "var wreflect: float3 = reflect(-vvec, n);");
	node_shader_write_frag(kong, "var envlod: float = roughness * 5.0;");
	node_shader_write_frag(kong, "var lod0: float = floor(envlod);");
	node_shader_write_frag(kong, "var lod1: float = ceil(envlod);");
	node_shader_write_frag(kong, "var lodf: float = envlod - lod0;");
	node_shader_write_frag(kong, "var envmap_coord: float2 = envmap_equirect(wreflect, constants.envmap_data.x);");
	node_shader_write_frag(kong, "var lodc0: float3 = envmap_sample(lod0, envmap_coord);");
	node_shader_write_frag(kong, "var lodc1: float3 = envmap_sample(lod1, envmap_coord);");
	node_shader_write_frag(kong, "var prefiltered_color: float3 = lerp3(lodc0, lodc1, lodf);");
	// Rotate normal by envmap angle for irradiance
	node_shader_write_frag(kong, "var indirect: float3 = albedo * (sh_irradiance(float3(n.x * constants.envmap_data.z - n.y * constants.envmap_data.y, n.x * "
	                             "constants.envmap_data.y + n.y * constants.envmap_data.z, n.z)) / 3.14159265);");
	node_shader_write_frag(kong, "indirect = indirect + prefiltered_color * env_brdf_approx(f0, roughness, dotnv) * 0.5;");
	node_shader_write_frag(kong, "indirect = indirect * constants.envmap_data.w;");
	node_shader_write_frag(kong, "indirect = max3(indirect, float3(0.0, 0.0, 0.0));");

	// Filmic tone-mapping (matches compositor_pass)
	node_shader_write_frag(kong, "var tc: float3 = max3(indirect - float3(0.004, 0.004, 0.004), float3(0.0, 0.0, 0.0));");
	node_shader_write_frag(kong,
	                       "indirect = (tc * (tc * 6.2 + float3(0.5, 0.5, 0.5))) / (tc * (tc * 6.2 + float3(1.7, 1.7, 1.7)) + float3(0.06, 0.06, 0.06));");
	node_shader_write_frag(kong, "output = float4(indirect, 1.0);");

	parser_material_finalize(con);
	con->data->shader_from_source = true;
	gpu_create_shaders_from_kong(node_shader_get(kong), &con->data->vertex_shader, &con->data->fragment_shader, &con->data->_->vertex_shader_size,
	                             &con->data->_->fragment_shader_size);
	return con;
}

static mesh_object_t *create_envsphere_object(char *name, f32 roughness, f32 metallic) {
	mesh_data_t *md = ((mesh_object_t *)scene_get_child(".Sphere")->ext)->data;

	node_shader_context_t *con = make_envsphere_shader(name, roughness, metallic);
	shader_context_load(con->data);

	shader_data_t      *sd   = ALLOC_INIT(shader_data_t, {
	                                                         .name     = name,
	                                                         .contexts = any_array_create_from_raw((void *[]){con->data}, 1),
                                                  });
	material_context_t *mcon = ALLOC_INIT(material_context_t, {
	                                                              .name           = "overlay",
	                                                              .bind_constants = any_array_create_from_raw((void *[]){}, 0),
	                                                              .bind_textures  = any_array_create_from_raw((void *[]){}, 0),
	                                                          });
	material_context_load(mcon);
	material_data_t *mat = ALLOC_INIT(material_data_t, {
	                                                       .name     = name,
	                                                       .shader   = "",
	                                                       .contexts = any_array_create_from_raw((void *[]){mcon}, 1),
	                                                       ._        = ALLOC_INIT(material_data_runtime_t, {.uid = 0.0, .shader = sd}),
	                                                   });

	mesh_object_t *obj   = mesh_object_create(md, mat);
	obj->base->name      = name;
	obj->base->visible   = false;
	obj->frustum_culling = false;
	return obj;
}

static void render_envsphere_init() {
	render_envsphere_metallic = create_envsphere_object("render_envsphere_metallic", 0.0, 1.0);
	render_envsphere_diffuse  = create_envsphere_object("render_envsphere_diffuse", 0.9, 0.0);
}

void render_envsphere() {
	if (!g_context->show_envmap_spheres || g_config->workspace == WORKSPACE_PLAYER || g_context->capturing_screenshot) {
		return;
	}
	if (render_envsphere_metallic == NULL) {
		render_envsphere_init();
	}

	camera_object_t *cam   = scene_camera;
	f32              ratio = sys_w() / (float)sys_h();
	mat4_t           _P    = cam->p;
	cam->p                 = mat4_ortho(-8 * ratio, 8 * ratio, -8, 8, -2, 2);

	// Bottom-right corner
	f32 sphere_y       = -7.0;
	f32 sphere_x_right = 7.4 * ratio;
	f32 sphere_x_left  = (7.4 - 1.1) * ratio;
	f32 sphere_scale   = 1.6;

	render_envsphere_metallic->base->visible          = true;
	render_envsphere_metallic->base->parent           = cam->base;
	render_envsphere_metallic->base->transform->loc   = (vec4_t){sphere_x_right, sphere_y, -1, 1.0};
	render_envsphere_metallic->base->transform->scale = (vec4_t){sphere_scale, sphere_scale, sphere_scale, 1.0};
	transform_build_matrix(render_envsphere_metallic->base->transform);
	mesh_object_render(render_envsphere_metallic, "overlay", NULL);

	render_envsphere_diffuse->base->visible          = true;
	render_envsphere_diffuse->base->parent           = cam->base;
	render_envsphere_diffuse->base->transform->loc   = (vec4_t){sphere_x_left, sphere_y, -1, 1.0};
	render_envsphere_diffuse->base->transform->scale = (vec4_t){sphere_scale, sphere_scale, sphere_scale, 1.0};
	transform_build_matrix(render_envsphere_diffuse->base->transform);
	mesh_object_render(render_envsphere_diffuse, "overlay", NULL);

	cam->p = _P;

	render_envsphere_metallic->base->visible = false;
	render_envsphere_metallic->base->parent  = NULL;
	render_envsphere_diffuse->base->visible  = false;
	render_envsphere_diffuse->base->parent   = NULL;
}
