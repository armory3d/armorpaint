#include "kong.h"
#include "dir.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum mode {
	MODE_SELECT,
	MODE_NUMBER,
	MODE_OPERATOR,
	MODE_IDENTIFIER,
	MODE_LINE_COMMENT,
	MODE_COMMENT
} mode;

typedef struct tokenizer_state {
	const char *iterator;
	char        next;
	char        next_next;
	int         line, column;
	bool        line_end;
} tokenizer_state;

typedef struct allocated_global {
	global  *g;
	uint64_t variable_id;
} allocated_global;

typedef struct tokenizer_buffer {
	char  *buf;
	size_t current_size;
	size_t max_size;
	int    column, line;
} tokenizer_buffer;

typedef struct prefix {
	char   str[5];
	size_t size;
} prefix;

static type   *types           = NULL;
static type_id types_size      = 1024;
type_id        next_type_index = 0;

type_id void_id;
type_id float_id;
type_id float2_id;
type_id float3_id;
type_id float4_id;
type_id float2x2_id;
type_id float3x2_id;
type_id float2x3_id;
type_id float4x2_id;
type_id float2x4_id;
type_id float3x3_id;
type_id float4x3_id;
type_id float3x4_id;
type_id float4x4_id;
type_id int_id;
type_id int2_id;
type_id int3_id;
type_id int4_id;
type_id uint_id;
type_id uint2_id;
type_id uint3_id;
type_id uint4_id;
type_id bool_id;
type_id bool2_id;
type_id bool3_id;
type_id bool4_id;
type_id function_type_id;
type_id sampler_type_id;
type_id ray_type_id;
type_id bvh_type_id;

static render_pipelines       all_render_pipelines;
static render_pipeline_groups all_render_pipeline_groups;
static compute_shaders        all_compute_shaders;
static descriptor_set_groups  all_descriptor_set_groups;

static allocated_global allocated_globals[1024];
size_t                  allocated_globals_size = 0;

const char all_names[1024 * 1024];
uint64_t   next_variable_id = 1;
variable   all_variables[1024 * 1024];

bool               kong_error          = false;
static function   *functions           = NULL;
static function_id functions_size      = 128;
static function_id functions_zeroed    = 0;
function_id        next_function_index = 0;

static void zero_new_function_slots(void) {
	if (functions_size > functions_zeroed) {
		memset(&functions[functions_zeroed], 0, (functions_size - functions_zeroed) * sizeof(function));
		functions_zeroed = functions_size;
	}
}

static global globals[1024];
global_id     globals_size = 0;

#include "stb_ds.h"

static char  *names       = NULL;
static size_t names_size  = 1024 * 1024;
name_id       names_index = 1;

static descriptor_set sets[MAX_SETS];
size_t                sets_count = 0;

opcodes new_code;

struct {
	char   *key;
	name_id value;
} *hash = NULL;

static void find_referenced_global_for_var(variable v, global_array *globals, bool read, bool write) {
	for (global_id j = 0; get_global(j) != NULL && get_global(j)->type != NO_TYPE; ++j) {
		global *g = get_global(j);

		if (v.index == g->var_index) {
			bool found = false;
			for (size_t k = 0; k < globals->size; ++k) {
				if (globals->globals[k] == j) {
					found = true;

					if (read) {
						globals->readable[k] = true;
					}

					if (write) {
						globals->writable[k] = true;
					}

					break;
				}
			}
			if (!found) {
				globals->globals[globals->size]  = j;
				globals->readable[globals->size] = read;
				globals->writable[globals->size] = write;
				globals->size += 1;
			}
			return;
		}
	}
}

void find_referenced_globals(function *f, global_array *globals) {
	if (f->block == NULL) {
		return; // built-in
	}

	function *functions[256];
	size_t    functions_size = 0;

	functions[functions_size] = f;
	functions_size += 1;

	find_referenced_functions(f, functions, &functions_size);

	for (size_t l = 0; l < functions_size; ++l) {
		uint8_t *data = functions[l]->code.o;
		size_t   size = functions[l]->code.size;

		size_t index = 0;
		while (index < size) {
			opcode *o = (opcode *)&data[index];
			switch (o->type) {
			case OPCODE_MULTIPLY:
			case OPCODE_DIVIDE:
			case OPCODE_ADD:
			case OPCODE_SUB:
			case OPCODE_EQUALS:
			case OPCODE_NOT_EQUALS:
			case OPCODE_GREATER:
			case OPCODE_GREATER_EQUAL:
			case OPCODE_LESS:
			case OPCODE_LESS_EQUAL: {
				find_referenced_global_for_var(o->op_binary.left, globals, false, false);
				find_referenced_global_for_var(o->op_binary.right, globals, false, false);
				break;
			}
			case OPCODE_LOAD_ACCESS_LIST: {
				find_referenced_global_for_var(o->op_load_access_list.from, globals, true, false);
				break;
			}
			case OPCODE_STORE_ACCESS_LIST:
			case OPCODE_SUB_AND_STORE_ACCESS_LIST:
			case OPCODE_ADD_AND_STORE_ACCESS_LIST:
			case OPCODE_DIVIDE_AND_STORE_ACCESS_LIST:
			case OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST: {
				find_referenced_global_for_var(o->op_store_access_list.to, globals, false, true);
				break;
			}
			case OPCODE_CALL: {
				for (uint8_t i = 0; i < o->op_call.parameters_size; ++i) {
					find_referenced_global_for_var(o->op_call.parameters[i], globals, false, false);
				}
				break;
			}
			default:
				break;
			}

			index += o->size;
		}
	}
}

void find_referenced_functions(function *f, function **functions, size_t *functions_size) {
	if (f->block == NULL) {
		return; // built-in
	}

	uint8_t *data = f->code.o;
	size_t   size = f->code.size;

	size_t index = 0;
	while (index < size) {
		opcode *o = (opcode *)&data[index];
		switch (o->type) {
		case OPCODE_CALL: {
			for (function_id i = 0; get_function(i) != NULL; ++i) {
				function *f = get_function(i);
				if (f->name == o->op_call.func) {
					if (f->block == NULL) {
						break; // built-in
					}

					bool found = false;
					for (size_t j = 0; j < *functions_size; ++j) {
						if (functions[j]->name == o->op_call.func) {
							found = true;
							break;
						}
					}
					if (!found) {
						functions[*functions_size] = f;
						*functions_size += 1;
						find_referenced_functions(f, functions, functions_size);
					}
					break;
				}
			}
			break;
		}
		default:
			break;
		}

		index += o->size;
	}
}

void find_used_builtins(function *f) {
	if (f->block == NULL) {
		return; // built-in
	}

	if (f->used_builtins.builtins_analyzed) {
		return;
	}

	f->used_builtins.builtins_analyzed = true;

	uint8_t *data = f->code.o;
	size_t   size = f->code.size;

	size_t index = 0;
	while (index < size) {
		opcode *o = (opcode *)&data[index];
		switch (o->type) {
		case OPCODE_CALL: {
			name_id func = o->op_call.func;

			if (func == add_name("dispatch_thread_id")) {
				f->used_builtins.dispatch_thread_id = true;
			}

			if (func == add_name("group_thread_id")) {
				f->used_builtins.group_thread_id = true;
			}

			if (func == add_name("group_id")) {
				f->used_builtins.group_id = true;
			}

			if (func == add_name("vertex_id")) {
				f->used_builtins.vertex_id = true;
			}

			for (function_id i = 0; get_function(i) != NULL; ++i) {
				function *called = get_function(i);
				if (called->name == o->op_call.func) {
					find_used_builtins(f);

					f->used_builtins.dispatch_thread_id |= called->used_builtins.dispatch_thread_id;
					f->used_builtins.group_thread_id |= called->used_builtins.group_thread_id;
					f->used_builtins.group_id |= called->used_builtins.group_id;
					f->used_builtins.vertex_id |= called->used_builtins.vertex_id;

					break;
				}
			}
			break;
		}
		default:
			break;
		}

		index += o->size;
	}
}

static global *find_global_by_var(variable var) {
	for (global_id global_index = 0; get_global(global_index) != NULL && get_global(global_index)->type != NO_TYPE; ++global_index) {
		if (var.index == get_global(global_index)->var_index) {
			return get_global(global_index);
		}
	}

	return NULL;
}

