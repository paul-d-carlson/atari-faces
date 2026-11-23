/*
 * target_runner.c -- boot a 6502 unit-test disk, wait for the page-six
 * result block (tests/common/test_protocol.h) and report it.
 *
 *   target_runner IMAGE.atr IMAGE.dbg [--machine OPT] [--profile FILE] [--cover src/x.c ...]
 *
 * Test and suite names are read from the image's own memory, so this
 * runner needs no list of tests. Exit status 0 only if every test passed.
 * --profile FILE writes cycles per 6502 address for the whole run, and
 * FILE.N for the stretch ending at each tf_bench() timing (see
 * tools/profile.py).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu.h"
#include "test_protocol.h"

#define MAX_FRAMES  (60 * 60 * 10)   /* ten emulated minutes */
#define MAX_TESTS   255
#define MAX_COVER   16

static const char *g_names[MAX_TESTS];
static const char *g_suite_of[MAX_TESTS];
static char g_strings[MAX_TESTS * 2][64];

static const char *read_string(unsigned addr, int slot)
{
    unsigned char *m = emu_mem();
    int i;

    for (i = 0; i < 63 && m[(addr + i) & 0xFFFFu] != 0; ++i) {
        g_strings[slot][i] = (char)m[(addr + i) & 0xFFFFu];
    }
    g_strings[slot][i] = '\0';
    return g_strings[slot];
}

/* Walk the suite table the image left a pointer to. */
static int read_names(void)
{
    unsigned table = emu_peek16(TP_ADDR + TP_OFF_SUITES);
    unsigned suite;
    unsigned cases;
    const char *suite_name;
    int n = 0;
    int s;

    for (s = 0; (suite = emu_peek16(table + 2u * s)) != 0; ++s) {
        suite_name = read_string(emu_peek16(suite), MAX_TESTS + s);
        for (cases = emu_peek16(suite + 2u); emu_peek16(cases) != 0 && n < MAX_TESTS;
             cases += 4u) {
            g_names[n] = read_string(emu_peek16(cases), n);
            g_suite_of[n] = suite_name;
            ++n;
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *machine = "-atari";
    const char *profile = NULL;
    unsigned nbench_seen = 0;
    dbgcov_target_t cover[MAX_COVER];
    int ncover = 0;
    unsigned char *tp;
    int frame;
    int ntests;
    int i;
    int nfails;
    const char *prev_suite = "";

    if (argc < 3) {
        fprintf(stderr, "usage: %s IMAGE.atr IMAGE.dbg [--machine OPT] [--profile FILE] [--cover FILE...]\n",
                argv[0]);
        return 2;
    }
    for (i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            profile = argv[++i];
        } else if (strcmp(argv[i], "--machine") == 0 && i + 1 < argc) {
            machine = argv[++i];
        } else if (strcmp(argv[i], "--cover") == 0 && i + 1 < argc && ncover < MAX_COVER) {
            memset(&cover[ncover], 0, sizeof(cover[ncover]));
            cover[ncover].suffix = argv[++i];
            cover[ncover].label = argv[i];
            ++ncover;
        } else {
            fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 2;
        }
    }

    if (emu_boot(argv[1], machine) != 0) {
        return 2;
    }
    tp = emu_mem() + TP_ADDR;
    for (frame = 0; frame < MAX_FRAMES; ++frame) {
        /* With --profile, also save the cycles behind each benchmark:
         * PROFILE.N covers the time up to the Nth tf_bench() call. */
        if (profile != NULL && tp[TP_OFF_MAGIC] == TP_MAGIC &&
            tp[TP_OFF_NBENCH] > nbench_seen) {
            char path[512];
            snprintf(path, sizeof(path), "%s.%u", profile, nbench_seen);
            emu_profile_delta(path);
            nbench_seen = tp[TP_OFF_NBENCH];
        }
        if (emu_frame(0) != 0) {
            printf("  last test started: #%u\n", tp[TP_OFF_CUR]);
            emu_exit();
            return 1;
        }
        if (tp[TP_OFF_MAGIC] == TP_MAGIC && tp[TP_OFF_DONE]) {
            break;
        }
    }
    if (!(tp[TP_OFF_MAGIC] == TP_MAGIC && tp[TP_OFF_DONE])) {
        printf("TIMEOUT after %d frames: magic=%02X tests run=%u, running #%u\n", frame,
               tp[TP_OFF_MAGIC], tp[TP_OFF_TOTAL], tp[TP_OFF_CUR]);
        emu_exit();
        return 1;
    }

    ntests = read_names();
    printf("%s: %u tests in %d frames (%.1f emulated seconds)\n", argv[1],
           tp[TP_OFF_TOTAL], frame, frame / 60.0);
    nfails = tp[TP_OFF_NFAILS];
    for (i = 0; i < ntests; ++i) {
        int failed = 0;
        int j;
        for (j = 0; j < nfails; ++j) {
            failed |= tp[TP_OFF_FAILS + j * TP_FAIL_SIZE] == i;
        }
        if (strcmp(g_suite_of[i], prev_suite) != 0) {
            prev_suite = g_suite_of[i];
            printf("  %s\n", prev_suite);
        }
        printf("    [%s] %s\n", failed ? "FAIL" : "PASS", g_names[i]);
    }

    for (i = 0; i < nfails; ++i) {
        const unsigned char *f = tp + TP_OFF_FAILS + i * TP_FAIL_SIZE;
        long actual = (long)(int)(f[4] | f[5] << 8 | f[6] << 16 | (unsigned)f[7] << 24);
        long expected = (long)(int)(f[8] | f[9] << 8 | f[10] << 16 | (unsigned)f[11] << 24);
        printf("  FAIL %s/%s line %u: got %ld, want %ld\n",
               f[0] < ntests ? g_suite_of[f[0]] : "?", f[0] < ntests ? g_names[f[0]] : "?",
               f[2] | f[3] << 8, actual, expected);
    }

    if (tp[TP_OFF_NBENCH] != 0) {
        printf("Timings on a 1.79 MHz 6502 (60 frames = 1 second):\n");
        for (i = 0; i < tp[TP_OFF_NBENCH]; ++i) {
            const unsigned char *b = tp + TP_OFF_BENCH + i * TP_BENCH_SIZE;
            unsigned frames = b[2] | b[3] << 8;
            printf("  %-16s %5u frames  %5.2f s\n", read_string(b[0] | b[1] << 8, 0),
                   frames, frames / 60.0);
        }
    }
    if (ncover > 0) {
        emu_coverage(argv[2], cover, ncover);
    }
    if (profile != NULL) {
        emu_profile(profile);
    }
    printf("%u/%u tests passed\n", tp[TP_OFF_TOTAL] - tp[TP_OFF_FAILED], tp[TP_OFF_TOTAL]);
    i = (tp[TP_OFF_FAILED] == 0 && tp[TP_OFF_TOTAL] == ntests) ? 0 : 1;
    emu_exit();
    return i;
}
