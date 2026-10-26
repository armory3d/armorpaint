
#include "../global.h"

gpu_buffer_t *util_render_screen_aligned_full_vb = NULL;
gpu_buffer_t *util_render_screen_aligned_full_ib = NULL;

typedef struct preview_env {
	vec4_t         up;
	vec4_t         d;
	gpu_texture_t *radiance;
	any_array_t   *mips; // gpu_texture_t[], 5 blurred levels
} preview_env_t;

typedef struct env_params {
	f32 ground;
	f32 sky;
	f32 horizon[6];  // Gradient half width per blur level
	f32 lobe_exp[6]; // Key light lobe per blur level
	f32 lobe_peak[6];
} env_params_t;

// Dim key light
static env_params_t util_render_preview_env_params = {
    .ground    = 0.12,
    .sky       = 0.47,
    .horizon   = {0.5, 0.55, 0.6, 0.7, 0.8, 1.0},
    .lobe_exp  = {600.0, 150.0, 40.0, 12.0, 5.0, 2.0},
    .lobe_peak = {3.0, 2.0, 1.2, 0.8, 0.5, 0.35},
};

static f32_array_t   *util_render_preview_irradiance = NULL;
static preview_env_t  util_render_preview_envs[4];
static i32            util_render_preview_envs_count     = 0;
static f32_array_t   *util_render_saved_irradiance       = NULL;
static gpu_texture_t *util_render_saved_radiance         = NULL;
static any_array_t   *util_render_saved_radiance_mipmaps = NULL;
static f32            util_render_saved_envmap_angle     = 0.0;
static f32            util_render_saved_strength         = 0.0;

static f32 util_render_smoothstep(f32 e0, f32 e1, f32 x) {
	f32 t = math_min(math_max((x - e0) / (e1 - e0), 0.0), 1.0);
	return t * t * (3.0 - 2.0 * t);
}

static buffer_t *util_render_make_env_pixels(env_params_t *p, vec4_t up, vec4_t d, i32 level, i32 w) {
	// Equirect RGBA16F sky over a dark ground with a key light lobe, prefiltered for the given blur level
	// Inverse of envmap_equirect() in deferred_light with angle 0, n = (sin_phi * cos_theta, -sin_phi * sin_theta, cos_phi)
	i32       h       = w / 2;
	buffer_t *b       = buffer_create(w * h * 8);
	u16      *px      = (u16 *)b->buffer;
	f32      *col_up  = malloc(sizeof(f32) * w * 2);
	f32      *col_d   = col_up + w;
	f32       horizon = p->horizon[level];
	f32       e       = p->lobe_exp[level];
	f32       peak    = p->lobe_peak[level];
	f32       cutoff  = math_pow(0.0005 / peak, 1.0 / e); // Lobe below half float precision
	u16       one     = float_to_half_fast(1.0);
	for (i32 x = 0; x < w; ++x) {
		f32 theta = (x + 0.5) / w * 2.0 * math_pi() - math_pi();
		f32 ct    = math_cos(theta);
		f32 st    = math_sin(theta);
		col_up[x] = ct * up.x - st * up.y;
		col_d[x]  = ct * d.x - st * d.y;
	}
	for (i32 y = 0; y < h; ++y) {
		f32 phi = (y + 0.5) / h * math_pi();
		f32 sp  = math_sin(phi);
		f32 cp  = math_cos(phi);
		for (i32 x = 0; x < w; ++x) {
			f32 dot_up = sp * col_up[x] + cp * up.z;
			f32 dot_d  = sp * col_d[x] + cp * d.z;
			f32 v      = p->ground + (p->sky - p->ground) * util_render_smoothstep(-horizon, horizon, dot_up);
			if (dot_d > cutoff) {
				v += peak * math_pow(dot_d, e);
			}
			u16 hv    = float_to_half_fast(v);
			i32 i     = (y * w + x) * 4;
			px[i]     = hv;
			px[i + 1] = hv;
			px[i + 2] = hv;
			px[i + 3] = one;
		}
	}
	free(col_up);
	return b;
}

