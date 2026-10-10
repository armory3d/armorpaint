
#include "../global.h"

gpu_buffer_t *util_render_screen_aligned_full_vb = NULL;
gpu_buffer_t *util_render_screen_aligned_full_ib = NULL;

void util_render_make_material_preview() {
	g_context->material_preview = true;

	mesh_object_t *sphere         = scene_get_child(".Sphere")->ext;
	sphere->base->visible         = true;
	mesh_object_t_array_t *meshes = scene_meshes;
	scene_meshes                  = any_array_create_from_raw(
        (void *[]){
            sphere,
        },
        1);
	mesh_object_t *painto   = g_context->paint_object;
	g_context->paint_object = sphere;

	sphere->material                   = g_project->_->materials->buffer[0]->data;
	g_context->material->preview_ready = true;

	g_context->saved_camera = scene_camera->base->transform->local;
	mat4_t m =
	    (mat4_t){0.9146286343879498, 0.404295023959927,   0.000007410128652369705, 0, -0.0032648027153306235, 0.007367569133732468, 0.9999675337275382,   0,
	             0.404281837254303,  -0.9145989516155143, 0.008058532943908717,    0, 0.4659988049397712,     -1.0687517188018691,  0.015935682577325486, 1};

	transform_set_matrix(scene_camera->base->transform, m);
	f32 saved_fov           = scene_camera->data->fov;
	scene_camera->data->fov = 0.92;
	viewport_update_camera_type(CAMERA_TYPE_PERSPECTIVE);

	world_data_t *probe           = scene_world;
	f32           _probe_strength = probe->strength;
	probe->strength               = 2;
	f32 _envmap_angle             = g_context->envmap_angle;
	g_context->envmap_angle       = 0.0;
	f32 _brush_scale              = g_context->brush_scale;
	g_context->brush_scale        = 1.5;
	f32 _brush_nodes_scale        = g_context->brush_nodes_scale;
	g_context->brush_nodes_scale  = 1.0;

	gpu_texture_t *_envmap = scene_world->_->envmap;
	scene_world->_->envmap = g_context->preview_envmap;
	// No resize
	_render_path_last_w = util_render_material_preview_size;
	_render_path_last_h = util_render_material_preview_size;
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);

	make_material_parse_mesh_preview_material();
	void (*_commands)(void) = render_path_commands;
	render_path_commands    = render_path_preview_commands_preview;
	render_path_render_frame();
	render_path_commands = _commands;

	g_context->material_preview = false;
	_render_path_last_w         = sys_w();
	_render_path_last_h         = sys_h();

	// Restore
	sphere->base->visible = false;
	array_free(scene_meshes);
	free(scene_meshes);
	scene_meshes            = meshes;
	g_context->paint_object = painto;

	transform_set_matrix(scene_camera->base->transform, g_context->saved_camera);
	viewport_update_camera_type(g_context->camera_type);
	scene_camera->data->fov = saved_fov;
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);

	probe->strength              = _probe_strength;
	g_context->envmap_angle      = _envmap_angle;
	g_context->brush_scale       = _brush_scale;
	g_context->brush_nodes_scale = _brush_nodes_scale;
	scene_world->_->envmap       = _envmap;

	make_material_parse_mesh_material();
	g_context->ddirty = 0;
}

