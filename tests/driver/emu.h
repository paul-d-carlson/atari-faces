/*
 * emu.h -- the parts of libatari800 the test runners use.
 *
 * The runners boot a real DOS 2.5 disk image in a real (emulated) Atari,
 * step it one video frame at a time, read results out of its memory, take
 * screenshots and work out which 6502 code ran (for line coverage).
 */
#ifndef EMU_H
#define EMU_H

#include "dbgcov.h"

/* machine is an atari800 option such as "-atari" (400/800, 48K). */
int emu_boot(const char *atr, const char *machine);
unsigned char *emu_mem(void);
unsigned emu_peek16(unsigned addr);
/* Run one frame, START held if start != 0. Returns 0, or -1 if the 6502
 * crashed. */
int emu_frame(int start);
int emu_frames_run(void);
/* Save the current screen as a PNG. */
int emu_screenshot(const char *path);
/* Keep a copy of the current screen, and later save that copy. */
void emu_snapshot(void);
int emu_save_snapshot(const char *path);
/* Fill in coverage for each target from the run so far and print it. */
int emu_coverage(const char *dbg_path, dbgcov_target_t *targets, int n);
/* Write "address cycles count" for every address that ran (profiling). */
int emu_profile(const char *path);
/* The same, but only the cycles since the previous call (the first call
 * covers the whole run so far). */
int emu_profile_delta(const char *path);
void emu_exit(void);

#endif /* EMU_H */
