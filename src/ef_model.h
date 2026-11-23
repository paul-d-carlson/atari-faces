/*
 * ef_model.h -- eigenfaces training and matching on small integer faces.
 *
 * The method follows the article the demo is based on: subtract the mean
 * face, find the principal components of the training faces through the
 * small N x N covariance matrix (the "snapshot" trick), turn those into
 * eigenfaces, project every face onto them and rank the training faces by
 * Euclidean distance between weight vectors.
 *
 * Everything is integer maths sized for a 6502: pixels are 0..63, centred
 * pixels -63..63, eigenfaces are -63..63 with their length kept on the
 * side, and weights are in 1/4 grey-level units.
 *
 * Training is split into small steps so the caller can redraw the screen
 * between them:
 *
 *   ef_begin(n, k)            then fill ef_face_slot(i) / ef_set_label()
 *   ef_compute_mean()
 *   while (ef_center_step() == EF_MORE) ...
 *   while (ef_cov_step() == EF_MORE) ...
 *   while (ef_eigen_step() == EF_MORE) ...
 *   while (ef_eigenface_step() == EF_MORE) ...
 *   while (ef_weights_step() == EF_MORE) ...
 *   ef_query_set(); ef_query_project(); ef_rank();
 *
 * Calling a step out of order returns EF_ERR and changes nothing.
 * This module does no I/O and has no platform dependencies.
 */
#ifndef EF_MODEL_H
#define EF_MODEL_H

#include "types.h"

#define EF_W            24u
#define EF_H            24u
#define EF_PIXELS       576u
#define EF_LEVELS       64u
#define EF_MAX_FACES    10u
#define EF_MAX_K        4u

/* Sample-space eigenvectors are scaled so their largest entry is +-255. */
#define EF_VEC_SCALE    255
/* Weights and distances are in units of 1/EF_WEIGHT_UNIT grey level. */
#define EF_WEIGHT_UNIT  4
/* Eigenfaces are stored with this root-mean-square pixel value. */
#define EF_EF_RMS       12u
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

/* Start a new model with n training faces and k eigenfaces.
 * Needs 2 <= n <= EF_MAX_FACES and 1 <= k <= EF_MAX_K, k < n.
 * Returns EF_DONE or EF_ERR. */
u8 ef_begin(u8 n, u8 k);
/* Where to put the 576 pixels (0..63) of training face i, or 0. */
u8 *ef_face_slot(u8 i);
void ef_set_label(u8 i, u8 label);

u8 ef_stage(void);
u8 ef_face_count(void);
u8 ef_k(void);
u8 ef_label(u8 i);

/* ---- training ------------------------------------------------------ */

/* Mean face (rounded). Pixel values above 63 are clamped first. */
u8 ef_compute_mean(void);
const u8 *ef_mean(void);
/* Average of the first j training faces, for showing the mean build up;
 * like ef_reconstruct_rows(), only rows first_row .. first_row + rows - 1
 * of out are filled. Only before ef_compute_mean() (faces still 0..63). */
u8 ef_mean_of_first(u8 j, u8 first_row, u8 rows, u8 *out);

/* Subtract the mean from the next face, in place; one face per call. */
u8 ef_center_step(void);
u8 ef_center_done(void);
/* Face i as stored: raw 0..63 pixels until ef_center_step() has reached
 * it, centred -63..63 pixels afterwards. */
const s8 *ef_centered(u8 i);
/* Rebuild the original 0..63 pixels of training face i. */
void ef_face_pixels(u8 i, u8 *out);

/* Covariance C[i][j] = sum (face_i - mean)(face_j - mean), one entry per
 * call: the diagonal first, then the rest row by row. */
u8 ef_cov_step(void);
/* Entries computed so far, out of n(n+1)/2. */
u8 ef_cov_done(void);
u8 ef_cov_total(void);
/* Row and column of the entry the last step computed. */
u8 ef_cov_last_i(void);
u8 ef_cov_last_j(void);
/* C(i,j) once computed (0 before): the diagonal exactly, other entries to
 * within 2^shift, since only C >> shift is kept. */
s32 ef_cov(u8 i, u8 j);
u8 ef_cov_shift(void);

/* Power iteration with Gram-Schmidt, one iteration per call. */
u8 ef_eigen_step(void);
u8 ef_eigen_component(void);
u8 ef_eigen_iteration(void);
/* The vector currently being refined (while running) or component c. */
const s16 *ef_eigen_current(void);
const s16 *ef_eigen_vector(u8 c);
u8 ef_eigen_iterations_used(u8 c);
/* Share of the total variance explained by component c, in 1/1000. */
u16 ef_variance_permille(u8 c);

/* Eigenface c = sum_i v_c[i] * centred face i, one image row per call. */
u8 ef_eigenface_step(void);
u8 ef_eigenface_component(void);
u8 ef_eigenface_row(void);
const s8 *ef_eigenface(u8 c);
/* Length of eigenface c (it is stored at whatever scale fits a byte). */
u16 ef_eigenface_norm(u8 c);
/* Pixels clipped to +-63 so far (should stay 0). */
u8 ef_eigenface_clipped(void);

/* Project training face i onto the eigenfaces, one face per call. */
u8 ef_weights_step(void);
u8 ef_weights_done(void);
s16 ef_weight(u8 i, u8 c);

/* ---- matching ------------------------------------------------------ */

/* The query's 576 pixels (0..63; brighter values count as 63). The
 * buffer is read by ef_query_project(), so keep it until then. */
u8 ef_query_set(const u8 *pixels);
u8 ef_query_project(void);
s16 ef_query_weight(u8 c);

/* Sort training faces by distance to the query. */
u8 ef_rank(void);
u8 ef_rank_index(u8 r);
/* Euclidean distance from the query to training face i (1/4 grey levels). */
u16 ef_distance(u8 i);
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
