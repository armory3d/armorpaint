
#include "global.h"

i32 uniforms_ext_i32_link(object_t *object, material_data_t *mat, char *link) {
	if (string_equals(link, "_bloom_current_mip")) {
		return render_path_base_bloom_current_mip;
	}
	else if (string_equals(link, "_sculpt_vertex_offset")) {
		return sculpt_object_vertex_offset(object->ext);
	}
	return INT_MAX;
}

f32 uniforms_ext_f32_link(object_t *object, material_data_t *mat, char *link) {
	if (string_equals(link, "_brush_radius")) {
		bool decal = context_is_decal();
		bool decal_mask =
		    decal && operator_shortcut(string("%s+%s", any_map_get(g_keymap, "decal_mask"), any_map_get(g_keymap, "action_paint")), SHORTCUT_TYPE_DOWN);
		f32  brush_decal_mask_radius = g_context->brush_decal_mask_radius;
		bool paint2d                 = g_context->paint2d || g_context->paint2d_view;
		brush_decal_mask_radius *= 2.0;
		f32 radius = decal_mask ? brush_decal_mask_radius : g_context->brush_radius;
		f32 val    = (radius * g_context->brush_nodes_radius) / 15.0;
		if (g_config->pressure_radius && (pen_down("tip") || pen_released("tip")) && !decal && !slot_layer_is_path(g_context->layer)) {
			val *= pen_pressure * g_config->pressure_sensitivity;
		}
		f32 scale2d = (900 / (float)base_h()) * g_config->window_scale;
		if (!decal) {
			val *= paint2d ? 0.5 * 0.5 * scale2d * ui_view2d_pan_scale : 2;
		}
		else if (decal_mask) {
			val *= paint2d ? 0.5 * 0.5 * scale2d * ui_view2d_pan_scale : scale2d;
		}
		else {
			val *= paint2d ? 0.5 * 0.5 * scale2d * ui_view2d_pan_scale : scale2d * 2.0;
		}
		return val;
	}
	else if (string_equals(link, "_vignette_strength")) {
		return g_config->rp_vignette;
	}
	else if (string_equals(link, "_grain_strength")) {
		return g_config->rp_grain;
	}
	else if (string_equals(link, "_contrast_strength")) {
		return g_config->rp_contrast;
	}
	else if (string_equals(link, "_gamma_strength")) {
		return g_config->rp_gamma;
	}
	else if (string_equals(link, "_tonemap_strength")) {
		bool tonemap = g_context->viewport_mode == VIEWPORT_MODE_LIT || g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE;
		return tonemap ? 1.0 : 0.0;
	}
	else if (string_equals(link, "_lut_size")) {
		return lut_image != NULL ? (f32)lut_size : 0.0;
	}
	else if (string_equals(link, "_bloom_sample_scale")) {
		return render_path_base_bloom_sample_scale;
	}
	else if (string_equals(link, "_bloom_strength")) {
		f32 base = g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE ? 0.2 : 0.02;
		return base * g_config->rp_bloom;
	}
	else if (string_equals(link, "_brush_scale_x")) {
		return 1 / (float)g_context->brush_scale_x;
	}
	else if (string_equals(link, "_brush_opacity")) {
		f32 val = g_context->brush_opacity * g_context->brush_nodes_opacity;
		if (g_config->pressure_opacity && (pen_down("tip") || pen_released("tip")) && !slot_layer_is_path(g_context->layer)) {
			val *= pen_pressure * g_config->pressure_sensitivity;
		}
		return val;
	}
	else if (string_equals(link, "_brush_hardness")) {
		bool decal_mask = context_is_decal_mask_paint();
		if (g_context->tool != TOOL_TYPE_BRUSH && g_context->tool != TOOL_TYPE_ERASER && g_context->tool != TOOL_TYPE_CLONE && !decal_mask) {
			return 1.0;
		}
		f32 val = fmaxf((g_context->brush_hardness * g_context->brush_nodes_hardness) - 0.02, 0.0);
		if (g_config->pressure_hardness && (pen_down("tip") || pen_released("tip")) && !slot_layer_is_path(g_context->layer)) {
			val *= pen_pressure * g_config->pressure_sensitivity;
		}
		val *= val;
		if (g_config->workflow == WORKFLOW_SCULPT) {
			val *= 0.8;
		}
		return val;
	}
	else if (string_equals(link, "_brush_scale")) {
		// if (g_context->tool == TOOL_TYPE_CURSOR) {
		// 	i32 atlas_w      = config_get_scene_atlas_res();
		// 	i32 item_w       = config_get_layer_res();
		// 	i32 atlas_stride = atlas_w / (float)item_w;
		// 	return atlas_stride;
		// }
		bool fill = g_context->layer->fill_material != NULL;
		f32  val  = (fill ? g_context->layer->scale : g_context->brush_scale) * g_context->brush_nodes_scale;
		return val;
	}
	else if (string_equals(link, "_object_id")) {
		return array_index_of(g_project->_->paint_objects, object->ext);
	}
	else if (string_equals(link, "_sculpt_mask_offset")) {
		i32 om = slot_layer_get_object_mask(g_context->layer);
		if (om <= 0 || om > g_project->_->paint_objects->length) {
			return 0;
		}
		return sculpt_object_vertex_offset(g_project->_->paint_objects->buffer[om - 1]);
	}
	else if (string_equals(link, "_sculpt_mask_count")) {
		i32 om = slot_layer_get_object_mask(g_context->layer);
		if (om <= 0 || om > g_project->_->paint_objects->length) {
			return config_get_texture_res_x() * config_get_texture_res_y();
		}
		return g_project->_->paint_objects->buffer[om - 1]->data->index_array->length;
	}
	else if (string_equals(link, "_dilate_radius")) {
		return util_uv_dilatemap != NULL ? g_config->dilate_radius : 0.0;
	}
	else if (string_equals(link, "_particle_radius")) {
		i32 idx           = g_context->particle_index;
		f32 speed         = (physics_world_active != NULL && g_context->particles[idx].body != NULL)
		                        ? physics_world_get_speed(physics_world_active, g_context->particles[idx].body)
		                        : 0.0f;
		f32 vel_scale     = fminf(speed / 0.12f, 1.0f);
		f32 contact_scale = 1.0f - fminf(g_context->particles[idx].contact_time, 1.0f);
		return fmaxf(g_context->brush_radius * vel_scale * contact_scale, 0.1f);
	}
	else if (string_equals(link, "_decal_layer_dim")) {
		vec4_t sc = mat4_get_scale(g_context->layer->decal_mat);
		return sc.z * 0.5;
	}
	else if (string_equals(link, "_brush_camera_align")) {
		return context_is_brush_camera_align() ? 1.0f : 0.0f;
	}
	else if (string_equals(link, "_picker_opacity")) {
		return g_context->picked_color->opacity;
	}
	else if (string_equals(link, "_picker_occlusion")) {
		return g_context->picked_color->occlusion;
	}
	else if (string_equals(link, "_picker_roughness")) {
		return g_context->picked_color->roughness;
	}
	else if (string_equals(link, "_picker_metallic")) {
		return g_context->picked_color->metallic;
	}
	else if (string_equals(link, "_picker_height")) {
		return g_context->picked_color->height;
	}
	else if (string_equals(link, "_taa_blend")) {
		return scene_camera->frame == 0 ? 0.0 : 0.5;
	}
	if (parser_material_script_links != NULL) {
		string_array_t *keys = map_keys(parser_material_script_links);
		for (i32 i = 0; i < keys->length; ++i) {
			char *key    = keys->buffer[i];
			char *script = any_map_get(parser_material_script_links, key);
			f32   result = NAN;
			if (script != NULL) {
				result = 0.0;
			}
			if (!string_equals(script, "")) {
				minic_ctx_t *_ctx = minic_eval(string("float main() { return %s; }", script));
				result            = minic_ctx_result(_ctx);
				minic_ctx_free(_ctx);
			}
			return result;
		}
	}
	return NAN;
}

