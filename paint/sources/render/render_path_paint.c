
#include "../global.h"

bool             render_path_paint_dilated               = true;
mesh_object_t   *render_path_paint_painto                = NULL;
mesh_object_t   *render_path_paint_planeo                = NULL;
u8_array_t      *render_path_paint_visibles              = NULL;
bool             render_path_paint_merged_object_visible = false;
f32              render_path_paint_saved_fov             = 0.0;
render_target_t *_render_path_paint_texpaint;
render_target_t *_render_path_paint_texpaint_nor;
render_target_t *_render_path_paint_texpaint_pack;
render_target_t *_render_path_paint_texpaint_undo;
render_target_t *_render_path_paint_texpaint_nor_undo;
render_target_t *_render_path_paint_texpaint_pack_undo;
f32              render_path_paint_last_x = -1.0;
f32              render_path_paint_last_y = -1.0;
bake_type_t      _render_path_paint_bake_type;

void render_path_paint_init() {

	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_blend0";
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = "R8";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_blend1";
		t->width           = config_get_texture_res_x();
		t->height          = config_get_texture_res_y();
		t->format          = "R8";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_colorid";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_picker";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_nor_picker";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_pack_picker";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_uv_picker";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA32";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_posnortex_picker0";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA128";
		render_path_create_render_target(t);
	}
	{
		render_target_t *t = render_target_create();
		t->name            = "texpaint_posnortex_picker1";
		t->width           = 1;
		t->height          = 1;
		t->format          = "RGBA128";
		render_path_create_render_target(t);
	}

	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3_pass");
	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3RGBA64_pass");
	render_path_load_shader("Scene/copy_mrt3_pass/copy_mrt3RGBA128_pass");
	render_path_load_shader("Scene/dilate_pass/dilate_pass");
	render_path_load_shader("Scene/dilate_pass/dilateRGBA64_pass");
	render_path_load_shader("Scene/dilate_pass/dilateRGBA128_pass");
}

void render_path_paint_draw_fullscreen_triangle(char *ctx) {
	// Vertices are mangled in vertex shader to form a fullscreen triangle,
	// so plane transform does not matter
	mesh_object_t *plane    = scene_get_child(".Plane")->ext;
	bool           _visible = plane->base->visible;
	plane->base->visible    = true;
	mesh_object_render(plane, ctx, _render_path_bind_params);
	plane->base->visible = _visible;
	render_path_end();
}

