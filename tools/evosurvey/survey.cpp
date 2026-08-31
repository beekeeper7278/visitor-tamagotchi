/* ===========================================================================
 * evosurvey - offline evolution reachability survey.
 *
 * Compiles and drives the SHIPPED src/evolve.cpp. Nothing here reimplements
 * evolve_scores(), evolve_pick_form() or the EMA - that is the entire point:
 * a survey that proves a copy correct proves nothing about the device.
 *
 * Reachability is asked as: "does an ACHIEVABLE care regime, driven hour by
 * hour through the real accumulator, land on this form?" - not "can these
 * floats be assigned". A form only counts as reachable if the accumulator
 * state that selects it was produced by playing.
 * ======================================================================== */
#include <Arduino.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <vector>
#include <string>

#include "config.h"
#include "forms.h"
#include "pet.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "evolve.h"

int g_serial_quiet = 1;
HostSerial Serial;
long random(long lo, long hi) { return lo + (hi > lo ? (lo * 7919 + 13) % (hi - lo) : 0); }
long random(long hi) { return random(0, hi); }

/* --- the state under test ------------------------------------------------ */
static pet_state_t g_pet;
static float       g_age_days = 0.0f;
static bool        g_lights   = false;

extern "C" {
pet_state_t *pet_mutable(void) { return &g_pet; }
const pet_state_t *pet_get(void) { return &g_pet; }
float pet_age_days(void) { return g_age_days; }
const char *pet_stage_name(uint8_t s) { (void)s; return "?"; }
void pet_record_form(void) {}
bool care_lights_on(void) { return g_lights; }
void care_new_bath_target(void) {}
const char *gamerec_name(uint8_t g) { (void)g; return "?"; }
uint8_t gamerec_favorite(void) { return 0; }
void journal_add(uint8_t a, uint8_t b, uint8_t c) { (void)a;(void)b;(void)c; }
void persist_mark_dirty(const char *r) { (void)r; }
void ui_bubble_set_suppressed(bool b) { (void)b; }
void ui_pet_set_wander(bool b) { (void)b; }
void ui_pet_evolve_to(uint8_t f) { (void)f; }
bool ui_bubble_say(bubble_tier_t t, const char *s) { (void)t;(void)s; return true; }
void ui_pet_set_done_cb(pet_anim_done_cb_t cb) { (void)cb; }
}

/* --- a care regime: what the player actually does for a whole stage ------ */
struct regime {
    float happy, hunger, clean, disc;
    bool  lights_on_at_night;
    float junk_frac;
    int   games, ignored, unfair;
    float weight;
};

static std::vector<regime> build_grid(void)
{
    std::vector<regime> g;
    const float HAPPY[] = {0, 35, 55, 75, 100};
    const float CLEAN[] = {0, 35, 55, 75, 100};
    const float DISC[]  = {0, 30, 50, 70, 100};
    const float HUNG[]  = {15, 65};          /* starved / comfortable       */
    const float JUNK[]  = {0.0f, 1.0f};      /* never cake / only cake      */
    const int   GAMES[] = {0, 3, 12};
    const int   IGN[]   = {0, 5};
    const int   UNF[]   = {0, 3};
    const float WT[]    = {45, 90};
    for (float h : HAPPY) for (float c : CLEAN) for (float d : DISC)
    for (float hu : HUNG) for (float j : JUNK) for (int ga : GAMES)
    for (int ig : IGN) for (int un : UNF) for (float w : WT)
    for (int li = 0; li < 2; li++)
        g.push_back({h, hu, c, d, (bool)li, j, ga, ig, un, w});
    return g;
}

/* Priors: the accumulator state a stage can be ENTERED with. The EMA washes
 * these out over a multi-day stage, but not completely, so the sweep is run
 * from all three rather than from the default alone. */
enum { PRIOR_FRESH = 0, PRIOR_PERFECT, PRIOR_NEGLECT, PRIOR_COUNT };
static const char *PRIOR_NAME[] = {"fresh", "after-perfect", "after-neglect"};

