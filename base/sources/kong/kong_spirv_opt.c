
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	OP_UNDEF                   = 1,
	OP_SOURCE                  = 3,
	OP_NAME                    = 5,
	OP_MEMBER_NAME             = 6,
	OP_EXTENSION               = 10,
	OP_EXT_INST_IMPORT         = 11,
	OP_EXT_INST                = 12,
	OP_MEMORY_MODEL            = 14,
	OP_ENTRY_POINT             = 15,
	OP_EXECUTION_MODE          = 16,
	OP_CAPABILITY              = 17,
	OP_TYPE_VOID               = 19,
	OP_TYPE_BOOL               = 20,
	OP_TYPE_INT                = 21,
	OP_TYPE_FLOAT              = 22,
	OP_TYPE_VECTOR             = 23,
	OP_TYPE_MATRIX             = 24,
	OP_TYPE_IMAGE              = 25,
	OP_TYPE_SAMPLER            = 26,
	OP_TYPE_SAMPLED_IMAGE      = 27,
	OP_TYPE_ARRAY              = 28,
	OP_TYPE_RUNTIME_ARRAY      = 29,
	OP_TYPE_STRUCT             = 30,
	OP_TYPE_POINTER            = 32,
	OP_TYPE_FUNCTION           = 33,
	OP_CONSTANT_TRUE           = 41,
	OP_CONSTANT_FALSE          = 42,
	OP_CONSTANT                = 43,
	OP_CONSTANT_COMPOSITE      = 44,
	OP_FUNCTION                = 54,
	OP_FUNCTION_PARAMETER      = 55,
	OP_FUNCTION_END            = 56,
	OP_FUNCTION_CALL           = 57,
	OP_VARIABLE                = 59,
	OP_LOAD                    = 61,
	OP_STORE                   = 62,
	OP_ACCESS_CHAIN            = 65,
	OP_DECORATE                = 71,
	OP_MEMBER_DECORATE         = 72,
	OP_VECTOR_SHUFFLE          = 79,
	OP_COMPOSITE_CONSTRUCT     = 80,
	OP_COMPOSITE_EXTRACT       = 81,
	OP_COMPOSITE_INSERT        = 82,
	OP_COPY_OBJECT             = 83,
	OP_TRANSPOSE               = 84,
	OP_SAMPLED_IMAGE           = 86,
	OP_IMAGE_SAMPLE_IMPLICIT   = 87,
	OP_IMAGE_SAMPLE_EXPLICIT   = 88,
	OP_IMAGE_FETCH             = 95,
	OP_IMAGE_READ              = 98,
	OP_IMAGE_WRITE             = 99,
	OP_IMAGE_QUERY_SIZE_LOD    = 103,
	OP_IMAGE_QUERY_SIZE        = 104,
	OP_CONVERT_F_TO_U          = 109,
	OP_CONVERT_U_TO_F          = 112,
	OP_BITCAST                 = 124,
	OP_S_NEGATE                = 126,
	OP_F_NEGATE                = 127,
	OP_I_ADD                   = 128,
	OP_F_ADD                   = 129,
	OP_F_SUB                   = 131,
	OP_I_MUL                   = 132,
	OP_F_MUL                   = 133,
	OP_F_DIV                   = 136,
	OP_F_REM                   = 140,
	OP_F_MOD                   = 141,
	OP_VECTOR_TIMES_SCALAR     = 142,
	OP_MATRIX_TIMES_SCALAR     = 143,
	OP_VECTOR_TIMES_MATRIX     = 144,
	OP_MATRIX_TIMES_VECTOR     = 145,
	OP_MATRIX_TIMES_MATRIX     = 146,
	OP_DOT                     = 148,
	OP_LOGICAL_EQUAL           = 164,
	OP_LOGICAL_NOT_EQUAL       = 165,
	OP_LOGICAL_OR              = 166,
	OP_LOGICAL_AND             = 167,
	OP_LOGICAL_NOT             = 168,
	OP_SELECT                  = 169,
	OP_I_EQUAL                 = 170,
	OP_I_NOT_EQUAL             = 171,
	OP_F_ORD_EQUAL             = 180,
	OP_F_ORD_NOT_EQUAL         = 182,
	OP_F_UNORD_GREATER_EQUAL   = 191,
	OP_SHIFT_RIGHT_LOGICAL     = 194,
	OP_BITWISE_OR              = 197,
	OP_BITWISE_XOR             = 198,
	OP_BITWISE_AND             = 199,
	OP_NOT                     = 200,
	OP_DPDX                    = 207,
	OP_FWIDTH                  = 209,
	OP_PHI                     = 245,
	OP_LOOP_MERGE              = 246,
	OP_SELECTION_MERGE         = 247,
	OP_LABEL                   = 248,
	OP_BRANCH                  = 249,
	OP_BRANCH_CONDITIONAL      = 250,
	OP_KILL                    = 252,
	OP_RETURN                  = 253,
	OP_RETURN_VALUE            = 254,
	OP_UNREACHABLE             = 255,
	OP_TYPE_RAY_QUERY          = 4472,
	OP_RAY_QUERY_INITIALIZE    = 4473,
	OP_RAY_QUERY_PROCEED       = 4477,
	OP_RAY_QUERY_GET_TYPE      = 4479,
	OP_TYPE_ACCELERATION       = 5341,
	OP_RAY_QUERY_GET_FIRST     = 6018,
	OP_RAY_QUERY_GET_LAST      = 6031,
	STORAGE_UNIFORM_CONSTANT   = 0,
	STORAGE_INPUT              = 1,
	STORAGE_UNIFORM            = 2,
	STORAGE_FUNCTION           = 7,
	STORAGE_PUSH_CONSTANT      = 9,
	GLSL_MODF                  = 35,
	GLSL_FREXP                 = 51,
	GLSL_INTERPOLATE_CENTROID  = 76,
	GLSL_INTERPOLATE_OFFSET    = 78,
	MAX_IDS_PER_INSTRUCTION    = 1024,
	MAX_COMPOSITE_INDICES      = 16,
};

typedef struct inst {
	uint32_t *w;
	uint16_t  n;
	uint16_t  op;
	bool      dead;
	bool      live;
	int       block; // Block index inside its function, -1 for module level instructions
} inst;

typedef struct {
	inst **items;
	int    count;
	int    capacity;
} inst_list;

typedef struct {
	inst     *label;
	inst_list insts; // Everything after the label
	int       succ[2];
	int       succ_count;
	int      *pred;
	int       pred_count;
	int       pred_capacity;
	int       rpo; // Reverse post order index, -1 when unreachable
	int       idom;
	bool      removed;
} block;

typedef struct {
	inst     *header;
	inst_list params;
	block    *blocks;
	int       blocks_count;
	int       blocks_capacity;
	inst     *end;
} func;

typedef struct arena_chunk {
	struct arena_chunk *next;
	size_t              used;
	size_t              capacity;
	uint8_t             data[];
} arena_chunk;

static struct {
	arena_chunk *arena;
	bool         failed;
	uint32_t     bound;
	uint32_t     capacity; // Size of the per id arrays
	inst       **defs;
	uint32_t    *repl;
	bool        *pinned; // Decorated function level ids, kept exactly as they are
	uint32_t     header[5];
	inst_list    globals;
	inst_list    added; // New module level instructions, written after the existing ones
	func        *funcs;
	int          funcs_count;
	int          funcs_capacity;
	uint32_t    *undefs; // type id -> OpUndef id
} m;

static void *opt_alloc(size_t size) {
	size = (size + 7) & ~(size_t)7;
	if (m.arena == NULL || m.arena->used + size > m.arena->capacity) {
		size_t       capacity = size > 64 * 1024 ? size : 64 * 1024;
		arena_chunk *chunk    = malloc(sizeof(arena_chunk) + capacity);
		chunk->next           = m.arena;
		chunk->used           = 0;
		chunk->capacity       = capacity;
		m.arena               = chunk;
	}
	void *p = &m.arena->data[m.arena->used];
	m.arena->used += size;
	return p;
}

static void arena_free(void) {
	while (m.arena != NULL) {
		arena_chunk *next = m.arena->next;
		free(m.arena);
		m.arena = next;
	}
}

static void list_push(inst_list *list, inst *in) {
	if (list->count == list->capacity) {
		int    capacity = list->capacity == 0 ? 16 : list->capacity * 2;
		inst **items    = opt_alloc(capacity * sizeof(inst *));
		if (list->count > 0) {
			memcpy(items, list->items, list->count * sizeof(inst *));
		}
		list->items    = items;
		list->capacity = capacity;
	}
	list->items[list->count++] = in;
}

