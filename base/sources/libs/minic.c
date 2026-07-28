
// Minimal C interpreter

#include "minic.h"
#include <ctype.h>
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

#define MINIC_TOK_LIST                                                                                                                                \
	X(TOK_INT, "'int'")                                                                                                                               \
	X(TOK_FLOAT, "'float'")                                                                                                                           \
	X(TOK_CHAR, "'char'")                                                                                                                             \
	X(TOK_DOUBLE, "'double'")                                                                                                                         \
	X(TOK_BOOL, "'bool'")                                                                                                                             \
	X(TOK_RETURN, "'return'")                                                                                                                         \
	X(TOK_IF, "'if'")                                                                                                                                 \
	X(TOK_ELSE, "'else'") X(TOK_WHILE, "'while'") X(TOK_FOR, "'for'") X(TOK_BREAK, "'break'") X(TOK_CONTINUE, "'continue'") X(TOK_STRUCT, "'struct'") \
	    X(TOK_TYPEDEF, "'typedef'") X(TOK_ENUM, "'enum'") X(TOK_VOID, "'void'") X(TOK_IDENT, "identifier") X(TOK_NUMBER, "number")                    \
	        X(TOK_CHAR_LIT, "char literal") X(TOK_STR_LIT, "string literal") X(TOK_LPAREN, "'('") X(TOK_RPAREN, "')'") X(TOK_LBRACE, "'{'")           \
	            X(TOK_RBRACE, "'}'") X(TOK_LBRACKET, "'['") X(TOK_RBRACKET, "']'") X(TOK_SEMICOLON, "';'") X(TOK_COMMA, "','") X(TOK_ASSIGN, "'='")   \
	                X(TOK_PLUS_ASSIGN, "'+='") X(TOK_MINUS_ASSIGN, "'-='") X(TOK_MUL_ASSIGN, "'*='") X(TOK_DIV_ASSIGN, "'/='") X(TOK_EQ, "'=='")      \
	                    X(TOK_NEQ, "'!='") X(TOK_LT, "'<'") X(TOK_GT, "'>'") X(TOK_LE, "'<='") X(TOK_GE, "'>='") X(TOK_AND, "'&&'") X(TOK_OR, "'||'") \
	                        X(TOK_NOT, "'!'") X(TOK_AMP, "'&'") X(TOK_PLUS, "'+'") X(TOK_MINUS, "'-'") X(TOK_INC, "'++'") X(TOK_DEC, "'--'")          \
	                            X(TOK_STAR, "'*'") X(TOK_SLASH, "'/'") X(TOK_DOT, "'.'") X(TOK_ARROW, "'->'") X(TOK_EOF, "end of file")

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

static minic_u8 *minic_active_mem      = NULL;
static int      *minic_active_mem_used = NULL;

static const struct {
	const char      *kw;
	minic_tok_type_t tok;
} minic_keywords[] = {
    {"int", TOK_INT},           {"float", TOK_FLOAT},   {"char", TOK_CHAR},       {"double", TOK_DOUBLE}, {"bool", TOK_BOOL}, {"void", TOK_VOID},
    {"return", TOK_RETURN},     {"if", TOK_IF},         {"else", TOK_ELSE},       {"while", TOK_WHILE},   {"for", TOK_FOR},   {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE}, {"struct", TOK_STRUCT}, {"typedef", TOK_TYPEDEF}, {"enum", TOK_ENUM},
};

// Two-char operators must come before their one-char prefixes
static const struct {
	char             a, b;
	minic_tok_type_t tok;
} minic_ops[] = {
    {'+', '+', TOK_INC},        {'+', '=', TOK_PLUS_ASSIGN},
    {'-', '-', TOK_DEC},        {'-', '=', TOK_MINUS_ASSIGN},
    {'-', '>', TOK_ARROW},      {'*', '=', TOK_MUL_ASSIGN},
    {'/', '=', TOK_DIV_ASSIGN}, {'=', '=', TOK_EQ},
    {'!', '=', TOK_NEQ},        {'&', '&', TOK_AND},
    {'|', '|', TOK_OR},         {'<', '=', TOK_LE},
    {'>', '=', TOK_GE},         {'+', 0, TOK_PLUS},
    {'-', 0, TOK_MINUS},        {'*', 0, TOK_STAR},
    {'/', 0, TOK_SLASH},        {'=', 0, TOK_ASSIGN},
    {'!', 0, TOK_NOT},          {'&', 0, TOK_AMP},
    {'<', 0, TOK_LT},           {'>', 0, TOK_GT},
    {'(', 0, TOK_LPAREN},       {')', 0, TOK_RPAREN},
    {'{', 0, TOK_LBRACE},       {'}', 0, TOK_RBRACE},
    {'[', 0, TOK_LBRACKET},     {']', 0, TOK_RBRACKET},
    {';', 0, TOK_SEMICOLON},    {',', 0, TOK_COMMA},
    {'.', 0, TOK_DOT},
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

static void minic_lex_next(minic_lexer_t *l) {
	for (;;) {
		// Skip whitespace, comments and preprocessor directives
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

		if (isdigit((unsigned char)c)) {
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
			if (l->src[l->pos] == 'f' || l->src[l->pos] == 'F') {
				l->pos++;
				is_float = true;
			}
			l->cur.val  = is_float ? minic_val_float((float)n) : minic_val_int((int)n);
			l->cur.type = TOK_NUMBER;
			return;
		}

		if (c == '"') {
			l->pos++; // Consume opening '"'
			// Write the string into the active context's arena
			int start = (*minic_active_mem_used + 7) & ~7;
			int wi    = start;
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
				minic_active_mem[wi++] = (minic_u8)ch;
			}
			minic_active_mem[wi++] = '\0';
			*minic_active_mem_used = (wi + 7) & ~7;
			if (l->src[l->pos] == '"') {
				l->pos++; // Consume closing '"'
			}
			l->cur.type = TOK_STR_LIT;
			l->cur.val  = minic_val_ptr((void *)&minic_active_mem[start]);
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
				l->cur.text[i++] = l->src[l->pos++];
			}
			l->cur.text[i] = '\0';
			for (size_t k = 0; k < sizeof(minic_keywords) / sizeof(minic_keywords[0]); ++k) {
				if (strcmp(l->cur.text, minic_keywords[k].kw) == 0) {
					l->cur.type = minic_keywords[k].tok;
					return;
				}
			}
			if (strcmp(l->cur.text, "true") == 0 || strcmp(l->cur.text, "false") == 0) {
				l->cur.type = TOK_NUMBER;
				l->cur.val  = minic_val_int(l->cur.text[0] == 't');
				return;
			}
			l->cur.type = TOK_IDENT;
			return;
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

void *minic_alloc(int size) {
	// Align to 8 bytes
	int aligned            = (*minic_active_mem_used + 7) & ~7;
	*minic_active_mem_used = aligned + size;
	return &minic_active_mem[aligned];
}

typedef struct {
	char        name[MINIC_MAX_NAME];
	minic_val_t val;
} minic_var_t;

typedef struct {
	char         name[MINIC_MAX_NAME];
	int          offset; // index into global arr_data[]
	int          count;
	minic_type_t elem_type;
} minic_arr_t;

typedef struct {
	char         name[MINIC_MAX_NAME];
	char         params[MINIC_MAX_PARAMS][MINIC_MAX_NAME];
	char         param_structs[MINIC_MAX_PARAMS][MINIC_MAX_NAME]; // struct type name per param, or ""
	minic_type_t param_types[MINIC_MAX_PARAMS];
	int          param_count;
	int          body_pos; // lexer position of '{' that starts the body
	minic_type_t ret_type;
	minic_ctx_t *ctx; // owning context, set at parse time
} minic_func_t;

// Maps a variable name to its struct type name
typedef struct {
	char var_name[MINIC_MAX_NAME];
	char struct_name[MINIC_MAX_NAME];
} minic_vartype_t;

typedef struct minic_env_s {
	minic_lexer_t       lex;
	const char         *filename;
	minic_var_t        *vars;
	int                 var_count;
	int                 var_cap;
	minic_arr_t        *arrs;
	int                 arr_count;
	int                 arr_cap;
	minic_val_t        *arr_data;      // global array element storage
	int                *arr_data_used; // pointer to shared counter
	minic_func_t       *funcs;
	int                 func_count;
	int                 func_cap;
	minic_struct_t     *structs; // shared across calls
	int                 struct_count;
	int                 struct_cap;
	minic_vartype_t    *vartypes; // local: var->struct type mapping
	int                 vartype_count;
	int                 vartype_cap;
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
	minic_env_t e;
	float       result;
	char       *src_copy;
};

static minic_val_t minic_parse_cond(minic_env_t *e);
static void        minic_parse_stmt(minic_env_t *e);
static void        minic_parse_block(minic_env_t *e);

#define MINIC_INC_DELTA(l) ((l)->cur.type == TOK_INC ? 1.0 : -1.0)

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
	case TOK_CHAR:
	case TOK_BOOL:
		return MINIC_T_INT;
	case TOK_FLOAT:
	case TOK_DOUBLE:
		return MINIC_T_FLOAT;
	default:
		return MINIC_T_PTR; // void * -> PTR
	}
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
	default:
		return b;
	}
}