static void util_render_make_env(env_params_t *p, vec4_t up, vec4_t d, i32 w, gpu_texture_t **radiance, any_array_t **mips, buffer_t **radiance_pixels) {
	*mips = any_array_create_from_raw((void *[]){}, 0);
	for (i32 level = 0; level < 6; ++level) {
		buffer_t      *b   = util_render_make_env_pixels(p, up, d, level, w);
		gpu_texture_t *tex = gpu_create_texture_from_bytes(b, w, w / 2, GPU_TEXTURE_FORMAT_RGBA64);
		if (level == 0) {
			*radiance = tex;
		}
		else {
			any_array_push(*mips, tex);
		}
		if (level == 0 && radiance_pixels != NULL) {
			*radiance_pixels = b;
		}
		else {
			array_free(b);
			free(b);
		}
		w /= 2;
	}
}

// Energy conserving sun
static env_params_t util_render_world_env_params = {
    .ground    = 0.15,
    .sky       = 0.5,
    .horizon   = {0.1, 0.15, 0.25, 0.4, 0.6, 0.9},
    .lobe_exp  = {600.0, 150.0, 40.0, 12.0, 5.0, 2.0},
    .lobe_peak = {191.0, 48.0, 13.0, 4.1, 1.9, 0.95}, // sun_energy * (exp + 1) / 2pi
};

static gpu_texture_t *util_render_default_radiance        = NULL;
static buffer_t      *util_render_default_radiance_pixels = NULL;

void util_render_make_default_world() {
	vec4_t       up = (vec4_t){0.0, 0.0, 1.0, 0.0};
	vec4_t       d  = vec4_norm((vec4_t){-0.43, -0.52, 1.0, 0.0});
	any_array_t *mips;
	util_render_make_env(&util_render_world_env_params, up, d, util_render_default_envmap_w, &util_render_default_radiance, &mips,
	                     &util_render_default_radiance_pixels);
	// Prefiltering conserves the sun energy, a blurred level projects to sh without aliasing
	buffer_t *b                      = util_render_make_env_pixels(&util_render_world_env_params, up, d, 3, 64);
	scene_world->_->irradiance       = import_envmap_get_spherical_harmonics(b, 64, 32);
	scene_world->_->radiance         = util_render_default_radiance;
	scene_world->_->radiance_mipmaps = mips;
	scene_world->envmap              = util_render_default_envmap_name;
	array_free(b);
	free(b);
}

bool util_render_is_default_envmap(char *file) {
	return file != NULL && string_equals(file, util_render_default_envmap_name);
}

gpu_texture_t *util_render_default_envmap() {
	return util_render_default_radiance;
}

buffer_t *util_render_default_envmap_pixels() {
	return util_render_default_radiance_pixels;
}

void util_render_preview_lighting_begin(f32 ambient, f32 key) {
	// Neutral lighting independent of the scene envmap, with a key light from the top left of the camera
	if (util_render_preview_irradiance == NULL) {
		util_render_preview_irradiance = f32_array_create(27);
	}
	f32_array_t *irr = util_render_preview_irradiance;
	memset(irr->buffer, 0, 27 * sizeof(f32));
	vec4_t right = camera_object_right(scene_camera);
	vec4_t up    = camera_object_up(scene_camera);
	vec4_t back  = vec4_mult(camera_object_look(scene_camera), -1.0);
	vec4_t d     = vec4_norm((vec4_t){-0.6 * right.x + 0.3 * back.x + up.x, -0.6 * right.y + 0.3 * back.y + up.y, -0.6 * right.z + 0.3 * back.z + up.z, 0.0});
	for (i32 c = 0; c < 3; ++c) {
		irr->buffer[0 + c] = ambient;   // l00
		irr->buffer[3 + c] = key * d.y; // l1-1
		irr->buffer[6 + c] = key * d.z; // l10
		irr->buffer[9 + c] = key * d.x; // l11
	}

	// One radiance per preview camera
	preview_env_t *env = NULL;
	for (i32 i = 0; i < util_render_preview_envs_count; ++i) {
		preview_env_t *e = &util_render_preview_envs[i];
		if (vec4_dot(e->up, up) > 0.999 && vec4_dot(e->d, d) > 0.999) {
			env = e;
			break;
		}
	}
	if (env == NULL) {
		if (util_render_preview_envs_count < 4) {
			env = &util_render_preview_envs[util_render_preview_envs_count++];
		}
		else {
			env = &util_render_preview_envs[0];
			gpu_delete_texture(env->radiance);
			for (i32 i = 0; i < env->mips->length; ++i) {
				gpu_delete_texture(env->mips->buffer[i]);
			}
		}
		env->up = up;
		env->d  = d;
		util_render_make_env(&util_render_preview_env_params, up, d, 256, &env->radiance, &env->mips, NULL);
	}

	util_render_saved_irradiance       = scene_world->_->irradiance;
	util_render_saved_radiance         = scene_world->_->radiance;
	util_render_saved_radiance_mipmaps = scene_world->_->radiance_mipmaps;
	util_render_saved_envmap_angle     = g_context->envmap_angle;
	scene_world->_->irradiance         = irr;
	scene_world->_->radiance           = env->radiance;
	scene_world->_->radiance_mipmaps   = env->mips;
	g_context->envmap_angle            = 0.0;
	util_render_saved_strength         = scene_world->strength;
	scene_world->strength              = 2.0;
}

