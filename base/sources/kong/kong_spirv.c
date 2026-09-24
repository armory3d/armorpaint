#include "kong.h"
#include "stb_ds.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum spirv_opcode {
	SPIRV_OPCODE_EXT_INST_IMPORT     = 11,
	SPIRV_OPCODE_EXT_INST            = 12,
	SPIRV_OPCODE_MEMORY_MODEL        = 14,
	SPIRV_OPCODE_ENTRY_POINT         = 15,
	SPIRV_OPCODE_EXECUTION_MODE      = 16,
	SPIRV_OPCODE_CAPABILITY          = 17,
	SPIRV_OPCODE_TYPE_VOID           = 19,
	SPIRV_OPCODE_TYPE_BOOL           = 20,
	SPIRV_OPCODE_TYPE_INT            = 21,
	SPIRV_OPCODE_TYPE_FLOAT          = 22,
	SPIRV_OPCODE_TYPE_VECTOR         = 23,
	SPIRV_OPCODE_TYPE_MATRIX         = 24,
	SPIRV_OPCODE_TYPE_IMAGE          = 25,
	SPIRV_OPCODE_TYPE_SAMPLER        = 26,
	SPIRV_OPCODE_TYPE_SAMPLED_IMAGE  = 27,
	SPIRV_OPCODE_TYPE_ARRAY          = 28,
	SPIRV_OPCODE_TYPE_STRUCT         = 30,
	SPIRV_OPCODE_TYPE_POINTER        = 32,
	SPIRV_OPCODE_TYPE_FUNCTION       = 33,
	SPIRV_OPCODE_CONSTANT            = 43,
	SPIRV_OPCODE_CONSTANT_COMPOSITE  = 44,
	SPIRV_OPCODE_FUNCTION            = 54,
	SPIRV_OPCODE_FUNCTION_PARAMETER  = 55,
	SPIRV_OPCODE_FUNCTION_END        = 56,
	SPIRV_OPCODE_FUNCTION_CALL       = 57,
	SPIRV_OPCODE_VARIABLE            = 59,
	SPIRV_OPCODE_LOAD                = 61,
	SPIRV_OPCODE_STORE               = 62,
	SPIRV_OPCODE_ACCESS_CHAIN        = 65,
	SPIRV_OPCODE_DECORATE            = 71,
	SPIRV_OPCODE_MEMBER_DECORATE     = 72,
	SPIRV_OPCODE_COMPOSITE_CONSTRUCT = 80,
	SPIRV_OPCODE_COMPOSITE_EXTRACT   = 81,
	////
	SPIRV_OPCODE_TRANSPOSE = 84,
	////
	SPIRV_OPCODE_SAMPLED_IMAGE             = 86,
	SPIRV_OPCODE_IMAGE_SAMPLE_IMPLICIT_LOD = 87,
	SPIRV_OPCODE_IMAGE_SAMPLE_EXPLICIT_LOD = 88,
	////
	SPIRV_OPCODE_IMAGE_FETCH = 95,
	////
	SPIRV_OPCODE_IMAGE_READ               = 98,
	SPIRV_OPCODE_IMAGE_WRITE              = 99,
	SPIRV_OPCODE_CONVERT_F_TO_U           = 109,
	SPIRV_OPCODE_CONVERT_F_TO_S           = 110,
	SPIRV_OPCODE_CONVERT_S_TO_F           = 111,
	SPIRV_OPCODE_CONVERT_U_TO_F           = 112,
	SPIRV_OPCODE_BITCAST                  = 124,
	SPIRV_OPCODE_S_NEGATE                 = 126,
	SPIRV_OPCODE_F_NEGATE                 = 127,
	SPIRV_OPCODE_I_ADD                    = 128,
	SPIRV_OPCODE_F_ADD                    = 129,
	SPIRV_OPCODE_I_SUB                    = 130,
	SPIRV_OPCODE_F_SUB                    = 131,
	SPIRV_OPCODE_I_MUL                    = 132,
	SPIRV_OPCODE_F_MUL                    = 133,
	SPIRV_OPCODE_U_DIV                    = 134,
	SPIRV_OPCODE_S_DIV                    = 135,
	SPIRV_OPCODE_F_DIV                    = 136,
	SPIRV_OPCODE_U_MOD                    = 137,
	SPIRV_OPCODE_S_MOD                    = 139,
	SPIRV_OPCODE_F_MOD                    = 141,
	SPIRV_OPCODE_VECTOR_TIMES_MATRIX      = 144,
	SPIRV_OPCODE_MATRIX_TIMES_VECTOR      = 145,
	SPIRV_OPCODE_MATRIX_TIMES_MATRIX      = 146,
	SPIRV_OPCODE_DOT                      = 148,
	SPIRV_OPCODE_LOGICAL_OR               = 166,
	SPIRV_OPCODE_LOGICAL_AND              = 167,
	SPIRV_OPCODE_LOGICAL_NOT              = 168,
	SPIRV_OPCODE_I_EQUAL                  = 170,
	SPIRV_OPCODE_I_NOT_EQUAL              = 171,
	SPIRV_OPCODE_U_GREATER_THAN           = 172,
	SPIRV_OPCODE_S_GREATER_THAN           = 173,
	SPIRV_OPCODE_U_GREATER_THAN_EQUAL     = 174,
	SPIRV_OPCODE_S_GREATER_THAN_EQUAL     = 175,
	SPIRV_OPCODE_U_LESS_THAN              = 176,
	SPIRV_OPCODE_S_LESS_THAN              = 177,
	SPIRV_OPCODE_U_LESS_THAN_EQUAL        = 178,
	SPIRV_OPCODE_S_LESS_THAN_EQUAL        = 179,
	SPIRV_OPCODE_F_ORD_EQUAL              = 180,
	SPIRV_OPCODE_F_ORD_NOT_EQUAL          = 182,
	SPIRV_OPCODE_F_ORD_LESS_THAN          = 184,
	SPIRV_OPCODE_F_ORD_GREATER_THAN       = 186,
	SPIRV_OPCODE_F_ORD_LESS_THAN_EQUAL    = 188,
	SPIRV_OPCODE_F_ORD_GREATER_THAN_EQUAL = 190,
	SPIRV_OPCODE_SHIFT_RIGHT_LOGICAL      = 194,
	SPIRV_OPCODE_SHIFT_RIGHT_ARITHMETIC   = 195,
	SPIRV_OPCODE_SHIFT_LEFT_LOGICAL       = 196,
	SPIRV_OPCODE_BITWISE_OR               = 197,
	SPIRV_OPCODE_BITWISE_XOR              = 198,
	SPIRV_OPCODE_BITWISE_AND              = 199,
	SPIRV_OPCODE_DPDX                     = 207,
	SPIRV_OPCODE_DPDY                     = 208,
	SPIRV_OPCODE_LOOP_MERGE               = 246,
	SPIRV_OPCODE_SELECTION_MERGE          = 247,
	SPIRV_OPCODE_LABEL                    = 248,
	SPIRV_OPCODE_BRANCH                   = 249,
	SPIRV_OPCODE_BRANCH_CONDITIONAL       = 250,
	SPIRV_OPCODE_KILL                     = 252,
	SPIRV_OPCODE_RETURN                   = 253,
	SPIRV_OPCODE_RETURN_VALUE             = 254,
} spirv_opcode;

typedef enum spirv_glsl_std {
	SPIRV_GLSL_STD_ROUND = 1,
	SPIRV_GLSL_STD_FABS  = 4,
	SPIRV_GLSL_STD_FLOOR = 8,
	SPIRV_GLSL_STD_CEIL  = 9,
	SPIRV_GLSL_STD_FRACT = 10,
	SPIRV_GLSL_STD_SIN   = 13,
	SPIRV_GLSL_STD_COS   = 14,
	////
	SPIRV_GLSL_STD_TRUNC   = 3,
	SPIRV_GLSL_STD_FSIGN   = 6,
	SPIRV_GLSL_STD_RADIANS = 11,
	SPIRV_GLSL_STD_DEGREES = 12,
	SPIRV_GLSL_STD_TAN     = 15,
	SPIRV_GLSL_STD_SINH    = 19,
	SPIRV_GLSL_STD_COSH    = 20,
	SPIRV_GLSL_STD_TANH    = 21,
	SPIRV_GLSL_STD_EXP     = 27,
	SPIRV_GLSL_STD_LOG     = 28,
	////
	SPIRV_GLSL_STD_ASIN         = 16,
	SPIRV_GLSL_STD_ACOS         = 17,
	SPIRV_GLSL_STD_ATAN         = 18,
	SPIRV_GLSL_STD_ATAN2        = 25,
	SPIRV_GLSL_STD_POW          = 26,
	SPIRV_GLSL_STD_SQRT         = 31,
	SPIRV_GLSL_STD_INVERSE_SQRT = 32,
	SPIRV_GLSL_STD_FMIN         = 37,
	SPIRV_GLSL_STD_FMAX         = 40,
	SPIRV_GLSL_STD_FCLAMP       = 43,
	SPIRV_GLSL_STD_FMIX         = 46,
	SPIRV_GLSL_STD_STEP         = 48,
	SPIRV_GLSL_STD_SMOOTHSTEP   = 49,
	SPIRV_GLSL_STD_LENGTH       = 66,
	SPIRV_GLSL_STD_DISTANCE     = 67,
	SPIRV_GLSL_STD_CROSS        = 68,
	SPIRV_GLSL_STD_NORMALIZE    = 69,
	SPIRV_GLSL_STD_REFLECT      = 71,
} spirv_glsl_std;

typedef enum addressing_model {
	ADDRESSING_MODEL_LOGICAL = 0
} addressing_model;

typedef enum memory_model {
	MEMORY_MODEL_SIMPLE  = 0,
	MEMORY_MODEL_GLSL450 = 1
} memory_model;

typedef enum capability {
	CAPABILITY_SHADER                             = 1,
	CAPABILITY_STORAGE_IMAGE_READ_WITHOUT_FORMAT  = 55,
	CAPABILITY_STORAGE_IMAGE_WRITE_WITHOUT_FORMAT = 56,
} capability;

typedef enum execution_model {
	EXECUTION_MODEL_VERTEX    = 0,
	EXECUTION_MODEL_FRAGMENT  = 4,
	EXECUTION_MODEL_GLCOMPUTE = 5
} execution_model;

typedef enum decoration {
	DECORATION_BLOCK          = 2,
	DECORATION_COL_MAJOR      = 5,
	DECORATION_MATRIX_STRIDE  = 7,
	DECORATION_BUILTIN        = 11,
	DECORATION_LOCATION       = 30,
	DECORATION_BINDING        = 33,
	DECORATION_DESCRIPTOR_SET = 34,
	DECORATION_OFFSET         = 35
} decoration;

typedef enum builtin {
	BUILTIN_POSITION             = 0,
	BUILTIN_WORKGROUP_SIZE       = 25,
	BUILTIN_WORKGROUP_ID         = 26,
	BUILTIN_LOCAL_INVOCATION_ID  = 27,
	BUILTIN_GLOBAL_INVOCATION_ID = 28,
	BUILTIN_VERTEX_INDEX         = 42,
} builtin;

typedef enum storage_class {
	STORAGE_CLASS_UNIFORM_CONSTANT = 0,
	STORAGE_CLASS_INPUT            = 1,
	STORAGE_CLASS_UNIFORM          = 2,
	STORAGE_CLASS_OUTPUT           = 3,
	STORAGE_CLASS_FUNCTION         = 7,
	STORAGE_CLASS_PUSH_CONSTANT    = 9,
	STORAGE_CLASS_NONE             = 9999
} storage_class;

typedef enum selection_control {
	SELECTION_CONTROL_NONE         = 0,
	SELCTION_CONTROL_FLATTEN       = 1,
	SELECTION_CONTROL_DONT_FLATTEN = 2,
} selection_control;

typedef enum loop_control {
	LOOP_CONTROL_NONE        = 0,
	LOOP_CONTROL_UNROLL      = 1,
	LOOP_CONTROL_DONT_UNROLL = 2,
} loop_control;

typedef enum function_control {
	FUNCTION_CONTROL_NONE
} function_control;

typedef enum execution_mode {
	EXECUTION_MODE_ORIGIN_UPPER_LEFT = 7,
	EXECUTION_MODE_LOCAL_SIZE        = 17,
} execution_mode;

typedef enum dim {
	DIM_1D   = 0,
	DIM_2D   = 1,
	DIM_3D   = 2,
	DIM_CUBE = 3,
} dim;

typedef enum image_format {
	IMAGE_FORMAT_UNKNOWN     = 0,
	IMAGE_FORMAT_RGBA32F     = 1,
	IMAGE_FORMAT_RGBA16F     = 2,
	IMAGE_FORMAT_R32F        = 3,
	IMAGE_FORMAT_RGBA8       = 4,
	IMAGE_FORMAT_RGBA8_SNORM = 5,
} image_format;

typedef struct spirv_id {
	uint32_t id;
} spirv_id;

typedef struct instructions_buffer {
	uint32_t *instructions;
	size_t    offset;
} instructions_buffer;

static uint32_t operands_buffer[4096];

typedef struct pointer_relation {
	spirv_id non_pointer_type_id;
	spirv_id pointer_type_id;
} pointer_relation;

static_array(pointer_relation, written_pointers, 256);
static written_pointers written_pointer_relations;
static spirv_id         output_struct_pointer_type = {0};

typedef struct int_constant_container {
	struct container container;
	spirv_id         value;
} int_constant_container;

static struct hash_map *int_constants = NULL;

static struct {
	uint32_t key;
	spirv_id value;
} *uint_constants = NULL;

static struct {
	float    key;
	spirv_id value;
} *float_constants = NULL;

static struct {
	bool     key;
	spirv_id value;
} *bool_constants = NULL;

static struct {
	name_id  key;
	spirv_id value;
} *function_map = NULL;

static spirv_id per_vertex_var    = {0};
static spirv_id output_vars[256]  = {0};
static type_id  output_types[256] = {0};
static size_t   output_vars_count = 0;

static spirv_id input_vars[256]  = {0};
static type_id  input_types[256] = {0};
static size_t   input_vars_count = 0;

static uint32_t vertex_parameter_indices[256];
static uint32_t vertex_parameter_member_indices[256];

static void write_buffer(FILE *file, uint8_t *output, size_t output_size) {
	for (size_t i = 0; i < output_size; ++i) {
		// based on the encoding described in https://github.com/adobe/bin2c
		if (output[i] == '!' || output[i] == '#' || (output[i] >= '%' && output[i] <= '>') || (output[i] >= 'A' && output[i] <= '[') ||
		    (output[i] >= ']' && output[i] <= '~')) {
			fprintf(file, "%c", output[i]);
		}
		else if (output[i] == '\a') {
			fprintf(file, "\\a");
		}
		else if (output[i] == '\b') {
			fprintf(file, "\\b");
		}
		else if (output[i] == '\t') {
			fprintf(file, "\\t");
		}
		else if (output[i] == '\v') {
			fprintf(file, "\\v");
		}
		else if (output[i] == '\f') {
			fprintf(file, "\\f");
		}
		else if (output[i] == '\r') {
			fprintf(file, "\\r");
		}
		else if (output[i] == '\"') {
			fprintf(file, "\\\"");
		}
		else if (output[i] == '\\') {
			fprintf(file, "\\\\");
		}
		else {
			fprintf(file, "\\%03o", output[i]);
		}
	}
}

static type_id find_access_type(int *indices, access_kind *access_kinds, int indices_size, type_id base_type) {
	if (get_type(base_type)->tex_kind == TEXTURE_KIND_2D) {
		kong_assert(indices_size == 1);
		return float4_id;
	}

	if (indices_size == 1) {
		switch (access_kinds[0]) {
		case ACCESS_ELEMENT: {
			type *t = get_type(base_type);
			return t->base;
		}
		case ACCESS_SWIZZLE:
			return vector_base_type(base_type);
		case ACCESS_MEMBER: {
			type *t = get_type(base_type);
			kong_assert(indices[0] < t->members.size);
			return t->members.m[indices[0]].type.type;
		}
		}

		kong_assert(false);
		return base_type;
	}
	else {
		switch (access_kinds[0]) {
		case ACCESS_ELEMENT: {
			type *t = get_type(base_type);
			return find_access_type(&indices[1], &access_kinds[1], indices_size - 1, t->base);
		}
		case ACCESS_SWIZZLE:
			return base_type;
		case ACCESS_MEMBER: {
			type *t = get_type(base_type);
			kong_assert(indices[0] < t->members.size);
			return find_access_type(&indices[1], &access_kinds[1], indices_size - 1, t->members.m[indices[0]].type.type);
		}
		}

		kong_assert(false);
		return base_type;
	}
}

static void write_simple_instruction(instructions_buffer *instructions, spirv_opcode o) {
	instructions->instructions[instructions->offset++] = (1 << 16) | (uint16_t)o;
}

