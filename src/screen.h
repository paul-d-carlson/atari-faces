/*
 * screen.h -- 80x40 sixteen-gray picture above four lines of text.
 *
 * The top of the screen is GTIA mode 9 (Graphics 9: 80 pixels across, 16
 * shades of one hue), each picture row shown on four scan lines so pixels
 * are roughly square. Below it are four 40-column text lines in the
 * built-in font. A 20-byte display-list interrupt turns GTIA mode 9 on
 * above the picture and off again for the text.
 *
 * Everything lives at fixed addresses (BASIC off, so MEMTOP = $BC1F):
 *
 *   $B5E0  picture, 40 rows x 40 bytes (two pixels per byte), the top
 *          $640 bytes below MEMTOP, reserved by __RESERVED_MEMORY__ in
 *          src/atari.cfg
 *   $BC40  text, 4 x 40 bytes      } in the OS text screen above MEMTOP,
 *   $BCE0  display list, 492 bytes } unused once ours is switched on
 *
 * The display list must not cross a 1K boundary ($BC00-$BFFF here).
 *
 * Pixel brightness arguments are 0..63, like the face data; the screen
 * shows the top four bits.
 */
#ifndef SCREEN_H
#define SCREEN_H

#include "types.h"

#define SCR_GFX         0xB5E0u
#define SCR_TEXT        0xBC40u
#define SCR_DL          0xBCE0u
#define SCR_RESERVED    0x0640u   /* $BC20 - SCR_GFX */

#define SCR_W           80u
#define SCR_H           40u
#define SCR_ROW_BYTES   40u
#define SCR_COLS        40u
#define SCR_LINES       4u
#define SCR_DL_LEN      492u
#define SCR_DLI_LEN     20u

/* Picture layout: three 24x24 panels on top, ten 8x8 thumbnails, a marker
 * row and a strip of bars underneath. */
#define SCR_PANEL_A     2u
#define SCR_PANEL_B     28u
#define SCR_PANEL_C     54u
#define SCR_THUMB_Y     25u
#define SCR_MARK_Y      33u
#define SCR_BAR_Y       34u
#define SCR_BAR_H       6u

#define SCR_INVERSE     0x80u

extern const u8 scr_dli_code[SCR_DLI_LEN];

void scr_init(void);
/* Build the display list into dl (SCR_DL_LEN bytes). */
void scr_build_dlist(u8 *dl);

void scr_clear(void);
void scr_clear_text(void);
/* Hue 0..15 for the picture (and the screen border). */
void scr_hue(u8 hue);

void scr_pixel(u8 x, u8 y, u8 shade);
void scr_fill(u8 x, u8 y, u8 width, u8 height, u8 shade);
/* First `rows` rows of a 24x24 face (0..63) at even x. */
void scr_face_rows(u8 x, u8 y, const u8 *pixels, u8 rows);
void scr_face(u8 x, u8 y, const u8 *pixels);
/* A signed 24x24 image: 0 is mid-gray, and values are multiplied by
 * `contrast` (1, 2, ...) before -64..63 is mapped to black..white. */
void scr_face_signed(u8 x, u8 y, const s8 *values, u8 contrast);
/* 8x8 thumbnail (3x3 averages) of a face at thumbnail position 0..9. */
void scr_thumb(u8 position, const u8 *pixels);
/* Marker under the thumbnail at `position`, or clear it. */
void scr_mark(u8 position, u8 on);
/* Bar of height 0..SCR_BAR_H under the thumbnail at `position`. */
void scr_bar(u8 position, u8 height, u8 shade);

/* Text line `line`, padded with spaces to 40 columns. */
void scr_text(u8 line, const char *text);
/* Write text at a column, OR-ing every character with `inverse`. */
void scr_text_at(u8 line, u8 col, const char *text, u8 inverse);
/* Progress bar of `width` cells at a column: done / total filled. */
void scr_progress(u8 line, u8 col, u8 width, u16 done, u16 total);

#endif /* SCREEN_H */
