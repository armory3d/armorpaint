
#ifndef NDEBUG

#include "minic.h"
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
        *b++; \n\
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

// Test for leading-dot float literals (.3, .5f)
const char *test13 = " \n\
    float main() { \n\
        float a = .3; \n\
        float b = .5f; \n\
        float c = 0.0; \n\
        c = .7; \n\
        if (a == 0.3 && b == 0.5 && c == 0.7) { return 0.0; } \n\
        return 1.0; \n\
    } \n\
";

#define MINIC_TEST(n, src)                                                            \
	do {                                                                              \
		minic_ctx_t *_c = minic_eval(src);                                            \
		minic_ctx_result(_c) == 0.0f ? printf(#n ": PASS\n") : printf(#n ": FAIL\n"); \
		minic_ctx_free(_c);                                                           \
	} while (0)

void minic_tests() {
	MINIC_TEST(0, test0);
	MINIC_TEST(1, test1);
	minic_register("test2_get", "p(i)", (minic_ext_fn_raw_t)test2_get);
	MINIC_TEST(2, test2);
	minic_register("test3_call", "v(p)", (minic_ext_fn_raw_t)test3_call);
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
	MINIC_TEST(13, test13);
}

#endif
