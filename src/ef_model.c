/*
 * ef_model.c -- see ef_model.h.
 *
 * The method in brief. With N training faces x_i (576 pixels each) and
 * their mean m, let c_i = x_i - m. The principal axes ("eigenfaces") are
 * the eigenvectors of the 576 x 576 matrix sum c_i c_i'. That is far too
 * big for a 6502, but it has the same non-zero eigenvalues as the N x N
 * matrix C(i,j) = c_i . c_j, and if v is an eigenvector of C then
 * sum_i v[i] c_i is the matching eigenface (Turk & Pentland's trick).
 * So: build C (55 dot products), find its top K eigenvectors by power
 * iteration, turn each into an eigenface, and describe every face by its
 * K weights w_k = c . e_k / |e_k|. Faces are compared by the Euclidean
 * distance between their weight vectors.
 *
 * Value ranges (why nothing overflows on a 16-bit int / 32-bit long):
 *   centered pixel         |c| <= 63
 *   covariance entry       |C| <= 576 * 63 * 63      = 2,286,144   (s32),
 *                          kept only as C16 = C >> shift, |C16| <= 16383
 *   sample vector          |v| <= 255 (power iteration), a copy scaled
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

static u8  g_stage;         /* EF_ST_*: which step may run next */
static u8  g_n;             /* training faces */
static u8  g_k;             /* eigenfaces to find */
static u8  g_label[EF_MAX_FACES];

/* Raw pixels, then centered pixels once ef_center_step() reaches them. */
static s8  g_face[EF_MAX_FACES][EF_PIXELS];
static u8  g_centered;      /* faces centered so far */
static u8  g_mean[EF_PIXELS];

/* The diagonal of C exactly, and all of C as C >> g_cov_shift. */
static s32 g_diag[EF_MAX_FACES];
static s16 g_cov16[EF_MAX_FACES][EF_MAX_FACES];
static u8  g_cov_shift;
static u8  g_cov_done;      /* entries computed so far */
static u8  g_cov_i;         /* the entry computed last */
static u8  g_cov_j;
static s32 g_trace16;

/* Eigenvectors of C, found one at a time by power iteration. */
static s16 g_vec[EF_MAX_K][EF_MAX_FACES];   /* finished, largest entry +-255 */
static s32 g_vec_norm2[EF_MAX_K];           /* |g_vec[c]|^2 */
static s32 g_lambda16[EF_MAX_K];            /* eigenvalues of C16 */
static u8  g_iters[EF_MAX_K];               /* iterations each one took */
static s16 g_v[EF_MAX_FACES];               /* the one being refined */
static s16 g_vprev[EF_MAX_FACES];           /* ... and its previous value */
static s32 g_w[EF_MAX_FACES];               /* C16 * g_v */
static u8  g_eig_comp;
static u8  g_eig_iter;

/* Eigenfaces, -63..63 with their lengths, built one row per step. */
static s8  g_eigface[EF_MAX_K][EF_PIXELS];
static u16 g_eigface_norm[EF_MAX_K];
static s8  g_vec8[EF_MAX_FACES];            /* g_vec[c] scaled to -63..63 */
static u16 g_ef_mult;                       /* pixel scale, see begin_eigenface() */
static u32 g_ef_sumsq;                      /* running |eigenface|^2 */
static u8  g_ef_clipped;
static u8  g_ef_comp;
static u8  g_ef_row;

/* Each training face as K weights. */
static s16 g_weight[EF_MAX_FACES][EF_MAX_K];
static u8  g_weights_done;

/* The face being recognized. */
static const u8 *g_query;                   /* caller's pixels, 0..63 */
static s8  g_qrow[EF_W];                    /* one centered row of it */
static s16 g_qweight[EF_MAX_K];
static u8  g_query_state;    /* 0 none, 1 set, 2 projected, 3 ranked */
static s32 g_dist2[EF_MAX_FACES];           /* squared distance to each face */
static u8  g_rank[EF_MAX_FACES];            /* face indices, closest first */

