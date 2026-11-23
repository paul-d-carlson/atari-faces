/*
 * main_hw.c -- 6502 test image for the hardware code: screen, sound and
 * sys, running against the real (emulated) Atari.
 */
#include "suites.h"

static const tf_suite *const hw_suites[] = {
    &suite_screen,
    &suite_sound,
    &suite_sys,
    0
};

int main(void)
{
    tf_run(hw_suites);
    for (;;) {
    }
    return 0;
}
