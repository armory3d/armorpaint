#include "global.h"

char *tmp_root = "/tmp/armorpaint";
char *dir_textures;
char *dir_project;
char *dir_mesh;
char *dir_material;
char *file_project;

int step     = 0;
int checks   = 0;
int failures = 0;

slot_material_t *mat_noise;

void check(int ok, char *name) {
	checks += 1;
	if (ok) {
		printf("[test]   ok   %s", name);
	}
	else {
		failures += 1;
		printf("[test]   FAIL %s", name);
	}
}

int dir_file_count(char *path) {
	any_array_t *files = file_read_directory(path);
	if (files == NULL) {
		return 0;
	}
	return files->length;
}

// 0 - Tools

void step_tools() {
	context_t *ctx = script_get_context();
	int        ok  = 1;
	for (int i = 0; i < 14; ++i) {
		context_select_tool(i);
		if (ctx->tool != i) {
			ok = 0;
		}
	}
	check(ok, "select all tools");
	context_select_tool(TOOL_TYPE_BRUSH);
}

// 1 - Brush stroke

void step_paint_brush() {
	context_t *ctx = script_get_context();
	context_select_tool(TOOL_TYPE_BRUSH);
	ctx->brush_radius   = 0.5;
	ctx->brush_opacity  = 1.0;
	ctx->brush_hardness = 0.8;
	ctx->brush_scale    = 1.0;

	for (int i = 0; i < 8; ++i) {
		float t = i * 0.125;
		script_paint(0.35 + t * 0.3, 0.3 + t * 0.4);
	}
	script_paint_end();

	for (int i = 0; i < 4; ++i) {
		float t = i * 0.25;
		script_paint(0.65 - t * 0.3, 0.3 + t * 0.4);
	}
	script_paint_end();
	check(1, "brush strokes");
}

// 2 - Eraser and world-space painting

void step_paint_eraser() {
	context_select_tool(TOOL_TYPE_ERASER);
	script_paint(0.5, 0.5);
	script_paint(0.52, 0.54);
	script_paint(0.56, 0.58);
	script_paint_end();

	context_select_tool(TOOL_TYPE_BRUSH);
	script_paint_world(0.0, 0.0, 0.0);
	script_paint_world(0.1, 0.0, 0.1);
	script_paint_end();
	check(1, "eraser and world-space strokes");
}

// 3 - Fill

void step_fill() {
	script_fill_layer();
	check(1, "fill layer");
}

// 4 - Material nodes

void step_material_create() {
	mat_noise = script_material_create("test_noise");
	int ok    = 0;
	if (mat_noise != NULL) {
		ok = 1;
	}
	check(ok, "material created");

	ui_node_t *noise = script_material_create_node_at("TEX_NOISE", -700.0, 0.0);
	ui_node_t *rgb   = script_material_create_node_at("RGB", -700.0, 400.0);
	ui_node_t *mix   = script_material_create_node_at("MIX_RGB", -350.0, 0.0);
	ui_node_t *out   = script_material_get_node("OUTPUT_MATERIAL_PBR");

	ok = 0;
	if (noise != NULL) {
		if (rgb != NULL) {
			if (mix != NULL) {
				if (out != NULL) {
					ok = 1;
				}
			}
		}
	}
	check(ok, "material nodes created");
	if (!ok) {
		return;
	}

	script_material_set_float(noise, 1, 1, 8.0); // Scale
	script_material_set_float(noise, 1, 2, 4.0); // Detail
	script_material_set_color(rgb, 0, 0, 0.9, 0.25, 0.1, 1.0);
	script_material_set_vector(noise, 1, 0, 0.0, 0.0, 0.0);
	script_material_set_button(mix, 0, 0.0);

	script_material_connect(noise, 0, mix, 0);
	script_material_connect(rgb, 0, mix, 1);
	script_material_connect(mix, 0, out, 0);
	script_material_update();

	ui_node_t *found = script_material_get_node("TEX_NOISE");
	ok               = 0;
	if (found != NULL) {
		if (script_material_get_node_id(found->id) == found) {
			ok = 1;
		}
	}
	check(ok, "material node lookup");
}

// 5 - Material edit, assign, delete

void step_material_edit() {
	ui_node_t *mix = script_material_get_node("MIX_RGB");
	ui_node_t *rgb = script_material_get_node("RGB");
	if (mix != NULL) {
		script_material_disconnect(mix, 1);
	}
	if (rgb != NULL) {
		script_material_remove_node(rgb);
	}
	script_material_update();
	check(1, "material node disconnect and remove");

	mesh_object_t *mo = context_main_object();
	if (mo != NULL) {
		script_object_set_material(mo->base, mat_noise);
	}

	slot_material_t *tmp = script_material_create("test_temp");
	script_material_delete(tmp);
	int ok = 0;
	if (script_get_material("test_temp") == NULL) {
		ok = 1;
	}
	check(ok, "material deleted");

	script_material_set(mat_noise);
}

// 6 - Shapes and object duplication

