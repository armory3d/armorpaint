#include "kong.h"
#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct small_string {
	char str[64];
} small_string;

static type_id     vertex_inputs[256];
static size_t      vertex_inputs_size = 0;
static size_t      vertex_location_offsets[256];
static type_id     fragment_inputs[256];
static size_t      fragment_inputs_size = 0;
static function_id vertex_functions[256];
static size_t      vertex_functions_size = 0;
static function_id fragment_functions[256];
static size_t      fragment_functions_size = 0;
static function_id compute_functions[256];
static size_t      compute_functions_size = 0;

static char *type_string(type_id type) {
	if (type == float_id) {
		return "f32";
	}
	if (type == float2_id) {
		return "vec2<f32>";
	}
	if (type == float3_id) {
		return "vec3<f32>";
	}
	if (type == float4_id) {
		return "vec4<f32>";
	}
	if (type == int_id) {
		return "i32";
	}
	if (type == int2_id) {
		return "vec2<i32>";
	}
	if (type == int3_id) {
		return "vec3<i32>";
	}
	if (type == int4_id) {
		return "vec4<i32>";
	}
	if (type == uint_id) {
		return "u32";
	}
	if (type == uint2_id) {
		return "vec2<u32>";
	}
	if (type == uint3_id) {
		return "vec3<u32>";
	}
	if (type == uint4_id) {
		return "vec4<u32>";
	}
	if (type == bool_id) {
		return "bool";
	}
	if (type == bool2_id) {
		return "vec2<bool>";
	}
	if (type == bool3_id) {
		return "vec3<bool>";
	}
	if (type == bool4_id) {
		return "vec4<bool>";
	}
	if (type == float2x2_id) {
		return "mat2x2<f32>";
	}
	if (type == float2x3_id) {
		return "mat2x3<f32>";
	}
	if (type == float3x2_id) {
		return "mat3x2<f32>";
	}
	if (type == float3x3_id) {
		return "mat3x3<f32>";
	}
	if (type == float4x3_id) {
		return "mat4x3<f32>";
	}
	if (type == float3x4_id) {
		return "mat3x4<f32>";
	}
	if (type == float4x4_id) {
		return "mat4x4<f32>";
	}
	return get_name(get_type(type)->name);
}

static void write_code(char *wgsl, char *directory, const char *filename, const char *name, bool framebuffer_texture_format) {
	char full_filename[512];

	{
		sprintf(full_filename, "%s/%s.h", directory, filename);
		FILE *file = fopen(full_filename, "wb");
		fprintf(file, "#include <stdbool.h>\n");
		fprintf(file, "#include <stddef.h>\n\n");
		fprintf(file, "extern const char *%s;\n", name);
		fprintf(file, "extern size_t %s_size;\n", name);
		fprintf(file, "extern bool %s_uses_framebuffer_texture_format;\n", name);
		fclose(file);
	}

	{
		sprintf(full_filename, "%s/%s.c", directory, filename);

		FILE *file = fopen(full_filename, "wb");
		fprintf(file, "#include \"%s.h\"\n\n", filename);

		fprintf(file, "const char *%s = \"", name);

		size_t length = strlen(wgsl);

		for (size_t i = 0; i < length; ++i) {
			if (wgsl[i] == '\n') {
				fprintf(file, "\\n");
			}
			else if (wgsl[i] == '\r') {
				fprintf(file, "\\r");
			}
			else if (wgsl[i] == '\t') {
				fprintf(file, "\\t");
			}
			else if (wgsl[i] == '"') {
				fprintf(file, "\\\"");
			}
			else {
				fprintf(file, "%c", wgsl[i]);
			}
		}

		fprintf(file, "\";\n\n");

		fprintf(file, "size_t %s_size = %zu;\n\n", name, length);

		fprintf(file, "bool %s_uses_framebuffer_texture_format = %s;\n\n", name, framebuffer_texture_format ? "true" : "false");

		fprintf(file, "/*\n%s*/\n", wgsl);

		fclose(file);
	}
}

static bool is_vertex_input(type_id t) {
	for (size_t i = 0; i < vertex_inputs_size; ++i) {
		if (t == vertex_inputs[i]) {
			return true;
		}
	}
	return false;
}

static size_t find_vertex_location_offset(type_id t) {
	for (size_t i = 0; i < vertex_inputs_size; ++i) {
		if (t == vertex_inputs[i]) {
			return vertex_location_offsets[i];
		}
	}
	return 0;
}

static bool is_fragment_input(type_id t) {
	for (size_t i = 0; i < fragment_inputs_size; ++i) {
		if (t == fragment_inputs[i]) {
			return true;
		}
	}
	return false;
}

