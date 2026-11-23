/*
 * test_screen.c -- display list, text, drawing. Runs on the host (fake
 * memory) and on the Atari (real memory, real display).
 */
#include <string.h>

#include "tf.h"
#include "hw.h"
#include "screen.h"

#define EF_PIXELS_TEST 576u

static u8 g_dl[SCR_DL_LEN];
static u8 g_px[EF_PIXELS_TEST];

static u8 *gfx(u8 x, u8 y)
{
    return HW_PTR(SCR_GFX) + (u16)y * SCR_ROW_BYTES + (x >> 1);
}

static u8 *text(u8 line, u8 col)
{
    return HW_PTR(SCR_TEXT) + (u16)line * SCR_COLS + col;
}

static void test_layout_fits_memory(void)
{
    /* Picture ends exactly at MEMTOP + 1 with BASIC off. */
    TF_EQ(SCR_GFX + SCR_H * SCR_ROW_BYTES, 0xBC20u);
    TF_EQ(SCR_RESERVED, 0xBC20u - SCR_GFX);
    /* Text and display list in the OS screen area, below $C000. */
    TF_LE(SCR_TEXT + SCR_LINES * SCR_COLS, SCR_DL);
    TF_LE(SCR_DL + SCR_DL_LEN, 0xC000u);
    /* ANTIC cannot read a display list across a 1K boundary. */
    TF_EQ(SCR_DL & 0xFC00u, (SCR_DL + SCR_DL_LEN - 1u) & 0xFC00u);
    /* Nor text across a 4K boundary. */
    TF_EQ(SCR_TEXT & 0xF000u, (SCR_TEXT + 159u) & 0xF000u);
}

static void test_display_list(void)
{
    u8 r;
    u8 rep;
    u16 i = 3;
    u16 a;
    u8 dli = 0;

    scr_build_dlist(g_dl);
    TF_EQ(g_dl[0], 0x70);
    TF_EQ(g_dl[1], 0x70);
    TF_EQ(g_dl[2], 0xF0);                /* interrupt: GTIA mode 9 on */
    for (r = 0; r < SCR_H; ++r) {
        a = (u16)(SCR_GFX + (u16)r * SCR_ROW_BYTES);
        for (rep = 0; rep < 4u; ++rep) {
            TF_EQ(g_dl[i] & 0x7Fu, 0x4F);
            if (g_dl[i] & 0x80u) {
                ++dli;
            }
            TF_EQ(g_dl[i + 1u], a & 0xFFu);
            TF_EQ(g_dl[i + 2u], a >> 8);
            i += 3u;
        }
    }
    TF_EQ(dli, 1);
    TF_EQ(g_dl[i - 3u], 0xCF);           /* interrupt: back to text */
    TF_EQ(g_dl[i], 0x42);
    TF_EQ(g_dl[i + 1u], SCR_TEXT & 0xFFu);
    TF_EQ(g_dl[i + 2u], SCR_TEXT >> 8);
    TF_EQ(g_dl[i + 3u], 0x02);
    TF_EQ(g_dl[i + 5u], 0x02);
    TF_EQ(g_dl[i + 6u], 0x41);
    TF_EQ(g_dl[i + 7u], SCR_DL & 0xFFu);
    TF_EQ(g_dl[i + 8u], SCR_DL >> 8);
    TF_EQ(i + 9u, SCR_DL_LEN);
}

static void test_dli_code(void)
{
    /* LDA VCOUNT / STA WSYNC / STA PRIOR / RTI */
    TF_EQ(scr_dli_code[1], 0xAD);
    TF_EQ(scr_dli_code[2] | (scr_dli_code[3] << 8), VCOUNT);
    TF_EQ(scr_dli_code[13] | (scr_dli_code[14] << 8), WSYNC);
    TF_EQ(scr_dli_code[16] | (scr_dli_code[17] << 8), PRIOR);
    TF_EQ(scr_dli_code[SCR_DLI_LEN - 1u], 0x40);
}

