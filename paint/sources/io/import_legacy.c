
#include "../global.h"

ui_node_socket_t_array_t *import_arm_get_node_socket_array(any_map_t *old, char *key) {
	ui_node_socket_t_array_t *sockets = any_array_create_from_raw((void *[]){}, 0);
	any_array_t              *ias     = any_map_get(old, key);
	for (i32 i = 0; i < ias->length; ++i) {
		any_map_t        *old = ias->buffer[i];
		ui_node_socket_t *s   = GC_ALLOC_INIT(ui_node_socket_t, {0});
		s->id                 = armpack_map_get_i32(old, "id");
		s->node_id            = armpack_map_get_i32(old, "node_id");
		s->name               = string_copy(any_map_get(old, "name"));
		s->type               = string_copy(any_map_get(old, "type"));
		s->color              = armpack_map_get_i32(old, "color");
		s->default_value      = any_map_get(old, "default_value");
		s->min                = armpack_map_get_f32(old, "min");
		s->max                = armpack_map_get_f32(old, "max");
		s->precision          = armpack_map_get_f32(old, "precision");
		s->display            = armpack_map_get_i32(old, "display");
		any_array_push(sockets, s);
	}
	return sockets;
}

ui_node_canvas_t_array_t *import_arm_get_node_canvas_array(any_map_t *map, char *key) {
	any_array_t *cas = any_map_get(map, key);
	if (cas == NULL) {
		return NULL;
	}
	ui_node_canvas_t_array_t *ar = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < cas->length; ++i) {
		any_map_t        *old = cas->buffer[i];
		ui_node_canvas_t *c   = GC_ALLOC_INIT(ui_node_canvas_t, {0});
		c->name               = string_copy(any_map_get(old, "name"));
		c->nodes              = any_array_create_from_raw((void *[]){}, 0);
		any_array_t *ns       = any_map_get(old, "nodes");
		for (i32 i = 0; i < ns->length; ++i) {
			any_map_t *old   = ns->buffer[i];
			ui_node_t *n     = GC_ALLOC_INIT(ui_node_t, {0});
			n->id            = armpack_map_get_i32(old, "id");
			n->name          = string_copy(any_map_get(old, "name"));
			n->type          = string_copy(any_map_get(old, "type"));
			n->x             = armpack_map_get_f32(old, "x");
			n->y             = armpack_map_get_f32(old, "y");
			n->color         = armpack_map_get_i32(old, "color");
			n->inputs        = import_arm_get_node_socket_array(old, "inputs");
			n->outputs       = import_arm_get_node_socket_array(old, "outputs");
			n->buttons       = any_array_create_from_raw((void *[]){}, 0);
			any_array_t *bas = any_map_get(old, "buttons");
			for (i32 i = 0; i < bas->length; ++i) {
				any_map_t        *old = bas->buffer[i];
				ui_node_button_t *b   = GC_ALLOC_INIT(ui_node_button_t, {0});
				b->name               = string_copy(any_map_get(old, "name"));
				b->type               = string_copy(any_map_get(old, "type"));
				b->output             = armpack_map_get_i32(old, "output");
				b->default_value      = any_map_get(old, "default_value");
				b->data               = any_map_get(old, "data");
				b->min                = armpack_map_get_f32(old, "min");
				b->max                = armpack_map_get_f32(old, "max");
				b->precision          = armpack_map_get_f32(old, "precision");
				b->height             = armpack_map_get_f32(old, "height");
				any_array_push(n->buttons, b);
			}
			n->width = armpack_map_get_f32(old, "width");
			n->flags = armpack_map_get_i32(old, "flags");
			any_array_push(c->nodes, n);
		}
		c->links         = any_array_create_from_raw((void *[]){}, 0);
		any_array_t *las = any_map_get(old, "links");
		for (i32 i = 0; i < las->length; ++i) {
			any_map_t      *old = las->buffer[i];
			ui_node_link_t *l   = GC_ALLOC_INIT(ui_node_link_t, {0});
			l->id               = armpack_map_get_i32(old, "id");
			l->from_id          = armpack_map_get_i32(old, "from_id");
			l->from_socket      = armpack_map_get_i32(old, "from_socket");
			l->to_id            = armpack_map_get_i32(old, "to_id");
			l->to_socket        = armpack_map_get_i32(old, "to_socket");
			any_array_push(c->links, l);
		}
		any_array_push(ar, c);
	}
	return ar;
}