static void ensure_capacity(uint32_t id) {
	if (id < m.capacity) {
		return;
	}
	uint32_t capacity = m.capacity == 0 ? 1024 : m.capacity;
	while (capacity <= id) {
		capacity *= 2;
	}
	m.defs   = realloc(m.defs, capacity * sizeof(inst *));
	m.repl   = realloc(m.repl, capacity * sizeof(uint32_t));
	m.pinned = realloc(m.pinned, capacity * sizeof(bool));
	m.undefs = realloc(m.undefs, capacity * sizeof(uint32_t));
	memset(&m.defs[m.capacity], 0, (capacity - m.capacity) * sizeof(inst *));
	memset(&m.repl[m.capacity], 0, (capacity - m.capacity) * sizeof(uint32_t));
	memset(&m.pinned[m.capacity], 0, (capacity - m.capacity) * sizeof(bool));
	memset(&m.undefs[m.capacity], 0, (capacity - m.capacity) * sizeof(uint32_t));
	m.capacity = capacity;
}

static uint32_t new_id(void) {
	uint32_t id = m.bound++;
	ensure_capacity(id);
	return id;
}

static inst *new_inst(uint16_t op, int n, int block) {
	inst *in  = opt_alloc(sizeof(inst));
	in->w     = opt_alloc(n * sizeof(uint32_t));
	in->n     = (uint16_t)n;
	in->op    = op;
	in->dead  = false;
	in->live  = false;
	in->block = block;
	in->w[0]  = ((uint32_t)n << 16) | op;
	return in;
}

// Replaces the words of an instruction, keeping its identity and position
static void reshape(inst *in, uint16_t op, int n) {
	uint32_t *w = opt_alloc(n * sizeof(uint32_t));
	memcpy(w, in->w, (n < in->n ? n : in->n) * sizeof(uint32_t));
	in->w    = w;
	in->n    = (uint16_t)n;
	in->op   = op;
	in->w[0] = ((uint32_t)n << 16) | op;
}

// Instruction layout

static bool is_type_op(uint16_t op) {
	return (op >= OP_TYPE_VOID && op <= OP_TYPE_FUNCTION) || op == OP_TYPE_RAY_QUERY || op == OP_TYPE_ACCELERATION;
}

static bool is_ray_query_getter(uint16_t op) {
	return op == OP_RAY_QUERY_PROCEED || op == OP_RAY_QUERY_GET_TYPE || (op >= OP_RAY_QUERY_GET_FIRST && op <= OP_RAY_QUERY_GET_LAST);
}

// Value instructions with a result type and result id whose remaining operands are all ids
static bool is_all_ids_value_op(uint16_t op) {
	switch (op) {
	case OP_UNDEF:
	case OP_CONSTANT_COMPOSITE:
	case OP_FUNCTION_CALL:
	case OP_ACCESS_CHAIN:
	case OP_COMPOSITE_CONSTRUCT:
	case OP_COPY_OBJECT:
	case OP_TRANSPOSE:
	case OP_SAMPLED_IMAGE:
	case OP_IMAGE_QUERY_SIZE_LOD:
	case OP_IMAGE_QUERY_SIZE:
	case OP_BITCAST:
	case OP_DOT:
	case OP_SELECT:
	case OP_NOT:
	case OP_PHI:
		return true;
	default:
		break;
	}
	return (op >= OP_CONVERT_F_TO_U && op <= OP_CONVERT_U_TO_F) || (op >= OP_S_NEGATE && op <= OP_MATRIX_TIMES_MATRIX) ||
	       (op >= OP_LOGICAL_EQUAL && op <= OP_LOGICAL_NOT) || (op >= OP_I_EQUAL && op <= OP_F_UNORD_GREATER_EQUAL) ||
	       (op >= OP_SHIFT_RIGHT_LOGICAL && op <= OP_BITWISE_AND) || (op >= OP_DPDX && op <= OP_FWIDTH) || is_ray_query_getter(op);
}

static bool has_zero_byte(uint32_t word) {
	return (word & 0xff) == 0 || (word & 0xff00) == 0 || (word & 0xff0000) == 0 || (word & 0xff000000) == 0;
}

// Collects pointers to the id operands of an instruction, the result type included and the result id excluded.
// Returns -1 for instructions the optimizer does not model.
static int inst_ids(inst *in, uint32_t **ids) {
	uint32_t *w = in->w;
	int       n = in->n;
	int       c = 0;
#define ID(i)                \
	if ((i) < n) {           \
		ids[c++] = &w[(i)];  \
	}
#define IDS_FROM(s)                         \
	for (int i_ = (s); i_ < n; ++i_) {      \
		ids[c++] = &w[i_];                  \
	}
	switch (in->op) {
	case OP_SOURCE:
	case OP_EXTENSION:
	case OP_EXT_INST_IMPORT:
	case OP_MEMORY_MODEL:
	case OP_CAPABILITY:
	case OP_TYPE_VOID:
	case OP_TYPE_BOOL:
	case OP_TYPE_INT:
	case OP_TYPE_FLOAT:
	case OP_TYPE_SAMPLER:
	case OP_TYPE_RAY_QUERY:
	case OP_TYPE_ACCELERATION:
	case OP_FUNCTION_END:
	case OP_LABEL:
	case OP_KILL:
	case OP_RETURN:
	case OP_UNREACHABLE:
		break;
	case OP_NAME:
	case OP_MEMBER_NAME:
	case OP_DECORATE:
	case OP_MEMBER_DECORATE:
	case OP_EXECUTION_MODE:
	case OP_BRANCH:
	case OP_SELECTION_MERGE:
	case OP_RETURN_VALUE:
		ID(1);
		break;
	case OP_ENTRY_POINT: {
		ID(2);
		int i = 3;
		while (i < n && !has_zero_byte(w[i])) {
			i++;
		}
		IDS_FROM(i + 1);
		break;
	}
	case OP_TYPE_VECTOR:
	case OP_TYPE_MATRIX:
	case OP_TYPE_IMAGE:
	case OP_TYPE_SAMPLED_IMAGE:
	case OP_TYPE_RUNTIME_ARRAY:
		ID(2);
		break;
	case OP_TYPE_ARRAY:
		ID(2);
		ID(3);
		break;
	case OP_TYPE_STRUCT:
	case OP_TYPE_FUNCTION:
		IDS_FROM(2);
		break;
	case OP_TYPE_POINTER:
		ID(3);
		break;
	case OP_CONSTANT:
	case OP_CONSTANT_TRUE:
	case OP_CONSTANT_FALSE:
	case OP_FUNCTION_PARAMETER:
		ID(1);
		break;
	case OP_VARIABLE:
		ID(1);
		ID(4);
		break;
	case OP_FUNCTION:
		ID(1);
		ID(4);
		break;
	case OP_LOAD:
		ID(1);
		ID(3);
		break;
	case OP_STORE:
		ID(1);
		ID(2);
		break;
	case OP_COMPOSITE_EXTRACT:
		ID(1);
		ID(3);
		break;
	case OP_COMPOSITE_INSERT:
	case OP_VECTOR_SHUFFLE:
		ID(1);
		ID(3);
		ID(4);
		break;
	case OP_EXT_INST:
		ID(1);
		ID(3);
		IDS_FROM(5);
		break;
	case OP_IMAGE_SAMPLE_IMPLICIT:
	case OP_IMAGE_SAMPLE_EXPLICIT:
	case OP_IMAGE_FETCH:
	case OP_IMAGE_READ:
		ID(1);
		ID(3);
		ID(4);
		IDS_FROM(6);
		break;
	case OP_IMAGE_WRITE:
		ID(1);
		ID(2);
		ID(3);
		IDS_FROM(5);
		break;
	case OP_BRANCH_CONDITIONAL:
		ID(1);
		ID(2);
		ID(3);
		break;
	case OP_LOOP_MERGE:
		ID(1);
		ID(2);
		break;
	case OP_RAY_QUERY_INITIALIZE:
		IDS_FROM(1);
		break;
	default:
		if (!is_all_ids_value_op(in->op)) {
			return -1;
		}
		ID(1);
		IDS_FROM(3);
		break;
	}
#undef ID
#undef IDS_FROM
	return c;
}

static bool has_result_type(uint16_t op) {
	switch (op) {
	case OP_SOURCE:
	case OP_NAME:
	case OP_MEMBER_NAME:
	case OP_EXTENSION:
	case OP_EXT_INST_IMPORT:
	case OP_MEMORY_MODEL:
	case OP_ENTRY_POINT:
	case OP_EXECUTION_MODE:
	case OP_CAPABILITY:
	case OP_DECORATE:
	case OP_MEMBER_DECORATE:
	case OP_FUNCTION_END:
	case OP_STORE:
	case OP_IMAGE_WRITE:
	case OP_LOOP_MERGE:
	case OP_SELECTION_MERGE:
	case OP_LABEL:
	case OP_BRANCH:
	case OP_BRANCH_CONDITIONAL:
	case OP_KILL:
	case OP_RETURN:
	case OP_RETURN_VALUE:
	case OP_UNREACHABLE:
	case OP_RAY_QUERY_INITIALIZE:
		return false;
	default:
		return !is_type_op(op);
	}
}

static uint32_t result_id(inst *in) {
	if (is_type_op(in->op) || in->op == OP_LABEL || in->op == OP_EXT_INST_IMPORT) {
		return in->n > 1 ? in->w[1] : 0;
	}
	if (has_result_type(in->op)) {
		return in->n > 2 ? in->w[2] : 0;
	}
	return 0;
}