static void set_prior(int prior)
{
    pet_state_t *p = &g_pet;
    if (prior == PRIOR_FRESH) {
        p->care_happy = p->care_fed = p->care_clean = 60.0f;
        p->care_sleep = p->care_discipline = 50.0f;
        p->nutrition = 0.0f;
    } else if (prior == PRIOR_PERFECT) {
        p->care_happy = p->care_fed = p->care_clean = 98.0f;
        p->care_sleep = 95.0f; p->care_discipline = 98.0f;
        p->nutrition = 0.0f;
    } else {
        p->care_happy = p->care_fed = p->care_clean = 5.0f;
        p->care_sleep = 45.0f; p->care_discipline = 3.0f;
        p->nutrition = 100.0f;
    }
}

/* Run one stage hour by hour through the REAL accumulator, then ask the REAL
 * selector what form the Visitor enters the next stage with. */
static uint8_t run_stage(uint8_t stage, float days, const regime &r,
                         int prior, uint8_t form_in, evo_scores_t *out)
{
    pet_state_t *p = &g_pet;
    set_prior(prior);
    p->form_id         = form_in;
    p->stage_start_day = 0;
    g_age_days         = 0.0f;
    p->happiness = r.happy; p->hunger = r.hunger;
    p->cleanliness = r.clean; p->discipline = r.disc;
    p->weight_g = r.weight;
    p->games_played = 0; p->ignored_requests = 0; p->disc_unfair = 0;
    p->meals = 0; p->junk_meals = 0;

    const int hours = (int)(days * 24.0f + 0.5f);
    for (int h = 0; h < hours; h++) {
        const int hod = h % 24;
        /* SLEEP_START_HOUR 20 .. SLEEP_END_HOUR 7 */
        const bool asleep = (hod >= SLEEP_START_HOUR || hod < SLEEP_END_HOUR);
        g_lights = r.lights_on_at_night ? true : !asleep;
        /* A meal every 6 waking hours, junk per the regime. */
        if (!asleep && (h % 6) == 0) {
            p->meals++;
            if (r.junk_frac >= 1.0f) p->junk_meals++;
            else if (r.junk_frac > 0.0f && (p->meals % 2) == 0) p->junk_meals++;
        }
        evolve_accumulate(1.0f, asleep);
        g_age_days += 1.0f / 24.0f;
    }
    /* Per-stage counters are totals for the stage, applied before selection. */
    p->games_played     = (uint16_t)r.games;
    p->ignored_requests = (uint16_t)r.ignored;
    p->disc_unfair      = (uint16_t)r.unfair;
    if (out) *out = evolve_scores();
    return evolve_pick_form(stage);
}

struct hit { bool reached; regime r; int prior; uint8_t form_in; evo_scores_t s;
             long count; float sleep_acc, happy_acc, clean_acc; };

static void report(const char *title, hit *h, const uint8_t *forms, int nforms)
{
    printf("\n=== %s ===\n", title);
    for (int i = 0; i < nforms; i++) {
        const uint8_t f = forms[i];
        if (!h[f].reached) { printf("  %-14s UNREACHABLE\n", forms_name(f)); continue; }
        printf("  %-14s REACHABLE  (%ld of the swept regimes)\n", forms_name(f), h[f].count);
        printf("      via: happy %.0f hunger %.0f clean %.0f disc %.0f  "
               "lights-at-night %s junk %.0f%% games %d ignored %d unfair %d wt %.0fg  [%s",
               h[f].r.happy, h[f].r.hunger, h[f].r.clean, h[f].r.disc,
               h[f].r.lights_on_at_night ? "ON" : "off", h[f].r.junk_frac * 100,
               h[f].r.games, h[f].r.ignored, h[f].r.unfair, h[f].r.weight,
               PRIOR_NAME[h[f].prior]);
        if (h[f].form_in != 0xFF) printf(", from %s", forms_name(h[f].form_in));
        printf("]\n");
        printf("      CS %.2f  BA %.2f  IA %.2f  engage %.1f | acc: happy %.1f clean %.1f sleep %.1f\n",
               h[f].s.cs, h[f].s.ba, h[f].s.ia, h[f].s.engage,
               h[f].happy_acc, h[f].clean_acc, h[f].sleep_acc);
    }
}

