/* discipline - contextual telling-off. See discipline.h. */

#include <Arduino.h>
#include "config.h"
#include "pet.h"
#include "care.h"
#include "menu.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "audio.h"
#include "evolve.h"
#include "persist.h"
#include "dialogue.h"
#include "discipline.h"

static mischief_t s_open;
static uint32_t   s_opened_ms, s_last_mis_ms, s_last_roll_ms;
static uint8_t    s_annoyance;
static uint32_t   s_annoy_ms;

/* The gap to the NEXT opportunity, redrawn after each one. A fixed gap makes
 * a mischievous Visitor feel like a metronome; randomising it inside
 * [MISCHIEF_GAP_MIN_MS, MISCHIEF_GAP_MAX_MS] is what turns "roughly every
 * one to three minutes" into something that reads as a personality rather
 * than a timer. */
static uint32_t   s_gap_ms = MISCHIEF_GAP_MIN_MS;

static void draw_new_gap(void)
{
    s_gap_ms = MISCHIEF_GAP_MIN_MS +
               (uint32_t)random(0, (long)(MISCHIEF_GAP_MAX_MS - MISCHIEF_GAP_MIN_MS));
}

void discipline_init(void)
{
    s_open = MIS_NONE;
    s_opened_ms = s_last_mis_ms = 0;
    s_last_roll_ms = millis();
    s_annoyance = 0;
    draw_new_gap();
    /* Not in the first minute after switch-on: the return greeting and any
     * post-absence dream own the screen then. */
    discipline_settle(MISCHIEF_SETTLE_BOOT_MS);
}

void discipline_settle(uint32_t ms)
{
    s_last_mis_ms = millis();
    s_gap_ms      = ms;      /* one-shot; redrawn after the next opportunity */
    Serial.printf("DISCIPLINE: settling in - no spontaneous mischief for %lu s\n",
                  (unsigned long)(ms / 1000));
}

float discipline_learned(void) { return pet_get()->learned_mischief; }

uint8_t discipline_stage_weight(uint8_t stage)
{
    switch (stage) {
        case STAGE_BABY:  return MISCHIEF_W_BABY;
        case STAGE_KID:   return MISCHIEF_W_KID;
        case STAGE_TEEN:  return MISCHIEF_W_TEEN;
        case STAGE_ADULT: return MISCHIEF_W_ADULT;
        default:          return 0;         /* an egg misbehaves at nobody */
    }
}

/* One resolved window, folded into the rolling record. The EMA is the whole
 * mechanism: it decays, so neither a good start nor a bad one is permanent. */
static void learn(bool corrected)
{
    pet_state_t *p = pet_mutable();
    const float sample = corrected ? 0.0f : 100.0f;
    p->learned_mischief += LEARN_ALPHA * (sample - p->learned_mischief);
    if (p->learned_mischief < 0.0f)   p->learned_mischief = 0.0f;
    if (p->learned_mischief > 100.0f) p->learned_mischief = 100.0f;
    Serial.printf("DISCIPLINE: learned behaviour %s -> %.1f (0 calm, 100 wild)\n",
                  corrected ? "corrected" : "IGNORED", p->learned_mischief);
}

bool       discipline_window_open(void) { return s_open != MIS_NONE; }
mischief_t discipline_current(void)     { return s_open; }

const char *discipline_what(mischief_t m)
{
    switch (m) {
        case MIS_THREW_FOOD:   return "threw its food";
        case MIS_BEDTIME:      return "refused bedtime";
        case MIS_MESS:         return "made a mess on purpose";
        case MIS_CAKE_TANTRUM: return "demanded cake";
        default:               return "nothing";
    }
}



void discipline_misbehave(mischief_t what)
{
    if (what == MIS_NONE || s_open != MIS_NONE) return;
    s_open = what;
    s_opened_ms = millis();
    s_last_mis_ms = s_opened_ms;
    pet_mutable()->disc_opportunities++;

    audio_play(SND_GRUMBLE);
    ui_pet_play(PET_ANIM_REACT);
    ui_bubble_say(BUBBLE_T2_MOOD, dialogue_mischief((uint8_t)what));
    Serial.printf("MISCHIEF: %s - discipline window open for %lus\n",
                  discipline_what(what), (unsigned long)(DISC_WINDOW_MS / 1000));
}