vec2_t uniforms_ext_vec2_link(object_t *object, material_data_t *mat, char *link) {
	if (string_equals(link, "_gbuffer_size")) {
		render_target_t *gbuffer2 = any_map_get(render_path_render_targets, "gbuffer2");
		return (vec2_t){gbuffer2->_image->width, gbuffer2->_image->height};
	}
	else if (string_equals(link, "_clone_delta")) {
		return (vec2_t){g_context->clone_delta_x, g_context->clone_delta_y};
	}
	else if (string_equals(link, "_grab_start")) {
		return (vec2_t){g_context->grab_start_x, g_context->grab_start_y};
	}
	else if (string_equals(link, "_texpaint_size")) {
		return (vec2_t){config_get_texture_res_x(), config_get_texture_res_y()};
	}
	else if (string_equals(link, "_brush_angle")) {
		f32 brush_angle = g_context->brush_angle + g_context->brush_nodes_angle;
		f32 angle       = g_context->layer->fill_material != NULL ? g_context->layer->angle : brush_angle;
		angle *= (math_pi() / 180.0);
		if (g_config->pressure_angle && (pen_down("tip") || pen_released("tip")) && !slot_layer_is_path(g_context->layer)) {
			angle *= pen_pressure * g_config->pressure_sensitivity;
		}
		return (vec2_t){math_cos(-angle), math_sin(-angle)};
	}
	return vec2_nan();
}