static s32 abs32(s32 v)
{
    return v < 0 ? -v : v;
}

/* Round-to-nearest a / b for b > 0 and any sign of a. */
static s32 div_round(s32 a, s32 b)
{
    if (a >= 0) {
        return (a + b / 2) / b;
    }
    return -((-a + b / 2) / b);
}

u8 ef_begin(u8 n, u8 k)
{
    if (n < 2u || n > EF_MAX_FACES || k < 1u || k > EF_MAX_K || k >= n) {
        return EF_ERR;
    }
    memset(g_face, 0, sizeof(g_face));
    memset(g_label, 0, sizeof(g_label));
    memset(g_vec, 0, sizeof(g_vec));
    memset(g_lambda16, 0, sizeof(g_lambda16));
    memset(g_iters, 0, sizeof(g_iters));
    memset(g_eigface, 0, sizeof(g_eigface));
    memset(g_eigface_norm, 0, sizeof(g_eigface_norm));
    memset(g_weight, 0, sizeof(g_weight));
    g_n = n;
    g_k = k;
    g_centered = 0;
    g_cov_done = 0;
    g_eig_comp = 0;
    g_eig_iter = 0;
    g_ef_comp = 0;
    g_ef_row = 0;
    g_weights_done = 0;
    g_ef_clipped = 0;
    g_query_state = 0;
    g_stage = EF_ST_LOADING;
    return EF_DONE;
}

u8 *ef_face_slot(u8 i)
{
    if (g_stage != EF_ST_LOADING || i >= g_n) {
        return 0;
    }
    return (u8 *)g_face[i];
}

void ef_set_label(u8 i, u8 label)
{
    if (i < g_n) {
        g_label[i] = label;
    }
}

u8 ef_stage(void)      { return g_stage; }
u8 ef_face_count(void) { return g_n; }
u8 ef_k(void)          { return g_k; }

u8 ef_label(u8 i)
{
    return i < g_n ? g_label[i] : 0u;
}

/* ---- mean and centring --------------------------------------------- */

/* The per-pixel loops in this section keep their locals at fixed
 * addresses: much faster than cc65's software stack (see ef_kernel.c). */
#ifdef __CC65__
#pragma static-locals (push, on)
#endif

u8 ef_compute_mean(void)
{
    u16 p;
    u8 i;
    u16 sum;
    u8 *px;

    if (g_stage != EF_ST_LOADING) {
        return EF_ERR;
    }
    for (p = 0; p < EF_PIXELS; ++p) {
        sum = 0;
        px = (u8 *)&g_face[0][p];
        for (i = 0; i < g_n; ++i) {
            if (*px >= EF_LEVELS) {
                *px = EF_LEVELS - 1u;
            }
            sum += *px;
            px += EF_PIXELS;
        }
        g_mean[p] = (u8)((sum + g_n / 2u) / g_n);
    }
    g_stage = EF_ST_MEAN;
    return EF_DONE;
}

const u8 *ef_mean(void)
{
    return g_mean;
}

u8 ef_mean_of_first(u8 j, u8 first_row, u8 rows, u8 *out)
{
    u16 p;
    u16 end;
    u8 i;
    u16 sum;
    const u8 *px;

    if (g_stage != EF_ST_LOADING || j < 1u || j > g_n || out == 0 ||
        first_row >= EF_H) {
        return EF_ERR;
    }
    if (rows > EF_H - first_row) {
        rows = (u8)(EF_H - first_row);
    }
    end = (u16)(first_row + rows) * EF_W;
    for (p = (u16)first_row * EF_W; p < end; ++p) {
        sum = 0;
        px = (const u8 *)&g_face[0][p];
        for (i = 0; i < j; ++i) {
            sum += *px;
            px += EF_PIXELS;
        }
        out[p] = (u8)(sum / j);
    }
    return EF_DONE;
}