// Id helpers

static inst *def(uint32_t id) {
	return id < m.capacity ? m.defs[id] : NULL;
}

static void set_def(inst *in) {
	uint32_t id = result_id(in);
	if (id != 0) {
		ensure_capacity(id);
		m.defs[id] = in;
	}
}

static uint32_t resolve(uint32_t id) {
	uint32_t root = id;
	while (root < m.capacity && m.repl[root] != 0) {
		root = m.repl[root];
	}
	while (id < m.capacity && m.repl[id] != 0) {
		uint32_t next = m.repl[id];
		m.repl[id]    = root;
		id            = next;
	}
	return root;
}

static void replace(inst *in, uint32_t value) {
	uint32_t id = result_id(in);
	value       = resolve(value);
	if (id != value) {
		m.repl[id] = value;
	}
	in->dead = true;
}

static void resolve_operands(inst *in) {
	uint32_t *ids[MAX_IDS_PER_INSTRUCTION];
	int       count = inst_ids(in, ids);
	for (int i = 0; i < count; ++i) {
		*ids[i] = resolve(*ids[i]);
	}
}

static uint32_t type_of(uint32_t id) {
	inst *d = def(id);
	return d != NULL && has_result_type(d->op) ? d->w[1] : 0;
}

static int vector_size(uint32_t type) {
	inst *t = def(type);
	return t != NULL && t->op == OP_TYPE_VECTOR ? (int)t->w[3] : 0;
}

static uint32_t component_type(uint32_t type) {
	inst *t = def(type);
	return t != NULL && t->op == OP_TYPE_VECTOR ? t->w[2] : 0;
}

static bool is_constant(uint32_t id) {
	inst *d = def(id);
	return d != NULL && (d->op == OP_CONSTANT || d->op == OP_CONSTANT_COMPOSITE || d->op == OP_CONSTANT_TRUE || d->op == OP_CONSTANT_FALSE);
}

static bool constant_int(uint32_t id, uint32_t *value) {
	inst *d = def(id);
	if (d == NULL || d->op != OP_CONSTANT || d->n != 4) {
		return false;
	}
	inst *t = def(d->w[1]);
	if (t == NULL || t->op != OP_TYPE_INT) {
		return false;
	}
	*value = d->w[3];
	return true;
}

static int pointer_storage(uint32_t pointer) {
	inst *t = def(type_of(pointer));
	return t != NULL && t->op == OP_TYPE_POINTER ? (int)t->w[2] : -1;
}

static uint32_t pointee_type(uint32_t pointer_type) {
	inst *t = def(pointer_type);
	return t != NULL && t->op == OP_TYPE_POINTER ? t->w[3] : 0;
}

// Types whose values can live in SSA registers
static bool is_ssa_type(uint32_t type, int depth) {
	inst *t = def(type);
	if (t == NULL || depth > 16) {
		return false;
	}
	switch (t->op) {
	case OP_TYPE_BOOL:
	case OP_TYPE_INT:
	case OP_TYPE_FLOAT:
	case OP_TYPE_VECTOR:
	case OP_TYPE_MATRIX:
		return true;
	case OP_TYPE_ARRAY:
		return is_ssa_type(t->w[2], depth + 1);
	case OP_TYPE_STRUCT:
		for (int i = 2; i < t->n; ++i) {
			if (!is_ssa_type(t->w[i], depth + 1)) {
				return false;
			}
		}
		return true;
	default:
		return false;
	}
}

// Module level values

static inst *find_global(uint16_t op, uint32_t *w, int n) {
	// Values are looked up by everything but their result id
	for (int pass = 0; pass < 2; ++pass) {
		inst_list *list = pass == 0 ? &m.globals : &m.added;
		for (int i = 0; i < list->count; ++i) {
			inst *g = list->items[i];
			if (g->op == op && g->n == n && !g->dead && g->w[1] == w[1] && memcmp(&g->w[3], &w[3], (n - 3) * sizeof(uint32_t)) == 0) {
				return g;
			}
		}
	}
	return NULL;
}

static uint32_t get_constant_composite(uint32_t type, uint32_t *constituents, int count) {
	int       n = 3 + count;
	uint32_t *w = opt_alloc(n * sizeof(uint32_t));
	w[1]        = type;
	memcpy(&w[3], constituents, count * sizeof(uint32_t));
	inst *g = find_global(OP_CONSTANT_COMPOSITE, w, n);
	if (g != NULL) {
		return g->w[2];
	}
	g = new_inst(OP_CONSTANT_COMPOSITE, n, -1);
	memcpy(&g->w[1], &w[1], (n - 1) * sizeof(uint32_t));
	g->w[2] = new_id();
	set_def(g);
	list_push(&m.added, g);
	return g->w[2];
}

static uint32_t get_undef(uint32_t type) {
	ensure_capacity(type);
	if (m.undefs[type] != 0) {
		return m.undefs[type];
	}
	inst *g = new_inst(OP_UNDEF, 3, -1);
	g->w[1] = type;
	g->w[2] = new_id();
	set_def(g);
	list_push(&m.added, g);
	m.undefs[type] = g->w[2];
	return g->w[2];
}

// Parsing

static bool parse(uint32_t *words, size_t count) {
	if (count < 5 || words[0] != 0x07230203) {
		return false;
	}
	memcpy(m.header, words, sizeof(m.header));
	m.bound = words[3];
	ensure_capacity(m.bound);

	func  *f     = NULL;
	block *b     = NULL;
	size_t index = 5;
	while (index < count) {
		uint16_t n  = words[index] >> 16;
		uint16_t op = words[index] & 0xffff;
		if (n == 0 || index + n > count) {
			return false;
		}
		inst *in = new_inst(op, n, -1);
		memcpy(in->w, &words[index], n * sizeof(uint32_t));
		index += n;

		uint32_t *ids[MAX_IDS_PER_INSTRUCTION];
		if (n > MAX_IDS_PER_INSTRUCTION || inst_ids(in, ids) < 0) {
			return false; // Not modelled
		}
		uint32_t result = result_id(in);
		if (result >= m.bound) {
			return false;
		}

		if (op == OP_FUNCTION) {
			if (f != NULL) {
				return false;
			}
			if (m.funcs_count == m.funcs_capacity) {
				m.funcs_capacity = m.funcs_capacity == 0 ? 8 : m.funcs_capacity * 2;
				m.funcs          = realloc(m.funcs, m.funcs_capacity * sizeof(func));
			}
			f = &m.funcs[m.funcs_count++];
			memset(f, 0, sizeof(func));
			f->header = in;
		}
		else if (f == NULL) {
			list_push(&m.globals, in);
		}
		else if (op == OP_FUNCTION_PARAMETER) {
			list_push(&f->params, in);
		}
		else if (op == OP_LABEL) {
			if (f->blocks_count == f->blocks_capacity) {
				f->blocks_capacity = f->blocks_capacity == 0 ? 16 : f->blocks_capacity * 2;
				f->blocks          = realloc(f->blocks, f->blocks_capacity * sizeof(block));
			}
			b = &f->blocks[f->blocks_count++];
			memset(b, 0, sizeof(block));
			b->label = in;
			in->block = f->blocks_count - 1;
		}
		else if (op == OP_FUNCTION_END) {
			f->end = in;
			f      = NULL;
			b      = NULL;
		}
		else {
			if (b == NULL) {
				return false;
			}
			in->block = (int)(b - f->blocks);
			list_push(&b->insts, in);
		}
		set_def(in);
	}
	if (f != NULL) {
		return false;
	}

	// Decorated function level values (NonUniform) are kept untouched
	for (int i = 0; i < m.globals.count; ++i) {
		inst *g = m.globals.items[i];
		if (g->op == OP_DECORATE) {
			inst *target = def(g->w[1]);
			if (target != NULL && target->block >= 0) {
				m.pinned[g->w[1]] = true;
			}
		}
	}
	return true;
}

// Control flow

static inst *terminator(block *b) {
	return b->insts.count > 0 ? b->insts.items[b->insts.count - 1] : NULL;
}

static inst *merge_inst(block *b) {
	if (b->insts.count < 2) {
		return NULL;
	}
	inst *in = b->insts.items[b->insts.count - 2];
	return in->op == OP_SELECTION_MERGE || in->op == OP_LOOP_MERGE ? in : NULL;
}

static int block_of_label(uint32_t label) {
	inst *d = def(label);
	return d != NULL && d->op == OP_LABEL ? d->block : -1;
}

static void add_pred(block *b, int pred) {
	for (int i = 0; i < b->pred_count; ++i) {
		if (b->pred[i] == pred) {
			return;
		}
	}
	if (b->pred_count == b->pred_capacity) {
		int  capacity = b->pred_capacity == 0 ? 4 : b->pred_capacity * 2;
		int *preds    = opt_alloc(capacity * sizeof(int));
		if (b->pred_count > 0) {
			memcpy(preds, b->pred, b->pred_count * sizeof(int));
		}
		b->pred          = preds;
		b->pred_capacity = capacity;
	}
	b->pred[b->pred_count++] = pred;
}

