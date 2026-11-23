/*
 * sys.c -- see sys.h.
 */
#include "sys.h"
#include "hw.h"
#include "sound.h"

u16 sys_ticks(void)
{
    u8 hi;
    u8 lo;

    /* Re-read if the low byte wrapped between the two reads. */
    do {
        hi = HW(RTCLOK1);
        lo = HW(RTCLOK2);
    } while (hi != HW(RTCLOK1));
    return (u16)(((u16)hi << 8) | lo);
}

void sys_wait_frame(void)
{
    u8 frame = HW(RTCLOK2);

    while (HW(RTCLOK2) == frame) {
        HW_IDLE();
    }
    HW(ATRACT) = 0;
    snd_tick();
}

u8 sys_start_held(void)
{
    return (u8)((HW(CONSOL) & 1u) == 0u);
}

u8 sys_wait(u16 frames)
{
    while (frames != 0u) {
        if (sys_start_held()) {
            while (sys_start_held()) {
                sys_wait_frame();
            }
            return 1;
        }
        sys_wait_frame();
        --frames;
    }
    return 0;
}

u8 sys_memtop_ok(void)
{
    u16 top = (u16)(HW(MEMTOP) | ((u16)HW(MEMTOP + 1u) << 8));

    return (u8)(top >= SYS_MEMTOP_NEEDED);
}
