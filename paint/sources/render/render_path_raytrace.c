
#include "../global.h"

f32           render_path_raytrace_uv_scale = 1.0;
mat4_t        render_path_raytrace_transform;
gpu_buffer_t *render_path_raytrace_vb;
gpu_buffer_t *render_path_raytrace_ib;

void render_path_raytrace_init() {}

void render_path_raytrace_commands(bool use_live_layer) {
	if (!render_path_raytrace_ready || render_path_raytrace_is_bake) {
		render_path_raytrace_ready = true;
		if (render_path_raytrace_is_bake) {
			render_path_raytrace_is_bake     = false;
			render_path_raytrace_init_shader = true;
		}
		char *ext = "";
		// if (g_context->tool == TOOL_TYPE_CURSOR) {
		// 	ext = "forge_";
		// }
		char *mode = g_config->pathtrace_mode == PATHTRACE_MODE_FAST ? "core" : "full";
		render_path_raytrace_raytrace_init(string("raytrace_brute_%s%s%s", ext, mode, render_path_raytrace_ext), true);
		gc_unroot(render_path_raytrace_last_envmap);
		render_path_raytrace_last_envmap = NULL;
	}

	if (!g_context->envmap_loaded) {
		context_load_envmap();
		context_update_envmap();
	}

	world_data_t  *probe        = scene_world;
	gpu_texture_t *saved_envmap = g_context->show_envmap_blur ? probe->_->radiance_mipmaps->buffer[0] : g_context->saved_envmap;

	////
	if (render_path_raytrace_last_envmap != saved_envmap) {
		gc_unroot(render_path_raytrace_last_envmap);
		render_path_raytrace_last_envmap = saved_envmap;
		gc_root(render_path_raytrace_last_envmap);

		gpu_texture_t *bnoise_sobol    = any_map_get(scene_embedded, "bnoise_sobol.k");
		gpu_texture_t *bnoise_scramble = any_map_get(scene_embedded, "bnoise_scramble.k");
		gpu_texture_t *bnoise_rank     = any_map_get(scene_embedded, "bnoise_rank.k");

		slot_layer_t *l = layers_flatten(true, NULL);
		gpu_raytrace_set_textures(l->texpaint, l->texpaint_nor, l->texpaint_pack, saved_envmap, bnoise_sobol, bnoise_scramble, bnoise_rank);
	}
	////

	bool is_live = g_config->brush_live && render_path_paint_live_layer_drawn > 0 && render_path_paint_live_layer != NULL;
	if (g_context->pdirty > 0 || g_context->rtdirty > 0 || is_live) {
		slot_layer_t  *layer          = g_context->layer;
		gpu_texture_t *_texpaint      = layer->texpaint;
		gpu_texture_t *_texpaint_nor  = layer->texpaint_nor;
		gpu_texture_t *_texpaint_pack = layer->texpaint_pack;
		if (is_live) {
			layer->texpaint = render_path_paint_live_layer->texpaint;
			if (slot_layer_is_layer(layer)) {
				layer->texpaint_nor  = render_path_paint_live_layer->texpaint_nor;
				layer->texpaint_pack = render_path_paint_live_layer->texpaint_pack;
			}
		}
		layers_flatten(true, NULL);
		if (is_live) {
			layer->texpaint      = _texpaint;
			layer->texpaint_nor  = _texpaint_nor;
			layer->texpaint_pack = _texpaint_pack;
		}
		g_context->rtdirty = 0;
	}

	camera_object_t *cam                 = scene_camera;
	transform_t     *ct                  = cam->base->transform;
	render_path_raytrace_help_mat        = cam->v;
	render_path_raytrace_help_mat        = mat4_mult_mat(render_path_raytrace_help_mat, cam->p);
	render_path_raytrace_help_mat        = mat4_inv(render_path_raytrace_help_mat);
	render_path_raytrace_f32a->buffer[0] = transform_world_x(ct);
	render_path_raytrace_f32a->buffer[1] = transform_world_y(ct);
	render_path_raytrace_f32a->buffer[2] = transform_world_z(ct);
	render_path_raytrace_f32a->buffer[3] = render_path_raytrace_frame;
#ifdef IRON_METAL
	// render_path_raytrace_frame = (render_path_raytrace_frame % (16)) + 1; // _PAINT
	render_path_raytrace_frame = render_path_raytrace_frame + 1; // _RENDER
#else
	render_path_raytrace_frame = (render_path_raytrace_frame % 4) + 1; // _PAINT
// render_path_raytrace_frame = render_path_raytrace_frame + 1; // _RENDER
#endif
	render_path_raytrace_f32a->buffer[4]  = render_path_raytrace_help_mat.m00;
	render_path_raytrace_f32a->buffer[5]  = render_path_raytrace_help_mat.m01;
	render_path_raytrace_f32a->buffer[6]  = render_path_raytrace_help_mat.m02;
	render_path_raytrace_f32a->buffer[7]  = render_path_raytrace_help_mat.m03;
	render_path_raytrace_f32a->buffer[8]  = render_path_raytrace_help_mat.m10;
	render_path_raytrace_f32a->buffer[9]  = render_path_raytrace_help_mat.m11;
	render_path_raytrace_f32a->buffer[10] = render_path_raytrace_help_mat.m12;
	render_path_raytrace_f32a->buffer[11] = render_path_raytrace_help_mat.m13;
	render_path_raytrace_f32a->buffer[12] = render_path_raytrace_help_mat.m20;
	render_path_raytrace_f32a->buffer[13] = render_path_raytrace_help_mat.m21;
	render_path_raytrace_f32a->buffer[14] = render_path_raytrace_help_mat.m22;
	render_path_raytrace_f32a->buffer[15] = render_path_raytrace_help_mat.m23;
	render_path_raytrace_f32a->buffer[16] = render_path_raytrace_help_mat.m30;
	render_path_raytrace_f32a->buffer[17] = render_path_raytrace_help_mat.m31;
	render_path_raytrace_f32a->buffer[18] = render_path_raytrace_help_mat.m32;
	render_path_raytrace_f32a->buffer[19] = render_path_raytrace_help_mat.m33;
	render_path_raytrace_f32a->buffer[20] = scene_world->strength;
	if (!g_context->show_envmap) {
		render_path_raytrace_f32a->buffer[20] = -render_path_raytrace_f32a->buffer[20];
	}
	render_path_raytrace_f32a->buffer[21] = g_context->envmap_angle;
	render_path_raytrace_f32a->buffer[22] = render_path_raytrace_uv_scale;

	if (render_path_base_buf_swapped) {
		render_path_base_swap_buf("buf");
	}

	render_target_t *framebuffer = any_map_get(render_path_render_targets, "buf");
	_gpu_raytrace_dispatch_rays(framebuffer->_image, render_path_raytrace_f32a);

	if (g_context->ddirty == 1 || g_context->pdirty == 1) {
#ifdef IRON_METAL
		g_context->rdirty = 128;
#else
		g_context->rdirty = 4;
#endif
	}
	g_context->ddirty--;
	g_context->pdirty--;
	g_context->rdirty--;

	// g_context->ddirty = 1; // _RENDER
}

