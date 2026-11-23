/*
 * test_model.c -- ef_model on small synthetic data whose answers are known.
 *
 * The main set has four faces built from two orthogonal patterns:
 *   A = +1 in the top half, -1 in the bottom half
 *   B = +1 on even columns, -1 on odd columns
 *   face0 = 32 + 20A, face1 = 32 - 20A, face2 = 32 + 8B, face3 = 32 - 8B
 * so the mean is exactly 32, the first principal axis is A (variance
 * 2*400 of 2*400 + 2*64, i.e. 862/1000) and the second is B (138/1000).
 */
#include <string.h>

#include "tf.h"
#include "ef_model.h"

static u8 g_px[EF_PIXELS];

static s8 pat_a(u16 p)
{
    return (s8)(p < EF_PIXELS / 2u ? 1 : -1);
}

static s8 pat_b(u16 p)
{
    return (s8)(((p % EF_W) & 1u) ? -1 : 1);
}

static void make_face(u8 which, u8 *out)
{
    u16 p;

    for (p = 0; p < EF_PIXELS; ++p) {
        switch (which) {
        case 0:  out[p] = (u8)(32 + 20 * pat_a(p)); break;
        case 1:  out[p] = (u8)(32 - 20 * pat_a(p)); break;
        case 2:  out[p] = (u8)(32 + 8 * pat_b(p)); break;
        default: out[p] = (u8)(32 - 8 * pat_b(p)); break;
        }
    }
}

static u8 load_patterns(void)
{
    u8 i;

    if (ef_begin(4, 2) != EF_DONE) {
        return 0;
    }
    for (i = 0; i < 4u; ++i) {
        make_face(i, ef_face_slot(i));
        ef_set_label(i, (u8)(10u + i));
    }
    return 1;
}

static void run_to(u8 stage)
{
    if (ef_stage() == EF_ST_LOADING && stage >= EF_ST_MEAN) {
        ef_compute_mean();
    }
    if (stage >= EF_ST_CENTERED) {
        while (ef_center_step() == EF_MORE) {
        }
    }
    if (stage >= EF_ST_COV) {
        while (ef_cov_step() == EF_MORE) {
        }
    }
    if (stage >= EF_ST_EIGENFACE) {
        while (ef_eigen_step() == EF_MORE) {
        }
    }
    if (stage >= EF_ST_WEIGHTS) {
        while (ef_eigenface_step() == EF_MORE) {
        }
    }
    if (stage >= EF_ST_TRAINED) {
        while (ef_weights_step() == EF_MORE) {
        }
    }
}

static s32 abs_s32(s32 v)
{
    return v < 0 ? -v : v;
}

/* ---- set-up ---------------------------------------------------------- */

static void test_begin_validates(void)
{
    TF_EQ(ef_begin(1, 1), EF_ERR);
    TF_EQ(ef_begin(EF_MAX_FACES + 1u, 1), EF_ERR);
    TF_EQ(ef_begin(4, 0), EF_ERR);
    TF_EQ(ef_begin(10, EF_MAX_K + 1u), EF_ERR);
    TF_EQ(ef_begin(3, 3), EF_ERR);
    TF_EQ(ef_begin(10, 4), EF_DONE);
    TF_EQ(ef_stage(), EF_ST_LOADING);
    TF_EQ(ef_face_count(), 10);
    TF_EQ(ef_k(), 4);
}

static void test_slots_and_labels(void)
{
    TF_EQ(ef_begin(3, 1), EF_DONE);
    TF_CHECK(ef_face_slot(0) != 0);
    TF_CHECK(ef_face_slot(2) != ef_face_slot(1));
    TF_CHECK(ef_face_slot(3) == 0);
    ef_set_label(2, 39);
    ef_set_label(3, 5);           /* out of range: ignored */
    TF_EQ(ef_label(2), 39);
    TF_EQ(ef_label(3), 0);
    ef_compute_mean();
    TF_CHECK(ef_face_slot(0) == 0);  /* no loading after training starts */
}

static void test_steps_out_of_order(void)
{
    TF_CHECK(load_patterns());
    TF_EQ(ef_center_step(), EF_ERR);
    TF_EQ(ef_cov_step(), EF_ERR);
    TF_EQ(ef_eigen_step(), EF_ERR);
    TF_EQ(ef_eigenface_step(), EF_ERR);
    TF_EQ(ef_weights_step(), EF_ERR);
    TF_EQ(ef_query_set(g_px), EF_ERR);
    TF_EQ(ef_query_project(), EF_ERR);
    TF_EQ(ef_rank(), EF_ERR);
    TF_EQ(ef_reconstruct(0, g_px), EF_ERR);
    TF_EQ(ef_reconstruct_rows(0, 0, 0, EF_H, g_px), EF_ERR);
    TF_EQ(ef_compute_mean(), EF_DONE);
    TF_EQ(ef_compute_mean(), EF_ERR);
    TF_EQ(ef_stage(), EF_ST_MEAN);
}

