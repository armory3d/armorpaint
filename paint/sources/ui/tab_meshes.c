
#include "../global.h"

extern buffer_t *slot_material_default_canvas;

i32        _tab_meshes_draw_i;
i32        tab_meshes_mesh_name_edit = -1;
any_map_t *tab_meshes_preview_map    = NULL;
any_map_t *tab_meshes_override_map   = NULL; // object uid -> overridden material index

void tab_meshes_set_drag_mesh(mesh_object_t *o, f32 off_x, f32 off_y) {
	base_drag_off_x = off_x;
	base_drag_off_y = off_y;
	gc_unroot(base_drag_mesh);
	base_drag_mesh = o;
	gc_root(base_drag_mesh);
	g_context->drag_dest = array_index_of(g_project->_->paint_objects, o);
}

void tab_meshes_accept_mesh_drop(mesh_object_t *mesh) {
	i32 pos = array_index_of(g_project->_->paint_objects, mesh);
	if (pos == -1) {
		return;
	}
	i32 dest = g_context->drag_dest;
	if (dest == pos || dest == pos + 1) {
		return;
	}
	array_remove(g_project->_->paint_objects, mesh);
	i32 new_pos = dest > pos ? dest - 1 : dest;
	array_insert(g_project->_->paint_objects, new_pos, mesh);
}

void tab_meshes_set_override(mesh_object_t *o, i32 mat_index) {
	// Render an object with a chosen material instead of the painted layers
	if (tab_meshes_override_map == NULL) {
		tab_meshes_override_map = any_map_create();
		gc_root(tab_meshes_override_map);
	}
	char *uid_key = i32_to_string(o->base->uid);
	if (mat_index < 0 || mat_index >= g_project->_->materials->length) {
		o->material = g_project->_->materials->buffer[0]->data;
		map_delete(tab_meshes_override_map, uid_key);
	}
	else {
		slot_material_t *slot = g_project->_->materials->buffer[mat_index];
		o->material           = make_mesh_preview_viewport(slot);
		any_map_set(tab_meshes_override_map, uid_key, i32_to_string(mat_index));
	}
}

i32 tab_meshes_get_override(mesh_object_t *o) {
	if (tab_meshes_override_map == NULL) {
		return -1;
	}
	char *v = any_map_get(tab_meshes_override_map, i32_to_string(o->base->uid));
	return v != NULL ? parse_int(v) : -1;
}

void tab_meshes_on_material_deleted(i32 deleted_index) {
	if (tab_meshes_override_map == NULL) {
		return;
	}
	bool changed = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o   = g_project->_->paint_objects->buffer[i];
		i32            idx = tab_meshes_get_override(o);
		if (idx < 0) {
			continue;
		}
		if (idx == deleted_index) {
			tab_meshes_set_override(o, -1); // Reset
			changed = true;
		}
		else if (idx > deleted_index) {
			tab_meshes_set_override(o, idx - 1); // Offset by deleted material
			changed = true;
		}
	}
	if (changed) {
		g_project->mesh_materials = i32_array_create(0);
		g_context->ddirty         = 2;
	}
}

void tab_meshes_on_material_reordered(i32 old_index, i32 new_index) {
	if (tab_meshes_override_map == NULL || old_index == new_index) {
		return;
	}
	bool changed = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o   = g_project->_->paint_objects->buffer[i];
		i32            idx = tab_meshes_get_override(o);
		if (idx < 0) {
			continue;
		}
		i32 new_idx = idx;
		if (idx == old_index) {
			new_idx = new_index;
		}
		else if (old_index < new_index && idx > old_index && idx <= new_index) {
			new_idx = idx - 1;
		}
		else if (old_index > new_index && idx >= new_index && idx < old_index) {
			new_idx = idx + 1;
		}
		if (new_idx != idx) {
			tab_meshes_set_override(o, new_idx);
			changed = true;
		}
	}
	if (changed) {
		g_project->mesh_materials = i32_array_create(0);
		g_context->ddirty         = 2;
	}
}

void tab_meshes_draw_context_menu_delete_next_frame(mesh_object_t *o) {
	data_delete_mesh(o->data->_->handle);
	mesh_object_remove(o);
	tab_stages_prune();
	g_context->paint_object = context_main_object();
	util_mesh_merge(NULL);
	g_context->ddirty                                 = 2;
	ui_base_hwnds->buffer[TAB_AREA_SIDEBAR0]->redraws = 2;
}

void tab_meshes_draw_context_menu_delete(mesh_object_t *o) {
	array_remove(g_project->_->paint_objects, o);
	while (o->base->children->length > 0) {
		object_t *child = o->base->children->buffer[0];
		object_set_parent(child, NULL);
		if (g_project->_->paint_objects->buffer[0]->base != child) {
			object_set_parent(child, g_project->_->paint_objects->buffer[0]->base);
		}
		if (o->base->children->length == 0) {
			g_project->_->paint_objects->buffer[0]->base->transform->scale = o->base->transform->scale;
			transform_build_matrix(g_project->_->paint_objects->buffer[0]->base->transform);
		}
	}
	sys_notify_on_next_frame(tab_meshes_draw_context_menu_delete_next_frame, o);
}

