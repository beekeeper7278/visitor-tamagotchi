/* audio - I2S + ES8311 bring-up and the procedural voice. See audio.h. */

#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>

#include "config.h"
#include "board_pins.h"
#include "audio.h"
#include "forms.h"
#include "pet.h"
#include "evolve.h"

extern "C" {
#include "es8311/es8311.h"
}

/* 16 kHz mono-rendered, written as stereo because the codec is configured
 * for a two-slot frame. Low enough to keep the render buffer small, high
 * enough that a chirp does not sound like a buzz. */
#define AU_RATE        16000
#define AU_MCLK_MULT   256
#define AU_CHUNK       256          /* frames per render pass              */
#define AU_QUEUE_LEN   8

static I2SClass     s_i2s;
static bool         s_ready;
static uint8_t      s_vol = VOL_MED;
static TaskHandle_t s_task;
static QueueHandle_t s_q;

/* --- what a "sound" is ---------------------------------------------------
 * A tiny score: up to four segments of (frequency, ms, waveform), plus an
 * envelope. This is the whole synthesiser. It costs no flash beyond the
 * table and can be pitched at runtime, which is what makes one voice grow up
 * across four stages. */
typedef struct { uint16_t hz0, hz1; uint16_t ms; uint8_t wave; uint8_t amp; } seg_t;
enum { W_SINE = 0, W_SQUARE, W_NOISE, W_TRI };

typedef struct { seg_t seg[4]; uint8_t n; } voice_t;

/* --- the VOICE, as opposed to the beeps ---------------------------------
 * First cut rendered speech as pitched tone bursts and it sounded, in the
 * user's words, like R2D2 - which is exactly what tone bursts sound like.
 * Pitch is not what makes a noise read as a voice; FORMANTS are. A vowel is
 * two resonant peaks sitting on a buzzy source, and the ear identifies the
 * peaks, not the fundamental.
 *
 * So the voice is now a proper little formant synth: a pulse train
 * (the "vocal folds") through two 2-pole resonators (the "mouth"), with the
 * vowel changing per syllable. Same cost bracket as before - two biquads and
 * a phase counter - and it babbles rather than bleeps.
 *
 * Formants are scaled well ABOVE adult values. A small head has a short
 * vocal tract and therefore high formants; that is the acoustic reason a
 * child sounds like a child, and it is what keeps the Adult stage cute
 * rather than a realistic grown human, which the brief forbids. */
enum { VOW_A = 0, VOW_E, VOW_I, VOW_O, VOW_U, VOW_COUNT };

static const uint16_t FORMANT[VOW_COUNT][2] = {
    /* F1    F2   */
    { 1120, 1820 },   /* "ah" - open, the default babble vowel  */
    {  700, 2660 },   /* "eh" - bright                          */
    {  420, 3360 },   /* "ee" - brightest, good for questions   */
    {  700, 1260 },   /* "oh" - round                           */
    {  490, 1120 },   /* "oo" - darkest, good for grumbling     */
};

typedef struct { uint16_t f0a, f0b; uint8_t vowel; uint16_t ms; } vseg_t;
typedef struct { vseg_t seg[4]; uint8_t n; } speech_t;

/* A request travelling to the mixer task. */
enum { REQ_BEEP = 0, REQ_SPEECH };
typedef struct { uint8_t kind; voice_t v; speech_t sp; } req_t;

/* --- helpers ------------------------------------------------------------- */
static float vol_scale(void)
{
    switch (s_vol) {
        case VOL_MUTE: return 0.00f;
        case VOL_LOW:  return 0.18f;
        case VOL_MED:  return 0.45f;
        default:       return 0.90f;
    }
}

static inline float wave_at(uint8_t w, float ph)
{
    switch (w) {
        case W_SQUARE: return (ph < 0.5f) ? 1.0f : -1.0f;
        case W_TRI:    return 4.0f * fabsf(ph - 0.5f) - 1.0f;
        case W_NOISE:  return ((float)random(0, 2001) - 1000.0f) / 1000.0f;
        default:       return sinf(ph * 6.2831853f);
    }
}

