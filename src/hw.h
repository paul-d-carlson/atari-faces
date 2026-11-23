/*
 * hw.h -- Atari hardware and OS addresses, and how C code reaches them.
 *
 * On the Atari HW(addr) is the memory location itself. In the host unit
 * tests it is a byte of the fake 64K array hw_mem[], and HW_IDLE() (called
 * inside every busy-wait) advances the fake frame counter so waits finish.
 */
#ifndef HW_H
#define HW_H

#include "types.h"

#ifdef __CC65__
#define HW(a)       (*(volatile u8 *)(a))
#define HW_IDLE()   ((void)0)
#define HW_ADDR(p)  ((u16)(p))
#define HW_PTR(a)   ((u8 *)(a))
#else
extern u8 hw_mem[65536];
void hw_idle(void);
#define HW(a)       (hw_mem[(u16)(a)])
#define HW_IDLE()   hw_idle()
#define HW_ADDR(p)  ((u16)(uintptr_t)(p))
#define HW_PTR(a)   (&hw_mem[(u16)(a)])
#endif

/* OS zero page and shadows */
#define RTCLOK0     0x0012u   /* frame counter, high byte */
#define RTCLOK1     0x0013u
#define RTCLOK2     0x0014u   /* frame counter, low byte */
#define ATRACT      0x004Du
#define VDSLST      0x0200u   /* display-list interrupt vector */
#define SDMCTL      0x022Fu
#define SDLSTL      0x0230u
#define SDLSTH      0x0231u
#define GPRIOR      0x026Fu
#define COLOR1      0x02C5u   /* text luminance in ANTIC mode 2 */
#define COLOR2      0x02C6u   /* text background */
#define COLOR4      0x02C8u   /* background; the hue of GTIA mode 9 */
#define MEMTOP      0x02E5u

/* GTIA */
#define PRIOR       0xD01Bu
#define CONSOL      0xD01Fu   /* bit 0 START, 1 SELECT, 2 OPTION; 0 = held */

/* POKEY */
#define AUDF1       0xD200u
#define AUDC1       0xD201u
#define AUDCTL      0xD208u
#define SKCTL       0xD20Fu

/* ANTIC */
#define WSYNC       0xD40Au
#define VCOUNT      0xD40Bu
#define NMIEN       0xD40Eu

#endif /* HW_H */
