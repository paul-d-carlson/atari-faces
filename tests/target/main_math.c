/*
 * main_math.c -- 6502 test image for ef_kernel, ef_data and fmt. Results
 * go to page six for tests/driver/target_runner.c.
 */
#include "suites.h"

static const tf_suite *const math_suites[] = {
    &suite_kernel,
    &suite_data,
    &suite_fmt,
    0
};

int main(void)
{
    tf_run(math_suites);
    for (;;) {
    }
    return 0;
}