/* Render one voice straight to the codec. Runs ONLY on the mixer task. */
static void render(const voice_t *v)
{
    static int16_t buf[AU_CHUNK * 2];
    const float amp_master = vol_scale();
    if (amp_master <= 0.0f) return;             /* MUTE: nothing at all     */

    float ph = 0.0f;
    for (uint8_t i = 0; i < v->n; i++) {
        const seg_t *s = &v->seg[i];
        const int total = (int)((uint32_t)s->ms * AU_RATE / 1000);
        int done = 0;
        while (done < total) {
            const int n = (total - done > AU_CHUNK) ? AU_CHUNK : total - done;
            for (int k = 0; k < n; k++) {
                const float t  = (float)(done + k) / (float)total;
                const float hz = s->hz0 + (s->hz1 - s->hz0) * t;
                ph += hz / (float)AU_RATE;
                if (ph >= 1.0f) ph -= 1.0f;
                /* Short attack, long-ish decay: a click-free blip. */
                float env = 1.0f;
                const float atk = 0.06f;
                if (t < atk)       env = t / atk;
                else               env = 1.0f - (t - atk) / (1.0f - atk) * 0.85f;
                const float a = wave_at(s->wave, ph) * env
                              * ((float)s->amp / 255.0f) * amp_master;
                const int16_t v16 = (int16_t)(a * 20000.0f);
                buf[k * 2] = v16; buf[k * 2 + 1] = v16;
            }
            s_i2s.write((uint8_t *)buf, (size_t)n * 4);
            done += n;
        }
    }
}

/* A 2-pole resonator. r sets the bandwidth, theta the centre frequency.
 * y[n] = x[n] + 2 r cos(theta) y[n-1] - r^2 y[n-2] */
typedef struct { float b1, b2, z1, z2; } reso_t;

static void reso_set(reso_t *f, float hz, float bw)
{
    const float r = expf(-3.14159265f * bw / (float)AU_RATE);
    const float th = 2.0f * 3.14159265f * hz / (float)AU_RATE;
    f->b1 = 2.0f * r * cosf(th);
    f->b2 = -r * r;
}
static inline float reso_run(reso_t *f, float x)
{
    const float y = x + f->b1 * f->z1 + f->b2 * f->z2;
    f->z2 = f->z1; f->z1 = y;
    return y;
}

