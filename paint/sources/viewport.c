
#include "global.h"

void viewport_scale_to_bounds(f32 bounds) {
	mesh_object_t *po          = g_context->merged_object == NULL ? context_main_object() : g_context->merged_object;
	mesh_data_t   *md          = po->data;
	vec4_t         aabb        = mesh_data_calculate_aabb(md);
	f32            r           = math_sqrt(aabb.x * aabb.x + aabb.y * aabb.y + aabb.z * aabb.z);
	po                         = context_main_object();
	po->base->transform->dim.x = aabb.x;
	po->base->transform->dim.y = aabb.y;
	po->base->transform->dim.z = aabb.z;
	po->base->transform->scale = (vec4_t){bounds / (float)r, bounds / (float)r, bounds / (float)r, 1.0};
	po->base->transform->loc   = (vec4_t){0, 0, 0, 1.0};
	transform_build_matrix(po->base->transform);
	for (i32 i = 0; i < po->base->children->length; ++i) {
		object_t *c       = po->base->children->buffer[i];
		c->transform->loc = (vec4_t){0, 0, 0, 1.0};
		transform_build_matrix(c->transform);
	}
}

void viewport_reset() {
	camera_object_t *cam = scene_camera;
	for (i32 i = 0; i < _scene_raw->objects->length; ++i) {
		obj_t *o = _scene_raw->objects->buffer[i];
		if (string_equals(o->type, "camera_object")) {
			cam->base->transform->local = mat4_from_f32_array(o->transform, 0);
			transform_decompose(cam->base->transform);
			cam->data->fov         = g_config->camera_fov;
			g_context->camera_type = 0;
			cam->data->ortho       = NULL;
			camera_object_build_proj(cam, -1.0);
			g_context->ddirty = 2;
			camera_reset(-1);
			transform_reset(context_main_object()->base->transform);
			break;
		}
	}
}

void viewport_set_view(f32 x, f32 y, f32 z, f32 rx, f32 ry, f32 rz) {
	g_context->paint_object->base->transform->rot   = (quat_t){0, 0, 0, 1};
	g_context->paint_object->base->transform->dirty = true;
	camera_object_t *cam                            = scene_camera;
	f32              dist                           = vec4_len(cam->base->transform->loc);
	cam->base->transform->loc                       = (vec4_t){x * dist, y * dist, z * dist, 1.0};
	cam->base->transform->rot                       = quat_from_euler(rx, ry, rz);
	transform_build_matrix(cam->base->transform);
	camera_object_build_proj(cam, -1.0);
	g_context->ddirty = 2;
	camera_reset(g_context->view_index_last);
}

void viewport_orbit(f32 x, f32 y) {
	camera_object_t *cam  = scene_camera;
	f32              dist = camera_distance();
	transform_move(cam->base->transform, camera_object_look_world(cam), dist);
	transform_rotate(cam->base->transform, (vec4_t){0, 0, 1, 1.0}, x);
	transform_rotate(cam->base->transform, camera_object_right_world(cam), y);
	transform_move(cam->base->transform, camera_object_look_world(cam), -dist);
	g_context->ddirty = 2;
}

void viewport_orbit_opposite() {
	camera_object_t *cam  = scene_camera;
	vec4_t           look = camera_object_look(cam);
	f32              z    = math_abs(look.z) - 1.0;
    (z < 0.0001 && z > -0.0001) ? viewport_orbit(0, math_pi()) : viewport_orbit(math_pi(), 0);
}

void viewport_zoom(f32 f) {
	camera_object_t *cam = scene_camera;
	transform_move(cam->base->transform, camera_object_look(cam), f);
	g_context->ddirty = 2;
}

void viewport_update_camera_type(i32 camera_type) {
	camera_object_t *cam = scene_cameras->buffer[0];
	if (camera_type == CAMERA_TYPE_PERSPECTIVE) {
		cam->data->ortho = NULL;
	}
	else {
		f32_array_t *f32a = f32_array_create(4);
		f32          f    = cam->data->fov * vec4_len(mat4_get_loc(cam->base->transform->world)) / 2.5;
		f32a->buffer[0]   = -2 * f;
		f32a->buffer[1]   = 2 * f;
		f32a->buffer[2]   = -2 * f * (sys_h() / (float)sys_w());
		f32a->buffer[3]   = 2 * f * (sys_h() / (float)sys_w());
		cam->data->ortho  = f32a;
	}
	camera_object_build_proj(cam, -1.0);
	g_context->ddirty = 2;
}

void viewport_save_texture(gpu_texture_t *screenshot) {
	// Save into the textures tab
	if (g_project->packed_assets == NULL) {
		g_project->packed_assets = any_array_create_from_raw((void *[]){}, 0);
	}

	i32   num = 0;
	char *abs = "/packed/screenshot0.png";
	for (i32 i = 0; i < g_project->packed_assets->length; ++i) {
		packed_asset_t *pa = g_project->packed_assets->buffer[i];
		if (string_equals(pa->name, abs)) {
			i = 0;
			num++;
			abs = string("/packed/screenshot%d.png", num);
		}
	}
	packed_asset_t *pa =
	    ALLOC_INIT(packed_asset_t, {.name = abs, .bytes = iron_encode_png(gpu_get_texture_pixels(screenshot), screenshot->width, screenshot->height, 0)});
	any_array_push(g_project->packed_assets, pa);
	any_map_set(data_cached_textures, abs, screenshot);
	import_texture_run(abs, true);
}

void viewport_capture_screenshot() {
	render_target_t *rt  = any_map_get(render_path_render_targets, "last");
	gpu_texture_t   *tex = rt->_image;

	gpu_texture_t *screenshot = gpu_create_render_target(tex->width, tex->height, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(screenshot, false, 0);
	draw_image(tex, 0, 0);
	draw_end();

	viewport_save_texture(screenshot);
	g_context->capturing_screenshot = false;
	g_context->ddirty               = 2;
}

void viewport_capture_screenshot_to(gpu_texture_t *target, float x, float y, float w, float h) {
	render_target_t *rt  = any_map_get(render_path_render_targets, "last");
	gpu_texture_t   *tex = rt->_image;

	// Crop the viewport texture to a square
	float size = tex->width < tex->height ? tex->width : tex->height;
	float sx   = (tex->width - size) / 2.0f;
	float sy   = (tex->height - size) / 2.0f;

	draw_begin(target, false, 0);
	draw_scaled_sub_image(tex, sx, sy, size, size, x, y, w, h);
	draw_end();

	g_context->capturing_screenshot = false;
	g_context->ddirty               = 2;
}

void viewport_capture_video_update(void *_) {
	render_target_t *rt     = any_map_get(render_path_render_targets, "last");
	buffer_t        *pixels = gpu_get_texture_pixels(rt->_image);
#ifdef IRON_BGRA
	buffer_bgra_swap(pixels);
#endif
	iron_mp4_encode(pixels);
}

void viewport_capture_video_begin() {
	if (string_equals(g_project->_->filepath, "")) {
		console_error(tr("Save project first"));
		return;
	}
	viewport_recording    = true;
	char            *path = string("%s/output.mp4", path_base_dir(g_project->_->filepath));
	render_target_t *rt   = any_map_get(render_path_render_targets, "last");
	iron_mp4_begin(path, rt->_image->width, rt->_image->height);
	sys_notify_on_update(viewport_capture_video_update, NULL);
}

void viewport_capture_video_end() {
	sys_remove_update(viewport_capture_video_update);
	iron_mp4_end();
	viewport_recording = false;
}