f32 uniforms_ext_vec2d(f32 x) {
	// Transform from 3d viewport coord to 2d view coord
	g_context->paint2d_view = false;
	f32 res                 = (x * base_w() - base_w()) / (float)ui_view2d_ww;
	g_context->paint2d_view = true;
	return res;
}

vec4_t uniforms_ext_vec3_link(object_t *object, material_data_t *mat, char *link) {
	vec4_t v = vec4_nan();
	if (string_equals(link, "_brush_direction")) {
		// Discard first paint for directional brush (no prev position yet)
		bool allow_paint = g_context->prev_paint_vec_x > 0 && g_context->prev_paint_vec_y > 0 &&
		                   (g_context->prev_paint_vec_x != g_context->paint_vec.x || g_context->prev_paint_vec_y != g_context->paint_vec.y);
		f32 x     = g_context->paint_vec.x;
		f32 y     = g_context->paint_vec.y;
		f32 lastx = g_context->prev_paint_vec_x;
		f32 lasty = g_context->prev_paint_vec_y;
		if (g_context->paint2d) {
			x     = uniforms_ext_vec2d(x);
			lastx = uniforms_ext_vec2d(lastx);
		}
		f32 angle                   = math_atan2(-y + lasty, x - lastx) - math_pi() / 2.0;
		v                           = (vec4_t){math_cos(angle), math_sin(angle), allow_paint ? 1 : 0, 1.0};
		g_context->prev_paint_vec_x = g_context->last_paint_vec_x;
		g_context->prev_paint_vec_y = g_context->last_paint_vec_y;
		return v;
	}
	else if (string_equals(link, "_decal_layer_loc")) {
		v = (vec4_t){g_context->layer->decal_mat.m30, g_context->layer->decal_mat.m31, g_context->layer->decal_mat.m32, 1.0};
		return v;
	}
	else if (string_equals(link, "_decal_layer_nor")) {
		v = (vec4_t){g_context->layer->decal_mat.m20, g_context->layer->decal_mat.m21, g_context->layer->decal_mat.m22, 1.0};
		v = vec4_norm(v);
		return v;
	}
	else if (string_equals(link, "_picker_base")) {
		v = (vec4_t){color_get_rb(g_context->picked_color->base) / 255.0, color_get_gb(g_context->picked_color->base) / 255.0,
		             color_get_bb(g_context->picked_color->base) / 255.0, 1.0};
		return v;
	}
	else if (string_equals(link, "_picker_normal")) {
		v = (vec4_t){color_get_rb(g_context->picked_color->normal) / 255.0, color_get_gb(g_context->picked_color->normal) / 255.0,
		             color_get_bb(g_context->picked_color->normal) / 255.0, 1.0};
		return v;
	}
	else if (string_equals(link, "_particle_hit")) {
		v = (vec4_t){g_context->particle_hit_x, g_context->particle_hit_y, g_context->particle_hit_z, 1.0};
		return v;
	}
	else if (string_equals(link, "_particle_hit_last")) {
		v = (vec4_t){g_context->last_particle_hit_x, g_context->last_particle_hit_y, g_context->last_particle_hit_z, 1.0};
		return v;
	}
	else if (string_equals(link, "_camera_right")) {
		v = camera_object_right_world(scene_camera);
		return v;
	}
	else if (string_equals(link, "_camera_up")) {
		v = camera_object_up_world(scene_camera);
		return v;
	}
	return v;
}

