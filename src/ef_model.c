/*
 * ef_model.c -- see ef_model.h, which introduces eigenfaces from scratch.
 *
 * ======================================================================
 *  HOW THE MATH IS DONE HERE
 * ======================================================================
 *
 * Notation: N training faces (N = 10 in the demo), each a vector of
 * P = 576 pixels. m is the mean face and c_i = x_i - m the centered
 * faces. "a . b" is the dot product: sum of a[p] * b[p] over all p.
 *
 * --- The snapshot trick: a 10 x 10 matrix instead of 576 x 576 ---------
 *
 * Put the centered faces side by side as the columns of a P x N matrix
 * A. The pixel covariance matrix is then A A' (P x P, 576 x 576; A' is
 * A transposed). Its eigenvectors are the eigenfaces, but finding them
 * directly would need a 331,776-entry matrix: far more than 48K.
 *
 * Now look at the small N x N matrix L = A' A instead. Its entries are
 * dot products of whole faces:
 *
 *     L(i,j) = c_i . c_j
 *
 * which is large when faces i and j differ from the mean in the same
 * way. (This code calls L "the covariance matrix" C, as the demo does.)
 * Suppose v is an eigenvector of L, so L v = lambda v. Multiply both
 * sides by A:
 *
 *     A A' (A v) = A (A' A v) = A (L v) = lambda (A v)
 *
 * So A v is an eigenvector of the big matrix A A', with the SAME
 * eigenvalue. And A v is simply
 *
 *     eigenface = v[0] c_0 + v[1] c_1 + ... + v[N-1] c_{N-1}
 *
 * a blend of the centered training faces, with v saying how much of
 * each to take. That is all ef_eigenface_step() does. The big matrix
 * never exists: there are only N - 1 useful eigenfaces anyway (N points
 * span at most N - 1 directions once their mean is removed), and the
 * small matrix finds all of them.
 *
 * The same algebra gives the eigenface's length for free:
 *
 *     |A v|^2 = (A v) . (A v) = v' A' A v = v' L v = lambda |v|^2
 *
 * which tells begin_eigenface() how to scale it before it is built.
 *
 * Strictly, a covariance matrix is divided by N (or N - 1). Scaling a
 * matrix scales its eigenvalues but leaves its eigenvectors alone, and
 * everything here uses eigenvalues only as ratios, so the division is
 * left out.
 *
 * --- Power iteration: finding the largest eigenvector ----------------
 *
 * Start from almost any vector v and keep multiplying it by C:
 *
 *     v,  C v,  C C v,  C C C v,  ...
 *
 * Write the start vector in terms of C's eigenvectors u_1, u_2, ...
 * (eigenvalues lambda_1 > lambda_2 > ...):
 *
 *     v = a_1 u_1 + a_2 u_2 + ...
 *
 * Each multiplication scales every part by its own eigenvalue, so
 * after t steps
 *
 *     C^t v = a_1 lambda_1^t u_1 + a_2 lambda_2^t u_2 + ...
 *
 * The u_1 part grows fastest. Relative to it, the u_2 part shrinks by
 * lambda_2 / lambda_1 every step (13.5 / 52.2 for the first eigenface
 * here, so it all but vanishes within a handful of steps), and v ends up
 * pointing along u_1. The numbers would also grow without limit, so v is
 * rescaled after every step (scale_vector()); only its direction
 * matters. Once a step no longer changes v, it is an eigenvector, and
 * since C v = lambda v, comparing C v with v gives lambda.
 *
 * --- Gram-Schmidt: the next eigenvectors -----------------------------
 *
 * To find u_2, run power iteration again but, after every step, remove
 * the part of v that points along u_1:
 *
 *     v  <-  v - (v . u_1 / u_1 . u_1) u_1
 *
 * (orthogonalize()). With the u_1 part gone, u_2 is now the fastest
 * grower and v converges on it. For u_3, remove both u_1 and u_2; and so
 * on. The removal is repeated every step because rounding keeps
 * bringing a little of the old directions back. This works because the
 * eigenvectors of a symmetric matrix are at right angles to each other.
 *
 * The starting vector must have some of the wanted eigenvector in it.
 * One vector is useless: (1, 1, ..., 1). The centered faces add up to
 * zero, so C times that vector is zero (it is an eigenvector with
 * eigenvalue 0: "move every face toward the mean"). seed_vector() picks
 * a vector that is not a multiple of it.
 *
 * --- Weights and matching ---------------------------------------------
 *
 * The weight of a centered face c along eigenface e is c . e / |e|: the
 * length of c's shadow on the line through e. The stored eigenfaces have
 * an arbitrary scale, so dividing by their length |e| is what makes the
 * weights comparable. Faces are then compared by the Euclidean distance
 * between their weight vectors (ef_rank()), and a face can be rebuilt
 * as mean + sum of weight * e / |e| (ef_reconstruct_rows()).
 *
 * --- Fitting it into 16-bit integers ----------------------------------
 *
 * cc65 has no fast floating point, and 16-bit int arithmetic is several
 * times faster than 32-bit long. Every quantity is kept in a known range
 * so the cheap types never overflow (the host unit tests check this
 * with EF_CHECK_RANGE):
 *
 *   centered pixel         |c| <= 63
 *   covariance entry       |C| <= 576 * 63 * 63      = 2,286,144   (s32),
 *                          kept only as C16 = C >> shift, |C16| <= 16383
 *   small eigenvector      |v| <= 255 (power iteration), a copy scaled
 *                          to |v| <= 63 builds the eigenfaces
 *   C16 * v                <= 10 * 16383 * 255       = 41.8M       (s32)
 *   eigenface accumulator  <= 10 * 63 * 63           = 39,690      (s32)
 *   eigenface              |e| <= 63 (the kernels need -63..63)
 *   weight                 <= |c| * 4 <= 63 * 24 * 4 = 6048        (s16)
 *                          (|c| <= 63 * sqrt(576); 4 = EF_WEIGHT_UNIT)
 *   squared distance       <= 6 * (2 * 6048)^2       = 878M        (s32)
 */
