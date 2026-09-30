
#ifndef NDEBUG

#include "minic.h"
#include <stddef.h>
#include <stdio.h>

const char *test0 = " \n\
    float hello(float a, float b) { \n\
        return a * b + 4.0; \n\
    } \n\
    float main() { \n\
        if (hello(2, 3.1) == 10.2) { \n\
            return 0.0; \n\
        } \n\
        return 1.0; \n\
    } \n\
";

const char *test1 = " \n\
    float main() { \n\
        int i = 0; \n\
        int j = 0; \n\
        j += 1; \n\
        j -= 1; \n\
        while (i < 10) { \n\
            i++; \n\
        } \n\
        if (i == 0) { \n\
        } \n\
        else if (i == 1) { \n\
        } \n\
        else { \n\
            for (int i = 0; i < 4; ++i) { \n\
                j++; \n\
            } \n\
        } \n\
        if (j == 4) { \n\
            return 0.0; \n\
        } \n\
        return 1.0; \n\
    } \n\
";

static int  test2_var = 3;
static int *test2_get(int a) {
	test2_var += a;
	return &test2_var;
}
const char *test2 = " \n\
    float main() { \n\
        int *a = test2_get(1); \n\
        *a += 2; \n\
        *a *= 2; \n\
        int b = *a; \n\
        if (b == 12) { \n\
            return 0.0; \n\
        } \n\
        return 1.0; \n\
    } \n\
";

static void test3_call(void *fn) {
	minic_val_t args[1] = {minic_val_int(2)};
	minic_call_fn(fn, args, 1);
}
const char *test3 = " \n\
    void test3_fn(int i) { \n\
        printf(\"3: PASS (1/2)\n\"); \n\
    } \n\
    float main() { \n\
        test3_call(test3_fn); \n\
        return 0.0; \n\
    } \n\
";

const char *test4 = " \n\
    float main() { \n\
        int a = 3; \n\
        int *b = &a; \n\
        a += 2; \n\
        (*b)++; \n\
        int c[4]; \n\
        c[2] = *b; \n\
        if (c[2] == (2 + 1) * 2) { \n\
            return  0.0; \n\
        } \n\
        return 1.0; \n\
    } \n\
";

const char *test5 = " \n\
    typedef struct myvec4 { \n\
        float x; \n\
        float y; \n\
        float z; \n\
        float w; \n\
    } myvec4_t; \n\
    float main() { \n\
        myvec4_t v; \n\
        v.x = 1.0; \n\
        myvec4_t *w = &v; \n\
        w->y = v.x + 1.0; \n\
        if (w->y == 2.0) { \n\
            return 0.0; \n\
        } \n\
        return 1.0; \n\
    } \n\
";

const char *test6 = " \n\
    typedef enum { \n\
        UI_ALIGN_LEFT, \n\
        UI_ALIGN_CENTER, \n\
        UI_ALIGN_RIGHT \n\
    } ui_align_t; \n\
    float main() { \n\
        int a = UI_ALIGN_CENTER; \n\
        if (a == UI_ALIGN_CENTER) { \n\
            return 0.0; \n\
        } \n\
        return 1.0; \n\
    } \n\
";

const char *test7 = " \n\
    float main() { \n\
        int a = 1; \n\
        int b = 2; \n\
        int c = 3; \n\
        if (a == 1 && b == 2) { \n\
            if (c == 2 || c == 3) { \n\
                return 0.0; \n\
            } \n\
        } \n\
        return 1.0; \n\
    } \n\
";

const char *test8 = " \n\
    float main() { \n\
        int a = 1; \n\
        float b = 2.2; \n\
        void *c = &b; \n\
        char d = 'x'; \n\
        bool e = d == 'z'; \n\
        if (!e) { return 0.0; } \n\
        return 1.0; \n\
    } \n\
";

const char *test9 = " \n\
    float main() { \n\
        vec2_t v; \n\
        v.x = 1.5; \n\
        v.y = v.x + 1.5; \n\
        if (v.y == 3.0) { return 0.0; } \n\
        return 1.0; \n\
    } \n\
";

const char *test10 = " \n\
    float main() { \n\
        bool b = true; \n\
        int a = 0; \n\
        if (b) { int a = 3; } \n\
        if (a == 0) { return 0.0; } \n\
        return 1.0; \n\
    } \n\
";

