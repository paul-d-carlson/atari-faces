/*
 * demo.c -- see demo.h.
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

static efd_info g_info;
static u8 g_meta[2u * EFD_MAX_TOTAL];
static u8 g_query_px[EFD_MAX_QUERY][EF_PIXELS];
static u8 g_tmp[EF_PIXELS];
static s16 g_wtmp[EF_MAX_K];
static u16 g_clock;
static u16 g_train_frames;

demo_report *demo_report_block(void)
{
    return (demo_report *)HW_PTR(DEMO_REPORT_ADDR);
}

/* ---- timing ---------------------------------------------------------- */

static void clock_start(void)
{
    g_clock = sys_ticks();
}

/* Add the frames since clock_start() to a phase. */
static void clock_stop(u8 phase)
{
    demo_report *r = demo_report_block();
    u16 t = (u16)(r->frames[2u * phase] | ((u16)r->frames[2u * phase + 1u] << 8));

    t = (u16)(t + (u16)(sys_ticks() - g_clock));
    r->frames[2u * phase] = (u8)(t & 0xFFu);
    r->frames[2u * phase + 1u] = (u8)(t >> 8);
}

static u16 phase_frames(u8 phase)
{
    demo_report *r = demo_report_block();

    return (u16)(r->frames[2u * phase] | ((u16)r->frames[2u * phase + 1u] << 8));
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
    u8 i;

    for (i = 0; i < g_info.n_train; ++i) {
        ef_face_pixels(i, g_tmp);
        scr_thumb(i, g_tmp);
    }
}

