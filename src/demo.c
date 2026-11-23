/*
 * demo.c -- see demo.h.
 *
 * The show walks through the eigenfaces method (introduced step by step
 * in ef_model.h) one screen per step, calling into ef_model.c a little
 * at a time and drawing what each step produced:
 *
 *   step 1  demo_load()          read ten training faces and five
 *                                "mystery" faces from D:FACES.DAT
 *   step 2  train_mean()         the mean face, built up face by face
 *   step 3  train_center()       each face minus the mean: what is
 *                                special about it
 *   step 4  train_covariance()   the 10 x 10 table of how alike every
 *                                pair of faces is
 *   step 5  train_eigen()        the eigenvectors of that table, found
 *                                by power iteration, as bar charts
 *   step 6  train_eigenfaces()   each eigenvector turned into an image
 *   step 7  train_weights()      every face as four weights, and the
 *                                face rebuilt from just those four
 *   then    demo_match()         for each mystery face: its weights,
 *                                rebuilt from the eigenfaces, compared
 *                                with every training face
 *
 * Only the time spent inside ef_* calls is counted as "math"
 * (clock_start() / clock_stop()); drawing and pauses are not.
 *
 * Screen layout (80 x 40 gray pixels, then 4 text lines):
 *
 *   rows  0-23  three 24x24 panels: A (x=2), B (x=28), C (x=54)
 *   rows 25-32  the ten training faces as 8x8 thumbnails
 *   row  33     marker under the face being worked on
 *   rows 34-39  a bar under each thumbnail
 *   text 0      step title        text 1  what is being computed
 *   text 2      numbers           text 3  progress bar / prompt
 */
#include <string.h>

#include "demo.h"
#include "disk.h"
#include "ef_model.h"
#include "fmt.h"
#include "hw.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"

#define HUE_TITLE   9u
#define HUE_TRAIN   9u
#define HUE_EIGEN   6u
#define HUE_QUERY   12u
#define HUE_RIGHT   12u
#define HUE_WRONG   3u

#define HOLD_SHORT  20u
#define HOLD_STEP   45u
#define HOLD_LONG   150u
#define HOLD_RESULT 480u

#define N_STEPS     7u

const char *demo_data_path = DISK_FACES;

/* What FACES.DAT says about its faces (counts, subjects, photo numbers). */
static efd_info g_file;
static u8 g_file_meta[2u * EFD_MAX_TOTAL];
static u8 g_query_faces[EFD_MAX_QUERY][EF_PIXELS];
/* Scratch image for drawing a face. */
static u8 g_image[EF_PIXELS];
/* The weights of the face being drawn or rebuilt. */
static s16 g_shown_weights[EF_MAX_K];
static u16 g_clock_started;
static u16 g_training_frames;

demo_report *demo_report_block(void)
{
    return (demo_report *)HW_PTR(DEMO_REPORT_ADDR);
}

/* ---- timing ---------------------------------------------------------- */

static void clock_start(void)
{
    g_clock_started = sys_ticks();
}

/* Add the frames since clock_start() to a phase. */
static void clock_stop(u8 phase)
{
    demo_report *report = demo_report_block();
    u16 total = (u16)(report->frames[2u * phase] |
                      ((u16)report->frames[2u * phase + 1u] << 8));

    total = (u16)(total + (u16)(sys_ticks() - g_clock_started));
    report->frames[2u * phase] = (u8)(total & 0xFFu);
    report->frames[2u * phase + 1u] = (u8)(total >> 8);
}

static u16 phase_frames(u8 phase)
{
    demo_report *report = demo_report_block();

    return (u16)(report->frames[2u * phase] | ((u16)report->frames[2u * phase + 1u] << 8));
}

/* ---- text helpers ---------------------------------------------------- */

static void step_title(u8 step, const char *name)
{
    fmt_begin();
    fmt_str(" STEP ");
    fmt_u(step, 0);
    fmt_str(" OF ");
    fmt_u(N_STEPS, 0);
    fmt_char(' ');
    scr_text(0, "");
    scr_text_at(0, 0, fmt_end(), SCR_INVERSE);
    scr_text_at(0, (u8)(fmt_len() + 1u), name, 0);
}

