#include "global.h"

char *tmp_root = "/tmp/armorpaint";
char *dir_textures;
char *dir_project;
char *dir_mesh;
char *dir_material;
char *dir_checks;
char *file_project;
char *file_screenshot;
char *file_envmap;
char *file_geom;
char *file_sculpt;
char *dir_lightmap;
char *file_sculpt_obj;

int step       = 0;
int sub        = 0; // Frame counter of a step that repeats
int step_again = 0; // Set by a step to run again on the next frame
int checks     = 0;
int failures   = 0;

slot_material_t *mat_noise;
slot_material_t *mat_fill;
slot_layer_t    *layer_red;
slot_layer_t    *layer_group;
slot_layer_t    *layer_fill;
ui_node_t       *fill_rgb;
object_t        *shape_box;
int              export_count = 0;
char            *export_name  = NULL;
int              saved_layers = 0;

char *material_node_types = "ATTRIBUTE,RGB,NEW_GEOMETRY,LAYER,LAYER_MASK,MATERIAL,OBJECT_INFO,PICKER,TEX_COORD,TILESHEET_ANIM,TILESHEET,UVMAP,"
                            "VALUE,WIREFRAME,TEX_BRICK,TEX_CHECKER,BAKE_CURVATURE,TEX_GABOR,TEX_GRADIENT,TEX_IMAGE,TEX_MAGIC,TEX_NOISE,"
                            "TEX_TEXT,TEX_VORONOI,TEX_WAVE,BLUR,BRIGHTCONTRAST,COLMASK,VALTORGB,COMBINE_COLOR,GAMMA,HUE_SAT,INVERT_COLOR,"
                            "MIX_RGB,QUANTIZE,CURVE_RGB,RGBTOBW,REPLACECOL,SEPARATE_COLOR,DIRECT_WARP,BUMP,CLAMP,COMBXYZ,FLOAT_CURVE,"
                            "MAPRANGE,MAPPING,MATH,MIX_NORMAL_MAP,NORMAL,NORMAL_MAP,SEPXYZ,CURVE_VEC,VECT_MATH,VECT_ROTATE,VECT_TRANSFORM";
any_array_t *node_types;
int          sweep_ok = 1; // Result of a step that repeats

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

int index_count(object_t *o) {
	if (o == NULL) {
		return 0;
	}
	mesh_object_t *mo = o->ext;
	return mo->data->index_array->length;
}

// Exports the textures into a fresh directory, imports one channel (_base, _rough, ...) back and returns its pixels.
// The pixels belong to the imported texture, which is kept until the next export.
buffer_t *export_channel_pixels(char *suffix) {
	if (export_name != NULL) {
		script_texture_delete(export_name);
		export_name = NULL;
	}
	export_count += 1;
	char *dir = string("%s/export%d", dir_checks, export_count);
	iron_create_directory(dir);
	// Files from an earlier run can carry another project name
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
	export_texture_run(dir, false);

	any_array_t *files = file_read_directory(dir);
	if (files == NULL) {
		return NULL;
	}
	for (int i = 0; i < files->length; ++i) {
		char *name = files->buffer[i];
		if (ends_with(name, string("%s.png", suffix))) {
			script_import_asset(string("%s/%s", dir, name), false);
			gpu_texture_t *tex = script_get_texture(name);
			if (tex == NULL) {
				return NULL;
			}
			export_name = name;
			return script_get_texture_pixels(tex);
		}
	}
	return NULL;
}

buffer_t *export_base_pixels() {
	return export_channel_pixels("_base");
}

int near(int a, int b, int tolerance) {
	int d = a - b;
	if (d < 0) {
		d = -d;
	}
	return d <= tolerance;
}

// Fraction of sampled rgba8 pixels within the tolerance of the color
float pixels_fraction(buffer_t *pixels, int r, int g, int b, int tolerance) {
	if (pixels == NULL) {
		return -1.0;
	}
	int n      = pixels->length / 4;
	int stride = n / 8192 + 1;
	int hits   = 0;
	int total  = 0;
	for (int i = 0; i < n; i += stride) {
		int p = i * 4;
		if (near(buffer_get_u8(pixels, p), r, tolerance) && near(buffer_get_u8(pixels, p + 1), g, tolerance) &&
		    near(buffer_get_u8(pixels, p + 2), b, tolerance)) {
			hits += 1;
		}
		total += 1;
	}
	return (float)hits / (float)total;
}

// Fraction of sampled pixels where red dominates green and blue
float pixels_fraction_reddish(buffer_t *pixels) {
	if (pixels == NULL) {
		return -1.0;
	}
	int n      = pixels->length / 4;
	int stride = n / 8192 + 1;
	int hits   = 0;
	int total  = 0;
	for (int i = 0; i < n; i += stride) {
		int p = i * 4;
		int r = buffer_get_u8(pixels, p);
		if (r > buffer_get_u8(pixels, p + 1) + 40 && r > buffer_get_u8(pixels, p + 2) + 40) {
			hits += 1;
		}
		total += 1;
	}
	return (float)hits / (float)total;
}

