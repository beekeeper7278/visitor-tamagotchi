#pragma once
/* ===========================================================================
 * voice - the spoken-line pack  [PHASE 10]
 *
 * WHY THIS EXISTS. The first voice was procedural: pitched tone bursts, then
 * a formant synth with jitter, glides and consonants. The second sounded far
 * more like a mouth than the first - and still could not say "I did a mess.
 * It's art.", because formant babble has no WORDS in it. That is inherent,
 * not a tuning failure, and no amount of further synthesis would have fixed
 * it. The brief asked for a cute UNDERSTANDABLE voice; understandable means
 * recorded speech.
 *
 * WHAT IT IS. One packed asset in the (previously unused) 3.37 MB spiffs
 * partition: a sorted hash index followed by 4-bit IMA ADPCM at 11.025 kHz.
 * 258 lines land near 2 MB. Telephone speech is 8 kHz, so 11 kHz is
 * comfortably intelligible.
 *
 * LINES ARE KEYED BY A HASH OF THEIR TEXT. No ids, no table to keep in sync,
 * and no call site changes: dialogue.cpp gains a line, the generator picks it
 * up, and the device finds it because the string hashes the same on both
 * sides. A line with no clip falls back to the chirp voice, so a stale or
 * missing pack degrades rather than breaks - which also means the firmware
 * runs perfectly well with no pack flashed at all.
 *
 * ONE ASSET SET SERVES FOUR STAGES. Playback is resampled, so pitch and tempo
 * rise together the way a smaller creature actually sounds; the pack is
 * rendered deliberately slow so the sped-up result lands near natural. Four
 * separate recordings would have cost four times the flash to say the same
 * words.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mounts the filesystem and reads the index into RAM (12 B per clip, so a
 * 258-line pack costs about 3 KB). Safe to call when no pack is present. */
bool voice_begin(void);
bool voice_ready(void);

/* Must match fnv1a() in tools/voicepack/build_voicepack.py exactly. */
uint32_t voice_hash(const char *text);

/* TWO PACKS, one per gender. Gender is PRESENTATION ONLY - the same standing
 * rule the egg colour lives under - and a voice is presentation, exactly like
 * the colour tinting the Baby. It reaches no accumulator, no form choice, no
 * care rate, no discipline roll and no evolution path.
 *
 * Both packs stay mounted rather than one being chosen for the current
 * Visitor: a new Visitor can hatch with the other gender at any time, and
 * switching must not need a reload or a reboot. Two open files and two
 * indexes cost about 6 KB of PSRAM. */
enum { VOICE_BOY = 0, VOICE_GIRL, VOICE_PACKS };

/* Find a clip in one pack. Returns false when the line has no recording -
 * the caller then chirps instead, which is the documented fallback. */
bool voice_lookup(uint8_t pack, const char *text, uint32_t *off, uint32_t *len);

/* Read packed ADPCM bytes from a pack. Called from the audio task only. */
int  voice_read(uint8_t pack, uint32_t off, uint8_t *dst, int n);

bool voice_pack_ready(uint8_t pack);
uint32_t voice_count(void);
uint32_t voice_rate(void);
void     voice_report(void);

#ifdef __cplusplus
}
#endif