static void progress(u16 done, u16 total)
{
    scr_text(3, "");
    scr_progress(3, 0, 30, done, total);
    fmt_begin();
    fmt_u((u16)(((u32)done * 100u) / total), 3);
    fmt_char('%');
    scr_text_at(3, 32, fmt_end(), 0);
}

/* "1.2 SEC" from a frame count (60 frames a second). */
static void fmt_seconds(u16 frames)
{
    fmt_dec1((u16)(((u32)frames + 3u) / 6u));
    fmt_str(" SEC");
}

static void fmt_subject(u8 label)
{
    fmt_str("SUBJECT ");
    fmt_u0(label, 2);
}

static void draw_thumbs(void)
{
    u8 face;

    for (face = 0; face < g_file.n_train; ++face) {
        ef_face_pixels(face, g_image);
        scr_thumb(face, g_image);
    }
}

static void clear_marks(void)
{
    u8 slot;

    for (slot = 0; slot < EF_MAX_FACES; ++slot) {
        scr_mark(slot, 0);
        scr_bar(slot, 0, 0);
    }
}

/* Faces that take a while to compute are drawn a band of BAND rows at a
 * time, with a bright scan line on the band being worked on, so there is
 * always something moving on screen. */
#define BAND 6u

static void scan_line(u8 x, u8 row)
{
    scr_fill(x, row, 24u, 1u, 63u);
}

/* Reveal a face two rows per frame with a bright scan line ahead. */
static void reveal(u8 x, const u8 *pixels)
{
    u8 rows;

    for (rows = 2; rows <= 24u; rows += 2u) {
        scr_face_rows(x, 0, pixels, rows);
        if (rows < 24u) {
            scan_line(x, rows);
        }
        sys_wait_frame();
    }
}

/* Rebuild a face from its weights. */
static void draw_rebuilt(u8 x, const s16 *weights, u8 upto)
{
    u8 row;

    for (row = 0; row < EF_H; row += BAND) {
        scan_line(x, row);
        ef_reconstruct_rows(weights, upto, row, BAND, g_image);
        scr_face_rows(x, 0, g_image, (u8)(row + BAND));
    }
}

/* An eigenvector of the 10 x 10 matrix as ten bars, one under each
 * training face: how much of that face goes into the eigenface. Height
 * is the size of the entry, bright for positive and dark for negative. */
static void vector_bars(const s16 *vec)
{
    u8 face;
    s16 magnitude;

    for (face = 0; face < g_file.n_train; ++face) {
        magnitude = vec[face] < 0 ? (s16)-vec[face] : vec[face];
        scr_bar(face, (u8)(((u16)magnitude * SCR_BAR_H + EF_VEC_SCALE / 2) / EF_VEC_SCALE),
                vec[face] < 0 ? 24u : 63u);
    }
}

/* ---- loading --------------------------------------------------------- */

