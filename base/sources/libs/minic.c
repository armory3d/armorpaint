
// Minimal C interpreter
// The source is tokenized and compiled to bytecode once, then run on a small stack VM.
// Names resolve at compile time: variables to frame or global slots, fields to byte offsets,
// functions to indices. Values keep their runtime type tags, arithmetic widens like C.

#include "minic.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ████████╗ ██████╗ ██╗  ██╗███████╗███╗   ██╗
// ╚══██╔══╝██╔═══██╗██║ ██╔╝██╔════╝████╗  ██║
//    ██║   ██║   ██║█████╔╝ █████╗  ██╔██╗ ██║
//    ██║   ██║   ██║██╔═██╗ ██╔══╝  ██║╚██╗██║
//    ██║   ╚██████╔╝██║  ██╗███████╗██║ ╚████║
//    ╚═╝    ╚═════╝ ╚═╝  ╚═╝╚══════╝╚═╝  ╚═══╝

#define MINIC_TOK_LIST                                                                                                                                        \
	X(TOK_INT, "'int'")                                                                                                                                       \
	X(TOK_FLOAT, "'float'")                                                                                                                                   \
	X(TOK_CHAR, "'char'")                                                                                                                                     \
	X(TOK_DOUBLE, "'double'")                                                                                                                                 \
	X(TOK_BOOL, "'bool'")                                                                                                                                     \
	X(TOK_VOID, "'void'")                                                                                                                                     \
	X(TOK_RETURN, "'return'")                                                                                                                                 \
	X(TOK_IF, "'if'")                                                                                                                                         \
	X(TOK_ELSE, "'else'")                                                                                                                                     \
	X(TOK_WHILE, "'while'")                                                                                                                                   \
	X(TOK_FOR, "'for'")                                                                                                                                       \
	X(TOK_BREAK, "'break'")                                                                                                                                   \
	X(TOK_CONTINUE, "'continue'")                                                                                                                             \
	X(TOK_STRUCT, "'struct'")                                                                                                                                 \
	X(TOK_TYPEDEF, "'typedef'")                                                                                                                               \
	X(TOK_ENUM, "'enum'")                                                                                                                                     \
	X(TOK_IDENT, "identifier")                                                                                                                                \
	X(TOK_NUMBER, "number")                                                                                                                                   \
	X(TOK_CHAR_LIT, "char literal")                                                                                                                           \
	X(TOK_STR_LIT, "string literal")                                                                                                                          \
	X(TOK_LPAREN, "'('") X(TOK_RPAREN, "')'") X(TOK_LBRACE, "'{'") X(TOK_RBRACE, "'}'") X(TOK_LBRACKET, "'['") X(TOK_RBRACKET, "']'") X(TOK_SEMICOLON, "';'") \
	    X(TOK_COMMA, "','") X(TOK_ASSIGN, "'='") X(TOK_PLUS_ASSIGN, "'+='") X(TOK_MINUS_ASSIGN, "'-='") X(TOK_MUL_ASSIGN, "'*='") X(TOK_DIV_ASSIGN, "'/='")   \
	        X(TOK_MOD_ASSIGN, "'%='") X(TOK_SHL_ASSIGN, "'<<='") X(TOK_SHR_ASSIGN, "'>>='") X(TOK_AND_ASSIGN, "'&='") X(TOK_OR_ASSIGN, "'|='")                \
	            X(TOK_XOR_ASSIGN, "'^='") X(TOK_EQ, "'=='") X(TOK_NEQ, "'!='") X(TOK_LT, "'<'") X(TOK_GT, "'>'") X(TOK_LE, "'<='") X(TOK_GE, "'>='")          \
	                X(TOK_AND, "'&&'") X(TOK_OR, "'||'") X(TOK_NOT, "'!'") X(TOK_AMP, "'&'") X(TOK_PLUS, "'+'") X(TOK_MINUS, "'-'") X(TOK_INC, "'++'")        \
	                    X(TOK_DEC, "'--'") X(TOK_STAR, "'*'") X(TOK_SLASH, "'/'") X(TOK_PERCENT, "'%'") X(TOK_SHL, "'<<'") X(TOK_SHR, "'>>'")                 \
	                        X(TOK_BITOR, "'|'") X(TOK_XOR, "'^'") X(TOK_BITNOT, "'~'") X(TOK_DOT, "'.'") X(TOK_ARROW, "'->'") X(TOK_QUESTION, "'?'")          \
	                            X(TOK_COLON, "':'") X(TOK_EOF, "end of file")

typedef enum {
#define X(t, s) t,
	MINIC_TOK_LIST
#undef X
} minic_tok_type_t;

static const char *minic_tok_names[] = {
#define X(t, s) s,
    MINIC_TOK_LIST
#undef X
};

typedef struct {
	minic_tok_type_t type;
	int              pos; // source offset, for line numbers
	minic_val_t      val; // TOK_NUMBER, TOK_CHAR_LIT, TOK_STR_LIT
	char             text[MINIC_MAX_NAME];
} minic_token_t;

static minic_ctx_t *minic_active  = NULL; // Context whose arena minic_alloc uses
static bool         minic_mem_oom = false;

static minic_ext_func_t minic_ext_funcs[MINIC_MAX_EXTFUNS]; // Registry, defined with the other externals below
static const void      *minic_global_ptr(const char *name, minic_type_t *type);
static int              minic_enum_const_find(const char *name);

static const struct {
	const char      *kw;
	minic_tok_type_t tok;
} minic_keywords[] = {
    {"int", TOK_INT},           {"float", TOK_FLOAT},   {"char", TOK_CHAR},       {"double", TOK_DOUBLE}, {"bool", TOK_BOOL}, {"void", TOK_VOID},
    {"return", TOK_RETURN},     {"if", TOK_IF},         {"else", TOK_ELSE},       {"while", TOK_WHILE},   {"for", TOK_FOR},   {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE}, {"struct", TOK_STRUCT}, {"typedef", TOK_TYPEDEF}, {"enum", TOK_ENUM},
};

// Longer operators must come before their prefixes
static const struct {
	const char      *op;
	minic_tok_type_t tok;
} minic_ops[] = {
    {"<<=", TOK_SHL_ASSIGN}, {">>=", TOK_SHR_ASSIGN}, {"++", TOK_INC},        {"+=", TOK_PLUS_ASSIGN}, {"--", TOK_DEC},      {"-=", TOK_MINUS_ASSIGN},
    {"->", TOK_ARROW},       {"*=", TOK_MUL_ASSIGN},  {"/=", TOK_DIV_ASSIGN}, {"==", TOK_EQ},          {"!=", TOK_NEQ},      {"&&", TOK_AND},
    {"||", TOK_OR},          {"<=", TOK_LE},          {">=", TOK_GE},         {"<<", TOK_SHL},         {">>", TOK_SHR},      {"%=", TOK_MOD_ASSIGN},
    {"&=", TOK_AND_ASSIGN},  {"|=", TOK_OR_ASSIGN},   {"^=", TOK_XOR_ASSIGN}, {"+", TOK_PLUS},         {"-", TOK_MINUS},     {"*", TOK_STAR},
    {"/", TOK_SLASH},        {"%", TOK_PERCENT},      {"=", TOK_ASSIGN},      {"!", TOK_NOT},          {"&", TOK_AMP},       {"|", TOK_BITOR},
    {"^", TOK_XOR},          {"~", TOK_BITNOT},       {"<", TOK_LT},          {">", TOK_GT},           {"(", TOK_LPAREN},    {")", TOK_RPAREN},
    {"{", TOK_LBRACE},       {"}", TOK_RBRACE},       {"[", TOK_LBRACKET},    {"]", TOK_RBRACKET},     {";", TOK_SEMICOLON}, {",", TOK_COMMA},
    {".", TOK_DOT},          {"?", TOK_QUESTION},     {":", TOK_COLON},
};

static int minic_escape(char c) {
	const char *escapes = "ntr\\\"'";
	const char *values  = "\n\t\r\\\"'";
	const char *e       = c != '\0' ? strchr(escapes, c) : NULL;
	return e != NULL ? values[e - escapes] : '\0';
}

// Skip whitespace, comments and preprocessor directives
static int minic_lex_skip_trivia(const char *src, int pos) {
	for (;;) {
		while (src[pos] != '\0' && isspace((unsigned char)src[pos])) {
			pos++;
		}
		if ((src[pos] == '/' && src[pos + 1] == '/') || src[pos] == '#') {
			while (src[pos] != '\0' && src[pos] != '\n') {
				pos++;
			}
			continue;
		}
		if (src[pos] == '/' && src[pos + 1] == '*') {
			pos += 2;
			while (src[pos] != '\0' && !(src[pos] == '*' && src[pos + 1] == '/')) {
				pos++;
			}
			if (src[pos] != '\0') {
				pos += 2;
			}
			continue;
		}
		return pos;
	}
}

// Lex the token at src[pos] into t, return the position after it
static int minic_lex(const char *src, int pos, char *str_pool, minic_token_t *t) {
	for (;;) {
		pos    = minic_lex_skip_trivia(src, pos);
		t->pos = pos;
		char c = src[pos];

		if (c == '\0') {
			t->type = TOK_EOF;
			return pos;
		}

		if (c == '0' && (src[pos + 1] == 'x' || src[pos + 1] == 'X')) {
			char *end;
			t->val  = minic_val_int((int)(unsigned int)strtoull(src + pos, &end, 16));
			t->type = TOK_NUMBER;
			return (int)(end - src);
		}

		// Also accept a leading-dot float like .5
		if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)src[pos + 1]))) {
			double n = 0;
			while (isdigit((unsigned char)src[pos])) {
				n = n * 10 + (src[pos++] - '0');
			}
			bool is_float = false;
			if (src[pos] == '.') {
				pos++;
				double frac = 0.1;
				while (isdigit((unsigned char)src[pos])) {
					n += (src[pos++] - '0') * frac;
					frac *= 0.1;
				}
				is_float = true;
			}
			// Exponent like 1e-3, only when digits follow so '1e' stays unconsumed
			if (src[pos] == 'e' || src[pos] == 'E') {
				int p = pos + 1;
				if (src[p] == '+' || src[p] == '-') {
					p++;
				}
				if (isdigit((unsigned char)src[p])) {
					bool neg = src[pos + 1] == '-';
					int  exp = 0;
					while (isdigit((unsigned char)src[p])) {
						exp = exp * 10 + (src[p++] - '0');
					}
					n *= pow(10.0, neg ? -exp : exp);
					pos      = p;
					is_float = true;
				}
			}
			if (src[pos] == 'f' || src[pos] == 'F') {
				pos++;
				is_float = true;
			}
			t->val  = is_float ? minic_val_float((float)n) : minic_val_int((int)n);
			t->type = TOK_NUMBER;
			return pos;
		}

		if (c == '"') {
			// Literals live in the context's pool at the source offset of their opening quote,
			// valid for the context's lifetime, like static storage in C. The decoded text is
			// never longer than its source span, so literals cannot overlap.
			char *dst = str_pool + pos;
			int   wi  = 0;
			// Adjacent string literals concatenate into a single string
			while (src[pos] == '"') {
				pos++; // Consume opening '"'
				while (src[pos] != '"' && src[pos] != '\0') {
					char ch = src[pos++];
					if (ch == '\\') {
						char esc = src[pos++];
						if (esc == '\n') {
							continue; // Line continuation: backslash-newline, skip both
						}
						if (esc == '\r') { // Handle \r\n line endings
							if (src[pos] == '\n') {
								pos++;
							}
							continue;
						}
						ch = (char)minic_escape(esc);
					}
					dst[wi++] = ch;
				}
				if (src[pos] == '"') {
					pos++; // Consume closing '"'
				}
				int next = minic_lex_skip_trivia(src, pos); // Whitespace or a comment may separate the literals
				if (src[next] != '"') {
					break;
				}
				pos = next;
			}
			dst[wi] = '\0';
			t->type = TOK_STR_LIT;
			t->val  = minic_val_typed_ptr((void *)dst, MINIC_T_CHAR);
			return pos;
		}

		if (c == '\'') {
			pos++; // Consume opening '
			int v;
			if (src[pos] == '\\') {
				pos++;
				v = minic_escape(src[pos++]);
			}
			else {
				v = (unsigned char)src[pos++];
			}
			pos++; // Consume closing '
			t->type = TOK_CHAR_LIT;
			t->val  = minic_val_int(v);
			return pos;
		}

		if (isalpha((unsigned char)c) || c == '_') {
			int i = 0;
			while (isalnum((unsigned char)src[pos]) || src[pos] == '_') {
				// Names past the cap are truncated, not overflowed; the rest is still consumed
				// so the identifier does not split into two tokens
				if (i < MINIC_MAX_NAME - 1) {
					t->text[i++] = src[pos];
				}
				pos++;
			}
			t->text[i] = '\0';
			for (size_t k = 0; k < sizeof(minic_keywords) / sizeof(minic_keywords[0]); ++k) {
				if (minic_keywords[k].kw[0] == t->text[0] && strcmp(t->text, minic_keywords[k].kw) == 0) {
					t->type = minic_keywords[k].tok;
					return pos;
				}
			}
			if (strcmp(t->text, "true") == 0 || strcmp(t->text, "false") == 0) {
				t->type = TOK_NUMBER;
				t->val  = minic_val_int(t->text[0] == 't');
				return pos;
			}
			if (strcmp(t->text, "NULL") == 0) {
				t->type = TOK_NUMBER;
				t->val  = minic_val_int(0);
				return pos;
			}
			t->type = TOK_IDENT;
			return pos;
		}

		for (size_t k = 0; k < sizeof(minic_ops) / sizeof(minic_ops[0]); ++k) {
			const char *op = minic_ops[k].op;
			int         n  = 0;
			while (op[n] != '\0' && op[n] == src[pos + n]) {
				n++;
			}
			if (op[n] == '\0') {
				t->type = minic_ops[k].tok;
				return pos + n;
			}
		}
		pos++; // Unknown character: skip it
	}
}

// Lex the whole source once, the array ends with a TOK_EOF
static minic_token_t *minic_tokenize(const char *src, char *str_pool) {
	int            cap  = 256;
	int            n    = 0;
	int            pos  = 0;
	minic_token_t *toks = malloc(cap * sizeof(minic_token_t));
	for (;;) {
		if (n == cap) {
			cap *= 2;
			toks = realloc(toks, cap * sizeof(minic_token_t));
		}
		minic_token_t *t = &toks[n++];
		pos              = minic_lex(src, pos, str_pool, t);
		if (t->type == TOK_EOF) {
			return toks;
		}
	}
}

// ████████╗██╗   ██╗██████╗ ███████╗███████╗
// ╚══██╔══╝╚██╗ ██╔╝██╔══██╗██╔════╝██╔════╝
//    ██║    ╚████╔╝ ██████╔╝█████╗  ███████╗
//    ██║     ╚██╔╝  ██╔═══╝ ██╔══╝  ╚════██║
//    ██║      ██║   ██║     ███████╗███████║
//    ╚═╝      ╚═╝   ╚═╝     ╚══════╝╚══════╝

typedef struct {
	minic_type_t    kind;
	minic_type_t    deref;
	minic_struct_t *def;
	int             pointer; // pointer depth; zero for scalar or struct storage
	int             size;
	int             alignment;
} minic_ctype_t;