static void test_init(void)
{
    u16 i;

    memset(HW_PTR(SCR_GFX), 0x55, 100);
    memset(HW_PTR(SCR_TEXT), 0x55, 100);
    scr_init();
    scr_build_dlist(g_dl);
    TF_EQ(memcmp(HW_PTR(SCR_DL), g_dl, SCR_DL_LEN), 0);
    TF_EQ(HW(SDLSTL) | (HW(SDLSTH) << 8), SCR_DL);
    TF_EQ(HW(SDMCTL), 0x22);
    TF_EQ(HW(GPRIOR), 0x40);
#ifdef __CC65__
    TF_EQ(HW(VDSLST) | (HW(VDSLST + 1u) << 8), (u16)scr_dli_code);
#else
    TF_EQ(HW(NMIEN), 0xC0);
#endif
    for (i = 0; i < 100u; ++i) {
        TF_EQ(HW_PTR(SCR_GFX)[i], 0);
        TF_EQ(HW_PTR(SCR_TEXT)[i], 0);
    }
}

static void test_hue(void)
{
    scr_hue(9);
    TF_EQ(HW(COLOR4), 0x90);
    scr_hue(0);
    TF_EQ(HW(COLOR4), 0);
}

static void test_text_codes(void)
{
    scr_text(1, "A 0a\x01");
    TF_EQ(*text(1, 0), 0x21);            /* 'A' */
    TF_EQ(*text(1, 1), 0x00);            /* space */
    TF_EQ(*text(1, 2), 0x10);            /* '0' */
    TF_EQ(*text(1, 3), 0x61);            /* 'a' is unchanged */
    TF_EQ(*text(1, 4), 0x41);            /* control character */
    TF_EQ(*text(1, 5), 0x00);            /* padded */
    TF_EQ(*text(1, 39), 0x00);
}

static void test_text_inverse_and_clip(void)
{
    scr_text(2, "");
    scr_text_at(2, 38, "XYZ", SCR_INVERSE);
    TF_EQ(*text(2, 38), 0x38 | 0x80);
    TF_EQ(*text(2, 39), 0x39 | 0x80);
    TF_EQ(*text(3, 0), 0x00);            /* Z did not spill onto line 3 */
    *text(3, 0) = 0x11;
    scr_text(4, "NO");                   /* no such line */
    scr_text_at(9, 0, "NO", 0);
    TF_EQ(*text(3, 0), 0x11);
    scr_clear_text();
    TF_EQ(*text(3, 0), 0x00);
}

static void test_progress(void)
{
    scr_progress(3, 0, 10, 3, 10);
    TF_EQ(*text(3, 2), 0x80);
    TF_EQ(*text(3, 3), 0x52);
    TF_EQ(*text(3, 9), 0x52);
    scr_progress(3, 0, 10, 50, 10);      /* more than total: full */
    TF_EQ(*text(3, 9), 0x80);
    *text(3, 0) = 0x11;
    scr_progress(3, 0, 10, 1, 0);        /* nothing to show */
    scr_progress(5, 0, 10, 1, 1);
    TF_EQ(*text(3, 0), 0x11);
    scr_progress(3, 35, 10, 10, 10);     /* clipped at column 40 */
    TF_EQ(*text(3, 39), 0x80);
}

static void test_pixels(void)
{
    scr_clear();
    scr_pixel(4, 2, 63);
    scr_pixel(5, 2, 20);
    TF_EQ(*gfx(4, 2), 0xF5);
    scr_pixel(4, 2, 0);
    TF_EQ(*gfx(4, 2), 0x05);
    scr_pixel(SCR_W, 0, 63);             /* off screen: ignored */
    scr_pixel(0, SCR_H, 63);
    TF_EQ(*gfx(0, 0), 0);
    scr_fill(10, 5, 3, 2, 40);
    TF_EQ(*gfx(10, 5), 0xAA);
    TF_EQ(*gfx(12, 6), 0xA0);
    TF_EQ(*gfx(10, 7), 0x00);
}

static void test_face(void)
{
    u16 p;

    for (p = 0; p < EF_PIXELS_TEST; ++p) {
        g_px[p] = (u8)((p & 1u) ? 4 : 63);
    }
    scr_clear();
    scr_face(SCR_PANEL_B, 0, g_px);
    TF_EQ(*gfx(SCR_PANEL_B, 0), 0xF1);
    TF_EQ(*gfx(SCR_PANEL_B + 22u, 23), 0xF1);
    TF_EQ(*gfx(SCR_PANEL_B + 24u, 0), 0);
    TF_EQ(*gfx(SCR_PANEL_B - 2u, 0), 0);
    TF_EQ(*gfx(SCR_PANEL_B, 24), 0);

    scr_clear();
    scr_face_rows(SCR_PANEL_A, 0, g_px, 3);
    TF_EQ(*gfx(SCR_PANEL_A, 2), 0xF1);
    TF_EQ(*gfx(SCR_PANEL_A, 3), 0);
    scr_face(SCR_PANEL_A, 30, g_px);     /* rows past 39 are dropped */
    TF_EQ(*gfx(SCR_PANEL_A, 39), 0xF1);
}

