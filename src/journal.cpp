/* journal - bounded milestone log. See journal.h. */

#include <Arduino.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "forms.h"
#include "pet.h"
#include "rtc.h"
#include "gamerec.h"
#include "evolve.h"
#include "journal.h"

static journal_entry_t s_ring[24];
static uint8_t s_n;          /* how many are valid, capped at 24 */

void journal_clear(void) { memset(s_ring, 0, sizeof(s_ring)); s_n = 0; }

void journal_add(uint8_t type, uint8_t arg, uint16_t value)
{
    /* Oldest falls off the end. A full ring means the early days scroll
     * away, which is the right trade for a fixed keepsake. */
    if (s_n == 24) {
        memmove(&s_ring[0], &s_ring[1], sizeof(journal_entry_t) * 23);
        s_n = 23;
    }
    s_ring[s_n].ts    = rtc_trusted() ? rtc_now() : 0;
    s_ring[s_n].type  = type;
    s_ring[s_n].arg   = arg;
    s_ring[s_n].value = value;
    s_n++;
}

uint8_t journal_count(void) { return s_n; }

void journal_shift_ts(int32_t delta)
{
    if (!delta) return;
    uint8_t moved = 0;
    for (uint8_t i = 0; i < s_n; i++) {
        if (!s_ring[i].ts) continue;        /* logged with no trusted clock */
        s_ring[i].ts = rtc_shift_ts(s_ring[i].ts, delta);
        moved++;
    }
    if (moved) Serial.printf("JOURNAL: %u dated entries rebased\n", moved);
}

bool journal_line(uint8_t idx, char *buf, size_t len)
{
    if (idx >= s_n) return false;
    const journal_entry_t *e = &s_ring[s_n - 1 - idx];   /* newest first */

    char when[24] = "";
    if (e->ts) {
        char full[32];
        rtc_format(e->ts, full, sizeof(full));
        /* just the date and hour: a child does not need seconds */
        snprintf(when, sizeof(when), "%.10s  ", full + 5);
    }

    switch (e->type) {
        case JM_HATCHED:     snprintf(buf, len, "%sArrived on Earth!", when); break;
        case JM_EVOLVED:     snprintf(buf, len, "%sBecame a %s", when, forms_name(e->arg)); break;
        case JM_FIRST_GAME:  snprintf(buf, len, "%sPlayed %s for the first time", when, gamerec_name(e->arg)); break;
        case JM_RECORD:      snprintf(buf, len, "%sNew %s record: %u", when, gamerec_name(e->arg), e->value); break;
        case JM_FAV_FOOD:    snprintf(buf, len, "%sDecided %s is the best food", when, evolve_food_name(e->arg)); break;
        case JM_ACCIDENT:    snprintf(buf, len, "%sHad a little accident...", when); break;
        case JM_SPOTLESS:    snprintf(buf, len, "%sThe room was spotless!", when); break;
        case JM_FILTHY:      snprintf(buf, len, "%sThings got REALLY messy", when); break;
        case JM_MISCHIEF:    snprintf(buf, len, "%sGot up to mischief", when); break;
        case JM_DISCIPLINED: snprintf(buf, len, "%sLearned a lesson", when); break;
        case JM_CAKE:        snprintf(buf, len, "%sAte cake number %u!", when, e->value); break;
        case JM_LIGHTS:      snprintf(buf, len, "%sSlept with the light on", when); break;
        default:             snprintf(buf, len, "%s...", when); break;
    }
    return true;
}

void journal_load(const save_t *b)
{
    memcpy(s_ring, b->journal, sizeof(s_ring));
    s_n = 0;
    /* entries are written in order, so the first blank ends the list */
    for (uint8_t i = 0; i < 24; i++) {
        if (s_ring[i].type == 0 && s_ring[i].ts == 0 && s_ring[i].value == 0) break;
        s_n = i + 1;
    }
}

void journal_store(save_t *b) { memcpy(b->journal, s_ring, sizeof(s_ring)); }
