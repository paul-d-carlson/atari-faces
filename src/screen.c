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

#define LUM_TO_NIBBLE(v) ((u8)(((v) >> 2) & 0x0Fu))

static u8 *row_ptr(u8 y)
{
    return HW_PTR(SCR_GFX) + (((u16)y << 5) + ((u16)y << 3));
}

static u8 to_screen_code(u8 c)
{
    u8 inv = (u8)(c & 0x80u);

    c &= 0x7Fu;
    if (c < 32u) {
        c = (u8)(c + 64u);
    } else if (c < 96u) {
        c = (u8)(c - 32u);
    }
    return (u8)(c | inv);
}

void scr_build_dlist(u8 *dl)
{
    u8 r;
    u8 rep;
    u16 a;

    *dl++ = DL_BLANK8;
    *dl++ = DL_BLANK8;
    *dl++ = DL_BLANK8 | DL_DLI;
    for (r = 0; r < SCR_H; ++r) {
        a = (u16)(SCR_GFX + (u16)r * SCR_ROW_BYTES);
        for (rep = 0; rep < 4u; ++rep) {
            *dl++ = (u8)(DL_LMS | DL_MODE_F |
                         ((r == SCR_H - 1u && rep == 3u) ? DL_DLI : 0u));
            *dl++ = (u8)(a & 0xFFu);
            *dl++ = (u8)(a >> 8);
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

void scr_pixel(u8 x, u8 y, u8 lum)
{
    u8 *d;
    u8 n = LUM_TO_NIBBLE(lum);

    if (x >= SCR_W || y >= SCR_H) {
        return;
    }
    d = row_ptr(y) + (x >> 1);
    if (x & 1u) {
        *d = (u8)((*d & 0xF0u) | n);
    } else {
        *d = (u8)((*d & 0x0Fu) | (u8)(n << 4));
    }
}

void scr_fill(u8 x, u8 y, u8 w, u8 h, u8 lum)
{
    u8 i;
    u8 j;

    for (j = 0; j < h; ++j) {
        for (i = 0; i < w; ++i) {
            scr_pixel((u8)(x + i), (u8)(y + j), lum);
        }
    }
}

void scr_face_rows(u8 x, u8 y, const u8 *px, u8 rows)
{
    register u8 *d;
    register const u8 *s = px;
    u8 r;
    u8 c;

    for (r = 0; r < rows && (u8)(y + r) < SCR_H; ++r) {
        d = row_ptr((u8)(y + r)) + (x >> 1);
        for (c = 0; c < 12u; ++c) {
            *d++ = (u8)(((s[0] & 0x3Cu) << 2) | ((s[1] >> 2) & 0x0Fu));
            s += 2;
        }
    }
}

void scr_face(u8 x, u8 y, const u8 *px)
{
    scr_face_rows(x, y, px, 24u);
}

/* Brightness nibble for a signed value: 0 is mid-gray, +-64/contrast
 * is white/black. */
static u8 signed_nibble(s8 v, u8 contrast)
{
    s16 t = (s16)(v * contrast + 64);

    if (t < 0) {
        return 0;
    }
    return (u8)(t > 127 ? 15 : t >> 3);
}

void scr_face_signed(u8 x, u8 y, const s8 *v, u8 contrast)
{
    u8 *d;
    u8 r;
    u8 c;

    for (r = 0; r < 24u && (u8)(y + r) < SCR_H; ++r) {
        d = row_ptr((u8)(y + r)) + (x >> 1);
        for (c = 0; c < 12u; ++c) {
            *d++ = (u8)((signed_nibble(v[0], contrast) << 4) |
                        signed_nibble(v[1], contrast));
            v += 2;
        }
    }
}

static u8 block_avg(const u8 *px, u8 tx, u8 ty)
{
    const u8 *s = px + (u16)ty * 72u + (u16)tx * 3u;
    u16 sum;

    sum  = (u16)s[0] + s[1] + s[2];
    sum += (u16)s[24] + s[25] + s[26];
    sum += (u16)s[48] + s[49] + s[50];
    return (u8)(sum / 9u);
}

void scr_thumb(u8 slot, const u8 *px)
{
    u8 tx;
    u8 ty;
    u8 *d;

    if (slot >= 10u) {
        return;
    }
    for (ty = 0; ty < 8u; ++ty) {
        d = row_ptr((u8)(SCR_THUMB_Y + ty)) + slot * 4u;
        for (tx = 0; tx < 8u; tx += 2u) {
            *d++ = (u8)((LUM_TO_NIBBLE(block_avg(px, tx, ty)) << 4) |
                        LUM_TO_NIBBLE(block_avg(px, (u8)(tx + 1u), ty)));
        }
    }
}

void scr_mark(u8 slot, u8 on)
{
    scr_fill((u8)(slot * 8u + 1u), SCR_MARK_Y, 6u, 1u, on ? 63u : 0u);
}

void scr_bar(u8 slot, u8 height, u8 lum)
{
    u8 x = (u8)(slot * 8u + 2u);

    if (height > SCR_BAR_H) {
        height = SCR_BAR_H;
    }
    scr_fill(x, SCR_BAR_Y, 4u, (u8)(SCR_BAR_H - height), 0u);
    scr_fill(x, (u8)(SCR_BAR_Y + SCR_BAR_H - height), 4u, height, lum);
}

void scr_text_at(u8 line, u8 col, const char *s, u8 inverse)
{
    u8 *d;

    if (line >= SCR_LINES) {
        return;
    }
    d = HW_PTR(SCR_TEXT) + (u16)line * SCR_COLS;
    while (*s != '\0' && col < SCR_COLS) {
        d[col] = (u8)(to_screen_code((u8)*s) | inverse);
        ++col;
        ++s;
    }
}

void scr_text(u8 line, const char *s)
{
    if (line >= SCR_LINES) {
        return;
    }
    memset(HW_PTR(SCR_TEXT) + (u16)line * SCR_COLS, 0, SCR_COLS);
    scr_text_at(line, 0, s, 0);
}

void scr_progress(u8 line, u8 col, u8 width, u16 done, u16 total)
{
    u8 *d;
    u8 filled;
    u8 i;

    if (line >= SCR_LINES || total == 0u) {
        return;
    }
    if (done > total) {
        done = total;
    }
    filled = (u8)(((u32)done * width) / total);
    d = HW_PTR(SCR_TEXT) + (u16)line * SCR_COLS + col;
    for (i = 0; i < width && (u8)(col + i) < SCR_COLS; ++i) {
        /* Inverse space when filled, a thin line (ATASCII $12) otherwise. */
        d[i] = i < filled ? SCR_INVERSE : 0x52u;
    }
}
