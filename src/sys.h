/*
 * sys.h -- frame timing, console keys and the memory check.
 */
#ifndef SYS_H
#define SYS_H

#include "types.h"

/* MEMTOP when BASIC is off; the screen lives just below it. */
#define SYS_MEMTOP_NEEDED 0xBC1Fu

/* Frames since power-on (low 16 bits of RTCLOK). */
u16 sys_ticks(void);
/* Wait for the next vertical blank; also runs the sound envelopes and
 * stops the OS colour-cycling screen saver. */
void sys_wait_frame(void);
u8 sys_start_held(void);
/* Wait `frames` frames or until START is pressed (then released).
 * Returns 1 if START cut the wait short. */
u8 sys_wait(u16 frames);
/* 1 if the top of free memory leaves room for the screen (BASIC off). */
u8 sys_memtop_ok(void);

#endif /* SYS_H */
