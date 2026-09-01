/* ===========================================================================
 * stagejump - does an absence that crosses MORE THAN ONE stage boundary
 * produce the same Visitor as the same time passing with the device on?
 *
 * Links the SHIPPED src/pet.cpp and src/evolve.cpp. The only thing this file
 * reimplements is the ~20-line stage-transition block that sim_catch_up()
 * and care_tick() each contain, because linking sim.cpp would drag in the
 * whole care/rtc/farewell tree. That block is copied VERBATIM from
 * src/sim.cpp so the comparison is against the real control flow.
 * ======================================================================== */
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "forms.h"
#include "pet.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "evolve.h"

int g_serial_quiet = 1;
HostSerial Serial;
/* A real PRNG: evolve_new_personality() rejection-samples trait_b, so a
 * constant-valued stub spins forever. */
static unsigned long g_rng = 12345;
long random(long lo, long hi) {
    g_rng = g_rng * 1103515245UL + 12345UL;
    return (hi > lo) ? lo + (long)((g_rng >> 16) % (unsigned long)(hi - lo)) : lo;
}
long random(long hi) { return random(0, hi); }

/* --- clock the shipped pet.cpp reads ------------------------------------ */
static uint32_t g_now = 0;
static bool     g_lights = false;
extern "C" {
uint32_t rtc_now(void)     { return g_now; }
bool     rtc_trusted(void) { return true; }
bool care_lights_on(void) { return g_lights; }
void care_new_bath_target(void) {}
const char *gamerec_name(uint8_t g) { (void)g; return "?"; }
uint8_t gamerec_favorite(void) { return 0; }
void gamerec_on_stage_change(uint8_t s) { (void)s; }
void journal_add(uint8_t a, uint8_t b, uint8_t c) { (void)a;(void)b;(void)c; }
void journal_load(const void *b) { (void)b; }
void persist_mark_dirty(const char *r) { (void)r; }
void ui_bubble_set_suppressed(bool b) { (void)b; }
void ui_pet_set_wander(bool b) { (void)b; }
void ui_pet_evolve_to(uint8_t f) { (void)f; }
bool ui_bubble_say(bubble_tier_t t, const char *s) { (void)t;(void)s; return true; }
void ui_pet_add_done_cb(pet_anim_done_cb_t cb) { (void)cb; }
void ui_pet_set_baby_palette(int c) { (void)c; }
void audio_play(int s) { (void)s; }
}

static int g_trace = 0;
static void trace(const char *arm)
{
    if (!g_trace) return;
    const pet_state_t *p = pet_get();
    const float asof = (float)p->stage_day[p->stage];
    evo_scores_t sc = evolve_scores_on(asof);
    printf("    [%s] at stage=%s age=%.2f form=%-12s stage_start_day=%u "
           "stage_days=%.2f games=%u engage=%.2f cs=%.2f ba=%.2f\n",
           arm, pet_stage_name(p->stage), (double)asof,
           forms_name(p->form_id), p->stage_start_day,
           (double)evolve_stage_days_on(asof), p->games_played,
           (double)sc.engage, (double)sc.cs, (double)sc.ba);
}

/* The stage-transition block, copied verbatim from src/sim.cpp (the offline path)
 * (the offline path). This is the code under test. */
static void apply_boundaries_LIKE_SHIPPED(void)
{
    pet_state_t *p = pet_mutable();
    pet_refresh_age();
    while (pet_apply_one_stage(pet_age_days())) {
        trace("boundary");
        gamerec_on_stage_change(p->stage);
        const uint8_t f = evolve_pick_form_on(p->stage, (float)p->stage_day[p->stage]);
        if (f != p->form_id) {
            p->form_id = f;
            p->evo_announce = 1;
        }
        pet_record_form();
        evolve_on_stage_entered(p->stage, p->stage_day[p->stage]);
    }
}

/* Seed one identical care history in both arms. */
static void seed(pet_state_t *p)
{
    pet_init();
    p = pet_mutable();
    p->hatch_ts = 1000000;
    p->stage    = STAGE_BABY;
    p->form_id  = FORM_BABY;
    /* A GOOD but not flawless history: comfortably on the Good-Kid side of
     * the line, and near enough the Bright/Mellow teen line that a spurious
     * -5 bias changes the answer. This is the case the bug is visible in. */
    p->care_happy = 72.0f; p->care_fed = 78.0f; p->care_clean = 70.0f;
    p->care_sleep = 82.0f; p->care_discipline = 62.0f; p->nutrition = 8.0f;
    p->weight_g = 50.0f;
    p->games_played = 6; p->meals = 8; p->junk_meals = 0;
    p->ignored_requests = 0; p->disc_unfair = 0;
    p->stage_start_day = 0;
}

