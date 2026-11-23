/*
 * main_model.c -- 6502 test image for ef_model on synthetic faces whose
 * eigenvectors, weights and rankings are known exactly.
 */
#include "suites.h"

static const tf_suite *const model_suites[] = {
    &suite_model,
    0
};

int main(void)
{
    tf_run(model_suites);
    for (;;) {
    }
    return 0;
}
