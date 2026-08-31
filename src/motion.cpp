/* motion - gravity slide, upside-down, shake, annoyance. See motion.h. */

#include <Arduino.h>
#include <math.h>

#include "config.h"
#include "board_pins.h"
#include "motion.h"
#include "settings.h"
#include "diag.h"
#include "pet.h"
#include "care.h"
#include "games.h"
#include "menu.h"
#include "ui_pet.h"
#include "ui_bubble.h"
#include "dialogue.h"
#include "audio.h"

/* --- state --------------------------------------------------------------- */
static float    s_vx, s_vy;             /* slide velocity, px/tick          */
static bool     s_active;               /* gravity is driving the Visitor   */
static bool     s_upside;               /* confirmed upside-down            */
static uint32_t s_upside_since;         /* debounce timer                   */
static uint32_t s_upside_entered;
static uint32_t s_last_complaint;
static uint32_t s_last_tick;
static uint32_t s_last_bump;
static float    s_annoy;                /* 0..100                           */
static uint32_t s_annoy_last;
static uint32_t s_last_shake;
static uint32_t s_shake_window;
static uint8_t  s_shake_count;
static float    s_prev_mag;
static bool     s_was_active;

/* calibration capture */
static bool     s_cal_on;
static uint32_t s_cal_t0;
static uint8_t  s_cal_n;
static float    s_cal_r[MOTION_CAL_SAMPLES], s_cal_d[MOTION_CAL_SAMPLES];

bool    motion_active(void)      { return s_active || s_upside; }
bool    motion_calibrating(void) { return s_cal_on; }
uint8_t motion_annoyance(void)   { return (uint8_t)(s_annoy + 0.5f); }

void motion_begin(void)
{
    s_last_tick = s_annoy_last = millis();
    s_annoy = 0.0f;
}

/* Read the IMU and convert to the display frame the maze established. The
 * calibration offset is subtracted HERE, above the frozen transform. */
static bool read_tilt(float *right, float *down, float *zout)
{
    float gx, gy, gz;
    if (!diag_imu_read_screen(&gx, &gy, &gz)) return false;
    float cr, cd;
    settings_cal(&cr, &cd);
    *right =  gy - cr;
    *down  = -gx - cd;
    if (zout) *zout = gz;
    return true;
}

/* --- annoyance -----------------------------------------------------------
 * Rises with deliberate handling, decays with time, and is FLAVOUR ONLY.
 * A device carried in a backpack all afternoon must not come out with a
 * damaged Visitor, so this touches nothing that is scored: not happiness,
 * not an accumulator, not evolution, not visit quality. */
static void annoy_add(float n)
{
    s_annoy += n;
    if (s_annoy > 100.0f) s_annoy = 100.0f;
}

static void annoy_decay(uint32_t now)
{
    const uint32_t dt = now - s_annoy_last;
    if (dt < 1000) return;
    s_annoy_last = now;
    s_annoy -= MOTION_ANNOY_DECAY_PER_S * (float)(dt / 1000);
    if (s_annoy < 0.0f) s_annoy = 0.0f;
}

/* One complaint at a time, and never a stream of them. */
static void complain(const char *line, snd_t snd)
{
    const uint32_t now = millis();
    if (now - s_last_complaint < MOTION_COMPLAIN_GAP_MS) return;
    s_last_complaint = now;
    if (snd != SND_NONE) audio_play(snd);
    ui_bubble_say(BUBBLE_T1_REACTION, line);
}

/* --- calibration ---------------------------------------------------------- */
void motion_calibrate_start(void)
{
    s_cal_on = true;
    s_cal_t0 = millis();
    s_cal_n  = 0;
    Serial.println("CALIBRATE: hold the Visitor how you normally use it...");
}