void step_shapes() {
	string_array_t *shapes = script_shape_list();
	int             ok     = 0;
	if (shapes != NULL) {
		if (shapes->length > 0) {
			ok = 1;
		}
	}
	check(ok, "shape list");
	if (!ok) {
		return;
	}

	object_t *o = script_shape_add(shapes->buffer[0]);
	ok          = 0;
	if (o != NULL) {
		ok = 1;
	}
	check(ok, "shape added");
	if (!ok) {
		return;
	}

	object_t *dup = script_object_duplicate(o);
	ok            = 0;
	if (dup != NULL) {
		ok = 1;
	}
	check(ok, "object duplicated");

	script_object_set_material(o, mat_noise);
	script_physics_set_shape(o, PHYSICS_SHAPE_BOX);
	script_physics_set_mass(o, 1.0);
	script_physics_apply_impulse(o, 0.0, 0.0, 1.0);
	script_physics_set_velocity(o, 0.0, 0.0, 0.0);
	script_physics_sync_transform(o);
	check(1, "physics body");
}

// 7 - Export textures

void step_export_textures() {
	export_texture_run(dir_textures, false);
	check(dir_file_count(dir_textures) > 0, "textures exported");
}

// 8 - Export mesh and material

void step_export_mesh() {
	script_export_mesh(string("%s/test_mesh", dir_mesh));
	check(iron_file_exists(string("%s/test_mesh.obj", dir_mesh)), "mesh exported");

	script_export_material(string("%s/test_material.arm", dir_material));
	check(iron_file_exists(string("%s/test_material.arm", dir_material)), "material exported");
}

// 9 - Save project

void step_save() {
	project_filepath_set(file_project);
	project_save(false);
	check(string_equals(project_filepath_get(), file_project), "project filepath set");
}

// 10 - Saved file is on disk

void step_verify_save() {
	check(iron_file_exists(file_project), "project saved");
}

// 11 - New project

void step_new_project() {
	script_project_new();
	int ok = 0;
	if (context_main_object() != NULL) {
		ok = 1;
	}
	check(ok, "new project");
}

// 12 - Load the saved project back

void step_load() {
	script_project_open(file_project);
	check(1, "project loaded");
}

// 13 - Loaded content is there

void step_verify_load() {
	int ok = 0;
	if (context_main_object() != NULL) {
		ok = 1;
	}
	check(ok, "paint object after load");

	ok = 0;
	if (script_get_material("test_noise") != NULL) {
		ok = 1;
	}
	check(ok, "material survived save and load");
}

// 14 - Import one of the exported textures back in

void step_import_texture() {
	any_array_t *files = file_read_directory(dir_textures);
	int          ok    = 0;
	if (files != NULL) {
		if (files->length > 0) {
			char *name = files->buffer[0];
			script_import_asset(string("%s/%s", dir_textures, name), false);
			ok = 1;
		}
	}
	check(ok, "texture imported");
}

// 15 - Import the exported mesh

void step_import_mesh() {
	char *path = string("%s/test_mesh.obj", dir_mesh);
	script_import_asset(path, false);
	check(1, "mesh imported");
}

// 16 - Import the exported material

void step_import_material() {
	script_import_asset(string("%s/test_material.arm", dir_material), false);
	check(1, "material imported");
}

// 17 - Paint once more on the reloaded project, then save over it

void step_repaint_and_resave() {
	context_select_tool(TOOL_TYPE_BRUSH);
	script_paint(0.45, 0.45);
	script_paint(0.5, 0.5);
	script_paint_end();
	project_save(false);
	check(1, "repaint and resave");
}

void run_step(int s) {
	if (s == 0)
		step_tools();
	else if (s == 1)
		step_paint_brush();
	else if (s == 2)
		step_paint_eraser();
	else if (s == 3)
		step_fill();
	else if (s == 4)
		step_material_create();
	else if (s == 5)
		step_material_edit();
	else if (s == 6)
		step_shapes();
	else if (s == 7)
		step_export_textures();
	else if (s == 8)
		step_export_mesh();
	else if (s == 9)
		step_save();
	else if (s == 10)
		step_verify_save();
	else if (s == 11)
		step_new_project();
	else if (s == 12)
		step_load();
	else if (s == 13)
		step_verify_load();
	else if (s == 14)
		step_import_texture();
	else if (s == 15)
		step_import_mesh();
	else if (s == 16)
		step_import_material();
	else if (s == 17)
		step_repaint_and_resave();
}

int step_count      = 18;
int expected_checks = 26;

void next_frame() {
	printf("[test] step %d of %d", step + 1, step_count);
	run_step(step);

	step += 1;
	if (step < step_count) {
		script_notify_on_next_frame(next_frame);
		return;
	}

	if (checks != expected_checks) {
		failures += 1;
		printf("[test]   FAIL expected %d checks, ran %d - a step was cut short", expected_checks, checks);
	}
	printf("[test] %d checks, %d failures", checks, failures);
	if (failures == 0) {
		printf("TEST PASS");
	}
	else {
		printf("TEST FAIL");
	}
	script_quit();
}

void main() {
	dir_textures = string("%s/textures", tmp_root);
	dir_project  = string("%s/project", tmp_root);
	dir_mesh     = string("%s/mesh", tmp_root);
	dir_material = string("%s/material", tmp_root);
	file_project = string("%s/test_project.arm", dir_project);

	iron_create_directory(tmp_root);
	iron_create_directory(dir_textures);
	iron_create_directory(dir_project);
	iron_create_directory(dir_mesh);
	iron_create_directory(dir_material);

	printf("[test] output directory %s", tmp_root);
	script_notify_on_next_frame(next_frame);
}
