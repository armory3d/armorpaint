#include "global.h"

void *plugin;
bool  expanded = true;
bool  dirty    = false;

int   preset          = 0;
float light_azimuth   = 45.0;
float light_elevation = 40.0;
bool  follow_camera   = false;
float light_intensity = 1.05;

float cel_bands        = 3.0;
float shadow_threshold = 0.45;
float band_softness    = 0.05;
float shadow_depth     = 0.72;
float ao_influence     = 0.60;

float spec_strength = 0.50;
float spec_size     = 0.35;
float rim_strength  = 0.50;
float rim_size      = 0.40;
float outline_width = 0.0;

int light_color;
int shadow_color;
int rim_color;
int outline_color;

bool light_color_open   = false;
bool shadow_color_open  = false;
bool rim_color_open     = false;
bool outline_color_open = false;

ui_color_state_t light_color_state;
ui_color_state_t shadow_color_state;
ui_color_state_t rim_color_state;
ui_color_state_t outline_color_state;

f32_array_t    *color_row    = NULL;
string_array_t *preset_names = NULL;

float srgb_to_lin(int channel) {
	float c = channel / 255.0;
	return pow(c, 2.2);
}

void apply_preset(int p) {
	preset = p;
	if (p == 0) { // Anime
		light_azimuth    = 45.0;
		light_elevation  = 40.0;
		light_intensity  = 1.05;
		cel_bands        = 3.0;
		shadow_threshold = 0.45;
		band_softness    = 0.05;
		shadow_depth     = 0.72;
		ao_influence     = 0.60;
		spec_strength    = 0.50;
		spec_size        = 0.35;
		rim_strength     = 0.50;
		rim_size         = 0.40;
		outline_width    = 0.0;
		light_color      = ui_color(255, 250, 242, 255);
		shadow_color     = ui_color(78, 88, 138, 255);
		rim_color        = ui_color(224, 238, 255, 255);
		outline_color    = ui_color(28, 24, 34, 255);
	}
	else if (p == 1) { // Soft Toon
		light_azimuth    = 35.0;
		light_elevation  = 45.0;
		light_intensity  = 1.05;
		cel_bands        = 3.0;
		shadow_threshold = 0.40;
		band_softness    = 0.28;
		shadow_depth     = 0.58;
		ao_influence     = 0.50;
		spec_strength    = 0.35;
		spec_size        = 0.42;
		rim_strength     = 0.35;
		rim_size         = 0.45;
		outline_width    = 0.0;
		light_color      = ui_color(255, 248, 236, 255);
		shadow_color     = ui_color(98, 106, 148, 255);
		rim_color        = ui_color(255, 244, 224, 255);
		outline_color    = ui_color(38, 32, 42, 255);
	}
	else if (p == 2) { // Comic
		light_azimuth    = 55.0;
		light_elevation  = 35.0;
		light_intensity  = 1.10;
		cel_bands        = 2.0;
		shadow_threshold = 0.50;
		band_softness    = 0.01;
		shadow_depth     = 0.85;
		ao_influence     = 0.75;
		spec_strength    = 0.65;
		spec_size        = 0.25;
		rim_strength     = 0.35;
		rim_size         = 0.30;
		outline_width    = 0.22;
		light_color      = ui_color(255, 252, 245, 255);
		shadow_color     = ui_color(44, 50, 82, 255);
		rim_color        = ui_color(255, 255, 255, 255);
		outline_color    = ui_color(18, 16, 24, 255);
	}
	else if (p == 3) { // Rim Lit
		light_azimuth    = 135.0;
		light_elevation  = 30.0;
		light_intensity  = 1.15;
		cel_bands        = 3.0;
		shadow_threshold = 0.48;
		band_softness    = 0.06;
		shadow_depth     = 0.80;
		ao_influence     = 0.65;
		spec_strength    = 0.60;
		spec_size        = 0.30;
		rim_strength     = 1.10;
		rim_size         = 0.50;
		outline_width    = 0.0;
		light_color      = ui_color(255, 228, 196, 255);
		shadow_color     = ui_color(52, 64, 108, 255);
		rim_color        = ui_color(150, 210, 255, 255);
		outline_color    = ui_color(20, 22, 34, 255);
	}
	dirty = true;
}