#include <string.h>

#include "ef_model.h"
#include "ef_kernel.h"

/* g_query_stage: how far matching the current query has got. */
#define QUERY_NONE       0u
#define QUERY_SET        1u
#define QUERY_PROJECTED  2u
#define QUERY_RANKED     3u

static u8  g_stage;             /* EF_ST_*: which step may run next */
static u8  g_n_faces;           /* training faces, N */
static u8  g_n_eigenfaces;      /* eigenfaces to find, K */
static u8  g_labels[EF_MAX_FACES];

/* The training faces: raw 0..63 pixels at first, then each one replaced
 * by its centered pixels (face - mean, -63..63) once ef_center_step()
 * reaches it. Keeping one copy saves 5.6K. */
static s8  g_faces[EF_MAX_FACES][EF_PIXELS];
static u8  g_faces_centered;    /* faces centered so far */
static u8  g_mean_face[EF_PIXELS];

/* The covariance matrix C. Its diagonal is kept exactly; all of it is
 * also kept as C >> g_cov_shift, small enough for 16-bit multiplies. */
static s32 g_cov_diagonal[EF_MAX_FACES];
static s16 g_cov_scaled[EF_MAX_FACES][EF_MAX_FACES];
static u8  g_cov_shift;
static u8  g_cov_entries_done;
static u8  g_cov_last_row;      /* the entry computed last */
static u8  g_cov_last_col;
/* Sum of the diagonal of g_cov_scaled (the "trace"). It equals the sum
 * of all the eigenvalues: the total variation between the faces. */
static s32 g_cov_trace_scaled;

/* Eigenvectors of C, found one at a time by power iteration. */
static s16 g_eigvecs[EF_MAX_K][EF_MAX_FACES];   /* largest entry +-255 */
static s32 g_eigvec_len_sq[EF_MAX_K];           /* |g_eigvecs[comp]|^2 */
static s32 g_eigvals_scaled[EF_MAX_K];          /* eigenvalues of C >> shift */
static u8  g_eigvec_iterations[EF_MAX_K];       /* steps each one took */
static s16 g_guess[EF_MAX_FACES];               /* the vector being refined */
static s16 g_prev_guess[EF_MAX_FACES];          /* ... and its last value */
static s32 g_product[EF_MAX_FACES];             /* C times g_guess */
static u8  g_eig_comp;          /* which eigenvector is being found */
static u8  g_eig_iteration;     /* power-iteration steps for it so far */

/* Eigenfaces, -63..63 with their lengths, built one row per step. */
static s8  g_eigfaces[EF_MAX_K][EF_PIXELS];
static u16 g_eigface_len[EF_MAX_K];
/* The eigenvector being turned into an eigenface, rescaled to -63..63
 * so ef_dot_strided() can use it. */
static s8  g_blend[EF_MAX_FACES];
static u16 g_pixel_scale;       /* fixed-point pixel scale, see begin_eigenface() */
static u32 g_eigface_sum_sq;    /* running |eigenface|^2 */
static u8  g_eigface_clipped;
static u8  g_eigface_comp;      /* eigenface being built */
static u8  g_eigface_row;       /* ... and its next row */

/* Each training face as K weights. */
static s16 g_weights[EF_MAX_FACES][EF_MAX_K];
static u8  g_weights_done;

/* The face being recognized. */
static const u8 *g_query_pixels;            /* caller's pixels, 0..63 */
static s8  g_query_row[EF_W];               /* one centered row of it */
static s16 g_query_weights[EF_MAX_K];
static u8  g_query_stage;                   /* QUERY_* */
static s32 g_dist_sq[EF_MAX_FACES];         /* squared distance to each face */
static u8  g_ranking[EF_MAX_FACES];         /* face numbers, closest first */

static s32 abs32(s32 value)
{
    return value < 0 ? -value : value;
}

/* numerator / denominator rounded to the nearest integer, for
 * denominator > 0 and either sign of numerator. */