typedef struct {
	char          name[MINIC_MAX_NAME];
	char          params[MINIC_MAX_PARAMS][MINIC_MAX_NAME];
	minic_ctype_t param_types[MINIC_MAX_PARAMS];
	int           param_count;
	int           body;  // token index of the '{' that starts the body, -1 for a prototype
	int           entry; // bytecode offset
	int           slot_count;
	minic_ctype_t ret_type;
	minic_ctx_t  *ctx; // owning context
} minic_func_t;

typedef struct {
	int          pc;
	minic_val_t *fp;
} minic_frame_t;

#define MINIC_STACK_SIZE      (1024 * 1024) // Top of the arena, VM values: call frames and temporaries
#define MINIC_STACK_SLACK     1024          // Temporaries a frame may push on top of its slots
#define MINIC_MAX_FRAMES      4096
#define MINIC_MAX_GLOBAL_VARS 1024

struct minic_ctx_s {
	minic_u8       *mem;
	int             mem_used;
	int             mem_frame; // End of the heap, the VM stack sits above it
	char           *str_pool;  // String literals, indexed by source offset
	char           *src_copy;
	const char     *filename;
	int            *code;
	int            *code_pos; // Source offset of each code word, for runtime errors
	int             code_len;
	int             code_cap;
	minic_val_t    *consts;
	int             const_count;
	int             const_cap;
	minic_func_t   *funcs;
	int             func_count;
	int             func_cap;
	minic_struct_t *structs;
	int             struct_count;
	minic_val_t    *globals;
	int             global_count;
	minic_func_t    init; // Global initializers
	minic_val_t    *stack_end;
	minic_val_t    *sp;
	minic_frame_t  *frames;
	int             depth;
	minic_val_t     return_val;
	float           result;
};

static void *minic_alloc_aligned(int size, int alignment) {
	minic_ctx_t *ctx     = minic_active;
	uintptr_t    start   = (uintptr_t)ctx->mem + ctx->mem_used;
	uintptr_t    address = (start + alignment - 1) & ~(uintptr_t)(alignment - 1);
	size_t       offset  = address - (uintptr_t)ctx->mem;
	if (size < 0 || offset > (size_t)ctx->mem_frame || (size_t)size > (size_t)ctx->mem_frame - offset) {
		minic_mem_oom = true;
		return NULL;
	}
	ctx->mem_used = (int)offset + size;
	return (void *)address;
}

void *minic_alloc(int size) {
	return minic_alloc_aligned(size, MINIC_ALIGNOF(long double));
}

static bool minic_tok_is_type(minic_tok_type_t t) {
	return t >= TOK_INT && t <= TOK_VOID;
}

static minic_type_t minic_tok_to_type(minic_tok_type_t t) {
	static const minic_type_t types[] = {MINIC_T_INT, MINIC_T_FLOAT, MINIC_T_CHAR, MINIC_T_DOUBLE, MINIC_T_BOOL, MINIC_T_VOID};
	return types[t - TOK_INT];
}

static minic_ctype_t minic_scalar_type(minic_type_t kind) {
	minic_ctype_t type = {0};
	type.kind          = kind;
	type.deref         = kind;
	switch (kind) {
	case MINIC_T_INT:
		type.size      = sizeof(int32_t);
		type.alignment = MINIC_ALIGNOF(int32_t);
		break;
	case MINIC_T_FLOAT:
		type.size      = sizeof(float);
		type.alignment = MINIC_ALIGNOF(float);
		break;
	case MINIC_T_DOUBLE:
		type.size      = sizeof(double);
		type.alignment = MINIC_ALIGNOF(double);
		break;
	case MINIC_T_CHAR:
		type.size      = sizeof(char);
		type.alignment = MINIC_ALIGNOF(char);
		break;
	case MINIC_T_BOOL:
		type.size      = sizeof(bool);
		type.alignment = MINIC_ALIGNOF(bool);
		break;
	case MINIC_T_PTR:
		type.size      = sizeof(void *);
		type.alignment = MINIC_ALIGNOF(void *);
		type.pointer   = 1;
		break;
	default:
		type.alignment = 1;
		break;
	}
	return type;
}

static minic_ctype_t minic_pointer_type(minic_ctype_t element) {
	minic_ctype_t type = minic_scalar_type(MINIC_T_PTR);
	type.pointer       = element.pointer + 1;
	type.deref         = element.pointer ? element.deref : element.kind;
	type.def           = element.def;
	return type;
}

static minic_ctype_t minic_element_type(minic_ctype_t pointer) {
	if (pointer.kind == MINIC_T_VOID) {
		pointer = minic_scalar_type(MINIC_T_PTR); // Only known at run time, index it as a pointer array
	}
	if (pointer.pointer > 1) {
		pointer.pointer--;
		return pointer;
	}
	minic_ctype_t type = minic_scalar_type(pointer.deref);
	if (pointer.def != NULL) {
		type.kind      = MINIC_T_EMBED;
		type.def       = pointer.def;
		type.size      = pointer.def->size;
		type.alignment = pointer.def->alignment;
	}
	return type;
}

// The pointee tag a loaded pointer carries
static minic_type_t minic_load_deref(minic_ctype_t type) {
	return type.pointer > 1 ? MINIC_T_PTR : type.deref;
}

static minic_struct_t *minic_struct_get(minic_ctx_t *ctx, const char *name) {
	for (int i = 0; i < ctx->struct_count; ++i) {
		if (strcmp(ctx->structs[i].name, name) == 0) {
			return &ctx->structs[i];
		}
	}
	return NULL;
}

static int minic_struct_field_idx(minic_struct_t *def, const char *field) {
	for (int i = 0; i < def->field_count; ++i) {
		if (strcmp(def->fields[i], field) == 0) {
			return i;
		}
	}
	return -1;
}

static minic_ctype_t minic_field_type(minic_ctx_t *ctx, minic_struct_t *def, int idx) {
	minic_ctype_t type = minic_scalar_type(def->types[idx]);
	type.deref         = def->deref_types[idx];
	type.pointer       = def->pointer_depths[idx];
	type.def           = minic_struct_get(ctx, def->field_structs[idx]);
	if (type.kind == MINIC_T_EMBED && type.def != NULL) {
		type.size      = type.def->size;
		type.alignment = type.def->alignment;
	}
	return type;
}

// ██╗   ██╗ █████╗ ██╗     ██╗   ██╗███████╗███████╗
// ██║   ██║██╔══██╗██║     ██║   ██║██╔════╝██╔════╝
// ██║   ██║███████║██║     ██║   ██║█████╗  ███████╗
// ╚██╗ ██╔╝██╔══██║██║     ██║   ██║██╔══╝  ╚════██║
//  ╚████╔╝ ██║  ██║███████╗╚██████╔╝███████╗███████║
//   ╚═══╝  ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚══════╝╚══════╝

typedef enum {
	OP_HALT,
	OP_INT,   // imm: push an int
	OP_CONST, // k: push consts[k]
	OP_POP,
	OP_DUP,
	OP_LOADV,      // ref: push a variable slot
	OP_STOREV,     // ref kind size: store the top into a variable, the value stays
	OP_INITV,      // ref kind deref: pop the initial value of a declared variable
	OP_INIT_EMBED, // ref size alignment: allocate struct storage for a variable
	OP_INIT_ARR,   // ref kind size alignment: pop the count, allocate an array
	OP_ADDRV,      // ref deref: push the address of a variable
	OP_LOADM,      // kind deref: replace an address with the value stored there
	OP_STOREM,     // kind size: pop value and address, store, push the value
	OP_LOADH,      // k: push a host global, consts[k] holds its address and type
	OP_FIELD,      // offset field: replace a struct pointer with a field address
	OP_INDEX,      // size length: pop index and pointer, push the element address
	OP_INDEX_ARR,  // ref size: pop index, push the element address of an array variable
	OP_INDEX_BUF,  // size delta: pop index and a buffer field address, bounded by its length field
	OP_INCV,       // ref kind delta post stride
	OP_INCM,       // kind deref delta post stride size
	OP_COMPV,      // ref op kind size: compound assignment to a variable
	OP_COMPM,      // op kind deref size: compound assignment through an address
	OP_ADD,        // Binary operators, in the order of minic_binop
	OP_SUB,
	OP_MUL,
	OP_DIV,
	OP_MOD,
	OP_SHL,
	OP_SHR,
	OP_BAND,
	OP_BOR,
	OP_XOR,
	OP_EQ,
	OP_NE,
	OP_LT,
	OP_GT,
	OP_LE,
	OP_GE,
	OP_NEG,
	OP_NOT,
	OP_BNOT,
	OP_CAST,  // kind
	OP_TOPTR, // deref
	OP_JMP,   // target
	OP_JZ,    // target: pop, jump when false
	OP_JNZ,   // target: pop, jump when true
	OP_CALL,  // func argc
	OP_CALLN, // ext argc
	OP_FNPTR, // func
	OP_RET,   // kind deref
} minic_op_t;

static inline int minic_val_to_i(minic_val_t v) {
	return v.type == MINIC_T_INT ? v.i : (int)minic_val_to_d(v);
}

static minic_val_t minic_mem_load(void *p, minic_type_t kind, minic_type_t deref) {
	if (p == NULL) {
		return minic_val_int(0);
	}
	switch (kind) {
	case MINIC_T_PTR: {
		void *pointer;
		memcpy(&pointer, p, sizeof(pointer));
		return minic_val_typed_ptr(pointer, deref);
	}
	case MINIC_T_EMBED:
		return minic_val_typed_ptr(p, MINIC_T_EMBED);
	case MINIC_T_FLOAT: {
		float n;
		memcpy(&n, p, sizeof(n));
		return minic_val_float(n);
	}
	case MINIC_T_DOUBLE: {
		double n;
		memcpy(&n, p, sizeof(n));
		return minic_val_double(n);
	}
	case MINIC_T_BOOL:
		return minic_val_int(*(bool *)p);
	case MINIC_T_CHAR:
		return minic_val_int(*(minic_u8 *)p);
	case MINIC_T_VOID:
		return minic_val_int(0);
	default: {
		int32_t n;
		memcpy(&n, p, sizeof(n));
		return minic_val_int(n);
	}
	}
}

static void minic_mem_store(void *p, minic_val_t v, minic_type_t kind, int size) {
	if (p == NULL) {
		return;
	}
	switch (kind) {
	case MINIC_T_PTR: {
		void *pointer = minic_val_to_ptr(v);
		memcpy(p, &pointer, sizeof(pointer));
		break;
	}
	case MINIC_T_EMBED:
		if (v.type == MINIC_T_PTR && v.p != NULL) {
			memmove(p, v.p, size);
		}
		break;
	case MINIC_T_FLOAT: {
		float n = v.type == MINIC_T_FLOAT ? v.f : (float)minic_val_to_d(v);
		memcpy(p, &n, sizeof(n));
		break;
	}
	case MINIC_T_DOUBLE: {
		double n = minic_val_to_d(v);
		memcpy(p, &n, sizeof(n));
		break;
	}
	case MINIC_T_BOOL:
		*(bool *)p = minic_val_is_true(v);
		break;
	case MINIC_T_CHAR:
		*(minic_u8 *)p = (minic_u8)minic_val_to_i(v);
		break;
	default: {
		int32_t n = minic_val_to_i(v);
		memcpy(p, &n, sizeof(n));
		break;
	}
	}
}

// Variables live in minic_val_t slots whose tag is set when they are declared: int, char
// and bool use an INT tag, struct variables hold a pointer to their storage. The union
// is the variable's native storage, so '&x' points at it. MINIC_T_VOID is a variable
// typed by its first value.
static void minic_slot_store(minic_val_t *s, minic_val_t v, minic_type_t kind, int size) {
	switch (kind) {
	case MINIC_T_INT:
		s->i = minic_val_to_i(v);
		break;
	case MINIC_T_CHAR:
		s->i = (minic_u8)minic_val_to_i(v);
		break;
	case MINIC_T_BOOL:
		s->i = minic_val_is_true(v);
		break;
	case MINIC_T_FLOAT:
		s->f = v.type == MINIC_T_FLOAT ? v.f : (float)minic_val_to_d(v);
		break;
	case MINIC_T_DOUBLE:
		s->d = minic_val_to_d(v);
		break;
	case MINIC_T_PTR:
		s->p = minic_val_to_ptr(v);
		break;
	case MINIC_T_EMBED:
		if (v.type == MINIC_T_PTR && v.p != NULL && v.p != s->p) {
			memmove(s->p, v.p, size);
		}
		break;
	default: {
		minic_type_t deref = s->deref_type;
		*s                 = minic_val_cast(v, s->type);
		s->deref_type      = deref;
		break;
	}
	}
}

static void minic_slot_init(minic_val_t *s, minic_val_t v, minic_type_t kind, minic_type_t deref) {
	if (kind == MINIC_T_VOID) {
		*s = v;
		return;
	}
	s->type       = kind == MINIC_T_CHAR || kind == MINIC_T_BOOL ? MINIC_T_INT : kind;
	s->deref_type = deref;
	s->d          = 0.0;
	minic_slot_store(s, v, kind, 0);
}

static minic_val_t minic_arith(minic_val_t a, minic_val_t b, int op) {
	if (a.type == MINIC_T_INT && b.type == MINIC_T_INT) {
		unsigned int x = (unsigned int)a.i;
		unsigned int y = (unsigned int)b.i;
		switch (op) {
		case OP_ADD:
			return minic_val_int((int)(x + y));
		case OP_SUB:
			return minic_val_int((int)(x - y));
		case OP_MUL:
			return minic_val_int((int)(x * y));
		case OP_DIV:
			return minic_val_int(b.i == 0 ? 0 : b.i == -1 ? (int)(0u - x) : a.i / b.i);
		default:
			return minic_val_int(b.i == 0 || b.i == -1 ? 0 : a.i % b.i);
		}
	}
	if (a.type == MINIC_T_FLOAT && b.type == MINIC_T_FLOAT) {
		// Same results as the double path below: one float op rounds exactly like double then float
		switch (op) {
		case OP_ADD:
			return minic_val_float(a.f + b.f);
		case OP_SUB:
			return minic_val_float(a.f - b.f);
		case OP_MUL:
			return minic_val_float(a.f * b.f);
		case OP_DIV:
			return minic_val_float(b.f != 0.0f ? a.f / b.f : 0.0f);
		default:
			return minic_val_float(b.f != 0.0f ? fmodf(a.f, b.f) : 0.0f);
		}
	}
	// Determine result type (widening: int < float < double < ptr)
	minic_type_t rt;
	if (a.type == MINIC_T_PTR || b.type == MINIC_T_PTR) {
		rt = MINIC_T_PTR;
	}
	else if (a.type == MINIC_T_DOUBLE || b.type == MINIC_T_DOUBLE) {
		rt = MINIC_T_DOUBLE;
	}
	else if (a.type == MINIC_T_FLOAT || b.type == MINIC_T_FLOAT) {
		rt = MINIC_T_FLOAT;
	}
	else {
		rt = MINIC_T_INT;
	}
	double da = minic_val_to_d(a);
	double db = minic_val_to_d(b);
	double r;
	switch (op) {
	case OP_ADD:
		r = da + db;
		break;
	case OP_SUB:
		r = da - db;
		break;
	case OP_MUL:
		r = da * db;
		break;
	case OP_DIV:
		r = db != 0.0 ? da / db : 0.0;
		break;
	default:
		if (rt == MINIC_T_FLOAT || rt == MINIC_T_DOUBLE) {
			r = db != 0.0 ? fmod(da, db) : 0.0;
		}
		else {
			int ib = (int)db;
			r      = ib != 0 ? (double)((int)da % ib) : 0.0;
		}
		break;
	}
	return minic_val_coerce(r, rt);
}