bool import_arm_is_version(buffer_t *b, char *n) {
	// 0xdf map, i32 count, 0xdb string, i32 len, "version", 0xdb string, i32 len, version value
	if (b->buffer[10] != 'v') {
		return false;
	}
	uint32_t len = *(uint32_t *)(b->buffer + 18); // version value length
	char    *v   = (char *)(b->buffer + 22);      // version value
	for (uint32_t i = 0; i < len; ++i) {
		if (n[i] == '\0' || v[i] != n[i]) {
			return false;
		}
	}
	return n[len] == '\0';
}

bool import_arm_is_old(buffer_t *b) {
	return !import_arm_is_version(b, manifest_version_project);
}

project_t *import_arm_from_map_to_arm(any_map_t *old) {
	project_t *project = GC_ALLOC_INIT(project_t, {0});
	project->version   = string_copy(manifest_version_project);
	project->assets    = any_map_get(old, "assets");
	project->is_bgra   = armpack_map_get_i32(old, "is_bgra") > 0;

	any_array_t *pas = any_map_get(old, "packed_assets");
	if (pas != NULL) {
		project->packed_assets = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < pas->length; ++i) {
			any_map_t      *old = pas->buffer[i];
			packed_asset_t *pa  = GC_ALLOC_INIT(packed_asset_t, {0});
			pa->name            = string_copy(any_map_get(old, "name"));
			pa->bytes           = any_map_get(old, "bytes");
			any_array_push(project->packed_assets, pa);
		}
	}
	project->envmap          = string_copy(any_map_get(old, "envmap"));
	project->envmap_strength = armpack_map_get_f32(old, "envmap_strength");
	project->envmap_angle    = armpack_map_get_f32(old, "envmap_angle");
	project->envmap_blur     = armpack_map_get_i32(old, "envmap_blur");
	project->camera_world    = any_map_get(old, "camera_world");
	project->camera_origin   = any_map_get(old, "camera_origin");
	project->camera_fov      = armpack_map_get_f32(old, "camera_fov");

	any_array_t *ss = any_map_get(old, "swatches");
	if (ss != NULL) {
		project->swatches = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < ss->length; ++i) {
			any_map_t      *old = ss->buffer[i];
			swatch_color_t *s   = GC_ALLOC_INIT(swatch_color_t, {0});
			s->base             = armpack_map_get_i32(old, "base");
			s->opacity          = armpack_map_get_f32(old, "opacity");
			s->occlusion        = armpack_map_get_f32(old, "occlusion");
			s->roughness        = armpack_map_get_f32(old, "roughness");
			s->metallic         = armpack_map_get_f32(old, "metallic");
			s->normal           = armpack_map_get_i32(old, "normal");
			s->emission         = armpack_map_get_f32(old, "emission");
			s->height           = armpack_map_get_f32(old, "height");
			s->subsurface       = armpack_map_get_f32(old, "subsurface");
			any_array_push(project->swatches, s);
		}
	}
	project->brush_nodes    = import_arm_get_node_canvas_array(old, "brush_nodes");
	project->brush_icons    = any_map_get(old, "brush_icons");
	project->material_nodes = import_arm_get_node_canvas_array(old, "material_nodes");
	if (any_map_get(old, "material_groups") != NULL) {
		project->material_groups = import_arm_get_node_canvas_array(old, "material_groups");
	}
	project->material_icons = any_map_get(old, "material_icons");

	any_array_t *mds = any_map_get(old, "material_datas");
	if (mds != NULL) {
		project->material_datas = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < mds->length; ++i) {
			any_map_t        *old = mds->buffer[i];
			material_data2_t *md  = GC_ALLOC_INIT(layer_data_t, {0});
			md->paint_base        = armpack_map_get_i32(old, "paint_base") > 0;
			md->paint_opac        = armpack_map_get_i32(old, "paint_opac") > 0;
			md->paint_occ         = armpack_map_get_i32(old, "paint_occ") > 0;
			md->paint_rough       = armpack_map_get_i32(old, "paint_rough") > 0;
			md->paint_met         = armpack_map_get_i32(old, "paint_met") > 0;
			md->paint_nor         = armpack_map_get_i32(old, "paint_nor") > 0;
			md->paint_height      = armpack_map_get_i32(old, "paint_height") > 0;
			md->paint_emis        = armpack_map_get_i32(old, "paint_emis") > 0;
			md->paint_subs        = armpack_map_get_i32(old, "paint_subs") > 0;
			md->opac_mode         = armpack_map_get_i32(old, "opac_mode");
			any_array_push(project->material_datas, md);
		}
	}

	project->font_assets  = any_map_get(old, "font_assets");
	project->sound_assets = any_map_get(old, "sound_assets");

	any_array_t *lds = any_map_get(old, "layer_datas");
	if (lds != NULL) {
		project->layer_datas = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < lds->length; ++i) {
			any_map_t    *old      = lds->buffer[i];
			layer_data_t *ld       = GC_ALLOC_INIT(layer_data_t, {0});
			ld->name               = string_copy(any_map_get(old, "name"));
			ld->res                = armpack_map_get_i32(old, "res");
			ld->bpp                = armpack_map_get_i32(old, "bpp");
			ld->texpaint           = any_map_get(old, "texpaint");
			ld->uv_scale           = armpack_map_get_f32(old, "uv_scale");
			ld->uv_rot             = armpack_map_get_f32(old, "uv_rot");
			ld->uv_type            = armpack_map_get_i32(old, "uv_type");
			ld->decal_mat          = any_map_get(old, "decal_mat");
			ld->opacity_mask       = armpack_map_get_f32(old, "opacity_mask");
			ld->fill_material      = armpack_map_get_i32(old, "fill_material");
			ld->object_mask        = armpack_map_get_i32(old, "object_mask");
			ld->blending           = armpack_map_get_i32(old, "blending");
			ld->parent             = armpack_map_get_i32(old, "parent");
			ld->visible            = armpack_map_get_i32(old, "visible") > 0;
			ld->texpaint_nor       = any_map_get(old, "texpaint_nor");
			ld->texpaint_pack      = any_map_get(old, "texpaint_pack");
			ld->texpaint_sculpt    = any_map_get(old, "texpaint_sculpt");
			ld->paint_base         = armpack_map_get_i32(old, "paint_base") > 0;
			ld->paint_opac         = armpack_map_get_i32(old, "paint_opac") > 0;
			ld->paint_occ          = armpack_map_get_i32(old, "paint_occ") > 0;
			ld->paint_rough        = armpack_map_get_i32(old, "paint_rough") > 0;
			ld->paint_met          = armpack_map_get_i32(old, "paint_met") > 0;
			ld->paint_nor          = armpack_map_get_i32(old, "paint_nor") > 0;
			ld->paint_nor_blend    = armpack_map_get_i32(old, "paint_nor_blend") > 0;
			ld->paint_height       = armpack_map_get_i32(old, "paint_height") > 0;
			ld->paint_height_blend = armpack_map_get_i32(old, "paint_height_blend") > 0;
			ld->paint_emis         = armpack_map_get_i32(old, "paint_emis") > 0;
			ld->paint_subs         = armpack_map_get_i32(old, "paint_subs") > 0;
			ld->uv_map             = armpack_map_get_i32(old, "uv_map");
			any_array_push(project->layer_datas, ld);
		}
	}

	any_array_t *ms = any_map_get(old, "mesh_datas");
	if (ms != NULL) {
		project->mesh_datas = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < ms->length; ++i) {
			any_map_t   *old  = ms->buffer[i];
			mesh_data_t *md   = GC_ALLOC_INIT(mesh_data_t, {0});
			md->name          = string_copy(any_map_get(old, "name"));
			md->scale_pos     = armpack_map_get_f32(old, "scale_pos");
			md->scale_tex     = armpack_map_get_f32(old, "scale_tex");
			any_array_t *vas  = any_map_get(old, "vertex_arrays");
			md->vertex_arrays = any_array_create_from_raw((void *[]){}, 0);
			for (i32 i = 0; i < vas->length; ++i) {
				any_map_t      *old = vas->buffer[i];
				vertex_array_t *va  = GC_ALLOC_INIT(vertex_array_t, {0});
				va->attrib          = string_copy(any_map_get(old, "attrib"));
				va->data            = string_copy(any_map_get(old, "data"));
				va->values          = any_map_get(old, "values");
				any_array_push(md->vertex_arrays, va);
			}
			md->index_array = any_map_get(old, "index_array");
			any_array_push(project->mesh_datas, md);
		}
	}

	project->mesh_assets         = any_map_get(old, "mesh_assets");
	project->mesh_icons          = any_map_get(old, "mesh_icons");
	project->mesh_transforms     = any_map_get(old, "mesh_transforms");
	project->mesh_materials      = any_map_get(old, "mesh_materials");
	project->mesh_parents        = any_map_get(old, "mesh_parents");
	project->mesh_physics_shapes = any_map_get(old, "mesh_physics_shapes");
	project->mesh_physics_masses = any_map_get(old, "mesh_physics_masses");
	project->mesh_skins          = any_map_get(old, "mesh_skins");
	project->atlas_objects       = any_map_get(old, "atlas_objects");
	project->atlas_names         = any_map_get(old, "atlas_names");
	project->script_datas        = any_map_get(old, "script_datas");
	project->script_names        = any_map_get(old, "script_names");
	project->timeline_frame_rate = armpack_map_get_i32(old, "timeline_frame_rate");
	project->timeline_max_frames = armpack_map_get_i32(old, "timeline_max_frames");

	any_array_t *tls = any_map_get(old, "timeline_layers");
	if (tls != NULL) {
		project->timeline_layers = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < tls->length; ++i) {
			any_map_t                      *old = tls->buffer[i];
			timeline_layer_keyframe_data_t *d   = GC_ALLOC_INIT(timeline_layer_keyframe_data_t, {0});
			d->frame                            = armpack_map_get_i32(old, "frame");
			d->layer_index                      = armpack_map_get_i32(old, "layer_index");
			d->texpaint                         = any_map_get(old, "texpaint");
			d->texpaint_nor                     = any_map_get(old, "texpaint_nor");
			d->texpaint_pack                    = any_map_get(old, "texpaint_pack");
			d->path_points                      = any_map_get(old, "path_points");
			d->path_points_world                = any_map_get(old, "path_points_world");
			d->path_points_camera               = any_map_get(old, "path_points_camera");
			d->path_points_parent               = any_map_get(old, "path_points_parent");
			d->tween                            = armpack_map_get_i32(old, "tween") > 0;
			any_array_push(project->timeline_layers, d);
		}
	}

	any_array_t *tms = any_map_get(old, "timeline_meshes");
	if (tms != NULL) {
		project->timeline_meshes = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < tms->length; ++i) {
			any_map_t                     *old = tms->buffer[i];
			timeline_mesh_keyframe_data_t *d   = GC_ALLOC_INIT(timeline_mesh_keyframe_data_t, {0});
			d->frame                           = armpack_map_get_i32(old, "frame");
			d->mesh_index                      = armpack_map_get_i32(old, "mesh_index");
			d->transform                       = any_map_get(old, "transform");
			d->tween                           = armpack_map_get_i32(old, "tween") > 0;
			any_array_push(project->timeline_meshes, d);
		}
	}

	any_array_t *stages = any_map_get(old, "stages");
	if (stages != NULL) {
		project->stages = any_array_create_from_raw((void *[]){}, 0);
		for (i32 i = 0; i < stages->length; ++i) {
			any_map_t *old = stages->buffer[i];
			stage_t   *d   = GC_ALLOC_INIT(stage_t, {0});
			d->name        = any_map_get(old, "name");
			d->objects     = any_map_get(old, "objects");
			d->layers      = any_map_get(old, "layers");
			any_array_push(project->stages, d);
		}
	}

	return project;
}

