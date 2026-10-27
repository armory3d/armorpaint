
// Minimal C interpreter
// The source is tokenized and compiled to bytecode once, then run on a register VM.
// Names resolve at compile time: variables to frame or static registers, fields to byte offsets,
// functions to indices. Values keep their runtime type tags, arithmetic widens like C. Operations
// on values whose static type surely matches the tag (floats and ints) use typed instructions.

#include "minic.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__clang__) && !defined(__wasm__)
#define MINIC_COMPUTED_GOTO
#endif

#ifdef __clang__
#pragma STDC FP_CONTRACT OFF
#endif

// ████████╗ ██████╗ ██╗  ██╗███████╗███╗   ██╗
// ╚══██╔══╝██╔═══██╗██║ ██╔╝██╔════╝████╗  ██║
//    ██║   ██║   ██║█████╔╝ █████╗  ██╔██╗ ██║
//    ██║   ██║   ██║██╔═██╗ ██╔══╝  ██║╚██╗██║
//    ██║   ╚██████╔╝██║  ██╗███████╗██║ ╚████║
//    ╚═╝    ╚═════╝ ╚═╝  ╚═╝╚══════╝╚═╝  ╚═══╝

#define MINIC_TOK_LIST                                                                                                                                 \
	X(TOK_INT, "'int'")                                                                                                                                \
	X(TOK_FLOAT, "'float'")                                                                                                                            \
	X(TOK_CHAR, "'char'")                                                                                                                              \
	X(TOK_DOUBLE, "'double'")                                                                                                                          \
	X(TOK_BOOL, "'bool'")                                                                                                                              \
	X(TOK_INT16, "'int16_t'")                                                                                                                          \
	X(TOK_UINT16, "'uint16_t'")                                                                                                                        \
	X(TOK_VOID, "'void'")                                                                                                                              \
	X(TOK_RETURN, "'return'")                                                                                                                          \
	X(TOK_IF, "'if'")                                                                                                                                  \
	X(TOK_ELSE, "'else'")                                                                                                                              \
	X(TOK_WHILE, "'while'")                                                                                                                            \
	X(TOK_FOR, "'for'")                                                                                                                                \
	X(TOK_BREAK, "'break'")                                                                                                                            \
	X(TOK_CONTINUE, "'continue'")                                                                                                                      \
	X(TOK_STRUCT, "'struct'")                                                                                                                          \
	X(TOK_TYPEDEF, "'typedef'")                                                                                                                        \
	X(TOK_ENUM, "'enum'")                                                                                                                              \
	X(TOK_IDENT, "identifier")                                                                                                                         \
	X(TOK_NUMBER, "number")                                                                                                                            \
	X(TOK_CHAR_LIT, "char literal")                                                                                                                    \
	X(TOK_STR_LIT, "string literal")                                                                                                                   \
	X(TOK_LPAREN, "'('")                                                                                                                               \
	X(TOK_RPAREN, "')'")                                                                                                                               \
	X(TOK_LBRACE, "'{'")                                                                                                                               \
	X(TOK_RBRACE, "'}'") X(TOK_LBRACKET, "'['") X(TOK_RBRACKET, "']'") X(TOK_SEMICOLON, "';'") X(TOK_COMMA, "','") X(TOK_ASSIGN, "'='")                \
	    X(TOK_PLUS_ASSIGN, "'+='") X(TOK_MINUS_ASSIGN, "'-='") X(TOK_MUL_ASSIGN, "'*='") X(TOK_DIV_ASSIGN, "'/='") X(TOK_MOD_ASSIGN, "'%='")           \
	        X(TOK_SHL_ASSIGN, "'<<='") X(TOK_SHR_ASSIGN, "'>>='") X(TOK_AND_ASSIGN, "'&='") X(TOK_OR_ASSIGN, "'|='") X(TOK_XOR_ASSIGN, "'^='")         \
	            X(TOK_EQ, "'=='") X(TOK_NEQ, "'!='") X(TOK_LT, "'<'") X(TOK_GT, "'>'") X(TOK_LE, "'<='") X(TOK_GE, "'>='") X(TOK_AND, "'&&'")          \
	                X(TOK_OR, "'||'") X(TOK_NOT, "'!'") X(TOK_AMP, "'&'") X(TOK_PLUS, "'+'") X(TOK_MINUS, "'-'") X(TOK_INC, "'++'") X(TOK_DEC, "'--'") \
	                    X(TOK_STAR, "'*'") X(TOK_SLASH, "'/'") X(TOK_PERCENT, "'%'") X(TOK_SHL, "'<<'") X(TOK_SHR, "'>>'") X(TOK_BITOR, "'|'")         \
	                        X(TOK_XOR, "'^'") X(TOK_BITNOT, "'~'") X(TOK_DOT, "'.'") X(TOK_ARROW, "'->'") X(TOK_QUESTION, "'?'") X(TOK_COLON, "':'")   \
	                            X(TOK_EOF, "end of file")

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
    {"int", TOK_INT},       {"float", TOK_FLOAT}, {"char", TOK_CHAR},       {"double", TOK_DOUBLE},     {"bool", TOK_BOOL},     {"void", TOK_VOID},
    {"int16_t", TOK_INT16}, {"short", TOK_INT16}, {"uint16_t", TOK_UINT16}, {"return", TOK_RETURN},     {"if", TOK_IF},         {"else", TOK_ELSE},
    {"while", TOK_WHILE},   {"for", TOK_FOR},     {"break", TOK_BREAK},     {"continue", TOK_CONTINUE}, {"struct", TOK_STRUCT}, {"typedef", TOK_TYPEDEF},
    {"enum", TOK_ENUM},
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
			pos += 2;
			unsigned int n = 0;
			while (isxdigit((unsigned char)src[pos])) {
				char h = src[pos++];
				n      = n * 16 + (isdigit((unsigned char)h) ? h - '0' : (h | 0x20) - 'a' + 10);
			}
			t->val  = minic_val_int((int)n);
			t->type = TOK_NUMBER;
			return pos;
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
	int           body;       // token index of the '{' that starts the body, -1 for a prototype
	int           entry;      // bytecode offset, script callers pass converted arguments
	int           entry_ext;  // bytecode offset that converts the arguments first, for native callers
	int           slot_count; // registers: parameters, locals and temporaries
	minic_ctype_t ret_type;
	minic_ctx_t  *ctx; // owning context
} minic_func_t;

typedef struct {
	const int    *ip;
	minic_val_t  *fp;
	minic_func_t *fn;
} minic_frame_t;

#define MINIC_STACK_SIZE      (1024 * 1024) // Top of the arena, the registers of the active calls
#define MINIC_MAX_FRAMES      4096
#define MINIC_MAX_GLOBAL_VARS 1024

struct minic_ctx_s {
	minic_u8       *mem;
	int             mem_used;
	int             mem_frame; // End of the heap, the call registers sit above it
	char           *str_pool;  // String literals, indexed by source offset
	char           *src_copy;
	const char     *filename;
	int            *code;
	int            *code_pos; // Source offset of each code word, for runtime errors
	int             code_len;
	int             code_cap;
	minic_val_t    *statics; // Registers shared by all calls: global variables and constants
	int             static_count;
	int             static_cap;
	minic_func_t   *funcs;
	int             func_count;
	int             func_cap;
	minic_struct_t *structs;
	int             struct_count;
	minic_func_t    init; // Global initializers
	minic_val_t    *stack_end;
	minic_val_t    *sp; // Registers from here on are free, a native that calls back into the context runs there
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
	static const minic_type_t types[] = {MINIC_T_INT, MINIC_T_FLOAT, MINIC_T_CHAR, MINIC_T_DOUBLE, MINIC_T_BOOL, MINIC_T_I16, MINIC_T_U16, MINIC_T_VOID};
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
	case MINIC_T_I16:
	case MINIC_T_U16:
		type.size      = sizeof(int16_t);
		type.alignment = MINIC_ALIGNOF(int16_t);
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

// Instructions write register d and read registers a, b, c. Registers are numbered from 0 up in
// the frame of the call, statics (global variables and constants) are ~0, ~1 and down. The
// operands list the words after the opcode: r a register, i an immediate. In the final code a
// register is a byte offset from the frame, or from the end of the statics stored in reverse.
// [addr] is 8 words, base idx mul add scale off len field: the address base + i * scale + off
// where i = idx * mul + add must be below the length register. A null base is an error when
// field (struct index * MINIC_MAX_STRUCT_FIELDS + field index) is >= 0, else loads read 0 and
// stores are dropped.
// The instructions from MOV to MADD_I write d as their first operand, compare and jump
// instructions keep the order of the comparisons from LT_F to NE_I.
#define MINIC_OPS                                                                                          \
	X(HALT, "")                                                                                            \
	X(MOV, "rr")          /* d a */                                                                        \
	X(INIT, "rrii")       /* d a kind deref: declare d with a converted to kind */                         \
	X(INIT_EMBED, "rrii") /* d a size align: allocate struct storage, copy a */                            \
	X(INIT_ARR, "rrriii") /* d b a kind size align: allocate a elements, b is the count */                 \
	X(ADDRV, "rri")       /* d a deref: the address of variable a */                                       \
	X(LD, "rrriiiirii")   /* d [addr] kind|deref<<8 */                                                     \
	X(LEA, "rrriiiiri")   /* d [addr] */                                                                   \
	X(BIN, "rrri")        /* d a b op: any binary operator on any values */                                \
	X(MIX, "rrri")        /* d a b op|a_int<<4|b_int<<5: arithmetic or comparison of a float and an int */ \
	X(CAST, "rri")        /* d a kind */                                                                   \
	X(NEG, "rr")          /* d a */                                                                        \
	X(ADD_F, "rrr")       /* d a b: two floats */                                                          \
	X(SUB_F, "rrr")                                                                                        \
	X(MUL_F, "rrr")                                                                                        \
	X(DIV_F, "rrr")                                                                                        \
	X(ADD_I, "rrr") /* d a b: two ints */                                                                  \
	X(SUB_I, "rrr")                                                                                        \
	X(MUL_I, "rrr")                                                                                        \
	X(DIV_I, "rrr")                                                                                        \
	X(MOD_I, "rrr")                                                                                        \
	X(LT_F, "rrr")                                                                                         \
	X(LE_F, "rrr")                                                                                         \
	X(EQ_F, "rrr")                                                                                         \
	X(NE_F, "rrr")                                                                                         \
	X(LT_I, "rrr")                                                                                         \
	X(LE_I, "rrr")                                                                                         \
	X(EQ_I, "rrr")                                                                                         \
	X(NE_I, "rrr")                                                                                         \
	X(MADD_F, "rrrr") /* d a b c: a + b * c */                                                             \
	X(MADD_I, "rrrr")                                                                                      \
	X(STV, "rrii")       /* a b kind size: store b into variable a */                                      \
	X(ST, "rriiiiririi") /* [addr] b kind size */                                                          \
	X(JMP, "i")          /* target */                                                                      \
	X(JZ, "ir")          /* target a */                                                                    \
	X(JNZ, "ir")         /* target a */                                                                    \
	X(JLT_F, "irr")      /* target a b: jump when a < b */                                                 \
	X(JLE_F, "irr")                                                                                        \
	X(JEQ_F, "irr")                                                                                        \
	X(JNE_F, "irr")                                                                                        \
	X(JLT_I, "irr")                                                                                        \
	X(JLE_I, "irr")                                                                                        \
	X(JEQ_I, "irr")                                                                                        \
	X(JNE_I, "irr")                                                                                        \
	X(JNLT_F, "irr") /* target a b: jump when not a < b */                                                 \
	X(JNLE_F, "irr")                                                                                       \
	X(LOOP_LT, "irir") /* target a k b: a += k, jump when a < b */                                         \
	X(LOOP_LE, "irir")                                                                                     \
	X(CALL, "ir")   /* func base: arguments from register base on, the result lands there */               \
	X(CALLN, "iri") /* ext base argc */                                                                    \
	X(RET, "rii")   /* a kind deref */

typedef enum {
#define X(name, operands) OP_##name,
	MINIC_OPS
#undef X
} minic_op_t;

// Operators of OP_BIN and OP_MIX
enum {
	BIN_ADD,
	BIN_SUB,
	BIN_MUL,
	BIN_DIV,
	BIN_MOD,
	BIN_SHL,
	BIN_SHR,
	BIN_BAND,
	BIN_BOR,
	BIN_XOR,
	BIN_EQ,
	BIN_NE,
	BIN_LT,
	BIN_GT,
	BIN_LE,
	BIN_GE
};

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
	case MINIC_T_I16: {
		int16_t n;
		memcpy(&n, p, sizeof(n));
		return minic_val_int(n);
	}
	case MINIC_T_U16: {
		uint16_t n;
		memcpy(&n, p, sizeof(n));
		return minic_val_int(n);
	}
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
	case MINIC_T_I16:
	case MINIC_T_U16: {
		uint16_t n = (uint16_t)minic_val_to_i(v);
		memcpy(p, &n, sizeof(n));
		break;
	}
	default: {
		int32_t n = minic_val_to_i(v);
		memcpy(p, &n, sizeof(n));
		break;
	}
	}
}