static s32 div_round(s32 numerator, s32 denominator)
{
    if (numerator >= 0) {
        return (numerator + denominator / 2) / denominator;
    }
    return -((-numerator + denominator / 2) / denominator);
}

u8 ef_begin(u8 n_faces, u8 n_eigenfaces)
{
    if (n_faces < 2u || n_faces > EF_MAX_FACES || n_eigenfaces < 1u ||
        n_eigenfaces > EF_MAX_K || n_eigenfaces >= n_faces) {
        return EF_ERR;
    }
    memset(g_faces, 0, sizeof(g_faces));
    memset(g_labels, 0, sizeof(g_labels));
    memset(g_eigvecs, 0, sizeof(g_eigvecs));
    memset(g_eigvals_scaled, 0, sizeof(g_eigvals_scaled));
    memset(g_eigvec_iterations, 0, sizeof(g_eigvec_iterations));
    memset(g_eigfaces, 0, sizeof(g_eigfaces));
    memset(g_eigface_len, 0, sizeof(g_eigface_len));
    memset(g_weights, 0, sizeof(g_weights));
    g_n_faces = n_faces;
    g_n_eigenfaces = n_eigenfaces;
    g_faces_centered = 0;
    g_cov_entries_done = 0;
    g_eig_comp = 0;
    g_eig_iteration = 0;
    g_eigface_comp = 0;
    g_eigface_row = 0;
    g_weights_done = 0;
    g_eigface_clipped = 0;
    g_query_stage = QUERY_NONE;
    g_stage = EF_ST_LOADING;
    return EF_DONE;
}

u8 *ef_face_slot(u8 face)
{
    if (g_stage != EF_ST_LOADING || face >= g_n_faces) {
        return 0;
    }
    return (u8 *)g_faces[face];
}

void ef_set_label(u8 face, u8 label)
{
    if (face < g_n_faces) {
        g_labels[face] = label;
    }
}

u8 ef_stage(void)      { return g_stage; }
u8 ef_face_count(void) { return g_n_faces; }
u8 ef_k(void)          { return g_n_eigenfaces; }

u8 ef_label(u8 face)
{
    return face < g_n_faces ? g_labels[face] : 0u;
}

/* ---- mean and centering -------------------------------------------- */

/* The per-pixel loops in this section keep their locals at fixed
 * addresses: much faster than cc65's software stack (see ef_kernel.c). */
#ifdef __CC65__
#pragma static-locals (push, on)
#endif

/* mean[p] = (face_0[p] + ... + face_{N-1}[p]) / N, rounded. */
u8 ef_compute_mean(void)
{
    u16 pixel;
    u8 face;
    u16 sum;
    u8 *value;

    if (g_stage != EF_ST_LOADING) {
        return EF_ERR;
    }
    for (pixel = 0; pixel < EF_PIXELS; ++pixel) {
        sum = 0;
        /* The same pixel of every face: EF_PIXELS bytes apart. */
        value = (u8 *)&g_faces[0][pixel];
        for (face = 0; face < g_n_faces; ++face) {
            if (*value >= EF_LEVELS) {
                *value = EF_LEVELS - 1u;
            }
            sum += *value;
            value += EF_PIXELS;
        }
        g_mean_face[pixel] = (u8)((sum + g_n_faces / 2u) / g_n_faces);
    }
    g_stage = EF_ST_MEAN;
    return EF_DONE;
}

const u8 *ef_mean(void)
{
    return g_mean_face;
}

u8 ef_mean_of_first(u8 n_faces, u8 first_row, u8 rows, u8 *out)
{
    u16 pixel;
    u16 end;
    u8 face;
    u16 sum;
    const u8 *value;

    if (g_stage != EF_ST_LOADING || n_faces < 1u || n_faces > g_n_faces || out == 0 ||
        first_row >= EF_H) {
        return EF_ERR;
    }
    if (rows > EF_H - first_row) {
        rows = (u8)(EF_H - first_row);
    }
    end = (u16)(first_row + rows) * EF_W;
    for (pixel = (u16)first_row * EF_W; pixel < end; ++pixel) {
        sum = 0;
        value = (const u8 *)&g_faces[0][pixel];
        for (face = 0; face < n_faces; ++face) {
            sum += *value;
            value += EF_PIXELS;
        }
        out[pixel] = (u8)(sum / n_faces);
    }
    return EF_DONE;
}

/* c_i = x_i - m for the next face i, overwriting x_i. */
u8 ef_center_step(void)
{
    u16 pixel;
    u8 *face;
    const u8 *mean;

    if (g_stage != EF_ST_MEAN) {
        return EF_ERR;
    }
    face = (u8 *)g_faces[g_faces_centered];
    mean = g_mean_face;
    for (pixel = 0; pixel < EF_PIXELS; ++pixel) {
        *face = (u8)(*face - *mean++);  /* pixel - mean, as a signed byte */
        ++face;
    }
    ++g_faces_centered;
    if (g_faces_centered < g_n_faces) {
        return EF_MORE;
    }
    g_stage = EF_ST_CENTERED;
    return EF_DONE;
}

