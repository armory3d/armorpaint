
#include "../global.h"

i32 util_encode_mesh_data_size(mesh_data_t_array_t *datas) {
	if (datas == NULL) {
		return 0;
	}
	i32 size = 0;
	for (i32 i = 0; i < datas->length; ++i) {
		for (i32 j = 0; j < datas->buffer[i]->vertex_arrays->length; ++j) {
			size += datas->buffer[i]->vertex_arrays->buffer[j]->values->length * 2;
		}
		size += datas->buffer[i]->index_array->length * 4;
	}
	return size;
}

void util_encode_mesh_datas(mesh_data_t_array_t *datas) {
	armpack_encode_string("mesh_datas");
	if (datas == NULL) {
		armpack_encode_null();
		return;
	}

	armpack_encode_array(datas->length);
	for (i32 i = 0; i < datas->length; ++i) {
		armpack_encode_map(5);
		armpack_encode_string("name");
		armpack_encode_string(datas->buffer[i]->name);
		armpack_encode_string("scale_pos");
		armpack_encode_f32(datas->buffer[i]->scale_pos);
		armpack_encode_string("scale_tex");
		armpack_encode_f32(datas->buffer[i]->scale_tex);
		armpack_encode_string("vertex_arrays");
		armpack_encode_array(datas->buffer[i]->vertex_arrays->length);
		for (i32 j = 0; j < datas->buffer[i]->vertex_arrays->length; ++j) {
			armpack_encode_map(3);
			armpack_encode_string("attrib");
			armpack_encode_string(datas->buffer[i]->vertex_arrays->buffer[j]->attrib);
			armpack_encode_string("data");
			armpack_encode_string(datas->buffer[i]->vertex_arrays->buffer[j]->data);
			armpack_encode_string("values");
			armpack_encode_array_i16(datas->buffer[i]->vertex_arrays->buffer[j]->values);
		}
		armpack_encode_string("index_array");
		armpack_encode_array_i32(datas->buffer[i]->index_array);
	}
}

buffer_t *util_encode_scene(scene_t *raw) {
	i32       size    = 8 * 1024 * 1024 + util_encode_mesh_data_size(raw->mesh_datas);
	buffer_t *encoded = buffer_create(size);
	armpack_encode_start(encoded->buffer);
	armpack_encode_map(13);
	armpack_encode_string("name");
	armpack_encode_null();
	armpack_encode_string("objects");
	armpack_encode_null();
	util_encode_mesh_datas(raw->mesh_datas);
	armpack_encode_string("camera_datas");
	armpack_encode_null();
	armpack_encode_string("camera_ref");
	armpack_encode_null();
	armpack_encode_string("material_datas");
	armpack_encode_null();
	armpack_encode_string("shader_datas");
	armpack_encode_null();
	armpack_encode_string("world_datas");
	armpack_encode_null();
	armpack_encode_string("world_ref");
	armpack_encode_null();
	armpack_encode_string("speaker_datas"); // TODO: deprecated
	armpack_encode_null();
	armpack_encode_string("embedded_datas");
	armpack_encode_null();
	i32 ei          = armpack_encode_end();
	encoded->length = ei;
	return encoded;
}

void util_encode_node_canvas(ui_node_canvas_t *c) {
	ui_node_canvas_encode(c);
}

i32 util_encode_packed_assets_size(packed_asset_t_array_t *assets) {
	if (assets == NULL) {
		return 0;
	}
	i32 size = 0;
	for (i32 i = 0; i < assets->length; ++i) {
		packed_asset_t *asset = assets->buffer[i];
		buffer_t       *bytes = asset->bytes;
		size += bytes->length;
	}
	return size;
}

i32 util_encode_buffers_size(buffer_t_array_t *buffers) {
	if (buffers == NULL) {
		return 0;
	}
	i32 size = 0;
	for (i32 i = 0; i < buffers->length; ++i) {
		size += buffers->buffer[i]->length;
	}
	return size;
}

i32 util_encode_f32_arrays_size(f32_array_t_array_t *arrays) {
	if (arrays == NULL) {
		return 0;
	}
	i32 size = 0;
	for (i32 i = 0; i < arrays->length; ++i) {
		size += arrays->buffer[i]->length * 4;
	}
	return size;
}

