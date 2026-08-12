#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MINIC_MEM_SIZE          (8 * 1024 * 1024)
#define MINIC_MAX_PARAMS        20
#define MINIC_MAX_VARS          128 // locals per scope
#define MINIC_MAX_VARTYPES      128 // struct-typed locals per scope
#define MINIC_MAX_EXTFUNS       1024
#define MINIC_MAX_SIG           64
#define MINIC_MAX_ENUM_CONSTS   512
#define MINIC_MAX_INT_TYPEDEFS  128
#define MINIC_MAX_STRUCT_FIELDS 32
#define MINIC_MAX_STRUCTS       64
#define MINIC_MAX_GLOBALS       64
#define MINIC_MAX_NAME          64

typedef unsigned char minic_u8;

typedef enum {
	MINIC_T_INT   = 0,
	MINIC_T_FLOAT = 1,
	MINIC_T_PTR   = 2, // void *, always holds a real host pointer
	MINIC_T_BOOL  = 3, // used in extern-call ABI, stored as INT in vals
	MINIC_T_CHAR  = 4, // used in extern-call ABI, stored as INT in vals
	MINIC_T_VOID  = 5, // void return only; stored as INT/0 in vals
	MINIC_T_EMBED = 6, // embedded struct field; field address is the value (no indirection)
} minic_type_t;

typedef struct {
	minic_type_t type;
	minic_type_t deref_type; // pointed-to type (for MINIC_T_PTR)
	union {
		int   i; // MINIC_T_INT
		float f; // MINIC_T_FLOAT
		void *p; // MINIC_T_PTR
	};
} minic_val_t;

// Struct descriptor. When native is true, offsets/types describe a real C layout;
// otherwise instances are stored as an array of boxed minic_val_t (script layout)
typedef struct {
	char         name[MINIC_MAX_NAME];
	int          size;   // sizeof the native C struct, 0 if unknown
	bool         native; // field offsets/types describe a native C layout
	int          field_count;
	char         fields[MINIC_MAX_STRUCT_FIELDS][MINIC_MAX_NAME];
	int          offsets[MINIC_MAX_STRUCT_FIELDS];                       // byte offset in the native C struct
	minic_type_t types[MINIC_MAX_STRUCT_FIELDS];                         // storage type of each field
	minic_type_t deref_types[MINIC_MAX_STRUCT_FIELDS];                   // pointed-to type for PTR fields
	char         field_structs[MINIC_MAX_STRUCT_FIELDS][MINIC_MAX_NAME]; // struct type name for struct-typed fields, or ""
} minic_struct_t;

extern minic_struct_t minic_structs[MINIC_MAX_STRUCTS];
extern int            minic_struct_count;

typedef void (*minic_ext_fn_raw_t)(void); // type-erased
typedef struct minic_ctx_s minic_ctx_t;
typedef minic_val_t (*minic_native_fn_t)(minic_val_t *args, int argc);

typedef struct {
	char               name[MINIC_MAX_NAME];
	char               sig[MINIC_MAX_SIG];
	minic_ext_fn_raw_t fn;
	minic_native_fn_t  native_fn; // if non-NULL, bypasses typed dispatch
	minic_type_t       ret_type;
	minic_type_t       param_types[MINIC_MAX_PARAMS];
	int                param_count;
} minic_ext_func_t;

// Script evaluation
minic_ctx_t *minic_eval(const char *src);
minic_ctx_t *minic_eval_named(const char *src, const char *filename);
void         minic_ctx_free(minic_ctx_t *ctx);
float        minic_ctx_result(minic_ctx_t *ctx);
minic_val_t  minic_ctx_call_fn(minic_ctx_t *ctx, void *fn_ptr, minic_val_t *args, int argc);
// Call a minic function from native C. fn_ptr is a minic func passed from a script,
// valid as long as the owning minic_ctx_t has not been freed
minic_val_t minic_call_fn(void *fn_ptr, minic_val_t *args, int argc);
void       *minic_alloc(int size);   // allocate in the active context's arena
bool        minic_in_arena(void *p); // true if p points into the active arena (script value) vs native C memory

// Host api registration (idempotent, safe to re-run)
void minic_register(const char *name, const char *sig, minic_ext_fn_raw_t fn); // sig like "f(p,i)" using i/f/p/b/c/v
void minic_register_native(const char *name, minic_native_fn_t fn);
void minic_struct_begin(const char *name, int size);
void minic_struct_field(const char *field, int offset, minic_type_t type, minic_type_t deref_type, const char *struct_type);
void minic_register_struct(const char *name, const char **fields, int field_count);                   // script-layout struct (boxed fields)
void minic_register_enum(const char *typedef_name, const char **names, const int *values, int count); // values NULL = 0,1,2...
void minic_enum_const_add(const char *name, int value);
void minic_int_typedef_add(const char *name);
void minic_register_global(const char *name, const void *ptr, minic_type_t type);
void minic_register_builtins(void);