static void write_instruction(instructions_buffer *instructions, uint16_t word_count, spirv_opcode o, uint32_t *operands) {
	instructions->instructions[instructions->offset++] = (word_count << 16) | (uint16_t)o;
	for (uint16_t i = 0; i < word_count - 1; ++i) {
		instructions->instructions[instructions->offset++] = operands[i];
	}
}

static void write_magic_number(instructions_buffer *instructions) {
	instructions->instructions[instructions->offset++] = 0x07230203;
}

static void write_version_number(instructions_buffer *instructions) {
	instructions->instructions[instructions->offset++] = 0x00010000;
}

static void write_generator_magic_number(instructions_buffer *instructions) {
	instructions->instructions[instructions->offset++] = 44;
}

static uint32_t next_index = 1;

static void write_bound(instructions_buffer *instructions) {
	instructions->instructions[instructions->offset++] = next_index;
}

static void write_instruction_schema(instructions_buffer *instructions) {
	instructions->instructions[instructions->offset++] = 0; // reserved in SPIR-V for later use, currently always zero
}

static void write_capability(instructions_buffer *instructions, capability c) {
	uint32_t operand = (uint32_t)c;
	write_instruction(instructions, 2, SPIRV_OPCODE_CAPABILITY, &operand);
}

static spirv_id allocate_index(void) {
	uint32_t result = next_index;
	++next_index;

	spirv_id id;
	id.id = result;
	return id;
}

static uint16_t write_string(uint32_t *operands, const char *string) {
	uint16_t length          = (uint16_t)strlen(string);
	uint16_t word_count      = (length + 1) / 4 + 1;
	operands[word_count - 1] = 0;
	memcpy(&operands[0], string, length + 1);
	return word_count;
}

static spirv_id write_op_ext_inst_import(instructions_buffer *instructions, const char *name) {
	spirv_id result = allocate_index();

	operands_buffer[0] = result.id;

	uint32_t name_length = write_string(&operands_buffer[1], name);

	write_instruction(instructions, 2 + name_length, SPIRV_OPCODE_EXT_INST_IMPORT, operands_buffer);

	return result;
}

static void write_op_memory_model(instructions_buffer *instructions, uint32_t addressing_model, uint32_t memory_model) {
	uint32_t args[2] = {addressing_model, memory_model};
	write_instruction(instructions, 3, SPIRV_OPCODE_MEMORY_MODEL, args);
}

static void write_op_entry_point(instructions_buffer *instructions, execution_model em, spirv_id entry_point, const char *name, spirv_id *interfaces,
                                 uint16_t interfaces_size) {
	operands_buffer[0] = (uint32_t)em;
	operands_buffer[1] = entry_point.id;

	uint32_t name_length = write_string(&operands_buffer[2], name);

	for (uint16_t i = 0; i < interfaces_size; ++i) {
		operands_buffer[2 + name_length + i] = interfaces[i].id;
	}

	write_instruction(instructions, 3 + name_length + interfaces_size, SPIRV_OPCODE_ENTRY_POINT, operands_buffer);
}

#define WORD_COUNT(operands) (1 + sizeof(operands) / 4)

static void write_op_execution_mode(instructions_buffer *instructions, spirv_id entry_point, execution_mode mode) {
	uint32_t operands[] = {entry_point.id, (uint32_t)mode};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_EXECUTION_MODE, operands);
}

static void write_op_execution_mode3(instructions_buffer *instructions, spirv_id entry_point, execution_mode mode, uint32_t param0, uint32_t param1,
                                     uint32_t param2) {
	uint32_t operands[] = {entry_point.id, (uint32_t)mode, param0, param1, param2};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_EXECUTION_MODE, operands);
}

static void write_capabilities(instructions_buffer *instructions, const capabilities *caps) {
	write_capability(instructions, CAPABILITY_SHADER);
}

static spirv_id write_type_void(instructions_buffer *instructions) {
	spirv_id void_type = allocate_index();
	write_instruction(instructions, 2, SPIRV_OPCODE_TYPE_VOID, &void_type.id);
	return void_type;
}

static spirv_id write_type_function(instructions_buffer *instructions, spirv_id return_type, spirv_id *parameter_types, uint16_t parameter_types_size) {
	spirv_id function_type = allocate_index();

	operands_buffer[0] = function_type.id;
	operands_buffer[1] = return_type.id;
	for (uint16_t i = 0; i < parameter_types_size; ++i) {
		operands_buffer[i + 2] = parameter_types[i].id;
	}
	write_instruction(instructions, 3 + parameter_types_size, SPIRV_OPCODE_TYPE_FUNCTION, operands_buffer);
	return function_type;
}

static spirv_id write_type_float(instructions_buffer *instructions, uint32_t width) {
	spirv_id float_type = allocate_index();

	uint32_t operands[] = {float_type.id, width};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_FLOAT, operands);
	return float_type;
}

static spirv_id write_type_vector_preallocated(instructions_buffer *instructions, spirv_id component_type, uint32_t component_count, spirv_id vector_type) {
	uint32_t operands[] = {vector_type.id, component_type.id, component_count};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_VECTOR, operands);
	return vector_type;
}

static spirv_id write_type_matrix(instructions_buffer *instructions, spirv_id column_type, uint32_t column_count) {
	spirv_id matrix_type = allocate_index();

	uint32_t operands[] = {matrix_type.id, column_type.id, column_count};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_MATRIX, operands);
	return matrix_type;
}

static spirv_id write_type_int(instructions_buffer *instructions, uint32_t width, bool signedness) {
	spirv_id int_type = allocate_index();

	uint32_t operands[] = {int_type.id, width, signedness ? 1 : 0};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_INT, operands);
	return int_type;
}

static spirv_id write_type_bool(instructions_buffer *instructions) {
	spirv_id bool_type = allocate_index();

	uint32_t operands[] = {bool_type.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_BOOL, operands);
	return bool_type;
}

static spirv_id write_type_image(instructions_buffer *instructions, spirv_id sampled_type, dim dimensionality, uint32_t depth, uint32_t arrayed, uint32_t ms,
                                 uint32_t sampled, image_format img_format) {
	spirv_id image_type = allocate_index();

	uint32_t operands[] = {image_type.id, sampled_type.id, (uint32_t)dimensionality, depth, arrayed, ms, sampled, (uint32_t)img_format};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_IMAGE, operands);
	return image_type;
}

static spirv_id write_type_sampler(instructions_buffer *instructions) {
	spirv_id sampler_type = allocate_index();

	uint32_t operands[] = {sampler_type.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_SAMPLER, operands);
	return sampler_type;
}

static spirv_id write_type_sampled_image(instructions_buffer *instructions, spirv_id image_type) {
	spirv_id sampled_image_type = allocate_index();

	uint32_t operands[] = {sampled_image_type.id, image_type.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_SAMPLED_IMAGE, operands);
	return sampled_image_type;
}

static spirv_id write_type_struct(instructions_buffer *instructions, spirv_id *types, uint16_t types_size) {
	spirv_id struct_type = allocate_index();

	operands_buffer[0] = struct_type.id;
	for (uint16_t i = 0; i < types_size; ++i) {
		operands_buffer[i + 1] = types[i].id;
	}
	write_instruction(instructions, 2 + types_size, SPIRV_OPCODE_TYPE_STRUCT, operands_buffer);
	return struct_type;
}

static spirv_id write_type_array(instructions_buffer *instructions, spirv_id element_type, spirv_id length) {
	spirv_id array_type = allocate_index();

	uint32_t operands[] = {array_type.id, element_type.id, length.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_ARRAY, operands);
	return array_type;
}

static spirv_id write_type_pointer(instructions_buffer *instructions, storage_class storage, spirv_id type) {
	spirv_id pointer_type = allocate_index();

	uint32_t operands[] = {pointer_type.id, (uint32_t)storage, type.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_POINTER, operands);
	return pointer_type;
}

static spirv_id write_type_pointer_preallocated(instructions_buffer *instructions, storage_class storage, spirv_id type, spirv_id pointer_type) {
	uint32_t operands[] = {pointer_type.id, (uint32_t)storage, type.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TYPE_POINTER, operands);
	return pointer_type;
}

static spirv_id void_type;
static spirv_id void_function_type;
static spirv_id spirv_float_type;
static spirv_id spirv_float2_type;
static spirv_id spirv_float3_type;
static spirv_id spirv_float4_type;
static spirv_id spirv_int_type;
static spirv_id spirv_int2_type;
static spirv_id spirv_int3_type;
static spirv_id spirv_int4_type;
static spirv_id spirv_uint_type;
static spirv_id spirv_uint2_type;
static spirv_id spirv_uint3_type;
static spirv_id spirv_uint4_type;
static spirv_id spirv_bool_type;
static spirv_id spirv_sampler_type;
static spirv_id spirv_sampler_pointer_type;
static spirv_id spirv_image_type;
static spirv_id spirv_image_pointer_type;
static spirv_id spirv_image2darray_type;
static spirv_id spirv_image2darray_pointer_type;
static spirv_id spirv_imagecube_type;
static spirv_id spirv_imagecube_pointer_type;
static spirv_id spirv_readwrite_image_type;
static spirv_id spirv_readwrite_image_pointer_type;
static spirv_id spirv_sampled_image_type;
static spirv_id spirv_sampled_image2darray_type;
static spirv_id spirv_sampled_imagecube_type;
static spirv_id spirv_float3x3_type;
static spirv_id spirv_float4x4_type;

static spirv_id glsl_import;

static spirv_id dispatch_thread_id_variable;
static spirv_id group_thread_id_variable;
static spirv_id group_id_variable;
static spirv_id work_group_size_variable;
static spirv_id vertex_id_variable;

typedef struct complex_type {
	type_id  type;
	uint16_t readwrite;
	uint16_t storage;
} complex_type;

static struct {
	complex_type key;
	spirv_id     value;
} *type_map = NULL;

static void add_to_type_map(type_id kong_type, spirv_id spirv_type, bool readwrite, storage_class storage) {
	kong_assert(kong_type != NO_TYPE);

	complex_type ct = {
	    .type      = kong_type,
	    .readwrite = readwrite,
	    .storage   = (uint16_t)storage,
	};
	hmput(type_map, ct, spirv_type);
}

static spirv_id convert_complex_type_to_spirv_id(complex_type ct) {
	spirv_id spirv_index = hmget(type_map, ct);
	if (spirv_index.id == 0) {
		spirv_index = allocate_index();
		add_to_type_map(ct.type, spirv_index, ct.readwrite, ct.storage);
	}
	return spirv_index;
}

static spirv_id convert_type_to_spirv_id(type_id type) {
	complex_type ct;
	ct.type      = type;
	ct.readwrite = false;
	ct.storage   = (uint16_t)STORAGE_CLASS_NONE;

	spirv_id spirv_index = hmget(type_map, ct);
	if (spirv_index.id == 0) {
		spirv_index = allocate_index();
		add_to_type_map(type, spirv_index, false, STORAGE_CLASS_NONE);
	}
	return spirv_index;
}

static spirv_id convert_pointer_type_to_spirv_id(type_id type, storage_class storage) {
	complex_type ct;
	ct.type      = type;
	ct.readwrite = false;
	ct.storage   = (uint16_t)storage;

	spirv_id spirv_index = hmget(type_map, ct);
	if (spirv_index.id == 0) {
		spirv_index = allocate_index();
		add_to_type_map(type, spirv_index, false, storage);
	}
	return spirv_index;
}

static void write_base_types(instructions_buffer *buffer) {
	void_type          = write_type_void(buffer);
	void_function_type = write_type_function(buffer, void_type, NULL, 0);

	spirv_float_type = write_type_float(buffer, 32);
	add_to_type_map(float_id, spirv_float_type, false, STORAGE_CLASS_NONE);

	spirv_float2_type = convert_type_to_spirv_id(float2_id);
	write_type_vector_preallocated(buffer, spirv_float_type, 2, spirv_float2_type);
	add_to_type_map(float2_id, spirv_float2_type, false, STORAGE_CLASS_NONE);

	spirv_float3_type = convert_type_to_spirv_id(float3_id);
	write_type_vector_preallocated(buffer, spirv_float_type, 3, spirv_float3_type);
	add_to_type_map(float3_id, spirv_float3_type, false, STORAGE_CLASS_NONE);

	spirv_float4_type = convert_type_to_spirv_id(float4_id);
	write_type_vector_preallocated(buffer, spirv_float_type, 4, spirv_float4_type);
	add_to_type_map(float4_id, spirv_float4_type, false, STORAGE_CLASS_NONE);

	spirv_int_type = write_type_int(buffer, 32, true);
	add_to_type_map(int_id, spirv_int_type, false, STORAGE_CLASS_NONE);

	spirv_int2_type = convert_type_to_spirv_id(int2_id);
	write_type_vector_preallocated(buffer, spirv_int_type, 2, spirv_int2_type);
	add_to_type_map(int2_id, spirv_int2_type, false, STORAGE_CLASS_NONE);

	spirv_int3_type = convert_type_to_spirv_id(int3_id);
	write_type_vector_preallocated(buffer, spirv_int_type, 3, spirv_int3_type);
	add_to_type_map(int3_id, spirv_int3_type, false, STORAGE_CLASS_NONE);

	spirv_int4_type = convert_type_to_spirv_id(int4_id);
	write_type_vector_preallocated(buffer, spirv_int_type, 4, spirv_int4_type);
	add_to_type_map(int4_id, spirv_int4_type, false, STORAGE_CLASS_NONE);

	spirv_uint_type = write_type_int(buffer, 32, false);
	add_to_type_map(uint_id, spirv_uint_type, false, STORAGE_CLASS_NONE);

	spirv_uint2_type = convert_type_to_spirv_id(uint2_id);
	write_type_vector_preallocated(buffer, spirv_uint_type, 2, spirv_uint2_type);
	add_to_type_map(uint2_id, spirv_uint2_type, false, STORAGE_CLASS_NONE);

	spirv_uint3_type = convert_type_to_spirv_id(uint3_id);
	write_type_vector_preallocated(buffer, spirv_uint_type, 3, spirv_uint3_type);
	add_to_type_map(uint3_id, spirv_uint3_type, false, STORAGE_CLASS_NONE);

	spirv_uint4_type = convert_type_to_spirv_id(uint4_id);
	write_type_vector_preallocated(buffer, spirv_uint_type, 4, spirv_uint4_type);
	add_to_type_map(uint4_id, spirv_uint4_type, false, STORAGE_CLASS_NONE);

	spirv_bool_type = write_type_bool(buffer);
	add_to_type_map(bool_id, spirv_bool_type, false, STORAGE_CLASS_NONE);

	spirv_sampler_type                 = write_type_sampler(buffer);
	spirv_sampler_pointer_type         = allocate_index();
	spirv_image_type                   = write_type_image(buffer, spirv_float_type, DIM_2D, 0, 0, 0, 1, IMAGE_FORMAT_UNKNOWN);
	spirv_image_pointer_type           = allocate_index();
	spirv_image2darray_type            = write_type_image(buffer, spirv_float_type, DIM_2D, 0, 1, 0, 1, IMAGE_FORMAT_UNKNOWN);
	spirv_image2darray_pointer_type    = allocate_index();
	spirv_imagecube_type               = write_type_image(buffer, spirv_float_type, DIM_CUBE, 0, 0, 0, 1, IMAGE_FORMAT_UNKNOWN);
	spirv_imagecube_pointer_type       = allocate_index();
	spirv_readwrite_image_type         = write_type_image(buffer, spirv_float_type, DIM_2D, 0, 0, 0, 2, IMAGE_FORMAT_UNKNOWN);
	spirv_readwrite_image_pointer_type = allocate_index();
	spirv_sampled_image_type           = write_type_sampled_image(buffer, spirv_image_type);
	spirv_sampled_image2darray_type    = write_type_sampled_image(buffer, spirv_image2darray_type);
	spirv_sampled_imagecube_type       = write_type_sampled_image(buffer, spirv_imagecube_type);

	add_to_type_map(float2x2_id, write_type_matrix(buffer, spirv_float2_type, 2), false, STORAGE_CLASS_NONE);
	add_to_type_map(float2x3_id, write_type_matrix(buffer, spirv_float3_type, 2), false, STORAGE_CLASS_NONE);
	add_to_type_map(float3x2_id, write_type_matrix(buffer, spirv_float2_type, 3), false, STORAGE_CLASS_NONE);
	spirv_float3x3_type = write_type_matrix(buffer, spirv_float3_type, 3);
	add_to_type_map(float3x3_id, spirv_float3x3_type, false, STORAGE_CLASS_NONE);
	add_to_type_map(float2x4_id, write_type_matrix(buffer, spirv_float4_type, 2), false, STORAGE_CLASS_NONE);
	add_to_type_map(float4x2_id, write_type_matrix(buffer, spirv_float2_type, 4), false, STORAGE_CLASS_NONE);
	add_to_type_map(float3x4_id, write_type_matrix(buffer, spirv_float4_type, 3), false, STORAGE_CLASS_NONE);
	add_to_type_map(float4x3_id, write_type_matrix(buffer, spirv_float3_type, 4), false, STORAGE_CLASS_NONE);
	spirv_float4x4_type = write_type_matrix(buffer, spirv_float4_type, 4);
	add_to_type_map(float4x4_id, spirv_float4x4_type, false, STORAGE_CLASS_NONE);
}