static void dump(const char *label)
{
    const pet_state_t *p = pet_get();
    printf("  %-34s stage=%-5s form=%-12s evo_path=[%s / %s / %s / %s]\n",
           label, pet_stage_name(p->stage), forms_name(p->form_id),
           forms_name(p->evo_path[0]),
           p->evo_path[1] ? forms_name(p->evo_path[1]) : "(not recorded)",
           p->evo_path[2] ? forms_name(p->evo_path[2]) : "(not recorded)",
           p->evo_path[3] ? forms_name(p->evo_path[3]) : "(not recorded)");
}

static void seed_custom(float happy, float fed, float clean, float sleep,
                        float disc, float junk, float wt, int games)
{
    pet_init();
    pet_state_t *p = pet_mutable();
    p->hatch_ts = 1000000;
    p->stage = STAGE_BABY; p->form_id = FORM_BABY;
    p->care_happy = happy; p->care_fed = fed; p->care_clean = clean;
    p->care_sleep = sleep; p->care_discipline = disc; p->nutrition = junk;
    p->weight_g = wt; p->games_played = games; p->meals = 8; p->junk_meals = 0;
    p->ignored_requests = 0; p->disc_unfair = 0; p->stage_start_day = 0;
}

static void run_arm(bool stepwise, uint8_t *form_out, uint8_t *path_out)
{
    pet_state_t *p = pet_mutable();
    if (stepwise) {
        for (int h = 1; h <= 24 * 4; h++) {
            g_now = p->hatch_ts + (uint32_t)h * 3600u;
            apply_boundaries_LIKE_SHIPPED();
        }
    } else {
        g_now = p->hatch_ts + 4u * 86400u;
        apply_boundaries_LIKE_SHIPPED();
    }
    *form_out = p->form_id;
    memcpy(path_out, p->evo_path, 4);
}

int main(void)
{
    printf("\n=== STAGE-BOUNDARY JUMP SWEEP =============================\n");
    printf("Same care history, two shapes of elapsed time:\n");
    printf("  ARM A  device ON  - boundaries crossed one at a time\n");
    printf("  ARM B  device OFF - day 0 -> day 4 in ONE catch-up\n");
    printf("Both cross Baby->Kid (1.0) and Kid->Teen (3.0).\n\n");

    const float HAPPY[] = {30, 45, 55, 65, 75, 85, 95};
    const float CLEAN[] = {30, 50, 65, 80, 95};
    const float DISC[]  = {35, 50, 62, 75, 90};
    const float FED[]   = {40, 60, 80, 95};
    const float SLEEP[] = {45, 70, 95};
    const int   GAMES[] = {0, 3, 8, 14};

    /* Trace the known divergent case first. */
    g_trace = 1;
    {
        uint8_t fa, fb; uint8_t pa[4], pb[4];
        printf("  TRACE happy 30 fed 60 clean 30 sleep 95 disc 35 games 3\n");
        printf("  ARM A (stepwise):\n");
        seed_custom(30, 60, 30, 95, 35, 8.0f, 50.0f, 3); run_arm(true,  &fa, pa);
        printf("  ARM B (one jump):\n");
        seed_custom(30, 60, 30, 95, 35, 8.0f, 50.0f, 3); run_arm(false, &fb, pb);
        printf("  -> A=%s  B=%s\n\n", forms_name(fa), forms_name(fb));
    }
    g_trace = 0;

    long total = 0, path_diff = 0, form_diff = 0;
    char first_example[256] = "";

    for (float h : HAPPY) for (float c : CLEAN) for (float d : DISC)
    for (float f : FED) for (float sl : SLEEP) for (int g : GAMES) {
        uint8_t fa, fb; uint8_t pa[4], pb[4];
        seed_custom(h, f, c, sl, d, 8.0f, 50.0f, g); run_arm(true,  &fa, pa);
        seed_custom(h, f, c, sl, d, 8.0f, 50.0f, g); run_arm(false, &fb, pb);
        total++;
        if (memcmp(pa, pb, 4) != 0) path_diff++;
        if (fa != fb) {
            form_diff++;
            if (!first_example[0])
                snprintf(first_example, sizeof(first_example),
                    "happy %.0f fed %.0f clean %.0f sleep %.0f disc %.0f games %d"
                    "  ->  ON gives %s, OFF gives %s",
                    h, f, c, sl, d, g, forms_name(fa), forms_name(fb));
        }
    }

    printf("  care histories swept        : %ld\n", total);
    printf("  evo_path[] differs          : %ld  (%.1f%%)\n",
           path_diff, 100.0 * path_diff / total);
    printf("  FINAL TEEN FORM differs     : %ld  (%.1f%%)\n",
           form_diff, 100.0 * form_diff / total);
    if (first_example[0]) printf("  first divergent case        : %s\n", first_example);
    printf("\n  RESULT: %s\n", (path_diff == 0 && form_diff == 0) ? "PASS" : "FAIL");
    printf("------------------------------------------------------------\n");
    return (path_diff == 0 && form_diff == 0) ? 0 : 1;
}