// Variables live in minic_val_t registers whose tag is set when they are declared: int, char,
// int16_t, uint16_t and bool use an INT tag, struct variables hold a pointer to their storage. The union
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
	case MINIC_T_I16:
		s->i = (int16_t)minic_val_to_i(v);
		break;
	case MINIC_T_U16:
		s->i = (uint16_t)minic_val_to_i(v);
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
	if (kind == MINIC_T_VOID || (v.type == kind && (kind == MINIC_T_FLOAT || kind == MINIC_T_INT))) {
		*s = v;
		if (kind != MINIC_T_VOID) {
			s->deref_type = deref;
		}
		return;
	}
	s->type       = kind == MINIC_T_CHAR || kind == MINIC_T_BOOL || kind == MINIC_T_I16 || kind == MINIC_T_U16 ? MINIC_T_INT : kind;
	s->deref_type = deref;
	s->d          = 0.0;
	minic_slot_store(s, v, kind, 0);
}

static minic_val_t minic_arith(minic_val_t a, minic_val_t b, int op) {
	if (a.type == MINIC_T_INT && b.type == MINIC_T_INT) {
		unsigned int x = (unsigned int)a.i;
		unsigned int y = (unsigned int)b.i;
		switch (op) {
		case BIN_ADD:
			return minic_val_int((int)(x + y));
		case BIN_SUB:
			return minic_val_int((int)(x - y));
		case BIN_MUL:
			return minic_val_int((int)(x * y));
		case BIN_DIV:
			return minic_val_int(b.i == 0 ? 0 : b.i == -1 ? (int)(0u - x) : a.i / b.i);
		default:
			return minic_val_int(b.i == 0 || b.i == -1 ? 0 : a.i % b.i);
		}
	}
	if (a.type == MINIC_T_FLOAT && b.type == MINIC_T_FLOAT) {
		// Same results as the double path below: one float op rounds exactly like double then float
		switch (op) {
		case BIN_ADD:
			return minic_val_float(a.f + b.f);
		case BIN_SUB:
			return minic_val_float(a.f - b.f);
		case BIN_MUL:
			return minic_val_float(a.f * b.f);
		case BIN_DIV:
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
	case BIN_ADD:
		r = da + db;
		break;
	case BIN_SUB:
		r = da - db;
		break;
	case BIN_MUL:
		r = da * db;
		break;
	case BIN_DIV:
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
	case BIN_ADD:
	case BIN_SUB:
	case BIN_MUL:
	case BIN_DIV:
	case BIN_MOD:
		return minic_arith(a, b, op);
	case BIN_SHL:
		return minic_val_int((int)((unsigned int)minic_val_to_i(a) << (minic_val_to_i(b) & 31)));
	case BIN_SHR:
		return minic_val_int(minic_val_to_i(a) >> (minic_val_to_i(b) & 31));
	case BIN_BAND:
		return minic_val_int(minic_val_to_i(a) & minic_val_to_i(b));
	case BIN_BOR:
		return minic_val_int(minic_val_to_i(a) | minic_val_to_i(b));
	case BIN_XOR:
		return minic_val_int(minic_val_to_i(a) ^ minic_val_to_i(b));
	case BIN_EQ:
		return minic_val_int(minic_val_to_d(a) == minic_val_to_d(b));
	case BIN_NE:
		return minic_val_int(minic_val_to_d(a) != minic_val_to_d(b));
	case BIN_LT:
		return minic_val_int(minic_val_to_d(a) < minic_val_to_d(b));
	case BIN_GT:
		return minic_val_int(minic_val_to_d(a) > minic_val_to_d(b));
	case BIN_LE:
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
	case MINIC_T_I16:
		return minic_val_int((int16_t)minic_val_to_i(v));
	case MINIC_T_U16:
		return minic_val_int((uint16_t)minic_val_to_i(v));
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
	int           ref;   // Register
	bool          array; // The register holds the data pointer, the next one the element count
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
	int            depth;      // Block depth, the top level of main() is 1
	int            locals_end; // Registers from here on hold temporaries
	int            slot_count; // Next free register
	minic_loop_t  *loop;
	int            last;  // Offset of the last instruction, -1 when unknown
	int            prev;  // and of the one before it
	int            label; // Last jump target, instructions before it are not rewritten
	int            expr_depth;
	int            expr_end; // Token after the outermost expression being compiled
	bool          *konst;    // Which statics are constants
	int            k0;       // Constant registers: 0, the default index, and INT_MAX, no length
	int            kmax;
} minic_comp_t;

typedef enum {
	MINIC_E_VALUE,   // In register reg
	MINIC_E_VAR,     // A variable
	MINIC_E_MEM,     // In memory at addr
	MINIC_E_ADDR,    // The address addr itself, a pointer value
	MINIC_E_FUNC,    // A function name
	MINIC_E_NEWVAR,  // Unknown name about to be assigned, declares a variable
	MINIC_E_POSTINC, // 'x++' of a float or int variable, emitted once it is known whether the old value is read
} minic_emode_t;

typedef struct {
	int  base;
	int  idx;
	int  mul;
	int  add;
	int  scale;
	int  off;
	int  len;
	int  field;
	bool indexed;
} minic_addr_t;

typedef struct {
	minic_emode_t     mode;
	minic_ctype_t     type; // MINIC_T_VOID when only known at run time
	minic_sym_t      *sym;
	int               reg;
	minic_addr_t      addr;
	int               length;    // Static element count of a decayed array field, else -1
	int               buf_delta; // '->buffer' field address to its 'length' field, 0 if none
	int               delta;     // Step of MINIC_E_POSTINC
	int               fn;        // Script function index, or -1 for a native
	minic_ext_func_t *ext;
	const char       *name;
	bool              loose; // The runtime tag may differ from type.kind: native results, void function results
	bool              konst; // A number literal
} minic_cexpr_t;

#define MINIC_ADDR_ARGS(a) (a).base, (a).idx, (a).mul, (a).add, (a).scale, (a).off, (a).len, (a).field

void console_log(char *s);

// "file:line" of pos, following #line N "file" directives
static const char *minic_where(minic_ctx_t *ctx, int pos) {
	static char where[256];
	const char *src  = ctx->src_copy;
	const char *file = ctx->filename;
	int         len  = (int)strlen(file);
	int         line = 1;
	for (int i = 0; i < pos && src[i] != '\0'; i++) {
		if ((i == 0 || src[i - 1] == '\n') && strncmp(src + i, "#line ", 6) == 0) {
			line          = atoi(src + i + 6) - 1; // The directive's own newline counts it up to N
			const char *q = strchr(src + i, '"');
			const char *e = q != NULL ? strchr(q + 1, '"') : NULL;
			if (e != NULL && memchr(src + i, '\n', q - (src + i)) == NULL) {
				file = q + 1;
				len  = (int)(e - file);
			}
		}
		if (src[i] == '\n') {
			line++;
		}
	}
	snprintf(where, sizeof(where), "%.*s:%d", len, file, line);
	return where;
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

int minic_error_count = 0;

static void minic_error(minic_comp_t *c, const char *fmt, ...) {
	if (c->error) {
		return;
	}
	minic_error_count++;
	char    msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	char log[512];
	snprintf(log, sizeof(log), "%s: error: %s (got %s)", minic_where(c->ctx, minic_tok(c)->pos), msg,
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

// Emit an instruction with n operands, return its offset
static int minic_emit(minic_comp_t *c, int op, int n, ...) {
	int at = c->ctx->code_len;
	minic_emit_word(c, op);
	va_list args;
	va_start(args, n);
	for (int k = 0; k < n; ++k) {
		minic_emit_word(c, va_arg(args, int));
	}
	va_end(args);
	c->prev = c->last;
	c->last = at;
	return at;
}

// Offset of the last instruction when it is one of first to last and no jump lands after it, else -1
static int minic_last(minic_comp_t *c, int first, int last) {
	int p = c->last;
	return p >= 0 && p >= c->label && c->ctx->code[p] >= first && c->ctx->code[p] <= last ? p : -1;
}

// Drop the last instruction
static void minic_unemit(minic_comp_t *c) {
	c->ctx->code_len = c->last;
	c->last          = c->prev;
	c->prev          = -1;
}

// The next instruction is a jump target
static int minic_label(minic_comp_t *c) {
	c->label = c->ctx->code_len;
	return c->label;
}

// Emit a jump, return its target operand for minic_patch
static int minic_emit_jump(minic_comp_t *c, int op, int target, int reg) {
	return minic_emit(c, op, op == OP_JMP ? 1 : 2, target, reg) + 1;
}

// Point a chain of jump operands at the next instruction
static void minic_patch(minic_comp_t *c, int chain) {
	if (chain > 0) {
		minic_label(c);
	}
	while (chain > 0) {
		int next            = c->ctx->code[chain];
		c->ctx->code[chain] = c->ctx->code_len;
		chain               = next;
	}
}

static int minic_static(minic_comp_t *c, minic_val_t v, bool konst) {
	minic_ctx_t *ctx = c->ctx;
	if (ctx->static_count == ctx->static_cap) {
		ctx->static_cap = ctx->static_cap > 0 ? ctx->static_cap * 2 : 64;
		ctx->statics    = realloc(ctx->statics, ctx->static_cap * sizeof(minic_val_t));
		c->konst        = realloc(c->konst, ctx->static_cap * sizeof(bool));
	}
	ctx->statics[ctx->static_count] = v;
	c->konst[ctx->static_count]     = konst;
	return ~ctx->static_count++;
}

static int minic_const(minic_comp_t *c, minic_val_t v) {
	minic_val_t *s = c->ctx->statics;
	for (int i = 0; i < c->ctx->static_count; ++i) {
		if (c->konst[i] && s[i].type == v.type && s[i].deref_type == v.deref_type && memcmp(&s[i].d, &v.d, sizeof(v.d)) == 0) {
			return ~i;
		}
	}
	return minic_static(c, v, true);
}

static int minic_kint(minic_comp_t *c, int n) {
	return minic_const(c, minic_val_int(n));
}

// The constant in register r, NULL for any other register
static minic_val_t *minic_konst(minic_comp_t *c, int r) {
	return r < 0 && c->konst[~r] ? &c->ctx->statics[~r] : NULL;
}

static bool minic_is_temp(minic_comp_t *c, int r) {
	return r >= c->locals_end;
}

static int minic_temp(minic_comp_t *c) {
	int r = c->slot_count++;
	if (c->slot_count > c->fn->slot_count) {
		c->fn->slot_count = c->slot_count;
	}
	return r;
}

// Release the temporaries from mark on
static void minic_free(minic_comp_t *c, int mark) {
	c->slot_count = mark > c->locals_end ? mark : c->locals_end;
}

// A register for the result of an instruction that reads a and b, their temporaries are released
static int minic_dest(minic_comp_t *c, int a, int b) {
	int mark = c->slot_count;
	if (minic_is_temp(c, a) && a < mark) {
		mark = a;
	}
	if (minic_is_temp(c, b) && b < mark) {
		mark = b;
	}
	minic_free(c, mark);
	return minic_temp(c);
}

// Copy register r to register to, the instruction that just computed r writes to 'to' instead when it can
static void minic_move(minic_comp_t *c, int to, int r) {
	if (r == to) {
		return;
	}
	int p = minic_last(c, OP_MOV, OP_MADD_I);
	if (p >= 0 && c->ctx->code[p + 1] == r && minic_is_temp(c, r)) {
		c->ctx->code[p + 1] = to;
		return;
	}
	minic_emit(c, OP_MOV, 2, to, r);
}

static bool minic_is_integer(minic_ctype_t t) {
	return t.kind == MINIC_T_INT || t.kind == MINIC_T_CHAR || t.kind == MINIC_T_BOOL || t.kind == MINIC_T_I16 || t.kind == MINIC_T_U16;
}

// MINIC_T_FLOAT or MINIC_T_INT when the value surely carries that tag at run time, else MINIC_T_VOID
static minic_type_t minic_typed(minic_cexpr_t e) {
	if (e.loose || e.type.pointer > 0) {
		return MINIC_T_VOID;
	}
	return e.type.kind == MINIC_T_FLOAT ? MINIC_T_FLOAT : minic_is_integer(e.type) ? MINIC_T_INT : MINIC_T_VOID;
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

// Globals are the top-level declarations plus the top level of main(), which every function sees.
// The name is set by the caller, a variable is not visible in its own initializer.
static minic_sym_t *minic_declare(minic_comp_t *c, const char *name, minic_ctype_t type, bool array) {
	minic_sym_t *sym;
	if (c->fn == &c->ctx->init || (c->in_main && c->depth == 1)) {
		// Included scripts share one scope, a repeated top-level name would silently rebind every use
		if (c->fn == &c->ctx->init) {
			for (int i = 0; i < c->global_count; ++i) {
				if (strcmp(c->globals[i].name, name) == 0) {
					minic_error(c, "redefinition of global '%s'", name);
					return NULL;
				}
			}
		}
		if (c->global_count >= MINIC_MAX_GLOBAL_VARS) {
			minic_error(c, "too many global variables (max %d), cannot declare '%s'", MINIC_MAX_GLOBAL_VARS, name);
			return NULL;
		}
		sym      = &c->globals[c->global_count++];
		sym->ref = minic_static(c, minic_val_int(0), false);
		if (array) {
			minic_static(c, minic_val_int(0), false); // The element count, register ref - 1
		}
	}
	else {
		if (c->local_count >= MINIC_MAX_VARS) {
			minic_error(c, "too many local variables (max %d), cannot declare '%s'", MINIC_MAX_VARS, name);
			return NULL;
		}
		sym      = &c->locals[c->local_count++];
		sym->ref = minic_temp(c);
		if (array) {
			minic_temp(c);
		}
		c->locals_end = c->slot_count;
	}
	sym->name[0] = '\0';
	sym->type    = type;
	sym->array   = array;
	return sym;
}

static void minic_name(minic_sym_t *sym, const char *name) {
	strncpy(sym->name, name, MINIC_MAX_NAME - 1);
	sym->name[MINIC_MAX_NAME - 1] = '\0';
}

static void minic_scope_push(minic_comp_t *c, int *saved) {
	saved[0] = c->local_count;
	saved[1] = c->locals_end;
	c->depth++;
}

static void minic_scope_pop(minic_comp_t *c, int *saved) {
	c->local_count = saved[0];
	c->locals_end  = saved[1];
	c->slot_count  = saved[1];
	c->depth--;
}

static minic_cexpr_t minic_c_assign(minic_comp_t *c);
static int           minic_c_branch(minic_comp_t *c, minic_cexpr_t e, bool when, int chain);
static minic_cexpr_t minic_c_unary(minic_comp_t *c);
static minic_cexpr_t minic_c_ternary(minic_comp_t *c);
static void          minic_c_stmt(minic_comp_t *c);

static minic_cexpr_t minic_expr(minic_emode_t mode, minic_ctype_t type, int reg) {
	minic_cexpr_t r = {0};
	r.mode          = mode;
	r.type          = type;
	r.reg           = reg;
	r.length        = -1;
	r.fn            = -1;
	return r;
}

static minic_cexpr_t minic_var_expr(minic_sym_t *sym) {
	minic_cexpr_t r = minic_expr(MINIC_E_VAR, sym->array ? minic_pointer_type(sym->type) : sym->type, sym->ref);
	r.sym           = sym;
	return r;
}

// An address at the pointer in register base
static minic_addr_t minic_addr_at(minic_comp_t *c, int base) {
	minic_addr_t a = {0};
	a.base         = base;
	a.idx          = c->k0;
	a.len          = c->kmax;
	a.field        = -1;
	return a;
}

// A register for a value computed from the address a, the temporaries of a are released
static int minic_addr_dest(minic_comp_t *c, minic_addr_t a) {
	int regs[] = {a.base, a.idx, a.len};
	int mark   = c->slot_count;
	for (int k = 0; k < 3; ++k) {
		if (minic_is_temp(c, regs[k]) && regs[k] < mark) {
			mark = regs[k];
		}
	}
	minic_free(c, mark);
	return minic_temp(c);
}

static int minic_kd(minic_ctype_t t) {
	return t.kind | minic_load_deref(t) << 8;
}

// Whether the rest of the expression may change a variable that was just read: it assigns the
// variable, or calls a script function and the variable is a global
static bool minic_changed_later(minic_comp_t *c, minic_sym_t *sym) {
	if (sym->array || sym->type.kind == MINIC_T_EMBED) {
		return false;
	}
	for (int i = c->i; i < c->expr_end; ++i) {
		minic_token_t   *t    = &c->toks[i];
		minic_tok_type_t next = c->toks[i + 1].type;
		if (t->type == TOK_IDENT && next == TOK_LPAREN && sym->ref < 0 && minic_func_index(c->ctx, t->text) >= 0) {
			return true;
		}
		if (t->type == TOK_IDENT && strcmp(t->text, sym->name) == 0 &&
		    (next == TOK_ASSIGN || (next >= TOK_PLUS_ASSIGN && next <= TOK_XOR_ASSIGN) || next == TOK_INC || next == TOK_DEC || t[-1].type == TOK_INC ||
		     t[-1].type == TOK_DEC)) {
			return true;
		}
	}
	return false;
}

static void minic_c_step(minic_comp_t *c, minic_sym_t *sym, int delta);

// Turn an expression into a value in a register
static minic_cexpr_t minic_c_load(minic_comp_t *c, minic_cexpr_t e) {
	int d;
	switch (e.mode) {
	case MINIC_E_VAR:
		e.reg = e.sym->ref;
		if (minic_changed_later(c, e.sym)) {
			e.reg = minic_temp(c); // Read it now
			minic_emit(c, OP_MOV, 2, e.reg, e.sym->ref);
		}
		break;
	case MINIC_E_MEM:
		d = minic_addr_dest(c, e.addr);
		minic_emit(c, OP_LD, 10, d, MINIC_ADDR_ARGS(e.addr), minic_kd(e.type));
		e.reg = d;
		break;
	case MINIC_E_ADDR:
		d = minic_addr_dest(c, e.addr);
		minic_emit(c, OP_LEA, 9, d, MINIC_ADDR_ARGS(e.addr));
		e.reg = d;
		break;
	case MINIC_E_POSTINC:
		e.reg = minic_temp(c);
		minic_emit(c, OP_MOV, 2, e.reg, e.sym->ref);
		minic_c_step(c, e.sym, e.delta);
		break;
	case MINIC_E_FUNC:
		if (e.fn < 0) {
			minic_error(c, "native function '%s' cannot be used as a value", e.name);
			break;
		}
		e.reg  = minic_const(c, minic_val_ptr(&c->ctx->funcs[e.fn])); // The function list is complete, the address stays
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

static minic_cexpr_t minic_c_binop(minic_comp_t *c, int o, minic_cexpr_t a, minic_cexpr_t b, int dest);
static minic_cexpr_t minic_c_store_var(minic_comp_t *c, minic_sym_t *sym, minic_cexpr_t v);

// The tag of a value of type t
static minic_type_t minic_tag(minic_ctype_t t) {
	return minic_is_integer(t) ? MINIC_T_INT : t.kind == MINIC_T_EMBED ? MINIC_T_PTR : t.kind;
}

// 'target o= v': the old value is read after v, the result converts to its type. The value of the
// expression is the old one when 'post'.
static minic_cexpr_t minic_c_update(minic_comp_t *c, minic_cexpr_t target, int o, minic_cexpr_t v, bool post) {
	minic_ctype_t t   = target.type;
	minic_type_t  tag = minic_tag(t);
	minic_cexpr_t old = minic_expr(MINIC_E_VALUE, t, target.reg);
	if (target.mode == MINIC_E_MEM) {
		old.reg = minic_temp(c); // The address stays for the store
		minic_emit(c, OP_LD, 10, old.reg, MINIC_ADDR_ARGS(target.addr), minic_kd(t));
	}
	else if (post) {
		old.reg = minic_temp(c);
		minic_emit(c, OP_MOV, 2, old.reg, target.reg);
	}
	minic_cexpr_t r = minic_c_binop(c, o, old, v, post ? minic_temp(c) : -1);
	if (tag != MINIC_T_VOID && (r.loose || minic_tag(r.type) != tag)) {
		minic_emit(c, OP_CAST, 3, r.reg, r.reg, tag);
		r.type  = minic_scalar_type(tag);
		r.loose = false;
	}
	if (tag == MINIC_T_PTR) {
		r.type = t; // Keep the pointee type
	}
	if (target.mode == MINIC_E_MEM) {
		minic_emit(c, OP_ST, 11, MINIC_ADDR_ARGS(target.addr), r.reg, t.kind, t.size);
	}
	else {
		minic_cexpr_t stored = minic_c_store_var(c, target.sym, r);
		r                    = tag == MINIC_T_VOID ? target : stored; // A variable typed at run time reads back converted
	}
	return post ? old : r;
}

// Add delta to a float or int variable
static void minic_c_step(minic_comp_t *c, minic_sym_t *sym, int delta) {
	minic_c_update(c, minic_var_expr(sym), BIN_ADD, minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), minic_kint(c, delta)), false);
}

static minic_cexpr_t minic_c_value(minic_comp_t *c) {
	return minic_c_load(c, minic_c_assign(c));
}

// A value whose result is not used
static void minic_c_drop(minic_comp_t *c, minic_cexpr_t e) {
	if (e.mode == MINIC_E_POSTINC) {
		minic_c_step(c, e.sym, e.delta);
	}
	else {
		minic_c_load(c, e);
	}
	minic_free(c, c->locals_end);
}

// Store the value v in the variable sym being declared, converted to its type
static void minic_c_init(minic_comp_t *c, minic_sym_t *sym, minic_cexpr_t *v) {
	minic_ctype_t type = sym->type;
	if (type.kind == MINIC_T_EMBED) {
		if (type.size <= 0) {
			minic_error(c, "incomplete struct type for '%s'", sym->name);
			return;
		}
		minic_emit(c, OP_INIT_EMBED, 4, sym->ref, v != NULL ? v->reg : c->k0, type.size, type.alignment);
	}
	else if (v != NULL && minic_typed(*v) == type.kind) {
		minic_move(c, sym->ref, v->reg);
	}
	else {
		minic_emit(c, OP_INIT, 4, sym->ref, v != NULL ? v->reg : c->k0, type.kind, minic_load_deref(type));
	}
}

static minic_cexpr_t minic_c_call(minic_comp_t *c, minic_cexpr_t f) {
	minic_next(c); // Consume '('
	minic_func_t *fn   = f.fn >= 0 ? &c->ctx->funcs[f.fn] : NULL;
	int           base = c->slot_count; // Arguments go to consecutive registers, the callee's first ones
	int           argc = 0;
	while (minic_cur(c) != TOK_RPAREN && minic_cur(c) != TOK_EOF && !c->error) {
		int           slot = base + argc;
		minic_cexpr_t v    = minic_c_value(c);
		minic_free(c, slot);
		if (c->slot_count != slot) {
			minic_error(c, "cannot declare a variable in a call argument");
		}
		minic_move(c, minic_temp(c), v.reg);
		if (fn != NULL && argc < fn->param_count) {
			// The callee takes its arguments converted to the parameter types, unless the types match
			minic_ctype_t p = fn->param_types[argc];
			if (p.kind != MINIC_T_EMBED && minic_typed(v) != p.kind && !(p.kind == MINIC_T_PTR && v.type.kind == MINIC_T_PTR && !v.loose)) {
				minic_emit(c, OP_INIT, 4, slot, slot, p.kind, minic_load_deref(p));
			}
		}
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
	minic_free(c, base);
	minic_temp(c); // The result
	if (fn != NULL) {
		if (argc != fn->param_count) {
			minic_error(c, "'%s' expects %d arguments, got %d", fn->name, fn->param_count, argc);
		}
		minic_emit(c, OP_CALL, 2, f.fn, base);
		bool          is_void = fn->ret_type.kind == MINIC_T_VOID;
		minic_cexpr_t r       = minic_expr(MINIC_E_VALUE, is_void ? minic_scalar_type(MINIC_T_INT) : fn->ret_type, base);
		r.loose               = is_void; // 'return x;' in a void function passes x as it is
		return r;
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
	minic_emit(c, OP_CALLN, 3, (int)(f.ext - minic_ext_funcs), base, argc);
	minic_cexpr_t r = minic_expr(MINIC_E_VALUE, minic_native_type(c, f.ext), base);
	r.loose         = true; // The signature does not bind what the native returns
	return r;
}

static minic_cexpr_t minic_c_sizeof(minic_comp_t *c) {
	minic_expect(c, TOK_LPAREN);
	minic_ctype_t type;
	int           i = c->i;
	minic_cexpr_t r = minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), 0);
	if (minic_parse_type(c, &i, false, &type)) {
		c->i  = i;
		r.reg = minic_kint(c, type.size);
	}
	else {
		minic_sym_t *sym = minic_cur(c) == TOK_IDENT ? minic_sym_find(c, minic_tok(c)->text) : NULL;
		minic_expect(c, TOK_IDENT);
		r.reg = minic_kint(c, sym != NULL ? sym->type.size : 0);
		if (sym != NULL && sym->array) {
			int count = sym->ref >= 0 ? sym->ref + 1 : sym->ref - 1;
			int d     = minic_temp(c);
			minic_emit(c, OP_MUL_I, 3, d, count, r.reg);
			r.reg = d;
		}
	}
	minic_expect(c, TOK_RPAREN);
	return r;
}

static minic_cexpr_t minic_c_primary(minic_comp_t *c) {
	minic_token_t *t = minic_tok(c);
	if (t->type == TOK_NUMBER || t->type == TOK_CHAR_LIT || t->type == TOK_STR_LIT) {
		minic_next(c);
		bool          string = t->type == TOK_STR_LIT;
		minic_cexpr_t r =
		    minic_expr(MINIC_E_VALUE, string ? minic_pointer_type(minic_scalar_type(MINIC_T_CHAR)) : minic_scalar_type(t->val.type), minic_const(c, t->val));
		r.konst = !string;
		return r;
	}
	if (t->type == TOK_IDENT) {
		const char *name = t->text;
		minic_next(c);
		if (strcmp(name, "sizeof") == 0) {
			return minic_c_sizeof(c);
		}
		minic_cexpr_t r = minic_expr(MINIC_E_FUNC, minic_scalar_type(MINIC_T_PTR), 0);
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
			return minic_var_expr(sym);
		}
		r.fn = minic_func_index(c->ctx, name);
		if (r.fn >= 0) {
			return r; // A script function passed as a callback
		}
		int ec = minic_enum_const_find(name);
		if (ec >= 0) {
			r       = minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), minic_kint(c, minic_enum_const_value_at(ec)));
			r.konst = true;
			return r;
		}
		minic_tok_type_t next = minic_cur(c);
		if (next == TOK_ASSIGN) {
			r.mode = MINIC_E_NEWVAR;
			return r;
		}
		minic_type_t kind;
		const void  *host = minic_global_ptr(name, &kind);
		if (host != NULL && next != TOK_INC && next != TOK_DEC && (next < TOK_PLUS_ASSIGN || next > TOK_XOR_ASSIGN)) {
			r      = minic_expr(MINIC_E_MEM, minic_scalar_type(kind), 0);
			r.addr = minic_addr_at(c, minic_const(c, minic_val_ptr((void *)host)));
			return minic_c_load(c, r); // A value, not assignable
		}
		minic_error(c, "unknown identifier '%s'", name);
		return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), c->k0);
	}
	if (t->type == TOK_LPAREN) {
		minic_ctype_t type;
		int           i = c->i + 1;
		if (minic_parse_type(c, &i, false, &type) && c->toks[i].type == TOK_RPAREN) {
			c->i            = i + 1;
			minic_cexpr_t v = minic_c_load(c, minic_c_unary(c));
			minic_cexpr_t r = minic_expr(MINIC_E_VALUE, type, v.reg);
			r.loose         = v.loose;
			if (type.pointer || (type.def == NULL && v.type.kind != type.kind)) {
				r.reg = minic_dest(c, v.reg, v.reg);
				minic_emit(c, OP_CAST, 3, r.reg, v.reg, type.pointer ? MINIC_T_PTR : type.kind);
				r.loose = type.pointer && v.loose;
			}
			return r;
		}
		minic_next(c);
		minic_cexpr_t r = minic_c_assign(c);
		minic_expect(c, TOK_RPAREN);
		return r;
	}
	minic_error(c, "expected expression");
	return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), c->k0);
}

