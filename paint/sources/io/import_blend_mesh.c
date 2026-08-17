
#include "../global.h"

void import_blend_mesh_ui_blender_folder_picked(char *path) {
#ifdef IRON_WINDOWS
	path = string_copy(string_replace_all(path, "\\", "/"));
#endif
	g_config->blender = string_copy(path);
	config_save();
}

void import_blend_mesh_ui() {
	if (g_config->blender == NULL) {
		g_config->blender = "";
	}

	ui_text(tr("Blender Executable"), UI_ALIGN_LEFT, 0x00000000);

	f32_array_t *ar = f32_array_create_from_raw(
	    (f32[]){
	        7 / 8.0,
	        1 / 8.0,
	    },
	    2);
	ui_row(ar);

	ui_handle_t *h    = ui_handle(__ID__);
	h->text           = string_copy(g_config->blender);
	g_config->blender = string_copy(ui_text_input(h, "", UI_ALIGN_LEFT, true, false));
	if (ui_icon_button("", ICON_FOLDER_OPEN, UI_ALIGN_CENTER)) {
		ui_files_show("", false, false, &import_blend_mesh_ui_blender_folder_picked);
	}
}

void import_blend_mesh_run(char *path, bool replace_existing) {
	if (g_config->blender == NULL || string_equals(g_config->blender, "")) {
		console_error(tr("Blender executable path not set"));
		return;
	}

	// Blender must write the temporary OBJ to a user-writable location. The
	// working directory can be read-only on Linux and in installed builds.
	char *save       = string("%stmp.obj", iron_internal_save_path());
	char *bpy_folder = "";
	if (iron_file_exists(save)) {
		iron_delete_file(save);
	}

	// Have to use ; instead of \n on windows
	char *py = string("import bpy;bpy.ops.wm.obj_export(filepath='%s%s',export_triangulated_mesh=True,export_materials=False,check_existing=False)", bpy_folder,
	                  string_replace_all(save, "\\", "/"));
#ifdef IRON_WINDOWS
	char *bl = string("\"%s\"", string_replace_all(g_config->blender, "/", "\\"));
#else
	char *bl = string_replace_all(g_config->blender, " ", "\\ ");
#endif
	iron_sys_command(string("%s \"%s\" -b --python-expr \"%s\"", bl, path, py));
	if (!iron_file_exists(save)) {
		console_error(tr("Error: Blender did not create the temporary OBJ"));
		return;
	}
	import_obj_run(save, replace_existing);
}