u8 demo_load(void)
{
    demo_report *report = demo_report_block();
    u8 header[EFD_HEADER_LEN];
    u8 face;
    u8 n_in_file;
    u8 *slot;
    int fd;
    u8 result = DEMO_OK;

    memset(report, 0, sizeof(*report));
    report->magic = DEMO_MAGIC;
    report->k = DEMO_K;

    step_title(1, "LOADING THE FACES");
    scr_hue(HUE_TRAIN);
    scr_clear();
    scr_text(1, "READING D:FACES.DAT FROM DISK");
    scr_text(2, "");
    scr_text(3, "");

    clock_start();
    fd = disk_open(demo_data_path);
    if (fd < 0) {
        return DEMO_ERR_OPEN;
    }
    if (disk_read(fd, header, EFD_HEADER_LEN) != EFD_HEADER_LEN ||
        efd_parse_header(header, &g_file) != EFD_OK || g_file.n_query == 0u) {
        result = DEMO_ERR_FORMAT;
        goto done;
    }
    if (disk_read(fd, g_file_meta, efd_meta_len(&g_file)) != efd_meta_len(&g_file) ||
        efd_parse_meta(g_file_meta, &g_file) != EFD_OK) {
        result = DEMO_ERR_FORMAT;
        goto done;
    }
    if (ef_begin(g_file.n_train, DEMO_K) != EF_DONE) {
        result = DEMO_ERR_MODEL;
        goto done;
    }
    report->n_train = g_file.n_train;
    report->n_query = g_file.n_query;

    n_in_file = (u8)(g_file.n_train + g_file.n_query);
    for (face = 0; face < n_in_file; ++face) {
        slot = face < g_file.n_train ? ef_face_slot(face)
                                     : g_query_faces[face - g_file.n_train];
        if (disk_read(fd, slot, EF_PIXELS) != EF_PIXELS) {
            result = DEMO_ERR_READ;
            goto done;
        }
        if (efd_check_face(slot) != EFD_OK) {
            result = DEMO_ERR_FORMAT;
            goto done;
        }
        fmt_begin();
        if (face < g_file.n_train) {
            ef_set_label(face, g_file.label[face]);
            scr_face(SCR_PANEL_A, 0, slot);
            scr_thumb(face, slot);
            fmt_str("FACE ");
            fmt_u0((u16)(face + 1u), 2);
            fmt_char('/');
            fmt_u0(g_file.n_train, 2);
            fmt_str("  ");
            fmt_subject(g_file.label[face]);
            fmt_str("  PHOTO ");
            fmt_u(g_file.shot[face], 0);
            snd_click();
        } else {
            fmt_str("MYSTERY FACE ");
            fmt_u((u16)(face - g_file.n_train + 1u), 0);
            fmt_str(" OF ");
            fmt_u(g_file.n_query, 0);
            fmt_str(" (KEPT HIDDEN)");
        }
        scr_text(2, fmt_end());
        progress((u16)(face + 1u), n_in_file);
        clock_stop(DEMO_PH_LOAD);
        sys_wait(face < g_file.n_train ? HOLD_SHORT : 4u);
        clock_start();
    }
done:
    disk_close(fd);
    clock_stop(DEMO_PH_LOAD);
    return result;
}

/* ---- title ----------------------------------------------------------- */

void demo_title(void)
{
    u8 shade;

    scr_hue(HUE_TITLE);
    scr_clear();
    /* All sixteen shades of GTIA mode 9, dark to light and back. */
    for (shade = 0; shade < 16u; ++shade) {
        scr_fill((u8)(shade * 5u), 4u, 5u, 14u, (u8)(shade * 4u + 3u));
        scr_fill((u8)(75u - shade * 5u), 22u, 5u, 14u, (u8)(shade * 4u + 3u));
    }
    scr_text(0, "");
    scr_text_at(0, 0, "  E I G E N F A C E S                   ", SCR_INVERSE);
    scr_text(1, "FACE RECOGNITION WITH PRINCIPAL");
    scr_text_at(1, 32, "AXES", 0);
    scr_text(2, "6502 @ 1.79 MHZ  -  OLIVETTI FACES 24X24");
    scr_text(3, "PRESS START");
    snd_chime();
    sys_wait(HOLD_LONG);
}

/* ---- training -------------------------------------------------------- */

/* Step 2: average the faces. The running average of the first few
 * faces is drawn as it goes, so the viewer sees it blur into the mean. */
static void train_mean(void)
{
    u8 n_averaged;
    u8 row;

    step_title(2, "THE AVERAGE FACE");
    scr_text(1, "ADD EVERY FACE TOGETHER, DIVIDE BY TEN");
    /* Running average of the first n_averaged faces, so the viewer sees
     * it blur. */
    for (n_averaged = 1; n_averaged <= g_file.n_train; ++n_averaged) {
        scr_face(SCR_PANEL_A, 0, (const u8 *)ef_centered((u8)(n_averaged - 1u)));
        for (row = 0; row < EF_H; row += BAND) {
            scan_line(SCR_PANEL_B, row);
            ef_mean_of_first(n_averaged, row, BAND, g_image);
            scr_face_rows(SCR_PANEL_B, 0, g_image, (u8)(row + BAND));
        }
        scr_mark((u8)(n_averaged - 1u), 1);
        fmt_begin();
        fmt_u(n_averaged, 2);
        fmt_str(n_averaged == 1u ? " FACE AVERAGED" : " FACES AVERAGED");
        scr_text(2, fmt_end());
        progress(n_averaged, g_file.n_train);
        sys_wait(HOLD_SHORT);
    }
    clock_start();
    ef_compute_mean();
    clock_stop(DEMO_PH_MEAN);
    scr_face(SCR_PANEL_B, 0, ef_mean());
    scr_text(2, "THE MEAN FACE: WHAT ALL FACES SHARE");
    sys_wait(HOLD_STEP);
}

