/*
 * sound.c -- see sound.h.
 */
#include "sound.h"
#include "hw.h"

#define DIST_PURE   0xA0u
#define DIST_NOISE  0x80u

static u8 g_tone_pitch;
static u8 g_tone_vol;
static u8 g_click_vol;
static u8 g_q_pitch[SND_QUEUE];
static u8 g_q_vol[SND_QUEUE];
static u8 g_q_len;

void snd_init(void)
{
    HW(AUDCTL) = 0;
    HW(SKCTL) = 3;
    snd_off();
}

void snd_off(void)
{
    u8 i;

    for (i = 0; i < 8u; ++i) {
        HW(AUDF1 + i) = 0;
    }
    g_tone_vol = 0;
    g_click_vol = 0;
    g_q_len = 0;
}

void snd_tone(u8 pitch, u8 vol)
{
    g_q_len = 0;
    g_tone_pitch = pitch;
    g_tone_vol = (u8)(vol & 15u);
    HW(AUDF1) = pitch;
    HW(AUDC1) = (u8)(DIST_PURE | g_tone_vol);
}

u8 snd_queue(u8 pitch, u8 vol)
{
    if (g_tone_vol == 0u && g_q_len == 0u) {
        snd_tone(pitch, vol);
        return 1;
    }
    if (g_q_len >= SND_QUEUE) {
        return 0;
    }
    g_q_pitch[g_q_len] = pitch;
    g_q_vol[g_q_len] = vol;
    ++g_q_len;
    return 1;
}

void snd_click(void)
{
    g_click_vol = 6;
    HW(AUDF1 + 2u) = 8;
    HW(AUDC1 + 2u) = (u8)(DIST_NOISE | g_click_vol);
}

void snd_chime(void)
{
    snd_tone(SND_C5, 10);
    snd_queue(SND_E5, 10);
    snd_queue(SND_G5, 12);
}

void snd_buzz(void)
{
    snd_tone(SND_E4, 10);
    snd_queue(SND_C4, 12);
}

void snd_tick(void)
{
    u8 i;

    if (g_click_vol != 0u) {
        g_click_vol = (u8)(g_click_vol > 3u ? g_click_vol - 3u : 0u);
        HW(AUDC1 + 2u) = (u8)(DIST_NOISE | g_click_vol);
    }
    if (g_tone_vol != 0u) {
        --g_tone_vol;
        /* Cut a note short once it is quiet if another one is waiting. */
        if (g_tone_vol < 4u && g_q_len != 0u) {
            g_tone_vol = 0;
        }
    }
    if (g_tone_vol == 0u && g_q_len != 0u) {
        g_tone_pitch = g_q_pitch[0];
        g_tone_vol = g_q_vol[0];
        for (i = 1; i < g_q_len; ++i) {
            g_q_pitch[i - 1u] = g_q_pitch[i];
            g_q_vol[i - 1u] = g_q_vol[i];
        }
        --g_q_len;
        HW(AUDF1) = g_tone_pitch;
    }
    HW(AUDC1) = (u8)(DIST_PURE | g_tone_vol);
}

u8 snd_busy(void)
{
    return (u8)(g_tone_vol != 0u || g_q_len != 0u || g_click_vol != 0u);
}