static minic_cexpr_t minic_c_field(minic_comp_t *c, minic_cexpr_t owner, const char *name) {
	minic_struct_t *def = owner.type.def;
	if (def == NULL) {
		minic_error(c, "member access requires a known struct type");
		return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), c->k0);
	}
	int idx = minic_struct_field_idx(def, name);
	if (idx < 0) {
		minic_error(c, "struct '%s' has no field '%s'", def->name, name);
		return minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), c->k0);
	}
	minic_addr_t a;
	if ((owner.mode == MINIC_E_MEM && owner.type.kind == MINIC_T_EMBED) || owner.mode == MINIC_E_ADDR) {
		a = owner.addr; // The struct is at that address
	}
	else {
		a = minic_addr_at(c, minic_c_load(c, owner).reg);
	}
	a.off += def->offsets[idx];
	if (a.field < 0) {
		a.field = (int)(def - c->ctx->structs) * MINIC_MAX_STRUCT_FIELDS + idx;
	}
	minic_ctype_t type = minic_field_type(c->ctx, def, idx);
	if (def->counts[idx] > 0) {
		minic_cexpr_t r = minic_expr(MINIC_E_ADDR, minic_pointer_type(type), 0);
		r.addr          = a;
		r.length        = def->counts[idx];
		return r;
	}
	minic_cexpr_t r = minic_expr(MINIC_E_MEM, type, 0);
	r.addr          = a;
	int l           = strcmp(name, "buffer") == 0 ? minic_struct_field_idx(def, "length") : -1;
	if (l >= 0 && def->offsets[l] != def->offsets[idx]) {
		r.buf_delta = def->offsets[l] - def->offsets[idx]; // Indexing checks against the array's length
	}
	return r;
}

