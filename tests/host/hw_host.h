/*
 * hw_host.h -- controls for the fake hardware in hw_host.c.
 */
#ifndef HW_HOST_H
#define HW_HOST_H

/* Zero memory; START/SELECT/OPTION up; MEMTOP $BC1F. */
void hw_host_reset(void);
/* Hold START for this many frames (hw_idle() calls). */
void hw_host_hold_start(unsigned frames);

#endif /* HW_HOST_H */
