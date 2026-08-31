/* dialogue - personality lines, dreams, About Me. See dialogue.h. */

#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "config.h"
#include "forms.h"
#include "pet.h"
#include "care.h"
#include "evolve.h"
#include "gamerec.h"
#include "discipline.h"
#include "dialogue.h"

/* --- helpers ------------------------------------------------------------- */

static bool trait(uint8_t t)
{
    const pet_state_t *p = pet_get();
    return p->trait_a == t || p->trait_b == t;
}

static bool form_is(uint8_t f) { return pet_get()->form_id == f; }

#define NELEM(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

static const char *pick(const char *const *arr, uint8_t n)
{
    return (n == 0) ? "..." : arr[random(0, n)];
}

/* A trait/form pool wins MOST of the time but not always, so a Visitor still
 * surprises you occasionally instead of having exactly one voice. */
static bool flavour_wins(void) { return random(0, 100) < 70; }

/* --- 1. Dreams -----------------------------------------------------------
 * Weighted selection, not a flat roll. The weights are what make a dream
 * feel like it belongs to THIS Visitor: a Visitor that plays the maze
 * constantly should dream about mazes more often than one that has never
 * opened the Games page.
 *
 * Every line is self-contained - see the header for why substitution is
 * banned here. */

enum {
    DT_ANY = 0,     /* always plausible                                     */
    DT_FOOD,        /* likelier for a foodie / a cake habit                 */
    DT_GAME,        /* likelier once games have actually been played        */
    DT_MESS,        /* likelier after accidents                             */
    DT_SLEEPY,      /* likelier for a sleepy Visitor                        */
    DT_BABY,        /* Baby only - small, soft, silly                       */
    DT_BIG          /* Teen/Adult only - a bit more elaborate               */
};

typedef struct {
    uint8_t tag;
    int8_t  trait;      /* -1 for none */
    const char *bubble;
    const char *journal;
} dream_t;

static const dream_t DREAMS[] = {
/*  tag        trait              wake-up bubble                    Journal (first person, longer) */
{ DT_ANY,   -1,               "I dreamed I was a spaceship!",
  "I dreamed my bed turned into a spaceship! I forgot where I parked it." },
{ DT_ANY,   -1,               "I dreamed the floor was jelly.",
  "I dreamed the whole floor was wobbly jelly. I bounced everywhere and nobody could tell me off." },
{ DT_ANY,   PERS_CURIOUS,     "I found a door in the sky!",
  "I dreamed there was a little door in the sky. I opened it and there was ANOTHER sky behind it." },
{ DT_ANY,   PERS_DRAMATIC,    "I was a GIANT. Enormous!",
  "I dreamed I was ENORMOUS. I could see the whole room from up there, and everyone clapped." },
{ DT_ANY,   PERS_SHY,         "I dreamed of a very quiet cloud.",
  "I dreamed I sat on a small quiet cloud. Nobody looked at me and it was lovely." },
{ DT_FOOD,  PERS_FOODIE,      "It was raining burgers!",
  "I dreamed it rained burgers. I caught eleven of them. I only dropped two, which is very good." },
{ DT_FOOD,  -1,               "I dreamed of a cake as big as me.",
  "I dreamed about a cake that was bigger than me. I ate a doorway into it and had a little nap inside." },
{ DT_FOOD,  -1,               "The fruit was singing!",
  "I dreamed the fruit was singing. It was not a good song, but they were trying their best." },
{ DT_GAME,  PERS_COMPETITIVE, "I won EVERYTHING.",
  "I dreamed I won every single game, and then I won them again to make sure." },
{ DT_GAME,  -1,               "The maze had no walls!",
  "I dreamed the maze had no walls at all. I finished it instantly. It was not very hard." },
{ DT_GAME,  PERS_PLAYFUL,     "The buttons ran away from me!",
  "I dreamed the buttons kept running away when I tried to press them. I caught one. It squeaked." },
{ DT_MESS,  -1,               "A tidy monster ate the mess!",
  "I dreamed a big friendly monster came and ate all the mess. Then it said thank you and left." },
{ DT_MESS,  PERS_TIDY,        "Everything sorted itself!",
  "I dreamed everything tidied itself up in size order. It was the best dream I have ever had." },
{ DT_SLEEPY, PERS_SLEEPY,     "I dreamed I was still asleep.",
  "I dreamed I was asleep, and in that dream I was also asleep. It was very restful." },
{ DT_SLEEPY, -1,              "My bed was a boat.",
  "I dreamed my bed was a little boat. I floated all night and did not fall out once." },
{ DT_BABY,  -1,               "I was a tiny duck!",
  "I dreamed I was a very small duck. I could not do much, but everyone was pleased to see me." },
{ DT_BABY,  -1,               "I dreamed about socks.",
  "I dreamed about socks. Just socks. I do not know why, but I liked it." },
{ DT_BABY,  -1,               "The blanket was a hill!",
  "I dreamed my blanket was a big soft hill and I rolled all the way down it, twice." },
{ DT_BIG,   -1,               "I could talk to the moon.",
  "I dreamed I had a long chat with the moon. It is very polite and it asked how you were." },
{ DT_BIG,   PERS_MISCHIEVOUS, "I hid the whole room!",
  "I dreamed I hid the entire room somewhere clever. In the morning I could not remember where." },
{ DT_ANY,   PERS_PLAYFUL,     "I bounced off the ceiling!",
  "I dreamed I bounced so high I stuck to the ceiling and had to be fetched down." },
{ DT_ANY,   PERS_TIDY,        "The dust said sorry.",
  "I dreamed all the dust apologised and swept itself up. Very good dust." },
{ DT_ANY,   -1,               "I had extra feet!",
  "I dreamed I grew four extra feet. I kept tripping over them but I was VERY fast." },
{ DT_ANY,   PERS_MISCHIEVOUS, "I was invisible. Nobody knew.",
  "I dreamed I was completely invisible. I did some things. I will not say which things." },
};