void discipline_press(void)
{
    pet_state_t *p = pet_mutable();
    if (p->stage == STAGE_EGG) {
        ui_bubble_say_deferred(BUBBLE_T2_MOOD, "It's still an egg!");
        Serial.println("discipline ignored: still an egg");
        return;
    }
    if (menu_is_open()) menu_close();      /* the reaction is the point */

    if (s_open != MIS_NONE) {
        /* Fair: a moderate bump, not a meter to farm. The window closes, so
         * pressing again immediately is an UNFAIR press and costs. */
        p->discipline += DISC_GAIN;
        if (p->discipline > 100.0f) p->discipline = 100.0f;
        p->disc_correct++;
        if (p->mischief > 3) p->mischief -= 3;
        s_open = MIS_NONE;
        s_annoyance = 0;
        learn(true);            /* rolling record, not a permanent verdict */
        draw_new_gap();

        ui_pet_play(PET_ANIM_SAD);
        audio_play(SND_SCOLD);
        ui_bubble_say(BUBBLE_T1_REACTION, dialogue_told_off());
        Serial.printf("DISCIPLINE: fair -> +%.0f (now %.0f), mischief %u\n",
                      DISC_GAIN, p->discipline, p->mischief);
        persist_mark_dirty("disciplined");
        return;
    }

    /* Unfair: it did nothing wrong. No gain, a small happiness cost, and
     * repeating it builds annoyance. */
    p->disc_unfair++;
    p->happiness -= DISC_UNFAIR_HAPPY_LOSS;
    if (p->happiness < 0.0f) p->happiness = 0.0f;
    if (s_annoyance < 3) s_annoyance++;
    s_annoy_ms = millis();

    ui_pet_play(PET_ANIM_REFUSE);
    ui_bubble_say(BUBBLE_T1_REACTION,
                  s_annoyance >= 3 ? "Stop telling me off!"
                : s_annoyance == 2 ? "That's not fair!"
                                   : "What did I do?!");
    Serial.printf("DISCIPLINE: UNFAIR (nothing was wrong) -> happiness %.0f, "
                  "annoyance %u\n", p->happiness, s_annoyance);
    persist_mark_dirty("unfair discipline");
}

/* --- spontaneous mischief [rewritten for PHASE 9.5] ----------------------
 *
 * A LEGITIMATE opportunity, every time. Everything this function can produce
 * is something the Visitor CHOSE to do and could have chosen not to - which
 * is the only kind of thing a child may fairly tell it off for. Hunger,
 * tiredness, dirt, a bathroom accident from an ignored need and refusing food
 * when genuinely full are all still outside this function entirely, and the
 * guards below are what keep them there.
 *
 * The roll is: base rate, weighted by STAGE, adjusted by the hidden
 * tendency, by current discipline, by personality, and by the rolling
 * LEARNED record. The stage weighting is the part with the ordering
 * requirement (Kid > Baby > Teen > Adult), and it is applied as a
 * multiplier so the other terms cannot invert it by accident. */

/* WHAT A BABY IS ALLOWED TO DO. All four kinds, as it turns out - a dropped
 * dinner, a small deliberate mess, cake begging and refusing to settle are
 * every one of them in the harmless category already, by construction. An
 * earlier draft of this pass gated Baby to a subset, which was a distinction
 * with no substance behind it: what actually separates a Baby from a Kid
 * here is FREQUENCY (MISCHIEF_W_BABY vs MISCHIEF_W_KID), not the kind of
 * trouble. Keeping a fake gate would have implied a rule that was not real.
 *
 * The precondition on each candidate in choose_mischief() is what keeps all
 * four fair at any stage: a Visitor never throws food it is hungry for, and
 * never refuses a bedtime that is not actually due. */

/* Choose an opportunity that makes sense RIGHT NOW. Each candidate carries
 * its own precondition, so a Visitor never "refuses bedtime" at nine in the
 * morning and never "throws its food" when it is genuinely starving - the
 * second of those would be indistinguishable from a need, which is exactly
 * the line this whole mechanic is drawn along. */
