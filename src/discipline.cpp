/* discipline - contextual telling-off. See discipline.h. */

#include <Arduino.h>
#include "config.h"
#include "pet.h"
#include "care.h"
#include "menu.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "evolve.h"
#include "persist.h"
#include "discipline.h"

static mischief_t s_open;
static uint32_t   s_opened_ms, s_last_mis_ms, s_last_roll_ms;
static uint8_t    s_annoyance;
static uint32_t   s_annoy_ms;

void discipline_init(void)
{
    s_open = MIS_NONE;
    s_opened_ms = s_last_mis_ms = 0;
    s_last_roll_ms = millis();
    s_annoyance = 0;
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

static const char *mischief_line(mischief_t m)
{
    switch (m) {
        case MIS_THREW_FOOD:   return "Whoops! It slipped. Honest.";
        case MIS_BEDTIME:      return "I'm NOT sleepy!";
        case MIS_MESS:         return "Look what I made!";
        default:               return "CAKE! Cake cake cake!";
    }
}

void discipline_misbehave(mischief_t what)
{
    if (what == MIS_NONE || s_open != MIS_NONE) return;
    s_open = what;
    s_opened_ms = millis();
    s_last_mis_ms = s_opened_ms;
    pet_mutable()->disc_opportunities++;

    ui_pet_play(PET_ANIM_REACT);
    ui_bubble_say(BUBBLE_T2_MOOD, mischief_line(what));
    Serial.printf("MISCHIEF: %s - discipline window open for %lus\n",
                  discipline_what(what), (unsigned long)(DISC_WINDOW_MS / 1000));
}

/* Personality colours the reaction; it never changes the rules. */
static const char *told_off_line(void)
{
    const pet_state_t *p = pet_get();
    const uint8_t a = p->trait_a, b = p->trait_b;
    if (a == PERS_MISCHIEVOUS || b == PERS_MISCHIEVOUS) return "Worth it.";
    if (a == PERS_DRAMATIC    || b == PERS_DRAMATIC)    return "I KNOW.";
    if (a == PERS_SHY         || b == PERS_SHY)         return "...sorry.";
    if (a == PERS_COMPETITIVE || b == PERS_COMPETITIVE) return "Fine...";
    return "Okay, okay...";
}

void discipline_press(void)
{
    pet_state_t *p = pet_mutable();
    if (p->stage == STAGE_EGG) {
        ui_bubble_say(BUBBLE_T2_MOOD, "It's still an egg!");
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

        ui_pet_play(PET_ANIM_SAD);
        ui_bubble_say(BUBBLE_T1_REACTION, told_off_line());
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

/* Spontaneous mischief. Rolls occasionally; the odds rise with the hidden
 * tendency and fall as discipline climbs, so raising a Visitor well genuinely
 * calms it down. */
static void maybe_misbehave(void)
{
    const pet_state_t *p = pet_get();
    const uint32_t now = millis();

    if (p->stage < STAGE_KID) return;              /* babies are blameless */
    if (p->asleep || care_is_holding()) return;
    if (now - s_last_mis_ms < MISCHIEF_MIN_GAP_MS) return;

    int pct = MISCHIEF_BASE_PCT
            + (int)p->mischief / 8
            - (int)(p->discipline / 12.0f);
    if (p->trait_a == PERS_MISCHIEVOUS || p->trait_b == PERS_MISCHIEVOUS) pct += 4;
    if (p->trait_a == PERS_TIDY        || p->trait_b == PERS_TIDY)        pct -= 3;
    if (pct < 1)  pct = 1;
    if (pct > 30) pct = 30;

    if ((int)random(0, 100) >= pct) return;

    /* What it does depends on what makes sense right now. */
    mischief_t what;
    if (p->hunger > 70.0f && (random(0, 2) == 0)) what = MIS_CAKE_TANTRUM;
    else if (care_mess_count() < 4)               what = MIS_MESS;
    else                                          what = MIS_THREW_FOOD;
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
    }

    if (now - s_last_roll_ms >= MISCHIEF_CHECK_MS) {
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
    Serial.println("-----------------------------------------------------------");
}
