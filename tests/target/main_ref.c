/*
 * main_ref.c -- 6502 test image that reads D:FACES.DAT from the test disk
 * and checks the integer eigenfaces against the numpy reference, timing
 * every phase on the 1.79 MHz CPU.
 */
#include "suites.h"

static const tf_suite *const ref_suites[] = {
    &suite_disk,
    &suite_reference,
    0
};

int main(void)
{
    tf_run(ref_suites);
    for (;;) {
    }
    return 0;
}
