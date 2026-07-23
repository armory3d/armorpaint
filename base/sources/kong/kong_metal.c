#include "kong.h"
#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static type_id     vertex_inputs[256];
size_t             vertex_inputs_size = 0;
static type_id     fragment_inputs[256];
size_t             fragment_inputs_size = 0;
static int         global_register_indices[512];
static function_id vertex_functions[256];
size_t             vertex_functions_size = 0;
static function_id fragment_functions[256];
size_t             fragment_functions_size = 0;
static function_id compute_functions[256];
static size_t      compute_functions_size = 0;

static char *type_string(type_id type) {
	if (type == float_id) {
		return "float";
	}
	if (type == float2_id) {
		return "float2";
	}
	if (type == float3_id) {
		return "float3";
	}
	if (type == float4_id) {
		return "float4";
	}
	if (type == float4x4_id) {
		return "float4x4";
	}
	return get_name(get_type(type)->name);
}

static char *function_string(name_id func) {
	return get_name(func);
}

static void write_code(char *metal, char *directory, const char *filename) {
	char full_filename[512];
	sprintf(full_filename, "%s/%s.metal", directory, filename);

	FILE *file = fopen(full_filename, "wb");
	fprintf(file, "%s", metal);
	fclose(file);
}

static bool is_vertex_input(type_id t) {
	for (size_t i = 0; i < vertex_inputs_size; ++i) {
		if (t == vertex_inputs[i]) {
			return true;
		}
	}
	return false;
}

static bool is_fragment_input(type_id t) {
	for (size_t i = 0; i < fragment_inputs_size; ++i) {
		if (t == fragment_inputs[i]) {
			return true;
		}
	}
	return false;
}

static void type_name(type_id id, char *output_name) {
	type *t = get_type(id);

	if (t->name == NO_NAME) {
		bool found = false;
		for (global_id j = 0; get_global(j)->type != NO_TYPE; ++j) {
			global *g = get_global(j);
			if (g->type == id) {
				sprintf(output_name, "_%" PRIu64 "_type", g->var_index);
				found = true;
				break;
			}
		}

		if (!found) {
			strcpy(output_name, "Unknown");
		}
	}
	else {
		strcpy(output_name, get_name(t->name));
	}
}

