/*
 * demo_runner.c -- integration test: boot the real demo disk (the exact
 * AUTORUN.SYS users get), let it train and recognise every mystery face,
 * then check what it reported in page six (src/demo.h) against the numpy
 * reference and the speed budgets below (3 s per face, 60 s to train,
 * and the screen never frozen for more than 1.5 s).
 *
 *   demo_runner DEMO.atr DEMO.dbg [--machine OPT] [--profile FILE] [--shots DIR]
 *               [--realtime] [--cover src/x.c ...]
 *
 * START is tapped every few frames so the pauses meant for a human viewer
 * finish quickly; --realtime leaves START alone and plays the show at its
 * natural pace. --shots saves a PNG of the last frame of every screen
 * (each time the top text line changes).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu.h"
#include "../../src/demo.h"
#include "../../src/screen.h"
#include "../fixtures/ef_reference.h"

#define MAX_FRAMES (60 * 60 * 15)
#define MAX_COVER  16

/* The C stack (src/atari.cfg): __STACKSIZE__ bytes growing down from
 * MEMTOP - __RESERVED_MEMORY__. The OS clears RAM at power-on, so the
 * lowest non-zero byte shows how deep the stack ever went. */
#define STACK_TOP    (0xBC1Fu - 0x0640u)
#define STACK_SIZE   0x0200u
#define STACK_LIMIT  (STACK_SIZE * 3u / 4u)

/* Speed budgets, in frames (60 a second). The demo times only the
 * eigenfaces maths (see src/demo.h); BUDGET_FROZEN covers everything
 * else, so the show itself never stalls. */
#define BUDGET_MATCH     (3 * 60)    /* recognising one face */
#define BUDGET_TRAINING  (60 * 60)   /* everything from mean to weights */
#define BUDGET_FROZEN    90          /* longest time the screen may not change */

static const char *const g_phase_names[DEMO_PHASES] = {
    "load", "mean", "centre", "covariance", "eigenvectors", "eigenfaces",
    "weights", "match",
};

/* A cheap fingerprint of the picture and text, to spot a frozen screen. */
static unsigned long screen_sum(void)
{
    const unsigned char *m = emu_mem();
    unsigned long sum = 0;
    unsigned a;

    for (a = SCR_GFX; a < SCR_GFX + SCR_H * SCR_ROW_BYTES; ++a) {
        sum = sum * 31u + m[a];
    }
    for (a = SCR_TEXT; a < SCR_TEXT + SCR_LINES * SCR_COLS; ++a) {
        sum = sum * 31u + m[a];
    }
    return sum;
}

static void text_line(unsigned line, char *out)
{
    const unsigned char *t = emu_mem() + SCR_TEXT + line * SCR_COLS;
    int i;

    for (i = 0; i < (int)SCR_COLS; ++i) {
        unsigned char c = (unsigned char)(t[i] & 0x7F);
        out[i] = (char)(c < 64 ? c + 32 : c);
    }
    out[SCR_COLS] = '\0';
}

static void save_shot(const char *dir, int n, const char *title)
{
    char path[512];
    char name[48];
    int i;
    int j = 0;

    for (i = 0; title[i] != '\0' && j < 40; ++i) {
        char c = title[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            name[j++] = (char)(c - (c >= 'A' ? 'A' - 'a' : 0));
        } else if (j > 0 && name[j - 1] != '-') {
            name[j++] = '-';
        }
    }
    while (j > 0 && name[j - 1] == '-') {
        --j;
    }
    name[j] = '\0';
    snprintf(path, sizeof(path), "%s/%02d-%s.png", dir, n, name);
    emu_save_snapshot(path);
}