// Number of distinct values among a few sampled red channel values, 1 means a flat image
int pixels_variety(buffer_t *pixels) {
	if (pixels == NULL) {
		return 0;
	}
	int n      = pixels->length / 4;
	int stride = n / 512 + 1;
	int lo     = 255;
	int hi     = 0;
	for (int i = 0; i < n; i += stride) {
		int v = buffer_get_u8(pixels, i * 4) + buffer_get_u8(pixels, i * 4 + 1) + buffer_get_u8(pixels, i * 4 + 2);
		if (v < lo) {
			lo = v;
		}
		if (v > hi) {
			hi = v;
		}
	}
	return hi - lo;
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

// 1 - Script runtime: strings, arrays, maps, json, armpack

void step_runtime() {
	char *s = string("%s-%d-%.1f", "abc", 42, 1.5);
	check(string_equals(s, "abc-42-1.5"), "string format");

	any_array_t *parts = string_split("a,b,c", ",");
	int          ok    = parts->length == 3 && string_equals(parts->buffer[2], "c") && string_equals(string_array_join(parts, "+"), "a+b+c");
	ok                 = ok && string_index_of("hello world", "world") == 6 && starts_with(s, "abc") && ends_with(s, "1.5");
	ok                 = ok && string_equals(string_replace_all("a.b.c", ".", "/"), "a/b/c") && string_equals(substring("hello", 1, 3), "el");
	ok                 = ok && string_equals(to_upper_case("abc"), "ABC") && parse_int("123") == 123 && fabs(parse_float("2.5") - 2.5) < 0.0001;
	check(ok, "string functions");

	i32_array_t *ia = i32_array_create(0);
	for (int i = 0; i < 10; ++i) {
		i32_array_push(ia, i * i);
	}
	ok = ia->length == 10 && ia->buffer[9] == 81 && i32_array_index_of(ia, 49) == 7;
	i32_array_remove(ia, 49);
	ok = ok && ia->length == 9 && i32_array_pop(ia) == 81;
	check(ok, "i32 array");

	any_map_t *m = any_map_create();
	any_map_set(m, "key", "value");
	i32_map_t *im = i32_map_create();
	i32_map_set(im, "n", 7);
	ok = string_equals(any_map_get(m, "key"), "value") && i32_map_get(im, "n") == 7;
	map_delete(m, "key");
	ok = ok && any_map_get(m, "key") == NULL;
	check(ok, "maps");

	json_encode_begin();
	json_encode_string("name", "test");
	json_encode_i32("count", 3);
	json_encode_f32("scale", 0.5);
	char      *json = json_encode_end();
	any_map_t *jm   = json_parse_to_map(json);
	ok              = jm != NULL && string_equals(any_map_get(jm, "name"), "test");
	buffer_t  *pack = json_encode_to_armpack(json);
	any_map_t *pm   = armpack_decode_to_map(pack);
	ok              = ok && pm != NULL && armpack_map_get_i32(pm, "count") == 3 && fabs(armpack_map_get_f32(pm, "scale") - 0.5) < 0.0001;
	check(ok, "json and armpack round trip");
}

// 2 - Project state is reachable

void step_project_state() {
	context_t *ctx = script_get_context();
	config_t  *cfg = script_get_config();
	project_t *prj = script_get_project();
	int        ok  = ctx != NULL && cfg != NULL && prj != NULL && script_get_world() != NULL;
	check(ok, "context, config, project and world");
	ok = ctx->paint_object != NULL && ctx->material != NULL && ctx->layer != NULL && script_layer_count() >= 1;
	check(ok, "paint object, material and layer");
}

// 3 - Brush stroke

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

// 4 - One stroke per brush blending mode, one mode per frame

void step_paint_blending() {
	context_t *ctx      = script_get_context();
	ctx->brush_blending = sub;
	ctx->brush_angle    = sub * 20.0;
	ctx->brush_opacity  = 0.6;
	float y             = 0.2 + sub * 0.03;
	script_paint(0.3, y);
	script_paint(0.5, y + 0.02);
	script_paint(0.7, y);
	script_paint_end();
	if (sub < 17) {
		step_again = 1;
		return;
	}
	ctx->brush_blending = 0;
	ctx->brush_angle    = 0.0;
	ctx->brush_opacity  = 1.0;
	check(1, "stroke with every blending mode");
}

// 5 - One stroke per paint tool, one tool per frame

void step_paint_tools() {
	context_t *ctx = script_get_context();
	context_select_tool(sub);
	script_paint(0.4, 0.45);
	script_paint(0.45, 0.5);
	script_paint(0.5, 0.55);
	script_paint_end();
	if (ctx->tool != sub) {
		sweep_ok = 0;
	}
	if (sub < TOOL_TYPE_MATERIAL) {
		step_again = 1;
		return;
	}
	context_select_tool(TOOL_TYPE_BRUSH);
	check(sweep_ok, "stroke with every paint tool");
	sweep_ok = 1;
}

// 6 - Eraser and world-space painting

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

// 7 - Fill

void step_fill() {
	script_fill_layer();
	check(1, "fill layer");
}

// 8 - Material nodes

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

// 9 - Material edit, assign, delete

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
		// An override material leaves the object out of the paint mesh, clear it so later steps can paint
		script_object_set_material(mo->base, NULL);
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

// 10 - Every material node type compiles into the paint shader and paints, one type per frame

void step_material_nodes() {
	if (sub == 0) {
		node_types = string_split(material_node_types, ",");
		script_material_create("test_nodes");
	}
	char      *type = node_types->buffer[sub];
	ui_node_t *n    = script_material_create_node_at(type, -400.0, 0.0);
	if (n == NULL || !string_equals(n->type, type)) {
		sweep_ok = 0;
		printf("[test]   node %s not created", type);
	}
	else {
		if (n->outputs->length > 0) {
			script_material_connect(n, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
		}
		script_material_update();
		script_paint(0.5, 0.5);
		script_paint(0.55, 0.5);
		script_paint_end();
		script_material_remove_node(n);
	}
	if (sub < node_types->length - 1) {
		step_again = 1;
		return;
	}
	check(sweep_ok, string("%d material node types", node_types->length));
	sweep_ok = 1;

	slot_material_t *m = script_get_material("test_nodes");
	script_material_delete(m);
	check(script_get_material("test_nodes") == NULL, "node test material deleted");
	script_material_set(mat_noise);
}

// 11 - Shader node with a custom input

void step_shader_node() {
	ui_node_t *sh = script_material_create_node_at("SHADER_GPU", -400.0, 200.0);
	if (sh == NULL) {
		check(0, "shader node");
		return;
	}
	script_material_set_text(sh, 0, "float2 c = frac(tex_coord * 8.0);\nreturn lerp(float3(1.0, 0.8, 0.2), float3(0.1, 0.1, 0.3), step(dot_size, length(c - float2(0.5, 0.5))));");
	script_material_add_input(sh, "VALUE", "Dot Size");
	script_material_set_float(sh, 1, 0, 0.3);
	int ok = sh->inputs->length == 1;
	script_material_connect(sh, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	script_paint(0.3, 0.6);
	script_paint(0.35, 0.65);
	script_paint_end();
	script_material_remove_node(sh);
	script_material_connect(script_material_get_node("MIX_RGB"), 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	check(ok, "shader node with input");
}

// 12 - Color layer

void step_layer_color() {
	context_t *ctx   = script_get_context();
	int        count = script_layer_count();
	layer_red        = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	if (layer_red == NULL) {
		check(0, "color layer created");
		return;
	}
	script_layer_set_name(layer_red, "test_red");
	int ok = script_layer_count() == count + 1 && script_layer_get("test_red") == layer_red && ctx->layer == layer_red;
	ok     = ok && script_layer_at(script_layer_index(layer_red)) == layer_red;
	check(ok, "color layer created and named");
}

// 13 - The color layer shows up in the exported base color

void step_layer_export() {
	float f = pixels_fraction(export_base_pixels(), 255, 0, 0, 40);
	check(f > 0.5, string("red layer exported (%.2f red)", f));
}

// 14 - A hidden layer is not exported

void step_layer_hidden() {
	layer_red->visible = 0;
	script_layer_update(layer_red);
	float f = pixels_fraction(export_base_pixels(), 255, 0, 0, 40);
	check(f >= 0.0 && f < 0.05, string("hidden layer not exported (%.2f red)", f));
	layer_red->visible = 1;
	script_layer_update(layer_red);
}

// 15 - Black mask hides the layer, white mask keeps it

void step_layer_masks() {
	int           count = script_layer_count();
	slot_layer_t *black = script_layer_new_mask(layer_red, 0);
	int           ok    = black != NULL && black->parent == layer_red && script_layer_count() == count + 1;
	check(ok, "black mask created");
	float f = pixels_fraction(export_base_pixels(), 255, 0, 0, 40);
	check(f >= 0.0 && f < 0.05, string("black mask hides layer (%.2f red)", f));

	script_layer_delete(black);
	check(script_layer_count() == count, "mask deleted");

	slot_layer_t *white = script_layer_new_mask(layer_red, 1);
	ok                  = white != NULL && white->parent == layer_red;
	f                   = pixels_fraction(export_base_pixels(), 255, 0, 0, 40);
	check(ok && f > 0.5, string("white mask keeps layer (%.2f red)", f));
}

// 16 - Groups

void step_layer_group() {
	layer_group = script_layer_new_group(layer_red);
	int ok      = layer_group != NULL && layer_red->parent == layer_group;
	check(ok, "group created");
	if (!ok) {
		return;
	}
	script_layer_set_name(layer_group, "test_group");

	layer_group->visible = 0;
	script_layer_update(layer_group);
	float f = pixels_fraction(export_base_pixels(), 255, 0, 0, 40);
	check(f >= 0.0 && f < 0.05, string("hidden group hides its layers (%.2f red)", f));
	layer_group->visible = 1;
	script_layer_update(layer_group);

	slot_layer_t *l = script_layer_new();
	script_layer_set_name(l, "test_loose");
	ok = script_layer_set_group(l, layer_group) && l->parent == layer_group;
	check(ok, "layer moved into group");
	ok = script_layer_set_group(l, NULL) && l->parent == NULL;
	check(ok, "layer moved out of group");
	ok = script_layer_move(l, 1) && script_layer_index(l) == 1;
	check(ok, "layer moved to index 1");
	int count = script_layer_count();
	script_layer_delete(l);
	check(script_layer_count() == count - 1 && script_layer_get("test_loose") == NULL, "layer deleted");
}

// 17 - Fill layer with a flat green material

void step_layer_fill() {
	mat_fill = script_material_create("test_fill");
	fill_rgb = script_material_create_node_at("RGB", -400.0, 0.0);
	script_material_set_color(fill_rgb, 0, 0, 0.0, 1.0, 0.0, 1.0);
	script_material_connect(fill_rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();

	layer_fill = script_layer_new_fill();
	int ok     = layer_fill != NULL && layer_fill->fill_material == mat_fill;
	check(ok, "fill layer created");
	if (!ok) {
		return;
	}
	script_layer_set_name(layer_fill, "test_fill");
	script_layer_move(layer_fill, script_layer_count() - 1);
	float f = pixels_fraction(export_base_pixels(), 0, 255, 0, 40);
	check(f > 0.5, string("fill layer exported (%.2f green)", f));
}

// 18 - Editing the fill material refills the layer, object mask

void step_layer_refill() {
	script_material_set_color(fill_rgb, 0, 0, 0.0, 0.0, 1.0, 1.0);
	script_material_update();
	script_layer_update(layer_fill);
	float f = pixels_fraction(export_base_pixels(), 0, 0, 255, 40);
	check(f > 0.5, string("fill layer refilled (%.2f blue)", f));

	context_t *ctx = script_get_context();
	script_layer_set_object(layer_fill, ctx->paint_object->base);
	int ok = layer_fill->object_mask == 1;
	script_layer_set_object(layer_fill, NULL);
	check(ok && layer_fill->object_mask == 0, "layer object mask");

	layer_fill->visible = 0;
	script_layer_update(layer_fill);
	script_material_set(mat_noise);
}

// 19 - Half opacity mixes the layer with the ones below

void step_layer_opacity() {
	layer_red->mask_opacity = 0.5;
	script_layer_update(layer_red);
	buffer_t *pixels  = export_base_pixels();
	float     pure    = pixels_fraction(pixels, 255, 0, 0, 20);
	float     reddish = pixels_fraction_reddish(pixels);
	check(pure >= 0.0 && pure < 0.05 && reddish > 0.5, string("half opacity layer (%.2f red, %.2f reddish)", pure, reddish));

	// Keep the layer hidden from here on so later strokes stay visible
	layer_red->mask_opacity = 1.0;
	layer_group->visible    = 0;
	script_layer_update(layer_group);
	script_layer_set(script_layer_at(0));
}

// 20 - A stroke lands in the texture and the eraser takes it out again

void stroke_across(int tool) {
	context_select_tool(tool);
	for (int i = 0; i < 16; ++i) {
		script_paint(0.3 + i * 0.025, 0.5);
	}
	script_paint_end();
}

void step_paint_pixels() {
	context_t *ctx = script_get_context();
	script_material_set(mat_fill); // Flat blue since the refill step
	slot_layer_t *l = script_layer_new();
	script_layer_set_name(l, "test_paint");
	ctx->brush_radius  = 0.5;
	ctx->brush_opacity = 1.0;
	stroke_across(TOOL_TYPE_BRUSH);
	float painted = pixels_fraction(export_base_pixels(), 0, 0, 255, 40);
	check(painted > 0.01 && painted < 0.6, string("stroke painted (%.3f blue)", painted));

	ctx->brush_radius = 1.0;
	stroke_across(TOOL_TYPE_ERASER);
	float erased = pixels_fraction(export_base_pixels(), 0, 0, 255, 40);
	check(erased >= 0.0 && erased < painted * 0.5, string("stroke erased (%.3f blue)", erased));

	ctx->brush_radius = 0.5;
	context_select_tool(TOOL_TYPE_BRUSH);
	script_material_set(mat_noise);
	script_texture_delete(export_name); // Keep the export out of the saved project
	export_name = NULL;
}

// 21 - A material reading the layer it paints (Layer node) sees the stroke start state, not its own output

void step_paint_self() {
	context_t *ctx     = script_get_context();
	ctx->brush_radius  = 0.5;
	ctx->brush_opacity = 1.0;

	// Reference: a flat cyan stroke over a red layer
	slot_layer_t    *ref  = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	slot_material_t *flat = script_material_create("test_cyan");
	ui_node_t       *cyan = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(cyan, 0, 0, 0.0, 1.0, 1.0, 1.0);
	script_material_connect(cyan, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	stroke_across(TOOL_TYPE_BRUSH);
	float expected = pixels_fraction(export_base_pixels(), 0, 255, 255, 40);
	script_layer_delete(ref);

	// The same stroke inverting the red layer it paints on
	slot_layer_t    *l      = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	slot_material_t *invert = script_material_create("test_invert_self");
	ui_node_t       *ln     = script_material_create_node_at("LAYER", -600.0, 0.0);
	ui_node_t       *inv    = script_material_create_node_at("INVERT_COLOR", -300.0, 0.0);
	script_material_set_button(ln, 0, script_layer_index(l));
	script_material_connect(ln, 0, inv, 1);
	script_material_connect(inv, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	stroke_across(TOOL_TYPE_BRUSH);
	float inverted = pixels_fraction(export_base_pixels(), 0, 255, 255, 40);
	check(expected > 0.001 && inverted > expected * 0.9, string("self-reading stroke inverts the whole stroke (%.4f of %.4f cyan)", inverted, expected));

	script_layer_delete(l);
	script_material_delete(flat);
	script_material_delete(invert);
	script_material_set(mat_noise);
	script_texture_delete(export_name);
	export_name = NULL;
}

// 22 - Shapes and object duplication

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

	o->transform->dim.z    = 0.7; // Box size comes from the transform dim
	o->transform->has_dim  = true;
	o->transform->measured = false;
	script_physics_set_shape(o, PHYSICS_SHAPE_BOX);
	physics_body_t *body = script_physics_get_body(o);
	check(body != NULL && fabs(body->dimz - 0.7 * o->transform->scale.z) < 0.001, "physics dim");
}

// 23 - Every shape, rename, transform, parent, clone and remove

void step_objects() {
	string_array_t *shapes = script_shape_list();
	int             ok     = 1;
	for (int i = 0; i < shapes->length; ++i) {
		char *name = shapes->buffer[i];
		if (string_equals(name, "empty")) {
			continue;
		}
		object_t *o = script_shape_add(name);
		if (o == NULL) {
			ok = 0;
			printf("[test]   shape %s not added", name);
			continue;
		}
		if (index_count(o) == 0) {
			ok = 0;
			printf("[test]   shape %s has no triangles", name);
		}
		script_object_remove(o);
		if (script_get_object(o->name) == o) {
			ok = 0;
			printf("[test]   shape %s not removed", name);
		}
	}
	check(ok, string("add and remove %d shapes", shapes->length));

	shape_box = script_shape_add("cube");
	script_object_set_name(shape_box, "test_box");
	check(shape_box != NULL && script_get_object("test_box") == shape_box, "object renamed");

	shape_box->transform->loc.x = 2.0;
	shape_box->transform->loc.z = 0.5;
	transform_build_matrix(shape_box->transform);
	ok = fabs(transform_world_x(shape_box->transform) - 2.0) < 0.001 && fabs(transform_world_z(shape_box->transform) - 0.5) < 0.001;
	check(ok, "object transform");

	object_t *clone = script_object_clone("test_box");
	ok              = clone != NULL && clone != shape_box && index_count(clone) == index_count(shape_box);
	check(ok, "object cloned by name");
	if (clone != NULL) {
		script_object_set_name(clone, "test_box_child");
		object_set_parent(clone, shape_box);
		ok = clone->parent == shape_box && array_index_of(shape_box->children, clone) >= 0;
		check(ok, "object parented");
		object_set_parent(clone, NULL);
		script_object_remove(clone);
		check(script_get_object("test_box_child") == NULL, "object removed");
	}
}

// 24 - Mesh modifiers

void step_mesh_modifiers() {
	any_array_t *objects = util_mesh_get_hierarchy(shape_box->ext);
	int          before  = index_count(shape_box);
	util_mesh_subdivide(objects);
	int subdivided = index_count(shape_box);
	check(subdivided > before, string("subdivide %d -> %d indices", before, subdivided));

	util_mesh_smooth(objects);
	check(index_count(shape_box) == subdivided, "smooth keeps topology");

	util_mesh_decimate(objects, 0.5);
	int decimated = index_count(shape_box);
	check(decimated > 0 && decimated < subdivided, string("decimate %d -> %d indices", subdivided, decimated));

	util_mesh_bevel(objects, 0.05);
	check(index_count(shape_box) > decimated, string("bevel %d -> %d indices", decimated, index_count(shape_box)));

	util_mesh_uv_unwrap(objects);
	check(index_count(shape_box) > 0, "uv unwrap");

	script_object_origin_to_geometry(shape_box);
	check(1, "origin to geometry");
}

// 25 - Mesh nodes build the object from a generated shape

int mesh_nodes_original = 0;

void step_mesh_nodes() {
	object_t      *o   = script_shape_add("sphere");
	mesh_object_t *mo  = o->ext;
	mesh_nodes_original = index_count(o);
	script_object_set_name(o, "test_nodes_sphere");

	ui_node_t *shape = script_mesh_create_node_at(mo, "MESH_SHAPE", 0.0, 0.0);
	ui_node_t *subd  = script_mesh_create_node_at(mo, "MESH_SUBDIVIDE", 200.0, 0.0);
	ui_node_t *out   = script_mesh_get_node(mo, "MESH_OUTPUT");
	int        ok    = shape != NULL && subd != NULL && out != NULL && script_mesh_get_node(mo, "MESH_INPUT") != NULL;
	check(ok, "mesh nodes created");
	if (!ok) {
		return;
	}
	script_material_set_button(shape, 0, 0.0); // Box
	script_mesh_connect(mo, shape, 0, subd, 0);
	script_material_set_float(subd, 1, 1, 1.0);
	script_mesh_connect(mo, subd, 0, out, 0);
	script_mesh_update(mo);
	int box = index_count(o);
	check(box > 0 && box != mesh_nodes_original, string("mesh nodes output %d -> %d indices", mesh_nodes_original, box));

	ui_node_t *bool_node = script_mesh_create_node_at(mo, "MESH_BOOLEAN", 400.0, 0.0);
	ui_node_t *torus     = script_mesh_create_node_at(mo, "MESH_SHAPE", 200.0, 200.0);
	script_material_set_button(torus, 0, 4.0); // Torus
	script_material_set_button(bool_node, 0, 1.0); // Difference
	script_mesh_connect(mo, subd, 0, bool_node, 0);
	script_mesh_connect(mo, torus, 0, bool_node, 1);
	script_mesh_connect(mo, bool_node, 0, out, 0);
	script_mesh_update(mo);
	check(index_count(o) > 0 && index_count(o) != box, string("mesh boolean %d -> %d indices", box, index_count(o)));
}

// 26 - Mesh script node

void step_mesh_script() {
	object_t      *o  = script_get_object("test_nodes_sphere");
	mesh_object_t *mo = o->ext;
	ui_node_t     *sc = script_mesh_create_node_at(mo, "MESH_SCRIPT", 300.0, 100.0);
	if (sc == NULL) {
		check(0, "mesh script node");
		return;
	}
	script_material_set_text(sc, 0,
	                         "void main() {\nmesh_geo_t *g = mesh_geo_shape(5, 1.0, 1.0, 1.0, 1);\nint n = (int)mesh_geo_input_value(\"Count\");\n"
	                         "g = mesh_geo_array(g, n, 1.2, 0.0, 0.0, false);\nmesh_geo_output(g);\n}\n");
	script_mesh_add_input(mo, sc, "VALUE", "Count");
	script_material_set_float(sc, 1, 1, 3.0);
	script_mesh_connect(mo, script_mesh_get_node(mo, "MESH_INPUT"), 0, sc, 0);
	script_mesh_connect(mo, sc, 0, script_mesh_get_node(mo, "MESH_OUTPUT"), 0);
	script_mesh_update(mo);
	int planes = index_count(o);
	check(sc->inputs->length == 2 && planes == 18, string("mesh script node: 3 planes, %d indices", planes));

	script_material_set_float(sc, 1, 1, 5.0);
	script_mesh_update(mo);
	check(index_count(o) == 30, string("mesh script node input: 5 planes, %d indices", index_count(o)));

	// Back to the original mesh
	script_mesh_connect(mo, script_mesh_get_node(mo, "MESH_INPUT"), 0, script_mesh_get_node(mo, "MESH_OUTPUT"), 0);
	script_mesh_update(mo);
	check(index_count(o) == mesh_nodes_original, "mesh nodes bypassed");
}

// 27 - Procedural geometry export and import

void step_geom() {
	geom_t *box = geom_box(-0.2, -0.2, 0.0, 0.2, 0.2, 0.4);
	geom_t *tor = geom_torus(0.0, 0.0, 0.6, 0.3, 0.08, 24, 8);
	geom_rotate(tor, 0, 90.0, 0.0, 0.0, 0.6);
	int     count  = geom_vertex_count(box) + geom_vertex_count(tor);
	geom_t *merged = geom_merge(box, tor);
	geom_unwrap(merged, 0.01);
	check(geom_vertex_count(merged) == count, string("geom merge (%d vertices)", count));

	geom_export_add(merged, "test_geom", 0);
	geom_export_write_obj(file_geom);
	check(iron_file_exists(file_geom), "geom obj written");

	script_append_mesh(file_geom);
	object_t *o = script_get_object("test_geom");
	check(o != NULL && index_count(o) > 0, "geom obj appended");

	script_append_mesh_obj("o test_tri\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n");
	o = script_get_object("test_tri");
	check(o != NULL && index_count(o) == 3, "obj text appended");
}

// 28 - Stages and timeline

void step_stages_timeline() {
	script_stage_create("test_stage_a");
	script_stage_add_object("test_stage_a", "test_box");
	script_stage_create("test_stage_b");
	script_stage_set_hidden("test_stage_b", "test_box", 1);
	script_set_stage("test_stage_b");
	int ok = script_get_stage() != NULL && string_equals(script_get_stage(), "test_stage_b");
	script_set_stage("test_stage_a");
	ok = ok && string_equals(script_get_stage(), "test_stage_a");
	check(ok, "stages created and switched");

	script_timeline_set_frame(12);
	ok = script_timeline_get_frame() == 12;
	script_timeline_add_keyframe("test_box", 12);
	shape_box->transform->loc.x = 3.0;
	transform_build_matrix(shape_box->transform);
	script_timeline_add_keyframe("test_box", 24);
	script_timeline_set_frame(0);
	ok = ok && script_timeline_get_frame() == 0;
	script_timeline_resume();
	script_timeline_pause();
	check(ok, "timeline frames and keyframes");
}

// 29 - Generated envmap, applied on the next frame

void step_envmap_write() {
	env_begin(128, 64, 0.1, 0.12, 0.15);
	env_add_lobe(0.0, 0.0, 1.0, 4.0, 0.4, 0.5, 0.7);
	env_add_disc(0.3, 0.2, 0.8, 2.0, 3.0, 1.0, 0.95, 0.85);
	env_write(file_envmap);
	check(iron_file_exists(file_envmap), "envmap written");

	script_import_asset(file_envmap, true);
}

void step_envmap() {
	if (sub == 0) {
		step_envmap_write();
		step_again = 1;
		return;
	}
	project_t *prj = script_get_project();
	check(prj->envmap != NULL && ends_with(prj->envmap, "test_envmap.hdr"), "envmap imported");
	script_show_envmap(1);
}

// 30 - Lightmap bakes run over many frames, the scene changes under them while they run

int bakes_done          = 0;
int viewport_path_trace = 16; // VIEWPORT_MODE_PATH_TRACE

void on_bake_done() {
	bakes_done += 1;
}

char *lightmap_path(char *name) {
	return string("%s/%s.png", dir_lightmap, name);
}

void step_bake_lightmap() {
	if (sub == 0) {
		string_array_t *names = string_split("box,torus,main", ",");
		for (int i = 0; i < names->length; ++i) {
			char *path = lightmap_path(names->buffer[i]);
			if (iron_file_exists(path)) {
				iron_delete_file(path);
			}
		}
		object_t *torus = script_shape_add("torus");
		script_object_set_name(torus, "test_bake_torus");
		bakes_done = 0;
		script_bake_lightmap(shape_box, 128, 32, 2.0, lightmap_path("box"), on_bake_done);
		script_bake_lightmap(torus, 128, 32, 2.0, lightmap_path("torus"), on_bake_done);
		script_bake_lightmap(context_main_object()->base, 128, 32, 2.0, lightmap_path("main"), on_bake_done);
		check(script_bake_lightmap_busy(), "lightmap bakes queued");
	}
	else if (sub == 2) {
		// The viewport path tracer shares the raytrace pipeline with the bake
		context_set_viewport_mode(viewport_path_trace);
	}
	else if (sub == 3) {
		// Frees mesh buffers the bake scene points to, the torus job is dropped
		script_object_remove(script_get_object("test_bake_torus"));
	}
	else if (sub == 4) {
		util_mesh_subdivide(util_mesh_get_hierarchy(shape_box->ext));
	}
	if (script_bake_lightmap_busy() && sub < 600) {
		step_again = 1;
		return;
	}
	check(!script_bake_lightmap_busy(), string("lightmap bakes finished in %d frames", sub));
	check(bakes_done == 2, string("done called for the remaining bakes (%d of 2)", bakes_done));
	check(script_get_context()->viewport_mode == viewport_path_trace, "viewport mode kept during the bake");
	context_set_viewport_mode(0);

	int ok = iron_file_exists(lightmap_path("box")) && iron_file_exists(lightmap_path("main")) && !iron_file_exists(lightmap_path("torus"));
	check(ok, "lightmaps written for the objects left");
	script_import_asset(lightmap_path("box"), false);
	gpu_texture_t *tex    = script_get_texture("box.png");
	buffer_t      *pixels = script_get_texture_pixels(tex);
	int            lit    = pixels != NULL && pixels_fraction(pixels, 0, 0, 0, 8) < 0.9;
	check(tex != NULL && tex->width == 128 && lit && pixels_variety(pixels) > 10, string("box lightmap is lit (%d)", pixels_variety(pixels)));
	script_texture_delete("box.png");
}

// 31 - Cancel drops queued bakes without calling done

void step_bake_cancel() {
	if (sub == 0) {
		bakes_done = 0;
		script_bake_lightmap(shape_box, 64, 64, 2.0, lightmap_path("cancel0"), on_bake_done);
		script_bake_lightmap(shape_box, 64, 64, 2.0, lightmap_path("cancel1"), on_bake_done);
		step_again = 1;
		return;
	}
	if (sub < 3) {
		step_again = 1; // Let the first job start
		return;
	}
	if (sub == 3) {
		script_bake_lightmap_cancel();
		check(!script_bake_lightmap_busy(), "lightmap bakes cancelled");
	}
	if (sub < 6) {
		step_again = 1;
		return;
	}
	int ok = bakes_done == 0 && !iron_file_exists(lightmap_path("cancel0")) && !iron_file_exists(lightmap_path("cancel1"));
	check(ok, "cancelled bakes write nothing");
}

// 32 - Every viewport mode, one per frame, path trace last for a few frames

void step_viewport_modes() {
	context_t *ctx = script_get_context();
	int        mode = sub;
	if (mode > 16) {
		mode = 16;
	}
	context_set_viewport_mode(mode);
	if (ctx->viewport_mode != mode) {
		sweep_ok = 0;
	}
	if (sub < 20) {
		step_again = 1;
		return;
	}
	context_set_viewport_mode(0);
	check(sweep_ok, "every viewport mode");
	sweep_ok = 1;
}

// 33 - Screenshot, waits for the file

void step_screenshot() {
	if (sub == 0) {
		if (iron_file_exists(file_screenshot)) {
			iron_delete_file(file_screenshot);
		}
		script_screenshot(file_screenshot);
	}
	if (!iron_file_exists(file_screenshot) && sub < 60) {
		step_again = 1;
		return;
	}
	check(iron_file_exists(file_screenshot), "screenshot saved");
	script_import_asset(file_screenshot, false);
	gpu_texture_t *tex     = script_get_texture("test_screenshot.png");
	int            variety = pixels_variety(script_get_texture_pixels(tex));
	check(tex != NULL && variety > 30, string("screenshot has content (%d)", variety));
	script_texture_delete("test_screenshot.png");
}

// 34 - Export textures

void step_export_textures() {
	export_texture_run(dir_textures, false);
	check(dir_file_count(dir_textures) > 0, "textures exported");
}

// 35 - Export mesh and material

void step_export_mesh() {
	script_export_mesh(string("%s/test_mesh", dir_mesh));
	check(iron_file_exists(string("%s/test_mesh.obj", dir_mesh)), "mesh exported");

	script_export_material(string("%s/test_material.arm", dir_material));
	check(iron_file_exists(string("%s/test_material.arm", dir_material)), "material exported");
}

// 36 - Save project

void step_save() {
	saved_layers = script_layer_count();
	project_filepath_set(file_project);
	project_save(false);
	check(string_equals(project_filepath_get(), file_project), "project filepath set");
}

// 37 - Saved file is on disk

void step_verify_save() {
	check(iron_file_exists(file_project), "project saved");
}

// 38 - New project

void step_new_project() {
	script_project_new();
	int ok = 0;
	if (context_main_object() != NULL) {
		ok = 1;
	}
	check(ok, "new project");
	check(script_layer_get("test_red") == NULL && script_get_object("test_box") == NULL, "new project is empty");
}

// 39 - Load the saved project back

void step_load() {
	script_project_open(file_project);
	check(1, "project loaded");
}

// 40 - Loaded content is there

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

	check(script_layer_count() == saved_layers, string("layer count survived (%d of %d)", script_layer_count(), saved_layers));
	slot_layer_t *red   = script_layer_get("test_red");
	slot_layer_t *group = script_layer_get("test_group");
	slot_layer_t *fill  = script_layer_get("test_fill");
	ok                  = red != NULL && group != NULL && fill != NULL && red->parent == group && !group->visible && !fill->visible;
	check(ok, "layers, groups and visibility survived");
	ok = fill != NULL && fill->fill_material != NULL && script_get_material("test_fill") != NULL;
	check(ok, "fill layer material survived");

	check(script_get_object("test_box") != NULL && script_get_object("test_geom") != NULL, "objects survived");
	script_set_stage("test_stage_b");
	ok = script_get_stage() != NULL && string_equals(script_get_stage(), "test_stage_b");
	check(ok, "stages survived");
	project_t *prj = script_get_project();
	check(prj->envmap != NULL && ends_with(prj->envmap, "test_envmap.hdr"), "envmap survived");
}

// 41 - Import one of the exported textures back in

void step_import_texture() {
	any_array_t *files = file_read_directory(dir_textures);
	int          ok    = 0;
	if (files != NULL) {
		if (files->length > 0) {
			char *name = files->buffer[0];
			script_import_asset(string("%s/%s", dir_textures, name), false);
			if (script_get_texture(name) != NULL) {
				script_texture_delete(name);
				if (script_get_texture(name) == NULL) {
					ok = 1;
				}
			}
			script_import_asset(string("%s/%s", dir_textures, name), false);
		}
	}
	check(ok, "texture imported, deleted and reimported");
}

// 42 - Import the exported mesh

void step_import_mesh() {
	char *path = string("%s/test_mesh.obj", dir_mesh);
	script_import_asset(path, false);
	check(1, "mesh imported");
}

// 43 - Import the exported material

void step_import_material() {
	script_import_asset(string("%s/test_material.arm", dir_material), false);
	check(1, "material imported");
}

// 44 - Paint once more on the reloaded project, then save over it

void step_repaint_and_resave() {
	context_select_tool(TOOL_TYPE_BRUSH);
	script_paint(0.45, 0.45);
	script_paint(0.5, 0.5);
	script_paint_end();
	project_save(false);
	check(1, "repaint and resave");
}

// Sculpt

f32_array_t *sculpt_base;  // Vertex positions before sculpting
f32_array_t *sculpt_prev;  // Positions after the previous stroke
f32_array_t *sculpt_saved; // Positions in the saved project
int          diff_moved  = 0;
float        diff_max    = 0.0;
float        diff_radial = 0.0; // Summed change of distance from the origin, the sphere center

// Exports the mesh, the sculpt workflow writes the sculpted positions, and reads the vertex positions back
f32_array_t *sculpt_positions() {
	script_export_mesh(file_sculpt_obj);
	buffer_t *b = script_load_blob(string("%s.obj", file_sculpt_obj));
	if (b == NULL) {
		return NULL;
	}
	any_array_t *lines = string_split(sys_buffer_to_string(b), "\n");
	f32_array_t *pos   = f32_array_create(0);
	for (int i = 0; i < lines->length; ++i) {
		char *l = lines->buffer[i];
		if (starts_with(l, "v ")) {
			any_array_t *p = string_split(l, " ");
			f32_array_push(pos, parse_float(p->buffer[1]));
			f32_array_push(pos, parse_float(p->buffer[2]));
			f32_array_push(pos, parse_float(p->buffer[3]));
		}
	}
	return pos;
}

float vec_length(f32_array_t *a, int i) {
	return sqrt(a->buffer[i] * a->buffer[i] + a->buffer[i + 1] * a->buffer[i + 1] + a->buffer[i + 2] * a->buffer[i + 2]);
}

// Compares two position sets into diff_moved, diff_max and diff_radial, false when the vertex counts differ
int sculpt_diff(f32_array_t *a, f32_array_t *b) {
	diff_moved  = 0;
	diff_max    = 0.0;
	diff_radial = 0.0;
	if (a == NULL || b == NULL || a->length != b->length) {
		return 0;
	}
	for (int i = 0; i < a->length; i += 3) {
		float dx = a->buffer[i] - b->buffer[i];
		float dy = a->buffer[i + 1] - b->buffer[i + 1];
		float dz = a->buffer[i + 2] - b->buffer[i + 2];
		float d  = sqrt(dx * dx + dy * dy + dz * dz);
		if (d > 0.0001) {
			diff_moved += 1;
			diff_radial += vec_length(a, i) - vec_length(b, i);
		}
		if (d > diff_max) {
			diff_max = d;
		}
	}
	return 1;
}

void sculpt_stroke(int tool, float y) {
	context_select_tool(tool);
	for (int i = 0; i < 8; ++i) {
		script_paint(0.4 + i * 0.025, y);
	}
	script_paint_end();
}

// 45 - Sculpt workflow on a dense sphere, the default cube has too few vertices

void step_sculpt_setup() {
	script_project_new();
	object_t *cube   = context_main_object()->base;
	object_t *sphere = script_shape_add("sphere");
	script_object_remove(cube);
	int ok = sphere != NULL && context_main_object()->base == sphere && index_count(sphere) > 10000;
	check(ok, "sphere replaces the cube");

	script_set_tools(TOOLS_SCULPT);
	check(script_get_config()->tools == TOOLS_SCULPT, "sculpt workflow");
}

// 46 - Switching creates a sculpt layer on the next update

void step_sculpt_layer() {
	context_t *ctx = script_get_context();
	check(ctx->layer->texpaint_sculpt != NULL, string("sculpt layer %s selected", ctx->layer->name));
	sculpt_base = sculpt_positions();
	sculpt_prev = sculpt_base;
	check(sculpt_base != NULL && sculpt_base->length / 3 > 1000, "sculpted mesh exported");
}

// 47 - Draw pushes the surface out

void step_sculpt_draw() {
	context_t *ctx      = script_get_context();
	ctx->brush_sculpt   = SCULPT_TYPE_DRAW;
	ctx->brush_radius   = 0.5;
	ctx->brush_opacity  = 1.0;
	sculpt_stroke(TOOL_TYPE_BRUSH, 0.5);
	f32_array_t *now = sculpt_positions();
	int          ok  = sculpt_diff(now, sculpt_prev) && diff_moved > 0 && diff_radial > 0.0;
	check(ok, string("draw pushes %d vertices out (max %.4f)", diff_moved, diff_max));
	sculpt_prev = now;
}

// 48 - Every sculpt brush mode moves the mesh, one mode per frame

void step_sculpt_modes() {
	context_t *ctx    = script_get_context();
	ctx->brush_sculpt = sub;
	sculpt_stroke(TOOL_TYPE_BRUSH, 0.45 + sub * 0.01);
	f32_array_t *now = sculpt_positions();
	if (!sculpt_diff(now, sculpt_prev) || diff_moved == 0) {
		sweep_ok = 0;
		printf("[test]   sculpt mode %d moved nothing", sub);
	}
	sculpt_prev = now;
	if (sub < SCULPT_TYPE_PLATEAU) {
		step_again = 1;
		return;
	}
	ctx->brush_sculpt = SCULPT_TYPE_DRAW;
	check(sweep_ok, "every sculpt mode moves the mesh");
	sweep_ok = 1;
}

// 49 - Blur relaxes, the eraser pushes the surface in

void step_sculpt_blur_eraser() {
	context_t *ctx = script_get_context();
	sculpt_stroke(TOOL_TYPE_BLUR, 0.5);
	f32_array_t *now = sculpt_positions();
	check(sculpt_diff(now, sculpt_prev) && diff_moved > 0, string("blur moves %d vertices", diff_moved));
	sculpt_prev = now;

	sculpt_stroke(TOOL_TYPE_ERASER, 0.5);
	now = sculpt_positions();
	check(sculpt_diff(now, sculpt_prev) && diff_moved > 0 && diff_radial < 0.0, string("eraser pushes %d vertices in", diff_moved));
	sculpt_prev = now;
	context_select_tool(TOOL_TYPE_BRUSH);
}

// 50 - Save the sculpted project

void step_sculpt_save() {
	sculpt_saved = sculpt_positions();
	sculpt_diff(sculpt_saved, sculpt_base);
	check(diff_moved > 0, string("%d vertices differ from the original", diff_moved));
	project_filepath_set(file_sculpt);
	project_save(false);
}

// 51 - A new project in the sculpt workflow, the open on the next frame used to crash on its deferred layer clear

void step_sculpt_new() {
	check(iron_file_exists(file_sculpt), "sculpt project saved");
	script_project_new();
	check(script_get_config()->tools == TOOLS_SCULPT, "new project keeps the sculpt workflow");
}

// 52 - Load the sculpted project

void step_sculpt_load() {
	script_project_open(file_sculpt);
	check(1, "sculpt project loaded");
}

// 53 - The sculpt survived save and load

void step_sculpt_verify() {
	context_t *ctx = script_get_context();
	check(ctx->layer->texpaint_sculpt != NULL, "sculpt layer after load");
	int ok = sculpt_diff(sculpt_positions(), sculpt_saved) && diff_moved == 0;
	check(ok, string("sculpt survived save and load (%d vertices moved)", diff_moved));
}

// 54 - Back to painting

void step_sculpt_end() {
	context_t *ctx = script_get_context();
	script_set_tools(TOOLS_PAINT);
	int ok = script_get_config()->tools == TOOLS_PAINT && ctx->layer->texpaint_sculpt == NULL;
	check(ok, string("paint workflow, layer %s selected", ctx->layer->name));
}

// Extras on a fresh project

slot_material_t *mat_blue;
float            mix_fraction = 0.0; // Painted area of the reference stroke

slot_material_t *flat_material(char *name, float r, float g, float b) {
	slot_material_t *m   = script_material_create(name);
	ui_node_t       *rgb = script_material_create_node_at("RGB", -300.0, 0.0);
	script_material_set_color(rgb, 0, 0, r, g, b, 1.0);
	script_material_connect(rgb, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	return m;
}

// Paints a blue stroke over a fresh red layer and returns the fraction of the expected color, the layer is deleted again
float blend_fraction(int blending, float opacity, int r, int g, int b) {
	context_t    *ctx  = script_get_context();
	slot_layer_t *l    = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	ctx->brush_blending = blending;
	ctx->brush_opacity  = opacity;
	ctx->brush_radius   = 0.5;
	stroke_across(TOOL_TYPE_BRUSH);
	float f             = pixels_fraction(export_base_pixels(), r, g, b, 40);
	ctx->brush_blending = 0;
	ctx->brush_opacity  = 1.0;
	script_layer_delete(l);
	return f;
}

// 55 - Blending modes give the expected colors: blue painted over red

void step_blend_colors() {
	script_project_new();
	mat_blue  = flat_material("test_blue", 0.0, 0.0, 1.0);
	float mix    = blend_fraction(0, 1.0, 0, 0, 255);
	mix_fraction = mix;
	check(mix > 0.005 && mix < 0.6, string("mix stroke (%.3f blue)", mix));

	// Mode, expected color
	i32_array_t *modes = i32_array_create(0);
	i32_array_push(modes, 1); // Darken, black
	i32_array_push(modes, 0x000000);
	i32_array_push(modes, 2); // Multiply
	i32_array_push(modes, 0x000000);
	i32_array_push(modes, 4); // Lighten, magenta
	i32_array_push(modes, 0xff00ff);
	i32_array_push(modes, 5); // Screen
	i32_array_push(modes, 0xff00ff);
	i32_array_push(modes, 7); // Add
	i32_array_push(modes, 0xff00ff);
	i32_array_push(modes, 11); // Difference
	i32_array_push(modes, 0xff00ff);
	for (int i = 0; i < modes->length; i += 2) {
		int   c = modes->buffer[i + 1];
		float f = blend_fraction(modes->buffer[i], 1.0, (c >> 16) & 255, (c >> 8) & 255, c & 255);
		check(f > mix * 0.8, string("blending %d gives %06x (%.3f of %.3f)", modes->buffer[i], c, f, mix));
	}
}

// 56 - Half brush opacity mixes the stroke with the layer

void step_brush_opacity() {
	float half = blend_fraction(0, 0.5, 128, 0, 128);
	check(half > mix_fraction * 0.8, string("half opacity stroke is purple (%.3f)", half));
	float blue = pixels_fraction(script_get_texture_pixels(script_get_texture(export_name)), 0, 0, 255, 40);
	check(blue < 0.005, string("half opacity stroke is not blue (%.3f)", blue));
}

// 57 - Material channel toggles: a stroke without base color and roughness changes nothing, roughness alone paints only roughness

void step_material_channels() {
	slot_layer_t *l       = script_layer_new_color(0xffff0000, 1.0, 0.0, 0.0);
	mat_blue->paint_base  = 0;
	mat_blue->paint_rough = 0;
	script_material_update();
	stroke_across(TOOL_TYPE_BRUSH);
	float blue   = pixels_fraction(export_base_pixels(), 0, 0, 255, 40);
	float smooth = pixels_fraction(export_channel_pixels("_rough"), 0, 0, 0, 20);
	check(blue < 0.001 && smooth > 0.999, string("base color and roughness off (%.3f blue, %.3f smooth)", blue, smooth));

	mat_blue->paint_rough = 1;
	script_material_update();
	stroke_across(TOOL_TYPE_BRUSH);
	blue   = pixels_fraction(export_base_pixels(), 0, 0, 255, 40);
	smooth = pixels_fraction(export_channel_pixels("_rough"), 0, 0, 0, 20);
	check(blue < 0.001 && smooth < 1.0 - mix_fraction * 0.8, string("roughness only (%.3f blue, %.3f smooth)", blue, smooth));

	mat_blue->paint_base = 1;
	script_material_update();
	script_layer_delete(l);
}

// 58 - Custom material node from a plugin category

int   custom_node_calls = 0;
char *custom_node_type  = "TEST_CUSTOM";

char *custom_node(ui_node_t *node, char *socket_name) {
	custom_node_calls += 1;
	return "float3(1.0, 0.0, 1.0)";
}

ui_node_socket_t *custom_socket(char *name, char *type) {
	ui_node_socket_t *s = calloc(1, sizeof(ui_node_socket_t));
	s->name             = name;
	s->type             = type;
	s->color            = 0xffc7c729;
	s->default_value    = f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0);
	s->max              = 1.0;
	s->precision        = 100.0;
	return s;
}

void step_custom_node() {
	any_array_t *list = any_array_create(0);
	ui_node_t   *n    = calloc(1, sizeof(ui_node_t));
	n->name           = "Test Custom";
	n->type           = custom_node_type;
	n->color          = 0xffb34f5a;
	n->inputs         = any_array_create(0);
	n->outputs        = any_array_create(0);
	n->buttons        = any_array_create(0);
	any_array_push(n->outputs, custom_socket("Color", "RGBA"));
	any_array_push(list, n);
	script_material_category_add("Test", list);
	script_material_custom_nodes_set(custom_node_type, custom_node);

	slot_material_t *m  = script_material_create("test_custom");
	ui_node_t       *cn = script_material_create_node_at(custom_node_type, -300.0, 0.0);
	check(cn != NULL && string_equals(cn->type, custom_node_type), "custom node created");
	if (cn == NULL) {
		return;
	}
	script_material_connect(cn, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	slot_layer_t *l = script_layer_new_fill();
	float         f = pixels_fraction(export_base_pixels(), 255, 0, 255, 40);
	check(custom_node_calls > 0 && f > 0.5, string("custom node output (%d calls, %.2f magenta)", custom_node_calls, f));

	script_layer_delete(l);
	script_material_delete(m);
	script_material_custom_nodes_remove(custom_node_type);
	script_material_category_remove("Test");
	check(script_material_create_node("TEST_CUSTOM") == NULL, "custom node category removed");
	script_material_set(mat_blue);
}

// 59 - Custom viewport shader is called when the viewport shader is rebuilt

int viewport_shader_calls = 0;

void viewport_shader(void *shader) {
	viewport_shader_calls += 1;
	node_shader_write_frag(shader, "output_color = float3(1.0, 0.0, 1.0);");
}

void step_viewport_shader() {
	context_t *ctx = script_get_context();
	if (sub == 0) {
		context_set_viewport_shader(viewport_shader);
		script_layer_update(ctx->layer); // Rebuilds the viewport shader
		step_again = 1;
		return;
	}
	if (sub < 3) {
		step_again = 1;
		return;
	}
	check(viewport_shader_calls > 0, string("viewport shader called %d times", viewport_shader_calls));
	check(ctx->viewport_mode == 17, "viewport shader switches to the plugin mode"); // VIEWPORT_MODE_PLUGIN
	context_set_viewport_shader(NULL);
	check(ctx->viewport_mode == 0, "removing the viewport shader returns to lit");
	script_layer_update(ctx->layer);
	int calls = viewport_shader_calls;
	script_layer_update(ctx->layer);
	check(viewport_shader_calls == calls, "viewport shader removed");
}

// 60 - Draw into a render target, read it back, write it as png and jpg and import both

char *file_draw_png;
char *file_draw_jpg;

void step_draw_target() {
	gpu_texture_t *t = gpu_create_render_target(64, 64, GPU_TEXTURE_FORMAT_RGBA32);
	draw_begin(t, true, 0xff000000);
	draw_set_color(0xff00ff00);
	draw_filled_rect(0.0, 0.0, 32.0, 64.0);
	draw_end();
	buffer_t *pixels = script_get_texture_pixels(t);
	float     green  = pixels_fraction(pixels, 0, 255, 0, 8);
	float     black  = pixels_fraction(pixels, 0, 0, 0, 8);
	check(green > 0.45 && green < 0.55 && black > 0.45, string("render target drawn (%.2f green, %.2f black)", green, black));

	iron_write_png(file_draw_png, pixels, 64, 64, 0);
	iron_write_jpg(file_draw_jpg, pixels, 64, 64, 0, 90);
	check(iron_file_exists(file_draw_png) && iron_file_exists(file_draw_jpg), "png and jpg written");

	script_import_asset(file_draw_png, false);
	script_import_asset(file_draw_jpg, false);
	gpu_texture_t *png = script_get_texture("test_draw.png");
	gpu_texture_t *jpg = script_get_texture("test_draw.jpg");
	int            ok  = png != NULL && jpg != NULL && png->width == 64 && jpg->height == 64;
	check(ok, "png and jpg imported");
	if (ok) {
		float pg = pixels_fraction(script_get_texture_pixels(png), 0, 255, 0, 8);
		float jg = pixels_fraction(script_get_texture_pixels(jpg), 0, 255, 0, 24);
		check(pg > 0.45 && pg < 0.55 && jg > 0.4 && jg < 0.6, string("imported pixels kept (%.2f png, %.2f jpg green)", pg, jg));
	}
}

// 61 - Packed assets

char *file_packed;

void step_packed_assets() {
	int packed = script_pack_assets("test_draw.png");
	check(packed == 1, string("asset packed (%d)", packed));
	if (iron_file_exists(file_packed)) {
		iron_delete_file(file_packed);
	}
	int saved = script_packed_asset_save("test_draw.png", file_packed);
	check(saved && iron_file_exists(file_packed), "packed asset saved to a file");
	check(script_packed_assets_remove("test_draw.png") == 1, "packed asset removed");
	int removed = script_assets_remove("test_draw");
	check(removed == 2 && script_get_texture("test_draw.png") == NULL && script_get_texture("test_draw.jpg") == NULL,
	      string("assets removed (%d)", removed));
}

// 62 - Custom mesh, texture and text importers

int   text_imports    = 0;
int   texture_imports = 0;
char *dir_custom;

void *import_test_mesh(char *path) {
	i16_array_t *posa = i16_array_create(16);
	i16_array_t *nora = i16_array_create(8);
	u32_array_t *inda = u32_array_create(6);
	// A quad in the xy plane facing +z, the normal z is in posa w
	int xs[4] = {-32767, 32767, 32767, -32767};
	int ys[4] = {-32767, -32767, 32767, 32767};
	for (int i = 0; i < 4; ++i) {
		posa->buffer[i * 4]     = xs[i];
		posa->buffer[i * 4 + 1] = ys[i];
		posa->buffer[i * 4 + 2] = 0;
		posa->buffer[i * 4 + 3] = 32767;
	}
	int ind[6] = {0, 1, 2, 0, 2, 3};
	for (int i = 0; i < 6; ++i) {
		inda->buffer[i] = ind[i];
	}
	return script_make_raw_mesh("test_custom_mesh", posa, nora, inda, 1.0);
}

void *import_test_texture(char *path) {
	texture_imports += 1;
	return gpu_create_render_target(32, 16, GPU_TEXTURE_FORMAT_RGBA32);
}

void import_test_text(char *path) {
	buffer_t *b = script_load_blob(path);
	if (b != NULL && string_equals(sys_buffer_to_string(b), "hello")) {
		text_imports += 1;
	}
}

void write_text_file(char *path, char *text) {
	buffer_t *b = sys_string_to_buffer(text);
	iron_file_save_bytes(path, b, b->length);
}

void step_custom_importers() {
	char *mesh_path    = string("%s/test.tmsh", dir_custom);
	char *texture_path = string("%s/test.ttex", dir_custom);
	char *text_path    = string("%s/test.ttxt", dir_custom);
	write_text_file(mesh_path, "mesh");
	write_text_file(texture_path, "texture");
	write_text_file(text_path, "hello");

	script_register_mesh("tmsh", import_test_mesh);
	script_register_texture("ttex", import_test_texture);
	script_register_text("ttxt", import_test_text);

	script_append_mesh(mesh_path);
	object_t *o = script_get_object("test_custom_mesh");
	check(o != NULL && index_count(o) == 6, "custom mesh importer");

	script_import_asset(texture_path, false);
	gpu_texture_t *t = script_get_texture("test.ttex");
	check(texture_imports == 1 && t != NULL && t->width == 32 && t->height == 16, "custom texture importer");

	script_import_asset(text_path, false);
	check(text_imports == 1, "custom text importer");

	script_unregister_mesh("tmsh");
	script_unregister_texture("ttex");
	script_unregister_text("ttxt");
	script_import_asset(text_path, false); // Unknown format now, shows a message box
	check(text_imports == 1, "custom text importer unregistered");
	if (o != NULL) {
		script_object_remove(o);
	}
	script_assets_remove("test.ttex");
}

// 63 - Font and sound import

char *file_font;
char *file_sound;

void write_wav(char *path) {
	int       samples = 4410;
	buffer_t *b       = buffer_create(44 + samples * 2);
	char     *tags    = "RIFFWAVEfmt data";
	for (int i = 0; i < 4; ++i) {
		buffer_set_u8(b, i, char_code_at(tags, i));
		buffer_set_u8(b, 8 + i, char_code_at(tags, 4 + i));
		buffer_set_u8(b, 12 + i, char_code_at(tags, 8 + i));
		buffer_set_u8(b, 36 + i, char_code_at(tags, 12 + i));
	}
	buffer_set_u32(b, 4, 36 + samples * 2);
	buffer_set_u32(b, 16, 16);
	buffer_set_u16(b, 20, 1); // PCM
	buffer_set_u16(b, 22, 1); // Mono
	buffer_set_u32(b, 24, 44100);
	buffer_set_u32(b, 28, 44100 * 2);
	buffer_set_u16(b, 32, 2);
	buffer_set_u16(b, 34, 16);
	buffer_set_u32(b, 40, samples * 2);
	for (int i = 0; i < samples; ++i) {
		buffer_set_i16(b, 44 + i * 2, (int)(sin(i * 0.06) * 8000.0));
	}
	iron_file_save_bytes(path, b, b->length);
}

void step_font_sound() {
	file_copy(string("%s/font_mono.ttf", data_path()), file_font);
	check(iron_file_exists(file_font), "font copied");
	script_import_asset(file_font, false);
	check(1, "font imported");

	write_wav(file_sound);
	script_import_asset(file_sound, false);
	check(script_get_sound("test_sound.wav") != NULL, "sound imported");
	check(script_sound_remove("test_sound.wav") && script_get_sound("test_sound.wav") == NULL, "sound removed");
}

// 64 - Mesh geometry functions outside a mesh script node

void step_mesh_geo() {
	mesh_geo_t *box   = mesh_geo_shape(0, 1.0, 1.0, 1.0, 8);
	mesh_geo_t *plane = mesh_geo_shape(5, 1.0, 1.0, 1.0, 8);
	mesh_geo_t *ball  = mesh_geo_shape(1, 1.0, 1.0, 1.0, 24);
	int         ok    = box->index_count == 36 && plane->index_count == 6 && ball->index_count > 1000;
	check(ok, string("mesh geo shapes (%d, %d, %d indices)", box->index_count, plane->index_count, ball->index_count));
	ok = 1;
	for (int s = 0; s < 6; ++s) {
		mesh_geo_t *g = mesh_geo_shape(s, 1.0, 2.0, 1.0, 12);
		if (g->index_count == 0 || g->index_count % 3 != 0) {
			ok = 0;
		}
	}
	check(ok, "every mesh geo shape");

	mesh_geo_t *moved = mesh_geo_transform(box, 1.0, 2.0, 3.0, 0.0, 0.0, 0.0, 2.0, 2.0, 2.0);
	float       cx    = 0.0;
	for (int i = 0; i < moved->vertex_count; ++i) {
		cx += moved->pos[i * 3];
	}
	cx /= moved->vertex_count;
	check(moved->vertex_count == box->vertex_count && fabs(cx - 1.0) < 0.01, string("mesh geo transform (center x %.3f)", cx));

	mesh_geo_t *arr = mesh_geo_array(plane, 4, 1.5, 0.0, 0.0, false);
	check(arr->index_count == plane->index_count * 4, "mesh geo array");
	check(mesh_geo_join(box, plane)->index_count == 42, "mesh geo join");
	check(mesh_geo_mirror(box, 0)->index_count >= box->index_count, "mesh geo mirror");
	check(mesh_geo_copy(ball)->vertex_count == ball->vertex_count, "mesh geo copy");

	mesh_geo_t *sub = mesh_geo_subdivide(box, 2);
	check(sub->index_count > box->index_count * 4, string("mesh geo subdivide (%d)", sub->index_count));
	check(mesh_geo_smooth(sub, 2)->index_count == sub->index_count, "mesh geo smooth");
	int dec = mesh_geo_decimate(ball, 0.5)->index_count;
	check(dec > 0 && dec < ball->index_count, string("mesh geo decimate %d -> %d", ball->index_count, dec));
	check(mesh_geo_bevel(box, 0.1)->index_count > box->index_count, "mesh geo bevel");
	check(mesh_geo_normals(ball, true)->index_count == ball->index_count, "mesh geo normals");

	mesh_geo_t *small = mesh_geo_transform(ball, 0.5, 0.5, 0.5, 0.0, 0.0, 0.0, 0.6, 0.6, 0.6);
	mesh_geo_t *diff  = mesh_geo_boolean(box, small, MESH_BOOLEAN_DIFFERENCE);
	mesh_geo_t *inter = mesh_geo_boolean(box, small, MESH_BOOLEAN_INTERSECT);
	ok                = diff->index_count > 0 && inter->index_count > 0 && diff->index_count != box->index_count;
	check(ok, string("mesh geo boolean (%d difference, %d intersect)", diff->index_count, inter->index_count));

	mesh_geo_t  *dst = mesh_geo_create(box->vertex_count * 2, box->index_count * 2);
	f32_array_t *m   = f32_array_create(12);
	m->buffer[0]     = 1.0;
	m->buffer[4]     = 1.0;
	m->buffer[8]     = 1.0;
	m->buffer[9]     = 3.0;
	dst->vertex_count = 0;
	dst->index_count  = 0;
	ok                = mesh_geo_append(dst, box, m->buffer) && mesh_geo_append(dst, box, m->buffer) && !mesh_geo_append(dst, box, m->buffer);
	check(ok && dst->index_count == box->index_count * 2, string("mesh geo append (%d indices)", dst->index_count));
}

// 65 - Procedural geom: primitives, sdf, tubes, atlas

char *file_atlas;
char *file_skin;

void step_geom_more() {
	geom_t *grid = geom_grid(-1.0, -1.0, 1.0, 1.0, 4, 4);
	geom_t *cyl  = geom_cylinder(0.0, 0.0, 0.0, 0.5, 1.0, 16);
	geom_t *ell  = geom_ellipsoid(0.0, 0.0, 0.0, 0.5, 0.3, 0.2, 16, 8);
	int     ok   = geom_vertex_count(grid) > 0 && geom_vertex_count(cyl) > 0 && geom_vertex_count(ell) > 0;
	check(ok, string("geom primitives (%d, %d, %d vertices)", geom_vertex_count(grid), geom_vertex_count(cyl), geom_vertex_count(ell)));

	geom_t *c  = geom_copy(cyl);
	float   z0 = geom_vertex_get(c, 0, 2);
	geom_translate(c, 0.0, 0.0, 2.0);
	geom_scale(c, 2.0);
	float z1 = geom_vertex_get(c, 0, 2);
	check(geom_vertex_count(c) == geom_vertex_count(cyl) && fabs(geom_vertex_get(cyl, 0, 2) - z0) < 0.0001 && fabs(z1 - z0) > 1.0,
	      string("geom copy, translate, scale (z %.2f -> %.2f)", z0, z1));
	geom_vertex_set(c, 0, 1.0, 2.0, 3.0);
	check(fabs(geom_vertex_get(c, 0, 1) - 2.0) < 0.0001, "geom vertex set");
	geom_flip(c);
	geom_basis(c, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0);

	geom_t *half = geom_copy(ell);
	geom_keep(half, 2, 0.0, 10.0);
	check(geom_vertex_count(half) < geom_vertex_count(ell) && geom_vertex_count(half) > 0, string("geom keep (%d of %d)", geom_vertex_count(half), geom_vertex_count(ell)));

	geom_sdf_begin(0.05);
	geom_sdf_box(-0.3, -0.3, -0.3, 0.3, 0.3, 0.3, 0.05, 0);
	geom_sdf_ellipsoid(0.0, 0.0, 0.3, 0.3, 0.3, 0.3, 1); // Subtract
	geom_sdf_transform(2, 45.0, 0.0, 0.0, 0.4);
	geom_sdf_cylinder(0.0, 0.0, 0.0, 0.1, 0.4, 2, 0.0, 0);
	geom_sdf_capsule(-0.4, 0.0, 0.0, 0.4, 0.0, 0.0, 0.05, 0.1, 0);
	geom_sdf_torus(0.0, 0.0, -0.3, 0.3, 0.05, 1.0, 0);
	geom_t *sdf = geom_sdf_end(1);
	check(sdf != NULL && geom_vertex_count(sdf) > 100, string("geom sdf (%d vertices)", geom_vertex_count(sdf)));
	geom_clay(sdf, 0.01, 4.0, 1.0, 0.5);
	geom_displace(sdf, 2.0, 0.01, 8.0, 0.005);

	geom_tube_begin();
	geom_tube_point(0.0, 0.0, 0.0, 0.1);
	geom_tube_point(0.0, 0.5, 0.3, 0.08);
	geom_tube_point(0.2, 1.0, 0.4, 0.05);
	geom_t *tube = geom_tube_end(8, 6, 1.0, 4, 1);
	check(tube != NULL && geom_vertex_count(tube) > 24, string("geom tube (%d vertices)", geom_vertex_count(tube)));

	// Two colored parts in a texture atlas
	geom_part(sdf, 1);
	geom_part(tube, 2);
	geom_t *parts = geom_merge(sdf, tube); // geom_copy drops the parts
	geom_unwrap(parts, 0.01);
	geom_palette(0, 0, 0, 0);
	geom_palette(1, 255, 0, 0);
	geom_palette(2, 0, 0, 255);
	if (iron_file_exists(file_atlas)) {
		iron_delete_file(file_atlas);
	}
	geom_atlas(parts, 128, 1, file_atlas);
	check(iron_file_exists(file_atlas), "geom atlas written");
	script_import_asset(file_atlas, false);
	buffer_t *pixels = script_get_texture_pixels(script_get_texture("test_atlas.png"));
	float     red    = pixels_fraction(pixels, 255, 0, 0, 30);
	float     blue   = pixels_fraction(pixels, 0, 0, 255, 30);
	check(red > 0.01 && blue > 0.001, string("geom atlas has both parts (%.3f red, %.3f blue)", red, blue));
	script_assets_remove("test_atlas");
}

// 66 - Skinned mesh: bones, weights and a clip, written as .skin, imported and reskinned

float mesh_max_x(object_t *o) {
	mesh_object_t  *mo  = o->ext;
	vertex_array_t *va  = mesh_data_get_vertex_array(mo->data, "pos");
	float           mx  = -100000.0;
	for (int i = 0; i < va->values->length; i += 4) {
		float x = va->values->buffer[i] / 32767.0 * mo->data->scale_pos;
		if (x > mx) {
			mx = x;
		}
	}
	return mx;
}

void step_skin() {
	// A vertical bar with two bones, the upper one bends
	geom_t *g = geom_cylinder(0.0, 0.0, 0.5, 0.05, 1.0, 12);
	geom_unwrap(g, 0.01);
	skel_begin();
	int b0 = skel_bone(-1, 0.0, 0.0, 0.0, 0.0, 0.0, 0.5, 0.0, -1.0, 0.0);
	int b1 = skel_bone(b0, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0, 0.0, -1.0, 0.0);
	for (int v = 0; v < geom_vertex_count(g); ++v) {
		if (geom_vertex_get(g, v, 2) > 0.5) {
			geom_weights(g, v, b1, 1.0, b0, 0.0, b0, 0.0);
		}
		else {
			geom_weights(g, v, b0, 1.0, b1, 0.0, b1, 0.0);
		}
	}
	skel_clip_begin();
	for (int f = 0; f < 4; ++f) {
		skel_pose(b1, f * 25.0, f * 25.0, 0.0, 0.0, 0.0, 0.0);
		skel_frame();
	}
	if (iron_file_exists(file_skin)) {
		iron_delete_file(file_skin);
	}
	int ok = skel_write(g, "test_skin", file_skin);
	check(ok && iron_file_exists(file_skin), "skin written");
	script_append_mesh_skinned(file_skin);
	object_t *skinned = script_get_object("test_skin");
	check(skinned != NULL && index_count(skinned) > 0, "skinned mesh appended");
	if (skinned == NULL) {
		return;
	}
	project_reskin_mesh(0);
	float x0 = mesh_max_x(skinned);
	ok       = project_reskin_mesh(3);
	float x3 = mesh_max_x(skinned);
	check(ok && fabs(x3 - x0) > 0.1, string("reskin bends the mesh (max x %.3f -> %.3f)", x0, x3));
	script_clip_play(skinned, 0, 3);
	script_clip_set_frame(skinned, 2);
	check(1, "clip play and set frame");
	script_object_remove(skinned);
}

// 70 - Duplicate and merge paint objects

void step_duplicate_merge() {
	context_t      *ctx    = script_get_context();
	mesh_object_t  *before = ctx->paint_object;
	int             count  = index_count(before->base);
	util_mesh_duplicate();
	mesh_object_t *dup = ctx->paint_object;
	check(dup != before && index_count(dup->base) == count, "paint object duplicated");
	dup->base->transform->loc.x = 3.0;
	transform_build_matrix(dup->base->transform);
	util_mesh_merge_geometry();
	mesh_object_t *main = context_main_object();
	check(index_count(main->base) == count * 2, string("paint objects merged (%d -> %d indices)", count, index_count(main->base)));
}

// 67 - Timers fire after their delay

int timer_fired = 0;

void on_timer() {
	timer_fired += 1;
}

void step_timer() {
	if (sub == 0) {
		timer_fired = 0;
		script_timer(0.1, on_timer);
		check(timer_fired == 0, "timer not fired at once");
	}
	if (timer_fired == 0 && sub < 120) {
		step_again = 1;
		return;
	}
	check(timer_fired == 1, string("timer fired after %d frames", sub));
}

// 68, 69 - Object workflow tools, camera controls, picking

void step_object_tools() {
	context_t *ctx = script_get_context();
	script_set_tools(TOOLS_OBJECT);
	check(script_get_config()->tools == TOOLS_OBJECT, "object workflow");
	int ok = 1;
	for (int i = TOOL_TYPE_CURSOR; i <= TOOL_TYPE_BAKE; ++i) {
		context_select_tool(i);
		if (ctx->tool != i) {
			ok = 0;
			printf("[test]   tool %d not selected", i);
		}
	}
	check(ok, "select object workflow tools");
	context_select_tool(TOOL_TYPE_CURSOR);
	script_set_tools(TOOLS_PAINT);
	context_select_tool(TOOL_TYPE_BRUSH);
	check(script_get_config()->tools == TOOLS_PAINT, "back to paint workflow");

	for (int i = 0; i < 3; ++i) {
		context_set_camera_controls(i);
	}
	context_set_camera_controls(0);
	ctx->envmap_angle = 1.0;
	ctx->xray         = 1;
	ctx->ddirty       = 2;
	check(1, "camera controls, envmap angle and xray");
}

void step_pick() {
	// Scripts can't move the cursor (mouse_x is a read-only copy), so this only runs the pick at the real cursor
	script_get_context()->xray = 0;
	script_pick_object();
	check(1, "pick object at the cursor");
}

// 71 - Messages and stage fades

int fade_frames = 0;

void step_message_fade() {
	if (sub == 0) {
		script_show_message("test message", 0.2);
		script_stage_create("test_fade");
		script_fade_to_stage("test_fade");
	}
	char *stage = script_get_stage();
	if ((stage == NULL || !string_equals(stage, "test_fade")) && sub < 240) {
		step_again = 1;
		return;
	}
	check(stage != NULL && string_equals(stage, "test_fade"), string("faded to stage after %d frames", sub));
}

// Layer operations from the layers context menu

int layer_names_unique() {
	for (int i = 0; i < script_layer_count(); ++i) {
		for (int j = i + 1; j < script_layer_count(); ++j) {
			if (string_equals(script_layer_at(i)->name, script_layer_at(j)->name)) {
				printf("[test]   duplicate layer name %s", script_layer_at(i)->name);
				return 0;
			}
		}
	}
	return 1;
}

float base_fraction(int color) {
	return pixels_fraction(export_base_pixels(), (color >> 16) & 255, (color >> 8) & 255, color & 255, 40);
}

// 72 - Duplicate keeps the content, layer blending mode

void step_layer_duplicate() {
	script_project_new();
	mat_blue           = flat_material("test_blue", 0.0, 0.0, 1.0);
	slot_layer_t *red  = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	script_layer_set_name(red, "test_red");
	stroke_across(TOOL_TYPE_BRUSH);
	float stroke = base_fraction(0x0000ff);
	int   count  = script_layer_count();

	slot_layer_t *dup = script_layer_duplicate(red);
	int           ok  = dup != NULL && dup != red && script_layer_count() == count + 1 && script_layer_index(dup) == script_layer_index(red) + 1;
	check(ok && script_get_context()->layer == dup && layer_names_unique(), string("layer %s duplicated above and selected", dup->name));
	red->visible = 0;
	script_layer_update(red);
	float dup_stroke = base_fraction(0x0000ff);
	float dup_red    = base_fraction(0xff0000);
	check(stroke > 0.005 && fabs(dup_stroke - stroke) < 0.002 && dup_red > 0.5,
	      string("duplicate has the content (%.3f of %.3f blue, %.2f red)", dup_stroke, stroke, dup_red));
	script_layer_delete(dup);
	red->visible = 1;
	script_layer_update(red);

	// Red multiplied over a blue layer is black
	slot_layer_t *blue = script_layer_new_color(0xff0000ff, 1.0, 0.5, 0.0);
	script_layer_move(blue, script_layer_index(red));
	red->blending = 2; // Multiply
	script_layer_update(red);
	float black = base_fraction(0x000000);
	red->blending = 0;
	script_layer_update(red);
	float red_again = base_fraction(0xff0000);
	check(black > 0.5 && red_again > 0.5, string("layer blending multiply (%.2f black, then %.2f red)", black, red_again));
}

// 73 - Merge down keeps what is shown, clear empties the layer

void step_layer_merge_down() {
	slot_layer_t *red  = script_layer_get("test_red");
	slot_layer_t *blue = script_layer_at(script_layer_index(red) - 1);
	check(script_layer_merge_down(script_layer_at(0)) == NULL, "lowest layer can't merge down");

	red->mask_opacity = 0.5;
	script_layer_update(red);
	float purple = base_fraction(0x800080);
	int   count  = script_layer_count();

	slot_layer_t *merged = script_layer_merge_down(red);
	int           ok     = merged == blue && script_layer_count() == count - 1 && script_layer_get("test_red") == NULL;
	check(ok && layer_names_unique(), "layer merged down into the one below");
	float merged_purple = base_fraction(0x800080);
	check(purple > 0.5 && fabs(merged_purple - purple) < 0.02, string("merge keeps the result (%.2f of %.2f purple)", merged_purple, purple));

	script_layer_clear(merged);
	float cleared = base_fraction(0x800080) + base_fraction(0x0000ff);
	check(script_layer_count() == count - 1 && cleared < 0.01, string("layer cleared (%.3f left)", cleared));
	script_layer_delete(merged);
}

// 74 - Invert and apply masks

void step_layer_mask_ops() {
	slot_layer_t *red  = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	int           count = script_layer_count();
	slot_layer_t *mask  = script_layer_new_mask(red, 0);
	check(base_fraction(0xff0000) < 0.05, "black mask hides the layer");

	check(!script_layer_invert_mask(red), "a layer is not inverted as a mask");
	int   ok    = script_layer_invert_mask(mask);
	float shown = base_fraction(0xff0000);
	check(ok && shown > 0.5, string("inverted mask shows the layer (%.2f red)", shown));
	script_layer_invert_mask(mask);
	float hidden = base_fraction(0xff0000);
	check(hidden < 0.05, string("inverted twice hides it again (%.2f red)", hidden));

	// Applying the black mask bakes it into the layer, which ends up empty
	slot_layer_t *parent = script_layer_apply_mask(mask);
	ok                   = parent == red && script_layer_count() == count && script_get_context()->layer == red;
	check(ok, "mask applied and removed");
	float applied = base_fraction(0xff0000);
	check(applied < 0.05, string("applied black mask erased the layer (%.2f red)", applied));
	script_layer_delete(red);

	// A white mask with a black stroke cuts the stroke out of the layer
	red  = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	mask = script_layer_new_mask(red, 1);
	slot_material_t *black = flat_material("test_black", 0.0, 0.0, 0.0);
	script_layer_set(mask);
	stroke_across(TOOL_TYPE_BRUSH);
	float masked = base_fraction(0xff0000);
	script_layer_apply_mask(mask);
	float baked = base_fraction(0xff0000);
	check(masked > 0.5 && masked < 0.995 && fabs(baked - masked) < 0.01, string("applied mask keeps the cut (%.3f -> %.3f red)", masked, baked));
	script_layer_delete(red);
	script_material_delete(black);
	script_material_set(mat_blue);
}

// 75 - Paint layer to fill layer and back

void step_layer_fill_ops() {
	slot_material_t *green = flat_material("test_green", 0.0, 1.0, 0.0);
	slot_layer_t    *l     = script_layer_new();
	check(!script_layer_to_paint(l), "a paint layer is not converted to paint");
	int ok = script_layer_to_fill(l) && l->fill_material == green;
	check(ok && base_fraction(0x00ff00) > 0.5, "converted to a green fill layer");
	check(!script_layer_to_fill(l), "a fill layer is not converted to fill");

	ok = script_layer_to_paint(l) && l->fill_material == NULL;
	// The paint layer keeps the green and no longer follows the material
	ui_node_t *rgb = NULL;
	for (int i = 0; i < green->canvas->nodes->length; ++i) {
		ui_node_t *n = green->canvas->nodes->buffer[i];
		if (string_equals(n->type, "RGB")) {
			rgb = n;
		}
	}
	script_material_set_color(rgb, 0, 0, 1.0, 1.0, 0.0, 1.0);
	script_material_update();
	script_layer_update(l); // Would refill a fill layer with yellow
	float kept = base_fraction(0x00ff00);
	check(ok && kept > 0.5, string("converted back to paint, content kept (%.2f green)", kept));
	script_layer_delete(l);
	script_material_delete(green);
	script_material_set(mat_blue);
}

// 76 - Merge a group into one layer

void step_layer_merge_group() {
	slot_layer_t *blue  = script_layer_new_color(0xff0000ff, 1.0, 0.5, 0.0);
	slot_layer_t *group = script_layer_new_group(blue);
	slot_layer_t *red   = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	script_layer_set_name(group, "test_merge_group");
	int ok = script_layer_set_group(red, group) && script_layer_index(red) > script_layer_index(blue);
	check(ok, "group with two layers");
	red->mask_opacity = 0.5;
	script_layer_update(red);
	float purple = base_fraction(0x800080);
	int   count  = script_layer_count();

	check(script_layer_merge_group(red) == NULL, "a layer is not merged as a group");
	slot_layer_t *merged = script_layer_merge_group(group);
	ok                   = merged != NULL && merged->parent == NULL && script_layer_count() == count - 2;
	ok = ok && string_equals(merged->name, "test_merge_group") && layer_names_unique();
	check(ok, string("group merged into one layer %s (%d -> %d layers)", merged->name, count, script_layer_count()));
	float merged_purple = base_fraction(0x800080);
	check(purple > 0.5 && fabs(merged_purple - purple) < 0.02, string("group merge keeps the result (%.2f of %.2f purple)", merged_purple, purple));
	script_texture_delete(export_name);
	export_name = NULL;
}

// 77 - Path layer: points paint a line with the current material, clear removes the points

char *file_paths;

void step_layer_path() {
	script_project_new();
	mat_blue        = flat_material("test_blue", 0.0, 0.0, 1.0);
	context_t *ctx  = script_get_context();
	ctx->brush_radius = 0.5;
	slot_layer_t *l = script_layer_new_path(false);
	check(l != NULL && l->path_points != NULL && !l->path_curved && l->path_material == mat_blue && layer_names_unique(),
	      string("path layer %s created", l->name));
	int ok = script_layer_add_path_point(l, 0.35, 0.5);
	float dot = base_fraction(0x0000ff);
	ok     = ok && script_layer_add_path_point(l, 0.65, 0.5);
	float line = base_fraction(0x0000ff);
	check(ok && l->path_points->length == 4 && dot > 0.0005 && line > dot * 2.0, string("path points paint a line (%.4f dot, %.4f line)", dot, line));
	check(!script_layer_add_path_point(script_layer_at(0), 0.5, 0.5), "no path points on a paint layer");

	script_layer_clear(l);
	float cleared = base_fraction(0x0000ff);
	check(l->path_points->length == 0 && cleared < 0.0005, string("path cleared (%.4f blue)", cleared));
	script_layer_add_path_point(l, 0.35, 0.4);
	script_layer_add_path_point(l, 0.65, 0.6);
	script_layer_set_name(l, "test_path");

	// To paint keeps the pixels and drops the points
	slot_layer_t *p = script_layer_new_path(false);
	script_layer_add_path_point(p, 0.4, 0.3);
	script_layer_add_path_point(p, 0.6, 0.3);
	float before = base_fraction(0x0000ff);
	ok           = script_layer_to_paint(p) && p->path_points == NULL && p->path_material == NULL;
	float after  = base_fraction(0x0000ff);
	check(ok && fabs(after - before) < 0.0005, string("path converted to paint (%.4f -> %.4f blue)", before, after));
	script_layer_delete(p);
}

// 78 - Curve and text layers

void step_layer_curve_text() {
	slot_layer_t *c  = script_layer_new_path(true);
	float         f0 = base_fraction(0x0000ff);
	// Anchor, control, anchor: the curve shows up with the second anchor
	script_layer_add_path_point(c, 0.35, 0.55);
	script_layer_add_path_point(c, 0.5, 0.65);
	float f1 = base_fraction(0x0000ff);
	script_layer_add_path_point(c, 0.65, 0.55);
	float f2 = base_fraction(0x0000ff);
	check(c->path_curved && c->path_points->length == 6 && f2 > f1 + 0.001, string("curve painted (%.4f, %.4f, %.4f blue)", f0, f1, f2));
	script_layer_set_name(c, "test_curve");

	int           count = script_layer_count();
	slot_layer_t *t     = script_layer_new_text("AB");
	check(t != NULL && t->path_text && string_equals(t->name, "AB") && script_layer_count() == count + 1, "text layer created");
	float before = base_fraction(0x0000ff);
	script_layer_add_path_point(t, 0.5, 0.45);
	float after = base_fraction(0x0000ff);
	check(after > before + 0.0002, string("text painted (%.4f -> %.4f blue)", before, after));
	check(script_layer_new_text("") == NULL && layer_names_unique(), "empty text refused, names unique");
}

// 79, 80, 81 - Path layers survive save and load

void step_layer_paths_save() {
	project_filepath_set(file_paths);
	project_save(false);
}

void step_layer_paths_load() {
	check(iron_file_exists(file_paths), "path project saved");
	script_project_open(file_paths);
}

void step_layer_paths_verify() {
	slot_layer_t *p  = script_layer_get("test_path");
	slot_layer_t *c  = script_layer_get("test_curve");
	slot_layer_t *t  = script_layer_get("AB");
	int           ok = p != NULL && c != NULL && t != NULL;
	ok               = ok && p->path_points != NULL && p->path_points->length == 4 && !p->path_curved;
	ok               = ok && c->path_points != NULL && c->path_points->length == 6 && c->path_curved;
	ok               = ok && t->path_text && t->path_material != NULL;
	check(ok, "path, curve and text layers survived save and load");
	// Repainting from the loaded points gives the same picture
	float before = base_fraction(0x0000ff);
	ok           = script_layer_add_path_point(p, 0.65, 0.35) && p->path_points->length == 6; // Clear of the curve and text
	float after  = base_fraction(0x0000ff);
	check(ok && after > before, string("loaded path keeps growing (%d, %.4f -> %.4f blue)", p->path_points->length, before, after));
	script_texture_delete(export_name);
	export_name = NULL;
}

void run_step(int s) {
	if (s == 0)
		step_tools();
	else if (s == 1)
		step_runtime();
	else if (s == 2)
		step_project_state();
	else if (s == 3)
		step_paint_brush();
	else if (s == 4)
		step_paint_blending();
	else if (s == 5)
		step_paint_tools();
	else if (s == 6)
		step_paint_eraser();
	else if (s == 7)
		step_fill();
	else if (s == 8)
		step_material_create();
	else if (s == 9)
		step_material_edit();
	else if (s == 10)
		step_material_nodes();
	else if (s == 11)
		step_shader_node();
	else if (s == 12)
		step_layer_color();
	else if (s == 13)
		step_layer_export();
	else if (s == 14)
		step_layer_hidden();
	else if (s == 15)
		step_layer_masks();
	else if (s == 16)
		step_layer_group();
	else if (s == 17)
		step_layer_fill();
	else if (s == 18)
		step_layer_refill();
	else if (s == 19)
		step_layer_opacity();
	else if (s == 20)
		step_paint_pixels();
	else if (s == 21)
		step_paint_self();
	else if (s == 22)
		step_shapes();
	else if (s == 23)
		step_objects();
	else if (s == 24)
		step_mesh_modifiers();
	else if (s == 25)
		step_mesh_nodes();
	else if (s == 26)
		step_mesh_script();
	else if (s == 27)
		step_geom();
	else if (s == 28)
		step_stages_timeline();
	else if (s == 29)
		step_envmap();
	else if (s == 30)
		step_bake_lightmap();
	else if (s == 31)
		step_bake_cancel();
	else if (s == 32)
		step_viewport_modes();
	else if (s == 33)
		step_screenshot();
	else if (s == 34)
		step_export_textures();
	else if (s == 35)
		step_export_mesh();
	else if (s == 36)
		step_save();
	else if (s == 37)
		step_verify_save();
	else if (s == 38)
		step_new_project();
	else if (s == 39)
		step_load();
	else if (s == 40)
		step_verify_load();
	else if (s == 41)
		step_import_texture();
	else if (s == 42)
		step_import_mesh();
	else if (s == 43)
		step_import_material();
	else if (s == 44)
		step_repaint_and_resave();
	else if (s == 45)
		step_sculpt_setup();
	else if (s == 46)
		step_sculpt_layer();
	else if (s == 47)
		step_sculpt_draw();
	else if (s == 48)
		step_sculpt_modes();
	else if (s == 49)
		step_sculpt_blur_eraser();
	else if (s == 50)
		step_sculpt_save();
	else if (s == 51)
		step_sculpt_new();
	else if (s == 52)
		step_sculpt_load();
	else if (s == 53)
		step_sculpt_verify();
	else if (s == 54)
		step_sculpt_end();
	else if (s == 55)
		step_blend_colors();
	else if (s == 56)
		step_brush_opacity();
	else if (s == 57)
		step_material_channels();
	else if (s == 58)
		step_custom_node();
	else if (s == 59)
		step_viewport_shader();
	else if (s == 60)
		step_draw_target();
	else if (s == 61)
		step_packed_assets();
	else if (s == 62)
		step_custom_importers();
	else if (s == 63)
		step_font_sound();
	else if (s == 64)
		step_mesh_geo();
	else if (s == 65)
		step_geom_more();
	else if (s == 66)
		step_skin();
	else if (s == 67)
		step_timer();
	else if (s == 68)
		step_object_tools();
	else if (s == 69)
		step_pick();
	else if (s == 70)
		step_duplicate_merge();
	else if (s == 71)
		step_message_fade();
	else if (s == 72)
		step_layer_duplicate();
	else if (s == 73)
		step_layer_merge_down();
	else if (s == 74)
		step_layer_mask_ops();
	else if (s == 75)
		step_layer_fill_ops();
	else if (s == 76)
		step_layer_merge_group();
	else if (s == 77)
		step_layer_path();
	else if (s == 78)
		step_layer_curve_text();
	else if (s == 79)
		step_layer_paths_save();
	else if (s == 80)
		step_layer_paths_load();
	else if (s == 81)
		step_layer_paths_verify();
}

int step_count      = 82;
int expected_checks = 223;

void next_frame() {
	if (sub == 0) {
		printf("[test] step %d of %d", step + 1, step_count);
	}
	step_again = 0;
	run_step(step);

	if (step_again) {
		sub += 1;
	}
	else {
		sub = 0;
		step += 1;
	}
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
	dir_textures    = string("%s/textures", tmp_root);
	dir_project     = string("%s/project", tmp_root);
	dir_mesh        = string("%s/mesh", tmp_root);
	dir_material    = string("%s/material", tmp_root);
	dir_checks      = string("%s/checks", tmp_root);
	file_project    = string("%s/test_project.arm", dir_project);
	file_screenshot = string("%s/test_screenshot.png", tmp_root);
	file_envmap     = string("%s/test_envmap.hdr", tmp_root);
	file_geom       = string("%s/test_geom.obj", dir_mesh);
	file_sculpt     = string("%s/test_sculpt.arm", dir_project);
	dir_lightmap    = string("%s/lightmap", tmp_root);
	file_sculpt_obj = string("%s/test_sculpt", dir_mesh);
	file_draw_png   = string("%s/test_draw.png", tmp_root);
	file_draw_jpg   = string("%s/test_draw.jpg", tmp_root);
	file_packed     = string("%s/test_packed.png", dir_checks);
	dir_custom      = string("%s/custom", tmp_root);
	file_font       = string("%s/test_font.ttf", tmp_root);
	file_sound      = string("%s/test_sound.wav", tmp_root);
	file_atlas      = string("%s/test_atlas.png", tmp_root);
	file_skin       = string("%s/test_skin.skin", dir_mesh);
	file_paths      = string("%s/test_paths.arm", dir_project);

	iron_create_directory(tmp_root);
	iron_create_directory(dir_textures);
	iron_create_directory(dir_project);
	iron_create_directory(dir_mesh);
	iron_create_directory(dir_material);
	iron_create_directory(dir_checks);
	iron_create_directory(dir_lightmap);
	iron_create_directory(dir_custom);

	printf("[test] output directory %s", tmp_root);
	script_notify_on_next_frame(next_frame);
}
