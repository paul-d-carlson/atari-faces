/*
 * test_protocol.h -- how a 6502 test image reports to the host runner.
 *
 * The cc65 test image writes this block into page six ($0600-$06FF, free
 * RAM that neither the OS, DOS 2.5 nor cc65 use). The host runner
 * (tests/driver/target_runner.c) reads it out of emulated memory by byte
 * offset, so every field is listed with its offset; cc65 does not pad
 * structs, so the target can simply use the struct.
 *
 * Shared by cc65 and the host compiler: keep it C89 and free of
 * platform types.
 */
#ifndef TEST_PROTOCOL_H
#define TEST_PROTOCOL_H

#define TP_ADDR        0x0600
#define TP_MAGIC       0xA5
#define TP_MAX_FAILS   8
#define TP_MAX_BENCH   12

/* Byte offsets inside the block. */
#define TP_OFF_MAGIC   0    /* TP_MAGIC once the image is running         */
#define TP_OFF_DONE    1    /* 1 when every test has run                  */
#define TP_OFF_TOTAL   2    /* tests run so far                           */
#define TP_OFF_FAILED  3    /* tests that failed                          */
#define TP_OFF_CUR     4    /* index of the test now running              */
#define TP_OFF_NFAILS  5    /* entries used in the failure log            */
#define TP_OFF_NBENCH  6    /* entries used in the benchmark log          */
#define TP_OFF_SUITES  8    /* u16: address of the NULL-ended suite table */
#define TP_OFF_FAILS   16   /* TP_MAX_FAILS x 12 bytes, see below         */
#define TP_OFF_BENCH   112  /* TP_MAX_BENCH x 4 bytes, see below          */

/* Failure entry: test u8, pad u8, line u16, actual s32, expected s32. */
#define TP_FAIL_SIZE   12
/* Benchmark entry: name address u16, frames u16. */
#define TP_BENCH_SIZE  4

/* In memory a suite table is an array of u16 suite addresses ending in 0.
 * A suite is {u16 name, u16 cases}; cases are {u16 name, u16 function}
 * ending with a 0 name. These are the tf_suite / tf_case structs. */

#endif /* TEST_PROTOCOL_H */