u8 ef_center_done(void) { return g_faces_centered; }

#ifdef __CC65__
#pragma static-locals (pop)
#endif

const s8 *ef_centered(u8 face)
{
    return face < g_n_faces ? g_faces[face] : 0;
}

void ef_face_pixels(u8 face, u8 *out)
{
    u16 pixel;
    const s8 *stored;

    if (face >= g_n_faces) {
        return;
    }
    stored = g_faces[face];
    if (face >= g_faces_centered) {
        memcpy(out, stored, EF_PIXELS);
        return;
    }
    /* Undo the centering: x = c + m. */
    for (pixel = 0; pixel < EF_PIXELS; ++pixel) {
        out[pixel] = (u8)(stored[pixel] + (s8)g_mean_face[pixel]);
    }
}

/* ---- covariance ---------------------------------------------------- */

/* value >> g_cov_shift, rounding toward zero for either sign. */
static s16 shift_down(s32 value)
{
    return (s16)(value < 0 ? -(-value >> g_cov_shift) : value >> g_cov_shift);
}

/*
 * C is symmetric (C(i,j) = c_i . c_j = c_j . c_i), so only its
 * n(n+1)/2 distinct entries are computed: 55 for ten faces. This is the
 * order they are computed in: the diagonal C(0,0)..C(n-1,n-1) first,
 * then the upper triangle row by row. Entry number `step` is at
 * (*row, *col).
 *
 * The diagonal goes first because no entry can be bigger than the
 * largest diagonal one (Cauchy-Schwarz: |c_i . c_j| <= |c_i| |c_j|, and
 * C(i,i) = |c_i|^2). Once the diagonal is known, choose_shift() can fix
 * how far to shift every entry down to fit 16 bits, before any other
 * entry is stored.
 */
static void entry_of(u8 step, u8 *row, u8 *col)
{
    u8 r = 0;
    u8 row_len;

    if (step < g_n_faces) {
        *row = step;
        *col = step;
        return;
    }
    step = (u8)(step - g_n_faces);
    row_len = (u8)(g_n_faces - 1u);
    while (step >= row_len) {
        step = (u8)(step - row_len);
        --row_len;
        ++r;
    }
    *row = r;
    *col = (u8)(r + 1u + step);
}

/* The step number at which C(row,col) is computed (inverse of entry_of). */
static u8 order_of(u8 row, u8 col)
{
    u8 step;
    u8 r;

    if (row == col) {
        return row;
    }
    if (row > col) {
        step = row;
        row = col;
        col = step;
    }
    step = g_n_faces;
    for (r = 0; r < row; ++r) {
        step = (u8)(step + g_n_faces - 1u - r);
    }
    return (u8)(step + (col - row - 1u));
}

/* Pick the smallest shift that brings the largest diagonal entry to at
 * most 16383 (14 bits plus sign, which leaves room for the sums in
 * mat_vec()), then store the shifted diagonal and its sum. */
static void choose_shift(void)
{
    u8 i;
    s32 largest = 0;

    for (i = 0; i < g_n_faces; ++i) {
        if (g_cov_diagonal[i] > largest) {
            largest = g_cov_diagonal[i];
        }
    }
    g_cov_shift = 0;
    while (largest > 16383L) {
        largest >>= 1;
        ++g_cov_shift;
    }
    g_cov_trace_scaled = 0;
    for (i = 0; i < g_n_faces; ++i) {
        g_cov_scaled[i][i] = shift_down(g_cov_diagonal[i]);
        g_cov_trace_scaled += g_cov_scaled[i][i];
    }
}

/* One entry C(i,j) = c_i . c_j: a 576-pixel dot product. */
u8 ef_cov_step(void)
{
    u8 i;
    u8 j;
    s32 dot;

    if (g_stage != EF_ST_CENTERED) {
        return EF_ERR;
    }
    entry_of(g_cov_entries_done, &i, &j);
    dot = ef_dot_s8(g_faces[i], g_faces[j], EF_PIXELS);
    if (i == j) {
        g_cov_diagonal[i] = dot;
        if (i == g_n_faces - 1u) {
            choose_shift();
        }
    } else {
        g_cov_scaled[i][j] = shift_down(dot);
        g_cov_scaled[j][i] = g_cov_scaled[i][j];
    }
    g_cov_last_row = i;
    g_cov_last_col = j;
    ++g_cov_entries_done;
    if (g_cov_entries_done < ef_cov_total()) {
        return EF_MORE;
    }
    g_stage = EF_ST_COV;
    return EF_DONE;
}

u8 ef_cov_done(void)  { return g_cov_entries_done; }
u8 ef_cov_total(void) { return (u8)(g_n_faces * (g_n_faces + 1u) / 2u); }
u8 ef_cov_last_i(void) { return g_cov_last_row; }
u8 ef_cov_last_j(void) { return g_cov_last_col; }
u8 ef_cov_shift(void) { return g_cov_shift; }