// 'p[i * 3 + 1]' is a single instruction: the constant addend and multiplier of the index move into the address
static void minic_fold_index(minic_comp_t *c, minic_addr_t *a) {
	int *code = c->ctx->code;
	int  p    = minic_last(c, OP_ADD_I, OP_MADD_I);
	if (p < 0 || code[p + 1] != a->idx || !minic_is_temp(c, a->idx)) {
		return;
	}
	int          x  = code[p + 2];
	int          y  = code[p + 3];
	minic_val_t *kx = minic_konst(c, x);
	minic_val_t *ky = minic_konst(c, y);
	minic_val_t *kz = code[p] == OP_MADD_I ? minic_konst(c, code[p + 4]) : NULL;
	if (code[p] == OP_MADD_I && kx != NULL && (ky != NULL || kz != NULL)) { // k + i * k
		a->add = kx->i;
		a->mul = kz != NULL ? kz->i : ky->i;
		a->idx = kz != NULL ? y : code[p + 4];
	}
	else if ((code[p] == OP_ADD_I || code[p] == OP_MUL_I) && (kx != NULL || ky != NULL)) {
		*(code[p] == OP_ADD_I ? &a->add : &a->mul) = ky != NULL ? ky->i : kx->i;
		a->idx                                     = ky != NULL ? x : y;
	}
	else {
		return;
	}
	minic_unemit(c);
}