static void write_types(char *metal, size_t *offset) {
	for (type_id i = 0; get_type(i) != NULL; ++i) {
		type *t = get_type(i);
		if (!t->built_in && has_attribute(&t->attributes, add_name("pipe"))) {
			name_id vertex_shader_name = NO_NAME;

			for (size_t j = 0; j < t->members.size; ++j) {
				if (t->members.m[j].name == add_name("vertex")) {
					vertex_shader_name = t->members.m[j].value.identifier;
				}
			}

			for (function_id i = 0; get_function(i) != NULL; ++i) {
				function *f = get_function(i);
				if (f->name == vertex_shader_name) {
					uint64_t parameter_ids[256] = {0};
					for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
						for (size_t i = 0; i < f->block->block.vars.size; ++i) {
							if (f->parameter_names[parameter_index] == f->block->block.vars.v[i].name) {
								parameter_ids[parameter_index] = f->block->block.vars.v[i].variable_id;
								break;
							}
						}
					}

					*offset += sprintf(&metal[*offset], "struct _kong_%s_attributes {\n", get_name(f->name));

					uint32_t a = 0;

					for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
						type *t = get_type(f->parameter_types[parameter_index].type);

						for (size_t j = 0; j < t->members.size; ++j) {
							*offset += sprintf(&metal[*offset], "\t%s _%" PRIu64 "_%s [[attribute(%u)]];\n", type_string(t->members.m[j].type.type),
							                   parameter_ids[parameter_index], get_name(t->members.m[j].name), a);

							a += 1;
						}
					}

					*offset += sprintf(&metal[*offset], "};\n\n");
				}
			}
		}
	}

	for (type_id i = 0; get_type(i) != NULL; ++i) {
		if (is_vertex_input(i)) {
			continue;
		}

		type *t = get_type(i);

		if (!t->built_in && !has_attribute(&t->attributes, add_name("pipe"))) {
			char name[256];
			type_name(i, name);
			*offset += sprintf(&metal[*offset], "struct %s {\n", name);

			if (is_fragment_input(i)) {
				for (size_t j = 0; j < t->members.size; ++j) {
					if (j == 0) {
						*offset += sprintf(&metal[*offset], "\t%s %s [[position]];\n", type_string(t->members.m[j].type.type), get_name(t->members.m[j].name));
					}
					else {
						*offset += sprintf(&metal[*offset], "\t%s %s [[user(locn%zu)]];\n", type_string(t->members.m[j].type.type),
						                   get_name(t->members.m[j].name), j - 1);
					}
				}
			}
			else {
				for (size_t j = 0; j < t->members.size; ++j) {
					*offset += sprintf(&metal[*offset], "\t%s %s;\n", type_string(t->members.m[j].type.type), get_name(t->members.m[j].name));
				}
			}
			*offset += sprintf(&metal[*offset], "};\n\n");
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

static void write_argument_buffers(char *code, size_t *offset) {
	for (size_t set_index = 0; set_index < get_sets_count(); ++set_index) {
		descriptor_set *set = get_set(set_index);

		if (set->name == add_name("root_constants")) {
			if (set->globals.size != 1) {
				debug_context context = {0};
				error(context, "More than one root constants struct found");
			}

			continue;
		}

		*offset += sprintf(&code[*offset], "struct %s {\n", get_name(set->name));

		for (size_t global_index = 0; global_index < set->globals.size; ++global_index) {
			global_id g_id     = set->globals.globals[global_index];
			global   *g        = get_global(g_id);
			bool      writable = set->globals.writable[global_index];

			if (!get_type(g->type)->built_in) {
				if (!has_attribute(&g->attributes, add_name("indexed"))) {
					char name[256];
					type_name(g->type, name);
					*offset += sprintf(&code[*offset], "\tconstant %s *_%" PRIu64 " [[id(%zu)]];\n", name, g->var_index,
					                   global_index); // TODO: Make use of constantDataAtIndex
				}
			}
			else if (is_texture(g->type)) {
				if (writable) {
					*offset += sprintf(&code[*offset], "\ttexture2d<float, access::write> _%" PRIu64 " [[id(%zu)]];\n", g->var_index, global_index);
				}
				else {
					*offset += sprintf(&code[*offset], "\ttexture2d<float> _%" PRIu64 " [[id(%zu)]];\n", g->var_index, global_index);
				}
			}
			else if (is_sampler(g->type)) {
				*offset += sprintf(&code[*offset], "\tsampler _%" PRIu64 " [[id(%zu)]];\n", g->var_index, global_index);
			}
			else {
				type *t = get_type(g->type);
				if (t->array_size > 0) {
					*offset +=
					    sprintf(&code[*offset], "\tdevice %s *_%" PRIu64 " [[id(%zu)]];\n", get_name(get_type(g->type)->name), g->var_index, global_index);
				}
			}
		}

		*offset += sprintf(&code[*offset], "};\n\n");
	}
}

static void write_globals(char *code, size_t *offset) {
	global_array globals = {0};
	for (function_id i = 0; get_function(i) != NULL; ++i) {
		function *f = get_function(i);
		find_referenced_globals(f, &globals);
	}

	for (size_t i = 0; i < globals.size; ++i) {
		global *g         = get_global(globals.globals[i]);
		type   *t         = get_type(g->type);
		type_id base_type = t->array_size > 0 ? t->base : g->type;

		if (base_type == float_id) {
			*offset += sprintf(&code[*offset], "constant float _%" PRIu64 " = %f;\n\n", g->var_index, g->value.value.floats[0]);
		}
		else if (base_type == float2_id) {
			*offset +=
			    sprintf(&code[*offset], "constant float2 _%" PRIu64 " = float2(%f, %f);\n\n", g->var_index, g->value.value.floats[0], g->value.value.floats[1]);
		}
		else if (base_type == float3_id) {
			*offset += sprintf(&code[*offset], "constant float3 _%" PRIu64 " = float3(%f, %f, %f);\n\n", g->var_index, g->value.value.floats[0],
			                   g->value.value.floats[1], g->value.value.floats[2]);
		}
		else if (base_type == float4_id) {
			*offset += sprintf(&code[*offset], "constant float4 _%" PRIu64 " = float4(%f, %f, %f, %f);\n\n", g->var_index, g->value.value.floats[0],
			                   g->value.value.floats[1], g->value.value.floats[2], g->value.value.floats[3]);
		}
	}
}

static bool var_name(variable var, char *output_name) {
	global *g = NULL;

	for (global_id j = 0; get_global(j) != NULL && get_global(j)->type != NO_TYPE; ++j) {
		if (get_global(j)->var_index == var.index) {
			g = get_global(j);
			break;
		}
	}

	if (g == NULL || has_attribute(&g->attributes, add_name("indexed"))) {
		sprintf(output_name, "_%" PRIu64, var.index);
	}
	else if (g->sets[0]->name == add_name("root_constants")) {
		sprintf(output_name, "root_constants");
		return true;
	}
	else {
		sprintf(output_name, "argument_buffer0._%" PRIu64, var.index);
	}

	return false;
}

static void write_functions(char *code, size_t *offset) {
	for (function_id i = 0; get_function(i) != NULL; ++i) {
		function *f = get_function(i);

		if (f->block == NULL) {
			continue;
		}

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

		debug_context context = {0};
		for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
			check(parameter_ids[parameter_index] != 0, context, "Parameter not found");
		}

		char buffers[1024];
		memset(buffers, 0, sizeof(buffers));

		size_t buffer_index = f->parameters_size;

		if (is_vertex_function(i) || is_fragment_function(i) || is_compute_function(i)) {
			size_t buffers_offset = 0;

			descriptor_set_group *set_group = get_descriptor_set_group(f->descriptor_set_group_index);

			size_t argument_buffer_index = 0;

			for (size_t set_index = 0; set_index < set_group->size; ++set_index) {
				descriptor_set *set = set_group->values[set_index];

				if (set->name == add_name("root_constants")) {
					global *g = get_global(set->globals.globals[0]);

					char name[256];
					type_name(g->type, name);

					buffers_offset += sprintf(&buffers[buffers_offset], ", constant %s& root_constants [[buffer(%zu)]]", name, buffer_index);
				}
				else {
					buffers_offset += sprintf(&buffers[buffers_offset], ", constant %s& argument_buffer%zu [[buffer(%zu)]]", get_name(set->name),
					                          argument_buffer_index, buffer_index);
					argument_buffer_index += 1;
				}

				buffer_index += 1;
			}

			for (size_t set_index = 0; set_index < set_group->size; ++set_index) {
				descriptor_set *set = set_group->values[set_index];

				for (size_t global_index = 0; global_index < set->globals.size; ++global_index) {
					global *g = get_global(set->globals.globals[global_index]);
					if (has_attribute(&g->attributes, add_name("indexed"))) {
						char t[256];
						type_name(g->type, t);

						buffers_offset += sprintf(&buffers[buffers_offset], ", constant %s *_%" PRIu64 " [[buffer(%zu)]]", t, g->var_index, buffer_index);

						buffer_index += 1;
					}
				}
			}
		}

		if (is_vertex_function(i)) {
			*offset += sprintf(&code[*offset], "vertex %s %s(_kong_%s_attributes _kong_stage_in [[stage_in]]", type_string(f->return_type.type),
			                   get_name(f->name), get_name(f->name));

			*offset += sprintf(&code[*offset], "%s, uint _kong_vertex_id [[vertex_id]], uint _kong_instance_id [[instance_id]]) {\n", buffers);
		}
		else if (is_fragment_function(i)) {
			if (get_type(f->return_type.type)->array_size > 0) {
				*offset += sprintf(&code[*offset], "struct _kong_colors_out {\n");
				for (uint32_t j = 0; j < get_type(f->return_type.type)->array_size; ++j) {
					*offset += sprintf(&code[*offset], "\t%s _%i [[color(%i)]];\n", type_string(f->return_type.type), j, j);
				}
				*offset += sprintf(&code[*offset], "};\n\n");

				*offset += sprintf(&code[*offset], "fragment _kong_colors_out %s(%s _%" PRIu64 " [[stage_in]]", get_name(f->name),
				                   type_string(f->parameter_types[0].type), parameter_ids[0]);
				for (uint8_t parameter_index = 1; parameter_index < f->parameters_size; ++parameter_index) {
					*offset += sprintf(&code[*offset], ", %s _%" PRIu64, type_string(f->parameter_types[parameter_index].type), parameter_ids[parameter_index]);
				}
				*offset += sprintf(&code[*offset], "%s) {\n", buffers);
			}
			else {
				*offset += sprintf(&code[*offset], "fragment _kong_color_out %s(%s _%" PRIu64 " [[stage_in]]", get_name(f->name),
				                   type_string(f->parameter_types[0].type), parameter_ids[0]);
				for (uint8_t parameter_index = 1; parameter_index < f->parameters_size; ++parameter_index) {
					*offset += sprintf(&code[*offset], ", %s _%" PRIu64, type_string(f->parameter_types[parameter_index].type), parameter_ids[parameter_index]);
				}
				*offset += sprintf(&code[*offset], "%s) {\n", buffers);
			}
		}
		else if (is_compute_function(i)) {
			*offset +=
			    sprintf(&code[*offset],
			            "kernel void %s(uint3 _kong_group_thread_id [[thread_position_in_threadgroup]], uint3 _kong_group_id [[threadgroup_position_in_grid]], "
			            "uint _kong_group_index [[thread_index_in_threadgroup]], uint3 _kong_dispatch_thread_id [[thread_position_in_grid]]",
			            get_name(f->name));
			for (uint8_t parameter_index = 1; parameter_index < f->parameters_size; ++parameter_index) {
				*offset += sprintf(&code[*offset], ", %s _%" PRIu64, type_string(f->parameter_types[0].type), parameter_ids[0]);
			}
			*offset += sprintf(&code[*offset], "%s) {\n", buffers);
		}
		else {
			descriptor_set_group *set_group = get_descriptor_set_group(0);
			descriptor_set       *set       = set_group->values[0];

			*offset += sprintf(&code[*offset], "%s %s(constant %s& argument_buffer0", type_string(f->return_type.type), get_name(f->name), get_name(set->name));

			for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
				*offset += sprintf(&code[*offset], ", %s _%" PRIu64, type_string(f->parameter_types[parameter_index].type), parameter_ids[parameter_index]);
			}
			*offset += sprintf(&code[*offset], ") {\n");
		}

		int indentation = 1;

		size_t index = 0;
		while (index < size) {
			opcode *o = (opcode *)&data[index];
			switch (o->type) {
			case OPCODE_LOAD_ACCESS_LIST: {
				global *g = NULL;

				for (global_id j = 0; get_global(j) != NULL && get_global(j)->type != NO_TYPE; ++j) {
					if (o->op_load_access_list.from.index == get_global(j)->var_index) {
						g = get_global(j);
						break;
					}
				}

				char from_name[256];
				bool root_constant = var_name(o->op_load_access_list.from, from_name);

				indent(code, offset, indentation);
				*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = ", type_string(o->op_load_access_list.to.type.type), o->op_load_access_list.to.index);

				type_id s = o->op_load_access_list.from.type.type;

				for (size_t i = 0; i < o->op_load_access_list.access_list_size; ++i) {
					switch (o->op_load_access_list.access_list[i].kind) {
					case ACCESS_ELEMENT:
						if (i == 0) {
							*offset += sprintf(&code[*offset], "%s", from_name);
						}

						if (is_texture(o->op_load_access_list.from.type.type) && i == 0) {
							*offset += sprintf(&code[*offset], ".read(_%" PRIu64 ")", o->op_load_access_list.access_list[i].access_element.index.index);
						}
						else {
							*offset += sprintf(&code[*offset], "[_%" PRIu64 "]", o->op_load_access_list.access_list[i].access_element.index.index);
						}
						break;
					case ACCESS_MEMBER:
						if (i == 0 && g != NULL && !root_constant) {
							*offset += sprintf(&code[*offset], "%s", from_name);

							*offset += sprintf(&code[*offset], "->%s", get_name(o->op_load_access_list.access_list[i].access_member.name));
						}
						else if (i == 0 && is_vertex_input(s)) {
							*offset +=
							    sprintf(&code[*offset], "_kong_stage_in.%s_%s", from_name, get_name(o->op_load_access_list.access_list[i].access_member.name));
						}
						else {
							if (i == 0) {
								*offset += sprintf(&code[*offset], "%s", from_name);
							}

							*offset += sprintf(&code[*offset], ".%s", get_name(o->op_load_access_list.access_list[i].access_member.name));
						}
						break;
					case ACCESS_SWIZZLE: {
						if (i == 0) {
							*offset += sprintf(&code[*offset], "%s", from_name);
						}

						char swizzle[4];

						for (uint32_t swizzle_index = 0; swizzle_index < o->op_load_access_list.access_list[i].access_swizzle.swizzle.size; ++swizzle_index) {
							swizzle[swizzle_index] = "xyzw"[o->op_load_access_list.access_list[i].access_swizzle.swizzle.indices[swizzle_index]];
						}
						swizzle[o->op_load_access_list.access_list[i].access_swizzle.swizzle.size] = 0;

						*offset += sprintf(&code[*offset], ".%s", swizzle);
						break;
					}
					}

					s = o->op_load_access_list.access_list[i].type;
				}

				*offset += sprintf(&code[*offset], ";\n");

				break;
			}
			case OPCODE_STORE_ACCESS_LIST:
			case OPCODE_SUB_AND_STORE_ACCESS_LIST:
			case OPCODE_ADD_AND_STORE_ACCESS_LIST:
			case OPCODE_DIVIDE_AND_STORE_ACCESS_LIST:
			case OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST: {
				indent(code, offset, indentation);

				char to_name[256];
				var_name(o->op_store_access_list.to, to_name);

				*offset += sprintf(&code[*offset], "%s", to_name);

				type *s = get_type(o->op_store_access_list.to.type.type);

				for (size_t i = 0; i < o->op_store_access_list.access_list_size; ++i) {
					switch (o->op_store_access_list.access_list[i].kind) {
					case ACCESS_ELEMENT:
						if (is_texture(o->op_store_access_list.to.type.type)) {
							*offset += sprintf(&code[*offset], ".write(_%" PRIu64 ", _%" PRIu64 ");\n", o->op_store_access_list.from.index,
							                   o->op_store_access_list.access_list[i].access_element.index.index);
						}
						else {
							*offset += sprintf(&code[*offset], "[_%" PRIu64 "]", o->op_store_access_list.access_list[i].access_element.index.index);
						}
						break;
					case ACCESS_MEMBER:
						*offset += sprintf(&code[*offset], ".%s", get_name(o->op_store_access_list.access_list[i].access_member.name));
						break;
					case ACCESS_SWIZZLE: {
						char swizzle[4];

						for (uint32_t swizzle_index = 0; swizzle_index < o->op_store_access_list.access_list[i].access_swizzle.swizzle.size; ++swizzle_index) {
							swizzle[swizzle_index] = "xyzw"[o->op_store_access_list.access_list[i].access_swizzle.swizzle.indices[swizzle_index]];
						}
						swizzle[o->op_store_access_list.access_list[i].access_swizzle.swizzle.size] = 0;

						*offset += sprintf(&code[*offset], ".%s", swizzle);

						break;
					}
					}

					s = get_type(o->op_store_access_list.access_list[i].type);
				}

				if (!is_texture(o->op_store_access_list.to.type.type)) {
					switch (o->type) {
					case OPCODE_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " = _%" PRIu64 ";\n", o->op_store_access_list.from.index);
						break;
					case OPCODE_SUB_AND_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " -= _%" PRIu64 ";\n", o->op_store_access_list.from.index);
						break;
					case OPCODE_ADD_AND_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " += _%" PRIu64 ";\n", o->op_store_access_list.from.index);
						break;
					case OPCODE_DIVIDE_AND_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " /= _%" PRIu64 ";\n", o->op_store_access_list.from.index);
						break;
					case OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST:
						*offset += sprintf(&code[*offset], " *= _%" PRIu64 ";\n", o->op_store_access_list.from.index);
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
					if (is_fragment_function(i) && get_type(f->return_type.type)->array_size > 0) {
						indent(code, offset, indentation);
						*offset += sprintf(&code[*offset], "{\n");
						indent(code, offset, indentation + 1);
						*offset += sprintf(&code[*offset], "_kong_colors_out _kong_colors;\n");
						for (uint32_t j = 0; j < get_type(f->return_type.type)->array_size; ++j) {
							indent(code, offset, indentation + 1);
							*offset += sprintf(&code[*offset], "_kong_colors._%i = _%" PRIu64 "[%i];\n", j, o->op_return.var.index, j);
						}
						indent(code, offset, indentation + 1);
						*offset += sprintf(&code[*offset], "return _kong_colors;\n");
						indent(code, offset, indentation);
						*offset += sprintf(&code[*offset], "}\n");
					}
					else if (is_fragment_function(i)) {
						indent(code, offset, indentation);
						*offset += sprintf(&code[*offset], "{\n");
						indent(code, offset, indentation + 1);
						*offset += sprintf(&code[*offset], "_kong_color_out _kong_color;\n");
						indent(code, offset, indentation + 1);
						*offset += sprintf(&code[*offset], "_kong_color._0 = _%" PRIu64 ";\n", o->op_return.var.index);
						indent(code, offset, indentation + 1);
						*offset += sprintf(&code[*offset], "return _kong_color;\n");
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
				*offset += sprintf(&code[*offset], "discard_fragment();\n");
				break;
			}
			case OPCODE_CALL: {
				debug_context context = {0};

				indent(code, offset, indentation);

				if (o->op_call.func == add_name("sample")) {
					check(o->op_call.parameters_size == 3, context, "sample requires three parameters");

					variable image_var = o->op_call.parameters[0];
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = argument_buffer0._%" PRIu64 ".sample(argument_buffer0._%" PRIu64 ", _%" PRIu64 ");\n",
					                   type_string(o->op_call.var.type.type), o->op_call.var.index, image_var.index, o->op_call.parameters[1].index,
					                   o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("sample_lod")) {
					check(o->op_call.parameters_size == 4, context, "sample_lod requires four parameters");

					*offset +=
					    sprintf(&code[*offset],
					            "%s _%" PRIu64 " = argument_buffer0._%" PRIu64 ".sample(argument_buffer0._%" PRIu64 ", _%" PRIu64 ", level(_%" PRIu64 "));\n",
					            type_string(o->op_call.var.type.type), o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index,
					            o->op_call.parameters[2].index, o->op_call.parameters[3].index);
				}
				else if (o->op_call.func == add_name("group_id")) {
					check(o->op_call.parameters_size == 0, context, "group_id can not have a parameter");
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _kong_group_id;\n", type_string(o->op_call.var.type.type), o->op_call.var.index);
				}
				else if (o->op_call.func == add_name("group_thread_id")) {
					check(o->op_call.parameters_size == 0, context, "group_thread_id can not have a parameter");
					*offset +=
					    sprintf(&code[*offset], "%s _%" PRIu64 " = _kong_group_thread_id;\n", type_string(o->op_call.var.type.type), o->op_call.var.index);
				}
				else if (o->op_call.func == add_name("dispatch_thread_id")) {
					check(o->op_call.parameters_size == 0, context, "dispatch_thread_id can not have a parameter");
					*offset +=
					    sprintf(&code[*offset], "%s _%" PRIu64 " = _kong_dispatch_thread_id;\n", type_string(o->op_call.var.type.type), o->op_call.var.index);
				}
				else if (o->op_call.func == add_name("group_index")) {
					check(o->op_call.parameters_size == 0, context, "group_index can not have a parameter");
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _kong_group_index;\n", type_string(o->op_call.var.type.type), o->op_call.var.index);
				}
				else if (o->op_call.func == add_name("vertex_id")) {
					check(o->op_call.parameters_size == 0, context, "vertex_id can not have a parameter");
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _kong_vertex_id;\n", type_string(o->op_call.var.type.type), o->op_call.var.index);
				}
				else if (o->op_call.func == add_name("lerp")) {
					*offset +=
					    sprintf(&code[*offset], "%s _%" PRIu64 " = mix(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					            o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index, o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("frac")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = fract(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddx")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = dfdx(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddy")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = dfdy(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}

				////

				else if (o->op_call.func == add_name("lerp3")) {
					*offset +=
					    sprintf(&code[*offset], "%s _%" PRIu64 " = mix(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					            o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index, o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("lerp4")) {
					*offset +=
					    sprintf(&code[*offset], "%s _%" PRIu64 " = mix(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					            o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index, o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("frac3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = fract(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("abs3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = abs(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("clamp3")) {
					*offset +=
					    sprintf(&code[*offset], "%s _%" PRIu64 " = clamp(_%" PRIu64 ", _%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					            o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index, o->op_call.parameters[2].index);
				}
				else if (o->op_call.func == add_name("min3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = min(_%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					                   o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("max3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = max(_%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					                   o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("max4")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = max(_%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					                   o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("step3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = step(_%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					                   o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("pow3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = pow(_%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_call.var.type.type),
					                   o->op_call.var.index, o->op_call.parameters[0].index, o->op_call.parameters[1].index);
				}
				else if (o->op_call.func == add_name("floor3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = floor(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ceil3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = ceil(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddx2")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = dfdx(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddy2")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = dfdy(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddx3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = dfdx(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}
				else if (o->op_call.func == add_name("ddy3")) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = dfdy(_%" PRIu64 ");\n", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   o->op_call.parameters[0].index);
				}

				////

				else {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = %s(", type_string(o->op_call.var.type.type), o->op_call.var.index,
					                   function_string(o->op_call.func));

					bool is_built_in = true;
					for (function_id i = 0; get_function(i) != NULL; ++i) {
						function *f = get_function(i);
						if (o->op_call.func == f->name && f->block != NULL) {
							is_built_in = false;
							break;
						}
					}

					if (!is_built_in) {
						*offset += sprintf(&code[*offset], "argument_buffer0");
						if (o->op_call.parameters_size > 0) {
							*offset += sprintf(&code[*offset], ", ");
						}
					}

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
			case OPCODE_MOD: {
				indent(code, offset, indentation);
				type_id base_type = vector_base_type(o->op_binary.result.type.type);
				if (base_type == int_id || base_type == uint_id) {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = _%" PRIu64 " %% _%" PRIu64 ";\n", type_string(o->op_binary.result.type.type),
					                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
				}
				else {
					*offset += sprintf(&code[*offset], "%s _%" PRIu64 " = fmod(_%" PRIu64 ", _%" PRIu64 ");\n", type_string(o->op_binary.result.type.type),
					                   o->op_binary.result.index, o->op_binary.left.index, o->op_binary.right.index);
				}
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

static char *metal_export_everything(char *directory) {
	char         *metal   = (char *)calloc(1024 * 1024 * 2, 1);
	debug_context context = {0};
	check(metal != NULL, context, "Could not allocate Metal string");
	size_t offset = 0;

	offset += sprintf(&metal[offset], "#include <metal_stdlib>\n");
	offset += sprintf(&metal[offset], "#include <simd/simd.h>\n\n");
	offset += sprintf(&metal[offset], "using namespace metal;\n\n");

	offset += sprintf(&metal[offset], "struct _kong_color_out {\n");
	offset += sprintf(&metal[offset], "\tfloat4 _0 [[color(0)]];\n");
	offset += sprintf(&metal[offset], "};\n\n");

	write_types(metal, &offset);
	write_argument_buffers(metal, &offset);
	write_globals(metal, &offset);
	write_functions(metal, &offset);
	return metal;
}

char *metal_export(char *directory) {
	int cbuffer_index = 0;
	int texture_index = 0;
	int sampler_index = 0;

	memset(global_register_indices, 0, sizeof(global_register_indices));

	for (global_id i = 0; get_global(i) != NULL && get_global(i)->type != NO_TYPE; ++i) {
		global *g = get_global(i);
		if (g->type == sampler_type_id) {
			global_register_indices[i] = sampler_index;
			sampler_index += 1;
		}
		else if (is_texture(g->type)) {
			global_register_indices[i] = texture_index;
			texture_index += 1;
		}
		else if (g->type == float_id) {
		}
		else {
			global_register_indices[i] = cbuffer_index;
			cbuffer_index += 1;
		}
	}

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
			check(vertex_shader_name != NO_NAME, context, "vertex shader missing");
			check(fragment_shader_name != NO_NAME, context, "fragment shader missing");

			for (function_id i = 0; get_function(i) != NULL; ++i) {
				function *f = get_function(i);
				if (f->name == vertex_shader_name) {
					vertex_functions[vertex_functions_size] = i;
					vertex_functions_size += 1;

					for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
						vertex_inputs[vertex_inputs_size] = f->parameter_types[parameter_index].type;
						vertex_inputs_size += 1;
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

	for (function_id i = 0; get_function(i) != NULL; ++i) {
		function *f = get_function(i);
		if (has_attribute(&f->attributes, add_name("compute"))) {
			compute_functions[compute_functions_size] = i;
			compute_functions_size += 1;
		}
	}

	return metal_export_everything(directory);
}
