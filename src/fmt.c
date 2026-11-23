/*
 * fmt.c -- see fmt.h.
 */
#include "fmt.h"

/* The line being built, always NUL-terminated. */
static char g_buf[FMT_MAX + 1u];
static u8 g_len;

void fmt_begin(void)
{
    g_len = 0;
    g_buf[0] = '\0';
}

void fmt_char(char c)
{
    if (g_len < FMT_MAX) {
        g_buf[g_len++] = c;
        g_buf[g_len] = '\0';
    }
}

void fmt_str(const char *s)
{
    while (*s != '\0') {
        fmt_char(*s++);
    }
}

/* value in decimal, padded on the left with `pad` to `width` columns.
 * Digits come out lowest first, so they are collected and then written
 * in reverse. */
static void fmt_num(u16 value, u8 width, char pad)
{
    char digits[5];
    u8 n_digits = 0;

    do {
        digits[n_digits++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (width > n_digits) {
        fmt_char(pad);
        --width;
    }
    while (n_digits != 0u) {
        fmt_char(digits[--n_digits]);
    }
}

void fmt_u(u16 v, u8 width)
{
    fmt_num(v, width, ' ');
}

void fmt_u0(u16 v, u8 width)
{
    fmt_num(v, width, '0');
}

void fmt_s(s16 v, u8 digits)
{
    if (v < 0) {
        fmt_char('-');
        fmt_num((u16)(-(s32)v), digits, '0');
    } else {
        fmt_char('+');
        fmt_num((u16)v, digits, '0');
    }
}

void fmt_dec1(u16 v)
{
    fmt_num((u16)(v / 10u), 0, ' ');
    fmt_char('.');
    fmt_char((char)('0' + v % 10u));
}

void fmt_permille(u16 pm)
{
    fmt_dec1(pm);
    fmt_char('%');
}

void fmt_tab(u8 col)
{
    while (g_len < col && g_len < FMT_MAX) {
        fmt_char(' ');
    }
}

u8 fmt_len(void)
{
    return g_len;
}

const char *fmt_end(void)
{
    return g_buf;
}