static void add_found_type(type_id t, type_id *types, size_t *types_size) {
	for (size_t i = 0; i < *types_size; ++i) {
		if (types[i] == t) {
			return;
		}
	}

	types[*types_size] = t;
	*types_size += 1;
}

static void write_types(char *wgsl, size_t *offset, shader_stage stage, type_id inputs[64], size_t inputs_count, type_id output, function *main) {
	type_id types[256];
	size_t  types_size = 0;
	find_referenced_types(main, types, &types_size);

	global_array globals = {0};

	find_referenced_globals(main, &globals);

	for (size_t i = 0; i < globals.size; ++i) {
		global *g = get_global(globals.globals[i]);

		if (g->type == sampler_type_id) {
		}
		else if (get_type(g->type)->tex_kind != TEXTURE_KIND_NONE) {
		}
		else if (g->type == float_id) {
		}
		else {
			add_found_type(g->type, types, &types_size);
		}
	}

	for (size_t i = 0; i < types_size; ++i) {
		type *t = get_type(types[i]);

		if (!t->built_in && !has_attribute(&t->attributes, add_name("pipe"))) {
			if (t->name == NO_NAME) {
				char name[256];

				bool found = false;
				for (global_id j = 0; get_global(j) != NULL && get_global(j)->type != NO_TYPE; ++j) {
					global *g = get_global(j);
					if (g->type == types[i]) {
						sprintf(name, "_%" PRIu64, g->var_index);
						found = true;
						break;
					}
				}

				if (!found) {
					strcpy(name, "Unknown");
				}

				*offset += sprintf(&wgsl[*offset], "struct %s_type {\n", name);
			}
			else {
				*offset += sprintf(&wgsl[*offset], "struct %s {\n", get_name(t->name));
			}

			if (is_vertex_input(types[i])) {
				size_t location = find_vertex_location_offset(types[i]);

				for (size_t j = 0; j < t->members.size; ++j) {
					*offset +=
					    sprintf(&wgsl[*offset], "\t@location(%zu) %s: %s,\n", location, get_name(t->members.m[j].name), type_string(t->members.m[j].type.type));
					++location;
				}
			}
			else if (is_fragment_input(types[i])) {
				for (size_t j = 0; j < t->members.size; ++j) {
					if (j == 0) {
						*offset +=
						    sprintf(&wgsl[*offset], "\t@builtin(position) %s: %s,\n", get_name(t->members.m[j].name), type_string(t->members.m[j].type.type));
					}
					else {
						*offset += sprintf(&wgsl[*offset], "\t@location(%zu) %s: %s,\n", j - 1, get_name(t->members.m[j].name),
						                   type_string(t->members.m[j].type.type));
					}
				}
			}
			else {
				for (size_t j = 0; j < t->members.size; ++j) {
					*offset += sprintf(&wgsl[*offset], "\t%s: %s,\n", get_name(t->members.m[j].name), type_string(t->members.m[j].type.type));
				}
			}
			*offset += sprintf(&wgsl[*offset], "};\n\n");
		}
	}
}

static void write_globals(char *wgsl, size_t *offset, function *main, bool *framebuffer_format_texture) {
	global_array referenced_globals = {0};

	find_referenced_globals(main, &referenced_globals);

	for (size_t i = 0; i < referenced_globals.size; ++i) {
		global *g         = get_global(referenced_globals.globals[i]);
		type   *t         = get_type(g->type);
		type_id base_type = t->array_size > 0 ? t->base : g->type;
		if (base_type == float_id) {
			*offset += sprintf(&wgsl[*offset], "const _%" PRIu64 ": f32 = %f;\n\n", g->var_index, g->value.value.floats[0]);
		}
	}

	descriptor_set_group *group = find_descriptor_set_group_for_function(main);

	if (group == NULL) {
		return;
	}

	for (size_t set_index = 0; set_index < group->size; ++set_index) {
		uint32_t binding = 0;

		descriptor_set *set = group->values[set_index];

		for (size_t g_index = 0; g_index < set->globals.size; ++g_index) {
			global_id global_index = set->globals.globals[g_index];
			bool      writable     = set->globals.writable[g_index];

			bool referenced = false;

			for (size_t i = 0; i < referenced_globals.size; ++i) {
				if (global_index == referenced_globals.globals[i]) {
					referenced = true;
					break;
				}
			}

			global *g = get_global(global_index);

			type   *t         = get_type(g->type);
			type_id base_type = t->array_size > 0 ? t->base : g->type;

			if (base_type == sampler_type_id) {
				if (referenced) {
					*offset +=
					    sprintf(&wgsl[*offset], "@group(%zu) @binding(%u) var _set%zu_%" PRIu64 ": sampler;\n\n", set_index, binding, set_index, g->var_index);
				}
				binding += 1;
			}
			else if (get_type(base_type)->tex_kind != TEXTURE_KIND_NONE) {
				if (referenced) {
					*offset += sprintf(&wgsl[*offset], "@group(%zu) @binding(%u) var _set%zu_%" PRIu64 ": texture_2d<f32>;\n\n", set_index, binding, set_index,
					                   g->var_index);
				}
				binding += 1;
			}
			else if (base_type == bvh_type_id) {
				assert(false);
				binding += 1;
			}
			else if (get_type(g->type)->built_in) {
				if (get_type(g->type)->array_size > 0) {
					assert(false);
					binding += 1;
				}
			}
			else {
				if (get_type(g->type)->array_size > 0) {
					assert(false);
					binding += 1;
				}
				else {
					if (referenced) {
						type *t = get_type(g->type);
						char  type_name[256];
						if (t->name != NO_NAME) {
							strcpy(type_name, get_name(t->name));
						}
						else {
							sprintf(type_name, "_%" PRIu64 "_type", g->var_index);
						}
						*offset += sprintf(&wgsl[*offset], "@group(%zu) @binding(%u) var<uniform> _set%zu_%" PRIu64 ": %s;\n\n", set_index, binding, set_index,
						                   g->var_index, type_name);
					}

					binding += 1;
				}
			}
		}
	}
}