static minic_cexpr_t minic_c_index(minic_comp_t *c, minic_cexpr_t owner) {
	minic_ctype_t element = minic_element_type(owner.type);
	minic_addr_t  a;
	if (owner.mode == MINIC_E_VAR && owner.sym->array) {
		element = owner.sym->type;
		a       = minic_addr_at(c, owner.sym->ref);
		a.len   = owner.sym->ref >= 0 ? owner.sym->ref + 1 : owner.sym->ref - 1;
	}
	else if (owner.mode == MINIC_E_MEM && owner.buf_delta != 0) {
		a              = minic_addr_at(c, minic_temp(c));
		a.len          = minic_temp(c);
		minic_addr_t l = owner.addr;
		l.off += owner.buf_delta;
		minic_emit(c, OP_LD, 10, a.base, MINIC_ADDR_ARGS(owner.addr), minic_kd(owner.type));
		minic_emit(c, OP_LD, 10, a.len, MINIC_ADDR_ARGS(l), MINIC_T_INT | MINIC_T_INT << 8);
	}
	else if (owner.mode == MINIC_E_ADDR && !owner.addr.indexed) {
		a = owner.addr;
	}
	else {
		owner = minic_c_load(c, owner);
		a     = minic_addr_at(c, owner.reg);
	}
	if (owner.length >= 0) {
		a.len = minic_kint(c, owner.length);
	}
	minic_cexpr_t i = minic_c_value(c);
	a.idx           = i.reg;
	a.mul           = 1;
	a.scale         = element.size;
	a.indexed       = true;
	if (minic_typed(i) != MINIC_T_INT) {
		a.idx = minic_dest(c, i.reg, i.reg);
		minic_emit(c, OP_CAST, 3, a.idx, i.reg, MINIC_T_INT);
	}
	else {
		minic_fold_index(c, &a);
	}
	minic_cexpr_t r = minic_expr(MINIC_E_MEM, element, 0);
	r.addr          = a;
	return r;
}

static minic_cexpr_t minic_c_increment(minic_comp_t *c, minic_cexpr_t r, int delta, bool post) {
	if ((r.mode != MINIC_E_VAR || r.sym->array) && r.mode != MINIC_E_MEM) {
		minic_error(c, "expression is not writable");
		return r;
	}
	if (r.mode == MINIC_E_VAR && post && (r.sym->type.kind == MINIC_T_FLOAT || r.sym->type.kind == MINIC_T_INT)) {
		r.mode  = MINIC_E_POSTINC;
		r.delta = delta;
		return r;
	}
	int step = r.type.kind == MINIC_T_PTR ? delta * minic_element_type(r.type).size : delta;
	return minic_c_update(c, r, BIN_ADD, minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), minic_kint(c, step)), post);
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
		if (r.mode == MINIC_E_VAR && (r.sym->array || r.sym->type.kind == MINIC_T_EMBED)) {
			r      = minic_c_load(c, r); // Arrays decay to the address of their first element, struct variables hold their address
			r.type = r.sym->array ? r.type : type;
			return r;
		}
		if (r.mode == MINIC_E_VAR) {
			int d = minic_temp(c);
			minic_emit(c, OP_ADDRV, 3, d, r.sym->ref, r.sym->type.kind);
			return minic_expr(MINIC_E_VALUE, type, d);
		}
		if (r.mode == MINIC_E_MEM) {
			r.mode = MINIC_E_ADDR;
			r.type = type;
			return r;
		}
		if (r.mode == MINIC_E_ADDR || (r.mode == MINIC_E_VALUE && r.length >= 0)) {
			return r;
		}
		minic_error(c, "expression has no address");
		return r;
	}
	case TOK_STAR:
		if (r.mode != MINIC_E_ADDR) {
			r      = minic_c_load(c, r);
			r.addr = minic_addr_at(c, r.reg);
		}
		r.mode = MINIC_E_MEM;
		r.type = minic_element_type(r.type);
		return r;
	case TOK_INC:
	case TOK_DEC:
		return minic_c_increment(c, r, op == TOK_INC ? 1 : -1, false);
	case TOK_MINUS: {
		r              = minic_c_load(c, r);
		minic_val_t *k = r.konst ? minic_konst(c, r.reg) : NULL;
		r.length       = -1;
		if (k != NULL) { // A negative literal is a constant too
			minic_val_t v = *k;
			v             = v.type == MINIC_T_INT ? minic_val_int((int)(0u - (unsigned int)v.i)) : minic_val_coerce(-minic_val_to_d(v), v.type);
			r.reg         = minic_const(c, v);
			return r;
		}
		int d = minic_dest(c, r.reg, r.reg);
		minic_emit(c, OP_NEG, 2, d, r.reg);
		r.reg   = d;
		r.konst = false;
		return r;
	}
	default: // '!x' is 'x == 0', '~x' is 'x ^ -1'
		return minic_c_binop(c, op == TOK_NOT ? BIN_EQ : BIN_XOR, minic_c_load(c, r),
		                     minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), minic_kint(c, op == TOK_NOT ? 0 : -1)), -1);
	}
}

// C precedence, loosest first, every level is left-associative. Compound assignments
// have no precedence but share the operator.
#define MINIC_PREC_MAX 10
static const struct {
	int prec;
	int op;
} minic_binops[TOK_EOF + 1] = {
    [TOK_OR]           = {1, 0},
    [TOK_AND]          = {2, 0},
    [TOK_BITOR]        = {3, BIN_BOR},
    [TOK_XOR]          = {4, BIN_XOR},
    [TOK_AMP]          = {5, BIN_BAND},
    [TOK_EQ]           = {6, BIN_EQ},
    [TOK_NEQ]          = {6, BIN_NE},
    [TOK_LT]           = {7, BIN_LT},
    [TOK_GT]           = {7, BIN_GT},
    [TOK_LE]           = {7, BIN_LE},
    [TOK_GE]           = {7, BIN_GE},
    [TOK_SHL]          = {8, BIN_SHL},
    [TOK_SHR]          = {8, BIN_SHR},
    [TOK_PLUS]         = {9, BIN_ADD},
    [TOK_MINUS]        = {9, BIN_SUB},
    [TOK_STAR]         = {10, BIN_MUL},
    [TOK_SLASH]        = {10, BIN_DIV},
    [TOK_PERCENT]      = {10, BIN_MOD},
    [TOK_PLUS_ASSIGN]  = {0, BIN_ADD},
    [TOK_MINUS_ASSIGN] = {0, BIN_SUB},
    [TOK_MUL_ASSIGN]   = {0, BIN_MUL},
    [TOK_DIV_ASSIGN]   = {0, BIN_DIV},
    [TOK_MOD_ASSIGN]   = {0, BIN_MOD},
    [TOK_SHL_ASSIGN]   = {0, BIN_SHL},
    [TOK_SHR_ASSIGN]   = {0, BIN_SHR},
    [TOK_AND_ASSIGN]   = {0, BIN_BAND},
    [TOK_OR_ASSIGN]    = {0, BIN_BOR},
    [TOK_XOR_ASSIGN]   = {0, BIN_XOR},
};

// 'p + n' and 'p += n' step by whole elements, the same stride as 'p[n]' and 'p++'
// An untyped pointer (bare 'p' natives, void *) keeps stepping in bytes
static int minic_ptr_stride(minic_ctype_t t) {
	if (t.kind != MINIC_T_PTR || (t.pointer == 1 && (t.deref == MINIC_T_PTR || t.deref == MINIC_T_VOID))) {
		return 0;
	}
	return minic_element_type(t).size;
}

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

// An int literal next to a float becomes a float when it converts exactly, one float operation
// then rounds like the double one
static void minic_float_literal(minic_comp_t *c, minic_cexpr_t *e, minic_type_t other) {
	minic_val_t *k = e->konst ? minic_konst(c, e->reg) : NULL;
	if (other == MINIC_T_FLOAT && k != NULL && k->type == MINIC_T_INT && k->i >= -(1 << 24) && k->i <= (1 << 24)) {
		e->reg  = minic_const(c, minic_val_float((float)k->i));
		e->type = minic_scalar_type(MINIC_T_FLOAT);
	}
}

// The typed instruction of a binary operator on two floats or two ints, or -1
static int minic_typed_op(int o, minic_type_t t) {
	static const int f[] = {OP_ADD_F, OP_SUB_F, OP_MUL_F, OP_DIV_F, -1, -1, -1, -1, -1, -1, OP_EQ_F, OP_NE_F, OP_LT_F, OP_LT_F, OP_LE_F, OP_LE_F};
	static const int i[] = {OP_ADD_I, OP_SUB_I, OP_MUL_I, OP_DIV_I, OP_MOD_I, -1, -1, -1, -1, -1, OP_EQ_I, OP_NE_I, OP_LT_I, OP_LT_I, OP_LE_I, OP_LE_I};
	return t == MINIC_T_FLOAT ? f[o] : t == MINIC_T_INT ? i[o] : -1;
}

// a o b into register dest, or into a new one when dest is -1
static minic_cexpr_t minic_c_binop(minic_comp_t *c, int o, minic_cexpr_t a, minic_cexpr_t b, int dest) {
	minic_float_literal(c, &b, minic_typed(a));
	minic_float_literal(c, &a, minic_typed(b));
	minic_type_t  t = minic_typed(a);
	minic_cexpr_t r = minic_expr(MINIC_E_VALUE, o <= BIN_MOD ? minic_arith_type(a.type, b.type) : minic_scalar_type(MINIC_T_INT), 0);
	r.loose         = o <= BIN_MOD && (a.loose || b.loose);
	int op          = t == minic_typed(b) ? minic_typed_op(o, t) : -1;
	int x           = o == BIN_GT || o == BIN_GE ? b.reg : a.reg; // 'a > b' is 'b < a'
	int y           = o == BIN_GT || o == BIN_GE ? a.reg : b.reg;
	if (op < 0) {
		minic_type_t tb  = minic_typed(b);
		bool         mix = t != tb && t != MINIC_T_VOID && tb != MINIC_T_VOID && o != BIN_MOD && (o <= BIN_DIV || o >= BIN_EQ);
		r.reg            = dest >= 0 ? dest : minic_dest(c, a.reg, b.reg);
		minic_emit(c, mix ? OP_MIX : OP_BIN, 4, r.reg, a.reg, b.reg, mix ? o | (t == MINIC_T_INT) << 4 | (tb == MINIC_T_INT) << 5 : o);
		return r;
	}
	int *code = c->ctx->code;
	int  p    = op == OP_ADD_F || op == OP_ADD_I ? minic_last(c, op + 2, op + 2) : -1; // MUL_F or MUL_I, the product in 'a + x * y'
	if (dest < 0 && p >= 0 && (code[p + 1] == x || code[p + 1] == y) && x != y && minic_is_temp(c, code[p + 1])) {
		int other = code[p + 1] == x ? y : x;
		int m1    = code[p + 2];
		int m2    = code[p + 3];
		minic_unemit(c);
		r.reg = minic_dest(c, a.reg, b.reg);
		minic_emit(c, op == OP_ADD_F ? OP_MADD_F : OP_MADD_I, 4, r.reg, other, m1, m2);
		return r;
	}
	r.reg = dest >= 0 ? dest : minic_dest(c, a.reg, b.reg);
	minic_emit(c, op, 3, r.reg, x, y);
	return r;
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
			int d  = minic_temp(c);
			int j1 = minic_c_branch(c, r, op == TOK_OR, 0);
			int j2 = minic_c_branch(c, minic_c_load(c, minic_c_binary(c, level + 1)), op == TOK_OR, j1);
			minic_emit(c, OP_MOV, 2, d, minic_kint(c, op == TOK_AND));
			int end = minic_emit_jump(c, OP_JMP, 0, 0);
			minic_patch(c, j2);
			minic_emit(c, OP_MOV, 2, d, minic_kint(c, op != TOK_AND));
			minic_patch(c, end);
			minic_free(c, d + 1);
			r = minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), d);
			continue;
		}
		minic_cexpr_t b = minic_c_load(c, minic_c_binary(c, level + 1));
		int           o = minic_binops[op].op;
		if ((o == BIN_ADD || o == BIN_SUB) && minic_is_integer(b.type)) {
			int stride = minic_ptr_stride(r.type);
			if (stride > 1) {
				b = minic_c_binop(c, BIN_MUL, b, minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), minic_kint(c, stride)), -1);
			}
			if (r.type.kind == MINIC_T_PTR) {
				minic_ctype_t type = r.type; // Keep the pointee type
				r                  = minic_c_binop(c, o, r, b, -1);
				r.type             = type;
				continue;
			}
		}
		r = minic_c_binop(c, o, r, b, -1);
	}
	return r;
}

