/*
 * disk.h -- read a whole data file in pieces.
 *
 * Uses open()/read() rather than stdio: cc65's stdio pulls in several KB
 * that the demo does not need. The same calls work in the host tests.
 */
#ifndef DISK_H
#define DISK_H

#include "types.h"

#ifdef __CC65__
#define DISK_FACES "D:FACES.DAT"
#else
#ifndef DISK_FACES
#define DISK_FACES "data/FACES.DAT"
#endif
#endif

/* Returns a handle >= 0, or -1 if the file cannot be opened. */
int disk_open(const char *name);
/* Read len bytes, or fewer if the file ends first; returns bytes read. */
u16 disk_read(int fd, u8 *buf, u16 len);
void disk_close(int fd);

#endif /* DISK_H */