static minic_val_t minic_binop(int op, minic_val_t a, minic_val_t b) {
	switch (op) {
	case OP_ADD:
	case OP_SUB:
	case OP_MUL:
	case OP_DIV:
	case OP_MOD:
		return minic_arith(a, b, op);
	case OP_SHL:
		return minic_val_int((int)((unsigned int)minic_val_to_i(a) << (minic_val_to_i(b) & 31)));
	case OP_SHR:
		return minic_val_int(minic_val_to_i(a) >> (minic_val_to_i(b) & 31));
	case OP_BAND:
		return minic_val_int(minic_val_to_i(a) & minic_val_to_i(b));
	case OP_BOR:
		return minic_val_int(minic_val_to_i(a) | minic_val_to_i(b));
	case OP_XOR:
		return minic_val_int(minic_val_to_i(a) ^ minic_val_to_i(b));
	case OP_EQ:
		return minic_val_int(minic_val_to_d(a) == minic_val_to_d(b));
	case OP_NE:
		return minic_val_int(minic_val_to_d(a) != minic_val_to_d(b));
	case OP_LT:
		return minic_val_int(minic_val_to_d(a) < minic_val_to_d(b));
	case OP_GT:
		return minic_val_int(minic_val_to_d(a) > minic_val_to_d(b));
	case OP_LE:
		return minic_val_int(minic_val_to_d(a) <= minic_val_to_d(b));
	default:
		return minic_val_int(minic_val_to_d(a) >= minic_val_to_d(b));
	}
}

static minic_val_t minic_step(minic_val_t old, int delta, int stride) {
	if (old.type == MINIC_T_PTR) {
		if (old.p != NULL) {
			old.p = (char *)old.p + delta * stride; // Keep the pointee type as well
		}
		return old;
	}
	if (old.type == MINIC_T_INT) {
		return minic_val_int((int)((unsigned int)old.i + (unsigned int)delta));
	}
	return minic_val_coerce(minic_val_to_d(old) + delta, old.type);
}

static minic_val_t minic_cast(minic_val_t v, minic_type_t kind) {
	switch (kind) {
	case MINIC_T_FLOAT:
	case MINIC_T_DOUBLE:
	case MINIC_T_PTR:
		return minic_val_cast(v, kind);
	case MINIC_T_CHAR:
		return minic_val_int((minic_u8)minic_val_to_i(v));
	case MINIC_T_BOOL:
		return minic_val_int(minic_val_is_true(v));
	case MINIC_T_EMBED:
		return v;
	default:
		return minic_val_cast(v, MINIC_T_INT);
	}
}

// ██████╗  ██████╗ ███╗   ███╗██████╗ ██╗██╗     ███████╗
// ██╔════╝██╔═══██╗████╗ ████║██╔══██╗██║██║     ██╔════╝
// ██║     ██║   ██║██╔████╔██║██████╔╝██║██║     █████╗
// ██║     ██║   ██║██║╚██╔╝██║██╔═══╝ ██║██║     ██╔══╝
// ╚██████╗╚██████╔╝██║ ╚═╝ ██║██║     ██║███████╗███████╗
//  ╚═════╝ ╚═════╝ ╚═╝     ╚═╝╚═╝     ╚═╝╚══════╝╚══════╝

typedef struct {
	char          name[MINIC_MAX_NAME];
	minic_ctype_t type;  // Element type for arrays, MINIC_T_VOID when typed by its first value
	int           ref;   // Frame slot, or -(global slot + 1)
	bool          array; // The slot holds the data pointer, the next one the element count
} minic_sym_t;

typedef struct {
	int breaks; // Chains of jump operands to patch, linked through the operands
	int continues;
} minic_loop_t;

typedef struct {
	minic_ctx_t   *ctx;
	minic_token_t *toks;
	int            i;
	bool           error;
	minic_sym_t   *locals;
	int            local_count;
	minic_sym_t   *globals;
	int            global_count;
	minic_func_t  *fn; // Function being compiled
	bool           in_main;
	int            depth; // Block depth, the top level of main() is 1
	int            slot_count;
	minic_loop_t  *loop;
} minic_comp_t;

typedef enum {
	MINIC_E_VALUE,  // On the stack
	MINIC_E_VAR,    // In a variable slot
	MINIC_E_MEM,    // At the address on the stack
	MINIC_E_FUNC,   // A function name
	MINIC_E_NEWVAR, // Unknown name about to be assigned, declares a variable
} minic_emode_t;

typedef struct {
	minic_emode_t     mode;
	minic_ctype_t     type; // MINIC_T_VOID when only known at run time
	minic_sym_t      *sym;
	int               length;    // Static element count of a decayed array field, else -1
	int               buf_delta; // '->buffer' field address to its 'length' field, 0 if none
	int               fn;        // Script function index, or -1 for a native
	minic_ext_func_t *ext;
	const char       *name;
} minic_cexpr_t;

void console_log(char *s);

static int minic_line_at(const char *src, int pos) {
	int line = 1;
	for (int i = 0; i < pos && src[i] != '\0'; i++) {
		if (src[i] == '\n') {
			line++;
		}
	}
	return line;
}

static minic_token_t *minic_tok(minic_comp_t *c) {
	return &c->toks[c->i];
}

static minic_tok_type_t minic_cur(minic_comp_t *c) {
	return c->toks[c->i].type;
}

static minic_tok_type_t minic_peek(minic_comp_t *c, int k) {
	int i = c->i;
	while (k-- > 0 && c->toks[i].type != TOK_EOF) {
		i++;
	}
	return c->toks[i].type;
}

static void minic_next(minic_comp_t *c) {
	if (c->toks[c->i].type != TOK_EOF) {
		c->i++;
	}
}

// Skip to the next 'stop' token outside of nested parentheses and braces
static void minic_skip_to(minic_comp_t *c, minic_tok_type_t stop) {
	int depth = 0;
	while (minic_cur(c) != TOK_EOF && !(minic_cur(c) == stop && depth == 0)) {
		depth += minic_cur(c) == TOK_LBRACE || minic_cur(c) == TOK_LPAREN;
		depth -= minic_cur(c) == TOK_RBRACE || minic_cur(c) == TOK_RPAREN;
		minic_next(c);
	}
}

static void minic_error(minic_comp_t *c, const char *fmt, ...) {
	if (c->error) {
		return;
	}
	char    msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	char log[512];
	snprintf(log, sizeof(log), "%s:%d: error: %s (got %s)", c->ctx->filename, minic_line_at(c->ctx->src_copy, minic_tok(c)->pos), msg,
	         minic_tok_names[minic_cur(c)]);
	console_log(log);
	c->error = true;
}

static void minic_expect(minic_comp_t *c, minic_tok_type_t expected) {
	if (minic_cur(c) != expected) {
		minic_error(c, "expected %s", minic_tok_names[expected]);
		return;
	}
	minic_next(c);
}

static void minic_emit_word(minic_comp_t *c, int word) {
	minic_ctx_t *ctx = c->ctx;
	if (ctx->code_len == ctx->code_cap) {
		ctx->code_cap = ctx->code_cap > 0 ? ctx->code_cap * 2 : 1024;
		ctx->code     = realloc(ctx->code, ctx->code_cap * sizeof(int));
		ctx->code_pos = realloc(ctx->code_pos, ctx->code_cap * sizeof(int));
	}
	ctx->code_pos[ctx->code_len] = minic_tok(c)->pos;
	ctx->code[ctx->code_len++]   = word;
}

static void minic_emit(minic_comp_t *c, int op, int n, ...) {
	minic_emit_word(c, op);
	va_list args;
	va_start(args, n);
	for (int k = 0; k < n; ++k) {
		minic_emit_word(c, va_arg(args, int));
	}
	va_end(args);
}

// Emit a jump, return its operand for minic_patch
static int minic_emit_jump(minic_comp_t *c, int op, int target) {
	minic_emit(c, op, 1, target);
	return c->ctx->code_len - 1;
}

// Point a chain of jump operands at the next instruction
static void minic_patch(minic_comp_t *c, int chain) {
	while (chain > 0) {
		int next            = c->ctx->code[chain];
		c->ctx->code[chain] = c->ctx->code_len;
		chain               = next;
	}
}

static int minic_const(minic_comp_t *c, minic_val_t v) {
	minic_ctx_t *ctx = c->ctx;
	if (ctx->const_count == ctx->const_cap) {
		ctx->const_cap = ctx->const_cap > 0 ? ctx->const_cap * 2 : 64;
		ctx->consts    = realloc(ctx->consts, ctx->const_cap * sizeof(minic_val_t));
	}
	ctx->consts[ctx->const_count] = v;
	return ctx->const_count++;
}

static void minic_emit_val(minic_comp_t *c, minic_val_t v) {
	if (v.type == MINIC_T_INT && v.deref_type == MINIC_T_INT) {
		minic_emit(c, OP_INT, 1, v.i);
	}
	else {
		minic_emit(c, OP_CONST, 1, minic_const(c, v));
	}
}

// On failure the index is unchanged. Opaque names are accepted in declaration
// contexts; expression contexts only recognize registered types.
static bool minic_parse_type(minic_comp_t *c, int *index, bool opaque, minic_ctype_t *type) {
	int i       = *index;
	*type       = (minic_ctype_t){0};
	type->deref = MINIC_T_PTR;
	if (minic_tok_is_type(c->toks[i].type)) {
		*type = minic_scalar_type(minic_tok_to_type(c->toks[i].type));
		i++;
	}
	else {
		bool tagged = c->toks[i].type == TOK_STRUCT;
		if (tagged) {
			i++;
		}
		if (c->toks[i].type != TOK_IDENT) {
			return false;
		}
		minic_struct_t *def     = minic_struct_get(c->ctx, c->toks[i].text);
		bool            integer = minic_is_int_typedef(c->toks[i].text);
		if (!tagged && def == NULL && !integer && !opaque) {
			return false;
		}
		*type     = minic_scalar_type(integer ? MINIC_T_INT : MINIC_T_EMBED);
		type->def = def;
		if (def != NULL) {
			type->size      = def->size;
			type->alignment = def->alignment;
		}
		i++;
	}
	while (c->toks[i].type == TOK_STAR) {
		*type = minic_pointer_type(*type);
		i++;
	}
	*index = i;
	return true;
}

// Type of a native call result, from its signature: "f(...)", or "p:struct_name(...)" for typed pointers
static minic_ctype_t minic_native_type(minic_comp_t *c, minic_ext_func_t *ext) {
	switch (ext->sig[0]) {
	case 'f':
		return minic_scalar_type(MINIC_T_FLOAT);
	case 'd':
		return minic_scalar_type(MINIC_T_DOUBLE);
	case 'i':
	case 'b':
	case 'c':
	case 'v':
		return minic_scalar_type(MINIC_T_INT);
	case 'p':
		break;
	default:
		return minic_scalar_type(MINIC_T_VOID); // Unknown until it returns
	}
	minic_ctype_t type = minic_scalar_type(MINIC_T_PTR);
	if (ext->sig[1] != ':') {
		return type;
	}
	char        name[MINIC_MAX_NAME];
	const char *start = ext->sig + 2;
	int         n     = 0;
	while (start[n] != '\0' && start[n] != '(' && start[n] != '*' && n < MINIC_MAX_NAME - 1) {
		name[n] = start[n];
		n++;
	}
	name[n]              = '\0';
	minic_ctype_t target = minic_scalar_type(MINIC_T_EMBED);
	target.def           = minic_struct_get(c->ctx, name);
	for (size_t k = 0; k < sizeof(minic_keywords) / sizeof(minic_keywords[0]); ++k) {
		if (strcmp(name, minic_keywords[k].kw) == 0 && minic_tok_is_type(minic_keywords[k].tok)) {
			target = minic_scalar_type(minic_tok_to_type(minic_keywords[k].tok));
		}
	}
	if (minic_is_int_typedef(name)) {
		target = minic_scalar_type(MINIC_T_INT);
	}
	if (target.def != NULL) {
		target.size      = target.def->size;
		target.alignment = target.def->alignment;
	}
	type = minic_pointer_type(target);
	for (const char *p = start + n; *p == '*'; ++p) {
		type = minic_pointer_type(type);
	}
	return type;
}