// Jump to chain when e is true, or when it is false, through the comparison that computed it when there is one
static int minic_c_branch(minic_comp_t *c, minic_cexpr_t e, bool when, int chain) {
	static const int jt[] = {OP_JLT_F, OP_JLE_F, OP_JEQ_F, OP_JNE_F, OP_JLT_I, OP_JLE_I, OP_JEQ_I, OP_JNE_I};
	static const int jf[] = {OP_JNLT_F, OP_JNLE_F, OP_JNE_F, OP_JEQ_F, OP_JLE_I, OP_JLT_I, OP_JNE_I, OP_JEQ_I}; // Ints swap the operands
	int             *code = c->ctx->code;
	int              p    = minic_last(c, OP_LT_F, OP_NE_I);
	if (p < 0 || code[p + 1] != e.reg || !minic_is_temp(c, e.reg)) {
		return minic_emit_jump(c, when ? OP_JNZ : OP_JZ, chain, e.reg);
	}
	int  k    = code[p] - OP_LT_F;
	int  x    = code[p + 2];
	int  y    = code[p + 3];
	bool swap = !when && (k == 4 || k == 5);
	minic_unemit(c);
	return minic_emit(c, when ? jt[k] : jf[k], 3, chain, swap ? y : x, swap ? x : y) + 1;
}

static minic_cexpr_t minic_c_ternary(minic_comp_t *c) {
	minic_cexpr_t r = minic_c_binary(c, 1);
	if (c->error || minic_cur(c) != TOK_QUESTION) {
		return r;
	}
	r = minic_c_load(c, r);
	minic_next(c); // Consume '?'
	int           d    = minic_temp(c);
	int           skip = minic_c_branch(c, r, false, 0);
	minic_cexpr_t a    = minic_c_value(c);
	minic_move(c, d, a.reg);
	minic_expect(c, TOK_COLON);
	int end = minic_emit_jump(c, OP_JMP, 0, 0);
	minic_patch(c, skip);
	minic_cexpr_t b = minic_c_load(c, minic_c_ternary(c));
	minic_move(c, d, b.reg);
	minic_patch(c, end);
	minic_free(c, d + 1);
	r       = minic_expr(MINIC_E_VALUE, a.type.kind == b.type.kind ? a.type : minic_scalar_type(MINIC_T_VOID), d);
	r.loose = a.loose || b.loose;
	return r;
}

// Store v in a variable, converted to its type
static minic_cexpr_t minic_c_store_var(minic_comp_t *c, minic_sym_t *sym, minic_cexpr_t v) {
	minic_type_t kind = sym->type.kind;
	if (minic_typed(v) == kind && (kind == MINIC_T_FLOAT || kind == MINIC_T_INT)) {
		minic_move(c, sym->ref, v.reg);
		return minic_var_expr(sym);
	}
	minic_emit(c, OP_STV, 4, sym->ref, v.reg, kind, sym->type.size);
	return v;
}

static minic_cexpr_t minic_c_assign_to(minic_comp_t *c) {
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
			minic_name(sym, target.name);
			minic_c_init(c, sym, &v);
		}
		return v;
	}
	bool          var = target.mode == MINIC_E_VAR && !target.sym->array;
	minic_ctype_t t   = target.type;
	if (!var && target.mode != MINIC_E_MEM) {
		minic_error(c, "expression is not writable");
		return target;
	}
	if (op == TOK_ASSIGN) {
		minic_cexpr_t v = minic_c_value(c);
		if (var) {
			return minic_c_store_var(c, target.sym, v);
		}
		minic_emit(c, OP_ST, 11, MINIC_ADDR_ARGS(target.addr), v.reg, t.kind, t.size);
		return v;
	}
	int           o = minic_binops[op].op;
	minic_cexpr_t v = minic_c_value(c);
	if ((o == BIN_ADD || o == BIN_SUB) && minic_is_integer(v.type) && minic_ptr_stride(t) > 1) {
		v = minic_c_binop(c, BIN_MUL, v, minic_expr(MINIC_E_VALUE, minic_scalar_type(MINIC_T_INT), minic_kint(c, minic_ptr_stride(t))), -1);
	}
	return minic_c_update(c, target, o, v, false);
}

static minic_cexpr_t minic_c_assign(minic_comp_t *c) {
	if (c->expr_depth++ == 0) {
		int depth = 0;
		int i     = c->i;
		for (; c->toks[i].type != TOK_EOF; ++i) {
			minic_tok_type_t t = c->toks[i].type;
			if (depth == 0 && (t == TOK_SEMICOLON || t == TOK_COMMA || t == TOK_RPAREN || t == TOK_RBRACKET || t == TOK_RBRACE)) {
				break;
			}
			depth += (t == TOK_LPAREN || t == TOK_LBRACKET || t == TOK_LBRACE) - (t == TOK_RPAREN || t == TOK_RBRACKET || t == TOK_RBRACE);
		}
		c->expr_end = i;
	}
	minic_cexpr_t r = minic_c_assign_to(c);
	c->expr_depth--;
	return r;
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
			int  count = sized ? minic_c_value(c).reg : 0;
			minic_expect(c, TOK_RBRACKET);
			bool listed = minic_cur(c) == TOK_ASSIGN && minic_peek(c, 1) == TOK_LBRACE;
			if (!sized) { // 'name[]' takes its size from the initializer
				count = minic_kint(c, listed ? minic_init_list_count(c, c->i + 1) : 0);
			}
			if (type.size <= 0) {
				minic_error(c, "invalid array element type for '%s'", name);
				return;
			}
			minic_sym_t *sym = minic_declare(c, name, type, true);
			if (sym == NULL) {
				return;
			}
			minic_name(sym, name);
			minic_emit(c, OP_INIT_ARR, 6, sym->ref, sym->ref >= 0 ? sym->ref + 1 : sym->ref - 1, count, type.kind, type.size, type.alignment);
			minic_free(c, c->locals_end);
			if (listed) {
				minic_next(c); // Consume '='
				minic_next(c); // Consume '{'
				minic_addr_t a = minic_addr_at(c, sym->ref);
				a.len          = sym->ref >= 0 ? sym->ref + 1 : sym->ref - 1;
				a.scale        = type.size;
				for (int i = 0; minic_cur(c) != TOK_RBRACE && !c->error; ++i) {
					a.add = i;
					int v = minic_c_value(c).reg;
					minic_emit(c, OP_ST, 11, MINIC_ADDR_ARGS(a), v, type.kind, type.size);
					minic_free(c, c->locals_end);
					if (minic_cur(c) != TOK_COMMA) {
						break;
					}
					minic_next(c); // Consume ','
				}
				minic_expect(c, TOK_RBRACE);
			}
		}
		else {
			minic_sym_t *sym = minic_declare(c, name, type, false);
			if (sym == NULL) {
				return;
			}
			if (minic_cur(c) == TOK_ASSIGN) {
				minic_next(c);
				minic_cexpr_t v = minic_c_value(c);
				minic_c_init(c, sym, &v);
			}
			else {
				minic_c_init(c, sym, NULL);
			}
			minic_name(sym, name);
			minic_free(c, c->locals_end);
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

// A loop at the body, with the condition and the step at tokens 'cond' and 'step', -1 when empty.
// The condition is compiled twice: before the body, and after the step to jump back.
static void minic_c_loop(minic_comp_t *c, int cond, int step) {
	minic_loop_t  loop  = {0};
	minic_loop_t *outer = c->loop;
	int           body  = c->i;
	if (cond >= 0) {
		c->i        = cond;
		loop.breaks = minic_c_branch(c, minic_c_value(c), false, 0);
		minic_free(c, c->locals_end);
		c->i = body;
	}
	int top = minic_label(c);
	c->loop = &loop;
	minic_c_body(c);
	c->loop = outer;
	minic_patch(c, loop.continues);
	int after = c->i;
	if (step >= 0) {
		c->i = step;
		minic_c_drop(c, minic_c_assign(c));
	}
	if (cond >= 0) {
		c->i = cond;
		minic_c_branch(c, minic_c_value(c), true, top);
		minic_free(c, c->locals_end);
		// 'i++' then 'i < n' is a single instruction
		int *code = c->ctx->code;
		int  p    = c->last;
		int  q    = c->prev;
		if ((code[p] == OP_JLT_I || code[p] == OP_JLE_I) && q >= c->label && q + 4 == p && code[q] == OP_ADD_I && code[q + 1] == code[q + 2] &&
		    code[q + 1] == code[p + 2] && minic_konst(c, code[q + 3]) != NULL) {
			int op = code[p] == OP_JLT_I ? OP_LOOP_LT : OP_LOOP_LE;
			int x  = code[q + 1];
			int k  = minic_konst(c, code[q + 3])->i;
			int n  = code[p + 3];
			minic_unemit(c);
			minic_unemit(c);
			minic_emit(c, op, 4, top, x, k, n);
		}
	}
	else {
		minic_emit_jump(c, OP_JMP, top, 0);
	}
	c->i = after;
	minic_patch(c, loop.breaks);
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
		int r = minic_cur(c) == TOK_SEMICOLON ? c->k0 : minic_c_value(c).reg;
		minic_emit(c, OP_RET, 3, r, c->fn->ret_type.kind, minic_load_deref(c->fn->ret_type));
		minic_free(c, c->locals_end);
		minic_expect(c, TOK_SEMICOLON);
		return;
	}

	if (t == TOK_IF) {
		minic_next(c);
		minic_expect(c, TOK_LPAREN);
		int skip = minic_c_branch(c, minic_c_value(c), false, 0);
		minic_free(c, c->locals_end);
		minic_expect(c, TOK_RPAREN);
		minic_c_body(c);
		if (minic_cur(c) == TOK_ELSE) {
			minic_next(c);
			int end = minic_emit_jump(c, OP_JMP, 0, 0);
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
				minic_c_drop(c, minic_c_assign(c));
			}
			minic_expect(c, TOK_SEMICOLON);
		}
		// The condition and the step run after the body, compile them there
		int cond = minic_cur(c) != TOK_SEMICOLON ? c->i : -1;
		minic_skip_to(c, TOK_SEMICOLON);
		minic_expect(c, TOK_SEMICOLON);
		int step = minic_cur(c) != TOK_RPAREN ? c->i : -1;
		minic_skip_to(c, TOK_RPAREN);
		minic_expect(c, TOK_RPAREN);
		minic_c_loop(c, cond, step);
		minic_scope_pop(c, saved);
		return;
	}

	if (t == TOK_WHILE) {
		minic_next(c);
		minic_expect(c, TOK_LPAREN);
		int cond = c->i;
		minic_skip_to(c, TOK_RPAREN);
		minic_expect(c, TOK_RPAREN);
		minic_c_loop(c, cond, -1);
		return;
	}

	if (t == TOK_BREAK || t == TOK_CONTINUE) {
		if (c->loop == NULL) {
			minic_error(c, "%s outside a loop", minic_tok_names[t]);
			return;
		}
		minic_next(c);
		int *chain = t == TOK_BREAK ? &c->loop->breaks : &c->loop->continues;
		*chain     = minic_emit_jump(c, OP_JMP, *chain, 0);
		minic_expect(c, TOK_SEMICOLON);
		return;
	}

	minic_c_drop(c, minic_c_assign(c));
	minic_expect(c, TOK_SEMICOLON);
}

