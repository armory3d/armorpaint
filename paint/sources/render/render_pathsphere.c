
#include "../global.h"

extern char *str_sh_irradiance;
extern char *str_envmap_equirect;
extern char *str_envmap_sample;
extern char *str_env_brdf_approx;

static mesh_object_t *render_pathsphere_obj = NULL;

static node_shader_context_t *make_pathsphere_shader(char *name) {
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

	node_shader_add_constant(kong, "float4x4 WVP", "_world_view_proj_matrix");
	node_shader_write_vert(kong, "output.pos = constants.WVP * float4(input.pos.xyz, 1.0);");

	node_shader_add_constant(kong, "float3 cam_look", "_camera_look");
	node_shader_write_attrib_frag(kong, "float3 vvec = -constants.cam_look;");

	node_shader_add_texture(kong, "senvmap_radiance", "_envmap_radiance");
	node_shader_add_texture(kong, "senvmap_radiance0", "_envmap_radiance0");
	node_shader_add_texture(kong, "senvmap_radiance1", "_envmap_radiance1");
	node_shader_add_texture(kong, "senvmap_radiance2", "_envmap_radiance2");
	node_shader_add_texture(kong, "senvmap_radiance3", "_envmap_radiance3");
	node_shader_add_texture(kong, "senvmap_radiance4", "_envmap_radiance4");

	node_shader_add_constant(kong, "float4 envmap_data", "_envmap_data"); // (angle, sin, cos, strength)
	node_shader_add_constant(kong, "float4 shirr0", "_envmap_irradiance0");
	node_shader_add_constant(kong, "float4 shirr1", "_envmap_irradiance1");
	node_shader_add_constant(kong, "float4 shirr2", "_envmap_irradiance2");
	node_shader_add_constant(kong, "float4 shirr3", "_envmap_irradiance3");
	node_shader_add_constant(kong, "float4 shirr4", "_envmap_irradiance4");
	node_shader_add_constant(kong, "float4 shirr5", "_envmap_irradiance5");
	node_shader_add_constant(kong, "float4 shirr6", "_envmap_irradiance6");

	node_shader_add_function(kong, str_sh_irradiance);
	node_shader_add_function(kong, str_envmap_equirect);
	node_shader_add_function(kong, str_envmap_sample);
	node_shader_add_function(kong, str_env_brdf_approx);

	node_shader_write_frag(kong, "float3 basecol = float3(0.4, 0.0, 0.0);");
	node_shader_write_frag(kong, "float roughness = 0.5;");
	node_shader_write_frag(kong, "float metallic = 0.0;");

	// Indirect lighting
	node_shader_write_frag(kong, "float3 albedo = lerp3(basecol, float3(0.0, 0.0, 0.0), metallic);");
	node_shader_write_frag(kong, "float3 f0 = lerp3(float3(0.04, 0.04, 0.04), basecol, metallic);");
	node_shader_write_frag(kong, "float dotnv = max(0.0, dot(n, vvec));");
	node_shader_write_frag(kong, "float3 wreflect = reflect(-vvec, n);");
	node_shader_write_frag(kong, "float envlod = roughness * 5.0;");
	node_shader_write_frag(kong, "float lod0 = floor(envlod);");
	node_shader_write_frag(kong, "float lod1 = ceil(envlod);");
	node_shader_write_frag(kong, "float lodf = envlod - lod0;");
	node_shader_write_frag(kong, "float2 envmap_coord = envmap_equirect(wreflect, constants.envmap_data.x);");
	node_shader_write_frag(kong, "float3 lodc0 = envmap_sample(lod0, envmap_coord);");
	node_shader_write_frag(kong, "float3 lodc1 = envmap_sample(lod1, envmap_coord);");
	node_shader_write_frag(kong, "float3 prefiltered_color = lerp3(lodc0, lodc1, lodf);");
	// Rotate normal by envmap angle for irradiance
	node_shader_write_frag(kong, "float3 indirect = albedo * (sh_irradiance(float3(n.x * constants.envmap_data.z + n.y * constants.envmap_data.y, n.y * "
	                             "constants.envmap_data.z - n.x * constants.envmap_data.y, n.z)) / 3.14159265);");
	node_shader_write_frag(kong, "indirect = indirect + prefiltered_color * env_brdf_approx(f0, roughness, dotnv);");
	node_shader_write_frag(kong, "indirect = indirect * constants.envmap_data.w;");
	node_shader_write_frag(kong, "indirect = max3(indirect, float3(0.0, 0.0, 0.0));");

	// Filmic tone-mapping (matches compositor_pass)
	node_shader_write_frag(kong, "float3 tc = max3(indirect - float3(0.004, 0.004, 0.004), float3(0.0, 0.0, 0.0));");
	node_shader_write_frag(kong,
	                       "indirect = (tc * (tc * 6.2 + float3(0.5, 0.5, 0.5))) / (tc * (tc * 6.2 + float3(1.7, 1.7, 1.7)) + float3(0.06, 0.06, 0.06));");
	node_shader_write_frag(kong, "output = float4(indirect, 1.0);");

	parser_material_finalize(con);
	con->data->shader_from_source = true;
	gpu_create_shaders_from_kong(node_shader_get(kong), &con->data->vertex_shader, &con->data->fragment_shader, &con->data->_->vertex_shader_size,
	                             &con->data->_->fragment_shader_size);
	return con;
}