static spirv_id get_int_constant(int value);

static void write_types(instructions_buffer *buffer, function *main) {
	type_id types[256];
	size_t  types_size = 0;
	find_referenced_types(main, types, &types_size);

	for (size_t i = 0; i < types_size; ++i) {
		type *t = get_type(types[i]);

		if (t->built_in) {
			if (t->array_size > 0) {
				spirv_id array_type = write_type_array(buffer, convert_type_to_spirv_id(t->base), get_int_constant(t->array_size));
				add_to_type_map(types[i], array_type, false, STORAGE_CLASS_NONE);
			}
		}
		else {
			spirv_id member_types[256];
			uint16_t member_types_size = 0;

			for (size_t j = 0; j < t->members.size; ++j) {
				member_types[member_types_size] = convert_type_to_spirv_id(t->members.m[j].type.type);
				member_types_size += 1;
				kong_assert(member_types_size < 256);
			}

			spirv_id struct_type = write_type_struct(buffer, member_types, member_types_size);
			add_to_type_map(types[i], struct_type, false, STORAGE_CLASS_NONE);
		}
	}

	static_array_init(written_pointer_relations);

	size_t size = hmlenu(type_map);
	for (size_t i = 0; i < size; ++i) {
		complex_type type = type_map[i].key;
		if (type.storage != STORAGE_CLASS_NONE) {
			complex_type non_pointer_type = type;
			non_pointer_type.storage      = STORAGE_CLASS_NONE;
			spirv_id non_pointer_type_id  = convert_complex_type_to_spirv_id(non_pointer_type);

			bool found = false;

			for (size_t relation_index = 0; relation_index < written_pointer_relations.size; ++relation_index) {
				pointer_relation *previous_relation = &written_pointer_relations.values[relation_index];

				if (previous_relation->pointer_type_id.id == type_map[i].value.id) {
					kong_assert(previous_relation->non_pointer_type_id.id == non_pointer_type_id.id);
					found = true;
					break;
				}
			}

			if (!found) {
				pointer_relation relation = {
				    .non_pointer_type_id = non_pointer_type_id,
				    .pointer_type_id     = type_map[i].value,
				};
				static_array_push(written_pointer_relations, relation);

				write_type_pointer_preallocated(buffer, type.storage, non_pointer_type_id, type_map[i].value);
			}
		}
	}
}

static spirv_id write_constant(instructions_buffer *instructions, spirv_id type, spirv_id value_id, uint32_t value) {
	uint32_t operands[] = {type.id, value_id.id, value};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_CONSTANT, operands);
	return value_id;
}

static spirv_id write_constant_int(instructions_buffer *instructions, spirv_id value_id, int32_t value) {
	uint32_t uint32_value = *(uint32_t *)&value;
	return write_constant(instructions, spirv_int_type, value_id, uint32_value);
}

static spirv_id write_constant_uint(instructions_buffer *instructions, spirv_id value_id, uint32_t value) {
	return write_constant(instructions, spirv_uint_type, value_id, value);
}

static spirv_id write_constant_float(instructions_buffer *instructions, spirv_id value_id, float value) {
	uint32_t uint32_value = *(uint32_t *)&value;
	return write_constant(instructions, spirv_float_type, value_id, uint32_value);
}

static spirv_id write_constant_bool(instructions_buffer *instructions, spirv_id value_id, bool value) {
	uint32_t uint32_value = *(uint32_t *)&value;
	return write_constant(instructions, spirv_bool_type, value_id, uint32_value);
}

static void write_constant_composite_preallocated3(instructions_buffer *instructions, spirv_id result_type, spirv_id result, spirv_id consituent0,
                                                   spirv_id consituent1, spirv_id consituent2) {
	uint32_t operands[] = {result_type.id, result.id, consituent0.id, consituent1.id, consituent2.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_CONSTANT_COMPOSITE, operands);
}

static void write_vertex_output_decorations(instructions_buffer *instructions, spirv_id output_struct, spirv_id *outputs, uint32_t outputs_size) {
	{
		uint32_t operands[] = {output_struct.id, 0, (uint32_t)DECORATION_BUILTIN, (uint32_t)BUILTIN_POSITION};
		write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_MEMBER_DECORATE, operands);
	}

	{
		uint32_t operands[] = {output_struct.id, (uint32_t)DECORATION_BLOCK};
		write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DECORATE, operands);
	}

	for (uint32_t i = 1; i < outputs_size; ++i) {
		uint32_t operands[] = {outputs[i].id, (uint32_t)DECORATION_LOCATION, i - 1};
		write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DECORATE, operands);
	}
}

static void write_vertex_input_decorations(instructions_buffer *instructions, spirv_id *inputs, uint32_t inputs_size) {
	for (uint32_t i = 0; i < inputs_size; ++i) {
		uint32_t operands[] = {inputs[i].id, (uint32_t)DECORATION_LOCATION, i};
		write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DECORATE, operands);
	}
}

static void write_fragment_input_decorations(instructions_buffer *instructions, spirv_id *inputs, uint32_t inputs_size) {
	for (uint32_t i = 0; i < inputs_size; ++i) {
		uint32_t operands[] = {inputs[i].id, (uint32_t)DECORATION_LOCATION, i};
		write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DECORATE, operands);
	}
}

static void write_fragment_output_decorations(instructions_buffer *instructions, spirv_id *outputs, uint32_t outputs_size) {
	for (uint32_t output_index = 0; output_index < outputs_size; ++output_index) {
		uint32_t operands[] = {outputs[output_index].id, (uint32_t)DECORATION_LOCATION, output_index};
		write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DECORATE, operands);
	}
}

static void write_op_decorate(instructions_buffer *instructions, spirv_id target, decoration decor) {
	uint32_t operands[] = {target.id, (uint32_t)decor};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DECORATE, operands);
}

static void write_op_decorate_value(instructions_buffer *instructions, spirv_id target, decoration decor, uint32_t value) {
	uint32_t operands[] = {target.id, (uint32_t)decor, value};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DECORATE, operands);
}

static void write_op_member_decorate(instructions_buffer *instructions, spirv_id structure_type, uint32_t member, decoration decor) {
	uint32_t operands[] = {structure_type.id, member, (uint32_t)decor};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_MEMBER_DECORATE, operands);
}

static void write_op_member_decorate_value(instructions_buffer *instructions, spirv_id structure_type, uint32_t member, decoration decor, uint32_t value) {
	uint32_t operands[] = {structure_type.id, member, (uint32_t)decor, value};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_MEMBER_DECORATE, operands);
}

static spirv_id write_op_function_preallocated(instructions_buffer *instructions, spirv_id result_type, function_control control, spirv_id function_type,
                                               spirv_id result) {
	uint32_t operands[] = {result_type.id, result.id, (uint32_t)control, function_type.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_FUNCTION, operands);
	return result;
}

static spirv_id write_op_function_call(instructions_buffer *instructions, spirv_id return_type, spirv_id fun_id, spirv_id *arguments, uint16_t arguments_size) {
	spirv_id result = allocate_index();

	operands_buffer[0] = return_type.id;
	operands_buffer[1] = result.id;
	operands_buffer[2] = fun_id.id;
	for (uint16_t i = 0; i < arguments_size; ++i) {
		operands_buffer[3 + i] = arguments[i].id;
	}

	write_instruction(instructions, 4 + arguments_size, SPIRV_OPCODE_FUNCTION_CALL, operands_buffer);
	return result;
}

static spirv_id write_op_label(instructions_buffer *instructions) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_LABEL, operands);
	return result;
}

static void write_op_label_preallocated(instructions_buffer *instructions, spirv_id result) {
	uint32_t operands[] = {result.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_LABEL, operands);
}

static void write_op_branch(instructions_buffer *instructions, spirv_id target) {
	uint32_t operands[] = {target.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_BRANCH, operands);
}

static void write_op_loop_merge(instructions_buffer *instructions, spirv_id merge_block, spirv_id continue_target, loop_control control) {
	uint32_t operands[] = {merge_block.id, continue_target.id, (uint32_t)control};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_LOOP_MERGE, operands);
}

static void write_op_return(instructions_buffer *instructions) {
	write_simple_instruction(instructions, SPIRV_OPCODE_RETURN);
}

static void write_op_return_value(instructions_buffer *instructions, spirv_id value) {
	uint32_t operands[] = {value.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_RETURN_VALUE, operands);
}

static void write_op_discard(instructions_buffer *instructions) {
	write_simple_instruction(instructions, SPIRV_OPCODE_KILL);
}

static void write_op_function_end(instructions_buffer *instructions) {
	write_simple_instruction(instructions, SPIRV_OPCODE_FUNCTION_END);
}

static spirv_id get_int_constant(int value) {
	int_constant_container *container = (int_constant_container *)hash_map_get(int_constants, value);

	if (container == NULL) {
		container = (int_constant_container *)malloc(sizeof(int_constant_container));
		kong_assert(container != NULL);
		container->container.key = value;
		container->value         = allocate_index();
		hash_map_add(int_constants, (struct container *)container);
	}

	return container->value;
}

static spirv_id get_uint_constant(uint32_t value) {
	spirv_id index = hmget(uint_constants, value);
	if (index.id == 0) {
		index = allocate_index();
		hmput(uint_constants, value, index);
	}
	return index;
}

static spirv_id get_float_constant(float value) {
	spirv_id index = hmget(float_constants, value);
	if (index.id == 0) {
		index = allocate_index();
		hmput(float_constants, value, index);
	}
	return index;
}

static spirv_id get_bool_constant(bool value) {
	spirv_id index = hmget(bool_constants, value);
	if (index.id == 0) {
		index = allocate_index();
		hmput(bool_constants, value, index);
	}
	return index;
}

static spirv_id write_op_access_chain(instructions_buffer *instructions, spirv_id result_type, spirv_id base, spirv_id *indices, uint16_t indices_size) {
	spirv_id pointer = allocate_index();

	operands_buffer[0] = result_type.id;
	operands_buffer[1] = pointer.id;
	operands_buffer[2] = base.id;
	for (uint16_t i = 0; i < indices_size; ++i) {
		operands_buffer[i + 3] = indices[i].id;
	}

	write_instruction(instructions, 4 + indices_size, SPIRV_OPCODE_ACCESS_CHAIN, operands_buffer);
	return pointer;
}

static spirv_id write_op_load(instructions_buffer *instructions, spirv_id result_type, spirv_id pointer) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, pointer.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_LOAD, operands);
	return result;
}

static void write_op_store(instructions_buffer *instructions, spirv_id pointer, spirv_id object) {
	uint32_t operands[] = {pointer.id, object.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_STORE, operands);
}

static spirv_id write_op_composite_construct(instructions_buffer *instructions, spirv_id type, spirv_id *constituents, uint16_t constituents_size) {
	spirv_id result = allocate_index();

	operands_buffer[0] = type.id;
	operands_buffer[1] = result.id;
	for (uint16_t i = 0; i < constituents_size; ++i) {
		operands_buffer[i + 2] = constituents[i].id;
	}
	write_instruction(instructions, 3 + constituents_size, SPIRV_OPCODE_COMPOSITE_CONSTRUCT, operands_buffer);
	return result;
}

static spirv_id write_op_composite_extract(instructions_buffer *instructions, spirv_id type, spirv_id composite, uint32_t *indices, uint16_t indices_size) {
	spirv_id result = allocate_index();

	operands_buffer[0] = type.id;
	operands_buffer[1] = result.id;
	operands_buffer[2] = composite.id;
	for (uint16_t i = 0; i < indices_size; ++i) {
		operands_buffer[i + 3] = indices[i];
	}
	write_instruction(instructions, 4 + indices_size, SPIRV_OPCODE_COMPOSITE_EXTRACT, operands_buffer);
	return result;
}

static spirv_id write_op_f_ord_less_than(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_ORD_LESS_THAN, operands);

	return result;
}

static spirv_id write_op_u_less_than(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_U_LESS_THAN, operands);

	return result;
}

static spirv_id write_op_s_less_than(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_S_LESS_THAN, operands);

	return result;
}

static spirv_id write_op_f_ord_less_than_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_ORD_LESS_THAN_EQUAL, operands);

	return result;
}

static spirv_id write_op_u_less_than_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_U_LESS_THAN_EQUAL, operands);

	return result;
}

static spirv_id write_op_s_less_than_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_S_LESS_THAN_EQUAL, operands);

	return result;
}

static spirv_id write_op_f_ord_greater_than(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_ORD_GREATER_THAN, operands);

	return result;
}

static spirv_id write_op_u_greater_than(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_U_GREATER_THAN, operands);

	return result;
}

static spirv_id write_op_s_greater_than(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_S_GREATER_THAN, operands);

	return result;
}

static spirv_id write_op_f_ord_greater_than_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_ORD_GREATER_THAN_EQUAL, operands);

	return result;
}

static spirv_id write_op_u_greater_than_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_U_GREATER_THAN_EQUAL, operands);

	return result;
}

static spirv_id write_op_s_greater_than_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_S_GREATER_THAN_EQUAL, operands);

	return result;
}

static spirv_id write_op_i_add(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_I_ADD, operands);

	return result;
}

static spirv_id write_op_f_add(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_ADD, operands);

	return result;
}

static spirv_id write_op_f_sub(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_SUB, operands);

	return result;
}

static spirv_id write_op_i_sub(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_I_SUB, operands);

	return result;
}

static spirv_id write_op_f_mul(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_MUL, operands);

	return result;
}

static spirv_id write_op_i_mul(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_I_MUL, operands);

	return result;
}

static spirv_id write_op_f_div(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_DIV, operands);

	return result;
}

static spirv_id write_op_f_mod(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_MOD, operands);

	return result;
}

static spirv_id write_op_u_div(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_U_DIV, operands);

	return result;
}

static spirv_id write_op_s_div(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_S_DIV, operands);

	return result;
}

static bool is_integer_type(type_id t) {
	return is_vector_or_scalar(t) && (vector_base_type(t) == int_id || vector_base_type(t) == uint_id);
}

static spirv_id write_op_mul(instructions_buffer *instructions, type_id t, spirv_id operand1, spirv_id operand2) {
	if (is_integer_type(t)) {
		return write_op_i_mul(instructions, convert_type_to_spirv_id(t), operand1, operand2);
	}
	return write_op_f_mul(instructions, convert_type_to_spirv_id(t), operand1, operand2);
}

static spirv_id write_op_div(instructions_buffer *instructions, type_id t, spirv_id operand1, spirv_id operand2) {
	if (is_integer_type(t) && vector_base_type(t) == uint_id) {
		return write_op_u_div(instructions, convert_type_to_spirv_id(t), operand1, operand2);
	}
	if (is_integer_type(t)) {
		return write_op_s_div(instructions, convert_type_to_spirv_id(t), operand1, operand2);
	}
	return write_op_f_div(instructions, convert_type_to_spirv_id(t), operand1, operand2);
}

static spirv_id write_op_u_mod(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_U_MOD, operands);

	return result;
}

static spirv_id write_op_s_mod(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_S_MOD, operands);

	return result;
}

static spirv_id write_op_matrix_times_vector(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_MATRIX_TIMES_VECTOR, operands);

	return result;
}

static spirv_id write_op_vector_times_matrix(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_VECTOR_TIMES_MATRIX, operands);

	return result;
}

static spirv_id write_op_matrix_times_matrix(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_MATRIX_TIMES_MATRIX, operands);

	return result;
}

static spirv_id write_op_convert_s_to_f(instructions_buffer *instructions, spirv_id result_type, spirv_id signed_value) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, signed_value.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_CONVERT_S_TO_F, operands);

	return result;
}

static spirv_id write_op_convert_u_to_f(instructions_buffer *instructions, spirv_id result_type, spirv_id unsigned_value) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, unsigned_value.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_CONVERT_U_TO_F, operands);

	return result;
}

static spirv_id write_op_convert_f_to_s(instructions_buffer *instructions, spirv_id result_type, spirv_id float_value) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, float_value.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_CONVERT_F_TO_S, operands);

	return result;
}

static spirv_id write_op_convert_f_to_u(instructions_buffer *instructions, spirv_id result_type, spirv_id float_value) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, float_value.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_CONVERT_F_TO_U, operands);

	return result;
}

