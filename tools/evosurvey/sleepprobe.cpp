/* Probe: can care_sleep ever fall under the Grumpy threshold (40 - EVO_EPS)?
 * care_sleep is seeded at 50 and evolve_accumulate() only ever EMAs it toward
 * 45 (lights on) or 95 (lights off), so the question is whether an EMA that
 * is bounded below by 45 can cross 39.25. Driven through the REAL
 * evolve_accumulate() rather than argued on paper. */
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
#include "audio.h"

int g_serial_quiet = 1;
HostSerial Serial;
long random(long lo, long hi) { return lo; }
long random(long hi) { return 0; }
static pet_state_t g_pet;
static float g_age_days = 0.0f;
static bool  g_lights = true;
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
void ui_pet_add_done_cb(pet_anim_done_cb_t cb) {}
/* Phase 10 gave evolve_present() a sound. Without this stub neither host
 * harness links, which is how both silently stopped building at the
 * phase10-feature-baseline tag. */
void audio_play(snd_t s) { (void)s; }
}

int main(void)
{
    const float THRESH = 40.0f - EVO_EPS;
    printf("Grumpy sleep clause: fires when care_sleep < %.2f\n", (double)THRESH);
    printf("care_sleep seed = 50.0; samples = 45 (lights ON) or 95 (lights OFF)\n\n");

    /* Worst case for sleep quality: lights left on for EVERY sleep hour, for
     * the whole 6-day visit and then far beyond it. */
    memset(&g_pet, 0, sizeof(g_pet));
    g_pet.care_sleep = 50.0f;
    g_lights = true;
    float worst = g_pet.care_sleep;
    const int DAYS = 60;                    /* ten times the whole visit    */
    for (int d = 0; d < DAYS; d++) {
        for (int h = 0; h < 11; h++) {      /* 20:00-07:00 asleep, lights on */
            evolve_accumulate(1.0f, true);
            if (g_pet.care_sleep < worst) worst = g_pet.care_sleep;
        }
        if (d == 0 || d == 2 || d == 5 || d == 13 || d == 59)
            printf("  after %2d day(s) of lights-on sleep: care_sleep = %.4f\n",
                   d + 1, (double)g_pet.care_sleep);
    }
    printf("\n  lowest care_sleep attainable in %d days: %.4f\n", DAYS, (double)worst);
    printf("  threshold needed:                       %.4f\n", (double)THRESH);
    printf("  clause reachable: %s\n\n", worst < THRESH ? "YES" : "NO - the EMA is bounded below by the 45 sample");

    /* And the same question asked of the OTHER Grumpy clause, care_happy. */
    memset(&g_pet, 0, sizeof(g_pet));
    g_pet.care_happy = 60.0f;
    g_pet.happiness  = 0.0f;
    float hworst = g_pet.care_happy;
    for (int h = 0; h < 48; h++) {
        evolve_accumulate(1.0f, false);
        if (g_pet.care_happy < hworst) hworst = g_pet.care_happy;
    }
    printf("  care_happy after 48 h of zero happiness: %.4f  -> clause %s\n",
           (double)g_pet.care_happy, hworst < THRESH ? "REACHABLE" : "unreachable");
    return 0;
}