static void calibrate_tick(uint32_t now)
{
    float r, d;
    if (!read_tilt(&r, &d, NULL)) { s_cal_on = false; return; }
    /* read_tilt() already subtracted any PREVIOUS calibration, so add it back:
     * we want the absolute neutral, not a correction to a correction. */
    float cr, cd;
    settings_cal(&cr, &cd);
    r += cr; d += cd;

    if (s_cal_n < MOTION_CAL_SAMPLES) {
        s_cal_r[s_cal_n] = r; s_cal_d[s_cal_n] = d; s_cal_n++;
    }
    if (now - s_cal_t0 < MOTION_CAL_MS) return;

    s_cal_on = false;
    if (s_cal_n < MOTION_CAL_SAMPLES / 2) {
        Serial.println("CALIBRATE: too few samples - keeping the old calibration");
        ui_bubble_say(BUBBLE_T1_REACTION, "Let's try that again!");
        return;
    }

    /* Reject a capture taken while the device was MOVING. A calibration
     * recorded mid-wave silently tilts every future reading, and the player
     * would experience it as a broken Visitor rather than a bad capture. */
    float mr = 0, md = 0;
    for (uint8_t i = 0; i < s_cal_n; i++) { mr += s_cal_r[i]; md += s_cal_d[i]; }
    mr /= s_cal_n; md /= s_cal_n;
    float spread = 0;
    for (uint8_t i = 0; i < s_cal_n; i++) {
        const float dr = s_cal_r[i] - mr, dd = s_cal_d[i] - md;
        const float e = sqrtf(dr * dr + dd * dd);
        if (e > spread) spread = e;
    }
    if (spread > MOTION_CAL_MAX_SPREAD) {
        Serial.printf("CALIBRATE: REJECTED - too unsteady (spread %.3f g > %.3f)\n",
                      (double)spread, (double)MOTION_CAL_MAX_SPREAD);
        ui_bubble_say(BUBBLE_T1_REACTION, "Hold me still!");
        audio_play(SND_UI_REFUSE);
        return;
    }
    settings_set_cal(mr, md);
    Serial.printf("CALIBRATE: OK - neutral right %+.3f down %+.3f (spread %.3f)\n",
                  (double)mr, (double)md, (double)spread);
    ui_bubble_say(BUBBLE_T1_REACTION, "Tilt calibrated!");
    audio_play(SND_UI_CONFIRM);
}

/* --- MOTION HISTORY (diagnostic) -----------------------------------------
 * Synchronising a serial capture to a human's hands does not work: two 90 s
 * windows caught a board sitting flat on a desk both times, because the
 * flip happened while the console was not looking. So the DEVICE records the
 * extremes instead, and the flip can happen whenever the tester likes.
 *
 * OFF by default and it costs nothing when off. When ON it reads the IMU even
 * on ticks the ambient logic skips - which is the point: "did Z ever go below
 * the threshold" must be answerable even if a menu was open at the time. */
static bool     s_st_on;
static uint32_t s_st_since, s_st_ticks, s_st_blocked;
static float    s_st_zmin, s_st_zmax, s_st_dmin, s_st_dmax;
static float    s_st_rmin, s_st_rmax, s_st_tiltmax, s_st_jerkmax;
static uint16_t s_st_upside_n, s_st_shake_n;
static float    s_st_annoy_peak;
/* HOW LONG, not how far. min/max conflates a shake spike (which rails the
 * accelerometer at its +/-4 g full scale in a few ms) with a steady hold, and
 * the trigger only cares about the latter: 1.2 s CONTINUOUS below the
 * threshold. Three bands, so a hold that never reaches -0.65 still shows how
 * far over it actually got - which is the difference between "the detector is
 * broken" and "the threshold is stricter than what a person calls upside
 * down". */
static uint32_t s_st_run_start[3], s_st_run_max[3];
static const float ST_BAND[3] = { MOTION_UPSIDE_Z, -0.30f, 0.00f };

void motion_stats_reset(void)
{
    s_st_on = true;
    s_st_since = millis();
    s_st_ticks = s_st_blocked = 0;
    s_st_zmin = s_st_dmin = s_st_rmin =  99.0f;
    s_st_zmax = s_st_dmax = s_st_rmax = -99.0f;
    s_st_tiltmax = s_st_jerkmax = s_st_annoy_peak = 0.0f;
    s_st_upside_n = s_st_shake_n = 0;
    for (int i = 0; i < 3; i++) { s_st_run_start[i] = 0; s_st_run_max[i] = 0; }
    Serial.println("MOTION HISTORY: armed - flip/shake whenever you like, "
                   "then read it with TAB m");
}

bool motion_stats_on(void) { return s_st_on; }