static bool build_cfg(func *f) {
	for (int i = 0; i < f->blocks_count; ++i) {
		block *b      = &f->blocks[i];
		b->succ_count = 0;
		b->pred_count = 0;
		b->rpo        = -1;
		b->idom       = -1;
	}
	for (int i = 0; i < f->blocks_count; ++i) {
		block *b = &f->blocks[i];
		if (b->removed) {
			continue;
		}
		inst *t = terminator(b);
		if (t == NULL) {
			return false;
		}
		if (t->op == OP_BRANCH) {
			b->succ[b->succ_count++] = block_of_label(t->w[1]);
		}
		else if (t->op == OP_BRANCH_CONDITIONAL) {
			b->succ[b->succ_count++] = block_of_label(t->w[2]);
			if (t->w[3] != t->w[2]) {
				b->succ[b->succ_count++] = block_of_label(t->w[3]);
			}
		}
		else if (t->op != OP_RETURN && t->op != OP_RETURN_VALUE && t->op != OP_KILL && t->op != OP_UNREACHABLE) {
			return false;
		}
		for (int s = 0; s < b->succ_count; ++s) {
			if (b->succ[s] < 0 || f->blocks[b->succ[s]].removed) {
				return false;
			}
			add_pred(&f->blocks[b->succ[s]], i);
		}
	}

	// Reverse post order of the reachable blocks
	int *stack   = malloc(f->blocks_count * sizeof(int));
	int *next    = calloc(f->blocks_count, sizeof(int));
	int *order   = malloc(f->blocks_count * sizeof(int));
	bool *seen   = calloc(f->blocks_count, sizeof(bool));
	int  top     = 0;
	int  ordered = 0;
	stack[top++] = 0;
	seen[0]      = true;
	while (top > 0) {
		int    current = stack[top - 1];
		block *b       = &f->blocks[current];
		if (next[current] < b->succ_count) {
			int s = b->succ[next[current]++];
			if (!seen[s]) {
				seen[s]      = true;
				stack[top++] = s;
			}
		}
		else {
			order[ordered++] = current;
			top--;
		}
	}
	for (int i = 0; i < ordered; ++i) {
		f->blocks[order[ordered - 1 - i]].rpo = i;
	}
	free(stack);
	free(next);
	free(order);
	free(seen);
	return true;
}

// Unreachable blocks are dropped, merge and continue targets have to stay and become stubs
static bool remove_unreachable(func *f) {
	if (!build_cfg(f)) {
		return false;
	}
	bool changed = false;
	for (int i = 0; i < f->blocks_count; ++i) {
		block *b = &f->blocks[i];
		if (b->removed || b->rpo >= 0) {
			continue;
		}
		int continue_of = -1;
		int merge_of    = -1;
		for (int j = 0; j < f->blocks_count; ++j) {
			block *h = &f->blocks[j];
			if (h->removed || h->rpo < 0) {
				continue;
			}
			inst *merge = merge_inst(h);
			if (merge == NULL) {
				continue;
			}
			if (merge->op == OP_LOOP_MERGE && merge->w[2] == b->label->w[1]) {
				continue_of = j;
			}
			if (merge->w[1] == b->label->w[1]) {
				merge_of = j;
			}
		}
		for (int k = 0; k < b->insts.count; ++k) {
			b->insts.items[k]->dead = true;
		}
		b->insts.count = 0;
		if (continue_of >= 0) {
			inst *branch = new_inst(OP_BRANCH, 2, i);
			branch->w[1] = f->blocks[continue_of].label->w[1];
			list_push(&b->insts, branch);
		}
		else if (merge_of >= 0) {
			list_push(&b->insts, new_inst(OP_UNREACHABLE, 1, i));
		}
		else {
			b->removed = true;
		}
		changed = true;
	}
	return !changed || build_cfg(f);
}

static int intersect(func *f, int a, int b) {
	while (a != b) {
		while (f->blocks[a].rpo > f->blocks[b].rpo) {
			a = f->blocks[a].idom;
		}
		while (f->blocks[b].rpo > f->blocks[a].rpo) {
			b = f->blocks[b].idom;
		}
	}
	return a;
}

static int *rpo_blocks(func *f, int *count) {
	int *order = malloc(f->blocks_count * sizeof(int));
	*count     = 0;
	for (int i = 0; i < f->blocks_count; ++i) {
		if (!f->blocks[i].removed && f->blocks[i].rpo >= 0) {
			order[f->blocks[i].rpo] = i;
			(*count)++;
		}
	}
	return order;
}

static void build_dominators(func *f) {
	int  count;
	int *order        = rpo_blocks(f, &count);
	f->blocks[0].idom = 0;
	bool changed      = true;
	while (changed) {
		changed = false;
		for (int r = 1; r < count; ++r) {
			block *b        = &f->blocks[order[r]];
			int    new_idom = -1;
			for (int p = 0; p < b->pred_count; ++p) {
				int pred = b->pred[p];
				if (f->blocks[pred].rpo < 0 || f->blocks[pred].idom < 0) {
					continue;
				}
				new_idom = new_idom < 0 ? pred : intersect(f, pred, new_idom);
			}
			if (new_idom != b->idom) {
				b->idom = new_idom;
				changed = true;
			}
		}
	}
	free(order);
}

// Promotion of function variables to SSA values

typedef struct {
	func     *f;
	int       vars_count;
	uint32_t *var_ids;
	uint32_t *var_types;  // Pointee types
	uint32_t *local_last; // [var * blocks + block], last value stored in the block
	uint32_t *entry;      // [var * blocks + block], value on block entry
	inst_list *phis;      // Per block
} ssa_state;

static uint32_t entry_value(ssa_state *s, int var, int b);

static uint32_t end_value(ssa_state *s, int var, int b) {
	uint32_t last = s->local_last[var * s->f->blocks_count + b];
	return last != 0 ? last : entry_value(s, var, b);
}

static uint32_t entry_value(ssa_state *s, int var, int b) {
	uint32_t *memo = &s->entry[var * s->f->blocks_count + b];
	if (*memo != 0) {
		return *memo;
	}
	block *blk = &s->f->blocks[b];
	if (b == 0 || blk->pred_count == 0) {
		*memo = get_undef(s->var_types[var]);
	}
	else if (blk->pred_count == 1) {
		// A cycle always passes through a block with several predecessors, which memoizes before recursing
		uint32_t value = end_value(s, var, blk->pred[0]);
		*memo          = value;
	}
	else {
		inst *phi = new_inst(OP_PHI, 3 + 2 * blk->pred_count, b);
		phi->w[1] = s->var_types[var];
		phi->w[2] = new_id();
		set_def(phi);
		list_push(&s->phis[b], phi);
		*memo = phi->w[2];
		for (int p = 0; p < blk->pred_count; ++p) {
			uint32_t value       = end_value(s, var, blk->pred[p]);
			phi->w[3 + 2 * p]    = value;
			phi->w[3 + 2 * p + 1] = s->f->blocks[blk->pred[p]].label->w[1];
		}
	}
	return *memo;
}