static minic_var_t *minic_var_find(minic_env_t *e, const char *name) {
	for (int i = e->var_count - 1; i >= 0; --i) {
		if (strcmp(e->vars[i].name, name) == 0) {
			return &e->vars[i];
		}
	}
	if (e->global_env != NULL) {
		minic_env_t *g = e->global_env;
		for (int i = g->var_count - 1; i >= 0; --i) {
			if (strcmp(g->vars[i].name, name) == 0) {
				return &g->vars[i];
			}
		}
	}
	return NULL;
}

static minic_var_t *minic_var_push(minic_env_t *e, const char *name, minic_val_t val) {
	if (e->var_count >= e->var_cap) {
		return NULL;
	}
	minic_var_t *v = &e->vars[e->var_count++];
	strncpy(v->name, name, MINIC_MAX_NAME - 1);
	v->val = val;
	return v;
}

static minic_val_t minic_var_get(minic_env_t *e, const char *name) {
	minic_var_t *v = minic_var_find(e, name);
	return v != NULL ? v->val : minic_val_int(0);
}

static void minic_var_set(minic_env_t *e, const char *name, minic_val_t val) {
	minic_var_t *v = minic_var_find(e, name);
	if (v == NULL) {
		minic_var_push(e, name, val);
		return;
	}
	// Preserve declared type, coerce if needed
	if (v->val.type != val.type) {
		val = minic_val_cast(val, v->val.type);
	}
	// Preserve deref_type for pointer variables (it was set at declaration)
	if (v->val.type == MINIC_T_PTR && v->val.deref_type != MINIC_T_PTR) {
		val.deref_type = v->val.deref_type;
	}
	v->val = val;
}

static void minic_var_decl(minic_env_t *e, const char *name, minic_type_t type, minic_val_t init) {
	// Coerce init to declared type; typed pointers keep the deref_type from init
	minic_val_t v = minic_val_cast(init, type);
	if (type == MINIC_T_PTR) {
		v.deref_type = init.deref_type;
	}
	minic_var_push(e, name, v);
}

