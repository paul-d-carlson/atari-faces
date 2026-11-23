/*
 * emu.c -- see emu.h. Based on the libatari800 driver in atari-emacs
 * (tests/driver/harness_runner.c there).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libatari800.h"
#include "emu.h"

/* Execution counts per address, from atari800 built with MONITOR_PROFILE
 * (see mk/deps.mk). Declared by hand to match monitor.h. */
typedef struct {
    unsigned long count;
    unsigned long cycles;
} monitor_coverage_rec_t;
extern monitor_coverage_rec_t MONITOR_coverage[0x10000];

/* atari800's palette, 0xRRGGBB per colour (colours.h). */
extern int Colours_table[256];

#define SCREEN_W 384
#define SCREEN_H 240

static input_template_t g_input;
static int g_frames;

int emu_boot(const char *atr, const char *machine)
{
    char *args[8];
    int n = 0;

    /* No argv[0]: libatari800_init() supplies its own (see atari-emacs). */
    args[n++] = "-config";
    args[n++] = "/dev/null";
    args[n++] = (char *)machine;
    args[n++] = "-nobasic";
    args[n++] = "-ntsc";
    args[n++] = (char *)atr;
    args[n] = NULL;
    if (!libatari800_init(-1, args)) {
        fprintf(stderr, "libatari800_init failed: %s\n", libatari800_error_message());
        return -1;
    }
    libatari800_clear_input_array(&g_input);
    g_frames = 0;
    return 0;
}

unsigned char *emu_mem(void)
{
    return libatari800_get_main_memory_ptr();
}

unsigned emu_peek16(unsigned addr)
{
    unsigned char *m = emu_mem();

    return m[addr & 0xFFFFu] | (m[(addr + 1u) & 0xFFFFu] << 8);
}

int emu_frame(int start)
{
    g_input.start = (unsigned char)(start != 0);
    ++g_frames;
    /* next_frame() returns 0 when an error code is set. Display-list
     * errors happen while the OS boots and are harmless; a jammed 6502
     * is not. */
    if (libatari800_next_frame(&g_input) == 0 &&
        libatari800_error_code == LIBATARI800_CPU_CRASH) {
        fprintf(stderr, "6502 crashed after %d frames: %s\n", g_frames,
                libatari800_error_message());
        return -1;
    }
    return 0;
}

int emu_frames_run(void)
{
    return g_frames;
}

/* ---- a tiny PNG writer (stored, uncompressed deflate blocks) ---------- */

static unsigned long crc_table[256];

