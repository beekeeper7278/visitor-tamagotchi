#pragma once
/* ===========================================================================
 * dialogue - personality-flavoured lines, dreams, and "About Me" [PHASE 9.5]
 *
 * WHY THIS IS ONE MODULE. Before this, dialogue lived wherever the event
 * that triggered it happened to live: sleepy pokes in scr_main, told-off
 * lines in discipline, mischief lines in discipline, a fixed four-line pool
 * per tier in strings.cpp. That was fine while there were twelve lines. It
 * stops being fine the moment personality is supposed to colour SIX
 * different kinds of moment, because "several variants so one line does not
 * repeat constantly" is a property of the whole line set, not of one call
 * site - and you cannot see the whole line set when it is scattered.
 *
 * THE TONE RULES ARE NOT NEGOTIABLE, and they are the reason every line is
 * written out longhand here rather than assembled from fragments:
 *   - Kid-friendly, always. This is read aloud to a five-year-old.
 *   - Grumpy is GRUMBLY, never cruel. No form is ever mean or scary.
 *   - Nothing blames the child. A mess is funny; a mess is not an accusation.
 *   - Short. A bubble that needs two seconds to read is already too long.
 *
 * PERSONALITY IS A FLAVOUR, NEVER A RULE. Every selector here falls back to
 * a generic pool, so a Visitor whose traits have no special line for a
 * moment still gets a good line rather than silence. Nothing in this file
 * reads or writes a stat, an accumulator or the evolution path - it is
 * presentation, and both the traits and the FORM are inputs to it because
 * "Chonky" and "food-loving" are different kinds of fact about a Visitor.
 *
 * DREAMS ARE FLAVOUR ONLY. A dream never changes a stat. Each one is stored
 * as a single byte - an index into the table below - so the Journal can show
 * it again after a reboot without a per-dream string in the save blob, and
 * so the text can be rewritten later without invalidating saved history.
 * That means every line must be SELF-CONTAINED: no runtime substitution, or
 * a dream about burgers would silently become a dream about fruit the day
 * the Visitor's favourite food changed.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- dreams -------------------------------------------------------------- */

/* Choose a dream that suits this Visitor right now: personality, favourite
 * food and game, what has actually been happening, stage, and randomness.
 * `nap` picks from the shorter, sillier end of the table. Never repeats the
 * dream immediately before it. Returns an index into the dream table. */
uint8_t     dialogue_dream_pick(bool nap);
uint8_t     dialogue_dream_count(void);

/* The short wake-up bubble, and the longer first-person Journal version of
 * the SAME dream. Both are safe for any id; an out-of-range id returns the
 * first dream rather than a null. */
const char *dialogue_dream_bubble(uint8_t id);
const char *dialogue_dream_journal(uint8_t id);

/* Record a dream in the Visitor's rolling keepsake of the last DREAM_KEEP.
 * Persisted; nothing else reads it. */
void        dialogue_dream_record(uint8_t id);

/* --- reactions ----------------------------------------------------------- */

const char *dialogue_lights_off(void);   /* the room just went dark          */
const char *dialogue_lights_on(void);    /* ...and came back                 */
const char *dialogue_stink(void);        /* an old accident, still on the floor */
const char *dialogue_wake(bool nap);     /* good morning / after a nap       */
const char *dialogue_told_off(void);     /* a FAIR telling-off               */
const char *dialogue_mischief(uint8_t what);   /* mischief_t                 */
const char *dialogue_hatch_greeting(void);     /* the very first words       */

/* Food. `f` is a food_t (0 burger, 1 fruit, 2 cake). The refusal line is
 * the one that matters most: refusing food when genuinely full is NOT
 * misconduct, so the line has to sound like a full Visitor rather than a
 * defiant one, or a child will reasonably reach for the Discipline button. */
const char *dialogue_food_yum(uint8_t f);
const char *dialogue_food_partial(void);
const char *dialogue_food_refuse(void);

/* Poked while asleep. `insistent` is the fourth poke in a row. */
const char *dialogue_sleepy_poke(bool insistent);

/* After a game. */
const char *dialogue_game_done(void);

/* --- the Journal --------------------------------------------------------- */

/* "About Me", written as though the Visitor wrote it. Short, specific,
 * funny, kid-friendly. Built from personality, favourites, what it actually
 * does, its discipline history and the events worth mentioning - so two
 * Visitors never produce the same paragraph. */
void dialogue_about_me(char *out, size_t len);

/* Console: prove the variety is real rather than claimed. Prints one sample
 * of every category plus the whole dream table. */
void dialogue_report(void);

#ifdef __cplusplus
}
#endif
