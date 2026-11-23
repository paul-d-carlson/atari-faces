/*
 * demo.h -- the eigenfaces show: load, train step by step, then recognise
 * each mystery face in turn, forever.
 *
 * Progress and results are also written to a small report block in page
 * six ($0600), which the integration test reads out of emulated memory.
 * The block is bytes only, so the host test driver can read it by offset.
 */
#ifndef DEMO_H
#define DEMO_H

#include "types.h"
#include "ef_data.h"

#define DEMO_REPORT_ADDR 0x0600u
#define DEMO_MAGIC       0xD5u

/* Phases timed in demo_report.frames: the eigenfaces maths only (the
 * ef_* calls), not the drawing around it or the pauses for the viewer.
 * Loading is the exception: it counts the disk reads. */
#define DEMO_PH_LOAD      0u
#define DEMO_PH_MEAN      1u
#define DEMO_PH_CENTER    2u
#define DEMO_PH_COV       3u
#define DEMO_PH_EIGEN     4u
#define DEMO_PH_EIGENFACE 5u
#define DEMO_PH_WEIGHTS   6u
#define DEMO_PH_MATCH     7u
#define DEMO_PHASES       8u

/* Errors (demo_report.error). */
#define DEMO_OK           0u
#define DEMO_ERR_OPEN     1u
#define DEMO_ERR_READ     2u
#define DEMO_ERR_FORMAT   3u
#define DEMO_ERR_MODEL    4u

typedef struct {
    u8 label;          /* true subject of the query */
    u8 best_index;     /* closest training face */
    u8 best_label;
    u8 confidence;     /* 0..100 */
    u8 dist_lo;        /* distance to the best match, 1/4 grey levels */
    u8 dist_hi;
} demo_query_report;

typedef struct {
    u8 magic;
    u8 error;
    u8 n_train;
    u8 n_query;
    u8 k;
    u8 trained;        /* 1 once training has finished */
    u8 queries_done;   /* counts up for ever; the first n_query are kept */
    u8 eigen_iters;    /* power iterations over all components */
    u8 frames[DEMO_PHASES * 2u];  /* little-endian frame counts */
    demo_query_report q[EFD_MAX_QUERY];
} demo_report;

/* Number of eigenfaces the demo keeps. */
#define DEMO_K 4u

/* File demo_load() reads; DISK_FACES unless a test changes it. */
extern const char *demo_data_path;

demo_report *demo_report_block(void);
/* Read FACES.DAT into the model. Returns DEMO_OK or an error. */
u8 demo_load(void);
void demo_title(void);
void demo_train(void);
/* Recognise query q. */
void demo_match(u8 q);
/* Show an error on screen and record it in the report block. */
void demo_fail(u8 error, u8 detail);
/* Everything, forever. */
void demo_run(void);

#endif /* DEMO_H */