/* ---- mean and centring ------------------------------------------------ */

static void test_mean_rounds(void)
{
    TF_EQ(ef_begin(3, 1), EF_DONE);
    memset(ef_face_slot(0), 0, EF_PIXELS);
    memset(ef_face_slot(1), 1, EF_PIXELS);
    memset(ef_face_slot(2), 1, EF_PIXELS);
    ef_face_slot(0)[1] = 1;       /* pixel 1: (1+1+1)/3 = 1 */
    ef_face_slot(2)[2] = 0;       /* pixel 2: (0+1+0)/3 -> 0.33 -> 0 */
    ef_compute_mean();
    TF_EQ(ef_mean()[0], 1);       /* 2/3 rounds up */
    TF_EQ(ef_mean()[1], 1);
    TF_EQ(ef_mean()[2], 0);
}

static void test_mean_clamps_bright_pixels(void)
{
    TF_EQ(ef_begin(2, 1), EF_DONE);
    memset(ef_face_slot(0), 200, EF_PIXELS);
    memset(ef_face_slot(1), 63, EF_PIXELS);
    ef_compute_mean();
    TF_EQ(ef_mean()[5], 63);
    TF_EQ(ef_face_slot(0) == 0, 1);
    ef_face_pixels(0, g_px);
    TF_EQ(g_px[5], 63);
}

static void test_mean_of_first(void)
{
    TF_CHECK(load_patterns());
    TF_EQ(ef_mean_of_first(1, 0, EF_H, g_px), EF_DONE);
    TF_EQ(g_px[0], 52);                  /* face 0 alone */
    TF_EQ(ef_mean_of_first(2, 0, EF_H, g_px), EF_DONE);
    TF_EQ(g_px[0], 32);                  /* faces 0 and 1 cancel */
    TF_EQ(ef_mean_of_first(3, 0, EF_H, g_px), EF_DONE);
    TF_EQ(g_px[0], (52 + 12 + 40) / 3);
    /* Only the asked-for rows; a band running off the bottom is clipped. */
    memset(g_px, 99, sizeof(g_px));
    TF_EQ(ef_mean_of_first(1, 20, 9, g_px), EF_DONE);
    TF_EQ(g_px[20u * EF_W - 1u], 99);
    TF_EQ(g_px[EF_PIXELS - 1u], 12);
    TF_EQ(ef_mean_of_first(1, EF_H, 1, g_px), EF_ERR);
    TF_EQ(ef_mean_of_first(0, 0, EF_H, g_px), EF_ERR);
    TF_EQ(ef_mean_of_first(5, 0, EF_H, g_px), EF_ERR);
    TF_EQ(ef_mean_of_first(1, 0, EF_H, 0), EF_ERR);
    ef_compute_mean();
    TF_EQ(ef_mean_of_first(1, 0, EF_H, g_px), EF_ERR);
}

static void test_center_and_restore(void)
{
    u16 p;

    TF_CHECK(load_patterns());
    ef_face_pixels(0, g_px);            /* before centring: raw copy */
    TF_EQ(g_px[0], 52);
    ef_compute_mean();
    TF_EQ(ef_center_step(), EF_MORE);   /* face 0 centered, the rest not */
    TF_EQ(ef_center_done(), 1);
    TF_EQ(ef_centered(0)[0], 20);
    TF_EQ(ef_centered(1)[0], 12);       /* still the raw pixel */
    ef_face_pixels(0, g_px);
    TF_EQ(g_px[0], 52);                 /* rebuilt as mean + difference */
    ef_face_pixels(1, g_px);
    TF_EQ(g_px[0], 12);                 /* copied as is */
    run_to(EF_ST_CENTERED);
    TF_EQ(ef_stage(), EF_ST_CENTERED);
    for (p = 0; p < EF_PIXELS; p += 97u) {
        TF_EQ(ef_mean()[p], 32);
        TF_EQ(ef_centered(0)[p], 20 * pat_a(p));
        TF_EQ(ef_centered(3)[p], -8 * pat_b(p));
    }
    ef_face_pixels(1, g_px);
    TF_EQ(g_px[0], 12);
    TF_EQ(g_px[EF_PIXELS - 1u], 52);
    TF_CHECK(ef_centered(4) == 0);
    g_px[0] = 99;
    ef_face_pixels(4, g_px);            /* out of range: untouched */
    TF_EQ(g_px[0], 99);
}