void find_used_capabilities(function *f) {
	if (f->block == NULL) {
		return; // built-in
	}

	if (f->used_capabilities.capabilities_analyzed) {
		return;
	}

	f->used_capabilities.capabilities_analyzed = true;

	uint8_t *data = f->code.o;
	size_t   size = f->code.size;

	size_t   index                  = 0;
	variable last_base_texture_from = {0};
	variable last_base_texture_to   = {0};

	while (index < size) {
		opcode *o = (opcode *)&data[index];
		switch (o->type) {
		case OPCODE_STORE_ACCESS_LIST: {
			variable to      = o->op_store_access_list.to;
			type_id  to_type = to.type.type;

			if (is_texture(to_type)) {
				assert(get_type(to_type)->array_size == 0);

				f->used_capabilities.image_write = true;

				global *g = find_global_by_var(to);
				assert(g != NULL);
			}
			break;
		}
		case OPCODE_LOAD_ACCESS_LIST: {
			variable from      = o->op_load_access_list.from;
			type_id  from_type = from.type.type;

			if (is_texture(from_type)) {
				f->used_capabilities.image_read = true;

				if (get_type(from_type)->array_size > 0) {
					last_base_texture_from = from;
					last_base_texture_to   = o->op_load_access_list.to;
				}
				else {
					global *g = find_global_by_var(from);
					assert(g != NULL);
				}
			}

			break;
		}
		case OPCODE_CALL: {
			name_id func_name = o->op_call.func;

			if (func_name == add_name("sample") || func_name == add_name("sample_lod")) {
				variable tex_parameter = o->op_call.parameters[0];

				global *g = NULL;

				if (tex_parameter.kind == VARIABLE_INTERNAL) {
					assert(last_base_texture_to.index == tex_parameter.index);
					g = find_global_by_var(last_base_texture_from);
				}
				else {
					g = find_global_by_var(tex_parameter);
				}

				assert(g != NULL);
			}

			for (function_id i = 0; get_function(i) != NULL; ++i) {
				function *called = get_function(i);
				if (called->name == func_name) {
					find_used_capabilities(f);

					f->used_capabilities.image_read |= called->used_capabilities.image_read;
					f->used_capabilities.image_write |= called->used_capabilities.image_write;

					break;
				}
			}
			break;
		}
		default:
			break;
		}

		index += o->size;
	}
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

void find_referenced_types(function *f, type_id *types, size_t *types_size) {
	if (f->block == NULL) {
		return; // built-in
	}

	function *functions[256];
	size_t    functions_size = 0;

	functions[functions_size] = f;
	functions_size += 1;

	find_referenced_functions(f, functions, &functions_size);

	for (size_t function_index = 0; function_index < functions_size; ++function_index) {
		function     *func    = functions[function_index];
		debug_context context = {0};
		for (uint8_t parameter_index = 0; parameter_index < func->parameters_size; ++parameter_index) {
			check(func->parameter_types[parameter_index].type != NO_TYPE, context, "Function parameter type not found");
			add_found_type(func->parameter_types[parameter_index].type, types, types_size);
		}
		check(func->return_type.type != NO_TYPE, context, "Function return type missing");
		add_found_type(func->return_type.type, types, types_size);

		uint8_t *data = functions[function_index]->code.o;
		size_t   size = functions[function_index]->code.size;

		size_t index = 0;
		while (index < size) {
			opcode *o = (opcode *)&data[index];
			switch (o->type) {
			case OPCODE_VAR:
				add_found_type(o->op_var.var.type.type, types, types_size);
				break;
			default:
				break;
			}

			index += o->size;
		}
	}
}

static bool has_set(descriptor_sets *sets, descriptor_set *set) {
	for (size_t set_index = 0; set_index < sets->size; ++set_index) {
		if (sets->values[set_index] == set) {
			return true;
		}
	}

	return false;
}

static void add_set(descriptor_sets *sets, descriptor_set *set) {
	if (has_set(sets, set)) {
		return;
	}

	static_array_push_p(sets, set);
}

static void find_referenced_sets(global_array *globals, descriptor_sets *sets) {
	for (size_t global_index = 0; global_index < globals->size; ++global_index) {
		global *g = get_global(globals->globals[global_index]);

		if (g->sets_count == 0) {
			continue;
		}

		if (g->sets_count == 1) {
			add_set(sets, g->sets[0]);
			continue;
		}
	}

	for (size_t global_index = 0; global_index < globals->size; ++global_index) {
		global *g = get_global(globals->globals[global_index]);

		if (g->sets_count < 2) {
			continue;
		}

		bool found = false;

		for (size_t set_index = 0; set_index < g->sets_count; ++set_index) {
			descriptor_set *set = g->sets[set_index];

			if (has_set(sets, set)) {
				found = true;
				break;
			}
		}

		if (!found) {
			debug_context context = {0};
			error(context, "Global %s could be used from multiple descriptor sets.", get_name(g->name));
		}
	}
}

static render_pipeline extract_render_pipeline_from_type(type *t) {
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

	render_pipeline pipeline = {0};

	for (function_id i = 0; get_function(i) != NULL; ++i) {
		function *f = get_function(i);
		if (vertex_shader_name != NO_NAME && f->name == vertex_shader_name) {
			pipeline.vertex_shader = f;
		}
		if (f->name == fragment_shader_name) {
			pipeline.fragment_shader = f;
		}
	}

	return pipeline;
}

static void find_all_render_pipelines(void) {
	static_array_init(all_render_pipelines);

	for (type_id i = 0; get_type(i) != NULL; ++i) {
		type *t = get_type(i);
		if (!t->built_in && has_attribute(&t->attributes, add_name("pipe"))) {
			static_array_push(all_render_pipelines, extract_render_pipeline_from_type(t));
		}
	}
}

static bool same_shader(function *a, function *b) {
	if (a == NULL && b == NULL) {
		return false;
	}

	return a == b;
}

static void find_render_pipeline_groups(void) {
	static_array_init(all_render_pipeline_groups);

	render_pipeline_indices remaining_pipelines;
	static_array_init(remaining_pipelines);

	for (uint32_t index = 0; index < all_render_pipelines.size; ++index) {
		static_array_push(remaining_pipelines, index);
	}

	while (remaining_pipelines.size > 0) {
		render_pipeline_indices next_remaining_pipelines;
		static_array_init(next_remaining_pipelines);

		render_pipeline_group group;
		static_array_init(group);

		static_array_push(group, remaining_pipelines.values[0]);

		for (size_t index = 1; index < remaining_pipelines.size; ++index) {
			uint32_t         pipeline_index = remaining_pipelines.values[index];
			render_pipeline *pipeline       = &all_render_pipelines.values[pipeline_index];

			bool found = false;

			for (size_t index_in_bucket = 0; index_in_bucket < group.size; ++index_in_bucket) {
				render_pipeline *pipeline_in_group = &all_render_pipelines.values[group.values[index_in_bucket]];
				if (same_shader(pipeline->vertex_shader, pipeline_in_group->vertex_shader) ||
				    same_shader(pipeline->fragment_shader, pipeline_in_group->fragment_shader)) {
					found = true;
					break;
				}
			}

			if (found) {
				static_array_push(group, pipeline_index);
			}
			else {
				static_array_push(next_remaining_pipelines, pipeline_index);
			}
		}

		remaining_pipelines = next_remaining_pipelines;
		static_array_push(all_render_pipeline_groups, group);
	}
}

static void find_all_compute_shaders(void) {
	static_array_init(all_compute_shaders);

	for (function_id i = 0; get_function(i) != NULL; ++i) {
		function *f = get_function(i);
		if (has_attribute(&f->attributes, add_name("compute"))) {
			static_array_push(all_compute_shaders, f);
		}
	}
}

static void check_globals_in_descriptor_set_group(descriptor_set_group *group) {
	static_array(global_id, globals, 256);

	globals set_globals;
	static_array_init(set_globals);

	for (size_t set_index = 0; set_index < group->size; ++set_index) {
		descriptor_set *set = group->values[set_index];
		for (size_t global_index = 0; global_index < set->globals.size; ++global_index) {
			global_id g = set->globals.globals[global_index];

			for (size_t global_index2 = 0; global_index2 < set_globals.size; ++global_index2) {
				if (set_globals.values[global_index2] == g) {
					debug_context context = {0};
					error(context, "Global used from more than one descriptor set in one descriptor set group");
				}
			}

			static_array_push(set_globals, g);
		}
	}
}

static void update_globals_in_descriptor_set_group(descriptor_set_group *group, global_array *globals) {
	for (size_t set_index = 0; set_index < group->size; ++set_index) {
		descriptor_set *set = group->values[set_index];

		for (size_t global_index = 0; global_index < set->globals.size; ++global_index) {
			global_id g = set->globals.globals[global_index];

			for (size_t global_index2 = 0; global_index2 < globals->size; ++global_index2) {
				if (globals->globals[global_index2] == g) {
					if (globals->readable[global_index2]) {
						set->globals.readable[global_index] = true;
					}
					if (globals->writable[global_index2]) {
						set->globals.writable[global_index] = true;
					}
				}
			}
		}
	}
}

descriptor_set_group *get_descriptor_set_group(uint32_t descriptor_set_group_index) {
	assert(descriptor_set_group_index < all_descriptor_set_groups.size);
	return &all_descriptor_set_groups.values[descriptor_set_group_index];
}

static void assign_descriptor_set_group_index(function *f, uint32_t descriptor_set_group_index) {
	assert(f->descriptor_set_group_index == UINT32_MAX || f->descriptor_set_group_index == descriptor_set_group_index);
	f->descriptor_set_group_index = descriptor_set_group_index;
}

static void find_descriptor_set_groups(void) {
	static_array_init(all_descriptor_set_groups);

	for (size_t pipeline_group_index = 0; pipeline_group_index < all_render_pipeline_groups.size; ++pipeline_group_index) {
		descriptor_set_group group;
		static_array_init(group);

		global_array function_globals = {0};

		render_pipeline_group *pipeline_group = &all_render_pipeline_groups.values[pipeline_group_index];
		for (size_t pipeline_index = 0; pipeline_index < pipeline_group->size; ++pipeline_index) {
			render_pipeline *pipeline = &all_render_pipelines.values[pipeline_group->values[pipeline_index]];

			if (pipeline->vertex_shader != NULL) {
				find_referenced_globals(pipeline->vertex_shader, &function_globals);
			}
			if (pipeline->fragment_shader != NULL) {
				find_referenced_globals(pipeline->fragment_shader, &function_globals);
			}
		}

		find_referenced_sets(&function_globals, &group);

		check_globals_in_descriptor_set_group(&group);

		update_globals_in_descriptor_set_group(&group, &function_globals);

		uint32_t descriptor_set_group_index = (uint32_t)all_descriptor_set_groups.size;
		static_array_push(all_descriptor_set_groups, group);

		for (size_t pipeline_index = 0; pipeline_index < pipeline_group->size; ++pipeline_index) {
			render_pipeline *pipeline = &all_render_pipelines.values[pipeline_group->values[pipeline_index]];

			if (pipeline->vertex_shader != NULL) {
				assign_descriptor_set_group_index(pipeline->vertex_shader, descriptor_set_group_index);
			}
			if (pipeline->fragment_shader != NULL) {
				assign_descriptor_set_group_index(pipeline->fragment_shader, descriptor_set_group_index);
			}
		}
	}

	for (size_t compute_shader_index = 0; compute_shader_index < all_compute_shaders.size; ++compute_shader_index) {
		descriptor_set_group group;
		static_array_init(group);

		global_array function_globals = {0};

		find_referenced_globals(all_compute_shaders.values[compute_shader_index], &function_globals);

		find_referenced_sets(&function_globals, &group);

		check_globals_in_descriptor_set_group(&group);

		update_globals_in_descriptor_set_group(&group, &function_globals);

		uint32_t descriptor_set_group_index = (uint32_t)all_descriptor_set_groups.size;
		static_array_push(all_descriptor_set_groups, group);

		for (size_t compute_shader_index = 0; compute_shader_index < all_compute_shaders.size; ++compute_shader_index) {
			assign_descriptor_set_group_index(all_compute_shaders.values[compute_shader_index], descriptor_set_group_index);
		}
	}
}

descriptor_set_group *find_descriptor_set_group_for_pipe_type(type *t) {
	if (!t->built_in && has_attribute(&t->attributes, add_name("pipe"))) {
		render_pipeline pipeline = extract_render_pipeline_from_type(t);

		if (pipeline.vertex_shader->descriptor_set_group_index != UINT32_MAX) {
			return &all_descriptor_set_groups.values[pipeline.vertex_shader->descriptor_set_group_index];
		}

		if (pipeline.fragment_shader->descriptor_set_group_index != UINT32_MAX) {
			return &all_descriptor_set_groups.values[pipeline.fragment_shader->descriptor_set_group_index];
		}

		return NULL;
	}

	return NULL;
}

descriptor_set_group *find_descriptor_set_group_for_function(function *f) {
	if (f->descriptor_set_group_index != UINT32_MAX) {
		return &all_descriptor_set_groups.values[f->descriptor_set_group_index];
	}
	else {
		return NULL;
	}
}

void analyze(void) {
	find_all_render_pipelines();
	find_render_pipeline_groups();
	find_all_compute_shaders();
	find_descriptor_set_groups();
}

allocated_global find_allocated_global(name_id name) {
	for (size_t i = 0; i < allocated_globals_size; ++i) {
		if (name == allocated_globals[i].g->name) {
			return allocated_globals[i];
		}
	}

	allocated_global a;
	a.g           = NULL;
	a.variable_id = 0;
	return a;
}

variable find_local_var(block *b, name_id name) {
	if (b == NULL) {
		variable var;
		var.index = 0;
		init_type_ref(&var.type, NO_NAME);
		return var;
	}

	for (size_t i = 0; i < b->vars.size; ++i) {
		if (b->vars.v[i].name == name) {
			debug_context context = {0};
			check(b->vars.v[i].type.type != NO_TYPE, context, "Local variable does not have a type");
			variable var;
			var.index = b->vars.v[i].variable_id;
			var.type  = b->vars.v[i].type;
			var.kind  = VARIABLE_LOCAL;
			return var;
		}
	}

	return find_local_var(b->parent, name);
}

variable find_variable(block *parent, name_id name) {
	variable local_var = find_local_var(parent, name);
	if (local_var.index == 0) {
		allocated_global global = find_allocated_global(name);
		if (global.g->type != NO_TYPE && global.variable_id != 0) {
			variable v;
			init_type_ref(&v.type, NO_NAME);
			v.type.type = global.g->type;
			v.index     = global.variable_id;
			v.kind      = VARIABLE_GLOBAL;
			return v;
		}
		else {
			debug_context context = {0};
			error(context, "Variable %s not found", get_name(name));

			variable v;
			v.index = 0;
			return v;
		}
	}
	else {
		return local_var;
	}
}

variable allocate_variable(type_ref type, variable_kind kind) {
	variable v;
	v.index                = next_variable_id;
	v.type                 = type;
	v.kind                 = kind;
	all_variables[v.index] = v;
	++next_variable_id;
	return v;
}

opcode *emit_op(opcodes *code, opcode *o) {
	assert(code->size + o->size < OPCODES_SIZE);

	if (code->o == NULL) {
		code->o = (uint8_t *)malloc(OPCODES_SIZE);
	}

	uint8_t *location = &code->o[code->size];

	memcpy(&code->o[code->size], o, o->size);

	code->size += o->size;

	return (opcode *)location;
}

variable emit_expression(opcodes *code, block *parent, expression *e) {
	switch (e->kind) {
	case EXPRESSION_BINARY: {
		expression *left  = e->binary.left;
		expression *right = e->binary.right;

		debug_context context = {0};

		switch (e->binary.op) {
		case OPERATOR_EQUALS:
		case OPERATOR_NOT_EQUALS:
		case OPERATOR_GREATER:
		case OPERATOR_GREATER_EQUAL:
		case OPERATOR_LESS:
		case OPERATOR_LESS_EQUAL:
		case OPERATOR_AND:
		case OPERATOR_OR: {
			variable right_var = emit_expression(code, parent, right);
			variable left_var  = emit_expression(code, parent, left);
			type_ref t;
			init_type_ref(&t, NO_NAME);
			t.type              = bool_id;
			variable result_var = allocate_variable(t, VARIABLE_INTERNAL);

			opcode o;
			switch (e->binary.op) {
			case OPERATOR_EQUALS:
				o.type = OPCODE_EQUALS;
				break;
			case OPERATOR_NOT_EQUALS:
				o.type = OPCODE_NOT_EQUALS;
				break;
			case OPERATOR_GREATER:
				o.type = OPCODE_GREATER;
				break;
			case OPERATOR_GREATER_EQUAL:
				o.type = OPCODE_GREATER_EQUAL;
				break;
			case OPERATOR_LESS:
				o.type = OPCODE_LESS;
				break;
			case OPERATOR_LESS_EQUAL:
				o.type = OPCODE_LESS_EQUAL;
				break;
			case OPERATOR_AND:
				o.type = OPCODE_AND;
				break;
			case OPERATOR_OR:
				o.type = OPCODE_OR;
				break;
			default: {
				error(context, "Unexpected operator");
			}
			}
			o.size             = OP_SIZE(o, op_binary);
			o.op_binary.right  = right_var;
			o.op_binary.left   = left_var;
			o.op_binary.result = result_var;
			emit_op(code, &o);

			return result_var;
		}
		case OPERATOR_MINUS:
		case OPERATOR_PLUS:
		case OPERATOR_DIVIDE:
		case OPERATOR_MULTIPLY:
		case OPERATOR_MOD:
		case OPERATOR_BITWISE_XOR:
		case OPERATOR_BITWISE_AND:
		case OPERATOR_BITWISE_OR:
		case OPERATOR_LEFT_SHIFT:
		case OPERATOR_RIGHT_SHIFT: {
			variable right_var  = emit_expression(code, parent, right);
			variable left_var   = emit_expression(code, parent, left);
			variable result_var = allocate_variable(e->type, VARIABLE_INTERNAL);

			opcode o;
			switch (e->binary.op) {
			case OPERATOR_MINUS:
				o.type = OPCODE_SUB;
				break;
			case OPERATOR_PLUS:
				o.type = OPCODE_ADD;
				break;
			case OPERATOR_DIVIDE:
				o.type = OPCODE_DIVIDE;
				break;
			case OPERATOR_MULTIPLY:
				o.type = OPCODE_MULTIPLY;
				break;
			case OPERATOR_MOD:
				o.type = OPCODE_MOD;
				break;
			case OPERATOR_BITWISE_XOR:
				o.type = OPCODE_BITWISE_XOR;
				break;
			case OPERATOR_BITWISE_AND:
				o.type = OPCODE_BITWISE_AND;
				break;
			case OPERATOR_BITWISE_OR:
				o.type = OPCODE_BITWISE_OR;
				break;
			case OPERATOR_LEFT_SHIFT:
				o.type = OPCODE_LEFT_SHIFT;
				break;
			case OPERATOR_RIGHT_SHIFT:
				o.type = OPCODE_RIGHT_SHIFT;
				break;
			default: {
				error(context, "Unexpected operator");
			}
			}
			o.size             = OP_SIZE(o, op_binary);
			o.op_binary.right  = right_var;
			o.op_binary.left   = left_var;
			o.op_binary.result = result_var;
			emit_op(code, &o);

			return result_var;
		}
		case OPERATOR_NOT: {
			error(context, "! is not a binary operator");
		}
		case OPERATOR_ASSIGN:
		case OPERATOR_MINUS_ASSIGN:
		case OPERATOR_PLUS_ASSIGN:
		case OPERATOR_DIVIDE_ASSIGN:
		case OPERATOR_MULTIPLY_ASSIGN: {
			variable v = emit_expression(code, parent, right);

			switch (left->kind) {
			case EXPRESSION_VARIABLE: {
				opcode o;
				switch (e->binary.op) {
				case OPERATOR_ASSIGN:
					o.type = OPCODE_STORE_VARIABLE;
					break;
				case OPERATOR_MINUS_ASSIGN:
					o.type = OPCODE_SUB_AND_STORE_VARIABLE;
					break;
				case OPERATOR_PLUS_ASSIGN:
					o.type = OPCODE_ADD_AND_STORE_VARIABLE;
					break;
				case OPERATOR_DIVIDE_ASSIGN:
					o.type = OPCODE_DIVIDE_AND_STORE_VARIABLE;
					break;
				case OPERATOR_MULTIPLY_ASSIGN:
					o.type = OPCODE_MULTIPLY_AND_STORE_VARIABLE;
					break;
				default: {
					error(context, "Unexpected operator");
				}
				}
				o.size              = OP_SIZE(o, op_store_var);
				o.op_store_var.from = v;
				o.op_store_var.to   = find_variable(parent, left->variable);
				emit_op(code, &o);
				break;
			}
			case EXPRESSION_ELEMENT:
			case EXPRESSION_MEMBER:
			case EXPRESSION_SWIZZLE: {
				opcode o;
				switch (e->binary.op) {
				case OPERATOR_ASSIGN:
					o.type = OPCODE_STORE_ACCESS_LIST;
					break;
				case OPERATOR_MINUS_ASSIGN:
					o.type = OPCODE_SUB_AND_STORE_ACCESS_LIST;
					break;
				case OPERATOR_PLUS_ASSIGN:
					o.type = OPCODE_ADD_AND_STORE_ACCESS_LIST;
					break;
				case OPERATOR_DIVIDE_ASSIGN:
					o.type = OPCODE_DIVIDE_AND_STORE_ACCESS_LIST;
					break;
				case OPERATOR_MULTIPLY_ASSIGN:
					o.type = OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST;
					break;
				default: {
					error(context, "Unexpected operator");
				}
				}
				o.size                      = OP_SIZE(o, op_store_access_list);
				o.op_store_access_list.from = v;

				expression *of = left;

				kong_access access_list[64];
				uint32_t    access_list_size = 0;

				while (of->kind == EXPRESSION_ELEMENT || of->kind == EXPRESSION_MEMBER || of->kind == EXPRESSION_SWIZZLE) {
					kong_access *a = &access_list[access_list_size];
					a->type        = of->type.type;

					switch (of->kind) {
					case EXPRESSION_ELEMENT:
						a->kind                 = ACCESS_ELEMENT;
						a->access_element.index = emit_expression(code, parent, of->element.element_index);

						of = of->element.of;

						break;
					case EXPRESSION_MEMBER:
						a->kind               = ACCESS_MEMBER;
						a->access_member.name = of->member.member_name;

						of = of->member.of;

						break;
					case EXPRESSION_SWIZZLE:
						a->kind                   = ACCESS_SWIZZLE;
						a->access_swizzle.swizzle = of->swizzle.swizz;

						of = of->swizzle.of;

						break;
					default:
						assert(false);
						break;
					}

					access_list_size += 1;
				}

				o.op_store_access_list.access_list_size = access_list_size;

				for (uint32_t access_index = 0; access_index < access_list_size; ++access_index) {
					o.op_store_access_list.access_list[access_list_size - access_index - 1] = access_list[access_index];
				}

				o.op_store_access_list.to = emit_expression(code, parent, of);

				emit_op(code, &o);
				break;
			}
			default: {
				debug_context context = {0};
				error(context, "Expected a variable or an access");
			}
			}

			return v;
		}
		}
		break;
	}
	case EXPRESSION_UNARY: {
		debug_context context = {0};
		switch (e->unary.op) {
		case OPERATOR_EQUALS:
			error(context, "not implemented");
		case OPERATOR_NOT_EQUALS:
			error(context, "not implemented");
		case OPERATOR_GREATER:
			error(context, "not implemented");
		case OPERATOR_GREATER_EQUAL:
			error(context, "not implemented");
		case OPERATOR_LESS:
			error(context, "not implemented");
		case OPERATOR_LESS_EQUAL:
			error(context, "not implemented");
		case OPERATOR_MINUS: {
			variable v = emit_expression(code, parent, e->unary.right);
			opcode   o;
			o.type           = OPCODE_NEGATE;
			o.size           = OP_SIZE(o, op_negate);
			o.op_negate.from = v;
			o.op_negate.to   = allocate_variable(v.type, VARIABLE_INTERNAL);
			emit_op(code, &o);
			return o.op_negate.to;
		}
		case OPERATOR_PLUS:
			error(context, "not implemented");
		case OPERATOR_DIVIDE:
			error(context, "not implemented");
		case OPERATOR_MULTIPLY:
			error(context, "not implemented");
		case OPERATOR_NOT: {
			variable v = emit_expression(code, parent, e->unary.right);
			opcode   o;
			o.type        = OPCODE_NOT;
			o.size        = OP_SIZE(o, op_not);
			o.op_not.from = v;
			o.op_not.to   = allocate_variable(v.type, VARIABLE_INTERNAL);
			emit_op(code, &o);
			return o.op_not.to;
		}
		case OPERATOR_OR:
			error(context, "not implemented");
		case OPERATOR_BITWISE_XOR:
			error(context, "not implemented");
		case OPERATOR_BITWISE_AND:
			error(context, "not implemented");
		case OPERATOR_BITWISE_OR:
			error(context, "not implemented");
		case OPERATOR_LEFT_SHIFT:
			error(context, "not implemented");
		case OPERATOR_RIGHT_SHIFT:
			error(context, "not implemented");
		case OPERATOR_AND:
			error(context, "not implemented");
		case OPERATOR_MOD:
			error(context, "not implemented");
		case OPERATOR_ASSIGN:
			error(context, "not implemented");
		case OPERATOR_PLUS_ASSIGN:
		case OPERATOR_MINUS_ASSIGN:
		case OPERATOR_MULTIPLY_ASSIGN:
		case OPERATOR_DIVIDE_ASSIGN:
			error(context, "not implemented");
		}
	}
	case EXPRESSION_BOOLEAN: {
		type_ref t;
		init_type_ref(&t, NO_NAME);
		t.type     = float_id;
		variable v = allocate_variable(t, VARIABLE_INTERNAL);

		opcode o;
		o.type                          = OPCODE_LOAD_BOOL_CONSTANT;
		o.size                          = OP_SIZE(o, op_load_bool_constant);
		o.op_load_bool_constant.boolean = e->boolean;
		o.op_load_bool_constant.to      = v;
		emit_op(code, &o);

		return v;
	}
	case EXPRESSION_FLOAT: {
		type_ref t;
		init_type_ref(&t, NO_NAME);
		t.type     = float_id;
		variable v = allocate_variable(t, VARIABLE_INTERNAL);

		opcode o;
		o.type                          = OPCODE_LOAD_FLOAT_CONSTANT;
		o.size                          = OP_SIZE(o, op_load_float_constant);
		o.op_load_float_constant.number = (float)e->number;
		o.op_load_float_constant.to     = v;
		emit_op(code, &o);

		return v;
	}
	case EXPRESSION_INT: {
		type_ref t;
		init_type_ref(&t, NO_NAME);
		t.type     = int_id;
		variable v = allocate_variable(t, VARIABLE_INTERNAL);

		opcode o;
		o.type                        = OPCODE_LOAD_INT_CONSTANT;
		o.size                        = OP_SIZE(o, op_load_float_constant);
		o.op_load_int_constant.number = (int)e->number;
		o.op_load_int_constant.to     = v;
		emit_op(code, &o);

		return v;
	}
	case EXPRESSION_VARIABLE: {
		return find_variable(parent, e->variable);
	}
	case EXPRESSION_GROUPING: {
		return emit_expression(code, parent, e->grouping);
	}
	case EXPRESSION_CALL: {
		type_ref t;
		init_type_ref(&t, NO_NAME);
		t.type     = e->type.type;
		variable v = allocate_variable(t, VARIABLE_INTERNAL);

		opcode o;
		o.type         = OPCODE_CALL;
		o.size         = OP_SIZE(o, op_call);
		o.op_call.func = e->call.func_name;
		o.op_call.var  = v;

		debug_context context = {0};
		check(e->call.parameters.size <= sizeof(o.op_call.parameters) / sizeof(variable), context, "Call parameters missized");
		for (size_t i = 0; i < e->call.parameters.size; ++i) {
			o.op_call.parameters[i] = emit_expression(code, parent, e->call.parameters.e[i]);
		}
		o.op_call.parameters_size = (uint8_t)e->call.parameters.size;

		emit_op(code, &o);

		return v;
	}
	case EXPRESSION_ELEMENT:
	case EXPRESSION_MEMBER:
	case EXPRESSION_SWIZZLE: {
		opcode o;
		o.type = OPCODE_LOAD_ACCESS_LIST;
		o.size = OP_SIZE(o, op_load_access_list);

		variable v               = allocate_variable(e->type, VARIABLE_INTERNAL);
		o.op_load_access_list.to = v;

		expression *of = e;

		kong_access access_list[64];
		uint32_t    access_list_size = 0;

		while (of->kind == EXPRESSION_ELEMENT || of->kind == EXPRESSION_MEMBER || of->kind == EXPRESSION_SWIZZLE) {
			kong_access *a = &access_list[access_list_size];
			a->type        = of->type.type;

			switch (of->kind) {
			case EXPRESSION_ELEMENT:
				a->kind                 = ACCESS_ELEMENT;
				a->access_element.index = emit_expression(code, parent, of->element.element_index);

				of = of->element.of;

				break;
			case EXPRESSION_MEMBER:
				a->kind               = ACCESS_MEMBER;
				a->access_member.name = of->member.member_name;

				of = of->member.of;

				break;
			case EXPRESSION_SWIZZLE:
				a->kind                   = ACCESS_SWIZZLE;
				a->access_swizzle.swizzle = of->swizzle.swizz;

				of = of->swizzle.of;

				break;
			default:
				assert(false);
				break;
			}

			access_list_size += 1;
		}

		o.op_load_access_list.access_list_size = access_list_size;

		for (uint32_t access_index = 0; access_index < access_list_size; ++access_index) {
			o.op_load_access_list.access_list[access_list_size - access_index - 1] = access_list[access_index];
		}

		o.op_load_access_list.from = emit_expression(code, parent, of);

		emit_op(code, &o);

		return v;
	}
	default: {
		debug_context context = {0};
		error(context, "not implemented");
	}
	}

	{
		debug_context context = {0};
		error(context, "Supposedly unreachable code reached");
		variable v;
		v.index = 0;
		return v;
	}
}

typedef struct block_ids {
	uint64_t start;
	uint64_t end;
} block_ids;

static block_ids emit_statement(opcodes *code, block *parent, statement *statement) {
	switch (statement->kind) {
	case STATEMENT_EXPRESSION:
		emit_expression(code, parent, statement->expression);
		break;
	case STATEMENT_RETURN_EXPRESSION: {
		opcode o;
		o.type     = OPCODE_RETURN;
		variable v = emit_expression(code, parent, statement->expression);
		if (v.index == 0) {
			o.size = offsetof(opcode, op_return);
		}
		else {
			o.size          = OP_SIZE(o, op_return);
			o.op_return.var = v;
		}
		emit_op(code, &o);
		break;
	}
	case STATEMENT_DISCARD: {
		opcode o;
		o.type = OPCODE_DISCARD;
		o.size = offsetof(opcode, op_nothing);

		emit_op(code, &o);
		break;
	}
	case STATEMENT_IF: {
		struct previous_condition {
			variable condition;
			variable summed_condition;
		};
		struct previous_condition previous_conditions[64]  = {0};
		uint8_t                   previous_conditions_size = 0;

		{
			opcode o;
			o.type = OPCODE_IF;
			o.size = OP_SIZE(o, op_if);

			variable initial_condition = emit_expression(code, parent, statement->iffy.test);

			o.op_if.condition = initial_condition;

			opcode *written_opcode = emit_op(code, &o);

			previous_conditions[previous_conditions_size].condition = initial_condition;
			previous_conditions_size += 1;

			block_ids ids = emit_statement(code, parent, statement->iffy.if_block);

			written_opcode->op_if.start_id = ids.start;
			written_opcode->op_if.end_id   = ids.end;
		}

		for (uint16_t i = 0; i < statement->iffy.else_size; ++i) {
			variable current_condition;
			{
				opcode o;
				o.type        = OPCODE_NOT;
				o.size        = OP_SIZE(o, op_not);
				o.op_not.from = previous_conditions[previous_conditions_size - 1].condition;
				type_ref t;
				init_type_ref(&t, NO_NAME);
				t.type            = bool_id;
				current_condition = allocate_variable(t, VARIABLE_INTERNAL);
				o.op_not.to       = current_condition;
				emit_op(code, &o);
			}

			variable summed_condition;
			if (previous_conditions_size == 1) {
				summed_condition = previous_conditions[0].summed_condition = current_condition;
			}
			else {
				opcode o;
				o.type            = OPCODE_AND;
				o.size            = OP_SIZE(o, op_binary);
				o.op_binary.left  = previous_conditions[previous_conditions_size - 2].summed_condition;
				o.op_binary.right = current_condition;
				type_ref t;
				init_type_ref(&t, NO_NAME);
				t.type             = bool_id;
				summed_condition   = allocate_variable(t, VARIABLE_INTERNAL);
				o.op_binary.result = summed_condition;
				emit_op(code, &o);
				previous_conditions[previous_conditions_size - 1].summed_condition = summed_condition;
			}

			opcode o;
			o.type = OPCODE_IF;
			o.size = OP_SIZE(o, op_if);

			if (statement->iffy.else_tests[i] != NULL) {
				variable v = emit_expression(code, parent, statement->iffy.else_tests[i]);

				variable else_test;
				{
					opcode o;
					o.type            = OPCODE_AND;
					o.size            = OP_SIZE(o, op_binary);
					o.op_binary.left  = summed_condition;
					o.op_binary.right = v;
					type_ref t;
					init_type_ref(&t, NO_NAME);
					t.type             = bool_id;
					else_test          = allocate_variable(t, VARIABLE_INTERNAL);
					o.op_binary.result = else_test;
					emit_op(code, &o);
				}

				o.op_if.condition = else_test;

				previous_conditions[previous_conditions_size].condition = v;
				previous_conditions_size += 1;
			}
			else {
				o.op_if.condition = summed_condition;
			}

			{
				opcode *written_opcode = emit_op(code, &o);

				block_ids ids = emit_statement(code, parent, statement->iffy.else_blocks[i]);

				written_opcode->op_if.start_id = ids.start;
				written_opcode->op_if.end_id   = ids.end;
			}
		}

		break;
	}
	case STATEMENT_WHILE: {
		uint64_t start_id = next_variable_id;
		++next_variable_id;
		uint64_t continue_id = next_variable_id;
		++next_variable_id;
		uint64_t end_id = next_variable_id;
		++next_variable_id;

		{
			opcode o;
			o.type                       = OPCODE_WHILE_START;
			o.op_while_start.start_id    = start_id;
			o.op_while_start.continue_id = continue_id;
			o.op_while_start.end_id      = end_id;
			o.size                       = OP_SIZE(o, op_while_start);
			emit_op(code, &o);
		}

		{
			opcode o;
			o.type = OPCODE_WHILE_CONDITION;
			o.size = OP_SIZE(o, op_while);

			variable v = emit_expression(code, parent, statement->whiley.test);

			o.op_while.condition = v;
			o.op_while.end_id    = end_id;

			emit_op(code, &o);
		}

		emit_statement(code, parent, statement->whiley.while_block);

		{
			opcode o;
			o.type                     = OPCODE_WHILE_END;
			o.op_while_end.start_id    = start_id;
			o.op_while_end.continue_id = continue_id;
			o.op_while_end.end_id      = end_id;
			o.size                     = OP_SIZE(o, op_while_end);
			emit_op(code, &o);
		}

		break;
	}
	case STATEMENT_DO_WHILE: {
		uint64_t start_id = next_variable_id;
		++next_variable_id;
		uint64_t continue_id = next_variable_id;
		++next_variable_id;
		uint64_t end_id = next_variable_id;
		++next_variable_id;

		{
			opcode o;
			o.type                       = OPCODE_WHILE_START;
			o.op_while_start.start_id    = start_id;
			o.op_while_start.continue_id = continue_id;
			o.op_while_start.end_id      = end_id;
			o.size                       = OP_SIZE(o, op_while_start);
			emit_op(code, &o);
		}

		emit_statement(code, parent, statement->whiley.while_block);

		{
			opcode o;
			o.type = OPCODE_WHILE_CONDITION;
			o.size = OP_SIZE(o, op_while);

			variable v = emit_expression(code, parent, statement->whiley.test);

			o.op_while.condition = v;
			o.op_while.end_id    = end_id;

			emit_op(code, &o);
		}

		{
			opcode o;
			o.type                     = OPCODE_WHILE_END;
			o.op_while_end.start_id    = start_id;
			o.op_while_end.continue_id = continue_id;
			o.op_while_end.end_id      = end_id;
			o.size                     = OP_SIZE(o, op_while_end);
			emit_op(code, &o);
		}

		break;
	}
	case STATEMENT_BLOCK: {
		for (size_t i = 0; i < statement->block.vars.size; ++i) {
			variable var                           = allocate_variable(statement->block.vars.v[i].type, VARIABLE_LOCAL);
			statement->block.vars.v[i].variable_id = var.index;
		}

		uint64_t start_block_id = next_variable_id;
		++next_variable_id;

		uint64_t end_block_id = next_variable_id;
		++next_variable_id;

		{
			opcode o;
			o.type        = OPCODE_BLOCK_START;
			o.op_block.id = start_block_id;
			o.size        = OP_SIZE(o, op_block);
			emit_op(code, &o);
		}

		for (size_t i = 0; i < statement->block.statements.size; ++i) {
			emit_statement(code, &statement->block, statement->block.statements.s[i]);
		}

		{
			opcode o;
			o.type        = OPCODE_BLOCK_END;
			o.op_block.id = end_block_id;
			o.size        = OP_SIZE(o, op_block);
			emit_op(code, &o);
		}

		block_ids ids;
		ids.start = start_block_id;
		ids.end   = end_block_id;
		return ids;
	}
	case STATEMENT_LOCAL_VARIABLE: {
		opcode o;
		o.type = OPCODE_VAR;
		o.size = OP_SIZE(o, op_var);

		variable init_var = {0};
		if (statement->local_variable.init != NULL) {
			init_var = emit_expression(code, parent, statement->local_variable.init);
		}

		variable local_var                        = find_local_var(parent, statement->local_variable.var.name);
		statement->local_variable.var.variable_id = local_var.index;
		o.op_var.var.index                        = statement->local_variable.var.variable_id;
		debug_context context                     = {0};
		check(statement->local_variable.var.type.type != NO_TYPE, context, "Local var has no type");
		o.op_var.var.type = statement->local_variable.var.type;
		emit_op(code, &o);

		if (statement->local_variable.init != NULL) {
			opcode o;
			o.type = OPCODE_STORE_VARIABLE;
			o.size = OP_SIZE(o, op_store_var);

			o.op_store_var.from = init_var;
			o.op_store_var.to   = local_var;

			emit_op(code, &o);
		}

		break;
	}
	}

	block_ids ids;
	ids.start = 0;
	ids.end   = 0;
	return ids;
}

void allocate_globals(void) {
	for (global_id i = 0; get_global(i) != NULL && get_global(i)->type != NO_TYPE; ++i) {
		global *g = get_global(i);

		type_ref t;
		init_type_ref(&t, NO_NAME);
		t.type                                                = g->type;
		variable v                                            = allocate_variable(t, VARIABLE_GLOBAL);
		allocated_globals[allocated_globals_size].g           = g;
		allocated_globals[allocated_globals_size].variable_id = v.index;
		allocated_globals_size += 1;

		assign_global_var(i, v.index);
	}
}

void compile_function_block(opcodes *code, struct statement *block) {
	if (block == NULL) {
		return; // built-in
	}

	if (block->kind != STATEMENT_BLOCK) {
		debug_context context = {0};
		error(context, "Expected a block");
	}
	for (size_t i = 0; i < block->block.vars.size; ++i) {
		variable var                       = allocate_variable(block->block.vars.v[i].type, VARIABLE_LOCAL);
		block->block.vars.v[i].variable_id = var.index;
	}
	for (size_t i = 0; i < block->block.statements.size; ++i) {
		emit_statement(code, &block->block, block->block.statements.s[i]);
	}
}

void error_args(debug_context context, const char *message, va_list args) {
	char buffer[4096];

	if (context.filename != NULL) {
		sprintf(buffer, "In column %i at line %i in %s: ", context.column + 1, context.line + 1, context.filename);
	}
	else {
		sprintf(buffer, "In column %i at line %i: ", context.column + 1, context.line + 1);
	}

	strcat(buffer, message);

	iron_log_args(IRON_LOG_LEVEL_ERROR, buffer, args);
	kong_error = true;
}

void error_args_no_context(const char *message, va_list args) {
	iron_log_args(IRON_LOG_LEVEL_ERROR, message, args);
	kong_error = true;
}

void error(debug_context context, const char *message, ...) {
	va_list args;
	va_start(args, message);
	error_args(context, message, args);
	va_end(args);
}

void error_no_context(const char *message, ...) {
	va_list args;
	va_start(args, message);
	error_args_no_context(message, args);
	va_end(args);
}

void check_args(bool test, debug_context context, const char *message, va_list args) {
	if (!test) {
		error_args(context, message, args);
	}
}

void check_function(bool test, debug_context context, const char *message, ...) {
	if (!test) {
		va_list args;
		va_start(args, message);
		error_args(context, message, args);
		va_end(args);
	}
}

static void add_func_float2_float2(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float2"));
	f->return_type.type   = find_type_by_ref(&f->return_type);
	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float2"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);
	f->parameters_size         = 1;
	f->block                   = NULL;
}

static void add_func_float3_float3_float_float(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float3"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float3"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameter_names[1] = add_name("b");
	init_type_ref(&f->parameter_types[1], add_name("float"));
	f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

	f->parameter_names[2] = add_name("c");
	init_type_ref(&f->parameter_types[2], add_name("float"));
	f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

	f->parameters_size = 3;
	f->block           = NULL;
}

static void add_func_float3_float3_float3_float(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float3"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float3"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameter_names[1] = add_name("b");
	init_type_ref(&f->parameter_types[1], add_name("float3"));
	f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

	f->parameter_names[2] = add_name("c");
	init_type_ref(&f->parameter_types[2], add_name("float"));
	f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

	f->parameters_size = 3;
	f->block           = NULL;
}

static void add_func_float4_float4_float4_float(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float4"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float4"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameter_names[1] = add_name("b");
	init_type_ref(&f->parameter_types[1], add_name("float4"));
	f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

	f->parameter_names[2] = add_name("c");
	init_type_ref(&f->parameter_types[2], add_name("float"));
	f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

	f->parameters_size = 3;
	f->block           = NULL;
}

static void add_func_float3x3_float3x3(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float3x3"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float3x3"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameters_size = 1;
	f->block           = NULL;
}

////

static void add_func_int(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("int"));
	f->return_type.type = find_type_by_ref(&f->return_type);
	f->parameters_size  = 0;
	f->block            = NULL;
}