u8 ef_center_step(void)
{
    u16 p;
    u8 *f;
    const u8 *m;

    if (g_stage != EF_ST_MEAN) {
        return EF_ERR;
    }
    f = (u8 *)g_face[g_centered];
    m = g_mean;
    for (p = 0; p < EF_PIXELS; ++p) {
        *f = (u8)(*f - *m++);    /* pixel - mean, as a signed byte */
        ++f;
    }
    ++g_centered;
    if (g_centered < g_n) {
        return EF_MORE;
    }
    g_stage = EF_ST_CENTERED;
    return EF_DONE;
}

u8 ef_center_done(void) { return g_centered; }

#ifdef __CC65__
#pragma static-locals (pop)
#endif

const s8 *ef_centered(u8 i)
{
    return i < g_n ? g_face[i] : 0;
}

void ef_face_pixels(u8 i, u8 *out)
{
    u16 p;
    const s8 *f;

    if (i >= g_n) {
        return;
    }
    f = g_face[i];
    if (i >= g_centered) {
        memcpy(out, f, EF_PIXELS);
        return;
    }
    for (p = 0; p < EF_PIXELS; ++p) {
        out[p] = (u8)(f[p] + (s8)g_mean[p]);
    }
}

/* ---- covariance ---------------------------------------------------- */

static s16 shift_down(s32 v)
{
    return (s16)(v < 0 ? -(-v >> g_cov_shift) : v >> g_cov_shift);
}

/*
 * The n(n+1)/2 distinct entries of C, in the order they are computed:
 * the diagonal C(0,0)..C(n-1,n-1) first, then the upper triangle row by
 * row. The diagonal goes first because C is positive semi-definite: its
 * largest entry is on the diagonal, which fixes the 16-bit shift before
 * any other entry is stored.
 */
static void entry_of(u8 k, u8 *i, u8 *j)
{
    u8 r = 0;
    u8 row_len;

    if (k < g_n) {
        *i = k;
        *j = k;
        return;
    }
    k = (u8)(k - g_n);
    row_len = (u8)(g_n - 1u);
    while (k >= row_len) {
        k = (u8)(k - row_len);
        --row_len;
        ++r;
    }
    *i = r;
    *j = (u8)(r + 1u + k);
}

/* Position of C(i,j) in that order. */
static u8 order_of(u8 i, u8 j)
{
    u8 k;
    u8 r;

    if (i == j) {
        return i;
    }
    if (i > j) {
        k = i;
        i = j;
        j = k;
    }
    k = g_n;
    for (r = 0; r < i; ++r) {
        k = (u8)(k + g_n - 1u - r);
    }
    return (u8)(k + (j - i - 1u));
}

static void choose_shift(void)
{
    u8 i;
    s32 m = 0;

    for (i = 0; i < g_n; ++i) {
        if (g_diag[i] > m) {
            m = g_diag[i];
        }
    }
    g_cov_shift = 0;
    while (m > 16383L) {
        m >>= 1;
        ++g_cov_shift;
    }
    g_trace16 = 0;
    for (i = 0; i < g_n; ++i) {
        g_cov16[i][i] = shift_down(g_diag[i]);
        g_trace16 += g_cov16[i][i];
    }
}

u8 ef_cov_step(void)
{
    u8 i;
    u8 j;
    s32 c;

    if (g_stage != EF_ST_CENTERED) {
        return EF_ERR;
    }
    entry_of(g_cov_done, &i, &j);
    c = ef_dot_s8(g_face[i], g_face[j], EF_PIXELS);
    if (i == j) {
        g_diag[i] = c;
        if (i == g_n - 1u) {
            choose_shift();
        }
    } else {
        g_cov16[i][j] = shift_down(c);
        g_cov16[j][i] = g_cov16[i][j];
    }
    g_cov_i = i;
    g_cov_j = j;
    ++g_cov_done;
    if (g_cov_done < ef_cov_total()) {
        return EF_MORE;
    }
    g_stage = EF_ST_COV;
    return EF_DONE;
}

u8 ef_cov_done(void)  { return g_cov_done; }
u8 ef_cov_total(void) { return (u8)(g_n * (g_n + 1u) / 2u); }
u8 ef_cov_last_i(void) { return g_cov_i; }
u8 ef_cov_last_j(void) { return g_cov_j; }
u8 ef_cov_shift(void) { return g_cov_shift; }