void util_render_make_decal_preview() {
	if (g_context->decal_image == NULL) {
		g_context->decal_image = gpu_create_render_target(util_render_decal_preview_size, util_render_decal_preview_size, GPU_TEXTURE_FORMAT_RGBA64);
	}
	g_context->decal_preview = true;

	mesh_object_t *plane          = scene_get_child(".Plane")->ext;
	plane->base->transform->scale = (vec4_t){1, 1, 1, 1.0};
	plane->base->transform->rot   = quat_from_euler(-math_pi() / 2.0, 0, 0);
	transform_build_matrix(plane->base->transform);
	plane->base->visible          = true;
	mesh_object_t_array_t *meshes = scene_meshes;
	scene_meshes                  = any_array_create_from_raw(
        (void *[]){
            plane,
        },
        1);
	mesh_object_t *painto   = g_context->paint_object;
	g_context->paint_object = plane;

	g_context->saved_camera = scene_camera->base->transform->local;
	mat4_t m                = mat4_identity();
	m                       = mat4_translate(m, 0, 0, 1);
	transform_set_matrix(scene_camera->base->transform, m);
	f32 saved_fov           = scene_camera->data->fov;
	scene_camera->data->fov = 0.92;
	viewport_update_camera_type(CAMERA_TYPE_PERSPECTIVE);
	gpu_texture_t *_envmap = scene_world->_->envmap;
	scene_world->_->envmap = g_context->preview_envmap;

	// No resize
	_render_path_last_w = util_render_decal_preview_size;
	_render_path_last_h = util_render_decal_preview_size;
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);

	make_material_parse_mesh_preview_material();
	void (*_commands)(void) = render_path_commands;
	render_path_commands    = render_path_preview_commands_decal;
	render_path_render_frame();
	render_path_commands = _commands;

	g_context->decal_preview = false;
	_render_path_last_w      = sys_w();
	_render_path_last_h      = sys_h();

	// Restore
	plane->base->visible = false;
	array_free(scene_meshes);
	free(scene_meshes);
	scene_meshes            = meshes;
	g_context->paint_object = painto;

	transform_set_matrix(scene_camera->base->transform, g_context->saved_camera);
	scene_camera->data->fov = saved_fov;
	viewport_update_camera_type(g_context->camera_type);
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);

	scene_world->_->envmap = _envmap;

	make_material_parse_mesh_material();
	g_context->ddirty = 1; // Refresh depth for decal paint
}

void util_render_make_text_preview() {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	char        *text      = g_context->text_tool_text;
	draw_font_t *font      = g_context->font->font;
	i32          font_size = util_render_font_preview_size;
	i32          text_w    = math_floor(draw_string_width(font, font_size, text));
	i32          text_h    = math_floor(draw_font_height(font, font_size));
	i32          tex_w     = text_w + 32;
	if (tex_w < 512) {
		tex_w = 512;
	}
	if (g_context->text_tool_image != NULL && g_context->text_tool_image->width < tex_w) {
		gpu_delete_texture(g_context->text_tool_image);
		g_context->text_tool_image = NULL;
	}
	if (g_context->text_tool_image == NULL) {
		g_context->text_tool_image = gpu_create_render_target(tex_w, tex_w, GPU_TEXTURE_FORMAT_RGBA32);
	}
	draw_begin(g_context->text_tool_image, true, 0x00000000);
	draw_set_font(font, font_size);
	draw_set_color(0xffffffff);
	draw_string(text, tex_w / 2.0 - text_w / 2.0, tex_w / 2.0 - text_h / 2.0);
	draw_end();

	if (in_use)
		draw_begin(current, false, 0);
}

void util_render_make_font_preview() {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	char        *text      = "Abg";
	draw_font_t *font      = g_context->font->font;
	i32          font_size = util_render_font_preview_size;
	i32          text_w    = math_floor(draw_string_width(font, font_size, text)) + 8;
	i32          text_h    = math_floor(draw_font_height(font, font_size)) + 8;
	i32          tex_w     = text_w + 32;
	if (g_context->font->image == NULL) {
		g_context->font->image = gpu_create_render_target(tex_w, tex_w, GPU_TEXTURE_FORMAT_RGBA32);
	}
	draw_begin(g_context->font->image, true, 0x00000000);
	draw_set_font(font, font_size);
	draw_set_color(0xffffffff);
	draw_string(text, tex_w / 2.0 - text_w / 2.0, tex_w / 2.0 - text_h / 2.0);
	draw_end();
	g_context->font->preview_ready = true;

	if (in_use)
		draw_begin(current, false, 0);
}

void util_render_make_brush_preview_parse_paint_material(void *_) {
	make_material_parse_paint_material(false);
}