vec4_t uniforms_ext_vec4_link(object_t *object, material_data_t *mat, char *link) {
	if (string_equals(link, "_input_brush")) {
		bool   down = mouse_down("left") || pen_down("tip");
		vec4_t v    = (vec4_t){g_context->paint_vec.x, g_context->paint_vec.y, down ? 1.0 : 0.0, g_context->paint2d ? 1.0 : 0.0};
		if (g_context->paint2d) {
			v.x = uniforms_ext_vec2d(v.x);
		}

		return v;
	}
	else if (string_equals(link, "_input_brush_last")) {
		bool   down = mouse_down("left") || pen_down("tip");
		vec4_t v    = (vec4_t){g_context->last_paint_vec_x, g_context->last_paint_vec_y, down ? 1.0 : 0.0, g_context->paint2d ? 1.0 : 0.0};
		if (g_context->paint2d) {
			v.x = uniforms_ext_vec2d(v.x);
		}

		return v;
	}
	else if (string_equals(link, "_envmap_data")) {
		return (vec4_t){g_context->envmap_angle, math_sin(-g_context->envmap_angle), math_cos(-g_context->envmap_angle), scene_world->strength * 2.0};
	}
	else if (string_equals(link, "_envmap_data_world")) {
		bool tonemap = g_context->viewport_mode == VIEWPORT_MODE_LIT || g_context->viewport_mode == VIEWPORT_MODE_PATH_TRACE;
		return (vec4_t){g_context->envmap_angle, tonemap ? 0.0 : 1.0, 0.0, g_context->show_envmap ? scene_world->strength : 1.0};
	}
	else if (string_equals(link, "_stencil_transform")) {
		vec4_t v = (vec4_t){g_context->brush_stencil_x, g_context->brush_stencil_y, g_context->brush_stencil_scale, g_context->brush_stencil_angle};
		if (g_context->paint2d) {
			v.x = uniforms_ext_vec2d(v.x);
		}

		return v;
	}
	else if (string_equals(link, "_decal_mask")) {
		bool decal_mask = context_is_decal_mask_paint();
		f32  val        = (g_context->brush_radius * g_context->brush_nodes_radius) / 15.0;
		f32  scale2d    = (900 / (float)base_h()) * g_config->window_scale;
		val *= g_context->paint2d ? 0.5 * 0.5 * scale2d * ui_view2d_pan_scale : scale2d * 2.0;
		vec4_t v = (vec4_t){g_context->decal_x, g_context->decal_y, decal_mask ? 1 : 0, val};
		if (g_context->paint2d) {
			v.x = uniforms_ext_vec2d(v.x);
		}
		return v;
	}
	else if (string_equals(link, "_select_mask")) {
		return (vec4_t){g_context->select_x1, g_context->select_y1, g_context->select_x2, g_context->select_y2};
	}

	return vec4_nan();
}

