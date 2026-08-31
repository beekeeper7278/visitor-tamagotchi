/* ===========================================================================
 * growth - what does the growth cycle produce under EXTRA GOOD care?
 *
 * Two questions, deliberately kept apart:
 *
 *   A. LIVED care - hour-by-hour excellent play driven through the real
 *      evolve_accumulate(), accumulators CHAINED across stages the way a
 *      real visit does. This is what a devoted player actually gets.
 *   B. The `+` fixture - the exact values diag_seed_care(2) writes. It sets
 *      the accumulators DIRECTLY, so it can hold care_sleep at 90, which
 *      lived play cannot (the sleep EMA is bounded to 45..95 and only ever
 *      samples 45 or 95). Included so the hardware run can be predicted.
 *
 * Drives the shipped src/evolve.cpp. See README.
 * ======================================================================== */
#include <Arduino.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "config.h"
#include "forms.h"
#include "pet.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "evolve.h"

int g_serial_quiet = 1;
HostSerial Serial;
long random(long lo, long hi) { return lo; }
long random(long hi) { return 0; }
static pet_state_t g_pet;
static float g_age_days = 0.0f;
static bool  g_lights = false;
extern "C" {
pet_state_t *pet_mutable(void) { return &g_pet; }
const pet_state_t *pet_get(void) { return &g_pet; }
float pet_age_days(void) { return g_age_days; }
const char *pet_stage_name(uint8_t s) { return "?"; }
void pet_record_form(void) {}
bool care_lights_on(void) { return g_lights; }
void care_new_bath_target(void) {}
const char *gamerec_name(uint8_t g) { return "?"; }
uint8_t gamerec_favorite(void) { return 0; }
void journal_add(uint8_t a, uint8_t b, uint8_t c) {}
void persist_mark_dirty(const char *r) {}
void ui_bubble_set_suppressed(bool b) {}
void ui_pet_set_wander(bool b) {}
void ui_pet_evolve_to(uint8_t f) {}
bool ui_bubble_say(bubble_tier_t t, const char *s) { return true; }
void ui_pet_set_done_cb(pet_anim_done_cb_t cb) {}
}

struct care { float happy, hunger, clean, disc; int games_per_day; float junk_frac; };

static void live_stage(const char *label, uint8_t stage, float from_day,
                       float to_day, const care &c)
{
    pet_state_t *p = &g_pet;
    p->stage_start_day = (uint16_t)from_day;
    p->happiness = c.happy; p->hunger = c.hunger;
    p->cleanliness = c.clean; p->discipline = c.disc;
    p->ignored_requests = 0; p->disc_unfair = 0;
    p->games_played = 0; p->meals = 0; p->junk_meals = 0;

    const float days = to_day - from_day;
    const int hours = (int)(days * 24.0f + 0.5f);
    for (int h = 0; h < hours; h++) {
        const int hod = (int)((from_day * 24.0f) + h) % 24;
        const bool asleep = (hod >= SLEEP_START_HOUR || hod < SLEEP_END_HOUR);
        g_lights = !asleep;                    /* attentive: lights OUT at night */
        if (!asleep && (h % 6) == 0) {
            p->meals++;
            if (c.junk_frac > 0.0f && (p->meals % 5) == 0) p->junk_meals++;
        }
        evolve_accumulate(1.0f, asleep);
        g_age_days += 1.0f / 24.0f;
    }
    p->games_played = (uint16_t)(c.games_per_day * (days < 1 ? 1 : (int)days));

    evo_scores_t s = evolve_scores();
    const uint8_t f = evolve_pick_form(stage);
    printf("  %-6s day %.0f-%.0f  -> %-13s  CS %6.2f BA %6.2f IA %5.2f eng %5.1f | "
           "happy %.1f fed %.1f clean %.1f sleep %.1f disc %.1f\n",
           label, from_day, to_day, forms_name(f), s.cs, s.ba, s.ia, s.engage,
           p->care_happy, p->care_fed, p->care_clean, p->care_sleep,
           p->care_discipline);
    p->form_id = f;
}

