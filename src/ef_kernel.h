/*
 * ef_kernel.h -- the inner loops of the eigenfaces maths.
 *
 * Nearly all of the run time is spent in these dot products of signed
 * bytes: two 576-pixel images, or the same pixel of every training face.
 * They multiply with a quarter-square table
 *
 *     a * b = floor((a + b)^2 / 4) - floor((a - b)^2 / 4)
 *
 * (exact for integers), which on the 6502 is two table look-ups instead
 * of a call to cc65's general 16 x 16 multiply. With every input in
 * -63..63, a + b and a - b fit a signed byte, so the byte itself indexes
 * the table: no absolute values, no sign tests. Centred pixels are
 * already in that range; eigenfaces and weights are scaled to it.
 */
#ifndef EF_KERNEL_H
#define EF_KERNEL_H

#include "types.h"

/* Sum of a[i] * b[i]. Every value must be in -63..63. */
s32 ef_dot_s8(const s8 *a, const s8 *b, u16 n);

/* Sum of a[i] * b[i * stride] for n <= 192, all values in -63..63: one
 * pixel of several images stored one after another (stride = image size). */
s32 ef_dot_strided(const s8 *a, const s8 *b, u16 stride, u8 n);

/* Integer square root: the largest r with r * r <= v. */
u16 ef_isqrt32(u32 v);

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