static void promote_variables(func *f) {
	block *entry = &f->blocks[0];

	// Candidates: plain function variables in the entry block
	int      *var_index  = NULL; // id -> candidate index, built lazily
	uint32_t  capacity   = m.capacity;
	var_index            = malloc(capacity * sizeof(int));
	for (uint32_t i = 0; i < capacity; ++i) {
		var_index[i] = -1;
	}
	int       vars_count = 0;
	uint32_t *var_ids    = malloc((entry->insts.count + 1) * sizeof(uint32_t));
	bool     *bad        = calloc(entry->insts.count + 1, sizeof(bool));
	for (int i = 0; i < entry->insts.count; ++i) {
		inst *in = entry->insts.items[i];
		if (in->op != OP_VARIABLE || in->n != 4 || in->w[3] != STORAGE_FUNCTION || m.pinned[in->w[2]] || !is_ssa_type(pointee_type(in->w[1]), 0)) {
			continue;
		}
		var_index[in->w[2]]   = vars_count;
		var_ids[vars_count++] = in->w[2];
	}
	if (vars_count == 0) {
		free(var_index);
		free(var_ids);
		free(bad);
		return;
	}

	// Constant index access chains into candidates, -1 when not one
	int *chain_var = malloc(capacity * sizeof(int));
	for (uint32_t i = 0; i < capacity; ++i) {
		chain_var[i] = -1;
	}
	for (int bi = 0; bi < f->blocks_count; ++bi) {
		block *b = &f->blocks[bi];
		for (int i = 0; i < b->insts.count; ++i) {
			inst *in = b->insts.items[i];
			if (in->dead || in->op != OP_ACCESS_CHAIN || in->w[3] >= capacity || var_index[in->w[3]] < 0) {
				continue;
			}
			bool constant = in->n - 4 <= MAX_COMPOSITE_INDICES;
			for (int k = 4; k < in->n && constant; ++k) {
				uint32_t value;
				constant = constant_int(in->w[k], &value);
			}
			if (constant && !m.pinned[in->w[2]]) {
				chain_var[in->w[2]] = var_index[in->w[3]];
			}
			else {
				bad[var_index[in->w[3]]] = true;
			}
		}
	}

	// Every use has to be a load, a store target or a constant access chain
	for (int bi = 0; bi < f->blocks_count; ++bi) {
		block *b = &f->blocks[bi];
		for (int i = 0; i < b->insts.count; ++i) {
			inst     *in = b->insts.items[i];
			uint32_t *ids[MAX_IDS_PER_INSTRUCTION];
			int       count = in->dead ? 0 : inst_ids(in, ids);
			for (int k = 0; k < count; ++k) {
				uint32_t id = *ids[k];
				if (id >= capacity) {
					continue;
				}
				int  position = (int)(ids[k] - in->w);
				bool pointer  = (in->op == OP_LOAD && position == 3 && in->n == 4) || (in->op == OP_STORE && position == 1 && in->n == 3);
				if (var_index[id] >= 0) {
					if (!pointer && !(in->op == OP_ACCESS_CHAIN && position == 3 && chain_var[in->w[2]] >= 0)) {
						bad[var_index[id]] = true;
					}
				}
				else if (chain_var[id] >= 0 && !pointer) {
					bad[chain_var[id]] = true;
				}
			}
		}
	}

	// Rewrite chain accesses into whole variable loads and stores with composite extract/insert
	for (int bi = 0; bi < f->blocks_count; ++bi) {
		block    *b      = &f->blocks[bi];
		inst_list result = {0};
		for (int i = 0; i < b->insts.count; ++i) {
			inst    *in    = b->insts.items[i];
			uint32_t ptr   = in->op == OP_LOAD ? in->w[3] : in->op == OP_STORE ? in->w[1] : 0;
			inst    *chain = ptr != 0 && ptr < capacity && chain_var[ptr] >= 0 && !bad[chain_var[ptr]] ? def(ptr) : NULL;
			if (in->dead || chain == NULL) {
				if (!(in->op == OP_ACCESS_CHAIN && in->w[2] < capacity && chain_var[in->w[2]] >= 0 && !bad[chain_var[in->w[2]]])) {
					list_push(&result, in);
				}
				continue;
			}
			uint32_t var          = chain->w[3];
			uint32_t var_type     = pointee_type(type_of(var));
			int      index_count  = chain->n - 4;
			inst    *whole        = new_inst(OP_LOAD, 4, bi);
			whole->w[1]           = var_type;
			whole->w[2]           = new_id();
			whole->w[3]           = var;
			set_def(whole);
			list_push(&result, whole);
			if (in->op == OP_LOAD) {
				reshape(in, OP_COMPOSITE_EXTRACT, 4 + index_count);
				in->w[3] = whole->w[2];
				for (int k = 0; k < index_count; ++k) {
					constant_int(chain->w[4 + k], &in->w[4 + k]);
				}
				list_push(&result, in);
			}
			else {
				inst *insert = new_inst(OP_COMPOSITE_INSERT, 5 + index_count, bi);
				insert->w[1] = var_type;
				insert->w[2] = new_id();
				insert->w[3] = in->w[2];
				insert->w[4] = whole->w[2];
				for (int k = 0; k < index_count; ++k) {
					constant_int(chain->w[4 + k], &insert->w[5 + k]);
				}
				set_def(insert);
				list_push(&result, insert);
				in->w[1] = var;
				in->w[2] = insert->w[2];
				list_push(&result, in);
			}
		}
		b->insts = result;
	}
	for (int bi = 0; bi < f->blocks_count; ++bi) {
		block *b = &f->blocks[bi];
		for (int i = 0; i < b->insts.count; ++i) {
			inst *in = b->insts.items[i];
			if (in->op == OP_ACCESS_CHAIN && in->w[2] < capacity && chain_var[in->w[2]] >= 0 && !bad[chain_var[in->w[2]]]) {
				in->dead = true;
			}
		}
	}

	// Compact the candidate list to the promotable variables
	int *promoted = malloc(vars_count * sizeof(int));
	int  count    = 0;
	for (int k = 0; k < vars_count; ++k) {
		if (bad[k]) {
			var_index[var_ids[k]] = -1;
		}
		else {
			var_index[var_ids[k]] = count;
			promoted[count++]     = k;
		}
	}
	if (count == 0) {
		free(var_index);
		free(var_ids);
		free(bad);
		free(chain_var);
		free(promoted);
		return;
	}

	ssa_state s  = {0};
	s.f          = f;
	s.vars_count = count;
	s.var_ids    = malloc(count * sizeof(uint32_t));
	s.var_types  = malloc(count * sizeof(uint32_t));
	for (int k = 0; k < count; ++k) {
		s.var_ids[k]   = var_ids[promoted[k]];
		s.var_types[k] = pointee_type(type_of(s.var_ids[k]));
	}
	s.local_last = calloc((size_t)count * f->blocks_count, sizeof(uint32_t));
	s.entry      = calloc((size_t)count * f->blocks_count, sizeof(uint32_t));
	s.phis       = calloc(f->blocks_count, sizeof(inst_list));

	for (int bi = 0; bi < f->blocks_count; ++bi) {
		block *b = &f->blocks[bi];
		for (int i = 0; i < b->insts.count; ++i) {
			inst *in = b->insts.items[i];
			if (!in->dead && in->op == OP_STORE && in->w[1] < capacity && var_index[in->w[1]] >= 0) {
				s.local_last[var_index[in->w[1]] * f->blocks_count + bi] = in->w[2];
			}
		}
	}

	uint32_t *current = malloc(count * sizeof(uint32_t));
	for (int bi = 0; bi < f->blocks_count; ++bi) {
		block *b = &f->blocks[bi];
		if (b->removed) {
			continue;
		}
		memset(current, 0, count * sizeof(uint32_t));
		for (int i = 0; i < b->insts.count; ++i) {
			inst *in = b->insts.items[i];
			if (in->dead) {
				continue;
			}
			if (in->op == OP_LOAD && in->w[3] < capacity && var_index[in->w[3]] >= 0) {
				int var = var_index[in->w[3]];
				if (current[var] == 0) {
					current[var] = entry_value(&s, var, bi);
				}
				replace(in, current[var]);
			}
			else if (in->op == OP_STORE && in->w[1] < capacity && var_index[in->w[1]] >= 0) {
				current[var_index[in->w[1]]] = in->w[2];
				in->dead                     = true;
			}
			else if (in->op == OP_VARIABLE && in->w[2] < capacity && var_index[in->w[2]] >= 0) {
				in->dead = true;
			}
		}
	}

	// Phis go first in their blocks
	for (int bi = 0; bi < f->blocks_count; ++bi) {
		if (s.phis[bi].count == 0) {
			continue;
		}
		block    *b      = &f->blocks[bi];
		inst_list merged = {0};
		for (int i = 0; i < s.phis[bi].count; ++i) {
			list_push(&merged, s.phis[bi].items[i]);
		}
		for (int i = 0; i < b->insts.count; ++i) {
			list_push(&merged, b->insts.items[i]);
		}
		b->insts = merged;
	}

	free(current);
	free(s.var_ids);
	free(s.var_types);
	free(s.local_last);
	free(s.entry);
	free(s.phis);
	free(var_index);
	free(var_ids);
	free(bad);
	free(chain_var);
	free(promoted);
}

// Phis merging a single value are replaced by it
static void remove_trivial_phis(func *f) {
	bool changed = true;
	while (changed) {
		changed = false;
		for (int bi = 0; bi < f->blocks_count; ++bi) {
			block *b = &f->blocks[bi];
			for (int i = 0; i < b->insts.count; ++i) {
				inst *in = b->insts.items[i];
				if (in->op != OP_PHI || in->dead || m.pinned[in->w[2]]) {
					continue;
				}
				uint32_t same    = 0;
				bool     trivial = true;
				for (int k = 3; k < in->n; k += 2) {
					uint32_t value = resolve(in->w[k]);
					if (value == in->w[2] || value == same) {
						continue;
					}
					if (same != 0) {
						trivial = false;
						break;
					}
					same = value;
				}
				if (trivial) {
					replace(in, same != 0 ? same : get_undef(in->w[1]));
					changed = true;
				}
			}
		}
	}
}

// Folding

static bool is_splat(uint32_t id, uint32_t *scalar) {
	inst *d = def(id);
	if (d == NULL || (d->op != OP_COMPOSITE_CONSTRUCT && d->op != OP_CONSTANT_COMPOSITE) || d->n < 5) {
		return false;
	}
	int size = vector_size(d->w[1]);
	if (size == 0 || d->n - 3 != size) {
		return false;
	}
	for (int k = 4; k < d->n; ++k) {
		if (d->w[k] != d->w[3]) {
			return false;
		}
	}
	*scalar = d->w[3];
	return true;
}

