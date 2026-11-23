/*
 * test_sound.c -- the sound-effect state machine. POKEY's registers read
 * back as something else on a real Atari, so the register values are only
 * checked on the host.
 */
#include "tf.h"
#include "hw.h"
#include "sound.h"

static void test_init_silences(void)
{
    snd_tone(SND_C4, 10);
    snd_init();
    TF_EQ(snd_busy(), 0);
#ifndef __CC65__
    TF_EQ(HW(AUDCTL), 0);
    TF_EQ(HW(SKCTL), 3);
    TF_EQ(HW(AUDC1), 0);
    TF_EQ(HW(AUDC1 + 6u), 0);
#endif
}

static void test_tone_fades(void)
{
    u8 i;

    snd_init();
    snd_tone(SND_G4, 3);
    TF_EQ(snd_busy(), 1);
#ifndef __CC65__
    TF_EQ(HW(AUDF1), SND_G4);
    TF_EQ(HW(AUDC1), 0xA3);
#endif
    for (i = 0; i < 3u; ++i) {
        snd_tick();
    }
    TF_EQ(snd_busy(), 0);
#ifndef __CC65__
    TF_EQ(HW(AUDC1), 0xA0);
#endif
    snd_tone(SND_C5, 0x1F);               /* volume is 4 bits */
#ifndef __CC65__
    TF_EQ(HW(AUDC1), 0xAF);
#endif
}

static void test_queue_plays_in_order(void)
{
    snd_init();
    TF_EQ(snd_queue(SND_C4, 2), 1);       /* idle: starts at once */
    TF_EQ(snd_queue(SND_E4, 8), 1);
    TF_EQ(snd_queue(SND_G4, 8), 1);
    TF_EQ(snd_queue(SND_C5, 8), 1);
    TF_EQ(snd_queue(SND_E5, 8), 1);
    TF_EQ(snd_queue(SND_G5, 8), 0);       /* four waiting: full */
    snd_tick();
    snd_tick();                           /* C4 finished */
#ifndef __CC65__
    TF_EQ(HW(AUDF1), SND_E4);
#endif
    snd_tick();
    snd_tick();
    snd_tick();
    snd_tick();                           /* E4 cut short at volume < 4 */
#ifndef __CC65__
    TF_EQ(HW(AUDF1), SND_G4);
#endif
    TF_EQ(snd_busy(), 1);
    snd_off();
    TF_EQ(snd_busy(), 0);
}

static void test_click_and_effects(void)
{
    u8 i;

    snd_init();
    snd_click();
    TF_EQ(snd_busy(), 1);
#ifndef __CC65__
    TF_EQ(HW(AUDC1 + 2u), 0x86);
#endif
    snd_tick();
    snd_tick();
    TF_EQ(snd_busy(), 0);
    snd_chime();
    TF_EQ(snd_busy(), 1);
    for (i = 0; i < 60u; ++i) {
        snd_tick();
    }
    TF_EQ(snd_busy(), 0);
    snd_buzz();
#ifndef __CC65__
    TF_EQ(HW(AUDF1), SND_E4);
#endif
    snd_off();
}

static const tf_case sound_cases[] = {
    TF_CASE(test_init_silences),
    TF_CASE(test_tone_fades),
    TF_CASE(test_queue_plays_in_order),
    TF_CASE(test_click_and_effects),
    TF_END
};

const tf_suite suite_sound = { "sound", sound_cases };