const char *test11 = " \n\
    int g = 3; \n\
    float main() { \n\
        int a = g + 1; \n\
        if (a == 4) { return 0.0; } \n\
        return 1.0; \n\
    } \n\
";

const char *test12 = " \n\
    float main() { \n\
        object_t *o = scene_get_child(\"Scene\"); \n\
        transform_t *t = o->transform; \n\
        vec4_t *v = &t->loc; \n\
        v->x = 3.0; \n\
        v->y = 2.0; \n\
        if (t->loc.x == 3.0 && v->y == 2.0) { return 0.0; } \n\
        return 1.0; \n\
    } \n\
";

typedef struct minic_test_context_s {
	int                          ddirty;
	int                          calls;
	int                         *buffer;
	int                          length;
	float                        weight;
	bool                         active;
	struct minic_test_context_s *child;
} minic_test_context_t;

static minic_test_context_t test_context;

static minic_val_t mw_test_get_context(minic_val_t *args, int argc) {
	(void)args;
	(void)argc;
	test_context.calls++;
	return minic_val_ptr(&test_context);
}

const char *test13 = "float main() {"
                     "  test_get_context()->ddirty = 4;"
                     "  int value = test_get_context()->ddirty;"
                     "  if (value != 4) { return 1; }"
                     "  test_get_context()->ddirty += 2;"
                     "  test_get_context()->ddirty++;"
                     "  test_get_context()->child->ddirty = 9;"
                     "  if (test_get_context()->child->ddirty != 9) { return 2; }"
                     "  test_get_context()->buffer[1] = 7;"
                     "  if (test_get_context()->buffer[1] != 7) { return 3; }"
                     "  minic_test_context_t *ctx = test_get_context();"
                     "  ctx->ddirty += 1;"
                     "  if (ctx->ddirty != 10) { return 4; }"
                     "  test_get_context();"
                     "  if (ctx->calls != 10) { return 5; }"
                     "  return 0;"
                     "}";

const char *test14 = "int calls = 0;"
                     "int tick() { calls += 1; return 1; }"
                     "float main() {"
                     "  if (2 + 3 * 4 != 14) { return 1; }"
                     "  if ((32 >> 1 + 2) != 4) { return 2; }"
                     "  if ((3 | 4 ^ 6 & 2) != 7) { return 3; }"
                     "  if ((1 || 0 && 0) != 1) { return 4; }"
                     "  if ((2 + 3 < 4 * 2) != 1) { return 5; }"
                     "  if ((5.5 % 2.0) != 1.5) { return 6; }"
                     "  if ((20 / 2 / 2) != 5) { return 7; }"
                     "  int a = 0 && tick();"
                     "  int b = 1 || tick();"
                     "  if (calls != 2) { return 8; }"
                     "  return 0;"
                     "}";

const char *test15 = "typedef struct pair { int x; int y; } pair_t;"
                     "float main() {"
                     "  int values[] = {1, 2, 3};"
                     "  int i = 0;"
                     "  values[i++] += 4;"
                     "  ++values[1];"
                     "  int *p = &values[0];"
                     "  (*p) += 2;"
                     "  if (values[0] != 7 || values[1] != 3 || i != 1) { return 1; }"
                     "  int a = 0; int b = 0; a = b = 4;"
                     "  if (a + b != 8) { return 2; }"
                     "  pair_t item; item.x = 5;"
                     "  if (true) { int item = 2; item += 1; if (item != 3) { return 3; } }"
                     "  if (item.x != 5) { return 4; }"
                     "  for (i = 0; i < 3; values[i++] += 1) {}"
                     "  if (values[0] != 8 || values[2] != 4) { return 5; }"
                     "  if (p[1] != values[1]) { return 6; }"
                     "  char *text = \"abc\"; text[1] = 100;"
                     "  if (text[1] != 100) { return 6; }"
                     "  return 0;"
                     "}";

const char *test16 = "minic_test_context_t *context() { return test_get_context(); }"
                     "int change(minic_test_context_t *ctx) { ctx->ddirty = 10; return 2; }"
                     "float main() {"
                     "  minic_test_context_t *ctx = context();"
                     "  int calls = ctx->calls;"
                     "  context()->ddirty = 4;"
                     "  context()->ddirty += change(ctx);"
                     "  if (ctx->ddirty != 12 || ctx->calls != calls + 2) { return 1; }"
                     "  int *dirty = &ctx->ddirty; *dirty += 1;"
                     "  if (context()->ddirty != 13) { return 2; }"
                     "  (*ctx).ddirty = 7;"
                     "  if ((&*ctx)->ddirty != 7) { return 3; }"
                     "  ctx->weight = 1.5; ctx->weight *= 2;"
                     "  ctx->active = true;"
                     "  if (ctx->weight != 3.0 || !ctx->active) { return 4; }"
                     "  return 0;"
                     "}";