uint8_t dialogue_dream_count(void) { return NELEM(DREAMS); }

uint8_t dialogue_dream_pick(bool nap)
{
    const pet_state_t *p = pet_get();
    const gamerec_t   *g = gamerec_get();
    const uint8_t n = NELEM(DREAMS);

    /* Do not repeat the dream immediately before this one. A child who hears
     * the same dream twice in two mornings concludes there is only one. */
    const uint8_t last = (p->dream_n > 0) ? p->dream_id[p->dream_n - 1] : 0xFF;

    uint16_t w[NELEM(DREAMS)];
    uint32_t total = 0;

    for (uint8_t i = 0; i < n; i++) {
        const dream_t *d = &DREAMS[i];
        int weight = 10;

        switch (d->tag) {
            case DT_BABY:
                /* Stage gates, not preferences: a Baby dream out of an Adult
                 * reads as a different Visitor. */
                weight = (p->stage <= STAGE_BABY) ? 26 : 0;
                break;
            case DT_BIG:
                weight = (p->stage >= STAGE_TEEN) ? 20 : 0;
                break;
            case DT_FOOD:
                weight += 8 * (int)(p->cakes_eaten > 0)
                        + 10 * (int)(evolve_favourite_food() == 2)
                        + 8  * (int)trait(PERS_FOODIE);
                break;
            case DT_GAME:
                weight += (g->total_games > 0) ? 12 : -6;
                weight += 8 * (int)trait(PERS_COMPETITIVE);
                break;
            case DT_MESS:
                weight += (p->accidents > 0 || care_mess_count() > 0) ? 14 : -4;
                break;
            case DT_SLEEPY:
                weight += 8 * (int)trait(PERS_SLEEPY);
                break;
            default: break;
        }

        if (d->trait >= 0 && trait((uint8_t)d->trait)) weight += 20;

        /* A NAP is short, so it gets the short silly end of the table: no
         * elaborate Teen/Adult dreams during an afternoon doze. */
        if (nap && d->tag == DT_BIG) weight = 0;

        if (i == last) weight = 0;
        if (weight < 1) weight = (weight <= 0) ? 0 : 1;

        w[i] = (uint16_t)weight;
        total += weight;
    }

    if (total == 0) return 0;                 /* cannot happen, but be safe */

    uint32_t r = (uint32_t)random(0, (long)total);
    for (uint8_t i = 0; i < n; i++) {
        if (r < w[i]) return i;
        r -= w[i];
    }
    return 0;
}

const char *dialogue_dream_bubble(uint8_t id)
{
    return DREAMS[id < NELEM(DREAMS) ? id : 0].bubble;
}

const char *dialogue_dream_journal(uint8_t id)
{
    return DREAMS[id < NELEM(DREAMS) ? id : 0].journal;
}

void dialogue_dream_record(uint8_t id)
{
    pet_state_t *p = pet_mutable();
    if (id >= NELEM(DREAMS)) return;

    if (p->dream_n < DREAM_KEEP) {
        p->dream_id[p->dream_n++] = id;
    } else {
        for (uint8_t i = 1; i < DREAM_KEEP; i++) p->dream_id[i - 1] = p->dream_id[i];
        p->dream_id[DREAM_KEEP - 1] = id;
    }
}

/* --- 2. Lights -----------------------------------------------------------
 * The room going dark while the Visitor is AWAKE is a joke, not a bedtime.
 * Nothing here touches the sleep model. */

