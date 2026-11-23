/*
 * ef_model.h -- eigenfaces training and matching on small integer faces.
 *
 * ======================================================================
 *  EIGENFACES IN A NUTSHELL (read this first if the subject is new)
 * ======================================================================
 *
 * 1. A face is a list of numbers.
 *
 *    Each face here is a 24 x 24 photo with 64 shades of gray: 576
 *    pixels, each 0 (black) .. 63 (white). Read row by row, it is a list
 *    (a "vector") of 576 numbers. Think of it as a single point in a
 *    576-dimensional space: one axis per pixel. Two photos that look
 *    alike are two points close together.
 *
 * 2. The mean face, and what makes each face different.
 *
 *    Averaging the training faces pixel by pixel gives the mean face m:
 *    what all of them have in common (two eyes, a nose, a mouth, roughly
 *    in the same places). Subtracting it from face i leaves the
 *    "centered" face c_i = x_i - m: only what is special about that face.
 *    Centered pixels are -63..63; zero means "same as the average".
 *
 * 3. Covariance: which pixels change together.
 *
 *    Pixels do not vary independently. If one face has a wider nose
 *    than average, the pixels around its nose are all brighter or all
 *    darker together. The covariance matrix records this: entry (p, q)
 *    is large and positive when pixels p and q tend to be above (or
 *    below) average at the same time. With 576 pixels it is a 576 x 576
 *    table.
 *
 *    Seen as points in space, the centered faces form a cloud around
 *    zero. The cloud is not round: it is stretched out in a few
 *    directions (the ways faces really differ: lighting from the left,
 *    a beard, glasses) and thin in all the others. Principal component
 *    analysis (PCA) finds those directions, longest first.
 *
 * 4. Eigenvectors and eigenvalues.
 *
 *    A square matrix M turns a vector v into another vector M v. Usually
 *    that changes v's direction. An EIGENVECTOR of M is a special
 *    direction that M only stretches (or shrinks), without turning:
 *
 *        M v = lambda v
 *
 *    The stretch factor lambda is the matching EIGENVALUE. ("Eigen" is
 *    German for "own": the matrix's own directions.) A 2 x 2 example:
 *
 *        M = | 2 1 |     M (1, 1)  = (3, 3)  = 3 * (1, 1)
 *            | 1 2 |     M (1, -1) = (1, -1) = 1 * (1, -1)
 *
 *    so (1, 1) is an eigenvector with eigenvalue 3, and (1, -1) is one
 *    with eigenvalue 1. Any other vector, say (1, 0), comes out turned:
 *    M (1, 0) = (2, 1).
 *
 *    A covariance matrix is symmetric (entry (p, q) equals (q, p)), and
 *    for such matrices the eigenvectors are at right angles to each
 *    other and the eigenvalues are never negative. Better still, they
 *    are exactly the axes of the data cloud from point 3: the
 *    eigenvector with the largest eigenvalue points along the cloud's
 *    longest direction, and its eigenvalue is the variance (spread) of
 *    the faces along it. The next eigenvector is the longest direction
 *    at right angles to the first, and so on.
 *
 * 5. Eigenfaces.
 *
 *    Each eigenvector has 576 entries, one per pixel, so it can be drawn
 *    as a picture: an EIGENFACE. Gray means "this pixel does not take
 *    part"; bright and dark areas show pixels that move in opposite
 *    directions along that axis. The first few eigenfaces capture most
 *    of the variation between faces; the demo keeps four
 *    (EF_MAX_K / DEMO_K), which here cover about 86% of it.
 *
 * 6. Every face as a few numbers ("weights").
 *
 *    The dot product of a centered face with a unit-length eigenface
 *    (multiply pixel by pixel, add everything up) says how far the face
 *    lies along that axis. These numbers are the face's WEIGHTS. With
 *    four eigenfaces e_1..e_4, any face is approximately
 *
 *        face  ~=  mean  +  w_1 e_1  +  w_2 e_2  +  w_3 e_3  +  w_4 e_4
 *
 *    so 576 pixels shrink to 4 numbers that keep what matters most.
 *
 * 7. Recognition.
 *
 *    To recognize a new photo, subtract the mean, work out its four
 *    weights the same way, and find the training face whose weights are
 *    closest: the smallest Euclidean distance
 *    sqrt((w_1 - w'_1)^2 + ... + (w_4 - w'_4)^2). That training face's
 *    person is the answer.
 *
 * The one hard part is step 4: finding eigenvectors of a 576 x 576
 * matrix is far too much for a 6502 with 48K. ef_model.c explains the
 * two tricks that make it small: the N x N "snapshot" matrix (Turk and
 * Pentland) and power iteration.
 *
 * This follows the method of the article the demo is based on (Hassan
 * Id Mansour, "Face Recognition using Eigenfaces (Python)") and of
 * M. Turk and A. Pentland, "Eigenfaces for Recognition", 1991.
 *
 * ======================================================================
 *  HOW THIS MODULE IS USED
 * ======================================================================
 *
 * Everything is integer math sized for a 6502: pixels are 0..63,
 * centered pixels -63..63, eigenfaces are stored as -63..63 with their
 * length kept on the side, and weights are in 1/4 gray-level units.
 *
 * Training is split into small steps so the caller can redraw the screen
 * between them:
 *
 *   ef_begin(n, k)            then fill ef_face_slot(i) / ef_set_label()
 *   ef_compute_mean()                                  point 2
 *   while (ef_center_step() == EF_MORE) ...            point 2
 *   while (ef_cov_step() == EF_MORE) ...               point 3
 *   while (ef_eigen_step() == EF_MORE) ...             point 4
 *   while (ef_eigenface_step() == EF_MORE) ...         point 5
 *   while (ef_weights_step() == EF_MORE) ...           point 6
 *   ef_query_set(); ef_query_project(); ef_rank();     point 7
 *
 * Calling a step out of order returns EF_ERR and changes nothing.
 * This module does no I/O and has no platform dependencies.
 */