void util_render_preview_lighting_end() {
	scene_world->_->irradiance       = util_render_saved_irradiance;
	scene_world->_->radiance         = util_render_saved_radiance;
	scene_world->_->radiance_mipmaps = util_render_saved_radiance_mipmaps;
	g_context->envmap_angle          = util_render_saved_envmap_angle;
	scene_world->strength            = util_render_saved_strength;
}

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

	f32 _brush_scale             = g_context->brush_scale;
	g_context->brush_scale       = 1.5;
	f32 _brush_nodes_scale       = g_context->brush_nodes_scale;
	g_context->brush_nodes_scale = 1.0;

	gpu_texture_t *_envmap = scene_world->_->envmap;
	scene_world->_->envmap = g_context->preview_envmap;
	util_render_preview_lighting_begin(0.5, 0.65);
	// No resize
	i32 _last_w         = _render_path_last_w;
	i32 _last_h         = _render_path_last_h;
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
	_render_path_last_w         = _last_w; // Keep a pending viewport resize
	_render_path_last_h         = _last_h;

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

	g_context->brush_scale       = _brush_scale;
	g_context->brush_nodes_scale = _brush_nodes_scale;
	scene_world->_->envmap       = _envmap;
	util_render_preview_lighting_end();

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
	util_render_preview_lighting_begin(0.7, 0.65);

	// No resize
	i32 _last_w         = _render_path_last_w;
	i32 _last_h         = _render_path_last_h;
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
	_render_path_last_w      = _last_w; // Keep a pending viewport resize
	_render_path_last_h      = _last_h;

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
	util_render_preview_lighting_end();

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

#define UTIL_RENDER_BRUSH_DOT_RADIUS 1.0

static f32 util_render_brush_wave_x(i32 i, i32 segments) {
	return 0.15 + 0.7 * (i / (f32)segments);
}

static f32 util_render_brush_wave_y(i32 i, i32 segments) {
	return 0.5 + 0.04 * sinf(i / (f32)segments * math_pi() * 2.0);
}

// Scale the horizontal strip of the given height around the center of the live layer down to the target
static void util_render_brush_preview_copy(gpu_texture_t *source, gpu_texture_t *target, f32 strip) {
	f32 sh = strip > 0.0 ? source->height * strip : source->height;
	draw_begin(target, true, 0x00000000);
	draw_set_pipeline(pipes_copy);
	draw_scaled_sub_image(source, 0, (source->height - sh) / 2.0, source->width, sh, 0, 0, target->width, target->height);
	draw_set_pipeline(NULL);
	draw_end();
}

