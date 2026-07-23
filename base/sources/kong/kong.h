#pragma once

#include "../iron_system.h"
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPCODES_SIZE                         (1024 * 1024 * 2)
#define OP_SIZE(op, opmember)                offsetof(opcode, opmember) + sizeof(op.opmember)
#define NO_FUNCTION                          0xFFFFFFFF
#define NO_NAME                              0
#define MAX_SET_DEFINITIONS                  32
#define MAX_SETS                             256
#define TRANSFORM_FLAG_ONE_COMPONENT_SWIZZLE (1 << 0)
#define TRANSFORM_FLAG_BINARY_UNIFY_LENGTH   (1 << 1)
#define NO_TYPE                              0xFFFFFFFF
#define MAX_MEMBERS                          1024
#define HASH_MAP_SIZE                        256

#define static_array(type, name, max_size) \
	typedef struct name {                  \
		type   values[max_size];           \
		size_t size;                       \
		size_t max;                        \
	} name;

#define static_array_init(array) \
	array.size = 0;              \
	array.max  = sizeof(array.values) / sizeof(array.values[0])

#define static_array_push(array, value)   \
	if (array.size >= array.max) {        \
		debug_context context = {0};      \
		error(context, "Array overflow"); \
	}                                     \
	array.values[array.size++] = value;

#define static_array_push_p(array, value) \
	if (array->size >= array->max) {      \
		debug_context context = {0};      \
		error(context, "Array overflow"); \
	}                                     \
	array->values[array->size++] = value;

typedef enum api_kind {
	API_DEFAULT,
	API_DIRECT3D11,
	API_DIRECT3D12,
	API_METAL,
	API_WEBGPU,
	API_VULKAN
} api_kind;

typedef enum variable_kind {
	VARIABLE_GLOBAL,
	VARIABLE_LOCAL,
	VARIABLE_INTERNAL
} variable_kind;

typedef enum access_kind {
	ACCESS_MEMBER,
	ACCESS_ELEMENT,
	ACCESS_SWIZZLE
} access_kind;

typedef enum shader_stage {
	SHADER_STAGE_VERTEX,
	SHADER_STAGE_FRAGMENT,
	SHADER_STAGE_COMPUTE
} shader_stage;

typedef enum operatorr {
	OPERATOR_EQUALS,
	OPERATOR_NOT_EQUALS,
	OPERATOR_GREATER,
	OPERATOR_GREATER_EQUAL,
	OPERATOR_LESS,
	OPERATOR_LESS_EQUAL,
	OPERATOR_MINUS,
	OPERATOR_PLUS,
	OPERATOR_DIVIDE,
	OPERATOR_MULTIPLY,
	OPERATOR_NOT,
	OPERATOR_OR,
	OPERATOR_BITWISE_XOR,
	OPERATOR_BITWISE_AND,
	OPERATOR_BITWISE_OR,
	OPERATOR_LEFT_SHIFT,
	OPERATOR_RIGHT_SHIFT,
	OPERATOR_AND,
	OPERATOR_MOD,
	OPERATOR_ASSIGN,
	OPERATOR_MINUS_ASSIGN,
	OPERATOR_PLUS_ASSIGN,
	OPERATOR_DIVIDE_ASSIGN,
	OPERATOR_MULTIPLY_ASSIGN
} operatorr;

typedef enum texture_kind {
	TEXTURE_KIND_NONE,
	TEXTURE_KIND_2D,
} texture_kind;

typedef size_t   name_id;
typedef uint32_t type_id;
typedef uint32_t function_id;
typedef uint32_t global_id;

typedef struct unresolved_type_ref {
	name_id  name;
	uint32_t array_size;
} unresolved_type_ref;

typedef struct type_ref {
	type_id             type;
	unresolved_type_ref unresolved;
} type_ref;

typedef struct variable {
	variable_kind kind;
	uint64_t      index;
	type_ref      type;
} variable;

typedef struct swizzle {
	uint32_t indices[4];
	uint32_t size;
} swizzle;

typedef struct kong_access {
	access_kind kind;
	type_id     type;

	union {
		struct {
			name_id name;
		} access_member;

		struct {
			variable index;
		} access_element;

		struct {
			swizzle swizzle;
		} access_swizzle;
	};
} kong_access;