#ifndef EF_MODEL_H
#define EF_MODEL_H

#include "types.h"

/* Face size: 24 x 24 pixels, each 0..EF_LEVELS-1. */
#define EF_W            24u
#define EF_H            24u
#define EF_PIXELS       576u
#define EF_LEVELS       64u
/* At most this many training faces and eigenfaces. */
#define EF_MAX_FACES    10u
#define EF_MAX_K        4u

/* Eigenvectors of the small matrix are scaled so their largest entry is
 * +-EF_VEC_SCALE (only their direction matters). */
#define EF_VEC_SCALE    255
/* Weights and distances are in units of 1/EF_WEIGHT_UNIT gray level. */
#define EF_WEIGHT_UNIT  4
/* Eigenfaces are stored with this root-mean-square pixel value. */
#define EF_EF_RMS       12u
/* Power iteration runs at least EF_MIN_ITERS and at most EF_MAX_ITERS
 * times for each eigenvector. */
#define EF_MIN_ITERS    3u
#define EF_MAX_ITERS    40u

/* Step results. */
#define EF_DONE         0u
#define EF_MORE         1u
#define EF_ERR          0xFFu

/* Training stages, in order. */
#define EF_ST_EMPTY     0u
#define EF_ST_LOADING   1u
#define EF_ST_MEAN      2u
#define EF_ST_CENTERED  3u
#define EF_ST_COV       4u
#define EF_ST_EIGEN     5u
#define EF_ST_EIGENFACE 6u
#define EF_ST_WEIGHTS   7u
#define EF_ST_TRAINED   8u

/* ---- set-up -------------------------------------------------------- */

/* Start a new model with n_faces training faces and n_eigenfaces
 * eigenfaces. Needs 2 <= n_faces <= EF_MAX_FACES and
 * 1 <= n_eigenfaces <= EF_MAX_K, n_eigenfaces < n_faces (N faces have at
 * most N - 1 directions of variation once the mean is removed).
 * Returns EF_DONE or EF_ERR. */
u8 ef_begin(u8 n_faces, u8 n_eigenfaces);
/* Where to put the 576 pixels (0..63) of training face `face`, or 0. */
u8 *ef_face_slot(u8 face);
/* Who the face belongs to (any number; the demo uses the subject). */
void ef_set_label(u8 face, u8 label);

u8 ef_stage(void);
u8 ef_face_count(void);
/* Number of eigenfaces ("K"). */
u8 ef_k(void);
u8 ef_label(u8 face);

/* ---- training ------------------------------------------------------ */

/* Mean face (rounded). Pixel values above 63 are clamped first. */
u8 ef_compute_mean(void);
const u8 *ef_mean(void);
/* Average of the first n_faces training faces, for showing the mean
 * build up; like ef_reconstruct_rows(), only rows first_row ..
 * first_row + rows - 1 of out are filled. Only before ef_compute_mean()
 * (faces still 0..63). */
