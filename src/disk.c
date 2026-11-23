/*
 * disk.c -- see disk.h.
 */
#include <fcntl.h>
#include <unistd.h>

#include "disk.h"

int disk_open(const char *name)
{
    return open(name, O_RDONLY);
}

u16 disk_read(int fd, u8 *buf, u16 len)
{
    u16 total = 0;
    int got;

    if (fd < 0) {
        return 0;
    }
    while (total < len) {
        got = read(fd, buf + total, (unsigned)(len - total));
        if (got <= 0) {
            break;
        }
        total = (u16)(total + (u16)got);
        /* A short read means the end of the file. Do not ask again:
         * DOS 2.5 then hands back the last sector a second time. */
        if (total < len) {
            break;
        }
    }
    return total;
}

void disk_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}