void util_render_make_brush_preview() {
	if (render_path_paint_live_layer_locked) {
		return;
	}

	if (g_config->workflow == WORKFLOW_SCULPT) {
		return;
	}

	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	g_context->material_preview = true;

	// Prepare layers
	if (render_path_paint_live_layer == NULL) {
		render_path_paint_live_layer = slot_layer_create("_live", LAYER_SLOT_TYPE_LAYER, NULL);
	}

	slot_layer_t *l = render_path_paint_live_layer;
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);

	if (g_context->brush->image == NULL) {
		g_context->brush->image = gpu_create_render_target(util_render_material_preview_size, util_render_material_preview_size, GPU_TEXTURE_FORMAT_RGBA32);
		g_context->brush->image_icon = gpu_create_render_target(50, 50, GPU_TEXTURE_FORMAT_RGBA32);
	}

	slot_material_t *_material = g_context->material;
	g_context->material        = slot_material_create(NULL, NULL);

	// Prevent grid jump
	g_context->material->nodes->pan_x = g_context->brush->nodes->pan_x;
	g_context->material->nodes->pan_y = g_context->brush->nodes->pan_y;
	g_context->material->nodes->zoom  = g_context->brush->nodes->zoom;

	tool_type_t _tool = g_context->tool;
	g_context->tool   = TOOL_TYPE_BRUSH;

	slot_layer_t *_layer = g_context->layer;
	if (slot_layer_is_mask(g_context->layer)) {
		g_context->layer = g_context->layer->parent;
	}

	slot_material_t *_fill_material = g_context->layer->fill_material;
	g_context->layer->fill_material = NULL;

	render_path_paint_use_live_layer(true);
	make_material_parse_paint_material(false);

	i32 hid = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
	any_map_set(render_path_render_targets, string("texpaint_undo%d", hid), any_map_get(render_path_render_targets, "empty_black"));

	// Set plane mesh
	mesh_object_t *painto   = g_context->paint_object;
	u8_array_t    *visibles = u8_array_create_from_raw((u8[]){}, 0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		u8_array_push(visibles, p->base->visible);
		p->base->visible = false;
	}
	bool merged_object_visible = false;
	if (g_context->merged_object != NULL) {
		merged_object_visible                   = g_context->merged_object->base->visible;
		g_context->merged_object->base->visible = false;
	}

	camera_object_t *cam    = scene_camera;
	g_context->saved_camera = cam->base->transform->local;
	f32 saved_fov           = cam->data->fov;
	viewport_update_camera_type(CAMERA_TYPE_PERSPECTIVE);
	mat4_t m = mat4_identity();
	m        = mat4_translate(m, 0, 0, 0.5);
	transform_set_matrix(cam->base->transform, m);
	cam->data->fov = 0.92;
	camera_object_build_proj(cam, -1.0);
	camera_object_build_mat(cam);
	m = mat4_inv(scene_camera->vp);

	mesh_object_t *planeo   = scene_get_child(".Plane")->ext;
	planeo->base->visible   = true;
	g_context->paint_object = planeo;

	vec4_t v                       = (vec4_t){0.0, 0.0, 0.0, 1.0};
	v                              = (vec4_t){m.m00, m.m01, m.m02, 1.0};
	f32 sx                         = vec4_len(v);
	planeo->base->transform->rot   = quat_from_euler(-math_pi() / 2.0, 0, 0);
	planeo->base->transform->scale = (vec4_t){sx, 1.0, sx, 1.0};
	planeo->base->transform->loc   = (vec4_t){m.m30, -m.m31, 0.0, 1.0};
	transform_build_matrix(planeo->base->transform);

	render_path_paint_live_layer_drawn = 0;
	render_path_base_draw_gbuffer();

	// Paint brush preview
	f32 _brush_radius         = g_context->brush_radius;
	f32 _brush_opacity        = g_context->brush_opacity;
	f32 _brush_hardness       = g_context->brush_hardness;
	g_context->brush_radius   = 0.33;
	g_context->brush_opacity  = 1.0;
	g_context->brush_hardness = 1.0;
	f32 _x                    = g_context->paint_vec.x;
	f32 _y                    = g_context->paint_vec.y;
	f32 _last_x               = g_context->last_paint_vec_x;
	f32 _last_y               = g_context->last_paint_vec_y;
	i32 _pdirty               = g_context->pdirty;
	g_context->pdirty         = 2;

	f32_array_t *points_x = f32_array_create_from_raw(
	    (f32[]){
	        0.2,
	        0.2,
	        0.35,
	        0.5,
	        0.5,
	        0.5,
	        0.65,
	        0.8,
	        0.8,
	        0.8,
	    },
	    10);
	f32_array_t *points_y = f32_array_create_from_raw(
	    (f32[]){
	        0.5,
	        0.5,
	        0.35 - 0.04,
	        0.2 - 0.08,
	        0.4 + 0.015,
	        0.6 + 0.03,
	        0.45 - 0.025,
	        0.3 - 0.05,
	        0.5 + 0.025,
	        0.7 + 0.05,
	    },
	    10);

	bool sphere_mode = g_context->brush_lazy_radius > 0 && g_context->brush_lazy_step > 0;
	f32  dot_spacing = 0.0;
	if (sphere_mode) {
		f32 _posx              = g_context->posx_picked;
		f32 _posy              = g_context->posy_picked;
		f32 _posz              = g_context->posz_picked;
		g_context->posx_picked = planeo->base->transform->loc.x;
		g_context->posy_picked = planeo->base->transform->loc.y;
		g_context->posz_picked = planeo->base->transform->loc.z;
		dot_spacing            = g_context->brush_lazy_radius * g_context->brush_lazy_step * util_layer_brush_screen_radius() * 3.0;
		g_context->posx_picked = _posx;
		g_context->posy_picked = _posy;
		g_context->posz_picked = _posz;
	}

	f32 aspect = sys_w() / (f32)sys_h();
	for (i32 i = 1; i < points_x->length; ++i) {
		f32 x0 = points_x->buffer[i - 1];
		f32 y0 = points_y->buffer[i - 1];
		f32 x1 = points_x->buffer[i];
		f32 y1 = points_y->buffer[i];
		i32 n  = 1;
		if (dot_spacing > 0.0) {
			f32 len = vec4_dist((vec4_t){x0 * aspect, y0, 0.0, 1.0}, (vec4_t){x1 * aspect, y1, 0.0, 1.0});
			n       = ceilf(len / dot_spacing);
			n       = n < 1 ? 1 : n > 16 ? 16 : n;
		}
		for (i32 s = 1; s <= n; ++s) {
			f32 t                       = s / (f32)n;
			f32 px                      = x0 + (x1 - x0) * t;
			f32 py                      = y0 + (y1 - y0) * t;
			g_context->last_paint_vec_x = sphere_mode ? px : x0;
			g_context->last_paint_vec_y = sphere_mode ? py : y0;
			g_context->paint_vec.x      = px;
			g_context->paint_vec.y      = py;
			render_path_paint_commands_paint(false);
		}
	}

	g_context->brush_radius     = _brush_radius;
	g_context->brush_opacity    = _brush_opacity;
	g_context->brush_hardness   = _brush_hardness;
	g_context->paint_vec.x      = _x;
	g_context->paint_vec.y      = _y;
	g_context->last_paint_vec_x = _last_x;
	g_context->last_paint_vec_y = _last_y;
	g_context->prev_paint_vec_x = -1;
	g_context->prev_paint_vec_y = -1;
	g_context->pdirty           = _pdirty;
	render_path_paint_use_live_layer(false);
	g_context->layer->fill_material = _fill_material;
	g_context->layer                = _layer;
	g_context->material             = _material;
	g_context->tool                 = _tool;
	sys_notify_on_next_frame(&util_render_make_brush_preview_parse_paint_material, NULL);

	// Restore paint mesh
	g_context->material_preview = false;
	planeo->base->visible       = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		g_project->_->paint_objects->buffer[i]->base->visible = visibles->buffer[i];
	}
	if (g_context->merged_object != NULL) {
		g_context->merged_object->base->visible = merged_object_visible;
	}
	g_context->paint_object = painto;
	transform_set_matrix(scene_camera->base->transform, g_context->saved_camera);
	scene_camera->data->fov = saved_fov;
	viewport_update_camera_type(g_context->camera_type);
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);

	// Scale layer down to to image preview
	l                     = render_path_paint_live_layer;
	gpu_texture_t *target = g_context->brush->image;
	draw_begin(target, true, 0x00000000);
	draw_set_pipeline(pipes_copy);
	draw_scaled_image(l->texpaint, 0, 0, target->width, target->height);
	draw_set_pipeline(NULL);
	draw_end();

	// Scale image preview down to icon
	render_target_t *texpreview      = any_map_get(render_path_render_targets, "texpreview");
	texpreview->_image               = g_context->brush->image;
	render_target_t *texpreview_icon = any_map_get(render_path_render_targets, "texpreview_icon");
	texpreview_icon->_image          = g_context->brush->image_icon;
	render_path_set_target("texpreview_icon", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("texpreview", "tex");
	render_path_draw_shader("Scene/supersample_resolve/supersample_resolve");

	g_context->brush->preview_ready = true;
	g_context->brush_blend_dirty    = true;

	if (in_use)
		draw_begin(current, false, 0);
}