static const char *const LIGHT_OFF_ANY[] = {
    "Who turned out the sun?",
    "Why are the lights off?",
    "I can't see my feet!",
    "Ooh. Spooky.",
    "Is it night already?",
    "Everything went dark blue.",
};
static const char *const LIGHT_OFF_DRAMATIC[] = {
    "DARKNESS! At this hour!",
    "The sun has ABANDONED me.",
    "I am but a shape in the gloom.",
};
static const char *const LIGHT_OFF_SHY[] = {
    "Um... it's a bit dark.",
    "...is anyone there?",
    "I'd like the light back please.",
};
static const char *const LIGHT_OFF_MISCHIEF[] = {
    "Ooh, I could get up to ANYTHING now.",
    "Nobody can see me. Excellent.",
    "Dark! My favourite working conditions.",
};
static const char *const LIGHT_OFF_CURIOUS[] = {
    "Where does the light GO?",
    "Fascinating. It's all gone black.",
    "Can I still see if I try really hard?",
};
static const char *const LIGHT_OFF_GRUMPY[] = {
    "Lights. Please.",
    "I was USING that light.",
    "Hmph. Dark.",
};

const char *dialogue_lights_off(void)
{
    if (flavour_wins()) {
        if (trait(PERS_DRAMATIC))    return pick(LIGHT_OFF_DRAMATIC, NELEM(LIGHT_OFF_DRAMATIC));
        if (trait(PERS_SHY))         return pick(LIGHT_OFF_SHY, NELEM(LIGHT_OFF_SHY));
        if (trait(PERS_MISCHIEVOUS)) return pick(LIGHT_OFF_MISCHIEF, NELEM(LIGHT_OFF_MISCHIEF));
        if (trait(PERS_CURIOUS))     return pick(LIGHT_OFF_CURIOUS, NELEM(LIGHT_OFF_CURIOUS));
        if (form_is(FORM_ADULT_GRUMPY)) return pick(LIGHT_OFF_GRUMPY, NELEM(LIGHT_OFF_GRUMPY));
    }
    return pick(LIGHT_OFF_ANY, NELEM(LIGHT_OFF_ANY));
}

static const char *const LIGHT_ON_ANY[] = {
    "Oh! There it is.",
    "I found the sun!",
    "Much better.",
    "Hello again, room.",
    "My feet are back!",
};
static const char *const LIGHT_ON_DRAMATIC[] = {
    "THE SUN RETURNS!",
    "I have been SAVED.",
};
static const char *const LIGHT_ON_MISCHIEF[] = {
    "I wasn't doing anything.",
    "Everything is exactly where you left it. Probably.",
};

const char *dialogue_lights_on(void)
{
    if (flavour_wins()) {
        if (trait(PERS_DRAMATIC))    return pick(LIGHT_ON_DRAMATIC, NELEM(LIGHT_ON_DRAMATIC));
        if (trait(PERS_MISCHIEVOUS)) return pick(LIGHT_ON_MISCHIEF, NELEM(LIGHT_ON_MISCHIEF));
    }
    return pick(LIGHT_ON_ANY, NELEM(LIGHT_ON_ANY));
}

/* --- 3. Old messes -------------------------------------------------------
 * Only ever said about a poop that has already grown its stink lines, so the
 * joke always refers to something the child can see. Nobody is blamed - the
 * Visitor made it, and the funniest version is the one where it is not
 * entirely sure about that. */

static const char *const STINK_ANY[] = {
    "Why does it stink in here?",
    "Something smells... suspicious.",
    "Uh... did I do that?",
    "What is that squishy brown thing?",
    "There's a smell. I have questions.",
    "It's a bit whiffy in here.",
};
static const char *const STINK_MISCHIEF[] = {
    "Wasn't me.",
    "That was there when I arrived.",
    "No idea. None. Zero.",
};
static const char *const STINK_DRAMATIC[] = {
    "THE SMELL HAS RETURNED.",
    "I cannot LIVE like this!",
    "Behold: the smell.",
};
static const char *const STINK_GRUMPY[] = {
    "Seriously. Clean it.",
    "That is still there, you know.",
    "Hmph. Smelly.",
};
static const char *const STINK_CURIOUS[] = {
    "What IS that smell?",
    "I'd like to study it. From here.",
    "Where do smells come from?",
};
static const char *const STINK_SWEET[] = {
    "Sorry about the smell.",
    "Could we tidy that away? Please?",
    "It's a bit smelly. No rush though!",
};
static const char *const STINK_SCRUFFY[] = {
    "Smells like home!",
    "I've decided to call it Kevin.",
    "You get used to it. Mostly.",
};
static const char *const STINK_CHONKY[] = {
    "That smell is putting me off my snack.",
    "It smells like NOT cake.",
    "I can't eat in these conditions.",
};
static const char *const STINK_TIDY[] = {
    "This is unacceptable to me personally.",
    "I have made a list. It has one item.",
};