project_t *import_arm_from_version_12(any_map_t *old) {
	any_map_set(old, "mesh_skins", NULL);
	return import_arm_from_map_to_arm(old);
}

project_t *import_arm_from_version_11(any_map_t *old) {
	any_map_set(old, "mesh_physics_shapes", NULL);
	any_map_set(old, "mesh_physics_masses", NULL);
	return import_arm_from_version_12(old);
}

project_t *import_arm_from_version_10(any_map_t *old) {
	any_map_set(old, "sound_assets", NULL);
	return import_arm_from_version_11(old);
}

project_t *import_arm_from_version_9(any_map_t *old) {
	any_array_t *lds = any_map_get(old, "layer_datas");
	if (lds != NULL) {
		for (i32 i = 0; i < lds->length; ++i) {
			any_map_t *ld = lds->buffer[i];
			any_map_set(ld, "texpaint_sculpt", NULL);
		}
	}
	return import_arm_from_version_10(old);
}

project_t *import_arm_from_version_8(any_map_t *old) {
	any_map_set(old, "mesh_materials", NULL);
	any_map_set(old, "mesh_parents", NULL);
	any_map_set(old, "script_names", NULL);
	any_map_set(old, "stages", NULL);
	return import_arm_from_version_9(old);
}

project_t *import_arm_from_version_7(any_map_t *old) {
	armpack_map_set_i32(old, "timeline_frame_rate", 24);
	armpack_map_set_i32(old, "timeline_max_frames", 200);
	any_map_set(old, "timeline_layers", NULL);
	any_map_set(old, "timeline_meshes", NULL);
	return import_arm_from_version_8(old);
}

