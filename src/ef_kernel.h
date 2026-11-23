/*
 * ef_kernel.h -- the inner loops of the eigenfaces math.
 *
 * Nearly all of the run time goes into DOT PRODUCTS: multiply two lists
 * of numbers entry by entry and add up the results,
 *
 *     a . b = a[0]*b[0] + a[1]*b[1] + ... + a[n-1]*b[n-1]
 *
 * The dot product measures how much two images "agree": large and
 * positive when they are bright and dark in the same places, negative
 * when one is bright where the other is dark, near zero when they are
 * unrelated. Every covariance entry, every eigenface pixel and every
 * weight is one. Here they are of signed bytes: two 576-pixel images, or
 * the same pixel of every training face.
 *
 * The 6502 has no multiply instruction, and cc65's general 16 x 16
 * multiply routine is slow. So these loops multiply with a table of
 * QUARTER SQUARES, q(v) = floor(v^2 / 4), using
 *
 *     a * b = q(a + b) - q(a - b)
 *
 * Why it works: (a + b)^2 - (a - b)^2 = 4ab, so without the floor the
 * identity is plain algebra. The floor does no harm because a + b and
 * a - b are both even or both odd: either both squares are multiples of
 * 4, or both are one more than a multiple of 4, and the dropped
 * remainders cancel. For example 7 * 5: q(12) - q(2) = 36 - 1 = 35.
 *
 * On the 6502 that is two table look-ups instead of a call to a
 * multiply routine. With every input in -63..63, a + b and a - b fit a
 * signed byte, so the byte itself indexes the table: no absolute
 * values, no sign tests. Centered pixels are already in that range;
 * eigenfaces and weights are scaled to it.
 */
#ifndef EF_KERNEL_H
#define EF_KERNEL_H

#include "types.h"

/* Sum of a[i] * b[i]. Every value must be in -63..63. */
s32 ef_dot_s8(const s8 *a, const s8 *b, u16 n);

/* Sum of a[i] * b[i * stride] for n <= 192, all values in -63..63: one
 * pixel of several images stored one after another (stride = image size). */
s32 ef_dot_strided(const s8 *a, const s8 *b, u16 stride, u8 n);

/* Integer square root: the largest root with root * root <= value. */
u16 ef_isqrt32(u32 value);

/* 16 x 16 -> 32-bit multiplies (signed and unsigned). cc65 would
 * otherwise widen both operands and do a much slower 32 x 32 multiply. */
#ifdef __CC65__
#include <cc65.h>
#define EF_MUL16(a, b)  imul16x16r32((int)(a), (int)(b))
#define EF_UMUL16(a, b) umul16x16r32((unsigned)(a), (unsigned)(b))
#else
#define EF_MUL16(a, b)  ((s32)(a) * (s32)(b))
#define EF_UMUL16(a, b) ((u32)(a) * (u32)(b))
#endif

#endif /* EF_KERNEL_H */