/* Step 3: subtract the mean from every face. The difference is drawn
 * signed: gray where the face matches the average, lighter or darker
 * where it does not. */
static void train_center(void)
{
    u8 face;

    step_title(3, "WHAT MAKES A FACE UNIQUE");
    scr_text(1, "FACE - MEAN  (GRAY = SAME AS AVERAGE)");
    clear_marks();
    for (face = 0; face < g_file.n_train; ++face) {
        clock_start();
        ef_center_step();
        clock_stop(DEMO_PH_CENTER);
        ef_face_pixels(face, g_image);
        scr_face(SCR_PANEL_A, 0, g_image);
        scr_face_signed(SCR_PANEL_C, 0, ef_centered(face), 2);
        if (face != 0u) {
            scr_mark((u8)(face - 1u), 0);
        }
        scr_mark(face, 1);
        fmt_begin();
        fmt_str("FACE ");
        fmt_u0((u16)(face + 1u), 2);
        fmt_str(" = MEAN + DIFFERENCE");
        scr_text(2, fmt_end());
        progress((u16)(face + 1u), g_file.n_train);
        sys_wait(HOLD_SHORT);
    }
    scr_mark((u8)(g_file.n_train - 1u), 0);
}

/* Covariance cell (row, col) in panel B, shaded by C(row,col) /
 * C(row,row): white when the two faces differ from the mean in the same
 * way, black when in opposite ways, mid-gray when unrelated. Drawn on
 * both sides of the diagonal, since C is symmetric. */
static void cov_cell(u8 row, u8 col)
{
    s32 diagonal = ef_cov(row, row);
    s32 entry = ef_cov(row, col);
    s16 shade = 32;

    if (diagonal > 0) {
        /* Shift both down first so entry * 31 cannot overflow. */
        while (diagonal > 0x00FFFFL) {
            diagonal >>= 1;
            entry /= 2;
        }
        shade = (s16)(32 + (entry * 31) / diagonal);
    }
    if (shade < 0) {
        shade = 0;
    } else if (shade > 63) {
        shade = 63;
    }
    scr_fill((u8)(SCR_PANEL_B + 2u + col * 2u), (u8)(2u + row * 2u), 2u, 2u, (u8)shade);
    scr_fill((u8)(SCR_PANEL_B + 2u + row * 2u), (u8)(2u + col * 2u), 2u, 2u, (u8)shade);
}

/* Step 4: the covariance matrix, one dot product per frame, with the
 * two faces being compared on either side of it. */
static void train_covariance(void)
{
    u8 row;
    u8 col;
    u8 more;

    step_title(4, "HOW ALIKE ARE THE FACES?");
    scr_text(1, "COVARIANCE: 55 DOT PRODUCTS OF 576 PX");
    scr_fill(SCR_PANEL_B, 0, 24u, 24u, 0u);
    scr_fill(SCR_PANEL_C, 0, 24u, 24u, 0u);
    do {
        clock_start();
        more = ef_cov_step();
        clock_stop(DEMO_PH_COV);
        row = ef_cov_last_i();
        col = ef_cov_last_j();
        /* The diagonal comes first; a row is shaded once it is known. */
        if (row != col) {
            cov_cell(row, col);
        } else if (row == g_file.n_train - 1u) {
            for (col = 0; col < g_file.n_train; ++col) {
                cov_cell(col, col);
            }
        }
        ef_face_pixels(row, g_image);
        scr_face(SCR_PANEL_A, 0, g_image);
        ef_face_pixels(col, g_image);
        scr_face(SCR_PANEL_C, 0, g_image);
        fmt_begin();
        fmt_str("FACE ");
        fmt_u0((u16)(row + 1u), 2);
        fmt_str(" VS ");
        fmt_u0((u16)(col + 1u), 2);
        fmt_str("  BRIGHT=ALIKE DARK=OPPOSITE");
        scr_text(2, fmt_end());
        progress(ef_cov_done(), ef_cov_total());
        snd_click();
        /* A click fades out over two frame waits. Cut short, it would
         * hiss through the whole of the next dot product. */
        sys_wait_frame();
        sys_wait_frame();
    } while (more == EF_MORE);
    scr_fill(SCR_PANEL_C, 0, 24u, 24u, 0u);
    fmt_begin();
    fmt_str("10X10 MATRIX IN ");
    fmt_seconds(phase_frames(DEMO_PH_COV));
    scr_text(2, fmt_end());
    sys_wait(HOLD_STEP);
}