typedef struct token {
	int line, column;

	enum {
		TOKEN_NONE,
		TOKEN_BOOLEAN,
		TOKEN_FLOAT,
		TOKEN_INT,
		TOKEN_IDENTIFIER,
		TOKEN_LEFT_PAREN,
		TOKEN_RIGHT_PAREN,
		TOKEN_LEFT_CURLY,
		TOKEN_RIGHT_CURLY,
		TOKEN_LEFT_SQUARE,
		TOKEN_RIGHT_SQUARE,
		TOKEN_HASH,
		TOKEN_IF,
		TOKEN_ELSE,
		TOKEN_WHILE,
		TOKEN_DO,
		TOKEN_FOR,
		TOKEN_SEMICOLON,
		TOKEN_COLON,
		TOKEN_DOT,
		TOKEN_COMMA,
		TOKEN_OPERATOR,
		TOKEN_IN,
		TOKEN_STRUCT,
		TOKEN_FUNCTION,
		TOKEN_VAR,
		TOKEN_CONST,
		TOKEN_RETURN,
		TOKEN_DISCARD
	} kind;

	union {
		bool      boolean;
		double    number;
		name_id   identifier;
		operatorr op;
	};
} token;

typedef struct tokens {
	token *t;
	size_t current_size;
	size_t max_size;
} tokens;

typedef struct member {
	name_id  name;
	type_ref type;
	token    value;
} member;

typedef struct members {
	member m[MAX_MEMBERS];
	size_t size;
} members;

typedef struct attribute {
	name_id name;
	double  parameters[16];
	uint8_t paramters_count;
} attribute;

typedef struct attributes {
	attribute attributes[64];
	uint8_t   attributes_count;
} attribute_list;

typedef struct type {
	attribute_list attributes;
	name_id        name;
	bool           built_in;
	members        members;
	type_id        base;
	uint32_t       array_size;
	texture_kind   tex_kind;
} type;

typedef struct opcode {
	enum {
		OPCODE_VAR,
		OPCODE_NOT,
		OPCODE_NEGATE,
		OPCODE_STORE_VARIABLE,
		OPCODE_SUB_AND_STORE_VARIABLE,
		OPCODE_ADD_AND_STORE_VARIABLE,
		OPCODE_DIVIDE_AND_STORE_VARIABLE,
		OPCODE_MULTIPLY_AND_STORE_VARIABLE,
		OPCODE_STORE_ACCESS_LIST,
		OPCODE_SUB_AND_STORE_ACCESS_LIST,
		OPCODE_ADD_AND_STORE_ACCESS_LIST,
		OPCODE_DIVIDE_AND_STORE_ACCESS_LIST,
		OPCODE_MULTIPLY_AND_STORE_ACCESS_LIST,
		OPCODE_LOAD_FLOAT_CONSTANT,
		OPCODE_LOAD_INT_CONSTANT,
		OPCODE_LOAD_BOOL_CONSTANT,
		OPCODE_LOAD_ACCESS_LIST,
		OPCODE_RETURN,
		OPCODE_DISCARD,
		OPCODE_CALL,
		OPCODE_MULTIPLY,
		OPCODE_DIVIDE,
		OPCODE_MOD,
		OPCODE_ADD,
		OPCODE_SUB,
		OPCODE_EQUALS,
		OPCODE_NOT_EQUALS,
		OPCODE_GREATER,
		OPCODE_GREATER_EQUAL,
		OPCODE_LESS,
		OPCODE_LESS_EQUAL,
		OPCODE_AND,
		OPCODE_OR,
		OPCODE_BITWISE_XOR,
		OPCODE_BITWISE_AND,
		OPCODE_BITWISE_OR,
		OPCODE_LEFT_SHIFT,
		OPCODE_RIGHT_SHIFT,
		OPCODE_IF,
		OPCODE_WHILE_START,
		OPCODE_WHILE_CONDITION,
		OPCODE_WHILE_END,
		OPCODE_WHILE_BODY,
		OPCODE_BLOCK_START,
		OPCODE_BLOCK_END
	} type;
	uint32_t size;

	union {
		struct {
			variable var;
		} op_var;
		struct {
			variable from;
			variable to;
		} op_negate;
		struct {
			variable from;
			variable to;
		} op_not;
		struct {
			variable from;
			variable to;
		} op_store_var;
		struct {
			variable from;
			variable to;

			kong_access access_list[64];
			uint8_t     access_list_size;
		} op_store_access_list;
		struct {
			float    number;
			variable to;
		} op_load_float_constant;
		struct {
			int      number;
			variable to;
		} op_load_int_constant;
		struct {
			bool     boolean;
			variable to;
		} op_load_bool_constant;
		struct {
			variable from;
			variable to;

			kong_access access_list[64];
			uint8_t     access_list_size;
		} op_load_access_list;
		struct {
			variable var;
		} op_return;
		struct {
			variable var;
			name_id  func;
			variable parameters[64];
			uint8_t  parameters_size;
		} op_call;
		struct {
			variable right;
			variable left;
			variable result;
		} op_binary;
		struct {
			variable condition;
			uint64_t start_id;
			uint64_t end_id;
		} op_if;
		struct {
			uint64_t start_id;
			uint64_t continue_id;
			uint64_t end_id;
		} op_while_start;
		struct {
			uint64_t start_id;
			uint64_t continue_id;
			uint64_t end_id;
		} op_while_end;
		struct {
			variable condition;
			uint64_t end_id;
		} op_while;
		struct {
			uint64_t id;
		} op_block;
		struct {
			uint8_t nothing;
		} op_nothing;
	};
} opcode;