static int minic_func_index(minic_ctx_t *ctx, const char *name) {
	for (int i = 0; i < ctx->func_count; ++i) {
		if (strcmp(ctx->funcs[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

static minic_sym_t *minic_sym_find(minic_comp_t *c, const char *name) {
	for (int i = c->local_count - 1; i >= 0; --i) {
		if (c->locals[i].name[0] == name[0] && strcmp(c->locals[i].name, name) == 0) {
			return &c->locals[i];
		}
	}
	for (int i = c->global_count - 1; i >= 0; --i) {
		if (c->globals[i].name[0] == name[0] && strcmp(c->globals[i].name, name) == 0) {
			return &c->globals[i];
		}
	}
	return NULL;
}

// Globals are the top-level declarations plus the top level of main(), which every function sees
static minic_sym_t *minic_declare(minic_comp_t *c, const char *name, minic_ctype_t type, bool array) {
	int          slots  = array ? 2 : 1;
	bool         global = c->fn == &c->ctx->init || (c->in_main && c->depth == 1);
	minic_sym_t *sym;
	if (global) {
		if (c->global_count >= MINIC_MAX_GLOBAL_VARS) {
			minic_error(c, "too many global variables (max %d), cannot declare '%s'", MINIC_MAX_GLOBAL_VARS, name);
			return NULL;
		}
		sym      = &c->globals[c->global_count++];
		sym->ref = -(c->ctx->global_count + 1);
		c->ctx->global_count += slots;
	}
	else {
		if (c->local_count >= MINIC_MAX_VARS) {
			minic_error(c, "too many local variables (max %d), cannot declare '%s'", MINIC_MAX_VARS, name);
			return NULL;
		}
		sym      = &c->locals[c->local_count++];
		sym->ref = c->slot_count;
		c->slot_count += slots;
		if (c->slot_count > c->fn->slot_count) {
			c->fn->slot_count = c->slot_count;
		}
	}
	strncpy(sym->name, name, MINIC_MAX_NAME - 1);
	sym->name[MINIC_MAX_NAME - 1] = '\0';
	sym->type                     = type;
	sym->array                    = array;
	return sym;
}

// Initialize a declared variable, from the value on the stack when there is one
static void minic_init_var(minic_comp_t *c, minic_sym_t *sym, bool has_value) {
	minic_ctype_t type = sym->type;
	if (type.kind == MINIC_T_EMBED) {
		if (type.size <= 0) {
			minic_error(c, "incomplete struct type for '%s'", sym->name);
			return;
		}
		minic_emit(c, OP_INIT_EMBED, 3, sym->ref, type.size, type.alignment);
		if (has_value) {
			minic_emit(c, OP_STOREV, 3, sym->ref, MINIC_T_EMBED, type.size);
			minic_emit(c, OP_POP, 0);
		}
		return;
	}
	if (!has_value) {
		minic_emit(c, OP_INT, 1, 0);
	}
	minic_emit(c, OP_INITV, 3, sym->ref, type.kind, minic_load_deref(type));
}

static void minic_scope_push(minic_comp_t *c, int *saved) {
	saved[0] = c->local_count;
	saved[1] = c->slot_count;
	c->depth++;
}

static void minic_scope_pop(minic_comp_t *c, int *saved) {
	c->local_count = saved[0];
	c->slot_count  = saved[1];
	c->depth--;
}

static minic_cexpr_t minic_c_assign(minic_comp_t *c);
static minic_cexpr_t minic_c_unary(minic_comp_t *c);
static minic_cexpr_t minic_c_ternary(minic_comp_t *c);
static void          minic_c_stmt(minic_comp_t *c);

static minic_cexpr_t minic_expr(minic_emode_t mode, minic_ctype_t type) {
	minic_cexpr_t r = {0};
	r.mode          = mode;
	r.type          = type;
	r.length        = -1;
	r.fn            = -1;
	return r;
}

// Turn an expression into a value on the stack
static minic_cexpr_t minic_c_load(minic_comp_t *c, minic_cexpr_t e) {
	switch (e.mode) {
	case MINIC_E_VAR:
		minic_emit(c, OP_LOADV, 1, e.sym->ref);
		break;
	case MINIC_E_MEM:
		if (e.type.kind != MINIC_T_EMBED) { // Struct storage is its own address
			minic_emit(c, OP_LOADM, 2, e.type.kind, minic_load_deref(e.type));
		}
		break;
	case MINIC_E_FUNC:
		if (e.fn < 0) {
			minic_error(c, "native function '%s' cannot be used as a value", e.name);
		}
		minic_emit(c, OP_FNPTR, 1, e.fn);
		e.type = minic_scalar_type(MINIC_T_PTR);
		break;
	case MINIC_E_NEWVAR:
		minic_error(c, "unknown identifier '%s'", e.name);
		break;
	default:
		break;
	}
	e.mode      = MINIC_E_VALUE;
	e.buf_delta = 0;
	return e;
}

static minic_cexpr_t minic_c_value(minic_comp_t *c) {
	return minic_c_load(c, minic_c_assign(c));
}

static minic_cexpr_t minic_c_call(minic_comp_t *c, minic_cexpr_t f) {
	minic_next(c); // Consume '('
	int argc = 0;
	while (minic_cur(c) != TOK_RPAREN && minic_cur(c) != TOK_EOF && !c->error) {
		minic_c_value(c);
		argc++;
		if (minic_cur(c) == TOK_COMMA) {
			minic_next(c);
		}
		else if (minic_cur(c) != TOK_RPAREN) {
			minic_error(c, "expected ',' or ')' in call to '%s'", f.name);
		}
	}
	minic_expect(c, TOK_RPAREN);
	if (argc > MINIC_MAX_ARGS) {
		minic_error(c, "too many arguments (max %d)", MINIC_MAX_ARGS);
	}
	if (f.fn >= 0) {
		minic_func_t *fn = &c->ctx->funcs[f.fn];
		if (argc != fn->param_count) {
			minic_error(c, "'%s' expects %d arguments, got %d", fn->name, fn->param_count, argc);
		}
		minic_emit(c, OP_CALL, 2, f.fn, argc);
		minic_ctype_t type = fn->ret_type.kind == MINIC_T_VOID ? minic_scalar_type(MINIC_T_INT) : fn->ret_type;
		return minic_expr(MINIC_E_VALUE, type);
	}
	const char *open = strchr(f.ext->sig, '(');
	if (open != NULL && strstr(f.ext->sig, "...") == NULL) {
		int count = open[1] == ')' ? 0 : 1;
		for (const char *p = open + 1; *p != '\0' && *p != ')'; ++p) {
			count += *p == ',';
		}
		if (argc != count) {
			minic_error(c, "'%s' expects %d arguments, got %d", f.name, count, argc);
		}
	}
	minic_emit(c, OP_CALLN, 2, (int)(f.ext - minic_ext_funcs), argc);
	return minic_expr(MINIC_E_VALUE, minic_native_type(c, f.ext));
}

static minic_cexpr_t minic_c_sizeof(minic_comp_t *c) {
	minic_expect(c, TOK_LPAREN);
	minic_ctype_t type;
	int           i = c->i;
	if (minic_parse_type(c, &i, false, &type)) {
		c->i = i;
		minic_emit(c, OP_INT, 1, type.size);
	}
	else {
		minic_sym_t *sym = minic_cur(c) == TOK_IDENT ? minic_sym_find(c, minic_tok(c)->text) : NULL;
		minic_expect(c, TOK_IDENT);
		if (sym != NULL && sym->array) {
			minic_emit(c, OP_LOADV, 1, sym->ref >= 0 ? sym->ref + 1 : sym->ref - 1); // Element count
			minic_emit(c, OP_INT, 1, sym->type.size);
			minic_emit(c, OP_MUL, 0);
		}
		else {
			minic_emit(c, OP_INT, 1, sym != NULL ? sym->type.size : 0);
		}
	}
	minic_expect(c, TOK_RPAREN);
	return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
}

static minic_cexpr_t minic_c_primary(minic_comp_t *c) {
	minic_token_t *t = minic_tok(c);
	if (t->type == TOK_NUMBER || t->type == TOK_CHAR_LIT || t->type == TOK_STR_LIT) {
		minic_next(c);
		minic_emit_val(c, t->val);
		bool string = t->type == TOK_STR_LIT;
		return minic_expr(MINIC_E_VALUE, string ? minic_pointer_type(minic_scalar_type(MINIC_T_CHAR)) : minic_scalar_type(t->val.type));
	}
	if (t->type == TOK_IDENT) {
		const char *name = t->text;
		minic_next(c);
		if (strcmp(name, "sizeof") == 0) {
			return minic_c_sizeof(c);
		}
		minic_cexpr_t r = minic_expr(MINIC_E_FUNC, minic_scalar_type(MINIC_T_PTR));
		r.name          = name;
		if (minic_cur(c) == TOK_LPAREN) {
			r.fn  = minic_func_index(c->ctx, name);
			r.ext = r.fn < 0 ? minic_ext_func_get(name) : NULL;
			if (r.fn < 0 && r.ext == NULL) {
				minic_error(c, "unknown function '%s'", name);
			}
			if (r.fn >= 0 && c->ctx->funcs[r.fn].body < 0) {
				minic_error(c, "function '%s' is declared but not defined", name);
			}
			return r;
		}
		minic_sym_t *sym = minic_sym_find(c, name);
		if (sym != NULL) {
			r.mode = MINIC_E_VAR;
			r.sym  = sym;
			r.type = sym->array ? minic_pointer_type(sym->type) : sym->type;
			return r;
		}
		r.fn = minic_func_index(c->ctx, name);
		if (r.fn >= 0) {
			return r; // A script function passed as a callback
		}
		int ec = minic_enum_const_find(name);
		if (ec >= 0) {
			minic_emit(c, OP_INT, 1, minic_enum_const_value_at(ec));
			return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
		}
		minic_tok_type_t next = minic_cur(c);
		if (next == TOK_ASSIGN) {
			r.mode = MINIC_E_NEWVAR;
			return r;
		}
		minic_type_t kind;
		const void  *host = minic_global_ptr(name, &kind);
		if (host != NULL && next != TOK_INC && next != TOK_DEC && (next < TOK_PLUS_ASSIGN || next > TOK_XOR_ASSIGN)) {
			minic_emit(c, OP_LOADH, 1, minic_const(c, minic_val_typed_ptr((void *)host, kind)));
			return minic_expr(MINIC_E_VALUE, minic_scalar_type(kind));
		}
		minic_error(c, "unknown identifier '%s'", name);
		return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
	}
	if (t->type == TOK_LPAREN) {
		minic_ctype_t type;
		int           i = c->i + 1;
		if (minic_parse_type(c, &i, false, &type) && c->toks[i].type == TOK_RPAREN) {
			c->i            = i + 1;
			minic_cexpr_t v = minic_c_load(c, minic_c_unary(c));
			if (type.pointer) {
				minic_emit(c, OP_TOPTR, 1, type.deref);
			}
			else if (type.def == NULL && v.type.kind != type.kind) {
				minic_emit(c, OP_CAST, 1, type.kind);
			}
			return minic_expr(MINIC_E_VALUE, type);
		}
		minic_next(c);
		minic_cexpr_t r = minic_c_assign(c);
		minic_expect(c, TOK_RPAREN);
		return r;
	}
	minic_error(c, "expected expression");
	return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
}

static minic_cexpr_t minic_c_field(minic_comp_t *c, minic_cexpr_t owner, const char *name) {
	minic_struct_t *def = owner.type.def;
	if (def == NULL) {
		minic_error(c, "member access requires a known struct type");
		return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
	}
	int idx = minic_struct_field_idx(def, name);
	if (idx < 0) {
		minic_error(c, "struct '%s' has no field '%s'", def->name, name);
		return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
	}
	minic_c_load(c, owner); // The struct pointer
	minic_emit(c, OP_FIELD, 2, def->offsets[idx], (int)(def - c->ctx->structs) * MINIC_MAX_STRUCT_FIELDS + idx);
	minic_ctype_t type = minic_field_type(c->ctx, def, idx);
	if (def->counts[idx] > 0) {
		minic_cexpr_t r = minic_expr(MINIC_E_VALUE, minic_pointer_type(type));
		r.length        = def->counts[idx];
		return r;
	}
	minic_cexpr_t r = minic_expr(MINIC_E_MEM, type);
	int           l = strcmp(name, "buffer") == 0 ? minic_struct_field_idx(def, "length") : -1;
	if (l >= 0 && def->offsets[l] != def->offsets[idx]) {
		r.buf_delta = def->offsets[l] - def->offsets[idx]; // Indexing checks against the array's length
	}
	return r;
}

static minic_cexpr_t minic_c_index(minic_comp_t *c, minic_cexpr_t owner) {
	if (owner.mode == MINIC_E_VAR && owner.sym->array) {
		minic_c_value(c);
		minic_emit(c, OP_INDEX_ARR, 2, owner.sym->ref, owner.sym->type.size);
		return minic_expr(MINIC_E_MEM, owner.sym->type);
	}
	minic_ctype_t element = minic_element_type(owner.type);
	if (owner.mode == MINIC_E_MEM && owner.buf_delta != 0) {
		minic_c_value(c);
		minic_emit(c, OP_INDEX_BUF, 2, element.size, owner.buf_delta);
		return minic_expr(MINIC_E_MEM, element);
	}
	owner = minic_c_load(c, owner);
	minic_c_value(c);
	minic_emit(c, OP_INDEX, 2, element.size, owner.length);
	return minic_expr(MINIC_E_MEM, element);
}

static minic_cexpr_t minic_c_increment(minic_comp_t *c, minic_cexpr_t r, int delta, bool post) {
	int stride = r.type.kind == MINIC_T_PTR ? minic_element_type(r.type).size : 0;
	if (r.mode == MINIC_E_VAR && !r.sym->array) {
		minic_emit(c, OP_INCV, 5, r.sym->ref, r.sym->type.kind, delta, post, stride);
	}
	else if (r.mode == MINIC_E_MEM) {
		minic_emit(c, OP_INCM, 6, r.type.kind, minic_load_deref(r.type), delta, post, stride, r.type.size);
	}
	else {
		minic_error(c, "expression is not writable");
	}
	return minic_expr(MINIC_E_VALUE, r.type);
}

static minic_cexpr_t minic_c_postfix(minic_comp_t *c) {
	minic_cexpr_t r = minic_c_primary(c);
	while (!c->error) {
		minic_tok_type_t t = minic_cur(c);
		if (t == TOK_LPAREN) {
			if (r.mode != MINIC_E_FUNC) {
				minic_error(c, "expression is not callable");
				break;
			}
			r = minic_c_call(c, r);
		}
		else if (t == TOK_DOT || t == TOK_ARROW) {
			minic_next(c);
			const char *field = minic_tok(c)->text;
			minic_expect(c, TOK_IDENT);
			if (!c->error) {
				r = minic_c_field(c, r, field);
			}
		}
		else if (t == TOK_LBRACKET) {
			minic_next(c);
			r = minic_c_index(c, r);
			minic_expect(c, TOK_RBRACKET);
		}
		else if (t == TOK_INC || t == TOK_DEC) {
			minic_next(c);
			r = minic_c_increment(c, r, t == TOK_INC ? 1 : -1, true);
		}
		else {
			break;
		}
	}
	return r;
}

static minic_cexpr_t minic_c_unary(minic_comp_t *c) {
	minic_tok_type_t op = minic_cur(c);
	if (op != TOK_AMP && op != TOK_STAR && op != TOK_MINUS && op != TOK_NOT && op != TOK_BITNOT && op != TOK_INC && op != TOK_DEC) {
		return minic_c_postfix(c);
	}
	minic_next(c);
	minic_cexpr_t r = minic_c_unary(c);
	switch (op) {
	case TOK_AMP: {
		minic_ctype_t type = minic_pointer_type(r.type);
		if (r.mode == MINIC_E_VAR && r.sym->array) {
			return minic_c_load(c, r); // Arrays already decay to the address of their first element
		}
		if (r.mode == MINIC_E_VAR) {
			if (r.sym->type.kind == MINIC_T_EMBED) {
				minic_emit(c, OP_LOADV, 1, r.sym->ref); // The slot holds the storage address
			}
			else {
				minic_emit(c, OP_ADDRV, 2, r.sym->ref, r.sym->type.kind);
			}
			return minic_expr(MINIC_E_VALUE, type);
		}
		if (r.mode == MINIC_E_MEM) {
			return minic_expr(MINIC_E_VALUE, type); // The address is already on the stack
		}
		if (r.mode == MINIC_E_VALUE && r.length >= 0) {
			return r;
		}
		minic_error(c, "expression has no address");
		return r;
	}
	case TOK_STAR:
		r = minic_c_load(c, r);
		return minic_expr(MINIC_E_MEM, minic_element_type(r.type));
	case TOK_INC:
	case TOK_DEC:
		return minic_c_increment(c, r, op == TOK_INC ? 1 : -1, false);
	case TOK_MINUS:
		r = minic_c_load(c, r);
		minic_emit(c, OP_NEG, 0);
		r.length = -1;
		return r;
	case TOK_NOT:
		minic_c_load(c, r);
		minic_emit(c, OP_NOT, 0);
		return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
	default:
		minic_c_load(c, r);
		minic_emit(c, OP_BNOT, 0);
		return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
	}
}

// C precedence, loosest first, every level is left-associative. Compound assignments
// have no precedence but share the opcode of their operator.
#define MINIC_PREC_MAX 10
static const struct {
	int prec;
	int op;
} minic_binops[TOK_EOF + 1] = {
    [TOK_OR]           = {1, 0},
    [TOK_AND]          = {2, 0},
    [TOK_BITOR]        = {3, OP_BOR},
    [TOK_XOR]          = {4, OP_XOR},
    [TOK_AMP]          = {5, OP_BAND},
    [TOK_EQ]           = {6, OP_EQ},
    [TOK_NEQ]          = {6, OP_NE},
    [TOK_LT]           = {7, OP_LT},
    [TOK_GT]           = {7, OP_GT},
    [TOK_LE]           = {7, OP_LE},
    [TOK_GE]           = {7, OP_GE},
    [TOK_SHL]          = {8, OP_SHL},
    [TOK_SHR]          = {8, OP_SHR},
    [TOK_PLUS]         = {9, OP_ADD},
    [TOK_MINUS]        = {9, OP_SUB},
    [TOK_STAR]         = {10, OP_MUL},
    [TOK_SLASH]        = {10, OP_DIV},
    [TOK_PERCENT]      = {10, OP_MOD},
    [TOK_PLUS_ASSIGN]  = {0, OP_ADD},
    [TOK_MINUS_ASSIGN] = {0, OP_SUB},
    [TOK_MUL_ASSIGN]   = {0, OP_MUL},
    [TOK_DIV_ASSIGN]   = {0, OP_DIV},
    [TOK_MOD_ASSIGN]   = {0, OP_MOD},
    [TOK_SHL_ASSIGN]   = {0, OP_SHL},
    [TOK_SHR_ASSIGN]   = {0, OP_SHR},
    [TOK_AND_ASSIGN]   = {0, OP_BAND},
    [TOK_OR_ASSIGN]    = {0, OP_BOR},
    [TOK_XOR_ASSIGN]   = {0, OP_XOR},
};

// Static result type of arithmetic, following the widening in minic_arith
static minic_ctype_t minic_arith_type(minic_ctype_t a, minic_ctype_t b) {
	if (a.kind == MINIC_T_VOID || b.kind == MINIC_T_VOID) {
		return minic_scalar_type(MINIC_T_VOID);
	}
	if (a.kind == MINIC_T_PTR || b.kind == MINIC_T_PTR || a.kind == MINIC_T_EMBED || b.kind == MINIC_T_EMBED) {
		return minic_scalar_type(MINIC_T_PTR);
	}
	if (a.kind == MINIC_T_DOUBLE || b.kind == MINIC_T_DOUBLE) {
		return minic_scalar_type(MINIC_T_DOUBLE);
	}
	if (a.kind == MINIC_T_FLOAT || b.kind == MINIC_T_FLOAT) {
		return minic_scalar_type(MINIC_T_FLOAT);
	}
	return minic_scalar_type(MINIC_T_INT);
}

static minic_cexpr_t minic_c_binary(minic_comp_t *c, int level) {
	if (level > MINIC_PREC_MAX) {
		return minic_c_unary(c);
	}
	minic_cexpr_t r = minic_c_binary(c, level + 1);
	while (!c->error && minic_binops[minic_cur(c)].prec == level) {
		minic_tok_type_t op = minic_cur(c);
		r                   = minic_c_load(c, r);
		minic_next(c);
		if (op == TOK_AND || op == TOK_OR) {
			// Short-circuit: the right side only runs when it decides the result
			int jump = op == TOK_AND ? OP_JZ : OP_JNZ;
			int j1   = minic_emit_jump(c, jump, 0);
			minic_c_load(c, minic_c_binary(c, level + 1));
			int j2 = minic_emit_jump(c, jump, j1);
			minic_emit(c, OP_INT, 1, op == TOK_AND);
			int end = minic_emit_jump(c, OP_JMP, 0);
			minic_patch(c, j2);
			minic_emit(c, OP_INT, 1, op != TOK_AND);
			minic_patch(c, end);
			r = minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT));
			continue;
		}
		minic_cexpr_t b = minic_c_load(c, minic_c_binary(c, level + 1));
		int           o = minic_binops[op].op;
		minic_emit(c, o, 0);
		r = minic_expr(MINIC_E_VALUE, o <= OP_MOD ? minic_arith_type(r.type, b.type) : minic_scalar_type(MINIC_T_INT));
	}
	return r;
}

static minic_cexpr_t minic_c_ternary(minic_comp_t *c) {
	minic_cexpr_t r = minic_c_binary(c, 1);
	if (c->error || minic_cur(c) != TOK_QUESTION) {
		return r;
	}
	minic_c_load(c, r);
	minic_next(c); // Consume '?'
	int           skip = minic_emit_jump(c, OP_JZ, 0);
	minic_cexpr_t a    = minic_c_value(c);
	minic_expect(c, TOK_COLON);
	int end = minic_emit_jump(c, OP_JMP, 0);
	minic_patch(c, skip);
	minic_cexpr_t b = minic_c_load(c, minic_c_ternary(c));
	minic_patch(c, end);
	return minic_expr(MINIC_E_VALUE, a.type.kind == b.type.kind ? a.type : minic_scalar_type(MINIC_T_VOID));
}

static minic_cexpr_t minic_c_assign(minic_comp_t *c) {
	minic_cexpr_t    target = minic_c_ternary(c);
	minic_tok_type_t op     = minic_cur(c);
	if (c->error || (op != TOK_ASSIGN && (op < TOK_PLUS_ASSIGN || op > TOK_XOR_ASSIGN))) {
		return target;
	}
	minic_next(c);
	if (target.mode == MINIC_E_NEWVAR) {
		// A plain store to an unknown name declares a variable typed by the value
		minic_cexpr_t v   = minic_c_value(c);
		minic_sym_t  *sym = minic_declare(c, target.name, v.type, false);
		if (sym != NULL) {
			minic_emit(c, OP_DUP, 0);
			minic_init_var(c, sym, true);
		}
		return v;
	}
	bool          var = target.mode == MINIC_E_VAR && !target.sym->array;
	minic_ctype_t t   = target.type;
	if (!var && target.mode != MINIC_E_MEM) {
		minic_error(c, "expression is not writable");
		return target;
	}
	minic_cexpr_t v = minic_c_value(c);
	if (op == TOK_ASSIGN) {
		if (var) {
			minic_emit(c, OP_STOREV, 3, target.sym->ref, t.kind, t.size);
		}
		else {
			minic_emit(c, OP_STOREM, 2, t.kind, t.size);
		}
		return v;
	}
	// The old value is read after the right side runs
	if (var) {
		minic_emit(c, OP_COMPV, 4, target.sym->ref, minic_binops[op].op, t.kind, t.size);
	}
	else {
		minic_emit(c, OP_COMPM, 4, minic_binops[op].op, t.kind, minic_load_deref(t), t.size);
	}
	return minic_expr(MINIC_E_VALUE, t);
}

// Count the top-level elements of the brace initializer that starts at token 'start'
static int minic_init_list_count(minic_comp_t *c, int start) {
	int  depth   = 0;
	int  count   = 0;
	bool in_elem = false;
	for (int i = start; c->toks[i].type != TOK_EOF; ++i) {
		minic_tok_type_t t = c->toks[i].type;
		if (t == TOK_RBRACE && --depth == 0) {
			break;
		}
		if (depth == 1) {
			if (t == TOK_COMMA) {
				in_elem = false; // The next token starts another element
			}
			else if (!in_elem) {
				in_elem = true; // First token of an element, a nested '{' included
				count++;
			}
		}
		if (t == TOK_LBRACE) {
			depth++;
		}
	}
	return count;
}

// Local declarations and globals use the same allocation and initialization path.
static void minic_c_decl(minic_comp_t *c, minic_ctype_t type) {
	minic_ctype_t base = type;
	while (base.pointer > 0) {
		base = minic_element_type(base);
	}
	for (;;) {
		const char *name = minic_tok(c)->text;
		minic_expect(c, TOK_IDENT);
		if (c->error) {
			return;
		}
		if (minic_cur(c) == TOK_LBRACKET) {
			minic_next(c); // Consume '['
			bool sized = minic_cur(c) != TOK_RBRACKET;
			if (sized) {
				minic_c_value(c);
			}
			minic_expect(c, TOK_RBRACKET);
			bool listed = minic_cur(c) == TOK_ASSIGN && minic_peek(c, 1) == TOK_LBRACE;
			if (!sized) { // 'name[]' takes its size from the initializer
				minic_emit(c, OP_INT, 1, listed ? minic_init_list_count(c, c->i + 1) : 0);
			}
			if (type.size <= 0) {
				minic_error(c, "invalid array element type for '%s'", name);
				return;
			}
			minic_sym_t *sym = minic_declare(c, name, type, true);
			if (sym == NULL) {
				return;
			}
			minic_emit(c, OP_INIT_ARR, 4, sym->ref, type.kind, type.size, type.alignment);
			if (listed) {
				minic_next(c); // Consume '='
				minic_next(c); // Consume '{'
				for (int i = 0; minic_cur(c) != TOK_RBRACE && !c->error; ++i) {
					minic_emit(c, OP_INT, 1, i);
					minic_emit(c, OP_INDEX_ARR, 2, sym->ref, type.size);
					minic_c_value(c);
					minic_emit(c, OP_STOREM, 2, type.kind, type.size);
					minic_emit(c, OP_POP, 0);
					if (minic_cur(c) != TOK_COMMA) {
						break;
					}
					minic_next(c); // Consume ','
				}
				minic_expect(c, TOK_RBRACE);
			}
		}
		else {
			bool initialized = minic_cur(c) == TOK_ASSIGN;
			if (initialized) {
				minic_next(c);
				minic_c_value(c);
			}
			minic_sym_t *sym = minic_declare(c, name, type, false);
			if (sym == NULL) {
				return;
			}
			minic_init_var(c, sym, initialized);
		}
		if (minic_cur(c) != TOK_COMMA || c->error) {
			break;
		}
		minic_next(c); // 'int *a, b' declares an int b
		type = base;
		while (minic_cur(c) == TOK_STAR) {
			type = minic_pointer_type(type);
			minic_next(c);
		}
	}
	minic_expect(c, TOK_SEMICOLON);
}

// Recognize opaque pointer declarations without mistaking 'value * value' for a type.
static bool minic_decl_type(minic_comp_t *c, minic_ctype_t *type) {
	int i = c->i;
	if (minic_parse_type(c, &i, false, type)) {
		c->i = i;
		return true;
	}
	if (minic_cur(c) == TOK_IDENT && minic_sym_find(c, minic_tok(c)->text) == NULL) {
		if (minic_parse_type(c, &i, true, type) && type->pointer && c->toks[i].type == TOK_IDENT) {
			c->i = i;
			return true;
		}
	}
	return false;
}

// A statement in its own scope, the body of a control statement
static void minic_c_body(minic_comp_t *c) {
	int saved[2];
	minic_scope_push(c, saved);
	minic_c_stmt(c);
	minic_scope_pop(c, saved);
}

static void minic_c_block(minic_comp_t *c) {
	int saved[2];
	minic_scope_push(c, saved);
	minic_expect(c, TOK_LBRACE);
	while (minic_cur(c) != TOK_RBRACE && minic_cur(c) != TOK_EOF && !c->error) {
		minic_c_stmt(c);
	}
	minic_expect(c, TOK_RBRACE);
	minic_scope_pop(c, saved);
}

static void minic_c_loop_body(minic_comp_t *c, minic_loop_t *loop) {
	minic_loop_t *outer = c->loop;
	c->loop             = loop;
	minic_c_body(c);
	c->loop = outer;
}

static void minic_c_stmt(minic_comp_t *c) {
	minic_tok_type_t t = minic_cur(c);
	if (t == TOK_LBRACE) {
		minic_c_block(c);
		return;
	}
	if (t == TOK_SEMICOLON) {
		minic_next(c);
		return;
	}
	// Skip bare typedef declarations inside function bodies
	if (t == TOK_TYPEDEF) {
		minic_skip_to(c, TOK_SEMICOLON);
		minic_next(c);
		return;
	}

	minic_ctype_t type;
	if (minic_decl_type(c, &type)) {
		minic_c_decl(c, type);
		return;
	}

	if (t == TOK_RETURN) {
		minic_next(c);
		if (minic_cur(c) == TOK_SEMICOLON) {
			minic_emit(c, OP_INT, 1, 0);
		}
		else {
			minic_c_value(c);
		}
		minic_emit(c, OP_RET, 2, c->fn->ret_type.kind, minic_load_deref(c->fn->ret_type));
		minic_expect(c, TOK_SEMICOLON);
		return;
	}

	if (t == TOK_IF) {
		minic_next(c);
		minic_expect(c, TOK_LPAREN);
		minic_c_value(c);
		minic_expect(c, TOK_RPAREN);
		int skip = minic_emit_jump(c, OP_JZ, 0);
		minic_c_body(c);
		if (minic_cur(c) == TOK_ELSE) {
			minic_next(c);
			int end = minic_emit_jump(c, OP_JMP, 0);
			minic_patch(c, skip);
			minic_c_body(c);
			minic_patch(c, end);
		}
		else {
			minic_patch(c, skip);
		}
		return;
	}

	if (t == TOK_FOR) {
		int saved[2];
		minic_scope_push(c, saved); // The loop variable goes out of scope after the loop
		minic_next(c);
		minic_expect(c, TOK_LPAREN);
		if (minic_decl_type(c, &type)) {
			minic_c_decl(c, type);
		}
		else {
			if (minic_cur(c) != TOK_SEMICOLON) {
				minic_c_value(c);
				minic_emit(c, OP_POP, 0);
			}
			minic_expect(c, TOK_SEMICOLON);
		}
		minic_loop_t loop = {0};
		int          top  = c->ctx->code_len;
		if (minic_cur(c) != TOK_SEMICOLON) {
			minic_c_value(c);
			loop.breaks = minic_emit_jump(c, OP_JZ, 0);
		}
		minic_expect(c, TOK_SEMICOLON);
		// The increment runs after the body, compile it there
		int step = c->i;
		minic_skip_to(c, TOK_RPAREN);
		minic_expect(c, TOK_RPAREN);
		minic_c_loop_body(c, &loop);
		minic_patch(c, loop.continues);
		int after = c->i;
		c->i      = step;
		if (minic_cur(c) != TOK_RPAREN) {
			minic_c_value(c);
			minic_emit(c, OP_POP, 0);
		}
		c->i = after;
		minic_emit(c, OP_JMP, 1, top);
		minic_patch(c, loop.breaks);
		minic_scope_pop(c, saved);
		return;
	}

	if (t == TOK_WHILE) {
		minic_next(c);
		minic_loop_t loop = {0};
		int          top  = c->ctx->code_len;
		minic_expect(c, TOK_LPAREN);
		minic_c_value(c);
		minic_expect(c, TOK_RPAREN);
		loop.breaks = minic_emit_jump(c, OP_JZ, 0);
		minic_c_loop_body(c, &loop);
		minic_patch(c, loop.continues);
		minic_emit(c, OP_JMP, 1, top);
		minic_patch(c, loop.breaks);
		return;
	}

	if (t == TOK_BREAK || t == TOK_CONTINUE) {
		if (c->loop == NULL) {
			minic_error(c, "%s outside a loop", minic_tok_names[t]);
			return;
		}
		minic_next(c);
		int *chain = t == TOK_BREAK ? &c->loop->breaks : &c->loop->continues;
		*chain     = minic_emit_jump(c, OP_JMP, *chain);
		minic_expect(c, TOK_SEMICOLON);
		return;
	}

	minic_c_value(c);
	minic_emit(c, OP_POP, 0);
	minic_expect(c, TOK_SEMICOLON);
}

static void minic_c_function(minic_comp_t *c, minic_func_t *fn) {
	c->fn          = fn;
	c->in_main     = strcmp(fn->name, "main") == 0;
	c->depth       = 0;
	c->local_count = 0;
	c->slot_count  = 0;
	c->loop        = NULL;
	fn->entry      = c->ctx->code_len;
	c->i           = fn->body;
	// Arguments arrive in the first slots, give them the parameter types
	for (int i = 0; i < fn->param_count; ++i) {
		minic_sym_t *sym = minic_declare(c, fn->params[i], fn->param_types[i], false);
		if (sym == NULL) {
			return;
		}
		minic_emit(c, OP_LOADV, 1, sym->ref);
		minic_init_var(c, sym, true);
	}
	minic_c_block(c);
	minic_emit(c, OP_INT, 1, 0);
	minic_emit(c, OP_RET, 2, fn->ret_type.kind, minic_load_deref(fn->ret_type));
}

// Constant integer expression of an enum value: literals, earlier enum constants and operators
static int minic_const_expr(minic_token_t *toks, int *i, int level) {
	if (level > MINIC_PREC_MAX) {
		minic_token_t *t = &toks[*i];
		if (t->type == TOK_EOF) {
			return 0;
		}
		(*i)++;
		switch (t->type) {
		case TOK_NUMBER:
		case TOK_CHAR_LIT:
			return minic_val_to_i(t->val);
		case TOK_IDENT: {
			int k = minic_enum_const_find(t->text);
			return k >= 0 ? minic_enum_const_value_at(k) : 0;
		}
		case TOK_PLUS:
			return minic_const_expr(toks, i, level);
		case TOK_MINUS:
			return (int)(0u - (unsigned int)minic_const_expr(toks, i, level));
		case TOK_BITNOT:
			return ~minic_const_expr(toks, i, level);
		case TOK_NOT:
			return !minic_const_expr(toks, i, level);
		case TOK_LPAREN: {
			int v = minic_const_expr(toks, i, 1);
			if (toks[*i].type == TOK_RPAREN) {
				(*i)++;
			}
			return v;
		}
		default:
			(*i)--; // Not part of the expression
			return 0;
		}
	}
	int v = minic_const_expr(toks, i, level + 1);
	while (minic_binops[toks[*i].type].prec == level) {
		minic_tok_type_t op = toks[(*i)++].type;
		int              b  = minic_const_expr(toks, i, level + 1);
		if (op == TOK_AND || op == TOK_OR) {
			v = op == TOK_AND ? v && b : v || b;
		}
		else {
			v = minic_val_to_i(minic_binop(minic_binops[op].op, minic_val_int(v), minic_val_int(b)));
		}
	}
	return v;
}

// Zero pass: scan for enum and struct definitions
static void minic_register_structs(minic_comp_t *c) {
	minic_ctx_t   *ctx  = c->ctx;
	minic_token_t *toks = c->toks;
	int            i    = 0;
	while (toks[i].type != TOK_EOF) {
		bool is_typedef = toks[i].type == TOK_TYPEDEF;
		if (is_typedef) {
			i++; // Consume 'typedef'
		}

		if (toks[i].type == TOK_ENUM) {
			i++; // Consume 'enum'
			if (toks[i].type == TOK_IDENT) {
				i++; // Optional tag name
			}
			if (toks[i].type != TOK_LBRACE) {
				continue;
			}
			i++; // Consume '{'
			int val = 0;
			while (toks[i].type != TOK_RBRACE && toks[i].type != TOK_EOF) {
				if (toks[i].type == TOK_IDENT) {
					const char *cname = toks[i].text;
					i++;
					if (toks[i].type == TOK_ASSIGN) {
						i++; // Consume '='
						val = minic_const_expr(toks, &i, 1);
					}
					minic_enum_const_add(cname, val);
					val++;
				}
				else {
					i++;
				}
				if (toks[i].type == TOK_COMMA) {
					i++;
				}
			}
			if (toks[i].type == TOK_RBRACE) {
				i++;
			}
			if (is_typedef && toks[i].type == TOK_IDENT) {
				minic_int_typedef_add(toks[i].text);
				i++;
			}
		}
		else if (toks[i].type == TOK_STRUCT) {
			i++; // Consume 'struct'

			// Optional struct tag name
			char struct_name[MINIC_MAX_NAME] = "";
			if (toks[i].type == TOK_IDENT) {
				strncpy(struct_name, toks[i].text, MINIC_MAX_NAME - 1);
				i++; // Consume struct name
			}
			if (toks[i].type != TOK_LBRACE) {
				continue; // Forward decl or typedef-without-body
			}
			if (ctx->struct_count >= MINIC_MAX_STRUCTS) {
				break;
			}
			minic_struct_t *def = &ctx->structs[ctx->struct_count];
			memset(def, 0, sizeof(minic_struct_t));
			strncpy(def->name, struct_name, MINIC_MAX_NAME - 1);
			i++; // Consume '{'

			while (toks[i].type != TOK_RBRACE && toks[i].type != TOK_EOF && !c->error) {
				// Keep the name as well as its resolved type for forward/self pointers.
				int type_start = toks[i].type == TOK_STRUCT ? i + 1 : i;
				c->i           = i;
				minic_ctype_t field_type;
				if (!minic_parse_type(c, &i, true, &field_type)) {
					minic_error(c, "expected field type in '%s'", def->name);
					return;
				}
				minic_ctype_t field_base = field_type;
				while (field_base.pointer > 0) {
					field_base = minic_element_type(field_base);
				}
				for (;;) {
					c->i = i;
					if (toks[i].type != TOK_IDENT || def->field_count >= MINIC_MAX_STRUCT_FIELDS) {
						minic_error(c, "invalid or too many fields in '%s'", def->name);
						return;
					}
					int idx = def->field_count++;
					strncpy(def->fields[idx], toks[i].text, MINIC_MAX_NAME - 1);
					def->types[idx]          = field_type.kind;
					def->deref_types[idx]    = field_type.deref;
					def->pointer_depths[idx] = field_type.pointer;
					if (field_type.kind == MINIC_T_EMBED || field_type.deref == MINIC_T_EMBED) {
						strncpy(def->field_structs[idx], toks[type_start].text, MINIC_MAX_NAME - 1);
					}
					i++;
					if (toks[i].type == TOK_LBRACKET) {
						i++;
						c->i = i;
						if (toks[i].type != TOK_NUMBER || toks[i].val.type != MINIC_T_INT || toks[i].val.i <= 0) {
							minic_error(c, "field array requires a positive integer size");
							return;
						}
						def->counts[idx] = toks[i].val.i;
						i++;
						c->i = i;
						if (toks[i].type != TOK_RBRACKET) {
							minic_error(c, "expected ']' after field array size");
							return;
						}
						i++;
					}
					if (toks[i].type != TOK_COMMA) {
						break;
					}
					i++;
					field_type = field_base;
					while (toks[i].type == TOK_STAR) {
						field_type = minic_pointer_type(field_type);
						i++;
					}
				}
				c->i = i;
				if (toks[i].type != TOK_SEMICOLON) {
					minic_error(c, "expected ';' after struct field");
					return;
				}
				i++;
			}
			if (toks[i].type == TOK_RBRACE) {
				i++;
			}

			if (is_typedef && toks[i].type == TOK_IDENT) {
				// typedef struct [Name] { ... } alias;
				const char *alias = toks[i].text;
				i++; // Consume alias name
				if (struct_name[0] != '\0') {
					// Register under the tag name, plus a copy under the alias name
					ctx->struct_count++;
					if (ctx->struct_count < MINIC_MAX_STRUCTS) {
						minic_struct_t *adef = &ctx->structs[ctx->struct_count++];
						*adef                = *def;
						strncpy(adef->name, alias, MINIC_MAX_NAME - 1);
					}
				}
				else {
					// Anonymous struct: name it after the alias
					strncpy(def->name, alias, MINIC_MAX_NAME - 1);
					ctx->struct_count++;
				}
			}
			else if (struct_name[0] != '\0') {
				// Plain struct definition: must have a tag name to be usable
				ctx->struct_count++;
			}
		}
		else {
			i++;
			continue;
		}

		while (toks[i].type != TOK_SEMICOLON && toks[i].type != TOK_EOF) {
			i++;
		}
		if (toks[i].type == TOK_SEMICOLON) {
			i++;
		}
	}
}

// Resolve script layouts after collecting all definitions. Native descriptors
// already have their compiler-provided sizes, offsets, and alignment.
static bool minic_layout_struct(minic_comp_t *c, minic_struct_t *def) {
	if (def->layout_state == 2) {
		return true;
	}
	if (def->layout_state == 1) {
		minic_error(c, "recursive embedded struct '%s'", def->name);
		return false;
	}
	def->layout_state = 1;
	def->size         = 0;
	def->alignment    = 1;
	for (int i = 0; i < def->field_count; ++i) {
		if (def->types[i] == MINIC_T_EMBED) {
			minic_struct_t *child = minic_struct_get(c->ctx, def->field_structs[i]);
			if (child == NULL) {
				minic_error(c, "unknown embedded struct '%s'", def->field_structs[i]);
				return false;
			}
			if (!minic_layout_struct(c, child)) {
				return false;
			}
		}
		minic_ctype_t type  = minic_field_type(c->ctx, def, i);
		int           count = def->counts[i] > 0 ? def->counts[i] : 1;
		if (type.size <= 0 || count > (MINIC_MEM_SIZE - def->size) / type.size) {
			minic_error(c, "invalid field size in '%s'", def->name);
			return false;
		}
		int offset      = (def->size + type.alignment - 1) / type.alignment * type.alignment;
		def->offsets[i] = offset;
		def->size       = offset + count * type.size;
		if (type.alignment > def->alignment) {
			def->alignment = type.alignment;
		}
	}
	def->size         = (def->size + def->alignment - 1) / def->alignment * def->alignment;
	def->layout_state = 2;
	return true;
}

// Walk the top level: collect function signatures, or compile the global declarations
static void minic_scan_top_level(minic_comp_t *c, bool globals) {
	minic_token_t *toks = c->toks;
	c->i                = 0;
	while (minic_cur(c) != TOK_EOF && !c->error) {
		minic_tok_type_t t = minic_cur(c);
		if (t == TOK_TYPEDEF || t == TOK_ENUM || t == TOK_STRUCT) {
			int  scan       = c->i + 1;
			bool definition = t == TOK_TYPEDEF || t == TOK_ENUM;
			if (toks[scan].type == TOK_IDENT) {
				scan++;
			}
			if (definition || toks[scan].type == TOK_LBRACE) {
				minic_skip_to(c, TOK_SEMICOLON);
				minic_next(c);
				continue;
			}
		}
		minic_ctype_t type;
		if (!minic_parse_type(c, &c->i, true, &type)) {
			minic_next(c);
			continue;
		}
		if (minic_cur(c) != TOK_IDENT) {
			continue;
		}
		if (minic_peek(c, 1) != TOK_LPAREN) {
			// A global declaration, skipped until the second walk compiles it
			if (globals) {
				minic_c_decl(c, type);
				continue;
			}
			minic_skip_to(c, TOK_SEMICOLON);
			minic_next(c);
			continue;
		}

		minic_func_t fn = {0};
		strncpy(fn.name, minic_tok(c)->text, MINIC_MAX_NAME - 1);
		fn.ret_type = type;
		fn.ctx      = c->ctx;
		minic_next(c); // Consume the name
		minic_next(c); // Consume '('
		while (minic_cur(c) != TOK_RPAREN && minic_cur(c) != TOK_EOF && !c->error) {
			minic_ctype_t parameter;
			if (!minic_parse_type(c, &c->i, true, &parameter)) {
				minic_error(c, "expected parameter type");
				return;
			}
			if (minic_cur(c) == TOK_IDENT) {
				if (fn.param_count >= MINIC_MAX_PARAMS) {
					minic_error(c, "too many parameters (max %d)", MINIC_MAX_PARAMS);
					return;
				}
				int pi = fn.param_count++;
				strncpy(fn.params[pi], minic_tok(c)->text, MINIC_MAX_NAME - 1);
				fn.param_types[pi] = parameter;
				minic_next(c);
			}
			if (minic_cur(c) == TOK_COMMA) {
				minic_next(c);
			}
		}
		minic_next(c); // Consume ')'
		fn.body = minic_cur(c) == TOK_LBRACE ? c->i : -1;

		// Skip the body, or the ';' of a prototype
		if (fn.body >= 0) {
			minic_next(c);
			minic_skip_to(c, TOK_RBRACE);
		}
		else {
			minic_skip_to(c, TOK_SEMICOLON);
		}
		minic_next(c);
		if (globals) {
			continue;
		}
		minic_ctx_t *ctx = c->ctx;
		int          idx = minic_func_index(ctx, fn.name);
		if (idx >= 0) {
			if (ctx->funcs[idx].body < 0) {
				ctx->funcs[idx] = fn; // The definition after a prototype
			}
			continue;
		}
		if (ctx->func_count == ctx->func_cap) {
			ctx->func_cap = ctx->func_cap > 0 ? ctx->func_cap * 2 : 32;
			ctx->funcs    = realloc(ctx->funcs, ctx->func_cap * sizeof(minic_func_t));
		}
		ctx->funcs[ctx->func_count++] = fn;
	}
}

static bool minic_compile(minic_ctx_t *ctx) {
	minic_comp_t c = {0};
	c.ctx          = ctx;
	c.toks         = minic_tokenize(ctx->src_copy, ctx->str_pool);
	minic_emit_word(&c, OP_HALT); // Offset 0 terminates the jump patch chains
	c.locals  = malloc(MINIC_MAX_VARS * sizeof(minic_sym_t));
	c.globals = malloc(MINIC_MAX_GLOBAL_VARS * sizeof(minic_sym_t));

	// Seed with globally pre-registered struct definitions
	for (int i = 0; i < minic_struct_count && ctx->struct_count < MINIC_MAX_STRUCTS; ++i) {
		ctx->structs[ctx->struct_count++] = minic_structs[i];
	}
	minic_register_structs(&c);
	for (int i = 0; i < ctx->struct_count && !c.error; ++i) {
		minic_layout_struct(&c, &ctx->structs[i]);
	}
	if (!c.error) {
		minic_scan_top_level(&c, false);
	}

	// Global initializers, then main() so that its top level joins the globals, then the rest
	ctx->init.ctx      = ctx;
	ctx->init.ret_type = minic_scalar_type(MINIC_T_VOID);
	ctx->init.entry    = ctx->code_len;
	c.fn               = &ctx->init;
	if (!c.error) {
		minic_scan_top_level(&c, true);
	}
	minic_emit(&c, OP_INT, 1, 0);
	minic_emit(&c, OP_RET, 2, MINIC_T_VOID, MINIC_T_VOID);
	int main_idx = minic_func_index(ctx, "main");
	if (main_idx >= 0 && ctx->funcs[main_idx].body >= 0 && !c.error) {
		minic_c_function(&c, &ctx->funcs[main_idx]);
	}
	for (int i = 0; i < ctx->func_count && !c.error; ++i) {
		if (i != main_idx && ctx->funcs[i].body >= 0) {
			minic_c_function(&c, &ctx->funcs[i]);
		}
	}

	free(c.toks);
	free(c.locals);
	free(c.globals);
	return !c.error;
}

// ██████╗ ██╗   ██╗███╗   ██╗
// ██╔══██╗██║   ██║████╗  ██║
// ██████╔╝██║   ██║██╔██╗ ██║
// ██╔══██╗██║   ██║██║╚██╗██║
// ██║  ██║╚██████╔╝██║ ╚████║
// ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═══╝

static void minic_runtime_error(minic_ctx_t *ctx, int pc, const char *fmt, ...) {
	char    msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	char log[512];
	snprintf(log, sizeof(log), "%s:%d: error: %s", ctx->filename, minic_line_at(ctx->src_copy, ctx->code_pos[pc]), msg);
	console_log(log);
}

// Run a function to completion. Script calls stay in this loop, natives that call back
// into the context start a nested run above the current stack top.
static bool minic_run(minic_ctx_t *ctx, minic_func_t *fn, minic_val_t *args, int argc, minic_val_t *ret) {
	minic_val_t *base       = ctx->sp;
	int          base_depth = ctx->depth;
	minic_val_t *globals    = ctx->globals;
	const int   *code       = ctx->code;
	*ret                    = minic_val_int(0);
	if (base + fn->slot_count + MINIC_STACK_SLACK > ctx->stack_end || ctx->depth >= MINIC_MAX_FRAMES) {
		minic_runtime_error(ctx, fn->entry, "out of script memory calling '%s', recursion too deep", fn->name);
		return false;
	}
	minic_val_t *fp = base;
	for (int i = 0; i < fn->slot_count; ++i) {
		fp[i] = i < argc && i < fn->param_count ? args[i] : minic_val_int(0);
	}
	minic_val_t *sp              = fp + fn->slot_count;
	ctx->frames[ctx->depth].pc   = 0;
	ctx->frames[ctx->depth++].fp = NULL;
	int pc                       = fn->entry;

#define MINIC_SLOT(r) ((r) >= 0 ? fp + (r) : globals - (r) - 1)
#define MINIC_FAIL(...)                                \
	do {                                               \
		minic_runtime_error(ctx, pc - 1, __VA_ARGS__); \
		goto fail;                                     \
	} while (0)
#define MINIC_ARITH(OPC, IEXPR, FEXPR)                                   \
	case OPC: {                                                          \
		minic_val_t *a = sp - 2;                                         \
		minic_val_t *b = sp - 1;                                         \
		if (a->type == MINIC_T_INT && b->type == MINIC_T_INT) {          \
			a->i = (IEXPR);                                              \
		}                                                                \
		else if (a->type == MINIC_T_FLOAT && b->type == MINIC_T_FLOAT) { \
			a->f = (FEXPR);                                              \
		}                                                                \
		else {                                                           \
			*a = minic_binop(OPC, *a, *b);                               \
		}                                                                \
		sp--;                                                            \
		break;                                                           \
	}
#define MINIC_CMP(OPC, OP)                                               \
	case OPC: {                                                          \
		minic_val_t *a = sp - 2;                                         \
		minic_val_t *b = sp - 1;                                         \
		int          r;                                                  \
		if (a->type == MINIC_T_INT && b->type == MINIC_T_INT) {          \
			r = a->i OP b->i;                                            \
		}                                                                \
		else if (a->type == MINIC_T_FLOAT && b->type == MINIC_T_FLOAT) { \
			r = a->f OP b->f;                                            \
		}                                                                \
		else {                                                           \
			r = minic_val_to_d(*a) OP minic_val_to_d(*b);                \
		}                                                                \
		*a = minic_val_int(r);                                           \
		sp--;                                                            \
		break;                                                           \
	}

	for (;;) {
		switch ((minic_op_t)code[pc++]) {
		case OP_HALT:
			goto fail;
		case OP_INT:
			*sp++ = minic_val_int(code[pc++]);
			break;
		case OP_CONST:
			*sp++ = ctx->consts[code[pc++]];
			break;
		case OP_POP:
			sp--;
			break;
		case OP_DUP:
			*sp = sp[-1];
			sp++;
			break;
		case OP_LOADV:
			*sp++ = *MINIC_SLOT(code[pc]);
			pc++;
			break;
		case OP_STOREV: {
			minic_val_t *s = MINIC_SLOT(code[pc]);
			minic_val_t  v = sp[-1];
			if (v.type == s->type && (v.type == MINIC_T_FLOAT || (v.type == MINIC_T_INT && code[pc + 1] == MINIC_T_INT))) {
				s->d = v.d; // Same representation, copy the bits
			}
			else {
				minic_slot_store(s, v, code[pc + 1], code[pc + 2]);
			}
			pc += 3;
			break;
		}
		case OP_INITV:
			minic_slot_init(MINIC_SLOT(code[pc]), *--sp, code[pc + 1], code[pc + 2]);
			pc += 3;
			break;
		case OP_INIT_EMBED: {
			void *p = minic_alloc_aligned(code[pc + 1], code[pc + 2]);
			if (p == NULL) {
				MINIC_FAIL("out of script memory (%d KB)", MINIC_MEM_SIZE / 1024);
			}
			memset(p, 0, code[pc + 1]);
			*MINIC_SLOT(code[pc]) = minic_val_typed_ptr(p, MINIC_T_EMBED);
			pc += 3;
			break;
		}
		case OP_INIT_ARR: {
			int count = minic_val_to_i(*--sp);
			int size  = code[pc + 2];
			if (count < 0 || count > MINIC_MEM_SIZE / size) {
				MINIC_FAIL("invalid array size %d", count);
			}
			void *p = minic_alloc_aligned(count * size, code[pc + 3]);
			if (p == NULL) {
				MINIC_FAIL("out of script memory (%d KB)", MINIC_MEM_SIZE / 1024);
			}
			memset(p, 0, count * size);
			minic_val_t *s = MINIC_SLOT(code[pc]);
			s[0]           = minic_val_typed_ptr(p, code[pc + 1]);
			s[1]           = minic_val_int(count);
			pc += 4;
			break;
		}
		case OP_ADDRV:
			*sp++ = minic_val_typed_ptr(&MINIC_SLOT(code[pc])->i, code[pc + 1]);
			pc += 2;
			break;
		case OP_LOADM:
			sp[-1] = minic_mem_load(minic_val_to_ptr(sp[-1]), code[pc], code[pc + 1]);
			pc += 2;
			break;
		case OP_STOREM:
			minic_mem_store(minic_val_to_ptr(sp[-2]), sp[-1], code[pc], code[pc + 1]);
			sp[-2] = sp[-1];
			sp--;
			pc += 2;
			break;
		case OP_LOADH: {
			minic_val_t h = ctx->consts[code[pc++]];
			*sp++         = minic_mem_load(h.p, h.deref_type, h.deref_type);
			break;
		}
		case OP_FIELD: {
			char *base = minic_val_to_ptr(sp[-1]);
			if (base == NULL) {
				int             f   = code[pc + 1];
				minic_struct_t *def = &ctx->structs[f / MINIC_MAX_STRUCT_FIELDS];
				pc += 2;
				MINIC_FAIL("null pointer access on '%s->%s'", def->name, def->fields[f % MINIC_MAX_STRUCT_FIELDS]);
			}
			sp[-1] = minic_val_ptr(base + code[pc]);
			pc += 2;
			break;
		}
		case OP_INDEX: {
			int   idx  = minic_val_to_i(*--sp);
			int   len  = code[pc + 1];
			char *base = minic_val_to_ptr(sp[-1]);
			pc += 2;
			if (idx < 0 || (len >= 0 && idx >= len)) {
				MINIC_FAIL("index %d out of range (length %d)", idx, len);
			}
			sp[-1] = minic_val_ptr(base != NULL ? base + (size_t)idx * code[pc - 2] : NULL);
			break;
		}
		case OP_INDEX_ARR: {
			minic_val_t *s   = MINIC_SLOT(code[pc]);
			int          idx = minic_val_to_i(sp[-1]);
			pc += 2;
			if (idx < 0 || idx >= s[1].i) {
				MINIC_FAIL("index %d out of range (length %d)", idx, s[1].i);
			}
			sp[-1] = minic_val_ptr((char *)s[0].p + (size_t)idx * code[pc - 1]);
			break;
		}
		case OP_INDEX_BUF: {
			int   idx   = minic_val_to_i(*--sp);
			char *field = sp[-1].p;
			char *base;
			int   len;
			memcpy(&base, field, sizeof(base));
			memcpy(&len, field + code[pc + 1], sizeof(len));
			pc += 2;
			if (idx < 0 || idx >= len) {
				MINIC_FAIL("index %d out of range (length %d)", idx, len);
			}
			sp[-1] = minic_val_ptr(base != NULL ? base + (size_t)idx * code[pc - 2] : NULL);
			break;
		}
		case OP_INCV: {
			minic_val_t *s    = MINIC_SLOT(code[pc]);
			minic_val_t  old  = *s;
			minic_val_t  next = minic_step(old, code[pc + 2], code[pc + 4]);
			if (code[pc + 1] == MINIC_T_INT && old.type == MINIC_T_INT) {
				s->i = next.i;
			}
			else {
				minic_slot_store(s, next, code[pc + 1], 0);
			}
			*sp++ = code[pc + 3] ? old : next;
			pc += 5;
			break;
		}
		case OP_INCM: {
			void       *p    = minic_val_to_ptr(sp[-1]);
			minic_val_t old  = minic_mem_load(p, code[pc], code[pc + 1]);
			minic_val_t next = minic_step(old, code[pc + 2], code[pc + 4]);
			minic_mem_store(p, next, code[pc], code[pc + 5]);
			sp[-1] = code[pc + 3] ? old : next;
			pc += 6;
			break;
		}
		case OP_COMPV: {
			minic_val_t *s = MINIC_SLOT(code[pc]);
			minic_val_t  r = minic_binop(code[pc + 1], *s, sp[-1]);
			if (r.type != s->type) {
				r = minic_val_cast(r, s->type);
			}
			if (code[pc + 2] == (int)s->type && (s->type == MINIC_T_FLOAT || s->type == MINIC_T_INT)) {
				s->d = r.d; // Same representation, copy the bits
			}
			else {
				minic_slot_store(s, r, code[pc + 2], code[pc + 3]);
			}
			sp[-1] = r;
			pc += 4;
			break;
		}
		case OP_COMPM: {
			void       *p   = minic_val_to_ptr(sp[-2]);
			minic_val_t old = minic_mem_load(p, code[pc + 1], code[pc + 2]);
			minic_val_t r   = minic_val_cast(minic_binop(code[pc], old, sp[-1]), old.type);
			minic_mem_store(p, r, code[pc + 1], code[pc + 3]);
			sp[-2] = r;
			sp--;
			pc += 4;
			break;
		}
			MINIC_ARITH(OP_ADD, (int)((unsigned int)a->i + (unsigned int)b->i), a->f + b->f)
			MINIC_ARITH(OP_SUB, (int)((unsigned int)a->i - (unsigned int)b->i), a->f - b->f)
			MINIC_ARITH(OP_MUL, (int)((unsigned int)a->i * (unsigned int)b->i), a->f * b->f)
			MINIC_ARITH(OP_DIV, minic_arith(*a, *b, OP_DIV).i, b->f != 0.0f ? a->f / b->f : 0.0f)
			MINIC_CMP(OP_EQ, ==)
			MINIC_CMP(OP_NE, !=)
			MINIC_CMP(OP_LT, <)
			MINIC_CMP(OP_GT, >)
			MINIC_CMP(OP_LE, <=)
			MINIC_CMP(OP_GE, >=)
		case OP_MOD:
		case OP_SHL:
		case OP_SHR:
		case OP_BAND:
		case OP_BOR:
		case OP_XOR:
			sp[-2] = minic_binop(code[pc - 1], sp[-2], sp[-1]);
			sp--;
			break;
		case OP_NEG: {
			minic_val_t v = sp[-1];
			sp[-1]        = v.type == MINIC_T_INT ? minic_val_int((int)(0u - (unsigned int)v.i)) : minic_val_coerce(-minic_val_to_d(v), v.type);
			break;
		}
		case OP_NOT:
			sp[-1] = minic_val_int(!minic_val_is_true(sp[-1]));
			break;
		case OP_BNOT:
			sp[-1] = minic_val_int(~minic_val_to_i(sp[-1]));
			break;
		case OP_CAST:
			sp[-1] = minic_cast(sp[-1], code[pc++]);
			break;
		case OP_TOPTR:
			sp[-1] = minic_val_typed_ptr(minic_val_to_ptr(sp[-1]), code[pc++]);
			break;
		case OP_JMP:
			pc = code[pc];
			break;
		case OP_JZ: {
			minic_val_t v = *--sp;
			pc            = (v.type == MINIC_T_INT ? v.i != 0 : minic_val_is_true(v)) ? pc + 1 : code[pc];
			break;
		}
		case OP_JNZ: {
			minic_val_t v = *--sp;
			pc            = (v.type == MINIC_T_INT ? v.i != 0 : minic_val_is_true(v)) ? code[pc] : pc + 1;
			break;
		}
		case OP_CALL: {
			minic_func_t *f = &ctx->funcs[code[pc]];
			int           n = code[pc + 1];
			pc += 2;
			if (sp + f->slot_count + MINIC_STACK_SLACK > ctx->stack_end || ctx->depth >= MINIC_MAX_FRAMES) {
				MINIC_FAIL("out of script memory calling '%s', recursion too deep", f->name);
			}
			ctx->frames[ctx->depth].pc   = pc;
			ctx->frames[ctx->depth++].fp = fp;
			fp                           = sp - n;
			for (; sp < fp + f->slot_count; ++sp) {
				*sp = minic_val_int(0);
			}
			sp = fp + f->slot_count;
			pc = f->entry;
			break;
		}
		case OP_CALLN: {
			minic_ext_func_t *ef = &minic_ext_funcs[code[pc]];
			int               n  = code[pc + 1];
			pc += 2;
			ctx->sp       = sp; // A native may call back into this context
			minic_val_t r = minic_dispatch(ef, sp - n, n);
			sp -= n;
			*sp++ = r;
			if (minic_mem_oom) {
				MINIC_FAIL("out of script memory (%d KB)", MINIC_MEM_SIZE / 1024);
			}
			break;
		}
		case OP_FNPTR:
			*sp++ = minic_val_ptr(&ctx->funcs[code[pc++]]);
			break;
		case OP_RET: {
			minic_val_t  v    = sp[-1];
			minic_type_t kind = code[pc];
			if (kind == MINIC_T_PTR) {
				v = minic_val_typed_ptr(minic_val_to_ptr(v), code[pc + 1]);
			}
			else if (kind != MINIC_T_VOID && kind != MINIC_T_EMBED && v.type != kind) {
				v = minic_cast(v, kind);
			}
			sp                   = fp;
			minic_frame_t *frame = &ctx->frames[--ctx->depth];
			if (ctx->depth == base_depth) {
				*ret    = v;
				ctx->sp = base;
				return true;
			}
			pc    = frame->pc;
			fp    = frame->fp;
			*sp++ = v;
			break;
		}
		}
	}

fail:
	ctx->depth = base_depth;
	ctx->sp    = base;
	return false;
#undef MINIC_SLOT
#undef MINIC_FAIL
#undef MINIC_ARITH
#undef MINIC_CMP
}

static minic_val_t minic_call_in_ctx(minic_ctx_t *ctx, minic_func_t *fn, minic_val_t *args, int argc) {
	minic_ctx_t *prev       = minic_active;
	int          saved_used = ctx->mem_used;
	minic_active            = ctx;
	minic_val_t r;
	minic_run(ctx, fn, args, argc, &r);
	ctx->mem_used = saved_used; // Rewind, the arena is free again
	minic_active  = prev;
	return r;
}

minic_val_t minic_call_fn(void *fn_ptr, minic_val_t *args, int argc) {
	minic_func_t *fn = (minic_func_t *)fn_ptr;
	if (fn == NULL || fn->ctx == NULL) {
		return minic_val_int(0);
	}
	return minic_call_in_ctx(fn->ctx, fn, args, argc);
}

minic_val_t minic_ctx_call_fn(minic_ctx_t *ctx, void *fn_ptr, minic_val_t *args, int argc) {
	if (ctx == NULL || fn_ptr == NULL) {
		return minic_val_int(0);
	}
	return minic_call_in_ctx(ctx, (minic_func_t *)fn_ptr, args, argc);
}

minic_ctx_t *minic_eval_named(const char *src, const char *filename) {
	minic_register_builtins();

	minic_ctx_t *ctx = (minic_ctx_t *)calloc(1, sizeof(minic_ctx_t));
	ctx->filename    = filename;
	ctx->mem         = (minic_u8 *)calloc(1, MINIC_MEM_SIZE);
	ctx->mem_frame   = MINIC_MEM_SIZE - MINIC_STACK_SIZE;
	ctx->stack_end   = (minic_val_t *)(ctx->mem + MINIC_MEM_SIZE);
	ctx->sp          = (minic_val_t *)(ctx->mem + ctx->mem_frame);
	ctx->frames      = malloc(MINIC_MAX_FRAMES * sizeof(minic_frame_t));
	ctx->structs     = malloc(MINIC_MAX_STRUCTS * sizeof(minic_struct_t));
	// Copy the source so the context stays valid after the caller frees its buffer
	int src_len   = (int)strlen(src);
	ctx->src_copy = (char *)malloc(src_len + 1);
	memcpy(ctx->src_copy, src, src_len + 1);
	ctx->str_pool = (char *)malloc(src_len + 1);

	// Install the arena so minic_alloc uses this context
	minic_ctx_t *prev = minic_active;
	minic_active      = ctx;
	minic_mem_oom     = false;

	bool ok = minic_compile(ctx);
	if (ok) {
		ctx->globals = calloc(ctx->global_count + 1, sizeof(minic_val_t));
		minic_val_t r;
		ok           = minic_run(ctx, &ctx->init, NULL, 0, &r);
		int main_idx = minic_func_index(ctx, "main");
		if (ok && main_idx >= 0 && ctx->funcs[main_idx].body >= 0) {
			ok = minic_run(ctx, &ctx->funcs[main_idx], NULL, 0, &ctx->return_val);
		}
	}
	minic_active = prev;

	ctx->result = (!ok || minic_mem_oom) ? -1.0f : (float)minic_val_to_d(ctx->return_val);
	return ctx;
}

minic_ctx_t *minic_eval(const char *src) {
	return minic_eval_named(src, "<script>");
}

void minic_ctx_free(minic_ctx_t *ctx) {
	if (ctx != NULL) {
		free(ctx->mem);
		free(ctx->funcs);
		free(ctx->structs);
		free(ctx->frames);
		free(ctx->globals);
		free(ctx->code);
		free(ctx->code_pos);
		free(ctx->consts);
		free(ctx->src_copy);
		free(ctx->str_pool);
		free(ctx);
	}
}

float minic_ctx_result(minic_ctx_t *ctx) {
	return ctx != NULL ? ctx->result : -1.0f;
}

minic_val_t minic_ctx_return_val(minic_ctx_t *ctx) {
	return ctx != NULL ? ctx->return_val : minic_val_int(0);
}

// ███████╗██╗  ██╗████████╗███████╗██████╗ ███╗   ██╗ █████╗ ██╗
// ██╔════╝╚██╗██╔╝╚══██╔══╝██╔════╝██╔══██╗████╗  ██║██╔══██╗██║
// █████╗   ╚███╔╝    ██║   █████╗  ██████╔╝██╔██╗ ██║███████║██║
// ██╔══╝   ██╔██╗    ██║   ██╔══╝  ██╔══██╗██║╚██╗██║██╔══██║██║
// ███████╗██╔╝ ██╗   ██║   ███████╗██║  ██║██║ ╚████║██║  ██║███████╗
// ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝  ╚═╝╚══════╝

typedef struct {
	char name[MINIC_MAX_NAME];
	int  value;
} minic_enum_const_t;

typedef struct {
	char         name[MINIC_MAX_NAME];
	const void  *ptr;  // points at the live host variable
	minic_type_t type; // MINIC_T_INT or MINIC_T_FLOAT
} minic_global_t;

static int                minic_ext_func_count = 0;
static minic_enum_const_t minic_enum_consts[MINIC_MAX_ENUM_CONSTS];
static int                minic_enum_const_count = 0;
static char               minic_int_typedefs[MINIC_MAX_INT_TYPEDEFS][MINIC_MAX_NAME];
static int                minic_int_typedef_count = 0;
static minic_global_t     minic_globals[MINIC_MAX_GLOBALS];
static int                minic_global_count = 0;

minic_struct_t         minic_structs[MINIC_MAX_STRUCTS];
int                    minic_struct_count = 0;
static minic_struct_t *minic_struct_cur   = NULL;

void minic_struct_begin(const char *name, int size, int alignment) {
	minic_struct_cur = NULL;
	for (int i = 0; i < minic_struct_count; ++i) {
		if (strcmp(minic_structs[i].name, name) == 0) {
			minic_struct_cur = &minic_structs[i];
			break;
		}
	}
	if (minic_struct_cur == NULL) {
		if (minic_struct_count >= MINIC_MAX_STRUCTS) {
			return;
		}
		minic_struct_cur = &minic_structs[minic_struct_count++];
	}
	memset(minic_struct_cur, 0, sizeof(minic_struct_t));
	strncpy(minic_struct_cur->name, name, MINIC_MAX_NAME - 1);
	minic_struct_cur->size         = size;
	minic_struct_cur->alignment    = alignment;
	minic_struct_cur->layout_state = 2;
}

void minic_struct_field(const char *field, int offset, minic_type_t type, minic_type_t deref_type, const char *struct_type) {
	minic_struct_t *s = minic_struct_cur;
	if (s == NULL || s->field_count >= MINIC_MAX_STRUCT_FIELDS) {
		return;
	}
	int i = s->field_count++;
	strncpy(s->fields[i], field, MINIC_MAX_NAME - 1);
	s->offsets[i]        = offset;
	s->types[i]          = type;
	s->deref_types[i]    = deref_type;
	s->pointer_depths[i] = type == MINIC_T_PTR ? 1 : 0;
	if (struct_type != NULL) {
		strncpy(s->field_structs[i], struct_type, MINIC_MAX_NAME - 1);
	}
}

void minic_register_struct(const char *name, const char **fields, int field_count) {
	minic_struct_begin(name, field_count * (int)sizeof(int32_t), MINIC_ALIGNOF(int32_t));
	for (int i = 0; i < field_count; ++i) {
		minic_struct_field(fields[i], i * (int)sizeof(int32_t), MINIC_T_INT, MINIC_T_INT, NULL);
	}
}

static int minic_enum_const_find(const char *name) {
	for (int i = 0; i < minic_enum_const_count; ++i) {
		if (strcmp(minic_enum_consts[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

void minic_enum_const_add(const char *name, int value) {
	if (minic_enum_const_find(name) >= 0 || minic_enum_const_count >= MINIC_MAX_ENUM_CONSTS) {
		return;
	}
	strncpy(minic_enum_consts[minic_enum_const_count].name, name, MINIC_MAX_NAME - 1);
	minic_enum_consts[minic_enum_const_count++].value = value;
}

int minic_enum_const_get(const char *name) {
	int i = minic_enum_const_find(name);
	return i >= 0 ? minic_enum_consts[i].value : -1;
}

static int minic_global_find(const char *name) {
	for (int i = 0; i < minic_global_count; ++i) {
		if (strcmp(minic_globals[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

void minic_register_global(const char *name, const void *ptr, minic_type_t type) {
	int i = minic_global_find(name);
	if (i < 0) {
		if (minic_global_count >= MINIC_MAX_GLOBALS) {
			return;
		}
		i = minic_global_count++;
		strncpy(minic_globals[i].name, name, MINIC_MAX_NAME - 1);
	}
	minic_globals[i].ptr  = ptr;
	minic_globals[i].type = type;
}

static const void *minic_global_ptr(const char *name, minic_type_t *type) {
	int i = minic_global_find(name);
	if (i < 0) {
		return NULL;
	}
	*type = minic_globals[i].type;
	return minic_globals[i].ptr;
}

bool minic_global_get(const char *name, minic_val_t *out) {
	minic_type_t type;
	const void  *ptr = minic_global_ptr(name, &type);
	if (ptr != NULL) {
		*out = minic_mem_load((void *)ptr, type, type);
	}
	return ptr != NULL;
}

void minic_int_typedef_add(const char *name) {
	if (minic_is_int_typedef(name) || minic_int_typedef_count >= MINIC_MAX_INT_TYPEDEFS) {
		return;
	}
	strncpy(minic_int_typedefs[minic_int_typedef_count++], name, MINIC_MAX_NAME - 1);
}

bool minic_is_int_typedef(const char *name) {
	for (int i = 0; i < minic_int_typedef_count; ++i) {
		if (strcmp(minic_int_typedefs[i], name) == 0) {
			return true;
		}
	}
	return false;
}

void minic_register_enum(const char *typedef_name, const char **names, const int *values, int count) {
	if (typedef_name != NULL) {
		minic_int_typedef_add(typedef_name);
	}
	for (int i = 0; i < count; ++i) {
		minic_enum_const_add(names[i], values != NULL ? values[i] : i);
	}
}

static minic_ext_func_t *minic_ext_func_add(const char *name) {
	minic_ext_func_t *ef = minic_ext_func_get(name);
	if (ef == NULL && minic_ext_func_count < MINIC_MAX_EXTFUNS) {
		ef = &minic_ext_funcs[minic_ext_func_count++];
		memset(ef, 0, sizeof(*ef));
		strncpy(ef->name, name, MINIC_MAX_NAME - 1);
	}
	return ef;
}

void minic_register(const char *name, const char *sig, minic_native_fn_t fn) {
	minic_ext_func_t *ef = minic_ext_func_add(name);
	if (ef == NULL) {
		return;
	}
	strncpy(ef->sig, sig != NULL ? sig : "i()", MINIC_MAX_SIG - 1);
	ef->fn = fn;
}

void minic_register_native(const char *name, minic_native_fn_t fn) {
	minic_ext_func_t *ef = minic_ext_func_add(name);
	if (ef != NULL) {
		ef->fn = fn;
	}
}

#define MINIC_EXT_HASH_SIZE 2048

static int16_t minic_ext_hash[MINIC_EXT_HASH_SIZE];
static int     minic_ext_hash_count = -1;

static unsigned minic_name_hash(const char *s) {
	unsigned h = 2166136261u; // FNV-1a
	while (*s != '\0') {
		h ^= (unsigned char)*s++;
		h *= 16777619u;
	}
	return h & (MINIC_EXT_HASH_SIZE - 1);
}

static void minic_ext_hash_build(void) {
	for (int i = 0; i < MINIC_EXT_HASH_SIZE; ++i) {
		minic_ext_hash[i] = -1;
	}
	for (int i = 0; i < minic_ext_func_count; ++i) {
		unsigned h = minic_name_hash(minic_ext_funcs[i].name);
		while (minic_ext_hash[h] != -1) {
			h = (h + 1) & (MINIC_EXT_HASH_SIZE - 1);
		}
		minic_ext_hash[h] = (int16_t)i;
	}
	minic_ext_hash_count = minic_ext_func_count;
}

minic_ext_func_t *minic_ext_func_get(const char *name) {
	if (minic_ext_hash_count != minic_ext_func_count) {
		minic_ext_hash_build();
	}
	unsigned h = minic_name_hash(name);
	while (minic_ext_hash[h] != -1) {
		minic_ext_func_t *ef = &minic_ext_funcs[minic_ext_hash[h]];
		if (strcmp(ef->name, name) == 0) {
			return ef;
		}
		h = (h + 1) & (MINIC_EXT_HASH_SIZE - 1);
	}
	return NULL;
}

int minic_ext_func_count_get(void) {
	return minic_ext_func_count;
}

const char *minic_ext_func_name_at(int i) {
	return minic_ext_funcs[i].name;
}

const char *minic_ext_func_sig_at(int i) {
	return minic_ext_funcs[i].sig;
}

int minic_global_count_get(void) {
	return minic_global_count;
}

const char *minic_global_name_at(int i) {
	return minic_globals[i].name;
}

minic_type_t minic_global_type_at(int i) {
	return minic_globals[i].type;
}

int minic_enum_const_count_get(void) {
	return minic_enum_const_count;
}

const char *minic_enum_const_name_at(int i) {
	return minic_enum_consts[i].name;
}

int minic_enum_const_value_at(int i) {
	return minic_enum_consts[i].value;
}

// ██████╗ ██╗███████╗██████╗  █████╗ ████████╗ ██████╗██╗  ██╗
// ██╔══██╗██║██╔════╝██╔══██╗██╔══██╗╚══██╔══╝██╔════╝██║  ██║
// ██║  ██║██║███████╗██████╔╝███████║   ██║   ██║     ███████║
// ██║  ██║██║╚════██║██╔═══╝ ██╔══██║   ██║   ██║     ██╔══██║
// ██████╔╝██║███████║██║     ██║  ██║   ██║   ╚██████╗██║  ██║
// ╚═════╝ ╚═╝╚══════╝╚═╝     ╚═╝  ╚═╝   ╚═╝    ╚═════╝╚═╝  ╚═╝
//
float minic_arg_f(minic_val_t *args, int argc, int i) {
	return i < argc ? (float)minic_val_to_d(args[i]) : 0.0f;
}

int minic_arg_i(minic_val_t *args, int argc, int i) {
	return i < argc ? (int)minic_val_to_d(args[i]) : 0;
}

void *minic_arg_p(minic_val_t *args, int argc, int i) {
	return i < argc ? minic_val_to_ptr(args[i]) : NULL;
}

minic_val_t minic_dispatch(minic_ext_func_t *ef, minic_val_t *args, int argc) {
	if (ef->fn == NULL) {
		fprintf(stderr, "minic: '%s' has no thunk, add it to minic_api_list.h\n", ef->name);
		return minic_val_int(0);
	}
	return ef->fn(args, argc);
}
