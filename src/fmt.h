/*
 * fmt.h -- build one 40-column text line at a time, without printf.
 *
 *   fmt_begin(); fmt_str("DIST "); fmt_dec1(123); scr_text(2, fmt_end());
 *
 * The line silently stops growing at 40 characters.
 */
#ifndef FMT_H
#define FMT_H

#include "types.h"

#define FMT_MAX 40u

void fmt_begin(void);
void fmt_str(const char *s);
void fmt_char(char c);
/* Unsigned number, right-aligned in `width` columns (0 = no padding). */
void fmt_u(u16 v, u8 width);
/* Same, padded with zeros. */
void fmt_u0(u16 v, u8 width);
/* Signed number with a leading + or -, zero-padded to `digits`. */
void fmt_s(s16 v, u8 digits);
/* v / 10 with one decimal, e.g. 123 -> "12.3". */
void fmt_dec1(u16 v);
/* Per-mille as a percentage, e.g. 522 -> "52.2%". */
void fmt_permille(u16 pm);
/* Pad with spaces up to column `col`. */
void fmt_tab(u8 col);
u8 fmt_len(void);
const char *fmt_end(void);

#endif /* FMT_H */
