/*
 * main.c -- Atari entry point for the eigenfaces demo.
 */
#include <unistd.h>

#include "demo.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"

/* ATASCII, with $9B as the end-of-line character. */
static const char basic_msg[] =
    "EIGENFACES NEEDS BASIC TURNED OFF.\x9B"
    "HOLD OPTION WHILE THE COMPUTER BOOTS.\x9B";

int main(void)
{
    if (!sys_memtop_ok()) {
        /* The screen sits where the BASIC cartridge would be. */
        write(1, basic_msg, sizeof(basic_msg) - 1u);
        for (;;) {
        }
    }
    snd_init();
    scr_init();
    demo_run();
    return 0;
}
