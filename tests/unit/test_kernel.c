/*
 * test_kernel.c -- ef_dot_s8, ef_dot_strided and ef_isqrt32.
 */
#include "tf.h"
#include "tutil.h"
#include "ef_kernel.h"

static s8 g_a[576];
static s8 g_b[576];

static s32 naive_dot(u16 n)
{
    s32 t = 0;
    u16 i;

    for (i = 0; i < n; ++i) {
        t += (s32)g_a[i] * g_b[i];
    }
    return t;
}

static void test_dot_empty(void)
{
    TF_EQ(ef_dot_s8(g_a, g_b, 0), 0);
}

static void test_dot_small_lengths(void)
{
    u16 n;

    for (n = 0; n < 8u; ++n) {
        g_a[n] = (s8)(n + 1);
        g_b[n] = (s8)(-(s8)n * 3);
    }
    /* 1..7 exercise the tail loop after the blocks of four. */
    for (n = 1; n < 8u; ++n) {
        TF_EQ(ef_dot_s8(g_a, g_b, n), naive_dot(n));
    }
}

static void test_dot_extremes(void)
{
    u16 i;

    for (i = 0; i < 576u; ++i) {
        g_a[i] = 63;
        g_b[i] = 63;
    }
    TF_EQ(ef_dot_s8(g_a, g_b, 576), 576L * 63 * 63);
    for (i = 0; i < 576u; ++i) {
        g_a[i] = (s8)((i & 1u) ? -63 : 63);
        g_b[i] = -63;
    }
    TF_EQ(ef_dot_s8(g_a, g_b, 576), 0);
    TF_EQ(ef_dot_s8(g_a, g_b, 575), -63L * 63);
}

static void test_dot_random_matches_naive(void)
{
    u16 i;
    u8 round;

    t_seed(7);
    for (round = 0; round < 4u; ++round) {
        for (i = 0; i < 576u; ++i) {
            g_a[i] = t_rand_s8(63);
            g_b[i] = t_rand_s8(round < 2u ? 63 : 20);
        }
        TF_EQ(ef_dot_s8(g_a, g_b, 576), naive_dot(576));
    }
}

static void test_dot_every_pair(void)
{
    s16 x;
    s16 y;

    /* Every pair of inputs the table can see. */
    for (x = -63; x <= 63; ++x) {
        g_a[0] = (s8)x;
        for (y = -63; y <= 63; ++y) {
            g_b[0] = (s8)y;
            TF_EQ(ef_dot_s8(g_a, g_b, 1), (s32)x * y);
        }
    }
}

static void test_dot_strided(void)
{
    u16 i;

    /* b holds three "images" of 100 bytes; take pixel 7 of each. */
    for (i = 0; i < 300u; ++i) {
        g_b[i] = 0;
    }
    g_b[7] = 63;
    g_b[107] = -20;
    g_b[207] = 5;
    g_a[0] = -63;
    g_a[1] = 10;
    g_a[2] = 63;
    TF_EQ(ef_dot_strided(g_a, &g_b[7], 100, 3), -63L * 63 + 10 * -20 + 63 * 5);
    TF_EQ(ef_dot_strided(g_a, &g_b[7], 100, 1), -63L * 63);
    TF_EQ(ef_dot_strided(g_a, &g_b[7], 100, 0), 0);
}

static void test_isqrt_small(void)
{
    TF_EQ(ef_isqrt32(0), 0);
    TF_EQ(ef_isqrt32(1), 1);
    TF_EQ(ef_isqrt32(2), 1);
    TF_EQ(ef_isqrt32(3), 1);
    TF_EQ(ef_isqrt32(4), 2);
    TF_EQ(ef_isqrt32(15), 3);
    TF_EQ(ef_isqrt32(16), 4);
    TF_EQ(ef_isqrt32(17), 4);
}

static void test_isqrt_large(void)
{
    TF_EQ(ef_isqrt32(65535UL), 255);
    TF_EQ(ef_isqrt32(65536UL), 256);
    TF_EQ(ef_isqrt32(4294836225UL), 65535U);       /* 65535^2 */
    TF_EQ(ef_isqrt32(4294836224UL), 65534U);
    TF_EQ(ef_isqrt32(0xFFFFFFFFUL), 65535U);
}

static void test_isqrt_bounds_random(void)
{
    u8 i;
    u32 v;
    u32 r;

    t_seed(99);
    for (i = 0; i < 40u; ++i) {
        v = ((u32)t_rand() << 12) ^ t_rand();
        r = ef_isqrt32(v);
        TF_LE(r * r, v);
        TF_CHECK((r + 1u) * (r + 1u) > v);
    }
}

static const tf_case kernel_cases[] = {
    TF_CASE(test_dot_empty),
    TF_CASE(test_dot_small_lengths),
    TF_CASE(test_dot_extremes),
    TF_CASE(test_dot_random_matches_naive),
    TF_CASE(test_dot_every_pair),
    TF_CASE(test_dot_strided),
    TF_CASE(test_isqrt_small),
    TF_CASE(test_isqrt_large),
    TF_CASE(test_isqrt_bounds_random),
    TF_END
};

const tf_suite suite_kernel = { "ef_kernel", kernel_cases };