i32 util_encode_layer_data_size(layer_data_t_array_t *datas) {
	if (datas == NULL) {
		return 0;
	}
	i32 size = 0;
	for (i32 i = 0; i < datas->length; ++i) {
		buffer_t *tp = datas->buffer[i]->texpaint;
		if (tp != NULL) {
			size += tp->length;
		}

		buffer_t *tp_nor = datas->buffer[i]->texpaint_nor;
		if (tp_nor != NULL) {
			size += tp_nor->length;
		}

		buffer_t *tp_pack = datas->buffer[i]->texpaint_pack;
		if (tp_pack != NULL) {
			size += tp_pack->length;
		}

		buffer_t *tp_sculpt = datas->buffer[i]->texpaint_sculpt;
		if (tp_sculpt != NULL) {
			size += tp_sculpt->length;
		}
	}
	return size;
}

i32 util_encode_timeline_layers_size(timeline_layer_keyframe_data_t_array_t *datas) {
	if (datas == NULL) {
		return 0;
	}
	i32 size = 0;
	for (i32 i = 0; i < datas->length; ++i) {
		buffer_t *tp = datas->buffer[i]->texpaint;
		if (tp != NULL) {
			size += tp->length;
		}
		buffer_t *tp_nor = datas->buffer[i]->texpaint_nor;
		if (tp_nor != NULL) {
			size += tp_nor->length;
		}
		buffer_t *tp_pack = datas->buffer[i]->texpaint_pack;
		if (tp_pack != NULL) {
			size += tp_pack->length;
		}
		f32_array_t *pp = datas->buffer[i]->path_points;
		if (pp != NULL) {
			size += pp->length * 4;
		}
		f32_array_t *ppw = datas->buffer[i]->path_points_world;
		if (ppw != NULL) {
			size += ppw->length * 4;
		}
		f32_array_t *ppc = datas->buffer[i]->path_points_camera;
		if (ppc != NULL) {
			size += ppc->length * 4;
		}
		i32_array_t *ppp = datas->buffer[i]->path_points_parent;
		if (ppp != NULL) {
			size += ppp->length * 4;
		}
	}
	return size;
}

i32 util_encode_timeline_meshes_size(timeline_mesh_keyframe_data_t_array_t *datas) {
	if (datas == NULL) {
		return 0;
	}
	i32 size = 0;
	for (i32 i = 0; i < datas->length; ++i) {
		f32_array_t *transform = datas->buffer[i]->transform;
		if (transform != NULL) {
			size += transform->length * 4;
		}
	}
	return size;
}