int main(void)
{
    std::vector<regime> grid = build_grid();
    printf("evolution reachability survey - ACCUM_HALFLIFE_HOURS %.0f, EVO_EPS %.2f\n",
           (double)ACCUM_HALFLIFE_HOURS, (double)EVO_EPS);
    printf("driving the shipped evolve_accumulate() / evolve_scores() / evolve_pick_form()\n");
    printf("%zu care regimes x %d entry priors, per decision\n", grid.size(), PRIOR_COUNT);

    static hit H[FORM_COUNT];
    memset(H, 0, sizeof(H));

    auto record = [&](uint8_t f, const regime &r, int prior, uint8_t fin,
                      const evo_scores_t &s) {
        H[f].count++;
        if (H[f].reached) return;
        H[f].reached = true; H[f].r = r; H[f].prior = prior;
        H[f].form_in = fin; H[f].s = s;
        H[f].happy_acc = g_pet.care_happy; H[f].clean_acc = g_pet.care_clean;
        H[f].sleep_acc = g_pet.care_sleep;
    };

    /* --- KID: decided on the 1-day Baby stage ---------------------------- */
    for (int pr = 0; pr < PRIOR_COUNT; pr++)
        for (const regime &r : grid) {
            evo_scores_t s;
            uint8_t f = run_stage(STAGE_KID, STAGE_DAY_KID - 0.0f, r, pr, FORM_BABY, &s);
            record(f, r, pr, 0xFF, s);
        }

    /* --- TEEN: decided on the 2-day Kid stage, biased by the kid form ---- */
    const uint8_t KIDS[] = { FORM_KID_GOOD, FORM_KID_MISCHIEF };
    for (uint8_t kid : KIDS)
        for (int pr = 0; pr < PRIOR_COUNT; pr++)
            for (const regime &r : grid) {
                evo_scores_t s;
                uint8_t f = run_stage(STAGE_TEEN, STAGE_DAY_TEEN - STAGE_DAY_KID,
                                      r, pr, kid, &s);
                record(f, r, pr, kid, s);
            }

    /* --- ADULT: decided on the 3-day Teen stage -------------------------- */
    const uint8_t TEENS[] = { FORM_TEEN_BRIGHT, FORM_TEEN_MELLOW, FORM_TEEN_ROWDY };
    for (uint8_t tn : TEENS)
        for (int pr = 0; pr < PRIOR_COUNT; pr++)
            for (const regime &r : grid) {
                evo_scores_t s;
                uint8_t f = run_stage(STAGE_ADULT, STAGE_DAY_ADULT - STAGE_DAY_TEEN,
                                      r, pr, tn, &s);
                record(f, r, pr, tn, s);
            }

    H[FORM_BABY].reached = true; H[FORM_BABY].count = -1;
    printf("\n=== BABY ===\n  Baby           REACHABLE  (the start form; no selection involved)\n");

    const uint8_t KIDF[] = {FORM_KID_GOOD, FORM_KID_MISCHIEF};
    const uint8_t TEENF[] = {FORM_TEEN_BRIGHT, FORM_TEEN_MELLOW, FORM_TEEN_ROWDY};
    const uint8_t ADF[] = {FORM_ADULT_BEST, FORM_ADULT_SWEET, FORM_ADULT_PLAYFUL,
                           FORM_ADULT_CHONKY, FORM_ADULT_GRUMPY, FORM_ADULT_SCRUFFY};
    report("KIDS (2)", H, KIDF, 2);
    report("TEENS (3)", H, TEENF, 3);
    report("ADULTS (6)", H, ADF, 6);

    int miss = 0;
    for (int i = 0; i < FORM_COUNT; i++) if (!H[i].reached) miss++;
    printf("\n---------------------------------------------------------------\n");
    printf("RESULT: %d of %d forms reachable%s\n", FORM_COUNT - miss, FORM_COUNT,
           miss ? "  -> FAIL" : "  -> PASS");
    return miss ? 1 : 0;
}
