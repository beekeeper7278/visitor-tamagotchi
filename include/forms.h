#pragma once
/* ===========================================================================
 * forms.h - procedural form parameter table  [SPEC section 8]
 *
 * Header-only const data. No logic, no globals, no LVGL objects.
 *
 * TWELVE FORMS TOTAL: 1 Baby + 2 Kids + 3 Teens + 6 Adults.
 * (Any older note claiming 15 forms is stale and wrong.)
 *
 * PHASE 2 SCOPE: only FORM_BABY is populated for real. The other eleven are
 * declared so that IDs, indices and save-file values are stable from now on,
 * but their appearance is NOT invented here - that is Phase 11 (Evolution)
 * work and it needs art direction we do not have yet. Unpopulated rows carry
 * body_w == 0 as the sentinel; forms_is_populated() is the check.
 *
 * DESIGN NOTE - colours are stored as uint32_t rather than lv_color_t.
 * lv_color_t is a union and LV_COLOR_MAKE expands to a compound literal,
 * which is not a portable constant initialiser in C++. Storing hex and
 * converting via lv_color_hex() at draw time keeps this table plain-old-data
 * and genuinely const-in-flash. Pure implementation detail; the parameter
 * set is exactly the one in the design.
 * ======================================================================== */

#include <stdint.h>
#include <stdbool.h>

/* --- style enums --------------------------------------------------------
 * The renderer implements EVERY style below, because they are form
 * parameters rather than behaviours. Implementing SLEEPY eyes is Phase 2;
 * deciding that the pet is asleep is Phase 5. */

enum {                      /* eye_style */
    EYE_DOT = 0, EYE_OVAL, EYE_SLEEPY, EYE_STAR, EYE_ANGRY_SLANT, EYE_SPIRAL,
    EYE_STYLE_COUNT
};

enum {                      /* mouth_style */
    MOUTH_SMILE = 0, MOUTH_SMIRK, MOUTH_FLAT, MOUTH_FROWN,
    MOUTH_OPEN_HAPPY, MOUTH_TOOTHY, MOUTH_WOBBLE,
    MOUTH_STYLE_COUNT
};

enum { BROW_NONE = 0, BROW_ANGRY, BROW_WORRIED, BROW_STYLE_COUNT };

/* PNG-overlay features. LVGL 8.3 primitives cannot draw concave shapes, so
 * anything below that is not NONE waits for the asset pipeline. The Baby
 * form needs none of them, which is why Phase 2 ships no art. */
enum { EAR_NONE = 0, EAR_ROUND, EAR_POINTY, EAR_FLOPPY };
enum { LIMB_NUBS = 0, LIMB_ARMS, LIMB_NONE };
enum { TAIL_NONE = 0, TAIL_CURL, TAIL_PUFF };

/* deco_mask bits */
#define DECO_CRUMBS   (1u << 0)
#define DECO_STINK    (1u << 1)
#define DECO_SPARKLE  (1u << 2)
#define DECO_BANDANA  (1u << 3)

/* --- the form record ---------------------------------------------------- */
typedef struct {
    /* silhouette */
    uint8_t  body_w, body_h;     /* 60..160 px                              */
    uint8_t  body_round;         /* corner radius %, 0..50 (50 = blob)      */
    int8_t   body_squash;        /* -30..+30, + = wide (chonky), - = tall   */
    uint8_t  head_ratio;         /* head as % of body; 0 = merged blob      */
    /* palette (0xRRGGBB) */
    uint32_t c_body, c_belly, c_accent, c_eye;
    /* face */
    uint8_t  eye_style;
    uint8_t  eye_spacing, eye_size;
    uint8_t  mouth_style;
    uint8_t  brow_style;
    /* features */
    uint8_t  ear_style;
    uint8_t  limb_style;
    uint8_t  tail_style;
    uint8_t  cheek_blush;
    uint8_t  deco_mask;
} pet_form_t;

/* --- form IDs -----------------------------------------------------------
 * These values are persisted in save_t.form_id / kid_form / teen_form.
 * NEVER renumber them; append only. */
enum {
    FORM_BABY = 0,              /* 1 baby                                   */

    FORM_KID_GOOD,              /* 2 kids                                   */
    FORM_KID_MISCHIEF,

    FORM_TEEN_BRIGHT,           /* 3 teens                                  */
    FORM_TEEN_MELLOW,
    FORM_TEEN_ROWDY,

    FORM_ADULT_BEST,            /* 6 adults, ranked best -> worst           */
    FORM_ADULT_SWEET,
    FORM_ADULT_PLAYFUL,
    FORM_ADULT_CHONKY,
    FORM_ADULT_GRUMPY,
    FORM_ADULT_SCRUFFY,

    FORM_COUNT                  /* == 12                                    */
};

/* Compile-time guard: the count is load-bearing (save-file values, evolution
 * tables), so a miscount should break the build, not the pet. */
#ifdef __cplusplus
static_assert(FORM_COUNT == 12, "form table must be 1 baby + 2 kid + 3 teen + 6 adult");
#else
_Static_assert(FORM_COUNT == 12, "form table must be 1 baby + 2 kid + 3 teen + 6 adult");
#endif

