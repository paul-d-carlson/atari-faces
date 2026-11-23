/*
 * screen.c -- see screen.h.
 */
#include <string.h>

#include "screen.h"
#include "hw.h"
#include "sys.h"

/* Fixed-address locals: much faster than cc65's software stack in the
 * per-pixel loops below (see ef_kernel.c). Nothing here is recursive. */
#ifdef __CC65__
#pragma static-locals (on)
#endif

/*
 * Display-list interrupt. It runs on the last blank line above the
 * picture and on the last picture line. Rather than toggling state, it
 * looks at the current scan line, so it can never get out of step (for
 * example while the OS skips its vertical-blank work during disk I/O).
 */
const u8 scr_dli_code[SCR_DLI_LEN] = {
    0x48,             /* PHA                                      */
    0xAD, 0x0B, 0xD4, /* LDA VCOUNT     scan line / 2             */
    0xC9, 0x40,       /* CMP #$40       above scan line 128?      */
    0xA9, 0x40,       /* LDA #$40       yes: GTIA mode 9 on       */
    0x90, 0x02,       /* BCC +2                                   */
    0xA9, 0x00,       /* LDA #$00       no: normal text           */
    0x8D, 0x0A, 0xD4, /* STA WSYNC      wait for the line to end  */
    0x8D, 0x1B, 0xD0, /* STA PRIOR                                */
    0x68,             /* PLA                                      */
    0x40              /* RTI                                      */
};

#define DL_BLANK8   0x70u
#define DL_DLI      0x80u
#define DL_LMS      0x40u
#define DL_MODE_F   0x0Fu   /* 1 scan line, 40 bytes (GTIA modes use F) */
#define DL_MODE_2   0x02u   /* 40-column text */
#define DL_JVB      0x41u

/* GTIA mode 9 has 16 shades, so a 0..63 brightness keeps its top four
 * bits. Each screen byte holds two pixels: the left one in the high
 * nibble, the right one in the low nibble. */
#define LUM_TO_NIBBLE(v) ((u8)(((v) >> 2) & 0x0Fu))

/* First byte of picture row y: y * 40, as shifts and an add. */
static u8 *row_ptr(u8 y)
{
    return HW_PTR(SCR_GFX) + (((u16)y << 5) + ((u16)y << 3));
}

/* ATASCII (what the strings hold) to the internal character codes the
 * screen memory uses; bit 7 (inverse video) passes through. */
static u8 to_screen_code(u8 ch)
{
    u8 inverse = (u8)(ch & 0x80u);

    ch &= 0x7Fu;
    if (ch < 32u) {
        ch = (u8)(ch + 64u);
    } else if (ch < 96u) {
        ch = (u8)(ch - 32u);
    }
    return (u8)(ch | inverse);
}

/* The display list is ANTIC's program for drawing the screen, one
 * instruction per group of scan lines. */
void scr_build_dlist(u8 *dl)
{
    u8 row;
    u8 repeat;
    u16 addr;

    *dl++ = DL_BLANK8;
    *dl++ = DL_BLANK8;
    *dl++ = DL_BLANK8 | DL_DLI;
    for (row = 0; row < SCR_H; ++row) {
        addr = (u16)(SCR_GFX + (u16)row * SCR_ROW_BYTES);
        for (repeat = 0; repeat < 4u; ++repeat) {
            *dl++ = (u8)(DL_LMS | DL_MODE_F |
                         ((row == SCR_H - 1u && repeat == 3u) ? DL_DLI : 0u));
            *dl++ = (u8)(addr & 0xFFu);
            *dl++ = (u8)(addr >> 8);
        }
    }
    *dl++ = DL_LMS | DL_MODE_2;
    *dl++ = (u8)(SCR_TEXT & 0xFFu);
    *dl++ = (u8)(SCR_TEXT >> 8);
    *dl++ = DL_MODE_2;
    *dl++ = DL_MODE_2;
    *dl++ = DL_MODE_2;
    *dl++ = DL_JVB;
    *dl++ = (u8)(SCR_DL & 0xFFu);
    *dl = (u8)(SCR_DL >> 8);
}

void scr_init(void)
{
    u16 dli = HW_ADDR(scr_dli_code);

    HW(NMIEN) = 0x40;          /* vertical blank only while rebuilding */
    HW(SDMCTL) = 0;
    sys_wait_frame();
    scr_clear();
    scr_clear_text();
    scr_build_dlist(HW_PTR(SCR_DL));
    HW(VDSLST) = (u8)(dli & 0xFFu);
    HW(VDSLST + 1u) = (u8)(dli >> 8);
    HW(GPRIOR) = 0x40;
    HW(COLOR4) = 0x00;
    HW(COLOR2) = 0x00;
    HW(COLOR1) = 0x0C;
    HW(SDLSTL) = (u8)(SCR_DL & 0xFFu);
    HW(SDLSTH) = (u8)(SCR_DL >> 8);
    HW(SDMCTL) = 0x22;         /* normal-width playfield, display list on */
    sys_wait_frame();
    HW(NMIEN) = 0xC0;          /* display-list interrupts + vertical blank */
}

void scr_clear(void)
{
    memset(HW_PTR(SCR_GFX), 0, (u16)SCR_H * SCR_ROW_BYTES);
}

void scr_clear_text(void)
{
    memset(HW_PTR(SCR_TEXT), 0, (u16)SCR_LINES * SCR_COLS);
}

void scr_hue(u8 hue)
{
    HW(COLOR4) = (u8)(hue << 4);
}