static void render_speech(const speech_t *sp)
{
    static int16_t buf[AU_CHUNK * 2];
    const float amp_master = vol_scale();
    if (amp_master <= 0.0f) return;

    float ph = 0.0f;                 /* glottal phase   */
    reso_t f1 = {}, f2 = {};
    uint8_t last_vowel = 0xFF;

    for (uint8_t i = 0; i < sp->n; i++) {
        const vseg_t *g = &sp->seg[i];
        const int total = (int)((uint32_t)g->ms * AU_RATE / 1000);
        if (total <= 0) continue;
        const uint8_t vw = g->vowel % VOW_COUNT;
        if (vw != last_vowel) {
            /* ~110 Hz bandwidths: narrow enough to colour the tone strongly,
             * wide enough not to ring like a bell between pulses. */
            reso_set(&f1, (float)FORMANT[vw][0], 110.0f);
            reso_set(&f2, (float)FORMANT[vw][1], 130.0f);
            last_vowel = vw;
        }
        int done = 0;
        while (done < total) {
            const int n = (total - done > AU_CHUNK) ? AU_CHUNK : total - done;
            for (int k = 0; k < n; k++) {
                const float t = (float)(done + k) / (float)total;
                /* Pitch contour across the syllable, plus a little vibrato -
                 * a perfectly steady pitch is the other half of sounding
                 * robotic. */
                const float vib = 1.0f + 0.012f * sinf((float)(done + k) * 0.0045f);
                const float f0 = (g->f0a + (g->f0b - g->f0a) * t) * vib;

                /* Glottal source: a short pulse each period. Cheap, and much
                 * more voice-like than a sine, which has nothing for the
                 * formants to shape. */
                ph += f0 / (float)AU_RATE;
                float exc = 0.0f;
                if (ph >= 1.0f) { ph -= 1.0f; exc = 1.0f; }
                else if (ph < 0.08f) exc = 0.35f;      /* softens the click  */

                float v = reso_run(&f1, exc) * 0.75f + reso_run(&f2, exc) * 0.45f;

                /* Speech-shaped envelope: quick on, gentle off, and never a
                 * hard edge - a clicky syllable reads as a beep again. */
                float env;
                if (t < 0.12f)      env = t / 0.12f;
                else if (t > 0.72f) env = (1.0f - t) / 0.28f;
                else                env = 1.0f;
                env *= env;

                v *= env * 0.16f * amp_master;
                if (v >  1.0f) v =  1.0f;
                if (v < -1.0f) v = -1.0f;
                const int16_t s16 = (int16_t)(v * 20000.0f);
                buf[k * 2] = s16; buf[k * 2 + 1] = s16;
            }
            s_i2s.write((uint8_t *)buf, (size_t)n * 4);
            done += n;
        }
    }
}

static void mixer_task(void *arg)
{
    (void)arg;
    req_t r;
    for (;;) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) continue;
        if (r.kind == REQ_SPEECH) render_speech(&r.sp);
        else                      render(&r.v);
    }
}

static void submit(const voice_t *v)
{
    if (!s_ready || s_vol == VOL_MUTE || !s_q) return;
    req_t r; r.kind = REQ_BEEP; r.v = *v;
    /* Drop rather than block: audio must never stall the UI thread. */
    xQueueSend(s_q, &r, 0);
}

/* --- the sound table -----------------------------------------------------
 * Deliberately restrained. Footsteps and chews are near-silent ticks; the
 * loud slots are the ones that matter (hatch, evolve, farewell). */