void render_path_raytrace_build_data() {
	if (g_context->merged_object == NULL) {
		util_mesh_merge(NULL);
	}

	mesh_object_t *mo = !context_layer_filter_used() ? g_context->merged_object : g_context->paint_object;

	// if (g_context->tool == TOOL_TYPE_CURSOR) {
	// 	render_path_raytrace_transform = mo->base->transform->world_unpack;
	// }
	// else {
	render_path_raytrace_transform = mat4_identity();
	// }

	f32 sc = mo->base->transform->scale.x * mo->data->scale_pos;
	if (mo->base->parent != NULL) {
		sc *= mo->base->parent->transform->scale.x;
	}
	render_path_raytrace_transform = mat4_scale(render_path_raytrace_transform, (vec4_t){sc, sc, sc, 1.0});

	gc_unroot(render_path_raytrace_vb);
	render_path_raytrace_vb = mo->data->_->vertex_buffer;
	gc_root(render_path_raytrace_vb);
	gc_unroot(render_path_raytrace_ib);
	render_path_raytrace_ib = mo->data->_->index_buffer;
	gc_root(render_path_raytrace_ib);
}

void render_path_raytrace_raytrace_init(char *shader_name, bool build) {
	if (render_path_raytrace_init_shader) {
		render_path_raytrace_init_shader = false;
		scene_embed_data("bnoise_sobol.k");
		scene_embed_data("bnoise_scramble.k");
		scene_embed_data("bnoise_rank.k");

		buffer_t *shader = data_get_blob(shader_name);
		_gpu_raytrace_init(shader);
	}

	if (build) {
		render_path_raytrace_build_data();
	}

	{
		_gpu_raytrace_as_init();

		// if (g_context->tool == TOOL_TYPE_CURSOR) {
		// 	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		// 		mesh_object_t *po = g_project->_->paint_objects->buffer[i];
		// 		if (!po->base->visible) {
		// 			continue;
		// 		}
		// 		_gpu_raytrace_as_add(po->data->_->vertex_buffer, po->data->_->index_buffer, po->base->transform->world_unpack);
		// 	}
		// }
		// else {
		_gpu_raytrace_as_add(render_path_raytrace_vb, render_path_raytrace_ib, render_path_raytrace_transform);
		// }

		gpu_buffer_t *vb_full = g_context->merged_object->data->_->vertex_buffer;
		gpu_buffer_t *ib_full = g_context->merged_object->data->_->index_buffer;

		_gpu_raytrace_as_build(vb_full, ib_full);
	}
}

void render_path_raytrace_draw(bool use_live_layer) {
	bool is_live = g_config->brush_live && render_path_paint_live_layer_drawn > 0;
	if (g_context->ddirty > 1 || g_context->pdirty > 0 || is_live) {
		render_path_raytrace_frame = 0;
	}

#ifdef IRON_METAL
	// Delay path tracing additional samples while painting
	bool down = mouse_down("left") || pen_down("tip");
	if (context_in_3d_view() && down && !ui_menu_show) {
		render_path_raytrace_frame = 0;
	}
#endif

	render_path_raytrace_commands(use_live_layer);
	render_path_set_target("buf", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_draw_meshes("overlay");
	render_path_set_target("buf", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_compass();
	render_path_set_target("last", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("buf", "tex");
	render_path_draw_shader("Scene/compositor_pass/compositor_pass");
	render_path_base_draw_bloom("buf", "last");
	render_path_set_target("last", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_envsphere();
	render_pathsphere();
	render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("last", "tex");
	render_path_draw_shader("Scene/copy_pass/copy_pass");
	render_path_paint_commands_cursor();
}