void scr_pixel(u8 x, u8 y, u8 shade)
{
    u8 *dst;
    u8 nibble = LUM_TO_NIBBLE(shade);

    if (x >= SCR_W || y >= SCR_H) {
        return;
    }
    dst = row_ptr(y) + (x >> 1);
    if (x & 1u) {
        *dst = (u8)((*dst & 0xF0u) | nibble);
    } else {
        *dst = (u8)((*dst & 0x0Fu) | (u8)(nibble << 4));
    }
}

void scr_fill(u8 x, u8 y, u8 width, u8 height, u8 shade)
{
    u8 dx;
    u8 dy;

    for (dy = 0; dy < height; ++dy) {
        for (dx = 0; dx < width; ++dx) {
            scr_pixel((u8)(x + dx), (u8)(y + dy), shade);
        }
    }
}

void scr_face_rows(u8 x, u8 y, const u8 *pixels, u8 rows)
{
    register u8 *dst;
    register const u8 *src = pixels;
    u8 row;
    u8 pair;

    for (row = 0; row < rows && (u8)(y + row) < SCR_H; ++row) {
        dst = row_ptr((u8)(y + row)) + (x >> 1);
        /* Two pixels per byte: (left >> 2) << 4 | (right >> 2). */
        for (pair = 0; pair < 12u; ++pair) {
            *dst++ = (u8)(((src[0] & 0x3Cu) << 2) | ((src[1] >> 2) & 0x0Fu));
            src += 2;
        }
    }
}

void scr_face(u8 x, u8 y, const u8 *pixels)
{
    scr_face_rows(x, y, pixels, 24u);
}

/* Brightness nibble for a signed value: 0 is mid-gray, +-64/contrast
 * is white/black. */
static u8 signed_nibble(s8 value, u8 contrast)
{
    s16 shade = (s16)(value * contrast + 64);

    if (shade < 0) {
        return 0;
    }
    return (u8)(shade > 127 ? 15 : shade >> 3);
}

void scr_face_signed(u8 x, u8 y, const s8 *values, u8 contrast)
{
    u8 *dst;
    u8 row;
    u8 pair;

    for (row = 0; row < 24u && (u8)(y + row) < SCR_H; ++row) {
        dst = row_ptr((u8)(y + row)) + (x >> 1);
        for (pair = 0; pair < 12u; ++pair) {
            *dst++ = (u8)((signed_nibble(values[0], contrast) << 4) |
                          signed_nibble(values[1], contrast));
            values += 2;
        }
    }
}

/* Average of the 3 x 3 block of face pixels behind one thumbnail pixel. */
static u8 block_avg(const u8 *pixels, u8 thumb_x, u8 thumb_y)
{
    const u8 *src = pixels + (u16)thumb_y * 72u + (u16)thumb_x * 3u;
    u16 sum;

    sum  = (u16)src[0] + src[1] + src[2];
    sum += (u16)src[24] + src[25] + src[26];
    sum += (u16)src[48] + src[49] + src[50];
    return (u8)(sum / 9u);
}

void scr_thumb(u8 position, const u8 *pixels)
{
    u8 thumb_x;
    u8 thumb_y;
    u8 *dst;

    if (position >= 10u) {
        return;
    }
    for (thumb_y = 0; thumb_y < 8u; ++thumb_y) {
        dst = row_ptr((u8)(SCR_THUMB_Y + thumb_y)) + position * 4u;
        for (thumb_x = 0; thumb_x < 8u; thumb_x += 2u) {
            *dst++ = (u8)((LUM_TO_NIBBLE(block_avg(pixels, thumb_x, thumb_y)) << 4) |
                          LUM_TO_NIBBLE(block_avg(pixels, (u8)(thumb_x + 1u), thumb_y)));
        }
    }
}

void scr_mark(u8 position, u8 on)
{
    scr_fill((u8)(position * 8u + 1u), SCR_MARK_Y, 6u, 1u, on ? 63u : 0u);
}

void scr_bar(u8 position, u8 height, u8 shade)
{
    u8 x = (u8)(position * 8u + 2u);

    if (height > SCR_BAR_H) {
        height = SCR_BAR_H;
    }
    scr_fill(x, SCR_BAR_Y, 4u, (u8)(SCR_BAR_H - height), 0u);
    scr_fill(x, (u8)(SCR_BAR_Y + SCR_BAR_H - height), 4u, height, shade);
}

void scr_text_at(u8 line, u8 col, const char *text, u8 inverse)
{
    u8 *dst;

    if (line >= SCR_LINES) {
        return;
    }
    dst = HW_PTR(SCR_TEXT) + (u16)line * SCR_COLS;
    while (*text != '\0' && col < SCR_COLS) {
        dst[col] = (u8)(to_screen_code((u8)*text) | inverse);
        ++col;
        ++text;
    }
}

void scr_text(u8 line, const char *text)
{
    if (line >= SCR_LINES) {
        return;
    }
    memset(HW_PTR(SCR_TEXT) + (u16)line * SCR_COLS, 0, SCR_COLS);
    scr_text_at(line, 0, text, 0);
}

void scr_progress(u8 line, u8 col, u8 width, u16 done, u16 total)
{
    u8 *dst;
    u8 filled;
    u8 cell;

    if (line >= SCR_LINES || total == 0u) {
        return;
    }
    if (done > total) {
        done = total;
    }
    filled = (u8)(((u32)done * width) / total);
    dst = HW_PTR(SCR_TEXT) + (u16)line * SCR_COLS + col;
    for (cell = 0; cell < width && (u8)(col + cell) < SCR_COLS; ++cell) {
        /* Inverse space when filled, a thin line (ATASCII $12) otherwise. */
        dst[cell] = cell < filled ? SCR_INVERSE : 0x52u;
    }
}
