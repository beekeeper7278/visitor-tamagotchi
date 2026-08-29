/* strings - Phase 2 sample dialogue. See strings.h. */

#include <Arduino.h>
#include "strings.h"

/* NOTE ON NAMING: these arrays are NOT called T0..T3. The ESP32 Arduino core
 * defines T0..T9 in pins_arduino.h as touch-pin constants, so those names
 * expand to integers here and produce a genuinely baffling error. */

/* Tone: warm, short, a little silly. Written to be read aloud by an adult to
 * a five-year-old, so: no sarcasm, no guilt-tripping, and nothing that reads
 * as the pet blaming the child. */

static const char *LN_CRITICAL[] = {          /* critical need */
    "I'm really hungry!",
    "I need the toilet!",
    "Please clean me up!",
    "I'm so sleepy...",
};

static const char *LN_REACTION[] = {          /* reaction to input */
    "Hehe!",
    "That tickles!",
    "Hi there!",
    "Oof!",
    "Boop!",
    "Again! Again!",
};

static const char *LN_MOOD[] = {          /* mood flavour */
    "I feel good today.",
    "What shall we do?",
    "I like it here.",
    "You're my favourite.",
    "Today is a good day.",
};

static const char *LN_IDLE[] = {          /* idle chatter */
    "La la la...",
    "Hmm...",
    "Is it snack time?",
    "I wonder what's out there.",
    "*hums quietly*",
    "Just wobbling about.",
};

typedef struct { const char **lines; uint8_t n; } tier_tab_t;

static const tier_tab_t TAB[BUBBLE_TIER_COUNT] = {
    { LN_CRITICAL, (uint8_t)(sizeof(LN_CRITICAL) / sizeof(LN_CRITICAL[0])) },
    { LN_REACTION, (uint8_t)(sizeof(LN_REACTION) / sizeof(LN_REACTION[0])) },
    { LN_MOOD, (uint8_t)(sizeof(LN_MOOD) / sizeof(LN_MOOD[0])) },
    { LN_IDLE, (uint8_t)(sizeof(LN_IDLE) / sizeof(LN_IDLE[0])) },
};

uint8_t strings_count(bubble_tier_t t)
{
    return (t < BUBBLE_TIER_COUNT) ? TAB[t].n : 0;
}

const char *strings_at(bubble_tier_t t, uint8_t i)
{
    if (t >= BUBBLE_TIER_COUNT || TAB[t].n == 0) return "...";
    return TAB[t].lines[i % TAB[t].n];
}

const char *strings_random(bubble_tier_t t)
{
    if (t >= BUBBLE_TIER_COUNT || TAB[t].n == 0) return "...";
    return TAB[t].lines[random(TAB[t].n)];
}