/* --- the table ----------------------------------------------------------
 * PHASE 2: Baby only. See the header comment before adding rows. */
static const pet_form_t PET_FORMS[FORM_COUNT] = {

    /* ---- FORM_BABY -----------------------------------------------------
     * A merged blob: head_ratio 0, body_round 50, slightly wide. Soft mint
     * body against the 0x101018 background, blush accent, dark eyes. Nubs
     * for limbs, no ears and no tail - so it renders completely from LVGL
     * primitives with nothing deferred to PNG. */
    /* index 0 == FORM_BABY */
    {
        .body_w = 108, .body_h = 96,
        .body_round = 50,
        .body_squash = 8,
        .head_ratio = 0,
        .c_body   = 0x7FD8C0,
        .c_belly  = 0xA9E9DA,
        .c_accent = 0xFF9EB5,
        .c_eye    = 0x16161E,
        .eye_style = EYE_OVAL, .eye_spacing = 40, .eye_size = 17,
        .mouth_style = MOUTH_SMILE,
        .brow_style = BROW_NONE,
        .ear_style = EAR_NONE,
        .limb_style = LIMB_NUBS,
        .tail_style = TAIL_NONE,
        .cheek_blush = 1,
        .deco_mask = 0,
    },


    /* ---- FORM_KID_GOOD -------------------------------------------------
     * The lineage colour carries forward from the Baby; a Good Kid is the
     * Baby grown slightly taller and brighter. */
    {
        .body_w = 112, .body_h = 104, .body_round = 46, .body_squash = 4,
        .head_ratio = 0,
        .c_body = 0x7FD8C0, .c_belly = 0xB4EEDF, .c_accent = 0xFF9EB5, .c_eye = 0x16161E,
        .eye_style = EYE_OVAL, .eye_spacing = 42, .eye_size = 17,
        .mouth_style = MOUTH_SMILE, .brow_style = BROW_NONE,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 1, .deco_mask = 0,
    },

    /* ---- FORM_KID_MISCHIEF ---------------------------------------------
     * Cheeky rather than nasty: a smirk and a raised brow. Low discipline is
     * meant to read as funny, never as a pet you should feel bad about. */
    {
        .body_w = 110, .body_h = 102, .body_round = 44, .body_squash = 6,
        .head_ratio = 0,
        .c_body = 0x9FD86B, .c_belly = 0xCBEBA4, .c_accent = 0xFFB347, .c_eye = 0x16161E,
        .eye_style = EYE_DOT, .eye_spacing = 44, .eye_size = 16,
        .mouth_style = MOUTH_SMIRK, .brow_style = BROW_ANGRY,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 1, .deco_mask = 0,
    },

    /* ---- FORM_TEEN_BRIGHT ---------------------------------------------- */
    {
        .body_w = 116, .body_h = 122, .body_round = 40, .body_squash = -6,
        .head_ratio = 0,
        .c_body = 0x74D0E8, .c_belly = 0xB2E7F5, .c_accent = 0xFFC7DA, .c_eye = 0x16161E,
        .eye_style = EYE_STAR, .eye_spacing = 44, .eye_size = 16,
        .mouth_style = MOUTH_OPEN_HAPPY, .brow_style = BROW_NONE,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 1, .deco_mask = 0,
    },

    /* ---- FORM_TEEN_MELLOW ---------------------------------------------- */
    {
        .body_w = 118, .body_h = 118, .body_round = 44, .body_squash = 0,
        .head_ratio = 0,
        .c_body = 0x8FBFE8, .c_belly = 0xC4DCF3, .c_accent = 0xFFB6C8, .c_eye = 0x16161E,
        .eye_style = EYE_OVAL, .eye_spacing = 44, .eye_size = 17,
        .mouth_style = MOUTH_SMILE, .brow_style = BROW_NONE,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 1, .deco_mask = 0,
    },

    /* ---- FORM_TEEN_ROWDY ----------------------------------------------- */
    {
        .body_w = 120, .body_h = 116, .body_round = 36, .body_squash = 8,
        .head_ratio = 0,
        .c_body = 0xE8A05A, .c_belly = 0xF5CFA6, .c_accent = 0xFF7B6B, .c_eye = 0x16161E,
        .eye_style = EYE_ANGRY_SLANT, .eye_spacing = 46, .eye_size = 17,
        .mouth_style = MOUTH_TOOTHY, .brow_style = BROW_ANGRY,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 0, .deco_mask = 0,
    },

    /* ---- FORM_ADULT_BEST ----------------------------------------------- */
    {
        .body_w = 128, .body_h = 134, .body_round = 42, .body_squash = 0,
        .head_ratio = 0,
        .c_body = 0x7FE8C8, .c_belly = 0xC2F5E4, .c_accent = 0xFFD1E0, .c_eye = 0x16161E,
        .eye_style = EYE_STAR, .eye_spacing = 46, .eye_size = 17,
        .mouth_style = MOUTH_OPEN_HAPPY, .brow_style = BROW_NONE,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 1, .deco_mask = DECO_SPARKLE,
    },

    /* ---- FORM_ADULT_SWEET ---------------------------------------------- */
    {
        .body_w = 126, .body_h = 128, .body_round = 46, .body_squash = 2,
        .head_ratio = 0,
        .c_body = 0xFFB6C8, .c_belly = 0xFFD9E3, .c_accent = 0xFF8FA8, .c_eye = 0x16161E,
        .eye_style = EYE_OVAL, .eye_spacing = 44, .eye_size = 18,
        .mouth_style = MOUTH_SMILE, .brow_style = BROW_NONE,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 1, .deco_mask = 0,
    },

    /* ---- FORM_ADULT_PLAYFUL -------------------------------------------- */
    {
        .body_w = 124, .body_h = 124, .body_round = 44, .body_squash = 4,
        .head_ratio = 0,
        .c_body = 0xFFD24A, .c_belly = 0xFFE9A8, .c_accent = 0xFF9E5A, .c_eye = 0x16161E,
        .eye_style = EYE_DOT, .eye_spacing = 46, .eye_size = 17,
        .mouth_style = MOUTH_OPEN_HAPPY, .brow_style = BROW_NONE,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 1, .deco_mask = 0,
    },

    /* ---- FORM_ADULT_CHONKY ---------------------------------------------
     * Widest form. body_w is held at 116 on purpose: the renderer multiplies
     * width by the squash (+26 -> x1.13) and then by up to 1.2 for weight,
     * so 116 x 1.13 x 1.2 = 157 - just inside the 160 px pet box. A larger
     * base here would clip at maximum weight, which is exactly the state a
     * Chonky is most likely to be in. */
    {
        .body_w = 116, .body_h = 116, .body_round = 50, .body_squash = 26,
        .head_ratio = 0,
        .c_body = 0xF2B07A, .c_belly = 0xFAD8B8, .c_accent = 0xFF9EB5, .c_eye = 0x16161E,
        .eye_style = EYE_OVAL, .eye_spacing = 46, .eye_size = 16,
        .mouth_style = MOUTH_SMILE, .brow_style = BROW_NONE,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 1, .deco_mask = DECO_CRUMBS,
    },

    /* ---- FORM_ADULT_GRUMPY ---------------------------------------------- */
    {
        .body_w = 124, .body_h = 122, .body_round = 38, .body_squash = 0,
        .head_ratio = 0,
        .c_body = 0x9AA0B8, .c_belly = 0xC6CBDC, .c_accent = 0xB78A9A, .c_eye = 0x16161E,
        .eye_style = EYE_ANGRY_SLANT, .eye_spacing = 44, .eye_size = 17,
        .mouth_style = MOUTH_FROWN, .brow_style = BROW_ANGRY,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 0, .deco_mask = 0,
    },

    /* ---- FORM_ADULT_SCRUFFY --------------------------------------------- */
    {
        .body_w = 122, .body_h = 120, .body_round = 40, .body_squash = -4,
        .head_ratio = 0,
        .c_body = 0xB0A184, .c_belly = 0xD3C8B0, .c_accent = 0x9AB07A, .c_eye = 0x16161E,
        .eye_style = EYE_SLEEPY, .eye_spacing = 44, .eye_size = 17,
        .mouth_style = MOUTH_WOBBLE, .brow_style = BROW_WORRIED,
        .ear_style = EAR_NONE, .limb_style = LIMB_NUBS, .tail_style = TAIL_NONE,
        .cheek_blush = 0, .deco_mask = DECO_STINK,
    },
};