const char *test20 = "float main() {"
                     "  int values[] = {10, 20, 30};"
                     "  int *p = &values[0];"
                     "  int old = *p++;"
                     "  if (old != 10 || *p != 20 || values[0] != 10) { return 1; }"
                     "  old = (*p)++;"
                     "  if (old != 20 || *p != 21) { return 2; }"
                     "  old = *++p;"
                     "  if (old != 30 || *p != 30) { return 3; }"
                     "  old = ++*p;"
                     "  if (old != 31 || values[2] != 31) { return 4; }"
                     "  old = *p--;"
                     "  if (old != 31 || *p != 21) { return 5; }"
                     "  old = *--p;"
                     "  if (old != 10 || *p != 10) { return 6; }"
                     "  *p++ = 7;"
                     "  if (values[0] != 7 || *p != 21) { return 7; }"
                     "  char *text = \"abc\";"
                     "  int ch = *text++;"
                     "  if (ch != 97 || *text != 98) { return 8; }"
                     "  return 0;"
                     "}";

static minic_val_t mw_test_ints(minic_val_t *args, int argc) {
	(void)args;
	(void)argc;
	static int values[] = {4, 8, 12};
	return minic_val_typed_ptr(values, MINIC_T_INT);
}

const char *test21 = "float main() {"
                     "  int *p = test_ints();"
                     "  int a = *p++;"
                     "  int b = *++p;"
                     "  int c = *p--;"
                     "  if (a != 4 || b != 12 || c != 12 || *p != 8) { return 1; }"
                     "  if (*--p != 4) { return 2; }"
                     "  return 0;"
                     "}";

typedef struct minic_test_inner_s {
	char   tag;
	int    count;
	double weight;
	bool   active;
} minic_test_inner_t;

typedef struct minic_test_outer_s {
	char                prefix;
	minic_test_inner_t  item;
	float               samples[3];
	minic_test_inner_t *link;
} minic_test_outer_t;

static minic_val_t mw_test_layout(minic_val_t *args, int argc) {
	minic_test_outer_t *value = minic_arg_p(args, argc, 0);
	if (value == NULL || (uintptr_t)value % MINIC_ALIGNOF(minic_test_outer_t) != 0 || value->prefix != 'P' || value->item.tag != 'T' ||
	    value->item.count != 7 || value->item.weight != 16777217.0 || !value->item.active || value->samples[2] != 3.5f || value->link != &value->item) {
		return minic_val_int(0);
	}
	value->item.count = 19;
	value->item.weight += 1.0;
	value->samples[1] = 4.5f;
	return minic_val_int(sizeof(minic_test_outer_t));
}

static minic_val_t mw_test_native_buffers(minic_val_t *args, int argc) {
	int    *ints    = minic_arg_p(args, argc, 0);
	float  *floats  = minic_arg_p(args, argc, 1);
	double *doubles = minic_arg_p(args, argc, 2);
	char   *chars   = minic_arg_p(args, argc, 3);
	bool   *bools   = minic_arg_p(args, argc, 4);
	int   **pointer = minic_arg_p(args, argc, 5);
	if (ints[1] != 2 || floats[1] != 2.5f || doubles[1] != 16777217.0 || chars[1] != 'b' || !bools[1]) {
		return minic_val_int(1);
	}
	ints[1]   = 9;
	floats[1] = 4.5f;
	doubles[1] += 1.0;
	snprintf(chars, 8, "raw");
	bools[0] = true;
	*pointer = &ints[1];
	return minic_val_int(0);
}

