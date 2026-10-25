#include "global.h"

// Bake Mesh Maps plugin based on the work of Falkor.
// Bakes mesh maps of the paint objects on the CPU, optionally projected from a high poly file.
// Each map is saved as <name>_<map>.png and added to the project textures.
// The ray tracer runs natively on worker threads, see plugins/bake_maps/bake_maps.c.

void *plugin;
int   box_tab  = 0;
bool  box_open = false;

char *map_names[13];
char *map_suffixes[13];
int   map_types[13]; // bake_type_t, 14 is cavity
bool  map_enabled[13];

int   res_index       = 1;
float rays            = 64;
int   axis            = 0; // XYZ, X, Y, Z, -X, -Y, -Z
int   up_axis         = 0; // Z, Y
float ao_strength     = 1.0;
float ao_radius       = 1.0;
float ao_offset       = 1.0;
float curv_strength   = 1.0;
float curv_radius     = 1.0;
float curv_offset     = 0.0;
float curv_smooth     = 1;
float cavity_size     = 2.0;
float cavity_contrast = 1.0;

bool  use_high    = false;
char *high_path   = "";
char *high_loaded = ""; // Path currently held by the ray tracer
char *high_tried  = ""; // Do not retry a failing file every frame
int   high_count  = 0;
bool  average     = true;
float cage_dist   = 0.02;

char *out_name   = "";
char *auto_name  = ""; // Default name last applied, replaced while the user did not edit it
char *out_folder = "";

void *preview_image = NULL;
bool  preview_fast  = false; // Cheaper render while orbiting
bool  loaded        = false; // Bake data is held natively
float yaw           = 0.8;
float pitch         = 0.35;
float zoom          = 1.0;
bool  show_low      = true;
bool  show_high     = false;
bool  show_cage     = true;
bool  show_miss     = true;

bool running = false;
int  queue[13];
int  queue_len  = 0;
int  queue_i    = 0;
int  done_count = 0;
int  bake_res   = 2048;

int  inpaint_tool   = -1;
int  inpaint_mode   = 0;     // Copy, Smooth
int  inpaint_target = 0;     // 0: every baked map, n: the n-th of them
bool inpaint_ready  = false; // Mask layout prepared for the current stroke
bool inpaint_tried  = false; // One attempt per stroke

void add_map(int i, char *name, char *suffix, int type, bool enabled) {
	map_names[i]    = name;
	map_suffixes[i] = suffix;
	map_types[i]    = type;
	map_enabled[i]  = enabled;
}

bool any_selected(int a, int b) {
	for (int i = 0; i < 13; ++i) {
		if (map_enabled[i] && (map_types[i] == a || map_types[i] == b)) {
			return true;
		}
	}
	return false;
}

char *default_name() {
	context_t *c = script_get_context();
	if (c->paint_object != NULL && c->paint_object->base->name != NULL) {
		return c->paint_object->base->name;
	}
	return "bake";
}

char *default_folder() {
	char *path = project_filepath_get();
	int   i    = string_last_index_of(path, "/");
	int   j    = string_last_index_of(path, "\\");
	if (j > i) {
		i = j;
	}
	if (i < 0) {
		return "";
	}
	return substring(path, 0, i);
}

void unload_high() {
	bake_maps_load_high("");
	high_loaded = "";
	high_tried  = "";
	high_count  = 0;
}

bool load_high() {
	if (string_equals(high_loaded, high_path)) {
		return high_count > 0;
	}
	high_tried = string_copy(high_path);
	high_count = bake_maps_load_high(high_path);
	if (high_count > 0) {
		high_loaded = string_copy(high_path);
	}
	return high_count > 0;
}

// Releases the high poly and the bake data, they can take gigabytes
void release() {
	bake_maps_end();
	preview_image = NULL;
	high_loaded   = "";
	high_tried    = "";
	high_count    = 0;
	loaded        = false;
}

