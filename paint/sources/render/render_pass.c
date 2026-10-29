#include "../global.h"

// Fullscreen passes created by scripts. The frag code writes float4 output, blended over the target by its alpha.
// It reads tex (screen uv), depth, wposition (world position from depth) and constants.<name>: eye, time, or a float4
// set with render_pass_set_vec4.

static any_array_t *render_pass_list   = NULL;
static any_map_t   *render_pass_values = NULL; // Constant link -> vec4_t *
static i32          render_pass_ids    = 0;

static char *render_pass_builtins[][3] = {
    {"invVP", "float4x4", "_inv_view_proj_matrix"},  {"eye", "float3", "_camera_pos"}, {"eye_look", "float3", "_camera_look"},
    {"camera_proj", "float2", "_camera_plane_proj"}, {"time", "float", "_time"},
};

static i32 render_pass_builtin(char *name) {
	for (i32 i = 0; i < sizeof(render_pass_builtins) / sizeof(render_pass_builtins[0]); ++i) {
		if (string_equals(render_pass_builtins[i][0], name)) {
			return i;
		}
	}
	return -1;
}

static void render_pass_add_const(shader_context_t *scon, char *name, char *type, char *link) {
	shader_const_t *c = ALLOC_INIT(shader_const_t, {.name = string_copy(name), .type = type, .link = string_copy(link)});
	any_array_push(scon->constants, c);
}

render_pass_t *render_pass_create(char *frag, render_pass_stage_t stage) {
	render_pass_t *p = ALLOC_INIT(render_pass_t, {.stage = stage, .id = render_pass_ids++});

	shader_context_t *scon =
	    ALLOC_INIT(shader_context_t,
	               {
	                   .name                    = "pass",
	                   .depth_write             = false,
	                   .compare_mode            = "always",
	                   .cull_mode               = "none",
	                   .shader_from_source      = true,
	                   .blend_source            = "source_alpha",
	                   .blend_destination       = "inverse_source_alpha",
	                   .alpha_blend_source      = "blend_zero",
	                   .alpha_blend_destination = "blend_one",
	                   .color_attachments       = any_array_create_from_raw((void *[]){"RGBA64"}, 1),
	                   .depth_attachment        = "NONE",
	                   .vertex_elements         = any_array_create_from_raw((void *[]){ALLOC_INIT(vertex_element_t, {.name = "pos", .data = "float2"})}, 1),
	                   .constants               = any_array_create_from_raw((void *[]){}, 0),
	                   .texture_units           = any_array_create_from_raw((void *[]){ALLOC_INIT(tex_unit_t, {.name = "gbufferD"})}, 1),
	               });

	// The wrapper reads these, the rest only when the code does
	for (i32 i = 0; i < 4; ++i) {
		render_pass_add_const(scon, render_pass_builtins[i][0], render_pass_builtins[i][1], render_pass_builtins[i][2]);
	}
	char *consts = "\tfloat4x4 invVP;\n\tfloat3 eye;\n\tfloat3 eye_look;\n\tfloat2 camera_proj;\n";
	char *s      = frag;
	while ((s = strstr(s, "constants.")) != NULL) {
		s += 10;
		i32 len = 0;
		while (isalnum(s[len]) || s[len] == '_') {
			len++;
		}
		char *name  = substring(s, 0, len);
		bool  found = false;
		for (i32 i = 0; i < scon->constants->length; ++i) {
			found = found || string_equals(((shader_const_t *)scon->constants->buffer[i])->name, name);
		}
		if (len == 0 || found) {
			continue;
		}
		i32 b = render_pass_builtin(name);
		if (b >= 0) {
			render_pass_add_const(scon, name, render_pass_builtins[b][1], render_pass_builtins[b][2]);
		}
		else {
			render_pass_add_const(scon, name, "float4", string_tmp("_pass%d_%s", p->id, name));
		}
		consts = string("%s\t%s %s;\n", consts, b >= 0 ? render_pass_builtins[b][1] : "float4", name);
	}

	char *kong = string("cbuffer constants {\n%s};\n\
sampler sampler_linear;\n\
tex2d gbufferD;\n\
struct vert_in { float2 pos; };\n\
struct vert_out { float4 pos; float2 tex; float3 view_ray; };\n\
vert_out vert(vert_in input) {\n\
	vert_out output;\n\
	output.tex = input.pos.xy * 0.5 + 0.5;\n\
	output.tex.y = 1.0 - output.tex.y;\n\
	output.pos = float4(input.pos.xy, 0.0, 1.0);\n\
	float4 v = constants.invVP * float4(input.pos.xy, 1.0, 1.0);\n\
	output.view_ray = v.xyz / v.w - constants.eye;\n\
	return output;\n\
}\n\
float4 frag(vert_out input) {\n\
	float2 tex = input.tex;\n\
	float depth = sample_lod(gbufferD, sampler_linear, tex, 0.0).r;\n\
	float3 view_ray = normalize(input.view_ray);\n\
	float linear_depth = constants.camera_proj.y / (depth - constants.camera_proj.x);\n\
	float3 wposition = constants.eye + view_ray * (linear_depth / dot(constants.eye_look, view_ray));\n\
	float4 output = float4(0.0, 0.0, 0.0, 0.0);\n\
%s\n\
	return output;\n\
}\n",
	                    consts, frag);

	scon->_ = calloc(1, sizeof(shader_context_runtime_t));
	gpu_create_shaders_from_kong(kong, &scon->vertex_shader, &scon->fragment_shader, &scon->_->vertex_shader_size, &scon->_->fragment_shader_size);
	shader_context_load(scon);

	p->scon                     = scon;
	p->handle                   = string("_pass%d", p->id);
	cached_shader_context_t *cc = ALLOC_INIT(cached_shader_context_t, {.context = scon});
	if (_render_path_cached_shader_contexts == NULL) {
		_render_path_cached_shader_contexts = any_map_create();
	}
	any_map_set(_render_path_cached_shader_contexts, p->handle, cc);

	if (render_pass_list == NULL) {
		render_pass_list   = any_array_create_from_raw((void *[]){}, 0);
		render_pass_values = any_map_create();
	}
	any_array_push(render_pass_list, p);
	g_context->ddirty = 2;
	return p;
}