const char *test22 = "typedef struct native_inner { char tag; int count; double weight; bool active; } native_inner_t;"
                     "typedef struct native_outer { char prefix; native_inner_t item; float samples[3]; native_inner_t *link; } native_outer_t;"
                     "int copy_count(native_inner_t item) { item.count = 99; return item.count; }"
                     "float main() {"
                     "  native_outer_t value;"
                     "  value.prefix = 'P'; value.item.tag = 'T'; value.item.count = 7;"
                     "  value.item.weight = 16777217; value.item.active = true;"
                     "  value.samples[2] = 3.5; value.link = &value.item;"
                     "  if (test_layout(&value) != sizeof(native_outer_t)) { return 1; }"
                     "  if (value.item.count != 19 || value.item.weight != 16777218 || value.samples[1] != 4.5) { return 2; }"
                     "  native_inner_t copy = value.item; copy.count = 3;"
                     "  if (value.item.count != 19 || copy_count(copy) != 99 || copy.count != 3) { return 3; }"
                     "  native_outer_t array[2]; array[0] = value; array[1] = value;"
                     "  native_outer_t *p = &array[0]; p++; p->item.count = 42;"
                     "  if (array[1].item.count != 42 || array[0].item.count != 19) { return 4; }"
                     "  return 0;"
                     "}";

const char *test23 = "float main() {"
                     "  int ints[] = {1, 2, 3}; float floats[] = {1.5, 2.5};"
                     "  double doubles[] = {1, 16777217}; char chars[8]; bool bools[] = {false, true};"
                     "  chars[1] = 'b'; int *p = &ints[0];"
                     "  if (test_native_buffers(ints, floats, doubles, chars, bools, &p) != 0) { return 1; }"
                     "  if (*p != 9 || floats[1] != 4.5 || doubles[1] != 16777218 || chars[2] != 'w' || !bools[0]) { return 2; }"
                     "  int **pp = &p; **pp = 12;"
                     "  if (ints[1] != 12) { return 3; }"
                     "  if (sizeof(chars) != 8 || sizeof(bools) != 2 || sizeof(doubles) != 2 * sizeof(double)) { return 4; }"
                     "  return 0;"
                     "}";

const char *test24 = "float main() {"
                     "  vec4_t v; v.x = 1.5; v.y = 2; v.z = 3; v.w = 1;"
                     "  vec4_t scaled = vec4_mult(v, 2);"
                     "  if (scaled.x != 3 || scaled.z != 6 || v.x != 1.5) { return 1; }"
                     "  mat4_t m = mat4_identity(); mat4_t n = mat4_mult_mat(m, m);"
                     "  if (n.m00 != 1 || n.m33 != 1) { return 2; }"
                     "  mat3_t small = mat3_identity(); if (small.m22 != 1) { return 3; }"
                     "  char buf[16]; int len = sprintf(buf, \"%s %d\", \"raw\", 12);"
                     "  if (len != 6 || buf[0] != 'r' || buf[4] != '1' || buf[6] != 0) { return 4; }"
                     "  return 0;"
                     "}";

static void minic_test_register_context(void) {
	static int buffer[2];
	test_context        = (minic_test_context_t){0};
	test_context.buffer = buffer;
	test_context.length = 2;
	test_context.child  = &test_context;
	minic_struct_begin("minic_test_context_t", sizeof(minic_test_context_t), MINIC_ALIGNOF(minic_test_context_t));
	minic_struct_field("ddirty", offsetof(minic_test_context_t, ddirty), MINIC_T_INT, MINIC_T_INT, NULL);
	minic_struct_field("calls", offsetof(minic_test_context_t, calls), MINIC_T_INT, MINIC_T_INT, NULL);
	minic_struct_field("buffer", offsetof(minic_test_context_t, buffer), MINIC_T_PTR, MINIC_T_INT, NULL);
	minic_struct_field("length", offsetof(minic_test_context_t, length), MINIC_T_INT, MINIC_T_INT, NULL);
	minic_struct_field("weight", offsetof(minic_test_context_t, weight), MINIC_T_FLOAT, MINIC_T_FLOAT, NULL);
	minic_struct_field("active", offsetof(minic_test_context_t, active), MINIC_T_BOOL, MINIC_T_BOOL, NULL);
	minic_struct_field("child", offsetof(minic_test_context_t, child), MINIC_T_PTR, MINIC_T_PTR, "minic_test_context_t");
	minic_register("test_get_context", "p:minic_test_context_t()", mw_test_get_context);
}