static void create_pathsphere_object() {
	mesh_data_t *md = ((mesh_object_t *)scene_get_child(".Sphere")->ext)->data;

	node_shader_context_t *con = make_pathsphere_shader("render_pathsphere");
	shader_context_load(con->data);

	shader_data_t *mat = ALLOC_INIT(shader_data_t, {
	                                                   .name     = "render_pathsphere",
	                                                   .contexts = any_array_create_from_raw((void *[]){con->data}, 1),
	                                                   ._        = ALLOC_INIT(shader_data_runtime_t, {.uid = 0.0}),
	                                               });

	render_pathsphere_obj                  = mesh_object_create(md, mat);
	render_pathsphere_obj->base->name      = "render_pathsphere";
	render_pathsphere_obj->base->visible   = false;
	render_pathsphere_obj->frustum_culling = false;
}

void render_pathsphere() {
	if (g_config->workspace == WORKSPACE_PLAYER || g_context->capturing_screenshot || g_context->paint2d) {
		return;
	}

	slot_layer_t *l = g_context->layer;
	if (l == NULL || !slot_layer_is_path(l)) {
		return;
	}

	f32_array_t *points_world = l->path_points_world;
	if (points_world == NULL || points_world->length == 0) {
		return;
	}

	if (path_point_dragging >= 0) {
		return;
	}

	if (render_pathsphere_obj == NULL) {
		create_pathsphere_object();
	}

	i32 num_points   = points_world->length / 3;
	i32 active_index = path_layer_last_active;

	render_pathsphere_obj->base->visible = true;

	for (i32 i = 0; i < num_points; i++) {
		f32 wx    = points_world->buffer[i * 3];
		f32 wy    = points_world->buffer[i * 3 + 1];
		f32 wz    = points_world->buffer[i * 3 + 2];
		f32 dist  = vec4_dist(scene_camera->base->transform->loc, (vec4_t){wx, wy, wz, 1.0});
		f32 fov   = scene_camera->data->fov;
		f32 scale = dist / 8.0f * fov * 0.1f * (i == active_index ? 1.5f : 1.0f);

		render_pathsphere_obj->base->transform->loc   = (vec4_t){wx, wy, wz, 1.0};
		render_pathsphere_obj->base->transform->scale = (vec4_t){scale, scale, scale, 1.0};
		transform_build_matrix(render_pathsphere_obj->base->transform);
		mesh_object_render(render_pathsphere_obj, "overlay", NULL);
	}

	render_pathsphere_obj->base->visible = false;
}