s32 ef_cov(u8 row, u8 col)
{
    if (row >= g_n_faces || col >= g_n_faces || g_stage < EF_ST_CENTERED ||
        order_of(row, col) >= g_cov_entries_done) {
        return 0;
    }
    if (row == col) {
        return g_cov_diagonal[row];
    }
    return (s32)g_cov_scaled[row][col] * (1L << g_cov_shift);
}

/* ---- eigenvectors (power iteration on the small N x N matrix) ------- */

/* out = in rescaled so its largest |entry| is EF_VEC_SCALE, keeping its
 * direction. Returns 0 if in is all zeros. `in` is first shifted down
 * until its largest entry fits 15 bits, so each entry * EF_VEC_SCALE is
 * a (fast) 16 x 16-bit multiply. */
static u8 scale_vector(const s32 *in, s16 *out)
{
    u8 i;
    u8 shift = 0;
    s32 largest = 0;
    s32 magnitude;

    for (i = 0; i < g_n_faces; ++i) {
        if (abs32(in[i]) > largest) {
            largest = abs32(in[i]);
        }
    }
    if (largest == 0) {
        memset(out, 0, g_n_faces * sizeof(s16));
        return 0;
    }
    while (largest > 32767L) {
        largest >>= 1;
        ++shift;
    }
    for (i = 0; i < g_n_faces; ++i) {
        magnitude = abs32(in[i]) >> shift;
        magnitude = div_round(EF_MUL16((s16)magnitude, EF_VEC_SCALE), largest);
        out[i] = (s16)(in[i] < 0 ? -magnitude : magnitude);
    }
    return 1;
}

/* Gram-Schmidt: remove from vec its part along every eigenvector found
 * so far, so that power iteration converges on the next one. Then
 * rescale. Returns 0 if nothing is left. */
static u8 orthogonalize(s16 *vec)
{
    u8 comp;
    u8 i;
    s32 dot;

    for (comp = 0; comp < g_eig_comp; ++comp) {
        if (g_eigvec_len_sq[comp] == 0) {
            continue;
        }
        /* vec -= (vec . e / e . e) e. |dot| <= 10 * 255 * 255, so
         * dot * e[i] stays inside 32 bits. */
        dot = 0;
        for (i = 0; i < g_n_faces; ++i) {
            dot += EF_MUL16(vec[i], g_eigvecs[comp][i]);
        }
        for (i = 0; i < g_n_faces; ++i) {
            vec[i] = (s16)(vec[i] - div_round(dot * g_eigvecs[comp][i],
                                              g_eigvec_len_sq[comp]));
        }
    }
    for (i = 0; i < g_n_faces; ++i) {
        g_product[i] = vec[i];
    }
    return scale_vector(g_product, vec);
}

/* out = C vec (with C >> g_cov_shift): out[i] = row i of C . vec. */
static void mat_vec(const s16 *vec, s32 *out)
{
    u8 i;
    u8 j;
    s32 sum;
    const s16 *row;

    for (i = 0; i < g_n_faces; ++i) {
        sum = 0;
        row = g_cov_scaled[i];
        for (j = 0; j < g_n_faces; ++j) {
            sum += EF_MUL16(row[j], vec[j]);
        }
        out[i] = sum;
    }
}

/* Start power iteration for the next eigenvector. */
static void seed_vector(void)
{
    u8 i;

    /* Any vector works except multiples of (1,1,...,1): the centered faces
     * sum to zero, so that vector is in the null space of C (C times it
     * is zero). 255, 208, 161, ... is not. */
    for (i = 0; i < g_n_faces; ++i) {
        g_guess[i] = (s16)(EF_VEC_SCALE - (s16)i * 47);
    }
    orthogonalize(g_guess);
    memset(g_prev_guess, 0, sizeof(g_prev_guess));
    g_eig_iteration = 0;
}

/* Store the converged g_guess as eigenvector g_eig_comp, with its
 * eigenvalue, and move on to the next one. */
static void finish_component(u8 nonzero)
{
    u8 comp = g_eig_comp;
    u8 i;
    s32 sum_product = 0;
    s32 sum_guess = 0;

    memcpy(g_eigvecs[comp], g_guess, sizeof(g_guess));
    g_eigvec_len_sq[comp] = 0;
    g_eigvec_iterations[comp] = g_eig_iteration;
    if (nonzero) {
        mat_vec(g_guess, g_product);
        for (i = 0; i < g_n_faces; ++i) {
            g_eigvec_len_sq[comp] += EF_MUL16(g_guess[i], g_guess[i]);
            sum_product += abs32(g_product[i]);
            sum_guess += abs32(g_guess[i]);
        }
    }
    /* C v = lambda v, entry by entry, so
     * lambda = sum |C v| / sum |v|. Summing every entry averages out
     * rounding. */
    g_eigvals_scaled[comp] = sum_guess != 0 ? div_round(sum_product, sum_guess) : 0;
    ++g_eig_comp;
}

