/*
 * ef_kernel.c -- see ef_kernel.h for the quarter-square method.
 */
#include "ef_kernel.h"

/*
 * cc65 normally keeps local variables on a software stack, which costs
 * several instructions per access. These loops run millions of times, so
 * here locals live at fixed addresses instead. (Safe: nothing in this
 * file is recursive or re-entered from an interrupt.)
 */
#ifdef __CC65__
#pragma static-locals (on)
#endif

/*
 * q(v) = floor(v * v / 4) for the signed byte v, stored as two byte
 * tables (low and high half) indexed by the byte (u8)v. On the 6502 a
 * byte-table look-up is a single instruction; a 16-bit table costs a
 * subroutine call in cc65 to work out the address.
 *
 * Indexing by the byte itself is what makes this cheap: for a and b in
 * -63..63, a + b and a - b are in -126..126, so their low byte names the
 * right entry and no absolute value or sign test is needed. (For
 * example -2 is the byte 254, and entry 254 holds q(-2) = 1.)
 */
static u8 g_qsq_lo[256];
static u8 g_qsq_hi[256];
static u8 g_table_ready;

static void make_table(void)
{
    s16 v;
    u16 quarter_square;

    for (v = -128; v < 128; ++v) {
        quarter_square = (u16)((v * v) / 4);
        g_qsq_lo[(u8)v] = (u8)(quarter_square & 0xFFu);
        g_qsq_hi[(u8)v] = (u8)(quarter_square >> 8);
    }
    g_table_ready = 1;
}

/*
 * Low and high halves are summed separately in 16 bits, which the 6502
 * adds far faster than 32 bits, and combined at the end. A low byte is
 * at most 255 and a high byte at most 15, so 192 terms cannot overflow
 * either sum; longer dot products are done 192 terms at a time.
 */
#define CHUNK 192u

s32 ef_dot_s8(const s8 *a, const s8 *b, u16 n)
{
    /* Local copies: cc65 keeps parameters on the slow software stack. */
    const u8 *a_bytes = (const u8 *)a;
    const u8 *b_bytes = (const u8 *)b;
    s32 total = 0;
    u16 plus_lo;            /* sums of q(a + b), low and high halves */
    u16 plus_hi;
    u16 minus_lo;           /* sums of q(a - b) */
    u16 minus_hi;
    u8 chunk;
    u8 left;
    u8 i;
    u8 a_val;
    u8 b_val;
    u8 index;

    if (!g_table_ready) {
        make_table();
    }
    while (n != 0u) {
        chunk = n > CHUNK ? CHUNK : (u8)n;
        plus_lo = plus_hi = minus_lo = minus_hi = 0;
        /* Counting `left` down to zero: cc65 turns a test against zero
         * into one instruction, but i < chunk into a subroutine call. */
        for (i = 0, left = chunk; left != 0u; ++i, --left) {
            EF_CHECK((s8)a_bytes[i] >= -63 && (s8)a_bytes[i] <= 63 &&
                     (s8)b_bytes[i] >= -63 && (s8)b_bytes[i] <= 63);
            a_val = a_bytes[i];
            b_val = b_bytes[i];
            index = (u8)(a_val + b_val);    /* a * b = q(a + b) - q(a - b) */
            plus_lo += g_qsq_lo[index];
            plus_hi += g_qsq_hi[index];
            index = (u8)(a_val - b_val);
            minus_lo += g_qsq_lo[index];
            minus_hi += g_qsq_hi[index];
        }
        total += ((s32)plus_hi << 8) + plus_lo;
        total -= ((s32)minus_hi << 8) + minus_lo;
        a_bytes += chunk;
        b_bytes += chunk;
        n -= chunk;
    }
    return total;
}

/* The same, but b steps `stride` bytes at a time: with stride = 576 it
 * visits the same pixel of each face stored one after another. */
s32 ef_dot_strided(const s8 *a, const s8 *b, u16 stride, u8 n)
{
    const u8 *a_bytes = (const u8 *)a;
    const u8 *b_bytes = (const u8 *)b;
    u16 plus_lo = 0;
    u16 plus_hi = 0;
    u16 minus_lo = 0;
    u16 minus_hi = 0;
    u8 a_val;
    u8 b_val;
    u8 index;

    if (!g_table_ready) {
        make_table();
    }
    EF_CHECK(n <= CHUNK);
    for (; n != 0u; --n) {
        EF_CHECK((s8)*a_bytes >= -63 && (s8)*a_bytes <= 63 &&
                 (s8)*b_bytes >= -63 && (s8)*b_bytes <= 63);
        a_val = *a_bytes;
        b_val = *b_bytes;
        index = (u8)(a_val + b_val);
        plus_lo += g_qsq_lo[index];
        plus_hi += g_qsq_hi[index];
        index = (u8)(a_val - b_val);
        minus_lo += g_qsq_lo[index];
        minus_hi += g_qsq_hi[index];
        ++a_bytes;
        b_bytes += stride;
    }
    return ((s32)plus_hi << 8) + plus_lo - ((s32)minus_hi << 8) - minus_lo;
}

/*
 * Square root one bit at a time, like long division: `bit` walks down
 * the powers of four, and at each one the root either gains the matching
 * bit (if the remaining value is big enough) or not. Only shifts, adds
 * and compares, so no multiply or divide is needed.
 */
u16 ef_isqrt32(u32 value)
{
    u32 root = 0;
    u32 bit = 0x40000000UL;     /* the highest power of four in 32 bits */

    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (u16)root;
}