s32 ef_cov(u8 i, u8 j)
{
    if (i >= g_n || j >= g_n || g_stage < EF_ST_CENTERED ||
        order_of(i, j) >= g_cov_done) {
        return 0;
    }
    if (i == j) {
        return g_diag[i];
    }
    return (s32)g_cov16[i][j] * (1L << g_cov_shift);
}

/* ---- eigenvectors (power iteration in the N-dimensional sample space) -- */

/* v = w scaled so the largest |entry| is EF_VEC_SCALE. Returns 0 if w = 0.
 * w is first shifted down until its largest entry fits 15 bits, so each
 * entry * EF_VEC_SCALE is a (fast) 16 x 16-bit multiply. */
static u8 scale_vector(const s32 *w, s16 *v)
{
    u8 i;
    u8 shift = 0;
    s32 m = 0;
    s32 a;

    for (i = 0; i < g_n; ++i) {
        if (abs32(w[i]) > m) {
            m = abs32(w[i]);
        }
    }
    if (m == 0) {
        memset(v, 0, g_n * sizeof(s16));
        return 0;
    }
    while (m > 32767L) {
        m >>= 1;
        ++shift;
    }
    for (i = 0; i < g_n; ++i) {
        a = abs32(w[i]) >> shift;
        a = div_round(EF_MUL16((s16)a, EF_VEC_SCALE), m);
        v[i] = (s16)(w[i] < 0 ? -a : a);
    }
    return 1;
}

/* Remove the components along every finished eigenvector, then rescale. */
static u8 orthogonalize(s16 *v)
{
    u8 c;
    u8 i;
    s32 num;

    for (c = 0; c < g_eig_comp; ++c) {
        if (g_vec_norm2[c] == 0) {
            continue;
        }
        /* v -= (v.e / e.e) e. |num| <= 10 * 255 * 255, so num * e[i]
         * stays inside 32 bits. */
        num = 0;
        for (i = 0; i < g_n; ++i) {
            num += EF_MUL16(v[i], g_vec[c][i]);
        }
        for (i = 0; i < g_n; ++i) {
            v[i] = (s16)(v[i] - div_round(num * g_vec[c][i], g_vec_norm2[c]));
        }
    }
    for (i = 0; i < g_n; ++i) {
        g_w[i] = v[i];
    }
    return scale_vector(g_w, v);
}

/* w = C16 v */
static void mat_vec(const s16 *v, s32 *w)
{
    u8 i;
    u8 j;
    s32 acc;
    const s16 *row;

    for (i = 0; i < g_n; ++i) {
        acc = 0;
        row = g_cov16[i];
        for (j = 0; j < g_n; ++j) {
            acc += EF_MUL16(row[j], v[j]);
        }
        w[i] = acc;
    }
}

static void seed_vector(void)
{
    u8 i;

    /* Any vector works except multiples of (1,1,...,1): the centered faces
     * sum to zero, so that vector is in the null space of C. */
    for (i = 0; i < g_n; ++i) {
        g_v[i] = (s16)(EF_VEC_SCALE - (s16)i * 47);
    }
    orthogonalize(g_v);
    memset(g_vprev, 0, sizeof(g_vprev));
    g_eig_iter = 0;
}