static void lived(const char *title, const care &c)
{
    memset(&g_pet, 0, sizeof(g_pet));
    g_pet.care_happy = g_pet.care_fed = g_pet.care_clean = 60.0f;
    g_pet.care_sleep = g_pet.care_discipline = 50.0f;
    g_pet.weight_g = 45.0f; g_pet.form_id = FORM_BABY;
    g_age_days = 0.0f;
    printf("\n%s\n", title);
    live_stage("Baby",  STAGE_KID,   0.0f, STAGE_DAY_KID,   c);
    live_stage("Kid",   STAGE_TEEN,  STAGE_DAY_KID,  STAGE_DAY_TEEN,  c);
    live_stage("Teen",  STAGE_ADULT, STAGE_DAY_TEEN, STAGE_DAY_ADULT, c);
    printf("  lineage: Baby -> %s\n", forms_name(g_pet.form_id));
}

/* The exact values diag_seed_care(2) writes, per src/diag.cpp. */
static void seed_plus(void)
{
    pet_state_t *p = &g_pet;
    p->care_happy = 92.0f; p->care_fed = 95.0f; p->care_clean = 88.0f;
    p->care_sleep = 90.0f; p->care_discipline = 78.0f; p->nutrition = 10.0f;
    p->happiness = 95.0f; p->hunger = 70.0f; p->cleanliness = 90.0f;
    p->energy = 90.0f; p->discipline = 78.0f;
    p->meals = 10; p->junk_meals = 1;
    p->ignored_requests = 0; p->disc_unfair = 0; p->disc_correct = 3;
    p->weight_g = 52.0f;
}

int main(void)
{
    printf("growth cycle under extra good care - shipped evolve.cpp, "
           "half-life %.0f h, EVO_EPS %.2f\n",
           (double)ACCUM_HALFLIFE_HOURS, (double)EVO_EPS);

    /* A. lived care, accumulators chained across the whole visit */
    lived("A1. DEVOTED but human  (happy 95, clean 90, disc 78, 6 games/day, "
          "20% junk, lights out)",
          {95, 65, 90, 78, 6, 0.2f});
    lived("A2. FLAWLESS           (happy 100, clean 100, disc 100, 12 games/day, "
          "no junk, lights out)",
          {100, 65, 100, 100, 12, 0.0f});
    lived("A3. GOOD, few games    (happy 90, clean 85, disc 75, 1 game/day, "
          "no junk, lights out)",
          {90, 65, 85, 75, 1, 0.0f});

    /* B. the `+` fixture, evaluated at each stage the way the device would */
    printf("\nB. the `+` EXCELLENT fixture (diag_seed_care(2)), evaluated per stage\n");
    const char *NM[] = {"Kid", "Teen", "Adult"};
    const uint8_t ST[] = {STAGE_KID, STAGE_TEEN, STAGE_ADULT};
    const float FROM[] = {0.0f, STAGE_DAY_KID, STAGE_DAY_TEEN};
    const float AT[]   = {STAGE_DAY_KID, STAGE_DAY_TEEN, STAGE_DAY_ADULT};
    uint8_t prev = FORM_BABY;
    for (int i = 0; i < 3; i++) {
        memset(&g_pet, 0, sizeof(g_pet));
        seed_plus();
        g_pet.form_id = prev;
        g_pet.stage_start_day = (uint16_t)FROM[i];
        g_age_days = AT[i];
        /* seed_engage(p, 90) sets games so engage lands on 90 */
        g_pet.games_played = (uint16_t)(90.0f * 2.0f * evolve_stage_days() / 100.0f + 0.5f);
        evo_scores_t s = evolve_scores();
        uint8_t f = evolve_pick_form(ST[i]);
        printf("  entering %-5s -> %-13s  CS %6.2f BA %6.2f IA %5.2f eng %5.1f\n",
               NM[i], forms_name(f), s.cs, s.ba, s.ia, s.engage);
        prev = f;
    }
    printf("  lineage: Baby -> %s\n", forms_name(prev));
    return 0;
}