static spirv_id write_op_bitcast(instructions_buffer *instructions, spirv_id result_type, spirv_id operand) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, operand.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_BITCAST, operands);

	return result;
}

static void write_op_selection_merge(instructions_buffer *instructions, spirv_id merge_block, selection_control control) {
	uint32_t operands[] = {merge_block.id, (uint32_t)control};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_SELECTION_MERGE, operands);
}

static void write_op_branch_conditional(instructions_buffer *instructions, spirv_id condition, spirv_id pass, spirv_id fail) {
	uint32_t operands[] = {condition.id, pass.id, fail.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_BRANCH_CONDITIONAL, operands);
}

static spirv_id write_op_sampled_image(instructions_buffer *instructions, spirv_id result_type, spirv_id image, spirv_id sampler) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, image.id, sampler.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_SAMPLED_IMAGE, operands);

	return result;
}

static spirv_id write_op_image_sample_implicit_lod(instructions_buffer *instructions, spirv_id result_type, spirv_id sampled_image, spirv_id coordinate) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, sampled_image.id, coordinate.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_IMAGE_SAMPLE_IMPLICIT_LOD, operands);

	return result;
}

static spirv_id write_op_image_sample_explicit_lod(instructions_buffer *instructions, spirv_id result_type, spirv_id sampled_image, spirv_id coordinate,
                                                   spirv_id lod) {
	spirv_id result = allocate_index();

	int      lod_operands = 0x2;
	uint32_t operands[]   = {result_type.id, result.id, sampled_image.id, coordinate.id, lod_operands, lod.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_IMAGE_SAMPLE_EXPLICIT_LOD, operands);

	return result;
}

////

static spirv_id write_op_image_fetch(instructions_buffer *instructions, spirv_id result_type, spirv_id sampled_image, spirv_id coordinate) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, sampled_image.id, coordinate.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_IMAGE_FETCH, operands);

	return result;
}

static spirv_id write_op_transpose(instructions_buffer *instructions, spirv_id type, spirv_id operand) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_TRANSPOSE, operands);
	return result;
}

////

static spirv_id write_op_ext_inst(instructions_buffer *instructions, spirv_id result_type, spirv_id set, uint32_t instruction, spirv_id operand) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, set.id, instruction, operand.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_EXT_INST, operands);

	return result;
}

static spirv_id write_op_ext_inst2(instructions_buffer *instructions, spirv_id result_type, spirv_id set, uint32_t instruction, spirv_id operand1,
                                   spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, set.id, instruction, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_EXT_INST, operands);

	return result;
}

static spirv_id write_op_ext_inst3(instructions_buffer *instructions, spirv_id result_type, spirv_id set, uint32_t instruction, spirv_id operand1,
                                   spirv_id operand2, spirv_id operand3) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, set.id, instruction, operand1.id, operand2.id, operand3.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_EXT_INST, operands);

	return result;
}

static spirv_id write_op_image_read(instructions_buffer *instructions, spirv_id result_type, spirv_id image, spirv_id coordinate) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, image.id, coordinate.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_IMAGE_READ, operands);

	return result;
}

static void write_op_image_write(instructions_buffer *instructions, spirv_id image, spirv_id coordinate, spirv_id texel) {
	uint32_t operands[] = {image.id, coordinate.id, texel.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_IMAGE_WRITE, operands);
}

static spirv_id write_op_variable(instructions_buffer *instructions, spirv_id result_type, storage_class storage) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {result_type.id, result.id, (uint32_t)storage};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_VARIABLE, operands);
	return result;
}

static void write_op_variable_preallocated(instructions_buffer *instructions, spirv_id result_type, spirv_id result, storage_class storage) {
	uint32_t operands[] = {result_type.id, result.id, (uint32_t)storage};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_VARIABLE, operands);
}

static spirv_id write_op_f_ord_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_ORD_EQUAL, operands);

	return result;
}

static spirv_id write_op_i_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_I_EQUAL, operands);

	return result;
}

static spirv_id write_op_f_ord_not_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_ORD_NOT_EQUAL, operands);

	return result;
}

static spirv_id write_op_i_not_equal(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_I_NOT_EQUAL, operands);

	return result;
}

static spirv_id write_op_f_negate(instructions_buffer *instructions, spirv_id type, spirv_id operand) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_F_NEGATE, operands);
	return result;
}

static spirv_id write_op_s_negate(instructions_buffer *instructions, spirv_id type, spirv_id operand) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_S_NEGATE, operands);
	return result;
}

static spirv_id write_op_logical_and(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};

	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_LOGICAL_AND, operands);

	return result;
}

static spirv_id write_op_logical_or(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_LOGICAL_OR, operands);
	return result;
}

static spirv_id write_op_bitwise_xor(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_BITWISE_XOR, operands);
	return result;
}

static spirv_id write_op_bitwise_and(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_BITWISE_AND, operands);
	return result;
}

static spirv_id write_op_bitwise_or(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_BITWISE_OR, operands);
	return result;
}

static spirv_id write_op_left_shift(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_SHIFT_LEFT_LOGICAL, operands);
	return result;
}

static spirv_id write_op_right_shift(instructions_buffer *instructions, type_id t, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	// Signed values keep their sign like in HLSL and Metal
	spirv_opcode opcode     = vector_base_type(t) == int_id ? SPIRV_OPCODE_SHIFT_RIGHT_ARITHMETIC : SPIRV_OPCODE_SHIFT_RIGHT_LOGICAL;
	uint32_t     operands[] = {convert_type_to_spirv_id(t).id, result.id, operand1.id, operand2.id};
	write_instruction(instructions, WORD_COUNT(operands), opcode, operands);
	return result;
}

static spirv_id write_op_not(instructions_buffer *instructions, spirv_id type, spirv_id operand) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_LOGICAL_NOT, operands);
	return result;
}

static spirv_id write_op_dot(instructions_buffer *instructions, spirv_id type, spirv_id operand1, spirv_id operand2) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand1.id, operand2.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DOT, operands);
	return result;
}

static spirv_id write_op_dpdx(instructions_buffer *instructions, spirv_id type, spirv_id operand) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DPDX, operands);
	return result;
}

static spirv_id write_op_dpdy(instructions_buffer *instructions, spirv_id type, spirv_id operand) {
	spirv_id result = allocate_index();

	uint32_t operands[] = {type.id, result.id, operand.id};
	write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_DPDY, operands);
	return result;
}

static struct {
	uint64_t key;
	spirv_id value;
} *index_map = NULL;

static spirv_id convert_kong_index_to_spirv_id(uint64_t index) {
	spirv_id id = hmget(index_map, index);
	if (id.id == 0) {
		id = allocate_index();
		hmput(index_map, index, id);
	}
	return id;
}

static bool is_global_const(uint64_t index) {
	for (global_id i = 0; get_global(i) != NULL; ++i) {
		global *g = get_global(i);
		if (g->var_index == index && g->type == float_id) {
			return true;
		}
	}
	return false;
}

static spirv_id get_var(instructions_buffer *instructions, variable param) {
	spirv_id id = convert_kong_index_to_spirv_id(param.index);
	if (param.kind != VARIABLE_INTERNAL && !is_global_const(param.index)) {
		id = write_op_load(instructions, convert_type_to_spirv_id(param.type.type), id);
	}
	return id;
}