// Register custom viewport shader
void viewport_shader(void *shader) {
	float pi = 3.14159265;
	float az = light_azimuth * (pi / 180.0);
	float el = light_elevation * (pi / 180.0);
	float lx = cos(el) * sin(az);
	float ly = -cos(el) * cos(az);
	float lz = sin(el);

	float lr = srgb_to_lin(ui_color_r(light_color));
	float lg = srgb_to_lin(ui_color_g(light_color));
	float lb = srgb_to_lin(ui_color_b(light_color));

	float sr = srgb_to_lin(ui_color_r(shadow_color));
	float sg = srgb_to_lin(ui_color_g(shadow_color));
	float sb = srgb_to_lin(ui_color_b(shadow_color));

	float rr = srgb_to_lin(ui_color_r(rim_color));
	float rg = srgb_to_lin(ui_color_g(rim_color));
	float rb = srgb_to_lin(ui_color_b(rim_color));

	float or_ = srgb_to_lin(ui_color_r(outline_color));
	float og  = srgb_to_lin(ui_color_g(outline_color));
	float ob  = srgb_to_lin(ui_color_b(outline_color));

	float steps = floor(cel_bands + 0.5) - 1.0;
	if (steps < 1.0) {
		steps = 1.0;
	}

	// Light direction (world-space or camera-relative)
	if (follow_camera) {
		node_shader_write_frag(shader, "float3 cel_up_ref = float3(0.0, 0.0, 1.0);");
		node_shader_write_frag(shader, "if (abs(vvec.z) > 0.99) { cel_up_ref = float3(0.0, 1.0, 0.0); }");
		node_shader_write_frag(shader, "float3 cel_cam_r = normalize(cross(cel_up_ref, vvec));");
		node_shader_write_frag(shader, "float3 cel_cam_u = normalize(cross(vvec, cel_cam_r));");
		node_shader_write_frag(shader, string("float3 cel_ldir = normalize(cel_cam_r * (%.4f) + vvec * (%.4f) + cel_cam_u * (%.4f));", lx, -ly, lz));
	}
	else {
		node_shader_write_frag(shader, string("float3 cel_ldir = normalize(float3(%.4f, %.4f, %.4f));", lx, ly, lz));
	}

	// Linear base albedo, normal/view terms, and AO-modulated luminance
	node_shader_write_frag(shader, "float3 cel_albedo = pow(max(basecol, float3(0.0, 0.0, 0.0)), float3(2.2, 2.2, 2.2));");
	node_shader_write_frag(shader, "float cel_ndl = dot(n, cel_ldir);");
	node_shader_write_frag(shader, "float cel_ndv = max(dot(n, vvec), 0.0);");
	node_shader_write_frag(shader, string("float cel_ao = lerp(1.0, occlusion, %.4f);", ao_influence * 0.65));
	node_shader_write_frag(shader, string("float cel_lum = clamp((cel_ndl * 0.5 + 0.5) - (1.0 - occlusion) * (%.4f), 0.0, 1.0);", ao_influence * 0.55));

	// Anti-aliased multi-band toon diffuse ramp
	node_shader_write_frag(shader, string("float cel_steps = %.4f;", steps));
	node_shader_write_frag(shader, string("float cel_thresh = %.4f;", shadow_threshold));
	node_shader_write_frag(shader, "float cel_span = max(1.0 - cel_thresh, 0.05) / cel_steps;");
	node_shader_write_frag(shader, "float cel_s = (cel_lum - cel_thresh) / cel_span;");
	node_shader_write_frag(shader, "float cel_sc = clamp(cel_s, -0.5, cel_steps - 0.5);");
	node_shader_write_frag(shader, "float cel_cell = floor(cel_sc + 0.5);");
	node_shader_write_frag(shader, "float cel_f = cel_sc - cel_cell;");
	node_shader_write_frag(shader, "float cel_aa = (abs(ddx(cel_s)) + abs(ddy(cel_s))) * 0.75;");
	node_shader_write_frag(shader, string("float cel_w = clamp(max(%.4f, cel_aa), 0.001, 0.5);", band_softness * 0.5));
	node_shader_write_frag(shader, "float cel_band = (cel_cell + smoothstep(-cel_w, cel_w, cel_f)) / cel_steps;");

	// Colored light + hemisphere-shaded chromatic shadow + subtle self-saturated terminator bounce
	node_shader_write_frag(shader, string("float3 cel_light_col = float3(%.4f, %.4f, %.4f) * (%.4f);", lr, lg, lb, light_intensity));
	node_shader_write_frag(shader, "float cel_hemi = 0.85 + 0.15 * (n.z * 0.5 + 0.5);");
	node_shader_write_frag(shader, string("float3 cel_shadow_col = float3(%.4f, %.4f, %.4f) * cel_hemi;", sr, sg, sb));
	node_shader_write_frag(shader, string("float3 cel_shad_lit = lerp(cel_light_col, cel_shadow_col, %.4f);", shadow_depth));
	node_shader_write_frag(shader, "float3 cel_lit = lerp(cel_shad_lit, cel_light_col, cel_band);");
	node_shader_write_frag(shader, string("float cel_term = exp(-cel_s * cel_s * 18.0) * (%.4f) * (1.0 - metallic);", shadow_depth * 0.14));
	node_shader_write_frag(shader, "float3 cel_diffuse = cel_albedo * cel_lit * lerp(1.0, 0.35, metallic) * cel_ao;");
	node_shader_write_frag(shader, "cel_diffuse = cel_diffuse + cel_albedo * cel_albedo * cel_light_col * cel_term * cel_ao;");

	// Crisp PBR-aware toon specular highlight
	node_shader_write_frag(shader, "float3 cel_h = normalize(cel_ldir + vvec);");
	node_shader_write_frag(shader, "float cel_ndh = max(dot(n, cel_h), 0.0);");
	node_shader_write_frag(shader, "float cel_gloss = clamp(1.0 - roughness, 0.02, 1.0);");
	node_shader_write_frag(shader, "float cel_spec_pow = lerp(6.0, 96.0, cel_gloss * cel_gloss);");
	node_shader_write_frag(shader, "float cel_spec_val = pow(max(cel_ndh, 0.0001), cel_spec_pow);");
	node_shader_write_frag(shader, string("float cel_spec_th = clamp(1.0 - (%.4f), 0.02, 0.98);", spec_size));
	node_shader_write_frag(shader, "float cel_spec_aa = (abs(ddx(cel_spec_val)) + abs(ddy(cel_spec_val))) * 0.75;");
	node_shader_write_frag(shader, string("float cel_spec_w = clamp(max(%.4f + roughness * 0.15, cel_spec_aa), 0.002, 0.45);", band_softness * 0.25));
	node_shader_write_frag(shader, "float cel_spec_mask = smoothstep(cel_spec_th - cel_spec_w, cel_spec_th + cel_spec_w, cel_spec_val);");
	node_shader_write_frag(shader, "cel_spec_mask = cel_spec_mask * smoothstep(-0.05, 0.15, cel_ndl) * cel_ao;");
	node_shader_write_frag(shader, "float3 cel_spec_col = lerp(cel_light_col, cel_albedo * cel_light_col * 1.4, metallic);");
	node_shader_write_frag(shader, string("float3 cel_spec = cel_spec_col * cel_spec_mask * (%.4f) * (cel_gloss * cel_gloss) * lerp(1.0, 1.6, metallic);",
	                                      spec_strength));

	// Directional Fresnel rim light
	node_shader_write_frag(shader, "float cel_fresnel = 1.0 - cel_ndv;");
	node_shader_write_frag(shader, "float cel_rim_dir = lerp(1.0, smoothstep(-0.3, 0.5, cel_ndl), 0.75);");
	node_shader_write_frag(shader, "float cel_rim_val = cel_fresnel * cel_rim_dir;");
	node_shader_write_frag(shader, string("float cel_rim_th = clamp(1.0 - (%.4f), 0.05, 0.98);", rim_size));
	node_shader_write_frag(shader, "float cel_rim_aa = (abs(ddx(cel_rim_val)) + abs(ddy(cel_rim_val))) * 0.75;");
	node_shader_write_frag(shader, string("float cel_rim_w = clamp(max(%.4f, cel_rim_aa), 0.002, 0.45);", band_softness * 0.25));
	node_shader_write_frag(shader, "float cel_rim_mask = smoothstep(cel_rim_th - cel_rim_w, cel_rim_th + cel_rim_w, cel_rim_val) * cel_ao;");
	node_shader_write_frag(shader,
	                       string("float3 cel_rim_col = float3(%.4f, %.4f, %.4f) * lerp(float3(1.0, 1.0, 1.0), cel_albedo, metallic * 0.5);", rr, rg, rb));
	node_shader_write_frag(shader, string("float3 cel_rim = cel_rim_col * (cel_rim_mask * (%.4f));", rim_strength));

	// Combine lighting and preserve emissive materials
	node_shader_write_frag(shader, "float3 cel_col = cel_diffuse + cel_spec + cel_rim;");
	node_shader_write_frag(shader, "if (float(int(matid * 255.0 + 0.5)) % 3.0 == 1.0) { cel_col = cel_albedo * 1.25; }");

	// Optional anti-aliased silhouette outline
	if (outline_width > 0.001) {
		node_shader_write_frag(shader, "float cel_geom_ndv = max(dot(normalize(input.wnormal), vvec), 0.0);");
		node_shader_write_frag(shader, string("float cel_out_th = %.4f;", outline_width * 0.5));
		node_shader_write_frag(shader, "float cel_out_aa = max(abs(ddx(cel_geom_ndv)) + abs(ddy(cel_geom_ndv)), 0.005);");
		node_shader_write_frag(shader, "float cel_out_mask = 1.0 - smoothstep(cel_out_th - cel_out_aa, cel_out_th + cel_out_aa, cel_geom_ndv);");
		node_shader_write_frag(shader, string("cel_col = lerp(cel_col, float3(%.4f, %.4f, %.4f), cel_out_mask);", or_, og, ob));
	}

	// Soft highlight shoulder above 0.9 and linear -> sRGB output
	node_shader_write_frag(shader, "float cel_maxc = max(cel_col.r, max(cel_col.g, cel_col.b));");
	node_shader_write_frag(shader, "if (cel_maxc > 0.9) {");
	node_shader_write_frag(shader, "float cel_mapped = 0.9 + 0.1 * ((cel_maxc - 0.9) / (cel_maxc - 0.9 + 0.35));");
	node_shader_write_frag(shader, "cel_col = cel_col * (cel_mapped / cel_maxc);");
	node_shader_write_frag(shader, "}");
	node_shader_write_frag(shader, "cel_col = clamp(cel_col, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));");
	node_shader_write_frag(shader, "output_color = pow(cel_col, float3(0.454545, 0.454545, 0.454545));");
}

