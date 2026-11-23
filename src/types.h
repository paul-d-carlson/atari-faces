/*
 * types.h -- fixed-size integer types shared by cc65 and the host build.
 *
 * cc65 has a 16-bit int and 32-bit long. The host build (unit tests) has a
 * 32-bit int, so arithmetic that would silently wrap on the 6502 does not
 * wrap there. Build the host tests with -DEF_CHECK_RANGE: every place that
 * relies on a value fitting a 16-bit int then calls EF_CHECK16(), which
 * reports the overflow the Atari would have had.
 */
#ifndef TYPES_H
#define TYPES_H

typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned short u16;
typedef signed short   s16;

#ifdef __CC65__
typedef signed long    s32;
typedef unsigned long  u32;
#else
#include <stdint.h>
typedef int32_t        s32;
typedef uint32_t       u32;
#endif

#ifdef EF_CHECK_RANGE
void ef_range_fail(const char *expr, long value, const char *file, int line);
#define EF_CHECK16(v) \
    do { if ((long)(v) > 32767L || (long)(v) < -32768L) \
        ef_range_fail(#v, (long)(v), __FILE__, __LINE__); } while (0)
#define EF_CHECK(c) \
    do { if (!(c)) ef_range_fail(#c, 0L, __FILE__, __LINE__); } while (0)
#else
#define EF_CHECK16(v) ((void)0)
#define EF_CHECK(c)   ((void)0)
#endif

#endif /* TYPES_H */