typedef struct opcodes {
	uint8_t *o;
	size_t   size;
} opcodes;

struct statement;

typedef struct builtins {
	bool builtins_analyzed;
	bool dispatch_thread_id;
	bool group_thread_id;
	bool group_id;
	bool vertex_id;
} builtins;

typedef struct capabilities {
	bool capabilities_analyzed;
	bool image_read;
	bool image_write;
} capabilities;

typedef struct function {
	attribute_list    attributes;
	name_id           name;
	type_ref          return_type;
	name_id           parameter_names[256];
	type_ref          parameter_types[256];
	name_id           parameter_attributes[256];
	uint8_t           parameters_size;
	struct statement *block;

	uint32_t descriptor_set_group_index;

	builtins     used_builtins;
	capabilities used_capabilities;

	opcodes code;
} function;

typedef struct render_pipeline {
	function *vertex_shader;
	function *fragment_shader;
} render_pipeline;

typedef struct debug_context {
	const char *filename;
	uint32_t    column;
	uint32_t    line;
} debug_context;

typedef struct global_value {
	enum {
		GLOBAL_VALUE_FLOAT,
		GLOBAL_VALUE_FLOAT2,
		GLOBAL_VALUE_FLOAT3,
		GLOBAL_VALUE_FLOAT4,
		GLOBAL_VALUE_INT,
		GLOBAL_VALUE_INT2,
		GLOBAL_VALUE_INT3,
		GLOBAL_VALUE_INT4,
		GLOBAL_VALUE_UINT,
		GLOBAL_VALUE_UINT2,
		GLOBAL_VALUE_UINT3,
		GLOBAL_VALUE_UINT4,
		GLOBAL_VALUE_BOOL,
		GLOBAL_VALUE_NONE
	} kind;
	union {
		float    floats[4];
		int      ints[4];
		unsigned uints[4];
		bool     b;
	} value;
} global_value;

struct descriptor_set;

typedef struct global {
	name_id                name;
	type_id                type;
	uint64_t               var_index;
	global_value           value;
	attribute_list         attributes;
	struct descriptor_set *sets[64];
	size_t                 sets_count;
	uint32_t               usage;
} global;

typedef struct global_array {
	global_id globals[256];
	bool      readable[256];
	bool      writable[256];
	size_t    size;
} global_array;

struct expression;

typedef struct expressions {
	struct expression *e[256];
	size_t             size;
} expressions;

typedef struct expression {
	enum {
		EXPRESSION_BINARY,
		EXPRESSION_UNARY,
		EXPRESSION_BOOLEAN,
		EXPRESSION_FLOAT,
		EXPRESSION_INT,
		EXPRESSION_VARIABLE,
		EXPRESSION_GROUPING,
		EXPRESSION_CALL,
		EXPRESSION_MEMBER,
		EXPRESSION_ELEMENT,
		EXPRESSION_SWIZZLE
	} kind;

	type_ref type;

	union {
		struct {
			struct expression *left;
			operatorr          op;
			struct expression *right;
		} binary;
		struct {
			operatorr          op;
			struct expression *right;
		} unary;
		bool               boolean;
		double             number;
		name_id            variable;
		uint32_t           index;
		struct expression *grouping;
		struct {
			name_id     func_name;
			expressions parameters;
		} call;
		struct {
			struct expression *of;
			name_id            member_name;
		} member;
		struct {
			struct expression *of;
			struct expression *element_index;
		} element;
		struct {
			struct expression *of;
			struct swizzle     swizz;
		} swizzle;
	};
} expression;

