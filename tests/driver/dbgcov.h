/*
 * dbgcov.h -- taken unchanged (apart from comments) from atari-emacs.
 *
 * Statement-level line coverage from a cc65/ld65 debug-info file
 * (produced via `cl65 -g -Wl --dbgfile,<path>`) cross-referenced against
 * a bitmap of which 6502 addresses actually executed during a test run.
 *
 * This is NOT branch coverage: a line is "covered" if at least one byte
 * of its generated code executed at least once (the same guarantee
 * gcov's basic line-coverage mode gives you). A line is "coverable" only
 * if the compiler actually emitted code for it (declarations, comments,
 * and blank lines have no associated span and are excluded).
 */
#ifndef DBGCOV_H
#define DBGCOV_H

/* Large enough for any single source file under test. */
#define DBGCOV_MAX_LINE 8192

typedef struct {
    const char *suffix;            /* match dbg file name ending in this,
                                     * e.g. "/buffer.c"                  */
    const char *label;             /* display label, e.g. "src/buffer.c" */
    unsigned int coverable_lines;  /* filled in by dbgcov_analyze()      */
    unsigned int covered_lines;    /* filled in by dbgcov_analyze()      */
    int verbose;                   /* caller sets: if nonzero, print every
                                     * coverable-but-not-covered line number
                                     * for this target to stdout          */
} dbgcov_target_t;

/*
 * Parses dbg_path and fills in coverable_lines/covered_lines for each of
 * the num_targets entries in `targets` (suffix/label must already be
 * set by the caller). hit[addr] must be nonzero for every 6502 address
 * that executed at least one instruction during the run.
 *
 * Returns 0 on success, -1 if dbg_path could not be opened or read (in
 * which case all target counts are left at whatever the caller
 * initialized them to).
 */
int dbgcov_analyze(const char *dbg_path, dbgcov_target_t *targets,
                    int num_targets, const unsigned char hit[65536]);

#endif /* DBGCOV_H */
