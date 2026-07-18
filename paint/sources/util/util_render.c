
#include "../global.h"

gpu_buffer_t *util_render_screen_aligned_full_vb = NULL;
gpu_buffer_t *util_render_screen_aligned_full_ib = NULL;

void util_render_make_material_preview() {
	g_context->material_preview = true;

	mesh_object_t *sphere         = scene_get_child(".Sphere")->ext;
	sphere->base->visible         = true;
	mesh_object_t_array_t *meshes = scene_meshes;
	gc_unroot(scene_meshes);
	scene_meshes = any_array_create_from_raw(
	    (void *[]){
	        sphere,
	    },
	    1);
	gc_root(scene_meshes);
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
	gc_unroot(render_path_commands);
	render_path_commands = render_path_preview_commands_preview;
	gc_root(render_path_commands);
	render_path_render_frame();
	gc_unroot(render_path_commands);
	render_path_commands = _commands;
	gc_root(render_path_commands);

	g_context->material_preview = false;
	_render_path_last_w         = sys_w();
	_render_path_last_h         = sys_h();

	// Restore
	sphere->base->visible = false;
	gc_unroot(scene_meshes);
	scene_meshes = meshes;
	gc_root(scene_meshes);
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
	gc_unroot(scene_meshes);
	scene_meshes = any_array_create_from_raw(
	    (void *[]){
	        plane,
	    },
	    1);
	gc_root(scene_meshes);
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
	gc_unroot(render_path_commands);
	render_path_commands = render_path_preview_commands_decal;
	gc_root(render_path_commands);
	render_path_render_frame();
	gc_unroot(render_path_commands);
	render_path_commands = _commands;
	gc_root(render_path_commands);

	g_context->decal_preview = false;
	_render_path_last_w      = sys_w();
	_render_path_last_h      = sys_h();

	// Restore
	plane->base->visible = false;
	gc_unroot(scene_meshes);
	scene_meshes = meshes;
	gc_root(scene_meshes);
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
		gc_unroot(render_path_paint_live_layer);
		render_path_paint_live_layer = slot_layer_create("_live", LAYER_SLOT_TYPE_LAYER, NULL);
		gc_root(render_path_paint_live_layer);
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
	gpu_vertex_structure_t *structure = GC_ALLOC_INIT(gpu_vertex_structure_t, {0});
	gpu_vertex_structure_add(structure, "pos", GPU_VERTEX_DATA_I16_4X_NORM);
	gpu_vertex_structure_add(structure, "nor", GPU_VERTEX_DATA_I16_2X_NORM);
	gpu_vertex_structure_add(structure, "tex", GPU_VERTEX_DATA_I16_2X_NORM);
	gpu_vertex_structure_add(structure, "col", GPU_VERTEX_DATA_I16_4X_NORM);
	gc_unroot(util_render_screen_aligned_full_vb);
	util_render_screen_aligned_full_vb =
	    gpu_create_vertex_buffer(math_floor(data->length / (float)math_floor(gpu_vertex_struct_size(structure) / 2.0)), structure);
	gc_root(util_render_screen_aligned_full_vb);
	int16_t *vertices = gpu_vertex_buffer_lock(util_render_screen_aligned_full_vb);
	for (i32 i = 0; i < data->length; ++i) {
		vertices[i] = data->buffer[i];
	}
	gpu_vertex_buffer_unlock(util_render_screen_aligned_full_vb);

	gc_unroot(util_render_screen_aligned_full_ib);
	util_render_screen_aligned_full_ib = gpu_create_index_buffer(indices->length);
	gc_root(util_render_screen_aligned_full_ib);
	uint32_t *id = gpu_index_buffer_lock(util_render_screen_aligned_full_ib);
	for (i32 i = 0; i < indices->length; ++i) {
		id[i] = indices->buffer[i];
	}
	gpu_index_buffer_unlock(util_render_screen_aligned_full_ib);
}

void util_render_make_node_preview(ui_node_canvas_t *canvas, ui_node_t *node, gpu_texture_t *image, ui_node_canvas_t *group, ui_node_t_array_t *parents) {
	parse_node_preview_result_t *res = make_material_parse_node_preview_material(node, group, parents);
	if (res == NULL || res->scon == NULL) {
		return;
	}

	if (util_render_screen_aligned_full_vb == NULL) {
		util_render_create_screen_aligned_full_data();
	}

	f32 _scale_world                                      = g_context->paint_object->base->transform->scale_world;
	g_context->paint_object->base->transform->scale_world = 3.0;
	transform_build_matrix(g_context->paint_object->base->transform);

	_gpu_begin(image, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(res->scon->_->pipe);
	string_array_t *empty = any_array_create_from_raw(
	    (void *[]){
	        "",
	    },
	    1);
	uniforms_set_context_consts(res->scon, empty);
	uniforms_set_obj_consts(res->scon, g_context->paint_object->base);
	uniforms_set_material_consts(res->scon, res->mcon);
	gpu_set_vertex_buffer(util_render_screen_aligned_full_vb);
	gpu_set_index_buffer(util_render_screen_aligned_full_ib);
	gpu_draw();
	gpu_end();

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

mat4_t util_render_get_decal_mat() {
	util_render_pick_pos_nor_tex();
	mat4_t decal_mat = mat4_identity();
	vec4_t loc       = (vec4_t){g_context->posx_picked, g_context->posy_picked, g_context->posz_picked, 1.0};
	quat_t rot       = quat_from_to((vec4_t){0.0, 0.0, -1.0, 1.0}, (vec4_t){g_context->norx_picked, g_context->nory_picked, g_context->norz_picked, 1.0});
	vec4_t scale     = (vec4_t){g_context->brush_radius * 0.5, g_context->brush_radius * 0.5, g_context->brush_radius * 0.5, 1.0};
	decal_mat        = mat4_compose(loc, rot, scale);
	return decal_mat;
}