// Recreates the "<name> Baked" material with the baked Occlusion, Normal and Height maps linked to the output
void update_material() {
	char *sep         = string_index_of(out_folder, "\\") >= 0 ? "\\" : "/";
	char *suffixes[3] = {"ao", "normal", "height"};
	int   sockets[3]  = {2, 5, 7}; // Occlusion, Normal Map, Height of OUTPUT_MATERIAL_PBR
	int   assets[3];
	bool  any = false;
	for (int k = 0; k < 3; ++k) {
		assets[k] = script_asset_index(string("%s%s%s_%s.png", out_folder, sep, out_name, suffixes[k]));
		any       = any || assets[k] >= 0;
	}
	if (!any) {
		return;
	}

	char            *name = string("%s Baked", out_name);
	slot_material_t *old  = script_get_material(name);
	if (old != NULL) {
		script_material_delete(old);
	}
	if (script_material_create(name) == NULL) {
		return;
	}
	// Image nodes go left of the default nodes
	ui_node_t *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t *rgb = script_material_get_node("RGB");
	float      x   = (rgb != NULL && rgb->x < out->x ? rgb->x : out->x) - 260;
	for (int k = 0; k < 3; ++k) {
		if (assets[k] < 0) {
			continue;
		}
		ui_node_t *n = script_material_create_node_at("TEX_IMAGE", x, 100 + 180 * k);
		script_material_set_button(n, 0, assets[k]);
		script_material_connect(n, 0, out, sockets[k]);
	}
	script_material_update();
	console_info(string("Baked maps linked in material %s", name));
}

void finish() {
	running = false;
	bake_maps_end_images();
	console_info(string("Baked %d maps", done_count));
	if (done_count > 0) {
		update_material();
	}
	if (!box_open) {
		release(); // Window was closed while baking
	}
}

void start_next() {
	while (queue_i < queue_len) {
		int type = map_types[queue[queue_i]];
		if (bake_maps_start(type, rays, axis, up_axis, use_high, bake_res, bake_res)) {
			return;
		}
		console_error("Unable to bake, check that the mesh has a UV map");
		queue_i = queue_len;
	}
	finish();
}

void run() {
	queue_len = 0;
	for (int i = 0; i < 13; ++i) {
		if (map_enabled[i]) {
			queue[queue_len] = i;
			queue_len        = queue_len + 1;
		}
	}
	if (queue_len == 0) {
		console_error("No maps selected");
		return;
	}
	if (string_equals(out_name, "")) {
		out_name = string_copy(default_name());
	}
	if (use_high) {
		if (string_equals(high_path, "")) {
			console_error("Select a high poly mesh file or disable High Poly");
			return;
		}
		if (!load_high()) {
			return;
		}
	}

	// Read the current paint objects, they may have changed since the preview
	loaded = true;
	if (bake_maps_set_low() == 0) {
		console_error("Unable to bake, check that the mesh has a UV map");
		return;
	}
	bake_maps_set_cage(cage_dist, average);
	bake_maps_set_ao(ao_strength, ao_radius, ao_offset);
	bake_maps_set_curvature(curv_strength, curv_radius, curv_offset, curv_smooth);
	bake_maps_set_cavity(cavity_size, cavity_contrast);

	int sizes[4] = {1024, 2048, 4096, 8192};
	bake_res     = sizes[res_index];
	running      = true;
	box_tab      = 0; // The progress is the only tab
	queue_i      = 0;
	done_count   = 0;
	start_next();
}

// Inpaint tool: paint over bake artifacts, the baked maps are repaired on release

// Path of a baked map, as saved by the last bake
char *map_path(int i) {
	char *name   = string_equals(out_name, "") ? default_name() : out_name;
	char *folder = string_equals(out_folder, "") ? default_folder() : out_folder;
	char *sep    = string_index_of(folder, "\\") >= 0 ? "\\" : "/";
	return string("%s%s%s_%s.png", folder, sep, name, map_suffixes[i]);
}

// Maps with a texture in the project, repaired by the stroke
int inpaint_targets(int *out) {
	int count = 0;
	for (int i = 0; i < 13; ++i) {
		if (script_asset_index(map_path(i)) >= 0) {
			out[count] = i;
			count      = count + 1;
		}
	}
	return count;
}