struct statement;

typedef struct statements {
	struct statement *s[256];
	size_t            size;
} statements;

typedef struct local_variable {
	name_id  name;
	type_ref type;
	uint64_t variable_id;
} local_variable;

typedef struct local_variables {
	local_variable v[256];
	size_t         size;
} local_variables;

typedef struct block {
	struct block   *parent;
	local_variables vars;
	statements      statements;
} block;

typedef struct statement {
	enum {
		STATEMENT_EXPRESSION,
		STATEMENT_RETURN_EXPRESSION,
		STATEMENT_DISCARD,
		STATEMENT_IF,
		STATEMENT_WHILE,
		STATEMENT_DO_WHILE,
		STATEMENT_BLOCK,
		STATEMENT_LOCAL_VARIABLE
	} kind;

	union {
		expression *expression;
		struct {
			expression       *test;
			struct statement *if_block;
			expression       *else_tests[64];
			struct statement *else_blocks[64];
			uint16_t          else_size;
		} iffy;
		struct {
			expression       *test;
			struct statement *while_block;
		} whiley;
		block block;
		struct {
			local_variable var;
			expression    *init;
		} local_variable;
	};
} statement;

typedef struct definition {
	enum {
		DEFINITION_FUNCTION,
		DEFINITION_STRUCT,
		DEFINITION_TEX1D,
		DEFINITION_TEX2D,
		DEFINITION_TEX3D,
		DEFINITION_TEXCUBE,
		DEFINITION_TEX1DARRAY,
		DEFINITION_TEX2DARRAY,
		DEFINITION_TEXCUBEARRAY,
		DEFINITION_SAMPLER,
		DEFINITION_CONST_CUSTOM,
		DEFINITION_CONST_BASIC,
		DEFINITION_BVH
	} kind;

	union {
		function_id function;
		global_id   global;
		type_id     type;
	};
} definition;

typedef struct descriptor_set {
	uint32_t     index;
	name_id      name;
	global_array globals;
} descriptor_set;

static_array(render_pipeline, render_pipelines, 256);
static_array(uint32_t, render_pipeline_indices, 256);
typedef render_pipeline_indices render_pipeline_group;
static_array(render_pipeline_group, render_pipeline_groups, 64);
static_array(function *, compute_shaders, 256);
static_array(uint32_t, compute_shader_indices, 256);
static_array(descriptor_set *, descriptor_sets, 256);
typedef descriptor_sets descriptor_set_group;
static_array(descriptor_set_group, descriptor_set_groups, 256);

void                  allocate_globals(void);
void                  compile_function_block(opcodes *code, struct statement *block);
variable              allocate_variable(type_ref type, variable_kind kind);
void                  find_referenced_functions(function *f, function **functions, size_t *functions_size);
void                  find_referenced_types(function *f, type_id *types, size_t *types_size);
void                  find_referenced_globals(function *f, global_array *globals);
void                  find_used_builtins(function *f);
void                  find_used_capabilities(function *f);
descriptor_set_group *get_descriptor_set_group(uint32_t descriptor_set_group_index);
descriptor_set_group *find_descriptor_set_group_for_pipe_type(type *t);
descriptor_set_group *find_descriptor_set_group_for_function(function *f);
void                  analyze(void);
void                  error(debug_context context, const char *message, ...);
void                  error_no_context(const char *message, ...);
void                  error_args(debug_context context, const char *message, va_list args);
void                  error_args_no_context(const char *message, va_list args);
void                  check_function(bool test, debug_context context, const char *message, ...);