static voice_t sound_of(snd_t s)
{
    voice_t v = {}; v.n = 1;
    switch (s) {
    case SND_UI_TAP:      v.seg[0] = {1800, 1800,  18, W_SINE,  90}; break;
    case SND_UI_PAGE:     v.seg[0] = {1200, 1600,  40, W_SINE, 110}; break;
    case SND_UI_CONFIRM:  v.n=2; v.seg[0]={1200,1200,50,W_SINE,140};
                                 v.seg[1]={1800,1800,70,W_SINE,140}; break;
    case SND_UI_REFUSE:   v.n=2; v.seg[0]={700,700,60,W_SQUARE,120};
                                 v.seg[1]={500,500,90,W_SQUARE,120}; break;
    /* A step is a soft tick, not a footfall sample - and care.cpp rate-limits
     * it so it cannot fire every animation frame. */
    case SND_STEP:        v.seg[0] = { 260,  200,  22, W_TRI,    45}; break;
    case SND_HAPPY:       v.n=3; v.seg[0]={900,900,60,W_SINE,150};
                                 v.seg[1]={1200,1200,60,W_SINE,150};
                                 v.seg[2]={1600,1600,90,W_SINE,150}; break;
    case SND_SAD:         v.n=2; v.seg[0]={800,600,120,W_SINE,130};
                                 v.seg[1]={600,420,160,W_SINE,120}; break;
    case SND_SURPRISE:    v.n=2; v.seg[0]={800,1900,90,W_SINE,160};
                                 v.seg[1]={1900,1700,60,W_SINE,140}; break;
    case SND_ANNOYED:     v.n=2; v.seg[0]={420,380,110,W_SQUARE,120};
                                 v.seg[1]={330,300,140,W_SQUARE,120}; break;
    case SND_GRUMBLE:     v.seg[0] = { 240,  200, 220, W_TRI,   110}; break;
    case SND_FOOD_DROP:   v.n=2; v.seg[0]={900,500,50,W_TRI,120};
                                 v.seg[1]={300,240,60,W_NOISE,70}; break;
    case SND_EAT:         v.n=2; v.seg[0]={420,360,55,W_TRI,90};
                                 v.seg[1]={360,300,55,W_TRI,80}; break;
    case SND_CAKE_YAY:    v.n=4; v.seg[0]={1000,1000,55,W_SINE,150};
                                 v.seg[1]={1300,1300,55,W_SINE,150};
                                 v.seg[2]={1600,1600,55,W_SINE,150};
                                 v.seg[3]={2100,2100,110,W_SINE,150}; break;
    case SND_REFUSE_FOOD: v.n=2; v.seg[0]={600,520,80,W_SQUARE,110};
                                 v.seg[1]={520,440,100,W_SQUARE,110}; break;
    /* Kept deliberately abstract and quiet - a soft descending whoosh, not a
     * bathroom impression. This is a child's device. */
    case SND_BATHROOM_GO: v.seg[0] = { 700,  340, 180, W_TRI,    70}; break;
    case SND_BATHROOM_BACK:v.seg[0]= { 480,  760, 150, W_TRI,    70}; break;
    case SND_CLEAN_PUFF:  v.n=2; v.seg[0]={1400,900,70,W_NOISE,70};
                                 v.seg[1]={900,600,80,W_NOISE,55}; break;
    case SND_SCOLD:       v.n=2; v.seg[0]={520,420,110,W_SQUARE,130};
                                 v.seg[1]={400,320,150,W_SQUARE,130}; break;
    case SND_PRAISE:      v.n=2; v.seg[0]={1200,1600,70,W_SINE,150};
                                 v.seg[1]={1600,2000,90,W_SINE,150}; break;
    case SND_BEDTIME:     v.n=3; v.seg[0]={900,800,110,W_SINE,110};
                                 v.seg[1]={800,650,130,W_SINE,100};
                                 v.seg[2]={650,520,170,W_SINE,90}; break;
    case SND_WAKE:        v.n=3; v.seg[0]={700,900,90,W_SINE,120};
                                 v.seg[1]={900,1200,90,W_SINE,130};
                                 v.seg[2]={1200,1500,110,W_SINE,140}; break;
    /* One sleepy in-breath. Sparse by policy - see care.cpp. */
    case SND_SNORE:       v.n=2; v.seg[0]={220,300,320,W_TRI,70};
                                 v.seg[1]={300,200,300,W_TRI,55}; break;
    case SND_HATCH_TICK:  v.seg[0] = {1700, 1700,  70, W_SINE, 170}; break;
    case SND_HATCH_CHIME: v.n=4; v.seg[0]={1046,1046,110,W_SINE,180};
                                 v.seg[1]={1318,1318,110,W_SINE,180};
                                 v.seg[2]={1568,1568,110,W_SINE,180};
                                 v.seg[3]={2093,2093,300,W_SINE,180}; break;
    case SND_EVOLVE:      v.n=4; v.seg[0]={700,1000,90,W_SINE,170};
                                 v.seg[1]={1000,1400,90,W_SINE,170};
                                 v.seg[2]={1400,1900,90,W_SINE,170};
                                 v.seg[3]={1900,2400,220,W_SINE,170}; break;
    case SND_FAREWELL:    v.n=4; v.seg[0]={1568,1568,180,W_SINE,150};
                                 v.seg[1]={1318,1318,180,W_SINE,150};
                                 v.seg[2]={1046,1046,180,W_SINE,150};
                                 v.seg[3]={ 880, 880,420,W_SINE,140}; break;
    case SND_GAME_SELECT: v.seg[0] = {1400, 1400,  35, W_SINE, 120}; break;
    case SND_GAME_REVEAL: v.n=2; v.seg[0]={900,1300,70,W_SINE,140};
                                 v.seg[1]={1300,1300,90,W_SINE,140}; break;
    case SND_GAME_CORRECT:v.n=2; v.seg[0]={1318,1318,80,W_SINE,160};
                                 v.seg[1]={1975,1975,140,W_SINE,160}; break;
    case SND_GAME_WRONG:  v.n=2; v.seg[0]={400,400,110,W_SQUARE,130};
                                 v.seg[1]={300,260,180,W_SQUARE,130}; break;
    case SND_GAME_HIT:    v.n=2; v.seg[0]={1600,2100,50,W_SINE,150};
                                 v.seg[1]={2100,2100,60,W_SINE,140}; break;
    case SND_GAME_MISS:   v.seg[0] = { 420,  300, 130, W_SQUARE,120}; break;
    case SND_GAME_GOAL:   v.n=3; v.seg[0]={1046,1318,90,W_SINE,170};
                                 v.seg[1]={1318,1568,90,W_SINE,170};
                                 v.seg[2]={2093,2093,240,W_SINE,170}; break;
    /* The maze bump: very short and soft, or a wall-hugging player is being
     * pinged constantly. */
    case SND_GAME_BUMP:   v.seg[0] = { 300,  220,  30, W_TRI,    55}; break;
    /* Memory needs four DISTINCT and pleasant pitches - a major arpeggio, so
     * a wrong answer is audibly wrong without being harsh. */
    case SND_MEMO_1:      v.seg[0] = { 523,  523, 220, W_SINE, 150}; break;
    case SND_MEMO_2:      v.seg[0] = { 659,  659, 220, W_SINE, 150}; break;
    case SND_MEMO_3:      v.seg[0] = { 784,  784, 220, W_SINE, 150}; break;
    case SND_MEMO_4:      v.seg[0] = {1046, 1046, 220, W_SINE, 150}; break;
    case SND_DIZZY:       v.n=4; v.seg[0]={900,700,90,W_SINE,140};
                                 v.seg[1]={700,1000,90,W_SINE,140};
                                 v.seg[2]={1000,650,90,W_SINE,140};
                                 v.seg[3]={650,900,120,W_SINE,130}; break;
    case SND_UPSIDE_DOWN: v.n=2; v.seg[0]={1000,500,180,W_SINE,140};
                                 v.seg[1]={500,380,200,W_TRI,120}; break;
    default:              v.n = 0; break;
    }
    return v;
}