/*
 * One step of power iteration: v <- C v, rescaled, with the directions of
 * the eigenvectors already found projected out (so it converges on the
 * next-largest one). The vector has converged when no entry moved by
 * more than 1 (out of EF_VEC_SCALE) since the previous step.
 */
u8 ef_eigen_step(void)
{
    u8 i;
    u8 nonzero;
    s16 change;
    s16 largest_change = 0;

    if (g_stage == EF_ST_COV) {
        g_stage = EF_ST_EIGEN;
        g_eig_comp = 0;
        seed_vector();
    } else if (g_stage != EF_ST_EIGEN || g_eig_comp >= g_n_eigenfaces) {
        return EF_ERR;
    }

    mat_vec(g_guess, g_product);
    nonzero = scale_vector(g_product, g_guess);
    if (nonzero) {
        nonzero = orthogonalize(g_guess);
    }
    ++g_eig_iteration;
    for (i = 0; i < g_n_faces; ++i) {
        change = (s16)(g_guess[i] - g_prev_guess[i]);
        if (change < 0) {
            change = (s16)-change;
        }
        if (change > largest_change) {
            largest_change = change;
        }
    }
    memcpy(g_prev_guess, g_guess, sizeof(g_guess));

    /* Stop when converged, after the iteration limit, or if the vector
     * has vanished (C has no more variation to find). */
    if (!nonzero || (g_eig_iteration >= EF_MIN_ITERS && largest_change <= 1) ||
        g_eig_iteration >= EF_MAX_ITERS) {
        finish_component(nonzero);
        if (g_eig_comp >= g_n_eigenfaces) {
            g_stage = EF_ST_EIGENFACE;
            g_eigface_comp = 0;
            g_eigface_row = 0;
            return EF_DONE;
        }
        seed_vector();
    }
    return EF_MORE;
}

u8 ef_eigen_component(void) { return g_eig_comp; }
u8 ef_eigen_iteration(void) { return g_eig_iteration; }

const s16 *ef_eigen_current(void)
{
    return g_guess;
}

const s16 *ef_eigen_vector(u8 comp)
{
    return comp < g_n_eigenfaces ? g_eigvecs[comp] : 0;
}

u8 ef_eigen_iterations_used(u8 comp)
{
    return comp < g_n_eigenfaces ? g_eigvec_iterations[comp] : 0u;
}

/* The trace of C (sum of its diagonal) equals the sum of ALL its
 * eigenvalues, i.e. the total variation between the faces, so
 * eigenvalue / trace is the share one axis explains. */
u16 ef_variance_permille(u8 comp)
{
    if (comp >= g_n_eigenfaces || g_cov_trace_scaled <= 0 || g_eig_comp <= comp) {
        return 0;
    }
    return (u16)div_round(g_eigvals_scaled[comp] * 1000L, g_cov_trace_scaled);
}

/* ---- eigenfaces ---------------------------------------------------- */

/*
 * Eigenface comp is sum = sum_i v[i] * c_i, with v its small eigenvector
 * (the snapshot trick at the top of this file). For each pixel that is a
 * strided dot product of v (rescaled to -63..63 in g_blend, as the
 * kernel needs) with the same pixel of every centered face.
 *
 * The result must fit a signed byte, and its size is known before it is
 * built: |sum|^2 = v'Cv = lambda |v|^2, so the root-mean-square (typical)
 * pixel is sqrt(lambda) * |v| / 24 (24 = sqrt(576)). Every pixel is
 * scaled by EF_EF_RMS / rms, held as a 16-bit fixed-point multiplier so
 * each pixel costs a multiply rather than a 32-bit division. That leaves
 * more than five RMS of headroom before a pixel is clipped at +-63
 * (ef_eigenface_clipped() counts any that are). The scale does not
 * matter later: weights divide by the eigenface's length.
 */
static void begin_eigenface(u8 comp)
{
    u32 rms;
    u32 blend_len_sq = 0;
    u8 i;

    for (i = 0; i < g_n_faces; ++i) {
        g_blend[i] = (s8)div_round(EF_MUL16(g_eigvecs[comp][i], 63), EF_VEC_SCALE);
        blend_len_sq += (u32)((s16)g_blend[i] * g_blend[i]);
    }
    /* The eigenvalue of the unshifted C is g_eigvals_scaled << shift. */
    rms = (u32)ef_isqrt32((u32)g_eigvals_scaled[comp] << g_cov_shift) *
          ef_isqrt32(blend_len_sq) / EF_W;
    /* pixel = |sum| * scale / 2^16 = |sum| * EF_EF_RMS / rms. */
    if (rms > (u32)EF_EF_RMS) {
        g_pixel_scale = (u16)(((u32)EF_EF_RMS * 65536UL + rms / 2u) / rms);
    } else {
        g_pixel_scale = 65535u;
    }
    g_eigface_sum_sq = 0;
}