// Registry enumeration (used for editor autocomplete and api.h generation)
int          minic_ext_func_count_get(void);
const char  *minic_ext_func_name_at(int i);
const char  *minic_ext_func_sig_at(int i);
int          minic_global_count_get(void);
const char  *minic_global_name_at(int i);
minic_type_t minic_global_type_at(int i);
int          minic_enum_const_count_get(void);
const char  *minic_enum_const_name_at(int i);
int          minic_enum_const_value_at(int i);

// Registry lookups (used by the interpreter)
minic_ext_func_t *minic_ext_func_get(const char *name);
minic_val_t       minic_dispatch(minic_ext_func_t *ef, minic_val_t *args, int argc);
int               minic_enum_const_get(const char *name); // -1 if unknown
bool              minic_is_int_typedef(const char *name);
bool              minic_global_get(const char *name, minic_val_t *out); // false if unknown

// Native struct registration helpers:
//   MINIC_STRUCT(my_t); MINIC_I(count); MINIC_S(name); MINIC_O(child, other_t); MINIC_END();
#define MINIC_STRUCT(T)       \
	{                         \
		typedef T minic_st_t; \
		minic_struct_begin(#T, (int)sizeof(minic_st_t))
#define MINIC_FIELD(f, t, dt, s) minic_struct_field(#f, (int)offsetof(minic_st_t, f), t, dt, s)
#define MINIC_I(f)               MINIC_FIELD(f, MINIC_T_INT, MINIC_T_INT, NULL)     // int
#define MINIC_F(f)               MINIC_FIELD(f, MINIC_T_FLOAT, MINIC_T_FLOAT, NULL) // float
#define MINIC_B(f)               MINIC_FIELD(f, MINIC_T_BOOL, MINIC_T_BOOL, NULL)   // bool
#define MINIC_S(f)               MINIC_FIELD(f, MINIC_T_PTR, MINIC_T_CHAR, NULL)    // char * string
#define MINIC_P(f)               MINIC_FIELD(f, MINIC_T_PTR, MINIC_T_PTR, NULL)     // untyped pointer
#define MINIC_O(f, T2)           MINIC_FIELD(f, MINIC_T_PTR, MINIC_T_PTR, #T2)      // pointer to struct T2
#define MINIC_E(f, T2)           MINIC_FIELD(f, MINIC_T_EMBED, MINIC_T_PTR, #T2)    // embedded struct T2
#define MINIC_END()              }

// Enum registration helper (sequential values starting at 0):
//   MINIC_ENUM("my_enum_t", "MY_A", "MY_B", "MY_C");
#define MINIC_ENUM(T, ...)                                                                                 \
	do {                                                                                                   \
		static const char *_minic_names[] = {__VA_ARGS__};                                                 \
		minic_register_enum(T, _minic_names, NULL, (int)(sizeof(_minic_names) / sizeof(_minic_names[0]))); \
	} while (0)

static inline minic_val_t minic_val_int(int v) {
	minic_val_t r = {0};
	r.type        = MINIC_T_INT;
	r.deref_type  = MINIC_T_INT;
	r.i           = v;
	return r;
}

static inline minic_val_t minic_val_float(float v) {
	minic_val_t r = {0};
	r.type        = MINIC_T_FLOAT;
	r.deref_type  = MINIC_T_FLOAT;
	r.f           = v;
	return r;
}

static inline minic_val_t minic_val_ptr(void *v) {
	minic_val_t r;
	r.type       = MINIC_T_PTR;
	r.deref_type = MINIC_T_PTR;
	r.p          = v;
	return r;
}

static inline minic_val_t minic_val_typed_ptr(void *v, minic_type_t deref_type) {
	minic_val_t r;
	r.type       = MINIC_T_PTR;
	r.deref_type = deref_type;
	r.p          = v;
	return r;
}

static inline minic_val_t minic_val_void(void) {
	return minic_val_int(0);
}

static inline double minic_val_to_d(minic_val_t v) {
	switch (v.type) {
	case MINIC_T_INT:
	case MINIC_T_BOOL:
	case MINIC_T_CHAR:
		return (double)v.i;
	case MINIC_T_FLOAT:
		return (double)v.f;
	case MINIC_T_PTR:
		return (double)(uintptr_t)v.p;
	default:
		return 0.0;
	}
}

static inline void *minic_val_to_ptr(minic_val_t v) {
	return (v.type == MINIC_T_PTR) ? v.p : (void *)(uintptr_t)(uint64_t)minic_val_to_d(v);
}

static inline int minic_val_is_true(minic_val_t v) {
	return minic_val_to_d(v) != 0.0;
}

static inline minic_val_t minic_val_coerce(double d, minic_type_t t) {
	switch (t) {
	case MINIC_T_FLOAT:
		return minic_val_float((float)d);
	case MINIC_T_PTR:
		return minic_val_ptr((void *)(uintptr_t)(uint64_t)d);
	default:
		return minic_val_int((int)d);
	}
}

static inline minic_val_t minic_val_cast(minic_val_t v, minic_type_t t) {
	if (v.type == t)
		return v;
	return minic_val_coerce(minic_val_to_d(v), t);
}

void minic_tests();