const char *dialogue_stink(void)
{
    if (flavour_wins()) {
        if (form_is(FORM_ADULT_GRUMPY))  return pick(STINK_GRUMPY, NELEM(STINK_GRUMPY));
        if (form_is(FORM_ADULT_SWEET))   return pick(STINK_SWEET, NELEM(STINK_SWEET));
        if (form_is(FORM_ADULT_SCRUFFY)) return pick(STINK_SCRUFFY, NELEM(STINK_SCRUFFY));
        if (form_is(FORM_ADULT_CHONKY))  return pick(STINK_CHONKY, NELEM(STINK_CHONKY));
        if (trait(PERS_MISCHIEVOUS))     return pick(STINK_MISCHIEF, NELEM(STINK_MISCHIEF));
        if (trait(PERS_DRAMATIC))        return pick(STINK_DRAMATIC, NELEM(STINK_DRAMATIC));
        if (trait(PERS_CURIOUS))         return pick(STINK_CURIOUS, NELEM(STINK_CURIOUS));
        if (trait(PERS_TIDY))            return pick(STINK_TIDY, NELEM(STINK_TIDY));
    }
    return pick(STINK_ANY, NELEM(STINK_ANY));
}

/* --- 4. Waking ----------------------------------------------------------- */

static const char *const WAKE_ANY[] = {
    "Morning!", "Good morning!", "*big stretch*",
    "Is it today already?", "Hello, day.",
};
static const char *const WAKE_SLEEPY[] = {
    "Five more minutes...", "*yaaaawn*", "Morning. Barely.",
};
static const char *const WAKE_PLAYFUL[] = {
    "MORNING! What are we doing?", "Up! Up! Up!", "Let's do something!",
};
static const char *const WAKE_DRAMATIC[] = {
    "I HAVE AWOKEN.", "Behold: me, awake.",
};
static const char *const NAP_ANY[] = {
    "Nap over!", "That was a good nap.", "Oh! I was asleep.",
    "Back again!",
};

const char *dialogue_wake(bool nap)
{
    if (nap) return pick(NAP_ANY, NELEM(NAP_ANY));
    if (flavour_wins()) {
        if (trait(PERS_SLEEPY))   return pick(WAKE_SLEEPY, NELEM(WAKE_SLEEPY));
        if (trait(PERS_PLAYFUL))  return pick(WAKE_PLAYFUL, NELEM(WAKE_PLAYFUL));
        if (trait(PERS_DRAMATIC)) return pick(WAKE_DRAMATIC, NELEM(WAKE_DRAMATIC));
    }
    return pick(WAKE_ANY, NELEM(WAKE_ANY));
}

/* --- 5. Discipline ------------------------------------------------------- */

static const char *const TOLD_ANY[] = {
    "Okay, okay...", "Fiiine.", "I'll be good.", "Point taken.",
};
static const char *const TOLD_MISCHIEF[] = {
    "Worth it.", "No regrets.", "I'd do it again. Probably.",
};
static const char *const TOLD_DRAMATIC[] = {
    "I KNOW.", "My one mistake!", "The shame!",
};
static const char *const TOLD_SHY[] = {
    "...sorry.", "I didn't mean it.", "*small voice* okay",
};
static const char *const TOLD_COMPETITIVE[] = {
    "Fine. I'll do better.", "Next time I'll be perfect.",
};
static const char *const TOLD_GRUMPY[] = {
    "Yes. Fine. Noted.", "Hmph.",
};

const char *dialogue_told_off(void)
{
    if (flavour_wins()) {
        if (trait(PERS_MISCHIEVOUS)) return pick(TOLD_MISCHIEF, NELEM(TOLD_MISCHIEF));
        if (trait(PERS_DRAMATIC))    return pick(TOLD_DRAMATIC, NELEM(TOLD_DRAMATIC));
        if (trait(PERS_SHY))         return pick(TOLD_SHY, NELEM(TOLD_SHY));
        if (trait(PERS_COMPETITIVE)) return pick(TOLD_COMPETITIVE, NELEM(TOLD_COMPETITIVE));
        if (form_is(FORM_ADULT_GRUMPY)) return pick(TOLD_GRUMPY, NELEM(TOLD_GRUMPY));
    }
    return pick(TOLD_ANY, NELEM(TOLD_ANY));
}

