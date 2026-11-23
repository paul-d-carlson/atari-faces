/*
 * tutil.h -- small helpers shared by the unit tests (host and 6502).
 */
#ifndef TUTIL_H
#define TUTIL_H

#include "types.h"
#include "hw.h"

/* Deterministic pseudo-random numbers (16-bit LCG). */
void t_seed(u16 seed);
u16 t_rand(void);
/* Uniform-ish value in -lim..lim. */
s8 t_rand_s8(u8 lim);

/* Frames since power-on, read straight from RTCLOK (no sys.c needed). */
u16 t_ticks(void);

#endif /* TUTIL_H */