/* --- the main tick -------------------------------------------------------- */
void motion_tick(void)
{
    const uint32_t now = millis();
    if (now - s_last_tick < MOTION_TICK_MS) return;
    s_last_tick = now;

    /* Calibration runs even with gravity reactions OFF - it is a device
     * setting, not an ambient reaction, and Tilt Maze wants it either way. */
    if (s_cal_on) { calibrate_tick(now); return; }

    annoy_decay(now);

    /* History first, and deliberately BEFORE the blocked check, so a flip made
     * while a menu was open still shows up as a Z excursion rather than as
     * silence that looks identical to "the sensor never saw it". */
    if (s_st_on) {
        float sr, sd, sz;
        if (read_tilt(&sr, &sd, &sz)) {
            s_st_ticks++;
            if (sz < s_st_zmin) s_st_zmin = sz;
            if (sz > s_st_zmax) s_st_zmax = sz;
            if (sd < s_st_dmin) s_st_dmin = sd;
            if (sd > s_st_dmax) s_st_dmax = sd;
            if (sr < s_st_rmin) s_st_rmin = sr;
            if (sr > s_st_rmax) s_st_rmax = sr;
            const float t = sqrtf(sr * sr + sd * sd);
            if (t > s_st_tiltmax) s_st_tiltmax = t;
            for (int i = 0; i < 3; i++) {
                if (sz < ST_BAND[i]) {
                    if (!s_st_run_start[i]) s_st_run_start[i] = now;
                    const uint32_t run = now - s_st_run_start[i];
                    if (run > s_st_run_max[i]) s_st_run_max[i] = run;
                } else {
                    s_st_run_start[i] = 0;
                }
            }
        }
        if (s_annoy > s_st_annoy_peak) s_st_annoy_peak = s_annoy;
    }

    const pet_state_t *p = pet_get();
    /* Ambient motion never fights something that owns the screen, and never
     * wakes a sleeping Visitor - sleep outranks handling, exactly as a poke
     * does. */
    const bool blocked = !settings_gravity_on() || games_active() || menu_is_open()
                       || p->asleep || p->stage == STAGE_EGG;
    if (blocked) {
        if (s_st_on) s_st_blocked++;
        if (s_was_active) {
            /* Hand the Visitor back cleanly rather than leaving it stranded
             * mid-slide with wandering switched off. */
            s_active = false; s_upside = false; s_vx = s_vy = 0.0f;
            ui_pet_set_wander(true);
            s_was_active = false;
        }
        return;
    }

    float right, down, gz;
    if (!read_tilt(&right, &down, &gz)) return;

    /* --- shake ----------------------------------------------------------
     * Deliberate shaking is a JERK - a fast change in magnitude - not merely
     * a large one. Carrying the device tilts it a lot and jerks it little,
     * which is what keeps a walk to school from reading as an assault. */
    const float mag = sqrtf(right * right + down * down + gz * gz);
    const float jerk = fabsf(mag - s_prev_mag);
    s_prev_mag = mag;
    if (s_st_on && jerk > s_st_jerkmax) s_st_jerkmax = jerk;
    if (jerk > MOTION_SHAKE_JERK_G) {
        if (now - s_shake_window > MOTION_SHAKE_WINDOW_MS) s_shake_count = 0;
        s_shake_window = now;
        if (s_shake_count < 255) s_shake_count++;
        if (s_shake_count >= MOTION_SHAKE_MIN && now - s_last_shake > MOTION_SHAKE_GAP_MS) {
            s_last_shake = now;
            if (s_st_on) s_st_shake_n++;
            annoy_add(MOTION_ANNOY_PER_SHAKE);
            if (s_annoy >= MOTION_ANNOY_CROSS) {
                complain(dialogue_shaken_annoyed(), SND_ANNOYED);
                ui_pet_play(PET_ANIM_REACT);
            } else {
                complain(dialogue_shaken(), SND_DIZZY);
                ui_pet_play(PET_ANIM_REACT);
            }
        }
    }

    /* --- upside down -----------------------------------------------------
     * Screen +Z points INTO the screen, so a strongly negative z means the
     * panel is facing the floor. Debounced, because a quick flip while
     * putting the device down is not "being held upside down". */
    const bool z_flipped = (gz < MOTION_UPSIDE_Z);
    if (z_flipped) {
        if (!s_upside_since) s_upside_since = now;
        if (!s_upside && now - s_upside_since >= MOTION_UPSIDE_MS) {
            s_upside = true;
            s_upside_entered = now;
            if (s_st_on) s_st_upside_n++;
            ui_pet_set_wander(false);
            annoy_add(MOTION_ANNOY_PER_FLIP);
            complain(dialogue_upside_down(), SND_UPSIDE_DOWN);
        }
    } else {
        s_upside_since = 0;
        if (s_upside) {
            s_upside = false;
            /* Relief, and only if being upside down actually lasted. */
            if (now - s_upside_entered > MOTION_UPSIDE_RELIEF_MS) {
                ui_bubble_say(BUBBLE_T1_REACTION, dialogue_upright_relief());
                audio_play(SND_HAPPY);
                ui_pet_play(PET_ANIM_HAPPY);
            }
            ui_pet_set_wander(true);
            s_vx = s_vy = 0.0f;
        }
    }

    /* While upside down the Visitor is pinned at the low edge, complaining
     * now and then rather than sliding around. */
    if (s_upside) {
        s_active = true; s_was_active = true;
        lv_coord_t x = ui_pet_x(), y = ui_pet_y();
        if (y < PET_ROAM_Y_MAX) y += 3;
        ui_pet_place(x, y);
        if (now - s_last_complaint > MOTION_COMPLAIN_GAP_MS * 2)
            complain(dialogue_upside_down(), SND_NONE);
        return;
    }

    /* --- gravity slide ---------------------------------------------------
     * A dead zone so a hand's natural tremor does not jitter the Visitor,
     * and the tilt BEYOND the dead zone drives the acceleration, so a small
     * lean is a drift and a real tip is a slide. */
    const float tilt = sqrtf(right * right + down * down);
    if (tilt < MOTION_DEADZONE_G) {
        /* Near neutral: bleed off and hand control back to wandering. */
        s_vx *= MOTION_DAMP_NEUTRAL; s_vy *= MOTION_DAMP_NEUTRAL;
        if (fabsf(s_vx) < 0.2f && fabsf(s_vy) < 0.2f) {
            s_vx = s_vy = 0.0f;
            if (s_active) { s_active = false; s_was_active = false; ui_pet_set_wander(true); }
            return;
        }
    } else {
        if (!s_active) { s_active = true; s_was_active = true; ui_pet_set_wander(false); }
        const float over = (tilt - MOTION_DEADZONE_G) / tilt;   /* scale, keep direction */
        s_vx += right * over * MOTION_ACCEL;
        s_vy += down  * over * MOTION_ACCEL;
        s_vx *= MOTION_DAMP; s_vy *= MOTION_DAMP;
        if (s_vx >  MOTION_VMAX) s_vx =  MOTION_VMAX;
        if (s_vx < -MOTION_VMAX) s_vx = -MOTION_VMAX;
        if (s_vy >  MOTION_VMAX) s_vy =  MOTION_VMAX;
        if (s_vy < -MOTION_VMAX) s_vy = -MOTION_VMAX;
    }

    if (!s_active) return;

    lv_coord_t x = ui_pet_x(), y = ui_pet_y();
    float nx = (float)x + s_vx, ny = (float)y + s_vy;
    bool bumped = false;

    /* Edges: stop AT the edge and lose the velocity into a squash, rather
     * than sticking. The Visitor must never end up parked outside the roam
     * box with a velocity that keeps pressing it there. */
    if (nx < PET_ROAM_X_MIN) { nx = PET_ROAM_X_MIN; if (s_vx < -MOTION_BUMP_V) bumped = true; s_vx = 0; }
    if (nx > PET_ROAM_X_MAX) { nx = PET_ROAM_X_MAX; if (s_vx >  MOTION_BUMP_V) bumped = true; s_vx = 0; }
    if (ny < PET_ROAM_Y_MIN) { ny = PET_ROAM_Y_MIN; if (s_vy < -MOTION_BUMP_V) bumped = true; s_vy = 0; }
    if (ny > PET_ROAM_Y_MAX) { ny = PET_ROAM_Y_MAX; if (s_vy >  MOTION_BUMP_V) bumped = true; s_vy = 0; }

    ui_pet_place((lv_coord_t)nx, (lv_coord_t)ny);

    if (bumped && now - s_last_bump > MOTION_BUMP_GAP_MS) {
        s_last_bump = now;
        audio_play(SND_GAME_BUMP);
        ui_pet_play(PET_ANIM_REACT);
        annoy_add(MOTION_ANNOY_PER_BUMP);
        if (s_annoy >= MOTION_ANNOY_CROSS)
            complain(dialogue_shaken_annoyed(), SND_NONE);
    }
}