/* One pool per kind of mischief. The line has to make the misbehaviour
 * LEGIBLE - a child cannot fairly tell a Visitor off for something they did
 * not notice, which is the whole reason discipline is contextual. */
static const char *const LN_MIS_FOOD[] = {
    "Whoops! It slipped. Honest.",
    "I threw it. On purpose. Sorry!",
    "Watch this! *throws dinner*",
    "Food goes on the FLOOR now.",
};
static const char *const LN_MIS_BED[] = {
    "I'm NOT sleepy!",
    "Bed? Never heard of it.",
    "One more minute! Or twenty.",
    "You can't make me!",
};
static const char *const LN_MIS_MESS[] = {
    "Look what I made!",
    "I did a mess. It's art.",
    "Oops. Deliberate oops.",
    "The floor was too clean.",
};
static const char *const LN_MIS_CAKE[] = {
    "CAKE! Cake cake cake!",
    "I require cake immediately.",
    "Cake. Now. Please. CAKE.",
    "I would like cake for every meal.",
};

const char *dialogue_mischief(uint8_t what)
{
    switch (what) {
        case MIS_THREW_FOOD:   return pick(LN_MIS_FOOD, NELEM(LN_MIS_FOOD));
        case MIS_BEDTIME:      return pick(LN_MIS_BED,  NELEM(LN_MIS_BED));
        case MIS_MESS:         return pick(LN_MIS_MESS, NELEM(LN_MIS_MESS));
        case MIS_CAKE_TANTRUM: return pick(LN_MIS_CAKE, NELEM(LN_MIS_CAKE));
        default:               return "Hehe.";
    }
}

/* --- 6. The first words ever --------------------------------------------- */

static const char *const HATCH_GREET[] = {
    "Hi!",
    "Is this Earth?",
    "Are you my person?",
    "I think I live here now.",
    "Hello! What's happening?",
    "Oh! You're big.",
};

const char *dialogue_hatch_greeting(void)
{
    return pick(HATCH_GREET, NELEM(HATCH_GREET));
}

/* --- 6b. Food, sleep and games ------------------------------------------- */

static const char *const YUM_BURGER[] = {
    "Yum!", "Burger! Excellent.", "*happy chewing*", "That's the good stuff.",
};
static const char *const YUM_FRUIT[] = {
    "Crunchy!", "Ooh, fresh.", "That's very healthy of me.", "Juicy!",
};
static const char *const YUM_CAKE[] = {
    "Cake! Yesss.", "CAKE. Finally.", "Best day ever.", "Mmmmmm cake.",
};
static const char *const YUM_FOODIE[] = {
    "More! More!", "I could eat that again.", "Ten out of ten.",
};

const char *dialogue_food_yum(uint8_t f)
{
    if (f != 2 && trait(PERS_FOODIE) && flavour_wins())
        return pick(YUM_FOODIE, NELEM(YUM_FOODIE));
    if (f == 2) return pick(YUM_CAKE,   NELEM(YUM_CAKE));
    if (f == 1) return pick(YUM_FRUIT,  NELEM(YUM_FRUIT));
    return pick(YUM_BURGER, NELEM(YUM_BURGER));
}

static const char *const PARTIAL_ANY[] = {
    "I can't finish it all...",
    "That's enough for me.",
    "I'm nearly full!",
    "Half now, half later?",
};
const char *dialogue_food_partial(void) { return pick(PARTIAL_ANY, NELEM(PARTIAL_ANY)); }

/* NOT a refusal to be told off for. Every one of these says FULL, not
 * defiant - the distinction the whole discipline model rests on. */
static const char *const REFUSE_ANY[] = {
    "No thanks, I'm stuffed!",
    "I couldn't. I'm full!",
    "My tummy is completely full.",
    "Maybe later? I'm full.",
};
static const char *const REFUSE_DRAMATIC[] = {
    "I could not POSSIBLY.", "I am full to the BRIM.",
};
static const char *const REFUSE_SHY[] = {
    "Um... I'm full, sorry.", "No thank you. I'm full.",
};
static const char *const REFUSE_CHONKY[] = {
    "Even I have limits.", "Save it for me for later?",
};

const char *dialogue_food_refuse(void)
{
    if (flavour_wins()) {
        if (trait(PERS_DRAMATIC))       return pick(REFUSE_DRAMATIC, NELEM(REFUSE_DRAMATIC));
        if (trait(PERS_SHY))            return pick(REFUSE_SHY, NELEM(REFUSE_SHY));
        if (form_is(FORM_ADULT_CHONKY)) return pick(REFUSE_CHONKY, NELEM(REFUSE_CHONKY));
    }
    return pick(REFUSE_ANY, NELEM(REFUSE_ANY));
}