static void clear_marks(void)
{
    u8 i;

    for (i = 0; i < EF_MAX_FACES; ++i) {
        scr_mark(i, 0);
        scr_bar(i, 0, 0);
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
static void reveal(u8 x, const u8 *px)
{
    u8 r;

    for (r = 2; r <= 24u; r += 2u) {
        scr_face_rows(x, 0, px, r);
        if (r < 24u) {
            scan_line(x, r);
        }
        sys_wait_frame();
    }
}

/* Rebuild a face from its weights. */
static void draw_rebuilt(u8 x, const s16 *weights, u8 upto)
{
    u8 r;

    for (r = 0; r < EF_H; r += BAND) {
        scan_line(x, r);
        ef_reconstruct_rows(weights, upto, r, BAND, g_tmp);
        scr_face_rows(x, 0, g_tmp, (u8)(r + BAND));
    }
}

/* Sample-space eigenvector as bars: height |v|, bright when positive. */
static void vector_bars(const s16 *v)
{
    u8 i;
    s16 a;

    for (i = 0; i < g_info.n_train; ++i) {
        a = v[i] < 0 ? (s16)-v[i] : v[i];
        scr_bar(i, (u8)(((u16)a * SCR_BAR_H + EF_VEC_SCALE / 2) / EF_VEC_SCALE),
                v[i] < 0 ? 24u : 63u);
    }
}

/* ---- loading --------------------------------------------------------- */

u8 demo_load(void)
{
    demo_report *r = demo_report_block();
    u8 hdr[EFD_HEADER_LEN];
    u8 i;
    u8 n;
    u8 *slot;
    int fd;
    u8 rc = DEMO_OK;

    memset(r, 0, sizeof(*r));
    r->magic = DEMO_MAGIC;
    r->k = DEMO_K;

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
    if (disk_read(fd, hdr, EFD_HEADER_LEN) != EFD_HEADER_LEN ||
        efd_parse_header(hdr, &g_info) != EFD_OK || g_info.n_query == 0u) {
        rc = DEMO_ERR_FORMAT;
        goto done;
    }
    if (disk_read(fd, g_meta, efd_meta_len(&g_info)) != efd_meta_len(&g_info) ||
        efd_parse_meta(g_meta, &g_info) != EFD_OK) {
        rc = DEMO_ERR_FORMAT;
        goto done;
    }
    if (ef_begin(g_info.n_train, DEMO_K) != EF_DONE) {
        rc = DEMO_ERR_MODEL;
        goto done;
    }
    r->n_train = g_info.n_train;
    r->n_query = g_info.n_query;

    n = (u8)(g_info.n_train + g_info.n_query);
    for (i = 0; i < n; ++i) {
        slot = i < g_info.n_train ? ef_face_slot(i) : g_query_px[i - g_info.n_train];
        if (disk_read(fd, slot, EF_PIXELS) != EF_PIXELS) {
            rc = DEMO_ERR_READ;
            goto done;
        }
        if (efd_check_face(slot) != EFD_OK) {
            rc = DEMO_ERR_FORMAT;
            goto done;
        }
        fmt_begin();
        if (i < g_info.n_train) {
            ef_set_label(i, g_info.label[i]);
            scr_face(SCR_PANEL_A, 0, slot);
            scr_thumb(i, slot);
            fmt_str("FACE ");
            fmt_u0((u16)(i + 1u), 2);
            fmt_char('/');
            fmt_u0(g_info.n_train, 2);
            fmt_str("  ");
            fmt_subject(g_info.label[i]);
            fmt_str("  PHOTO ");
            fmt_u(g_info.shot[i], 0);
            snd_click();
        } else {
            fmt_str("MYSTERY FACE ");
            fmt_u((u16)(i - g_info.n_train + 1u), 0);
            fmt_str(" OF ");
            fmt_u(g_info.n_query, 0);
            fmt_str(" (KEPT HIDDEN)");
        }
        scr_text(2, fmt_end());
        progress((u16)(i + 1u), n);
        clock_stop(DEMO_PH_LOAD);
        sys_wait(i < g_info.n_train ? HOLD_SHORT : 4u);
        clock_start();
    }
done:
    disk_close(fd);
    clock_stop(DEMO_PH_LOAD);
    return rc;
}

/* ---- title ----------------------------------------------------------- */

void demo_title(void)
{
    u8 i;

    scr_hue(HUE_TITLE);
    scr_clear();
    /* All sixteen shades of GTIA mode 9, dark to light and back. */
    for (i = 0; i < 16u; ++i) {
        scr_fill((u8)(i * 5u), 4u, 5u, 14u, (u8)(i * 4u + 3u));
        scr_fill((u8)(75u - i * 5u), 22u, 5u, 14u, (u8)(i * 4u + 3u));
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

static void train_mean(void)
{
    u8 j;
    u8 r;

    step_title(2, "THE AVERAGE FACE");
    scr_text(1, "ADD EVERY FACE TOGETHER, DIVIDE BY TEN");
    /* Running average of the first j faces, so the viewer sees it blur. */
    for (j = 1; j <= g_info.n_train; ++j) {
        scr_face(SCR_PANEL_A, 0, (const u8 *)ef_centered((u8)(j - 1u)));
        for (r = 0; r < EF_H; r += BAND) {
            scan_line(SCR_PANEL_B, r);
            ef_mean_of_first(j, r, BAND, g_tmp);
            scr_face_rows(SCR_PANEL_B, 0, g_tmp, (u8)(r + BAND));
        }
        scr_mark((u8)(j - 1u), 1);
        fmt_begin();
        fmt_u(j, 2);
        fmt_str(j == 1u ? " FACE AVERAGED" : " FACES AVERAGED");
        scr_text(2, fmt_end());
        progress(j, g_info.n_train);
        sys_wait(HOLD_SHORT);
    }
    clock_start();
    ef_compute_mean();
    clock_stop(DEMO_PH_MEAN);
    scr_face(SCR_PANEL_B, 0, ef_mean());
    scr_text(2, "THE MEAN FACE: WHAT ALL FACES SHARE");
    sys_wait(HOLD_STEP);
}

static void train_center(void)
{
    u8 i;

    step_title(3, "WHAT MAKES A FACE UNIQUE");
    scr_text(1, "FACE - MEAN  (GRAY = SAME AS AVERAGE)");
    clear_marks();
    for (i = 0; i < g_info.n_train; ++i) {
        clock_start();
        ef_center_step();
        clock_stop(DEMO_PH_CENTER);
        ef_face_pixels(i, g_tmp);
        scr_face(SCR_PANEL_A, 0, g_tmp);
        scr_face_signed(SCR_PANEL_C, 0, ef_centered(i), 2);
        if (i != 0u) {
            scr_mark((u8)(i - 1u), 0);
        }
        scr_mark(i, 1);
        fmt_begin();
        fmt_str("FACE ");
        fmt_u0((u16)(i + 1u), 2);
        fmt_str(" = MEAN + DIFFERENCE");
        scr_text(2, fmt_end());
        progress((u16)(i + 1u), g_info.n_train);
        sys_wait(HOLD_SHORT);
    }
    scr_mark((u8)(g_info.n_train - 1u), 0);
}

/* Covariance cell (i, j) in panel B, shaded by C(i,j) / C(i,i). */
static void cov_cell(u8 i, u8 j)
{
    s32 d = ef_cov(i, i);
    s32 c = ef_cov(i, j);
    s16 lum = 32;

    if (d > 0) {
        /* Shift both down first so c * 31 cannot overflow. */
        while (d > 0x00FFFFL) {
            d >>= 1;
            c /= 2;
        }
        lum = (s16)(32 + (c * 31) / d);
    }
    if (lum < 0) {
        lum = 0;
    } else if (lum > 63) {
        lum = 63;
    }
    scr_fill((u8)(SCR_PANEL_B + 2u + j * 2u), (u8)(2u + i * 2u), 2u, 2u, (u8)lum);
    scr_fill((u8)(SCR_PANEL_B + 2u + i * 2u), (u8)(2u + j * 2u), 2u, 2u, (u8)lum);
}

static void train_covariance(void)
{
    u8 i;
    u8 j;
    u8 more;

    step_title(4, "HOW ALIKE ARE THE FACES?");
    scr_text(1, "COVARIANCE: 55 DOT PRODUCTS OF 576 PX");
    scr_fill(SCR_PANEL_B, 0, 24u, 24u, 0u);
    scr_fill(SCR_PANEL_C, 0, 24u, 24u, 0u);
    do {
        clock_start();
        more = ef_cov_step();
        clock_stop(DEMO_PH_COV);
        i = ef_cov_last_i();
        j = ef_cov_last_j();
        /* The diagonal comes first; a row is shaded once it is known. */
        if (i != j) {
            cov_cell(i, j);
        } else if (i == g_info.n_train - 1u) {
            for (j = 0; j < g_info.n_train; ++j) {
                cov_cell(j, j);
            }
        }
        ef_face_pixels(i, g_tmp);
        scr_face(SCR_PANEL_A, 0, g_tmp);
        ef_face_pixels(j, g_tmp);
        scr_face(SCR_PANEL_C, 0, g_tmp);
        fmt_begin();
        fmt_str("FACE ");
        fmt_u0((u16)(i + 1u), 2);
        fmt_str(" VS ");
        fmt_u0((u16)(j + 1u), 2);
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

static void eigen_summary(u8 c)
{
    u8 i;
    u16 cum = 0;

    for (i = 0; i <= c; ++i) {
        cum = (u16)(cum + ef_variance_permille(i));
    }
    fmt_begin();
    fmt_str("AXIS ");
    fmt_u((u16)(c + 1u), 0);
    fmt_str(": ");
    fmt_permille(ef_variance_permille(c));
    fmt_str("  TOTAL ");
    fmt_permille(cum);
    fmt_str("  ");
    fmt_u(ef_eigen_iterations_used(c), 0);
    fmt_str(" IT");
    scr_text(2, fmt_end());
}

static void train_eigen(void)
{
    u8 more;
    u8 comp;
    demo_report *r = demo_report_block();

    step_title(5, "MAIN AXES OF VARIATION");
    scr_text(1, "POWER METHOD: MULTIPLY, NORMALIZE, AGAIN");
    scr_hue(HUE_EIGEN);
    do {
        comp = ef_eigen_component();
        clock_start();
        more = ef_eigen_step();
        clock_stop(DEMO_PH_EIGEN);
        ++r->eigen_iters;
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

static void draw_eigenface(u8 c)
{
    static const u8 panel[3] = { SCR_PANEL_A, SCR_PANEL_B, SCR_PANEL_C };
    u8 first = c < 3u ? 0u : (u8)(c - 2u);
    u8 i;

    for (i = first; i <= c && i < first + 3u; ++i) {
        scr_face_signed(panel[i - first], 0, ef_eigenface(i), 2);
    }
}

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

static void fmt_weights(const s16 *w)
{
    u8 c;

    for (c = 0; c < DEMO_K; ++c) {
        fmt_char(' ');
        fmt_s((s16)(w[c] / EF_WEIGHT_UNIT), 3);
    }
}

static void train_weights(void)
{
    u8 i;
    u8 c;

    step_title(7, "EVERY FACE AS FOUR NUMBERS");
    scr_text(1, "FACE  =  MEAN  +  W1*E1 + ... + W4*E4");
    clear_marks();
    scr_face(SCR_PANEL_B, 0, ef_mean());
    for (i = 0; i < g_info.n_train; ++i) {
        clock_start();
        ef_weights_step();
        clock_stop(DEMO_PH_WEIGHTS);
        for (c = 0; c < DEMO_K; ++c) {
            g_wtmp[c] = ef_weight(i, c);
        }
        ef_face_pixels(i, g_tmp);
        scr_face(SCR_PANEL_A, 0, g_tmp);
        draw_rebuilt(SCR_PANEL_C, g_wtmp, DEMO_K);
        if (i != 0u) {
            scr_mark((u8)(i - 1u), 0);
        }
        scr_mark(i, 1);
        fmt_begin();
        fmt_str("FACE ");
        fmt_u0((u16)(i + 1u), 2);
        fmt_str(" W=");
        fmt_weights(g_wtmp);
        scr_text(2, fmt_end());
        progress((u16)(i + 1u), g_info.n_train);
        snd_click();
        sys_wait(HOLD_SHORT);
    }
    scr_mark((u8)(g_info.n_train - 1u), 0);
}

void demo_train(void)
{
    u8 p;

    train_mean();
    train_center();
    train_covariance();
    train_eigen();
    train_eigenfaces();
    train_weights();

    g_train_frames = 0;
    for (p = DEMO_PH_MEAN; p <= DEMO_PH_WEIGHTS; ++p) {
        g_train_frames = (u16)(g_train_frames + phase_frames(p));
    }
    demo_report_block()->trained = 1;
    scr_text(0, "");
    scr_text_at(0, 0, " TRAINED ", SCR_INVERSE);
    fmt_begin();
    fmt_str("MATH TOOK ");
    fmt_seconds(g_train_frames);
    fmt_str(" ON A 6502");
    scr_text_at(0, 10, fmt_end(), 0);
    scr_text(1, "ANY FACE NOW BOILS DOWN TO FOUR WEIGHTS");
    scr_text(2, "NEXT: FIVE FACES THE MACHINE HAS NOT SEEN");
    scr_text(3, "PRESS START");
    snd_chime();
    sys_wait(HOLD_LONG);
}

/* ---- matching -------------------------------------------------------- */

static void query_title(u8 q, const char *what)
{
    fmt_begin();
    fmt_str(" MYSTERY FACE ");
    fmt_u((u16)(q + 1u), 0);
    fmt_str(" OF ");
    fmt_u(g_info.n_query, 0);
    fmt_char(' ');
    scr_text(0, "");
    scr_text_at(0, 0, fmt_end(), SCR_INVERSE);
    scr_text_at(0, (u8)(fmt_len() + 1u), what, 0);
}

static void match_reconstruct(void)
{
    u8 u;

    for (u = 0; u < DEMO_K; ++u) {
        g_wtmp[u] = ef_query_weight(u);
    }
    scr_text(1, "REBUILD IT FROM THE MEAN + EIGENFACES");
    for (u = 0; u <= DEMO_K; ++u) {
        fmt_begin();
        if (u == 0u) {
            fmt_str("MEAN FACE ONLY");
        } else {
            fmt_str("+ EIGENFACE ");
            fmt_u(u, 0);
            fmt_str(" X ");
            fmt_s((s16)(ef_query_weight((u8)(u - 1u)) / EF_WEIGHT_UNIT), 3);
        }
        scr_text(2, fmt_end());
        progress(u, DEMO_K);
        draw_rebuilt(SCR_PANEL_B, g_wtmp, u);
        snd_tone((u8)(SND_C4 - u * 10u), 6);
        sys_wait(HOLD_STEP);
    }
}

static void match_scan(void)
{
    u8 i;
    u16 dmin = ef_distance(ef_rank_index(0));
    u16 dmax = ef_distance(ef_rank_index((u8)(g_info.n_train - 1u)));
    u16 d;
    u8 h;

    scr_text(1, "COMPARE ITS 4 WEIGHTS WITH EVERY FACE");
    for (i = 0; i < g_info.n_train; ++i) {
        d = ef_distance(i);
        h = dmax == dmin ? SCR_BAR_H
                         : (u8)(1u + ((u32)(dmax - d) * (SCR_BAR_H - 1u)) / (dmax - dmin));
        if (i != 0u) {
            scr_mark((u8)(i - 1u), 0);
        }
        scr_mark(i, 1);
        scr_bar(i, h, 40u);
        ef_face_pixels(i, g_tmp);
        scr_face(SCR_PANEL_C, 0, g_tmp);
        fmt_begin();
        fmt_str("FACE ");
        fmt_u0((u16)(i + 1u), 2);
        fmt_str("  ");
        fmt_subject(ef_label(i));
        fmt_str("  DIST ");
        fmt_dec1((u16)(((u32)d * 10u + 2u) / EF_WEIGHT_UNIT));
        scr_text(2, fmt_end());
        progress((u16)(i + 1u), g_info.n_train);
        snd_tone((u8)(40u + h * 12u), 6);
        sys_wait(HOLD_SHORT);
    }
    scr_mark((u8)(g_info.n_train - 1u), 0);
}

void demo_match(u8 q)
{
    demo_report *r = demo_report_block();
    demo_query_report *qr;
    u8 best;
    u8 truth = g_info.label[g_info.n_train + q];
    u8 ok;
    u8 i;
    u8 blink;
    u16 d;

    /* Title first, then the picture: the screen is never blank under an
     * old title. */
    query_title(q, "WHO IS THIS?");
    scr_hue(HUE_QUERY);
    scr_clear();
    draw_thumbs();
    scr_text(1, "A FACE THE MACHINE HAS NEVER SEEN");
    scr_text(2, "");
    scr_text(3, "");
    reveal(SCR_PANEL_A, g_query_px[q]);
    sys_wait(HOLD_STEP);

    scr_text(2, "WORKING OUT ITS FOUR WEIGHTS...");
    clock_start();
    ef_query_set(g_query_px[q]);
    ef_query_project();
    ef_rank();
    clock_stop(DEMO_PH_MATCH);

    match_reconstruct();
    match_scan();

    best = ef_rank_index(0);
    ok = (u8)(ef_label(best) == truth);
    d = ef_distance(best);
    if (q < EFD_MAX_QUERY) {
        qr = &r->q[q];
        qr->label = truth;
        qr->best_index = best;
        qr->best_label = ef_label(best);
        qr->confidence = ef_confidence();
        qr->dist_lo = (u8)(d & 0xFFu);
        qr->dist_hi = (u8)(d >> 8);
    }

    ef_face_pixels(best, g_tmp);
    scr_face(SCR_PANEL_C, 0, g_tmp);
    scr_hue(ok ? HUE_RIGHT : HUE_WRONG);
    if (ok) {
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
    fmt_str(ok ? "CORRECT" : "WRONG - IT WAS ");
    if (!ok) {
        fmt_u0(truth, 2);
    }
    scr_text_at(0, 22, fmt_end(), 0);

    fmt_begin();
    fmt_str("CONFIDENCE ");
    fmt_u(ef_confidence(), 0);
    fmt_str("%  DISTANCE ");
    fmt_dec1((u16)(((u32)d * 10u + 2u) / EF_WEIGHT_UNIT));
    scr_text(1, fmt_end());

    fmt_begin();
    fmt_str("RUNNERS-UP:");
    for (i = 1; i < 4u && i < g_info.n_train; ++i) {
        fmt_str(" S");
        fmt_u0(ef_label(ef_rank_index(i)), 2);
        fmt_char('=');
        fmt_u((u16)((ef_distance(ef_rank_index(i)) + 2u) / EF_WEIGHT_UNIT), 0);
    }
    scr_text(2, fmt_end());
    scr_text(3, "PRESS START FOR THE NEXT FACE");

    r->queries_done = (u8)(r->queries_done + 1u);
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
    u8 q = 0;
    u8 rc;

    demo_title();
    rc = demo_load();
    if (rc != DEMO_OK) {
        demo_fail(rc, 0);
        for (;;) {
            sys_wait_frame();
        }
    }
    demo_train();
    for (;;) {
        demo_match(q);
        q = (u8)(q + 1u);
        if (q >= g_info.n_query) {
            q = 0;
        }
    }
}
