
// Minimal C interpreter

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
	X(TOK_VOID, "'void'") X(TOK_IDENT, "identifier") X(TOK_NUMBER, "number") X(TOK_CHAR_LIT, "char literal") X(TOK_STR_LIT, "string literal")                 \
	    X(TOK_LPAREN, "'('") X(TOK_RPAREN, "')'") X(TOK_LBRACE, "'{'") X(TOK_RBRACE, "'}'") X(TOK_LBRACKET, "'['") X(TOK_RBRACKET, "']'")                     \
	        X(TOK_SEMICOLON, "';'") X(TOK_COMMA, "','") X(TOK_ASSIGN, "'='") X(TOK_PLUS_ASSIGN, "'+='") X(TOK_MINUS_ASSIGN, "'-='") X(TOK_MUL_ASSIGN, "'*='") \
	            X(TOK_DIV_ASSIGN, "'/='") X(TOK_MOD_ASSIGN, "'%='") X(TOK_SHL_ASSIGN, "'<<='") X(TOK_SHR_ASSIGN, "'>>='") X(TOK_AND_ASSIGN, "'&='")           \
	                X(TOK_OR_ASSIGN, "'|='") X(TOK_XOR_ASSIGN, "'^='") X(TOK_EQ, "'=='") X(TOK_NEQ, "'!='") X(TOK_LT, "'<'") X(TOK_GT, "'>'")                 \
	                    X(TOK_LE, "'<='") X(TOK_GE, "'>='") X(TOK_AND, "'&&'") X(TOK_OR, "'||'") X(TOK_NOT, "'!'") X(TOK_AMP, "'&'") X(TOK_PLUS, "'+'")       \
	                        X(TOK_MINUS, "'-'") X(TOK_INC, "'++'") X(TOK_DEC, "'--'") X(TOK_STAR, "'*'") X(TOK_SLASH, "'/'") X(TOK_PERCENT, "'%'")            \
	                            X(TOK_SHL, "'<<'") X(TOK_SHR, "'>>'") X(TOK_BITOR, "'|'") X(TOK_XOR, "'^'") X(TOK_BITNOT, "'~'") X(TOK_DOT, "'.'")            \
	                                X(TOK_ARROW, "'->'") X(TOK_QUESTION, "'?'") X(TOK_COLON, "':'") X(TOK_EOF, "end of file")

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
	char             text[MINIC_MAX_NAME];
	minic_val_t      val; // TOK_NUMBER, TOK_CHAR_LIT, TOK_STR_LIT
} minic_token_t;

typedef struct {
	const char   *src;
	int           pos;
	minic_token_t cur;
} minic_lexer_t;

// Direct-mapped cache of string literals already written into the arena, keyed by
// the source offset of the opening quote.
#define MINIC_STR_CACHE_SLOTS 512

static minic_u8 *minic_active_mem       = NULL;
static int      *minic_active_mem_used  = NULL;
static int      *minic_active_mem_frame = NULL;
static int      *minic_active_str_key   = NULL; // Source offset + 1 per slot, 0 when empty
static int      *minic_active_str_off   = NULL; // Arena offset of that literal
static bool      minic_mem_oom          = false;
static bool      minic_oom_reported     = false; // Every unwinding scope sees the OOM, only the first reports it

static char minic_str_empty[1] = ""; // Stand-in when the arena is full

static int minic_str_cache_slot(int src_pos) {
	return (int)(((unsigned int)src_pos * 2654435761u) % MINIC_STR_CACHE_SLOTS);
}

// Arena offset of the literal lexed at src_pos, or -1 when it must be written again
static int minic_str_cache_get(int src_pos) {
	int slot = minic_str_cache_slot(src_pos);
	return minic_active_str_key[slot] == src_pos + 1 ? minic_active_str_off[slot] : -1;
}

static void minic_str_cache_put(int src_pos, int off) {
	int slot                   = minic_str_cache_slot(src_pos);
	minic_active_str_key[slot] = src_pos + 1;
	minic_active_str_off[slot] = off;
}

static const struct {
	const char      *kw;
	minic_tok_type_t tok;
} minic_keywords[] = {
    {"int", TOK_INT},           {"float", TOK_FLOAT},   {"char", TOK_CHAR},       {"double", TOK_DOUBLE}, {"bool", TOK_BOOL}, {"void", TOK_VOID},
    {"return", TOK_RETURN},     {"if", TOK_IF},         {"else", TOK_ELSE},       {"while", TOK_WHILE},   {"for", TOK_FOR},   {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE}, {"struct", TOK_STRUCT}, {"typedef", TOK_TYPEDEF}, {"enum", TOK_ENUM},
};

// Three-char operators are matched before the two-char table below
static const struct {
	char             a, b, c;
	minic_tok_type_t tok;
} minic_ops3[] = {
    {'<', '<', '=', TOK_SHL_ASSIGN},
    {'>', '>', '=', TOK_SHR_ASSIGN},
};

// Two-char operators must come before their one-char prefixes
static const struct {
	char             a, b;
	minic_tok_type_t tok;
} minic_ops[] = {
    {'+', '+', TOK_INC},        {'+', '=', TOK_PLUS_ASSIGN}, {'-', '-', TOK_DEC},        {'-', '=', TOK_MINUS_ASSIGN},
    {'-', '>', TOK_ARROW},      {'*', '=', TOK_MUL_ASSIGN},  {'/', '=', TOK_DIV_ASSIGN}, {'=', '=', TOK_EQ},
    {'!', '=', TOK_NEQ},        {'&', '&', TOK_AND},         {'|', '|', TOK_OR},         {'<', '=', TOK_LE},
    {'>', '=', TOK_GE},         {'<', '<', TOK_SHL},         {'>', '>', TOK_SHR},        {'%', '=', TOK_MOD_ASSIGN},
    {'&', '=', TOK_AND_ASSIGN}, {'|', '=', TOK_OR_ASSIGN},   {'^', '=', TOK_XOR_ASSIGN}, {'+', 0, TOK_PLUS},
    {'-', 0, TOK_MINUS},        {'*', 0, TOK_STAR},          {'/', 0, TOK_SLASH},        {'%', 0, TOK_PERCENT},
    {'=', 0, TOK_ASSIGN},       {'!', 0, TOK_NOT},           {'&', 0, TOK_AMP},          {'|', 0, TOK_BITOR},
    {'^', 0, TOK_XOR},          {'~', 0, TOK_BITNOT},        {'<', 0, TOK_LT},           {'>', 0, TOK_GT},
    {'(', 0, TOK_LPAREN},       {')', 0, TOK_RPAREN},        {'{', 0, TOK_LBRACE},       {'}', 0, TOK_RBRACE},
    {'[', 0, TOK_LBRACKET},     {']', 0, TOK_RBRACKET},      {';', 0, TOK_SEMICOLON},    {',', 0, TOK_COMMA},
    {'.', 0, TOK_DOT},          {'?', 0, TOK_QUESTION},      {':', 0, TOK_COLON},
};

static int minic_escape(char c) {
	switch (c) {
	case 'n':
		return '\n';
	case 't':
		return '\t';
	case 'r':
		return '\r';
	case '\\':
		return '\\';
	case '"':
		return '"';
	case '\'':
		return '\'';
	default:
		return '\0';
	}
}

// Skip whitespace, comments and preprocessor directives
static void minic_lex_skip_trivia(minic_lexer_t *l) {
	for (;;) {
		while (l->src[l->pos] != '\0' && isspace((unsigned char)l->src[l->pos])) {
			l->pos++;
		}
		if ((l->src[l->pos] == '/' && l->src[l->pos + 1] == '/') || l->src[l->pos] == '#') {
			while (l->src[l->pos] != '\0' && l->src[l->pos] != '\n') {
				l->pos++;
			}
			continue;
		}
		if (l->src[l->pos] == '/' && l->src[l->pos + 1] == '*') {
			l->pos += 2;
			while (l->src[l->pos] != '\0' && !(l->src[l->pos] == '*' && l->src[l->pos + 1] == '/')) {
				l->pos++;
			}
			if (l->src[l->pos] != '\0') {
				l->pos += 2;
			}
			continue;
		}
		return;
	}
}