static void fold_extract(inst *in) {
	uint32_t composite = in->w[3];
	int      count     = in->n - 4;
	uint32_t indices[MAX_COMPOSITE_INDICES];
	if (count < 1 || count > MAX_COMPOSITE_INDICES) {
		return;
	}
	memcpy(indices, &in->w[4], count * sizeof(uint32_t));
	bool changed = false;

	for (int steps = 0; steps < 256 && count > 0; ++steps) {
		inst *d = def(composite);
		if (d == NULL || m.pinned[composite]) {
			break;
		}
		if (d->op == OP_COMPOSITE_INSERT) {
			int       inserted_count = d->n - 5;
			uint32_t *inserted       = &d->w[5];
			int       common         = inserted_count < count ? inserted_count : count;
			bool      prefix         = memcmp(inserted, indices, common * sizeof(uint32_t)) == 0;
			if (prefix && inserted_count == count) {
				composite = d->w[3];
				count     = 0;
			}
			else if (prefix && inserted_count < count) {
				composite = d->w[3];
				memmove(indices, &indices[inserted_count], (count - inserted_count) * sizeof(uint32_t));
				count -= inserted_count;
			}
			else if (prefix) {
				break; // Reads a partially overwritten composite
			}
			else {
				composite = d->w[4];
			}
			changed = true;
		}
		else if (d->op == OP_COMPOSITE_CONSTRUCT || d->op == OP_CONSTANT_COMPOSITE) {
			uint32_t type = d->w[1];
			if (vector_size(type) > 0) {
				// Vectors can be constructed from smaller vectors
				uint32_t position = 0;
				bool     found    = false;
				for (int k = 3; k < d->n; ++k) {
					uint32_t size = (uint32_t)vector_size(type_of(d->w[k]));
					size          = size == 0 ? 1 : size;
					if (indices[0] < position + size) {
						composite  = d->w[k];
						indices[0] = indices[0] - position;
						if (size == 1) {
							count = 0;
						}
						found = true;
						break;
					}
					position += size;
				}
				if (!found) {
					break;
				}
			}
			else {
				if (3 + indices[0] >= d->n) {
					break;
				}
				composite = d->w[3 + indices[0]];
				memmove(indices, &indices[1], (count - 1) * sizeof(uint32_t));
				count--;
			}
			changed = true;
		}
		else if (d->op == OP_VECTOR_SHUFFLE && count == 1) {
			uint32_t selected = indices[0] + 5 < d->n ? d->w[5 + indices[0]] : 0xffffffff;
			if (selected == 0xffffffff) {
				break;
			}
			uint32_t first_size = (uint32_t)vector_size(type_of(d->w[3]));
			if (selected < first_size) {
				composite  = d->w[3];
				indices[0] = selected;
			}
			else {
				composite  = d->w[4];
				indices[0] = selected - first_size;
			}
			changed = true;
		}
		else {
			break;
		}
	}

	if (count == 0) {
		replace(in, composite);
	}
	else if (changed) {
		reshape(in, OP_COMPOSITE_EXTRACT, 4 + count);
		in->w[3] = composite;
		memcpy(&in->w[4], indices, count * sizeof(uint32_t));
	}
}

// Scalar extracts gathered back into a vector become a shuffle, or the source itself
static bool fold_construct_to_shuffle(inst *in, uint32_t *values, int size) {
	uint32_t sources[2]  = {0, 0};
	int      source_size = 0;
	uint32_t components[4];
	if (size > 4) {
		return false;
	}
	for (int k = 0; k < size; ++k) {
		inst *e = def(values[k]);
		if (e == NULL || e->op != OP_COMPOSITE_EXTRACT || e->n != 5 || m.pinned[values[k]]) {
			return false;
		}
		uint32_t source      = e->w[3];
		uint32_t source_type = type_of(source);
		if (vector_size(source_type) == 0 || component_type(source_type) != component_type(in->w[1])) {
			return false;
		}
		int slot = source == sources[0] ? 0 : source == sources[1] ? 1 : -1;
		if (slot < 0) {
			if (source_size == 2) {
				return false;
			}
			slot                   = source_size;
			sources[source_size++] = source;
		}
		components[k] = slot == 0 ? e->w[4] : (uint32_t)vector_size(type_of(sources[0])) + e->w[4];
	}
	if (source_size == 1 && type_of(sources[0]) == in->w[1]) {
		bool identity = true;
		for (int k = 0; k < size; ++k) {
			identity = identity && components[k] == (uint32_t)k;
		}
		if (identity) {
			replace(in, sources[0]);
			return true;
		}
	}
	reshape(in, OP_VECTOR_SHUFFLE, 5 + size);
	in->w[3] = sources[0];
	in->w[4] = source_size == 2 ? sources[1] : sources[0];
	memcpy(&in->w[5], components, size * sizeof(uint32_t));
	return true;
}

static void fold_construct(inst *in) {
	int count = in->n - 3;
	if (count < 1) {
		return;
	}
	bool all_constant = true;
	for (int k = 3; k < in->n && all_constant; ++k) {
		all_constant = is_constant(in->w[k]);
	}
	if (all_constant) {
		int size = vector_size(in->w[1]);
		if (size == 0) {
			replace(in, get_constant_composite(in->w[1], &in->w[3], count));
			return;
		}
		// Constant vectors list one scalar per component
		uint32_t scalars[4];
		int      scalars_count = 0;
		for (int k = 3; k < in->n; ++k) {
			inst *c = def(in->w[k]);
			if (c->op == OP_CONSTANT_COMPOSITE) {
				for (int j = 3; j < c->n && scalars_count < 4; ++j) {
					scalars[scalars_count++] = c->w[j];
				}
			}
			else if (scalars_count < 4) {
				scalars[scalars_count++] = in->w[k];
			}
		}
		if (scalars_count == size) {
			replace(in, get_constant_composite(in->w[1], scalars, size));
		}
		return;
	}
	if (vector_size(in->w[1]) == count) {
		uint32_t values[4];
		if (count <= 4) {
			memcpy(values, &in->w[3], count * sizeof(uint32_t));
			fold_construct_to_shuffle(in, values, count);
		}
	}
}

// Single component inserts into a vector become a construct when they cover it or a shuffle when they come from one vector
static void fold_insert(inst *in) {
	uint32_t type = in->w[1];
	int      size = vector_size(type);
	if (size == 0 || size > 4 || in->n != 6) {
		return;
	}
	// A component taken from one of the vectors of the shuffle it is inserted into folds into that shuffle
	inst *shuffle = def(in->w[4]);
	inst *object  = def(in->w[3]);
	if (shuffle != NULL && shuffle->op == OP_VECTOR_SHUFFLE && shuffle->w[1] == type && !m.pinned[in->w[4]] && object != NULL && object->op == OP_COMPOSITE_EXTRACT &&
	    object->n == 5 && (object->w[3] == shuffle->w[3] || object->w[3] == shuffle->w[4]) && in->w[5] < (uint32_t)size) {
		uint32_t components[4];
		memcpy(components, &shuffle->w[5], size * sizeof(uint32_t));
		components[in->w[5]] = object->w[3] == shuffle->w[3] ? object->w[4] : (uint32_t)vector_size(type_of(shuffle->w[3])) + object->w[4];
		uint32_t first       = shuffle->w[3];
		uint32_t second      = shuffle->w[4];
		reshape(in, OP_VECTOR_SHUFFLE, 5 + size);
		in->w[3] = first;
		in->w[4] = second;
		memcpy(&in->w[5], components, size * sizeof(uint32_t));
		return;
	}

	uint32_t values[4] = {0, 0, 0, 0};
	int      covered   = 0;
	int      inserts   = 0;
	uint32_t base      = in->w[2];
	inst    *current   = in;
	while (current != NULL && current->op == OP_COMPOSITE_INSERT && current->n == 6 && current->w[1] == type && current->w[5] < (uint32_t)size &&
	       (current == in || !m.pinned[current->w[2]])) {
		uint32_t component = current->w[5];
		if (values[component] == 0) {
			values[component] = current->w[3];
			covered++;
		}
		inserts++;
		base    = current->w[4];
		current = def(base);
	}
	if (covered == size) {
		reshape(in, OP_COMPOSITE_CONSTRUCT, 3 + size);
		memcpy(&in->w[3], values, size * sizeof(uint32_t));
		fold_construct(in);
		return;
	}
	if (inserts < 2) {
		return;
	}
	// Partial overwrite from a single source vector: shuffle(source, base)
	uint32_t source = 0;
	uint32_t components[4];
	for (int k = 0; k < size; ++k) {
		if (values[k] == 0) {
			components[k] = 0xffffffff;
			continue;
		}
		inst *e = def(values[k]);
		if (e == NULL || e->op != OP_COMPOSITE_EXTRACT || e->n != 5 || vector_size(type_of(e->w[3])) == 0 ||
		    component_type(type_of(e->w[3])) != component_type(type) || (source != 0 && e->w[3] != source)) {
			return;
		}
		source        = e->w[3];
		components[k] = e->w[4];
	}
	int source_size = vector_size(type_of(source));
	for (int k = 0; k < size; ++k) {
		if (components[k] == 0xffffffff) {
			components[k] = (uint32_t)(source_size + k);
		}
	}
	reshape(in, OP_VECTOR_SHUFFLE, 5 + size);
	in->w[3] = source;
	in->w[4] = base;
	memcpy(&in->w[5], components, size * sizeof(uint32_t));
}