u8 ef_mean_of_first(u8 n_faces, u8 first_row, u8 rows, u8 *out);

/* Subtract the mean from the next face, in place; one face per call. */
u8 ef_center_step(void);
u8 ef_center_done(void);
/* Face `face` as stored: raw 0..63 pixels until ef_center_step() has
 * reached it, centered -63..63 pixels afterwards. */
const s8 *ef_centered(u8 face);
/* Rebuild the original 0..63 pixels of a training face. */
void ef_face_pixels(u8 face, u8 *out);

/* The N x N matrix C(i,j) = (face_i - mean) . (face_j - mean): how much
 * two faces differ from the mean in the same way (see ef_model.c for why
 * this small matrix stands in for the 576 x 576 pixel covariance). One
 * entry per call: the diagonal first, then the rest row by row. */
u8 ef_cov_step(void);
/* Entries computed so far, out of n(n+1)/2 (C is symmetric, so only one
 * triangle is computed). */
u8 ef_cov_done(void);
u8 ef_cov_total(void);
/* Row and column of the entry the last step computed. */
u8 ef_cov_last_i(void);
u8 ef_cov_last_j(void);
/* C(row,col) once computed (0 before): the diagonal exactly, other
 * entries to within 2^shift, since only C >> shift is kept. */
s32 ef_cov(u8 row, u8 col);
u8 ef_cov_shift(void);

/* Find the eigenvectors of C, largest eigenvalue first, by power
 * iteration with Gram-Schmidt; one iteration per call. */
u8 ef_eigen_step(void);
/* Which eigenvector is being found (0-based), and its iteration count. */
u8 ef_eigen_component(void);
u8 ef_eigen_iteration(void);
/* The vector currently being refined (while running) or finished
 * eigenvector `comp`; entries are -EF_VEC_SCALE..EF_VEC_SCALE. */
const s16 *ef_eigen_current(void);
const s16 *ef_eigen_vector(u8 comp);
u8 ef_eigen_iterations_used(u8 comp);
/* Share of all the variation between faces that eigenvector `comp`
 * accounts for: its eigenvalue over the sum of all eigenvalues, in
 * 1/1000. */
u16 ef_variance_permille(u8 comp);

/* Eigenface comp = sum over faces i of v_comp[i] * centered face i, one
 * image row per call. */
u8 ef_eigenface_step(void);
u8 ef_eigenface_component(void);
u8 ef_eigenface_row(void);
const s8 *ef_eigenface(u8 comp);
/* Length of eigenface comp (it is stored at whatever scale fits a byte). */
u16 ef_eigenface_norm(u8 comp);
/* Pixels clipped to +-63 so far (should stay 0). */
u8 ef_eigenface_clipped(void);

/* Project training face i onto the eigenfaces (work out its weights),
 * one face per call. */
u8 ef_weights_step(void);
u8 ef_weights_done(void);
/* Weight of training face `face` along eigenface `comp`, in
 * 1/EF_WEIGHT_UNIT gray levels. */
s16 ef_weight(u8 face, u8 comp);

/* ---- matching ------------------------------------------------------ */

/* The query's 576 pixels (0..63; brighter values count as 63). The
 * buffer is read by ef_query_project(), so keep it until then. */
u8 ef_query_set(const u8 *pixels);
/* Work out the query's weights. */
u8 ef_query_project(void);
s16 ef_query_weight(u8 comp);

/* Sort training faces by distance to the query, in weight space. */
u8 ef_rank(void);
/* The training face in place `place` (0 = closest). */
u8 ef_rank_index(u8 place);
/* Euclidean distance from the query to training face `face`, in weight
 * space (1/4 gray levels). */
u16 ef_distance(u8 face);
/* 0..100: how much closer the best match is than the closest face of a
 * different label. 100 when every training face has the same label. */
u8 ef_confidence(void);

/* mean + the first `upto` eigenfaces weighted by the query's weights,
 * clamped to 0..63. upto = 0 gives the mean face. */
u8 ef_reconstruct(u8 upto, u8 *out);
/* The same for any weight vector (e.g. a training face's), filling only
 * image rows first_row .. first_row + rows - 1 of out, so a caller can
 * draw the face as it is rebuilt. */
u8 ef_reconstruct_rows(const s16 *weights, u8 upto, u8 first_row, u8 rows, u8 *out);

#endif /* EF_MODEL_H */
