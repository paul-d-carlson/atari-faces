/*
 * test_fmt.c -- the text-line builder.
 */
#include <string.h>

#include "tf.h"
#include "fmt.h"

static u8 same(const char *want)
{
    return (u8)(strcmp(fmt_end(), want) == 0);
}

static void test_empty(void)
{
    fmt_begin();
    TF_CHECK(same(""));
    TF_EQ(fmt_len(), 0);
}

static void test_str_char(void)
{
    fmt_begin();
    fmt_str("AB");
    fmt_char('C');
    TF_CHECK(same("ABC"));
    TF_EQ(fmt_len(), 3);
}

static void test_unsigned(void)
{
    fmt_begin();
    fmt_u(0, 0);
    fmt_char('|');
    fmt_u(42, 4);
    fmt_char('|');
    fmt_u(65535U, 2);
    TF_CHECK(same("0|  42|65535"));
}

static void test_unsigned_zero_pad(void)
{
    fmt_begin();
    fmt_u0(7, 2);
    fmt_char(' ');
    fmt_u0(123, 2);
    TF_CHECK(same("07 123"));
}

static void test_signed(void)
{
    fmt_begin();
    fmt_s(12, 3);
    fmt_s(-5, 3);
    fmt_s(0, 1);
    fmt_s((s16)(-32767 - 1), 1);
    TF_CHECK(same("+012-005+0-32768"));
}

static void test_decimal_and_percent(void)
{
    fmt_begin();
    fmt_dec1(0);
    fmt_char(' ');
    fmt_dec1(123);
    fmt_char(' ');
    fmt_permille(522);
    fmt_char(' ');
    fmt_permille(1000);
    TF_CHECK(same("0.0 12.3 52.2% 100.0%"));
}

static void test_tab(void)
{
    fmt_begin();
    fmt_str("AB");
    fmt_tab(5);
    fmt_char('X');
    fmt_tab(2);      /* already past column 2: no change */
    TF_CHECK(same("AB   X"));
}

static void test_truncates_at_40(void)
{
    u8 i;

    fmt_begin();
    for (i = 0; i < 50u; ++i) {
        fmt_char('X');
    }
    TF_EQ(fmt_len(), FMT_MAX);
    TF_EQ(strlen(fmt_end()), FMT_MAX);
    fmt_tab(60);
    TF_EQ(fmt_len(), FMT_MAX);
}

static const tf_case fmt_cases[] = {
    TF_CASE(test_empty),
    TF_CASE(test_str_char),
    TF_CASE(test_unsigned),
    TF_CASE(test_unsigned_zero_pad),
    TF_CASE(test_signed),
    TF_CASE(test_decimal_and_percent),
    TF_CASE(test_tab),
    TF_CASE(test_truncates_at_40),
    TF_END
};

const tf_suite suite_fmt = { "fmt", fmt_cases };