/* Build one row (24 pixels) of the current eigenface. */
u8 ef_eigenface_step(void)
{
    u8 comp = g_eigface_comp;
    u8 x;
    u16 offset;
    s32 sum;
    u16 magnitude;
    s16 pixel;
    s8 *out;

    if (g_stage != EF_ST_EIGENFACE) {
        return EF_ERR;
    }
    if (g_eigface_row == 0u) {
        begin_eigenface(comp);
    }
    offset = (u16)g_eigface_row * EF_W;
    out = &g_eigfaces[comp][offset];
    for (x = 0; x < EF_W; ++x) {
        /* sum_i g_blend[i] * c_i[pixel]: the pixel in every face is
         * EF_PIXELS bytes after the one in the face before. */
        sum = ef_dot_strided(g_blend, &g_faces[0][offset + x], EF_PIXELS, g_n_faces);
        /* |sum| <= 10 * 63 * 63, so it fits 16 unsigned bits. */
        magnitude = (u16)(sum < 0 ? -sum : sum);
        pixel = (s16)((EF_UMUL16(magnitude, g_pixel_scale) + 32768UL) >> 16);
        if (pixel > 63) {
            pixel = 63;
            ++g_eigface_clipped;
        }
        out[x] = (s8)(sum < 0 ? -pixel : pixel);
        g_eigface_sum_sq += (u16)(pixel * pixel);
    }
    ++g_eigface_row;
    if (g_eigface_row < EF_H) {
        return EF_MORE;
    }
    g_eigface_len[comp] = ef_isqrt32(g_eigface_sum_sq);
    g_eigface_row = 0;
    ++g_eigface_comp;
    if (g_eigface_comp < g_n_eigenfaces) {
        return EF_MORE;
    }
    g_stage = EF_ST_WEIGHTS;
    g_weights_done = 0;
    return EF_DONE;
}

u8 ef_eigenface_clipped(void) { return g_eigface_clipped; }

u8 ef_eigenface_component(void) { return g_eigface_comp; }
u8 ef_eigenface_row(void)       { return g_eigface_row; }

const s8 *ef_eigenface(u8 comp)
{
    return comp < g_n_eigenfaces ? g_eigfaces[comp] : 0;
}

u16 ef_eigenface_norm(u8 comp)
{
    return comp < g_n_eigenfaces ? g_eigface_len[comp] : 0u;
}

/* ---- weights ------------------------------------------------------- */

/* weights[comp] = centered . e / |e|, in 1/EF_WEIGHT_UNIT gray levels:
 * how far the face lies along each eigenface. */
static void project(const s8 *centered, s16 *weights)
{
    u8 comp;
    s32 dot;

    for (comp = 0; comp < g_n_eigenfaces; ++comp) {
        if (g_eigface_len[comp] == 0) {
            weights[comp] = 0;
            continue;
        }
        dot = ef_dot_s8(g_eigfaces[comp], centered, EF_PIXELS);
        weights[comp] = (s16)div_round(dot * EF_WEIGHT_UNIT, (s32)g_eigface_len[comp]);
    }
}

u8 ef_weights_step(void)
{
    if (g_stage != EF_ST_WEIGHTS) {
        return EF_ERR;
    }
    project(g_faces[g_weights_done], g_weights[g_weights_done]);
    ++g_weights_done;
    if (g_weights_done < g_n_faces) {
        return EF_MORE;
    }
    g_stage = EF_ST_TRAINED;
    return EF_DONE;
}

u8 ef_weights_done(void) { return g_weights_done; }

s16 ef_weight(u8 face, u8 comp)
{
    return (face < g_n_faces && comp < g_n_eigenfaces) ? g_weights[face][comp] : 0;
}

/* ---- matching ------------------------------------------------------ */

static u8 trained(void)
{
    return g_stage == EF_ST_TRAINED;
}

u8 ef_query_set(const u8 *pixels)
{
    if (!trained() || pixels == 0) {
        return EF_ERR;
    }
    g_query_pixels = pixels;
    g_query_stage = QUERY_SET;
    return EF_DONE;
}

/* The query's weights, as project() works them out for a training face.
 * The query is centered one row at a time, so it needs no 576-byte copy:
 * each eigenface dot product is summed row by row instead. */
u8 ef_query_project(void)
{
    s32 dot[EF_MAX_K];
    u8 comp;
    u8 row;
    u8 x;
    u8 value;
    u16 offset;

    if (!trained() || g_query_stage < QUERY_SET) {
        return EF_ERR;
    }
    memset(dot, 0, sizeof(dot));
    for (row = 0; row < EF_H; ++row) {
        offset = (u16)row * EF_W;
        for (x = 0; x < EF_W; ++x) {
            value = g_query_pixels[offset + x];
            if (value >= EF_LEVELS) {
                value = EF_LEVELS - 1u;
            }
            g_query_row[x] = (s8)((s8)value - (s8)g_mean_face[offset + x]);
        }
        for (comp = 0; comp < g_n_eigenfaces; ++comp) {
            dot[comp] += ef_dot_s8(&g_eigfaces[comp][offset], g_query_row, EF_W);
        }
    }
    for (comp = 0; comp < g_n_eigenfaces; ++comp) {
        g_query_weights[comp] = g_eigface_len[comp] == 0 ? 0 :
            (s16)div_round(dot[comp] * EF_WEIGHT_UNIT, (s32)g_eigface_len[comp]);
    }
    g_query_stage = QUERY_PROJECTED;
    return EF_DONE;
}