static void finish_component(u8 nonzero)
{
    u8 c = g_eig_comp;
    u8 i;
    s32 sum_w = 0;
    s32 sum_v = 0;

    memcpy(g_vec[c], g_v, sizeof(g_v));
    g_vec_norm2[c] = 0;
    g_iters[c] = g_eig_iter;
    if (nonzero) {
        mat_vec(g_v, g_w);
        for (i = 0; i < g_n; ++i) {
            g_vec_norm2[c] += EF_MUL16(g_v[i], g_v[i]);
            sum_w += abs32(g_w[i]);
            sum_v += abs32(g_v[i]);
        }
    }
    /* Cv = lambda v, so lambda = sum|Cv| / sum|v|. */
    g_lambda16[c] = sum_v != 0 ? div_round(sum_w, sum_v) : 0;
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
    s16 d;
    s16 dmax = 0;

    if (g_stage == EF_ST_COV) {
        g_stage = EF_ST_EIGEN;
        g_eig_comp = 0;
        seed_vector();
    } else if (g_stage != EF_ST_EIGEN || g_eig_comp >= g_k) {
        return EF_ERR;
    }

    mat_vec(g_v, g_w);
    nonzero = scale_vector(g_w, g_v);
    if (nonzero) {
        nonzero = orthogonalize(g_v);
    }
    ++g_eig_iter;
    for (i = 0; i < g_n; ++i) {
        d = (s16)(g_v[i] - g_vprev[i]);
        if (d < 0) {
            d = (s16)-d;
        }
        if (d > dmax) {
            dmax = d;
        }
    }
    memcpy(g_vprev, g_v, sizeof(g_v));

    if (!nonzero || (g_eig_iter >= EF_MIN_ITERS && dmax <= 1) ||
        g_eig_iter >= EF_MAX_ITERS) {
        finish_component(nonzero);
        if (g_eig_comp >= g_k) {
            g_stage = EF_ST_EIGENFACE;
            g_ef_comp = 0;
            g_ef_row = 0;
            return EF_DONE;
        }
        seed_vector();
    }
    return EF_MORE;
}

u8 ef_eigen_component(void) { return g_eig_comp; }
u8 ef_eigen_iteration(void) { return g_eig_iter; }

const s16 *ef_eigen_current(void)
{
    return g_v;
}

const s16 *ef_eigen_vector(u8 c)
{
    return c < g_k ? g_vec[c] : 0;
}

u8 ef_eigen_iterations_used(u8 c)
{
    return c < g_k ? g_iters[c] : 0u;
}

u16 ef_variance_permille(u8 c)
{
    if (c >= g_k || g_trace16 <= 0 || g_eig_comp <= c) {
        return 0;
    }
    return (u16)div_round(g_lambda16[c] * 1000L, g_trace16);
}

/* ---- eigenfaces ---------------------------------------------------- */

/*
 * Eigenface c is acc = sum_i v_c[i] * face_i. For each pixel that is a
 * strided dot product of v_c (rescaled to -63..63 in g_vec8, as the
 * kernel needs) with the same pixel of every face.
 *
 * The result must fit a signed byte, and its size is known before it is
 * built: |acc|^2 = v'Cv = lambda |v|^2, so the root-mean-square pixel is
 * sqrt(lambda) * |v| / 24. Every pixel is scaled by EF_EF_RMS / rms,
 * held as a 16-bit fixed-point multiplier so each pixel costs a multiply
 * rather than a 32-bit division. That leaves more than five RMS of
 * headroom before a pixel is clipped at +-63 (ef_eigenface_clipped()
 * counts any that are).
 */
static void begin_eigenface(u8 c)
{
    u32 rms;
    u32 norm2 = 0;
    u8 i;

    for (i = 0; i < g_n; ++i) {
        g_vec8[i] = (s8)div_round(EF_MUL16(g_vec[c][i], 63), EF_VEC_SCALE);
        norm2 += (u32)((s16)g_vec8[i] * g_vec8[i]);
    }
    rms = (u32)ef_isqrt32((u32)g_lambda16[c] << g_cov_shift) *
          ef_isqrt32(norm2) / EF_W;
    /* e = |acc| * mult / 2^16 = |acc| * EF_EF_RMS / rms. */
    if (rms > (u32)EF_EF_RMS) {
        g_ef_mult = (u16)(((u32)EF_EF_RMS * 65536UL + rms / 2u) / rms);
    } else {
        g_ef_mult = 65535u;
    }
    g_ef_sumsq = 0;
}