void util_render_create_screen_aligned_full_data() {
	// Over-sized triangle
	i16_array_t *data = i16_array_create_from_raw(
	    (i16[]){
	        -math_floor(32767 / 3),
	        -math_floor(32767 / 3),
	        0,
	        32767,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        32767,
	        -math_floor(32767 / 3),
	        0,
	        32767,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        -math_floor(32767 / 3),
	        32767,
	        0,
	        32767,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	        0,
	    },
	    36);
	u32_array_t *indices = u32_array_create_from_raw(
	    (u32[]){
	        0,
	        1,
	        2,
	    },
	    3);

	// Mandatory vertex data names and sizes
	gpu_vertex_structure_t *structure = ALLOC_INIT(gpu_vertex_structure_t, {0});
	gpu_vertex_structure_add(structure, "pos", GPU_VERTEX_DATA_I16_4X_NORM);
	gpu_vertex_structure_add(structure, "nor", GPU_VERTEX_DATA_I16_2X_NORM);
	gpu_vertex_structure_add(structure, "tex", GPU_VERTEX_DATA_I16_2X_NORM);
	gpu_vertex_structure_add(structure, "col", GPU_VERTEX_DATA_I16_4X_NORM);
	util_render_screen_aligned_full_vb =
	    gpu_create_vertex_buffer(math_floor(data->length / (float)math_floor(gpu_vertex_struct_size(structure) / 2.0)), structure);
	int16_t *vertices = gpu_vertex_buffer_lock(util_render_screen_aligned_full_vb);
	for (i32 i = 0; i < data->length; ++i) {
		vertices[i] = data->buffer[i];
	}
	gpu_vertex_buffer_unlock(util_render_screen_aligned_full_vb);

	util_render_screen_aligned_full_ib = gpu_create_index_buffer(indices->length);
	uint32_t *id                       = gpu_index_buffer_lock(util_render_screen_aligned_full_ib);
	for (i32 i = 0; i < indices->length; ++i) {
		id[i] = indices->buffer[i];
	}
	gpu_index_buffer_unlock(util_render_screen_aligned_full_ib);
}