#define MINIC_TEST_EXPECT(n, src, expected)                       \
	do {                                                          \
		minic_ctx_t *_c     = minic_eval(src);                    \
		bool         passed = minic_ctx_result(_c) == (expected); \
		printf(#n ": %s\n", passed ? "PASS" : "FAIL");            \
		minic_ctx_free(_c);                                       \
	} while (0)

#define MINIC_TEST(n, src) MINIC_TEST_EXPECT(n, src, 0.0f)

static minic_val_t mw_test2_get(minic_val_t *a, int n) {
	return minic_val_ptr(test2_get(minic_arg_i(a, n, 0)));
}

static minic_val_t mw_test3_call(minic_val_t *a, int n) {
	test3_call(minic_arg_p(a, n, 0));
	return minic_val_int(0);
}

void minic_tests() {
	MINIC_TEST(0, test0);
	MINIC_TEST(1, test1);
	minic_register("test2_get", "p(i)", mw_test2_get);
	MINIC_TEST(2, test2);
	minic_register("test3_call", "v(p)", mw_test3_call);
	{
		minic_ctx_t *_c = minic_eval(test3);
		minic_ctx_result(_c) == 0.0f ? printf("3: PASS (2/2)\n") : printf("3: FAIL\n");
		minic_ctx_free(_c);
	}
	MINIC_TEST(4, test4);
	MINIC_TEST(5, test5);
	MINIC_TEST(6, test6);
	MINIC_TEST(7, test7);
	MINIC_TEST(8, test8);
	MINIC_TEST(9, test9);
	MINIC_TEST(10, test10);
	MINIC_TEST(11, test11);
	MINIC_TEST(12, test12);
	minic_test_register_context();
	MINIC_TEST(13, test13);
	MINIC_TEST(14, test14);
	MINIC_TEST(15, test15);
	MINIC_TEST(16, test16);
	MINIC_TEST(20, test20);
	minic_register("test_ints", "p:int()", mw_test_ints);
	MINIC_TEST(21, test21);
	minic_register("test_layout", "i(p)", mw_test_layout);
	minic_register("test_native_buffers", "i(p,p,p,p,p,p)", mw_test_native_buffers);
	MINIC_TEST(22, test22);
	MINIC_TEST(23, test23);
	MINIC_TEST(24, test24);
	MINIC_TEST_EXPECT(25, "float main() { float a[] = {.25, 1, .5f}; return a[0] + a[2] - .75; }", 0.0f);
	MINIC_TEST_EXPECT(26, "float main() { float a[] = {1e3, 2.5E-2, .5e+1f, 3e0}; return a[0] + a[1] * 40 + a[2] + a[3] - 1009; }", 0.0f);
	MINIC_TEST_EXPECT(27,
	                  "float main() {"
	                  "  if ((1 || 1 && 0) != 1) { return 1; }"
	                  "  if ((3 == 3 < 4) != 0) { return 2; }"
	                  "  if ((3 > 2 > 1) != 0) { return 3; }"
	                  "  if ((1 | 2 ^ 3 & 1) != 3) { return 4; }"
	                  "  if ((1 != 2 == 1) != 1) { return 5; }"
	                  "  if ((1 << 2 < 5) != 1) { return 6; }"
	                  "  return 0;"
	                  "}",
	                  0.0f);
	MINIC_TEST_EXPECT(28,
	                  "float main() {"
	                  "  int n = 0; int c = 5;"
	                  "  float x = (c & 1) ? 1.5 : 2.5; if (x != 1.5) { return 1; }"
	                  "  if ((c & 2 ? 1 : 0) != 0) { return 2; }"
	                  "  int y = c > 3 ? c > 4 ? 10 : 20 : 30; if (y != 10) { return 3; }"
	                  "  y = c < 3 ? 10 : c < 5 ? 20 : 30; if (y != 30) { return 4; }"
	                  "  c ? n++ : n--; 0 ? (n += 100) : n; if (n != 1) { return 5; }"
	                  "  int a[] = {7, c ? 8 : 9}; if (a[1] != 8 || a[c > 9 ? 0 : 1] != 8) { return 6; }"
	                  "  y = 1 + (c ? 2 : 3) * 2; if (y != 5) { return 7; }"
	                  "  return 0;"
	                  "}",
	                  0.0f);
	MINIC_TEST_EXPECT(17, "float main() { int a[2]; return a[2]; }", -1.0f);
	MINIC_TEST_EXPECT(18, "float main() { test_get_context()->buffer[2] = 99; return 0; }", -1.0f);
	MINIC_TEST_EXPECT(19, "float main() { minic_test_context_t *p = NULL; return p->ddirty; }", -1.0f);
}

#endif