static void write_function(instructions_buffer *instructions, function *f, spirv_id result_type, spirv_id fun_type, spirv_id fun_id, shader_stage stage,
                           bool main, type_id output) {
	write_op_function_preallocated(instructions, result_type, FUNCTION_CONTROL_NONE, fun_type, fun_id);

	spirv_id parameter_value_ids[256] = {0};
	if (!main) {
		for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
			spirv_id param_type                  = convert_type_to_spirv_id(f->parameter_types[parameter_index].type);
			parameter_value_ids[parameter_index] = allocate_index();
			uint32_t operands[]                  = {param_type.id, parameter_value_ids[parameter_index].id};
			write_instruction(instructions, WORD_COUNT(operands), SPIRV_OPCODE_FUNCTION_PARAMETER, operands);
		}
	}

	write_op_label(instructions);

	debug_context context = {0};
	check(f->block != NULL, context, "Function block missing");

	uint8_t *data = f->code.o;
	size_t   size = f->code.size;

	uint64_t parameter_ids[256]   = {0};
	type_id  parameter_types[256] = {0};
	for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
		for (size_t i = 0; i < f->block->block.vars.size; ++i) {
			if (f->parameter_names[parameter_index] == f->block->block.vars.v[i].name) {
				parameter_ids[parameter_index]   = f->block->block.vars.v[i].variable_id;
				parameter_types[parameter_index] = f->block->block.vars.v[i].type.type;
				break;
			}
		}
	}

	for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
		check(parameter_ids[parameter_index] != 0, context, "Parameter not found");
	}

	// create variable for the input parameter
	spirv_id spirv_parameter_ids[256] = {0};
	uint32_t spirv_parameter_ids_size = 0;
	if (main) {
		if (stage == SHADER_STAGE_FRAGMENT) {
			spirv_parameter_ids[0] = convert_kong_index_to_spirv_id(parameter_ids[0]);
			write_op_variable_preallocated(instructions, convert_pointer_type_to_spirv_id(parameter_types[0], STORAGE_CLASS_FUNCTION), spirv_parameter_ids[0],
			                               STORAGE_CLASS_FUNCTION);
			spirv_parameter_ids_size++;
		}
		else if (stage == SHADER_STAGE_VERTEX) {
			for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
				spirv_parameter_ids[spirv_parameter_ids_size] = convert_kong_index_to_spirv_id(parameter_ids[parameter_index]);
				write_op_variable_preallocated(instructions, convert_pointer_type_to_spirv_id(parameter_types[parameter_index], STORAGE_CLASS_FUNCTION),
				                               spirv_parameter_ids[spirv_parameter_ids_size], STORAGE_CLASS_FUNCTION);
				spirv_parameter_ids_size++;
			}
		}
	}
	else {
		for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
			spirv_parameter_ids[spirv_parameter_ids_size] = convert_kong_index_to_spirv_id(parameter_ids[parameter_index]);
			write_op_variable_preallocated(instructions, convert_pointer_type_to_spirv_id(parameter_types[parameter_index], STORAGE_CLASS_FUNCTION),
			                               spirv_parameter_ids[spirv_parameter_ids_size], STORAGE_CLASS_FUNCTION);
			spirv_parameter_ids_size++;
		}
	}

	// all vars have to go first
	size_t index = 0;
	while (index < size) {
		opcode *o = (opcode *)&data[index];
		switch (o->type) {
		case OPCODE_VAR: {
			spirv_id result =
			    write_op_variable(instructions, convert_pointer_type_to_spirv_id(o->op_var.var.type.type, STORAGE_CLASS_FUNCTION), STORAGE_CLASS_FUNCTION);
			hmput(index_map, o->op_var.var.index, result);
			break;
		}
		default:
			break;
		}

		index += o->size;
	}

	// transfer input values into the input variable
	if (main) {
		if (stage == SHADER_STAGE_FRAGMENT) {
			for (size_t i = 0; i < input_vars_count; ++i) {
				spirv_id index   = get_int_constant((int)(i + 1)); // jump over the pos member
				spirv_id loaded  = write_op_load(instructions, convert_type_to_spirv_id(input_types[i]), input_vars[i]);
				spirv_id pointer = write_op_access_chain(instructions, convert_pointer_type_to_spirv_id(input_types[i], STORAGE_CLASS_FUNCTION),
				                                         spirv_parameter_ids[0], &index, 1);
				write_op_store(instructions, pointer, loaded);
			}
		}
		else if (stage == SHADER_STAGE_VERTEX) {
			for (size_t i = 0; i < input_vars_count; ++i) {
				////
				// spirv_id index   = get_int_constant((int)vertex_parameter_member_indices[i]);
				spirv_id index = get_int_constant((int)i);
				////
				spirv_id loaded  = write_op_load(instructions, convert_type_to_spirv_id(input_types[i]), input_vars[i]);
				spirv_id pointer = write_op_access_chain(instructions, convert_pointer_type_to_spirv_id(input_types[i], STORAGE_CLASS_FUNCTION),
				                                         spirv_parameter_ids[vertex_parameter_indices[i]], &index, 1);
				write_op_store(instructions, pointer, loaded);
			}
		}
	}
	else {
		for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
			write_op_store(instructions, spirv_parameter_ids[parameter_index], parameter_value_ids[parameter_index]);
		}
	}

	bool     ends_with_return         = false;
	uint64_t next_block_branch_id[16] = {0};
	uint64_t next_block_label_id[16]  = {0};
	uint8_t  nested_if_count          = 0;

	index = 0;
	while (index < size) {
		ends_with_return = false;
		opcode *o        = (opcode *)&data[index];
		switch (o->type) {
		case OPCODE_VAR: {
			break;
		}
		case OPCODE_LOAD_ACCESS_LIST: {
			uint16_t indices_size = o->op_load_access_list.access_list_size;

			if (get_type(o->op_load_access_list.from.type.type)->tex_kind == TEXTURE_KIND_2D) {
				kong_assert(indices_size == 1);
				kong_assert(o->op_load_access_list.access_list[0].kind == ACCESS_ELEMENT);

				////
				// spirv_id image = write_op_load(instructions, spirv_readwrite_image_type, convert_kong_index_to_spirv_id(o->op_load_access_list.from.index));
				spirv_id image = write_op_load(instructions, spirv_image_type, convert_kong_index_to_spirv_id(o->op_load_access_list.from.index));
				////

				variable coordinate_var = o->op_load_access_list.access_list[0].access_element.index;
				spirv_id coordinate     = get_var(instructions, coordinate_var);

				////
				// spirv_id value = write_op_image_read(instructions, spirv_float4_type, image, coordinate);
				spirv_id value = write_op_image_fetch(instructions, spirv_float4_type, image, coordinate);
				////

				hmput(index_map, o->op_load_access_list.to.index, value);
			}
			else if (o->op_load_access_list.from.kind == VARIABLE_INTERNAL) {
				uint32_t indices[256];

				type *s = get_type(o->op_load_access_list.from.type.type);

				for (uint16_t i = 0; i < indices_size; ++i) {
					switch (o->op_load_access_list.access_list[i].kind) {
					case ACCESS_ELEMENT:
						kong_assert(false);
						break;
					case ACCESS_MEMBER: {
						uint32_t member_index = 0;
						bool     found        = false;

						for (; member_index < s->members.size; ++member_index) {
							if (s->members.m[member_index].name == o->op_load_access_list.access_list[i].access_member.name) {
								found = true;
								break;
							}
						}

						kong_assert(found);

						indices[i] = member_index;

						break;
					}
					case ACCESS_SWIZZLE: {
						kong_assert(o->op_load_access_list.access_list[i].access_swizzle.swizzle.size == 1);

						indices[i] = o->op_load_access_list.access_list[i].access_swizzle.swizzle.indices[0];

						break;
					}
					}

					s = get_type(o->op_load_access_list.access_list[i].type);
				}

				spirv_id value = write_op_composite_extract(instructions, convert_type_to_spirv_id(o->op_load_access_list.to.type.type),
				                                            convert_kong_index_to_spirv_id(o->op_load_access_list.from.index), indices, indices_size);

				hmput(index_map, o->op_load_access_list.to.index, value);
			}
			else {
				spirv_id    indices[256];
				int         plain_indices[256];
				access_kind access_kinds[256];

				type *s = get_type(o->op_load_access_list.from.type.type);

				for (uint16_t i = 0; i < indices_size; ++i) {
					switch (o->op_load_access_list.access_list[i].kind) {
					case ACCESS_ELEMENT:
						access_kinds[i]  = ACCESS_SWIZZLE;
						plain_indices[i] = 0; // unused
						indices[i]       = convert_kong_index_to_spirv_id(o->op_load_access_list.access_list[i].access_element.index.index);
						break;
					case ACCESS_MEMBER: {
						int  member_index = 0;
						bool found        = false;

						for (; member_index < s->members.size; ++member_index) {
							if (s->members.m[member_index].name == o->op_load_access_list.access_list[i].access_member.name) {
								found = true;
								break;
							}
						}

						kong_assert(found);

						access_kinds[i]  = ACCESS_MEMBER;
						plain_indices[i] = member_index;
						indices[i]       = get_int_constant(member_index);

						break;
					}
					case ACCESS_SWIZZLE: {
						kong_assert(o->op_load_access_list.access_list[i].access_swizzle.swizzle.size == 1);

						access_kinds[i]  = ACCESS_SWIZZLE;
						plain_indices[i] = 0; // unused
						indices[i]       = get_int_constant(o->op_load_access_list.access_list[i].access_swizzle.swizzle.indices[0]);

						break;
					}
					}

					s = get_type(o->op_load_access_list.access_list[i].type);
				}

				type_id access_kong_type = find_access_type(plain_indices, access_kinds, indices_size, o->op_load_access_list.from.type.type);
				kong_assert(access_kong_type != NO_TYPE);

				spirv_id access_type = {0};

				switch (o->op_load_access_list.from.kind) {
				case VARIABLE_LOCAL:
					access_type = convert_pointer_type_to_spirv_id(access_kong_type, STORAGE_CLASS_FUNCTION);
					break;
				case VARIABLE_GLOBAL: {
					access_type = convert_pointer_type_to_spirv_id(access_kong_type, STORAGE_CLASS_UNIFORM);

					break;
				}
				case VARIABLE_INTERNAL:
					access_type = convert_pointer_type_to_spirv_id(access_kong_type, STORAGE_CLASS_INPUT);
					break;
				}

				spirv_id pointer =
				    write_op_access_chain(instructions, access_type, convert_kong_index_to_spirv_id(o->op_load_access_list.from.index), indices, indices_size);

				spirv_id value = write_op_load(instructions, convert_type_to_spirv_id(o->op_load_access_list.to.type.type), pointer);
				hmput(index_map, o->op_load_access_list.to.index, value);
			}
			break;
		}
		case OPCODE_LOAD_FLOAT_CONSTANT: {
			spirv_id id = get_float_constant(o->op_load_float_constant.number);
			hmput(index_map, o->op_load_float_constant.to.index, id);
			break;
		}
		case OPCODE_LOAD_INT_CONSTANT: {
			spirv_id id = get_int_constant(o->op_load_int_constant.number);
			hmput(index_map, o->op_load_int_constant.to.index, id);
			break;
		}
		case OPCODE_LOAD_BOOL_CONSTANT: {
			spirv_id id = get_bool_constant(o->op_load_bool_constant.boolean);
			hmput(index_map, o->op_load_bool_constant.to.index, id);
			break;
		}
		case OPCODE_CALL: {
			name_id func = o->op_call.func;

			char *func_name = get_name(func);

			if (func == add_name("sample")) {
				variable image_var = o->op_call.parameters[0];

				spirv_id image_type;
				spirv_id sampled_image_type;

				if (get_type(image_var.type.type)->tex_kind != TEXTURE_KIND_NONE) {
					image_type         = spirv_image_type;
					sampled_image_type = spirv_sampled_image_type;
				}

				spirv_id image         = write_op_load(instructions, image_type, convert_kong_index_to_spirv_id(image_var.index));
				spirv_id sampler       = write_op_load(instructions, spirv_sampler_type, convert_kong_index_to_spirv_id(o->op_call.parameters[1].index));
				spirv_id sampled_image = write_op_sampled_image(instructions, sampled_image_type, image, sampler);
				spirv_id coordinate    = get_var(instructions, o->op_call.parameters[2]);

				spirv_id id = write_op_image_sample_implicit_lod(instructions, spirv_float4_type, sampled_image, coordinate);

				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("sample_lod")) {
				variable image_var = o->op_call.parameters[0];

				spirv_id image_type;
				spirv_id sampled_image_type;

				if (get_type(image_var.type.type)->tex_kind != TEXTURE_KIND_NONE) {
					image_type         = spirv_image_type;
					sampled_image_type = spirv_sampled_image_type;
				}

				spirv_id image         = write_op_load(instructions, image_type, convert_kong_index_to_spirv_id(image_var.index));
				spirv_id sampler       = write_op_load(instructions, spirv_sampler_type, convert_kong_index_to_spirv_id(o->op_call.parameters[1].index));
				spirv_id sampled_image = write_op_sampled_image(instructions, sampled_image_type, image, sampler);
				spirv_id coordinate    = get_var(instructions, o->op_call.parameters[2]);
				spirv_id lod           = get_var(instructions, o->op_call.parameters[3]);

				spirv_id id = write_op_image_sample_explicit_lod(instructions, spirv_float4_type, sampled_image, coordinate, lod);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("float")) {
				if (o->op_call.parameters[0].type.type == int_id) {
					spirv_id id = write_op_convert_s_to_f(instructions, spirv_float_type, get_var(instructions, o->op_call.parameters[0]));
					hmput(index_map, o->op_call.var.index, id);
				}
				else if (o->op_call.parameters[0].type.type == uint_id) {
					spirv_id id = write_op_convert_u_to_f(instructions, spirv_float_type, get_var(instructions, o->op_call.parameters[0]));
					hmput(index_map, o->op_call.var.index, id);
				}
				////
				else if (o->op_call.parameters[0].type.type == float_id) {
					spirv_id id = get_var(instructions, o->op_call.parameters[0]);
					hmput(index_map, o->op_call.var.index, id);
				}
				////
				else {
					kong_assert(false);
				}
			}
			else if (func == add_name("float2")) {
				if (o->op_call.parameters_size == 1) {
					variable parameter = o->op_call.parameters[0];
					if (parameter.type.type == int2_id) {
						spirv_id id = write_op_convert_s_to_f(instructions, spirv_float2_type, convert_kong_index_to_spirv_id(parameter.index));
						hmput(index_map, o->op_call.var.index, id);
					}
					else if (parameter.type.type == uint2_id) {
						spirv_id id = write_op_convert_u_to_f(instructions, spirv_float2_type, convert_kong_index_to_spirv_id(parameter.index));
						hmput(index_map, o->op_call.var.index, id);
					}
					else {
						kong_assert(false);
					}
				}
				else if (o->op_call.parameters_size == 2) {
					spirv_id constituents[2];
					for (int i = 0; i < o->op_call.parameters_size; ++i) {
						constituents[i] = get_var(instructions, o->op_call.parameters[i]);
					}
					spirv_id id = write_op_composite_construct(instructions, spirv_float2_type, constituents, o->op_call.parameters_size);
					hmput(index_map, o->op_call.var.index, id);
				}
				else {
					kong_assert(false);
				}
			}
			else if (func == add_name("float3") && o->op_call.parameters_size == 1 && o->op_call.parameters[0].type.type == int3_id) {
				spirv_id id = write_op_convert_s_to_f(instructions, spirv_float3_type, get_var(instructions, o->op_call.parameters[0]));
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("float3") && o->op_call.parameters_size == 1 && o->op_call.parameters[0].type.type == uint3_id) {
				spirv_id id = write_op_convert_u_to_f(instructions, spirv_float3_type, get_var(instructions, o->op_call.parameters[0]));
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("float3")) {
				spirv_id constituents[3];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_float3_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("float4") && o->op_call.parameters_size == 1 && o->op_call.parameters[0].type.type == int4_id) {
				spirv_id id = write_op_convert_s_to_f(instructions, spirv_float4_type, get_var(instructions, o->op_call.parameters[0]));
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("float4") && o->op_call.parameters_size == 1 && o->op_call.parameters[0].type.type == uint4_id) {
				spirv_id id = write_op_convert_u_to_f(instructions, spirv_float4_type, get_var(instructions, o->op_call.parameters[0]));
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("float4")) {
				spirv_id constituents[4];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_float4_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("float3x3")) {
				spirv_id constituents[3];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_float3x3_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("float4x4")) {
				spirv_id constituents[4];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_float4x4_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("int")) {
				if (o->op_call.parameters[0].type.type == float_id) {
					spirv_id id = write_op_convert_f_to_s(instructions, spirv_int_type, get_var(instructions, o->op_call.parameters[0]));
					hmput(index_map, o->op_call.var.index, id);
				}
				else {
					kong_assert(false);
				}
			}
			else if (func == add_name("int2")) {
				if (o->op_call.parameters_size == 1) {
					spirv_id constituent = convert_kong_index_to_spirv_id(o->op_call.parameters[0].index);
					if (o->op_call.parameters[0].type.type == uint2_id) {
						spirv_id id = write_op_bitcast(instructions, spirv_int2_type, constituent);
						hmput(index_map, o->op_call.var.index, id);
					}
					else if (o->op_call.parameters[0].type.type == float2_id) {
						spirv_id id = write_op_convert_f_to_s(instructions, spirv_int2_type, constituent);
						hmput(index_map, o->op_call.var.index, id);
					}
					else {
						kong_assert(false);
					}
				}
				else {
					kong_assert(o->op_call.parameters_size == 2);
					spirv_id constituents[2];
					for (int i = 0; i < o->op_call.parameters_size; ++i) {
						constituents[i] = get_var(instructions, o->op_call.parameters[i]);
					}
					spirv_id id = write_op_composite_construct(instructions, spirv_int2_type, constituents, o->op_call.parameters_size);
					hmput(index_map, o->op_call.var.index, id);
				}
			}
			else if (func == add_name("int3")) {
				spirv_id constituents[3];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_int3_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("int4")) {
				spirv_id constituents[4];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_int4_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("uint")) {
				if (o->op_call.parameters[0].type.type == float_id) {
					spirv_id id = write_op_convert_f_to_u(instructions, spirv_uint_type, get_var(instructions, o->op_call.parameters[0]));
					hmput(index_map, o->op_call.var.index, id);
				}
				else if (o->op_call.parameters[0].type.type == int_id) {
					spirv_id id = write_op_bitcast(instructions, spirv_uint_type, get_var(instructions, o->op_call.parameters[0]));
					hmput(index_map, o->op_call.var.index, id);
				}
				else {
					kong_assert(false);
				}
			}
			else if (func == add_name("uint2")) {
				spirv_id constituents[2];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_uint2_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("uint3")) {
				spirv_id constituents[3];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_uint3_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("uint4")) {
				spirv_id constituents[4];
				for (int i = 0; i < o->op_call.parameters_size; ++i) {
					constituents[i] = get_var(instructions, o->op_call.parameters[i]);
				}
				spirv_id id = write_op_composite_construct(instructions, spirv_uint4_type, constituents, o->op_call.parameters_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("dispatch_thread_id")) {
				spirv_id id = write_op_load(instructions, convert_type_to_spirv_id(uint3_id), dispatch_thread_id_variable);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("group_thread_id")) {
				spirv_id id = write_op_load(instructions, convert_type_to_spirv_id(uint3_id), group_thread_id_variable);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("group_id")) {
				spirv_id id = write_op_load(instructions, convert_type_to_spirv_id(uint3_id), group_id_variable);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("vertex_id")) {
				spirv_id id = write_op_load(instructions, convert_type_to_spirv_id(uint_id), vertex_id_variable);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("dot")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_dot(instructions, spirv_float_type, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("ddx")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_dpdx(instructions, spirv_float_type, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("ddy")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_dpdy(instructions, spirv_float_type, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("round")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_ROUND, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("floor")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_FLOOR, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("sin")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_SIN, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("cos")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_COS, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("length")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_LENGTH, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("abs")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_FABS, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("ceil")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_CEIL, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("frac")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_FRACT, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("asin")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_ASIN, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("acos")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_ACOS, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("atan")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_ATAN, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("atan2")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_ATAN2, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("pow")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_POW, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("sqrt")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_SQRT, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("rsqrt")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_INVERSE_SQRT, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("min")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_FMIN, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("max")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_FMAX, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("clamp")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id operand3 = get_var(instructions, o->op_call.parameters[2]);
				spirv_id id       = write_op_ext_inst3(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_FCLAMP, operand1, operand2, operand3);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("lerp")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id operand3 = get_var(instructions, o->op_call.parameters[2]);
				spirv_id id       = write_op_ext_inst3(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_FMIX, operand1, operand2, operand3);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("step")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_STEP, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("smoothstep")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id operand3 = get_var(instructions, o->op_call.parameters[2]);
				spirv_id id       = write_op_ext_inst3(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_SMOOTHSTEP, operand1, operand2, operand3);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("distance")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_DISTANCE, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("cross")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_CROSS, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("normalize")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_NORMALIZE, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("reflect")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_REFLECT, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}

			////

			else if (func == add_name("tan")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_TAN, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("log")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_LOG, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("exp")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_EXP, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("sign")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_FSIGN, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("trunc")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_TRUNC, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("sinh")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_SINH, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("cosh")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_COSH, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("tanh")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_TANH, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("radians")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_RADIANS, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("degrees")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float_type, glsl_import, SPIRV_GLSL_STD_DEGREES, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("ddx2")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_dpdx(instructions, spirv_float2_type, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("ddy2")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_dpdy(instructions, spirv_float2_type, operand);
				hmput(index_map, o->op_call.var.index, id);
			}

			else if (func == add_name("ddx3")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_dpdx(instructions, spirv_float3_type, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("ddy3")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_dpdy(instructions, spirv_float3_type, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("clamp3")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id operand3 = get_var(instructions, o->op_call.parameters[2]);
				spirv_id id       = write_op_ext_inst3(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_FCLAMP, operand1, operand2, operand3);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("min3")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_FMIN, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("max3")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_FMAX, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("max4")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float4_type, glsl_import, SPIRV_GLSL_STD_FMAX, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("step3")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_STEP, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("pow3")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id id       = write_op_ext_inst2(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_POW, operand1, operand2);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("floor3")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_FLOOR, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("ceil3")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_CEIL, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("abs3")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_FABS, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("frac3")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_ext_inst(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_FRACT, operand);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("lerp3")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id operand3 = get_var(instructions, o->op_call.parameters[2]);

				if (o->op_call.parameters[2].type.type == float_id) {
					spirv_id operands[3] = {operand3, operand3, operand3};
					operand3             = write_op_composite_construct(instructions, spirv_float3_type, operands, 3);
				}

				spirv_id id = write_op_ext_inst3(instructions, spirv_float3_type, glsl_import, SPIRV_GLSL_STD_FMIX, operand1, operand2, operand3);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("lerp4")) {
				spirv_id operand1 = get_var(instructions, o->op_call.parameters[0]);
				spirv_id operand2 = get_var(instructions, o->op_call.parameters[1]);
				spirv_id operand3 = get_var(instructions, o->op_call.parameters[2]);

				if (o->op_call.parameters[2].type.type == float_id) {
					spirv_id operands[4] = {operand3, operand3, operand3, operand3};
					operand3             = write_op_composite_construct(instructions, spirv_float4_type, operands, 4);
				}

				spirv_id id = write_op_ext_inst3(instructions, spirv_float4_type, glsl_import, SPIRV_GLSL_STD_FMIX, operand1, operand2, operand3);
				hmput(index_map, o->op_call.var.index, id);
			}
			else if (func == add_name("transpose")) {
				spirv_id operand = get_var(instructions, o->op_call.parameters[0]);
				spirv_id id      = write_op_transpose(instructions, spirv_float3x3_type, operand);
				hmput(index_map, o->op_call.var.index, id);
			}

			////

			else {
				spirv_id return_type;

				for (function_id i = 0; get_function(i) != NULL; ++i) {
					function *f = get_function(i);
					if (f->name == func) {
						return_type = convert_type_to_spirv_id(f->return_type.type);
						break;
					}
				}

				spirv_id arguments[256];
				uint8_t  arguments_size = o->op_call.parameters_size;
				for (uint8_t i = 0; i < arguments_size; ++i) {
					arguments[i] = get_var(instructions, o->op_call.parameters[i]);
				}

				spirv_id fun_id = hmget(function_map, func);
				spirv_id id     = write_op_function_call(instructions, return_type, fun_id, arguments, arguments_size);
				hmput(index_map, o->op_call.var.index, id);
			}
			break;
		}
		case OPCODE_STORE_ACCESS_LIST:
		case OPCODE_ADD_AND_STORE_ACCESS_LIST:
		case OPCODE_SUB_AND_STORE_ACCESS_LIST:
		case OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST:
		case OPCODE_DIVIDE_AND_STORE_ACCESS_LIST: {
			spirv_id    indices[256];
			int         plain_indices[256];
			access_kind access_kinds[256];

			uint16_t indices_size = o->op_store_access_list.access_list_size;

			type *s = get_type(o->op_store_access_list.to.type.type);

			if (get_type(o->op_store_access_list.to.type.type)->tex_kind == TEXTURE_KIND_2D) {
				kong_assert(indices_size == 1);
				kong_assert(o->op_store_access_list.access_list[0].kind == ACCESS_ELEMENT);

				spirv_id image = write_op_load(instructions, spirv_readwrite_image_type, convert_kong_index_to_spirv_id(o->op_store_access_list.to.index));

				variable coordinate_var = o->op_store_access_list.access_list[0].access_element.index;
				spirv_id coordinate     = get_var(instructions, coordinate_var);
				spirv_id texel          = get_var(instructions, o->op_store_access_list.from);

				write_op_image_write(instructions, image, coordinate, texel);
			}
			else {
				for (uint16_t i = 0; i < indices_size; ++i) {
					switch (o->op_store_access_list.access_list[i].kind) {
					case ACCESS_ELEMENT:
						access_kinds[i]  = ACCESS_ELEMENT;
						plain_indices[i] = 0; // unused

						indices[i] = convert_kong_index_to_spirv_id(o->op_store_access_list.access_list[i].access_element.index.index);

						break;
					case ACCESS_MEMBER: {
						int  member_index = 0;
						bool found        = false;

						for (; member_index < s->members.size; ++member_index) {
							if (s->members.m[member_index].name == o->op_store_access_list.access_list[i].access_member.name) {
								found = true;
								break;
							}
						}

						kong_assert(found);

						access_kinds[i]  = ACCESS_MEMBER;
						plain_indices[i] = member_index;

						indices[i] = get_int_constant(member_index);

						break;
					}
					case ACCESS_SWIZZLE: {
						kong_assert(o->op_store_access_list.access_list[i].access_swizzle.swizzle.size == 1);

						access_kinds[i]  = ACCESS_SWIZZLE;
						plain_indices[i] = 0; // unused

						indices[i] = get_int_constant(o->op_store_access_list.access_list[i].access_swizzle.swizzle.indices[0]);

						break;
					}
					}

					s = get_type(o->op_store_access_list.access_list[i].type);
				}

				type_id access_kong_type = find_access_type(plain_indices, access_kinds, indices_size, o->op_store_access_list.to.type.type);
				kong_assert(access_kong_type != NO_TYPE);

				spirv_id access_type = {0};

				switch (o->op_store_access_list.to.kind) {
				case VARIABLE_LOCAL:
					access_type = convert_pointer_type_to_spirv_id(access_kong_type, STORAGE_CLASS_FUNCTION);
					break;
				case VARIABLE_GLOBAL:
					access_type = convert_pointer_type_to_spirv_id(access_kong_type, STORAGE_CLASS_OUTPUT);
					break;
				case VARIABLE_INTERNAL:
					kong_assert(false);
					break;
				}

				spirv_id pointer =
				    write_op_access_chain(instructions, access_type, convert_kong_index_to_spirv_id(o->op_store_access_list.to.index), indices, indices_size);

				spirv_id result;

				if (o->type == OPCODE_STORE_ACCESS_LIST) {
					result = get_var(instructions, o->op_store_access_list.from);
				}
				else {
					spirv_id loaded = write_op_load(instructions, convert_type_to_spirv_id(access_kong_type), pointer);
					spirv_id from   = get_var(instructions, o->op_store_access_list.from);

					if (o->type == OPCODE_ADD_AND_STORE_ACCESS_LIST) {
						if (vector_base_type(access_kong_type) == float_id) {
							result = write_op_f_add(instructions, convert_type_to_spirv_id(access_kong_type), loaded, from);
						}
						else if (vector_base_type(access_kong_type) == int_id || vector_base_type(access_kong_type) == uint_id) {
							result = write_op_i_add(instructions, convert_type_to_spirv_id(access_kong_type), loaded, from);
						}
					}
					else if (o->type == OPCODE_SUB_AND_STORE_ACCESS_LIST) {
						if (vector_base_type(access_kong_type) == float_id) {
							result = write_op_f_sub(instructions, convert_type_to_spirv_id(access_kong_type), loaded, from);
						}
						else if (vector_base_type(access_kong_type) == int_id || vector_base_type(access_kong_type) == uint_id) {
							result = write_op_i_sub(instructions, convert_type_to_spirv_id(access_kong_type), loaded, from);
						}
					}
					else if (o->type == OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST) {
						result = write_op_mul(instructions, access_kong_type, loaded, from);
					}
					else if (o->type == OPCODE_DIVIDE_AND_STORE_ACCESS_LIST) {
						result = write_op_div(instructions, access_kong_type, loaded, from);
					}
				}

				write_op_store(instructions, pointer, result);
			}
			break;
		}
		case OPCODE_AND: {
			spirv_id result = write_op_logical_and(instructions, spirv_bool_type, convert_kong_index_to_spirv_id(o->op_binary.left.index),
			                                       convert_kong_index_to_spirv_id(o->op_binary.right.index));
			hmput(index_map, o->op_binary.result.index, result);
			break;
		}
		case OPCODE_OR: {
			spirv_id result = write_op_logical_or(instructions, spirv_bool_type, convert_kong_index_to_spirv_id(o->op_binary.left.index),
			                                      convert_kong_index_to_spirv_id(o->op_binary.right.index));
			hmput(index_map, o->op_binary.result.index, result);
			break;
		}
		case OPCODE_BITWISE_XOR: {
			spirv_id left   = get_var(instructions, o->op_binary.left);
			spirv_id right  = get_var(instructions, o->op_binary.right);
			spirv_id result = write_op_bitwise_xor(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
			hmput(index_map, o->op_binary.result.index, result);
			break;
		}
		case OPCODE_BITWISE_AND: {
			spirv_id left   = get_var(instructions, o->op_binary.left);
			spirv_id right  = get_var(instructions, o->op_binary.right);
			spirv_id result = write_op_bitwise_and(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
			hmput(index_map, o->op_binary.result.index, result);
			break;
		}
		case OPCODE_BITWISE_OR: {
			spirv_id left   = get_var(instructions, o->op_binary.left);
			spirv_id right  = get_var(instructions, o->op_binary.right);
			spirv_id result = write_op_bitwise_or(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
			hmput(index_map, o->op_binary.result.index, result);
			break;
		}
		case OPCODE_LEFT_SHIFT: {
			spirv_id left   = get_var(instructions, o->op_binary.left);
			spirv_id right  = get_var(instructions, o->op_binary.right);
			spirv_id result = write_op_left_shift(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
			hmput(index_map, o->op_binary.result.index, result);
			break;
		}
		case OPCODE_RIGHT_SHIFT: {
			spirv_id left   = get_var(instructions, o->op_binary.left);
			spirv_id right  = get_var(instructions, o->op_binary.right);
			spirv_id result = write_op_right_shift(instructions, o->op_binary.result.type.type, left, right);
			hmput(index_map, o->op_binary.result.index, result);
			break;
		}
		case OPCODE_NOT: {
			spirv_id operand = get_var(instructions, o->op_not.from);
			spirv_id result  = write_op_not(instructions, spirv_bool_type, operand);
			hmput(index_map, o->op_not.to.index, result);
			break;
		}
		case OPCODE_NEGATE: {
			spirv_id from = get_var(instructions, o->op_negate.from);

			if (vector_base_type(o->op_negate.from.type.type) == float_id) {
				spirv_id result = write_op_f_negate(instructions, convert_type_to_spirv_id(o->op_negate.to.type.type), from);
				hmput(index_map, o->op_negate.to.index, result);
			}
			else if (vector_base_type(o->op_negate.from.type.type) == int_id || vector_base_type(o->op_negate.from.type.type) == uint_id) {
				spirv_id result = write_op_s_negate(instructions, convert_type_to_spirv_id(o->op_negate.to.type.type), from);
				hmput(index_map, o->op_negate.to.index, result);
			}

			break;
		}
		case OPCODE_STORE_VARIABLE: {
			spirv_id from = get_var(instructions, o->op_store_var.from);
			write_op_store(instructions, convert_kong_index_to_spirv_id(o->op_store_var.to.index), from);
			break;
		}
		case OPCODE_ADD_AND_STORE_VARIABLE:
		case OPCODE_SUB_AND_STORE_VARIABLE:
		case OPCODE_MULTIPLY_AND_STORE_VARIABLE:
		case OPCODE_DIVIDE_AND_STORE_VARIABLE: {
			spirv_id from = get_var(instructions, o->op_store_var.from);
			spirv_id to   = get_var(instructions, o->op_store_var.to);
			spirv_id result;

			switch (o->type) {
			case OPCODE_ADD_AND_STORE_VARIABLE: {
				if (vector_base_type(o->op_store_var.to.type.type) == float_id) {
					result = write_op_f_add(instructions, convert_type_to_spirv_id(o->op_store_var.to.type.type), to, from);
				}
				else if (vector_base_type(o->op_store_var.to.type.type) == int_id || vector_base_type(o->op_store_var.to.type.type) == uint_id) {
					result = write_op_i_add(instructions, convert_type_to_spirv_id(o->op_store_var.to.type.type), to, from);
				}
				break;
			}
			case OPCODE_SUB_AND_STORE_VARIABLE: {
				if (vector_base_type(o->op_store_var.to.type.type) == float_id) {
					result = write_op_f_sub(instructions, convert_type_to_spirv_id(o->op_store_var.to.type.type), to, from);
				}
				else if (vector_base_type(o->op_store_var.to.type.type) == int_id || vector_base_type(o->op_store_var.to.type.type) == uint_id) {
					result = write_op_i_sub(instructions, convert_type_to_spirv_id(o->op_store_var.to.type.type), to, from);
				}
				break;
			}
			case OPCODE_MULTIPLY_AND_STORE_VARIABLE: {
				result = write_op_mul(instructions, o->op_store_var.to.type.type, to, from);
				break;
			}
			case OPCODE_DIVIDE_AND_STORE_VARIABLE: {
				result = write_op_div(instructions, o->op_store_var.to.type.type, to, from);
				break;
			}
			default:
				kong_assert(false);
				break;
			}

			write_op_store(instructions, convert_kong_index_to_spirv_id(o->op_store_var.to.index), result);

			break;
		}
		case OPCODE_RETURN: {
			if (stage == SHADER_STAGE_VERTEX && main) {
				type *output_type = get_type(output);

				for (size_t i = 0; i < output_type->members.size; ++i) {
					member m = output_type->members.m[i];

					spirv_id index = get_int_constant((int)i);
					spirv_id spirv_type;
					if (m.type.type == float2_id) {
						spirv_type = spirv_float2_type;
					}
					else if (m.type.type == float3_id) {
						spirv_type = spirv_float3_type;
					}
					else if (m.type.type == float4_id) {
						spirv_type = spirv_float4_type;
					}
					else {
						debug_context context = {0};
						error(context, "Type unsupported for input in SPIR-V");
					}

					spirv_id load_pointer = write_op_access_chain(instructions, convert_pointer_type_to_spirv_id(m.type.type, STORAGE_CLASS_FUNCTION),
					                                              convert_kong_index_to_spirv_id(o->op_return.var.index), &index, 1);
					spirv_id value        = write_op_load(instructions, spirv_type, load_pointer);

					if (i == 0) {
						// position
						spirv_id store_pointer =
						    write_op_access_chain(instructions, convert_pointer_type_to_spirv_id(m.type.type, STORAGE_CLASS_OUTPUT), output_vars[i], &index, 1);
						write_op_store(instructions, store_pointer, value);
					}
					else {
						write_op_store(instructions, output_vars[i], value);
					}
				}
				write_op_return(instructions);
			}
			else if (stage == SHADER_STAGE_FRAGMENT && main) {
				type_id pixel_output = f->return_type.type;
				type   *output       = get_type(pixel_output);

				if (output->array_size > 0) {
					for (uint32_t array_index = 0; array_index < output->array_size; ++array_index) {
						spirv_id index  = get_int_constant((int)array_index);
						spirv_id chain  = write_op_access_chain(instructions, convert_pointer_type_to_spirv_id(output->base, STORAGE_CLASS_FUNCTION),
						                                        convert_kong_index_to_spirv_id(o->op_return.var.index), &index, 1);
						spirv_id loaded = write_op_load(instructions, convert_type_to_spirv_id(float4_id), chain);

						write_op_store(instructions, output_vars[array_index], loaded);
					}
				}
				else {
					spirv_id loaded = get_var(instructions, o->op_return.var);
					write_op_store(instructions, output_vars[0], loaded);
				}
				write_op_return(instructions);
			}
			else {
				spirv_id return_value = get_var(instructions, o->op_return.var);
				write_op_return_value(instructions, return_value);
			}
			ends_with_return                      = true;
			next_block_branch_id[nested_if_count] = 0;
			break;
		}
		case OPCODE_DISCARD: {
			write_op_discard(instructions);
			ends_with_return                      = true;
			next_block_branch_id[nested_if_count] = 0;
			break;
		}
		case OPCODE_LESS: {
			kong_assert(o->op_binary.left.type.type == o->op_binary.right.type.type);

			spirv_id left  = get_var(instructions, o->op_binary.left);
			spirv_id right = get_var(instructions, o->op_binary.right);

			spirv_id result;

			if (vector_base_type(o->op_binary.left.type.type) == float_id) {
				result = write_op_f_ord_less_than(instructions, spirv_bool_type, left, right);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == int_id) {
				result = write_op_s_less_than(instructions, spirv_bool_type, left, right);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == uint_id) {
				result = write_op_u_less_than(instructions, spirv_bool_type, left, right);
			}
			else {
				kong_assert(false);
			}

			hmput(index_map, o->op_binary.result.index, result);

			break;
		}
		case OPCODE_LESS_EQUAL: {
			kong_assert(o->op_binary.left.type.type == o->op_binary.right.type.type);

			spirv_id left  = get_var(instructions, o->op_binary.left);
			spirv_id right = get_var(instructions, o->op_binary.right);

			spirv_id result;

			if (vector_base_type(o->op_binary.left.type.type) == float_id) {
				result = write_op_f_ord_less_than_equal(instructions, spirv_bool_type, left, right);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == int_id) {
				result = write_op_s_less_than_equal(instructions, spirv_bool_type, left, right);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == uint_id) {
				result = write_op_u_less_than_equal(instructions, spirv_bool_type, left, right);
			}
			else {
				kong_assert(false);
			}

			hmput(index_map, o->op_binary.result.index, result);

			break;
		}
		case OPCODE_GREATER: {
			kong_assert(o->op_binary.left.type.type == o->op_binary.right.type.type);

			spirv_id left  = get_var(instructions, o->op_binary.left);
			spirv_id right = get_var(instructions, o->op_binary.right);

			spirv_id result;

			if (vector_base_type(o->op_binary.left.type.type) == float_id) {
				result = write_op_f_ord_greater_than(instructions, spirv_bool_type, left, right);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == int_id) {
				result = write_op_s_greater_than(instructions, spirv_bool_type, left, right);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == uint_id) {
				result = write_op_u_greater_than(instructions, spirv_bool_type, left, right);
			}
			else {
				kong_assert(false);
			}

			hmput(index_map, o->op_binary.result.index, result);

			break;
		}
		case OPCODE_GREATER_EQUAL: {
			kong_assert(o->op_binary.left.type.type == o->op_binary.right.type.type);

			spirv_id left  = get_var(instructions, o->op_binary.left);
			spirv_id right = get_var(instructions, o->op_binary.right);

			spirv_id result;

			if (vector_base_type(o->op_binary.left.type.type) == float_id) {
				result = write_op_f_ord_greater_than_equal(instructions, spirv_bool_type, left, right);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == int_id) {
				result = write_op_s_greater_than_equal(instructions, spirv_bool_type, left, right);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == uint_id) {
				result = write_op_u_greater_than_equal(instructions, spirv_bool_type, left, right);
			}
			else {
				kong_assert(false);
			}

			hmput(index_map, o->op_binary.result.index, result);

			break;
		}
		case OPCODE_ADD: {
			spirv_id left  = get_var(instructions, o->op_binary.left);
			spirv_id right = get_var(instructions, o->op_binary.right);

			if (vector_base_type(o->op_binary.result.type.type) == float_id) {
				spirv_id result = write_op_f_add(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
				hmput(index_map, o->op_binary.result.index, result);
			}
			else if (vector_base_type(o->op_binary.result.type.type) == int_id || vector_base_type(o->op_binary.result.type.type) == uint_id) {
				spirv_id result = write_op_i_add(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
				hmput(index_map, o->op_binary.result.index, result);
			}
			else {
				kong_assert(false);
			}

			break;
		}
		case OPCODE_SUB: {
			spirv_id left        = get_var(instructions, o->op_binary.left);
			spirv_id right       = get_var(instructions, o->op_binary.right);
			type_id  result_type = o->op_binary.result.type.type;

			if (result_type == int_id || result_type == int2_id || result_type == int3_id || result_type == int4_id || result_type == uint_id ||
			    result_type == uint2_id || result_type == uint3_id || result_type == uint4_id) {
				spirv_id result = write_op_i_sub(instructions, convert_type_to_spirv_id(result_type), left, right);
				hmput(index_map, o->op_binary.result.index, result);
			}
			else if (result_type == float_id || result_type == float2_id || result_type == float3_id || result_type == float4_id) {
				spirv_id result = write_op_f_sub(instructions, convert_type_to_spirv_id(result_type), left, right);
				hmput(index_map, o->op_binary.result.index, result);
			}

			break;
		}
		case OPCODE_MULTIPLY: {
			spirv_id left            = get_var(instructions, o->op_binary.left);
			spirv_id right           = get_var(instructions, o->op_binary.right);
			bool     left_is_matrix  = is_matrix(o->op_binary.left.type.type);
			bool     right_is_matrix = is_matrix(o->op_binary.right.type.type);
			spirv_id result;

			if (left_is_matrix && right_is_matrix) {
				result = write_op_matrix_times_matrix(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
			}
			else if (left_is_matrix) {
				result = write_op_matrix_times_vector(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
			}
			else if (right_is_matrix) {
				result = write_op_vector_times_matrix(instructions, convert_type_to_spirv_id(o->op_binary.result.type.type), left, right);
			}
			else {
				result = write_op_mul(instructions, o->op_binary.result.type.type, left, right);
			}

			hmput(index_map, o->op_binary.result.index, result);

			break;
		}
		case OPCODE_DIVIDE: {
			spirv_id left      = get_var(instructions, o->op_binary.left);
			spirv_id right     = get_var(instructions, o->op_binary.right);
			spirv_id type      = convert_type_to_spirv_id(o->op_binary.result.type.type);
			type_id  base_type = vector_base_type(o->op_binary.result.type.type);
			spirv_id result;

			if (base_type == uint_id) {
				result = write_op_u_div(instructions, type, left, right);
			}
			else if (base_type == int_id) {
				result = write_op_s_div(instructions, type, left, right);
			}
			else {
				result = write_op_f_div(instructions, type, left, right);
			}

			hmput(index_map, o->op_binary.result.index, result);

			break;
		}
		case OPCODE_MOD: {
			spirv_id left      = get_var(instructions, o->op_binary.left);
			spirv_id right     = get_var(instructions, o->op_binary.right);
			spirv_id type      = convert_type_to_spirv_id(o->op_binary.result.type.type);
			type_id  base_type = vector_base_type(o->op_binary.result.type.type);
			spirv_id result;

			if (base_type == uint_id) {
				result = write_op_u_mod(instructions, type, left, right);
			}
			else if (base_type == int_id) {
				result = write_op_s_mod(instructions, type, left, right);
			}
			else {
				result = write_op_f_mod(instructions, type, left, right);
			}

			hmput(index_map, o->op_binary.result.index, result);

			break;
		}
		case OPCODE_EQUALS: {
			spirv_id left  = get_var(instructions, o->op_binary.left);
			spirv_id right = get_var(instructions, o->op_binary.right);

			if (vector_base_type(o->op_binary.left.type.type) == float_id) {
				spirv_id result = write_op_f_ord_equal(instructions, spirv_bool_type, left, right);
				hmput(index_map, o->op_binary.result.index, result);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == int_id || vector_base_type(o->op_binary.left.type.type) == uint_id) {
				spirv_id result = write_op_i_equal(instructions, spirv_bool_type, left, right);
				hmput(index_map, o->op_binary.result.index, result);
			}

			break;
		}
		case OPCODE_NOT_EQUALS: {
			spirv_id left  = get_var(instructions, o->op_binary.left);
			spirv_id right = get_var(instructions, o->op_binary.right);

			if (vector_base_type(o->op_binary.left.type.type) == float_id) {
				spirv_id result = write_op_f_ord_not_equal(instructions, spirv_bool_type, left, right);
				hmput(index_map, o->op_binary.result.index, result);
			}
			else if (vector_base_type(o->op_binary.left.type.type) == int_id || vector_base_type(o->op_binary.left.type.type) == uint_id) {
				spirv_id result = write_op_i_not_equal(instructions, spirv_bool_type, left, right);
				hmput(index_map, o->op_binary.result.index, result);
			}

			break;
		}
		case OPCODE_IF: {
			nested_if_count++;
			next_block_branch_id[nested_if_count] = o->op_if.end_id;
			next_block_label_id[nested_if_count]  = o->op_if.end_id;
			write_op_selection_merge(instructions, convert_kong_index_to_spirv_id(o->op_if.end_id), SELECTION_CONTROL_NONE);

			write_op_branch_conditional(instructions, convert_kong_index_to_spirv_id(o->op_if.condition.index),
			                            convert_kong_index_to_spirv_id(o->op_if.start_id), convert_kong_index_to_spirv_id(o->op_if.end_id));

			write_op_label_preallocated(instructions, convert_kong_index_to_spirv_id(o->op_if.start_id));

			break;
		}
		case OPCODE_WHILE_START: {
			spirv_id while_start_label    = convert_kong_index_to_spirv_id(o->op_while_start.start_id);
			spirv_id while_continue_label = convert_kong_index_to_spirv_id(o->op_while_start.continue_id);
			spirv_id while_end_label      = convert_kong_index_to_spirv_id(o->op_while_start.end_id);

			write_op_branch(instructions, while_start_label);
			write_op_label_preallocated(instructions, while_start_label);

			write_op_loop_merge(instructions, while_end_label, while_continue_label, LOOP_CONTROL_NONE);

			spirv_id loop_start_id = allocate_index();
			write_op_branch(instructions, loop_start_id);
			write_op_label_preallocated(instructions, loop_start_id);
			break;
		}
		case OPCODE_WHILE_CONDITION: {
			spirv_id while_end_label = convert_kong_index_to_spirv_id(o->op_while.end_id);

			spirv_id pass = allocate_index();

			write_op_branch_conditional(instructions, convert_kong_index_to_spirv_id(o->op_while.condition.index), pass, while_end_label);

			write_op_label_preallocated(instructions, pass);
			break;
		}
		case OPCODE_WHILE_END: {
			spirv_id while_start_label    = convert_kong_index_to_spirv_id(o->op_while_end.start_id);
			spirv_id while_continue_label = convert_kong_index_to_spirv_id(o->op_while_end.continue_id);
			spirv_id while_end_label      = convert_kong_index_to_spirv_id(o->op_while_end.end_id);

			write_op_branch(instructions, while_continue_label);
			write_op_label_preallocated(instructions, while_continue_label);

			write_op_branch(instructions, while_start_label);
			write_op_label_preallocated(instructions, while_end_label);
			break;
		}
		case OPCODE_BLOCK_START: {
			break;
		}
		case OPCODE_BLOCK_END: {
			if (o->op_block.id == next_block_branch_id[nested_if_count]) {
				write_op_branch(instructions, convert_kong_index_to_spirv_id(o->op_block.id));
			}
			if (o->op_block.id == next_block_label_id[nested_if_count]) {
				write_op_label_preallocated(instructions, convert_kong_index_to_spirv_id(o->op_block.id));
				nested_if_count--;
			}
			break;
		}
		default: {
			debug_context context = {0};
			error(context, "Opcode %d not implemented for SPIR-V", o->type);
			break;
		}
		}

		index += o->size;
	}

	if (!ends_with_return) {
		if (main) {
			kong_assert(stage == SHADER_STAGE_COMPUTE);
		}
		write_op_return(instructions);
	}
	write_op_function_end(instructions);
}

static void write_functions(instructions_buffer *instructions, function *main, spirv_id entry_point, shader_stage stage, type_id output) {
	function *functions[256];
	size_t    functions_size = 0;

	if (main != NULL) {
		functions[functions_size] = main;
		functions_size += 1;

		find_referenced_functions(main, functions, &functions_size);
	}

	for (size_t i = 0; i < functions_size; ++i) {
		function *f = functions[i];

		spirv_id fun_id = (f == main) ? entry_point : allocate_index();
		hmput(function_map, f->name, fun_id);
	}

	spirv_id function_types[256];
	for (size_t i = 0; i < functions_size; ++i) {
		function *f = functions[i];

		if (f == main) {
			function_types[i] = void_function_type;
		}
		else {
			spirv_id return_type = convert_type_to_spirv_id(f->return_type.type);

			spirv_id parameter_types[256];
			uint8_t  parameter_types_size = 0;
			for (uint8_t parameter_index = 0; parameter_index < f->parameters_size; ++parameter_index) {
				parameter_types[parameter_index] = convert_type_to_spirv_id(f->parameter_types[parameter_index].type);
				parameter_types_size++;
			}

			int function_type_index = -1;
			for (size_t j = 0; j < i; ++j) {
				function *f2 = functions[j];
				if (return_type.id != convert_type_to_spirv_id(f2->return_type.type).id || f->parameters_size != f2->parameters_size) {
					continue;
				}
				bool parameters_match = true;
				for (uint8_t parameter_index = 0; parameter_index < f2->parameters_size; ++parameter_index) {
					if (parameter_types[parameter_index].id != convert_type_to_spirv_id(f2->parameter_types[parameter_index].type).id) {
						parameters_match = false;
						break;
					}
				}
				if (parameters_match) {
					function_type_index = (int)j;
					break;
				}
			}

			if (function_type_index == -1) {
				function_types[i] = write_type_function(instructions, return_type, parameter_types, parameter_types_size);
			}
			else {
				function_types[i] = function_types[function_type_index];
			}
		}
	}

	for (size_t i = 0; i < functions_size; ++i) {
		function *f = functions[i];

		spirv_id return_type = f == main ? void_type : convert_type_to_spirv_id(f->return_type.type);
		spirv_id fun_type    = function_types[i];
		spirv_id fun_id      = hmget(function_map, f->name);
		write_function(instructions, f, return_type, fun_type, fun_id, stage, f == main, output);
	}
}

static void write_int_constant(struct container *container, void *data) {
	int_constant_container *int_constant = (int_constant_container *)container;
	instructions_buffer    *instructions = (instructions_buffer *)data;

	write_constant_int(instructions, int_constant->value, int_constant->container.key);
}

static void write_constants(instructions_buffer *instructions) {
	hash_map_iterate(int_constants, write_int_constant, instructions);

	size_t size = hmlenu(uint_constants);
	for (size_t i = 0; i < size; ++i) {
		write_constant_uint(instructions, uint_constants[i].value, uint_constants[i].key);
	}

	size = hmlenu(float_constants);
	for (size_t i = 0; i < size; ++i) {
		write_constant_float(instructions, float_constants[i].value, float_constants[i].key);
	}

	size = hmlenu(bool_constants);
	for (size_t i = 0; i < size; ++i) {
		write_constant_bool(instructions, bool_constants[i].value, bool_constants[i].key);
	}
}

static void assign_bindings(uint32_t *bindings, function *shader) {
	descriptor_set_group *set_group = get_descriptor_set_group(shader->descriptor_set_group_index);

	for (size_t group_index = 0; group_index < set_group->size; ++group_index) {
		uint32_t binding = 0;

		descriptor_set *set = set_group->values[group_index];

		for (size_t g_index = 0; g_index < set->globals.size; ++g_index) {
			global_id global_index = set->globals.globals[g_index];

			global *g = get_global(global_index);

			type   *t         = get_type(g->type);
			type_id base_type = t->array_size > 0 ? t->base : g->type;

			if (base_type == sampler_type_id) {
				bindings[global_index] = binding;
				binding += 1;
			}
			else if (get_type(base_type)->tex_kind != TEXTURE_KIND_NONE) {
				if (t->array_size == UINT32_MAX) {
					bindings[global_index] = 0;
				}
				else {
					bindings[global_index] = binding;
					binding += 1;
				}
			}
			else if (base_type == bvh_type_id) {
				bindings[global_index] = binding;
				binding += 1;
			}
			else if (get_type(g->type)->built_in) {
				if (get_type(g->type)->array_size > 0) {
					bindings[global_index] = binding;
					binding += 1;
				}
			}
			else {
				if (get_type(g->type)->array_size > 0) {
					bindings[global_index] = binding;
					binding += 1;
				}
				else {
					bindings[global_index] = binding;
					binding += 1;
				}
			}
		}
	}
}

static uint32_t member_size(type_id member_type) {
	if (member_type == float_id || member_type == int_id || member_type == uint_id) {
		return 4;
	}
	if (member_type == float2_id || member_type == int2_id || member_type == uint2_id) {
		return 8;
	}
	if (member_type == float3_id || member_type == int3_id || member_type == uint3_id) {
		return 16;
	}
	if (member_type == float4_id || member_type == int4_id || member_type == uint4_id) {
		return 16;
	}
	if (member_type == float3x3_id) {
		return 48;
	}
	if (member_type == float4x4_id) {
		return 64;
	}
	return 0;
}

static uint32_t member_padding(uint32_t offset, uint32_t size) {
	if (size > 16) {
		size = 16;
	}
	return (size - (offset % size)) % size;
}

static void write_globals(instructions_buffer *decorations, instructions_buffer *aggregate_types_block, instructions_buffer *global_vars_block, function *main,
                          shader_stage stage) {
	uint32_t bindings[512] = {0};
	assign_bindings(bindings, main);

	global_array globals = {0};

	find_referenced_globals(main, &globals);

	descriptor_set_group *group = find_descriptor_set_group_for_function(main);
	for (size_t descriptor_set_index = 0; descriptor_set_index < group->size; ++descriptor_set_index) {
		descriptor_set *set = group->values[descriptor_set_index];
		for (size_t global_index = 0; global_index < set->globals.size; ++global_index) {
			global_id id = set->globals.globals[global_index];
			for (size_t function_global_index = 0; function_global_index < globals.size; ++function_global_index) {
				global_id function_global_id = globals.globals[function_global_index];

				if (id == function_global_id) {
					if (set->globals.writable[global_index]) {
						globals.writable[function_global_index] = true;
					}
					break;
				}
			}
		}
	}

	for (size_t i = 0; i < globals.size; ++i) {
		global  *g       = get_global(globals.globals[i]);
		uint32_t binding = bindings[globals.globals[i]];

		type   *t         = get_type(g->type);
		type_id base_type = t->array_size > 0 ? t->base : g->type;
		bool    readable  = globals.readable[i];
		bool    writable  = globals.writable[i];

		////
		readable = false;
		writable = false;
		////

		if (base_type == sampler_type_id) {
			add_to_type_map(g->type, spirv_sampler_type, false, STORAGE_CLASS_NONE);
			add_to_type_map(g->type, spirv_sampler_pointer_type, false, STORAGE_CLASS_UNIFORM_CONSTANT);

			spirv_id spirv_var_id = convert_kong_index_to_spirv_id(g->var_index);
			write_op_variable_preallocated(global_vars_block, spirv_sampler_pointer_type, spirv_var_id, STORAGE_CLASS_UNIFORM_CONSTANT);

			write_op_decorate_value(decorations, spirv_var_id, DECORATION_DESCRIPTOR_SET, 0);
			write_op_decorate_value(decorations, spirv_var_id, DECORATION_BINDING, binding);
		}
		else if (get_type(base_type)->tex_kind != TEXTURE_KIND_NONE) {
			if (t->array_size == UINT32_MAX) {
				kong_assert(false);
			}
			else {
				spirv_id image_pointer_type;

				if (readable || writable) {
					add_to_type_map(g->type, spirv_readwrite_image_type, true, STORAGE_CLASS_NONE);
					image_pointer_type = spirv_readwrite_image_pointer_type;
				}
				else {
					add_to_type_map(g->type, spirv_image_type, false, STORAGE_CLASS_NONE);
					image_pointer_type = spirv_image_pointer_type;
				}

				add_to_type_map(g->type, image_pointer_type, readable || writable, STORAGE_CLASS_UNIFORM_CONSTANT);

				spirv_id spirv_var_id = convert_kong_index_to_spirv_id(g->var_index);
				write_op_variable_preallocated(global_vars_block, image_pointer_type, spirv_var_id, STORAGE_CLASS_UNIFORM_CONSTANT);

				write_op_decorate_value(decorations, spirv_var_id, DECORATION_DESCRIPTOR_SET, 0);
				write_op_decorate_value(decorations, spirv_var_id, DECORATION_BINDING, binding);
			}
		}
		else if (base_type == bvh_type_id) {
			kong_assert(false);
		}
		else if (base_type == float_id) {
			spirv_id id = get_float_constant(g->value.value.floats[0]);
			hmput(index_map, g->var_index, id);
		}
		else if (base_type == float2_id) {
			kong_assert(false);
		}
		else if (base_type == float3_id) {
			kong_assert(false);
		}
		else if (base_type == float4_id) {
			kong_assert(false);
		}
		else {
			storage_class storage = STORAGE_CLASS_UNIFORM;

			type *t = get_type(g->type);

			spirv_id member_types[256];
			uint16_t member_types_size = 0;
			for (size_t j = 0; j < t->members.size; ++j) {
				type_id member_type = t->members.m[j].type.type;

				member_types[member_types_size] = convert_type_to_spirv_id(member_type);

				spirv_id member_pointer_type = allocate_index();

				add_to_type_map(member_type, member_pointer_type, false, storage);

				member_types_size += 1;
				kong_assert(member_types_size < 256);
			}

			spirv_id struct_type = write_type_struct(aggregate_types_block, member_types, member_types_size);

			uint32_t offset = 0;
			for (uint32_t j = 0; j < (uint32_t)t->members.size; ++j) {
				type_id member_type = t->members.m[j].type.type;

				uint32_t size = member_size(member_type);
				offset += member_padding(offset, size);
				write_op_member_decorate_value(decorations, struct_type, j, DECORATION_OFFSET, offset);
				offset += size;

				if (member_type == float3x3_id) {
					write_op_member_decorate(decorations, struct_type, j, DECORATION_COL_MAJOR);
					write_op_member_decorate_value(decorations, struct_type, j, DECORATION_MATRIX_STRIDE, 16);
				}
				else if (member_type == float4x4_id) {
					write_op_member_decorate(decorations, struct_type, j, DECORATION_COL_MAJOR);
					write_op_member_decorate_value(decorations, struct_type, j, DECORATION_MATRIX_STRIDE, 16);
				}
			}

			add_to_type_map(g->type, struct_type, false, STORAGE_CLASS_NONE);

			spirv_id struct_pointer_type = allocate_index();

			add_to_type_map(g->type, struct_pointer_type, false, storage);

			spirv_id spirv_var_id = convert_kong_index_to_spirv_id(g->var_index);
			write_op_variable_preallocated(global_vars_block, struct_pointer_type, spirv_var_id, storage);

			write_op_decorate(decorations, struct_type, DECORATION_BLOCK);

			write_op_decorate_value(decorations, spirv_var_id, DECORATION_DESCRIPTOR_SET, 0);
			write_op_decorate_value(decorations, spirv_var_id, DECORATION_BINDING, binding);
		}
	}

	if (main->used_builtins.dispatch_thread_id) {
		write_op_variable_preallocated(global_vars_block, convert_pointer_type_to_spirv_id(uint3_id, STORAGE_CLASS_INPUT), dispatch_thread_id_variable,
		                               STORAGE_CLASS_INPUT);
		write_op_decorate_value(decorations, dispatch_thread_id_variable, DECORATION_BUILTIN, BUILTIN_GLOBAL_INVOCATION_ID);
	}

	if (main->used_builtins.group_thread_id) {
		write_op_variable_preallocated(global_vars_block, convert_pointer_type_to_spirv_id(uint3_id, STORAGE_CLASS_INPUT), group_thread_id_variable,
		                               STORAGE_CLASS_INPUT);
		write_op_decorate_value(decorations, group_thread_id_variable, DECORATION_BUILTIN, BUILTIN_LOCAL_INVOCATION_ID);
	}

	if (main->used_builtins.group_id) {
		write_op_variable_preallocated(global_vars_block, convert_pointer_type_to_spirv_id(uint3_id, STORAGE_CLASS_INPUT), group_id_variable,
		                               STORAGE_CLASS_INPUT);
		write_op_decorate_value(decorations, group_id_variable, DECORATION_BUILTIN, BUILTIN_WORKGROUP_ID);
	}

	if (main->used_builtins.vertex_id) {
		write_op_variable_preallocated(global_vars_block, convert_pointer_type_to_spirv_id(uint_id, STORAGE_CLASS_INPUT), vertex_id_variable,
		                               STORAGE_CLASS_INPUT);
		write_op_decorate_value(decorations, vertex_id_variable, DECORATION_BUILTIN, BUILTIN_VERTEX_INDEX);
	}

	if (stage == SHADER_STAGE_COMPUTE) {
		write_op_decorate_value(decorations, work_group_size_variable, DECORATION_BUILTIN, BUILTIN_WORKGROUP_SIZE);
	}
}

static void init_index_map(void) {
	spirv_id default_id = {0};
	hmdefault(index_map, default_id);
	size_t size = hmlenu(index_map);
	for (size_t i = 0; i < size; ++i) {
		hmdel(index_map, index_map[i].key);
	}
}

static void init_type_map(void) {
	spirv_id default_id = {0};
	hmdefault(type_map, default_id);
	size_t size = hmlenu(type_map);
	for (size_t i = 0; i < size; ++i) {
		hmdel(type_map, type_map[i].key);
	}
}

static void init_function_map(void) {
	spirv_id default_id = {0};
	hmdefault(function_map, default_id);
	size_t size = hmlenu(function_map);
	for (size_t i = 0; i < size; ++i) {
		hmdel(function_map, function_map[i].key);
	}
}

static void init_int_constants(void) {
	hash_map_destroy(int_constants);
	int_constants = hash_map_create();
}

static void init_float_constants(void) {
	spirv_id default_id = {0};
	hmdefault(float_constants, default_id);
	size_t size = hmlenu(float_constants);
	for (size_t i = 0; i < size; ++i) {
		hmdel(float_constants, float_constants[i].key);
	}
}

void init_maps(void) {
	init_index_map();
	init_type_map();
	init_function_map();
	init_int_constants();
	init_float_constants();
}

static char *write_bytecode2(char *buffer, int *size_out, instructions_buffer *header, instructions_buffer *decorations, instructions_buffer *base_types,
                             instructions_buffer *constants, instructions_buffer *aggregate_types, instructions_buffer *global_vars,
                             instructions_buffer *instructions, bool debug) {
	uint8_t *output_header      = (uint8_t *)header->instructions;
	size_t   output_header_size = header->offset * 4;

	uint8_t *output_decorations      = (uint8_t *)decorations->instructions;
	size_t   output_decorations_size = decorations->offset * 4;

	uint8_t *output_base_types      = (uint8_t *)base_types->instructions;
	size_t   output_base_types_size = base_types->offset * 4;

	uint8_t *output_constants      = (uint8_t *)constants->instructions;
	size_t   output_constants_size = constants->offset * 4;

	uint8_t *output_aggregate_types      = (uint8_t *)aggregate_types->instructions;
	size_t   output_aggregate_types_size = aggregate_types->offset * 4;

	uint8_t *output_global_vars      = (uint8_t *)global_vars->instructions;
	size_t   output_global_vars_size = global_vars->offset * 4;

	uint8_t *output_instructions      = (uint8_t *)instructions->instructions;
	size_t   output_instructions_size = instructions->offset * 4;

	{
		int pos = 0;
		memcpy(buffer + pos, output_header, output_header_size);
		pos += output_header_size;
		memcpy(buffer + pos, output_decorations, output_decorations_size);
		pos += output_decorations_size;
		memcpy(buffer + pos, output_base_types, output_base_types_size);
		pos += output_base_types_size;
		memcpy(buffer + pos, output_constants, output_constants_size);
		pos += output_constants_size;
		memcpy(buffer + pos, output_aggregate_types, output_aggregate_types_size);
		pos += output_aggregate_types_size;
		memcpy(buffer + pos, output_global_vars, output_global_vars_size);
		pos += output_global_vars_size;
		memcpy(buffer + pos, output_instructions, output_instructions_size);
		pos += output_instructions_size;

		*size_out = output_header_size + output_decorations_size + output_base_types_size + output_constants_size + output_aggregate_types_size +
		            output_global_vars_size + output_instructions_size;

		return buffer;
	}
}

static char *spirv_export_vertex2(function *main, bool debug, int *size_out) {
	next_index = 1;
	init_maps();

	find_used_builtins(main);
	find_used_capabilities(main);

	instructions_buffer header = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer decorations = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer base_types = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer constants = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer aggregate_types = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer global_vars = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer instructions = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	kong_assert(main->parameters_size > 0);
	type_id vertex_input  = main->parameter_types[0].type;
	type_id vertex_output = main->return_type.type;

	debug_context context = {0};
	check(vertex_input != NO_TYPE, context, "vertex input missing");
	check(vertex_output != NO_TYPE, context, "vertex output missing");

	write_capabilities(&decorations, &main->used_capabilities);
	glsl_import = write_op_ext_inst_import(&decorations, "GLSL.std.450");
	write_op_memory_model(&decorations, ADDRESSING_MODEL_LOGICAL, MEMORY_MODEL_GLSL450);

	type *input  = get_type(vertex_input);
	type *output = get_type(vertex_output);

	spirv_id entry_point = allocate_index();

	input_vars_count = input->members.size;
	for (size_t input_var_index = 0; input_var_index < input_vars_count; ++input_var_index) {
		input_vars[input_var_index] = allocate_index();
	}

	output_vars_count = output->members.size;
	for (size_t output_var_index = 0; output_var_index < output_vars_count; ++output_var_index) {
		output_vars[output_var_index] = allocate_index();
	}
	per_vertex_var = output_vars[0];

	spirv_id interfaces[256];
	size_t   interfaces_count = 0;

	for (size_t input_var_index = 0; input_var_index < input_vars_count; ++input_var_index) {
		interfaces[interfaces_count] = input_vars[input_var_index];
		interfaces_count += 1;
	}

	for (size_t output_var_index = 0; output_var_index < output_vars_count; ++output_var_index) {
		interfaces[interfaces_count] = output_vars[output_var_index];
		interfaces_count += 1;
	}

	if (main->used_builtins.vertex_id) {
		vertex_id_variable           = allocate_index();
		interfaces[interfaces_count] = vertex_id_variable;
		interfaces_count += 1;
	}

	write_op_entry_point(&decorations, EXECUTION_MODEL_VERTEX, entry_point, "main", interfaces, (uint16_t)interfaces_count);

	write_vertex_input_decorations(&decorations, input_vars, (uint32_t)input_vars_count);

	write_base_types(&base_types);

	write_globals(&decorations, &aggregate_types, &global_vars, main, SHADER_STAGE_VERTEX);

	for (size_t i = 0; i < input_vars_count; ++i) {
		member m       = input->members.m[i];
		input_types[i] = m.type.type;

		if (m.type.type == float2_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float2_id, STORAGE_CLASS_INPUT), input_vars[i], STORAGE_CLASS_INPUT);
		}
		else if (m.type.type == float3_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float3_id, STORAGE_CLASS_INPUT), input_vars[i], STORAGE_CLASS_INPUT);
		}
		else if (m.type.type == float4_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float4_id, STORAGE_CLASS_INPUT), input_vars[i], STORAGE_CLASS_INPUT);
		}
		else {
			debug_context context = {0};
			error(context, "Type unsupported for input in SPIR-V");
		}
	}

	spirv_id types[]       = {spirv_float4_type};
	spirv_id output_struct = write_type_struct(&aggregate_types, types, 1);
	write_vertex_output_decorations(&decorations, output_struct, output_vars, (uint32_t)output_vars_count);

	output_struct_pointer_type = write_type_pointer(&aggregate_types, STORAGE_CLASS_OUTPUT, output_struct);
	write_op_variable_preallocated(&instructions, output_struct_pointer_type, per_vertex_var, STORAGE_CLASS_OUTPUT);

	// special handling for the first one (position) via per_vertex_var
	for (size_t i = 1; i < output_vars_count; ++i) {
		member m        = output->members.m[i];
		output_types[i] = m.type.type;

		if (m.type.type == float2_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float2_id, STORAGE_CLASS_OUTPUT), output_vars[i],
			                               STORAGE_CLASS_OUTPUT);
		}
		else if (m.type.type == float3_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float3_id, STORAGE_CLASS_OUTPUT), output_vars[i],
			                               STORAGE_CLASS_OUTPUT);
		}
		else if (m.type.type == float4_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float4_id, STORAGE_CLASS_OUTPUT), output_vars[i],
			                               STORAGE_CLASS_OUTPUT);
		}
		else {
			debug_context context = {0};
			error(context, "Type unsupported for input in SPIR-V");
		}
	}

	write_functions(&instructions, main, entry_point, SHADER_STAGE_VERTEX, vertex_output);

	write_types(&aggregate_types, main);

	// header
	write_magic_number(&header);
	write_version_number(&header);
	write_generator_magic_number(&header);
	write_bound(&header);
	write_instruction_schema(&header);

	write_constants(&constants);

	static char _buffer[1024 * 1024];
	char *result = write_bytecode2(&_buffer[0], size_out, &header, &decorations, &base_types, &constants, &aggregate_types, &global_vars, &instructions, debug);
	free(header.instructions);
	free(decorations.instructions);
	free(base_types.instructions);
	free(constants.instructions);
	free(aggregate_types.instructions);
	free(global_vars.instructions);
	free(instructions.instructions);
	return result;
}

static char *spirv_export_fragment2(function *main, bool debug, int *size_out) {
	next_index = 1;
	init_maps();

	find_used_builtins(main);
	find_used_capabilities(main);

	instructions_buffer header = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer decorations = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer base_types = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer constants = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer aggregate_types = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer global_vars = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	instructions_buffer instructions = {
	    .instructions = (uint32_t *)calloc(1024 * 1024, 1),
	};

	kong_assert(main->parameters_size > 0);
	type_id pixel_input  = main->parameter_types[0].type;
	type_id pixel_output = main->return_type.type;

	debug_context context = {0};
	check(pixel_input != NO_TYPE, context, "fragment input missing");
	check(pixel_output != NO_TYPE, context, "fragment output missing");

	write_capabilities(&decorations, &main->used_capabilities);
	glsl_import = write_op_ext_inst_import(&decorations, "GLSL.std.450");
	write_op_memory_model(&decorations, ADDRESSING_MODEL_LOGICAL, MEMORY_MODEL_GLSL450);

	type *input  = get_type(pixel_input);
	type *output = get_type(pixel_output);

	spirv_id entry_point = allocate_index();

	input_vars_count = input->members.size - 1; // jump over pos input
	for (size_t input_var_index = 0; input_var_index < input_vars_count; ++input_var_index) {
		input_vars[input_var_index] = allocate_index();
	}

	kong_assert(output->built_in); // has to be a float4 or a float4[]

	if (output->array_size > 0) {
		output_vars_count = output->array_size;
		for (size_t output_var_index = 0; output_var_index < output->array_size; ++output_var_index) {
			output_vars[output_var_index] = allocate_index();
		}
	}
	else {
		output_vars_count = 1;
		output_vars[0]    = allocate_index();
	}

	spirv_id interfaces[256];
	size_t   interfaces_count = 0;

	for (size_t input_var_index = 0; input_var_index < input_vars_count; ++input_var_index) {
		interfaces[interfaces_count] = input_vars[input_var_index];
		interfaces_count += 1;
	}

	for (size_t output_var_index = 0; output_var_index < output_vars_count; ++output_var_index) {
		interfaces[interfaces_count] = output_vars[output_var_index];
		interfaces_count += 1;
	}

	write_op_entry_point(&decorations, EXECUTION_MODEL_FRAGMENT, entry_point, "main", interfaces, (uint16_t)interfaces_count);

	write_op_execution_mode(&decorations, entry_point, EXECUTION_MODE_ORIGIN_UPPER_LEFT);

	write_fragment_input_decorations(&decorations, input_vars, (uint32_t)input_vars_count);

	write_base_types(&base_types);

	write_globals(&decorations, &aggregate_types, &global_vars, main, SHADER_STAGE_FRAGMENT);

	for (size_t i = 0; i < input_vars_count; ++i) {
		member m       = input->members.m[i + 1]; // jump over pos input
		input_types[i] = m.type.type;

		if (m.type.type == float2_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float2_id, STORAGE_CLASS_INPUT), input_vars[i], STORAGE_CLASS_INPUT);
		}
		else if (m.type.type == float3_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float3_id, STORAGE_CLASS_INPUT), input_vars[i], STORAGE_CLASS_INPUT);
		}
		else if (m.type.type == float4_id) {
			write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(float4_id, STORAGE_CLASS_INPUT), input_vars[i], STORAGE_CLASS_INPUT);
		}
		else {
			debug_context context = {0};
			error(context, "Type unsupported for input in SPIR-V");
		}
	}

	write_fragment_output_decorations(&decorations, output_vars, (uint32_t)output_vars_count);

	type_id output_type = output->array_size > 0 ? output->base : pixel_output;

	for (size_t i = 0; i < output_vars_count; ++i) {
		write_op_variable_preallocated(&instructions, convert_pointer_type_to_spirv_id(output_type, STORAGE_CLASS_OUTPUT), output_vars[i],
		                               STORAGE_CLASS_OUTPUT);
	}

	write_functions(&instructions, main, entry_point, SHADER_STAGE_FRAGMENT, NO_TYPE);

	write_types(&aggregate_types, main);

	// header
	write_magic_number(&header);
	write_version_number(&header);
	write_generator_magic_number(&header);
	write_bound(&header);
	write_instruction_schema(&header);

	write_constants(&constants);

	static char _buffer[1024 * 1024];
	char *result = write_bytecode2(&_buffer[0], size_out, &header, &decorations, &base_types, &constants, &aggregate_types, &global_vars, &instructions, debug);
	free(header.instructions);
	free(decorations.instructions);
	free(base_types.instructions);
	free(constants.instructions);
	free(aggregate_types.instructions);
	free(global_vars.instructions);
	free(instructions.instructions);
	return result;
}

void spirv_export2(char **vs, char **fs, int *vs_size, int *fs_size, bool debug) {
	function_id vertex_id   = find_vertex_function();
	function_id fragment_id = find_fragment_function();

	debug_context context = {0};
	check(vertex_id != NO_FUNCTION, context, "vert() missing");
	check(fragment_id != NO_FUNCTION, context, "frag() missing");

	*vs = spirv_export_vertex2(get_function(vertex_id), debug, vs_size);
	*fs = spirv_export_fragment2(get_function(fragment_id), debug, fs_size);
}