void util_render_make_node_preview(ui_node_canvas_t *canvas, ui_node_t *node, gpu_texture_t *image, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	shader_context_t *scon = make_material_parse_node_preview_material(node, group, parents);
	if (scon == NULL) {
		return;
	}

	if (util_render_screen_aligned_full_vb == NULL) {
		util_render_create_screen_aligned_full_data();
	}

	f32 _scale_world                                      = g_context->paint_object->base->transform->scale_world;
	g_context->paint_object->base->transform->scale_world = 3.0;
	transform_build_matrix(g_context->paint_object->base->transform);

	_gpu_begin(image, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(scon->_->pipe);
	static string_array_t *empty = NULL;
	if (empty == NULL) {
		empty = any_array_create_from_raw(
		    (void *[]){
		        "",
		    },
		    1);
	}
	uniforms_set_context_consts(scon, empty);
	uniforms_set_obj_consts(scon, g_context->paint_object->base);
	gpu_set_vertex_buffer(util_render_screen_aligned_full_vb);
	gpu_set_index_buffer(util_render_screen_aligned_full_ib);
	gpu_draw();
	gpu_end();

	make_material_delete_context(scon);

	g_context->paint_object->base->transform->scale_world = _scale_world;
	transform_build_matrix(g_context->paint_object->base->transform);
}

void util_render_pick_pos_nor_tex() {
	g_context->pick_pos_nor_tex = true;
	g_context->pdirty           = 1;
	tool_type_t _tool           = g_context->tool;
	g_context->tool             = TOOL_TYPE_PICKER;
	make_material_save_paint_material();
	make_material_parse_paint_material(false);
	if (g_context->paint2d) {
		render_path_paint_set_plane_mesh();
	}
	render_path_paint_commands_paint(false);
	if (g_context->paint2d) {
		render_path_paint_restore_plane_mesh();
	}
	g_context->tool             = _tool;
	g_context->pick_pos_nor_tex = false;
	make_material_restore_paint_material();
	g_context->pdirty = 0;
}

// Standard closest-point-on-triangle test (Ericson, Real-Time Collision Detection).
vec4_t util_render_closest_point_on_triangle(vec4_t p, vec4_t a, vec4_t b, vec4_t c) {
	vec4_t ab = vec4_sub(b, a);
	vec4_t ac = vec4_sub(c, a);
	vec4_t ap = vec4_sub(p, a);
	f32    d1 = vec4_dot(ab, ap);
	f32    d2 = vec4_dot(ac, ap);
	if (d1 <= 0.0 && d2 <= 0.0) {
		return a;
	}

	vec4_t bp = vec4_sub(p, b);
	f32    d3 = vec4_dot(ab, bp);
	f32    d4 = vec4_dot(ac, bp);
	if (d3 >= 0.0 && d4 <= d3) {
		return b;
	}

	f32 vc = d1 * d4 - d3 * d2;
	if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
		f32 v = d1 / (d1 - d3);
		return vec4_add(a, vec4_mult(ab, v));
	}

	vec4_t cp = vec4_sub(p, c);
	f32    d5 = vec4_dot(ab, cp);
	f32    d6 = vec4_dot(ac, cp);
	if (d6 >= 0.0 && d5 <= d6) {
		return c;
	}

	f32 vb = d5 * d2 - d1 * d6;
	if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
		f32 w = d2 / (d2 - d6);
		return vec4_add(a, vec4_mult(ac, w));
	}

	f32 va = d3 * d6 - d5 * d4;
	if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
		f32 w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		return vec4_add(b, vec4_mult(vec4_sub(c, b), w));
	}

	f32 denom = 1.0 / (va + vb + vc);
	f32 v     = vb * denom;
	f32 w     = vc * denom;
	return vec4_add(a, vec4_add(vec4_mult(ab, v), vec4_mult(ac, w)));
}

