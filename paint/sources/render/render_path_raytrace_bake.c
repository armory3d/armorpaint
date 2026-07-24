
#include "../global.h"

f32            render_path_raytrace_bake_rays_timer      = 0.0;
i32            render_path_raytrace_bake_rays_counter    = 0;
gpu_texture_t *render_path_raytrace_bake_last_layer      = NULL;
i32            render_path_raytrace_bake_last_bake_type  = 0;
i32            render_path_raytrace_bake_last_bake_type2 = 0;

void render_path_raytrace_bake_commands_parse_paint_material(void (*parse_paint_material)(bool)) {
	parse_paint_material(true);
}

char *render_path_raytrace_bake_get_bake_shader_name() {
	return g_context->bake_type == BAKE_TYPE_OCCLUSION     ? string("raytrace_bake_ao%s", render_path_raytrace_ext)
	       : g_context->bake_type == BAKE_TYPE_LIGHTMAP    ? string("raytrace_bake_light%s", render_path_raytrace_ext)
	       : g_context->bake_type == BAKE_TYPE_BENT_NORMAL ? string("raytrace_bake_bent%s", render_path_raytrace_ext)
	                                                       : string("raytrace_bake_thick%s", render_path_raytrace_ext);
}

bool render_path_raytrace_bake_commands(void (*parse_paint_material)(bool)) {

	if (!render_path_raytrace_ready || !render_path_raytrace_is_bake || render_path_raytrace_bake_last_bake_type != g_context->bake_type) {

		bool rebuild = !(render_path_raytrace_ready && render_path_raytrace_is_bake && render_path_raytrace_bake_last_bake_type != g_context->bake_type);
		render_path_raytrace_bake_last_bake_type = g_context->bake_type;
		render_path_raytrace_ready               = true;
		render_path_raytrace_is_bake             = true;
		gc_unroot(render_path_raytrace_last_envmap);
		render_path_raytrace_last_envmap = NULL;
		gc_unroot(render_path_raytrace_bake_last_layer);
		render_path_raytrace_bake_last_layer = NULL;

		if (any_map_get(render_path_render_targets, "baketex0") != NULL) {
			render_target_t *baketex0 = any_map_get(render_path_render_targets, "baketex0");
			render_target_t *baketex1 = any_map_get(render_path_render_targets, "baketex1");
			render_target_t *baketex2 = any_map_get(render_path_render_targets, "baketex2");
			gpu_delete_texture(baketex0->_image);
			gpu_delete_texture(baketex1->_image);
			gpu_delete_texture(baketex2->_image);
		}

		{
			render_target_t *t = render_target_create();
			t->name            = "baketex0";
			t->width           = config_get_texture_res_x();
			t->height          = config_get_texture_res_y();
			t->format          = "RGBA64";
			render_path_create_render_target(t);
		}
		{
			render_target_t *t = render_target_create();
			t->name            = "baketex1";
			t->width           = config_get_texture_res_x();
			t->height          = config_get_texture_res_y();
			t->format          = "RGBA64";
			render_path_create_render_target(t);
		}
		{
			render_target_t *t = render_target_create();
			t->name            = "baketex2";
			t->width           = config_get_texture_res_x();
			t->height          = config_get_texture_res_y();
			t->format          = "RGBA64"; // Match raytrace_target format
			render_path_create_render_target(t);
		}

		bake_type_t _bake_type = g_context->bake_type;
		g_context->bake_type   = BAKE_TYPE_INIT;
		parse_paint_material(false);
		render_path_set_target("baketex0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		// Pixels with alpha of 0.0 are skipped during raytracing
		string_array_t *additional = any_array_create_from_raw(
		    (void *[]){
		        "baketex1",
		    },
		    1);
		render_path_set_target("baketex0", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_draw_meshes("paint");
		g_context->bake_type = _bake_type;
		sys_notify_on_next_frame(&render_path_raytrace_bake_commands_parse_paint_material, parse_paint_material);

		render_path_raytrace_init_shader = true;
		render_path_raytrace_raytrace_init(render_path_raytrace_bake_get_bake_shader_name(), rebuild);

		return false;
	}

	if (!g_context->envmap_loaded) {
		context_load_envmap();
		context_update_envmap();
	}

	world_data_t  *probe        = scene_world;
	gpu_texture_t *saved_envmap = g_context->show_envmap_blur ? probe->_->radiance_mipmaps->buffer[0] : g_context->saved_envmap;

	if (render_path_raytrace_last_envmap != saved_envmap || render_path_raytrace_bake_last_layer != g_context->layer->texpaint ||
	    render_path_raytrace_bake_last_bake_type2 != g_context->bake_type || g_context->rtdirty > 0) {

		g_context->rtdirty = 0;

		gc_unroot(render_path_raytrace_last_envmap);
		render_path_raytrace_last_envmap = saved_envmap;
		gc_root(render_path_raytrace_last_envmap);
		gc_unroot(render_path_raytrace_bake_last_layer);
		render_path_raytrace_bake_last_layer = g_context->layer->texpaint;
		gc_root(render_path_raytrace_bake_last_layer);
		render_path_raytrace_bake_last_bake_type2 = g_context->bake_type;

		gpu_texture_t *bnoise_sobol    = any_map_get(scene_embedded, "bnoise_sobol.k");
		gpu_texture_t *bnoise_scramble = any_map_get(scene_embedded, "bnoise_scramble.k");
		gpu_texture_t *bnoise_rank     = any_map_get(scene_embedded, "bnoise_rank.k");

		render_target_t *baketex0 = any_map_get(render_path_render_targets, "baketex0");
		render_target_t *baketex1 = any_map_get(render_path_render_targets, "baketex1");

		gpu_texture_t *tex2 = NULL;
		if (g_context->bake_type == BAKE_TYPE_LIGHTMAP) {
			gpu_texture_t *_texpaint   = g_context->layer->texpaint;
			g_context->layer->texpaint = bake_texture_node_texpaint;
			slot_layer_t *flat         = layers_flatten(true, NULL);
			tex2                       = flat->texpaint;
			g_context->layer->texpaint = _texpaint;
		}
		else {
			render_target_t *texpaint_undo = any_map_get(render_path_render_targets, string("texpaint_undo%d", history_undo_i));
			tex2                           = texpaint_undo->_image;
		}

		gpu_raytrace_set_textures(baketex0->_image, baketex1->_image, tex2, saved_envmap, bnoise_sobol, bnoise_scramble, bnoise_rank);
	}

	if (g_context->brush_time > 0) {
		g_context->pdirty = 2;
		g_context->rdirty = 2;
	}

	if (g_context->pdirty > 0) {
		f32_array_t *f32a = render_path_raytrace_f32a;
		f32a->buffer[0]   = render_path_raytrace_frame++;
		f32a->buffer[1]   = g_context->bake_ao_strength;
		f32a->buffer[2]   = g_context->bake_ao_radius;
		f32a->buffer[3]   = g_context->bake_ao_offset;
		f32a->buffer[4]   = scene_world->strength;
		f32a->buffer[5]   = g_context->bake_up_axis;
		f32a->buffer[6]   = g_context->envmap_angle;

		render_target_t *framebuffer = any_map_get(render_path_render_targets, "baketex2");
		_gpu_raytrace_dispatch_rays(framebuffer->_image, f32a);

		i32   id          = g_context->layer->id;
		char *texpaint_id = string("texpaint%d", id);
		render_path_set_target(texpaint_id, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("baketex2", "tex");
		render_path_draw_shader("Scene/copy_pass/copy_pass");

#ifdef IRON_METAL
		i32 samples_per_frame = 4;
#else
		i32 samples_per_frame = 64;
#endif

		render_path_raytrace_bake_rays_pix = render_path_raytrace_frame * samples_per_frame;
		render_path_raytrace_bake_rays_counter += samples_per_frame;
		render_path_raytrace_bake_rays_timer += sys_real_delta();
		if (render_path_raytrace_bake_rays_timer >= 1) {
			render_path_raytrace_bake_rays_sec     = render_path_raytrace_bake_rays_counter;
			render_path_raytrace_bake_rays_timer   = 0;
			render_path_raytrace_bake_rays_counter = 0;
		}
		render_path_raytrace_bake_current_sample++;
		iron_delay_idle_sleep();
		return true;
	}
	else {
		render_path_raytrace_frame               = 0;
		render_path_raytrace_bake_rays_timer     = 0;
		render_path_raytrace_bake_rays_counter   = 0;
		render_path_raytrace_bake_current_sample = 0;
		return false;
	}
}