void motion_report(void)
{
    float right = 0, down = 0, gz = 0;
    const bool ok = read_tilt(&right, &down, &gz);
    Serial.println();
    Serial.println("=== MOTION =================================================");
    Serial.printf("  gravity reactions : %s\n", settings_gravity_on() ? "ON" : "OFF");
    Serial.printf("  imu read          : %s\n", ok ? "ok" : "FAILED");
    if (ok) Serial.printf("  display frame     : right %+.3f  down %+.3f  z %+.3f\n",
                          (double)right, (double)down, (double)gz);
    float cr, cd; settings_cal(&cr, &cd);
    Serial.printf("  calibration       : %s  (right %+.3f down %+.3f)\n",
                  settings_cal_valid() ? "stored" : "none", (double)cr, (double)cd);
    Serial.printf("  dead zone         : %.2f g   accel %.2f  vmax %.1f px\n",
                  (double)MOTION_DEADZONE_G, (double)MOTION_ACCEL, (double)MOTION_VMAX);
    Serial.printf("  state             : %s%s  v(%.2f, %.2f)\n",
                  s_active ? "ACTIVE" : "idle", s_upside ? " UPSIDE-DOWN" : "",
                  (double)s_vx, (double)s_vy);
    Serial.printf("  annoyance         : %.1f / 100  (decays %.1f per s)\n",
                  (double)s_annoy, (double)MOTION_ANNOY_DECAY_PER_S);
    Serial.println("  annoyance is FLAVOUR ONLY - no accumulator, no evolution,");
    Serial.println("  no visit quality. A backpack cannot damage a Visitor.");
    if (s_st_on) {
        Serial.printf("  --- HISTORY over %.1f s (TAB S to re-arm) ---\n",
                      (double)((millis() - s_st_since) / 1000.0f));
        Serial.printf("    z      %+.2f .. %+.2f   threshold for UPSIDE-DOWN is < %+.2f\n",
                      (double)s_st_zmin, (double)s_st_zmax, (double)MOTION_UPSIDE_Z);
        Serial.printf("    down   %+.2f .. %+.2f   (negative = gravity toward SCREEN TOP,\n",
                      (double)s_st_dmin, (double)s_st_dmax);
        Serial.println("                            i.e. the device turned 180 in-plane)");
        Serial.printf("    right  %+.2f .. %+.2f   peak tilt %.2f g   peak jerk %.2f g\n",
                      (double)s_st_rmin, (double)s_st_rmax,
                      (double)s_st_tiltmax, (double)s_st_jerkmax);
        Serial.printf("    events: upside-down %u   shake %u   peak annoyance %.1f\n",
                      (unsigned)s_st_upside_n, (unsigned)s_st_shake_n,
                      (double)s_st_annoy_peak);
        Serial.printf("    ticks %lu, of which %lu were BLOCKED (menu/game/asleep/egg/off)\n",
                      (unsigned long)s_st_ticks, (unsigned long)s_st_blocked);
        Serial.println("    LONGEST CONTINUOUS HOLD past each z level:");
        Serial.printf("      z < %+.2f (the trigger) : %4lu ms   %s\n",
                      (double)ST_BAND[0], (unsigned long)s_st_run_max[0],
                      s_st_run_max[0] >= MOTION_UPSIDE_MS
                          ? "<- long enough, SHOULD have fired"
                          : "<- never held long enough (needs 1200)");
        Serial.printf("      z < -0.30 (past ~110 deg): %4lu ms\n",
                      (unsigned long)s_st_run_max[1]);
        Serial.printf("      z <  0.00 (past vertical): %4lu ms\n",
                      (unsigned long)s_st_run_max[2]);
        if (s_st_zmin > MOTION_UPSIDE_Z)
            Serial.println("    -> Z NEVER crossed the threshold: whatever was done to the");
        if (s_st_zmin > MOTION_UPSIDE_Z)
            Serial.println("       device, the panel never faced the floor.");
    }
    Serial.println("-----------------------------------------------------------");
}