void util_render_make_brush_preview() {
	if (render_path_paint_live_layer_locked) {
		return;
	}

	if (g_config->tools == TOOLS_SCULPT) {
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
	if (g_context->brush->image_wave == NULL) {
		i32 w                        = util_render_material_preview_size;
		g_context->brush->image_wave = gpu_create_render_target(w, math_floor(w * util_render_brush_wave_strip), GPU_TEXTURE_FORMAT_RGBA32);
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

	// Dot, saved with the brush and shown in the brushes tab
	g_context->brush_radius     = UTIL_RENDER_BRUSH_DOT_RADIUS;
	g_context->prev_paint_vec_x = 0.49; // Directional brushes discard the dab without a previous position
	g_context->prev_paint_vec_y = 0.5;
	g_context->last_paint_vec_x = 0.5;
	g_context->last_paint_vec_y = 0.5;
	g_context->paint_vec.x      = 0.5;
	g_context->paint_vec.y      = 0.5;
	render_path_paint_commands_paint(false);
	util_render_brush_preview_copy(l->texpaint, g_context->brush->image, 0.0);

	// Shallow sine wave, generated at runtime for the parameters panel
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
	g_context->brush_radius = 0.25;

	// Lazy radius in screen space, the brush sits on the plane
	f32 lazy_r = 0.0;
	if (g_context->brush_lazy_radius > 0) {
		f32 _posx              = g_context->posx_picked;
		f32 _posy              = g_context->posy_picked;
		f32 _posz              = g_context->posz_picked;
		g_context->posx_picked = planeo->base->transform->loc.x;
		g_context->posy_picked = planeo->base->transform->loc.y;
		g_context->posz_picked = planeo->base->transform->loc.z;
		lazy_r                 = g_context->brush_lazy_radius * util_layer_brush_screen_radius() * 3.0;
		g_context->posx_picked = _posx;
		g_context->posy_picked = _posy;
		g_context->posz_picked = _posz;
	}
	f32  lazy_step   = g_context->brush_lazy_step;
	bool sphere_mode = lazy_r > 0 && lazy_step > 0;

	// The mouse moves along the wave and the brush follows it as in input_node_update
	// Spaced dabs are left in sphere mode, sampled finely as a slow mouse would be
	f32 aspect  = sys_w() / (f32)sys_h();
	i32 samples = 64;
	if (sphere_mode) {
		f32 len = 0.7 * aspect;
		samples = math_ceil(len / (lazy_step * lazy_r) * 4.0);
		samples = samples < 64 ? 64 : samples > 256 ? 256 : samples;
	}
	f32 bx = util_render_brush_wave_x(0, samples) * aspect; // Brush, aspect corrected
	f32 by = util_render_brush_wave_y(0, samples);
	f32 lx = bx; // Last dab
	f32 ly = by;
	// Directional brushes discard the dab without a previous position, lead into the wave
	g_context->prev_paint_vec_x = util_render_brush_wave_x(-1, samples);
	g_context->prev_paint_vec_y = util_render_brush_wave_y(-1, samples);
	for (i32 i = 0; i <= samples; ++i) {
		f32 mx = util_render_brush_wave_x(i, samples) * aspect;
		f32 my = util_render_brush_wave_y(i, samples);
		if (lazy_r > 0) {
			f32 d = vec4_dist((vec4_t){mx, my, 0.0, 1.0}, (vec4_t){bx, by, 0.0, 1.0});
			if (i > 0 && d <= lazy_r) {
				continue;
			}
			if (d > lazy_r) {
				bx = mx + (bx - mx) / d * (1.0 - lazy_step) * lazy_r;
				by = my + (by - my) / d * (1.0 - lazy_step) * lazy_r;
			}
		}
		else {
			bx = mx;
			by = my;
		}
		g_context->last_paint_vec_x = (sphere_mode ? bx : lx) / aspect; // Starts with a dot
		g_context->last_paint_vec_y = sphere_mode ? by : ly;
		g_context->paint_vec.x      = bx / aspect;
		g_context->paint_vec.y      = by;
		render_path_paint_commands_paint(false);
		lx = bx;
		ly = by;
	}
	util_render_brush_preview_copy(l->texpaint, g_context->brush->image_wave, util_render_brush_wave_strip);

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

// Background pixels reconstruct to the far plane
bool util_render_picked_on_mesh() {
	camera_object_t *cam = scene_camera;
	vec4_t           loc = (vec4_t){transform_world_x(cam->base->transform), transform_world_y(cam->base->transform), transform_world_z(cam->base->transform), 0.0};
	vec4_t           p   = (vec4_t){g_context->posx_picked, g_context->posy_picked, g_context->posz_picked, 0.0};
	f32              d   = vec4_dot(vec4_sub(p, loc), camera_object_look_world(cam));
	return d < cam->data->far_plane * 0.99;
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