static void minic_c_function(minic_comp_t *c, minic_func_t *fn) {
	c->fn          = fn;
	c->in_main     = strcmp(fn->name, "main") == 0;
	c->depth       = 0;
	c->local_count = 0;
	c->locals_end  = 0;
	c->slot_count  = 0;
	c->loop        = NULL;
	c->i           = fn->body;
	fn->entry_ext  = minic_label(c);
	// Arguments arrive in the first registers, native callers pass them unconverted
	for (int i = 0; i < fn->param_count; ++i) {
		minic_ctype_t type = fn->param_types[i];
		minic_sym_t  *sym  = minic_declare(c, fn->params[i], type, false);
		if (sym == NULL) {
			return;
		}
		minic_name(sym, fn->params[i]);
		if (type.kind != MINIC_T_EMBED) {
			minic_emit(c, OP_INIT, 4, i, i, type.kind, minic_load_deref(type));
		}
	}
	fn->entry = minic_label(c);
	for (int i = 0; i < fn->param_count; ++i) {
		if (fn->param_types[i].kind == MINIC_T_EMBED) {
			minic_cexpr_t v = minic_expr(MINIC_E_VALUE, fn->param_types[i], i);
			minic_c_init(c, &c->locals[i], &v); // Struct arguments are copies
		}
	}
	minic_c_block(c);
	minic_emit(c, OP_RET, 3, c->k0, fn->ret_type.kind, minic_load_deref(fn->ret_type));
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
			if (t != TOK_SEMICOLON) {
				minic_error(c, "unexpected token outside of a function");
				return;
			}
			minic_next(c);
			continue;
		}
		if (minic_cur(c) != TOK_IDENT) {
			// Qualifiers such as const are parsed as an opaque type, the declared type follows
			if (!minic_tok_is_type(minic_cur(c)) && minic_cur(c) != TOK_STRUCT) {
				minic_error(c, "statement outside of a function");
				return;
			}
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
	c.last         = -1;
	c.prev         = -1;
	c.toks         = minic_tokenize(ctx->src_copy, ctx->str_pool);
	minic_emit_word(&c, OP_HALT); // Offset 0 terminates the jump patch chains
	c.locals  = malloc(MINIC_MAX_VARS * sizeof(minic_sym_t));
	c.globals = malloc(MINIC_MAX_GLOBAL_VARS * sizeof(minic_sym_t));
	c.k0      = minic_kint(&c, 0);
	c.kmax    = minic_kint(&c, INT_MAX);

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
	ctx->init.ctx       = ctx;
	ctx->init.ret_type  = minic_scalar_type(MINIC_T_VOID);
	ctx->init.entry     = minic_label(&c);
	ctx->init.entry_ext = ctx->init.entry;
	c.fn                = &ctx->init;
	if (!c.error) {
		minic_scan_top_level(&c, true);
	}
	minic_emit(&c, OP_RET, 3, c.k0, MINIC_T_VOID, MINIC_T_VOID);
	int main_idx = minic_func_index(ctx, "main");
	if (main_idx >= 0 && ctx->funcs[main_idx].body >= 0 && !c.error) {
		minic_c_function(&c, &ctx->funcs[main_idx]);
	}
	for (int i = 0; i < ctx->func_count && !c.error; ++i) {
		if (i != main_idx && ctx->funcs[i].body >= 0) {
			minic_c_function(&c, &ctx->funcs[i]);
		}
	}

	// Registers to byte offsets, statics in reverse: the instructions select the frame or the statics by the sign
	static const char *operands[] = {
#define X(name, operands) operands,
	    MINIC_OPS
#undef X
	};
	for (int pc = 0; pc < ctx->code_len; pc += 1 + (int)strlen(operands[ctx->code[pc]])) {
		for (int k = 0; operands[ctx->code[pc]][k] != '\0'; ++k) {
			ctx->code[pc + 1 + k] *= operands[ctx->code[pc]][k] == 'r' ? (int)sizeof(minic_val_t) : 1;
		}
	}
	for (int i = 0; i < ctx->static_count / 2; ++i) {
		minic_val_t v                           = ctx->statics[i];
		ctx->statics[i]                         = ctx->statics[ctx->static_count - 1 - i];
		ctx->statics[ctx->static_count - 1 - i] = v;
	}
	free(c.toks);
	free(c.locals);
	free(c.globals);
	free(c.konst);
	return !c.error;
}

// ██████╗ ██╗   ██╗███╗   ██╗
// ██╔══██╗██║   ██║████╗  ██║
// ██████╔╝██║   ██║██╔██╗ ██║
// ██╔══██╗██║   ██║██║╚██╗██║
// ██║  ██║╚██████╔╝██║ ╚████║
// ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═══╝

static void minic_runtime_error(minic_ctx_t *ctx, int pc, const char *fmt, ...) {
	minic_error_count++;
	char    msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	char log[512];
	snprintf(log, sizeof(log), "%s: error: %s", minic_where(ctx, ctx->code_pos[pc]), msg);
	console_log(log);
}

// Run a function to completion. Script calls stay in this loop, natives that call back
// into the context start a nested run above the registers of the current call.
static bool minic_run(minic_ctx_t *ctx, minic_func_t *fn, minic_val_t *args, int argc, minic_val_t *ret) {
	minic_val_t *base       = ctx->sp;
	int          base_depth = ctx->depth;
	minic_val_t *st         = ctx->statics + ctx->static_count;
	const int   *code       = ctx->code;
	*ret                    = minic_val_int(0);
	if (base + fn->slot_count > ctx->stack_end || ctx->depth >= MINIC_MAX_FRAMES) {
		minic_runtime_error(ctx, fn->entry, "out of script memory calling '%s', recursion too deep", fn->name);
		return false;
	}
	minic_val_t *fp = base;
	for (int i = 0; i < fn->slot_count; ++i) {
		fp[i] = i < argc && i < fn->param_count ? args[i] : minic_val_int(0);
	}
	minic_func_t *cur         = fn;
	ctx->frames[ctx->depth++] = (minic_frame_t){NULL, NULL, NULL};
	const int *ip             = code + fn->entry_ext;

#define REG(r) ((minic_val_t *)((r) >= 0 ? (char *)fp + (r) : (char *)st + (r)))
#define MINIC_FAIL(...)                                          \
	do {                                                         \
		minic_runtime_error(ctx, (int)(ip - code), __VA_ARGS__); \
		goto fail;                                               \
	} while (0)
// The address of the [addr] operands from ip[o] on
#define MINIC_ADDR(o, p)                                                                                                  \
	char *p = minic_val_to_ptr(*REG(ip[o]));                                                                              \
	{                                                                                                                     \
		int i_   = (int)((unsigned int)REG(ip[o + 1])->i * (unsigned int)ip[o + 2] + (unsigned int)ip[o + 3]);            \
		int len_ = REG(ip[o + 6])->i;                                                                                     \
		if ((unsigned int)i_ >= (unsigned int)len_) {                                                                     \
			MINIC_FAIL("index %d out of range (length %d)", i_, len_ == INT_MAX ? -1 : len_);                             \
		}                                                                                                                 \
		if (p != NULL) {                                                                                                  \
			p += (size_t)i_ * ip[o + 4] + ip[o + 5];                                                                      \
		}                                                                                                                 \
		else if (ip[o + 7] >= 0) {                                                                                        \
			minic_struct_t *def_ = &ctx->structs[ip[o + 7] / MINIC_MAX_STRUCT_FIELDS];                                    \
			MINIC_FAIL("null pointer access on '%s->%s'", def_->name, def_->fields[ip[o + 7] % MINIC_MAX_STRUCT_FIELDS]); \
		}                                                                                                                 \
	}
// Typed instructions, x and y are the operands in EXPR
#define MINIC_TYPED(NAME, T, M, MAKE, EXPR) \
	case OP_##NAME:                         \
		L_##NAME : {                        \
			T x         = REG(ip[2])->M;    \
			T y         = REG(ip[3])->M;    \
			*REG(ip[1]) = MAKE(EXPR);       \
			ip += 4;                        \
			MINIC_NEXT;                     \
		}
#define MINIC_JUMP(NAME, T, M, EXPR)              \
	case OP_##NAME:                               \
		L_##NAME : {                              \
			T x = REG(ip[2])->M;                  \
			T y = REG(ip[3])->M;                  \
			ip  = (EXPR) ? code + ip[1] : ip + 4; \
			MINIC_NEXT;                           \
		}
#define MINIC_WRAP(OP) (int)((unsigned int)x OP(unsigned int) y)
#ifdef MINIC_COMPUTED_GOTO
	// Each instruction jumps straight to the next one through a table of label addresses
	static const void *labels[] = {
#define X(name, operands) &&L_##name,
	    MINIC_OPS
#undef X
	};
#define MINIC_NEXT goto *labels[*ip]
	MINIC_NEXT;