static inline bool forms_is_populated(uint8_t id)
{
    return id < FORM_COUNT && PET_FORMS[id].body_w != 0;
}

/* Always returns something renderable. An unpopulated form falls back to the
 * Baby rather than rendering a zero-sized pet, so a bad form_id in a save
 * file degrades visibly-but-safely instead of drawing nothing at all. */
static inline const pet_form_t *forms_get(uint8_t id)
{
    return forms_is_populated(id) ? &PET_FORMS[id] : &PET_FORMS[FORM_BABY];
}

static inline const char *forms_name(uint8_t id)
{
    switch (id) {
        case FORM_BABY:           return "Baby";
        case FORM_KID_GOOD:       return "Good Kid";
        case FORM_KID_MISCHIEF:   return "Mischief Kid";
        case FORM_TEEN_BRIGHT:    return "Bright Teen";
        case FORM_TEEN_MELLOW:    return "Mellow Teen";
        case FORM_TEEN_ROWDY:     return "Rowdy Teen";
        case FORM_ADULT_BEST:     return "Best";
        case FORM_ADULT_SWEET:    return "Sweet";
        case FORM_ADULT_PLAYFUL:  return "Playful";
        case FORM_ADULT_CHONKY:   return "Chonky";
        case FORM_ADULT_GRUMPY:   return "Grumpy";
        case FORM_ADULT_SCRUFFY:  return "Scruffy";
        default:                  return "?";
    }
}