project_t *import_arm_from_version_6(any_map_t *old) {
	any_array_t *lds = any_map_get(old, "layer_datas");
	if (lds != NULL) {
		for (i32 i = 0; i < lds->length; ++i) {
			any_map_t *ld = lds->buffer[i];
			armpack_map_set_i32(ld, "fill_material", armpack_map_get_i32(ld, "fill_layer"));
			any_map_set(ld, "path_points", NULL);
			any_map_set(ld, "path_points_world", NULL);
			any_map_set(ld, "path_points_camera", NULL);
			any_map_set(ld, "path_points_parent", NULL);
			armpack_map_set_i32(ld, "path_tool", 0);
			armpack_map_set_i32(ld, "path_curved", 0);
			armpack_map_set_i32(ld, "path_material", -1);
		}
	}

	any_array_t *mds = any_array_create_from_raw((void *[]){}, 0);
	any_array_t *mns = any_map_get(old, "material_nodes");
	for (i32 i = 0; i < mns->length; ++i) {
		any_map_t *m = any_map_create();
		armpack_map_set_i32(m, "paint_base", 1);
		armpack_map_set_i32(m, "paint_opac", 1);
		armpack_map_set_i32(m, "paint_occ", 1);
		armpack_map_set_i32(m, "paint_rough", 1);
		armpack_map_set_i32(m, "paint_met", 1);
		armpack_map_set_i32(m, "paint_nor", 1);
		armpack_map_set_i32(m, "paint_height", 1);
		armpack_map_set_i32(m, "paint_emis", 1);
		armpack_map_set_i32(m, "paint_subs", 1);
		armpack_map_set_i32(m, "opac_mode", 0);
		any_array_push(mds, m);
	}
	any_map_set(old, "material_datas", mds);

	return import_arm_from_version_7(old);
}