static minic_val_t minic_var_addr(minic_env_t *e, const char *name) {
	minic_var_t *v = minic_var_find(e, name);
	if (v == NULL) {
		v = minic_var_push(e, name, minic_val_int(0));
		if (v == NULL) {
			return minic_val_ptr(NULL);
		}
	}
	// Address of a minic_val_t — the MINIC_T_PTR deref sentinel means deref reads a full minic_val_t
	return minic_val_typed_ptr(&v->val, MINIC_T_PTR);
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

static void minic_arr_decl(minic_env_t *e, const char *name, int count, minic_type_t elem_type) {
	if (e->arr_count >= e->arr_cap) {
		return;
	}
	minic_arr_t *a = &e->arrs[e->arr_count++];
	strncpy(a->name, name, MINIC_MAX_NAME - 1);
	a->offset    = *e->arr_data_used;
	a->count     = count;
	a->elem_type = elem_type;
	*e->arr_data_used += count;
	// Zero-initialise
	for (int i = 0; i < count; i++) {
		e->arr_data[a->offset + i] = minic_val_coerce(0.0, elem_type);
	}
}

// Subscript a native C pointer by its deref element type
static minic_val_t minic_ptr_index_get(minic_val_t pv, int idx) {
	if (pv.type != MINIC_T_PTR || pv.p == NULL) {
		return minic_val_int(0);
	}
	switch (pv.deref_type) {
	case MINIC_T_FLOAT:
		return minic_val_float(((float *)pv.p)[idx]);
	case MINIC_T_INT:
		return minic_val_int(((int32_t *)pv.p)[idx]);
	default:
		return minic_val_ptr(((void **)pv.p)[idx]);
	}
}

static void minic_ptr_index_set(minic_val_t pv, int idx, minic_val_t val) {
	if (pv.type != MINIC_T_PTR || pv.p == NULL) {
		return;
	}
	switch (pv.deref_type) {
	case MINIC_T_FLOAT:
		((float *)pv.p)[idx] = (float)minic_val_to_d(val);
		break;
	case MINIC_T_INT:
		((int32_t *)pv.p)[idx] = (int32_t)minic_val_to_d(val);
		break;
	default:
		((void **)pv.p)[idx] = minic_val_to_ptr(val);
		break;
	}
}

static minic_val_t minic_arr_elem_get(minic_env_t *e, const char *name, int idx) {
	minic_arr_t *a = minic_arr_get(e, name);
	if (a != NULL && idx >= 0 && idx < a->count) {
		return e->arr_data[a->offset + idx];
	}
	return minic_ptr_index_get(minic_var_get(e, name), idx); // native pointer subscript
}

static void minic_arr_elem_set(minic_env_t *e, const char *name, int idx, minic_val_t val) {
	minic_arr_t *a = minic_arr_get(e, name);
	if (a != NULL && idx >= 0 && idx < a->count) {
		e->arr_data[a->offset + idx] = minic_val_cast(val, a->elem_type);
		return;
	}
	minic_ptr_index_set(minic_var_get(e, name), idx, val); // native pointer subscript
}

// Read through a pointer: a MINIC_T_PTR deref_type means "points at a boxed minic_val_t"
// (interpreter-internal), any other deref_type means a native C scalar at that address
static minic_val_t minic_deref(minic_val_t pv) {
	void *ptr = minic_val_to_ptr(pv);
	if (ptr == NULL) {
		return minic_val_int(0);
	}
	switch (pv.deref_type) {
	case MINIC_T_INT: {
		int v;
		memcpy(&v, ptr, sizeof(int));
		return minic_val_int(v);
	}
	case MINIC_T_FLOAT: {
		float v;
		memcpy(&v, ptr, sizeof(float));
		return minic_val_float(v);
	}
	default: {
		minic_val_t v;
		memcpy(&v, ptr, sizeof(minic_val_t));
		return v;
	}
	}
}

// Write through a pointer, with optional compound op: *pv = v, *pv += v, ...
static void minic_store_op(minic_val_t pv, minic_tok_type_t op, minic_val_t v) {
	void *ptr = minic_val_to_ptr(pv);
	if (ptr == NULL) {
		return;
	}
	switch (pv.deref_type) {
	case MINIC_T_INT: {
		int ov;
		memcpy(&ov, ptr, sizeof(int));
		int nv = (int)minic_apply_op(op, (double)ov, minic_val_to_d(v));
		memcpy(ptr, &nv, sizeof(int));
		break;
	}
	case MINIC_T_FLOAT: {
		float ov;
		memcpy(&ov, ptr, sizeof(float));
		float nv = (float)minic_apply_op(op, (double)ov, minic_val_to_d(v));
		memcpy(ptr, &nv, sizeof(float));
		break;
	}
	default: {
		minic_val_t ov;
		memcpy(&ov, ptr, sizeof(minic_val_t));
		minic_val_t nv = (op == TOK_ASSIGN) ? v : minic_val_coerce(minic_apply_op(op, minic_val_to_d(ov), minic_val_to_d(v)), ov.type);
		memcpy(ptr, &nv, sizeof(minic_val_t));
		break;
	}
	}
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

static void minic_vartype_set(minic_env_t *e, const char *var_name, const char *struct_name) {
	if (e->vartype_count < e->vartype_cap) {
		strncpy(e->vartypes[e->vartype_count].var_name, var_name, MINIC_MAX_NAME - 1);
		strncpy(e->vartypes[e->vartype_count].struct_name, struct_name, MINIC_MAX_NAME - 1);
		e->vartype_count++;
	}
}

static minic_struct_t *minic_var_struct(minic_env_t *e, const char *var_name) {
	for (int i = e->vartype_count - 1; i >= 0; --i) {
		if (strcmp(e->vartypes[i].var_name, var_name) == 0) {
			return minic_struct_get(e, e->vartypes[i].struct_name);
		}
	}
	if (e->global_env != NULL) {
		minic_env_t *g = e->global_env;
		for (int i = g->vartype_count - 1; i >= 0; --i) {
			if (strcmp(g->vartypes[i].var_name, var_name) == 0) {
				return minic_struct_get(e, g->vartypes[i].struct_name);
			}
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

bool minic_in_arena(void *p) {
	return p != NULL && (minic_u8 *)p >= minic_active_mem && (minic_u8 *)p < minic_active_mem + MINIC_MEM_SIZE;
}

static minic_val_t minic_struct_field_get_base(minic_env_t *e, void *base, minic_struct_t *def, const char *field) {
	int idx = minic_struct_field_idx(def, field);
	if (idx < 0) {
		minic_error(e, "struct '%s' has no field '%s'", def->name, field);
		return minic_val_int(0);
	}
	if (def->native && !minic_in_arena(base)) {
		char *p = (char *)base + def->offsets[idx];
		switch (def->types[idx]) {
		case MINIC_T_PTR:
			return minic_val_typed_ptr(*(void **)p, def->deref_types[idx]);
		case MINIC_T_EMBED:
			return minic_val_typed_ptr(p, def->deref_types[idx]);
		case MINIC_T_FLOAT:
			return minic_val_float(*(float *)p);
		case MINIC_T_BOOL:
			return minic_val_int(*(bool *)p);
		default:
			return minic_val_int(*(int32_t *)p);
		}
	}
	minic_val_t v;
	memcpy(&v, (minic_val_t *)base + idx, sizeof(minic_val_t));
	return v;
}

static void minic_struct_field_set_base(minic_env_t *e, void *base, minic_struct_t *def, const char *field, minic_val_t val) {
	int idx = minic_struct_field_idx(def, field);
	if (idx < 0) {
		minic_error(e, "struct '%s' has no field '%s'", def->name, field);
		return;
	}
	if (def->native && !minic_in_arena(base)) {
		char *p = (char *)base + def->offsets[idx];
		switch (def->types[idx]) {
		case MINIC_T_PTR:
			*(void **)p = minic_val_to_ptr(val);
			break;
		case MINIC_T_EMBED:
			break; // embedded structs are mutated through their own field accessors
		case MINIC_T_FLOAT:
			*(float *)p = (float)minic_val_to_d(val);
			break;
		case MINIC_T_BOOL:
			*(bool *)p = (minic_val_to_d(val) != 0.0);
			break;
		default:
			*(int32_t *)p = (int32_t)minic_val_to_d(val);
			break;
		}
		return;
	}
	memcpy((minic_val_t *)base + idx, &val, sizeof(minic_val_t));
}

static minic_val_t minic_call(minic_env_t *e, minic_func_t *fn, minic_val_t *args, int argc) {
	minic_env_t child   = {0};
	child.lex.src       = e->lex.src;
	child.lex.pos       = fn->body_pos;
	child.filename      = e->filename;
	child.var_cap       = 64;
	child.vars          = minic_alloc(child.var_cap * (int)sizeof(minic_var_t));
	child.global_env    = e->global_env != NULL ? e->global_env : e;
	child.arr_cap       = 32;
	child.arrs          = minic_alloc(child.arr_cap * (int)sizeof(minic_arr_t));
	child.arr_data      = e->arr_data;
	child.arr_data_used = e->arr_data_used;
	child.func_count    = e->func_count;
	child.func_cap      = e->func_cap;
	child.funcs         = e->funcs;
	child.struct_count  = e->struct_count;
	child.struct_cap    = e->struct_cap;
	child.structs       = e->structs;
	child.vartype_cap   = 32;
	child.vartypes      = minic_alloc(child.vartype_cap * (int)sizeof(minic_vartype_t));
	// Bind parameters
	for (int i = 0; i < argc && i < fn->param_count; ++i) {
		minic_var_decl(&child, fn->params[i], fn->param_types[i], minic_val_cast(args[i], fn->param_types[i]));
		if (fn->param_structs[i][0] != '\0') {
			minic_vartype_set(&child, fn->params[i], fn->param_structs[i]);
		}
	}
	minic_lex_next(&child.lex);
	minic_parse_block(&child);
	return child.return_val;
}

static minic_val_t minic_call_in_ctx(minic_ctx_t *ctx, minic_func_t *fn, minic_val_t *args, int argc) {
	minic_u8 *prev_mem      = minic_active_mem;
	int      *prev_mem_used = minic_active_mem_used;
	minic_active_mem        = ctx->mem;
	minic_active_mem_used   = &ctx->mem_used;
	int         saved_used  = ctx->mem_used;
	minic_val_t r           = minic_call(&ctx->e, fn, args, argc);
	ctx->mem_used           = saved_used;
	minic_active_mem        = prev_mem;
	minic_active_mem_used   = prev_mem_used;
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
	// Determine result type (widening: int < float < ptr)
	minic_type_t rt;
	if (a.type == MINIC_T_PTR || b.type == MINIC_T_PTR) {
		rt = MINIC_T_PTR;
	}
	else if (a.type == MINIC_T_FLOAT || b.type == MINIC_T_FLOAT) {
		rt = MINIC_T_FLOAT;
	}
	else {
		rt = MINIC_T_INT;
	}
	double da = minic_val_to_d(a);
	double db = minic_val_to_d(b);
	double r  = op == TOK_PLUS ? da + db : op == TOK_MINUS ? da - db : op == TOK_STAR ? da * db : (db != 0.0 ? da / db : 0.0);
	return minic_val_coerce(r, rt);
}

// Parse a call argument list (after '(') and invoke a script or extern function
static minic_val_t minic_parse_call(minic_env_t *e, const char *name) {
	minic_val_t args[MINIC_MAX_PARAMS];
	int         argc = 0;
	while (e->lex.cur.type != TOK_RPAREN && e->lex.cur.type != TOK_EOF) {
		minic_val_t v = minic_parse_cond(e);
		if (argc < MINIC_MAX_PARAMS) {
			args[argc++] = v;
		}
		if (e->lex.cur.type == TOK_COMMA) {
			minic_lex_next(&e->lex);
		}
	}
	minic_expect(e, TOK_RPAREN);
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

// primary: '&' IDENT | '*' primary | '-' primary | '!' primary | '++'/'--' IDENT |
//          NUMBER | CHAR_LIT | STR_LIT | IDENT ['[' expr ']' | '(' args ')' | ('.'|'->') field...] | '(' expr ')'
static minic_val_t minic_parse_primary(minic_env_t *e) {
	if (e->lex.cur.type == TOK_AMP) {
		minic_lex_next(&e->lex); // Consume '&'
		char aname[MINIC_MAX_NAME];
		strncpy(aname, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_arr_t    *arr = minic_arr_get(e, aname);
		minic_struct_t *def = minic_var_struct(e, aname);
		minic_val_t     addr;
		if (arr != NULL) {
			addr = minic_val_ptr(&e->arr_data[arr->offset]);
		}
		else if (def != NULL) {
			addr = minic_var_get(e, aname); // struct var holds the real base pointer
		}
		else {
			addr = minic_var_addr(e, aname);
		}
		minic_lex_next(&e->lex); // Consume ident
		// Handle &var->field or &var.field: follow the member-access chain
		while (def != NULL && (e->lex.cur.type == TOK_ARROW || e->lex.cur.type == TOK_DOT)) {
			minic_lex_next(&e->lex); // Consume '->' or '.'
			char field[MINIC_MAX_NAME];
			strncpy(field, e->lex.cur.text, MINIC_MAX_NAME - 1);
			minic_lex_next(&e->lex); // Consume field name
			addr = minic_struct_field_get_base(e, minic_val_to_ptr(addr), def, field);
			// Advance def to the field's struct type for further chaining
			int fidx = minic_struct_field_idx(def, field);
			def      = (fidx >= 0 && def->field_structs[fidx][0] != '\0') ? minic_struct_get(e, def->field_structs[fidx]) : NULL;
		}
		return addr;
	}
	if (e->lex.cur.type == TOK_STAR) {
		minic_lex_next(&e->lex); // Consume '*'
		return minic_deref(minic_parse_primary(e));
	}
	if (e->lex.cur.type == TOK_MINUS) {
		minic_lex_next(&e->lex);
		minic_val_t v = minic_parse_primary(e);
		return minic_val_coerce(-minic_val_to_d(v), v.type);
	}
	if (e->lex.cur.type == TOK_NOT) {
		minic_lex_next(&e->lex);
		return minic_val_int(!minic_val_is_true(minic_parse_primary(e)));
	}
	if (e->lex.cur.type == TOK_INC || e->lex.cur.type == TOK_DEC) {
		double delta = MINIC_INC_DELTA(&e->lex);
		minic_lex_next(&e->lex);
		char name[MINIC_MAX_NAME];
		strncpy(name, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex);
		minic_val_t ov = minic_var_get(e, name);
		minic_val_t nv = minic_val_coerce(minic_val_to_d(ov) + delta, ov.type);
		minic_var_set(e, name, nv);
		return nv;
	}
	if (e->lex.cur.type == TOK_NUMBER || e->lex.cur.type == TOK_CHAR_LIT || e->lex.cur.type == TOK_STR_LIT) {
		minic_val_t v = e->lex.cur.val;
		minic_lex_next(&e->lex);
		return v;
	}
	if (e->lex.cur.type == TOK_IDENT) {
		char name[MINIC_MAX_NAME];
		strncpy(name, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex);

		if (strcmp(name, "sizeof") == 0) {
			minic_expect(e, TOK_LPAREN);
			char type_name[MINIC_MAX_NAME];
			strncpy(type_name, e->lex.cur.text, MINIC_MAX_NAME - 1);
			minic_lex_next(&e->lex); // Consume type name
			minic_expect(e, TOK_RPAREN);
			minic_struct_t *def = minic_struct_get(e, type_name);
			return minic_val_int(def != NULL ? def->size : 0);
		}

		if (e->lex.cur.type == TOK_LBRACKET) {
			minic_lex_next(&e->lex); // Consume '['
			int idx = (int)minic_val_to_d(minic_parse_cond(e));
			minic_expect(e, TOK_RBRACKET);
			return minic_arr_elem_get(e, name, idx);
		}

		if (e->lex.cur.type == TOK_LPAREN) {
			minic_lex_next(&e->lex); // Consume '('
			return minic_parse_call(e, name);
		}

		if (e->lex.cur.type == TOK_DOT || e->lex.cur.type == TOK_ARROW) {
			minic_struct_t *def = minic_var_struct(e, name);
			if (def == NULL) {
				minic_error(e, "'%s' is not a struct", name);
				return minic_val_int(0);
			}
			void *base = minic_val_to_ptr(minic_var_get(e, name));
			char  field[MINIC_MAX_NAME];
			minic_lex_next(&e->lex); // Consume '.' or '->'
			strncpy(field, e->lex.cur.text, MINIC_MAX_NAME - 1);
			minic_lex_next(&e->lex);
			minic_val_t v = minic_struct_field_get_base(e, base, def, field);
			// Handle chained '->' / '.' access (e.g. node->inputs->buffer)
			while ((e->lex.cur.type == TOK_ARROW || e->lex.cur.type == TOK_DOT) && !e->error) {
				int fidx = minic_struct_field_idx(def, field);
				if (fidx < 0 || def->field_structs[fidx][0] == '\0') {
					break;
				}
				minic_struct_t *next_def = minic_struct_get(e, def->field_structs[fidx]);
				if (next_def == NULL) {
					break;
				}
				minic_lex_next(&e->lex); // Consume '->' or '.'
				strncpy(field, e->lex.cur.text, MINIC_MAX_NAME - 1);
				minic_lex_next(&e->lex);
				base = minic_val_to_ptr(v);
				def  = next_def;
				v    = minic_struct_field_get_base(e, base, def, field);
			}
			if (e->lex.cur.type == TOK_LBRACKET) {
				minic_lex_next(&e->lex); // Consume '['
				int idx = (int)minic_val_to_d(minic_parse_cond(e));
				minic_expect(e, TOK_RBRACKET);
				return minic_ptr_index_get(v, idx);
			}
			return v;
		}

		// If not a variable, check if it's a minic function (pass-as-pointer)
		minic_func_t *fn = minic_func_get(e, name);
		if (fn != NULL) {
			return minic_val_ptr(fn);
		}
		// Check for known enum constant
		int ec = minic_enum_const_get(name);
		if (ec >= 0) {
			return minic_val_int(ec);
		}
		// Check for a registered host global
		minic_val_t gv;
		if (minic_var_find(e, name) == NULL && minic_global_get(name, &gv)) {
			return gv;
		}
		return minic_var_get(e, name);
	}
	if (e->lex.cur.type == TOK_LPAREN) {
		minic_lex_next(&e->lex);
		minic_val_t v = minic_parse_cond(e);
		minic_expect(e, TOK_RPAREN);
		return v;
	}
	return minic_val_int(0);
}

// term: primary (('*' | '/') primary)*
static minic_val_t minic_parse_term(minic_env_t *e) {
	minic_val_t v = minic_parse_primary(e);
	while (e->lex.cur.type == TOK_STAR || e->lex.cur.type == TOK_SLASH) {
		minic_tok_type_t op = e->lex.cur.type;
		minic_lex_next(&e->lex);
		v = minic_arith(v, minic_parse_primary(e), op);
	}
	return v;
}

// expr: term (('+' | '-') term)*
static minic_val_t minic_parse_expr(minic_env_t *e) {
	minic_val_t v = minic_parse_term(e);
	while (e->lex.cur.type == TOK_PLUS || e->lex.cur.type == TOK_MINUS) {
		minic_tok_type_t op = e->lex.cur.type;
		minic_lex_next(&e->lex);
		v = minic_arith(v, minic_parse_term(e), op);
	}
	return v;
}

// cmp: expr (('=='|'!='|'<'|'>'|'<='|'>=') expr)?
static minic_val_t minic_parse_cmp(minic_env_t *e) {
	minic_val_t      v  = minic_parse_expr(e);
	minic_tok_type_t op = e->lex.cur.type;
	if (op == TOK_EQ || op == TOK_NEQ || op == TOK_LT || op == TOK_GT || op == TOK_LE || op == TOK_GE) {
		minic_lex_next(&e->lex);
		double a = minic_val_to_d(v);
		double b = minic_val_to_d(minic_parse_expr(e));
		int    res;
		switch (op) {
		case TOK_EQ:
			res = a == b;
			break;
		case TOK_NEQ:
			res = a != b;
			break;
		case TOK_LT:
			res = a < b;
			break;
		case TOK_GT:
			res = a > b;
			break;
		case TOK_LE:
			res = a <= b;
			break;
		default:
			res = a >= b;
			break;
		}
		return minic_val_int(res);
	}
	return v;
}

// cond: cmp (('&&' | '||') cmp)*
static minic_val_t minic_parse_cond(minic_env_t *e) {
	minic_val_t v = minic_parse_cmp(e);
	while (e->lex.cur.type == TOK_AND || e->lex.cur.type == TOK_OR) {
		minic_tok_type_t op = e->lex.cur.type;
		minic_lex_next(&e->lex);
		int vi = minic_val_is_true(v);
		int ri = minic_val_is_true(minic_parse_cmp(e));
		v      = minic_val_int(op == TOK_AND ? (vi && ri) : (vi || ri));
	}
	return v;
}

static void minic_skip_block(minic_env_t *e) {
	if (e->lex.cur.type != TOK_LBRACE) {
		// Single-statement body: skip until ';'
		while (e->lex.cur.type != TOK_SEMICOLON && e->lex.cur.type != TOK_EOF) {
			minic_lex_next(&e->lex);
		}
		minic_lex_next(&e->lex); // Consume ';'
		return;
	}
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
}

// struct value or pointer declaration; the struct type name has been consumed
static void minic_parse_struct_decl(minic_env_t *e, const char *sname) {
	minic_struct_t *def = minic_struct_get(e, sname);
	if (def == NULL) {
		minic_error(e, "unknown struct '%s'", sname);
		return;
	}
	bool is_ptr = (e->lex.cur.type == TOK_STAR);
	if (is_ptr) {
		minic_lex_next(&e->lex);
	}
	char vname[MINIC_MAX_NAME];
	strncpy(vname, e->lex.cur.text, MINIC_MAX_NAME - 1);
	minic_lex_next(&e->lex);
	minic_vartype_set(e, vname, sname);
	if (is_ptr) {
		minic_val_t v = minic_val_ptr(NULL);
		if (e->lex.cur.type == TOK_ASSIGN) {
			minic_lex_next(&e->lex);
			v = minic_parse_cond(e);
		}
		minic_var_decl(e, vname, MINIC_T_PTR, v);
	}
	else {
		// Value declaration: boxed field storage in the arena
		void *base = minic_alloc(def->field_count * (int)sizeof(minic_val_t));
		memset(base, 0, def->field_count * sizeof(minic_val_t));
		if (e->lex.cur.type == TOK_ASSIGN) {
			minic_lex_next(&e->lex);
			minic_val_t v = minic_parse_cond(e);
			if (v.type == MINIC_T_PTR && v.p != NULL) {
				memcpy(base, v.p, def->field_count * sizeof(minic_val_t));
			}
		}
		minic_var_decl(e, vname, MINIC_T_PTR, minic_val_ptr(base));
	}
	minic_expect(e, TOK_SEMICOLON);
}

// The increment clause of a for loop: ++i, i++, i += x, i = x
static void minic_parse_for_incr(minic_env_t *e) {
	if (e->lex.cur.type == TOK_INC || e->lex.cur.type == TOK_DEC) {
		double delta = MINIC_INC_DELTA(&e->lex);
		minic_lex_next(&e->lex);
		minic_val_t ov = minic_var_get(e, e->lex.cur.text);
		minic_var_set(e, e->lex.cur.text, minic_val_coerce(minic_val_to_d(ov) + delta, ov.type));
		return;
	}
	if (e->lex.cur.type != TOK_IDENT) {
		return;
	}
	char name[MINIC_MAX_NAME];
	strncpy(name, e->lex.cur.text, MINIC_MAX_NAME - 1);
	minic_lex_next(&e->lex);
	if (e->lex.cur.type == TOK_INC || e->lex.cur.type == TOK_DEC) {
		double      delta = MINIC_INC_DELTA(&e->lex);
		minic_val_t ov    = minic_var_get(e, name);
		minic_var_set(e, name, minic_val_coerce(minic_val_to_d(ov) + delta, ov.type));
	}
	else if (e->lex.cur.type == TOK_PLUS_ASSIGN || e->lex.cur.type == TOK_MINUS_ASSIGN || e->lex.cur.type == TOK_MUL_ASSIGN ||
	         e->lex.cur.type == TOK_DIV_ASSIGN) {
		minic_tok_type_t op = e->lex.cur.type;
		minic_lex_next(&e->lex);
		minic_val_t dv = minic_parse_cond(e);
		minic_val_t ov = minic_var_get(e, name);
		minic_var_set(e, name, minic_val_coerce(minic_apply_op(op, minic_val_to_d(ov), minic_val_to_d(dv)), ov.type));
	}
	else if (e->lex.cur.type == TOK_ASSIGN) {
		minic_lex_next(&e->lex);
		minic_var_set(e, name, minic_parse_cond(e));
	}
}

static void minic_parse_stmt(minic_env_t *e) {
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

	if (e->lex.cur.type == TOK_STRUCT) {
		minic_lex_next(&e->lex); // Consume 'struct'
		char sname[MINIC_MAX_NAME];
		strncpy(sname, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex); // Consume struct type name
		minic_parse_struct_decl(e, sname);
		return;
	}

	// Typedef'd int name (e.g. from typedef enum): alias_t var = expr;
	if (e->lex.cur.type == TOK_IDENT && minic_is_int_typedef(e->lex.cur.text)) {
		minic_lex_next(&e->lex); // Consume alias name
		char vname[MINIC_MAX_NAME];
		strncpy(vname, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex); // Consume var name
		minic_val_t v = minic_val_int(0);
		if (e->lex.cur.type == TOK_ASSIGN) {
			minic_lex_next(&e->lex);
			v = minic_parse_cond(e);
		}
		minic_var_decl(e, vname, MINIC_T_INT, minic_val_cast(v, MINIC_T_INT));
		minic_expect(e, TOK_SEMICOLON);
		return;
	}

	// Typedef'd struct name used as variable type: alias_t var; or alias_t *var = ...;
	if (e->lex.cur.type == TOK_IDENT && minic_struct_get(e, e->lex.cur.text) != NULL) {
		char sname[MINIC_MAX_NAME];
		strncpy(sname, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex); // Consume alias name
		minic_parse_struct_decl(e, sname);
		return;
	}

	if (minic_tok_is_type(e->lex.cur.type)) {
		minic_type_t base_type = minic_tok_to_type(e->lex.cur.type); // Type before '*'
		minic_type_t dtype     = base_type;
		minic_lex_next(&e->lex); // Consume type keyword

		bool is_ptr = (e->lex.cur.type == TOK_STAR);
		if (is_ptr) {
			dtype = MINIC_T_PTR;
			minic_lex_next(&e->lex);
		}
		char name[MINIC_MAX_NAME];
		strncpy(name, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex);

		if (e->lex.cur.type == TOK_LBRACKET) {
			minic_lex_next(&e->lex); // Consume '['
			int count = (int)minic_val_to_d(minic_parse_cond(e));
			minic_expect(e, TOK_RBRACKET);
			minic_arr_decl(e, name, count, dtype);
			minic_expect(e, TOK_SEMICOLON);
			return;
		}

		// Optional initializer: default to 0 / NULL when omitted
		minic_val_t v = is_ptr ? minic_val_ptr(NULL) : minic_val_coerce(0.0, dtype);
		if (is_ptr) {
			v.deref_type = base_type;
		}
		if (e->lex.cur.type == TOK_ASSIGN) {
			minic_lex_next(&e->lex);
			v = minic_parse_cond(e);
			if (is_ptr) {
				v.type = MINIC_T_PTR;
				if (v.p == NULL) {
					// NULL literal passed as integer 0 — convert
					uintptr_t ua = (uintptr_t)(uint64_t)minic_val_to_d(v);
					v.p          = (ua == 0) ? NULL : (void *)ua;
				}
				// Only stamp the declared element type for native C pointers.
				// Pointers into the active arena (e.g. from &var) use the MINIC_T_PTR sentinel
				// to signal that dereferencing reads a full minic_val_t.
				if (!minic_in_arena(v.p)) {
					v.deref_type = base_type;
				}
			}
		}
		minic_var_decl(e, name, dtype, v);
		minic_expect(e, TOK_SEMICOLON);
		return;
	}

	if (e->lex.cur.type == TOK_RETURN) {
		minic_lex_next(&e->lex);
		e->return_val = minic_parse_cond(e);
		e->returning  = true;
		minic_expect(e, TOK_SEMICOLON);
		return;
	}

	if (e->lex.cur.type == TOK_IDENT) {
		char name[MINIC_MAX_NAME];
		strncpy(name, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex);

		// Unknown opaque type followed by '*' + ident: local pointer declaration.
		// e.g. ui_handle_t *h; or my_t *p = create_p();
		if (e->lex.cur.type == TOK_STAR) {
			minic_lex_next(&e->lex); // Consume '*'
			char vname[MINIC_MAX_NAME];
			strncpy(vname, e->lex.cur.text, MINIC_MAX_NAME - 1);
			minic_lex_next(&e->lex); // Consume var name
			minic_val_t v = minic_val_ptr(NULL);
			if (e->lex.cur.type == TOK_ASSIGN) {
				minic_lex_next(&e->lex);
				v = minic_parse_cond(e);
			}
			minic_var_decl(e, vname, MINIC_T_PTR, v);
			minic_expect(e, TOK_SEMICOLON);
			return;
		}

		if (e->lex.cur.type == TOK_DOT || e->lex.cur.type == TOK_ARROW) {
			bool is_arrow = (e->lex.cur.type == TOK_ARROW);
			minic_lex_next(&e->lex);
			char field[MINIC_MAX_NAME];
			strncpy(field, e->lex.cur.text, MINIC_MAX_NAME - 1);
			minic_lex_next(&e->lex);
			minic_struct_t *def = minic_var_struct(e, name);
			if (def == NULL) {
				minic_error(e, "'%s' is not a struct%s", name, is_arrow ? " pointer" : "");
				return;
			}
			void *base = minic_val_to_ptr(minic_var_get(e, name));
			// Descend chained member access to the last field (e.g. o->transform->radius = x)
			while ((e->lex.cur.type == TOK_DOT || e->lex.cur.type == TOK_ARROW) && !e->error) {
				int fidx = minic_struct_field_idx(def, field);
				if (fidx < 0 || def->field_structs[fidx][0] == '\0') {
					break;
				}
				minic_struct_t *next_def = minic_struct_get(e, def->field_structs[fidx]);
				if (next_def == NULL) {
					break;
				}
				minic_lex_next(&e->lex); // Consume '->' or '.'
				base = minic_val_to_ptr(minic_struct_field_get_base(e, base, def, field));
				def  = next_def;
				strncpy(field, e->lex.cur.text, MINIC_MAX_NAME - 1);
				minic_lex_next(&e->lex);
			}
			if (e->lex.cur.type == TOK_LBRACKET) {
				minic_lex_next(&e->lex);
				int idx = (int)minic_val_to_d(minic_parse_cond(e));
				minic_expect(e, TOK_RBRACKET);
				minic_expect(e, TOK_ASSIGN);
				minic_val_t v = minic_parse_cond(e);
				minic_ptr_index_set(minic_struct_field_get_base(e, base, def, field), idx, v);
			}
			else if (e->lex.cur.type == TOK_INC || e->lex.cur.type == TOK_DEC) {
				double delta = MINIC_INC_DELTA(&e->lex);
				minic_lex_next(&e->lex);
				minic_val_t ov = minic_struct_field_get_base(e, base, def, field);
				minic_struct_field_set_base(e, base, def, field, minic_val_coerce(minic_val_to_d(ov) + delta, ov.type));
			}
			else if (e->lex.cur.type == TOK_PLUS_ASSIGN || e->lex.cur.type == TOK_MINUS_ASSIGN || e->lex.cur.type == TOK_MUL_ASSIGN ||
			         e->lex.cur.type == TOK_DIV_ASSIGN) {
				minic_tok_type_t op = e->lex.cur.type;
				minic_lex_next(&e->lex);
				minic_val_t dv = minic_parse_cond(e);
				minic_val_t ov = minic_struct_field_get_base(e, base, def, field);
				minic_struct_field_set_base(e, base, def, field, minic_val_coerce(minic_apply_op(op, minic_val_to_d(ov), minic_val_to_d(dv)), ov.type));
			}
			else {
				minic_expect(e, TOK_ASSIGN);
				minic_struct_field_set_base(e, base, def, field, minic_parse_cond(e));
			}
			minic_expect(e, TOK_SEMICOLON);
			return;
		}

		if (e->lex.cur.type == TOK_LPAREN) {
			minic_lex_next(&e->lex);
			minic_parse_call(e, name);
			minic_expect(e, TOK_SEMICOLON);
			return;
		}

		if (e->lex.cur.type == TOK_INC || e->lex.cur.type == TOK_DEC) {
			double delta = MINIC_INC_DELTA(&e->lex);
			minic_lex_next(&e->lex);
			minic_val_t ov = minic_var_get(e, name);
			minic_var_set(e, name, minic_val_coerce(minic_val_to_d(ov) + delta, ov.type));
			minic_expect(e, TOK_SEMICOLON);
			return;
		}

		if (e->lex.cur.type == TOK_PLUS_ASSIGN || e->lex.cur.type == TOK_MINUS_ASSIGN || e->lex.cur.type == TOK_MUL_ASSIGN ||
		    e->lex.cur.type == TOK_DIV_ASSIGN) {
			minic_tok_type_t op = e->lex.cur.type;
			minic_lex_next(&e->lex);
			minic_val_t dv = minic_parse_cond(e);
			minic_val_t ov = minic_var_get(e, name);
			minic_var_set(e, name, minic_val_coerce(minic_apply_op(op, minic_val_to_d(ov), minic_val_to_d(dv)), ov.type));
			minic_expect(e, TOK_SEMICOLON);
			return;
		}

		if (e->lex.cur.type == TOK_LBRACKET) {
			minic_lex_next(&e->lex);
			int idx = (int)minic_val_to_d(minic_parse_cond(e));
			minic_expect(e, TOK_RBRACKET);
			minic_expect(e, TOK_ASSIGN);
			minic_arr_elem_set(e, name, idx, minic_parse_cond(e));
			minic_expect(e, TOK_SEMICOLON);
			return;
		}

		minic_expect(e, TOK_ASSIGN);
		minic_var_set(e, name, minic_parse_cond(e));
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
		if (minic_tok_is_type(e->lex.cur.type)) {
			minic_lex_next(&e->lex);
		}
		{
			char iname[MINIC_MAX_NAME];
			strncpy(iname, e->lex.cur.text, MINIC_MAX_NAME - 1);
			minic_lex_next(&e->lex);
			minic_expect(e, TOK_ASSIGN);
			minic_var_set(e, iname, minic_parse_cond(e));
		}
		int cond_pos = e->lex.pos;
		minic_lex_next(&e->lex); // Consume ';'

		// Scan ahead for the increment clause and body positions
		int incr_pos, body_pos;
		{
			minic_lexer_t tmp = {0};
			tmp.src           = e->lex.src;
			tmp.pos           = cond_pos;
			minic_lex_next(&tmp);
			while (tmp.cur.type != TOK_SEMICOLON && tmp.cur.type != TOK_EOF) {
				minic_lex_next(&tmp);
			}
			incr_pos = tmp.pos;
			minic_lex_next(&tmp);
			while (tmp.cur.type != TOK_RPAREN && tmp.cur.type != TOK_EOF) {
				minic_lex_next(&tmp);
			}
			minic_lex_next(&tmp);
			body_pos = tmp.pos - 1;
		}

		for (;;) {
			e->continuing = false;
			e->lex.pos    = cond_pos;
			minic_lex_next(&e->lex);
			int cond = minic_val_is_true(minic_parse_cond(e));
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
			minic_parse_for_incr(e);
		}
		return;
	}

	if (e->lex.cur.type == TOK_STAR) {
		// Pointer write: *expr = val;  or  *expr += val;  or  *expr++;  etc.
		minic_lex_next(&e->lex);
		minic_val_t pv = minic_parse_primary(e);
		if (e->lex.cur.type == TOK_INC || e->lex.cur.type == TOK_DEC) {
			double delta = MINIC_INC_DELTA(&e->lex);
			minic_lex_next(&e->lex);
			minic_store_op(pv, TOK_PLUS_ASSIGN, minic_val_float((float)delta));
			minic_expect(e, TOK_SEMICOLON);
			return;
		}
		minic_tok_type_t op = e->lex.cur.type; // TOK_ASSIGN or a compound assign
		minic_lex_next(&e->lex);
		minic_store_op(pv, op, minic_parse_cond(e));
		minic_expect(e, TOK_SEMICOLON);
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

	minic_error(e, "unexpected token at start of statement");
}

static void minic_parse_block(minic_env_t *e) {
	int saved_var_count     = e->var_count;
	int saved_vartype_count = e->vartype_count;
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
	e->var_count     = saved_var_count;
	e->vartype_count = saved_vartype_count;
}

// ██████╗ ██╗   ██╗███╗   ██╗
// ██╔══██╗██║   ██║████╗  ██║
// ██████╔╝██║   ██║██╔██╗ ██║
// ██╔══██╗██║   ██║██║╚██╗██║
// ██║  ██║╚██████╔╝██║ ╚████║
// ╚═╝  ╚═╝ ╚═════╝ ╚═╝  ╚═══╝

// Consume a type specifier, return true if found
static bool minic_lex_type(minic_env_t *e) {
	if (minic_tok_is_type(e->lex.cur.type)) {
		minic_lex_next(&e->lex);
		return true;
	}
	if (e->lex.cur.type == TOK_STRUCT) {
		minic_lex_next(&e->lex); // Consume 'struct'
		minic_lex_next(&e->lex); // Consume struct name
		return true;
	}
	// Typedef'd struct or int name used as a type specifier
	if (e->lex.cur.type == TOK_IDENT && (minic_struct_get(e, e->lex.cur.text) != NULL || minic_is_int_typedef(e->lex.cur.text))) {
		minic_lex_next(&e->lex);
		return true;
	}
	return false;
}

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
				continue; // Forward decl or typedef-without-body — skip
			}
			if (e->struct_count >= e->struct_cap) {
				break;
			}
			minic_struct_t *def = &e->structs[e->struct_count];
			memset(def, 0, sizeof(minic_struct_t));
			strncpy(def->name, struct_name, MINIC_MAX_NAME - 1);
			minic_lex_next(&l); // Consume '{'

			while (l.cur.type != TOK_RBRACE && l.cur.type != TOK_EOF) {
				// Field type: builtin keyword, 'struct name' or typedef'd name
				if (l.cur.type == TOK_STRUCT) {
					minic_lex_next(&l);
					minic_lex_next(&l);
				}
				else if (minic_tok_is_type(l.cur.type) || l.cur.type == TOK_IDENT) {
					minic_lex_next(&l);
				}
				else {
					minic_lex_next(&l);
					continue;
				}
				if (l.cur.type == TOK_STAR) {
					minic_lex_next(&l);
				}
				if (l.cur.type == TOK_IDENT && def->field_count < MINIC_MAX_STRUCT_FIELDS) {
					strncpy(def->fields[def->field_count++], l.cur.text, MINIC_MAX_NAME - 1);
					minic_lex_next(&l);
				}
				while (l.cur.type != TOK_SEMICOLON && l.cur.type != TOK_RBRACE && l.cur.type != TOK_EOF) {
					minic_lex_next(&l);
				}
				if (l.cur.type == TOK_SEMICOLON) {
					minic_lex_next(&l);
				}
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

// First pass: register all function definitions and globals, stop at 'main'
static void minic_register_funcs(minic_env_t *e) {
	while (e->lex.cur.type != TOK_EOF) {
		// Remember the return-type token before consuming it
		minic_type_t ret_type                    = minic_tok_to_type(e->lex.cur.type);
		char         decl_struct[MINIC_MAX_NAME] = "";
		if (e->lex.cur.type == TOK_IDENT && minic_struct_get(e, e->lex.cur.text) != NULL) {
			strncpy(decl_struct, e->lex.cur.text, MINIC_MAX_NAME - 1);
		}
		if (!minic_lex_type(e)) {
			if (e->lex.cur.type == TOK_IDENT) {
				// Unknown typedef'd type — treat as an opaque pointer type
				strncpy(decl_struct, e->lex.cur.text, MINIC_MAX_NAME - 1);
				minic_lex_next(&e->lex);
				ret_type = MINIC_T_PTR;
			}
			else {
				minic_lex_next(&e->lex);
				continue;
			}
		}
		if (e->lex.cur.type == TOK_STAR) {
			ret_type = MINIC_T_PTR;
			minic_lex_next(&e->lex);
		}
		if (e->lex.cur.type != TOK_IDENT) {
			continue;
		}
		char fname[MINIC_MAX_NAME];
		strncpy(fname, e->lex.cur.text, MINIC_MAX_NAME - 1);
		minic_lex_next(&e->lex);

		if (e->lex.cur.type != TOK_LPAREN) {
			// Global variable declaration: type [*] ident [= expr] ;
			if (decl_struct[0] != '\0') {
				minic_vartype_set(e, fname, decl_struct);
			}
			minic_val_t init = minic_val_coerce(0.0, ret_type);
			if (e->lex.cur.type == TOK_ASSIGN) {
				minic_lex_next(&e->lex); // Consume '='
				init = minic_parse_cond(e);
			}
			minic_var_decl(e, fname, ret_type, init);
			while (e->lex.cur.type != TOK_SEMICOLON && e->lex.cur.type != TOK_EOF) {
				minic_lex_next(&e->lex);
			}
			if (e->lex.cur.type == TOK_SEMICOLON) {
				minic_lex_next(&e->lex);
			}
			continue;
		}
		minic_lex_next(&e->lex); // Consume '('

		minic_func_t fn = {0};
		strncpy(fn.name, fname, MINIC_MAX_NAME - 1);
		fn.ret_type = ret_type;

		while (e->lex.cur.type != TOK_RPAREN && e->lex.cur.type != TOK_EOF) {
			char         pstruct[MINIC_MAX_NAME] = "";
			minic_type_t ptype                   = MINIC_T_INT;
			if (e->lex.cur.type == TOK_STRUCT) {
				minic_lex_next(&e->lex);
				strncpy(pstruct, e->lex.cur.text, MINIC_MAX_NAME - 1);
				minic_lex_next(&e->lex);
				ptype = MINIC_T_PTR;
			}
			else {
				// Capture typedef'd struct name before consuming the type token
				if (e->lex.cur.type == TOK_IDENT && minic_struct_get(e, e->lex.cur.text) != NULL) {
					strncpy(pstruct, e->lex.cur.text, MINIC_MAX_NAME - 1);
				}
				ptype = minic_tok_to_type(e->lex.cur.type);
				minic_lex_type(e); // Consume type
			}
			if (e->lex.cur.type == TOK_STAR) {
				ptype = MINIC_T_PTR;
				minic_lex_next(&e->lex);
			}
			if (e->lex.cur.type == TOK_IDENT && fn.param_count < MINIC_MAX_PARAMS) {
				int pi = fn.param_count++;
				strncpy(fn.params[pi], e->lex.cur.text, MINIC_MAX_NAME - 1);
				strncpy(fn.param_structs[pi], pstruct, MINIC_MAX_NAME - 1);
				fn.param_types[pi] = ptype;
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
	ctx->mem         = (minic_u8 *)malloc(MINIC_MEM_SIZE);
	// Copy the source so the context stays valid after the caller frees its buffer
	int src_len   = (int)strlen(src);
	ctx->src_copy = (char *)malloc(src_len + 1);
	memcpy(ctx->src_copy, src, src_len + 1);

	// Save and install arena pointers so minic_alloc and the lexer use this context
	minic_u8 *prev_mem      = minic_active_mem;
	int      *prev_mem_used = minic_active_mem_used;
	minic_active_mem        = ctx->mem;
	minic_active_mem_used   = &ctx->mem_used;

	minic_env_t *e    = &ctx->e;
	e->lex.src        = ctx->src_copy;
	e->filename       = filename;
	e->var_cap        = 64;
	e->vars           = minic_alloc(e->var_cap * (int)sizeof(minic_var_t));
	e->arr_cap        = 32;
	e->arrs           = minic_alloc(e->arr_cap * (int)sizeof(minic_arr_t));
	e->arr_data       = minic_alloc(512 * (int)sizeof(minic_val_t));
	e->arr_data_used  = minic_alloc((int)sizeof(int));
	*e->arr_data_used = 0;
	e->func_cap       = 32;
	e->funcs          = minic_alloc(e->func_cap * (int)sizeof(minic_func_t));
	e->struct_cap     = MINIC_MAX_STRUCTS;
	e->structs        = minic_alloc(e->struct_cap * (int)sizeof(minic_struct_t));
	e->vartype_cap    = 64;
	e->vartypes       = minic_alloc(e->vartype_cap * (int)sizeof(minic_vartype_t));

	// Seed env with globally pre-registered struct definitions
	for (int i = 0; i < minic_struct_count && e->struct_count < e->struct_cap; ++i) {
		e->structs[e->struct_count++] = minic_structs[i];
	}

	minic_register_structs(e);
	minic_lex_next(&e->lex);
	minic_register_funcs(e);
	for (int i = 0; i < e->func_count; ++i) {
		e->funcs[i].ctx = ctx;
	}

	if (e->lex.cur.type == TOK_RPAREN) {
		minic_lex_next(&e->lex);
	}

	minic_parse_block(e);
	minic_active_mem      = prev_mem;
	minic_active_mem_used = prev_mem_used;

	ctx->result = e->error ? -1.0f : (float)minic_val_to_d(e->return_val);
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

void minic_struct_begin(const char *name, int size) {
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
	minic_struct_cur->size = size;
}

void minic_struct_field(const char *field, int offset, minic_type_t type, minic_type_t deref_type, const char *struct_type) {
	minic_struct_t *s = minic_struct_cur;
	if (s == NULL || s->field_count >= MINIC_MAX_STRUCT_FIELDS) {
		return;
	}
	int i = s->field_count++;
	strncpy(s->fields[i], field, MINIC_MAX_NAME - 1);
	s->offsets[i]     = offset;
	s->types[i]       = type;
	s->deref_types[i] = deref_type;
	if (struct_type != NULL) {
		strncpy(s->field_structs[i], struct_type, MINIC_MAX_NAME - 1);
	}
	if (offset >= 0) {
		s->native = true;
	}
}

void minic_register_struct(const char *name, const char **fields, int field_count) {
	minic_struct_begin(name, 0);
	for (int i = 0; i < field_count; ++i) {
		minic_struct_field(fields[i], -1, MINIC_T_INT, MINIC_T_INT, NULL);
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
			if (minic_globals[i].type == MINIC_T_FLOAT) {
				*out = minic_val_float(*(const float *)minic_globals[i].ptr);
			}
			else {
				*out = minic_val_int(*(const int *)minic_globals[i].ptr);
			}
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

static minic_type_t minic_sig_char(char c) {
	switch (c) {
	case 'f':
		return MINIC_T_FLOAT;
	case 'p':
		return MINIC_T_PTR;
	case 'b':
		return MINIC_T_BOOL;
	case 'c':
		return MINIC_T_CHAR;
	case 'v':
		return MINIC_T_VOID;
	default:
		return MINIC_T_INT;
	}
}

static void minic_parse_sig(minic_ext_func_t *ef) {
	const char *s = ef->sig;
	ef->ret_type  = minic_sig_char(*s);
	if (*s) {
		s++; // skip ret type char
	}
	if (*s == '(') {
		s++; // skip '('
	}
	ef->param_count = 0;
	while (*s && *s != ')') {
		if (*s != ',' && ef->param_count < MINIC_MAX_PARAMS) {
			ef->param_types[ef->param_count++] = minic_sig_char(*s);
		}
		s++;
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

void minic_register(const char *name, const char *sig, minic_ext_fn_raw_t fn) {
	minic_ext_func_t *ef = minic_ext_func_add(name);
	if (ef == NULL) {
		return;
	}
	strncpy(ef->sig, sig != NULL ? sig : "i()", MINIC_MAX_SIG - 1);
	ef->fn = fn;
	minic_parse_sig(ef);
}

void minic_register_native(const char *name, minic_native_fn_t fn) {
	minic_ext_func_t *ef = minic_ext_func_add(name);
	if (ef != NULL) {
		ef->native_fn = fn;
	}
}

minic_ext_func_t *minic_ext_func_get(const char *name) {
	for (int i = 0; i < minic_ext_func_count; ++i) {
		if (strcmp(minic_ext_funcs[i].name, name) == 0) {
			return &minic_ext_funcs[i];
		}
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
// Args are normalized into int/float/ptr slots and the C function is called
// through an exactly-typed cast (required for wasm and all native ABIs).
// One D-line per supported signature; add new combinations to the table below.

typedef union {
	int   i;
	float f;
	void *p;
} minic_arg_t;

// Per-class C types and argument accessors
#define TY_i   int
#define TY_f   float
#define TY_p   void *
#define A_i(k) a[k].i
#define A_f(k) a[k].f
#define A_p(k) a[k].p

// C return types and result boxing per minic return class
#define CT_INT       int
#define CT_BOOL      bool
#define CT_CHAR      char
#define CT_FLOAT     float
#define CT_PTR       void *
#define CT_VOID      void
#define RET_INT(x)   return minic_val_int(x)
#define RET_BOOL(x)  return minic_val_int((int)(x))
#define RET_CHAR(x)  return minic_val_int((int)(x))
#define RET_FLOAT(x) return minic_val_float(x)
#define RET_PTR(x)   return minic_val_ptr(x)
#define RET_VOID(x)              \
	do {                         \
		x;                       \
		return minic_val_int(0); \
	} while (0)

// Exactly-typed call builders per arity
#define C0(R)                     ((CT_##R (*)(void))fn)()
#define C1(R, a0)                 ((CT_##R (*)(TY_##a0))fn)(A_##a0(0))
#define C2(R, a0, a1)             ((CT_##R (*)(TY_##a0, TY_##a1))fn)(A_##a0(0), A_##a1(1))
#define C3(R, a0, a1, a2)         ((CT_##R (*)(TY_##a0, TY_##a1, TY_##a2))fn)(A_##a0(0), A_##a1(1), A_##a2(2))
#define C4(R, a0, a1, a2, a3)     ((CT_##R (*)(TY_##a0, TY_##a1, TY_##a2, TY_##a3))fn)(A_##a0(0), A_##a1(1), A_##a2(2), A_##a3(3))
#define C5(R, a0, a1, a2, a3, a4) ((CT_##R (*)(TY_##a0, TY_##a1, TY_##a2, TY_##a3, TY_##a4))fn)(A_##a0(0), A_##a1(1), A_##a2(2), A_##a3(3), A_##a4(4))
#define C6(R, a0, a1, a2, a3, a4, a5) \
	((CT_##R (*)(TY_##a0, TY_##a1, TY_##a2, TY_##a3, TY_##a4, TY_##a5))fn)(A_##a0(0), A_##a1(1), A_##a2(2), A_##a3(3), A_##a4(4), A_##a5(5))
#define C7(R, a0, a1, a2, a3, a4, a5, a6) \
	((CT_##R (*)(TY_##a0, TY_##a1, TY_##a2, TY_##a3, TY_##a4, TY_##a5, TY_##a6))fn)(A_##a0(0), A_##a1(1), A_##a2(2), A_##a3(3), A_##a4(4), A_##a5(5), A_##a6(6))
#define C8(R, a0, a1, a2, a3, a4, a5, a6, a7)                                                                                                                  \
	((CT_##R (*)(TY_##a0, TY_##a1, TY_##a2, TY_##a3, TY_##a4, TY_##a5, TY_##a6, TY_##a7))fn)(A_##a0(0), A_##a1(1), A_##a2(2), A_##a3(3), A_##a4(4), A_##a5(5), \
	                                                                                         A_##a6(6), A_##a7(7))
#define C9(R, a0, a1, a2, a3, a4, a5, a6, a7, a8)                                                                                                            \
	((CT_##R (*)(TY_##a0, TY_##a1, TY_##a2, TY_##a3, TY_##a4, TY_##a5, TY_##a6, TY_##a7, TY_##a8))fn)(A_##a0(0), A_##a1(1), A_##a2(2), A_##a3(3), A_##a4(4), \
	                                                                                                  A_##a5(5), A_##a6(6), A_##a7(7), A_##a8(8))
#define C10(R, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9)                                            \
	((CT_##R (*)(TY_##a0, TY_##a1, TY_##a2, TY_##a3, TY_##a4, TY_##a5, TY_##a6, TY_##a7, TY_##a8, \
	             TY_##a9))fn)(A_##a0(0), A_##a1(1), A_##a2(2), A_##a3(3), A_##a4(4), A_##a5(5), A_##a6(6), A_##a7(7), A_##a8(8), A_##a9(9))

// Dispatch table entries: match return class + arg descriptor, then call
#define D0(R)                                  \
	if (rt == MINIC_T_##R && adesc[0] == '\0') \
	RET_##R(C0(R))
#define D1(R, a0)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0) == 0) \
	RET_##R(C1(R, a0))
#define D2(R, a0, a1)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1) == 0) \
	RET_##R(C2(R, a0, a1))
#define D3(R, a0, a1, a2)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1 #a2) == 0) \
	RET_##R(C3(R, a0, a1, a2))
#define D4(R, a0, a1, a2, a3)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1 #a2 #a3) == 0) \
	RET_##R(C4(R, a0, a1, a2, a3))
#define D5(R, a0, a1, a2, a3, a4)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1 #a2 #a3 #a4) == 0) \
	RET_##R(C5(R, a0, a1, a2, a3, a4))
#define D6(R, a0, a1, a2, a3, a4, a5)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1 #a2 #a3 #a4 #a5) == 0) \
	RET_##R(C6(R, a0, a1, a2, a3, a4, a5))
#define D7(R, a0, a1, a2, a3, a4, a5, a6)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1 #a2 #a3 #a4 #a5 #a6) == 0) \
	RET_##R(C7(R, a0, a1, a2, a3, a4, a5, a6))
#define D8(R, a0, a1, a2, a3, a4, a5, a6, a7)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1 #a2 #a3 #a4 #a5 #a6 #a7) == 0) \
	RET_##R(C8(R, a0, a1, a2, a3, a4, a5, a6, a7))
#define D9(R, a0, a1, a2, a3, a4, a5, a6, a7, a8)                                     \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1 #a2 #a3 #a4 #a5 #a6 #a7 #a8) == 0) \
	RET_##R(C9(R, a0, a1, a2, a3, a4, a5, a6, a7, a8))
#define D10(R, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9)                                    \
	if (rt == MINIC_T_##R && strcmp(adesc, #a0 #a1 #a2 #a3 #a4 #a5 #a6 #a7 #a8 #a9) == 0) \
	RET_##R(C10(R, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9))

minic_val_t minic_dispatch(minic_ext_func_t *ef, minic_val_t *args, int argc) {
	if (ef->native_fn != NULL) {
		return ef->native_fn(args, argc);
	}

	// Normalize args by declared param type, build the arg descriptor
	minic_arg_t a[MINIC_MAX_PARAMS] = {0};
	char        adesc[MINIC_MAX_PARAMS + 1];
	int         n = argc < ef->param_count ? argc : ef->param_count;
	for (int i = 0; i < n; i++) {
		double dv = minic_val_to_d(args[i]);
		switch (ef->param_types[i]) {
		case MINIC_T_FLOAT:
			a[i].f   = (float)dv;
			adesc[i] = 'f';
			break;
		case MINIC_T_EMBED:
		case MINIC_T_PTR:
			a[i].p   = (args[i].type == MINIC_T_PTR) ? args[i].p : ((dv == 0.0) ? NULL : (void *)(uintptr_t)(uint64_t)dv);
			adesc[i] = 'p';
			break;
		default:
			a[i].i   = (int)dv;
			adesc[i] = 'i';
			break;
		}
	}
	adesc[n] = '\0';

	minic_ext_fn_raw_t fn = ef->fn;
	minic_type_t       rt = ef->ret_type;

	D0(INT);
	D1(INT, i);
	D1(INT, f);
	D1(INT, p);
	D2(INT, i, i);
	D2(INT, f, f);
	D2(INT, i, p);
	D2(INT, p, f);
	D2(INT, p, i);
	D2(INT, p, p);
	D3(INT, i, i, i);
	D3(INT, p, i, i);
	D3(INT, p, i, p);
	D3(INT, p, p, i);
	D3(INT, p, p, p);
	D4(INT, i, i, i, i);
	D4(INT, p, i, i, p);
	D4(INT, p, p, p, p);
	D6(INT, p, p, f, f, f, f);
	D6(INT, p, p, p, i, i, i);
	D7(INT, p, i, f, f, i, p, p);
	D7(INT, p, i, i, i, i, i, i);

	D0(BOOL);
	D1(BOOL, i);
	D1(BOOL, f);
	D1(BOOL, p);
	D2(BOOL, p, i);
	D2(BOOL, p, p);
	D3(BOOL, p, i, i);
	D3(BOOL, p, i, p);
	D3(BOOL, p, p, i);
	D3(BOOL, p, p, p);
	D4(BOOL, f, f, f, f);
	D4(BOOL, p, i, p, p);
	D4(BOOL, p, p, i, i);
	D5(BOOL, p, p, i, i, i);
	D6(BOOL, p, i, i, i, i, i);

	D0(CHAR);
	D1(CHAR, i);
	D1(CHAR, p);
	D2(CHAR, p, i);
	D2(CHAR, p, p);

	D0(FLOAT);
	D1(FLOAT, f);
	D1(FLOAT, i);
	D1(FLOAT, p);
	D2(FLOAT, f, f);
	D2(FLOAT, p, f);
	D2(FLOAT, p, i);
	D2(FLOAT, p, p);
	D3(FLOAT, f, f, f);
	D3(FLOAT, p, f, f);
	D3(FLOAT, p, p, i);
	D4(FLOAT, f, f, f, f);
	D4(FLOAT, p, f, f, f);
	D4(FLOAT, p, p, i, f);
	D5(FLOAT, f, f, f, f, f);
	D5(FLOAT, p, f, f, f, f);
	D5(FLOAT, p, i, p, i, i);
	D6(FLOAT, f, f, f, f, f, f);
	D6(FLOAT, p, f, f, f, f, f);
	D7(FLOAT, f, f, f, f, f, f, f);
	D7(FLOAT, p, f, f, f, f, f, f);
	D8(FLOAT, p, f, f, f, f, f, f, f);
	D9(FLOAT, f, f, f, f, f, f, f, f, f);
	D9(FLOAT, p, p, f, f, i, f, i, i, i);

	D0(VOID);
	D1(VOID, f);
	D1(VOID, i);
	D1(VOID, p);
	D2(VOID, f, f);
	D2(VOID, f, p);
	D2(VOID, i, i);
	D2(VOID, p, f);
	D2(VOID, p, i);
	D2(VOID, p, p);
	D3(VOID, f, f, f);
	D3(VOID, i, i, f);
	D3(VOID, i, i, i);
	D3(VOID, i, p, p);
	D3(VOID, p, f, f);
	D3(VOID, p, i, f);
	D3(VOID, p, i, i);
	D3(VOID, p, i, p);
	D3(VOID, p, p, f);
	D3(VOID, p, p, i);
	D3(VOID, p, p, p);
	D4(VOID, f, f, f, f);
	D4(VOID, f, f, f, i);
	D4(VOID, f, f, f, p);
	D4(VOID, i, i, i, i);
	D4(VOID, p, i, i, f);
	D4(VOID, p, i, i, i);
	D4(VOID, p, i, i, p);
	D4(VOID, p, i, p, i);
	D4(VOID, p, p, i, f);
	D4(VOID, p, p, i, i);
	D4(VOID, p, p, i, p);
	D4(VOID, p, p, p, i);
	D4(VOID, p, p, p, p);
	D5(VOID, f, f, f, f, f);
	D5(VOID, f, f, f, f, i);
	D5(VOID, f, f, f, i, f);
	D5(VOID, i, f, f, f, f);
	D5(VOID, p, f, f, f, f);
	D5(VOID, p, f, f, i, i);
	D5(VOID, p, i, i, i, i);
	D5(VOID, p, p, p, p, p);
	D6(VOID, f, f, f, f, f, f);
	D6(VOID, f, f, f, f, i, f);
	D6(VOID, i, i, i, i, i, i);
	D6(VOID, p, i, i, f, f, f);
	D6(VOID, p, p, p, i, i, f);
	D7(VOID, p, f, f, f, f, f, f);
	D7(VOID, p, f, f, f, f, i, i);
	D7(VOID, p, i, i, f, f, f, f);
	D9(VOID, p, f, f, f, f, f, f, f, f);
	D10(VOID, p, p, p, p, p, p, p, p, p, p);

	D0(PTR);
	D1(PTR, f);
	D1(PTR, i);
	D1(PTR, p);
	D2(PTR, f, f);
	D2(PTR, i, i);
	D2(PTR, p, i);
	D2(PTR, p, p);
	D3(PTR, f, f, f);
	D3(PTR, i, i, i);
	D3(PTR, p, f, f);
	D3(PTR, p, i, i);
	D3(PTR, p, p, i);
	D3(PTR, p, p, p);
	D4(PTR, f, f, f, f);
	D4(PTR, p, p, p, i);
	D4(PTR, p, p, p, p);
	D5(PTR, f, f, f, f, f);
	D5(PTR, p, i, i, p, i);
	D5(PTR, p, p, i, i, i);
	D5(PTR, p, p, p, p, f);
	D5(PTR, p, p, p, p, p);

	fprintf(stderr, "minic: unsupported signature '%s' for '%s'\n", ef->sig, ef->name);
	return minic_val_int(0);
}