static void fold_multiply(inst *in) {
	uint32_t type = in->w[1];
	inst    *t    = def(component_type(type));
	if (t == NULL || t->op != OP_TYPE_FLOAT || in->n != 5) {
		return;
	}
	uint32_t scalar;
	uint32_t other;
	if (is_splat(in->w[4], &scalar)) {
		other = in->w[3];
	}
	else if (is_splat(in->w[3], &scalar)) {
		other = in->w[4];
	}
	else {
		return;
	}
	reshape(in, OP_VECTOR_TIMES_SCALAR, 5);
	in->w[3] = other;
	in->w[4] = scalar;
}

static void fold(inst *in) {
	if (in->dead || m.pinned[result_id(in)]) {
		return;
	}
	switch (in->op) {
	case OP_COPY_OBJECT:
		replace(in, in->w[3]);
		break;
	case OP_COMPOSITE_EXTRACT:
		fold_extract(in);
		break;
	case OP_COMPOSITE_CONSTRUCT:
		fold_construct(in);
		break;
	case OP_COMPOSITE_INSERT:
		fold_insert(in);
		break;
	case OP_F_MUL:
		fold_multiply(in);
		break;
	default:
		break;
	}
}

// Value numbering

static bool is_commutative(uint16_t op) {
	switch (op) {
	case OP_I_ADD:
	case OP_F_ADD:
	case OP_I_MUL:
	case OP_F_MUL:
	case OP_LOGICAL_EQUAL:
	case OP_LOGICAL_NOT_EQUAL:
	case OP_LOGICAL_OR:
	case OP_LOGICAL_AND:
	case OP_I_EQUAL:
	case OP_I_NOT_EQUAL:
	case OP_F_ORD_EQUAL:
	case OP_F_ORD_NOT_EQUAL:
	case OP_BITWISE_OR:
	case OP_BITWISE_XOR:
	case OP_BITWISE_AND:
		return true;
	default:
		return false;
	}
}

static bool ext_inst_writes_memory(inst *in) {
	uint32_t number = in->w[4];
	return number == GLSL_MODF || number == GLSL_FREXP || (number >= GLSL_INTERPOLATE_CENTROID && number <= GLSL_INTERPOLATE_OFFSET);
}

// Instructions whose result only depends on their operands
static bool is_pure(inst *in) {
	switch (in->op) {
	case OP_ACCESS_CHAIN:
	case OP_VECTOR_SHUFFLE:
	case OP_COMPOSITE_CONSTRUCT:
	case OP_COMPOSITE_EXTRACT:
	case OP_COMPOSITE_INSERT:
	case OP_TRANSPOSE:
	case OP_SAMPLED_IMAGE:
	case OP_IMAGE_SAMPLE_IMPLICIT:
	case OP_IMAGE_SAMPLE_EXPLICIT:
	case OP_IMAGE_FETCH:
	case OP_IMAGE_QUERY_SIZE_LOD:
	case OP_IMAGE_QUERY_SIZE:
	case OP_BITCAST:
	case OP_DOT:
	case OP_SELECT:
	case OP_NOT:
		return true;
	case OP_EXT_INST:
		return !ext_inst_writes_memory(in);
	case OP_LOAD: {
		// Read only storage
		int storage = pointer_storage(in->w[3]);
		return in->n == 4 &&
		       (storage == STORAGE_UNIFORM_CONSTANT || storage == STORAGE_INPUT || storage == STORAGE_UNIFORM || storage == STORAGE_PUSH_CONSTANT);
	}
	default:
		break;
	}
	return (in->op >= OP_CONVERT_F_TO_U && in->op <= OP_CONVERT_U_TO_F) || (in->op >= OP_S_NEGATE && in->op <= OP_MATRIX_TIMES_MATRIX) ||
	       (in->op >= OP_LOGICAL_EQUAL && in->op <= OP_LOGICAL_NOT) || (in->op >= OP_I_EQUAL && in->op <= OP_F_UNORD_GREATER_EQUAL) ||
	       (in->op >= OP_SHIFT_RIGHT_LOGICAL && in->op <= OP_BITWISE_AND) || (in->op >= OP_DPDX && in->op <= OP_FWIDTH);
}

// Float arithmetic is not merged: a shared result changes how drivers contract multiplies and adds into fma,
// rounding differs and hash based noise (frac(sin(x) * 43758.5)) visibly changes. Drivers merge it after contraction.
static bool is_float_type(uint32_t type) {
	inst *t = def(type);
	if (t != NULL && (t->op == OP_TYPE_VECTOR || t->op == OP_TYPE_MATRIX)) {
		return is_float_type(t->w[2]);
	}
	return t != NULL && t->op == OP_TYPE_FLOAT;
}

static bool is_float_arithmetic(inst *in) {
	if (in->op == OP_EXT_INST) {
		return is_float_type(in->w[1]); // Some end in a multiply (normalize, radians)
	}
	switch (in->op) {
	case OP_F_NEGATE:
	case OP_F_ADD:
	case OP_F_SUB:
	case OP_F_MUL:
	case OP_F_DIV:
	case OP_F_REM:
	case OP_F_MOD:
	case OP_VECTOR_TIMES_SCALAR:
	case OP_MATRIX_TIMES_SCALAR:
	case OP_VECTOR_TIMES_MATRIX:
	case OP_MATRIX_TIMES_VECTOR:
	case OP_MATRIX_TIMES_MATRIX:
	case OP_DOT:
		return true;
	default:
		return false;
	}
}

typedef struct {
	uint32_t hash;
	inst    *in;
	int      next;
} vn_entry;

typedef struct {
	int      *buckets;
	int       bucket_mask;
	vn_entry *entries;
	int       count;
	int       capacity;
} vn_table;

static uint32_t vn_hash(inst *in) {
	uint32_t hash = 2166136261u ^ in->op;
	for (int k = 1; k < in->n; ++k) {
		if (k == 2) {
			continue;
		}
		hash = (hash ^ in->w[k]) * 16777619u;
	}
	return hash;
}

static bool vn_equal(inst *a, inst *b) {
	return a->op == b->op && a->n == b->n && a->w[1] == b->w[1] && memcmp(&a->w[3], &b->w[3], (a->n - 3) * sizeof(uint32_t)) == 0;
}

static void number_values(func *f, vn_table *table, int b, int **children, int *children_count) {
	block *blk  = &f->blocks[b];
	int    mark = table->count;
	for (int i = 0; i < blk->insts.count; ++i) {
		inst *in = blk->insts.items[i];
		if (in->dead) {
			continue;
		}
		resolve_operands(in);
		fold(in);
		if (in->dead || m.pinned[result_id(in)] || !is_pure(in) || is_float_arithmetic(in)) {
			continue;
		}
		if (is_commutative(in->op) && in->n == 5 && in->w[3] > in->w[4]) {
			uint32_t t = in->w[3];
			in->w[3]   = in->w[4];
			in->w[4]   = t;
		}
		uint32_t hash  = vn_hash(in);
		int      found = -1;
		for (int e = table->buckets[hash & table->bucket_mask]; e >= 0; e = table->entries[e].next) {
			vn_entry *entry = &table->entries[e];
			// Sampled images have to stay in the block that uses them
			if (entry->hash == hash && vn_equal(entry->in, in) && (in->op != OP_SAMPLED_IMAGE || entry->in->block == b)) {
				found = e;
				break;
			}
		}
		if (found >= 0) {
			replace(in, result_id(table->entries[found].in));
			continue;
		}
		if (table->count == table->capacity) {
			table->capacity = table->capacity == 0 ? 256 : table->capacity * 2;
			table->entries  = realloc(table->entries, table->capacity * sizeof(vn_entry));
		}
		int index                           = table->count++;
		table->entries[index].hash          = hash;
		table->entries[index].in            = in;
		table->entries[index].next          = table->buckets[hash & table->bucket_mask];
		table->buckets[hash & table->bucket_mask] = index;
	}
	for (int c = 0; c < children_count[b]; ++c) {
		number_values(f, table, children[b][c], children, children_count);
	}
	// Leave the dominator subtree, entries are popped in reverse
	while (table->count > mark) {
		vn_entry *entry                           = &table->entries[--table->count];
		table->buckets[entry->hash & table->bucket_mask] = entry->next;
	}
}