static vec4_t util_render_fill_vertex(i16_array_t *posa, u32 i, mat4_t world) {
	return vec4_apply_mat4((vec4_t){posa->buffer[i * 4] / 32767.0, posa->buffer[i * 4 + 1] / 32767.0, posa->buffer[i * 4 + 2] / 32767.0, 1.0}, world);
}

// UV centroid and world-space face normal of one triangle of the paint mesh
static void util_render_fill_tri_info(i32 tri, f32 *out_uvx, f32 *out_uvy, f32 *out_norx, f32 *out_nory, f32 *out_norz) {
	mesh_object_t *obj   = g_context->paint_object;
	mesh_data_t   *mesh  = obj->data;
	i16_array_t   *posa  = mesh->vertex_arrays->buffer[0]->values;
	i16_array_t   *uva   = mesh->vertex_arrays->buffer[2]->values;
	u32_array_t   *inda  = mesh->index_array;
	mat4_t         world = obj->base->transform->world_unpack;

	u32 j0 = inda->buffer[tri * 3];
	u32 j1 = inda->buffer[tri * 3 + 1];
	u32 j2 = inda->buffer[tri * 3 + 2];

	f32 u0 = uva->buffer[j0 * 2] / 32767.0;
	f32 v0 = uva->buffer[j0 * 2 + 1] / 32767.0;
	f32 u1 = uva->buffer[j1 * 2] / 32767.0;
	f32 v1 = uva->buffer[j1 * 2 + 1] / 32767.0;
	f32 u2 = uva->buffer[j2 * 2] / 32767.0;
	f32 v2 = uva->buffer[j2 * 2 + 1] / 32767.0;

	vec4_t q0       = util_render_fill_vertex(posa, j0, world);
	vec4_t q1       = util_render_fill_vertex(posa, j1, world);
	vec4_t q2       = util_render_fill_vertex(posa, j2, world);
	vec4_t face_nor = vec4_norm(vec4_cross(vec4_sub(q1, q0), vec4_sub(q2, q0)));

	*out_uvx  = (u0 + u1 + u2) / 3.0;
	*out_uvy  = (v0 + v1 + v2) / 3.0;
	*out_norx = face_nor.x;
	*out_nory = face_nor.y;
	*out_norz = face_nor.z;
}

// What the last util_render_pick_fill_candidates() call was computed from
static f32    util_render_fill_key[15];
static mat4_t util_render_fill_key_world;
static bool   util_render_fill_key_valid = false;

// Forget the remembered candidates, the mesh may have changed since (call when a stroke starts)
void util_render_pick_fill_reset() {
	util_render_fill_key_valid = false;
}

