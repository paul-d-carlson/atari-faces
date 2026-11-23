/*
 * hw_host.c -- fake Atari memory for the native unit tests.
 *
 * src/hw.h maps HW(addr) to hw_mem[addr] off the Atari. Busy-waits call
 * hw_idle(), which plays the part of the vertical blank: it advances the
 * frame counter and lets go of START after a set number of frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hw.h"
#include "hw_host.h"

u8 hw_mem[65536];
static unsigned g_start_frames;

void hw_host_reset(void)
{
    memset(hw_mem, 0, sizeof(hw_mem));
    hw_mem[CONSOL] = 7;                 /* no console key held */
    hw_mem[MEMTOP] = 0x1F;              /* BASIC off */
    hw_mem[MEMTOP + 1] = 0xBC;
    g_start_frames = 0;
}

void hw_host_hold_start(unsigned frames)
{
    g_start_frames = frames;
    hw_mem[CONSOL] = (u8)(hw_mem[CONSOL] & ~1u);
}

void hw_idle(void)
{
    if (++hw_mem[RTCLOK2] == 0) {
        if (++hw_mem[RTCLOK1] == 0) {
            ++hw_mem[RTCLOK0];
        }
    }
    if (g_start_frames != 0 && --g_start_frames == 0) {
        hw_mem[CONSOL] = (u8)(hw_mem[CONSOL] | 1u);
    }
}

void ef_range_fail(const char *expr, long value, const char *file, int line)
{
    fprintf(stderr, "%s:%d: %s = %ld does not fit a 16-bit int on the 6502\n",
            file, line, expr, value);
    abort();
}