mat4_t uniforms_ext_mat4_link(object_t *object, material_data_t *mat, char *link) {
	if (string_equals(link, "_sculpt_symmetry_reflect")) {
		transform_t *t       = object->transform;
		mat4_t       W       = t->world;
		vec4_t       axes[3] = {
            vec4_norm((vec4_t){W.m00, W.m10, W.m20, 0.0}),
            vec4_norm((vec4_t){W.m01, W.m11, W.m21, 0.0}),
            vec4_norm((vec4_t){W.m02, W.m12, W.m22, 0.0}),
        };
		f32    scale[3] = {t->scale.x, t->scale.y, t->scale.z};
		mat4_t F        = mat4_identity();
		for (i32 i = 0; i < 3; ++i) {
			if (scale[i] < 0.0f) {
				vec4_t a = axes[i];
				F.m00 -= 2.0f * a.x * a.x;
				F.m01 -= 2.0f * a.x * a.y;
				F.m02 -= 2.0f * a.x * a.z;
				F.m10 -= 2.0f * a.y * a.x;
				F.m11 -= 2.0f * a.y * a.y;
				F.m12 -= 2.0f * a.y * a.z;
				F.m20 -= 2.0f * a.z * a.x;
				F.m21 -= 2.0f * a.z * a.y;
				F.m22 -= 2.0f * a.z * a.z;
			}
		}
		return F;
	}
	if (string_equals(link, "_decal_layer_matrix")) { // Decal layer
		mat4_t m            = mat4_inv(g_context->layer->decal_mat);
		f32    parent_scale = object->parent != NULL ? object->parent->transform->scale.x : 1.0;
		f32    f            = parent_scale * object->transform->scale_world;
		m                   = mat4_scale(m, (vec4_t){f, f, f, 1.0});
		m                   = mat4_mult_mat(m, uniforms_ext_ortho_p);
		return m;
	}

	return mat4_nan();
}

void uniforms_ext_cache_uv_island_map(void *_) {
	util_uv_cache_uv_island_map();
}

void uniforms_ext_cache_triangle_map(void *_) {
	util_uv_cache_triangle_map();
}

void uniforms_ext_cache_uv_map(void *_) {
	util_uv_cache_uv_map();
}