s16 ef_query_weight(u8 comp)
{
    return comp < g_n_eigenfaces ? g_query_weights[comp] : 0;
}

/* Squared Euclidean distance from the query to every training face in
 * weight space, then the faces sorted closest first. (Squared distances
 * sort the same way as distances, without square roots.) */
u8 ef_rank(void)
{
    u8 face;
    u8 place;
    u8 comp;
    u8 moving;
    s32 diff;
    s32 sum;

    if (!trained() || g_query_stage < QUERY_PROJECTED) {
        return EF_ERR;
    }
    for (face = 0; face < g_n_faces; ++face) {
        sum = 0;
        for (comp = 0; comp < g_n_eigenfaces; ++comp) {
            diff = (s32)g_weights[face][comp] - g_query_weights[comp];
            sum += diff * diff;
        }
        g_dist_sq[face] = sum;
        g_ranking[face] = face;
    }
    /* Insertion sort: stable, so ties keep training order. */
    for (face = 1; face < g_n_faces; ++face) {
        moving = g_ranking[face];
        place = face;
        while (place > 0 && g_dist_sq[g_ranking[place - 1u]] > g_dist_sq[moving]) {
            g_ranking[place] = g_ranking[place - 1u];
            --place;
        }
        g_ranking[place] = moving;
    }
    g_query_stage = QUERY_RANKED;
    return EF_DONE;
}

u8 ef_rank_index(u8 place)
{
    return (g_query_stage == QUERY_RANKED && place < g_n_faces) ? g_ranking[place] : 0u;
}

u16 ef_distance(u8 face)
{
    return (g_query_stage == QUERY_RANKED && face < g_n_faces) ?
        ef_isqrt32((u32)g_dist_sq[face]) : 0u;
}

/* Compare the best match with the closest face of someone else: if that
 * is barely farther away, the answer was a close call. */
u8 ef_confidence(void)
{
    u8 place;
    u8 best;
    u16 best_dist;
    u16 other_dist;

    if (g_query_stage != QUERY_RANKED) {
        return 0;
    }
    best = g_ranking[0];
    best_dist = ef_distance(best);
    for (place = 1; place < g_n_faces; ++place) {
        if (g_labels[g_ranking[place]] != g_labels[best]) {
            other_dist = ef_distance(g_ranking[place]);
            if (other_dist == 0) {
                return 0;
            }
            return (u8)(((u32)(other_dist - best_dist) * 100u) / other_dist);
        }
    }
    return 100;
}

/* face ~= mean + sum over comp of (weight / EF_WEIGHT_UNIT) * e / |e|. */
u8 ef_reconstruct_rows(const s16 *weights, u8 upto, u8 first_row, u8 rows, u8 *out)
{
    u8 comp;
    u8 row;
    u8 x;
    s32 factor;
    s32 value;
    s8 factors[EF_MAX_K];
    u16 offset;

    if (g_stage < EF_ST_WEIGHTS || weights == 0 || upto > g_n_eigenfaces || out == 0 ||
        first_row >= EF_H) {
        return EF_ERR;
    }
    if (rows > EF_H - first_row) {
        rows = (u8)(EF_H - first_row);
    }
    /* Eigenface comp adds w/4 * e/|e| gray levels. Work in 1/16 gray
     * levels with |factor| <= 63, so each pixel is one small strided dot
     * product: factor = w * 4 / |e|, and factor * e / 16 = w/4 * e/|e|. */
    for (comp = 0; comp < upto; ++comp) {
        factor = 0;
        if (g_eigface_len[comp] != 0) {
            factor = div_round((s32)weights[comp] * 4L, (s32)g_eigface_len[comp]);
        }
        if (factor > 63) {
            factor = 63;
        } else if (factor < -63) {
            factor = -63;
        }
        factors[comp] = (s8)factor;
    }
    for (row = first_row; row < first_row + rows; ++row) {
        offset = (u16)row * EF_W;
        for (x = 0; x < EF_W; ++x) {
            value = ef_dot_strided(factors, &g_eigfaces[0][offset + x], EF_PIXELS, upto);
            /* Divide by 16, rounding to nearest. */
            value = value >= 0 ? (value + 8) >> 4 : -((-value + 8) >> 4);
            value += g_mean_face[offset + x];
            if (value < 0) {
                value = 0;
            } else if (value >= (s32)EF_LEVELS) {
                value = EF_LEVELS - 1;
            }
            out[offset + x] = (u8)value;
        }
    }
    return EF_DONE;
}

u8 ef_reconstruct(u8 upto, u8 *out)
{
    if (!trained() || g_query_stage < QUERY_PROJECTED) {
        return EF_ERR;
    }
    return ef_reconstruct_rows(g_query_weights, upto, 0, EF_H, out);
}
