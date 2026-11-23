/*
 * test_disk.c -- reading data/FACES.DAT (D:FACES.DAT on the Atari).
 */
#include "tf.h"
#include "disk.h"
#include "ef_data.h"

#ifdef __CC65__
#define MISSING_FILE "D:NOFILE.DAT"
#else
#define MISSING_FILE "data/NOFILE.DAT"
#endif

/* Size of the FACES.DAT built by tools/export_faces.py. */
#define FACES_SIZE (8u + 2u * 15u + 15u * 576u)

static u8 g_buf[600];

static void test_open_missing(void)
{
    TF_CHECK(disk_open(MISSING_FILE) < 0);
}

static void test_bad_handle(void)
{
    TF_EQ(disk_read(-1, g_buf, 10), 0);
    disk_close(-1);
}

static void test_read_header(void)
{
    efd_info info;
    int fd = disk_open(DISK_FACES);

    TF_CHECK(fd >= 0);
    TF_EQ(disk_read(fd, g_buf, EFD_HEADER_LEN), EFD_HEADER_LEN);
    disk_close(fd);
    TF_EQ(efd_parse_header(g_buf, &info), EFD_OK);
    TF_EQ(info.n_train, 10);
    TF_EQ(info.n_query, 5);
}

static void test_read_to_end(void)
{
    int fd = disk_open(DISK_FACES);
    u16 total = 0;
    u16 got;

    TF_CHECK(fd >= 0);
    /* 600-byte pieces do not line up with the file size, so the last
     * read comes back short. */
    do {
        got = disk_read(fd, g_buf, sizeof(g_buf));
        total = (u16)(total + got);
    } while (got == sizeof(g_buf));
    disk_close(fd);
    TF_EQ(total, FACES_SIZE);
    TF_EQ(FACES_SIZE % sizeof(g_buf), got);
}

static const tf_case disk_cases[] = {
    TF_CASE(test_open_missing),
    TF_CASE(test_bad_handle),
    TF_CASE(test_read_header),
    TF_CASE(test_read_to_end),
    TF_END
};

const tf_suite suite_disk = { "disk", disk_cases };