bool draw_color_setting(bool *open, char *label, int *color, ui_color_state_t *state) {
	ui_row(color_row);
	ui_panel(open, label, true, false, false);
	if (ui_text("", UI_ALIGN_LEFT, *color) == UI_STATE_RELEASED) {
		*open = !(*open);
	}
	if (*open) {
		ui_color_wheel(color, state, false, -1, -1, false, NULL, NULL);
		return ui_item_changed();
	}
	return false;
}

void on_ui() {
	if (!ui_panel(&expanded, "Cel Shade", false, true, false)) {
		return;
	}

	if (script_get_context()->viewport_mode != 17) { // VIEWPORT_MODE_PLUGIN
		if (ui_button("Enable Cel Shade", UI_ALIGN_CENTER, "")) {
			dirty = true;
		}
	}

	ui_combo(&preset, preset_names, "Preset", true, UI_ALIGN_LEFT, false);
	if (ui_item_changed() && preset < 4) {
		apply_preset(preset);
	}

	bool changed = false;

	ui_row2();
	ui_slider(&light_azimuth, "Azimuth", -180.0, 180.0, true, 1.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();
	ui_slider(&light_elevation, "Elevation", -90.0, 90.0, true, 1.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();

	ui_row2();
	ui_slider(&light_intensity, "Intensity", 0.0, 2.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();
	ui_check(&follow_camera, "Follow Camera", "");
	changed = changed || ui_item_changed();

	ui_row2();
	ui_slider(&cel_bands, "Bands", 2.0, 6.0, true, 1.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();
	ui_slider(&shadow_threshold, "Threshold", 0.0, 1.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();

	ui_row2();
	ui_slider(&band_softness, "Softness", 0.0, 1.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();
	ui_slider(&shadow_depth, "Shadow", 0.0, 1.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();

	ui_slider(&ao_influence, "AO Influence", 0.0, 1.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();

	ui_row2();
	ui_slider(&spec_strength, "Specular", 0.0, 2.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();
	ui_slider(&spec_size, "Spec Size", 0.05, 1.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();

	ui_row2();
	ui_slider(&rim_strength, "Rim", 0.0, 2.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();
	ui_slider(&rim_size, "Rim Size", 0.05, 1.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();

	ui_slider(&outline_width, "Outline", 0.0, 1.0, true, 100.0, true, UI_ALIGN_LEFT, true);
	changed = changed || ui_item_changed();

	if (draw_color_setting(&light_color_open, "Light Color", &light_color, &light_color_state)) {
		changed = true;
	}
	if (draw_color_setting(&shadow_color_open, "Shadow Color", &shadow_color, &shadow_color_state)) {
		changed = true;
	}
	if (draw_color_setting(&rim_color_open, "Rim Color", &rim_color, &rim_color_state)) {
		changed = true;
	}
	if (outline_width > 0.001) {
		if (draw_color_setting(&outline_color_open, "Outline Color", &outline_color, &outline_color_state)) {
			changed = true;
		}
	}

	if (changed) {
		preset = 4; // Custom
		dirty  = true;
	}

	if (ui_button("Reset", UI_ALIGN_CENTER, "")) {
		follow_camera = false;
		apply_preset(0);
	}
}

void on_update() {
	if (dirty) {
		dirty = false;
		context_set_viewport_shader(viewport_shader);
	}
}

void on_delete() {
	context_set_viewport_shader(NULL);
	if (color_row != NULL) {
		array_free(color_row);
		free(color_row);
		color_row = NULL;
	}
	if (preset_names != NULL) {
		array_free(preset_names);
		free(preset_names);
		preset_names = NULL;
	}
}

void main() {
	plugin = plugin_create();

	color_row            = f32_array_create(2);
	color_row->buffer[0] = 0.78;
	color_row->buffer[1] = 0.22;

	preset_names            = string_array_create(5);
	preset_names->buffer[0] = "Anime";
	preset_names->buffer[1] = "Soft Toon";
	preset_names->buffer[2] = "Comic";
	preset_names->buffer[3] = "Rim Lit";
	preset_names->buffer[4] = "Custom";

	apply_preset(0);
	dirty = false;

	context_set_viewport_shader(viewport_shader);
	plugin_notify_on_ui(plugin, on_ui);
	plugin_notify_on_update(plugin, on_update);
	plugin_notify_on_delete(plugin, on_delete);
}