static char *f32_to_string2(float f) {
	return f32_to_string((int)(f * 100) / 100.0);
}

void tab_meshes_duplicate_next_frame(void *_) {
	sim_duplicate();
}

void tab_meshes_merge_geometry_next_frame(void *_) {
	util_mesh_merge_geometry();
}

void tab_meshes_draw_transform_loc(mesh_object_t *o, char *ns) {
	transform_t *t       = o->base->transform;
	bool         changed = false;
	f32          f       = 0.0;

	ui_handle_t *h = ui_handle(string("%s%s", __ID__, ns));
	h->text        = string_copy(f32_to_string2(t->loc.x));
	f              = parse_float(ui_text_input(h, "X", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed  = true;
		t->loc.x = f;
	}

	h       = ui_handle(string("%s%s", __ID__, ns));
	h->text = string_copy(f32_to_string2(t->loc.y));
	f       = parse_float(ui_text_input(h, "Y", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed  = true;
		t->loc.y = f;
	}

	h       = ui_handle(string("%s%s", __ID__, ns));
	h->text = string_copy(f32_to_string2(t->loc.z));
	f       = parse_float(ui_text_input(h, "Z", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed  = true;
		t->loc.z = f;
	}

	if (changed) {
		transform_build_matrix(t);
		transform_compute_dim(t);
	}
}

void tab_meshes_draw_transform_rot(mesh_object_t *o, char *ns) {
	transform_t *t   = o->base->transform;
	vec4_t       rot = quat_get_euler(t->rot);
	rot              = vec4_mult(rot, 180 / 3.141592);
	bool changed     = false;
	f32  f           = 0.0;

	ui_handle_t *h = ui_handle(string("%s%s", __ID__, ns));
	h->text        = string_copy(f32_to_string2(rot.x));
	f              = parse_float(ui_text_input(h, "X", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed = true;
		rot.x   = f;
	}

	h       = ui_handle(string("%s%s", __ID__, ns));
	h->text = string_copy(f32_to_string2(rot.y));
	f       = parse_float(ui_text_input(h, "Y", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed = true;
		rot.y   = f;
	}

	h       = ui_handle(string("%s%s", __ID__, ns));
	h->text = string_copy(f32_to_string2(rot.z));
	f       = parse_float(ui_text_input(h, "Z", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed = true;
		rot.z   = f;
	}

	if (changed) {
		rot    = vec4_mult(rot, 3.141592 / 180.0);
		t->rot = quat_from_euler(rot.x, rot.y, rot.z);
		transform_build_matrix(t);
		transform_compute_dim(t);
	}
}

void tab_meshes_draw_transform_scale(mesh_object_t *o, char *ns) {
	transform_t *t       = o->base->transform;
	bool         changed = false;
	f32          f       = 0.0;

	ui_handle_t *h = ui_handle(string("%s%s", __ID__, ns));
	h->text        = string_copy(f32_to_string2(t->scale.x));
	f              = parse_float(ui_text_input(h, "X", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed    = true;
		t->scale.x = f;
	}

	h       = ui_handle(string("%s%s", __ID__, ns));
	h->text = string_copy(f32_to_string2(t->scale.y));
	f       = parse_float(ui_text_input(h, "Y", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed    = true;
		t->scale.y = f;
	}

	h       = ui_handle(string("%s%s", __ID__, ns));
	h->text = string_copy(f32_to_string2(t->scale.z));
	f       = parse_float(ui_text_input(h, "Z", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed    = true;
		t->scale.z = f;
	}

	if (changed) {
		transform_build_matrix(t);
		transform_compute_dim(t);
	}
}

void tab_meshes_draw_context_menu() {
	i32            i = _tab_meshes_draw_i;
	mesh_object_t *o = g_project->_->paint_objects->buffer[i];

	if (ui_menu_button(tr("Export"), "", ICON_EXPORT)) {
		g_context->export_mesh_index = i + 1;
		box_export_show_mesh();
		return;
	}
	if (g_project->_->paint_objects->length > 1 && ui_menu_button(tr("Delete"), "delete", ICON_DELETE)) {
		sys_notify_on_next_frame(tab_meshes_draw_context_menu_delete, o);
		return;
	}
	if (ui_menu_button(tr("Duplicate"), "ctrl+d", ICON_DUPLICATE)) {
		sim_duplicate();
		return;
	}
	if (ui_menu_button(tr("Edit Script"), "", ICON_EDIT)) {
		tab_timeline_edit_script(g_project->_->layers->length + i, 0);
		return;
	}

#ifdef WITH_PLUGINS
	if (ui_menu_button(tr("UV Unwrap"), "", ICON_NONE)) {
		plugin_uv_unwrap_per_object_button(o);
		return;
	}
#endif

	transform_t *t = o->base->transform;

	ui_row4();
	ui_text("Loc", UI_ALIGN_LEFT, 0x00000000);
	tab_meshes_draw_transform_loc(o, "menu");

	ui_row4();
	ui_text("Rot", UI_ALIGN_LEFT, 0x00000000);
	tab_meshes_draw_transform_rot(o, "menu");

	ui_row4();
	ui_text("Scale", UI_ALIGN_LEFT, 0x00000000);
	tab_meshes_draw_transform_scale(o, "menu");

	ui_row4();
	ui_text("Dim", UI_ALIGN_LEFT, 0x00000000);

	bool         changed = false;
	f32          f       = 0.0;
	ui_handle_t *h;

	h       = ui_handle(__ID__);
	h->text = string_copy(f32_to_string2(t->dim.x));
	f       = parse_float(ui_text_input(h, "X", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed  = true;
		t->dim.x = f;
	}

	h       = ui_handle(__ID__);
	h->text = string_copy(f32_to_string2(t->dim.y));
	f       = parse_float(ui_text_input(h, "Y", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed  = true;
		t->dim.y = f;
	}

	h       = ui_handle(__ID__);
	h->text = string_copy(f32_to_string2(t->dim.z));
	f       = parse_float(ui_text_input(h, "Z", UI_ALIGN_LEFT, true, false));
	if (h->changed) {
		changed  = true;
		t->dim.z = f;
	}

	if (changed) {
		transform_build_matrix(t);
		transform_compute_dim(t);
	}

	// Material override
	string_array_t *mat_combo = string_array_create(0);
	string_array_push(mat_combo, ""); // Empty = use painted layers
	for (i32 mi = 0; mi < g_project->_->materials->length; ++mi) {
		string_array_push(mat_combo, g_project->_->materials->buffer[mi]->canvas->name);
	}

	ui_handle_t *hmat = ui_handle(__ID__);
	hmat->i           = tab_meshes_get_override(o) + 1; // 0 = none
	ui_combo(hmat, mat_combo, tr("Material"), true, UI_ALIGN_LEFT, false);
	if (hmat->changed) {
		tab_meshes_set_override(o, hmat->i - 1);
		g_context->ddirty         = 2;
		g_project->mesh_materials = i32_array_create(0);
	}

	// Parent
	string_array_t *parent_combo = string_array_create(0);
	string_array_push(parent_combo, ""); // Empty = no parent
	i32 parent_idx = 0;
	for (i32 pi = 0; pi < g_project->_->paint_objects->length; ++pi) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[pi];
		string_array_push(parent_combo, p->base->name);
		if (o->base->parent == p->base) {
			parent_idx = pi + 1;
		}
	}

	ui_handle_t *hparent = ui_handle(__ID__);
	hparent->i           = parent_idx;
	ui_combo(hparent, parent_combo, tr("Parent"), true, UI_ALIGN_LEFT, false);
	if (hparent->changed) {
		object_t *new_parent = hparent->i == 0 ? NULL : g_project->_->paint_objects->buffer[hparent->i - 1]->base;
		object_set_parent(o->base, new_parent);
		g_project->mesh_parents = i32_array_create(0);
	}

	// Physics
	if (g_config->experimental) {
		asim_body_t    *pb         = o->base->_->body;
		string_array_t *phys_combo = string_array_create(0);
		string_array_push(phys_combo, ""); // Empty = no physics
		string_array_push(phys_combo, tr("Sphere"));
		string_array_push(phys_combo, tr("Mesh"));

		ui_handle_t *hphys = ui_handle(__ID__);
		hphys->i           = pb == NULL ? 0 : (pb->shape == ASIM_SHAPE_SPHERE ? 1 : 2);
		ui_combo(hphys, phys_combo, tr("Physics"), true, UI_ALIGN_LEFT, false);
		if (hphys->changed) {
			if (pb != NULL) {
				asim_body_remove(pb);
				pb = NULL;
			}
			if (hphys->i > 0) {
				sim_add_body(o->base, hphys->i == 1 ? ASIM_SHAPE_SPHERE : ASIM_SHAPE_MESH, hphys->i == 1 ? 1.0 : 0.0);
				pb = o->base->_->body;
			}
		}

		if (pb != NULL) {
			ui_handle_t *hmass = ui_handle(__ID__);
			hmass->f           = pb->mass;
			ui_slider(hmass, tr("Mass"), 0.0, 10.0, true, 100, true, UI_ALIGN_LEFT, true);
			if (hmass->changed) {
				asim_body_set_mass(pb, hmass->f); // Zero mass = static
				ui_menu_keep_open = true;
			}
		}
	}

	if (g_ui->changed || g_ui->is_typing) {
		ui_menu_keep_open = true;
	}
}

void tab_meshes_draw_edit() {

#ifdef WITH_PLUGINS
	if (ui_menu_button(tr("UV Unwrap"), "", ICON_NONE)) {
		plugin_uv_unwrap_button();
	}
#endif

	if (ui_menu_button(tr("Edit UV Map"), "", ICON_NONE)) {
		ui_base_show_2d_view(VIEW_2D_TYPE_UVMAP);
	}

	ui_menu_separator();

	if (ui_menu_sub_button(ui_handle(__ID__), tr("Calculate Normals"))) {
		ui_menu_sub_begin(2);
		if (ui_menu_button(tr("Smooth"), "", ICON_NONE)) {
			util_mesh_calc_normals(true);
			g_context->ddirty = 2;
		}
		if (ui_menu_button(tr("Flat"), "", ICON_NONE)) {
			util_mesh_calc_normals(false);
			g_context->ddirty = 2;
		}
		ui_menu_sub_end();
	}

	if (ui_menu_button(tr("Flip Normals"), "", ICON_NONE)) {
		util_mesh_flip_normals();
		g_context->ddirty = 2;
	}

	if (ui_menu_button(tr("Geometry to Origin"), "", ICON_NONE)) {
		util_mesh_to_origin();
		g_context->ddirty = 2;
	}

	g_ui->enabled = g_project->_->paint_objects->length > 1;
	if (ui_menu_button(tr("Merge Geometry"), "", ICON_NONE)) {
		sys_notify_on_next_frame(&tab_meshes_merge_geometry_next_frame, NULL);
	}
	g_ui->enabled = true;

	if (ui_menu_button(tr("Apply Displacement"), "", ICON_NONE)) {
		util_mesh_apply_displacement(g_project->_->layers->buffer[0]->texpaint_pack, 0.1, 1.0);
		util_mesh_calc_normals(false);
		g_context->ddirty = 2;
	}

	if (ui_menu_sub_button(ui_handle(__ID__), tr("Rotate"))) {
		ui_menu_sub_begin(3);
		if (ui_menu_button(tr("X"), "", ICON_NONE)) {
			util_mesh_swap_axis(1, 2);
			g_context->ddirty = 2;
			ui_menu_keep_open = true;
		}
		if (ui_menu_button(tr("Y"), "", ICON_NONE)) {
			util_mesh_swap_axis(2, 0);
			g_context->ddirty = 2;
			ui_menu_keep_open = true;
		}
		if (ui_menu_button(tr("Z"), "", ICON_NONE)) {
			util_mesh_swap_axis(0, 1);
			g_context->ddirty = 2;
			ui_menu_keep_open = true;
		}
		ui_menu_sub_end();
	}

	ui_menu_separator();

	if (ui_menu_sub_button(ui_handle(__ID__), tr("Modifiers"))) {
		ui_menu_sub_begin(4);
		if (ui_menu_button(tr("Decimate"), "", ICON_NONE)) {
			util_mesh_decimate(0.5);
		}
		if (ui_menu_button(tr("Smooth"), "", ICON_NONE)) {
			util_mesh_smooth();
		}
		if (ui_menu_button(tr("Subdivide"), "", ICON_NONE)) {
			util_mesh_subdivide();
		}
		if (ui_menu_button(tr("Bevel"), "", ICON_NONE)) {
			util_mesh_bevel(0.1);
		}
		ui_menu_sub_end();
	}
}

void tab_meshes_append_shape(char *mesh_name) {
	scene_t     *scene_raw = NULL;
	mesh_data_t *raw       = NULL;
	if (string_equals(mesh_name, "sphere")) {
		raw_mesh_t *mesh = geom_make_uv_sphere(1, 128, 64, true, 1.0);
		raw              = import_mesh_raw_mesh(mesh);
	}
	else if (string_equals(mesh_name, "sphere_2048")) {
		raw_mesh_t *mesh = geom_make_uv_sphere(1, 4096, 2048, true, 1.0);
		raw              = import_mesh_raw_mesh(mesh);
	}
	else if (string_equals(mesh_name, "plane")) {
		raw_mesh_t *mesh = geom_make_plane(1, 1, 2, 2, 1.0);
		raw              = import_mesh_raw_mesh(mesh);
	}
	else if (string_equals(mesh_name, "plane_2048")) {
		raw_mesh_t *mesh = geom_make_plane(1, 1, 2048, 2048, 1.0);
		raw              = import_mesh_raw_mesh(mesh);
	}
	else {
		buffer_t *b = iron_load_blob(string("%smeshes/%s.arm", data_path(), mesh_name));
		scene_raw   = armpack_decode(b);
		raw         = scene_raw->mesh_datas->buffer[0];
	}

	// util_mesh_pack_uvs(raw->vertex_arrays->buffer[2]->values);
	mesh_data_t *md   = mesh_data_create(raw);
	md->_->handle     = md->name;
	mesh_object_t *mo = scene_add_mesh_object(md, g_project->_->paint_objects->buffer[0]->material, NULL);

	// Ensure unique name
	char *ext = "";
	i32   n   = 0;
	while (!_import_mesh_is_unique_name(string("%s%s", md->name, ext))) {
		ext = string_copy(_import_mesh_number_ext(++n));
	}
	mo->base->name = string("%s%s", md->name, ext);

	obj_t *o      = GC_ALLOC_INIT(obj_t, {0});
	o->_          = GC_ALLOC_INIT(obj_runtime_t, {._gc = scene_raw});
	mo->base->raw = o;
	any_map_set(data_cached_meshes, md->_->handle, md);
	any_array_push(g_project->_->paint_objects, mo);
	g_context->paint_object = mo;
}

static icon_t tab_meshes_mesh_name_to_icon(char *s) {
	if (starts_with(s, "box"))
		return ICON_CUBE;
	if (starts_with(s, "cone"))
		return ICON_CONE;
	if (starts_with(s, "cylinder"))
		return ICON_CYLINDER;
	if (starts_with(s, "torus"))
		return ICON_TORUS;
	if (starts_with(s, "plane"))
		return ICON_PLANE;
	if (starts_with(s, "sphere"))
		return ICON_UVSPHERE;
	return ICON_NONE;
}

void tab_meshes_draw_new() {
	project_fetch_default_meshes();
	for (i32 i = 0; i < project_default_mesh_list->length; ++i) {
		if (ui_menu_button(project_default_mesh_list->buffer[i], "", tab_meshes_mesh_name_to_icon(project_default_mesh_list->buffer[i]))) {
			tab_meshes_append_shape(project_default_mesh_list->buffer[i]);
		}
	}
}

void tab_meshes_draw_import() {
	if (ui_menu_button(tr("Replace Existing"), any_map_get(g_keymap, "file_import_assets"), ICON_NONE)) {
		project_import_mesh(true, NULL);
	}
	if (ui_menu_button(tr("Append"), "", ICON_NONE)) {
		project_append_mesh();
	}
}

static vec4_t aabb_center(mesh_data_t *raw) {
	vec4_t          aabb_min  = (vec4_t){-0.01, -0.01, -0.01, 0.0};
	vec4_t          aabb_max  = (vec4_t){0.01, 0.01, 0.01, 0.0};
	i32             i         = 0;
	vertex_array_t *positions = mesh_data_get_vertex_array(raw, "pos");
	while (i < positions->values->length) {
		if (positions->values->buffer[i] > aabb_max.x) {
			aabb_max.x = positions->values->buffer[i];
		}
		if (positions->values->buffer[i + 1] > aabb_max.y) {
			aabb_max.y = positions->values->buffer[i + 1];
		}
		if (positions->values->buffer[i + 2] > aabb_max.z) {
			aabb_max.z = positions->values->buffer[i + 2];
		}
		if (positions->values->buffer[i] < aabb_min.x) {
			aabb_min.x = positions->values->buffer[i];
		}
		if (positions->values->buffer[i + 1] < aabb_min.y) {
			aabb_min.y = positions->values->buffer[i + 1];
		}
		if (positions->values->buffer[i + 2] < aabb_min.z) {
			aabb_min.z = positions->values->buffer[i + 2];
		}
		i += 4;
	}
	f32 f = raw->scale_pos / 32767.0f;
	return (vec4_t){(aabb_min.x + aabb_max.x) / 2.0f * f, (aabb_min.y + aabb_max.y) / 2.0f * f, (aabb_min.z + aabb_max.z) / 2.0f * f, 0.0f};
}

void tab_meshes_make_preview(mesh_object_t *o) {
	if (tab_meshes_preview_map == NULL) {
		tab_meshes_preview_map = any_map_create();
		gc_root(tab_meshes_preview_map);
	}

	char          *uid_key = i32_to_string(o->base->uid);
	gpu_texture_t *image   = any_map_get(tab_meshes_preview_map, uid_key);
	if (image == NULL) {
		image = gpu_create_render_target(util_render_material_preview_size, util_render_material_preview_size, GPU_TEXTURE_FORMAT_RGBA64);
		any_map_set(tab_meshes_preview_map, uid_key, image);
	}

	g_context->material_preview = true;

	slot_material_t *mat = GC_ALLOC_INIT(slot_material_t, {0});
	mat->image           = image;
	mat->image_icon      = gpu_create_render_target(50, 50, GPU_TEXTURE_FORMAT_RGBA64);
	mat->preview_ready   = true;
	mat->canvas          = armpack_decode(slot_material_default_canvas);
	mat->canvas          = util_clone_canvas(mat->canvas); // Clone to create GC references

	slot_material_t *_material = g_context->material;
	g_context->material        = mat;

	mesh_object_t_array_t *_scene_meshes = scene_meshes;
	gc_unroot(scene_meshes);
	scene_meshes = any_array_create_from_raw((void *[]){o}, 1);
	gc_root(scene_meshes);

	mesh_object_t *painto   = g_context->paint_object;
	g_context->paint_object = o;

	material_data_t *_override = o->material;
	o->material                = g_project->_->materials->buffer[0]->data;

	g_context->saved_camera = scene_camera->base->transform->local;
	mat4_t m =
	    (mat4_t){0.9146286343879498, 0.404295023959927,   0.000007410128652369705, 0, -0.0032648027153306235, 0.007367569133732468, 0.9999675337275382,   0,
	             0.404281837254303,  -0.9145989516155143, 0.008058532943908717,    0, 0.4659988049397712,     -1.0687517188018691,  0.015935682577325486, 1};
	transform_set_matrix(scene_camera->base->transform, m);
	f32 saved_fov           = scene_camera->data->fov;
	scene_camera->data->fov = 0.4;
	viewport_update_camera_type(CAMERA_TYPE_PERSPECTIVE);

	world_data_t *probe           = scene_world;
	f32           _probe_strength = probe->strength;
	probe->strength               = 2;
	f32 _envmap_angle             = g_context->envmap_angle;
	g_context->envmap_angle       = 0.0;

	gpu_texture_t *_envmap = scene_world->_->envmap;
	scene_world->_->envmap = g_context->preview_envmap;

	// Fit into camera
	vec4_t saved_scale = o->base->transform->scale;
	vec4_t saved_loc   = o->base->transform->loc;
	quat_t saved_rot   = o->base->transform->rot;
	{
		vec4_t aabb = mesh_data_calculate_aabb(o->data);
		f32    r    = math_max(aabb.x, math_max(aabb.y, aabb.z));
		f32    s    = 0.5 / r;
		if (o->base->parent == NULL || o->base->parent == _scene_scene_parent) {
			s *= o->base->transform->scale.x;
		}
		s *= o->data->scale_pos;
		vec4_t center             = aabb_center(o->data);
		o->base->transform->scale = (vec4_t){s, s, s, 1.0};
		o->base->transform->loc   = (vec4_t){-s * center.x, -s * center.y, -s * center.z, 1.0};
		o->base->transform->rot   = (quat_t){0, 0, 0, 1};
		transform_build_matrix(o->base->transform);
	}

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
	o->base->transform->scale = saved_scale;
	o->base->transform->loc   = saved_loc;
	o->base->transform->rot   = saved_rot;
	transform_build_matrix(o->base->transform);

	o->material = _override;

	gc_unroot(scene_meshes);
	scene_meshes = _scene_meshes;
	gc_root(scene_meshes);
	g_context->paint_object = painto;

	transform_set_matrix(scene_camera->base->transform, g_context->saved_camera);
	viewport_update_camera_type(g_context->camera_type);
	scene_camera->data->fov = saved_fov;
	camera_object_build_proj(scene_camera, -1.0);
	camera_object_build_mat(scene_camera);

	probe->strength         = _probe_strength;
	g_context->envmap_angle = _envmap_angle;
	scene_world->_->envmap  = _envmap;

	g_context->material = _material;
	gpu_delete_texture(mat->image_icon);

	make_material_parse_mesh_material();
	g_context->ddirty = 0;
}

void tab_meshes_apply_visible(mesh_object_t *o) {
	stage_t *stage = tab_stages_get_stage();
	if (stage != NULL) {
		i32 idx = string_array_index_of(stage->objects, o->base->name);
		if (o->base->visible && idx < 0) {
			string_array_push(stage->objects, o->base->name);
		}
		else if (!o->base->visible && idx >= 0) {
			array_splice(stage->objects, idx, 1);
		}
	}

	util_mesh_merge(util_mesh_get_visible());
	util_uv_uvmap_cached       = false;
	util_uv_trianglemap_cached = false;
	util_uv_dilatemap_cached   = false;
	g_context->ddirty          = 2;
}

void tab_meshes_draw_mesh_slot(mesh_object_t *o, i32 i) {
	i32 step   = g_theme->ELEMENT_H;
	f32 center = (step / 2.0) * UI_SCALE();
	f32 uiw    = g_ui->_w;
	f32 uix    = g_ui->_x;
	f32 uiy    = g_ui->_y;

	// Highlight drag destination
	if (base_is_dragging && base_drag_mesh != NULL && context_in_meshes()) {
		f32 absy   = g_ui->_window_y + g_ui->_y;
		f32 checkw = (g_ui->_window_w / 100.0 * 8) / (float)UI_SCALE();
		f32 w      = (g_ui->_window_w / (float)UI_SCALE() - 2) - checkw;
		if (mouse_y > absy + step && mouse_y < absy + step * 3) {
			g_context->drag_dest = i + 1; // Insert after this slot
			ui_fill(checkw, step * 2, w, 2 * UI_SCALE(), g_theme->HIGHLIGHT_COL);
		}
		else if (i == 0 && mouse_y < absy + step) {
			g_context->drag_dest = 0; // Insert before the first slot
			ui_fill(checkw, 0, w, 2 * UI_SCALE(), g_theme->HIGHLIGHT_COL);
		}
	}

	// Eye icon
	f32_array_t *row = f32_array_create_from_raw(
	    (f32[]){
	        0.08,
	    },
	    1);
	ui_row(row);
	gpu_texture_t *icons = resource_get("icons.k");
	rect_t        *r     = resource_tile18(icons, o->base->visible ? ICON18_EYE_ON : ICON18_EYE_OFF);
	g_ui->_x             = uix + 4;
	g_ui->_y             = uiy + 3 + center;
	i32 col              = g_theme->HOVER_COL + 0x00282828;
	if (ui_sub_image(icons, col, 18 * UI_SCALE(), r->x, r->y, r->w, r->h) == UI_STATE_RELEASED) {
		o->base->visible = !o->base->visible;
		tab_meshes_apply_visible(o);
	}

	// Mesh icon
	i32 icon_h = (UI_ELEMENT_H() - 3) * 2;
	g_ui->_x   = uix + uiw * 0.08;
	g_ui->_y   = uiy + 3;
	g_ui->_w   = math_max(uiw * 0.16, icon_h);

	char          *uid_key = i32_to_string(o->base->uid);
	gpu_texture_t *preview = tab_meshes_preview_map != NULL ? any_map_get(tab_meshes_preview_map, uid_key) : NULL;
	if (preview != NULL) {
		ui_image(preview, 0xffffffff, icon_h);
		if (g_ui->is_hovered) {
			ui_tooltip_image(preview, 0);
			ui_tooltip(o->base->name);
		}
	}
	else {
		rect_t *rect = resource_tile50(icons, ICON_CUBE);
		ui_sub_image(icons, g_theme->BUTTON_COL, icon_h, rect->x, rect->y, rect->w, rect->h);
		sys_notify_on_next_frame(tab_meshes_make_preview, o);
	}

	// Material override
	i32 override_idx = tab_meshes_get_override(o);
	f32 name_right   = uix + uiw;
	if (override_idx >= 0 && override_idx < g_project->_->materials->length) {
		slot_material_t *slot  = g_project->_->materials->buffer[override_idx];
		i32              mat_h = icon_h * 0.9;
		g_ui->_x               = uix + uiw - mat_h - 10 * UI_SCALE();
		g_ui->_y               = uiy + 5;
		g_ui->_w               = mat_h;
		name_right -= mat_h + 8 * UI_SCALE();
		gpu_texture_t *micon = slot->preview_ready ? slot->image_icon : NULL;
		if (micon != NULL && ui_image(micon, 0xffffffff, mat_h) == UI_STATE_RELEASED) {
			context_select_material(override_idx);
		}
		if (g_ui->is_hovered) {
			ui_tooltip(slot->canvas->name);
		}
	}

	// Mesh name
	f32 name_x = math_max(uix + uiw * 0.25, uix + uiw * 0.08 + icon_h + 4 * UI_SCALE());
	g_ui->_x   = name_x;
	g_ui->_y   = uiy + center;
	g_ui->_w   = name_right - name_x;

	bool over_name = g_ui->input_x > g_ui->_window_x + name_x && g_ui->input_x < g_ui->_window_x + name_right;

	if (tab_meshes_mesh_name_edit == o->base->uid) {
		tab_meshes_mesh_name_handle->text = string_copy(o->base->name);
		char *new_name                    = string_copy(ui_text_input(tab_meshes_mesh_name_handle, "", UI_ALIGN_LEFT, true, false));
		tab_stages_rename_object(o->base->name, new_name);
		o->base->name = new_name;
		o->data->name = string_copy(o->base->name);
		if (g_ui->text_selected_handle != tab_meshes_mesh_name_handle) {
			tab_meshes_mesh_name_edit = -1;
		}
	}
	else {
		ui_text(o->base->name, UI_ALIGN_LEFT, 0x00000000);

		// Row interaction
		f32  row_left = uix + uiw * 0.08;
		bool hovered  = g_ui->enabled && g_ui->input_enabled && g_ui->input_x > g_ui->_window_x + row_left && g_ui->input_x < g_ui->_window_x + uix + uiw &&
		               g_ui->input_y > g_ui->_window_y + uiy && g_ui->input_y < g_ui->_window_y + uiy + step * 2 * UI_SCALE();
		if (hovered) {
			ui_tooltip(o->base->name);
			if (g_ui->input_started) {
				g_context->paint_object = o;
				tab_meshes_set_drag_mesh(o, -(mouse_x - uix - g_ui->_window_x - 3), -(mouse_y - uiy - g_ui->_window_y + 1));
			}
			if (g_ui->input_released) {
				if (sys_time() - g_context->select_time < 0.2) {
					if (over_name) {
						// Double click name to rename
						tab_meshes_mesh_name_edit         = o->base->uid;
						tab_meshes_mesh_name_handle->text = string_copy(o->base->name);
						ui_start_text_edit(tab_meshes_mesh_name_handle, UI_ALIGN_LEFT);
					}
					else {
						// Double click to show only this mesh
						tab_layers_apply_filter(i + 1);
					}
				}
				if (sys_time() - g_context->select_time > 0.2) {
					g_context->select_time = sys_time();
				}
			}
			if (g_ui->input_released_r) {
				g_context->paint_object = o;
				_tab_meshes_draw_i      = i;
				ui_menu_draw(&tab_meshes_draw_context_menu, -1, -1);
			}
		}

		bool in_focus = g_ui->input_x > g_ui->_window_x && g_ui->input_x < g_ui->_window_x + g_ui->_window_w && g_ui->input_y > g_ui->_window_y &&
		                g_ui->input_y < g_ui->_window_y + g_ui->_window_h;
		if (in_focus && !g_ui->is_typing && g_ui->is_delete_down && g_project->_->paint_objects->length > 1) {
			g_ui->is_delete_down = false;
			sys_notify_on_next_frame(&tab_meshes_draw_context_menu_delete, g_context->paint_object);
		}
		if (in_focus && g_ui->is_ctrl_down && g_ui->is_key_pressed && g_ui->key_code == KEY_CODE_D) {
			g_ui->is_key_pressed = false;
			sys_notify_on_next_frame(&tab_meshes_duplicate_next_frame, NULL);
		}
	}

	g_ui->_x = uix;
	g_ui->_y = uiy + step * 2 * UI_SCALE();
	g_ui->_w = uiw;

	// Separator line
	ui_fill(0, 0, (g_ui->_w / (float)UI_SCALE() - 2), 1 * UI_SCALE(), g_theme->SEPARATOR_COL);

	// Highlight selected
	if (g_context->paint_object == o) {
		ui_rect(1, -step * 2 - 1, g_ui->_w / (float)UI_SCALE() - 2, step * 2 + 1, g_theme->HIGHLIGHT_COL, 2);
	}
}

void tab_meshes_highlight_odd_lines() {
	i32 step   = g_theme->ELEMENT_H * 2;
	i32 full_h = g_ui->_window_h - ui_base_hwnds->buffer[0]->scroll_offset;
	for (i32 i = 0; i < math_floor(full_h / (float)step); ++i) {
		if (i % 2 == 0) {
			ui_fill(0, i * step, (g_ui->_w / (float)UI_SCALE() - 2), step, base_darker(g_theme->WINDOW_BG_COL, 0x00040404));
		}
	}
}

void tab_meshes_draw(ui_handle_t *htab) {
	if (ui_tab(htab, tr("Meshes"), false, -1, false) && g_ui->_window_h > ui_statusbar_default_h * UI_SCALE()) {

		ui_begin_sticky();
		f32_array_t *row = f32_array_create_from_raw(
		    (f32[]){
		        -70,
		        -70,
		        -70,
		    },
		    3);
		ui_row(row);

		if (ui_icon_button(tr("New"), ICON_PLUS, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_meshes_draw_new, -1, -1);
		}
		if (ui_icon_button(tr("Import"), ICON_IMPORT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_meshes_draw_import, -1, -1);
		}
		if (g_ui->is_hovered)
			ui_tooltip(tr("Import mesh file"));

		if (ui_icon_button(tr("Edit"), ICON_EDIT, UI_ALIGN_CENTER)) {
			ui_menu_draw(&tab_meshes_draw_edit, -1, -1);
		}

		ui_end_sticky();
		g_ui->_y += 2;

		tab_meshes_highlight_odd_lines();

		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *o = g_project->_->paint_objects->buffer[i];
			tab_meshes_draw_mesh_slot(o, i);
		}
	}
}

void tab_meshes_reset_preview_map() {
	if (tab_meshes_preview_map == NULL) {
		return;
	}

	any_array_t *keys = map_keys(tab_meshes_preview_map);
	for (i32 i = 0; i < keys->length; ++i) {
		gpu_texture_t *image = any_map_get(tab_meshes_preview_map, keys->buffer[i]);
		gpu_delete_texture(image);
	}
	gc_unroot(tab_meshes_preview_map);
	tab_meshes_preview_map = NULL;
}