/* "AXIS 1: 52.2%  TOTAL 52.2%  9 IT": the share of all the variation
 * this eigenvector explains, the running total, and how many power
 * iterations it took. */
static void eigen_summary(u8 comp)
{
    u8 earlier;
    u16 total_permille = 0;

    for (earlier = 0; earlier <= comp; ++earlier) {
        total_permille = (u16)(total_permille + ef_variance_permille(earlier));
    }
    fmt_begin();
    fmt_str("AXIS ");
    fmt_u((u16)(comp + 1u), 0);
    fmt_str(": ");
    fmt_permille(ef_variance_permille(comp));
    fmt_str("  TOTAL ");
    fmt_permille(total_permille);
    fmt_str("  ");
    fmt_u(ef_eigen_iterations_used(comp), 0);
    fmt_str(" IT");
    scr_text(2, fmt_end());
}

/* Step 5: find the eigenvectors. Each frame shows the guess after one
 * more round of "multiply by the matrix, rescale": the bars wobble at
 * first and then settle once the guess has become an eigenvector. */
static void train_eigen(void)
{
    u8 more;
    u8 comp;
    demo_report *report = demo_report_block();

    step_title(5, "MAIN AXES OF VARIATION");
    scr_text(1, "POWER METHOD: MULTIPLY, NORMALIZE, AGAIN");
    scr_hue(HUE_EIGEN);
    do {
        comp = ef_eigen_component();
        clock_start();
        more = ef_eigen_step();
        clock_stop(DEMO_PH_EIGEN);
        ++report->eigen_iters;
        if (ef_eigen_component() != comp) {
            vector_bars(ef_eigen_vector(comp));
            eigen_summary(comp);
            snd_tone((u8)(SND_C5 - comp * 8u), 10);
            progress(ef_eigen_component(), DEMO_K);
            sys_wait(HOLD_STEP);
        } else {
            vector_bars(ef_eigen_current());
            fmt_begin();
            fmt_str("AXIS ");
            fmt_u((u16)(comp + 1u), 0);
            fmt_str(" OF ");
            fmt_u(DEMO_K, 0);
            fmt_str("  ITERATION ");
            fmt_u0(ef_eigen_iteration(), 2);
            scr_text(2, fmt_end());
            sys_wait_frame();
        }
    } while (more == EF_MORE);
}

/* The three most recent eigenfaces, in the three panels. */
static void draw_eigenface(u8 comp)
{
    static const u8 panel[3] = { SCR_PANEL_A, SCR_PANEL_B, SCR_PANEL_C };
    u8 first_shown = comp < 3u ? 0u : (u8)(comp - 2u);
    u8 shown;

    for (shown = first_shown; shown <= comp && shown < first_shown + 3u; ++shown) {
        scr_face_signed(panel[shown - first_shown], 0, ef_eigenface(shown), 2);
    }
}

/* Step 6: blend the centered faces, in the amounts the eigenvector's
 * bars show, into an eigenface; one row of pixels per call. */