/* ---- covariance -------------------------------------------------------- */

static void test_covariance_values(void)
{
    /* Expected order for n = 4: diagonal, then the upper triangle. */
    static const u8 want_i[10] = { 0, 1, 2, 3, 0, 0, 0, 1, 1, 2 };
    static const u8 want_j[10] = { 0, 1, 2, 3, 1, 2, 3, 2, 3, 3 };
    u8 steps = 0;
    u8 more;

    TF_CHECK(load_patterns());
    run_to(EF_ST_CENTERED);
    TF_EQ(ef_cov_total(), 10);
    TF_EQ(ef_cov(0, 0), 0);              /* not computed yet */
    do {
        more = ef_cov_step();
        TF_EQ(ef_cov_last_i(), want_i[steps]);
        TF_EQ(ef_cov_last_j(), want_j[steps]);
        ++steps;
        TF_EQ(ef_cov_done(), steps);
        if (steps == 5u) {
            TF_EQ(ef_cov(1, 0), ef_cov(0, 1));   /* just computed */
            TF_EQ(ef_cov(0, 2), 0);              /* next */
            TF_EQ(ef_cov(3, 2), 0);              /* last */
        }
    } while (more == EF_MORE);
    TF_EQ(steps, 10);
    TF_EQ(ef_stage(), EF_ST_COV);
    TF_EQ(ef_cov(0, 0), 400L * 576);
    TF_EQ(ef_cov(0, 1), -400L * 576);
    TF_EQ(ef_cov(1, 0), ef_cov(0, 1));
    TF_EQ(ef_cov(2, 3), -64L * 576);
    TF_EQ(ef_cov(0, 2), 0);
    TF_EQ(ef_cov(4, 0), 0);
    /* 230400 needs 18 bits; the 16-bit copy keeps the top 14. */
    TF_EQ(ef_cov_shift(), 4);
    TF_EQ(ef_cov_step(), EF_ERR);
}

/* ---- eigenvectors ------------------------------------------------------ */

static void test_eigen_finds_both_axes(void)
{
    const s16 *v;

    TF_CHECK(load_patterns());
    run_to(EF_ST_COV);
    TF_EQ(ef_eigen_step(), EF_MORE);
    TF_EQ(ef_stage(), EF_ST_EIGEN);
    while (ef_eigen_step() == EF_MORE) {
    }
    TF_EQ(ef_stage(), EF_ST_EIGENFACE);
    TF_EQ(ef_eigen_step(), EF_ERR);

    v = ef_eigen_vector(0);
    TF_EQ(abs_s32(v[0]), EF_VEC_SCALE);
    TF_EQ(v[1], -v[0]);
    TF_NEAR(v[2], 0, 1);
    TF_NEAR(v[3], 0, 1);
    v = ef_eigen_vector(1);
    TF_EQ(abs_s32(v[2]), EF_VEC_SCALE);
    TF_EQ(v[3], -v[2]);
    TF_NEAR(v[0], 0, 1);
    TF_CHECK(ef_eigen_vector(2) == 0);

    TF_NEAR(ef_variance_permille(0), 862, 3);
    TF_NEAR(ef_variance_permille(1), 138, 3);
    TF_EQ(ef_variance_permille(2), 0);
    TF_GE(ef_eigen_iterations_used(0), EF_MIN_ITERS);
    TF_LE(ef_eigen_iterations_used(1), EF_MAX_ITERS);
    TF_EQ(ef_eigen_iterations_used(5), 0);
    TF_EQ(ef_eigen_component(), 2);
    TF_CHECK(ef_eigen_current() != 0);
    TF_LE(ef_eigen_iteration(), EF_MAX_ITERS);
}

/* ---- eigenfaces -------------------------------------------------------- */

static void test_eigenfaces_match_patterns(void)
{
    const s8 *e;
    u16 p;
    s8 m;
    u8 steps = 0;

    TF_CHECK(load_patterns());
    run_to(EF_ST_EIGENFACE);
    while (ef_eigenface_step() == EF_MORE) {
        ++steps;
    }
    TF_EQ(steps, 2u * EF_H - 1u);
    TF_EQ(ef_stage(), EF_ST_WEIGHTS);
    TF_EQ(ef_eigenface_step(), EF_ERR);
    TF_EQ(ef_eigenface_component(), 2);
    TF_EQ(ef_eigenface_row(), 0);
    TF_EQ(ef_eigenface_clipped(), 0);

    /* Every pixel has the same size; the signs follow the pattern. */
    e = ef_eigenface(0);
    m = e[0] * pat_a(0);
    TF_EQ(m < 0 ? -m : m, EF_EF_RMS);
    for (p = 0; p < EF_PIXELS; ++p) {
        TF_EQ(e[p] * pat_a(p), m);
    }
    TF_EQ(ef_eigenface_norm(0), (m < 0 ? -m : m) * 24);
    e = ef_eigenface(1);
    m = e[0] * pat_b(0);
    for (p = 0; p < EF_PIXELS; p += 7u) {
        TF_EQ(e[p] * pat_b(p), m);
    }
    TF_CHECK(ef_eigenface(2) == 0);
    TF_EQ(ef_eigenface_norm(2), 0);
}

