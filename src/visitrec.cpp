/* visitrec - permanent Visit Records. See visitrec.h for the sizing. */

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "storage.h"
#include "forms.h"
#include "pet.h"
#include "care.h"
#include "rtc.h"
#include "gamerec.h"
#include "evolve.h"
#include "visitrec.h"

#define VR_NS  "visitorv"
#define VR_KEY "recs"

typedef struct __attribute__((packed)) {
    uint16_t version;
    uint16_t size;
    uint32_t crc32;
    visit_rec_t rec[VISIT_KEEP];
} vr_blob_t;

#ifdef __cplusplus
static_assert(sizeof(visit_rec_t) == 233, "visit record size changed - re-do the NVS sizing");
#endif

static Preferences s_prefs;
static vr_blob_t   s_blob;
static bool        s_open;

static const size_t VR_CRC_OFF = offsetof(vr_blob_t, crc32) + sizeof(uint32_t);

static uint32_t blob_crc(void)
{
    return storage_crc32((const uint8_t *)&s_blob + VR_CRC_OFF,
                         sizeof(s_blob) - VR_CRC_OFF);
}

static bool blob_save(void)
{
    if (!s_open) return false;
    s_blob.version = VISITREC_VERSION;
    s_blob.size    = sizeof(vr_blob_t);
    s_blob.crc32   = blob_crc();
    return s_prefs.putBytes(VR_KEY, &s_blob, sizeof(s_blob)) == sizeof(s_blob);
}

void visitrec_begin(void)
{
    memset(&s_blob, 0, sizeof(s_blob));
    s_blob.version = VISITREC_VERSION;
    s_blob.size    = sizeof(vr_blob_t);

    s_open = s_prefs.begin(VR_NS, false);
    if (!s_open) { Serial.println("VISITREC: NVS open failed"); return; }

    const size_t stored = s_prefs.getBytesLength(VR_KEY);
    if (stored == 0) { Serial.println("VISITREC: no history yet"); return; }
    if (stored != sizeof(vr_blob_t)) {
        Serial.println("VISITREC: size mismatch -> history reset");
        memset(&s_blob, 0, sizeof(s_blob));
        return;
    }
    vr_blob_t cand;
    if (s_prefs.getBytes(VR_KEY, &cand, sizeof(cand)) != sizeof(cand)) return;
    if (cand.version != VISITREC_VERSION) return;

    const vr_blob_t save = s_blob;
    s_blob = cand;
    if (blob_crc() != cand.crc32) {
        Serial.println("VISITREC: bad CRC -> history reset");
        s_blob = save;
        return;
    }
    Serial.printf("VISITREC: %u past Visitor(s) remembered\n", visitrec_count());
}

uint8_t visitrec_count(void)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < VISIT_KEEP; i++) if (s_blob.rec[i].used) n++;
    return n;
}

const visit_rec_t *visitrec_at(uint8_t idx)
{
    /* slot 0 is the newest; archive shifts the rest down */
    return (idx < VISIT_KEEP && s_blob.rec[idx].used) ? &s_blob.rec[idx] : nullptr;
}

bool visitrec_archive(const char *farewell)
{
    const pet_state_t *p = pet_get();
    const gamerec_t *g = gamerec_get();

    /* newest first: shift everything down, dropping the oldest */
    for (int i = VISIT_KEEP - 1; i > 0; i--) s_blob.rec[i] = s_blob.rec[i - 1];
    visit_rec_t *r = &s_blob.rec[0];
    memset(r, 0, sizeof(*r));

    r->used        = 1;
    r->final_form  = p->form_id;
    r->trait_a     = p->trait_a;
    r->trait_b     = p->trait_b;
    r->arrived_ts  = p->hatch_ts;
    r->departed_ts = rtc_trusted() ? rtc_now() : 0;
    r->days        = p->days_alive;
    memcpy(r->evo_path, p->evo_path, sizeof(r->evo_path));
    r->fav_food    = evolve_favourite_food();
    r->fav_game    = gamerec_favorite();
    for (uint8_t i = 0; i < 4; i++) r->game_best[i] = g->best[i];
    r->maze_best_ms = g->maze_best_ms;
    r->total_games  = g->total_games;

    r->care_happy = (uint8_t)p->care_happy;
    r->care_fed   = (uint8_t)p->care_fed;
    r->care_clean = (uint8_t)p->care_clean;
    r->care_sleep = (uint8_t)p->care_sleep;
    r->care_disc  = (uint8_t)p->care_discipline;

    r->meals            = p->meals;
    r->cakes            = p->cakes_eaten;
    r->accidents        = p->accidents;
    r->lights_forgotten = p->lights_forgotten;
    r->times_dirty      = p->times_dirty;
    r->disc_correct     = p->disc_correct;
    r->disc_ignored     = p->disc_ignored;

    if (farewell) {
        strncpy(r->farewell, farewell, FAREWELL_MAX - 1);
        r->farewell[FAREWELL_MAX - 1] = 0;
    }

    const bool ok = blob_save();
    Serial.printf("VISITREC: archived %s after %u days (%u kept)%s\n",
                  forms_name(r->final_form), r->days, visitrec_count(),
                  ok ? "" : "  SAVE FAILED");
    return ok;
}

/* A new Visitor mentioning the last one. Cheap by design: a handful of
 * compact facts, not a conversation system. */
const char *visitrec_previous_reference(void)
{
    static char line[96];
    const visit_rec_t *r = visitrec_at(0);
    if (!r) return nullptr;

    const uint8_t pick = (uint8_t)random(0, 4);
    switch (pick) {
    case 0:
        snprintf(line, sizeof(line), "My friend said your %s is amazing!",
                 evolve_food_name(r->fav_food));
        break;
    case 1:
        if (r->fav_game >= GAME_COUNT) return nullptr;
        snprintf(line, sizeof(line), "I heard you're good at %s!",
                 gamerec_name(r->fav_game));
        break;
    case 2:
        if (r->care_clean < 60) return nullptr;   /* only if it were true */
        snprintf(line, sizeof(line), "They said you keep this place really clean!");
        break;
    default:
        snprintf(line, sizeof(line), "A %s told me about you!",
                 forms_name(r->final_form));
        break;
    }
    return line;
}

void visitrec_clear_all(void)
{
    memset(&s_blob, 0, sizeof(s_blob));
    blob_save();
    Serial.println("VISITREC: all history cleared");
}

void visitrec_report(void)
{
    Serial.println();
    Serial.println("=== VISIT RECORDS =========================================");
    Serial.printf("  record %u B x %u kept = %u B on NVS (~11%% of usable)\n",
                  (unsigned)sizeof(visit_rec_t), VISIT_KEEP,
                  (unsigned)sizeof(vr_blob_t));
    if (!visitrec_count()) { Serial.println("  (none yet)"); }
    for (uint8_t i = 0; i < VISIT_KEEP; i++) {
        const visit_rec_t *r = visitrec_at(i);
        if (!r) continue;
        Serial.printf("  [%u] %s, %s + %s, %u days, fav %s / %s\n", i,
                      forms_name(r->final_form), evolve_trait_name(r->trait_a),
                      evolve_trait_name(r->trait_b), r->days,
                      evolve_food_name(r->fav_food), gamerec_name(r->fav_game));
        Serial.printf("      \"%s\"\n", r->farewell);
    }
    Serial.println("-----------------------------------------------------------");
}
