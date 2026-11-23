/*
 * test_demo.c -- the whole show on the host, with fake hardware: waits
 * finish at once, the screen is a byte array and FACES.DAT comes from
 * data/. (On the Atari the demo is covered by the integration test.)
 */
#include <string.h>

#include "tf.h"
#include "hw.h"
#include "hw_host.h"
#include "demo.h"
#include "ef_model.h"
#include "screen.h"
#include "../fixtures/ef_reference.h"

static u8 text_has(u8 line, const char *s)
{
    char row[SCR_COLS + 1u];
    const u8 *t = HW_PTR(SCR_TEXT) + (u16)line * SCR_COLS;
    u8 i;

    /* Undo the screen-code conversion for letters, digits and spaces. */
    for (i = 0; i < SCR_COLS; ++i) {
        u8 c = (u8)(t[i] & 0x7Fu);
        row[i] = (char)(c < 64u ? c + 32u : c);
    }
    row[SCR_COLS] = '\0';
    return (u8)(strstr(row, s) != 0);
}

static void test_title(void)
{
    hw_host_reset();
    scr_init();
    demo_title();
    TF_CHECK(text_has(0, "E I G E N F A C E S"));
    TF_EQ(HW(COLOR4), 0x90);
}

static void test_missing_file(void)
{
    const char *saved = demo_data_path;

    hw_host_reset();
    demo_data_path = "data/NOFILE.DAT";
    TF_EQ(demo_load(), DEMO_ERR_OPEN);
    demo_data_path = saved;
    demo_fail(DEMO_ERR_OPEN, 3);
    TF_EQ(demo_report_block()->error, DEMO_ERR_OPEN);
    TF_CHECK(text_has(1, "CANNOT OPEN"));
    demo_fail(DEMO_ERR_READ, 0);
    TF_CHECK(text_has(1, "TOO SHORT"));
    demo_fail(DEMO_ERR_FORMAT, 0);
    TF_CHECK(text_has(1, "NOT A FACE FILE"));
    demo_fail(DEMO_ERR_MODEL, 0);
    TF_CHECK(text_has(1, "MODEL ERROR"));
}

static void test_bad_file(void)
{
    const char *saved = demo_data_path;

    /* The exporter script is not a face file. */
    demo_data_path = "tools/export_faces.py";
    TF_EQ(demo_load(), DEMO_ERR_FORMAT);
    demo_data_path = saved;
}

static void test_load_and_train(void)
{
    demo_report *r;

    hw_host_reset();
    scr_init();
    TF_EQ(demo_load(), DEMO_OK);
    r = demo_report_block();
    TF_EQ(r->magic, DEMO_MAGIC);
    TF_EQ(r->n_train, REF_N_TRAIN);
    TF_EQ(r->n_query, REF_N_QUERY);
    TF_EQ(r->k, DEMO_K);
    TF_CHECK(text_has(3, "100%"));
    demo_train();
    TF_EQ(r->trained, 1);
    TF_EQ(ef_stage(), EF_ST_TRAINED);
    TF_GE(r->eigen_iters, DEMO_K * EF_MIN_ITERS);
    TF_CHECK(text_has(0, "TRAINED"));
}

static void test_every_query(void)
{
    demo_report *r = demo_report_block();
    u8 q;

    for (q = 0; q < REF_N_QUERY; ++q) {
        demo_match(q);
        TF_EQ(r->queries_done, q + 1u);
        TF_EQ(r->q[q].label, ref_query_labels[q]);
        TF_EQ(r->q[q].best_index, ref_rankings[q * REF_N_TRAIN]);
        TF_EQ(r->q[q].best_label, ref_query_labels[q]);
        TF_GE(r->q[q].confidence, 40);
        TF_CHECK(text_has(0, "CORRECT"));
    }
    TF_EQ(HW(COLOR4), 0xC0);              /* green for a right answer */
}

static void test_wrong_answer_is_shown(void)
{
    u8 i;

    /* Relabel the training faces so the right answer becomes wrong. */
    for (i = 0; i < REF_N_TRAIN; ++i) {
        ef_set_label(i, 99);
    }
    demo_match(0);
    TF_CHECK(text_has(0, "WRONG - IT WAS 07"));
    TF_EQ(HW(COLOR4), 0x30);
    TF_EQ(demo_report_block()->q[0].best_label, 99);
}

static const tf_case demo_cases[] = {
    TF_CASE(test_title),
    TF_CASE(test_missing_file),
    TF_CASE(test_bad_file),
    TF_CASE(test_load_and_train),
    TF_CASE(test_every_query),
    TF_CASE(test_wrong_answer_is_shown),
    TF_END
};

const tf_suite suite_demo = { "demo", demo_cases };