void audio_play(snd_t s)
{
    if (s <= SND_NONE || s >= SND_COUNT) return;
    voice_t v = sound_of(s);
    if (v.n) submit(&v);
}

/* --- the Visitor's voice -------------------------------------------------
 * ONE character growing up: the same chirp shape throughout, with the pitch
 * coming down and the delivery steadying as the stages pass. The Adult is
 * deliberately still well above a human register - fuller than the Baby,
 * never deep, never realistic. */
void audio_voice(uint8_t syllables, bool question)
{
    if (!s_ready || s_vol == VOL_MUTE) return;
    const pet_state_t *p = pet_get();

    /* ONE character growing up. The pitch comes down and the delivery
     * steadies as the stages pass, but the formants stay high throughout -
     * that is what keeps the Adult unmistakably the same cute Visitor rather
     * than a realistic grown human, which the brief rules out explicitly. */
    float base; float spread; uint16_t syl_ms;
    switch (p->stage) {
        case STAGE_KID:   base = 330.0f; spread = 0.20f; syl_ms = 150; break;
        case STAGE_TEEN:  base = 285.0f; spread = 0.17f; syl_ms = 165; break;
        case STAGE_ADULT: base = 250.0f; spread = 0.14f; syl_ms = 180; break;
        default:          base = 400.0f; spread = 0.26f; syl_ms = 130; break;
    }

    /* Personality colours the DELIVERY, never the identity. */
    const uint8_t a = p->trait_a, b = p->trait_b;
    bool dark = false, bright = false;
    if (a == PERS_MISCHIEVOUS || b == PERS_MISCHIEVOUS ||
        a == PERS_PLAYFUL     || b == PERS_PLAYFUL)   { spread += 0.10f; bright = true; }
    if (a == PERS_SHY   || b == PERS_SHY)             { base *= 1.05f; syl_ms -= 15; }
    if (a == PERS_SLEEPY|| b == PERS_SLEEPY)          { syl_ms += 40; spread *= 0.6f; }
    if (a == PERS_DRAMATIC || b == PERS_DRAMATIC)     { spread += 0.08f; syl_ms += 20; }
    /* Forms carry meaning, so they are allowed to show - a Grumpy Adult
     * grumbles in the back of its mouth, a Scruffy one is goofier. */
    if (p->form_id == FORM_ADULT_GRUMPY)  { base *= 0.88f; dark = true; }
    if (p->form_id == FORM_ADULT_SCRUFFY) { spread += 0.12f; dark = true; }

    if (syllables < 1) syllables = 1;
    if (syllables > 4) syllables = 4;

    speech_t sp = {}; sp.n = syllables;
    for (uint8_t i = 0; i < syllables; i++) {
        /* Vary the vowel per syllable so it babbles instead of chanting one
         * note. Dark and bright personalities lean on different vowels. */
        uint8_t vw;
        if (dark)        vw = (i & 1) ? VOW_U : VOW_O;
        else if (bright) vw = (i & 1) ? VOW_E : VOW_A;
        else             vw = (uint8_t)(random(0, VOW_COUNT));

        const float dir = (i & 1) ? -1.0f : 1.0f;
        float f0 = base * (1.0f + dir * spread * 0.35f);
        float f1 = base * (1.0f - dir * spread * 0.35f);
        /* A question lifts at the end, the way a real one does, and takes the
         * brightest vowel with it. */
        if (question && i == syllables - 1) {
            f0 = base; f1 = base * 1.45f; vw = VOW_I;
        }
        sp.seg[i] = { (uint16_t)f0, (uint16_t)f1, vw, syl_ms };
    }

    req_t r; r.kind = REQ_SPEECH; r.sp = sp;
    if (s_q) xQueueSend(s_q, &r, 0);
}

