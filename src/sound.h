/*
 * sound.h -- short POKEY sound effects, advanced once per frame.
 *
 * Voice 0 plays tones (with a queue for little melodies), voice 1 plays
 * noise clicks. sys_wait_frame() calls snd_tick(), so effects keep
 * playing while the demo waits. Nothing advances them while the demo
 * computes, so a sound still playing when a long computation starts
 * keeps sounding until the next wait: give a click its two frame waits
 * first.
 */
#ifndef SOUND_H
#define SOUND_H

#include "types.h"

#define SND_QUEUE 4u

/* AUDF values for a few notes (pure tone, 64 kHz clock). */
#define SND_C4  121u
#define SND_E4  96u
#define SND_G4  81u
#define SND_C5  60u
#define SND_E5  47u
#define SND_G5  40u

void snd_init(void);
void snd_off(void);
/* Pure tone on voice 0 starting at volume `vol` (0..15), fading by one
 * step per frame. Replaces anything queued. */
void snd_tone(u8 pitch, u8 vol);
/* Add a tone to the voice-0 queue; returns 0 if the queue is full. */
u8 snd_queue(u8 pitch, u8 vol);
/* Short noise tick on voice 1. */
void snd_click(void);
/* Rising three-note arpeggio. */
void snd_chime(void);
/* Falling two-note buzz. */
void snd_buzz(void);
void snd_tick(void);
u8 snd_busy(void);

#endif /* SOUND_H */