/* ---- weights and matching ---------------------------------------------- */

static void test_weights(void)
{
    u8 n = 0;

    TF_CHECK(load_patterns());
    run_to(EF_ST_WEIGHTS);
    while (ef_weights_step() == EF_MORE) {
        ++n;
        TF_EQ(ef_weights_done(), n);
    }
    TF_EQ(ef_stage(), EF_ST_TRAINED);
    TF_EQ(ef_weights_step(), EF_ERR);
    /* |20 A| = 20 * 24 = 480 gray levels, in quarter units = 1920. */
    TF_NEAR(abs_s32(ef_weight(0, 0)), 1920, 4);
    TF_EQ(ef_weight(1, 0), -ef_weight(0, 0));
    TF_NEAR(ef_weight(0, 1), 0, 1);
    TF_NEAR(abs_s32(ef_weight(2, 1)), 768, 4);
    TF_NEAR(ef_weight(2, 0), 0, 1);
    TF_EQ(ef_weight(4, 0), 0);
    TF_EQ(ef_weight(0, 2), 0);
}

static void test_match_known_face(void)
{
    TF_CHECK(load_patterns());
    run_to(EF_ST_TRAINED);
    TF_EQ(ef_query_project(), EF_ERR);   /* no query yet */
    make_face(0, g_px);
    TF_EQ(ef_query_set(g_px), EF_DONE);
    TF_EQ(ef_query_set(0), EF_ERR);
    TF_EQ(ef_rank(), EF_ERR);            /* not projected yet */
    TF_EQ(ef_rank_index(0), 0);
    TF_EQ(ef_distance(0), 0);
    TF_EQ(ef_confidence(), 0);
    TF_EQ(ef_query_project(), EF_DONE);
    TF_EQ(ef_query_weight(0), ef_weight(0, 0));
    TF_EQ(ef_query_weight(4), 0);
    TF_EQ(ef_rank(), EF_DONE);

    TF_EQ(ef_rank_index(0), 0);
    TF_EQ(ef_rank_index(1), 2);          /* 2 and 3 tie: training order */
    TF_EQ(ef_rank_index(2), 3);
    TF_EQ(ef_rank_index(3), 1);
    TF_EQ(ef_rank_index(4), 0);
    TF_EQ(ef_distance(0), 0);
    TF_NEAR(ef_distance(1), 3840, 8);
    TF_NEAR(ef_distance(2), 2068, 8);
    TF_EQ(ef_distance(9), 0);
    TF_EQ(ef_confidence(), 100);
}

static void test_query_clamps_pixels(void)
{
    s16 w;

    TF_CHECK(load_patterns());
    run_to(EF_ST_TRAINED);
    /* 250 must act like 63: same weights as an all-63 face. */
    memset(g_px, 63, sizeof(g_px));
    ef_query_set(g_px);
    ef_query_project();
    w = ef_query_weight(1);
    memset(g_px, 250, sizeof(g_px));
    TF_EQ(ef_query_set(g_px), EF_DONE);
    TF_EQ(ef_query_project(), EF_DONE);
    TF_EQ(ef_query_weight(1), w);
}