gpu_texture_t *uniforms_ext_tex_link(object_t *object, material_data_t *mat, char *link) {
	if (string_equals(link, "_texpaint_undo")) {
		i32              i  = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
		render_target_t *rt = any_map_get(render_path_render_targets, string("texpaint_undo%d", i));
		return rt->_image;
	}
	else if (string_equals(link, "_texpaint_nor_undo")) {
		i32              i  = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
		render_target_t *rt = any_map_get(render_path_render_targets, string("texpaint_nor_undo%d", i));
		return rt->_image;
	}
	else if (string_equals(link, "_texpaint_pack_undo")) {
		i32              i  = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
		render_target_t *rt = any_map_get(render_path_render_targets, string("texpaint_pack_undo%d", i));
		return rt->_image;
	}
	else if (string_equals(link, "_texpaint_sculpt_undo")) {
		// Per-frame accumulation reference
		render_target_t *rt = any_map_get(render_path_render_targets, "texpaint_sculpt_ref");
		return rt->_image;
	}
	else if (string_equals(link, "_texcolorid")) {
		if (g_project->_->assets->length == 0) {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_white");
			return rt->_image;
		}
		else {
			return project_get_image(g_project->_->assets->buffer[g_context->colorid]);
		}
	}
	else if (string_equals(link, "_textexttool")) { // Opacity map for text
		return g_context->text_tool_image;
	}
	else if (string_equals(link, "_texbrushmask")) {
		return g_context->brush_mask_image;
	}
	else if (string_equals(link, "_texbrushstencil")) {
		return g_context->brush_stencil_image;
	}
	else if (string_equals(link, "_texuvmap")) {
		if (!util_uv_uvmap_cached) {
			sys_notify_on_next_frame(&uniforms_ext_cache_uv_map, NULL);
		}
		return util_uv_uvmap;
	}
	else if (string_equals(link, "_textrianglemap")) {
		if (!util_uv_trianglemap_cached) {
			sys_notify_on_next_frame(&uniforms_ext_cache_triangle_map, NULL);
		}
		return util_uv_trianglemap;
	}
	else if (string_equals(link, "_texuvislandmap")) {
		sys_notify_on_next_frame(&uniforms_ext_cache_uv_island_map, NULL);
		if (util_uv_uvislandmap_cached) {
			return util_uv_uvislandmap;
		}
		else {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	else if (string_equals(link, "_texdilatemap")) {
		return util_uv_dilatemap;
	}
	if (starts_with(link, "_texpaint_pack_vert")) {
		char            *tid = substring(link, string_length(link) - 1, string_length(link));
		render_target_t *rt  = any_map_get(render_path_render_targets, string("texpaint_pack%s", tid));
		return rt->_image;
	}
	if (starts_with(link, "_texpaint_vert")) {
		i32 tid = parse_int(substring(link, string_length("_texpaint_vert"), string_length(link)));
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			if (g_project->_->layers->buffer[i]->id == tid) {
				return g_project->_->layers->buffer[i]->texpaint;
			}
		}
		return NULL;
	}
	if (starts_with(link, "_texpaint_nor")) {
		i32 tid = parse_int(substring(link, string_length(link) - 1, string_length(link)));
		return tid < g_project->_->layers->length ? g_project->_->layers->buffer[tid]->texpaint_nor : NULL;
	}
	if (starts_with(link, "_texpaint_pack")) {
		i32 tid = parse_int(substring(link, string_length(link) - 1, string_length(link)));
		return tid < g_project->_->layers->length ? g_project->_->layers->buffer[tid]->texpaint_pack : NULL;
	}
	if (string_equals(link, "_texpaint_sculpt_base")) {
		render_target_t *rt = any_map_get(render_path_render_targets, "texpaint_sculpt_base");
		return rt != NULL ? rt->_image : NULL;
	}
	if (starts_with(link, "_texpaint_sculpt")) {
		i32 tid = parse_int(substring(link, string_length(link) - 1, string_length(link)));
		return tid < g_project->_->layers->length ? g_project->_->layers->buffer[tid]->texpaint_sculpt : NULL;
	}
	if (starts_with(link, "_texpaint")) {
		i32 tid = parse_int(substring(link, string_length(link) - 1, string_length(link)));
		return tid < g_project->_->layers->length ? g_project->_->layers->buffer[tid]->texpaint : NULL;
	}
	if (starts_with(link, "_texblur_")) {
		char *id = substring(link, 9, string_length(link));
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	if (starts_with(link, "_texwarp_")) {
		char *id = substring(link, 9, string_length(link));
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	if (starts_with(link, "_texbake_")) {
		char *id = substring(link, 9, string_length(link));
		if (g_context->node_previews != NULL) {
			return any_map_get(g_context->node_previews, id);
		}
		else {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_black");
			return rt->_image;
		}
	}
	if (string_equals(link, "_camera_texture")) {
		render_target_t *rt = any_map_get(render_path_render_targets, "last");
		return rt->_image;
	}
	if (string_equals(link, "_lut_tex")) {
		if (lut_image == NULL) {
			render_target_t *rt = any_map_get(render_path_render_targets, "empty_white");
			return rt->_image;
		}
		return lut_image;
	}
	return NULL;
}

void uniforms_ext_init() {
	gc_unroot(uniforms_i32_links);
	uniforms_i32_links = uniforms_ext_i32_link;
	gc_root(uniforms_i32_links);
	gc_unroot(uniforms_f32_links);
	uniforms_f32_links = uniforms_ext_f32_link;
	gc_root(uniforms_f32_links);
	gc_unroot(uniforms_vec2_links);
	uniforms_vec2_links = uniforms_ext_vec2_link;
	gc_root(uniforms_vec2_links);
	gc_unroot(uniforms_vec3_links);
	uniforms_vec3_links = uniforms_ext_vec3_link;
	gc_root(uniforms_vec3_links);
	gc_unroot(uniforms_vec4_links);
	uniforms_vec4_links = uniforms_ext_vec4_link;
	gc_root(uniforms_vec4_links);
	gc_unroot(uniforms_mat4_links);
	uniforms_mat4_links = uniforms_ext_mat4_link;
	gc_root(uniforms_mat4_links);
	gc_unroot(uniforms_tex_links);
	uniforms_tex_links = uniforms_ext_tex_link;
	gc_root(uniforms_tex_links);
}