static void add_func_float3_float_float_float(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float3"));
	f->return_type.type   = find_type_by_ref(&f->return_type);
	f->parameter_names[0] = add_name("a");
	f->parameter_names[1] = add_name("b");
	f->parameter_names[2] = add_name("c");
	for (int i = 0; i < 3; ++i) {
		init_type_ref(&f->parameter_types[i], add_name("float"));
		f->parameter_types[i].type = find_type_by_ref(&f->parameter_types[i]);
	}
	f->parameters_size = 3;
	f->block           = NULL;
}

static void add_func_float(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float"));
	f->return_type.type = find_type_by_ref(&f->return_type);
	f->parameters_size  = 0;
	f->block            = NULL;
}

static void add_func_float3(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float3"));
	f->return_type.type = find_type_by_ref(&f->return_type);
	f->parameters_size  = 0;
	f->block            = NULL;
}

static void add_func_float3x3(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float3x3"));
	f->return_type.type = find_type_by_ref(&f->return_type);
	f->parameters_size  = 0;
	f->block            = NULL;
}

static void add_func_uint(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("uint"));
	f->return_type.type = find_type_by_ref(&f->return_type);
	f->parameters_size  = 0;
	f->block            = NULL;
}

static void add_func_uint3(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("uint3"));
	f->return_type.type = find_type_by_ref(&f->return_type);
	f->parameters_size  = 0;
	f->block            = NULL;
}

static void add_func_float_float(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float"));
	f->return_type.type   = find_type_by_ref(&f->return_type);
	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);
	f->parameters_size         = 1;
	f->block                   = NULL;
}

static void add_func_float_float_float(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameter_names[1] = add_name("b");
	init_type_ref(&f->parameter_types[1], add_name("float"));
	f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

	f->parameters_size = 2;
	f->block           = NULL;
}

static void add_func_float_float_float_float(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameter_names[1] = add_name("b");
	init_type_ref(&f->parameter_types[1], add_name("float"));
	f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

	f->parameter_names[2] = add_name("c");
	init_type_ref(&f->parameter_types[2], add_name("float"));
	f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

	f->parameters_size = 3;
	f->block           = NULL;
}

static void add_func_float_float2(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float"));
	f->return_type.type   = find_type_by_ref(&f->return_type);
	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float2"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);
	f->parameters_size         = 1;
	f->block                   = NULL;
}

static void add_func_float_float3_float3(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float3"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameter_names[1] = add_name("b");
	init_type_ref(&f->parameter_types[1], add_name("float3"));
	f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

	f->parameters_size = 2;
	f->block           = NULL;
}

static void add_func_float3_float3(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float3"));
	f->return_type.type   = find_type_by_ref(&f->return_type);
	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float3"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);
	f->parameters_size         = 1;
	f->block                   = NULL;
}

static void add_func_float3_float3_float3(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float3"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float3"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameter_names[1] = add_name("b");
	init_type_ref(&f->parameter_types[1], add_name("float3"));
	f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

	f->parameters_size = 2;
	f->block           = NULL;
}

