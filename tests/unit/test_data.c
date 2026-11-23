/*
 * test_data.c -- FACES.DAT header, metadata and pixel checks.
 */
#include <string.h>

#include "tf.h"
#include "ef_data.h"

static u8 g_hdr[EFD_HEADER_LEN];
static u8 g_px[EF_PIXELS];
static efd_info g_info;

static void good_header(void)
{
    g_hdr[0] = 'E';
    g_hdr[1] = 'F';
    g_hdr[2] = EFD_VERSION;
    g_hdr[3] = 10;
    g_hdr[4] = 5;
    g_hdr[5] = 24;
    g_hdr[6] = 24;
    g_hdr[7] = 64;
}

static void test_header_ok(void)
{
    good_header();
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_OK);
    TF_EQ(g_info.n_train, 10);
    TF_EQ(g_info.n_query, 5);
    TF_EQ(efd_meta_len(&g_info), 30);
}

static void test_header_bad_magic(void)
{
    good_header();
    g_hdr[1] = 'X';
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_MAGIC);
    good_header();
    g_hdr[0] = 'e';
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_MAGIC);
}

static void test_header_bad_version(void)
{
    good_header();
    g_hdr[2] = 2;
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_VERSION);
}

static void test_header_bad_size(void)
{
    good_header();
    g_hdr[5] = 32;
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_SIZE);
    good_header();
    g_hdr[6] = 23;
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_SIZE);
    good_header();
    g_hdr[7] = 255;
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_SIZE);
}

static void test_header_bad_counts(void)
{
    good_header();
    g_hdr[3] = 1;
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_COUNT);
    g_hdr[3] = EF_MAX_FACES + 1u;
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_COUNT);
    good_header();
    g_hdr[4] = EFD_MAX_QUERY + 1u;
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_BAD_COUNT);
    g_hdr[3] = 2;
    g_hdr[4] = 0;
    TF_EQ(efd_parse_header(g_hdr, &g_info), EFD_OK);
}

static void test_meta(void)
{
    static const u8 meta[6] = { 7, 0, 7, 2, 26, 9 };

    g_info.n_train = 2;
    g_info.n_query = 1;
    TF_EQ(efd_parse_meta(meta, &g_info), EFD_OK);
    TF_EQ(g_info.label[0], 7);
    TF_EQ(g_info.shot[1], 2);
    TF_EQ(g_info.label[2], 26);
    TF_EQ(g_info.shot[2], 9);
}

static void test_meta_bad_shot(void)
{
    static const u8 meta[4] = { 7, 0, 7, 10 };

    g_info.n_train = 2;
    g_info.n_query = 0;
    TF_EQ(efd_parse_meta(meta, &g_info), EFD_BAD_META);
}

static void test_face_pixels(void)
{
    memset(g_px, 63, sizeof(g_px));
    TF_EQ(efd_check_face(g_px), EFD_OK);
    g_px[EF_PIXELS - 1u] = 64;
    TF_EQ(efd_check_face(g_px), EFD_BAD_PIXEL);
    g_px[EF_PIXELS - 1u] = 0;
    g_px[0] = 255;
    TF_EQ(efd_check_face(g_px), EFD_BAD_PIXEL);
}

static const tf_case data_cases[] = {
    TF_CASE(test_header_ok),
    TF_CASE(test_header_bad_magic),
    TF_CASE(test_header_bad_version),
    TF_CASE(test_header_bad_size),
    TF_CASE(test_header_bad_counts),
    TF_CASE(test_meta),
    TF_CASE(test_meta_bad_shot),
    TF_CASE(test_face_pixels),
    TF_END
};

const tf_suite suite_data = { "ef_data", data_cases };