void render_path_paint_commands_particle(i32 tid, char *texpaint, bool is_mask) {
	// Hide bullets to avoid self-painting
	for (i32 _pi = 0; _pi < 32; ++_pi) {
		if (g_context->particles[_pi].bullet != NULL) {
			g_context->particles[_pi].bullet->visible = false;
		}
	}
	// Paint pass per particle
	for (i32 _pi = 0; _pi < 32; ++_pi) {
		if (g_context->particles[_pi].timer == NULL || g_context->particles[_pi].hit_x == 0) {
			continue;
		}
		g_context->particle_index      = _pi;
		g_context->particle_hit_x      = g_context->particles[_pi].hit_x;
		g_context->particle_hit_y      = g_context->particles[_pi].hit_y;
		g_context->particle_hit_z      = g_context->particles[_pi].hit_z;
		g_context->last_particle_hit_x = g_context->particles[_pi].hit_last_x;
		g_context->last_particle_hit_y = g_context->particles[_pi].hit_last_y;
		g_context->last_particle_hit_z = g_context->particles[_pi].hit_last_z;
		if (is_mask) {
			tid = g_context->layer->parent->id;
			if (slot_layer_is_group(g_context->layer->parent)) {
				for (i32 i = 0; i < slot_layer_get_children(g_context->layer->parent)->length; ++i) {
					slot_layer_t *c = slot_layer_get_children(g_context->layer->parent)->buffer[i];
					tid             = c->id;
					break;
				}
			}
		}

		string_array_t *additional = any_array_create_from_raw((void *[]){string("texpaint_nor%d", tid), string("texpaint_pack%d", tid), "texpaint_blend0"}, 3);
		render_path_set_target(texpaint, additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("main", "gbufferD");
		if (g_context->xray || g_config->brush_angle_reject || context_is_decal()) {
			render_path_bind_target("gbuffer0", "gbuffer0");
		}
		render_path_bind_target("texpaint_blend1", "paintmask");
		if (g_context->colorid_picked) {
			render_path_bind_target("texpaint_colorid", "texpaint_colorid");
		}
		render_path_draw_meshes("paint");
	}
	for (i32 _pi = 0; _pi < 32; ++_pi) {
		if (g_context->particles[_pi].bullet != NULL) {
			g_context->particles[_pi].bullet->visible = true;
		}
	}
}

void render_path_paint_commands_paint(bool dilation) {
	i32 tid = g_context->layer->id;

	bool picking_tool = g_context->tool == TOOL_TYPE_COLORID || g_context->tool == TOOL_TYPE_PICKER || g_context->tool == TOOL_TYPE_MATERIAL ||
	                    g_context->tool == TOOL_TYPE_CURSOR;
	if (g_context->layer->texpaint_sculpt != NULL && !picking_tool) {
		render_path_sculpt_commands();
		return;
	}

	if (g_context->pdirty <= 0) {
		return;
	}

	if (g_context->tool == TOOL_TYPE_COLORID) {
		render_path_set_target("texpaint_colorid", NULL, NULL, GPU_CLEAR_COLOR, 0xff000000, 0.0);
		render_path_bind_target("gbuffer2", "gbuffer2");
		render_path_paint_draw_fullscreen_triangle("paint");
		ui_header_handle->redraws = 2;
	}
	else if (g_context->tool == TOOL_TYPE_PICKER || g_context->tool == TOOL_TYPE_MATERIAL) {
		if (g_context->pick_pos_nor_tex) {
			if (g_context->paint2d) {
				string_array_t *additional = any_array_create_from_raw(
				    (void *[]){
				        "gbuffer1",
				        "gbuffer2",
				    },
				    2);
				render_path_set_target("gbuffer0", additional, "main", GPU_CLEAR_NONE, 0, 0.0);
				render_path_draw_meshes("mesh");
			}
			string_array_t *additional = any_array_create_from_raw(
			    (void *[]){
			        "texpaint_posnortex_picker1",
			    },
			    1);
			render_path_set_target("texpaint_posnortex_picker0", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("gbuffer2", "gbuffer2");
			render_path_bind_target("main", "gbufferD");
			render_path_draw_meshes("paint");
			render_target_t *picker0                    = any_map_get(render_path_render_targets, "texpaint_posnortex_picker0");
			render_target_t *picker1                    = any_map_get(render_path_render_targets, "texpaint_posnortex_picker1");
			gpu_texture_t   *texpaint_posnortex_picker0 = picker0->_image;
			gpu_texture_t   *texpaint_posnortex_picker1 = picker1->_image;
			buffer_t        *a                          = gpu_get_texture_pixels(texpaint_posnortex_picker0);
			buffer_t        *b                          = gpu_get_texture_pixels(texpaint_posnortex_picker1);
			g_context->posx_picked                      = buffer_get_f32(a, 0);
			g_context->posy_picked                      = buffer_get_f32(a, 4);
			g_context->posz_picked                      = buffer_get_f32(a, 8);
			g_context->uvx_picked                       = buffer_get_f32(a, 12);
			g_context->norx_picked                      = buffer_get_f32(b, 0);
			g_context->nory_picked                      = buffer_get_f32(b, 4);
			g_context->norz_picked                      = buffer_get_f32(b, 8);
			g_context->uvy_picked                       = buffer_get_f32(b, 12);
		}
		else {
			tid          = g_context->layer->id;
			bool is_mask = slot_layer_is_mask(g_context->layer);
			if (is_mask) {
				string_array_t *additional = any_array_create_from_raw(
				    (void *[]){
				        "texpaint_uv_picker",
				    },
				    1);
				render_path_set_target("texpaint_picker", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target("gbuffer2", "gbuffer2");
				render_path_bind_target(string("texpaint%d", tid), "texpaint");
				render_path_paint_draw_fullscreen_triangle("paint");
			}
			else {
				string_array_t *additional = any_array_create_from_raw(
				    (void *[]){
				        "texpaint_nor_picker",
				        "texpaint_pack_picker",
				        "texpaint_uv_picker",
				    },
				    3);
				render_path_set_target("texpaint_picker", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target("gbuffer2", "gbuffer2");
				bool use_live_layer = g_context->tool == TOOL_TYPE_MATERIAL;
				if (use_live_layer) {
					render_path_paint_use_live_layer(true);
				}
				render_path_bind_target(string("texpaint%d", tid), "texpaint");
				render_path_bind_target(string("texpaint_nor%d", tid), "texpaint_nor");
				render_path_bind_target(string("texpaint_pack%d", tid), "texpaint_pack");
				render_path_paint_draw_fullscreen_triangle("paint");
				if (use_live_layer) {
					render_path_paint_use_live_layer(false);
				}
			}
			ui_header_handle->redraws         = 2;
			ui_base_hwnds->buffer[2]->redraws = 2;

			render_target_t *texpaint_picker    = any_map_get(render_path_render_targets, "texpaint_picker");
			render_target_t *texpaint_uv_picker = any_map_get(render_path_render_targets, "texpaint_uv_picker");
			buffer_t        *a                  = gpu_get_texture_pixels(texpaint_picker->_image);
			buffer_t        *d                  = gpu_get_texture_pixels(texpaint_uv_picker->_image);

			if (g_context->color_picker_callback != NULL) {
				g_context->color_picker_callback(g_context->picked_color);
			}

// Picked surface values
#ifdef IRON_BGRA
			i32 i0 = 2;
			i32 i1 = 1;
			i32 i2 = 0;
#else
			i32 i0 = 0;
			i32 i1 = 1;
			i32 i2 = 2;
#endif
			i32 i3                           = 3;
			g_context->picked_color->base    = color_set_rb(g_context->picked_color->base, buffer_get_u8(a, i0));
			g_context->picked_color->base    = color_set_gb(g_context->picked_color->base, buffer_get_u8(a, i1));
			g_context->picked_color->base    = color_set_bb(g_context->picked_color->base, buffer_get_u8(a, i2));
			g_context->picked_color->opacity = buffer_get_u8(a, i3) / 255.0;
			g_context->uvx_picked            = buffer_get_u8(d, i0) / 255.0;
			g_context->uvy_picked            = buffer_get_u8(d, i1) / 255.0;

			if (!is_mask) {
				render_target_t *texpaint_nor_picker  = any_map_get(render_path_render_targets, "texpaint_nor_picker");
				render_target_t *texpaint_pack_picker = any_map_get(render_path_render_targets, "texpaint_pack_picker");
				buffer_t        *b                    = gpu_get_texture_pixels(texpaint_nor_picker->_image);
				buffer_t        *c                    = gpu_get_texture_pixels(texpaint_pack_picker->_image);
				g_context->picked_color->normal       = color_set_rb(g_context->picked_color->normal, buffer_get_u8(b, i0));
				g_context->picked_color->normal       = color_set_gb(g_context->picked_color->normal, buffer_get_u8(b, i1));
				g_context->picked_color->normal       = color_set_bb(g_context->picked_color->normal, buffer_get_u8(b, i2));
				g_context->picked_color->occlusion    = buffer_get_u8(c, i0) / 255.0;
				g_context->picked_color->roughness    = buffer_get_u8(c, i1) / 255.0;
				g_context->picked_color->metallic     = buffer_get_u8(c, i2) / 255.0;
				g_context->picked_color->height       = buffer_get_u8(c, i3) / 255.0;

				// Pick material
				if (g_context->color_picker_callback == NULL) {
					// matid % 3 == 0 - normal, 1 - emission, 2 - subsurface
					i32 id    = buffer_get_u8(b, 3);
					i32 matid = math_floor((id - (id % 3)) / 3.0);
					for (i32 i = 0; i < g_project->_->materials->length; ++i) {
						slot_material_t *m = g_project->_->materials->buffer[i];
						if (m->id == matid) {
							if (g_context->picker_select_material) {
								context_set_material(m);
							}
							g_context->materialid_picked = matid;
							break;
						}
					}
				}
			}

			// Update fill layers which are using materials containing a PICKER node
			slot_material_t *_material = g_context->material;
			for (i32 i = 0; i < g_project->_->materials->length; ++i) {
				slot_material_t *m          = g_project->_->materials->buffer[i];
				bool             has_picker = false;
				for (i32 j = 0; j < m->canvas->nodes->length; ++j) {
					ui_node_t *node = m->canvas->nodes->buffer[j];
					if (string_equals(node->type, "PICKER")) {
						has_picker = true;
						break;
					}
				}
				if (has_picker) {
					g_context->material = m;
					layers_update_fill_layers();
					util_render_make_material_preview();
					ui_base_hwnds->buffer[TAB_AREA_SIDEBAR1]->redraws = 2;
				}
			}
			g_context->material = _material;
		}
	}
	else if (g_context->tool == TOOL_TYPE_CURSOR) {
		if (g_context->pick_object_id) {
			viewport_mode_t _viewport_mode = g_context->viewport_mode;
			g_context->viewport_mode       = VIEWPORT_MODE_OBJECT_ID;
			make_material_parse_mesh_material();
			render_path_base_draw_gbuffer();
			g_context->viewport_mode = _viewport_mode;
			make_material_parse_mesh_material();

			g_context->pdirty = 1;
			make_material_parse_paint_material(false);
			render_path_set_target("texpaint_picker", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("gbuffer1", "gbuffer1");
			render_path_bind_target("gbuffer2", "gbuffer2");
			render_path_paint_draw_fullscreen_triangle("paint");

			render_target_t *rt    = any_map_get(render_path_render_targets, "texpaint_picker");
			buffer_t        *pixel = gpu_get_texture_pixels(rt->_image);
			if (buffer_get_u8(pixel, 3) > 0) {
#ifdef IRON_BGRA
				i32 r_byte = buffer_get_u8(pixel, 2);
#else
				i32 r_byte = buffer_get_u8(pixel, 0);
#endif
				i32 index = r_byte - 1;
				if (index >= 0 && index < g_project->_->paint_objects->length) {
					g_context->paint_object   = g_project->_->paint_objects->buffer[index];
					ui_header_handle->redraws = 2;

					// g_context->layer->object_mask = index + 1;
					// context_set_layer(g_context->layer);
					// make_material_parse_mesh_material();
					// layers_set_object_mask();

					base_redraw_status();
				}
			}

			g_context->pick_object_id = false;
			make_material_parse_paint_material(false);
			render_path_base_draw_gbuffer();
		}
	}
	else {
		char *texpaint = string("texpaint%d", tid);
		if (g_context->tool == TOOL_TYPE_BAKE && g_context->brush_time == sys_delta()) {
			// Clear to black on bake start
			render_path_set_target(texpaint, NULL, NULL, GPU_CLEAR_COLOR, 0xff000000, 0.0);
		}

		render_path_set_target("texpaint_blend1", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("texpaint_blend0", "tex");
		render_path_draw_shader("Scene/copy_pass/copyR8_pass");
		bool is_mask = slot_layer_is_mask(g_context->layer);

		// Read texcoords from gbuffer
		bool read_tc = (g_context->tool == TOOL_TYPE_FILL && g_context->fill_type == FILL_TYPE_FACE) || g_context->tool == TOOL_TYPE_CLONE ||
		               g_context->tool == TOOL_TYPE_BLUR;

		if (g_context->tool == TOOL_TYPE_PARTICLE) {
			render_path_paint_commands_particle(tid, texpaint, is_mask);
		}
		else {
			if (is_mask) {
				i32 ptid = g_context->layer->parent->id;
				if (slot_layer_is_group(g_context->layer->parent)) { // Group mask
					for (i32 i = 0; i < slot_layer_get_children(g_context->layer->parent)->length; ++i) {
						slot_layer_t *c = slot_layer_get_children(g_context->layer->parent)->buffer[i];
						ptid            = c->id;
						break;
					}
				}
				string_array_t *additional = any_array_create_from_raw(
				    (void *[]){
				        string("texpaint_nor%d", ptid),
				        string("texpaint_pack%d", ptid),
				        "texpaint_blend0",
				    },
				    3);
				render_path_set_target(texpaint, additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
			}
			else {
				string_array_t *additional = any_array_create_from_raw(
				    (void *[]){
				        string("texpaint_nor%d", tid),
				        string("texpaint_pack%d", tid),
				        "texpaint_blend0",
				    },
				    3);
				render_path_set_target(texpaint, additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
			}
			render_path_bind_target("main", "gbufferD");
			if (g_context->xray || g_config->brush_angle_reject || context_is_decal()) {
				render_path_bind_target("gbuffer0", "gbuffer0");
			}
			render_path_bind_target("texpaint_blend1", "paintmask");
			if (g_context->colorid_picked) {
				render_path_bind_target("texpaint_colorid", "texpaint_colorid");
			}
			if (read_tc) {
				render_path_bind_target("gbuffer2", "gbuffer2");
			}
			i32 sculpt_layer_count = 0;
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *sl = g_project->_->layers->buffer[i];
				if (sl->texpaint_sculpt != NULL && slot_layer_is_visible(sl)) {
					render_path_bind_target(string("texpaint_sculpt%d", sl->id), string("texpaint_sculpt%d", sl->id));
					sculpt_layer_count++;
				}
			}
			if (sculpt_layer_count > 1) {
				render_path_bind_target("texpaint_sculpt_base", "texpaint_sculpt_base");
			}
			object_t *bullet     = scene_get_child(".Bullet");
			bool      bullet_vis = bullet != NULL && bullet->visible;
			if (bullet != NULL) {
				bullet->visible = false;
			}
			render_path_draw_meshes("paint");
			if (bullet != NULL) {
				bullet->visible = bullet_vis;
			}
		}

		if (g_context->tool == TOOL_TYPE_BAKE && g_context->bake_type == BAKE_TYPE_CURVATURE && g_context->bake_curv_smooth > 0) {
			if (any_map_get(render_path_render_targets, "texpaint_blur") == NULL) {
				render_target_t *t = render_target_create();
				t->name            = "texpaint_blur";
				t->width           = math_floor(config_get_texture_res_x() * 0.95);
				t->height          = math_floor(config_get_texture_res_y() * 0.95);
				t->format          = "RGBA32";
				render_path_create_render_target(t);
			}
			i32 blurs = math_round(g_context->bake_curv_smooth);
			for (i32 i = 0; i < blurs; ++i) {
				render_path_set_target("texpaint_blur", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target(texpaint, "tex");
				render_path_draw_shader("Scene/copy_pass/copy_pass");
				render_path_set_target(texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
				render_path_bind_target("texpaint_blur", "tex");
				render_path_draw_shader("Scene/copy_pass/copy_pass");
			}
		}

		if (dilation) {
			render_path_paint_dilate(true, false);
		}
	}
}

void render_path_paint_use_live_layer(bool use) {
	i32 tid = g_context->layer->id;
	i32 hid = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
	if (use) {
		gc_unroot(_render_path_paint_texpaint);
		_render_path_paint_texpaint = any_map_get(render_path_render_targets, string("texpaint%d", tid));
		gc_root(_render_path_paint_texpaint);
		gc_unroot(_render_path_paint_texpaint_undo);
		_render_path_paint_texpaint_undo = any_map_get(render_path_render_targets, string("texpaint_undo%d", hid));
		gc_root(_render_path_paint_texpaint_undo);
		gc_unroot(_render_path_paint_texpaint_nor_undo);
		_render_path_paint_texpaint_nor_undo = any_map_get(render_path_render_targets, string("texpaint_nor_undo%d", hid));
		gc_root(_render_path_paint_texpaint_nor_undo);
		gc_unroot(_render_path_paint_texpaint_pack_undo);
		_render_path_paint_texpaint_pack_undo = any_map_get(render_path_render_targets, string("texpaint_pack_undo%d", hid));
		gc_root(_render_path_paint_texpaint_pack_undo);
		gc_unroot(_render_path_paint_texpaint_nor);
		_render_path_paint_texpaint_nor = any_map_get(render_path_render_targets, string("texpaint_nor%d", tid));
		gc_root(_render_path_paint_texpaint_nor);
		gc_unroot(_render_path_paint_texpaint_pack);
		_render_path_paint_texpaint_pack = any_map_get(render_path_render_targets, string("texpaint_pack%d", tid));
		gc_root(_render_path_paint_texpaint_pack);
		any_map_set(render_path_render_targets, string("texpaint_undo%d", hid), any_map_get(render_path_render_targets, string("texpaint%d", tid)));
		any_map_set(render_path_render_targets, string("texpaint%d", tid), any_map_get(render_path_render_targets, "texpaint_live"));
		if (slot_layer_is_layer(g_context->layer)) {
			any_map_set(render_path_render_targets, string("texpaint_nor_undo%d", hid), any_map_get(render_path_render_targets, string("texpaint_nor%d", tid)));
			any_map_set(render_path_render_targets, string("texpaint_pack_undo%d", hid),
			            any_map_get(render_path_render_targets, string("texpaint_pack%d", tid)));
			any_map_set(render_path_render_targets, string("texpaint_nor%d", tid), any_map_get(render_path_render_targets, "texpaint_nor_live"));
			any_map_set(render_path_render_targets, string("texpaint_pack%d", tid), any_map_get(render_path_render_targets, "texpaint_pack_live"));
		}
	}
	else {
		any_map_set(render_path_render_targets, string("texpaint%d", tid), _render_path_paint_texpaint);
		any_map_set(render_path_render_targets, string("texpaint_undo%d", hid), _render_path_paint_texpaint_undo);
		if (slot_layer_is_layer(g_context->layer)) {
			any_map_set(render_path_render_targets, string("texpaint_nor_undo%d", hid), _render_path_paint_texpaint_nor_undo);
			any_map_set(render_path_render_targets, string("texpaint_pack_undo%d", hid), _render_path_paint_texpaint_pack_undo);
			any_map_set(render_path_render_targets, string("texpaint_nor%d", tid), _render_path_paint_texpaint_nor);
			any_map_set(render_path_render_targets, string("texpaint_pack%d", tid), _render_path_paint_texpaint_pack);
		}
	}
	render_path_paint_live_layer_locked = use;
}

void render_path_paint_commands_symmetry() {
	if (g_context->sym_x || g_context->sym_y || g_context->sym_z) {
		g_context->ddirty = 2;
		transform_t *t    = g_context->paint_object->base->transform;
		f32          sx   = t->scale.x;
		f32          sy   = t->scale.y;
		f32          sz   = t->scale.z;
		if (g_context->sym_x) {
			t->scale = (vec4_t){-sx, sy, sz, 1.0};
			transform_build_matrix(t);
			render_path_paint_commands_paint(false);
		}
		if (g_context->sym_y) {
			t->scale = (vec4_t){sx, -sy, sz, 1.0};
			transform_build_matrix(t);
			render_path_paint_commands_paint(false);
		}
		if (g_context->sym_z) {
			t->scale = (vec4_t){sx, sy, -sz, 1.0};
			transform_build_matrix(t);
			render_path_paint_commands_paint(false);
		}
		if (g_context->sym_x && g_context->sym_y) {
			t->scale = (vec4_t){-sx, -sy, sz, 1.0};
			transform_build_matrix(t);
			render_path_paint_commands_paint(false);
		}
		if (g_context->sym_x && g_context->sym_z) {
			t->scale = (vec4_t){-sx, sy, -sz, 1.0};
			transform_build_matrix(t);
			render_path_paint_commands_paint(false);
		}
		if (g_context->sym_y && g_context->sym_z) {
			t->scale = (vec4_t){sx, -sy, -sz, 1.0};
			transform_build_matrix(t);
			render_path_paint_commands_paint(false);
		}
		if (g_context->sym_x && g_context->sym_y && g_context->sym_z) {
			t->scale = (vec4_t){-sx, -sy, -sz, 1.0};
			transform_build_matrix(t);
			render_path_paint_commands_paint(false);
		}
		t->scale = (vec4_t){sx, sy, sz, 1.0};
		transform_build_matrix(t);
	}
}

void render_path_paint_commands_live_brush() {
	tool_type_t tool = g_context->tool;
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_DECAL && tool != TOOL_TYPE_TEXT &&
	    tool != TOOL_TYPE_BLUR) {
		return;
	}

	if (render_path_paint_live_layer_locked) {
		return;
	}

	if (render_path_paint_live_layer == NULL) {
		gc_unroot(render_path_paint_live_layer);
		render_path_paint_live_layer = slot_layer_create("_live", LAYER_SLOT_TYPE_LAYER, NULL);
		gc_root(render_path_paint_live_layer);
	}

	i32 tid = g_context->layer->id;
	if (slot_layer_is_mask(g_context->layer)) {
		render_path_set_target("texpaint_live", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint%d", tid), "tex");
		render_path_draw_shader("Scene/copy_pass/copy_pass");
	}
	else {
		string_array_t *additional = any_array_create_from_raw(
		    (void *[]){
		        "texpaint_nor_live",
		        "texpaint_pack_live",
		    },
		    2);
		render_path_set_target("texpaint_live", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint%d", tid), "tex0");
		render_path_bind_target(string("texpaint_nor%d", tid), "tex1");
		render_path_bind_target(string("texpaint_pack%d", tid), "tex2");
		render_path_draw_shader("Scene/copy_mrt3_pass/copy_mrt3_pass");
	}

	render_path_paint_use_live_layer(true);

	render_path_paint_live_layer_drawn = 2;

	ui_view2d_hwnd->redraws     = 2;
	f32 _x                      = g_context->paint_vec.x;
	f32 _y                      = g_context->paint_vec.y;
	f32 _last_x                 = g_context->last_paint_vec_x;
	f32 _last_y                 = g_context->last_paint_vec_y;
	i32 _pdirty                 = g_context->pdirty;
	g_context->last_paint_vec_x = g_context->paint_vec.x;
	g_context->last_paint_vec_y = g_context->paint_vec.y;
	if (operator_shortcut(any_map_get(g_keymap, "brush_ruler"), SHORTCUT_TYPE_STARTED)) {
		g_context->last_paint_vec_x = g_context->last_paint_x;
		g_context->last_paint_vec_y = g_context->last_paint_y;
	}
	g_context->pdirty = 2;

	render_path_paint_commands_symmetry();
	render_path_paint_commands_paint(true);

	render_path_paint_use_live_layer(false);

	g_context->paint_vec.x       = _x;
	g_context->paint_vec.y       = _y;
	g_context->last_paint_vec_x  = _last_x;
	g_context->last_paint_vec_y  = _last_y;
	g_context->pdirty            = _pdirty;
	g_context->brush_blend_dirty = true;
}

void render_path_paint_draw_cursor(f32 mx, f32 my, f32 radius, f32 tint_r, f32 tint_g, f32 tint_b) {
	mesh_object_t *plane = scene_get_child(".Plane")->ext;
	mesh_data_t   *geom  = plane->data;

	render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_cursor);
	render_target_t *rt   = any_map_get(render_path_render_targets, "main");
	gpu_texture_t   *main = rt->_image;
	gpu_set_texture(pipes_cursor_gbufferd, main);
	gpu_set_float2(pipes_cursor_mouse, mx, my);
	gpu_set_float2(pipes_cursor_tex_step, 1 / (float)main->width, 1 / (float)main->height);
	gpu_set_float(pipes_cursor_radius, radius);
	vec4_t right = vec4_norm(camera_object_right_world(scene_camera));
	gpu_set_float3(pipes_cursor_camera_right, right.x, right.y, right.z);
	gpu_set_float3(pipes_cursor_tint, tint_r, tint_g, tint_b);
	vec4_t up = vec4_norm(camera_object_up_world(scene_camera));
	gpu_set_float3(pipes_cursor_camera_up, up.x, up.y, up.z);
	gpu_set_float(pipes_cursor_camera_align, context_is_brush_camera_align() ? 1.0f : 0.0f);
	gpu_set_mat4(pipes_cursor_vp, scene_camera->vp);
	mat4_t inv_vp = mat4_inv(scene_camera->vp);
	gpu_set_mat4(pipes_cursor_inv_vp, inv_vp);
	gpu_set_vertex_buffer(geom->_->vertex_buffer);
	gpu_set_index_buffer(geom->_->index_buffer);
	gpu_draw();
	render_path_end();
}

void render_path_paint_draw_cursor_decal(f32 mx, f32 my, f32 radius, f32 opacity_scale, gpu_texture_t *image) {
	if (image == NULL) {
		return;
	}

	mesh_object_t *plane = scene_get_child(".Plane")->ext;
	mesh_data_t   *geom  = plane->data;

	// Prevent cursor jumping constantly if randomness is used in brush nodes
	f32 brush_nodes_opacity = g_context->brush_nodes_opacity;
	f32 brush_nodes_angle   = g_context->brush_nodes_angle;
	if (g_context->brush_nodes_uses_random) {
		brush_nodes_opacity = 1.0;
		brush_nodes_angle   = 0.0;
	}

	render_path_set_target("", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_cursor_decal);
	render_target_t *rt   = any_map_get(render_path_render_targets, "main");
	gpu_texture_t   *main = rt->_image;
	gpu_set_texture(pipes_cursor_decal_gbufferd, main);
	gpu_set_texture(pipes_cursor_decal_texdecal, image);
	render_target_t *gbuffer0_rt = any_map_get(render_path_render_targets, "gbuffer0");
	gpu_set_texture(pipes_cursor_decal_gbuffer0, gbuffer0_rt->_image);
	gpu_set_float2(pipes_cursor_decal_mouse, mx, my);
	gpu_set_float2(pipes_cursor_decal_tex_step, 1 / (float)main->width, 1 / (float)main->height);
	gpu_set_float(pipes_cursor_decal_radius, radius);
	vec4_t right = vec4_norm(camera_object_right_world(scene_camera));
	gpu_set_float3(pipes_cursor_decal_camera_right, right.x, right.y, right.z);
	vec4_t up = vec4_norm(camera_object_up_world(scene_camera));
	gpu_set_float3(pipes_cursor_decal_camera_up, up.x, up.y, up.z);
	gpu_set_float(pipes_cursor_decal_camera_align, context_is_brush_camera_align() ? 1.0f : 0.0f);
	f32 opacity = g_context->brush_opacity * brush_nodes_opacity * opacity_scale;
	gpu_set_float(pipes_cursor_decal_opacity, opacity);
	f32 angle = (g_context->brush_angle + brush_nodes_angle) * (math_pi() / 180.0);
	gpu_set_float2(pipes_cursor_decal_angle, math_cos(angle), math_sin(angle));
	gpu_set_float(pipes_cursor_decal_scale_x, g_context->brush_scale_x);
	gpu_set_mat4(pipes_cursor_decal_vp, scene_camera->vp);
	mat4_t inv_vp = mat4_inv(scene_camera->vp);
	gpu_set_mat4(pipes_cursor_decal_inv_vp, inv_vp);
	gpu_set_vertex_buffer(geom->_->vertex_buffer);
	gpu_set_index_buffer(geom->_->index_buffer);
	gpu_draw();
	render_path_end();
}

void render_path_paint_commands_cursor() {
	bool        decal_mask = context_is_decal_mask();
	tool_type_t tool       = g_context->tool;
	if (tool != TOOL_TYPE_BRUSH && tool != TOOL_TYPE_ERASER && tool != TOOL_TYPE_CLONE && tool != TOOL_TYPE_BLUR && tool != TOOL_TYPE_PARTICLE &&
	    !context_is_decal() && !decal_mask) {
		return;
	}

	bool fill_layer  = g_context->layer->fill_material != NULL;
	bool group_layer = slot_layer_is_group(g_context->layer);
	if (!base_ui_enabled || base_is_dragging || fill_layer || group_layer) {
		return;
	}

	f32 mx = g_context->paint_vec.x;
	f32 my = 1.0 - g_context->paint_vec.y;

	// Prevent cursor jumping constantly if randomness is used in brush nodes
	f32 brush_nodes_radius = g_context->brush_nodes_radius;
	if (g_context->brush_nodes_uses_random) {
		brush_nodes_radius = 1.0;
	}

	if (context_is_decal() && !g_context->paint2d) {
		f32            scale2d = (900 / (f32)base_h()) * g_config->window_scale;
		f32            radius  = brush_nodes_radius * g_context->brush_radius / 15.0 * scale2d * 2.0;
		f32            cmx     = decal_mask ? g_context->decal_x : mx;
		f32            cmy     = decal_mask ? 1.0f - g_context->decal_y : my;
		gpu_texture_t *image   = g_context->decal_image;
		render_path_paint_draw_cursor_decal(cmx, cmy, radius, decal_mask ? 0.3f : 0.9f, image);
		if (decal_mask) {
			render_path_paint_draw_cursor(mx, my, brush_nodes_radius * g_context->brush_decal_mask_radius / 3.4, 1.0, 1.0, 1.0);
		}
		return;
	}

	f32 radius = decal_mask ? g_context->brush_decal_mask_radius : g_context->brush_radius;
	render_path_paint_draw_cursor(mx, my, brush_nodes_radius * radius / 3.4, 1.0, 1.0, 1.0);
}

bool render_path_paint_paint_enabled() {
	if (g_context->tool == TOOL_TYPE_BAKE) {
		return true;
	}

	bool fill_layer = g_context->layer->fill_material != NULL && g_context->tool != TOOL_TYPE_PICKER && g_context->tool != TOOL_TYPE_MATERIAL &&
	                  g_context->tool != TOOL_TYPE_COLORID;
	bool group_layer = slot_layer_is_group(g_context->layer);

	// Skip paint if material references this layer and wants to paint into it at the same time
	bool               self_in_material = false;
	i32                layer_i          = array_index_of(g_project->_->layers, g_context->layer);
	ui_node_t_array_t *nodes            = g_context->material->canvas->nodes;
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "LAYER") && (i32)n->buttons->buffer[0]->default_value->buffer[0] == layer_i) {
			self_in_material = true;
			break;
		}
	}

	return !fill_layer && !group_layer && !g_context->foreground_event && !self_in_material;
}

void render_path_paint_live_brush_dirty() {
	f32 mx                   = render_path_paint_last_x;
	f32 my                   = render_path_paint_last_y;
	render_path_paint_last_x = mouse_view_x();
	render_path_paint_last_y = mouse_view_y();
	if (g_config->brush_live && g_context->pdirty <= 0) {
		bool moved = (mx != render_path_paint_last_x || my != render_path_paint_last_y) && (context_in_3d_view() || context_in_2d_view(VIEW_2D_TYPE_LAYER));
		if (moved || g_context->brush_locked) {
			g_context->rdirty = 2;
		}
	}
}

void render_path_paint_begin() {
	if (g_context->layer->texpaint_sculpt != NULL) {
		render_path_sculpt_begin();
		return;
	}

	if (!render_path_paint_dilated) {
		render_path_paint_dilate(false, true);
		render_path_paint_dilated = true;
	}

	if (!render_path_paint_paint_enabled()) {
		return;
	}

	render_path_paint_push_undo_last = history_push_undo;

	if (history_push_undo && history_undo_layers != NULL) {
		history_paint();
	}

	if (g_context->paint2d) {
		render_path_paint_set_plane_mesh();
	}

	if (render_path_paint_live_layer_drawn > 0) {
		render_path_paint_live_layer_drawn--;
	}

	if (g_config->brush_live && g_context->pdirty <= 0 && g_context->ddirty <= 0 && g_context->brush_time == 0) {
		// Depth is unchanged, draw before gbuffer gets updated
		render_path_paint_commands_live_brush();
	}
}

void render_path_paint_end() {
	g_context->ddirty--;
	g_context->rdirty--;

	g_context->last_paint_vec_x = g_context->paint_vec.x;
	g_context->last_paint_vec_y = g_context->paint_vec.y;

	if (!render_path_paint_paint_enabled()) {
		return;
	}

	if (g_context->pdirty > 0) {
		layers_update_linked_layers();
	}

	g_context->pdirty--;
}

void _render_path_paint_final() {
	g_context->bake_type = _render_path_paint_bake_type;
	make_material_parse_paint_material(false);
	g_context->pdirty = 1;
	render_path_paint_commands_paint(true);
	g_context->pdirty        = 0;
	render_path_paint_baking = false;
}

void _render_path_paint_deriv_on_next_frame(void *_) {
	_render_path_paint_final();
}

void _render_path_paint_deriv() {
	g_context->bake_type = BAKE_TYPE_HEIGHT;
	make_material_parse_paint_material(false);
	g_context->pdirty = 1;
	render_path_paint_commands_paint(true);
	g_context->pdirty = 0;
	if (render_path_paint_push_undo_last) {
		history_paint();
	}
	sys_notify_on_next_frame(&_render_path_paint_deriv_on_next_frame, NULL);
}

bool render_path_paint_is_rt_bake() {
	return (g_context->bake_type == BAKE_TYPE_OCCLUSION || g_context->bake_type == BAKE_TYPE_LIGHTMAP || g_context->bake_type == BAKE_TYPE_BENT_NORMAL ||
	        g_context->bake_type == BAKE_TYPE_THICKNESS);
}

void render_path_paint_draw_bake(void *_) {
	_render_path_paint_final();
}

void render_path_paint_draw_bake_derivative(void *_) {
	_render_path_paint_deriv();
}

void render_path_paint_draw() {
	if (!render_path_paint_paint_enabled()) {
		return;
	}

	if (g_config->brush_live && g_context->pdirty <= 0 && g_context->ddirty > 0 && g_context->brush_time == 0) {
		// gbuffer has been updated now but brush will lag 1 frame
		render_path_paint_commands_live_brush();
	}

	if (history_undo_layers != NULL) {
		render_path_paint_commands_symmetry();

		if (g_context->pdirty > 0) {
			render_path_paint_dilated = false;
		}

		if (g_context->tool == TOOL_TYPE_BAKE) {
			if (g_context->bake_type == BAKE_TYPE_NORMAL || g_context->bake_type == BAKE_TYPE_HEIGHT || g_context->bake_type == BAKE_TYPE_DERIVATIVE) {
				if (!render_path_paint_baking && g_context->pdirty > 0) {

					render_path_paint_baking     = true;
					_render_path_paint_bake_type = g_context->bake_type;
					g_context->bake_type = g_context->bake_type == BAKE_TYPE_NORMAL ? BAKE_TYPE_NORMAL_OBJECT : BAKE_TYPE_POSITION; // Bake high poly data
					make_material_parse_paint_material(false);
					mesh_object_t *_paint_object = g_context->paint_object;
					mesh_object_t *high_poly     = g_project->_->paint_objects->buffer[g_context->bake_high_poly];
					bool           _visible      = high_poly->base->visible;
					high_poly->base->visible     = true;
					context_select_paint_object(high_poly);
					render_path_paint_commands_paint(true);
					high_poly->base->visible = _visible;
					if (render_path_paint_push_undo_last) {
						history_paint();
					}
					context_select_paint_object(_paint_object);

					if (g_context->bake_type == BAKE_TYPE_DERIVATIVE) {
						sys_notify_on_next_frame(&render_path_paint_draw_bake_derivative, NULL);
					}
					else {
						sys_notify_on_next_frame(&render_path_paint_draw_bake, NULL);
					}
				}
			}
			else if (g_context->bake_type == BAKE_TYPE_OBJECTID) {
				i32            _layer_filter = g_context->layer_filter;
				mesh_object_t *_paint_object = g_context->paint_object;
				bool           is_merged     = g_context->merged_object != NULL;
				bool           _visible      = is_merged && g_context->merged_object->base->visible;
				g_context->layer_filter      = 1;
				if (is_merged) {
					g_context->merged_object->base->visible = false;
				}

				for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
					mesh_object_t *p = g_project->_->paint_objects->buffer[i];
					context_select_paint_object(p);
					render_path_paint_commands_paint(true);
				}

				g_context->layer_filter = _layer_filter;
				context_select_paint_object(_paint_object);
				if (is_merged)
					g_context->merged_object->base->visible = _visible;
			}
			else if (render_path_paint_is_rt_bake()) {
				render_path_raytrace_bake_commands(make_material_parse_paint_material);
				render_path_paint_dilate(true, false);
			}
			else {
				render_path_paint_commands_paint(true);
			}
		}
		else { // Paint
			render_path_paint_commands_paint(true);
		}
	}

	if (g_context->brush_blend_dirty) {
		g_context->brush_blend_dirty = false;
		render_path_set_target("texpaint_blend0", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_set_target("texpaint_blend1", NULL, NULL, GPU_CLEAR_COLOR, 0x00000000, 0.0);
		render_path_end();
	}

	if (g_context->paint2d) {
		render_path_paint_restore_plane_mesh();
	}
}

void render_path_paint_set_plane_mesh() {
	g_context->paint2d_view = true;
	gc_unroot(render_path_paint_painto);
	render_path_paint_painto = g_context->paint_object;
	gc_root(render_path_paint_painto);
	gc_unroot(render_path_paint_visibles);
	render_path_paint_visibles = u8_array_create_from_raw((u8[]){}, 0);
	gc_root(render_path_paint_visibles);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		u8_array_push(render_path_paint_visibles, p->base->visible);
		p->base->visible = false;
	}
	if (g_context->merged_object != NULL) {
		render_path_paint_merged_object_visible = g_context->merged_object->base->visible;
		g_context->merged_object->base->visible = false;
	}

	camera_object_t *cam        = scene_camera;
	g_context->saved_camera     = cam->base->transform->local;
	render_path_paint_saved_fov = cam->data->fov;
	viewport_update_camera_type(CAMERA_TYPE_PERSPECTIVE);
	mat4_t m = mat4_identity();
	m        = mat4_translate(m, 0, 0, 0.5);
	transform_set_matrix(cam->base->transform, m);
	cam->data->fov = 0.69;
	camera_object_build_proj(cam, -1.0);
	camera_object_build_mat(cam);
	f32 tw    = 0.9 * ui_view2d_pan_scale * (fmin(ui_view2d_ww, ui_view2d_wh) / ui_view2d_ww);
	f32 tx    = ui_view2d_pan_x / (float)ui_view2d_ww;
	f32 ty    = ui_view2d_pan_y / (float)sys_h();
	m         = mat4_identity();
	m         = mat4_scale(m, (vec4_t){tw, tw, 1, 1.0});
	m         = mat4_set_loc(m, (vec4_t){tx, ty, 0, 1.0});
	mat4_t m2 = mat4_identity();
	m2        = mat4_inv(scene_camera->vp);
	m         = mat4_mult_mat(m, m2);

	bool tiled = ui_view2d_tiled_show;
	if (tiled && scene_get_child(".PlaneTiled") == NULL) {
		// 3x3 planes
		i16_array_t *posa = i16_array_create_from_raw(
		    (i16[]){
		        32767,  0, -32767, 0, 10922,  0, -10922, 0, 10922,  0, -32767, 0, 10922,  0, -10922, 0, -10922, 0, 10922,  0, -10922, 0, -10922, 0,
		        -10922, 0, 10922,  0, -32767, 0, 32767,  0, -32767, 0, 10922,  0, 10922,  0, 10922,  0, -10922, 0, 32767,  0, -10922, 0, 10922,  0,
		        32767,  0, 10922,  0, 10922,  0, 32767,  0, 10922,  0, 10922,  0, -10922, 0, -10922, 0, -32767, 0, 10922,  0, -32767, 0, -10922, 0,
		        32767,  0, -10922, 0, 10922,  0, 10922,  0, 10922,  0, -10922, 0, -10922, 0, -32767, 0, -32767, 0, -10922, 0, -32767, 0, -32767, 0,
		        10922,  0, -32767, 0, -10922, 0, -10922, 0, -10922, 0, -32767, 0, 32767,  0, -32767, 0, 32767,  0, -10922, 0, 10922,  0, -10922, 0,
		        10922,  0, -10922, 0, 10922,  0, 10922,  0, -10922, 0, 10922,  0, -10922, 0, 10922,  0, -10922, 0, 32767,  0, -32767, 0, 32767,  0,
		        10922,  0, 10922,  0, 10922,  0, 32767,  0, -10922, 0, 32767,  0, 32767,  0, 10922,  0, 32767,  0, 32767,  0, 10922,  0, 32767,  0,
		        -10922, 0, -10922, 0, -10922, 0, 10922,  0, -32767, 0, 10922,  0, 32767,  0, -10922, 0, 32767,  0, 10922,  0, 10922,  0, 10922,  0,
		        -10922, 0, -32767, 0, -10922, 0, -10922, 0, -32767, 0, -10922, 0, 10922,  0, -32767, 0, 10922,  0, -10922, 0, -10922, 0, -10922, 0,
		    },
		    216);
		i16_array_t *nora = i16_array_create_from_raw(
		    (i16[]){
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		        0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767, 0, -32767,
		    },
		    108);
		i16_array_t *texa = i16_array_create_from_raw(
		    (i16[]){
		        32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767,
		        32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767, 32767, 32767, 0,     0, 0, 32767,
		        32767, 32767, 0,     0, 0, 32767, 32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,
		        32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,
		        32767, 32767, 32767, 0, 0, 0,     32767, 32767, 32767, 0, 0, 0,
		    },
		    108);
		u32_array_t *inda = u32_array_create_from_raw(
		    (u32[]){
		        0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
		        27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
		    },
		    54);
		mesh_data_t *raw =
		    GC_ALLOC_INIT(mesh_data_t,
		                  {.name          = ".PlaneTiled",
		                   .vertex_arrays = any_array_create_from_raw(
		                       (void *[]){
		                           GC_ALLOC_INIT(vertex_array_t, {.attrib = "pos", .values = i16_array_create_from_array(posa), .data = "short4norm"}),
		                           GC_ALLOC_INIT(vertex_array_t, {.attrib = "nor", .values = i16_array_create_from_array(nora), .data = "short2norm"}),
		                           GC_ALLOC_INIT(vertex_array_t, {.attrib = "tex", .values = i16_array_create_from_array(texa), .data = "short2norm"}),
		                       },
		                       3),
		                   .index_array = u32_array_create_from_array(inda),
		                   .scale_pos   = 1.5,
		                   .scale_tex   = 1.0});
		mesh_data_t     *md       = mesh_data_create(raw);
		mesh_object_t   *mo       = scene_get_child(".Plane")->ext;
		material_data_t *material = mo->material;
		mesh_object_t   *o        = scene_add_mesh_object(md, material, NULL);
		o->base->name             = ".PlaneTiled";
	}

	gc_unroot(render_path_paint_planeo);
	render_path_paint_planeo = scene_get_child(tiled ? ".PlaneTiled" : ".Plane")->ext;
	gc_root(render_path_paint_planeo);

	render_path_paint_planeo->base->visible = true;
	g_context->paint_object                 = render_path_paint_planeo;

	vec4_t v                                         = (vec4_t){m.m00, m.m01, m.m02, 1.0};
	f32    sx                                        = vec4_len(v);
	render_path_paint_planeo->base->transform->rot   = quat_from_euler(-math_pi() / 2.0, 0, 0);
	render_path_paint_planeo->base->transform->scale = (vec4_t){sx, 1.0, sx, 1.0};
	render_path_paint_planeo->base->transform->scale.z *= config_get_texture_res_y() / (float)config_get_texture_res_x();
	render_path_paint_planeo->base->transform->loc = (vec4_t){m.m30, -m.m31, 0.0, 1.0};
	transform_build_matrix(render_path_paint_planeo->base->transform);
}

void render_path_paint_restore_plane_mesh() {
	g_context->paint2d_view                        = false;
	render_path_paint_planeo->base->visible        = false;
	render_path_paint_planeo->base->transform->loc = (vec4_t){0.0, 0.0, 0.0, 1.0};
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		g_project->_->paint_objects->buffer[i]->base->visible = render_path_paint_visibles->buffer[i];
	}
	if (g_context->merged_object != NULL) {
		g_context->merged_object->base->visible = render_path_paint_merged_object_visible;
	}
	g_context->paint_object = render_path_paint_painto;
	transform_set_matrix(scene_camera->base->transform, g_context->saved_camera);
	scene_camera->data->fov = render_path_paint_saved_fov;
	viewport_update_camera_type(g_context->camera_type);
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);

	render_path_base_draw_gbuffer();
}

void render_path_paint_bind_layers() {
	bool is_live          = g_config->brush_live && render_path_paint_live_layer_drawn > 0;
	bool is_material_tool = g_context->tool == TOOL_TYPE_MATERIAL;
	if (is_live || is_material_tool) {
		render_path_paint_use_live_layer(true);
	}

	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		render_path_bind_target(string("texpaint%d", l->id), string("texpaint%d", l->id));

		if (slot_layer_is_layer(l)) {
			render_path_bind_target(string("texpaint_nor%d", l->id), string("texpaint_nor%d", l->id));
			render_path_bind_target(string("texpaint_pack%d", l->id), string("texpaint_pack%d", l->id));
		}

		if (l->texpaint_sculpt != NULL) {
			render_path_bind_target(string("texpaint_sculpt%d", l->id), string("texpaint_sculpt%d", l->id));
		}
	}

	if (g_context->colorid_viewport_mask) {
		render_path_bind_target("texpaint_colorid", "texpaint_colorid");
	}
}

void render_path_paint_unbind_layers() {
	bool is_live          = g_config->brush_live && render_path_paint_live_layer_drawn > 0;
	bool is_material_tool = g_context->tool == TOOL_TYPE_MATERIAL;
	if (is_live || is_material_tool) {
		render_path_paint_use_live_layer(false);
	}
}

void render_path_paint_dilate(bool base, bool nor_pack) {
	if (g_config->dilate_radius > 0 && !g_context->paint2d) {
		util_uv_cache_dilate_map();
		layers_make_temp_img();
		i32 tid = g_context->layer->id;

		char *format      = base_bits_handle->i == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits_handle->i == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
		char *copy_pass   = string_equals(format, "RGBA64") ? "copyRGBA64_pass" : string_equals(format, "RGBA128") ? "copyRGBA128_pass" : "copy_pass";
		char *dilate_pass = string_equals(format, "RGBA64") ? "dilateRGBA64_pass" : string_equals(format, "RGBA128") ? "dilateRGBA128_pass" : "dilate_pass";

		if (base) {
			render_path_set_target("temptex0", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target(string("texpaint%d", tid), "tex");
			render_path_draw_shader(string("Scene/copy_pass/%s", copy_pass));
			render_path_set_target(string("texpaint%d", tid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("temptex0", "tex");
			render_path_draw_shader(string("Scene/dilate_pass/%s", dilate_pass));
		}
		if (nor_pack && !slot_layer_is_mask(g_context->layer)) {
			render_path_set_target("temptex0", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target(string("texpaint_nor%d", tid), "tex");
			render_path_draw_shader(string("Scene/copy_pass/%s", copy_pass));
			render_path_set_target(string("texpaint_nor%d", tid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("temptex0", "tex");
			render_path_draw_shader(string("Scene/dilate_pass/%s", dilate_pass));
			render_path_set_target("temptex0", NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target(string("texpaint_pack%d", tid), "tex");
			render_path_draw_shader(string("Scene/copy_pass/%s", copy_pass));
			render_path_set_target(string("texpaint_pack%d", tid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			render_path_bind_target("temptex0", "tex");
			render_path_draw_shader(string("Scene/dilate_pass/%s", dilate_pass));
		}
	}
}