static const char *const POKE_ANY[] = {
    "Mmm... too sleepy.", "Five more minutes...", "I'm sleeping!",
    "Can we play tomorrow?", "Soooo sleepy...",
};
static const char *const POKE_SLEEPY[] = {
    "Nooo. Sleeping.", "This is my best thing. Sleeping.", "*deep snooze*",
};
static const char *const POKE_DRAMATIC[] = {
    "I was having the BEST dream!", "You have RUINED it.",
};
static const char *const POKE_INSIST[] = {
    "I said I'm SLEEPING!", "Please. Sleeping.", "Tomorrow! Tomorrow!",
};

const char *dialogue_sleepy_poke(bool insistent)
{
    if (insistent) return pick(POKE_INSIST, NELEM(POKE_INSIST));
    if (flavour_wins()) {
        if (trait(PERS_SLEEPY))   return pick(POKE_SLEEPY, NELEM(POKE_SLEEPY));
        if (trait(PERS_DRAMATIC)) return pick(POKE_DRAMATIC, NELEM(POKE_DRAMATIC));
    }
    return pick(POKE_ANY, NELEM(POKE_ANY));
}

static const char *const GAME_ANY[] = {
    "That was fun!", "Again sometime?", "Good game!", "I liked that.",
};
static const char *const GAME_COMPETITIVE[] = {
    "I'll beat that next time.", "Rematch. Soon.", "Did you SEE that?",
};
static const char *const GAME_PLAYFUL[] = {
    "AGAIN! Let's go again!", "More! More games!",
};
static const char *const GAME_SLEEPY[] = {
    "That was fun. Nap now.", "Good game. I'm tired.",
};

const char *dialogue_game_done(void)
{
    if (flavour_wins()) {
        if (trait(PERS_COMPETITIVE)) return pick(GAME_COMPETITIVE, NELEM(GAME_COMPETITIVE));
        if (trait(PERS_PLAYFUL))     return pick(GAME_PLAYFUL, NELEM(GAME_PLAYFUL));
        if (trait(PERS_SLEEPY))      return pick(GAME_SLEEPY, NELEM(GAME_SLEEPY));
    }
    return pick(GAME_ANY, NELEM(GAME_ANY));
}

/* --- 7. About Me ---------------------------------------------------------
 * Written in the Visitor's own voice, from what actually happened. Assembled
 * as a few short sentences rather than one template with holes in it: a
 * paragraph that always has the same shape reads as a form letter, and this
 * is the page a child is most likely to have read to them twice.
 *
 * Every clause is CONDITIONAL on there being something true to say, so a
 * brand-new Baby gets two honest sentences rather than five padded ones. */

static const char *trait_sentence(uint8_t t)
{
    switch (t) {
        case PERS_PLAYFUL:     return "I love playing games.";
        case PERS_SLEEPY:      return "I am very good at napping.";
        case PERS_DRAMATIC:    return "I feel my feelings LOUDLY.";
        case PERS_TIDY:        return "I like things to be neat.";
        case PERS_MISCHIEVOUS: return "I get up to a bit of trouble.";
        case PERS_FOODIE:      return "I think about snacks a lot.";
        case PERS_COMPETITIVE: return "I really, really like winning.";
        case PERS_CURIOUS:     return "I want to know how everything works.";
        case PERS_SHY:         return "I am a little bit shy at first.";
        default:               return "I am me.";
    }
}

static const char *form_sentence(uint8_t f)
{
    switch (f) {
        case FORM_KID_GOOD:      return "People say I am a good kid.";
        case FORM_KID_MISCHIEF:  return "People say I am a handful.";
        case FORM_TEEN_BRIGHT:   return "I got clever when I grew up.";
        case FORM_TEEN_MELLOW:   return "I am pretty relaxed about things.";
        case FORM_TEEN_ROWDY:    return "I am a bit loud. Sorry.";
        case FORM_ADULT_BEST:    return "I grew up happy, and I know it.";
        case FORM_ADULT_SWEET:   return "I turned out kind, I think.";
        case FORM_ADULT_PLAYFUL: return "I never did stop playing.";
        case FORM_ADULT_CHONKY:  return "I grew up round. It was the cake.";
        case FORM_ADULT_GRUMPY:  return "I grumble, but I don't mean it.";
        case FORM_ADULT_SCRUFFY: return "I am a bit scruffy and that's fine.";
        default:                 return "I am still very small.";
    }
}