buffer_t *util_encode_project(project_t *raw) {
	i32 size = 32 * 1024 * 1024 + util_encode_layer_data_size(raw->layer_datas) + util_encode_mesh_data_size(raw->mesh_datas) +
	           util_encode_packed_assets_size(raw->packed_assets) + util_encode_buffers_size(raw->brush_icons) + util_encode_buffers_size(raw->material_icons) +
	           util_encode_buffers_size(raw->mesh_icons) + util_encode_buffers_size(raw->mesh_skins) + util_encode_f32_arrays_size(raw->mesh_transforms) +
	           util_encode_timeline_layers_size(raw->timeline_layers) + util_encode_timeline_meshes_size(raw->timeline_meshes);
	buffer_t *encoded = buffer_create(size);

	armpack_encode_start(encoded->buffer);
	armpack_encode_map(39);

	armpack_encode_string("version");
	armpack_encode_string(raw->version);
	armpack_encode_string("assets");
	armpack_encode_array_string(raw->assets);
	armpack_encode_string("is_bgra");
	armpack_encode_bool(raw->is_bgra);
	armpack_encode_string("packed_assets");
	if (raw->packed_assets != NULL) {
		armpack_encode_array(raw->packed_assets->length);
		for (i32 i = 0; i < raw->packed_assets->length; ++i) {
			armpack_encode_map(2);
			armpack_encode_string("name");
			armpack_encode_string(raw->packed_assets->buffer[i]->name);
			armpack_encode_string("bytes");
			armpack_encode_array_u8(raw->packed_assets->buffer[i]->bytes);
		}
	}
	else {
		armpack_encode_null();
	}
	armpack_encode_string("envmap");
	armpack_encode_string(raw->envmap);
	armpack_encode_string("envmap_strength");
	armpack_encode_f32(raw->envmap_strength);
	armpack_encode_string("envmap_angle");
	armpack_encode_f32(raw->envmap_angle);
	armpack_encode_string("envmap_blur");
	armpack_encode_bool(raw->envmap_blur);
	armpack_encode_string("camera_world");
	armpack_encode_array_f32(raw->camera_world);
	armpack_encode_string("camera_origin");
	armpack_encode_array_f32(raw->camera_origin);
	armpack_encode_string("camera_fov");
	armpack_encode_f32(raw->camera_fov);
	armpack_encode_string("swatches");
	if (raw->swatches != NULL) {
		armpack_encode_array(raw->swatches->length);
		for (i32 i = 0; i < raw->swatches->length; ++i) {
			armpack_encode_map(9);
			armpack_encode_string("base");
			armpack_encode_i32(raw->swatches->buffer[i]->base);
			armpack_encode_string("opacity");
			armpack_encode_f32(raw->swatches->buffer[i]->opacity);
			armpack_encode_string("occlusion");
			armpack_encode_f32(raw->swatches->buffer[i]->occlusion);
			armpack_encode_string("roughness");
			armpack_encode_f32(raw->swatches->buffer[i]->roughness);
			armpack_encode_string("metallic");
			armpack_encode_f32(raw->swatches->buffer[i]->metallic);
			armpack_encode_string("normal");
			armpack_encode_i32(raw->swatches->buffer[i]->normal);
			armpack_encode_string("emission");
			armpack_encode_f32(raw->swatches->buffer[i]->emission);
			armpack_encode_string("height");
			armpack_encode_f32(raw->swatches->buffer[i]->height);
			armpack_encode_string("subsurface");
			armpack_encode_f32(raw->swatches->buffer[i]->subsurface);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("brush_nodes");
	if (raw->brush_nodes != NULL) {
		armpack_encode_array(raw->brush_nodes->length);
		for (i32 i = 0; i < raw->brush_nodes->length; ++i) {
			util_encode_node_canvas(raw->brush_nodes->buffer[i]);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("brush_icons");
	if (raw->brush_icons != NULL) {
		armpack_encode_array(raw->brush_icons->length);
		for (i32 i = 0; i < raw->brush_icons->length; ++i) {
			armpack_encode_array_u8(raw->brush_icons->buffer[i]);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("material_nodes");
	if (raw->material_nodes != NULL) {
		armpack_encode_array(raw->material_nodes->length);
		for (i32 i = 0; i < raw->material_nodes->length; ++i) {
			util_encode_node_canvas(raw->material_nodes->buffer[i]);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("material_groups");
	if (raw->material_groups != NULL) {
		armpack_encode_array(raw->material_groups->length);
		for (i32 i = 0; i < raw->material_groups->length; ++i) {
			util_encode_node_canvas(raw->material_groups->buffer[i]);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("material_icons");
	if (raw->material_icons != NULL) {
		armpack_encode_array(raw->material_icons->length);
		for (i32 i = 0; i < raw->material_icons->length; ++i) {
			armpack_encode_array_u8(raw->material_icons->buffer[i]);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("material_datas");
	if (raw->material_datas != NULL) {
		armpack_encode_array(raw->material_datas->length);
		for (i32 i = 0; i < raw->material_datas->length; ++i) {
			armpack_encode_map(10);
			armpack_encode_string("paint_base");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_base);
			armpack_encode_string("paint_opac");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_opac);
			armpack_encode_string("paint_occ");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_occ);
			armpack_encode_string("paint_rough");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_rough);
			armpack_encode_string("paint_met");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_met);
			armpack_encode_string("paint_nor");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_nor);
			armpack_encode_string("paint_height");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_height);
			armpack_encode_string("paint_emis");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_emis);
			armpack_encode_string("paint_subs");
			armpack_encode_bool(raw->material_datas->buffer[i]->paint_subs);
			armpack_encode_string("opac_mode");
			armpack_encode_i32(raw->material_datas->buffer[i]->opac_mode);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("font_assets");
	armpack_encode_array_string(raw->font_assets);
	armpack_encode_string("sound_assets");
	armpack_encode_array_string(raw->sound_assets);

	armpack_encode_string("layer_datas");
	if (raw->layer_datas != NULL) {
		armpack_encode_array(raw->layer_datas->length);
		for (i32 i = 0; i < raw->layer_datas->length; ++i) {
			armpack_encode_map(36);
			armpack_encode_string("name");
			armpack_encode_string(raw->layer_datas->buffer[i]->name);
			armpack_encode_string("res");
			armpack_encode_i32(raw->layer_datas->buffer[i]->res);
			armpack_encode_string("bpp");
			armpack_encode_i32(raw->layer_datas->buffer[i]->bpp);
			armpack_encode_string("texpaint");
			armpack_encode_array_u8(raw->layer_datas->buffer[i]->texpaint);
			armpack_encode_string("uv_scale");
			armpack_encode_f32(raw->layer_datas->buffer[i]->uv_scale);
			armpack_encode_string("uv_rot");
			armpack_encode_f32(raw->layer_datas->buffer[i]->uv_rot);
			armpack_encode_string("uv_type");
			armpack_encode_i32(raw->layer_datas->buffer[i]->uv_type);
			armpack_encode_string("decal_mat");
			armpack_encode_array_f32(raw->layer_datas->buffer[i]->decal_mat);
			armpack_encode_string("opacity_mask");
			armpack_encode_f32(raw->layer_datas->buffer[i]->opacity_mask);
			armpack_encode_string("fill_material");
			armpack_encode_i32(raw->layer_datas->buffer[i]->fill_material);
			armpack_encode_string("object_mask");
			armpack_encode_i32(raw->layer_datas->buffer[i]->object_mask);
			armpack_encode_string("blending");
			armpack_encode_i32(raw->layer_datas->buffer[i]->blending);
			armpack_encode_string("parent");
			armpack_encode_i32(raw->layer_datas->buffer[i]->parent);
			armpack_encode_string("visible");
			armpack_encode_bool(raw->layer_datas->buffer[i]->visible);
			armpack_encode_string("texpaint_nor");
			armpack_encode_array_u8(raw->layer_datas->buffer[i]->texpaint_nor);
			armpack_encode_string("texpaint_pack");
			armpack_encode_array_u8(raw->layer_datas->buffer[i]->texpaint_pack);
			armpack_encode_string("texpaint_sculpt");
			armpack_encode_array_u8(raw->layer_datas->buffer[i]->texpaint_sculpt);
			armpack_encode_string("paint_base");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_base);
			armpack_encode_string("paint_opac");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_opac);
			armpack_encode_string("paint_occ");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_occ);
			armpack_encode_string("paint_rough");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_rough);
			armpack_encode_string("paint_met");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_met);
			armpack_encode_string("paint_nor");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_nor);
			armpack_encode_string("paint_nor_blend");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_nor_blend);
			armpack_encode_string("paint_height");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_height);
			armpack_encode_string("paint_height_blend");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_height_blend);
			armpack_encode_string("paint_emis");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_emis);
			armpack_encode_string("paint_subs");
			armpack_encode_bool(raw->layer_datas->buffer[i]->paint_subs);
			armpack_encode_string("uv_map");
			armpack_encode_i32(raw->layer_datas->buffer[i]->uv_map);
			armpack_encode_string("path_points");
			armpack_encode_array_f32(raw->layer_datas->buffer[i]->path_points);
			armpack_encode_string("path_points_world");
			armpack_encode_array_f32(raw->layer_datas->buffer[i]->path_points_world);
			armpack_encode_string("path_points_camera");
			armpack_encode_array_f32(raw->layer_datas->buffer[i]->path_points_camera);
			armpack_encode_string("path_points_parent");
			armpack_encode_array_i32(raw->layer_datas->buffer[i]->path_points_parent);
			armpack_encode_string("path_tool");
			armpack_encode_i32(raw->layer_datas->buffer[i]->path_tool);
			armpack_encode_string("path_curved");
			armpack_encode_bool(raw->layer_datas->buffer[i]->path_curved);
			armpack_encode_string("path_material");
			armpack_encode_i32(raw->layer_datas->buffer[i]->path_material);
		}
	}
	else {
		armpack_encode_null();
	}

	util_encode_mesh_datas(raw->mesh_datas);

	armpack_encode_string("mesh_assets");
	armpack_encode_array_string(raw->mesh_assets);

	armpack_encode_string("mesh_icons");
	if (raw->mesh_icons != NULL) {
		armpack_encode_array(raw->mesh_icons->length);
		for (i32 i = 0; i < raw->mesh_icons->length; ++i) {
			armpack_encode_array_u8(raw->mesh_icons->buffer[i]);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("mesh_transforms");
	if (raw->mesh_transforms != NULL) {
		armpack_encode_array(raw->mesh_transforms->length);
		for (i32 i = 0; i < raw->mesh_transforms->length; ++i) {
			armpack_encode_array_f32(raw->mesh_transforms->buffer[i]);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("mesh_materials");
	armpack_encode_array_i32(raw->mesh_materials);

	armpack_encode_string("mesh_parents");
	armpack_encode_array_i32(raw->mesh_parents);

	armpack_encode_string("mesh_physics_shapes");
	armpack_encode_array_i32(raw->mesh_physics_shapes);

	armpack_encode_string("mesh_physics_masses");
	armpack_encode_array_f32(raw->mesh_physics_masses);

	armpack_encode_string("mesh_skins");
	if (raw->mesh_skins != NULL) {
		armpack_encode_array(raw->mesh_skins->length);
		for (i32 i = 0; i < raw->mesh_skins->length; ++i) {
			armpack_encode_array_u8(raw->mesh_skins->buffer[i]);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("atlas_objects");
	armpack_encode_array_i32(raw->atlas_objects);
	armpack_encode_string("atlas_names");
	armpack_encode_array_string(raw->atlas_names);
	armpack_encode_string("script_datas");
	armpack_encode_array_string(raw->script_datas);
	armpack_encode_string("script_names");
	armpack_encode_array_string(raw->script_names);

	armpack_encode_string("timeline_frame_rate");
	armpack_encode_i32(raw->timeline_frame_rate);
	armpack_encode_string("timeline_max_frames");
	armpack_encode_i32(raw->timeline_max_frames);

	armpack_encode_string("timeline_layers");
	if (raw->timeline_layers != NULL) {
		armpack_encode_array(raw->timeline_layers->length);
		for (i32 i = 0; i < raw->timeline_layers->length; ++i) {
			armpack_encode_map(10);
			armpack_encode_string("frame");
			armpack_encode_i32(raw->timeline_layers->buffer[i]->frame);
			armpack_encode_string("layer_index");
			armpack_encode_i32(raw->timeline_layers->buffer[i]->layer_index);
			armpack_encode_string("texpaint");
			armpack_encode_array_u8(raw->timeline_layers->buffer[i]->texpaint);
			armpack_encode_string("texpaint_nor");
			armpack_encode_array_u8(raw->timeline_layers->buffer[i]->texpaint_nor);
			armpack_encode_string("texpaint_pack");
			armpack_encode_array_u8(raw->timeline_layers->buffer[i]->texpaint_pack);
			armpack_encode_string("path_points");
			armpack_encode_array_f32(raw->timeline_layers->buffer[i]->path_points);
			armpack_encode_string("path_points_world");
			armpack_encode_array_f32(raw->timeline_layers->buffer[i]->path_points_world);
			armpack_encode_string("path_points_camera");
			armpack_encode_array_f32(raw->timeline_layers->buffer[i]->path_points_camera);
			armpack_encode_string("path_points_parent");
			armpack_encode_array_i32(raw->timeline_layers->buffer[i]->path_points_parent);
			armpack_encode_string("tween");
			armpack_encode_bool(raw->timeline_layers->buffer[i]->tween);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("timeline_meshes");
	if (raw->timeline_meshes != NULL) {
		armpack_encode_array(raw->timeline_meshes->length);
		for (i32 i = 0; i < raw->timeline_meshes->length; ++i) {
			armpack_encode_map(4);
			armpack_encode_string("frame");
			armpack_encode_i32(raw->timeline_meshes->buffer[i]->frame);
			armpack_encode_string("mesh_index");
			armpack_encode_i32(raw->timeline_meshes->buffer[i]->mesh_index);
			armpack_encode_string("transform");
			armpack_encode_array_f32(raw->timeline_meshes->buffer[i]->transform);
			armpack_encode_string("tween");
			armpack_encode_bool(raw->timeline_meshes->buffer[i]->tween);
		}
	}
	else {
		armpack_encode_null();
	}

	armpack_encode_string("stages");
	if (raw->stages != NULL) {
		armpack_encode_array(raw->stages->length);
		for (i32 i = 0; i < raw->stages->length; ++i) {
			armpack_encode_map(3);
			armpack_encode_string("name");
			armpack_encode_string(raw->stages->buffer[i]->name);
			armpack_encode_string("objects");
			armpack_encode_array_string(raw->stages->buffer[i]->objects);
			armpack_encode_string("layers");
			armpack_encode_array_string(raw->stages->buffer[i]->layers);
		}
	}
	else {
		armpack_encode_null();
	}

	i32 ei          = armpack_encode_end();
	encoded->length = ei;
	return encoded;
}