project_t *import_arm_from_version_5(any_map_t *old) {
	any_array_t *mds = any_map_get(old, "mesh_datas");
	any_array_t *mts = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < mds->length; ++i) {
		any_array_push(mts, mat4_to_f32_array(mat4_identity()));
	}
	any_map_set(old, "mesh_transforms", mts);
	return import_arm_from_version_6(old);
}

project_t *import_arm_from_version_4(any_map_t *old) {
	f32_array_t *camera_world = any_map_get(old, "camera_world");
	if (camera_world != NULL) {
		mat4_t m     = mat4_from_f32_array(camera_world, 0);
		m            = mat4_transpose(m);
		camera_world = mat4_to_f32_array(m);
		any_map_set(old, "camera_world", camera_world);
	}
	return import_arm_from_version_5(old);
}

project_t *import_arm_from_version_3(any_map_t *old) {
	any_map_set(old, "envmap_angle", 0);
	any_map_set(old, "envmap_blur", 0);
	return import_arm_from_version_4(old);
}

project_t *import_arm_from_version_2(any_map_t *old) {
	any_array_t *lds = any_map_get(old, "layer_datas");
	if (lds != NULL) {
		for (i32 i = 0; i < lds->length; ++i) {
			any_map_t *ld = lds->buffer[i];
			any_map_set(ld, "uv_map", 0);
		}
	}
	return import_arm_from_version_3(old);
}

project_t *import_arm_from_version_1(any_map_t *old) {
	return import_arm_from_version_2(old);
}

project_t *import_arm_from_version_0(any_map_t *old) {
	return import_arm_from_version_1(old);
}

project_t *import_arm_from_old(buffer_t *b) {
	any_map_t *old                   = armpack_decode_to_map(b);
	project_t *(*fns[])(any_map_t *) = {
	    import_arm_from_version_0, import_arm_from_version_1, import_arm_from_version_2,  import_arm_from_version_3,
	    import_arm_from_version_4, import_arm_from_version_5, import_arm_from_version_6,  import_arm_from_version_7,
	    import_arm_from_version_8, import_arm_from_version_9, import_arm_from_version_10, import_arm_from_version_11,
	    import_arm_from_version_12,
	};
	for (i32 v = sizeof(fns) / sizeof(fns[0]) - 1; v >= 0; --v) {
		if (import_arm_is_version(b, i32_to_string(v))) {
			return fns[v](old);
		}
	}
	return NULL;
}
