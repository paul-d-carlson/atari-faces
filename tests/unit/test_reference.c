/*
 * test_reference.c -- train on data/FACES.DAT and compare every stage
 * with the numpy results in tests/fixtures/ef_reference.h (a float PCA of
 * the same 0..63 pixels, done the way the article does it). The integer
 * version should agree closely: same mean, same variance shares, same
 * eigenfaces up to sign, and the same ranking for every query (apart from
 * near ties).
 *
 * The tests run in order and share one trained model. On the Atari each
 * phase is timed and reported with tf_bench().
 */
#include <string.h>

#include "tf.h"
#include "tutil.h"
#include "disk.h"
#include "ef_data.h"
#include "ef_kernel.h"
#include "ef_model.h"
#include "../fixtures/ef_reference.h"

#define K REF_K
#define N REF_N_TRAIN

static efd_info g_info;
static u8 g_meta[2u * EFD_MAX_TOTAL];
static u8 g_query[REF_N_QUERY][EF_PIXELS];
static u8 g_px[EF_PIXELS];
static u8 g_loaded;

static s32 iabs(s32 v)
{
    return v < 0 ? -v : v;
}

/* |cos| between two signed-byte images, in 1/1000. */
static u16 cos_permille(const s8 *a, const s8 *b)
{
    s32 dot = 0;
    u32 aa = 0;
    u32 bb = 0;
    u16 p;
    u16 na;
    u16 nb;

    for (p = 0; p < EF_PIXELS; ++p) {
        dot += (s16)(a[p] * b[p]);
        aa += (u16)(a[p] * a[p]);
        bb += (u16)(b[p] * b[p]);
    }
    na = ef_isqrt32(aa);
    nb = ef_isqrt32(bb);
    if (na == 0 || nb == 0) {
        return 0;
    }
    return (u16)((iabs(dot) * 100 / na) * 10 / nb);
}

static void test_load(void)
{
    u8 hdr[EFD_HEADER_LEN];
    u8 i;
    int fd = disk_open(DISK_FACES);

    TF_CHECK(fd >= 0);
    TF_EQ(disk_read(fd, hdr, EFD_HEADER_LEN), EFD_HEADER_LEN);
    TF_EQ(efd_parse_header(hdr, &g_info), EFD_OK);
    TF_EQ(g_info.n_train, N);
    TF_EQ(g_info.n_query, REF_N_QUERY);
    TF_EQ(disk_read(fd, g_meta, efd_meta_len(&g_info)), efd_meta_len(&g_info));
    TF_EQ(efd_parse_meta(g_meta, &g_info), EFD_OK);
    TF_EQ(ef_begin(N, K), EF_DONE);
    for (i = 0; i < N; ++i) {
        TF_EQ(disk_read(fd, ef_face_slot(i), EF_PIXELS), EF_PIXELS);
        TF_EQ(efd_check_face(ef_face_slot(i)), EFD_OK);
        TF_EQ(g_info.label[i], ref_train_labels[i]);
        ef_set_label(i, g_info.label[i]);
    }
    for (i = 0; i < REF_N_QUERY; ++i) {
        TF_EQ(disk_read(fd, g_query[i], EF_PIXELS), EF_PIXELS);
        TF_EQ(g_info.label[N + i], ref_query_labels[i]);
    }
    TF_EQ(disk_read(fd, g_px, 1), 0);    /* nothing after the last face */
    disk_close(fd);
    g_loaded = 1;
}

static void test_mean_matches(void)
{
    u16 p;
    u16 t;

    TF_CHECK(g_loaded);
    t = t_ticks();
    TF_EQ(ef_compute_mean(), EF_DONE);
    while (ef_center_step() == EF_MORE) {
    }
    TF_EQ(ef_stage(), EF_ST_CENTERED);
    tf_bench("mean+centre", (u16)(t_ticks() - t));
    for (p = 0; p < EF_PIXELS; ++p) {
        TF_EQ(ef_mean()[p], ref_mean[p]);
    }
}

static void test_covariance(void)
{
    u16 t = t_ticks();
    u8 i;

    while (ef_cov_step() == EF_MORE) {
    }
    tf_bench("covariance", (u16)(t_ticks() - t));
    TF_EQ(ef_stage(), EF_ST_COV);
    for (i = 0; i < N; ++i) {
        TF_CHECK(ef_cov(i, i) > 0);
    }
}