static void train_eigenfaces(void)
{
    u8 more;
    u8 comp;

    step_title(6, "EIGENFACES");
    scr_text(1, "EACH AXIS AS A FACE: BLEND OF ALL TEN");
    scr_fill(SCR_PANEL_A, 0, 78u, 24u, 0u);
    do {
        comp = ef_eigenface_component();
        if (ef_eigenface_row() == 0u) {
            vector_bars(ef_eigen_vector(comp));
        }
        clock_start();
        more = ef_eigenface_step();
        clock_stop(DEMO_PH_EIGENFACE);
        progress((u16)(ef_eigenface_component() * EF_H + ef_eigenface_row()),
                 (u16)(DEMO_K * EF_H));
        if (ef_eigenface_component() != comp) {
            draw_eigenface(comp);
            fmt_begin();
            fmt_str("EIGENFACE ");
            fmt_u((u16)(comp + 1u), 0);
            fmt_str(": ");
            fmt_permille(ef_variance_permille(comp));
            fmt_str(" OF ALL VARIATION");
            scr_text(2, fmt_end());
            snd_tone((u8)(SND_C5 - comp * 8u), 10);
            sys_wait(HOLD_STEP);
        }
    } while (more == EF_MORE);
}

/* The weights in whole gray levels, e.g. " +012 -003 +021 +000". */
static void fmt_weights(const s16 *weights)
{
    u8 comp;

    for (comp = 0; comp < DEMO_K; ++comp) {
        fmt_char(' ');
        fmt_s((s16)(weights[comp] / EF_WEIGHT_UNIT), 3);
    }
}

/* Step 7: describe every training face by its four weights (how far it
 * lies along each eigenface), and rebuild it from mean + weights *
 * eigenfaces to show how much of the face four numbers keep. */
static void train_weights(void)
{
    u8 face;
    u8 comp;

    step_title(7, "EVERY FACE AS FOUR NUMBERS");
    scr_text(1, "FACE  =  MEAN  +  W1*E1 + ... + W4*E4");
    clear_marks();
    scr_face(SCR_PANEL_B, 0, ef_mean());
    for (face = 0; face < g_file.n_train; ++face) {
        clock_start();
        ef_weights_step();
        clock_stop(DEMO_PH_WEIGHTS);
        for (comp = 0; comp < DEMO_K; ++comp) {
            g_shown_weights[comp] = ef_weight(face, comp);
        }
        ef_face_pixels(face, g_image);
        scr_face(SCR_PANEL_A, 0, g_image);
        draw_rebuilt(SCR_PANEL_C, g_shown_weights, DEMO_K);
        if (face != 0u) {
            scr_mark((u8)(face - 1u), 0);
        }
        scr_mark(face, 1);
        fmt_begin();
        fmt_str("FACE ");
        fmt_u0((u16)(face + 1u), 2);
        fmt_str(" W=");
        fmt_weights(g_shown_weights);
        scr_text(2, fmt_end());
        progress((u16)(face + 1u), g_file.n_train);
        snd_click();
        sys_wait(HOLD_SHORT);
    }
    scr_mark((u8)(g_file.n_train - 1u), 0);
}

void demo_train(void)
{
    u8 phase;

    train_mean();
    train_center();
    train_covariance();
    train_eigen();
    train_eigenfaces();
    train_weights();

    g_training_frames = 0;
    for (phase = DEMO_PH_MEAN; phase <= DEMO_PH_WEIGHTS; ++phase) {
        g_training_frames = (u16)(g_training_frames + phase_frames(phase));
    }
    demo_report_block()->trained = 1;
    scr_text(0, "");
    scr_text_at(0, 0, " TRAINED ", SCR_INVERSE);
    fmt_begin();
    fmt_str("MATH TOOK ");
    fmt_seconds(g_training_frames);
    fmt_str(" ON A 6502");
    scr_text_at(0, 10, fmt_end(), 0);
    scr_text(1, "ANY FACE NOW BOILS DOWN TO FOUR WEIGHTS");
    scr_text(2, "NEXT: FIVE FACES THE MACHINE HAS NOT SEEN");
    scr_text(3, "PRESS START");
    snd_chime();
    sys_wait(HOLD_LONG);
}

/* ---- matching -------------------------------------------------------- */