static void minic_lex_next(minic_lexer_t *l) {
	for (;;) {
		minic_lex_skip_trivia(l);

		char c = l->src[l->pos];

		if (c == '\0') {
			l->cur.type = TOK_EOF;
			return;
		}

		if (c == '0' && (l->src[l->pos + 1] == 'x' || l->src[l->pos + 1] == 'X')) {
			l->pos += 2; // Consume '0x'
			unsigned int n = 0;
			while (isxdigit((unsigned char)l->src[l->pos])) {
				char h     = l->src[l->pos++];
				int  digit = (h >= '0' && h <= '9') ? h - '0' : (h >= 'a' && h <= 'f') ? h - 'a' + 10 : h - 'A' + 10;
				n          = n * 16 + digit;
			}
			l->cur.val  = minic_val_int((int)n);
			l->cur.type = TOK_NUMBER;
			return;
		}

		// Also accept a leading-dot float like .5
		if (isdigit((unsigned char)c) || (c == '.' && isdigit((unsigned char)l->src[l->pos + 1]))) {
			double n = 0;
			while (isdigit((unsigned char)l->src[l->pos])) {
				n = n * 10 + (l->src[l->pos++] - '0');
			}
			bool is_float = false;
			if (l->src[l->pos] == '.') {
				l->pos++;
				double frac = 0.1;
				while (isdigit((unsigned char)l->src[l->pos])) {
					n += (l->src[l->pos++] - '0') * frac;
					frac *= 0.1;
				}
				is_float = true;
			}
			// Exponent like 1e-3, only when digits follow so '1e' stays unconsumed
			if (l->src[l->pos] == 'e' || l->src[l->pos] == 'E') {
				int p = l->pos + 1;
				if (l->src[p] == '+' || l->src[p] == '-') {
					p++;
				}
				if (isdigit((unsigned char)l->src[p])) {
					bool neg = l->src[l->pos + 1] == '-';
					int  exp = 0;
					while (isdigit((unsigned char)l->src[p])) {
						exp = exp * 10 + (l->src[p++] - '0');
					}
					n *= pow(10.0, neg ? -exp : exp);
					l->pos   = p;
					is_float = true;
				}
			}
			if (l->src[l->pos] == 'f' || l->src[l->pos] == 'F') {
				l->pos++;
				is_float = true;
			}
			l->cur.val  = is_float ? minic_val_float((float)n) : minic_val_int((int)n);
			l->cur.type = TOK_NUMBER;
			return;
		}

		if (c == '"') {
			// Write the string into the active context's arena, unless the same
			// literal is already there from an earlier evaluation
			int  key   = l->pos; // Source offset of the opening quote
			int  hit   = minic_str_cache_get(key);
			int  start = (*minic_active_mem_used + 7) & ~7;
			int  wi    = start;
			bool store = hit < 0;
			// Adjacent string literals concatenate into a single string
			while (l->src[l->pos] == '"') {
				l->pos++; // Consume opening '"'
				while (l->src[l->pos] != '"' && l->src[l->pos] != '\0') {
					char ch = l->src[l->pos++];
					if (ch == '\\') {
						char esc = l->src[l->pos++];
						if (esc == '\n') {
							continue; // Line continuation: backslash-newline, skip both
						}
						if (esc == '\r') { // Handle \r\n line endings
							if (l->src[l->pos] == '\n') {
								l->pos++;
							}
							continue;
						}
						ch = (char)minic_escape(esc);
					}
					if (store && wi + 1 < *minic_active_mem_frame) {
						minic_active_mem[wi] = (minic_u8)ch;
					}
					wi++;
				}
				if (l->src[l->pos] == '"') {
					l->pos++; // Consume closing '"'
				}
				minic_lex_skip_trivia(l); // Whitespace or a comment may separate the literals
			}
			if (store) {
				if (wi + 1 >= *minic_active_mem_frame) { // Arena full, the script stops at the next statement
					minic_mem_oom = true;
					l->cur.type   = TOK_STR_LIT;
					l->cur.val    = minic_val_typed_ptr((void *)minic_str_empty, MINIC_T_CHAR);
					return;
				}
				minic_active_mem[wi++] = '\0';
				*minic_active_mem_used = (wi + 7) & ~7;
				minic_str_cache_put(key, start);
			}
			l->cur.type = TOK_STR_LIT;
			l->cur.val  = minic_val_typed_ptr((void *)&minic_active_mem[store ? start : hit], MINIC_T_CHAR);
			return;
		}

		if (c == '\'') {
			l->pos++; // Consume opening '
			int v;
			if (l->src[l->pos] == '\\') {
				l->pos++;
				v = minic_escape(l->src[l->pos++]);
			}
			else {
				v = (unsigned char)l->src[l->pos++];
			}
			l->pos++; // Consume closing '
			l->cur.type = TOK_CHAR_LIT;
			l->cur.val  = minic_val_int(v);
			return;
		}

		if (isalpha((unsigned char)c) || c == '_') {
			int i = 0;
			while (isalnum((unsigned char)l->src[l->pos]) || l->src[l->pos] == '_') {
				// Names past the cap are truncated, not overflowed; the rest is still consumed
				// so the identifier does not split into two tokens
				if (i < MINIC_MAX_NAME - 1) {
					l->cur.text[i++] = l->src[l->pos];
				}
				l->pos++;
			}
			l->cur.text[i] = '\0';
			for (size_t k = 0; k < sizeof(minic_keywords) / sizeof(minic_keywords[0]); ++k) {
				// The first character rules out most keywords without the call
				if (minic_keywords[k].kw[0] == l->cur.text[0] && strcmp(l->cur.text, minic_keywords[k].kw) == 0) {
					l->cur.type = minic_keywords[k].tok;
					return;
				}
			}
			if ((l->cur.text[0] == 't' && strcmp(l->cur.text, "true") == 0) || (l->cur.text[0] == 'f' && strcmp(l->cur.text, "false") == 0)) {
				l->cur.type = TOK_NUMBER;
				l->cur.val  = minic_val_int(l->cur.text[0] == 't');
				return;
			}
			l->cur.type = TOK_IDENT;
			return;
		}

		for (size_t k = 0; k < sizeof(minic_ops3) / sizeof(minic_ops3[0]); ++k) {
			if (c == minic_ops3[k].a && l->src[l->pos + 1] == minic_ops3[k].b && l->src[l->pos + 2] == minic_ops3[k].c) {
				l->pos += 3;
				l->cur.type = minic_ops3[k].tok;
				return;
			}
		}

		for (size_t k = 0; k < sizeof(minic_ops) / sizeof(minic_ops[0]); ++k) {
			if (c == minic_ops[k].a && (minic_ops[k].b == 0 || l->src[l->pos + 1] == minic_ops[k].b)) {
				l->pos += minic_ops[k].b != 0 ? 2 : 1;
				l->cur.type = minic_ops[k].tok;
				return;
			}
		}
		l->pos++; // Unknown character: skip it
	}
}

// ███████╗██╗   ██╗███╗   ██╗ ██████╗███████╗
// ██╔════╝██║   ██║████╗  ██║██╔════╝██╔════╝
// █████╗  ██║   ██║██╔██╗ ██║██║     ███████╗
// ██╔══╝  ██║   ██║██║╚██╗██║██║     ╚════██║
// ██║     ╚██████╔╝██║ ╚████║╚██████╗███████║
// ╚═╝      ╚═════╝ ╚═╝  ╚═══╝ ╚═════╝╚══════╝

static void *minic_alloc_aligned(int size, int alignment) {
	uintptr_t start   = (uintptr_t)minic_active_mem + *minic_active_mem_used;
	uintptr_t address = (start + alignment - 1) & ~(uintptr_t)(alignment - 1);
	size_t    offset  = address - (uintptr_t)minic_active_mem;
	if (size < 0 || offset > (size_t)*minic_active_mem_frame || (size_t)size > (size_t)*minic_active_mem_frame - offset) {
		minic_mem_oom = true;
		return NULL;
	}
	*minic_active_mem_used = (int)offset + size;
	return (void *)address;
}

void *minic_alloc(int size) {
	return minic_alloc_aligned(size, MINIC_ALIGNOF(long double));
}

// Allocate a call frame from the top of the arena, released when the call returns
static void *minic_frame_alloc(int size) {
	int top = (*minic_active_mem_frame - size) & ~7;
	if (size < 0 || top < *minic_active_mem_used) {
		minic_mem_oom = true; // The frame stack met the heap, the script stops
		return NULL;
	}
	*minic_active_mem_frame = top;
	return &minic_active_mem[top];
}

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
	minic_ctype_t type;
	void         *address;
	union {
		int32_t i;
		float   f;
		double  d;
		void   *p;
	} scalar;
} minic_var_t;

typedef struct {
	char          name[MINIC_MAX_NAME];
	void         *data; // contiguous native elements
	int           count;
	minic_ctype_t elem_type;
} minic_arr_t;

typedef struct {
	char          name[MINIC_MAX_NAME];
	char          params[MINIC_MAX_PARAMS][MINIC_MAX_NAME];
	minic_ctype_t param_types[MINIC_MAX_PARAMS];
	int           param_count;
	int           body_pos; // lexer position of '{' that starts the body
	minic_ctype_t ret_type;
	minic_ctx_t  *ctx; // owning context, set at parse time
} minic_func_t;

typedef struct minic_env_s {
	minic_lexer_t       lex;
	const char         *filename;
	minic_var_t        *vars;
	int                 var_count;
	int                 var_cap;
	minic_arr_t        *arrs;
	int                 arr_count;
	int                 arr_cap;
	minic_func_t       *funcs;
	int                 func_count;
	int                 func_cap;
	minic_struct_t     *structs; // shared across calls
	int                 struct_count;
	int                 struct_cap;
	bool                returning;
	bool                breaking;
	bool                continuing;
	bool                error;
	minic_val_t         return_val;
	struct minic_env_s *global_env; // top-level env that owns the script globals
} minic_env_t;

struct minic_ctx_s {
	minic_u8   *mem;
	int         mem_used;
	int         mem_frame; // Top of the call-frame stack, grows down from MINIC_MEM_SIZE
	int         str_key[MINIC_STR_CACHE_SLOTS];
	int         str_off[MINIC_STR_CACHE_SLOTS];
	minic_env_t e;
	float       result;
	char       *src_copy;
};

static minic_val_t minic_parse_cond(minic_env_t *e);
static void        minic_parse_stmt(minic_env_t *e);
static void        minic_parse_block(minic_env_t *e);
static bool        minic_parse_type(minic_env_t *e, minic_lexer_t *lex, bool opaque, minic_ctype_t *type);

static int minic_current_line(minic_env_t *e) {
	int line = 1;
	for (int i = 0; i < e->lex.pos; i++) {
		if (e->lex.src[i] == '\n') {
			line++;
		}
	}
	return line;
}

void console_log(char *s);