u8 ef_eigenface_step(void)
{
    u8 c = g_ef_comp;
    u8 x;
    u16 off;
    s32 acc;
    u16 mag;
    s16 a;
    s8 *e;

    if (g_stage != EF_ST_EIGENFACE) {
        return EF_ERR;
    }
    if (g_ef_row == 0u) {
        begin_eigenface(c);
    }
    off = (u16)g_ef_row * EF_W;
    e = &g_eigface[c][off];
    for (x = 0; x < EF_W; ++x) {
        acc = ef_dot_strided(g_vec8, &g_face[0][off + x], EF_PIXELS, g_n);
        /* |acc| <= 10 * 63 * 63, so it fits 16 unsigned bits. */
        mag = (u16)(acc < 0 ? -acc : acc);
        a = (s16)((EF_UMUL16(mag, g_ef_mult) + 32768UL) >> 16);
        if (a > 63) {
            a = 63;
            ++g_ef_clipped;
        }
        e[x] = (s8)(acc < 0 ? -a : a);
        g_ef_sumsq += (u16)(a * a);
    }
    ++g_ef_row;
    if (g_ef_row < EF_H) {
        return EF_MORE;
    }
    g_eigface_norm[c] = ef_isqrt32(g_ef_sumsq);
    g_ef_row = 0;
    ++g_ef_comp;
    if (g_ef_comp < g_k) {
        return EF_MORE;
    }
    g_stage = EF_ST_WEIGHTS;
    g_weights_done = 0;
    return EF_DONE;
}

u8 ef_eigenface_clipped(void) { return g_ef_clipped; }

u8 ef_eigenface_component(void) { return g_ef_comp; }
u8 ef_eigenface_row(void)       { return g_ef_row; }

const s8 *ef_eigenface(u8 c)
{
    return c < g_k ? g_eigface[c] : 0;
}

u16 ef_eigenface_norm(u8 c)
{
    return c < g_k ? g_eigface_norm[c] : 0u;
}

/* ---- weights ------------------------------------------------------- */

static void project(const s8 *centered, s16 *out)
{
    u8 c;
    s32 d;

    for (c = 0; c < g_k; ++c) {
        if (g_eigface_norm[c] == 0) {
            out[c] = 0;
            continue;
        }
        d = ef_dot_s8(g_eigface[c], centered, EF_PIXELS);
        out[c] = (s16)div_round(d * EF_WEIGHT_UNIT, (s32)g_eigface_norm[c]);
    }
}

u8 ef_weights_step(void)
{
    if (g_stage != EF_ST_WEIGHTS) {
        return EF_ERR;
    }
    project(g_face[g_weights_done], g_weight[g_weights_done]);
    ++g_weights_done;
    if (g_weights_done < g_n) {
        return EF_MORE;
    }
    g_stage = EF_ST_TRAINED;
    return EF_DONE;
}

u8 ef_weights_done(void) { return g_weights_done; }

s16 ef_weight(u8 i, u8 c)
{
    return (i < g_n && c < g_k) ? g_weight[i][c] : 0;
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
    g_query = pixels;
    g_query_state = 1;
    return EF_DONE;
}

/* The query is centered one row at a time, so it needs no 576-byte copy. */
u8 ef_query_project(void)
{
    s32 d[EF_MAX_K];
    u8 c;
    u8 r;
    u8 x;
    u8 v;
    u16 off;

    if (!trained() || g_query_state < 1u) {
        return EF_ERR;
    }
    memset(d, 0, sizeof(d));
    for (r = 0; r < EF_H; ++r) {
        off = (u16)r * EF_W;
        for (x = 0; x < EF_W; ++x) {
            v = g_query[off + x];
            if (v >= EF_LEVELS) {
                v = EF_LEVELS - 1u;
            }
            g_qrow[x] = (s8)((s8)v - (s8)g_mean[off + x]);
        }
        for (c = 0; c < g_k; ++c) {
            d[c] += ef_dot_s8(&g_eigface[c][off], g_qrow, EF_W);
        }
    }
    for (c = 0; c < g_k; ++c) {
        g_qweight[c] = g_eigface_norm[c] == 0 ? 0 :
            (s16)div_round(d[c] * EF_WEIGHT_UNIT, (s32)g_eigface_norm[c]);
    }
    g_query_state = 2;
    return EF_DONE;
}