static void query_title(u8 query, const char *what)
{
    fmt_begin();
    fmt_str(" MYSTERY FACE ");
    fmt_u((u16)(query + 1u), 0);
    fmt_str(" OF ");
    fmt_u(g_file.n_query, 0);
    fmt_char(' ');
    scr_text(0, "");
    scr_text_at(0, 0, fmt_end(), SCR_INVERSE);
    scr_text_at(0, (u8)(fmt_len() + 1u), what, 0);
}

/* Rebuild the mystery face from the mean plus one more eigenface at a
 * time, using its own weights: it sharpens as eigenfaces are added. */
static void match_reconstruct(void)
{
    u8 n_used;

    for (n_used = 0; n_used < DEMO_K; ++n_used) {
        g_shown_weights[n_used] = ef_query_weight(n_used);
    }
    scr_text(1, "REBUILD IT FROM THE MEAN + EIGENFACES");
    for (n_used = 0; n_used <= DEMO_K; ++n_used) {
        fmt_begin();
        if (n_used == 0u) {
            fmt_str("MEAN FACE ONLY");
        } else {
            fmt_str("+ EIGENFACE ");
            fmt_u(n_used, 0);
            fmt_str(" X ");
            fmt_s((s16)(ef_query_weight((u8)(n_used - 1u)) / EF_WEIGHT_UNIT), 3);
        }
        scr_text(2, fmt_end());
        progress(n_used, DEMO_K);
        draw_rebuilt(SCR_PANEL_B, g_shown_weights, n_used);
        snd_tone((u8)(SND_C4 - n_used * 10u), 6);
        sys_wait(HOLD_STEP);
    }
}

/* Show the distance, in weight space, from the mystery face to every
 * training face, as bars under the thumbnails. */
static void match_scan(void)
{
    u8 face;
    u16 nearest = ef_distance(ef_rank_index(0));
    u16 farthest = ef_distance(ef_rank_index((u8)(g_file.n_train - 1u)));
    u16 dist;
    u8 height;

    scr_text(1, "COMPARE ITS 4 WEIGHTS WITH EVERY FACE");
    for (face = 0; face < g_file.n_train; ++face) {
        dist = ef_distance(face);
        /* The nearest face gets the tallest bar, the farthest the shortest. */
        height = farthest == nearest ? SCR_BAR_H
                 : (u8)(1u + ((u32)(farthest - dist) * (SCR_BAR_H - 1u)) /
                             (farthest - nearest));
        if (face != 0u) {
            scr_mark((u8)(face - 1u), 0);
        }
        scr_mark(face, 1);
        scr_bar(face, height, 40u);
        ef_face_pixels(face, g_image);
        scr_face(SCR_PANEL_C, 0, g_image);
        fmt_begin();
        fmt_str("FACE ");
        fmt_u0((u16)(face + 1u), 2);
        fmt_str("  ");
        fmt_subject(ef_label(face));
        fmt_str("  DIST ");
        fmt_dec1((u16)(((u32)dist * 10u + 2u) / EF_WEIGHT_UNIT));
        scr_text(2, fmt_end());
        progress((u16)(face + 1u), g_file.n_train);
        snd_tone((u8)(40u + height * 12u), 6);
        sys_wait(HOLD_SHORT);
    }
    scr_mark((u8)(g_file.n_train - 1u), 0);
}

/* Recognize one mystery face: work out its four weights, rank the
 * training faces by distance to them, and name the closest face's
 * subject. The result goes on screen and into the report block. */
