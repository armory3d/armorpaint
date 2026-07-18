
#include "../global.h"

char *_project_import_mesh_box_path;
bool  _project_import_mesh_box_replace_existing;
bool  _project_import_mesh_box_clear_layers;
bool  _project_import_mesh_box_keep_camera;
void (*_project_import_mesh_box_done)(void);

extern int plugins_skinning_frame;
extern int plugins_split_by;

void project_import_mesh_box_menu() {
	if (ui_menu_button(tr("Append"), "", ICON_CHECK)) {
		ui_box_hide();
		import_mesh_run(_project_import_mesh_box_path, _project_import_mesh_box_clear_layers, false, _project_import_mesh_box_keep_camera);
		if (_project_import_mesh_box_done != NULL) {
			_project_import_mesh_box_done();
		}
	}
	if (ui_menu_button(tr("Help"), "", ICON_HELP)) {
		iron_load_url("https://github.com/armory3d/armorpaint_web/blob/main/manual.md#faq");
	}
}

void project_import_mesh_box_draw() {
	char *path             = _project_import_mesh_box_path;
	bool  replace_existing = _project_import_mesh_box_replace_existing;
	bool  clear_layers     = _project_import_mesh_box_clear_layers;
	bool  keep_camera      = _project_import_mesh_box_keep_camera;
	void (*done)(void)     = _project_import_mesh_box_done;

	if (ends_with(to_lower_case(path), ".obj") || ends_with(to_lower_case(path), ".fbx")) {
		string_array_t *split_by_combo = any_array_create_from_raw(
		    (void *[]){
		        tr("Object"),
		        tr("Material"),
		        tr("UDIM Tile"),
		    },
		    3);
		ui_text(tr("Split By"), UI_ALIGN_LEFT, 0);
		g_context->split_by = plugins_split_by = ui_inline_radio(ui_handle(__ID__), split_by_combo, UI_ALIGN_LEFT);
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Split mesh into objects"));
		}
	}

	if (ends_with(to_lower_case(path), ".blend")) {
		import_blend_mesh_ui();
	}

	if (ends_with(to_lower_case(path), ".fbx") || ends_with(to_lower_case(path), ".gltf") || ends_with(to_lower_case(path), ".glb")) {
		ui_row2();
		bool b                 = ui_check(ui_handle(__ID__), tr("Apply Skinning"), "");
		g_ui->enabled          = b;
		plugins_skinning_frame = ui_slider(ui_handle(__ID__), tr("Frame"), 1, 99, false, 1, true, UI_ALIGN_RIGHT, true);
		g_ui->enabled          = true;
		if (!b) {
			plugins_skinning_frame = -1;
		}
	}

	f32_array_t *row = f32_array_create_from_raw(
	    (f32[]){
	        0.45,
	        0.45,
	        0.1,
	    },
	    3);

	ui_end_element();
	ui_row(row);
	if (ui_icon_button(tr("Cancel"), ICON_CLOSE, UI_ALIGN_CENTER) && !ui_menu_show) {
		ui_box_hide();
	}
	if ((ui_icon_button(tr("Import"), ICON_CHECK, UI_ALIGN_CENTER) || g_ui->is_return_down) && !ui_menu_show) {
		ui_box_hide();

#if defined(IRON_ANDROID) || defined(IRON_IOS)
		console_toast(tr("Importing mesh"));
#endif

		import_mesh_run(path, clear_layers, replace_existing, keep_camera);
		if (done != NULL) {
			done();
		}
	}
	if (ui_icon_button("", ICON_CHEVRON_DOWN, UI_ALIGN_CENTER)) {
		ui_menu_draw(&project_import_mesh_box_menu, -1, -1);
	}
}

void project_import_mesh_box(char *path, bool replace_existing, bool clear_layers, bool keep_camera, void (*done)(void)) {
	gc_unroot(_project_import_mesh_box_path);
	_project_import_mesh_box_path = string_copy(path);
	gc_root(_project_import_mesh_box_path);
	_project_import_mesh_box_replace_existing = replace_existing;
	_project_import_mesh_box_clear_layers     = clear_layers;
	_project_import_mesh_box_keep_camera      = keep_camera;
	gc_unroot(_project_import_mesh_box_done);
	_project_import_mesh_box_done = done;
	gc_root(_project_import_mesh_box_done);
	ui_box_show_custom(&project_import_mesh_box_draw, 400, 200, NULL, true, tr("Import Mesh"));
	ui_box_click_to_hide = false; // Prevent closing when going back to window from file browser
}