// For the point picked under the cursor, finds the other surface points Fill has to match as well:
//  - Symmetry: the closest point on the mesh to its reflection across each active axis (about the paint
//    object's pivot and local axes, so position and rotation are respected)
//  - X-Ray: the second surface hit along the cursor ray, directly behind the visible one
// Both are geometric queries against the mesh triangles instead of extra renders, so they also work for
// sides the camera can not see (e.g. mirroring top to bottom).
//
// Every candidate is found in a single walk over the triangles (each one is transformed once). The result
// is remembered: nothing is recomputed while the picked point, cursor ray, symmetry/x-ray flags and object
// transform stay the same as in the previous call. Returns true when the candidates were recomputed.
bool util_render_pick_fill_candidates() {
	bool sym[3] = {g_context->sym_x, g_context->sym_y, g_context->sym_z};
	bool xray   = g_context->xray;

	mesh_object_t *obj   = g_context->paint_object;
	mat4_t         world = obj->base->transform->world_unpack;

	ray_t *ray = NULL;
	if (xray) {
		ray = raycast_get_ray(g_context->paint_vec.x * sys_w(), g_context->paint_vec.y * sys_h(), scene_camera);
	}

	f32 key[15] = {
	    g_context->posx_picked,
	    g_context->posy_picked,
	    g_context->posz_picked,
	    g_context->uvx_picked,
	    g_context->uvy_picked,
	    sym[0] ? 1.0 : 0.0,
	    sym[1] ? 1.0 : 0.0,
	    sym[2] ? 1.0 : 0.0,
	    xray ? 1.0 : 0.0,
	    xray ? ray->origin.x : 0.0,
	    xray ? ray->origin.y : 0.0,
	    xray ? ray->origin.z : 0.0,
	    xray ? ray->dir.x : 0.0,
	    xray ? ray->dir.y : 0.0,
	    xray ? ray->dir.z : 0.0,
	};
	if (util_render_fill_key_valid && memcmp(key, util_render_fill_key, sizeof(key)) == 0 &&
	    memcmp(&world, &util_render_fill_key_world, sizeof(mat4_t)) == 0) {
		free(ray);
		return false;
	}
	memcpy(util_render_fill_key, key, sizeof(key));
	util_render_fill_key_world = world;
	util_render_fill_key_valid = true;

	g_context->fill_sym_x_valid = false;
	g_context->fill_sym_y_valid = false;
	g_context->fill_sym_z_valid = false;
	g_context->fill_xray_valid  = false;

	bool any_sym = sym[0] || sym[1] || sym[2];
	if (!any_sym && !xray) {
		return true;
	}

	mesh_data_t *mesh = obj->data;
	i16_array_t *posa = mesh->vertex_arrays->buffer[0]->values;
	u32_array_t *inda = mesh->index_array;

	// Reflect the picked point across each active axis. Rows of the world matrix are the object's local
	// axis directions in world space (what vec4_apply_mat4 yields for (1,0,0,0), (0,1,0,0), (0,0,1,0))
	mat4_t W      = obj->base->transform->world;
	vec4_t origin = (vec4_t){W.m30, W.m31, W.m32, 1.0};
	vec4_t axis[3];
	axis[0] = vec4_norm((vec4_t){W.m00, W.m01, W.m02, 0.0});
	axis[1] = vec4_norm((vec4_t){W.m10, W.m11, W.m12, 0.0});
	axis[2] = vec4_norm((vec4_t){W.m20, W.m21, W.m22, 0.0});
	vec4_t delta = vec4_sub((vec4_t){g_context->posx_picked, g_context->posy_picked, g_context->posz_picked, 1.0}, origin);

	vec4_t target[3];
	f32    best_dist[3] = {-1.0, -1.0, -1.0};
	i32    best_tri[3]  = {-1, -1, -1};
	for (i32 k = 0; k < 3; ++k) {
		if (sym[k]) {
			target[k] = vec4_add(origin, vec4_reflect(delta, axis[k]));
		}
	}

	f32 dist1 = -1.0;
	f32 dist2 = -1.0;
	i32 tri1  = -1;
	i32 tri2  = -1;

	f32 ray_dir_len_sq = xray ? vec4_dot(ray->dir, ray->dir) : 0.0;

	i32 tri_count = math_floor(inda->length / 3.0);
	for (i32 i = 0; i < tri_count; ++i) {
		vec4_t p0 = util_render_fill_vertex(posa, inda->buffer[i * 3], world);
		vec4_t p1 = util_render_fill_vertex(posa, inda->buffer[i * 3 + 1], world);
		vec4_t p2 = util_render_fill_vertex(posa, inda->buffer[i * 3 + 2], world);

		// Bounding sphere of the triangle, lets the exact tests below be skipped for the ones that can not win
		vec4_t center = vec4_mult(vec4_add(vec4_add(p0, p1), p2), 1.0 / 3.0);
		f32    radius = math_sqrt(math_max(vec4_dot(vec4_sub(p0, center), vec4_sub(p0, center)),
		                                   math_max(vec4_dot(vec4_sub(p1, center), vec4_sub(p1, center)), vec4_dot(vec4_sub(p2, center), vec4_sub(p2, center)))));

		for (i32 k = 0; k < 3; ++k) {
			if (!sym[k]) {
				continue;
			}
			if (best_tri[k] != -1 && vec4_dist(target[k], center) - radius > best_dist[k] * 1.0001 + 0.000001) {
				continue;
			}
			vec4_t cp   = util_render_closest_point_on_triangle(target[k], p0, p1, p2);
			f32    dist = vec4_dist(cp, target[k]);
			if (best_tri[k] == -1 || dist < best_dist[k]) {
				best_dist[k] = dist;
				best_tri[k]  = i;
			}
		}

		if (xray) {
			// Distance from the sphere center to the ray, compared without normalizing the ray direction
			vec4_t to_center = vec4_sub(center, ray->origin);
			vec4_t off       = vec4_cross(to_center, ray->dir);
			if (vec4_dot(off, off) > radius * radius * ray_dir_len_sq * 1.0002 + 0.000001) {
				continue;
			}
			vec4_t hit = ray_intersect_triangle(ray, p0, p1, p2, false);
			if (!vec4_isnan(hit)) {
				f32 dist = vec4_len(vec4_sub(hit, ray->origin));
				if (tri1 == -1 || dist < dist1) {
					dist2 = dist1;
					tri2  = tri1;
					dist1 = dist;
					tri1  = i;
				}
				else if (tri2 == -1 || dist < dist2) {
					dist2 = dist;
					tri2  = i;
				}
			}
		}
	}

	free(ray);

	if (best_tri[0] != -1) {
		util_render_fill_tri_info(best_tri[0], &g_context->fill_sym_x_uvx, &g_context->fill_sym_x_uvy, &g_context->fill_sym_x_norx, &g_context->fill_sym_x_nory,
		                          &g_context->fill_sym_x_norz);
		g_context->fill_sym_x_valid = true;
	}
	if (best_tri[1] != -1) {
		util_render_fill_tri_info(best_tri[1], &g_context->fill_sym_y_uvx, &g_context->fill_sym_y_uvy, &g_context->fill_sym_y_norx, &g_context->fill_sym_y_nory,
		                          &g_context->fill_sym_y_norz);
		g_context->fill_sym_y_valid = true;
	}
	if (best_tri[2] != -1) {
		util_render_fill_tri_info(best_tri[2], &g_context->fill_sym_z_uvx, &g_context->fill_sym_z_uvy, &g_context->fill_sym_z_norx, &g_context->fill_sym_z_nory,
		                          &g_context->fill_sym_z_norz);
		g_context->fill_sym_z_valid = true;
	}
	// The first hit is the visible surface the normal pick already covers, X-Ray adds the one behind it
	if (tri2 != -1) {
		util_render_fill_tri_info(tri2, &g_context->fill_xray_uvx, &g_context->fill_xray_uvy, &g_context->fill_xray_norx, &g_context->fill_xray_nory,
		                          &g_context->fill_xray_norz);
		g_context->fill_xray_valid = true;
	}
	return true;
}

mat4_t util_render_get_decal_mat() {
	util_render_pick_pos_nor_tex();
	mat4_t decal_mat = mat4_identity();
	vec4_t loc       = (vec4_t){g_context->posx_picked, g_context->posy_picked, g_context->posz_picked, 1.0};
	quat_t rot       = quat_from_to((vec4_t){0.0, 0.0, -1.0, 1.0}, (vec4_t){g_context->norx_picked, g_context->nory_picked, g_context->norz_picked, 1.0});
	vec4_t scale     = (vec4_t){g_context->brush_radius * 0.5, g_context->brush_radius * 0.5, g_context->brush_radius * 0.5, 1.0};
	decal_mat        = mat4_compose(loc, rot, scale);
	return decal_mat;
}
