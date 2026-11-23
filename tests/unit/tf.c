/*
 * tf.c -- see tf.h. One file, two back ends: printf on the host, the page
 * six result block (tests/common/test_protocol.h) on the 6502.
 */
#include "tf.h"
#include "test_protocol.h"

static u8 g_failed_now;

static u8 passes(u8 op, s32 actual, s32 expected, s32 tol)
{
    s32 d;

    switch (op) {
    case TF_OP_NEAR:
        d = actual > expected ? actual - expected : expected - actual;
        return (u8)(d <= tol);
    case TF_OP_GE:
        return (u8)(actual >= expected);
    case TF_OP_LE:
        return (u8)(actual <= expected);
    default:
        return (u8)(actual == expected);
    }
}

#ifdef __CC65__

typedef struct {
    u8 test;
    u8 pad;
    u16 line;
    s32 actual;
    s32 expected;
} tp_fail;

typedef struct {
    u16 name;
    u16 frames;
} tp_bench;

typedef struct {
    u8 magic;
    u8 done;
    u8 total;
    u8 failed;
    u8 cur;
    u8 nfails;
    u8 nbench;
    u8 pad7;
    u16 suites;
    u8 pad10[6];
    tp_fail fails[TP_MAX_FAILS];
    tp_bench bench[TP_MAX_BENCH];
} tp_block;

#define TP (*(tp_block *)TP_ADDR)

u8 tf_cmp(u8 op, s32 actual, s32 expected, s32 tol, u16 line)
{
    if (passes(op, actual, expected, tol)) {
        return 1;
    }
    g_failed_now = 1;
    if (TP.nfails < TP_MAX_FAILS) {
        TP.fails[TP.nfails].test = TP.cur;
        TP.fails[TP.nfails].line = line;
        TP.fails[TP.nfails].actual = actual;
        TP.fails[TP.nfails].expected = expected;
        ++TP.nfails;
    }
    return 0;
}

void tf_bench(const char *name, u16 frames)
{
    if (TP.nbench < TP_MAX_BENCH) {
        TP.bench[TP.nbench].name = (u16)name;
        TP.bench[TP.nbench].frames = frames;
        ++TP.nbench;
    }
}

u16 tf_run(const tf_suite *const *suites)
{
    const tf_suite *const *s;
    const tf_case *c;
    u8 *p = (u8 *)TP_ADDR;
    u16 i;

    for (i = 0; i < 256u; ++i) {
        p[i] = 0;
    }
    TP.suites = (u16)suites;
    TP.magic = TP_MAGIC;
    for (s = suites; *s != 0; ++s) {
        for (c = (*s)->cases; c->name != 0; ++c) {
            g_failed_now = 0;
            c->fn();
            if (g_failed_now) {
                ++TP.failed;
            }
            ++TP.total;
            ++TP.cur;
        }
    }
    TP.done = 1;
    return TP.failed;
}

#else /* host */

#include <stdio.h>

static const char *g_suite;
static const char *g_case;

u8 tf_cmp(u8 op, s32 actual, s32 expected, s32 tol, u16 line,
          const char *file, const char *expr)
{
    if (passes(op, actual, expected, tol)) {
        return 1;
    }
    g_failed_now = 1;
    printf("  FAIL %s/%s  %s:%u  %s  (got %ld, want %ld)\n", g_suite, g_case, file,
           (unsigned)line, expr, (long)actual, (long)expected);
    return 0;
}

void tf_bench(const char *name, u16 frames)
{
    (void)name;
    (void)frames;
}

u16 tf_run(const tf_suite *const *suites)
{
    const tf_suite *const *s;
    const tf_case *c;
    unsigned total = 0;
    unsigned failed = 0;

    for (s = suites; *s != 0; ++s) {
        unsigned suite_failed = 0;
        unsigned suite_total = 0;
        g_suite = (*s)->name;
        for (c = (*s)->cases; c->name != 0; ++c) {
            g_case = c->name;
            g_failed_now = 0;
            c->fn();
            ++suite_total;
            if (g_failed_now) {
                ++suite_failed;
            }
        }
        printf("%-14s %3u/%-3u passed\n", (*s)->name, suite_total - suite_failed,
               suite_total);
        total += suite_total;
        failed += suite_failed;
    }
    printf("----------------------------------\n%u/%u tests passed\n", total - failed,
           total);
    return (u16)failed;
}

#endif
