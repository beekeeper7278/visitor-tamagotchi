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
 * WHY SYNTHESIS RATHER THAN SAMPLES. The brief asked for the tradeoff to be
 * compared rather than assumed:
 *
 *   prerecorded speech   Real words, and by far the best sound - but a cute
 *                        line is ~0.5 s; at 16 kHz 16-bit mono that is 16 KB
 *                        each, and the dialogue tables already hold well over
 *                        a hundred lines across traits and forms. Even at
 *                        4-bit ADPCM the voice alone would run to megabytes,
 *                        and every new line would need re-recording in four
 *                        stage voices. It cannot cover DYNAMIC text (the
 *                        farewell note is assembled at runtime) at all.
 *   shared material +    Cheaper, but still needs a recorded base, and
 *   pitch/rate shift     pitch-shifting speech badly is worse than not
 *                        speaking.
 *   TTS on-device        The brief is explicit: do not ship robotic TTS just
 *                        so the Visitor technically speaks. Agreed - on this
 *                        part it would sound like a fax machine.
 *
 * So: PROCEDURAL VOICE. Short pitched chirp-bursts with a vowel-ish wobble,
 * a per-stage pitch/rate profile and a per-personality delivery. It is a few
 * KB of code and no assets, it covers every line including dynamic text, and
 * it reads as one cute character growing up rather than four recordings.
 * Actual words stay in the speech bubble, which is where a pre-reader's adult
 * is reading them from anyway.
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

/* The Visitor's own voice. `syllables` is how chatty this line is; the pitch
 * and rate come from the CURRENT stage and personality, so one call site
 * sounds right from Baby to Adult. */
void audio_voice(uint8_t syllables, bool question);

void    audio_set_volume(uint8_t level);   /* VOL_* */
uint8_t audio_volume(void);

/* Bring-up only: a plain 1 kHz tone, the thing a human ear can confirm. */
void audio_test_tone(uint16_t ms, uint16_t hz);

/* Diagnostics: what the codec actually reported over I2C. */
void audio_report(void);

#ifdef __cplusplus
}
#endif