static mischief_t choose_mischief(void)
{
    const pet_state_t *p = pet_get();

    mischief_t cand[MIS_COUNT];
    uint8_t n = 0;

    /* Dropping food ON PURPOSE requires that it is NOT hungry - a hungry
     * Visitor turning down dinner is a need, not a joke. */
    if (p->hunger >= 55.0f)                 cand[n++] = MIS_THREW_FOOD;

    /* A tiny intentional mess, while there is still floor to put it on. */
    if (care_mess_count() < MESS_MAX - 1)   cand[n++] = MIS_MESS;

    /* Bedtime silliness only inside (or just before) the sleep window, and
     * only while actually awake. */
    if (care_sleep_due() && !p->asleep)     cand[n++] = MIS_BEDTIME;

    /* Cake begging: funnier when it has just eaten, and it must never be a
     * disguised hunger cue, so it is gated on NOT being hungry too. */
    if (p->hunger >= 45.0f)                 cand[n++] = MIS_CAKE_TANTRUM;

    if (n == 0) return MIS_NONE;

    /* Personality nudges WHICH, never WHETHER. */
    for (uint8_t tries = 0; tries < 4; tries++) {
        const mischief_t m = cand[random(0, n)];
        if (m == MIS_MESS && (p->trait_a == PERS_TIDY || p->trait_b == PERS_TIDY)
            && random(0, 100) < 70) continue;      /* a tidy one rarely does */
        if (m == MIS_CAKE_TANTRUM &&
            !(p->trait_a == PERS_FOODIE || p->trait_b == PERS_FOODIE)
            && random(0, 100) < 35) continue;      /* foodies beg more */
        return m;
    }
    /* Four unlucky rolls in a row: take the first candidate rather than
     * silently skipping the opportunity. */
    return cand[0];
}

/* THE ROLL, as one expression with one caller and one reporter.
 *
 * `for_stage` lets the console print the table for every stage using the
 * EXACT expression the roll uses, rather than a hand-copied approximation.
 * That mistake has already been made once in this project - a test fixture
 * and the code it was testing drifted apart and every calibration figure
 * derived from it was wrong - so the ordering requirement here is checked
 * against the real function or not at all.
 *
 * ORDER OF OPERATIONS MATTERS. Everything else is computed first, and the
 * STAGE WEIGHT multiplies the result. Applying the weight to the base term
 * alone (the first draft) let the shared additive terms swamp it: a Kid at
 * 21% and an Adult at 12% is technically the right order and nothing like
 * the intended difference. Multiplying at the end preserves the ordering by
 * construction - the weights are positive and monotonic - while keeping the
 * spread visible, and it means an Adult with a bad discipline history is
 * still meaningfully wilder than an Adult with a good one. */
static int mischief_pct(uint8_t for_stage)
{
    const pet_state_t *p = pet_get();

    int pct = MISCHIEF_BASE_PCT_9_5;
    pct += (int)p->mischief / 8;                    /* hidden tendency      */
    pct -= (int)(p->discipline / 12.0f);            /* current discipline   */

    /* THE LEARNED RECORD, centred on LEARN_START so a neutral history is
     * worth nothing either way. */
    pct += (int)((p->learned_mischief - LEARN_START) * LEARN_WEIGHT_PCT / 100.0f / 5.0f);

    if (p->trait_a == PERS_MISCHIEVOUS || p->trait_b == PERS_MISCHIEVOUS) pct += 6;
    if (p->trait_a == PERS_PLAYFUL     || p->trait_b == PERS_PLAYFUL)     pct += 2;
    if (p->trait_a == PERS_TIDY        || p->trait_b == PERS_TIDY)        pct -= 4;
    if (p->trait_a == PERS_SHY         || p->trait_b == PERS_SHY)         pct -= 2;

    if (pct < 2)  pct = 2;
    if (pct > 60) pct = 60;

    pct = pct * (int)discipline_stage_weight(for_stage) / 100;

    if (pct < 1)  pct = 1;
    if (pct > 45) pct = 45;
    return pct;
}

static void maybe_misbehave(void)
{
    const pet_state_t *p = pet_get();
    const uint32_t now = millis();

    /* Not while there is nothing to misbehave AT, and never while a real
     * need is in progress - the Visitor is holding on, and interrupting that
     * with a joke would blur exactly the line this mechanic depends on. */
    if (p->stage < STAGE_BABY || p->stage > STAGE_ADULT) return;
    if (p->asleep || care_is_holding()) return;
    if (now - s_last_mis_ms < s_gap_ms) return;

    if ((int)random(0, 100) >= mischief_pct(p->stage)) return;

    const mischief_t what = choose_mischief();
    if (what == MIS_NONE) return;   /* nothing legitimate to do right now */

    /* Redraw the gap BEFORE opening, so back-to-back mischief is impossible
     * even if this one is corrected instantly. */
    draw_new_gap();
    discipline_misbehave(what);
}

