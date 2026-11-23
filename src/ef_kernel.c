/*
 * ef_kernel.c -- see ef_kernel.h.
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
 * floor(v * v / 4) for the signed byte v, stored as two byte tables (low
 * and high half) indexed by the byte (u8)v. On the 6502 a byte-table
 * look-up is a single instruction; a 16-bit table costs a subroutine call
 * in cc65 to work out the address.
 *
 * Indexing by the byte itself is what makes this cheap: for a and b in
 * -63..63, a + b and a - b are in -126..126, so their low byte names the
 * right entry and no absolute value or sign test is needed.
 */
static u8 g_qsq_lo[256];
static u8 g_qsq_hi[256];
static u8 g_table_ready;

static void make_table(void)
{
    s16 v;
    u16 q;

    for (v = -128; v < 128; ++v) {
        q = (u16)((v * v) / 4);
        g_qsq_lo[(u8)v] = (u8)(q & 0xFFu);
        g_qsq_hi[(u8)v] = (u8)(q >> 8);
    }
    g_table_ready = 1;
}

/*
 * Low and high halves are summed separately in 16 bits, which the 6502
 * adds far faster than 32 bits. A low byte is at most 255 and a high byte
 * at most 15, so 192 terms cannot overflow either sum.
 */
#define CHUNK 192u

s32 ef_dot_s8(const s8 *a, const s8 *b, u16 n)
{
    /* Local copies: cc65 keeps parameters on the slow software stack. */
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    s32 total = 0;
    u16 plus_lo;
    u16 plus_hi;
    u16 minus_lo;
    u16 minus_hi;
    u8 chunk;
    u8 left;
    u8 i;
    u8 x;
    u8 y;
    u8 s;

    if (!g_table_ready) {
        make_table();
    }
    while (n != 0u) {
        chunk = n > CHUNK ? CHUNK : (u8)n;
        plus_lo = plus_hi = minus_lo = minus_hi = 0;
        /* Counting `left` down to zero: cc65 turns a test against zero
         * into one instruction, but i < chunk into a subroutine call. */
        for (i = 0, left = chunk; left != 0u; ++i, --left) {
            EF_CHECK((s8)pa[i] >= -63 && (s8)pa[i] <= 63 &&
                     (s8)pb[i] >= -63 && (s8)pb[i] <= 63);
            x = pa[i];
            y = pb[i];
            s = (u8)(x + y);            /* a * b = q(a + b) - q(a - b) */
            plus_lo += g_qsq_lo[s];
            plus_hi += g_qsq_hi[s];
            s = (u8)(x - y);
            minus_lo += g_qsq_lo[s];
            minus_hi += g_qsq_hi[s];
        }
        total += ((s32)plus_hi << 8) + plus_lo;
        total -= ((s32)minus_hi << 8) + minus_lo;
        pa += chunk;
        pb += chunk;
        n -= chunk;
    }
    return total;
}

s32 ef_dot_strided(const s8 *a, const s8 *b, u16 stride, u8 n)
{
    const u8 *pa = (const u8 *)a;
    const u8 *pb = (const u8 *)b;
    u16 plus_lo = 0;
    u16 plus_hi = 0;
    u16 minus_lo = 0;
    u16 minus_hi = 0;
    u8 x;
    u8 y;
    u8 s;

    if (!g_table_ready) {
        make_table();
    }
    EF_CHECK(n <= CHUNK);
    for (; n != 0u; --n) {
        EF_CHECK((s8)*pa >= -63 && (s8)*pa <= 63 && (s8)*pb >= -63 && (s8)*pb <= 63);
        x = *pa;
        y = *pb;
        s = (u8)(x + y);
        plus_lo += g_qsq_lo[s];
        plus_hi += g_qsq_hi[s];
        s = (u8)(x - y);
        minus_lo += g_qsq_lo[s];
        minus_hi += g_qsq_hi[s];
        ++pa;
        pb += stride;
    }
    return ((s32)plus_hi << 8) + plus_lo - ((s32)minus_hi << 8) - minus_lo;
}

u16 ef_isqrt32(u32 v)
{
    u32 root = 0;
    u32 bit = 0x40000000UL;

    while (bit > v) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (v >= root + bit) {
            v -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return (u16)root;
}
