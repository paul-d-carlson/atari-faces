/*
 * tutil.c -- see tutil.h.
 */
#include "tutil.h"

static u16 g_state = 1;

void t_seed(u16 seed)
{
    g_state = seed;
}

u16 t_rand(void)
{
    g_state = (u16)(g_state * 25173u + 13849u);
    return g_state;
}

s8 t_rand_s8(u8 lim)
{
    return (s8)((s16)(t_rand() >> 4) % (2 * lim + 1) - lim);
}

u16 t_ticks(void)
{
    u8 hi;
    u8 lo;

    do {
        hi = HW(RTCLOK1);
        lo = HW(RTCLOK2);
    } while (hi != HW(RTCLOK1));
    return (u16)(((u16)hi << 8) | lo);
}