/* Append, safely.
 *
 * The `n += snprintf(buf + n, sizeof(buf) - n, ...)` idiom used throughout
 * this project is WRONG at the boundary and it is wrong in a way that does
 * not show up until a Visitor happens to have a long enough history.
 * snprintf() returns the length it WOULD have written, so once n passes the
 * buffer size, `len - n` underflows: len is size_t, n is int, and the
 * subtraction promotes to an enormous unsigned value. The next call is then
 * told it has ~4 GB of room and writes straight past the end.
 *
 * About Me is the place it would actually have bitten - the longest
 * reachable paragraph measured about 280 characters against a 320-byte
 * buffer, which is a margin of two extra words, not a safety margin. */
static int app(char *out, size_t len, int n, const char *fmt, ...)
{
    if (n < 0 || (size_t)n + 1 >= len) return n;    /* full: write nothing */
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(out + n, len - (size_t)n, fmt, ap);
    va_end(ap);
    if (w < 0) return n;
    n += w;
    /* Clamp to the terminator so a truncated write cannot leave n past the
     * end of the buffer for the next caller. */
    if ((size_t)n >= len) n = (int)len - 1;
    return n;
}

/* --- motion [PHASE 10] ---------------------------------------------------
 * The brief's own examples are in here verbatim, because they set the tone
 * exactly: startled and comic, never hurt. Grumpy grumbles, it does not
 * snarl; Shy squeaks rather than shouts. */
static const char *SHAKE_GEN[] = {
    "Wheee! Do it again!", "Whoa!", "I'm getting dizzy!", "Everything's spinny!",
};
static const char *SHAKE_PLAYFUL[] = {
    "WHEEE! Again! Again!", "Is this a ride?", "Fastest Visitor alive!",
};
static const char *SHAKE_SHY[] = {
    "Eep!", "Oh! Um. Hello.", "That was a lot.",
};
static const char *SHAKE_DRAMATIC[] = {
    "I HAVE BEEN LAUNCHED!", "AN EARTHQUAKE! IN HERE!", "MY WHOLE LIFE FLASHED BY!",
};
static const char *SHAKE_ANNOY_GEN[] = {
    "HEY! Stop shaking me!", "Can you PLEASE hold still?", "I'm getting dizzy!",
    "Okay okay OKAY, enough!",
};
static const char *SHAKE_ANNOY_GRUMPY[] = {
    "Hmph. Put me down.", "This is my grumpy face.", "I was COMFY.",
};
static const char *SHAKE_ANNOY_SHY[] = {
    "Please stop... please?", "I don't like this one.",
};
static const char *UPSIDE_GEN[] = {
    "WHY AM I UPSIDE DOWN?!", "Can you flip me back?", "Hey! Put me back!",
    "All the blood is in my head!",
};
static const char *UPSIDE_DRAMATIC[] = {
    "THE WORLD HAS TURNED OVER!", "I LIVE ON THE CEILING NOW!",
};
static const char *UPSIDE_MISCHIEF[] = {
    "Ha! I meant to do this.", "I'm fine. This is fine. Flip me back though.",
};
static const char *UPRIGHT_GEN[] = {
    "Ahh, that's better!", "Thank you!", "Phew!", "Right way up! Hooray!",
};

const char *dialogue_shaken(void)
{
    if (flavour_wins()) {
        if (trait(PERS_PLAYFUL))  return pick(SHAKE_PLAYFUL, NELEM(SHAKE_PLAYFUL));
        if (trait(PERS_SHY))      return pick(SHAKE_SHY, NELEM(SHAKE_SHY));
        if (trait(PERS_DRAMATIC)) return pick(SHAKE_DRAMATIC, NELEM(SHAKE_DRAMATIC));
    }
    return pick(SHAKE_GEN, NELEM(SHAKE_GEN));
}

const char *dialogue_shaken_annoyed(void)
{
    if (flavour_wins()) {
        if (form_is(FORM_ADULT_GRUMPY) || trait(PERS_TIDY))
            return pick(SHAKE_ANNOY_GRUMPY, NELEM(SHAKE_ANNOY_GRUMPY));
        if (trait(PERS_SHY)) return pick(SHAKE_ANNOY_SHY, NELEM(SHAKE_ANNOY_SHY));
    }
    return pick(SHAKE_ANNOY_GEN, NELEM(SHAKE_ANNOY_GEN));
}

const char *dialogue_upside_down(void)
{
    if (flavour_wins()) {
        if (trait(PERS_DRAMATIC))    return pick(UPSIDE_DRAMATIC, NELEM(UPSIDE_DRAMATIC));
        if (trait(PERS_MISCHIEVOUS)) return pick(UPSIDE_MISCHIEF, NELEM(UPSIDE_MISCHIEF));
    }
    return pick(UPSIDE_GEN, NELEM(UPSIDE_GEN));
}

const char *dialogue_upright_relief(void)
{
    return pick(UPRIGHT_GEN, NELEM(UPRIGHT_GEN));
}

