#pragma once
/* ===========================================================================
 * audio - the ONE semantic sound interface  [PHASE 10]
 *
 * Gameplay asks for a MEANING ("the Visitor refused the food"), never for a
 * waveform and never for the codec. Nothing outside audio.cpp may talk to
 * the ES8311, touch I2S, or know what a sample rate is. That is what keeps
 * Mute honest and what lets the Visitor's voice mature by stage without a
 * single call site changing.
 *
 * TWO VOICES, AND WHY. The Visitor speaks with RECORDED SPEECH when a clip
 * exists for the line, and with a procedural chirp voice when one does not.
 *
 * The chirp voice came first and went through two generations - pitched tone
 * bursts, then a formant synth with jitter, glides and consonants. The second
 * genuinely sounds like a mouth. It still cannot say "I did a mess. It's
 * art.", because formant babble contains no WORDS, and no further tuning was
 * ever going to change that. Reported from the sofa as "a bunch of beeps and
 * boops", which was exactly right.
 *
 * So fixed lines are prerendered into a pack (see voice.h) that lives in the
 * previously unused 3.37 MB spiffs partition: 258 lines of 4-bit ADPCM at
 * 11 kHz, about 2 MB. One asset set covers all four stages because playback
 * is resampled, which raises pitch and tempo together the way a smaller
 * creature actually sounds.
 *
 * The chirp voice is NOT dead weight - it is the fallback, and it earns its
 * place three times over: lines assembled at runtime (the farewell note) can
 * never be prerendered; a board with no pack flashed still has a Visitor that
 * vocalises; and the wordless moments want a noise, not a sentence.
 *
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- volume -------------------------------------------------------------
 * MUTE MEANS SILENT, AND NOTHING ELSE. Bubbles, animations, game feedback
 * and every visual cue carry on exactly as before - a parent may leave this
 * muted forever and the Visitor must still be able to communicate. */
enum { VOL_MUTE = 0, VOL_LOW, VOL_MED, VOL_HIGH, VOL_COUNT };

/* --- what the game can ask for ------------------------------------------
 * Semantic, not acoustic. Add meanings here, never waveforms at call sites. */
typedef enum {
    SND_NONE = 0,
    /* UI */
    SND_UI_TAP, SND_UI_PAGE, SND_UI_CONFIRM, SND_UI_REFUSE,
    /* the Visitor */
    SND_STEP, SND_HAPPY, SND_SAD, SND_SURPRISE, SND_ANNOYED, SND_GRUMBLE,
    /* care */
    SND_FOOD_DROP, SND_EAT, SND_CAKE_YAY, SND_REFUSE_FOOD,
    SND_BATHROOM_GO, SND_BATHROOM_BACK, SND_CLEAN_PUFF,
    SND_SCOLD, SND_PRAISE,
    /* sleep */
    SND_BEDTIME, SND_WAKE, SND_SNORE,
    /* milestones */
    SND_HATCH_TICK, SND_HATCH_CHIME, SND_EVOLVE, SND_FAREWELL,
    /* games */
    SND_GAME_SELECT, SND_GAME_REVEAL, SND_GAME_CORRECT, SND_GAME_WRONG,
    SND_GAME_HIT, SND_GAME_MISS, SND_GAME_GOAL, SND_GAME_BUMP,
    SND_MEMO_1, SND_MEMO_2, SND_MEMO_3, SND_MEMO_4,
    /* motion [PHASE 10] */
    SND_DIZZY, SND_UPSIDE_DOWN,
    SND_COUNT
} snd_t;

/* Brings up I2S + the ES8311 and enables the amplifier. Safe to call when
 * the codec is absent: everything below then becomes a no-op rather than a
 * crash, so a board with no audio still runs the whole game. */
bool audio_init(void);
bool audio_ready(void);

/* Fire and forget. Never blocks the UI: the mixer runs on its own task. */
void audio_play(snd_t s);

/* SAY A LINE. Prefers the recorded clip for this exact text; falls back to
 * the chirp voice when there is no recording (a runtime-assembled line, or no
 * pack flashed at all). Call sites do not know or care which happened. */
void audio_say(const char *text);

/* The chirp voice on its own. Still used directly for the wordless moments -
 * and it is what audio_say() falls back to. */
void audio_voice(uint8_t syllables, bool question);

void    audio_set_volume(uint8_t level);   /* VOL_* */
uint8_t audio_volume(void);

/* Speak a line at a FORCED stage pitch, for A/B-ing the four voices without
 * having to age a real Visitor to hear them. Stage is STAGE_BABY..ADULT. */
void audio_say_as(const char *text, uint8_t stage);

/* Speak from a SPECIFIC pack (VOICE_BOY / VOICE_GIRL), ignoring the
 * Visitor's own gender. Diagnostics only: it is the sole way to hear the
 * pack this Visitor is not using without destroying it to hatch another. */
void audio_say_from(const char *text, uint8_t pack);

/* DIAGNOSTIC: force the REAL audio_say() path to speak from a chosen pack.
 *
 * audio_say_from() proves a pack can be READ; it does not prove that the
 * production path SELECTS the right one, because it takes the pack as an
 * argument and never calls the selector. This override sits inside the
 * selector instead, so audio_say() - the function every bubble actually
 * calls - can be made to switch packs at runtime.
 *
 * It does NOT touch the Visitor's gender, so a boy stays a boy: this is how
 * the girl pack gets heard without destroying a Visitor to hatch another.
 * Pass -1 to hand selection back to the pet. */
void audio_set_pack_override(int pack);
int  audio_pack_override(void);

/* DIAGNOSTIC: play the i'th clip of a pack BY INDEX, with no text.
 *
 * Exists so the whole pack can be auditioned end to end. Speaking every line
 * through audio_say() would need the 321 texts ON the device, and the pack is
 * keyed by HASH precisely so that no such table has to exist and be kept in
 * sync - adding one for a diagnostic would reintroduce the very thing the
 * design avoids. Walking the index instead needs nothing but the pack.
 *
 * Returns the clip's length in milliseconds so the caller can wait exactly as
 * long as it plays, or 0 if there is no such clip. The per-stage pitch is
 * applied, so what is heard is what this Visitor would hear. */
uint32_t audio_play_index(uint8_t pack, uint32_t i);

/* Bring-up only: a plain 1 kHz tone, the thing a human ear can confirm. */
void audio_test_tone(uint16_t ms, uint16_t hz);

/* Diagnostics: what the codec actually reported over I2C. */
void audio_report(void);

#ifdef __cplusplus
}
#endif