static void simplify(func *f) {
	build_dominators(f);
	int **children       = calloc(f->blocks_count, sizeof(int *));
	int  *children_count = calloc(f->blocks_count, sizeof(int));
	for (int i = 0; i < f->blocks_count; ++i) {
		children[i] = malloc(f->blocks_count * sizeof(int));
	}
	int  count;
	int *order = rpo_blocks(f, &count);
	for (int r = 1; r < count; ++r) {
		int idom = f->blocks[order[r]].idom;
		if (idom >= 0) {
			children[idom][children_count[idom]++] = order[r];
		}
	}
	int instructions = 0;
	for (int i = 0; i < f->blocks_count; ++i) {
		instructions += f->blocks[i].insts.count;
	}
	vn_table table    = {0};
	int      buckets  = 64;
	while (buckets < instructions * 2) {
		buckets *= 2;
	}
	table.buckets     = malloc(buckets * sizeof(int));
	table.bucket_mask = buckets - 1;
	for (int i = 0; i < buckets; ++i) {
		table.buckets[i] = -1;
	}
	number_values(f, &table, 0, children, children_count);

	free(table.buckets);
	free(table.entries);
	for (int i = 0; i < f->blocks_count; ++i) {
		free(children[i]);
	}
	free(children);
	free(children_count);
	free(order);
}

// Dead code

static bool has_side_effects(inst *in) {
	switch (in->op) {
	case OP_STORE:
	case OP_IMAGE_WRITE:
	case OP_FUNCTION_CALL:
	case OP_KILL:
	case OP_RETURN:
	case OP_RETURN_VALUE:
	case OP_BRANCH:
	case OP_BRANCH_CONDITIONAL:
	case OP_SELECTION_MERGE:
	case OP_LOOP_MERGE:
	case OP_UNREACHABLE:
	case OP_RAY_QUERY_INITIALIZE:
	case OP_RAY_QUERY_PROCEED:
		return true;
	case OP_EXT_INST:
		return ext_inst_writes_memory(in);
	default:
		return false;
	}
}

static void remove_dead_code(func *f) {
	inst_list work = {0};
	for (int bi = 0; bi < f->blocks_count; ++bi) {
		block *b = &f->blocks[bi];
		if (b->removed) {
			continue;
		}
		for (int i = 0; i < b->insts.count; ++i) {
			inst *in = b->insts.items[i];
			in->live = false;
			if (!in->dead) {
				resolve_operands(in);
				if (has_side_effects(in) || m.pinned[result_id(in)]) {
					in->live = true;
					list_push(&work, in);
				}
			}
		}
	}
	while (work.count > 0) {
		inst     *in = work.items[--work.count];
		uint32_t *ids[MAX_IDS_PER_INSTRUCTION];
		int       count = inst_ids(in, ids);
		for (int k = 0; k < count; ++k) {
			inst *d = def(*ids[k]);
			if (d != NULL && d->block >= 0 && d->op != OP_LABEL && !d->live && !d->dead) {
				d->live = true;
				list_push(&work, d);
			}
		}
	}
	for (int bi = 0; bi < f->blocks_count; ++bi) {
		block *b    = &f->blocks[bi];
		int    kept = 0;
		for (int i = 0; i < b->insts.count; ++i) {
			inst *in = b->insts.items[i];
			if (!in->dead && in->live) {
				b->insts.items[kept++] = in;
			}
			else {
				in->dead = true;
			}
		}
		b->insts.count = kept;
	}
}

static bool optimize_function(func *f) {
	if (f->blocks_count == 0) {
		return true;
	}
	if (!remove_unreachable(f)) {
		return false;
	}
	promote_variables(f);
	remove_trivial_phis(f);
	for (int pass = 0; pass < 2; ++pass) {
		simplify(f);
		remove_dead_code(f);
	}
	return true;
}

// Module level cleanup and output

static void mark_ids(inst *in, bool *live) {
	uint32_t *ids[MAX_IDS_PER_INSTRUCTION];
	int       count = inst_ids(in, ids);
	for (int k = 0; k < count; ++k) {
		uint32_t id = resolve(*ids[k]);
		*ids[k]     = id;
		if (id < m.capacity) {
			live[id] = true;
		}
	}
}

static bool is_declaration(inst *in) {
	return is_type_op(in->op) || in->op == OP_CONSTANT || in->op == OP_CONSTANT_TRUE || in->op == OP_CONSTANT_FALSE || in->op == OP_CONSTANT_COMPOSITE ||
	       in->op == OP_UNDEF;
}

// Writes the module to out, or only counts its words when out is NULL
static size_t emit_module(uint32_t *out, bool *live) {
	size_t count = 5;
#define EMIT(in)                                                      \
	do {                                                              \
		if (out != NULL) {                                            \
			memcpy(&out[count], (in)->w, (in)->n * sizeof(uint32_t)); \
		}                                                             \
		count += (in)->n;                                             \
	} while (0)

	if (out != NULL) {
		memcpy(out, m.header, sizeof(m.header));
		out[3] = m.bound;
	}
	for (int i = 0; i < m.globals.count; ++i) {
		inst *g = m.globals.items[i];
		if (g->dead) {
			continue;
		}
		if ((g->op == OP_NAME || g->op == OP_MEMBER_NAME || g->op == OP_DECORATE || g->op == OP_MEMBER_DECORATE) && !live[g->w[1]]) {
			continue;
		}
		EMIT(g);
	}
	for (int i = 0; i < m.added.count; ++i) {
		if (!m.added.items[i]->dead) {
			EMIT(m.added.items[i]);
		}
	}
	for (int fi = 0; fi < m.funcs_count; ++fi) {
		func *f = &m.funcs[fi];
		EMIT(f->header);
		for (int i = 0; i < f->params.count; ++i) {
			EMIT(f->params.items[i]);
		}
		for (int bi = 0; bi < f->blocks_count; ++bi) {
			block *b = &f->blocks[bi];
			if (b->removed) {
				continue;
			}
			EMIT(b->label);
			for (int i = 0; i < b->insts.count; ++i) {
				if (!b->insts.items[i]->dead) {
					EMIT(b->insts.items[i]);
				}
			}
		}
		EMIT(f->end);
	}
#undef EMIT
	return count;
}

static size_t write_module(uint32_t *out, size_t capacity) {
	bool *live = calloc(m.capacity, sizeof(bool));

	for (int fi = 0; fi < m.funcs_count; ++fi) {
		func *f = &m.funcs[fi];
		live[f->header->w[2]] = true;
		mark_ids(f->header, live);
		for (int i = 0; i < f->params.count; ++i) {
			live[f->params.items[i]->w[2]] = true;
			mark_ids(f->params.items[i], live);
		}
		for (int bi = 0; bi < f->blocks_count; ++bi) {
			block *b = &f->blocks[bi];
			if (b->removed) {
				continue;
			}
			live[b->label->w[1]] = true;
			for (int i = 0; i < b->insts.count; ++i) {
				inst *in = b->insts.items[i];
				if (!in->dead) {
					uint32_t result = result_id(in);
					if (result != 0) {
						live[result] = true;
					}
					mark_ids(in, live);
				}
			}
		}
	}
	// Declarations are live when referenced, propagated until nothing changes
	for (int pass = 0; pass < 2; ++pass) {
		inst_list *list = pass == 0 ? &m.globals : &m.added;
		for (int i = 0; i < list->count; ++i) {
			list->items[i]->live = false;
		}
	}
	bool changed = true;
	while (changed) {
		changed = false;
		for (int pass = 1; pass >= 0; --pass) {
			inst_list *list = pass == 0 ? &m.globals : &m.added;
			for (int i = list->count - 1; i >= 0; --i) {
				inst    *g      = list->items[i];
				uint32_t result = result_id(g);
				if (g->live || g->dead || g->op == OP_NAME || g->op == OP_MEMBER_NAME || g->op == OP_DECORATE || g->op == OP_MEMBER_DECORATE) {
					continue;
				}
				if (!is_declaration(g) || (result != 0 && live[result])) {
					g->live = true;
					if (result != 0) {
						live[result] = true;
					}
					mark_ids(g, live);
					changed = true;
				}
			}
		}
	}
	for (int pass = 0; pass < 2; ++pass) {
		inst_list *list = pass == 0 ? &m.globals : &m.added;
		for (int i = 0; i < list->count; ++i) {
			inst *g = list->items[i];
			if (is_declaration(g) && !g->live) {
				g->dead = true;
			}
		}
	}

	// Counted first, then written over the input words, which is safe because every instruction lives in the arena
	size_t count = emit_module(NULL, live);
	if (count <= capacity) {
		emit_module(out, live);
	}
	else {
		count = 0;
	}
	free(live);
	return count;
}

static void reset(void) {
	arena_free();
	for (int i = 0; i < m.funcs_count; ++i) {
		free(m.funcs[i].blocks);
	}
	free(m.funcs);
	free(m.defs);
	free(m.repl);
	free(m.pinned);
	free(m.undefs);
	memset(&m, 0, sizeof(m));
}

size_t spirv_optimize(uint32_t *words, size_t word_count, size_t capacity) {
	memset(&m, 0, sizeof(m));
	size_t result = word_count;
	if (parse(words, word_count)) {
		bool ok = true;
		for (int i = 0; i < m.funcs_count && ok; ++i) {
			ok = optimize_function(&m.funcs[i]);
		}
		if (ok) {
			size_t count = write_module(words, capacity);
			if (count > 0) {
				result = count;
			}
		}
	}
	reset();
	return result;
}