#else
#define MINIC_NEXT break
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-label" // The labels after the cases are only jumped to with computed goto
#endif

	for (;;) {
		switch (*ip) {
		case OP_HALT:
		L_HALT:
			goto fail;
		case OP_MOV:
		L_MOV:
			*REG(ip[1]) = *REG(ip[2]);
			ip += 3;
			MINIC_NEXT;
		case OP_INIT:
		L_INIT:
			minic_slot_init(REG(ip[1]), *REG(ip[2]), ip[3], ip[4]);
			ip += 5;
			MINIC_NEXT;
		case OP_INIT_EMBED:
		L_INIT_EMBED: {
			void       *p = minic_alloc_aligned(ip[3], ip[4]);
			minic_val_t v = *REG(ip[2]);
			if (p == NULL) {
				MINIC_FAIL("out of script memory (%d KB)", MINIC_MEM_SIZE / 1024);
			}
			if (v.type == MINIC_T_PTR && v.p != NULL) {
				memmove(p, v.p, ip[3]);
			}
			else {
				memset(p, 0, ip[3]);
			}
			*REG(ip[1]) = minic_val_typed_ptr(p, MINIC_T_EMBED);
			ip += 5;
			MINIC_NEXT;
		}
		case OP_INIT_ARR:
		L_INIT_ARR: {
			int count = minic_val_to_i(*REG(ip[3]));
			int size  = ip[5];
			if (count < 0 || count > MINIC_MEM_SIZE / size) {
				MINIC_FAIL("invalid array size %d", count);
			}
			void *p = minic_alloc_aligned(count * size, ip[6]);
			if (p == NULL) {
				MINIC_FAIL("out of script memory (%d KB)", MINIC_MEM_SIZE / 1024);
			}
			memset(p, 0, count * size);
			*REG(ip[1]) = minic_val_typed_ptr(p, ip[4]);
			*REG(ip[2]) = minic_val_int(count);
			ip += 7;
			MINIC_NEXT;
		}
		case OP_ADDRV:
		L_ADDRV:
			*REG(ip[1]) = minic_val_typed_ptr(&REG(ip[2])->i, ip[3]);
			ip += 4;
			MINIC_NEXT;
		case OP_LD:
		L_LD: {
			MINIC_ADDR(2, p);
			minic_type_t kind = ip[10] & 0xff;
			minic_val_t *d    = REG(ip[1]);
			if (p != NULL && (kind == MINIC_T_FLOAT || kind == MINIC_T_INT)) {
				minic_val_t v = kind == MINIC_T_FLOAT ? minic_val_float(0.0f) : minic_val_int(0);
				memcpy(&v.i, p, 4); // A float or an int32
				*d = v;
			}
			else {
				*d = minic_mem_load(p, kind, ip[10] >> 8);
			}
			ip += 11;
			MINIC_NEXT;
		}
		case OP_LEA:
		L_LEA: {
			MINIC_ADDR(2, p);
			*REG(ip[1]) = minic_val_ptr(p);
			ip += 10;
			MINIC_NEXT;
		}
		case OP_BIN:
		L_BIN:
			*REG(ip[1]) = minic_binop(ip[4], *REG(ip[2]), *REG(ip[3]));
			ip += 5;
			MINIC_NEXT;
		case OP_MIX:
		L_MIX: {
			minic_val_t *a = REG(ip[2]);
			minic_val_t *b = REG(ip[3]);
			double       x = ip[4] & 16 ? (double)a->i : (double)a->f;
			double       y = ip[4] & 32 ? (double)b->i : (double)b->f;
			minic_val_t *d = REG(ip[1]);
			switch (ip[4] & 15) {
			case BIN_ADD:
				*d = minic_val_float((float)(x + y));
				break;
			case BIN_SUB:
				*d = minic_val_float((float)(x - y));
				break;
			case BIN_MUL:
				*d = minic_val_float((float)(x * y));
				break;
			case BIN_DIV:
				*d = minic_val_float(y != 0.0 ? (float)(x / y) : 0.0f);
				break;
			default:
				*d = minic_binop(ip[4] & 15, minic_val_double(x), minic_val_double(y));
				break;
			}
			ip += 5;
			MINIC_NEXT;
		}
		case OP_NEG:
		L_NEG: {
			minic_val_t v = *REG(ip[2]);
			*REG(ip[1])   = v.type == MINIC_T_INT ? minic_val_int((int)(0u - (unsigned int)v.i)) : minic_val_coerce(-minic_val_to_d(v), v.type);
			ip += 3;
			MINIC_NEXT;
		}
		case OP_CAST:
		L_CAST:
			*REG(ip[1]) = minic_cast(*REG(ip[2]), ip[3]);
			ip += 4;
			MINIC_NEXT;
			MINIC_TYPED(ADD_F, float, f, minic_val_float, x + y)
			MINIC_TYPED(SUB_F, float, f, minic_val_float, x - y)
			MINIC_TYPED(MUL_F, float, f, minic_val_float, x *y)
			MINIC_TYPED(DIV_F, float, f, minic_val_float, y != 0.0f ? x / y : 0.0f)
			MINIC_TYPED(ADD_I, int, i, minic_val_int, MINIC_WRAP(+))
			MINIC_TYPED(SUB_I, int, i, minic_val_int, MINIC_WRAP(-))
			MINIC_TYPED(MUL_I, int, i, minic_val_int, MINIC_WRAP(*))
			MINIC_TYPED(DIV_I, int, i, minic_val_int, y == 0 ? 0 : (y == -1 ? (int)(0u - (unsigned int)x) : x / y))
			MINIC_TYPED(MOD_I, int, i, minic_val_int, y == 0 || y == -1 ? 0 : x % y)
			MINIC_TYPED(LT_F, float, f, minic_val_int, x < y)
			MINIC_TYPED(LE_F, float, f, minic_val_int, x <= y)
			MINIC_TYPED(EQ_F, float, f, minic_val_int, x == y)
			MINIC_TYPED(NE_F, float, f, minic_val_int, x != y)
			MINIC_TYPED(LT_I, int, i, minic_val_int, x < y)
			MINIC_TYPED(LE_I, int, i, minic_val_int, x <= y)
			MINIC_TYPED(EQ_I, int, i, minic_val_int, x == y)
			MINIC_TYPED(NE_I, int, i, minic_val_int, x != y)
		case OP_MADD_F:
		L_MADD_F: {
			float m     = REG(ip[3])->f * REG(ip[4])->f; // Rounded before the add, no fused multiply-add
			*REG(ip[1]) = minic_val_float(REG(ip[2])->f + m);
			ip += 5;
			MINIC_NEXT;
		}
		case OP_MADD_I:
		L_MADD_I: {
			unsigned int m = (unsigned int)REG(ip[3])->i * (unsigned int)REG(ip[4])->i;
			*REG(ip[1])    = minic_val_int((int)((unsigned int)REG(ip[2])->i + m));
			ip += 5;
			MINIC_NEXT;
		}
		case OP_STV:
		L_STV: {
			minic_val_t *s = REG(ip[1]);
			minic_val_t  v = *REG(ip[2]);
			if (v.type == s->type && (v.type == MINIC_T_FLOAT || (v.type == MINIC_T_INT && ip[3] == MINIC_T_INT))) {
				s->d = v.d; // Same representation, copy the bits
			}
			else {
				minic_slot_store(s, v, ip[3], ip[4]);
			}
			ip += 5;
			MINIC_NEXT;
		}
		case OP_ST:
		L_ST: {
			MINIC_ADDR(1, p);
			minic_val_t v = *REG(ip[9]);
			if (p != NULL && v.type == (minic_type_t)ip[10] && (v.type == MINIC_T_FLOAT || v.type == MINIC_T_INT)) {
				memcpy(p, &v.i, 4); // A float or an int32
			}
			else {
				minic_mem_store(p, v, ip[10], ip[11]);
			}
			ip += 12;
			MINIC_NEXT;
		}
		case OP_JMP:
		L_JMP:
			ip = code + ip[1];
			MINIC_NEXT;
		case OP_JZ:
		L_JZ:
		case OP_JNZ:
		L_JNZ: {
			minic_val_t *v = REG(ip[2]);
			bool         t = v->type == MINIC_T_INT ? v->i != 0 : minic_val_is_true(*v);
			ip             = t == (*ip == OP_JNZ) ? code + ip[1] : ip + 3;
			MINIC_NEXT;
		}
			MINIC_JUMP(JLT_F, float, f, x < y)
			MINIC_JUMP(JLE_F, float, f, x <= y)
			MINIC_JUMP(JEQ_F, float, f, x == y)
			MINIC_JUMP(JNE_F, float, f, x != y)
			MINIC_JUMP(JLT_I, int, i, x < y)
			MINIC_JUMP(JLE_I, int, i, x <= y)
			MINIC_JUMP(JEQ_I, int, i, x == y)
			MINIC_JUMP(JNE_I, int, i, x != y)
			MINIC_JUMP(JNLT_F, float, f, !(x < y))
			MINIC_JUMP(JNLE_F, float, f, !(x <= y))
		case OP_LOOP_LT:
		L_LOOP_LT:
		case OP_LOOP_LE:
		L_LOOP_LE: {
			minic_val_t *a = REG(ip[2]);
			a->i           = (int)((unsigned int)a->i + (unsigned int)ip[3]);
			int x          = a->i;
			int y          = REG(ip[4])->i;
			ip             = (*ip == OP_LOOP_LT ? x < y : x <= y) ? code + ip[1] : ip + 5;
			MINIC_NEXT;
		}
		case OP_CALL:
		L_CALL: {
			minic_func_t *f   = &ctx->funcs[ip[1]];
			minic_val_t  *nfp = REG(ip[2]);
			if (nfp + f->slot_count > ctx->stack_end || ctx->depth >= MINIC_MAX_FRAMES) {
				MINIC_FAIL("out of script memory calling '%s', recursion too deep", f->name);
			}
			ctx->frames[ctx->depth++] = (minic_frame_t){ip + 3, fp, cur};
			fp                        = nfp;
			cur                       = f;
			ip                        = code + f->entry;
			MINIC_NEXT;
		}
		case OP_CALLN:
		L_CALLN: {
			minic_val_t *a = REG(ip[2]);
			ctx->sp        = fp + cur->slot_count; // A native may call back into this context
			minic_val_t r  = minic_dispatch(&minic_ext_funcs[ip[1]], a, ip[3]);
			*a             = r;
			if (minic_mem_oom) {
				MINIC_FAIL("out of script memory (%d KB)", MINIC_MEM_SIZE / 1024);
			}
			ip += 4;
			MINIC_NEXT;
		}
		case OP_RET:
		L_RET: {
			minic_val_t  v    = *REG(ip[1]);
			minic_type_t kind = ip[2];
			if (kind == MINIC_T_PTR) {
				v = minic_val_typed_ptr(minic_val_to_ptr(v), ip[3]);
			}
			else if (kind != MINIC_T_VOID && kind != MINIC_T_EMBED && v.type != kind) {
				v = minic_cast(v, kind);
			}
			minic_frame_t *frame = &ctx->frames[--ctx->depth];
			if (ctx->depth == base_depth) {
				*ret    = v;
				ctx->sp = base;
				return true;
			}
			*fp = v; // The caller's register base
			ip  = frame->ip;
			fp  = frame->fp;
			cur = frame->fn;
			MINIC_NEXT;
		}
		}
	}

fail:
	ctx->depth = base_depth;
	ctx->sp    = base;
	return false;
#undef REG
#undef MINIC_FAIL
#undef MINIC_ADDR
#undef MINIC_TYPED
#undef MINIC_JUMP
#undef MINIC_WRAP
#undef MINIC_NEXT
}
#ifndef MINIC_COMPUTED_GOTO
#pragma GCC diagnostic pop // After the function, gcc reports unused labels at its end
#endif

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
		free(ctx->statics);
		free(ctx->code);
		free(ctx->code_pos);
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

void *minic_global_ptr_at(int i) {
	return (void *)minic_globals[i].ptr;
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