void dialogue_about_me(char *out, size_t len)
{
    const pet_state_t *p = pet_get();
    const gamerec_t   *g = gamerec_get();
    if (!out || len == 0) return;

    int n = 0;
    out[0] = 0;

    /* 1. who I am - always. */
    n = app(out, len, n, "%s ", trait_sentence(p->trait_a));
    n = app(out, len, n, "%s",  trait_sentence(p->trait_b));

    /* 2. what I like, once there is evidence for it. Favourite food is
     *    always answerable (personality breaks the tie), the favourite game
     *    is not - so it is only claimed once a game has been played. */
    n = app(out, len, n, "\nMy favourite food is %s.",
            evolve_food_name(evolve_favourite_food()));
    if (g->total_games)
        n = app(out, len, n, " I think the %s is my favourite game.",
                gamerec_name(gamerec_favorite()));

    /* 3. how I behave, from the DISCIPLINE history rather than a guess.
     *    Both directions are gentle: neither reads as a telling-off. */
    if (p->disc_opportunities >= 3) {
        const bool mostly_corrected =
            (uint32_t)p->disc_correct * 2u >= (uint32_t)p->disc_opportunities;
        if (p->learned_mischief >= 65.0f)
            n = app(out, len, n, "\nI get up to mischief quite a lot. It's fun.");
        else if (mostly_corrected && p->learned_mischief <= 35.0f)
            n = app(out, len, n, "\nI used to be naughty, but I learned better.");
        else
            n = app(out, len, n, "\nI'm mostly good. Mostly.");
    }

    /* 4. one specific, true, funny detail. Ordered so the most unusual thing
     *    about this Visitor wins, rather than the first thing on the list. */
    if (p->cakes_eaten >= 5)
        n = app(out, len, n, "\nI have eaten %u cakes. I regret none of them.",
                p->cakes_eaten);
    else if (p->accidents >= 3)
        n = app(out, len, n, "\nI have had %u little accidents. We don't talk about them.",
                p->accidents);
    else if (p->lights_forgotten >= 2)
        n = app(out, len, n, "\nSometimes I sleep with the light on. It's not ideal.");
    else if (g->total_games >= 10)
        n = app(out, len, n, "\nI have played %u games. I would like to play more.",
                g->total_games);
    else if (p->meals >= 10)
        n = app(out, len, n, "\nI have had %u meals here. Thank you for those.",
                p->meals);

    /* 5. and how I turned out. */
    app(out, len, n, "\n%s", form_sentence(p->form_id));
}

/* --- console ------------------------------------------------------------- */

void dialogue_report(void)
{
    const pet_state_t *p = pet_get();
    char about[320];

    Serial.println();
    Serial.println("=== DIALOGUE ==============================================");
    Serial.printf("  voice      : %s + %s, form %s\n",
                  evolve_trait_name(p->trait_a), evolve_trait_name(p->trait_b),
                  forms_name(p->form_id));
    Serial.println("  five samples of each category (variety must be VISIBLE):");
    for (uint8_t i = 0; i < 5; i++)
        Serial.printf("    lights off : %s\n", dialogue_lights_off());
    for (uint8_t i = 0; i < 3; i++)
        Serial.printf("    lights on  : %s\n", dialogue_lights_on());
    for (uint8_t i = 0; i < 5; i++)
        Serial.printf("    stink      : %s\n", dialogue_stink());
    for (uint8_t i = 0; i < 3; i++)
        Serial.printf("    waking     : %s\n", dialogue_wake(false));
    for (uint8_t i = 0; i < 3; i++)
        Serial.printf("    told off   : %s\n", dialogue_told_off());
    for (uint8_t i = 0; i < 3; i++)
        Serial.printf("    food full  : %s\n", dialogue_food_refuse());
    for (uint8_t i = 0; i < 3; i++)
        Serial.printf("    poked      : %s\n", dialogue_sleepy_poke(false));
    for (uint8_t i = 0; i < 3; i++)
        Serial.printf("    after game : %s\n", dialogue_game_done());
    for (uint8_t i = 0; i < 4; i++) {
        const uint8_t d = dialogue_dream_pick(false);
        Serial.printf("    dream %2u   : \"%s\"\n", d, dialogue_dream_bubble(d));
        Serial.printf("                 %s\n", dialogue_dream_journal(d));
    }
    Serial.printf("  dream table: %u entries; recent %u kept\n",
                  dialogue_dream_count(), p->dream_n);
    dialogue_about_me(about, sizeof(about));
    Serial.println("  ABOUT ME:");
    Serial.printf("%s\n", about);
    Serial.println("-----------------------------------------------------------");
}