static bool is_vertex_function(function_id f) {
	for (size_t i = 0; i < vertex_functions_size; ++i) {
		if (f == vertex_functions[i]) {
			return true;
		}
	}
	return false;
}

static bool is_fragment_function(function_id f) {
	for (size_t i = 0; i < fragment_functions_size; ++i) {
		if (f == fragment_functions[i]) {
			return true;
		}
	}
	return false;
}

static bool is_compute_function(function_id f) {
	for (size_t i = 0; i < compute_functions_size; ++i) {
		if (f == compute_functions[i]) {
			return true;
		}
	}
	return false;
}

static small_string get_var(variable var, function *f, function *main) {
	// TODO: Create the list of globals only once per function
	descriptor_set_group *group = NULL;
	if (main->descriptor_set_group_index != UINT32_MAX) {
		group = get_descriptor_set_group(main->descriptor_set_group_index);
	}
	if (group != NULL) {
		for (size_t set_index = 0; set_index < group->size; ++set_index) {
			descriptor_set *set = group->values[set_index];
			if (set == NULL)
				continue;

			for (size_t global_index = 0; global_index < set->globals.size; ++global_index) {
				global *g = get_global(set->globals.globals[global_index]);
				if (g != NULL && var.index == g->var_index) {
					small_string name;
					sprintf(name.str, "_set%zu_%" PRIu64, set_index, var.index);
					return name;
				}
			}
		}
	}

	small_string name;
	sprintf(name.str, "_%" PRIu64, var.index);
	return name;
}