static unsigned long crc32_update(unsigned long crc, const unsigned char *p, size_t n)
{
    size_t i;

    if (crc_table[1] == 0) {
        unsigned long c;
        int k;
        unsigned j;
        for (j = 0; j < 256; ++j) {
            c = j;
            for (k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            }
            crc_table[j] = c;
        }
    }
    for (i = 0; i < n; ++i) {
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static void chunk(FILE *f, const char *type, const unsigned char *data, size_t n)
{
    unsigned char hdr[8];
    unsigned char crc[4];
    unsigned long c;

    put32(hdr, (unsigned long)n);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (n) {
        fwrite(data, 1, n, f);
    }
    c = crc32_update(0xFFFFFFFFUL, hdr + 4, 4);
    c = crc32_update(c, data, n) ^ 0xFFFFFFFFUL;
    put32(crc, c);
    fwrite(crc, 1, 4, f);
}

static unsigned char g_snapshot[SCREEN_W * SCREEN_H];

static int write_png(const char *path, const unsigned char *screen)
{
    static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    size_t row = 1 + SCREEN_W * 3;
    size_t raw_len = row * SCREEN_H;
    unsigned char *raw = malloc(raw_len);
    unsigned char *z = malloc(raw_len + raw_len / 65535 * 5 + 16);
    unsigned char ihdr[13];
    unsigned long a = 1;
    unsigned long b = 0;
    size_t i;
    size_t zn = 0;
    size_t pos;
    FILE *f;
    int x;
    int y;

    if (raw == NULL || z == NULL) {
        free(raw);
        free(z);
        return -1;
    }
    for (y = 0; y < SCREEN_H; ++y) {
        unsigned char *r = raw + row * y;
        r[0] = 0;
        for (x = 0; x < SCREEN_W; ++x) {
            int rgb = Colours_table[screen[y * SCREEN_W + x]];
            r[1 + x * 3] = (unsigned char)(rgb >> 16);
            r[2 + x * 3] = (unsigned char)(rgb >> 8);
            r[3 + x * 3] = (unsigned char)rgb;
        }
    }
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    for (pos = 0; pos < raw_len; pos += 65535) {
        size_t len = raw_len - pos < 65535 ? raw_len - pos : 65535;
        z[zn++] = (unsigned char)(pos + len >= raw_len);
        z[zn++] = (unsigned char)(len & 0xFF);
        z[zn++] = (unsigned char)(len >> 8);
        z[zn++] = (unsigned char)(~len & 0xFF);
        z[zn++] = (unsigned char)((~len >> 8) & 0xFF);
        memcpy(z + zn, raw + pos, len);
        zn += len;
    }
    for (i = 0; i < raw_len; ++i) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put32(z + zn, (b << 16) | a);
    zn += 4;

    put32(ihdr, SCREEN_W);
    put32(ihdr + 4, SCREEN_H);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    f = fopen(path, "wb");
    if (f == NULL) {
        free(raw);
        free(z);
        return -1;
    }
    fwrite(sig, 1, 8, f);
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, zn);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}

int emu_screenshot(const char *path)
{
    return write_png(path, libatari800_get_screen_ptr());
}

void emu_snapshot(void)
{
    memcpy(g_snapshot, libatari800_get_screen_ptr(), sizeof(g_snapshot));
}

int emu_save_snapshot(const char *path)
{
    return write_png(path, g_snapshot);
}

/* ---- coverage ------------------------------------------------------- */

int emu_coverage(const char *dbg_path, dbgcov_target_t *targets, int n)
{
    static unsigned char hit[65536];
    unsigned covered = 0;
    unsigned coverable = 0;
    long addr;
    int t;

    for (addr = 0; addr < 65536; ++addr) {
        hit[addr] = (unsigned char)(MONITOR_coverage[addr].count > 0);
    }
    if (dbgcov_analyze(dbg_path, targets, n, hit) != 0) {
        printf("(no line coverage: cannot read %s)\n", dbg_path);
        return -1;
    }
    printf("Line coverage of 6502 code (statements that ran in the emulator):\n");
    for (t = 0; t < n; ++t) {
        unsigned c = targets[t].covered_lines;
        unsigned all = targets[t].coverable_lines;
        printf("  %-18s %4u/%-4u %5.1f%%\n", targets[t].label, c, all,
               all ? 100.0 * c / all : 0.0);
        covered += c;
        coverable += all;
    }
    printf("  %-18s %4u/%-4u %5.1f%%\n", "total", covered, coverable,
           coverable ? 100.0 * covered / coverable : 0.0);
    return 0;
}

int emu_profile(const char *path)
{
    FILE *f = fopen(path, "w");
    long addr;

    if (f == NULL) {
        return -1;
    }
    for (addr = 0; addr < 65536; ++addr) {
        if (MONITOR_coverage[addr].cycles != 0) {
            fprintf(f, "%04lX %lu %lu\n", addr, MONITOR_coverage[addr].cycles,
                    MONITOR_coverage[addr].count);
        }
    }
    fclose(f);
    return 0;
}

int emu_profile_delta(const char *path)
{
    static unsigned long prev_cycles[65536];
    static unsigned long prev_count[65536];
    FILE *f = fopen(path, "w");
    long addr;

    if (f == NULL) {
        return -1;
    }
    for (addr = 0; addr < 65536; ++addr) {
        unsigned long c = MONITOR_coverage[addr].cycles - prev_cycles[addr];
        if (c != 0) {
            fprintf(f, "%04lX %lu %lu\n", addr, c,
                    MONITOR_coverage[addr].count - prev_count[addr]);
        }
        prev_cycles[addr] = MONITOR_coverage[addr].cycles;
        prev_count[addr] = MONITOR_coverage[addr].count;
    }
    fclose(f);
    return 0;
}

void emu_exit(void)
{
    libatari800_exit();
}