static void test_eigen_variance(void)
{
    u16 t = t_ticks();
    u8 c;

    while (ef_eigen_step() == EF_MORE) {
    }
    tf_bench("eigenvectors", (u16)(t_ticks() - t));
    for (c = 0; c < K; ++c) {
        TF_NEAR(ef_variance_permille(c), ref_variance_permille[c], 2);
        TF_LE(ef_eigen_iterations_used(c), EF_MAX_ITERS);
    }
}

static void test_eigenfaces_match(void)
{
    u16 t = t_ticks();
    u8 c;
    u8 r;
    u16 cs;

    while (ef_eigenface_step() == EF_MORE) {
    }
    tf_bench("eigenfaces", (u16)(t_ticks() - t));
    TF_EQ(ef_eigenface_clipped(), 0);
    /* Every axis matches the float answer (up to sign), and each is
     * nearly perpendicular to the other reference axes. */
    for (c = 0; c < K; ++c) {
        for (r = 0; r < K; ++r) {
            cs = cos_permille(ef_eigenface(c), &ref_eigenfaces[(u16)r * EF_PIXELS]);
            if (r == c) {
                TF_GE(cs, 990);
            } else {
                TF_LE(cs, 80);
            }
        }
    }
}

static void test_weights(void)
{
    u16 t = t_ticks();

    while (ef_weights_step() == EF_MORE) {
    }
    tf_bench("weights", (u16)(t_ticks() - t));
    TF_EQ(ef_stage(), EF_ST_TRAINED);
}

static void test_queries_match_reference(void)
{
    u8 q;
    u8 i;
    u16 t;
    u16 d;
    s32 want;
    s32 here;
    s32 next;

    for (q = 0; q < REF_N_QUERY; ++q) {
        t = t_ticks();
        TF_EQ(ef_query_set(g_query[q]), EF_DONE);
        TF_EQ(ef_query_project(), EF_DONE);
        TF_EQ(ef_rank(), EF_DONE);
        if (q == 0u) {
            tf_bench("one match", (u16)(t_ticks() - t));
        }
        /* Same best match as the float answer, and the right person. */
        TF_EQ(ef_rank_index(0), ref_rankings[q * N]);
        TF_EQ(ef_label(ef_rank_index(0)), ref_query_labels[q]);
        /* Every distance within 3% (plus 3/4 of a grey level, for the
         * shortest ones) of the float answer. */
        for (i = 0; i < N; ++i) {
            d = ef_distance(i);
            want = ref_dist_x4[q * N + i];
            TF_NEAR(d, want, want / 33 + 3);
        }
        /* Our order agrees with the float distances, except between
         * faces less than 1% apart (a near tie may go either way). */
        for (i = 0; i + 1u < N; ++i) {
            here = ref_dist_x4[q * N + ef_rank_index(i)];
            next = ref_dist_x4[q * N + ef_rank_index((u8)(i + 1u))];
            TF_LE(here * 100, next * 101);
        }
        TF_GE(ef_confidence(), 70);
    }
}

static u32 error_to(const u8 *target, const u8 *img)
{
    u32 e = 0;
    u16 p;
    s16 d;

    for (p = 0; p < EF_PIXELS; ++p) {
        d = (s16)((s16)target[p] - img[p]);
        e += (u32)(d * d);
    }
    return e;
}

static void test_reconstruction_improves(void)
{
    u8 u;
    u32 prev;
    u32 err;

    ef_query_set(g_query[0]);
    ef_query_project();
    ef_reconstruct(0, g_px);
    prev = error_to(g_query[0], g_px);
    for (u = 1; u <= K; ++u) {
        ef_reconstruct(u, g_px);
        err = error_to(g_query[0], g_px);
        TF_LE(err, prev);
        prev = err;
    }
}

static const tf_case reference_cases[] = {
    TF_CASE(test_load),
    TF_CASE(test_mean_matches),
    TF_CASE(test_covariance),
    TF_CASE(test_eigen_variance),
    TF_CASE(test_eigenfaces_match),
    TF_CASE(test_weights),
    TF_CASE(test_queries_match_reference),
    TF_CASE(test_reconstruction_improves),
    TF_END
};

const tf_suite suite_reference = { "reference", reference_cases };
