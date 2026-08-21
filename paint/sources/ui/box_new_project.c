
#include "../global.h"

void project_fetch_default_meshes() {
	if (project_default_mesh_list == NULL) {
		project_default_mesh_list = file_read_directory(string("%s%smeshes", path_data(), PATH_SEP));
		for (i32 i = 0; i < project_default_mesh_list->length; ++i) {
			char *s                              = project_default_mesh_list->buffer[i];
			project_default_mesh_list->buffer[i] = substring(project_default_mesh_list->buffer[i], 0, string_length(s) - 4); // Trim .arm
		}
		any_array_push(project_default_mesh_list, "plane");
		any_array_push(project_default_mesh_list, "plane_2048");
		any_array_push(project_default_mesh_list, "sphere");
		any_array_push(project_default_mesh_list, "sphere_2048");

		if (g_context->project_type == -1) {
			g_context->project_type = string_array_index_of(project_default_mesh_list, "cube_bevel");
		}
	}
}

void project_new_box_draw() {
	project_fetch_default_meshes();

	ui_handle_t *h_project_type = ui_handle(__ID__);
	h_project_type->i           = g_context->project_type;
	g_context->project_type     = ui_combo(h_project_type, project_default_mesh_list, tr("Template"), true, UI_ALIGN_LEFT, true);
	ui_end_element();
	ui_row2();
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER)) {
		ui_box_hide();
	}
	if (ui_icon_button(tr("OK"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) {
		project_new(true);
		ui_box_hide();
	}
}

void project_new_box() {
	ui_box_show_custom(&project_new_box_draw, 400, 200, NULL, true, tr("New Project"));
}
