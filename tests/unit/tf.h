/*
 * tf.h -- a very small test framework that runs the same test sources
 * natively (host) and on the 6502 under the emulator (target).
 *
 *   static void test_something(void) { TF_EQ(add(2, 2), 4); }
 *   const tf_case kernel_cases[] = { TF_CASE(test_something), TF_END };
 *   const tf_suite suite_kernel = { "ef_kernel", kernel_cases };
 *
 * A failed check records the line and values and returns from the test.
 * On the host the expression and file are printed as well; the 6502
 * image keeps only the line number to save memory.
 */
#ifndef TF_H
#define TF_H

#include "types.h"

typedef void (*tf_fn)(void);

typedef struct {
    const char *name;
    tf_fn fn;
} tf_case;

typedef struct {
    const char *name;
    const tf_case *cases;
} tf_suite;

#define TF_CASE(fn) { #fn, fn }
#define TF_END      { 0, 0 }

#define TF_OP_EQ    0u
#define TF_OP_NEAR  1u
#define TF_OP_GE    2u
#define TF_OP_LE    3u

/* Record a timing (frames on the Atari, 0 on the host). */
void tf_bench(const char *name, u16 frames);
/* Run every suite in a NULL-terminated list; returns failures. */
u16 tf_run(const tf_suite *const *suites);

/*
 * Every check is one call to tf_cmp(), which compares, records a failure
 * and returns 0 if it failed. On the 6502 that keeps each assertion down
 * to a few bytes of code; the host also gets the file and expression.
 */
#ifdef __CC65__
u8 tf_cmp(u8 op, s32 actual, s32 expected, s32 tol, u16 line);
#define TF_WHERE(expr)
#else
u8 tf_cmp(u8 op, s32 actual, s32 expected, s32 tol, u16 line,
          const char *file, const char *expr);
#define TF_WHERE(expr) , __FILE__, expr
#endif

#define TF_CMP_(op, a, b, tol, text) \
    do { if (!tf_cmp(op, (s32)(a), (s32)(b), (s32)(tol), __LINE__ TF_WHERE(text))) \
        return; } while (0)

#define TF_CHECK(c)        TF_CMP_(TF_OP_EQ, (c) ? 1 : 0, 1, 0, #c)
#define TF_EQ(a, b)        TF_CMP_(TF_OP_EQ, a, b, 0, #a " == " #b)
#define TF_NEAR(a, b, tol) TF_CMP_(TF_OP_NEAR, a, b, tol, #a " ~= " #b)
#define TF_GE(a, b)        TF_CMP_(TF_OP_GE, a, b, 0, #a " >= " #b)
#define TF_LE(a, b)        TF_CMP_(TF_OP_LE, a, b, 0, #a " <= " #b)

#endif /* TF_H */