#define check(test, context, message, ...) \
	assert(test);                          \
	check_function(test, context, message, ##__VA_ARGS__)

void        check_args(bool test, debug_context context, const char *message, va_list args);
void        functions_init(void);
function_id add_function(name_id name);
function   *get_function(function_id function);
void        globals_init(void);
global_id   add_global(type_id type, attribute_list attributes, name_id name);
global_id   add_global_with_value(type_id type, attribute_list attributes, name_id name, global_value value);
global     *find_global(name_id name);
global     *get_global(global_id id);
void        assign_global_var(global_id id, uint64_t var_index);

struct meta {
	uint8_t c        : 1;
	uint8_t presence : 7;
	uint8_t hashes[7];
};

struct container {
	int key;
};

struct bucket {
	struct meta meta;
	void       *entries[7];
};

struct hash_map {
	struct bucket buckets[HASH_MAP_SIZE];
};

static inline uint64_t hashfn(int key) {
	uint64_t primary   = (uint32_t)key % HASH_MAP_SIZE;
	uint8_t  secondary = (uint32_t)key % HASH_MAP_SIZE;
	return primary | ((uint64_t)secondary << 56);
}

static inline struct bucket *allocate_bucket(void) {
#ifdef _WIN32
	struct bucket *new_bucket = (struct bucket *)_aligned_malloc(sizeof(struct bucket), 64);
#else
	struct bucket *new_bucket = (struct bucket *)aligned_alloc(64, sizeof(struct bucket));
#endif
	assert(new_bucket != NULL);
	memset(new_bucket, 0, sizeof(struct bucket));

	// cache line check
	assert((uint64_t)new_bucket % 64 == 0);
	assert(sizeof(struct bucket) == 64);

	return new_bucket;
}

static inline struct hash_map *hash_map_create(void) {
#ifdef _WIN32
	struct hash_map *map = _aligned_malloc(sizeof(struct hash_map), 64);
#else
	struct hash_map *map = aligned_alloc(64, sizeof(struct hash_map));
#endif
	assert(map != NULL);
	memset(map, 0, sizeof(struct hash_map));

	// cache line check
	assert((uint64_t)map % 64 == 0);

	return map;
}

static inline void hash_map_free_bucket_chain(struct bucket *bucket) {
	for (uint32_t index = 0; index < 6; ++index) {
		if ((bucket->meta.presence >> index) & 1) {
			free(bucket->entries[index]);
		}
	}
	if (bucket->meta.c) {
		struct bucket *overflow = (struct bucket *)bucket->entries[6];
		hash_map_free_bucket_chain(overflow);
#ifdef _WIN32
		_aligned_free(overflow);
#else
		free(overflow);
#endif
	}
	else if ((bucket->meta.presence >> 6) & 1) {
		free(bucket->entries[6]);
	}
}

static inline void hash_map_destroy(struct hash_map *map) {
	if (map == NULL) {
		return;
	}
	for (uint32_t i = 0; i < HASH_MAP_SIZE; ++i) {
		hash_map_free_bucket_chain(&map->buckets[i]);
	}
#ifdef _WIN32
	_aligned_free(map);
#else
	free(map);
#endif
}

static inline void hash_map_add_to_bucket(struct bucket *bucket, struct container *value, uint8_t secondary_hash) {
	for (uint32_t index = 0; index < 7; ++index) {
		if (((bucket->meta.presence >> index) & 1) == 0) {
			bucket->entries[index]     = value;
			bucket->meta.hashes[index] = secondary_hash;

			bucket->meta.presence |= (1 << index);

			return;
		}
	}

	if (bucket->meta.c) {
		hash_map_add_to_bucket((struct bucket *)bucket->entries[6], value, secondary_hash);
	}
	else {
		bucket->meta.c = 1;

		struct container *previous                = (struct container *)bucket->entries[6];
		uint8_t           previous_secondary_hash = bucket->meta.hashes[6];

		struct bucket *new_bucket = allocate_bucket();

		bucket->entries[6] = new_bucket;

		hash_map_add_to_bucket(new_bucket, previous, previous_secondary_hash);
		hash_map_add_to_bucket(new_bucket, value, secondary_hash);
	}
}

static inline void hash_map_add(struct hash_map *map, struct container *value) {
	uint64_t hash_value = hashfn(value->key);
	uint64_t primary    = hash_value & 0xffffffffffffffu;
	uint8_t  secondary  = (uint8_t)(hash_value << 56);

	hash_map_add_to_bucket(&map->buckets[primary], value, secondary);
}

static inline struct container *hash_map_get_from_bucket(struct bucket *bucket, int key, uint8_t secondary_hash) {
	for (uint32_t index = 0; index < 6; ++index) {
		if (((bucket->meta.presence >> index) & 1) != 0 && bucket->meta.hashes[index] == secondary_hash) {
			struct container *value = (struct container *)bucket->entries[index];

			if (value->key == key) {
				return value;
			}
		}
	}

	if (bucket->meta.c) {
		struct bucket *next_bucket = (struct bucket *)bucket->entries[6];
		return hash_map_get_from_bucket(next_bucket, key, secondary_hash);
	}
	else {
		if (((bucket->meta.presence >> 6) & 1) != 0 && bucket->meta.hashes[6] == secondary_hash) {
			struct container *value = (struct container *)bucket->entries[6];

			if (value->key == key) {
				return value;
			}
		}
	}

	return NULL;
}

static inline struct container *hash_map_get(struct hash_map *map, int key) {
	uint64_t hash_value = hashfn(key);
	uint64_t primary    = hash_value & 0xffffffffffffffu;
	uint8_t  secondary  = (uint8_t)(hash_value << 56);

	struct bucket *bucket = &map->buckets[primary];

	return hash_map_get_from_bucket(bucket, key, secondary);
}

static inline void hash_map_iterate_in_bucket(struct bucket *bucket, void (*callback)(struct container *, void *data), void *data) {
	for (uint32_t index = 0; index < 6; ++index) {
		if (((bucket->meta.presence >> index) & 1) != 0) {
			struct container *value = (struct container *)bucket->entries[index];
			callback(value, data);
		}
	}

	if (bucket->meta.c) {
		struct bucket *next_bucket = (struct bucket *)bucket->entries[6];
		hash_map_iterate_in_bucket(next_bucket, callback, data);
	}
	else {
		if (((bucket->meta.presence >> 6) & 1) != 0) {
			struct container *value = (struct container *)bucket->entries[6];
			callback(value, data);
		}
	}
}

static inline void hash_map_iterate(struct hash_map *map, void (*callback)(struct container *, void *data), void *data) {
	for (uint32_t bucket_index = 0; bucket_index < HASH_MAP_SIZE; ++bucket_index) {
		struct bucket *bucket = &map->buckets[bucket_index];
		hash_map_iterate_in_bucket(bucket, callback, data);
	}
}

void            names_init(void);
name_id         add_name(char *name);
char           *get_name(name_id index);
void            parse(const char *filename, tokens *tokens);
descriptor_set *create_set(name_id name);
descriptor_set *get_set(size_t index);
size_t          get_sets_count(void);
void            add_definition_to_set(descriptor_set *set, definition def);
token           tokens_get(tokens *arr, size_t index);
tokens          tokenize(const char *filename, const char *source);
void            transform(uint32_t flags);
void            resolve_types(void);
void            init_type_ref(type_ref *t, name_id name);
bool            has_attribute(attribute_list *attributes, name_id name);
attribute      *find_attribute(attribute_list *attributes, name_id name);
void            types_init(void);
type_id         add_type(name_id name);
type_id         add_full_type(type *t);
type_id         find_type_by_name(name_id name);
type_id         find_type_by_ref(type_ref *t);
type           *get_type(type_id t);

extern type_id void_id;
extern type_id float_id;
extern type_id float2_id;
extern type_id float3_id;
extern type_id float4_id;
extern type_id float2x2_id;
extern type_id float3x2_id;
extern type_id float2x3_id;
extern type_id float4x2_id;
extern type_id float2x4_id;
extern type_id float3x3_id;
extern type_id float4x3_id;
extern type_id float3x4_id;
extern type_id float4x4_id;
extern type_id int_id;
extern type_id int2_id;
extern type_id int3_id;
extern type_id int4_id;
extern type_id uint_id;
extern type_id uint2_id;
extern type_id uint3_id;
extern type_id uint4_id;
extern type_id bool_id;
extern type_id bool2_id;
extern type_id bool3_id;
extern type_id bool4_id;
extern type_id sampler_type_id;
extern type_id ray_type_id;
extern type_id bvh_type_id;

static inline bool is_texture(type_id id) {
	while (id != NO_TYPE) {
		type *t = get_type(id);

		if (t->tex_kind != TEXTURE_KIND_NONE) {
			return true;
		}

		id = t->base;
	}

	return false;
}

static inline bool is_sampler(type_id t) {
	return t == sampler_type_id;
}

bool     is_vector_or_scalar(type_id t);
bool     is_vector(type_id t);
bool     is_matrix(type_id t);
uint32_t vector_size(type_id t);
type_id  vector_base_type(type_id vector_type);
type_id  vector_to_size(type_id vector_type, uint32_t size);
void     indent(char *code, size_t *offset, int indentation);

typedef char *(*type_string_func)(type_id type);
void  cstyle_write_opcode(char *code, size_t *offset, opcode *o, type_string_func type_string, int *indentation);
char *metal_export(char *directory);
