/*
 * suites.h -- every unit-test suite. Each runner picks the ones it links.
 */
#ifndef SUITES_H
#define SUITES_H

#include "tf.h"

extern const tf_suite suite_kernel;
extern const tf_suite suite_data;
extern const tf_suite suite_disk;
extern const tf_suite suite_fmt;
extern const tf_suite suite_model;
extern const tf_suite suite_reference;
extern const tf_suite suite_screen;
extern const tf_suite suite_sound;
extern const tf_suite suite_sys;
extern const tf_suite suite_demo;

#endif /* SUITES_H */