static void test_reconstruct(void)
{
    u16 p;
    s16 w[EF_MAX_K];

    TF_CHECK(load_patterns());
    run_to(EF_ST_TRAINED);
    make_face(2, g_px);
    ef_query_set(g_px);
    ef_query_project();
    TF_EQ(ef_reconstruct(0, g_px), EF_DONE);
    for (p = 0; p < EF_PIXELS; p += 13u) {
        TF_EQ(g_px[p], 32);
    }
    TF_EQ(ef_reconstruct(2, g_px), EF_DONE);
    for (p = 0; p < EF_PIXELS; ++p) {
        TF_NEAR(g_px[p], 32 + 8 * pat_b(p), 1);
    }
    TF_EQ(ef_reconstruct(3, g_px), EF_ERR);
    TF_EQ(ef_reconstruct(1, 0), EF_ERR);

    /* A huge weight clamps to 0..63 instead of wrapping. */
    w[0] = 30000;
    TF_EQ(ef_reconstruct_rows(w, 1, 0, EF_H, g_px), EF_DONE);
    for (p = 0; p < EF_PIXELS; p += 11u) {
        TF_CHECK(g_px[p] == 0 || g_px[p] == 63);
    }

    /* A band of rows leaves the others alone; bands clip at the bottom. */
    memset(g_px, 99, sizeof(g_px));
    w[0] = 0;
    TF_EQ(ef_reconstruct_rows(w, 1, 20, 10, g_px), EF_DONE);
    TF_EQ(g_px[20u * EF_W - 1u], 99);
    TF_EQ(g_px[20u * EF_W], 32);
    TF_EQ(g_px[EF_PIXELS - 1u], 32);
    TF_EQ(ef_reconstruct_rows(w, 1, EF_H, 1, g_px), EF_ERR);
}

static void test_confidence_same_label(void)
{
    u8 i;

    TF_CHECK(load_patterns());
    for (i = 0; i < 4u; ++i) {
        ef_set_label(i, 7);
    }
    run_to(EF_ST_TRAINED);
    make_face(1, g_px);
    ef_query_set(g_px);
    ef_query_project();
    ef_rank();
    TF_EQ(ef_rank_index(0), 1);
    TF_EQ(ef_confidence(), 100);
}

/* ---- degenerate data ----------------------------------------------------- */

static void test_identical_faces(void)
{
    u8 i;

    TF_EQ(ef_begin(3, 2), EF_DONE);
    for (i = 0; i < 3u; ++i) {
        memset(ef_face_slot(i), 40, EF_PIXELS);
        ef_set_label(i, i);
    }
    run_to(EF_ST_COV);
    TF_EQ(ef_cov(1, 2), 0);
    TF_EQ(ef_cov_shift(), 0);
    TF_EQ(ef_eigen_step(), EF_MORE);     /* component 0: nothing there */
    TF_EQ(ef_eigen_step(), EF_DONE);     /* component 1: nothing either */
    TF_EQ(ef_variance_permille(0), 0);
    TF_EQ(ef_eigen_iterations_used(0), 1);
    run_to(EF_ST_TRAINED);
    TF_EQ(ef_eigenface_norm(0), 0);
    TF_EQ(ef_weight(1, 0), 0);
    memset(g_px, 40, sizeof(g_px));
    ef_query_set(g_px);
    ef_query_project();
    ef_rank();
    TF_EQ(ef_rank_index(0), 0);
    TF_EQ(ef_rank_index(2), 2);
    TF_EQ(ef_confidence(), 0);           /* everything is equally close */
    ef_reconstruct(2, g_px);
    TF_EQ(g_px[100], 40);
}

static void test_more_axes_than_data(void)
{
    u16 p;

    /* Rank one: the second axis has nothing left after Gram-Schmidt. */
    TF_EQ(ef_begin(3, 2), EF_DONE);
    for (p = 0; p < EF_PIXELS; ++p) {
        ef_face_slot(0)[p] = (u8)(32 + 10 * pat_a(p));
        ef_face_slot(1)[p] = (u8)(32 - 10 * pat_a(p));
        ef_face_slot(2)[p] = 32;
    }
    run_to(EF_ST_TRAINED);
    TF_NEAR(ef_variance_permille(0), 1000, 2);
    TF_EQ(ef_variance_permille(1), 0);
    TF_EQ(ef_eigenface_norm(0), EF_EF_RMS * 24);
    TF_EQ(ef_eigenface_norm(1), 0);
}

static const tf_case model_cases[] = {
    TF_CASE(test_begin_validates),
    TF_CASE(test_slots_and_labels),
    TF_CASE(test_steps_out_of_order),
    TF_CASE(test_mean_rounds),
    TF_CASE(test_mean_clamps_bright_pixels),
    TF_CASE(test_mean_of_first),
    TF_CASE(test_center_and_restore),
    TF_CASE(test_covariance_values),
    TF_CASE(test_eigen_finds_both_axes),
    TF_CASE(test_eigenfaces_match_patterns),
    TF_CASE(test_weights),
    TF_CASE(test_match_known_face),
    TF_CASE(test_query_clamps_pixels),
    TF_CASE(test_reconstruct),
    TF_CASE(test_confidence_same_label),
    TF_CASE(test_identical_faces),
    TF_CASE(test_more_axes_than_data),
    TF_END
};

const tf_suite suite_model = { "ef_model", model_cases };
