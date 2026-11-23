/*
 * main_host.c -- run every suite natively. Exit status is the number of
 * failures (0 = all passed).
 */
#include "suites.h"
#include "hw_host.h"

static const tf_suite *const all_suites[] = {
    &suite_kernel,
    &suite_data,
    &suite_disk,
    &suite_fmt,
    &suite_model,
    &suite_reference,
    &suite_screen,
    &suite_sound,
    &suite_sys,
    &suite_demo,
    0
};

int main(void)
{
    hw_host_reset();
    return tf_run(all_suites) == 0 ? 0 : 1;
}