/* --- volume -------------------------------------------------------------- */
void audio_set_volume(uint8_t level)
{
    s_vol = (level < VOL_COUNT) ? level : VOL_MED;
    Serial.printf("AUDIO: volume -> %s\n",
        s_vol == VOL_MUTE ? "MUTE" : s_vol == VOL_LOW ? "LOW"
      : s_vol == VOL_MED  ? "MEDIUM" : "HIGH");
}
uint8_t audio_volume(void) { return s_vol; }
bool    audio_ready(void)  { return s_ready; }

/* --- bring-up ------------------------------------------------------------ */
static es8311_handle_t s_es;

bool audio_init(void)
{
    if (s_ready) return true;

    pinMode(BSP_AUDIO_PA_EN, OUTPUT);
    digitalWrite(BSP_AUDIO_PA_EN, LOW);         /* quiet until configured   */

    s_i2s.setPins(BSP_I2S_BCLK, BSP_I2S_WS, BSP_I2S_DOUT,
                  BSP_I2S_DIN,  BSP_I2S_MCLK);
    if (!s_i2s.begin(I2S_MODE_STD, AU_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                     I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("AUDIO: I2S begin FAILED");
        return false;
    }

    s_es = es8311_create(0, BSP_ES8311_ADDR);   /* port 0 = the Wire bus    */
    if (!s_es) { Serial.println("AUDIO: es8311_create FAILED"); return false; }

    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,             /* MCLK is a real pin (16)  */
        .mclk_frequency = AU_RATE * AU_MCLK_MULT,
        .sample_frequency = AU_RATE
    };
    if (es8311_init(s_es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("AUDIO: es8311_init FAILED"); return false;
    }
    es8311_sample_frequency_config(s_es, clk.mclk_frequency, clk.sample_frequency);
    es8311_microphone_config(s_es, false);
    /* The codec runs near full scale; OUR volume control is the digital one
     * in vol_scale(), so Mute is provably silent at the source rather than
     * relying on the codec's own attenuation. */
    es8311_voice_volume_set(s_es, 85, NULL);

    s_q = xQueueCreate(AU_QUEUE_LEN, sizeof(req_t));
    if (!s_q) { Serial.println("AUDIO: queue alloc FAILED"); return false; }
    /* Own task: rendering must never stall LVGL or the care tick. */
    xTaskCreatePinnedToCore(mixer_task, "audio", 4096, NULL, 4, &s_task, 0);

    digitalWrite(BSP_AUDIO_PA_EN, HIGH);        /* amplifier on             */
    s_ready = true;
    Serial.printf("AUDIO: ready - I2S bclk %d ws %d dout %d din %d mclk %d, "
                  "PA %d, ES8311 @0x%02X, %d Hz\n",
                  BSP_I2S_BCLK, BSP_I2S_WS, BSP_I2S_DOUT, BSP_I2S_DIN,
                  BSP_I2S_MCLK, BSP_AUDIO_PA_EN, BSP_ES8311_ADDR, AU_RATE);
    return true;
}

void audio_test_tone(uint16_t ms, uint16_t hz)
{
    if (!s_ready) { Serial.println("AUDIO: not ready"); return; }
    Serial.printf("AUDIO: test tone %u Hz for %u ms at volume %u\n", hz, ms, s_vol);
    voice_t v = {}; v.n = 1;
    v.seg[0] = { hz, hz, ms, W_SINE, 200 };
    /* Bypass the mute gate ONLY here: this is the bring-up probe, and being
     * told "nothing is wired up" when the real answer is "you are muted"
     * would send the next person hunting the wrong fault. */
    const uint8_t save = s_vol;
    if (s_vol == VOL_MUTE) { s_vol = VOL_MED; Serial.println("  (muted - tone forced for the test)"); }
    req_t r; r.v = v;
    if (s_q) xQueueSend(s_q, &r, 0);
    s_vol = save;
}

void audio_report(void)
{
    Serial.println();
    Serial.println("=== AUDIO ==================================================");
    Serial.printf("  BSP_AUDIO_VERIFIED : %d\n", BSP_AUDIO_VERIFIED);
    Serial.printf("  state              : %s\n", s_ready ? "ready" : "NOT READY");
    Serial.printf("  I2S                : bclk %d  ws %d  dout %d  din %d  mclk %d\n",
                  BSP_I2S_BCLK, BSP_I2S_WS, BSP_I2S_DOUT, BSP_I2S_DIN, BSP_I2S_MCLK);
    Serial.printf("  PA enable          : GPIO %d (plain GPIO, HIGH = on)\n",
                  BSP_AUDIO_PA_EN);
    Serial.printf("  ES8311             : 0x%02X   MCLK %d Hz (%dx), rate %d Hz\n",
                  BSP_ES8311_ADDR, AU_RATE * AU_MCLK_MULT, AU_MCLK_MULT, AU_RATE);
    Serial.printf("  volume             : %u (%s)\n", s_vol,
                  s_vol == VOL_MUTE ? "MUTE" : s_vol == VOL_LOW ? "LOW"
                : s_vol == VOL_MED  ? "MEDIUM" : "HIGH");
    Serial.println("  source: waveshareteam/ESP32-S3-Touch-AMOLED-1.8, arduino-v2 tree");
    Serial.println("-----------------------------------------------------------");
}
