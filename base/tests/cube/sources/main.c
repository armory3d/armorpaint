// ../../make --run

#include <iron.h>

void render_commands() {
	render_path_set_target("", NULL, NULL, GPU_CLEAR_COLOR | GPU_CLEAR_DEPTH, 0xff6495ed, 1.0);
	render_path_draw_meshes("mesh");
}

void spin_cube(void *_) {
	object_t *cube = scene_get_child("Cube");
	transform_rotate(cube->transform, (vec4_t){0, 0, 1, 1.0}, 0.01);
}

void scene_ready() {
	// Set camera
	transform_t *t = scene_camera->base->transform;
	t->loc         = (vec4_t){0, -6, 0, 1.0};
	t->rot         = quat_from_to((vec4_t){0, 0, 1, 1.0}, (vec4_t){0, -1, 0, 1.0});
	transform_build_matrix(t);

	// Rotate cube
	sys_notify_on_update(spin_cube, NULL);
}

void ready() {
	render_path_commands = render_commands;

	scene_t *scene = ALLOC_INIT(
	    scene_t,
	    {.name    = "Scene",
	     .objects = any_array_create_from_raw(
	         (void *[]){
	             ALLOC_INIT(
	                 obj_t, {.name = "Cube", .type = "mesh_object", .data_ref = "cube.arm/Cube", .material_ref = "MyMaterial", .visible = true, .spawn = true}),
	             ALLOC_INIT(obj_t, {.name = "Camera", .type = "camera_object", .data_ref = "MyCamera", .visible = true, .spawn = true}),
	         },
	         2),
	     .camera_datas = any_array_create_from_raw(
	         (void *[]){
	             ALLOC_INIT(camera_data_t, {.name = "MyCamera", .near_plane = 0.1, .far_plane = 100.0, .fov = 0.85}),
	         },
	         1),
	     .camera_ref     = "Camera",
	     .world_ref      = "MyWorld",
	     .world_datas    = any_array_create_from_raw((void *[]){ALLOC_INIT(world_data_t, {.name = "MyWorld", .color = 0xff000000})}, 1),
	     .material_datas = any_array_create_from_raw(
	         (void *[]){
	             ALLOC_INIT(material_data_t,
	                           {.name     = "MyMaterial",
	                            .shader   = "MyShader",
	                            .contexts = any_array_create_from_raw(
	                                (void *[]){
	                                    ALLOC_INIT(material_context_t, {.name          = "mesh",
	                                                                       .bind_textures = any_array_create_from_raw(
	                                                                           (void *[]){
	                                                                               ALLOC_INIT(bind_tex_t, {.name = "my_texture", .file = "texture.k"}),
	                                                                           },
	                                                                           1)}),
	                                },
	                                1)}),
	         },
	         1),
	     .shader_datas = any_array_create_from_raw(
	         (void *[]){
	             ALLOC_INIT(shader_data_t, {.name     = "MyShader",
	                                           .contexts = any_array_create_from_raw(
	                                               (void *[]){
	                                                   ALLOC_INIT(shader_context_t,
	                                                                 {.name            = "mesh",
	                                                                  .vertex_shader   = "mesh.vert",
	                                                                  .fragment_shader = "mesh.frag",
	                                                                  .compare_mode    = "less",
	                                                                  .cull_mode       = "clockwise",
	                                                                  .depth_write     = true,
	                                                                  .vertex_elements = any_array_create_from_raw(
	                                                                      (void *[]){
	                                                                          ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "short4norm"}),
	                                                                          ALLOC_INIT(vertex_element_t, {.name = "tex", .data = "short2norm"}),
	                                                                      },
	                                                                      2),
	                                                                  .constants = any_array_create_from_raw(
	                                                                      (void *[]){
	                                                                          ALLOC_INIT(shader_const_t,
	                                                                                        {.name = "WVP", .type = "float4x4", .link = "_world_view_proj_matrix"}),
	                                                                      },
	                                                                      1),
	                                                                  .texture_units = any_array_create_from_raw(
	                                                                      (void *[]){
	                                                                          ALLOC_INIT(tex_unit_t, {.name = "my_texture"}),
	                                                                      },
	                                                                      1),
	                                                                  .depth_attachment = "D32"}),
	                                               },
	                                               1)}),
	         },
	         1)});

	data_cached_scene_raws = any_map_create();
	any_map_set(data_cached_scene_raws, scene->name, scene);

	// Instantiate scene
	scene_create(scene);
	scene_ready();
}

void _kickstart() {
	iron_window_options_t *ops =
	    ALLOC_INIT(iron_window_options_t, {.title     = "Empty",
	                                          .width     = 1280,
	                                          .height    = 720,
	                                          .x         = -1,
	                                          .y         = -1,
	                                          .features  = IRON_WINDOW_FEATURES_RESIZABLE | IRON_WINDOW_FEATURES_MINIMIZABLE | IRON_WINDOW_FEATURES_MAXIMIZABLE,
	                                          .mode      = IRON_WINDOW_MODE_WINDOW,
	                                          .frequency = 60,
	                                          .vsync     = true,
	                                          .depth_bits = 32});
	sys_start(ops);
	ready();
	iron_start();
}

////

any_map_t *ui_children;
any_map_t *ui_nodes_custom_buttons;
i32        pipes_offset;
char      *strings_check_internet_connection() {
    return "";
}
void  console_error(char *s) {}
void  console_info(char *s) {}
char *tr(char *id) {
	return id;
}
i32 pipes_get_constant_location(char *s) {
	return 0;
}