void discipline_tick(void)
{
    const uint32_t now = millis();
    pet_state_t *p = pet_mutable();

    if (s_annoyance && now - s_annoy_ms > DISC_ANNOY_DECAY_MS) s_annoyance = 0;

    /* An ignored window: discipline slips a little and the hidden tendency
     * grows. Letting misbehaviour slide has to mean something, or the whole
     * mechanic is optional. */
    if (s_open != MIS_NONE && now - s_opened_ms >= DISC_WINDOW_MS) {
        Serial.printf("DISCIPLINE: %s went uncorrected\n", discipline_what(s_open));
        s_open = MIS_NONE;
        p->disc_ignored++;
        p->discipline -= DISC_IGNORED_LOSS;
        if (p->discipline < 0.0f) p->discipline = 0.0f;
        p->mischief = (uint8_t)((p->mischief + DISC_IGNORED_MISCHIEF > 100)
                                ? 100 : p->mischief + DISC_IGNORED_MISCHIEF);
        learn(false);
        draw_new_gap();
        persist_mark_dirty("mischief ignored");
    }

    if (now - s_last_roll_ms >= MISCHIEF_CHECK_FAST_MS) {
        s_last_roll_ms = now;
        maybe_misbehave();
    }
}

void discipline_report(void)
{
    const pet_state_t *p = pet_get();
    Serial.println();
    Serial.println("=== DISCIPLINE ============================================");
    Serial.printf("  discipline %.0f   mischief tendency %u   annoyance %u\n",
                  p->discipline, p->mischief, s_annoyance);
    Serial.printf("  window: %s%s\n",
                  s_open == MIS_NONE ? "none open" : discipline_what(s_open),
                  s_open == MIS_NONE ? "" : "  <- pressing Discipline now is FAIR");
    Serial.printf("  lifetime: %u opportunities, %u ignored\n",
                  p->disc_opportunities, p->disc_ignored);
    Serial.printf("  this stage: %u correct, %u unfair\n",
                  p->disc_correct, p->disc_unfair);

    Serial.printf("  learned behaviour %.1f  (0 = always corrected, 100 = always ignored)\n",
                  p->learned_mischief);
    Serial.printf("  next opportunity no sooner than %lu s from the last (gap redrawn each time)\n",
                  (unsigned long)(s_gap_ms / 1000));

    /* Print the ORDERING rather than asking anyone to take it on trust. The
     * effective rate is what the roll actually uses, so a wrong weight shows
     * up here as a wrong order rather than as a feeling. */
    Serial.println("  baseline frequency by stage (spec order: Kid > Baby > Teen > Adult):");
    static const uint8_t ORDER[4] = { STAGE_KID, STAGE_BABY, STAGE_TEEN, STAGE_ADULT };
    for (uint8_t i = 0; i < 4; i++) {
        const uint8_t st = ORDER[i];
        /* The SAME function the roll uses, evaluated for each stage, so the
         * printed ordering cannot drift away from the real one. */
        const int eff = mischief_pct(st);
        const float per_min = 60.0f / (MISCHIEF_CHECK_FAST_MS / 1000.0f)
                            * (eff / 100.0f);
        Serial.printf("    %-6s weight %3u%%  ->  %2d%% per %lu s roll  "
                      "(~one every %.1f min before the gap)  %s\n",
                      pet_stage_name(st), discipline_stage_weight(st), eff,
                      (unsigned long)(MISCHIEF_CHECK_FAST_MS / 1000),
                      (double)(per_min > 0.0f ? 1.0f / per_min : 0.0f),
                      st == p->stage ? "<- current" : "");
    }
    Serial.printf("    plus a randomised %lu-%lu s gap after each one, so nothing\n",
                  (unsigned long)(MISCHIEF_GAP_MIN_MS / 1000),
                  (unsigned long)(MISCHIEF_GAP_MAX_MS / 1000));
    Serial.println("    is ever back to back.");
    Serial.println("  needs are NEVER misconduct: hunger, tiredness, dirt, an accident");
    Serial.println("  from an ignored need, and refusing food when genuinely full.");
    Serial.println("-----------------------------------------------------------");
}