static void test_face_signed(void)
{
    static s8 v[EF_PIXELS_TEST];

    v[0] = 0;
    v[1] = 63;
    v[2] = -63;
    v[3] = -100;                         /* out of range: clamps to black */
    v[4] = 100;                          /* and white */
    v[5] = 8;
    scr_face_signed(SCR_PANEL_C, 0, v, 1);
    TF_EQ(*gfx(SCR_PANEL_C, 0), 0x8F);       /* 0 -> 8, 63 -> 15 */
    TF_EQ(*gfx(SCR_PANEL_C + 2u, 0), 0x00);  /* -63 -> 0, -100 -> 0 */
    TF_EQ(*gfx(SCR_PANEL_C + 4u, 0), 0xF9);  /* 100 -> 15, 8 -> 9 */
    scr_face_signed(SCR_PANEL_C, 0, v, 2);
    TF_EQ(*gfx(SCR_PANEL_C + 4u, 0), 0xFA);  /* 8 * 2 -> 10 */
    TF_EQ(*gfx(SCR_PANEL_C, 0), 0x8F);       /* 63 * 2 clamps to white */
}

static void test_thumb(void)
{
    u16 p;

    for (p = 0; p < EF_PIXELS_TEST; ++p) {
        g_px[p] = (u8)(p < 72u ? 40 : 8);   /* first three rows bright */
    }
    scr_clear();
    scr_thumb(3, g_px);
    TF_EQ(*gfx(24, SCR_THUMB_Y), 0xAA);
    TF_EQ(*gfx(30, SCR_THUMB_Y), 0xAA);
    TF_EQ(*gfx(24, SCR_THUMB_Y + 1u), 0x22);
    TF_EQ(*gfx(22, SCR_THUMB_Y), 0);
    TF_EQ(*gfx(32, SCR_THUMB_Y), 0);
    scr_thumb(10, g_px);                 /* no slot 10 */
    TF_EQ(*gfx(0, SCR_THUMB_Y + 8u), 0);
}

static void test_mark_and_bar(void)
{
    scr_clear();
    scr_mark(2, 1);
    TF_EQ(*gfx(16, SCR_MARK_Y), 0x0F);
    TF_EQ(*gfx(18, SCR_MARK_Y), 0xFF);
    TF_EQ(*gfx(22, SCR_MARK_Y), 0xF0);
    scr_mark(2, 0);
    TF_EQ(*gfx(18, SCR_MARK_Y), 0);
    scr_bar(1, 2, 63);
    TF_EQ(*gfx(10, SCR_BAR_Y + SCR_BAR_H - 1u), 0xFF);
    TF_EQ(*gfx(10, SCR_BAR_Y + SCR_BAR_H - 2u), 0xFF);
    TF_EQ(*gfx(10, SCR_BAR_Y + SCR_BAR_H - 3u), 0);
    scr_bar(1, 99, 63);                  /* clamped to full height */
    TF_EQ(*gfx(10, SCR_BAR_Y), 0xFF);
    scr_bar(1, 0, 63);
    TF_EQ(*gfx(10, SCR_BAR_Y + SCR_BAR_H - 1u), 0);
}

static const tf_case screen_cases[] = {
    TF_CASE(test_layout_fits_memory),
    TF_CASE(test_display_list),
    TF_CASE(test_dli_code),
    TF_CASE(test_init),
    TF_CASE(test_hue),
    TF_CASE(test_text_codes),
    TF_CASE(test_text_inverse_and_clip),
    TF_CASE(test_progress),
    TF_CASE(test_pixels),
    TF_CASE(test_face),
    TF_CASE(test_face_signed),
    TF_CASE(test_thumb),
    TF_CASE(test_mark_and_bar),
    TF_END
};

const tf_suite suite_screen = { "screen", screen_cases };