static void minic_error(minic_env_t *e, const char *fmt, ...) {
	if (e->error) {
		return;
	}
	char    msg[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	char log[512];
	snprintf(log, sizeof(log), "%s:%d: error: %s (got %s)", e->filename, minic_current_line(e), msg, minic_tok_names[e->lex.cur.type]);
	console_log(log);
	e->error     = true;
	e->returning = true;
}

static void minic_expect(minic_env_t *e, minic_tok_type_t expected) {
	if (e->lex.cur.type != expected) {
		minic_error(e, "expected %s", minic_tok_names[expected]);
		return;
	}
	minic_lex_next(&e->lex);
}

static bool minic_tok_is_type(minic_tok_type_t t) {
	return t == TOK_INT || t == TOK_FLOAT || t == TOK_CHAR || t == TOK_DOUBLE || t == TOK_BOOL || t == TOK_VOID;
}

static minic_type_t minic_tok_to_type(minic_tok_type_t t) {
	switch (t) {
	case TOK_INT:
		return MINIC_T_INT;
	case TOK_CHAR:
		return MINIC_T_CHAR;
	case TOK_BOOL:
		return MINIC_T_BOOL;
	case TOK_FLOAT:
		return MINIC_T_FLOAT;
	case TOK_DOUBLE:
		return MINIC_T_DOUBLE;
	default:
		return MINIC_T_VOID;
	}
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

// Compute the result of an (op)= compound assignment; TOK_ASSIGN returns b
static double minic_apply_op(minic_tok_type_t op, double a, double b) {
	switch (op) {
	case TOK_PLUS_ASSIGN:
		return a + b;
	case TOK_MINUS_ASSIGN:
		return a - b;
	case TOK_MUL_ASSIGN:
		return a * b;
	case TOK_DIV_ASSIGN:
		return b != 0.0 ? a / b : 0.0;
	case TOK_MOD_ASSIGN:
		return b != 0.0 ? fmod(a, b) : 0.0;
	case TOK_SHL_ASSIGN:
	case TOK_SHR_ASSIGN: {
		int ia = (int)a;
		int ib = (int)b;
		return (double)(op == TOK_SHL_ASSIGN ? (int)((unsigned int)ia << ib) : (ia >> ib));
	}
	case TOK_AND_ASSIGN:
		return (double)((int)a & (int)b);
	case TOK_OR_ASSIGN:
		return (double)((int)a | (int)b);
	case TOK_XOR_ASSIGN:
		return (double)((int)a ^ (int)b);
	default:
		return b;
	}
}

static bool minic_is_compound_assign(minic_tok_type_t t) {
	return t == TOK_PLUS_ASSIGN || t == TOK_MINUS_ASSIGN || t == TOK_MUL_ASSIGN || t == TOK_DIV_ASSIGN || t == TOK_MOD_ASSIGN || t == TOK_SHL_ASSIGN ||
	       t == TOK_SHR_ASSIGN || t == TOK_AND_ASSIGN || t == TOK_OR_ASSIGN || t == TOK_XOR_ASSIGN;
}

static minic_var_t *minic_var_find(minic_env_t *e, const char *name) {
	for (int i = e->var_count - 1; i >= 0; --i) {
		// As in the keyword scan, compare the first character before calling strcmp
		if (e->vars[i].name[0] == name[0] && strcmp(e->vars[i].name, name) == 0) {
			return &e->vars[i];
		}
	}
	if (e->global_env != NULL) {
		minic_env_t *g = e->global_env;
		for (int i = g->var_count - 1; i >= 0; --i) {
			if (g->vars[i].name[0] == name[0] && strcmp(g->vars[i].name, name) == 0) {
				return &g->vars[i];
			}
		}
	}
	return NULL;
}

static minic_arr_t *minic_arr_get(minic_env_t *e, const char *name) {
	for (int i = 0; i < e->arr_count; ++i) {
		if (strcmp(e->arrs[i].name, name) == 0) {
			return &e->arrs[i];
		}
	}
	if (e->global_env != NULL) {
		minic_env_t *g = e->global_env;
		for (int i = 0; i < g->arr_count; ++i) {
			if (strcmp(g->arrs[i].name, name) == 0) {
				return &g->arrs[i];
			}
		}
	}
	return NULL;
}

static minic_func_t *minic_func_get(minic_env_t *e, const char *name) {
	for (int i = 0; i < e->func_count; ++i) {
		if (strcmp(e->funcs[i].name, name) == 0) {
			return &e->funcs[i];
		}
	}
	return NULL;
}

static minic_struct_t *minic_struct_get(minic_env_t *e, const char *name) {
	for (int i = 0; i < e->struct_count; ++i) {
		if (strcmp(e->structs[i].name, name) == 0) {
			return &e->structs[i];
		}
	}
	return NULL;
}

// On failure the lexer is unchanged. Opaque names are accepted in declaration
// contexts; expression contexts only recognize registered types.
static bool minic_parse_type(minic_env_t *e, minic_lexer_t *lex, bool opaque, minic_ctype_t *type) {
	minic_lexer_t l = *lex;
	*type           = (minic_ctype_t){0};
	type->deref     = MINIC_T_PTR;
	if (minic_tok_is_type(l.cur.type)) {
		*type = minic_scalar_type(minic_tok_to_type(l.cur.type));
		minic_lex_next(&l);
	}
	else {
		bool tagged = l.cur.type == TOK_STRUCT;
		if (tagged) {
			minic_lex_next(&l);
		}
		if (l.cur.type != TOK_IDENT) {
			return false;
		}
		type->def    = minic_struct_get(e, l.cur.text);
		bool integer = minic_is_int_typedef(l.cur.text);
		if (!tagged && type->def == NULL && !integer && !opaque) {
			return false;
		}
		minic_struct_t *def = type->def;
		*type               = minic_scalar_type(integer ? MINIC_T_INT : MINIC_T_EMBED);
		type->def           = def;
		if (def != NULL) {
			type->size      = def->size;
			type->alignment = def->alignment;
		}
		minic_lex_next(&l);
	}
	while (l.cur.type == TOK_STAR) {
		*type = minic_pointer_type(*type);
		minic_lex_next(&l);
	}
	*lex = l;
	return true;
}

// Native signatures encode typed pointer returns as "p:struct_name(...)".
static minic_ctype_t minic_call_type(minic_env_t *e, const char *name) {
	// Script functions take precedence over native functions of the same name.
	minic_func_t *fn = minic_func_get(e, name);
	if (fn != NULL) {
		return fn->ret_type;
	}
	minic_ext_func_t *ext = minic_ext_func_get(name);
	if (ext == NULL || strncmp(ext->sig, "p:", 2) != 0) {
		return minic_scalar_type(MINIC_T_PTR);
	}
	const char *start = ext->sig + 2;
	const char *end   = strchr(start, '(');
	if (end == NULL || end == start || end - start >= MINIC_MAX_NAME) {
		return minic_scalar_type(MINIC_T_PTR);
	}
	char type_name[MINIC_MAX_NAME];
	memcpy(type_name, start, end - start);
	type_name[end - start] = '\0';
	minic_lexer_t l        = {0};
	l.src                  = type_name;
	minic_lex_next(&l);
	minic_ctype_t type;
	if (minic_parse_type(e, &l, true, &type)) {
		return minic_pointer_type(type);
	}
	return minic_scalar_type(MINIC_T_PTR);
}

static int minic_struct_field_idx(minic_struct_t *def, const char *field) {
	for (int i = 0; i < def->field_count; ++i) {
		if (strcmp(def->fields[i], field) == 0) {
			return i;
		}
	}
	return -1;
}

// Every reference addresses native-layout storage. Only temporary expression
// values and the host-call interface use minic_val_t.
typedef struct {
	minic_val_t    value;
	void          *address;
	minic_ctype_t  type;
	minic_ctype_t *inferred; // undeclared variable: infer its type on first store
	bool           writable;
	const char    *call_name;
	int            length; // -1 when the pointer has no known bounds
} minic_expr_t;

static minic_expr_t minic_value(minic_val_t value) {
	minic_expr_t r = {0};
	r.value        = value;
	r.type         = minic_scalar_type(value.type);
	r.type.deref   = value.deref_type;
	r.length       = -1;
	return r;
}

static minic_expr_t minic_reference(void *address, minic_ctype_t type) {
	minic_expr_t r = minic_value(minic_val_int(0));
	r.address      = address;
	r.type         = type;
	r.writable     = true;
	return r;
}

static minic_val_t minic_load(minic_expr_t r) {
	if (!r.writable) {
		return r.value;
	}
	void *p = r.address;
	if (p == NULL) {
		return minic_val_int(0);
	}
	switch (r.type.kind) {
	case MINIC_T_PTR: {
		void *pointer;
		memcpy(&pointer, p, sizeof(pointer));
		return minic_val_typed_ptr(pointer, r.type.pointer > 1 ? MINIC_T_PTR : r.type.deref);
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

static void minic_store(minic_env_t *e, minic_expr_t r, minic_val_t v) {
	if (!r.writable) {
		minic_error(e, "expression is not writable");
		return;
	}
	void *p = r.address;
	if (p == NULL || e->error) {
		return;
	}
	if (r.inferred != NULL) {
		r.type      = minic_value(v).type;
		*r.inferred = r.type;
	}
	switch (r.type.kind) {
	case MINIC_T_PTR: {
		void *pointer = minic_val_to_ptr(v);
		memcpy(p, &pointer, sizeof(pointer));
		break;
	}
	case MINIC_T_EMBED:
		if (v.type == MINIC_T_PTR && v.p != NULL) {
			memmove(p, v.p, r.type.size);
		}
		break;
	case MINIC_T_FLOAT: {
		float n = (float)minic_val_to_d(v);
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
		*(minic_u8 *)p = (minic_u8)minic_val_to_d(v);
		break;
	default: {
		int32_t n = (int32_t)minic_val_to_d(v);
		memcpy(p, &n, sizeof(n));
		break;
	}
	}
}

static minic_var_t *minic_var_decl(minic_env_t *e, const char *name, minic_ctype_t type, minic_val_t init) {
	if (e->var_count >= e->var_cap) {
		minic_error(e, "too many local variables (max %d), cannot declare '%s'", e->var_cap, name);
		return NULL;
	}
	minic_var_t *var = &e->vars[e->var_count++];
	memset(var, 0, sizeof(*var));
	strncpy(var->name, name, MINIC_MAX_NAME - 1);
	var->type    = type;
	var->address = &var->scalar;
	if (type.kind == MINIC_T_EMBED) {
		if (type.size <= 0) {
			minic_error(e, "incomplete struct type for '%s'", name);
			return NULL;
		}
		var->address = minic_alloc_aligned(type.size, type.alignment);
		if (var->address == NULL) {
			minic_error(e, "out of script memory, cannot declare '%s'", name);
			return NULL;
		}
		memset(var->address, 0, type.size);
	}
	minic_store(e, minic_reference(var->address, type), init);
	return var;
}

static minic_arr_t *minic_arr_decl(minic_env_t *e, const char *name, int count, minic_ctype_t type) {
	if (count < 0 || type.size <= 0 || count > MINIC_MEM_SIZE / type.size) {
		minic_error(e, "invalid array size %d for '%s'", count, name);
		return NULL;
	}
	if (e->arr_count >= e->arr_cap) {
		minic_error(e, "too many arrays (max %d), cannot declare '%s'", e->arr_cap, name);
		return NULL;
	}
	minic_arr_t *a = &e->arrs[e->arr_count++];
	strncpy(a->name, name, MINIC_MAX_NAME - 1);
	a->data = minic_alloc_aligned(count * type.size, type.alignment);
	if (a->data == NULL) {
		e->arr_count--;
		minic_error(e, "out of script memory, cannot declare '%s'", name);
		return NULL;
	}
	a->count     = count;
	a->elem_type = type;
	memset(a->data, 0, count * type.size);
	return a;
}

static minic_ctype_t minic_field_type(minic_env_t *e, minic_struct_t *def, int idx) {
	minic_ctype_t type = minic_scalar_type(def->types[idx]);
	type.deref         = def->deref_types[idx];
	type.pointer       = def->pointer_depths[idx];
	type.def           = minic_struct_get(e, def->field_structs[idx]);
	if (type.kind == MINIC_T_EMBED && type.def != NULL) {
		type.size      = type.def->size;
		type.alignment = type.def->alignment;
	}
	return type;
}

static minic_expr_t minic_field(minic_env_t *e, minic_expr_t owner, const char *name) {
	minic_expr_t    r   = minic_value(minic_val_int(0));
	minic_struct_t *def = owner.type.def;
	if (def == NULL) {
		minic_error(e, "member access requires a known struct type");
		return r;
	}
	void *base = minic_val_to_ptr(minic_load(owner));
	int   idx  = minic_struct_field_idx(def, name);
	if (base == NULL || idx < 0) {
		minic_error(e, base == NULL ? "null pointer access on '%s->%s'" : "struct '%s' has no field '%s'", def->name, name);
		return r;
	}
	minic_ctype_t type    = minic_field_type(e, def, idx);
	void         *address = (char *)base + def->offsets[idx];
	if (def->counts[idx] > 0) {
		r        = minic_value(minic_val_typed_ptr(address, type.kind));
		r.type   = minic_pointer_type(type);
		r.length = def->counts[idx];
	}
	else {
		r = minic_reference(address, type);
	}
	if (strcmp(name, "buffer") == 0 && minic_struct_field_idx(def, "length") >= 0) {
		r.length = (int)minic_val_to_d(minic_load(minic_field(e, owner, "length")));
	}
	return r;
}

static minic_expr_t minic_index(minic_env_t *e, minic_expr_t owner, int idx) {
	if (idx < 0 || (owner.length >= 0 && idx >= owner.length)) {
		minic_error(e, "index %d out of range (length %d)", idx, owner.length);
		return minic_value(minic_val_int(0));
	}
	minic_val_t   pointer = minic_load(owner);
	minic_ctype_t element = minic_element_type(owner.type);
	void         *base    = pointer.type == MINIC_T_PTR ? pointer.p : NULL;
	return minic_reference(base != NULL ? (char *)base + (size_t)idx * element.size : NULL, element);
}

static minic_val_t minic_call(minic_env_t *e, minic_func_t *fn, minic_val_t *args, int argc) {
	int saved_frame = *minic_active_mem_frame; // The frame is released when the call returns

	minic_env_t child  = {0};
	child.lex.src      = e->lex.src;
	child.lex.pos      = fn->body_pos;
	child.filename     = e->filename;
	child.var_cap      = MINIC_MAX_VARS;
	child.vars         = minic_frame_alloc(child.var_cap * (int)sizeof(minic_var_t));
	child.global_env   = e->global_env != NULL ? e->global_env : e;
	child.arr_cap      = 32;
	child.arrs         = minic_frame_alloc(child.arr_cap * (int)sizeof(minic_arr_t));
	child.func_count   = e->func_count;
	child.func_cap     = e->func_cap;
	child.funcs        = e->funcs;
	child.struct_count = e->struct_count;
	child.struct_cap   = e->struct_cap;
	child.structs      = e->structs;
	// The arena cannot hold another frame, usually runaway recursion. Report against the
	// call site, which still has a line number, and unwind without touching the body
	if (child.vars == NULL || child.arrs == NULL) {
		if (!minic_oom_reported) {
			minic_oom_reported = true;
			minic_error(e, "out of script memory (%d KB) calling '%s', recursion too deep", MINIC_MEM_SIZE / 1024, fn->name);
		}
		e->error                = true;
		e->returning            = true;
		*minic_active_mem_frame = saved_frame;
		return minic_val_void();
	}
	// Bind parameters
	for (int i = 0; i < argc && i < fn->param_count; ++i) {
		minic_var_decl(&child, fn->params[i], fn->param_types[i], args[i]);
	}
	minic_lex_next(&child.lex);
	minic_parse_block(&child);
	*minic_active_mem_frame = saved_frame; // Release the frame
	return child.return_val;
}

static minic_val_t minic_call_in_ctx(minic_ctx_t *ctx, minic_func_t *fn, minic_val_t *args, int argc) {
	minic_u8 *prev_mem       = minic_active_mem;
	int      *prev_mem_used  = minic_active_mem_used;
	int      *prev_mem_frame = minic_active_mem_frame;
	int      *prev_str_key   = minic_active_str_key;
	int      *prev_str_off   = minic_active_str_off;
	minic_active_mem         = ctx->mem;
	minic_active_mem_used    = &ctx->mem_used;
	minic_active_mem_frame   = &ctx->mem_frame;
	minic_active_str_key     = ctx->str_key;
	minic_active_str_off     = ctx->str_off;
	int         saved_used   = ctx->mem_used;
	minic_val_t r            = minic_call(&ctx->e, fn, args, argc);
	ctx->mem_used            = saved_used; // Rewind, the arena is free again
	// The released region may be handed out again, so its cached literals are gone
	memset(ctx->str_key, 0, sizeof(ctx->str_key));
	minic_active_mem       = prev_mem;
	minic_active_mem_used  = prev_mem_used;
	minic_active_mem_frame = prev_mem_frame;
	minic_active_str_key   = prev_str_key;
	minic_active_str_off   = prev_str_off;
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

static minic_val_t minic_arith(minic_val_t a, minic_val_t b, minic_tok_type_t op) {
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
	if (op == TOK_PERCENT) {
		double r = 0.0;
		if (rt == MINIC_T_FLOAT || rt == MINIC_T_DOUBLE) {
			r = db != 0.0 ? fmod(da, db) : 0.0;
		}
		else {
			int ib = (int)db;
			r      = ib != 0 ? (double)((int)da % ib) : 0.0;
		}
		return minic_val_coerce(r, rt);
	}
	double r = op == TOK_PLUS ? da + db : op == TOK_MINUS ? da - db : op == TOK_STAR ? da * db : (db != 0.0 ? da / db : 0.0);
	return minic_val_coerce(r, rt);
}

// Parse a call argument list (after '(') and invoke a script or extern function
static minic_val_t minic_parse_call(minic_env_t *e, const char *name) {
	minic_val_t args[MINIC_MAX_ARGS];
	int         argc    = 0;
	int         dropped = 0;
	while (e->lex.cur.type != TOK_RPAREN && e->lex.cur.type != TOK_EOF && !e->error) {
		minic_val_t v = minic_parse_cond(e);
		if (argc < MINIC_MAX_ARGS) {
			args[argc++] = v;
		}
		else {
			dropped++;
		}
		if (e->lex.cur.type == TOK_COMMA) {
			minic_lex_next(&e->lex);
		}
	}
	minic_expect(e, TOK_RPAREN);
	if (dropped > 0) {
		minic_error(e, "too many arguments (max %d)", MINIC_MAX_ARGS);
		return minic_val_int(0);
	}
	minic_func_t *fn = minic_func_get(e, name);
	if (fn != NULL) {
		return minic_call(e, fn, args, argc);
	}
	minic_ext_func_t *ext = minic_ext_func_get(name);
	if (ext != NULL) {
		return minic_dispatch(ext, args, argc);
	}
	minic_error(e, "unknown function '%s'", name);
	return minic_val_int(0);
}

static minic_val_t minic_parse_sizeof(minic_env_t *e) {
	minic_expect(e, TOK_LPAREN);
	minic_ctype_t type;
	if (minic_parse_type(e, &e->lex, false, &type)) {
		minic_expect(e, TOK_RPAREN);
		return minic_val_int(type.size);
	}
	char name[MINIC_MAX_NAME];
	strncpy(name, e->lex.cur.text, MINIC_MAX_NAME - 1);
	name[MINIC_MAX_NAME - 1] = '\0';
	minic_expect(e, TOK_IDENT);
	minic_expect(e, TOK_RPAREN);
	minic_arr_t *arr = minic_arr_get(e, name);
	if (arr != NULL) {
		return minic_val_int(arr->count * arr->elem_type.size);
	}
	minic_var_t *var = minic_var_find(e, name);
	if (var == NULL) {
		return minic_val_int(0);
	}
	return minic_val_int(var->type.size);
}

static minic_expr_t minic_parse_assignment(minic_env_t *e);
static minic_expr_t minic_parse_unary(minic_env_t *e);

static minic_expr_t minic_parse_atom(minic_env_t *e) {
	if (e->lex.cur.type == TOK_NUMBER || e->lex.cur.type == TOK_CHAR_LIT || e->lex.cur.type == TOK_STR_LIT) {
		minic_val_t v = e->lex.cur.val;
		minic_lex_next(&e->lex);
		return minic_value(v);
	}
	if (e->lex.cur.type == TOK_IDENT) {
		char name[MINIC_MAX_NAME];
		strncpy(name, e->lex.cur.text, MINIC_MAX_NAME - 1);
		name[MINIC_MAX_NAME - 1] = '\0';
		minic_lex_next(&e->lex);
		if (strcmp(name, "sizeof") == 0) {
			return minic_value(minic_parse_sizeof(e));
		}
		minic_func_t     *fn  = minic_func_get(e, name);
		minic_ext_func_t *ext = minic_ext_func_get(name);
		if (fn != NULL || (ext != NULL && e->lex.cur.type == TOK_LPAREN)) {
			minic_expr_t r = minic_value(minic_val_ptr(fn));
			r.call_name    = fn != NULL ? fn->name : ext->name;
			return r;
		}
		if (e->lex.cur.type == TOK_LPAREN) {
			minic_error(e, "unknown function '%s'", name);
			return minic_value(minic_val_int(0));
		}
		int ec = minic_enum_const_get(name);
		if (ec >= 0) {
			return minic_value(minic_val_int(ec));
		}
		minic_arr_t *arr = minic_arr_get(e, name);
		if (arr != NULL) {
			minic_expr_t r = minic_value(minic_val_typed_ptr(arr->data, arr->elem_type.kind));
			r.type         = minic_pointer_type(arr->elem_type);
			r.length       = arr->count;
			return r;
		}
		minic_var_t *var = minic_var_find(e, name);
		minic_val_t  global;
		bool assigning = e->lex.cur.type == TOK_ASSIGN || minic_is_compound_assign(e->lex.cur.type) || e->lex.cur.type == TOK_INC || e->lex.cur.type == TOK_DEC;
		if (var == NULL && !assigning && minic_global_get(name, &global)) {
			return minic_value(global);
		}
		if (var == NULL) {
			var = minic_var_decl(e, name, minic_scalar_type(MINIC_T_VOID), minic_val_int(0));
		}
		if (var == NULL) {
			return minic_value(minic_val_int(0));
		}
		minic_expr_t r = minic_reference(var->address, var->type);
		if (var->type.kind == MINIC_T_VOID) {
			r.inferred = &var->type;
		}
		return r;
	}
	if (e->lex.cur.type == TOK_LPAREN) {
		minic_lexer_t saved = e->lex;
		minic_lex_next(&e->lex);
		minic_ctype_t type;
		if (minic_parse_type(e, &e->lex, false, &type) && e->lex.cur.type == TOK_RPAREN) {
			minic_lex_next(&e->lex);
			minic_val_t v = minic_load(minic_parse_unary(e));
			if (type.pointer) {
				v = minic_val_typed_ptr(minic_val_to_ptr(v), type.deref);
			}
			else if (type.def == NULL) {
				v = minic_val_cast(v, type.kind);
			}
			minic_expr_t r = minic_value(v);
			r.type         = type;
			return r;
		}
		e->lex = saved;
		minic_lex_next(&e->lex);
		minic_expr_t r = minic_parse_assignment(e);
		minic_expect(e, TOK_RPAREN);
		return r;
	}
	// A missing expression is used by void returns and empty loop clauses.
	return minic_value(minic_val_int(0));
}

static minic_expr_t minic_increment(minic_env_t *e, minic_expr_t r, minic_tok_type_t op, bool prefix) {
	minic_val_t old   = minic_load(r);
	int         delta = op == TOK_INC ? 1 : -1;
	minic_val_t next;
	if (old.type == MINIC_T_PTR) {
		int stride = minic_element_type(r.type).size;
		next       = old; // Keep the pointee type as well as the pointer's exact bits.
		if (old.p != NULL) {
			next.p = (char *)old.p + delta * stride;
		}
	}
	else {
		next = minic_val_coerce(minic_val_to_d(old) + delta, old.type);
	}
	minic_store(e, r, next);
	minic_expr_t result = minic_value(prefix ? next : old);
	result.type         = r.type;
	return result;
}

static minic_expr_t minic_parse_postfix(minic_env_t *e) {
	minic_expr_t r = minic_parse_atom(e);
	while (!e->error) {
		if (e->lex.cur.type == TOK_LPAREN) {
			if (r.call_name == NULL) {
				minic_error(e, "expression is not callable");
				break;
			}
			const char *name = r.call_name;
			minic_lex_next(&e->lex);
			r = minic_value(minic_parse_call(e, name));
			if (r.value.type == MINIC_T_PTR) {
				r.type = minic_call_type(e, name);
			}
		}
		else if (e->lex.cur.type == TOK_DOT || e->lex.cur.type == TOK_ARROW) {
			minic_lex_next(&e->lex);
			char field[MINIC_MAX_NAME];
			strncpy(field, e->lex.cur.text, MINIC_MAX_NAME - 1);
			field[MINIC_MAX_NAME - 1] = '\0';
			minic_expect(e, TOK_IDENT);
			r = minic_field(e, r, field);
		}
		else if (e->lex.cur.type == TOK_LBRACKET) {
			minic_lex_next(&e->lex);
			int idx = (int)minic_val_to_d(minic_parse_cond(e));
			minic_expect(e, TOK_RBRACKET);
			r = minic_index(e, r, idx);
		}
		else if (e->lex.cur.type == TOK_INC || e->lex.cur.type == TOK_DEC) {
			minic_tok_type_t op = e->lex.cur.type;
			minic_lex_next(&e->lex);
			r = minic_increment(e, r, op, false);
		}
		else {
			break;
		}
	}
	return r;
}

static minic_expr_t minic_parse_unary(minic_env_t *e) {
	minic_tok_type_t op = e->lex.cur.type;
	if (op != TOK_AMP && op != TOK_STAR && op != TOK_MINUS && op != TOK_NOT && op != TOK_BITNOT && op != TOK_INC && op != TOK_DEC) {
		return minic_parse_postfix(e);
	}
	minic_lex_next(&e->lex);
	minic_expr_t r = minic_parse_unary(e);
	minic_val_t  v = minic_load(r);
	switch (op) {
	case TOK_AMP: {
		if (!r.writable && r.length < 0) {
			minic_error(e, "expression has no address");
			return minic_value(minic_val_ptr(NULL));
		}
		if (!r.writable) {
			return r; // Arrays already decay to the address of their first element.
		}
		minic_expr_t address = minic_value(minic_val_typed_ptr(r.address, r.type.kind));
		address.type         = minic_pointer_type(r.type);
		return address;
	}
	case TOK_STAR:
		return minic_reference(minic_val_to_ptr(v), minic_element_type(r.type));
	case TOK_INC:
	case TOK_DEC:
		return minic_increment(e, r, op, true);
	case TOK_MINUS:
		return minic_value(minic_val_coerce(-minic_val_to_d(v), v.type));
	case TOK_NOT:
		return minic_value(minic_val_int(!minic_val_is_true(v)));
	default:
		return minic_value(minic_val_int(~(int)minic_val_to_d(v)));
	}
}

// C precedence, loosest first; every level is left-associative.
#define MINIC_PREC_MAX 10
static int minic_binary_precedence(minic_tok_type_t op) {
	switch (op) {
	case TOK_OR:
		return 1;
	case TOK_AND:
		return 2;
	case TOK_BITOR:
		return 3;
	case TOK_XOR:
		return 4;
	case TOK_AMP:
		return 5;
	case TOK_EQ:
	case TOK_NEQ:
		return 6;
	case TOK_LT:
	case TOK_GT:
	case TOK_LE:
	case TOK_GE:
		return 7;
	case TOK_SHL:
	case TOK_SHR:
		return 8;
	case TOK_PLUS:
	case TOK_MINUS:
		return 9;
	case TOK_STAR:
	case TOK_SLASH:
	case TOK_PERCENT:
		return 10;
	default:
		return 0;
	}
}

static minic_val_t minic_binary(minic_val_t lhs, minic_val_t rhs, minic_tok_type_t op) {
	double a = minic_val_to_d(lhs);
	double b = minic_val_to_d(rhs);
	switch (op) {
	case TOK_AND:
		return minic_val_int(minic_val_is_true(lhs) && minic_val_is_true(rhs));
	case TOK_OR:
		return minic_val_int(minic_val_is_true(lhs) || minic_val_is_true(rhs));
	case TOK_BITOR:
		return minic_val_int((int)a | (int)b);
	case TOK_XOR:
		return minic_val_int((int)a ^ (int)b);
	case TOK_AMP:
		return minic_val_int((int)a & (int)b);
	case TOK_EQ:
		return minic_val_int(a == b);
	case TOK_NEQ:
		return minic_val_int(a != b);
	case TOK_LT:
		return minic_val_int(a < b);
	case TOK_GT:
		return minic_val_int(a > b);
	case TOK_LE:
		return minic_val_int(a <= b);
	case TOK_GE:
		return minic_val_int(a >= b);
	case TOK_SHL:
		return minic_val_int((int)((unsigned int)(int)a << (int)b));
	case TOK_SHR:
		return minic_val_int((int)a >> (int)b);
	default:
		return minic_arith(lhs, rhs, op);
	}
}

// Each level consumes the next tighter level, keeping left-to-right evaluation.
static minic_expr_t minic_parse_binary(minic_env_t *e, int level) {
	if (level > MINIC_PREC_MAX) {
		return minic_parse_unary(e);
	}

	minic_expr_t r = minic_parse_binary(e, level + 1);
	while (!e->error && minic_binary_precedence(e->lex.cur.type) == level) {
		minic_tok_type_t op  = e->lex.cur.type;
		minic_val_t      lhs = minic_load(r);
		minic_lex_next(&e->lex);
		minic_val_t rhs = minic_load(minic_parse_binary(e, level + 1));
		r               = minic_value(minic_binary(lhs, rhs, op));
	}
	return r;
}

static void minic_skip_ternary_operand(minic_env_t *e) {
	int depth  = 0; // Open (, [ and {
	int nested = 0; // Unmatched '?' of inner ternaries
	while (e->lex.cur.type != TOK_EOF) {
		minic_tok_type_t t = e->lex.cur.type;
		if (t == TOK_LPAREN || t == TOK_LBRACKET || t == TOK_LBRACE) {
			depth++;
		}
		else if (t == TOK_RPAREN || t == TOK_RBRACKET || t == TOK_RBRACE) {
			if (depth == 0) {
				return;
			}
			depth--;
		}
		else if (depth == 0) {
			if (t == TOK_SEMICOLON || t == TOK_COMMA) {
				return;
			}
			if (t == TOK_QUESTION) {
				nested++;
			}
			else if (t == TOK_COLON) {
				if (nested == 0) {
					return;
				}
				nested--;
			}
		}
		minic_lex_next(&e->lex);
	}
}

static minic_expr_t minic_parse_ternary(minic_env_t *e) {
	minic_expr_t r = minic_parse_binary(e, 1);
	if (e->error || e->lex.cur.type != TOK_QUESTION) {
		return r;
	}
	bool taken = minic_val_is_true(minic_load(r));
	minic_lex_next(&e->lex); // Consume '?'
	minic_val_t v = minic_val_int(0);
	if (taken) {
		v = minic_load(minic_parse_assignment(e));
		minic_expect(e, TOK_COLON);
		minic_skip_ternary_operand(e);
	}
	else {
		minic_skip_ternary_operand(e);
		minic_expect(e, TOK_COLON);
		v = minic_load(minic_parse_ternary(e));
	}
	return minic_value(v);
}

static minic_expr_t minic_parse_assignment(minic_env_t *e) {
	minic_expr_t     target = minic_parse_ternary(e);
	minic_tok_type_t op     = e->lex.cur.type;
	if (!e->error && (op == TOK_ASSIGN || minic_is_compound_assign(op))) {
		minic_lex_next(&e->lex);
		minic_val_t v = minic_load(minic_parse_assignment(e));
		if (op != TOK_ASSIGN) {
			// Existing assignments read the old value after evaluating the RHS.
			minic_val_t old = minic_load(target);
			v               = minic_val_coerce(minic_apply_op(op, minic_val_to_d(old), minic_val_to_d(v)), old.type);
		}
		minic_store(e, target, v);
		return minic_value(v);
	}
	return target;
}

static minic_val_t minic_parse_cond(minic_env_t *e) {
	return minic_load(minic_parse_assignment(e));
}

// Skip the parenthesised header of an if/for/while, leaving the first body token current
static void minic_skip_header(minic_env_t *e) {
	int depth = 0;
	do {
		if (e->lex.cur.type == TOK_LPAREN) {
			depth++;
		}
		if (e->lex.cur.type == TOK_RPAREN) {
			depth--;
		}
		minic_lex_next(&e->lex);
	} while (depth > 0 && e->lex.cur.type != TOK_EOF);
}

// Skip one statement without executing it
static void minic_skip_block(minic_env_t *e) {
	if (e->lex.cur.type == TOK_LBRACE) {
		minic_lex_next(&e->lex); // Consume '{'
		int depth = 1;
		while (depth > 0 && e->lex.cur.type != TOK_EOF) {
			if (e->lex.cur.type == TOK_LBRACE) {
				depth++;
			}
			if (e->lex.cur.type == TOK_RBRACE) {
				depth--;
			}
			minic_lex_next(&e->lex);
		}
		return;
	}

	// Control statement: skip its own header, then its body
	if (e->lex.cur.type == TOK_IF || e->lex.cur.type == TOK_FOR || e->lex.cur.type == TOK_WHILE) {
		bool is_if = (e->lex.cur.type == TOK_IF);
		minic_lex_next(&e->lex); // Consume the keyword
		minic_skip_header(e);
		minic_skip_block(e);
		if (is_if && e->lex.cur.type == TOK_ELSE) {
			minic_lex_next(&e->lex); // Consume 'else'
			minic_skip_block(e);
		}
		return;
	}

	// Plain statement: up to the next ';' that is not inside parentheses
	int depth = 0;
	while (e->lex.cur.type != TOK_EOF && !(e->lex.cur.type == TOK_SEMICOLON && depth == 0)) {
		if (e->lex.cur.type == TOK_LPAREN) {
			depth++;
		}
		if (e->lex.cur.type == TOK_RPAREN) {
			depth--;
		}
		minic_lex_next(&e->lex);
	}
	if (e->lex.cur.type == TOK_SEMICOLON) {
		minic_lex_next(&e->lex); // Consume ';'
	}
}

// Count the top-level elements of a brace initializer without evaluating it,
// so that an unsized 'type name[] = {...}' can be allocated up front.
// The lexer sits on '{' and is left untouched.
static int minic_init_list_count(minic_env_t *e) {
	if (e->lex.cur.type != TOK_LBRACE) {
		return 0;
	}
	minic_lexer_t l       = e->lex; // Scan on a copy
	int           depth   = 0;
	int           count   = 0;
	bool          in_elem = false;
	while (l.cur.type != TOK_EOF) {
		if (l.cur.type == TOK_RBRACE && --depth == 0) {
			break;
		}
		if (depth == 1) {
			if (l.cur.type == TOK_COMMA) {
				in_elem = false; // The next token starts another element
			}
			else if (!in_elem) {
				in_elem = true; // First token of an element, a nested '{' included
				count++;
			}
		}
		if (l.cur.type == TOK_LBRACE) {
			depth++;
		}
		minic_lex_next(&l);
	}
	return count;
}

// Local declarations and globals use the same allocation and initialization path.
static void minic_parse_decl(minic_env_t *e, minic_ctype_t type) {
	char name[MINIC_MAX_NAME];
	strncpy(name, e->lex.cur.text, MINIC_MAX_NAME - 1);
	name[MINIC_MAX_NAME - 1] = '\0';
	minic_expect(e, TOK_IDENT);
	if (e->lex.cur.type == TOK_LBRACKET) {
		minic_lex_next(&e->lex); // Consume '['
		bool sized = e->lex.cur.type != TOK_RBRACKET;
		int  count = sized ? (int)minic_val_to_d(minic_parse_cond(e)) : 0;
		minic_expect(e, TOK_RBRACKET);
		if (e->lex.cur.type == TOK_ASSIGN) {
			minic_lex_next(&e->lex); // Consume '='
			int listed = minic_init_list_count(e);
			if (!sized) { // 'name[]' takes its size from the initializer
				count = listed;
			}
			minic_arr_t *a = minic_arr_decl(e, name, count, type);
			minic_expect(e, TOK_LBRACE);
			for (int i = 0; e->lex.cur.type != TOK_RBRACE && !e->error; ++i) {
				minic_val_t v = minic_parse_cond(e);
				if (a != NULL && i < a->count) {
					minic_store(e, minic_reference((char *)a->data + i * type.size, type), v);
				}
				if (e->lex.cur.type != TOK_COMMA) {
					break;
				}
				minic_lex_next(&e->lex); // Consume ','
			}
			minic_expect(e, TOK_RBRACE);
			if (sized && listed > count) {
				minic_error(e, "%d initializers for '%s[%d]'", listed, name, count);
			}
		}
		else {
			minic_arr_decl(e, name, count, type);
		}
		minic_expect(e, TOK_SEMICOLON);
		return;
	}
	minic_val_t v    = minic_val_coerce(0.0, type.kind);
	v.deref_type     = type.deref;
	bool initialized = e->lex.cur.type == TOK_ASSIGN;
	if (initialized) {
		minic_lex_next(&e->lex);
		v = minic_parse_cond(e);
	}
	minic_var_decl(e, name, type, v);
	minic_expect(e, TOK_SEMICOLON);
}

// Recognize opaque pointer declarations without mistaking 'value * value' for a type.
static bool minic_decl_type(minic_env_t *e, minic_ctype_t *type) {
	if (minic_parse_type(e, &e->lex, false, type)) {
		return true;
	}
	if (e->lex.cur.type == TOK_IDENT && minic_var_find(e, e->lex.cur.text) == NULL) {
		minic_lexer_t l = e->lex;
		if (minic_parse_type(e, &l, true, type) && type->pointer && l.cur.type == TOK_IDENT) {
			e->lex = l;
			return true;
		}
	}
	return false;
}

static void minic_parse_stmt(minic_env_t *e) {
	if (minic_mem_oom) {
		if (!minic_oom_reported) {
			minic_oom_reported = true;
			minic_error(e, "out of script memory (%d KB)", MINIC_MEM_SIZE / 1024);
		}
		else { // Already reported further in, just keep unwinding
			e->error     = true;
			e->returning = true;
		}
		return;
	}
	// Skip bare typedef declarations inside function bodies
	if (e->lex.cur.type == TOK_TYPEDEF) {
		while (e->lex.cur.type != TOK_SEMICOLON && e->lex.cur.type != TOK_EOF) {
			minic_lex_next(&e->lex);
		}
		if (e->lex.cur.type == TOK_SEMICOLON) {
			minic_lex_next(&e->lex);
		}
		return;
	}

	minic_ctype_t type;
	if (minic_decl_type(e, &type)) {
		minic_parse_decl(e, type);
		return;
	}

	if (e->lex.cur.type == TOK_RETURN) {
		minic_lex_next(&e->lex);
		e->return_val = minic_parse_cond(e);
		e->returning  = true;
		minic_expect(e, TOK_SEMICOLON);
		return;
	}

	if (e->lex.cur.type == TOK_IF) {
		minic_lex_next(&e->lex);
		minic_expect(e, TOK_LPAREN);
		int taken = minic_val_is_true(minic_parse_cond(e));
		minic_expect(e, TOK_RPAREN);
		if (taken) {
			minic_parse_block(e);
		}
		else {
			minic_skip_block(e);
		}
		while (e->lex.cur.type == TOK_ELSE && !e->error) {
			minic_lex_next(&e->lex);
			int cond = 1;
			if (e->lex.cur.type == TOK_IF) {
				minic_lex_next(&e->lex);
				minic_expect(e, TOK_LPAREN);
				cond = minic_val_is_true(minic_parse_cond(e));
				minic_expect(e, TOK_RPAREN);
			}
			if (!taken && cond) {
				minic_parse_block(e);
				taken = 1;
			}
			else {
				minic_skip_block(e);
			}
		}
		return;
	}

	if (e->lex.cur.type == TOK_FOR) {
		minic_lex_next(&e->lex);
		minic_expect(e, TOK_LPAREN);

		// Init clause
		int saved_var_count = e->var_count;
		if (minic_decl_type(e, &type)) {
			minic_parse_decl(e, type);
		}
		else {
			minic_parse_cond(e);
			minic_expect(e, TOK_SEMICOLON);
		}
		minic_lexer_t condition = e->lex;

		// Scan ahead for the increment clause and body positions
		int incr_pos, body_pos;
		{
			minic_lexer_t tmp   = condition;
			int           depth = 0;
			while (tmp.cur.type != TOK_EOF && !(tmp.cur.type == TOK_SEMICOLON && depth == 0)) {
				if (tmp.cur.type == TOK_LPAREN) {
					depth++;
				}
				if (tmp.cur.type == TOK_RPAREN) {
					depth--;
				}
				minic_lex_next(&tmp);
			}
			incr_pos = tmp.pos;
			minic_lex_next(&tmp);
			depth = 0;
			while (tmp.cur.type != TOK_EOF && !(tmp.cur.type == TOK_RPAREN && depth == 0)) {
				if (tmp.cur.type == TOK_LPAREN) {
					depth++;
				}
				if (tmp.cur.type == TOK_RPAREN) {
					depth--;
				}
				minic_lex_next(&tmp);
			}
			// tmp.pos sits just past ')', where the body starts
			body_pos = tmp.pos;
		}

		for (;;) {
			e->continuing = false;
			e->lex        = condition;
			int cond      = e->lex.cur.type == TOK_SEMICOLON || minic_val_is_true(minic_parse_cond(e));
			if (!cond || e->returning || e->breaking) {
				e->lex.pos = body_pos;
				minic_lex_next(&e->lex);
				minic_skip_block(e);
				e->breaking = false;
				break;
			}
			e->lex.pos = body_pos;
			minic_lex_next(&e->lex);
			minic_parse_block(e);
			if (e->returning || e->breaking) {
				e->breaking = false;
				break;
			}
			e->lex.pos = incr_pos;
			minic_lex_next(&e->lex);
			minic_parse_cond(e);
		}
		e->var_count = saved_var_count; // The loop variable goes out of scope
		return;
	}

	if (e->lex.cur.type == TOK_BREAK) {
		minic_lex_next(&e->lex);
		minic_expect(e, TOK_SEMICOLON);
		e->breaking = true;
		return;
	}

	if (e->lex.cur.type == TOK_CONTINUE) {
		minic_lex_next(&e->lex);
		minic_expect(e, TOK_SEMICOLON);
		e->continuing = true;
		return;
	}

	if (e->lex.cur.type == TOK_WHILE) {
		minic_lex_next(&e->lex);
		int cond_pos = e->lex.pos - 1;
		for (;;) {
			e->continuing = false;
			e->lex.pos    = cond_pos;
			minic_lex_next(&e->lex);
			minic_lex_next(&e->lex); // Consume '('
			int cond = minic_val_is_true(minic_parse_cond(e));
			minic_lex_next(&e->lex); // Consume ')'
			if (!cond || e->returning || e->breaking) {
				minic_skip_block(e);
				e->breaking = false;
				break;
			}
			minic_parse_block(e);
			if (e->breaking) {
				e->breaking = false;
				break;
			}
		}
		return;
	}

	minic_parse_cond(e);
	minic_expect(e, TOK_SEMICOLON);
}

static void minic_parse_block(minic_env_t *e) {
	int saved_var_count = e->var_count;
	int saved_arr_count = e->arr_count;
	if (e->lex.cur.type != TOK_LBRACE) {
		// Single-statement body without braces
		minic_parse_stmt(e);
	}
	else {
		minic_expect(e, TOK_LBRACE);
		while (e->lex.cur.type != TOK_RBRACE && e->lex.cur.type != TOK_EOF && !e->returning && !e->breaking && !e->continuing && !e->error) {
			minic_parse_stmt(e);
		}
		if (e->lex.cur.type == TOK_RBRACE) {
			minic_lex_next(&e->lex); // Consume '}'
		}
		else {
			// Left early (return/break/continue/error): skip to the matching '}'
			int depth = 1;
			while (depth > 0 && e->lex.cur.type != TOK_EOF) {
				if (e->lex.cur.type == TOK_LBRACE) {
					depth++;
				}
				if (e->lex.cur.type == TOK_RBRACE) {
					depth--;
				}
				minic_lex_next(&e->lex);
			}
		}
	}
	e->var_count = saved_var_count;
	e->arr_count = saved_arr_count;
}

// ██████╗ ██╗   ██╗███╗   ██╗
// ██╔══██╗██║   ██║████╗  ██║
// ██████╔╝██║   ██║██╔██╗ ██║
// ██╔══██╗██║   ██║██║╚██╗██║
// ██║  ██║╚██████╔╝██║ ╚████║
// ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═══╝

// Zero pass: scan for enum and struct definitions
static void minic_register_structs(minic_env_t *e) {
	minic_lexer_t l = {0};
	l.src           = e->lex.src;
	minic_lex_next(&l);
	while (l.cur.type != TOK_EOF) {
		bool is_typedef = (l.cur.type == TOK_TYPEDEF);
		if (is_typedef) {
			minic_lex_next(&l); // Consume 'typedef'
		}

		if (l.cur.type == TOK_ENUM) {
			minic_lex_next(&l); // Consume 'enum'
			if (l.cur.type == TOK_IDENT) {
				minic_lex_next(&l); // Optional tag name
			}
			if (l.cur.type != TOK_LBRACE) {
				continue;
			}
			minic_lex_next(&l); // Consume '{'
			int val = 0;
			while (l.cur.type != TOK_RBRACE && l.cur.type != TOK_EOF) {
				if (l.cur.type == TOK_IDENT) {
					char cname[MINIC_MAX_NAME];
					strncpy(cname, l.cur.text, MINIC_MAX_NAME - 1);
					minic_lex_next(&l);
					if (l.cur.type == TOK_ASSIGN) {
						minic_lex_next(&l); // Consume '='
						val = (int)minic_val_to_d(l.cur.val);
						minic_lex_next(&l); // Consume number
					}
					minic_enum_const_add(cname, val);
					val++;
				}
				else {
					minic_lex_next(&l);
				}
				if (l.cur.type == TOK_COMMA) {
					minic_lex_next(&l);
				}
			}
			if (l.cur.type == TOK_RBRACE) {
				minic_lex_next(&l);
			}
			if (is_typedef && l.cur.type == TOK_IDENT) {
				minic_int_typedef_add(l.cur.text);
				minic_lex_next(&l);
			}
		}
		else if (l.cur.type == TOK_STRUCT) {
			minic_lex_next(&l); // Consume 'struct'

			// Optional struct tag name
			char struct_name[MINIC_MAX_NAME] = "";
			if (l.cur.type == TOK_IDENT) {
				strncpy(struct_name, l.cur.text, MINIC_MAX_NAME - 1);
				minic_lex_next(&l); // Consume struct name
			}
			if (l.cur.type != TOK_LBRACE) {
				continue; // Forward decl or typedef-without-body
			}
			if (e->struct_count >= e->struct_cap) {
				break;
			}
			minic_struct_t *def = &e->structs[e->struct_count];
			memset(def, 0, sizeof(minic_struct_t));
			strncpy(def->name, struct_name, MINIC_MAX_NAME - 1);
			minic_lex_next(&l); // Consume '{'

			while (l.cur.type != TOK_RBRACE && l.cur.type != TOK_EOF && !e->error) {
				// Keep the name as well as its resolved type for forward/self pointers.
				minic_lexer_t type_start = l;
				if (type_start.cur.type == TOK_STRUCT) {
					minic_lex_next(&type_start);
				}
				minic_ctype_t field_type;
				if (!minic_parse_type(e, &l, true, &field_type)) {
					minic_error(e, "expected field type in '%s'", def->name);
					return;
				}
				minic_ctype_t field_base = field_type;
				while (field_base.pointer > 0) {
					field_base = minic_element_type(field_base);
				}
				for (;;) {
					if (l.cur.type != TOK_IDENT || def->field_count >= MINIC_MAX_STRUCT_FIELDS) {
						minic_error(e, "invalid or too many fields in '%s'", def->name);
						return;
					}
					int idx = def->field_count++;
					strncpy(def->fields[idx], l.cur.text, MINIC_MAX_NAME - 1);
					def->types[idx]          = field_type.kind;
					def->deref_types[idx]    = field_type.deref;
					def->pointer_depths[idx] = field_type.pointer;
					if (field_type.kind == MINIC_T_EMBED || field_type.deref == MINIC_T_EMBED) {
						strncpy(def->field_structs[idx], type_start.cur.text, MINIC_MAX_NAME - 1);
					}
					minic_lex_next(&l);
					if (l.cur.type == TOK_LBRACKET) {
						minic_lex_next(&l);
						if (l.cur.type != TOK_NUMBER || l.cur.val.type != MINIC_T_INT || l.cur.val.i <= 0) {
							minic_error(e, "field array requires a positive integer size");
							return;
						}
						def->counts[idx] = l.cur.val.i;
						minic_lex_next(&l);
						if (l.cur.type != TOK_RBRACKET) {
							minic_error(e, "expected ']' after field array size");
							return;
						}
						minic_lex_next(&l);
					}
					if (l.cur.type != TOK_COMMA) {
						break;
					}
					minic_lex_next(&l);
					field_type = field_base;
					while (l.cur.type == TOK_STAR) {
						field_type = minic_pointer_type(field_type);
						minic_lex_next(&l);
					}
				}
				if (l.cur.type != TOK_SEMICOLON) {
					minic_error(e, "expected ';' after struct field");
					return;
				}
				minic_lex_next(&l);
			}
			if (l.cur.type == TOK_RBRACE) {
				minic_lex_next(&l);
			}

			if (is_typedef && l.cur.type == TOK_IDENT) {
				// typedef struct [Name] { ... } alias;
				char alias[MINIC_MAX_NAME];
				strncpy(alias, l.cur.text, MINIC_MAX_NAME - 1);
				minic_lex_next(&l); // Consume alias name
				if (struct_name[0] != '\0') {
					// Register under the tag name, plus a copy under the alias name
					e->struct_count++;
					if (e->struct_count < e->struct_cap) {
						minic_struct_t *adef = &e->structs[e->struct_count++];
						*adef                = *def;
						strncpy(adef->name, alias, MINIC_MAX_NAME - 1);
					}
				}
				else {
					// Anonymous struct: name it after the alias
					strncpy(def->name, alias, MINIC_MAX_NAME - 1);
					e->struct_count++;
				}
			}
			else if (struct_name[0] != '\0') {
				// Plain struct definition: must have a tag name to be usable
				e->struct_count++;
			}
		}
		else {
			minic_lex_next(&l);
			continue;
		}

		while (l.cur.type != TOK_SEMICOLON && l.cur.type != TOK_EOF) {
			minic_lex_next(&l);
		}
		if (l.cur.type == TOK_SEMICOLON) {
			minic_lex_next(&l);
		}
	}
}

// Resolve script layouts after collecting all definitions. Native descriptors
// already have their compiler-provided sizes, offsets, and alignment.
static bool minic_layout_struct(minic_env_t *e, minic_struct_t *def) {
	if (def->layout_state == 2) {
		return true;
	}
	if (def->layout_state == 1) {
		minic_error(e, "recursive embedded struct '%s'", def->name);
		return false;
	}
	def->layout_state = 1;
	def->size         = 0;
	def->alignment    = 1;
	for (int i = 0; i < def->field_count; ++i) {
		if (def->types[i] == MINIC_T_EMBED) {
			minic_struct_t *child = minic_struct_get(e, def->field_structs[i]);
			if (child == NULL) {
				minic_error(e, "unknown embedded struct '%s'", def->field_structs[i]);
				return false;
			}
			if (!minic_layout_struct(e, child)) {
				return false;
			}
		}
		minic_ctype_t type  = minic_field_type(e, def, i);
		int           count = def->counts[i] > 0 ? def->counts[i] : 1;
		if (type.size <= 0 || count > (MINIC_MEM_SIZE - def->size) / type.size) {
			minic_error(e, "invalid field size in '%s'", def->name);
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

// First pass: register all function definitions and globals, stop at 'main'
static void minic_register_funcs(minic_env_t *e) {
	while (e->lex.cur.type != TOK_EOF && !e->error) {
		if (e->lex.cur.type == TOK_TYPEDEF || e->lex.cur.type == TOK_ENUM || e->lex.cur.type == TOK_STRUCT) {
			minic_lexer_t scan       = e->lex;
			bool          definition = scan.cur.type == TOK_TYPEDEF || scan.cur.type == TOK_ENUM;
			minic_lex_next(&scan);
			if (scan.cur.type == TOK_IDENT) {
				minic_lex_next(&scan);
			}
			definition = definition || scan.cur.type == TOK_LBRACE;
			if (definition) {
				int depth = 0;
				do {
					if (e->lex.cur.type == TOK_LBRACE) {
						depth++;
					}
					if (e->lex.cur.type == TOK_RBRACE) {
						depth--;
					}
					minic_lex_next(&e->lex);
				} while (e->lex.cur.type != TOK_EOF && !(e->lex.cur.type == TOK_SEMICOLON && depth == 0));
				if (e->lex.cur.type == TOK_SEMICOLON) {
					minic_lex_next(&e->lex);
				}
				continue;
			}
		}
		minic_ctype_t type;
		if (!minic_parse_type(e, &e->lex, true, &type)) {
			minic_lex_next(&e->lex);
			continue;
		}
		if (e->lex.cur.type != TOK_IDENT) {
			continue;
		}
		minic_lexer_t declaration = e->lex;
		char          fname[MINIC_MAX_NAME];
		strncpy(fname, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex);

		if (e->lex.cur.type != TOK_LPAREN) {
			e->lex = declaration;
			minic_parse_decl(e, type);
			if (e->error) {
				return;
			}
			continue;
		}
		minic_lex_next(&e->lex); // Consume '('

		minic_func_t fn = {0};
		strncpy(fn.name, fname, MINIC_MAX_NAME - 1);
		fn.ret_type = type;

		while (e->lex.cur.type != TOK_RPAREN && e->lex.cur.type != TOK_EOF && !e->error) {
			minic_ctype_t parameter;
			if (!minic_parse_type(e, &e->lex, true, &parameter)) {
				minic_error(e, "expected parameter type");
				return;
			}
			if (e->lex.cur.type == TOK_IDENT && fn.param_count < MINIC_MAX_PARAMS) {
				int pi = fn.param_count++;
				strncpy(fn.params[pi], e->lex.cur.text, MINIC_MAX_NAME - 1);
				fn.param_types[pi] = parameter;
				minic_lex_next(&e->lex);
			}
			if (e->lex.cur.type == TOK_COMMA) {
				minic_lex_next(&e->lex);
			}
		}
		minic_lex_next(&e->lex); // Consume ')'

		fn.body_pos = e->lex.pos - 1;

		if (strcmp(fname, "main") == 0) {
			break;
		}
		if (e->func_count < e->func_cap) {
			e->funcs[e->func_count++] = fn;
		}

		// Skip function body
		int depth = 1;
		minic_lex_next(&e->lex); // Consume '{'
		while (depth > 0 && e->lex.cur.type != TOK_EOF) {
			if (e->lex.cur.type == TOK_LBRACE) {
				depth++;
			}
			if (e->lex.cur.type == TOK_RBRACE) {
				depth--;
			}
			minic_lex_next(&e->lex);
		}
	}
}

minic_ctx_t *minic_eval_named(const char *src, const char *filename) {
	minic_register_builtins();

	minic_ctx_t *ctx = (minic_ctx_t *)calloc(1, sizeof(minic_ctx_t));
	ctx->mem         = (minic_u8 *)calloc(1, MINIC_MEM_SIZE);
	ctx->mem_frame   = MINIC_MEM_SIZE;
	// Copy the source so the context stays valid after the caller frees its buffer
	int src_len   = (int)strlen(src);
	ctx->src_copy = (char *)malloc(src_len + 1);
	memcpy(ctx->src_copy, src, src_len + 1);

	// Save and install arena pointers so minic_alloc and the lexer use this context
	minic_u8 *prev_mem       = minic_active_mem;
	int      *prev_mem_used  = minic_active_mem_used;
	int      *prev_mem_frame = minic_active_mem_frame;
	int      *prev_str_key   = minic_active_str_key;
	int      *prev_str_off   = minic_active_str_off;
	minic_active_mem         = ctx->mem;
	minic_active_mem_used    = &ctx->mem_used;
	minic_active_mem_frame   = &ctx->mem_frame;
	minic_active_str_key     = ctx->str_key;
	minic_active_str_off     = ctx->str_off;
	minic_mem_oom            = false;
	minic_oom_reported       = false;

	minic_env_t *e = &ctx->e;
	e->lex.src     = ctx->src_copy;
	e->filename    = filename;
	e->var_cap     = MINIC_MAX_VARS;
	e->vars        = minic_alloc(e->var_cap * (int)sizeof(minic_var_t));
	e->arr_cap     = 32;
	e->arrs        = minic_alloc(e->arr_cap * (int)sizeof(minic_arr_t));
	e->func_cap    = 32;
	e->funcs       = minic_alloc(e->func_cap * (int)sizeof(minic_func_t));
	e->struct_cap  = MINIC_MAX_STRUCTS;
	e->structs     = minic_alloc(e->struct_cap * (int)sizeof(minic_struct_t));

	// Seed env with globally pre-registered struct definitions
	for (int i = 0; i < minic_struct_count && e->struct_count < e->struct_cap; ++i) {
		e->structs[e->struct_count++] = minic_structs[i];
	}

	minic_register_structs(e);
	for (int i = 0; i < e->struct_count && !e->error; ++i) {
		minic_layout_struct(e, &e->structs[i]);
	}
	minic_lex_next(&e->lex);
	minic_register_funcs(e);
	for (int i = 0; i < e->func_count; ++i) {
		e->funcs[i].ctx = ctx;
	}

	if (e->lex.cur.type == TOK_RPAREN) {
		minic_lex_next(&e->lex);
	}

	minic_parse_block(e);
	minic_active_mem       = prev_mem;
	minic_active_mem_used  = prev_mem_used;
	minic_active_mem_frame = prev_mem_frame;
	minic_active_str_key   = prev_str_key;
	minic_active_str_off   = prev_str_off;

	// A frame or arena overflow deep in a call is reported on that scope's env, which does
	// not propagate outward, so consult the sticky flag too rather than return a partial value
	ctx->result = (e->error || minic_mem_oom) ? -1.0f : (float)minic_val_to_d(e->return_val);
	return ctx;
}

minic_ctx_t *minic_eval(const char *src) {
	return minic_eval_named(src, "<script>");
}

void minic_ctx_free(minic_ctx_t *ctx) {
	if (ctx != NULL) {
		free(ctx->mem);
		free(ctx->src_copy);
		free(ctx);
	}
}

float minic_ctx_result(minic_ctx_t *ctx) {
	return ctx != NULL ? ctx->result : -1.0f;
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

static minic_ext_func_t   minic_ext_funcs[MINIC_MAX_EXTFUNS];
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

void minic_enum_const_add(const char *name, int value) {
	for (int i = 0; i < minic_enum_const_count; ++i) {
		if (strcmp(minic_enum_consts[i].name, name) == 0) {
			return;
		}
	}
	if (minic_enum_const_count >= MINIC_MAX_ENUM_CONSTS) {
		return;
	}
	strncpy(minic_enum_consts[minic_enum_const_count].name, name, MINIC_MAX_NAME - 1);
	minic_enum_consts[minic_enum_const_count].value = value;
	minic_enum_const_count++;
}

int minic_enum_const_get(const char *name) {
	for (int i = 0; i < minic_enum_const_count; ++i) {
		if (strcmp(minic_enum_consts[i].name, name) == 0) {
			return minic_enum_consts[i].value;
		}
	}
	return -1;
}

void minic_register_global(const char *name, const void *ptr, minic_type_t type) {
	for (int i = 0; i < minic_global_count; ++i) {
		if (strcmp(minic_globals[i].name, name) == 0) {
			minic_globals[i].ptr  = ptr;
			minic_globals[i].type = type;
			return;
		}
	}
	if (minic_global_count >= MINIC_MAX_GLOBALS) {
		return;
	}
	strncpy(minic_globals[minic_global_count].name, name, MINIC_MAX_NAME - 1);
	minic_globals[minic_global_count].ptr  = ptr;
	minic_globals[minic_global_count].type = type;
	minic_global_count++;
}

bool minic_global_get(const char *name, minic_val_t *out) {
	for (int i = 0; i < minic_global_count; ++i) {
		if (strcmp(minic_globals[i].name, name) == 0) {
			*out = minic_load(minic_reference((void *)minic_globals[i].ptr, minic_scalar_type(minic_globals[i].type)));
			return true;
		}
	}
	return false;
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