void demo_match(u8 query)
{
    demo_report *report = demo_report_block();
    demo_query_report *query_report;
    u8 best;
    u8 true_label = g_file.label[g_file.n_train + query];
    u8 correct;
    u8 place;
    u8 blink;
    u16 best_dist;

    /* Title first, then the picture: the screen is never blank under an
     * old title. */
    query_title(query, "WHO IS THIS?");
    scr_hue(HUE_QUERY);
    scr_clear();
    draw_thumbs();
    scr_text(1, "A FACE THE MACHINE HAS NEVER SEEN");
    scr_text(2, "");
    scr_text(3, "");
    reveal(SCR_PANEL_A, g_query_faces[query]);
    sys_wait(HOLD_STEP);

    scr_text(2, "WORKING OUT ITS FOUR WEIGHTS...");
    clock_start();
    ef_query_set(g_query_faces[query]);
    ef_query_project();
    ef_rank();
    clock_stop(DEMO_PH_MATCH);

    match_reconstruct();
    match_scan();

    best = ef_rank_index(0);
    correct = (u8)(ef_label(best) == true_label);
    best_dist = ef_distance(best);
    if (query < EFD_MAX_QUERY) {
        query_report = &report->q[query];
        query_report->label = true_label;
        query_report->best_index = best;
        query_report->best_label = ef_label(best);
        query_report->confidence = ef_confidence();
        query_report->dist_lo = (u8)(best_dist & 0xFFu);
        query_report->dist_hi = (u8)(best_dist >> 8);
    }

    ef_face_pixels(best, g_image);
    scr_face(SCR_PANEL_C, 0, g_image);
    scr_hue(correct ? HUE_RIGHT : HUE_WRONG);
    if (correct) {
        snd_chime();
    } else {
        snd_buzz();
    }

    fmt_begin();
    fmt_str(" MATCH: ");
    fmt_subject(ef_label(best));
    fmt_char(' ');
    scr_text(0, "");
    scr_text_at(0, 0, fmt_end(), SCR_INVERSE);
    fmt_begin();
    fmt_str(correct ? "CORRECT" : "WRONG - IT WAS ");
    if (!correct) {
        fmt_u0(true_label, 2);
    }
    scr_text_at(0, 22, fmt_end(), 0);

    fmt_begin();
    fmt_str("CONFIDENCE ");
    fmt_u(ef_confidence(), 0);
    fmt_str("%  DISTANCE ");
    fmt_dec1((u16)(((u32)best_dist * 10u + 2u) / EF_WEIGHT_UNIT));
    scr_text(1, fmt_end());

    fmt_begin();
    fmt_str("RUNNERS-UP:");
    for (place = 1; place < 4u && place < g_file.n_train; ++place) {
        fmt_str(" S");
        fmt_u0(ef_label(ef_rank_index(place)), 2);
        fmt_char('=');
        fmt_u((u16)((ef_distance(ef_rank_index(place)) + 2u) / EF_WEIGHT_UNIT), 0);
    }
    scr_text(2, fmt_end());
    scr_text(3, "PRESS START FOR THE NEXT FACE");

    report->queries_done = (u8)(report->queries_done + 1u);
    for (blink = 0; blink < 6u; ++blink) {
        scr_mark(best, (u8)(blink & 1u ? 0u : 1u));
        scr_bar(best, SCR_BAR_H, (u8)(blink & 1u ? 40u : 63u));
        sys_wait(10);
    }
    scr_mark(best, 1);
    sys_wait(HOLD_RESULT);
}

void demo_fail(u8 error, u8 detail)
{
    demo_report_block()->error = error;
    scr_hue(HUE_WRONG);
    step_title(1, "SOMETHING WENT WRONG");
    fmt_begin();
    switch (error) {
    case DEMO_ERR_OPEN:   fmt_str("CANNOT OPEN D:FACES.DAT"); break;
    case DEMO_ERR_READ:   fmt_str("FACES.DAT IS TOO SHORT"); break;
    case DEMO_ERR_FORMAT: fmt_str("FACES.DAT IS NOT A FACE FILE"); break;
    default:              fmt_str("MODEL ERROR"); break;
    }
    fmt_str(" (");
    fmt_u(detail, 0);
    fmt_char(')');
    scr_text(1, fmt_end());
    scr_text(2, "REBUILD THE DISK WITH: MAKE ATR");
    scr_text(3, "");
    snd_buzz();
}

void demo_run(void)
{
    u8 query = 0;
    u8 result;

    demo_title();
    result = demo_load();
    if (result != DEMO_OK) {
        demo_fail(result, 0);
        for (;;) {
            sys_wait_frame();
        }
    }
    demo_train();
    for (;;) {
        demo_match(query);
        query = (u8)(query + 1u);
        if (query >= g_file.n_query) {
            query = 0;
        }
    }
}