void render_pass_set_vec4(render_pass_t *p, char *name, f32 x, f32 y, f32 z, f32 w) {
	char   *link = string_tmp("_pass%d_%s", p->id, name);
	vec4_t *v    = any_map_get(render_pass_values, link);
	if (v == NULL) {
		v = ALLOC_INIT(vec4_t, {0});
		any_map_set(render_pass_values, string_copy(link), v);
	}
	if (v->x != x || v->y != y || v->z != z || v->w != w) {
		*v                = (vec4_t){x, y, z, w};
		g_context->ddirty = 2;
	}
}

// NAN when the link is not a pass constant
vec4_t render_pass_get_vec4(char *link) {
	if (render_pass_values == NULL || !starts_with(link, "_pass")) {
		return vec4_nan();
	}
	vec4_t *v = any_map_get(render_pass_values, link);
	return v != NULL ? *v : (vec4_t){0.0, 0.0, 0.0, 0.0};
}

void render_pass_remove(render_pass_t *p) {
	array_remove(render_pass_list, p);
	g_context->ddirty = 2;
}

void render_pass_remove_all() {
	if (render_pass_list != NULL) {
		render_pass_list->length = 0;
	}
}

void render_pass_draw(render_pass_stage_t stage, char *target) {
	if (render_pass_list == NULL) {
		return;
	}
	for (i32 i = 0; i < render_pass_list->length; ++i) {
		render_pass_t *p = render_pass_list->buffer[i];
		if (p->stage != stage) {
			continue;
		}
		render_path_set_target(target, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target("main", "gbufferD");
		render_path_draw_shader(p->handle);
	}
}