s16 ef_query_weight(u8 c)
{
    return c < g_k ? g_qweight[c] : 0;
}

u8 ef_rank(void)
{
    u8 i;
    u8 j;
    u8 c;
    u8 t;
    s32 d;
    s32 sum;

    if (!trained() || g_query_state < 2u) {
        return EF_ERR;
    }
    for (i = 0; i < g_n; ++i) {
        sum = 0;
        for (c = 0; c < g_k; ++c) {
            d = (s32)g_weight[i][c] - g_qweight[c];
            sum += d * d;
        }
        g_dist2[i] = sum;
        g_rank[i] = i;
    }
    /* Insertion sort: stable, so ties keep training order. */
    for (i = 1; i < g_n; ++i) {
        t = g_rank[i];
        j = i;
        while (j > 0 && g_dist2[g_rank[j - 1u]] > g_dist2[t]) {
            g_rank[j] = g_rank[j - 1u];
            --j;
        }
        g_rank[j] = t;
    }
    g_query_state = 3;
    return EF_DONE;
}

u8 ef_rank_index(u8 r)
{
    return (g_query_state == 3u && r < g_n) ? g_rank[r] : 0u;
}

u16 ef_distance(u8 i)
{
    return (g_query_state == 3u && i < g_n) ? ef_isqrt32((u32)g_dist2[i]) : 0u;
}

u8 ef_confidence(void)
{
    u8 r;
    u8 best;
    u16 d1;
    u16 d2;

    if (g_query_state != 3u) {
        return 0;
    }
    best = g_rank[0];
    d1 = ef_distance(best);
    for (r = 1; r < g_n; ++r) {
        if (g_label[g_rank[r]] != g_label[best]) {
            d2 = ef_distance(g_rank[r]);
            if (d2 == 0) {
                return 0;
            }
            return (u8)(((u32)(d2 - d1) * 100u) / d2);
        }
    }
    return 100;
}

u8 ef_reconstruct_rows(const s16 *weights, u8 upto, u8 first_row, u8 rows, u8 *out)
{
    u8 c;
    u8 r;
    u8 x;
    s32 f;
    s32 v;
    s8 fc[EF_MAX_K];
    u16 off;

    if (g_stage < EF_ST_WEIGHTS || weights == 0 || upto > g_k || out == 0 ||
        first_row >= EF_H) {
        return EF_ERR;
    }
    if (rows > EF_H - first_row) {
        rows = (u8)(EF_H - first_row);
    }
    /* Eigenface c adds w/4 * e/|e| gray levels. Work in 1/16 gray levels
     * with |f| <= 63, so each pixel is one small strided dot product. */
    for (c = 0; c < upto; ++c) {
        f = 0;
        if (g_eigface_norm[c] != 0) {
            f = div_round((s32)weights[c] * 4L, (s32)g_eigface_norm[c]);
        }
        if (f > 63) {
            f = 63;
        } else if (f < -63) {
            f = -63;
        }
        fc[c] = (s8)f;
    }
    for (r = first_row; r < first_row + rows; ++r) {
        off = (u16)r * EF_W;
        for (x = 0; x < EF_W; ++x) {
            v = ef_dot_strided(fc, &g_eigface[0][off + x], EF_PIXELS, upto);
            v = v >= 0 ? (v + 8) >> 4 : -((-v + 8) >> 4);
            v += g_mean[off + x];
            if (v < 0) {
                v = 0;
            } else if (v >= (s32)EF_LEVELS) {
                v = EF_LEVELS - 1;
            }
            out[off + x] = (u8)v;
        }
    }
    return EF_DONE;
}

u8 ef_reconstruct(u8 upto, u8 *out)
{
    if (!trained() || g_query_state < 2u) {
        return EF_ERR;
    }
    return ef_reconstruct_rows(g_qweight, upto, 0, EF_H, out);
}
