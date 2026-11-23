/*
 * test_sys.c -- frame timing, START and the MEMTOP check.
 */
#include "tf.h"
#include "hw.h"
#include "sys.h"
#ifndef __CC65__
#include "hw_host.h"
#endif

static void test_wait_frame_advances(void)
{
    u16 t = sys_ticks();

    HW(ATRACT) = 5;
    sys_wait_frame();
    TF_GE((u16)(sys_ticks() - t), 1);
    TF_EQ(HW(ATRACT), 0);                /* screen saver held off */
}

static void test_wait_counts_frames(void)
{
    u16 t = sys_ticks();

    TF_EQ(sys_wait(30), 0);
    TF_NEAR((u16)(sys_ticks() - t), 30, 1);
    TF_EQ(sys_wait(0), 0);
}

static void test_memtop(void)
{
#ifdef __CC65__
    /* The test disk boots with BASIC off. */
    TF_EQ(sys_memtop_ok(), 1);
#else
    HW(MEMTOP) = 0x1F;
    HW(MEMTOP + 1u) = 0xBC;
    TF_EQ(sys_memtop_ok(), 1);
    HW(MEMTOP + 1u) = 0x9C;              /* BASIC on */
    TF_EQ(sys_memtop_ok(), 0);
    HW(MEMTOP + 1u) = 0xBC;
#endif
}

#ifndef __CC65__
static void test_start_cuts_wait_short(void)
{
    u16 t;

    TF_EQ(sys_start_held(), 0);
    hw_host_hold_start(5);
    TF_EQ(sys_start_held(), 1);
    t = sys_ticks();
    TF_EQ(sys_wait(1000), 1);
    TF_LE((u16)(sys_ticks() - t), 6);    /* waited only for the release */
    TF_EQ(sys_start_held(), 0);
}

static void test_ticks_carry(void)
{
    HW(RTCLOK1) = 0x12;
    HW(RTCLOK2) = 0xFF;
    TF_EQ(sys_ticks(), 0x12FF);
    sys_wait_frame();
    TF_EQ(sys_ticks(), 0x1300);
}
#endif

static const tf_case sys_cases[] = {
    TF_CASE(test_wait_frame_advances),
    TF_CASE(test_wait_counts_frames),
    TF_CASE(test_memtop),
#ifndef __CC65__
    TF_CASE(test_start_cuts_wait_short),
    TF_CASE(test_ticks_carry),
#endif
    TF_END
};

const tf_suite suite_sys = { "sys", sys_cases };