static void add_func_float4_float4_float4(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);
	init_type_ref(&f->return_type, add_name("float4"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	init_type_ref(&f->parameter_types[0], add_name("float4"));
	f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

	f->parameter_names[1] = add_name("b");
	init_type_ref(&f->parameter_types[1], add_name("float4"));
	f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

	f->parameters_size = 2;
	f->block           = NULL;
}

static void add_func_void_uint_uint(char *name) {
	function_id func = add_function(add_name(name));
	function   *f    = get_function(func);

	init_type_ref(&f->return_type, add_name("void"));
	f->return_type.type = find_type_by_ref(&f->return_type);

	f->parameter_names[0] = add_name("a");
	f->parameter_names[1] = add_name("b");
	for (int i = 0; i < 2; ++i) {
		init_type_ref(&f->parameter_types[i], add_name("uint"));
		f->parameter_types[i].type = find_type_by_ref(&f->parameter_types[i]);
	}
	f->parameters_size = 2;

	f->block = NULL;
}

void functions_init(void) {
	function     *new_functions = realloc(functions, functions_size * sizeof(function));
	debug_context context       = {0};
	check(new_functions != NULL, context, "Could not allocate functions");
	functions           = new_functions;
	zero_new_function_slots();
	next_function_index = 0;

	{
		function_id func = add_function(add_name("sample"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float4"));
		f->return_type.type   = find_type_by_ref(&f->return_type);
		f->parameter_names[0] = add_name("tex_coord");
		init_type_ref(&f->parameter_types[0], add_name("float2"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);
		f->parameters_size         = 1;
		f->block                   = NULL;
	}

	{
		function_id func = add_function(add_name("sample_lod"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float4"));
		f->return_type.type   = find_type_by_ref(&f->return_type);
		f->parameter_names[0] = add_name("tex_coord");
		init_type_ref(&f->parameter_types[0], add_name("float2"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);
		f->parameters_size         = 1;
		f->block                   = NULL;
	}

	{
		function_id func = add_function(add_name("float"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float"));
		f->return_type.type   = find_type_by_ref(&f->return_type);
		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("float"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameters_size = 1;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("float2"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float2"));
		f->return_type.type   = find_type_by_ref(&f->return_type);
		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("float"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("float"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameters_size = 2;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("float3"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float3"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("float"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("float"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("float"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameters_size = 3;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("float4"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float4"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("float"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("float"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("float"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameter_names[3] = add_name("w");
		init_type_ref(&f->parameter_types[3], add_name("float"));
		f->parameter_types[3].type = find_type_by_ref(&f->parameter_types[3]);

		f->parameters_size = 4;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("float2x2"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float2x2"));
		f->return_type.type   = find_type_by_ref(&f->return_type);
		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("float2"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("float2"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameters_size = 2;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("float3x3"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float3x3"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("float3"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("float3"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("float3"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameters_size = 3;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("float4x4"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("float4x4"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("float4"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("float4"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("float4"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameter_names[3] = add_name("w");
		init_type_ref(&f->parameter_types[3], add_name("float4"));
		f->parameter_types[3].type = find_type_by_ref(&f->parameter_types[3]);

		f->parameters_size = 4;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("int"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("int"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("int"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameters_size = 1;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("int2"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("int2"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("int"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("int"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameters_size = 2;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("int3"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("int3"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("int"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("int"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("int"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameters_size = 3;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("int4"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("int4"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("int"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("int"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("int"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameter_names[3] = add_name("w");
		init_type_ref(&f->parameter_types[3], add_name("int"));
		f->parameter_types[3].type = find_type_by_ref(&f->parameter_types[3]);

		f->parameters_size = 4;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("uint"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("uint"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("uint"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameters_size = 1;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("uint2"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("uint2"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("uint"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("uint"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameters_size = 2;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("uint3"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("uint3"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("uint"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("uint"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("uint"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameters_size = 3;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("uint4"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("uint4"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("uint"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("uint"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("uint"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameter_names[3] = add_name("w");
		init_type_ref(&f->parameter_types[3], add_name("uint"));
		f->parameter_types[3].type = find_type_by_ref(&f->parameter_types[3]);

		f->parameters_size = 4;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("bool"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("bool"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("bool"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameters_size = 1;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("bool2"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("bool2"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("bool"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("bool"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameters_size = 2;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("bool3"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("bool3"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("bool"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("bool"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("bool"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameters_size = 3;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("bool4"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("bool4"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("bool"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("bool"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("bool"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);

		f->parameter_names[3] = add_name("w");
		init_type_ref(&f->parameter_types[3], add_name("bool"));
		f->parameter_types[3].type = find_type_by_ref(&f->parameter_types[3]);

		f->parameters_size = 4;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("trace_ray"));
		function   *f    = get_function(func);

		init_type_ref(&f->return_type, add_name("void"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("scene");
		init_type_ref(&f->parameter_types[0], add_name("bvh"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);
		f->parameters_size += 1;

		f->parameter_names[1] = add_name("ray");
		init_type_ref(&f->parameter_types[1], add_name("ray"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);
		f->parameters_size += 1;

		f->parameter_names[2] = add_name("payload");
		init_type_ref(&f->parameter_types[2], add_name("void"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);
		f->parameters_size += 1;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("dispatch_mesh"));
		function   *f    = get_function(func);

		init_type_ref(&f->return_type, add_name("void"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("uint"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);
		f->parameters_size += 1;

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("uint"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);
		f->parameters_size += 1;

		f->parameter_names[2] = add_name("z");
		init_type_ref(&f->parameter_types[2], add_name("uint"));
		f->parameter_types[2].type = find_type_by_ref(&f->parameter_types[2]);
		f->parameters_size += 1;

		f->parameter_names[3] = add_name("payload");
		init_type_ref(&f->parameter_types[3], add_name("void"));
		f->parameter_types[3].type = find_type_by_ref(&f->parameter_types[3]);
		f->parameters_size += 1;

		f->block = NULL;
	}

	add_func_uint3("group_id");
	add_func_uint3("group_thread_id");
	add_func_uint3("dispatch_thread_id");
	add_func_int("group_index");
	add_func_int("instance_id");
	add_func_int("vertex_id");

	////
	// add_func_float3_float_float_float("lerp");
	add_func_float_float_float_float("lerp");
	////
	add_func_float3("world_ray_origin");
	add_func_float3("world_ray_direction");
	add_func_float("ray_length");
	add_func_float3_float3("normalize");
	add_func_float_float("sin");
	add_func_float_float("cos");
	add_func_float_float("asin");
	add_func_float_float("acos");
	add_func_float_float("atan");
	add_func_float_float_float("atan2");
	add_func_float_float2("length");
	add_func_float_float3_float3("distance");
	add_func_uint3("ray_index");
	add_func_float3("ray_dimensions");
	add_func_float_float("frac");
	add_func_float3x3("object_to_world3x3");
	add_func_float3_float3_float3("reflect");
	add_func_uint("primitive_index");
	////
	// add_func_float3_float3("abs");
	add_func_float_float("abs");
	add_func_float_float("tan");
	add_func_float_float("log");
	add_func_float_float("exp");
	add_func_float_float("sign");
	add_func_float_float("trunc");
	add_func_float_float("sinh");
	add_func_float_float("cosh");
	add_func_float_float("tanh");
	add_func_float_float("radians");
	add_func_float_float("degrees");
	////
	add_func_float_float_float("floor");
	add_func_float_float_float("ceil");
	add_func_float_float_float("round");
	add_func_float_float_float("sqrt");
	add_func_float_float_float("rsqrt");
	add_func_float_float_float("min");
	add_func_float_float_float("max");
	add_func_float_float_float_float("clamp");
	add_func_float_float_float("step");
	add_func_float_float_float("smoothstep");
	add_func_float_float_float("pow");
	add_func_float_float3_float3("dot");
	add_func_float3_float3_float3("cross");
	add_func_float3_float3("saturate3");
	add_func_float_float("saturate");
	add_func_float_float("ddx");
	add_func_float_float("ddy");

	////

	add_func_float2_float2("ddx2");
	add_func_float2_float2("ddy2");
	add_func_float3_float3("ddx3");
	add_func_float3_float3("ddy3");
	add_func_float3_float3_float_float("clamp3");
	add_func_float3_float3_float3("min3");
	add_func_float3_float3_float3("max3");
	add_func_float4_float4_float4("max4");
	add_func_float3_float3_float3("step3");
	add_func_float3_float3_float3("pow3");
	add_func_float3_float3_float3("floor3");
	add_func_float3_float3_float3("ceil3");
	add_func_float3_float3("abs3");
	add_func_float3_float3("frac3");
	add_func_float3_float3_float3_float("lerp3");
	add_func_float4_float4_float4_float("lerp4");
	add_func_float3x3_float3x3("transpose");

	////

	add_func_void_uint_uint("set_mesh_output_counts");

	{
		function_id func = add_function(add_name("set_mesh_triangle"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("void"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("uint"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("uint3"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameters_size = 2;

		f->block = NULL;
	}

	{
		function_id func = add_function(add_name("set_mesh_vertex"));
		function   *f    = get_function(func);
		init_type_ref(&f->return_type, add_name("void"));
		f->return_type.type = find_type_by_ref(&f->return_type);

		f->parameter_names[0] = add_name("x");
		init_type_ref(&f->parameter_types[0], add_name("uint"));
		f->parameter_types[0].type = find_type_by_ref(&f->parameter_types[0]);

		f->parameter_names[1] = add_name("y");
		init_type_ref(&f->parameter_types[1], add_name("void"));
		f->parameter_types[1].type = find_type_by_ref(&f->parameter_types[1]);

		f->parameters_size = 2;

		f->block = NULL;
	}
}

static void grow_functions_if_needed(uint64_t size) {
	while (size >= functions_size) {
		functions_size *= 2;
		function     *new_functions = realloc(functions, functions_size * sizeof(function));
		debug_context context       = {0};
		check(new_functions != NULL, context, "Could not allocate functions");
		functions = new_functions;
		zero_new_function_slots();
	}
}

function_id add_function(name_id name) {
	grow_functions_if_needed(next_function_index + 1);

	function_id f = next_function_index;
	++next_function_index;

	functions[f].name                        = name;
	functions[f].attributes.attributes_count = 0;
	init_type_ref(&functions[f].return_type, NO_NAME);
	functions[f].parameters_size = 0;
	memset(functions[f].parameter_attributes, 0, sizeof(functions[f].parameter_attributes));
	functions[f].block = NULL;
	functions[f].code.size                  = 0;
	functions[f].descriptor_set_group_index = UINT32_MAX;
	functions[f].used_builtins              = (builtins){0};
	functions[f].used_capabilities          = (capabilities){0};

	return f;
}

function *get_function(function_id function) {
	if (function >= next_function_index) {
		return NULL;
	}
	return &functions[function];
}

global_id add_global(type_id type, attribute_list attributes, name_id name) {
	uint32_t index            = globals_size;
	globals[index].name       = name;
	globals[index].type       = type;
	globals[index].var_index  = 0;
	globals[index].value.kind = GLOBAL_VALUE_NONE;
	globals[index].attributes = attributes;
	globals[index].sets_count = 0;
	globals[index].usage      = 0;
	globals_size += 1;
	return index;
}

global_id add_global_with_value(type_id type, attribute_list attributes, name_id name, global_value value) {
	uint32_t index            = globals_size;
	globals[index].name       = name;
	globals[index].type       = type;
	globals[index].var_index  = 0;
	globals[index].value      = value;
	globals[index].attributes = attributes;
	globals[index].sets_count = 0;
	globals[index].usage      = 0;
	globals_size += 1;
	return index;
}

global *find_global(name_id name) {
	for (uint32_t i = 0; i < globals_size; ++i) {
		if (globals[i].name == name) {
			return &globals[i];
		}
	}

	return NULL;
}

global *get_global(global_id id) {
	if (id >= globals_size) {
		return NULL;
	}

	return &globals[id];
}

void assign_global_var(global_id id, uint64_t var_index) {
	debug_context context = {0};
	check(id < globals_size, context, "Encountered a global with a weird id");
	globals[id].var_index = var_index;
}

void names_init(void) {
	char         *new_names = realloc(names, names_size);
	debug_context context   = {0};
	check(new_names != NULL, context, "Could not allocate names");
	names    = new_names;
	names[0] = 0; // make NO_NAME a proper string

	sh_new_arena(hash); // TODO: Get rid of this by using indices internally in the hash-map so it can survive grow_if_needed
}

static void grow_names_if_needed(size_t size) {
	while (size >= names_size) {
		names_size *= 2;
		char         *new_names = realloc(names, names_size);
		debug_context context   = {0};
		check(new_names != NULL, context, "Could not allocate names");
		names = new_names;
	}
}

name_id add_name(char *name) {
	ptrdiff_t old_id_index = shgeti(hash, name);

	if (old_id_index >= 0) {
		return hash[old_id_index].value;
	}

	size_t length = strlen(name);

	grow_names_if_needed(names_index + length + 1);

	name_id id = names_index;

	memcpy(&names[id], name, length);
	names[id + length] = 0;

	names_index += length + 1;

	shput(hash, &names[id], id);

	return id;
}

char *get_name(name_id id) {
	debug_context context = {0};
	check(id < names_index, context, "Encountered a weird name id");
	return &names[id];
}

////
static statement statements_buffer[8192];
int              statement_index = 0;
////

static statement *statement_allocate(void) {
	////
	// statement    *s       = (statement *)malloc(sizeof(statement));
	statement *s = &statements_buffer[statement_index];
	statement_index++;
	////
	debug_context context = {0};
	check(s != NULL, context, "Could not allocate statement");
	return s;
}

static void statements_init(statements *statements) {
	statements->size = 0;
}

static void statements_add(statements *statements, statement *statement) {
	statements->s[statements->size] = statement;
	statements->size += 1;
}

////
static expression experessions_buffer[8192];
int               expression_index = 0;
////

static expression *expression_allocate(void) {
	////
	// expression   *e       = (expression *)malloc(sizeof(expression));
	expression *e = &experessions_buffer[expression_index];
	expression_index++;
	////
	debug_context context = {0};
	check(e != NULL, context, "Could not allocate expression");
	init_type_ref(&e->type, NO_NAME);
	return e;
}

// static void expression_free(expression *expression) {
//	free(expression);
// }

typedef struct state {
	tokens       *tokens;
	size_t        index;
	debug_context context;
} state_t;

static token current(state_t *state) {
	token token = tokens_get(state->tokens, state->index);
	return token;
}

static void update_debug_context(state_t *state) {
	state->context.column = current(state).column;
	state->context.line   = current(state).line;
}

static void advance_state(state_t *state) {
	state->index += 1;
	update_debug_context(state);
}

static void match_token(state_t *state, int token, const char *error_message) {
	int current_token = current(state).kind;
	if (current_token != token) {
		error(state->context, error_message);
	}
}

static void match_token_identifier(state_t *state) {
	if (current(state).kind != TOKEN_IDENTIFIER) {
		error(state->context, "Expected an identifier");
	}
}

static definition  parse_definition(state_t *state);
static statement  *parse_statement(state_t *state, block *parent_block);
static expression *parse_expression(state_t *state);

void parse(const char *filename, tokens *tokens) {
	state_t state          = {0};
	state.context.filename = filename;
	state.tokens           = tokens;
	state.index            = 0;

	for (;;) {
		token token = current(&state);
		if (token.kind == TOKEN_NONE) {
			return;
		}
		else {
			parse_definition(&state);
		}
	}
}

static statement *parse_block(state_t *state, block *parent_block) {
	match_token(state, TOKEN_LEFT_CURLY, "Expected an opening curly bracket");
	advance_state(state);

	statements statements;
	statements_init(&statements);

	statement *new_block       = statement_allocate();
	new_block->kind            = STATEMENT_BLOCK;
	new_block->block.parent    = parent_block;
	new_block->block.vars.size = 0;

	for (;;) {
		switch (current(state).kind) {
		case TOKEN_RIGHT_CURLY: {
			advance_state(state);
			new_block->block.statements = statements;
			return new_block;
		}
		case TOKEN_NONE: {
			update_debug_context(state);
			error(state->context, "File ended before a block ended");
			return NULL;
		}
		default:
			statements_add(&statements, parse_statement(state, &new_block->block));
			break;
		}
	}
}

typedef enum modifier {
	MODIFIER_IN,
	// Out,
} modifier_t;

typedef struct modifiers {
	modifier_t m[16];
	size_t     size;
} modifiers_t;

static definition parse_struct(state_t *state);
static definition parse_function(state_t *state);
static definition parse_const(state_t *state, attribute_list attributes);

static double attribute_parameter_to_number(name_id attribute_name, name_id parameter_name) {
	if (attribute_name == add_name("topology") && parameter_name == add_name("triangle")) {
		return 0;
	}

	type_id type = find_type_by_name(parameter_name);
	if (type != NO_TYPE) {
		return (double)type;
	}

	debug_context context = {0};
	error(context, "Unknown attribute parameter %s", get_name(parameter_name));
	return 0;
}

static definition parse_definition(state_t *state) {
	attribute_list  attributes = {0};
	descriptor_set *current_sets[64];
	size_t          current_sets_count = 0;

	if (current(state).kind == TOKEN_HASH) {
		advance_state(state);
		match_token(state, TOKEN_LEFT_SQUARE, "Expected left square");
		advance_state(state);

		while (current(state).kind != TOKEN_RIGHT_SQUARE) {
			attribute current_attribute = {0};

			match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");
			current_attribute.name = current(state).identifier;

			if (current_attribute.name == add_name("root_constants")) {
				current_sets[current_sets_count]                                = create_set(current_attribute.name);
				current_attribute.parameters[current_attribute.paramters_count] = current_sets[current_sets_count]->index;
				current_sets_count += 1;
			}

			advance_state(state);

			if (current(state).kind == TOKEN_LEFT_PAREN) {
				advance_state(state);

				while (current(state).kind != TOKEN_RIGHT_PAREN) {
					if (current(state).kind == TOKEN_IDENTIFIER) {
						if (current_attribute.name == add_name("set")) {
							if (current(state).identifier == add_name("root_constants")) {
								debug_context context = {0};
								error(context, "Descriptor set can not be called root_constants");
							}
							current_sets[current_sets_count]                                = create_set(current(state).identifier);
							current_attribute.parameters[current_attribute.paramters_count] = current_sets[current_sets_count]->index;
							current_sets_count += 1;
						}
						else {
							current_attribute.parameters[current_attribute.paramters_count] =
							    attribute_parameter_to_number(current_attribute.name, current(state).identifier);
						}
						current_attribute.paramters_count += 1;
						advance_state(state);
					}
					else if (current(state).kind == TOKEN_FLOAT || current(state).kind == TOKEN_INT) {
						current_attribute.parameters[current_attribute.paramters_count] = current(state).number;
						current_attribute.paramters_count += 1;
						advance_state(state);
					}
					else {
						debug_context context = {0};
						error(context, "Expected an identifier or a number");
					}

					if (current(state).kind != TOKEN_RIGHT_PAREN) {
						match_token(state, TOKEN_COMMA, "Expected a comma");
						advance_state(state);
					}
				}
				advance_state(state);
			}

			attributes.attributes[attributes.attributes_count] = current_attribute;
			attributes.attributes_count += 1;

			if (current(state).kind != TOKEN_RIGHT_SQUARE) {
				match_token(state, TOKEN_COMMA, "Expected a comma");
				advance_state(state);
			}
		}
		advance_state(state);
	}

	switch (current(state).kind) {
	case TOKEN_STRUCT: {
		if (current_sets_count != 0) {
			debug_context context = {0};
			error(context, "A struct can not be assigned to a set");
		}

		definition structy                 = parse_struct(state);
		get_type(structy.type)->attributes = attributes;
		return structy;
	}
	case TOKEN_FUNCTION: {
		if (current_sets_count != 0) {
			debug_context context = {0};
			error(context, "A function can not be assigned to a set");
		}

		definition d  = parse_function(state);
		function  *f  = get_function(d.function);
		f->attributes = attributes;
		return d;
	}
	case TOKEN_CONST: {
		definition d = parse_const(state, attributes);

		for (size_t set_index = 0; set_index < current_sets_count; ++set_index) {
			add_definition_to_set(current_sets[set_index], d);
		}

		return d;
	}
	default: {
		update_debug_context(state);
		error(state->context, "Expected a struct, a function or a const");

		definition d = {0};
		return d;
	}
	}
}

static type_ref parse_type_ref(state_t *state) {
	match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");
	token type_name = current(state);
	advance_state(state);

	uint32_t array_size = 0;
	if (current(state).kind == TOKEN_LEFT_SQUARE) {
		advance_state(state);
		if (current(state).kind == TOKEN_INT) {
			array_size = (uint32_t)current(state).number;
			if (array_size == 0) {
				error(state->context, "Array size of 0 is not allowed");
			}
			advance_state(state);
		}
		else {
			array_size = UINT32_MAX;
		}
		match_token(state, TOKEN_RIGHT_SQUARE, "Expected a closing square bracket");
		advance_state(state);
	}

	type_ref t;
	init_type_ref(&t, type_name.identifier);
	t.unresolved.array_size = array_size;
	return t;
}

static statement *parse_statement(state_t *state, block *parent_block) {
	switch (current(state).kind) {
	case TOKEN_IF: {
		advance_state(state);
		match_token(state, TOKEN_LEFT_PAREN, "Expected an opening bracket");
		advance_state(state);

		expression *test = parse_expression(state);
		match_token(state, TOKEN_RIGHT_PAREN, "Expected a closing bracket");
		advance_state(state);

		statement *if_block = parse_statement(state, parent_block);
		statement *s        = statement_allocate();
		s->kind             = STATEMENT_IF;
		s->iffy.test        = test;
		s->iffy.if_block    = if_block;

		s->iffy.else_size = 0;

		while (current(state).kind == TOKEN_ELSE) {
			advance_state(state);

			if (current(state).kind == TOKEN_IF) {
				advance_state(state);
				match_token(state, TOKEN_LEFT_PAREN, "Expected an opening bracket");
				advance_state(state);

				expression *test = parse_expression(state);
				match_token(state, TOKEN_RIGHT_PAREN, "Expected a closing bracket");
				advance_state(state);

				statement *if_block = parse_statement(state, parent_block);

				s->iffy.else_tests[s->iffy.else_size]  = test;
				s->iffy.else_blocks[s->iffy.else_size] = if_block;
			}
			else {
				statement *else_block                  = parse_statement(state, parent_block);
				s->iffy.else_tests[s->iffy.else_size]  = NULL;
				s->iffy.else_blocks[s->iffy.else_size] = else_block;
			}

			s->iffy.else_size += 1;
			assert(s->iffy.else_size < 64);
		}

		return s;
	}
	case TOKEN_WHILE: {
		advance_state(state);
		match_token(state, TOKEN_LEFT_PAREN, "Expected an opening bracket");
		advance_state(state);

		expression *test = parse_expression(state);
		match_token(state, TOKEN_RIGHT_PAREN, "Expected a closing bracket");
		advance_state(state);

		statement *while_block = parse_statement(state, parent_block);

		statement *s          = statement_allocate();
		s->kind               = STATEMENT_WHILE;
		s->whiley.test        = test;
		s->whiley.while_block = while_block;

		return s;
	}
	case TOKEN_DO: {
		advance_state(state);

		statement *do_block = parse_statement(state, parent_block);

		statement *s          = statement_allocate();
		s->kind               = STATEMENT_DO_WHILE;
		s->whiley.while_block = do_block;

		match_token(state, TOKEN_WHILE, "Expected \"while\"");
		advance_state(state);
		match_token(state, TOKEN_LEFT_PAREN, "Expected an opening bracket");
		advance_state(state);

		expression *test = parse_expression(state);
		match_token(state, TOKEN_RIGHT_PAREN, "Expected a closing bracket");
		advance_state(state);

		s->whiley.test = test;

		match_token(state, TOKEN_SEMICOLON, "Expected a semicolon");
		advance_state(state);

		return s;
	}
	case TOKEN_FOR: {
		statements outer_block_statements;
		statements_init(&outer_block_statements);

		statement *outer_block        = statement_allocate();
		outer_block->kind             = STATEMENT_BLOCK;
		outer_block->block.parent     = parent_block;
		outer_block->block.vars.size  = 0;
		outer_block->block.statements = outer_block_statements;

		advance_state(state);

		match_token(state, TOKEN_LEFT_PAREN, "Expected an opening bracket");
		advance_state(state);

		statement *pre = parse_statement(state, &outer_block->block);
		statements_add(&outer_block->block.statements, pre);

		expression *test = parse_expression(state);
		match_token(state, TOKEN_SEMICOLON, "Expected a semicolon");
		advance_state(state);

		expression *post_expression = parse_expression(state);

		match_token(state, TOKEN_RIGHT_PAREN, "Expected a closing bracket");
		advance_state(state);

		statement *inner_block = parse_statement(state, &outer_block->block);

		statement *post_statement  = statement_allocate();
		post_statement->kind       = STATEMENT_EXPRESSION;
		post_statement->expression = post_expression;

		statements_add(&inner_block->block.statements, post_statement);

		statement *s          = statement_allocate();
		s->kind               = STATEMENT_WHILE;
		s->whiley.test        = test;
		s->whiley.while_block = inner_block;

		statements_add(&outer_block->block.statements, s);

		return outer_block;
	}
	case TOKEN_LEFT_CURLY: {
		return parse_block(state, parent_block);
	}
	case TOKEN_VAR: {
		advance_state(state);

		match_token_identifier(state);
		token name = current(state);
		advance_state(state);

		match_token(state, TOKEN_COLON, "Expected a colon");
		advance_state(state);

		type_ref type = parse_type_ref(state);

		expression *init = NULL;

		if (current(state).kind == TOKEN_OPERATOR) {
			check(current(state).op == OPERATOR_ASSIGN, state->context, "Expected an assign");
			advance_state(state);
			init = parse_expression(state);
		}

		match_token(state, TOKEN_SEMICOLON, "Expected a semicolon");
		advance_state(state);

		statement *statement                      = statement_allocate();
		statement->kind                           = STATEMENT_LOCAL_VARIABLE;
		statement->local_variable.var.name        = name.identifier;
		statement->local_variable.var.type        = type;
		statement->local_variable.var.variable_id = 0;
		statement->local_variable.init            = init;
		return statement;
	}
	case TOKEN_RETURN: {
		advance_state(state);

		expression *expr = parse_expression(state);
		match_token(state, TOKEN_SEMICOLON, "Expected a semicolon");
		advance_state(state);

		statement *statement  = statement_allocate();
		statement->kind       = STATEMENT_RETURN_EXPRESSION;
		statement->expression = expr;
		return statement;
	}
	case TOKEN_DISCARD: {
		advance_state(state);

		match_token(state, TOKEN_SEMICOLON, "Expected a semicolon");
		advance_state(state);

		statement *statement = statement_allocate();
		statement->kind      = STATEMENT_DISCARD;

		return statement;
	}
	default: {
		expression *expr = parse_expression(state);
		match_token(state, TOKEN_SEMICOLON, "Expected a semicolon");
		advance_state(state);

		statement *statement  = statement_allocate();
		statement->kind       = STATEMENT_EXPRESSION;
		statement->expression = expr;
		return statement;
	}
	}
}

static expression *parse_assign(state_t *state);

static expression *parse_expression(state_t *state) {
	return parse_assign(state);
}

static expression *parse_logical(state_t *state);

static expression *parse_assign(state_t *state) {
	expression *expr = parse_logical(state);
	bool        done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_ASSIGN || op == OPERATOR_MINUS_ASSIGN || op == OPERATOR_PLUS_ASSIGN || op == OPERATOR_DIVIDE_ASSIGN ||
			    op == OPERATOR_MULTIPLY_ASSIGN) {
				advance_state(state);
				expression *right        = parse_logical(state);
				expression *expression   = expression_allocate();
				expression->kind         = EXPRESSION_BINARY;
				expression->binary.left  = expr;
				expression->binary.op    = op;
				expression->binary.right = right;
				expr                     = expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return expr;
}

static expression *parse_bitwise(state_t *state);

static expression *parse_logical(state_t *state) {
	expression *expr = parse_bitwise(state);
	bool        done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_OR || op == OPERATOR_AND) {
				advance_state(state);
				expression *right        = parse_bitwise(state);
				expression *expression   = expression_allocate();
				expression->kind         = EXPRESSION_BINARY;
				expression->binary.left  = expr;
				expression->binary.op    = op;
				expression->binary.right = right;
				expr                     = expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return expr;
}

static expression *parse_equality(state_t *state);

static expression *parse_bitwise(state_t *state) {
	expression *expr = parse_equality(state);
	bool        done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_BITWISE_XOR || op == OPERATOR_BITWISE_OR || op == OPERATOR_BITWISE_AND) {
				advance_state(state);
				expression *right        = parse_equality(state);
				expression *expression   = expression_allocate();
				expression->kind         = EXPRESSION_BINARY;
				expression->binary.left  = expr;
				expression->binary.op    = op;
				expression->binary.right = right;
				expr                     = expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return expr;
}

static expression *parse_comparison(state_t *state);

static expression *parse_equality(state_t *state) {
	expression *expr = parse_comparison(state);
	bool        done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_EQUALS || op == OPERATOR_NOT_EQUALS) {
				advance_state(state);
				expression *right        = parse_comparison(state);
				expression *expression   = expression_allocate();
				expression->kind         = EXPRESSION_BINARY;
				expression->binary.left  = expr;
				expression->binary.op    = op;
				expression->binary.right = right;
				expr                     = expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return expr;
}

static expression *parse_shift(state_t *state);

static expression *parse_comparison(state_t *state) {
	expression *expr = parse_shift(state);
	bool        done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_GREATER || op == OPERATOR_GREATER_EQUAL || op == OPERATOR_LESS || op == OPERATOR_LESS_EQUAL) {
				advance_state(state);
				expression *right        = parse_shift(state);
				expression *expression   = expression_allocate();
				expression->kind         = EXPRESSION_BINARY;
				expression->binary.left  = expr;
				expression->binary.op    = op;
				expression->binary.right = right;
				expr                     = expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return expr;
}

static expression *parse_addition(state_t *state);

static expression *parse_shift(state_t *state) {
	expression *expr = parse_addition(state);
	bool        done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_LEFT_SHIFT || op == OPERATOR_RIGHT_SHIFT) {
				advance_state(state);
				expression *right        = parse_addition(state);
				expression *expression   = expression_allocate();
				expression->kind         = EXPRESSION_BINARY;
				expression->binary.left  = expr;
				expression->binary.op    = op;
				expression->binary.right = right;
				expr                     = expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return expr;
}

static expression *parse_multiplication(state_t *state);

static expression *parse_addition(state_t *state) {
	expression *expr = parse_multiplication(state);
	bool        done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_MINUS || op == OPERATOR_PLUS) {
				advance_state(state);
				expression *right        = parse_multiplication(state);
				expression *expression   = expression_allocate();
				expression->kind         = EXPRESSION_BINARY;
				expression->binary.left  = expr;
				expression->binary.op    = op;
				expression->binary.right = right;
				expr                     = expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return expr;
}

static expression *parse_unary(state_t *state);

static expression *parse_multiplication(state_t *state) {
	expression *expr = parse_unary(state);
	bool        done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_DIVIDE || op == OPERATOR_MULTIPLY || op == OPERATOR_MOD) {
				advance_state(state);
				expression *right        = parse_unary(state);
				expression *expression   = expression_allocate();
				expression->kind         = EXPRESSION_BINARY;
				expression->binary.left  = expr;
				expression->binary.op    = op;
				expression->binary.right = right;
				expr                     = expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return expr;
}

static expression *parse_primary(state_t *state);

static expression *parse_unary(state_t *state) {
	bool done = false;
	while (!done) {
		if (current(state).kind == TOKEN_OPERATOR) {
			operatorr op = current(state).op;
			if (op == OPERATOR_NOT || op == OPERATOR_MINUS) {
				advance_state(state);
				expression *right       = parse_unary(state);
				expression *expression  = expression_allocate();
				expression->kind        = EXPRESSION_UNARY;
				expression->unary.op    = op;
				expression->unary.right = right;
				return expression;
			}
			else {
				done = true;
			}
		}
		else {
			done = true;
		}
	}
	return parse_primary(state);
}

static expression *parse_member_or_element_access(state_t *state, expression *of) {
	if (current(state).kind == TOKEN_DOT) {
		advance_state(state);

		match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");

		token token = current(state);

		advance_state(state);

		expression *member         = expression_allocate();
		member->kind               = EXPRESSION_MEMBER;
		member->member.of          = of;
		member->member.member_name = token.identifier;

		expression *sub = parse_member_or_element_access(state, member);

		return sub;
	}
	else if (current(state).kind == TOKEN_LEFT_SQUARE) {
		advance_state(state);

		expression *index = parse_expression(state);

		match_token(state, TOKEN_RIGHT_SQUARE, "Expected a closing square bracket");

		advance_state(state);

		expression *element            = expression_allocate();
		element->kind                  = EXPRESSION_ELEMENT;
		element->element.of            = of;
		element->element.element_index = index;

		expression *sub = parse_member_or_element_access(state, element);

		return sub;
	}

	if (current(state).kind == TOKEN_LEFT_PAREN) {
		error(state->context, "Function members not supported.");
	}

	return of;
}

static expression *parse_call(state_t *state, name_id func_name);

static expression *parse_primary(state_t *state) {
	expression *left = NULL;

	switch (current(state).kind) {
	case TOKEN_BOOLEAN: {
		bool value = current(state).boolean;
		advance_state(state);
		left          = expression_allocate();
		left->kind    = EXPRESSION_BOOLEAN;
		left->boolean = value;
		break;
	}
	case TOKEN_FLOAT: {
		double value = current(state).number;
		advance_state(state);
		left         = expression_allocate();
		left->kind   = EXPRESSION_FLOAT;
		left->number = value;
		break;
	}
	case TOKEN_INT: {
		double value = current(state).number;
		advance_state(state);
		left         = expression_allocate();
		left->kind   = EXPRESSION_INT;
		left->number = value;
		break;
	}
	case TOKEN_IDENTIFIER: {
		token token = current(state);
		advance_state(state);
		if (current(state).kind == TOKEN_LEFT_PAREN) {
			left = parse_call(state, token.identifier);
		}
		else {
			expression *var = expression_allocate();
			var->kind       = EXPRESSION_VARIABLE;
			var->variable   = token.identifier;
			left            = var;
		}
		break;
	}
	case TOKEN_LEFT_PAREN: {
		advance_state(state);
		expression *expr = parse_expression(state);
		match_token(state, TOKEN_RIGHT_PAREN, "Expected a closing bracket");
		advance_state(state);
		left           = expression_allocate();
		left->kind     = EXPRESSION_GROUPING;
		left->grouping = expr;
		break;
	}
	default:
		error(state->context, "Unexpected token");
		return NULL;
	}

	return parse_member_or_element_access(state, left);
}

static expressions parse_parameters(state_t *state) {
	expressions e;
	e.size = 0;

	if (current(state).kind == TOKEN_RIGHT_PAREN) {
		advance_state(state);
		return e;
	}

	for (;;) {
		e.e[e.size] = parse_expression(state);
		e.size += 1;

		if (current(state).kind == TOKEN_COMMA) {
			advance_state(state);
		}
		else {
			match_token(state, TOKEN_RIGHT_PAREN, "Expected a closing bracket");
			advance_state(state);
			return e;
		}
	}
}

static expression *parse_call(state_t *state, name_id func_name) {
	match_token(state, TOKEN_LEFT_PAREN, "Expected an opening bracket");
	advance_state(state);

	expression *call = NULL;

	call                  = expression_allocate();
	call->kind            = EXPRESSION_CALL;
	call->call.func_name  = func_name;
	call->call.parameters = parse_parameters(state);

	return parse_member_or_element_access(state, call);
}

static definition parse_struct_inner(state_t *state, name_id name) {
	match_token(state, TOKEN_LEFT_CURLY, "Expected an opening curly bracket");
	advance_state(state);

	token    member_names[MAX_MEMBERS];
	type_ref type_refs[MAX_MEMBERS];
	token    member_values[MAX_MEMBERS];
	size_t   count = 0;

	while (current(state).kind != TOKEN_RIGHT_CURLY) {
		debug_context context = {0};
		check(count < MAX_MEMBERS, context, "Out of members");

		match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");
		member_names[count] = current(state);

		advance_state(state);

		if (current(state).kind == TOKEN_COLON) {
			advance_state(state);
			type_refs[count] = parse_type_ref(state);
		}
		else {
			type_ref t;
			t.type                  = NO_TYPE;
			t.unresolved.name       = NO_NAME;
			t.unresolved.array_size = 0;
			type_refs[count]        = t;
		}

		if (current(state).kind == TOKEN_OPERATOR && current(state).op == OPERATOR_ASSIGN) {
			advance_state(state);
			if (current(state).kind == TOKEN_BOOLEAN || current(state).kind == TOKEN_FLOAT || current(state).kind == TOKEN_INT ||
			    current(state).kind == TOKEN_IDENTIFIER) {
				member_values[count] = current(state);
				advance_state(state);

				if (current(state).kind == TOKEN_LEFT_PAREN) {
					advance_state(state);
					match_token(state, TOKEN_RIGHT_PAREN, "Expected a right paren");
					advance_state(state);
				}
			}
			else {
				debug_context context = {0};
				error(context, "Unsupported assign in struct");
			}
		}
		else {
			member_values[count].kind       = TOKEN_NONE;
			member_values[count].identifier = NO_NAME;
		}

		match_token(state, TOKEN_SEMICOLON, "Expected a semicolon");

		advance_state(state);

		++count;
	}

	advance_state(state);

	definition definition;
	definition.kind = DEFINITION_STRUCT;

	definition.type = add_type(name);

	type *s = get_type(definition.type);

	for (size_t i = 0; i < count; ++i) {
		member member;
		member.name  = member_names[i].identifier;
		member.value = member_values[i];
		if (member.value.kind != TOKEN_NONE) {
			if (member.value.kind == TOKEN_BOOLEAN) {
				init_type_ref(&member.type, add_name("bool"));
			}
			else if (member.value.kind == TOKEN_FLOAT) {
				init_type_ref(&member.type, add_name("float"));
			}
			else if (member.value.kind == TOKEN_INT) {
				init_type_ref(&member.type, add_name("int"));
			}
			else if (member.value.kind == TOKEN_IDENTIFIER) {
				global *g = find_global(member.value.identifier);
				if (g != NULL && g->name != NO_NAME) {
					init_type_ref(&member.type, get_type(g->type)->name);
				}
				else {
					init_type_ref(&member.type, add_name("fun"));
				}
			}
			else {
				debug_context context = {0};
				error(context, "Unsupported value in struct");
			}
		}
		else {
			member.type = type_refs[i];
		}

		s->members.m[i] = member;
	}
	s->members.size = count;

	return definition;
}

static definition parse_struct(state_t *state) {
	advance_state(state);

	match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");
	token name = current(state);
	advance_state(state);

	return parse_struct_inner(state, name.identifier);
}

static definition parse_function(state_t *state) {
	advance_state(state);
	match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");

	token name = current(state);
	advance_state(state);
	match_token(state, TOKEN_LEFT_PAREN, "Expected an opening bracket");
	advance_state(state);

	uint8_t  parameters_size       = 0;
	name_id  param_names[256]      = {0};
	type_ref param_types[256]      = {0};
	name_id  param_attributes[256] = {0};

	while (current(state).kind != TOKEN_RIGHT_PAREN) {
		if (current(state).kind == TOKEN_HASH) {
			advance_state(state);
			match_token(state, TOKEN_LEFT_SQUARE, "Expected an opening square bracket");
			advance_state(state);
			match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");
			token attribute_name              = current(state);
			param_attributes[parameters_size] = attribute_name.identifier;
			advance_state(state);
			match_token(state, TOKEN_RIGHT_SQUARE, "Expected a closing square bracket");
			advance_state(state);
		}

		match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");
		param_names[parameters_size] = current(state).identifier;
		advance_state(state);
		match_token(state, TOKEN_COLON, "Expected a colon");
		advance_state(state);
		param_types[parameters_size] = parse_type_ref(state);
		if (current(state).kind == TOKEN_COMMA) {
			advance_state(state);
		}
		parameters_size += 1;
	}

	match_token(state, TOKEN_RIGHT_PAREN, "Expected a closing bracket");
	advance_state(state);
	match_token(state, TOKEN_COLON, "Expected a colon");
	advance_state(state);

	type_ref return_type = parse_type_ref(state);

	statement *block = parse_block(state, NULL);

	definition d;
	d.kind             = DEFINITION_FUNCTION;
	d.function         = add_function(name.identifier);
	function *f        = get_function(d.function);
	f->return_type     = return_type;
	f->parameters_size = parameters_size;
	for (uint8_t parameter_index = 0; parameter_index < parameters_size; ++parameter_index) {
		f->parameter_names[parameter_index]      = param_names[parameter_index];
		f->parameter_types[parameter_index]      = param_types[parameter_index];
		f->parameter_attributes[parameter_index] = param_attributes[parameter_index];
	}
	f->block = block;

	return d;
}

static definition parse_const(state_t *state, attribute_list attributes) {
	advance_state(state);
	match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");

	token name = current(state);
	advance_state(state);
	match_token(state, TOKEN_COLON, "Expected a colon");
	advance_state(state);

	name_id type_name   = NO_NAME;
	type_id type        = NO_TYPE;
	name_id format_name = NO_NAME;

	if (current(state).kind == TOKEN_LEFT_CURLY) {
		type = parse_struct_inner(state, NO_NAME).type;
	}
	else {
		match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");
		type_name = current(state).identifier;
		advance_state(state);
	}

	bool     array      = false;
	uint32_t array_size = UINT32_MAX;

	if (current(state).kind == TOKEN_LEFT_SQUARE) {
		array = true;
		advance_state(state);

		if (current(state).kind == TOKEN_INT) {
			array_size = (uint32_t)current(state).number;
			advance_state(state);
		}

		match_token(state, TOKEN_RIGHT_SQUARE, "Expected a right square bracket");
		advance_state(state);
	}

	expression *value = NULL;
	if (current(state).kind == TOKEN_OPERATOR && current(state).op == OPERATOR_ASSIGN) {
		advance_state(state);
		value = parse_expression(state);
	}

	if (current(state).kind == TOKEN_OPERATOR && current(state).op == OPERATOR_LESS) {
		advance_state(state);
		match_token(state, TOKEN_IDENTIFIER, "Expected an identifier");
		format_name = current(state).identifier;
		advance_state(state);

		if (current(state).kind == TOKEN_LEFT_PAREN) {
			advance_state(state);
			match_token(state, TOKEN_RIGHT_PAREN, "Expected a right paren");
			advance_state(state);
		}

		if (current(state).kind != TOKEN_OPERATOR || current(state).op != OPERATOR_GREATER) {
			error(state->context, "Expected a greater than");
		}
		advance_state(state);
	}

	match_token(state, TOKEN_SEMICOLON, "Expected a semicolon");
	advance_state(state);

	definition d = {0};

	name_id tex1d_name        = add_name("tex1d");
	name_id tex2d_name        = add_name("tex2d");
	name_id tex3d_name        = add_name("tex3d");
	name_id texcube_name      = add_name("texcube");
	name_id tex1darray_name   = add_name("tex1darray");
	name_id tex2darray_name   = add_name("tex2darray");
	name_id texcubearray_name = add_name("texcubearray");

	if (type_name == NO_NAME) {
		debug_context context = {0};
		check(type != NO_TYPE, context, "Const has no type");
		d.kind   = DEFINITION_CONST_CUSTOM;
		d.global = add_global(type, attributes, name.identifier);
	}
	else if (type_name == tex1d_name || type_name == tex2d_name || type_name == tex3d_name || type_name == texcube_name || type_name == tex1darray_name ||
	         type_name == tex2darray_name || type_name == texcubearray_name) {
		struct type tex_type;
		tex_type.name                        = type_name;
		tex_type.attributes.attributes_count = 0;
		tex_type.members.size                = 0;
		tex_type.built_in                    = true;
		tex_type.array_size                  = 0;
		tex_type.base                        = NO_TYPE;
		d.kind                               = DEFINITION_TEX2D;
		tex_type.tex_kind                    = TEXTURE_KIND_2D;
		type_id t_id                         = add_full_type(&tex_type);

		if (array) {
			type_id array_type_id               = add_type(type_name);
			get_type(array_type_id)->base       = t_id;
			get_type(array_type_id)->built_in   = true;
			get_type(array_type_id)->array_size = array_size;
			t_id                                = array_type_id;
		}

		d.global = add_global(t_id, attributes, name.identifier);
	}
	else if (type_name == add_name("sampler")) {
		d.kind   = DEFINITION_SAMPLER;
		d.global = add_global(sampler_type_id, attributes, name.identifier);
	}
	else if (type_name == add_name("bvh")) {
		d.kind   = DEFINITION_BVH;
		d.global = add_global(bvh_type_id, attributes, name.identifier);
	}
	else if (type_name == add_name("float")) {
		debug_context context = {0};
		check(value != NULL, context, "const float requires an initialization value");
		check(value->kind == EXPRESSION_FLOAT || value->kind == EXPRESSION_INT, context, "const float requires a number");

		global_value float_value;
		float_value.kind = GLOBAL_VALUE_FLOAT;

		float_value.value.floats[0] = (float)value->number;

		d.kind   = DEFINITION_CONST_BASIC;
		d.global = add_global_with_value(float_id, attributes, name.identifier, float_value);
	}
	else if (type_name == add_name("float2")) {
		debug_context context = {0};
		check(value != NULL, context, "const float2 requires an initialization value");
		check(value->kind == EXPRESSION_CALL, context, "const float2 requires a constructor call");
		check(value->call.func_name == add_name("float2"), context, "const float2 requires a float2 call");
		check(value->call.parameters.size == 3, context, "const float2 construtor call requires two parameters");

		global_value float2_value;
		float2_value.kind = GLOBAL_VALUE_FLOAT2;

		for (int i = 0; i < 2; ++i) {
			check(value->call.parameters.e[i]->kind == EXPRESSION_FLOAT || value->call.parameters.e[i]->kind == EXPRESSION_INT, context,
			      "const float2 construtor parameters have to be numbers");
			float2_value.value.floats[i] = (float)value->call.parameters.e[i]->number;
		}

		d.kind   = DEFINITION_CONST_BASIC;
		d.global = add_global_with_value(float2_id, attributes, name.identifier, float2_value);
	}
	else if (type_name == add_name("float3")) {
		debug_context context = {0};
		check(value != NULL, context, "const float3 requires an initialization value");
		check(value->kind == EXPRESSION_CALL, context, "const float3 requires a constructor call");
		check(value->call.func_name == add_name("float3"), context, "const float3 requires a float3 call");
		check(value->call.parameters.size == 3, context, "const float3 construtor call requires three parameters");

		global_value float3_value;
		float3_value.kind = GLOBAL_VALUE_FLOAT3;

		for (int i = 0; i < 3; ++i) {
			check(value->call.parameters.e[i]->kind == EXPRESSION_FLOAT || value->call.parameters.e[i]->kind == EXPRESSION_INT, context,
			      "const float3 construtor parameters have to be numbers");
			float3_value.value.floats[i] = (float)value->call.parameters.e[i]->number;
		}

		d.kind   = DEFINITION_CONST_BASIC;
		d.global = add_global_with_value(float3_id, attributes, name.identifier, float3_value);
	}
	else if (type_name == add_name("float4")) {
		debug_context context = {0};
		if (!array) {
			check(value != NULL, context, "const float4 requires an initialization value");
			check(value->kind == EXPRESSION_CALL, context, "const float4 requires a constructor call");
			check(value->call.func_name == add_name("float4"), context, "const float4 requires a float4 call");
			check(value->call.parameters.size == 4, context, "const float4 construtor call requires four parameters");
		}
		else {
			check(value == NULL, context, "const float4[] does not allow an initialization value");
		}

		global_value float4_value;
		float4_value.kind = GLOBAL_VALUE_FLOAT4;

		if (!array) {
			for (int i = 0; i < 4; ++i) {
				check(value->call.parameters.e[i]->kind == EXPRESSION_FLOAT || value->call.parameters.e[i]->kind == EXPRESSION_INT, context,
				      "const float4 construtor parameters have to be numbers");
				float4_value.value.floats[i] = (float)value->call.parameters.e[i]->number;
			}
		}

		d.kind = DEFINITION_CONST_BASIC;
		if (array) {
			type_id array_type_id               = add_type(get_type(float4_id)->name);
			get_type(array_type_id)->base       = float4_id;
			get_type(array_type_id)->built_in   = true;
			get_type(array_type_id)->array_size = array_size;

			d.global = add_global(array_type_id, attributes, name.identifier);
		}
		else {
			d.global = add_global_with_value(float4_id, attributes, name.identifier, float4_value);
		}
	}
	else {
		debug_context context = {0};
		error(context, "Unsupported global");
	}

	return d;
}

descriptor_set *create_set(name_id name) {
	for (size_t set_index = 0; set_index < sets_count; ++set_index) {
		if (sets[set_index].name == name) {
			return &sets[set_index];
		}
	}

	if (sets_count >= MAX_SETS) {
		debug_context context = {0};
		error(context, "Max set count of %i reached", MAX_SETS);
		return NULL;
	}

	descriptor_set *new_set = &sets[sets_count];
	new_set->name           = name;
	new_set->index          = (uint32_t)sets_count;
	new_set->globals.size   = 0;

	sets_count += 1;

	return new_set;
}

descriptor_set *get_set(size_t index) {
	return &sets[index];
}

size_t get_sets_count(void) {
	return sets_count;
}

void add_definition_to_set(descriptor_set *set, definition def) {
	assert(def.kind != DEFINITION_FUNCTION && def.kind != DEFINITION_STRUCT);

	for (size_t global_index = 0; global_index < set->globals.size; ++global_index) {
		if (set->globals.globals[global_index] == def.global) {
			return;
		}
	}

	get_global(def.global)->sets[get_global(def.global)->sets_count] = set;
	get_global(def.global)->sets_count += 1;
	set->globals.globals[set->globals.size] = def.global;
	set->globals.size += 1;
}

token tokens_get(tokens *tokens, size_t index) {
	debug_context context = {0};
	check(tokens->current_size > index, context, "Token index out of bounds");
	return tokens->t[index];
}

static bool is_num(char ch, char chch) {
	return (ch >= '0' && ch <= '9') || (ch == '-' && chch >= '0' && chch <= '9');
}

static bool is_op(char ch) {
	return ch == '&' || ch == '|' || ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '=' || ch == '!' || ch == '<' || ch == '>' || ch == '%' ||
	       ch == '^';
}

static bool is_whitespace(char ch) {
	return ch == ' ' || (ch >= 9 && ch <= 13);
}

static void tokenizer_state_init(debug_context *context, tokenizer_state *state, const char *source) {
	state->line = state->column = 0;
	state->iterator             = source;
	state->next                 = *state->iterator;
	if (*state->iterator != 0) {
		state->iterator += 1;
	}
	state->next_next = *state->iterator;
	state->line_end  = false;

	context->column = 0;
	context->line   = 0;
}

static void tokenizer_state_advance(debug_context *context, tokenizer_state *state) {
	state->next = state->next_next;
	if (*state->iterator != 0) {
		state->iterator += 1;
	}
	state->next_next = *state->iterator;

	if (state->line_end) {
		state->line_end = false;
		state->line += 1;
		state->column = 0;
	}
	else {
		state->column += 1;
	}

	if (state->next == '\n') {
		state->line_end = true;
	}

	context->column = state->column;
	context->line   = state->line;
}

static void tokenizer_buffer_init(tokenizer_buffer *buffer) {
	buffer->max_size     = 1024 * 1024;
	buffer->buf          = (char *)malloc(buffer->max_size);
	buffer->current_size = 0;
	buffer->column = buffer->line = 0;
}

static void tokenizer_buffer_reset(tokenizer_buffer *buffer, tokenizer_state *state) {
	buffer->current_size = 0;
	buffer->column       = state->column;
	buffer->line         = state->line;
}

static void tokenizer_buffer_add(tokenizer_buffer *buffer, char ch) {
	debug_context context = {0};
	check(buffer->current_size < buffer->max_size, context, "Token buffer is too small");
	buffer->buf[buffer->current_size] = ch;
	buffer->current_size += 1;
}

static bool tokenizer_buffer_equals(tokenizer_buffer *buffer, const char *str) {
	buffer->buf[buffer->current_size] = 0;
	return strcmp(buffer->buf, str) == 0;
}

static name_id tokenizer_buffer_to_name(tokenizer_buffer *buffer) {
	debug_context context = {0};
	check(buffer->current_size < buffer->max_size, context, "Token buffer is too small");
	buffer->buf[buffer->current_size] = 0;
	buffer->current_size += 1;
	return add_name(buffer->buf);
}

static double tokenizer_buffer_parse_number(tokenizer_buffer *buffer) {
	buffer->buf[buffer->current_size] = 0;
	return strtod(buffer->buf, NULL);
}

token token_create(int kind, tokenizer_state *state) {
	token token;
	token.kind   = kind;
	token.column = state->column;
	token.line   = state->line;
	return token;
}

static void tokens_init(tokens *tokens) {
	tokens->max_size     = 1024 * 1024;
	tokens->t            = malloc(tokens->max_size * sizeof(token));
	tokens->current_size = 0;
}

static void tokens_add(tokens *tokens, token token) {
	tokens->t[tokens->current_size] = token;
	tokens->current_size += 1;
	debug_context context = {0};
	check(tokens->current_size <= tokens->max_size, context, "Out of tokens");
}

static void tokens_add_identifier(tokenizer_state *state, tokens *tokens, tokenizer_buffer *buffer) {
	token token;

	if (tokenizer_buffer_equals(buffer, "true")) {
		token         = token_create(TOKEN_BOOLEAN, state);
		token.boolean = true;
	}
	else if (tokenizer_buffer_equals(buffer, "false")) {
		token         = token_create(TOKEN_BOOLEAN, state);
		token.boolean = false;
	}
	else if (tokenizer_buffer_equals(buffer, "if")) {
		token = token_create(TOKEN_IF, state);
	}
	else if (tokenizer_buffer_equals(buffer, "else")) {
		token = token_create(TOKEN_ELSE, state);
	}
	else if (tokenizer_buffer_equals(buffer, "while")) {
		token = token_create(TOKEN_WHILE, state);
	}
	else if (tokenizer_buffer_equals(buffer, "do")) {
		token = token_create(TOKEN_DO, state);
	}
	else if (tokenizer_buffer_equals(buffer, "for")) {
		token = token_create(TOKEN_FOR, state);
	}
	else if (tokenizer_buffer_equals(buffer, "in")) {
		token = token_create(TOKEN_IN, state);
	}
	else if (tokenizer_buffer_equals(buffer, "struct")) {
		token = token_create(TOKEN_STRUCT, state);
	}
	else if (tokenizer_buffer_equals(buffer, "fun")) {
		token = token_create(TOKEN_FUNCTION, state);
	}
	else if (tokenizer_buffer_equals(buffer, "var")) {
		token = token_create(TOKEN_VAR, state);
	}
	else if (tokenizer_buffer_equals(buffer, "const")) {
		token = token_create(TOKEN_CONST, state);
	}
	else if (tokenizer_buffer_equals(buffer, "return")) {
		token = token_create(TOKEN_RETURN, state);
	}
	else if (tokenizer_buffer_equals(buffer, "discard")) {
		token = token_create(TOKEN_DISCARD, state);
	}
	else {
		token            = token_create(TOKEN_IDENTIFIER, state);
		token.identifier = tokenizer_buffer_to_name(buffer);
	}

	token.column = buffer->column;
	token.line   = buffer->line;
	tokens_add(tokens, token);
}

tokens tokenize(const char *filename, const char *source) {
	mode mode           = MODE_SELECT;
	bool number_has_dot = false;

	tokens tokens;
	tokens_init(&tokens);

	debug_context context = {0};
	context.filename      = filename;

	tokenizer_state state;
	tokenizer_state_init(&context, &state, source);

	tokenizer_buffer buffer;
	tokenizer_buffer_init(&buffer);

	for (;;) {
		if (state.next == 0) {
			switch (mode) {
			case MODE_IDENTIFIER:
				tokens_add_identifier(&state, &tokens, &buffer);
				break;
			case MODE_NUMBER: {
				token token  = token_create(number_has_dot ? TOKEN_FLOAT : TOKEN_INT, &state);
				token.number = tokenizer_buffer_parse_number(&buffer);
				tokens_add(&tokens, token);
				break;
			}
			case MODE_SELECT:
			case MODE_LINE_COMMENT:
				break;
			case MODE_OPERATOR:
				error(context, "File ends with an operator");
			case MODE_COMMENT:
				error(context, "Unclosed comment");
			}

			tokens_add(&tokens, token_create(TOKEN_NONE, &state));
			////
			free(buffer.buf);
			////
			return tokens;
		}
		else {
			char ch = (char)state.next;
			switch (mode) {
			case MODE_SELECT: {
				if (ch == '/') {
					if (state.next_next >= 0) {
						char chch = state.next_next;
						switch (chch) {
						case '/':
							mode = MODE_LINE_COMMENT;
							break;
						case '*':
							mode = MODE_COMMENT;
							break;
						default:
							tokenizer_buffer_reset(&buffer, &state);
							tokenizer_buffer_add(&buffer, ch);
							mode = MODE_OPERATOR;
						}
					}
				}
				else if (is_num(ch, state.next_next)) {
					mode           = MODE_NUMBER;
					number_has_dot = false;
					tokenizer_buffer_reset(&buffer, &state);
					tokenizer_buffer_add(&buffer, ch);
				}
				else if (is_op(ch)) {
					mode = MODE_OPERATOR;
					tokenizer_buffer_reset(&buffer, &state);
					tokenizer_buffer_add(&buffer, ch);
				}
				else if (is_whitespace(ch)) {
				}
				else if (ch == '(') {
					tokens_add(&tokens, token_create(TOKEN_LEFT_PAREN, &state));
				}
				else if (ch == ')') {
					tokens_add(&tokens, token_create(TOKEN_RIGHT_PAREN, &state));
				}
				else if (ch == '{') {
					tokens_add(&tokens, token_create(TOKEN_LEFT_CURLY, &state));
				}
				else if (ch == '}') {
					tokens_add(&tokens, token_create(TOKEN_RIGHT_CURLY, &state));
				}
				else if (ch == '#') {
					tokens_add(&tokens, token_create(TOKEN_HASH, &state));
				}
				else if (ch == '[') {
					tokens_add(&tokens, token_create(TOKEN_LEFT_SQUARE, &state));
				}
				else if (ch == ']') {
					tokens_add(&tokens, token_create(TOKEN_RIGHT_SQUARE, &state));
				}
				else if (ch == ';') {
					tokens_add(&tokens, token_create(TOKEN_SEMICOLON, &state));
				}
				else if (ch == '.') {
					tokens_add(&tokens, token_create(TOKEN_DOT, &state));
				}
				else if (ch == ':') {
					tokens_add(&tokens, token_create(TOKEN_COLON, &state));
				}
				else if (ch == ',') {
					tokens_add(&tokens, token_create(TOKEN_COMMA, &state));
				}
				else if (ch == '"' || ch == '\'') {
					error(context, "Strings are not supported");
				}
				else {
					mode = MODE_IDENTIFIER;
					tokenizer_buffer_reset(&buffer, &state);
					tokenizer_buffer_add(&buffer, ch);
				}
				tokenizer_state_advance(&context, &state);
				break;
			}
			case MODE_LINE_COMMENT: {
				if (ch == '\n') {
					mode = MODE_SELECT;
				}
				tokenizer_state_advance(&context, &state);
				break;
			}
			case MODE_COMMENT: {
				if (ch == '*') {
					if (state.next_next >= 0) {
						char chch = (char)state.next_next;
						if (chch == '/') {
							mode = MODE_SELECT;
							tokenizer_state_advance(&context, &state);
						}
					}
				}
				tokenizer_state_advance(&context, &state);
				break;
			}
			case MODE_NUMBER: {
				if (is_num(ch, 0) || ch == '.') {
					if (ch == '.') {
						number_has_dot = true;
					}
					tokenizer_buffer_add(&buffer, ch);
					tokenizer_state_advance(&context, &state);
				}
				else {
					token token  = token_create(number_has_dot ? TOKEN_FLOAT : TOKEN_INT, &state);
					token.number = tokenizer_buffer_parse_number(&buffer);
					tokens_add(&tokens, token);
					mode = MODE_SELECT;
				}
				break;
			}
			case MODE_OPERATOR: {
				char long_op[3];
				long_op[0] = 0;
				if (buffer.current_size == 1) {
					long_op[0] = buffer.buf[0];
					long_op[1] = ch;
					long_op[2] = 0;
				}

				if (strcmp(long_op, "==") == 0 || strcmp(long_op, "!=") == 0 || strcmp(long_op, "<=") == 0 || strcmp(long_op, ">=") == 0 ||
				    strcmp(long_op, "||") == 0 || strcmp(long_op, "&&") == 0 || strcmp(long_op, "->") == 0 || strcmp(long_op, "-=") == 0 ||
				    strcmp(long_op, "+=") == 0 || strcmp(long_op, "/=") == 0 || strcmp(long_op, "*=") == 0 || strcmp(long_op, "<<") == 0 ||
				    strcmp(long_op, ">>") == 0) {
					tokenizer_buffer_add(&buffer, ch);
					tokenizer_state_advance(&context, &state);
				}

				if (tokenizer_buffer_equals(&buffer, "==")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_EQUALS;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "!=")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_NOT_EQUALS;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, ">")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_GREATER;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, ">=")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_GREATER_EQUAL;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "<")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_LESS;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "<=")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_LESS_EQUAL;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "-=")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_MINUS_ASSIGN;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "+=")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_PLUS_ASSIGN;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "/=")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_DIVIDE_ASSIGN;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "*=")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_MULTIPLY_ASSIGN;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "-")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_MINUS;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "+")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_PLUS;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "/")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_DIVIDE;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "*")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_MULTIPLY;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "!")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_NOT;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "||")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_OR;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "^")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_BITWISE_XOR;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "&")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_BITWISE_AND;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "|")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_BITWISE_OR;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "<<")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_LEFT_SHIFT;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, ">>")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_RIGHT_SHIFT;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "&&")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_AND;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "%")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_MOD;
					tokens_add(&tokens, token);
				}
				else if (tokenizer_buffer_equals(&buffer, "=")) {
					token token = token_create(TOKEN_OPERATOR, &state);
					token.op    = OPERATOR_ASSIGN;
					tokens_add(&tokens, token);
				}
				else {
					error(context, "Weird operator");
				}

				mode = MODE_SELECT;
				break;
			}
			case MODE_IDENTIFIER: {
				if (is_whitespace(ch) || is_op(ch) || ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '"' || ch == '\'' ||
				    ch == ';' || ch == '.' || ch == ',' || ch == ':') {
					tokens_add_identifier(&state, &tokens, &buffer);
					mode = MODE_SELECT;
				}
				else {
					tokenizer_buffer_add(&buffer, ch);
					tokenizer_state_advance(&context, &state);
				}
				break;
			}
			}
		}
	}
}

static void copy_opcode(opcode *o) {
	if (new_code.o == NULL) {
		new_code.o = (uint8_t *)malloc(OPCODES_SIZE);
	}

	uint8_t *new_data = &new_code.o[new_code.size];

	assert(new_code.size + o->size < OPCODES_SIZE);

	memcpy(new_data, o, o->size);

	new_code.size += o->size;
}

void transform(uint32_t flags) {
	for (function_id i = 0; get_function(i) != NULL; ++i) {
		function *f = get_function(i);

		if (f->block == NULL) {
			continue;
		}

		uint8_t *data = f->code.o;
		size_t   size = f->code.size;

		new_code.size = 0;

		size_t index = 0;
		while (index < size) {
			opcode *o = (opcode *)&data[index];

			switch (o->type) {
			case OPCODE_STORE_ACCESS_LIST: {
				kong_access a = o->op_store_access_list.access_list[o->op_store_access_list.access_list_size - 1];

				if ((flags & TRANSFORM_FLAG_ONE_COMPONENT_SWIZZLE) != 0 && a.kind == ACCESS_SWIZZLE && a.access_swizzle.swizzle.size > 1) {
					assert(is_vector(o->op_store_access_list.from.type.type));

					type_id from_base_type = vector_base_type(o->op_store_access_list.from.type.type);

					type_ref from_base_type_ref;
					init_type_ref(&from_base_type_ref, NO_NAME);
					from_base_type_ref.type = from_base_type;

					for (uint32_t swizzle_index = 0; swizzle_index < a.access_swizzle.swizzle.size; ++swizzle_index) {
						variable from = allocate_variable(from_base_type_ref, o->op_store_access_list.from.kind);

						opcode from_opcode = {
						    .type = OPCODE_LOAD_ACCESS_LIST,
						    .op_load_access_list =
						        {
						            .from             = o->op_store_access_list.from,
						            .to               = from,
						            .access_list_size = 1,
						            .access_list      = {{
						                     .type = from_base_type,
						                     .kind = ACCESS_SWIZZLE,
						                     .access_swizzle =
                                            {
						                             .swizzle =
                                                    {
						                                     .size    = 1,
						                                     .indices = {swizzle_index},
                                                    },
                                            },
                                    }},
						        },
						};
						from_opcode.size = OP_SIZE(from_opcode, op_load_access_list);

						copy_opcode(&from_opcode);

						opcode new_opcode = *o;

						new_opcode.op_store_access_list.from = from;

						kong_access *new_access = &new_opcode.op_store_access_list.access_list[new_opcode.op_store_access_list.access_list_size - 1];
						new_access->access_swizzle.swizzle.size       = 1;
						new_access->access_swizzle.swizzle.indices[0] = a.access_swizzle.swizzle.indices[swizzle_index];
						new_access->type                              = from_base_type;

						copy_opcode(&new_opcode);
					}
				}
				else {
					copy_opcode(o);
				}
				break;
			}
			case OPCODE_LOAD_ACCESS_LIST: {
				kong_access a = o->op_load_access_list.access_list[o->op_load_access_list.access_list_size - 1];

				if ((flags & TRANSFORM_FLAG_ONE_COMPONENT_SWIZZLE) != 0 && a.kind == ACCESS_SWIZZLE && a.access_swizzle.swizzle.size > 1) {
					assert(is_vector(o->op_load_access_list.to.type.type));

					type_id to_type = vector_base_type(o->op_load_access_list.to.type.type);

					type_ref t;
					init_type_ref(&t, NO_NAME);
					t.type = to_type;

					variable to[4];

					for (uint32_t swizzle_index = 0; swizzle_index < a.access_swizzle.swizzle.size; ++swizzle_index) {
						to[swizzle_index] = allocate_variable(t, o->op_load_access_list.to.kind);

						opcode new_opcode = *o;

						new_opcode.op_load_access_list.to = to[swizzle_index];

						kong_access *new_access = &new_opcode.op_load_access_list.access_list[new_opcode.op_load_access_list.access_list_size - 1];
						new_access->access_swizzle.swizzle.size       = 1;
						new_access->access_swizzle.swizzle.indices[0] = a.access_swizzle.swizzle.indices[swizzle_index];
						new_access->type                              = to_type;

						copy_opcode(&new_opcode);
					}

					opcode constructor_call = {
					    .type = OPCODE_CALL,
					    .op_call =
					        {
					            .func            = get_type(o->op_load_access_list.to.type.type)->name,
					            .parameters      = {to[0], to[1], to[2], to[3]},
					            .parameters_size = a.access_swizzle.swizzle.size,
					            .var             = o->op_load_access_list.to,
					        },
					};
					constructor_call.size = OP_SIZE(constructor_call, op_call);
					copy_opcode(&constructor_call);
				}
				else {
					copy_opcode(o);
				}
				break;
			}
			case OPCODE_ADD:
			case OPCODE_SUB:
			case OPCODE_MULTIPLY:
			case OPCODE_DIVIDE: {
				type_id left_type  = o->op_binary.left.type.type;
				type_id right_type = o->op_binary.right.type.type;

				if (is_matrix(left_type) || is_matrix(right_type)) {
					copy_opcode(o);
					break;
				}

				uint32_t left_size  = vector_size(left_type);
				uint32_t right_size = vector_size(right_type);

				if ((flags & TRANSFORM_FLAG_BINARY_UNIFY_LENGTH) != 0 && left_size != right_size) {
					if (left_size < right_size) {
						variable last;

						if (is_vector(left_type)) {
							type_ref t;
							init_type_ref(&t, NO_NAME);
							t.type = vector_base_type(left_type);

							last = allocate_variable(t, o->op_binary.left.kind);

							opcode load_call = {
							    .type = OPCODE_LOAD_ACCESS_LIST,
							    .op_load_access_list =
							        {
							            .from = o->op_binary.left,
							            .to   = last,
							            .access_list =
							                {
							                    {
							                        .kind = ACCESS_SWIZZLE,
							                        .type = t.type,
							                        .access_swizzle =
							                            {
							                                .swizzle =
							                                    {
							                                        .indices = {left_size - 1},
							                                        .size    = 1,
							                                    },
							                            },
							                    },
							                },
							            .access_list_size = 1,
							        },
							};
							load_call.size = OP_SIZE(load_call, op_load_access_list);

							copy_opcode(&load_call);
						}
						else {
							last = o->op_binary.left;
						}

						variable vec;
						{
							type_ref t;
							init_type_ref(&t, NO_NAME);
							t.type = vector_to_size(left_type, right_size);

							vec = allocate_variable(t, VARIABLE_INTERNAL);

							opcode constructor_call = {
							    .type = OPCODE_CALL,
							    .op_call =
							        {
							            .func            = get_type(right_type)->name,
							            .parameters_size = right_size,
							            .var             = vec,
							        },
							};
							constructor_call.size = OP_SIZE(constructor_call, op_call);

							constructor_call.op_call.parameters[0] = o->op_binary.left;

							for (uint32_t index = left_size; index < right_size; ++index) {
								constructor_call.op_call.parameters[index] = last;
							}

							copy_opcode(&constructor_call);
						}

						{
							opcode bin         = *o;
							bin.op_binary.left = vec;

							copy_opcode(&bin);
						}
					}
					else {
						variable last;

						if (is_vector(right_type)) {
							type_ref t;
							init_type_ref(&t, NO_NAME);
							t.type = vector_base_type(right_type);

							last = allocate_variable(t, o->op_binary.right.kind);

							opcode load_call = {
							    .type = OPCODE_LOAD_ACCESS_LIST,
							    .op_load_access_list =
							        {
							            .from = o->op_binary.right,
							            .to   = last,
							            .access_list =
							                {
							                    {
							                        .kind = ACCESS_SWIZZLE,
							                        .type = t.type,
							                        .access_swizzle =
							                            {
							                                .swizzle =
							                                    {
							                                        .indices = {right_size - 1},
							                                        .size    = 1,
							                                    },
							                            },
							                    },
							                },
							            .access_list_size = 1,
							        },
							};
							load_call.size = OP_SIZE(load_call, op_load_access_list);

							copy_opcode(&load_call);
						}
						else {
							last = o->op_binary.right;
						}

						variable vec;
						{
							type_ref t;
							init_type_ref(&t, NO_NAME);
							t.type = vector_to_size(left_type, left_size);

							vec = allocate_variable(t, VARIABLE_INTERNAL);

							opcode constructor_call = {
							    .type = OPCODE_CALL,
							    .op_call =
							        {
							            .func            = get_type(left_type)->name,
							            .parameters_size = left_size,
							            .var             = vec,
							        },
							};
							constructor_call.size = OP_SIZE(constructor_call, op_call);

							constructor_call.op_call.parameters[0] = o->op_binary.right;

							for (uint32_t index = right_size; index < left_size; ++index) {
								constructor_call.op_call.parameters[index] = last;
							}

							copy_opcode(&constructor_call);
						}

						{
							opcode bin          = *o;
							bin.op_binary.right = vec;

							copy_opcode(&bin);
						}
					}
				}
				else {
					copy_opcode(o);
				}
				break;
			}
			default:
				copy_opcode(o);
				break;
			}

			index += o->size;
		}

		uint8_t *old_buffer = f->code.o;
		f->code.o           = new_code.o;
		f->code.size        = new_code.size;
		new_code.o          = old_buffer;
	}
}

type_ref find_local_var_type(block *b, name_id name) {
	if (b == NULL) {
		type_ref t;
		init_type_ref(&t, NO_NAME);
		return t;
	}

	for (size_t i = 0; i < b->vars.size; ++i) {
		if (b->vars.v[i].name == name) {
			debug_context context = {0};
			check(b->vars.v[i].type.type != NO_TYPE, context, "Local var has no type");
			return b->vars.v[i].type;
		}
	}

	return find_local_var_type(b->parent, name);
}

void resolve_types_in_expression(statement *parent, expression *e);

static void resolve_types_in_element(statement *parent_block, expression *element) {
	resolve_types_in_expression(parent_block, element->element.of);
	resolve_types_in_expression(parent_block, element->element.element_index);

	type_id of_type = element->element.of->type.type;

	assert(of_type != NO_TYPE);

	type *of = get_type(of_type);

	if (of->tex_kind != TEXTURE_KIND_NONE) {
		element->type.type = float4_id;
	}
	else {
		if (of->array_size > 0) {
			element->type.type = of->base;
		}
		else {
			debug_context context = {0};
			error(context, "Indexed non-array %s", get_name(of->name));
		}
	}
}

static void resolve_types_in_member(statement *parent_block, expression *member) {
	resolve_types_in_expression(parent_block, member->member.of);

	type_id of_type     = member->member.of->type.type;
	name_id member_name = member->member.member_name;

	assert(of_type != NO_TYPE);

	if (is_vector_or_scalar(of_type)) {
		expression *of = member->member.of;

		member->kind       = EXPRESSION_SWIZZLE;
		member->swizzle.of = of;

		char    *name         = get_name(member_name);
		uint32_t swizzle_size = (uint32_t)strlen(name);

		if (swizzle_size > 4) {
			debug_context context = {0};
			error(context, "Swizzle size can not be more than four", get_name(member_name));
		}

		member->swizzle.swizz.size = swizzle_size;

		for (uint32_t swizzle_index = 0; swizzle_index < swizzle_size; ++swizzle_index) {
			switch (name[swizzle_index]) {
			case 'r':
			case 'x':
				member->swizzle.swizz.indices[swizzle_index] = 0;
				break;
			case 'g':
			case 'y':
				if (1 >= vector_size(of_type)) {
					debug_context context = {0};
					error(context, "Swizzle out of bounds", get_name(member_name));
				}
				member->swizzle.swizz.indices[swizzle_index] = 1;
				break;
			case 'b':
			case 'z':
				if (2 >= vector_size(of_type)) {
					debug_context context = {0};
					error(context, "Swizzle out of bounds", get_name(member_name));
				}
				member->swizzle.swizz.indices[swizzle_index] = 2;
				break;
			case 'a':
			case 'w':
				if (3 >= vector_size(of_type)) {
					debug_context context = {0};
					error(context, "Swizzle out of bounds", get_name(member_name));
				}
				member->swizzle.swizz.indices[swizzle_index] = 3;
				break;
			}
		}

		if (of_type == float_id || of_type == float2_id || of_type == float3_id || of_type == float4_id) {
			switch (swizzle_size) {
			case 1:
				member->type.type = float_id;
				break;
			case 2:
				member->type.type = float2_id;
				break;
			case 3:
				member->type.type = float3_id;
				break;
			case 4:
				member->type.type = float4_id;
				break;
			default:
				assert(false);
				break;
			}
		}
		else if (of_type == int_id || of_type == int2_id || of_type == int3_id || of_type == int4_id) {
			switch (swizzle_size) {
			case 1:
				member->type.type = int_id;
				break;
			case 2:
				member->type.type = int2_id;
				break;
			case 3:
				member->type.type = int3_id;
				break;
			case 4:
				member->type.type = int4_id;
				break;
			default:
				assert(false);
				break;
			}
		}
		else if (of_type == uint_id || of_type == uint2_id || of_type == uint3_id || of_type == uint4_id) {
			switch (swizzle_size) {
			case 1:
				member->type.type = uint_id;
				break;
			case 2:
				member->type.type = uint2_id;
				break;
			case 3:
				member->type.type = uint3_id;
				break;
			case 4:
				member->type.type = uint4_id;
				break;
			default:
				assert(false);
				break;
			}
		}
		else if (of_type == bool_id || of_type == bool2_id || of_type == bool3_id || of_type == bool4_id) {
			switch (swizzle_size) {
			case 1:
				member->type.type = bool_id;
				break;
			case 2:
				member->type.type = bool2_id;
				break;
			case 3:
				member->type.type = bool3_id;
				break;
			case 4:
				member->type.type = bool4_id;
				break;
			default:
				assert(false);
				break;
			}
		}
		else {
			assert(false);
		}
	}
	else {
		type *of_struct = get_type(of_type);

		for (size_t i = 0; i < of_struct->members.size; ++i) {
			if (of_struct->members.m[i].name == member_name) {
				member->type = of_struct->members.m[i].type;
				return;
			}
		}

		debug_context context = {0};
		error(context, "Member %s not found", get_name(member_name));
	}
}

static bool types_compatible(type_id left, type_id right) {
	if (left == right) {
		return true;
	}

	if ((left == int_id && right == float_id) || (left == float_id && right == int_id)) {
		return true;
	}
	if ((left == int2_id && right == float2_id) || (left == float2_id && right == int2_id)) {
		return true;
	}
	if ((left == int3_id && right == float3_id) || (left == float3_id && right == int3_id)) {
		return true;
	}
	if ((left == int4_id && right == float4_id) || (left == float4_id && right == int4_id)) {
		return true;
	}

	if ((left == uint_id && right == float_id) || (left == float_id && right == uint_id)) {
		return true;
	}
	if ((left == uint2_id && right == float2_id) || (left == float2_id && right == uint2_id)) {
		return true;
	}
	if ((left == uint3_id && right == float3_id) || (left == float3_id && right == uint3_id)) {
		return true;
	}
	if ((left == uint4_id && right == float4_id) || (left == float4_id && right == uint4_id)) {
		return true;
	}

	if ((left == uint_id && right == int_id) || (left == int_id && right == uint_id)) {
		return true;
	}
	if ((left == uint2_id && right == int2_id) || (left == int2_id && right == uint2_id)) {
		return true;
	}
	if ((left == uint3_id && right == int3_id) || (left == int3_id && right == uint3_id)) {
		return true;
	}
	if ((left == uint4_id && right == int4_id) || (left == int4_id && right == uint4_id)) {
		return true;
	}

	if ((left == float_id && right == float2_id) || (left == float_id && right == float3_id) || (left == float_id && right == float4_id) ||
	    (left == float2_id && right == float_id) || (left == float3_id && right == float_id) || (left == float4_id && right == float_id)) {
		return true;
	}

	if ((left == int_id && right == int2_id) || (left == int_id && right == int3_id) || (left == int_id && right == int4_id) ||
	    (left == int2_id && right == int_id) || (left == int3_id && right == int_id) || (left == int4_id && right == int_id)) {
		return true;
	}

	if ((left == uint_id && right == uint2_id) || (left == uint_id && right == uint3_id) || (left == uint_id && right == uint4_id) ||
	    (left == uint2_id && right == uint_id) || (left == uint3_id && right == uint_id) || (left == uint4_id && right == uint_id)) {
		return true;
	}

	if ((left == uint_id && right == int2_id) || (left == uint_id && right == int3_id) || (left == uint_id && right == int4_id) ||
	    (left == int2_id && right == uint_id) || (left == int3_id && right == uint_id) || (left == int4_id && right == uint_id)) {
		return true;
	}

	if ((left == int_id && right == uint2_id) || (left == int_id && right == uint3_id) || (left == int_id && right == uint4_id) ||
	    (left == uint2_id && right == int_id) || (left == uint3_id && right == int_id) || (left == uint4_id && right == int_id)) {
		return true;
	}

	return false;
}

static type_ref upgrade_type(type_ref left_type, type_ref right_type) {
	type_id left  = left_type.type;
	type_id right = right_type.type;

	if (left == right) {
		return left_type;
	}

	if (left == int_id && right == float_id) {
		return right_type;
	}
	if (left == float_id && right == int_id) {
		return left_type;
	}
	if (left == int2_id && right == float2_id) {
		return right_type;
	}
	if (left == float2_id && right == int2_id) {
		return left_type;
	}
	if (left == int3_id && right == float3_id) {
		return right_type;
	}
	if (left == float3_id && right == int3_id) {
		return left_type;
	}
	if (left == int4_id && right == float4_id) {
		return right_type;
	}
	if (left == float4_id && right == int4_id) {
		return left_type;
	}

	if (left == uint_id && right == float_id) {
		return right_type;
	}
	if (left == float_id && right == uint_id) {
		return left_type;
	}
	if (left == uint2_id && right == float2_id) {
		return right_type;
	}
	if (left == float2_id && right == uint2_id) {
		return left_type;
	}
	if (left == uint3_id && right == float3_id) {
		return right_type;
	}
	if (left == float3_id && right == uint3_id) {
		return left_type;
	}
	if (left == uint4_id && right == float4_id) {
		return right_type;
	}
	if (left == float4_id && right == uint4_id) {
		return left_type;
	}

	if (left == uint_id && right == int_id) {
		return right_type;
	}
	if (left == int_id && right == uint_id) {
		return left_type;
	}
	if (left == uint2_id && right == int2_id) {
		return right_type;
	}
	if (left == int2_id && right == uint2_id) {
		return left_type;
	}
	if (left == uint3_id && right == int3_id) {
		return right_type;
	}
	if (left == int3_id && right == uint3_id) {
		return left_type;
	}
	if (left == uint4_id && right == int4_id) {
		return right_type;
	}
	if (left == int4_id && right == uint4_id) {
		return left_type;
	}

	if ((left == float2_id && right == float_id) || (left == float3_id && right == float_id) || (left == float4_id && right == float_id)) {
		return left_type;
	}

	if ((left == float_id && right == float2_id) || (left == float_id && right == float3_id) || (left == float_id && right == float4_id)) {
		return right_type;
	}

	if ((left == int2_id && right == int_id) || (left == int3_id && right == int_id) || (left == int4_id && right == int_id)) {
		return left_type;
	}

	if ((left == int_id && right == int2_id) || (left == int_id && right == int3_id) || (left == int_id && right == int4_id)) {
		return right_type;
	}

	if ((left == uint2_id && right == uint_id) || (left == uint3_id && right == uint_id) || (left == uint4_id && right == uint_id)) {
		return left_type;
	}

	if ((left == uint_id && right == uint2_id) || (left == uint_id && right == uint3_id) || (left == uint_id && right == uint4_id)) {
		return right_type;
	}

	if ((left == uint2_id && right == int_id) || (left == uint3_id && right == int_id) || (left == uint4_id && right == int_id)) {
		return left_type;
	}

	if ((left == int_id && right == uint2_id) || (left == int_id && right == uint3_id) || (left == int_id && right == uint4_id)) {
		return right_type;
	}

	if ((left == int2_id && right == uint_id) || (left == int3_id && right == uint_id) || (left == int4_id && right == uint_id)) {
		return left_type;
	}

	if ((left == uint_id && right == int2_id) || (left == uint_id && right == int3_id) || (left == uint_id && right == int4_id)) {
		return right_type;
	}

	iron_log("Suspicious type upgrade");
	return left_type;
}

void resolve_types_in_expression(statement *parent, expression *e) {
	switch (e->kind) {
	case EXPRESSION_BINARY: {
		resolve_types_in_expression(parent, e->binary.left);
		resolve_types_in_expression(parent, e->binary.right);
		switch (e->binary.op) {
		case OPERATOR_EQUALS:
		case OPERATOR_NOT_EQUALS:
		case OPERATOR_GREATER:
		case OPERATOR_GREATER_EQUAL:
		case OPERATOR_LESS:
		case OPERATOR_LESS_EQUAL:
		case OPERATOR_OR:
		case OPERATOR_AND: {
			e->type.type = bool_id;
			break;
		}
		case OPERATOR_BITWISE_XOR:
		case OPERATOR_BITWISE_AND:
		case OPERATOR_BITWISE_OR:
		case OPERATOR_LEFT_SHIFT:
		case OPERATOR_RIGHT_SHIFT: {
			e->type = e->binary.left->type;
			break;
		}
		case OPERATOR_MULTIPLY:
		case OPERATOR_MULTIPLY_ASSIGN: {
			type_id left_type  = e->binary.left->type.type;
			type_id right_type = e->binary.right->type.type;
			if ((left_type == float4x4_id && right_type == float4_id) || (left_type == float3x3_id && right_type == float3_id)) {
				e->type = e->binary.right->type;
			}
			else if (right_type == float_id && (left_type == float2_id || left_type == float3_id || left_type == float4_id)) {
				e->type = e->binary.left->type;
			}
			else if (types_compatible(left_type, right_type)) {
				e->type = upgrade_type(e->binary.left->type, e->binary.right->type);
			}
			else {
				debug_context context = {0};
				error(context, "Type mismatch %s vs %s", get_name(get_type(left_type)->name), get_name(get_type(right_type)->name));
			}
			break;
		}
		case OPERATOR_MINUS:
		case OPERATOR_PLUS:
		case OPERATOR_DIVIDE:
		case OPERATOR_MOD: {
			type_id left_type  = e->binary.left->type.type;
			type_id right_type = e->binary.right->type.type;
			if (!types_compatible(left_type, right_type)) {
				debug_context context = {0};
				error(context, "Type mismatch %s vs %s", get_name(get_type(left_type)->name), get_name(get_type(right_type)->name));
			}
			e->type = upgrade_type(e->binary.left->type, e->binary.right->type);
			break;
		}
		case OPERATOR_ASSIGN:
		case OPERATOR_DIVIDE_ASSIGN:
		case OPERATOR_MINUS_ASSIGN:
		case OPERATOR_PLUS_ASSIGN: {
			type_id left_type  = e->binary.left->type.type;
			type_id right_type = e->binary.right->type.type;
			if (!types_compatible(left_type, right_type)) {
				debug_context context = {0};
				error(context, "Type mismatch %s vs %s", get_name(get_type(left_type)->name), get_name(get_type(right_type)->name));
			}
			e->type = e->binary.left->type;
			break;
		}
		case OPERATOR_NOT: {
			debug_context context = {0};
			error(context, "Weird binary operator");
			break;
		}
		}
		break;
	}
	case EXPRESSION_UNARY: {
		resolve_types_in_expression(parent, e->unary.right);
		switch (e->unary.op) {
		case OPERATOR_MINUS:
		case OPERATOR_PLUS: {
			e->type = e->unary.right->type;
			break;
		}
		case OPERATOR_NOT: {
			e->type.type = bool_id;
			break;
		}
		case OPERATOR_EQUALS:
		case OPERATOR_NOT_EQUALS:
		case OPERATOR_GREATER:
		case OPERATOR_GREATER_EQUAL:
		case OPERATOR_LESS:
		case OPERATOR_LESS_EQUAL:
		case OPERATOR_DIVIDE:
		case OPERATOR_MULTIPLY:
		case OPERATOR_OR:
		case OPERATOR_BITWISE_XOR:
		case OPERATOR_BITWISE_AND:
		case OPERATOR_BITWISE_OR:
		case OPERATOR_LEFT_SHIFT:
		case OPERATOR_RIGHT_SHIFT:
		case OPERATOR_AND:
		case OPERATOR_MOD:
		case OPERATOR_ASSIGN:
		default: {
			debug_context context = {0};
			error(context, "Weird unary operator");
			break;
		}
		}
		break;
	}
	case EXPRESSION_BOOLEAN: {
		e->type.type = bool_id;
		break;
	}
	case EXPRESSION_FLOAT: {
		e->type.type = float_id;
		break;
	}
	case EXPRESSION_INT: {
		e->type.type = int_id;
		break;
	}
	case EXPRESSION_VARIABLE: {
		global *g = find_global(e->variable);
		if (g != NULL && g->type != NO_TYPE) {
			e->type.type = g->type;
		}
		else {
			type_ref type = find_local_var_type(&parent->block, e->variable);
			if (type.type == NO_TYPE) {
				type                  = find_local_var_type(&parent->block, e->variable);
				debug_context context = {0};
				error(context, "Variable %s not found", get_name(e->variable));
			}
			e->type = type;
		}
		break;
	}
	case EXPRESSION_GROUPING: {
		resolve_types_in_expression(parent, e->grouping);
		e->type = e->grouping->type;
		break;
	}
	case EXPRESSION_CALL: {
		if (e->call.func_name == add_name("sample") || e->call.func_name == add_name("sample_lod")) {
			if (e->call.parameters.e[0]->kind == EXPRESSION_VARIABLE) {
				global *g = find_global(e->call.parameters.e[0]->variable);
				assert(g != NULL);
				e->type.type = float4_id;
			}
			else {
				e->type.type = float4_id;
			}
		}
		else {
			for (function_id i = 0; get_function(i) != NULL; ++i) {
				function *f = get_function(i);
				if (f->name == e->call.func_name) {
					e->type = f->return_type;
					break;
				}
			}
		}

		for (size_t i = 0; i < e->call.parameters.size; ++i) {
			resolve_types_in_expression(parent, e->call.parameters.e[i]);
		}
		break;
	}
	case EXPRESSION_MEMBER: {
		resolve_types_in_member(parent, e);
		break;
	}
	case EXPRESSION_ELEMENT: {
		resolve_types_in_element(parent, e);
		break;
	}
	case EXPRESSION_SWIZZLE:
		assert(false); // swizzle is created in the typer
		break;
	}

	if (e->type.type == NO_TYPE) {
		debug_context context = {0};
		// const char *n = get_name(e->call.func_name);
		error(context, "Could not resolve type");
	}
}

void resolve_types_in_block(statement *parent, statement *block) {
	debug_context context = {0};
	check(block->kind == STATEMENT_BLOCK, context, "Malformed block");

	for (size_t i = 0; i < block->block.statements.size; ++i) {
		statement *s = block->block.statements.s[i];
		switch (s->kind) {
		case STATEMENT_DISCARD:
			break;
		case STATEMENT_EXPRESSION: {
			resolve_types_in_expression(block, s->expression);
			break;
		}
		case STATEMENT_RETURN_EXPRESSION: {
			resolve_types_in_expression(block, s->expression);
			break;
		}
		case STATEMENT_IF: {
			resolve_types_in_expression(block, s->iffy.test);
			resolve_types_in_block(block, s->iffy.if_block);
			for (uint16_t i = 0; i < s->iffy.else_size; ++i) {
				if (s->iffy.else_tests[i] != NULL) {
					resolve_types_in_expression(block, s->iffy.else_tests[i]);
				}
				resolve_types_in_block(block, s->iffy.else_blocks[i]);
			}
			break;
		}
		case STATEMENT_WHILE:
		case STATEMENT_DO_WHILE: {
			resolve_types_in_expression(block, s->whiley.test);
			resolve_types_in_block(block, s->whiley.while_block);
			break;
		}
		case STATEMENT_BLOCK: {
			resolve_types_in_block(block, s);
			break;
		}
		case STATEMENT_LOCAL_VARIABLE: {
			name_id var_name      = s->local_variable.var.name;
			name_id var_type_name = s->local_variable.var.type.unresolved.name;

			if (s->local_variable.var.type.type == NO_TYPE && var_type_name != NO_NAME) {
				s->local_variable.var.type.type = find_type_by_ref(&s->local_variable.var.type);
			}

			if (s->local_variable.var.type.type == NO_TYPE) {
				debug_context context = {0};
				error(context, "Could not find type %s for %s", get_name(var_type_name), get_name(var_name));
			}

			if (s->local_variable.init != NULL) {
				resolve_types_in_expression(block, s->local_variable.init);
			}

			block->block.vars.v[block->block.vars.size].name = var_name;
			block->block.vars.v[block->block.vars.size].type = s->local_variable.var.type;
			++block->block.vars.size;
			break;
		}
		}
	}
}

void resolve_types(void) {
	for (type_id i = 0; get_type(i) != NULL; ++i) {
		type *s = get_type(i);
		for (size_t j = 0; j < s->members.size; ++j) {
			if (s->members.m[j].type.type == NO_TYPE) {
				name_id name              = s->members.m[j].type.unresolved.name;
				s->members.m[j].type.type = find_type_by_name(name);
				if (s->members.m[j].type.type == NO_TYPE) {
					debug_context context = {0};
					error(context, "Could not find type %s in %s", get_name(name), get_name(s->name));
				}
			}
		}
	}

	for (function_id i = 0; get_function(i) != NULL; ++i) {
		function *f = get_function(i);

		for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
			if (f->parameter_types[parameter_index].type == NO_TYPE) {
				name_id parameter_type_name              = f->parameter_types[parameter_index].unresolved.name;
				f->parameter_types[parameter_index].type = find_type_by_name(parameter_type_name);
				if (f->parameter_types[parameter_index].type == NO_TYPE) {
					debug_context context = {0};
					error(context, "Could not find type %s for %s", get_name(parameter_type_name), get_name(f->name));
				}
			}
		}

		if (f->return_type.type == NO_TYPE) {
			f->return_type.type = find_type_by_ref(&f->return_type);

			if (f->return_type.type == NO_TYPE) {
				error_no_context("Could not find type %s for %s", get_name(f->return_type.unresolved.name), get_name(f->name));
			}
		}
	}

	for (function_id i = 0; get_function(i) != NULL; ++i) {
		function *f = get_function(i);

		if (f->block == NULL) {
			// built in
			continue;
		}

		for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
			f->block->block.vars.v[f->block->block.vars.size].name        = f->parameter_names[parameter_index];
			f->block->block.vars.v[f->block->block.vars.size].type        = f->parameter_types[parameter_index];
			f->block->block.vars.v[f->block->block.vars.size].variable_id = 0;
			++f->block->block.vars.size;
		}

		resolve_types_in_block(NULL, f->block);
	}
}

void init_type_ref(type_ref *t, name_id name) {
	t->type                  = NO_TYPE;
	t->unresolved.name       = name;
	t->unresolved.array_size = 0;
}

void types_init(void) {
	type         *new_types = realloc(types, types_size * sizeof(type));
	debug_context context   = {0};
	check(new_types != NULL, context, "Could not allocate types");
	types           = new_types;
	next_type_index = 0;

	void_id                     = add_type(add_name("void"));
	get_type(void_id)->built_in = true;

	sampler_type_id                     = add_type(add_name("sampler"));
	get_type(sampler_type_id)->built_in = true;

	bool_id                      = add_type(add_name("bool"));
	get_type(bool_id)->built_in  = true;
	float_id                     = add_type(add_name("float"));
	get_type(float_id)->built_in = true;
	int_id                       = add_type(add_name("int"));
	get_type(int_id)->built_in   = true;
	uint_id                      = add_type(add_name("uint"));
	get_type(uint_id)->built_in  = true;

	float2_id                     = add_type(add_name("float2"));
	get_type(float2_id)->built_in = true;

	float3_id                     = add_type(add_name("float3"));
	get_type(float3_id)->built_in = true;

	float4_id                     = add_type(add_name("float4"));
	get_type(float4_id)->built_in = true;

	int2_id                     = add_type(add_name("int2"));
	get_type(int2_id)->built_in = true;

	int3_id                     = add_type(add_name("int3"));
	get_type(int3_id)->built_in = true;

	int4_id                     = add_type(add_name("int4"));
	get_type(int4_id)->built_in = true;

	uint2_id                     = add_type(add_name("uint2"));
	get_type(uint2_id)->built_in = true;

	uint3_id                     = add_type(add_name("uint3"));
	get_type(uint3_id)->built_in = true;

	uint4_id                     = add_type(add_name("uint4"));
	get_type(uint4_id)->built_in = true;

	bool2_id                     = add_type(add_name("bool2"));
	get_type(bool2_id)->built_in = true;

	bool3_id                     = add_type(add_name("bool3"));
	get_type(bool3_id)->built_in = true;

	bool4_id                     = add_type(add_name("bool4"));
	get_type(bool4_id)->built_in = true;

	float2x2_id                     = add_type(add_name("float2x2"));
	get_type(float2x2_id)->built_in = true;

	float3x2_id                     = add_type(add_name("float3x2"));
	get_type(float3x2_id)->built_in = true;

	float2x3_id                     = add_type(add_name("float2x3"));
	get_type(float2x3_id)->built_in = true;

	float4x2_id                     = add_type(add_name("float4x2"));
	get_type(float4x2_id)->built_in = true;

	float2x4_id                     = add_type(add_name("float2x4"));
	get_type(float2x4_id)->built_in = true;

	float3x3_id                     = add_type(add_name("float3x3"));
	get_type(float3x3_id)->built_in = true;

	float4x3_id                     = add_type(add_name("float4x3"));
	get_type(float4x3_id)->built_in = true;

	float3x4_id                     = add_type(add_name("float3x4"));
	get_type(float3x4_id)->built_in = true;

	float4x4_id                     = add_type(add_name("float4x4"));
	get_type(float4x4_id)->built_in = true;

	{
		ray_type_id                     = add_type(add_name("ray"));
		get_type(ray_type_id)->built_in = true;

		type *t = get_type(ray_type_id);

		t->members.m[t->members.size].name      = add_name("origin");
		t->members.m[t->members.size].type.type = float3_id;
		++t->members.size;

		t->members.m[t->members.size].name      = add_name("direction");
		t->members.m[t->members.size].type.type = float3_id;
		++t->members.size;

		t->members.m[t->members.size].name      = add_name("min");
		t->members.m[t->members.size].type.type = float_id;
		++t->members.size;

		t->members.m[t->members.size].name      = add_name("max");
		t->members.m[t->members.size].type.type = float_id;
		++t->members.size;
	}

	{
		bvh_type_id                     = add_type(add_name("bvh"));
		get_type(bvh_type_id)->built_in = true;
	}

	{
		function_type_id                     = add_type(add_name("fun"));
		get_type(function_type_id)->built_in = true;
	}
}

static void grow_types_if_needed(uint64_t size) {
	while (size >= types_size) {
		types_size *= 2;
		type         *new_types = realloc(types, types_size * sizeof(type));
		debug_context context   = {0};
		check(new_types != NULL, context, "Could not allocate types");
		types = new_types;
	}
}

static bool types_equal(type *a, type *b) {
	return a->name == b->name && a->attributes.attributes_count == 0 && b->attributes.attributes_count == 0 && a->members.size == 0 && b->members.size == 0 &&
	       a->built_in == b->built_in && a->array_size == b->array_size && a->base == b->base &&
	       a->tex_kind == b->tex_kind; // && a->tex_format == b->tex_format;
}

type_id add_type(name_id name) {
	grow_types_if_needed(next_type_index + 1);

	type_id s = next_type_index;
	++next_type_index;

	types[s].name                        = name;
	types[s].attributes.attributes_count = 0;
	types[s].members.size                = 0;
	types[s].built_in                    = false;
	types[s].array_size                  = 0;
	types[s].base                        = NO_TYPE;
	types[s].tex_kind                    = TEXTURE_KIND_NONE;

	return s;
}

type_id add_full_type(type *t) {
	for (type_id type_index = 0; type_index < next_type_index; ++type_index) {
		if (types_equal(&types[type_index], t)) {
			return type_index;
		}
	}

	grow_types_if_needed(next_type_index + 1);

	type_id s = next_type_index;
	++next_type_index;

	types[s] = *t;

	return s;
}

type_id find_type_by_name(name_id name) {
	debug_context context = {0};
	check(name != NO_NAME, context, "Attempted to find a no-name");
	for (type_id i = 0; i < next_type_index; ++i) {
		if (types[i].name == name) {
			return i;
		}
	}

	return NO_TYPE;
}

type_id find_type_by_ref(type_ref *t) {
	if (t->type != NO_TYPE) {
		return t->type;
	}

	debug_context context = {0};
	check(t->unresolved.name != NO_NAME, context, "Attempted to find a no-name");

	type_id base_type_id = NO_TYPE;

	for (type_id i = 0; i < next_type_index; ++i) {
		if (types[i].name == t->unresolved.name) {
			if (types[i].array_size == t->unresolved.array_size) {
				return i;
			}
			base_type_id = i;
		}
	}

	if (base_type_id != NO_TYPE) {
		type_id new_type_id  = add_type(t->unresolved.name);
		type   *new_type     = get_type(new_type_id);
		new_type->array_size = t->unresolved.array_size;

		type *base_type    = get_type(base_type_id);
		new_type->base     = base_type_id;
		new_type->built_in = base_type->built_in;

		return new_type_id;
	}

	return NO_TYPE;
}

type *get_type(type_id s) {
	if (s >= next_type_index) {
		return NULL;
	}
	return &types[s];
}

bool has_attribute(attribute_list *attributes, name_id name) {
	for (uint8_t index = 0; index < attributes->attributes_count; ++index) {
		if (attributes->attributes[index].name == name) {
			return true;
		}
	}
	return false;
}

attribute *find_attribute(attribute_list *attributes, name_id name) {
	for (uint8_t index = 0; index < attributes->attributes_count; ++index) {
		if (attributes->attributes[index].name == name) {
			return &attributes->attributes[index];
		}
	}
	return NULL;
}

bool is_vector_or_scalar(type_id t) {
	return t == float_id || t == float2_id || t == float3_id || t == float4_id || t == int_id || t == int2_id || t == int3_id || t == int4_id || t == uint_id ||
	       t == uint2_id || t == uint3_id || t == uint4_id || t == bool_id || t == bool2_id || t == bool3_id || t == bool4_id;
}

bool is_vector(type_id t) {
	return t == float2_id || t == float3_id || t == float4_id || t == int2_id || t == int3_id || t == int4_id || t == uint2_id || t == uint3_id ||
	       t == uint4_id || t == bool2_id || t == bool3_id || t == bool4_id;
}

bool is_matrix(type_id t) {
	return t == float2x2_id || t == float2x3_id || t == float3x2_id || t == float3x3_id || t == float2x4_id || t == float4x2_id || t == float3x4_id ||
	       t == float4x3_id || t == float4x4_id;
}

uint32_t vector_size(type_id t) {
	if (t == float_id || t == int_id || t == uint_id || t == bool_id) {
		return 1u;
	}

	if (t == float2_id || t == int2_id || t == uint2_id || t == bool2_id) {
		return 2u;
	}

	if (t == float3_id || t == int3_id || t == uint3_id || t == bool3_id) {
		return 3u;
	}

	if (t == float4_id || t == int4_id || t == uint4_id || t == bool4_id) {
		return 4u;
	}

	assert(false);
	return 0;
}

type_id vector_base_type(type_id vector_type) {
	if (vector_type == float_id || vector_type == float2_id || vector_type == float3_id || vector_type == float4_id) {
		return float_id;
	}
	if (vector_type == int_id || vector_type == int2_id || vector_type == int3_id || vector_type == int4_id) {
		return int_id;
	}
	if (vector_type == uint_id || vector_type == uint2_id || vector_type == uint3_id || vector_type == uint4_id) {
		return uint_id;
	}
	if (vector_type == bool_id || vector_type == bool2_id || vector_type == bool3_id || vector_type == bool4_id) {
		return bool_id;
	}

	assert(false);
	return float_id;
}

type_id vector_to_size(type_id vector_type, uint32_t size) {
	type_id base_type = vector_base_type(vector_type);
	if (base_type == float_id) {
		switch (size) {
		case 1u:
			return float_id;
		case 2u:
			return float2_id;
		case 3u:
			return float3_id;
		case 4u:
			return float4_id;
		default:
			assert(false);
			return float_id;
		}
	}
	else if (base_type == int_id) {
		switch (size) {
		case 1u:
			return int_id;
		case 2u:
			return int2_id;
		case 3u:
			return int3_id;
		case 4u:
			return int4_id;
		default:
			assert(false);
			return int_id;
		}
	}
	else if (base_type == uint_id) {
		switch (size) {
		case 1u:
			return uint_id;
		case 2u:
			return uint2_id;
		case 3u:
			return uint3_id;
		case 4u:
			return uint4_id;
		default:
			assert(false);
			return uint_id;
		}
	}
	else if (base_type == bool_id) {
		switch (size) {
		case 1u:
			return bool_id;
		case 2u:
			return bool2_id;
		case 3u:
			return bool3_id;
		case 4u:
			return bool4_id;
		default:
			assert(false);
			return bool_id;
		}
	}
	else {
		assert(false);
		return float_id;
	}
}

void indent(char *code, size_t *offset, int indentation) {
	indentation = indentation < 15 ? indentation : 15;
	char str[16];
	memset(str, '\t', sizeof(str));
	str[indentation] = 0;
	*offset += sprintf(&code[*offset], "%s", str);
}

#ifdef IRON_METAL
extern size_t vertex_inputs_size;
extern size_t fragment_inputs_size;
extern size_t vertex_functions_size;
extern size_t fragment_functions_size;
#endif

uint64_t    _next_variable_id;
size_t      _allocated_globals_size;
function_id _next_function_index;
global_id   _globals_size;
name_id     _names_index;
size_t      _sets_count;
type_id     _next_type_index;
size_t      _vertex_inputs_size;
size_t      _fragment_inputs_size;
size_t      _vertex_functions_size;
size_t      _fragment_functions_size;
struct {
	char   *key;
	name_id value;
}   *_hash;
int  _expression_index;
int  _statement_index;
void hlsl_export2(char **vs, char **fs, api_kind d3d, bool debug);
void spirv_export2(char **vs, char **fs, int *vs_size, int *fs_size, bool debug);
void wgsl_export2(char **vs, char **fs);
void console_info(char *s);
#ifdef AMAKE
void console_info(char *s) {}
#endif

static struct {
	char   *key;
	name_id value;
} *_clone_hash(struct {
	char   *key;
	name_id value;
} * hash) {
	struct {
		char   *key;
		name_id value;
	} *clone = NULL;
	sh_new_arena(clone);
	ptrdiff_t len = shlen(hash);
	for (ptrdiff_t i = 0; i < len; i++) {
		shput(clone, hash[i].key, hash[i].value);
	}
	return clone;
}

void gpu_create_shaders_from_kong(char *kong, char **vs, char **fs, int *vs_size, int *fs_size) {
	static bool first = true;
	if (first) {
		first = false;
		names_init();
		types_init();
		functions_init();

		_next_variable_id       = next_variable_id;
		_allocated_globals_size = allocated_globals_size;
		_next_function_index    = next_function_index;
		_globals_size           = globals_size;
		_names_index            = names_index;
		_sets_count             = sets_count;
		_next_type_index        = next_type_index;

#ifdef IRON_METAL
		_vertex_inputs_size      = vertex_inputs_size;
		_fragment_inputs_size    = fragment_inputs_size;
		_vertex_functions_size   = vertex_functions_size;
		_fragment_functions_size = fragment_functions_size;
#endif

		_hash             = _clone_hash(hash);
		_expression_index = expression_index;
		_statement_index  = statement_index;
	}
	else {
		next_variable_id       = _next_variable_id;
		allocated_globals_size = _allocated_globals_size;
		next_function_index    = _next_function_index;
		globals_size           = _globals_size;
		names_index            = _names_index;
		sets_count             = _sets_count;
		next_type_index        = _next_type_index;

#ifdef IRON_METAL
		vertex_inputs_size      = _vertex_inputs_size;
		fragment_inputs_size    = _fragment_inputs_size;
		vertex_functions_size   = _vertex_functions_size;
		fragment_functions_size = _fragment_functions_size;
#endif

		shfree(hash);
		hash             = _clone_hash(_hash);
		expression_index = _expression_index;
		statement_index  = _statement_index;
	}

	kong_error    = false;
	char  *from   = "";
	tokens tokens = tokenize(from, kong);
	parse(from, &tokens);
	resolve_types();

	if (kong_error) {
		console_info("Warning: Shader compilation failed");
		free(tokens.t);
#if defined(__APPLE__)
		*vs = "";
		*fs = "";
#endif
		return;
	}
	allocate_globals();
	for (function_id i = 0; get_function(i) != NULL; ++i) {
		compile_function_block(&get_function(i)->code, get_function(i)->block);
	}
	analyze();

#ifdef _WIN32

	hlsl_export2(vs, fs, API_DIRECT3D11, false);

#elif defined(__APPLE__)

	static char vs_temp[1024 * 128];
	strcpy(vs_temp, "//>kong_vert\n");
	char *metal = metal_export("");
	strcat(vs_temp, metal);
	*vs = &vs_temp[0];
	*fs = "//>kong_frag\n";
	free(metal);

#elif defined(IRON_WASM)

	transform(TRANSFORM_FLAG_ONE_COMPONENT_SWIZZLE);
	wgsl_export2(vs, fs);

#else

	transform(TRANSFORM_FLAG_ONE_COMPONENT_SWIZZLE | TRANSFORM_FLAG_BINARY_UNIFY_LENGTH);
	spirv_export2(vs, fs, vs_size, fs_size, false);

#endif

	free(tokens.t);
}