void inpaint_stroke() {
	if (!inpaint_ready) {
		if (inpaint_tried) {
			return;
		}
		inpaint_tried = true;
		if (running) {
			console_error("Inpaint is not available while baking");
			return;
		}
		int targets[13];
		int count = inpaint_targets(targets);
		if (count == 0) {
			console_error("Inpaint: no baked maps found for this mesh, bake them first");
			return;
		}
		if (!bake_maps_inpaint_begin(map_path(targets[0]))) {
			console_error("Inpaint needs a mesh with a UV map");
			return;
		}
		inpaint_ready = true;
	}
	bake_maps_inpaint_stroke();
	plugin_tool_set_overlay(inpaint_tool, bake_maps_inpaint_mask(), 0xa6ff0d0d);
}

void inpaint_stroke_end() {
	if (inpaint_ready) {
		int targets[13];
		int count = inpaint_targets(targets);
		int done  = 0;
		for (int k = 0; k < count; ++k) {
			if (inpaint_target > 0 && inpaint_target != k + 1) {
				continue;
			}
			int  i      = targets[k];
			bool vector = string_index_of(map_suffixes[i], "normal") >= 0;
			if (bake_maps_inpaint_apply(map_path(i), inpaint_mode, vector)) {
				done = done + 1;
			}
		}
		console_info(string("Inpaint: %d maps repaired", done));
	}
	bake_maps_inpaint_clear();
	plugin_tool_set_overlay(inpaint_tool, NULL, 0);
	inpaint_ready = false;
	inpaint_tried = false;
}