int main(int argc, char **argv)
{
    const char *machine = "-atari";
    const char *profile = NULL;
    const char *shots = NULL;
    int realtime = 0;
    dbgcov_target_t cover[MAX_COVER];
    int ncover = 0;
    const demo_report *r;
    char line0[SCR_COLS + 1];
    char prev0[SCR_COLS + 1] = "";
    int nshot = 0;
    int title_since = 0;
    unsigned long last_sum = 0;
    int last_change = 0;
    int frozen = 0;
    int frozen_at = 0;
    char frozen_title[SCR_COLS + 1] = "";
    char change_title[SCR_COLS + 1] = "";
    int frame;
    int running = 0;
    int failures = 0;
    unsigned q;
    unsigned p;
    unsigned total = 0;
    int i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s DEMO.atr DEMO.dbg [--machine OPT] [--profile FILE] [--shots DIR] "
                        "[--realtime] [--cover FILE...]\n", argv[0]);
        return 2;
    }
    for (i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            profile = argv[++i];
        } else if (strcmp(argv[i], "--machine") == 0 && i + 1 < argc) {
            machine = argv[++i];
        } else if (strcmp(argv[i], "--shots") == 0 && i + 1 < argc) {
            shots = argv[++i];
        } else if (strcmp(argv[i], "--realtime") == 0) {
            realtime = 1;
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
    r = (const demo_report *)(emu_mem() + DEMO_REPORT_ADDR);
    for (frame = 0; frame < MAX_FRAMES; ++frame) {
        /* Never touch START until the demo owns the screen: START held
         * at power-on means "boot from cassette". */
        if (!running) {
            running = emu_peek16(0x0230) == SCR_DL;
            last_change = frame;    /* the freeze clock starts here */
        }
        if (emu_frame(running && !realtime && frame % 8 < 2) != 0) {
            return 1;
        }
        if (running) {
            unsigned long sum = screen_sum();
            if (sum != last_sum) {
                if (frame - last_change > frozen) {
                    frozen = frame - last_change;
                    frozen_at = last_change;
                    strcpy(frozen_title, change_title);
                }
                last_sum = sum;
                last_change = frame;
                text_line(0, change_title);
            }
        }
        if (running && shots != NULL) {
            /* A screen is saved when its title line changes, but only if
             * that title stayed put for a few frames (titles are written
             * a piece at a time). */
            text_line(0, line0);
            if (strcmp(line0, prev0) != 0) {
                if (prev0[0] != '\0' && frame - title_since >= 4) {
                    if (profile != NULL) {
                        /* PROFILE.N: the cycles spent on screen N. */
                        char path[512];
                        snprintf(path, sizeof(path), "%s.%02d", profile, nshot);
                        emu_profile_delta(path);
                    }
                    save_shot(shots, nshot++, prev0);
                }
                strcpy(prev0, line0);
                title_since = frame;
            }
            emu_snapshot();
        }
        if (r->magic == DEMO_MAGIC &&
            (r->error != DEMO_OK || (r->n_query > 0 && r->queries_done >= r->n_query))) {
            break;
        }
    }
    if (shots != NULL && prev0[0] != '\0') {
        /* Let the last result sit on screen for a moment, then save it. */
        for (i = 0; i < 30; ++i) {
            emu_frame(0);
        }
        emu_snapshot();
        save_shot(shots, nshot++, prev0);
    }

    printf("Demo run on %s: %d frames (%.1f emulated seconds)\n", machine, frame,
           frame / 60.0);
    if (r->magic != DEMO_MAGIC || frame >= MAX_FRAMES) {
        printf("FAIL: the demo did not finish (magic %02X, %u queries done)\n", r->magic,
               r->queries_done);
        emu_exit();
        return 1;
    }
    if (r->error != DEMO_OK) {
        printf("FAIL: the demo reported error %u\n", r->error);
        emu_exit();
        return 1;
    }

    printf("Time spent on the maths (drawing and pauses not counted):\n");
    for (p = 0; p < DEMO_PHASES; ++p) {
        unsigned f = r->frames[2 * p] | r->frames[2 * p + 1] << 8;
        if (p >= DEMO_PH_MEAN && p <= DEMO_PH_WEIGHTS) {
            total += f;
        }
        printf("  %-13s %5u frames %6.2f s\n", g_phase_names[p], f, f / 60.0);
    }
    printf("  %-13s %5u frames %6.2f s  (budget %u)%s\n", "training", total, total / 60.0,
           BUDGET_TRAINING, total > BUDGET_TRAINING ? "  OVER BUDGET" : "");
    failures += total > BUDGET_TRAINING;
    {
        unsigned matches = r->frames[2 * DEMO_PH_MATCH] | r->frames[2 * DEMO_PH_MATCH + 1] << 8;
        unsigned per = r->queries_done ? matches / r->queries_done : 0;
        printf("  %-13s %5u frames %6.2f s  (budget %u)%s\n", "per match", per, per / 60.0,
               BUDGET_MATCH, per > BUDGET_MATCH ? "  OVER BUDGET" : "");
        failures += per > BUDGET_MATCH;
    }
    printf("  longest frozen screen %d frames (budget %d)%s\n    from frame %d on: %s\n",
           frozen, BUDGET_FROZEN, frozen > BUDGET_FROZEN ? "  TOO LONG" : "", frozen_at,
           frozen_title);
    failures += frozen > BUDGET_FROZEN;
    printf("  power iterations: %u\n", r->eigen_iters);

    {
        const unsigned char *m = emu_mem();
        unsigned a = STACK_TOP - STACK_SIZE + 1u;
        unsigned used;
        while (a <= STACK_TOP && m[a] == 0) {
            ++a;
        }
        used = STACK_TOP + 1u - a;
        printf("C stack: at most %u of %u bytes used (limit %u)%s\n", used, STACK_SIZE,
               STACK_LIMIT, used > STACK_LIMIT ? "  TOO DEEP" : "");
        failures += used > STACK_LIMIT;
    }

    printf("Matches:\n");
    if (!r->trained || r->n_query != REF_N_QUERY || r->n_train != REF_N_TRAIN) {
        printf("  FAIL: trained=%u n_train=%u n_query=%u\n", r->trained, r->n_train,
               r->n_query);
        ++failures;
    }
    for (q = 0; q < r->n_query && q < REF_N_QUERY; ++q) {
        const demo_query_report *qr = &r->q[q];
        unsigned d = qr->dist_lo | qr->dist_hi << 8;
        int ok = qr->label == ref_query_labels[q] &&
                 qr->best_index == ref_rankings[q * REF_N_TRAIN] &&
                 qr->best_label == qr->label && qr->confidence >= 70;
        printf("  [%s] mystery face %u: subject %02u -> face %u (subject %02u), "
               "distance %.1f, confidence %u%%\n", ok ? "PASS" : "FAIL", q + 1, qr->label,
               qr->best_index, qr->best_label, d / 4.0, qr->confidence);
        failures += !ok;
    }
    if (ncover > 0) {
        emu_coverage(argv[2], cover, ncover);
    }
    if (profile != NULL) {
        emu_profile(profile);
    }
    if (shots != NULL) {
        printf("%d screenshots in %s\n", nshot, shots);
    }
    printf("%s\n", failures ? "INTEGRATION TEST FAILED" : "integration test passed");
    emu_exit();
    return failures ? 1 : 0;
}