static void write_functions(char *code, size_t *offset, shader_stage stage, function *main) {
	function *functions[256];
	size_t    functions_size = 0;

	functions[functions_size] = main;
	functions_size += 1;

	find_referenced_functions(main, functions, &functions_size);

	for (size_t i = 0; i < functions_size; ++i) {
		function *f = functions[i];
		assert(f != NULL);

		debug_context context = {0};
		check(f->block != NULL, context, "Function block missing");

		uint8_t *data = f->code.o;
		size_t   size = f->code.size;

		uint64_t parameter_ids[256] = {0};
		for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
			for (size_t i = 0; i < f->block->block.vars.size; ++i) {
				if (f->parameter_names[parameter_index] == f->block->block.vars.v[i].name) {
					parameter_ids[parameter_index] = f->block->block.vars.v[i].variable_id;
					break;
				}
			}
		}

		for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
			check(parameter_ids[parameter_index] != 0, context, "Parameter not found");
		}

		if (f == main) {
			if (stage == SHADER_STAGE_VERTEX) {
				*offset += sprintf(&code[*offset], "@vertex fn main(");
				for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
					if (parameter_index == 0) {
						*offset +=
						    sprintf(&code[*offset], "_%" PRIu64 ": %s", parameter_ids[parameter_index], type_string(f->parameter_types[parameter_index].type));
					}
					else {
						*offset += sprintf(&code[*offset], ", _%" PRIu64 ": %s", parameter_ids[parameter_index],
						                   type_string(f->parameter_types[parameter_index].type));
					}
				}
				*offset += sprintf(&code[*offset], ", @builtin(vertex_index) _kong_vertex_id: u32) -> %s {\n", type_string(f->return_type.type));
			}
			else if (stage == SHADER_STAGE_FRAGMENT) {
				if (get_type(f->return_type.type)->array_size > 0) {
					type_id base_type = get_type(f->return_type.type)->base;

					*offset += sprintf(&code[*offset], "struct _kong_colors_out {\n");
					for (uint32_t j = 0; j < get_type(f->return_type.type)->array_size; ++j) {
						*offset += sprintf(&code[*offset], "\t@location(%u) _%i: %s,\n", j, j, type_string(base_type));
					}
					*offset += sprintf(&code[*offset], "}\n\n");

					*offset += sprintf(&code[*offset], "@fragment fn main(");
					for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
						if (parameter_index == 0) {
							*offset += sprintf(&code[*offset], "_%" PRIu64 ": %s", parameter_ids[parameter_index],
							                   type_string(f->parameter_types[parameter_index].type));
						}
						else {
							*offset += sprintf(&code[*offset], ", _%" PRIu64 ": %s", parameter_ids[parameter_index],
							                   type_string(f->parameter_types[parameter_index].type));
						}
					}
					*offset += sprintf(&code[*offset], ") -> _kong_colors_out {\n");
				}
				else {
					*offset += sprintf(&code[*offset], "@fragment fn main(");
					for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
						if (parameter_index == 0) {
							*offset += sprintf(&code[*offset], "_%" PRIu64 ": %s", parameter_ids[parameter_index],
							                   type_string(f->parameter_types[parameter_index].type));
						}
						else {
							*offset += sprintf(&code[*offset], ", _%" PRIu64 ": %s", parameter_ids[parameter_index],
							                   type_string(f->parameter_types[parameter_index].type));
						}
					}
					*offset += sprintf(&code[*offset], ") -> @location(0) %s {\n", type_string(f->return_type.type));
				}
			}
			else if (stage == SHADER_STAGE_COMPUTE) {
				assert(f->parameters_size == 0);
				assert(f->return_type.type == void_id);

				attribute *threads = find_attribute(&f->attributes, add_name("threads"));
				assert(threads != NULL && threads->paramters_count == 3);

				*offset += sprintf(&code[*offset],
				                   "@compute @workgroup_size(%u, %u, %u) fn main(@builtin(local_invocation_id) _kong_group_thread_id: vec3<u32>, "
				                   "@builtin(workgroup_id) _kong_group_id: vec3<u32>, @builtin(global_invocation_id) _kong_dispatch_thread_id: vec3<u32>, "
				                   "@builtin(num_workgroups) _kong_threads_count: vec3<u32>, @builtin(local_invocation_index) _kong_group_index: u32) {\n",
				                   (uint32_t)threads->parameters[0], (uint32_t)threads->parameters[1], (uint32_t)threads->parameters[2]);
			}
		}
		else {
			*offset += sprintf(&code[*offset], "fn %s(", get_name(f->name));
			for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
				if (parameter_index == 0) {
					*offset +=
					    sprintf(&code[*offset], "_%" PRIu64 ": %s", parameter_ids[parameter_index], type_string(f->parameter_types[parameter_index].type));
				}
				else {
					*offset +=
					    sprintf(&code[*offset], ", _%" PRIu64 ": %s", parameter_ids[parameter_index], type_string(f->parameter_types[parameter_index].type));
				}
			}
			*offset += sprintf(&code[*offset], ") -> %s {\n", type_string(f->return_type.type));
		}

		int indentation = 1;

		size_t index = 0;
		while (index < size) {
			opcode *o = (opcode *)&data[index];
			switch (o->type) {
			case OPCODE_VAR:
				indent(code, offset, indentation);
				if (get_type(o->op_var.var.type.type)->array_size > 0) {
					type_id base_type = get_type(o->op_var.var.type.type)->base;

					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": array<%s, %i>;\n", o->op_var.var.index, type_string(base_type),
					                   get_type(o->op_var.var.type.type)->array_size);
				}
				else {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s;\n", o->op_var.var.index, type_string(o->op_var.var.type.type));
				}
				break;
			case OPCODE_LOAD_ACCESS_LIST: {
				indent(code, offset, indentation);

				type_id from_type = o->op_load_access_list.from.type.type;

				if (is_texture(from_type)) {
					assert(o->op_load_access_list.access_list_size == 1);
					assert(o->op_load_access_list.access_list[0].kind == ACCESS_ELEMENT);

					*offset += sprintf(&code[*offset], "var %s: %s = ", get_var(o->op_load_access_list.to, f, main).str,
					                   type_string(o->op_load_access_list.to.type.type));

					*offset +=
					    sprintf(&code[*offset], "textureLoad(%s, vec2<u32>(u32(%s.x), u32(%s.y)), 0);\n", get_var(o->op_load_access_list.from, f, main).str,
					            get_var(o->op_load_access_list.access_list[0].access_element.index, f, main).str,
					            get_var(o->op_load_access_list.access_list[0].access_element.index, f, main).str);
				}
				else {
					*offset += sprintf(&code[*offset], "var %s: %s = %s", get_var(o->op_load_access_list.to, f, main).str,
					                   type_string(o->op_load_access_list.to.type.type), get_var(o->op_load_access_list.from, f, main).str);

					type *s = get_type(o->op_load_access_list.from.type.type);

					for (size_t i = 0; i < o->op_load_access_list.access_list_size; ++i) {
						switch (o->op_load_access_list.access_list[i].kind) {
						case ACCESS_ELEMENT:
							*offset += sprintf(&code[*offset], "[_%" PRIu64 "]", o->op_load_access_list.access_list[i].access_element.index.index);
							break;
						case ACCESS_MEMBER:
							*offset += sprintf(&code[*offset], ".%s", get_name(o->op_load_access_list.access_list[i].access_member.name));
							break;
						case ACCESS_SWIZZLE: {
							char swizzle[5];

							for (uint32_t swizzle_index = 0; swizzle_index < o->op_load_access_list.access_list[i].access_swizzle.swizzle.size;
							     ++swizzle_index) {
								swizzle[swizzle_index] = "xyzw"[o->op_load_access_list.access_list[i].access_swizzle.swizzle.indices[swizzle_index]];
							}
							swizzle[o->op_load_access_list.access_list[i].access_swizzle.swizzle.size] = 0;

							*offset += sprintf(&code[*offset], ".%s", swizzle);
							break;
						}
						}

						s = get_type(o->op_load_access_list.access_list[i].type);
					}

					*offset += sprintf(&code[*offset], ";\n");
				}

				break;
			}
			case OPCODE_STORE_ACCESS_LIST:
			case OPCODE_SUB_AND_STORE_ACCESS_LIST:
			case OPCODE_ADD_AND_STORE_ACCESS_LIST:
			case OPCODE_DIVIDE_AND_STORE_ACCESS_LIST:
			case OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST: {
				indent(code, offset, indentation);

				type_id to_type = o->op_store_access_list.to.type.type;

				if (is_texture(to_type)) {
					assert(o->type == OPCODE_STORE_ACCESS_LIST);
					assert(o->op_store_access_list.access_list_size == 1);
					assert(o->op_store_access_list.access_list[0].kind == ACCESS_ELEMENT);

					*offset += sprintf(&code[*offset], "textureStore(%s, vec2<u32>(u32(_%" PRIu64 ".x), u32(_%" PRIu64 ".y)), _%" PRIu64 ");\n",
					                   get_var(o->op_store_access_list.to, f, main).str, o->op_store_access_list.access_list[0].access_element.index.index,
					                   o->op_store_access_list.access_list[0].access_element.index.index, o->op_store_access_list.from.index);
				}
				else {
					*offset += sprintf(&code[*offset], "%s", get_var(o->op_store_access_list.to, f, main).str);

					type *s = get_type(to_type);

					for (size_t i = 0; i < o->op_store_access_list.access_list_size; ++i) {
						switch (o->op_store_access_list.access_list[i].kind) {
						case ACCESS_ELEMENT:
							*offset += sprintf(&code[*offset], "[_%" PRIu64 "]", o->op_store_access_list.access_list[i].access_element.index.index);
							break;
						case ACCESS_MEMBER:
							*offset += sprintf(&code[*offset], ".%s", get_name(o->op_store_access_list.access_list[i].access_member.name));
							break;
						case ACCESS_SWIZZLE: {
							char swizzle[5];

							for (uint32_t swizzle_index = 0; swizzle_index < o->op_store_access_list.access_list[i].access_swizzle.swizzle.size;
							     ++swizzle_index) {
								swizzle[swizzle_index] = "xyzw"[o->op_store_access_list.access_list[i].access_swizzle.swizzle.indices[swizzle_index]];
							}
							swizzle[o->op_store_access_list.access_list[i].access_swizzle.swizzle.size] = 0;

							*offset += sprintf(&code[*offset], ".%s", swizzle);

							break;
						}
						}

						s = get_type(o->op_store_access_list.access_list[i].type);
					}

					switch (o->type) {
					case OPCODE_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " = %s;\n", get_var(o->op_store_access_list.from, f, main).str);
						break;
					case OPCODE_SUB_AND_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " -= %s;\n", get_var(o->op_store_access_list.from, f, main).str);
						break;
					case OPCODE_ADD_AND_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " += %s;\n", get_var(o->op_store_access_list.from, f, main).str);
						break;
					case OPCODE_DIVIDE_AND_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " /= %s;\n", get_var(o->op_store_access_list.from, f, main).str);
						break;
					case OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " *= %s;\n", get_var(o->op_store_access_list.from, f, main).str);
						break;
					default:
						assert(false);
						break;
					}
				}
				break;
			}
			case OPCODE_RETURN: {
				if (o->size > offsetof(opcode, op_return)) {
					if (f == main && stage == SHADER_STAGE_FRAGMENT && get_type(f->return_type.type)->array_size > 0) {
						indent(code, offset, indentation);
						*offset += sprintf(&code[*offset], "{\n");
						indent(code, offset, indentation + 1);
						*offset += sprintf(&code[*offset], "var _kong_colors: _kong_colors_out;\n");
						for (uint32_t j = 0; j < get_type(f->return_type.type)->array_size; ++j) {
							indent(code, offset, indentation + 1);
							*offset += sprintf(&code[*offset], "_kong_colors._%i = _%" PRIu64 "[%i];\n", j, o->op_return.var.index, j);
						}
						indent(code, offset, indentation + 1);
						*offset += sprintf(&code[*offset], "return _kong_colors;\n");
						indent(code, offset, indentation);
						*offset += sprintf(&code[*offset], "}\n");
					}
					else {
						indent(code, offset, indentation);
						*offset += sprintf(&code[*offset], "return _%" PRIu64 ";\n", o->op_return.var.index);
					}
				}
				else {
					indent(code, offset, indentation);
					*offset += sprintf(&code[*offset], "return;\n");
				}
				break;
			}
			case OPCODE_DISCARD: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "discard;\n");
				break;
			}
			case OPCODE_MULTIPLY: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " * _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_DIVIDE: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " / _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_ADD: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " + _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_SUB: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " - _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_MOD: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " %% _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_EQUALS: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " == _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_NOT_EQUALS: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " != _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_GREATER: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " > _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_GREATER_EQUAL: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " >= _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_LESS: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " < _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_LESS_EQUAL: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " <= _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_AND: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " && _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_OR: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " || _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_BITWISE_XOR: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " ^ _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_BITWISE_AND: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " & _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_BITWISE_OR: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " | _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_LEFT_SHIFT: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " << _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_RIGHT_SHIFT: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _%" PRIu64 " >> _%" PRIu64 ";\n", o->op_binary.result.index,
				                   type_string(o->op_binary.result.type.type), o->op_binary.left.index, o->op_binary.right.index);
				break;
			}
			case OPCODE_LOAD_FLOAT_CONSTANT:
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = %f;\n", o->op_load_float_constant.to.index,
				                   type_string(o->op_load_float_constant.to.type.type), o->op_load_float_constant.number);
				break;
			case OPCODE_LOAD_INT_CONSTANT:
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = %i;\n", o->op_load_int_constant.to.index,
				                   type_string(o->op_load_int_constant.to.type.type), o->op_load_int_constant.number);
				break;
			case OPCODE_LOAD_BOOL_CONSTANT:
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = %s;\n", o->op_load_bool_constant.to.index,
				                   type_string(o->op_load_bool_constant.to.type.type), o->op_load_bool_constant.boolean ? "true" : "false");
				break;
			case OPCODE_CALL: {
				debug_context context = {0};
				if (o->op_call.func == add_name("sample")) {
					check(o->op_call.parameters_size == 3, context, "sample requires three arguments");
					indent(code, offset, indentation);

					variable tex     = o->op_call.parameters[0];
					variable sampler = o->op_call.parameters[1];
					variable coord   = o->op_call.parameters[2];
					*offset +=
					    sprintf(&code[*offset], "var %s: %s = textureSample(%s, %s, %s);\n", get_var(o->op_call.var, f, main).str,
					            type_string(o->op_call.var.type.type), get_var(tex, f, main).str, get_var(sampler, f, main).str, get_var(coord, f, main).str);
				}
				else if (o->op_call.func == add_name("sample_lod")) {
					check(o->op_call.parameters_size == 4, context, "sample_lod requires four arguments");
					indent(code, offset, indentation);
					*offset += sprintf(&code[*offset], "var %s: %s = textureSampleLevel(%s, %s, %s, %s);\n", get_var(o->op_call.var, f, main).str,
					                   type_string(o->op_call.var.type.type), get_var(o->op_call.parameters[0], f, main).str,
					                   get_var(o->op_call.parameters[1], f, main).str, get_var(o->op_call.parameters[2], f, main).str,
					                   get_var(o->op_call.parameters[3], f, main).str);
				}
				else if (o->op_call.func == add_name("group_id")) {
					check(o->op_call.parameters_size == 0, context, "group_id can not have a parameter");
					indent(code, offset, indentation);
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _kong_group_id;\n", o->op_call.var.index, type_string(o->op_call.var.type.type));
				}
				else if (o->op_call.func == add_name("group_thread_id")) {
					check(o->op_call.parameters_size == 0, context, "group_thread_id can not have a parameter");
					indent(code, offset, indentation);
					*offset +=
					    sprintf(&code[*offset], "var _%" PRIu64 ": %s = _kong_group_thread_id;\n", o->op_call.var.index, type_string(o->op_call.var.type.type));
				}
				else if (o->op_call.func == add_name("dispatch_thread_id")) {
					check(o->op_call.parameters_size == 0, context, "dispatch_thread_id can not have a parameter");
					indent(code, offset, indentation);
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = _kong_dispatch_thread_id;\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type));
				}
				else if (o->op_call.func == add_name("group_index")) {
					check(o->op_call.parameters_size == 0, context, "group_index can not have a parameter");
					indent(code, offset, indentation);
					*offset +=
					    sprintf(&code[*offset], "var _%" PRIu64 ": %s = _kong_group_index;\n", o->op_call.var.index, type_string(o->op_call.var.type.type));
				}
				else if (o->op_call.func == add_name("vertex_id")) {
					check(o->op_call.parameters_size == 0, context, "vertex_id can not have a parameter");
					*offset +=
					    sprintf(&code[*offset], "var _%" PRIu64 ": %s = i32(_kong_vertex_id);\n", o->op_call.var.index, type_string(o->op_call.var.type.type));
				}
				else if (o->op_call.func == add_name("lerp")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = mix(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index,
					                   o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("ddx")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = dpdx(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddy")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = dpdy(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("rsqrt")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = inverseSqrt(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("frac")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = fract(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("float3x3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = mat3x3<f32>(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index,
					                   o->op_call.parameters[2].index);
				}

				////

				else if (o->op_call.func == add_name("lerp3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = mix(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index,
					                   o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("lerp4")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = mix(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index,
					                   o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("frac3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = fract(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("abs3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = abs(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("clamp3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = clamp(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index,
					                   o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("min3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = min(_%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("max3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = max(_%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("max4")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = max(_%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("step3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = step(_%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("pow3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = pow(_%" PRIu64 ", _%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("floor3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = floor(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ceil3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = ceil(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddx2")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = dpdx(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddy2")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = dpdy(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddx3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = dpdx(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddy3")) {
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = dpdy(_%" PRIu64 ");\n", o->op_call.var.index,
					                   type_string(o->op_call.var.type.type), o->op_call.parameters[0].index);
				}

				////

				else {
					name_id     func_name_id = o->op_call.func;
					const char *func_name    = get_name(o->op_call.func);
					if (func_name_id == add_name("float")) {
						func_name = "f32";
					}
					if (o->op_call.func == add_name("float2")) {
						func_name = "vec2<f32>";
					}
					else if (o->op_call.func == add_name("float3")) {
						func_name = "vec3<f32>";
					}
					else if (o->op_call.func == add_name("float4")) {
						func_name = "vec4<f32>";
					}
					else if (func_name_id == add_name("int")) {
						func_name = "i32";
					}
					else if (func_name_id == add_name("int2")) {
						func_name = "vec2<i32>";
					}
					else if (func_name_id == add_name("int3")) {
						func_name = "vec3<i32>";
					}
					else if (func_name_id == add_name("int4")) {
						func_name = "vec4<i32>";
					}
					else if (func_name_id == add_name("uint")) {
						func_name = "u32";
					}
					else if (func_name_id == add_name("uint2")) {
						func_name = "vec2<u32>";
					}
					else if (func_name_id == add_name("uint3")) {
						func_name = "vec3<u32>";
					}
					else if (func_name_id == add_name("uint4")) {
						func_name = "vec4<u32>";
					}

					indent(code, offset, indentation);
					*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = %s(", o->op_call.var.index, type_string(o->op_call.var.type.type), func_name);
					if (o->op_call.parameters_size > 0) {
						*offset += sprintf(&code[*offset], "_%" PRIu64, o->op_call.parameters[0].index);
						for (uint8_t i = 1; i < o->op_call.parameters_size; ++i) {
							*offset += sprintf(&code[*offset], ", _%" PRIu64, o->op_call.parameters[i].index);
						}
					}
					*offset += sprintf(&code[*offset], ");\n");
				}
				break;
			}
			case OPCODE_NEGATE: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = -_%" PRIu64 ";\n", o->op_negate.to.index, type_string(o->op_negate.to.type.type),
				                   o->op_negate.from.index);
				break;
			}
			case OPCODE_NOT: {
				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "var _%" PRIu64 ": %s = !_%" PRIu64 ";\n", o->op_not.to.index, type_string(o->op_not.to.type.type),
				                   o->op_not.from.index);
				break;
			}
			default:
				cstyle_write_opcode(code, offset, o, type_string, &indentation);
				break;
			}

			index += o->size;
		}

		*offset += sprintf(&code[*offset], "}\n\n");
	}
}

static char *wgsl_export_vertex2(function *main) {
	char         *wgsl    = (char *)calloc(1024 * 1024 * 2, 1);
	debug_context context = {0};
	check(wgsl != NULL, context, "Could not allocate the wgsl string");

	size_t offset = 0;

	type_id vertex_inputs[64];
	for (size_t input_index = 0; input_index < main->parameters_size; ++input_index) {
		vertex_inputs[input_index] = main->parameter_types[input_index].type;
	}
	type_id vertex_output = main->return_type.type;

	check(vertex_output != NO_TYPE, context, "vertex output missing");

	write_types(wgsl, &offset, SHADER_STAGE_VERTEX, vertex_inputs, main->parameters_size, vertex_output, main);

	bool framebuffer_texture_format = false;
	write_globals(wgsl, &offset, main, &framebuffer_texture_format);

	write_functions(wgsl, &offset, SHADER_STAGE_VERTEX, main);

	char *name = get_name(main->name);

	char filename[512];
	sprintf(filename, "kong_%s", name);

	char var_name[256];
	sprintf(var_name, "%s_code", name);

	return wgsl;
}

static char *wgsl_export_fragment2(function *main) {
	char         *wgsl    = (char *)calloc(1024 * 1024 * 2, 1);
	debug_context context = {0};
	check(wgsl != NULL, context, "Could not allocate the wgsl string");

	size_t offset = 0;

	assert(main->parameters_size > 0);
	type_id pixel_input = main->parameter_types[0].type;

	check(pixel_input != NO_TYPE, context, "fragment input missing");

	write_types(wgsl, &offset, SHADER_STAGE_FRAGMENT, &pixel_input, 1, NO_TYPE, main);

	bool framebuffer_texture_format = false;
	write_globals(wgsl, &offset, main, &framebuffer_texture_format);

	write_functions(wgsl, &offset, SHADER_STAGE_FRAGMENT, main);

	char *name = get_name(main->name);

	char filename[512];
	sprintf(filename, "kong_%s", name);

	char var_name[256];
	sprintf(var_name, "%s_code", name);

	return wgsl;
}

void wgsl_export2(char **vs, char **fs) {
	vertex_inputs_size      = 0;
	fragment_inputs_size    = 0;
	vertex_functions_size   = 0;
	fragment_functions_size = 0;

	for (type_id i = 0; get_type(i) != NULL; ++i) {
		type *t = get_type(i);
		if (!t->built_in && has_attribute(&t->attributes, add_name("pipe"))) {
			name_id vertex_shader_name   = NO_NAME;
			name_id fragment_shader_name = NO_NAME;

			for (size_t j = 0; j < t->members.size; ++j) {
				if (t->members.m[j].name == add_name("vertex")) {
					vertex_shader_name = t->members.m[j].value.identifier;
				}
				else if (t->members.m[j].name == add_name("fragment")) {
					fragment_shader_name = t->members.m[j].value.identifier;
				}
			}

			debug_context context = {0};
			check(vertex_shader_name != NO_NAME, context, "vertex shader not found");
			check(fragment_shader_name != NO_NAME, context, "fragment shader not found");

			for (function_id i = 0; get_function(i) != NULL; ++i) {
				function *f = get_function(i);
				if (f->name == vertex_shader_name) {
					vertex_functions[vertex_functions_size] = i;
					vertex_functions_size += 1;

					size_t vertex_location_offset = 0;

					for (uint32_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
						vertex_inputs[vertex_inputs_size]           = f->parameter_types[parameter_index].type;
						vertex_location_offsets[vertex_inputs_size] = vertex_location_offset;

						vertex_inputs_size += 1;
						vertex_location_offset += get_type(f->parameter_types[parameter_index].type)->members.size;
					}
				}
				else if (f->name == fragment_shader_name) {
					fragment_functions[fragment_functions_size] = i;
					fragment_functions_size += 1;

					assert(f->parameters_size > 0);
					fragment_inputs[fragment_inputs_size] = f->parameter_types[0].type;
					fragment_inputs_size += 1;
				}
			}
		}
	}

	*vs = wgsl_export_vertex2(get_function(vertex_functions[0]));
	*fs = wgsl_export_fragment2(get_function(fragment_functions[0]));
}