void inpaint_header() {
	string_array_t *mode_combo = string_array_create(2);
	mode_combo->buffer[0]      = "Copy";
	mode_combo->buffer[1]      = "Smooth";
	ui_combo(&inpaint_mode, mode_combo, "Mode", true, UI_ALIGN_LEFT, true);

	int             targets[13];
	int             count     = inpaint_targets(targets);
	string_array_t *map_combo = string_array_create(count + 1);
	map_combo->buffer[0]      = "All Baked Maps";
	for (int k = 0; k < count; ++k) {
		map_combo->buffer[k + 1] = map_names[targets[k]];
	}
	if (inpaint_target > count) {
		inpaint_target = 0;
	}
	ui_combo(&inpaint_target, map_combo, "Maps", false, UI_ALIGN_LEFT, true);

	context_t *c      = script_get_context();
	float      radius = c->brush_radius;
	ui_slider(&radius, "Radius", 0.01, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	c->brush_radius = radius;
	if (count == 0) {
		ui_text("No baked maps for this mesh", UI_ALIGN_LEFT, 0);
	}
}

void on_update() {
	if (!running) {
		return;
	}
	iron_delay_idle_sleep();
	base_redraw_ui(); // Keep the progress and the Stop button live
	if (!bake_maps_step()) {
		return;
	}
	int i = queue[queue_i];
	if (bake_maps_save(out_folder, string("%s_%s.png", out_name, map_suffixes[i]))) {
		done_count = done_count + 1;
	}
	queue_i = queue_i + 1;
	start_next();
}

void folder_picked(char *path) {
	out_folder = string_copy(path);
}

void folder_picked_and_run(char *path) {
	folder_picked(path);
	run();
}

void high_picked(char *path) {
	high_path = string_copy(path);
	use_high  = true;
}

void draw_progress() {
	float progress = bake_maps_progress();
	int   i        = queue[queue_i];
	ui_text(string("Baking %d/%d: %s %d%%", queue_i + 1, queue_len, map_names[i], (int)(progress * 100)), UI_ALIGN_LEFT, 0);
	float total = (queue_i + progress) / queue_len;
	ui_slider(&total, "", 0.0, 1.0, true, 100, false, UI_ALIGN_CENTER, false);
	if (ui_button("Stop", UI_ALIGN_CENTER, "")) {
		bake_maps_cancel();
		finish();
	}
}

void draw_maps() {
	bake_maps_ui_section("Maps");
	for (int i = 0; i < 13; i += 2) {
		ui_row2();
		ui_check(&map_enabled[i], map_names[i], "");
		if (i + 1 < 13) {
			ui_check(&map_enabled[i + 1], map_names[i + 1], "");
		}
		else {
			ui_end_element(); // Odd count, complete the row
		}
	}
	ui_row2();
	if (ui_button("Select All", UI_ALIGN_CENTER, "")) {
		for (int i = 0; i < 13; ++i) {
			map_enabled[i] = true;
		}
	}
	if (ui_button("Select None", UI_ALIGN_CENTER, "")) {
		for (int i = 0; i < 13; ++i) {
			map_enabled[i] = false;
		}
	}
}

void draw_high_poly() {
	bake_maps_ui_section("High Poly");
	ui_check(&use_high, "Bake from High Poly", "");
	if (!use_high) {
		return;
	}
	f32_array_t *row = f32_array_create(2);
	row->buffer[0]   = 0.8;
	row->buffer[1]   = 0.2;
	ui_row(row);
	ui_text(string_equals(high_path, "") ? "No file selected" : high_path, UI_ALIGN_LEFT, 0);
	if (ui_button("...", UI_ALIGN_CENTER, "")) {
		ui_files_show2("obj,fbx,gltf,glb", false, false, high_picked);
	}
	if (high_count > 0 && string_equals(high_loaded, high_path)) {
		ui_text(string("%d triangles", high_count), UI_ALIGN_LEFT, 0);
		float fit = bake_maps_high_fit();
		if (fit == 0.0) {
			ui_text("Warning: the high poly does not overlap the low poly", UI_ALIGN_LEFT, 0);
		}
		else if (fit < 0.5 || fit > 2.0) {
			ui_text("Warning: the high poly and low poly sizes differ a lot", UI_ALIGN_LEFT, 0);
		}
	}
	ui_check(&average, "Average Normals", "");
}

void draw_settings() {
	ui_text("Common", UI_ALIGN_LEFT, 0);
	string_array_t *res_combo = string_array_create(4);
	res_combo->buffer[0]      = "1024";
	res_combo->buffer[1]      = "2048";
	res_combo->buffer[2]      = "4096";
	res_combo->buffer[3]      = "8192";
	ui_combo(&res_index, res_combo, "Resolution", true, UI_ALIGN_LEFT, true);

	if (any_selected(2, 5) || any_selected(12, 12)) { // World space normal, position, bent normal
		string_array_t *up_combo = string_array_create(2);
		up_combo->buffer[0]      = "Z";
		up_combo->buffer[1]      = "Y";
		ui_combo(&up_axis, up_combo, "Up Axis", true, UI_ALIGN_LEFT, true);
	}
	if (any_selected(0, 0)) { // Curvature
		string_array_t *axis_combo = string_array_create(7);
		axis_combo->buffer[0]      = "XYZ";
		axis_combo->buffer[1]      = "X";
		axis_combo->buffer[2]      = "Y";
		axis_combo->buffer[3]      = "Z";
		axis_combo->buffer[4]      = "-X";
		axis_combo->buffer[5]      = "-Y";
		axis_combo->buffer[6]      = "-Z";
		ui_combo(&axis, axis_combo, "Axis", true, UI_ALIGN_LEFT, true);
	}
	if (any_selected(10, 13) || any_selected(12, 12)) { // Occlusion, thickness, bent normal
		ui_text("Raytracing", UI_ALIGN_LEFT, 0);
		ui_slider(&rays, "Rays", 4, 512, true, 1, true, UI_ALIGN_RIGHT, true);
		rays = floor(rays);
	}
	if (any_selected(10, 10)) {
		ui_text("Ambient Occlusion", UI_ALIGN_LEFT, 0);
		ui_slider(&ao_strength, "Strength", 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&ao_radius, "Radius", 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&ao_offset, "Offset", 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	}
	if (any_selected(0, 0)) {
		ui_text("Curvature", UI_ALIGN_LEFT, 0);
		ui_slider(&curv_strength, "Strength", 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&curv_radius, "Radius", 0.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&curv_offset, "Offset", -2.0, 2.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&curv_smooth, "Smooth", 0, 5, false, 1, true, UI_ALIGN_RIGHT, true);
		curv_smooth = floor(curv_smooth);
	}
	if (any_selected(14, 14)) {
		ui_text("Cavity", UI_ALIGN_LEFT, 0);
		ui_slider(&cavity_size, "Detail Size", 1.0, 16.0, true, 10.0, true, UI_ALIGN_RIGHT, true);
		ui_slider(&cavity_contrast, "Contrast", 0.5, 6.0, true, 100.0, true, UI_ALIGN_RIGHT, true);
	}
}

void draw_preview() {
	loaded        = true;
	bool has_high = use_high && !string_equals(high_path, "");
	if (has_high && (show_high || show_miss)) {
		if (!string_equals(high_loaded, high_path) && !string_equals(high_tried, high_path)) {
			load_high();
		}
	}
	else if (!has_high && !string_equals(high_loaded, "")) {
		unload_high();
	}
	bake_maps_set_cage(cage_dist, average);

	// Rendered only when something changed, the low poly is reread when the paint objects change
	preview_image = bake_maps_preview(620, 470, yaw, pitch, zoom, show_low, show_high, show_cage, show_miss, preview_fast);
	int state     = ui_image(preview_image, 0xffffffff, -1);
	if (state == UI_STATE_DOWN) {
		if (mouse_movement_x != 0.0 || mouse_movement_y != 0.0) {
			yaw          = yaw - mouse_movement_x * 0.01;
			pitch        = pitch + mouse_movement_y * 0.01;
			pitch        = pitch < -1.5 ? -1.5 : pitch > 1.5 ? 1.5 : pitch;
			preview_fast = true;
		}
	}
	else if (state == UI_STATE_HOVERED && mouse_wheel_delta != 0.0) {
		zoom         = zoom * (1.0 + mouse_wheel_delta * 0.1);
		zoom         = zoom < 0.2 ? 0.2 : zoom > 5.0 ? 5.0 : zoom;
		preview_fast = true;
	}
	else {
		preview_fast = false; // Interaction ended, refine with exact rays
	}

	ui_row4();
	ui_check(&show_low, "Low Poly", "");
	bake_maps_ui_enabled(has_high);
	ui_check(&show_high, "High Poly", "");
	bake_maps_ui_enabled(true);
	ui_check(&show_cage, "Cage", "");
	bake_maps_ui_enabled(has_high);
	ui_check(&show_miss, "Missed Rays", "");
	bake_maps_ui_enabled(true);
	ui_slider(&cage_dist, "Max Distance", 0.001, 0.2, true, 1000.0, true, UI_ALIGN_RIGHT, true);
	if (!has_high) {
		ui_text("Enable High Poly and pick a file to see the areas the rays miss", UI_ALIGN_LEFT, 0);
	}
	else if (show_miss && high_count > 0) {
		ui_text(string("Red: %.1f%% of rays miss the high poly, increase Max Distance", bake_maps_miss_ratio() * 100.0), UI_ALIGN_LEFT, 0);
	}
}

void draw_output() {
	bake_maps_ui_section("Output");
	ui_text_input(&out_name, "Name", UI_ALIGN_LEFT, true, false);
	f32_array_t *row = f32_array_create(2);
	row->buffer[0]   = 0.8;
	row->buffer[1]   = 0.2;
	ui_row(row);
	ui_text(string_equals(out_folder, "") ? "No folder selected" : out_folder, UI_ALIGN_LEFT, 0);
	if (ui_button("...", UI_ALIGN_CENTER, "")) {
		ui_files_show2("png", true, false, folder_picked);
	}
}

void draw_buttons() {
	ui_separator(8, false);
	ui_row2();
	if (ui_icon_button("Cancel", 40, UI_ALIGN_CENTER)) { // ICON_CLOSE
		ui_box_hide();
	}
	if (ui_icon_button("Bake", 21, UI_ALIGN_CENTER)) { // ICON_BAKE
		if (string_equals(out_folder, "")) {
			ui_files_show2("png", true, false, folder_picked_and_run);
		}
		else {
			run();
		}
	}
}

void draw_box() {
	if (running) {
		if (ui_tab(&box_tab, "Bake Mesh Maps", false, -1, false)) {
			draw_progress();
		}
		return;
	}
	if (string_equals(out_name, "") || string_equals(out_name, auto_name)) {
		out_name  = string_copy(default_name());
		auto_name = out_name;
	}

	// Settings on the left, 3D preview on the right, buttons below both
	if (ui_tab(&box_tab, "Maps", false, -1, false)) {
		bake_maps_ui_column(0);
		draw_maps();
		draw_high_poly();
		draw_output();
		bake_maps_ui_column(1);
		draw_preview();
		bake_maps_ui_column(-1);
		draw_buttons();
	}
	if (ui_tab(&box_tab, "Settings", false, -1, false)) {
		bake_maps_ui_column(0);
		draw_settings();
		bake_maps_ui_column(1);
		draw_preview();
		bake_maps_ui_column(-1);
		draw_buttons();
	}
	if (ui_tab(&box_tab, "About", false, -1, false)) {
		ui_text("Bake Mesh Maps plugin based on the work of Falkor.", UI_ALIGN_LEFT, 0);
		f32_array_t *row = f32_array_create(2);
		row->buffer[0]   = 0.3;
		row->buffer[1]   = 0.7;
		ui_row(row);
		if (ui_button("https://forums.armorpaint.org/t/146", UI_ALIGN_LEFT, "")) {
			iron_load_url("https://forums.armorpaint.org/t/146");
		}
		ui_end_element(); // Keep the button narrow
	}
}

// The high poly and the bake data can take gigabytes, released with the window
void on_hide() {
	box_open = false;
	if (!running) {
		release();
	}
}

void on_ui() {
	if (ui_button("Bake Mesh Maps...", UI_ALIGN_CENTER, "")) {
		if (string_equals(out_folder, "")) {
			out_folder = default_folder(); // Project folder when saved
		}
		box_open = true;
		ui_box_show_custom2(draw_box, 1100, 720, on_hide, "Bake Mesh Maps");
	}
}

void on_delete() {
	if (running) {
		bake_maps_cancel();
		running = false;
	}
	if (box_open) {
		ui_box_hide(); // The window draws through this plugin's functions
	}
	bake_maps_inpaint_end();
	release();
}

void main() {
	add_map(0, "Normal", "normal", 1, true);
	add_map(1, "Height", "height", 3, false);
	add_map(2, "Ambient Occlusion", "ao", 10, true);
	add_map(3, "Curvature", "curvature", 0, true);
	add_map(4, "Thickness", "thickness", 13, true);
	add_map(5, "Bent Normal", "bent_normal", 12, false);
	add_map(6, "World Space Normal", "world_normal", 2, false);
	add_map(7, "Position", "position", 5, true);
	add_map(8, "Material ID", "material_id", 7, false);
	add_map(9, "Object ID", "object_id", 8, false);
	add_map(10, "Vertex Color", "vertex_color", 9, false);
	add_map(11, "TexCoord", "texcoord", 6, false);
	add_map(12, "Cavity", "cavity", 14, false);

	plugin = plugin_create();
	plugin_notify_on_ui(plugin, on_ui);
	plugin_notify_on_update(plugin, on_update);
	plugin_notify_on_delete(plugin, on_delete);

	int icon_erase = 100; // ICON_ERASE
	inpaint_tool   = plugin_register_tool(plugin, "Inpaint", "Paint over bake artifacts, they are repaired when the mouse is released", icon_erase,
	                                      inpaint_header, inpaint_stroke, inpaint_stroke_end);
}
