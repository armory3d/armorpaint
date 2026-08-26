
#include "../global.h"

char        *box_append_path              = NULL;
project_t   *box_append_project           = NULL;
i32_array_t *box_append_mesh_selected     = NULL;
i32_array_t *box_append_material_selected = NULL;

static i32_array_t *box_append_zeros(i32 count) {
	i32_array_t *a = i32_array_create(count);
	for (i32 i = 0; i < count; ++i) {
		a->buffer[i] = 0;
	}
	return a;
}

static bool box_append_has_selection() {
	if (box_append_mesh_selected != NULL) {
		for (i32 i = 0; i < box_append_mesh_selected->length; ++i) {
			if (box_append_mesh_selected->buffer[i] != 0) {
				return true;
			}
		}
	}
	if (box_append_material_selected != NULL) {
		for (i32 i = 0; i < box_append_material_selected->length; ++i) {
			if (box_append_material_selected->buffer[i] != 0) {
				return true;
			}
		}
	}
	return false;
}

static bool box_append_mesh_exists(i32 i) {
	mesh_data_t *raw = box_append_project->mesh_datas->buffer[i];
	if (raw == NULL || raw->name == NULL || string_equals(raw->name, "")) {
		return false;
	}
	i32   source_index;
	char *object_name;
	char *name = (i > 0 && util_mesh_link_parse(raw->name, &source_index, &object_name)) ? object_name : raw->name;
	return import_arm_object_name_exists(name);
}

static bool box_append_material_exists(i32 i) {
	ui_node_canvas_t *c = box_append_project->material_nodes->buffer[i];
	return c != NULL && import_arm_material_name_index(c->name) >= 0;
}

static i32 box_append_mesh_material_index(i32 mesh_i) {
	if (box_append_project == NULL || box_append_project->mesh_materials == NULL) {
		return -1;
	}
	if (mesh_i < 0 || mesh_i >= box_append_project->mesh_materials->length) {
		return -1;
	}
	return box_append_project->mesh_materials->buffer[mesh_i];
}

static void box_append_select_mesh_material(i32 mesh_i) {
	i32 mat_i = box_append_mesh_material_index(mesh_i);
	if (mat_i < 0 || box_append_material_selected == NULL || mat_i >= box_append_material_selected->length) {
		return;
	}
	if (box_append_material_exists(mat_i)) {
		return;
	}
	box_append_material_selected->buffer[mat_i] = 1;
}

static char *box_append_mesh_name(mesh_data_t *raw, i32 i) {
	if (raw == NULL || raw->name == NULL || string_equals(raw->name, "")) {
		return string("%s %s", tr("Mesh"), i32_to_string(i + 1));
	}
	i32   source_index;
	char *object_name;
	if (util_mesh_link_parse(raw->name, &source_index, &object_name)) {
		return object_name;
	}
	return raw->name;
}

static char *box_append_material_name(ui_node_canvas_t *c, i32 i) {
	if (c == NULL || c->name == NULL || string_equals(c->name, "")) {
		return string("%s %s", tr("Material"), i32_to_string(i + 1));
	}
	return c->name;
}

void box_append_on_hide() {
	if (box_append_path != NULL) {
		data_delete_blob(box_append_path);
	}
	box_append_path              = NULL;
	box_append_project           = NULL;
	box_append_mesh_selected     = NULL;
	box_append_material_selected = NULL;
}

void box_append_draw() {
	if (box_append_project == NULL) {
		return;
	}

	if (box_append_mesh_selected != NULL && box_append_mesh_selected->length > 0) {
		ui_text(tr("Meshes"), UI_ALIGN_LEFT, 0);
		ui_handle_t *hmeshes = ui_handle(__ID__);
		for (i32 i = 0; i < box_append_mesh_selected->length; ++i) {
			bool exists = box_append_mesh_exists(i);
			if (exists) {
				box_append_mesh_selected->buffer[i] = 0;
			}
			ui_handle_t *h = ui_nest(hmeshes, i);
			h->b           = box_append_mesh_selected->buffer[i] != 0;
			g_ui->enabled  = !exists;
			ui_check(h, box_append_mesh_name(box_append_project->mesh_datas->buffer[i], i), "");
			g_ui->enabled = true;
			if (!exists && h->changed) {
				box_append_mesh_selected->buffer[i] = h->b ? 1 : 0;
				if (h->b) {
					box_append_select_mesh_material(i);
				}
			}
		}
	}

	if (box_append_material_selected != NULL && box_append_material_selected->length > 0) {
		ui_text(tr("Materials"), UI_ALIGN_LEFT, 0);
		ui_handle_t *hmats = ui_handle(__ID__);
		for (i32 i = 0; i < box_append_material_selected->length; ++i) {
			bool exists = box_append_material_exists(i);
			if (exists) {
				box_append_material_selected->buffer[i] = 0;
			}
			ui_handle_t *h = ui_nest(hmats, i);
			h->b           = box_append_material_selected->buffer[i] != 0;
			g_ui->enabled  = !exists;
			ui_check(h, box_append_material_name(box_append_project->material_nodes->buffer[i], i), "");
			g_ui->enabled = true;
			if (!exists && h->changed) {
				box_append_material_selected->buffer[i] = h->b ? 1 : 0;
			}
		}
	}

	ui_end_element();
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	bool has_sel  = box_append_has_selection();
	g_ui->enabled = has_sel;
	if ((ui_icon_button(tr("Append"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) && has_sel) {
		import_arm_append(box_append_project, box_append_path, box_append_mesh_selected, box_append_material_selected);
		ui_box_hide();
	}
	g_ui->enabled = true;
}

static void box_append_show_on_cache_cloud_done(char *abs) {
	box_append_show(string_copy(abs));
}

void box_append_show(char *path) {
	if (starts_with(path, "cloud")) {
#ifdef IRON_ANDROID
		console_toast(tr("Downloading"));
#else
		console_info(tr("Downloading"));
#endif
		file_cache_cloud(path, &box_append_show_on_cache_cloud_done, g_config->server);
		return;
	}

	box_append_path    = string_copy(path);
	buffer_t *b        = data_get_blob(path);
	box_append_project = import_arm_decode_project(b);
	if (box_append_project == NULL) {
		data_delete_blob(path);
		box_append_path = NULL;
		return;
	}

	i32 mesh_count               = box_append_project->mesh_datas != NULL ? box_append_project->mesh_datas->length : 0;
	i32 mat_count                = box_append_project->material_nodes != NULL ? box_append_project->material_nodes->length : 0;
	box_append_mesh_selected     = box_append_zeros(mesh_count);
	box_append_material_selected = box_append_zeros(mat_count);
	ui_box_show_custom(&box_append_draw, 600, 420, &box_append_on_hide, true, tr("Append"));
}
