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
#include "voice.h"

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
enum { W_SINE = 0, W_SQUARE, W_NOISE, W_TRI };
/* ENV_BLIP is the arcade shape - quick on, long even decay. ENV_THUMP is a
 * physical one: near-instant attack, exponential decay, no sustain. A
 * footstep is a THUMP; giving it a blip envelope and a pitch is precisely
 * what made the first version sound like a video game. */
enum { ENV_BLIP = 0, ENV_THUMP };
typedef struct {
    uint16_t hz0, hz1; uint16_t ms; uint8_t wave; uint8_t amp;
    uint8_t  env;        /* ENV_*                                          */
    uint16_t res_hz;     /* 0 = none; otherwise resonate the source here    */
} seg_t;

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

static const uint16_t FORMANT[VOW_COUNT][3] = {
    /* F1    F2    F3  */
    { 1120, 1820, 3200 },   /* "ah" - open, the default babble vowel  */
    {  700, 2660, 3400 },   /* "eh" - bright                          */
    {  420, 3360, 3800 },   /* "ee" - brightest, good for questions   */
    {  700, 1260, 3000 },   /* "oh" - round                           */
    {  490, 1120, 2900 },   /* "oo" - darkest, good for grumbling     */
};

/* `cons` gives the syllable an unvoiced onset and `gap_ms` the pause after
 * it. Both are what turn a run of vowels into something with WORDS in it. */
typedef struct {
    uint16_t f0a, f0b; uint8_t vowel; uint16_t ms;
    uint8_t  cons; uint16_t gap_ms;
} vseg_t;
typedef struct { vseg_t seg[4]; uint8_t n; } speech_t;

/* A request travelling to the mixer task. */
enum { REQ_BEEP = 0, REQ_SPEECH, REQ_CLIP };
typedef struct {
    uint8_t kind; voice_t v; speech_t sp;
    uint32_t off, len; float pitch; uint8_t pack;  /* REQ_CLIP             */
} req_t;

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
        reso_t body = {};
        if (s->res_hz) reso_set(&body, (float)s->res_hz, 190.0f);
        int done = 0;
        while (done < total) {
            const int n = (total - done > AU_CHUNK) ? AU_CHUNK : total - done;
            for (int k = 0; k < n; k++) {
                const float t  = (float)(done + k) / (float)total;
                const float hz = s->hz0 + (s->hz1 - s->hz0) * t;
                ph += hz / (float)AU_RATE;
                if (ph >= 1.0f) ph -= 1.0f;
                float env;
                if (s->env == ENV_THUMP) {
                    /* Physical: hit and gone. The 2 % attack removes the
                     * click without softening the impact. */
                    if (t < 0.02f) env = t / 0.02f;
                    else           env = expf(-5.5f * (t - 0.02f));
                } else {
                    /* Short attack, long-ish decay: a click-free blip. */
                    const float atk = 0.06f;
                    if (t < atk)   env = t / atk;
                    else           env = 1.0f - (t - atk) / (1.0f - atk) * 0.85f;
                }
                float src = wave_at(s->wave, ph);
                /* A resonator turns a hiss into something with a BODY - the
                 * difference between static and a foot landing on a floor. */
                if (s->res_hz) src = reso_run(&body, src) * 0.30f;
                const float a = src * env
                              * ((float)s->amp / 255.0f) * amp_master;
                const int16_t v16 = (int16_t)(a * 20000.0f);
                buf[k * 2] = v16; buf[k * 2 + 1] = v16;
            }
            s_i2s.write((uint8_t *)buf, (size_t)n * 4);
            done += n;
        }
    }
}

/* WHY THE FIRST TWO ATTEMPTS SOUNDED ROBOTIC, and what each fix addresses.
 * Tone bursts had no formants at all. Adding formants made it intelligible
 * but still machine-like, because four things were still perfectly regular
 * in a way no mouth ever is:
 *
 *   1. PERIODICITY. A pulse train with an exact period is the single
 *      strongest robot cue there is. Real vocal folds wobble a few percent
 *      every cycle (jitter) and vary in strength (shimmer). Both are added
 *      per PERIOD, not per sample - jittering every sample is just noise.
 *   2. INSTANT VOWEL CHANGES. Mouths have mass; formants SLIDE between
 *      targets. Jumping between them is the sound of a machine switching
 *      filters, so the resonators now glide and are never reset mid-word.
 *   3. NO CONSONANTS. Unbroken voicing is a hum. Real speech keeps
 *      interrupting itself with unvoiced noise, so a syllable can now open
 *      with a short noise burst - the source switches from noise to pulses,
 *      which is exactly what an actual consonant-vowel transition is.
 *   4. METRONOMIC TIMING. Equal syllables with no gaps is chanting. Lengths
 *      and gaps are varied per syllable by the caller.
 *
 * Plus a little aspiration mixed into the voiced source, because a pure
 * pulse train is cleaner than any real voice ever is. */
static void render_speech(const speech_t *sp)
{
    static int16_t buf[AU_CHUNK * 2];
    const float amp_master = vol_scale();
    if (amp_master <= 0.0f) return;

    reso_t r1 = {}, r2 = {}, r3 = {};
    /* Current (gliding) formant positions. Start at the first vowel so the
     * word does not open with a swoop from nowhere. */
    const uint8_t v0 = sp->seg[0].vowel % VOW_COUNT;
    float c1 = FORMANT[v0][0], c2 = FORMANT[v0][1], c3 = FORMANT[v0][2];
    reso_set(&r1, c1, 90.0f); reso_set(&r2, c2, 110.0f); reso_set(&r3, c3, 170.0f);

    float ph = 0.0f;          /* glottal phase                              */
    float period_gain = 1.0f; /* shimmer, redrawn each period               */
    float jit = 1.0f;         /* jitter, redrawn each period                */
    int   coef_age = 0;

    for (uint8_t i = 0; i < sp->n; i++) {
        const vseg_t *g = &sp->seg[i];
        const int total = (int)((uint32_t)g->ms * AU_RATE / 1000);
        if (total <= 0) continue;
        const uint8_t vw = g->vowel % VOW_COUNT;
        const float t1 = FORMANT[vw][0], t2 = FORMANT[vw][1], t3 = FORMANT[vw][2];
        /* Consonant occupies the first slice of the syllable. */
        const float cons_end = g->cons ? 0.16f : 0.0f;

        int done = 0;
        while (done < total) {
            const int n = (total - done > AU_CHUNK) ? AU_CHUNK : total - done;
            for (int k = 0; k < n; k++) {
                const float t = (float)(done + k) / (float)total;

                /* --- glide the formants (coarticulation) ---------------- */
                c1 += (t1 - c1) * 0.0016f;
                c2 += (t2 - c2) * 0.0016f;
                c3 += (t3 - c3) * 0.0016f;
                if (++coef_age >= 32) {          /* recompute occasionally  */
                    coef_age = 0;
                    reso_set(&r1, c1, 90.0f);
                    reso_set(&r2, c2, 110.0f);
                    reso_set(&r3, c3, 170.0f);
                }

                /* --- source -------------------------------------------- */
                float exc;
                if (t < cons_end) {
                    /* Unvoiced onset: noise, no glottal pulse at all. */
                    exc = ((float)random(0, 2001) - 1000.0f) / 1000.0f;
                    exc *= 0.5f;
                    ph = 0.0f;
                } else {
                    const float base = g->f0a + (g->f0b - g->f0a) * t;
                    ph += (base * jit) / (float)AU_RATE;
                    exc = 0.0f;
                    if (ph >= 1.0f) {
                        ph -= 1.0f;
                        /* New period: redraw jitter and shimmer. This is the
                         * single most effective de-robotising step. */
                        jit = 1.0f + ((float)random(0, 61) - 30.0f) / 1000.0f;
                        period_gain = 0.85f + (float)random(0, 31) / 100.0f;
                        exc = period_gain;
                    } else if (ph < 0.07f) {
                        exc = 0.32f * period_gain;
                    }
                    /* Aspiration: no real voice is a clean pulse train. */
                    exc += (((float)random(0, 2001) - 1000.0f) / 1000.0f) * 0.035f;
                }

                float v = reso_run(&r1, exc) * 0.70f
                        + reso_run(&r2, exc) * 0.42f
                        + reso_run(&r3, exc) * 0.16f;

                /* --- envelope ------------------------------------------- */
                float env;
                if (t < 0.10f)      env = t / 0.10f;
                else if (t > 0.70f) env = (1.0f - t) / 0.30f;
                else                env = 1.0f;
                env *= env;
                if (t < cons_end) env *= 0.55f;   /* consonants are quieter */

                v *= env * 0.15f * amp_master;
                if (v >  1.0f) v =  1.0f;
                if (v < -1.0f) v = -1.0f;
                const int16_t s16 = (int16_t)(v * 20000.0f);
                buf[k * 2] = s16; buf[k * 2 + 1] = s16;
            }
            s_i2s.write((uint8_t *)buf, (size_t)n * 4);
            done += n;
        }

        /* --- the gap between syllables ------------------------------------
         * Silence, but the resonators are NOT reset - the mouth stays where
         * it was, so the next syllable continues from this one rather than
         * restarting. */
        if (g->gap_ms) {
            int rem = (int)((uint32_t)g->gap_ms * AU_RATE / 1000);
            while (rem > 0) {
                const int n = (rem > AU_CHUNK) ? AU_CHUNK : rem;
                for (int k = 0; k < n * 2; k++) buf[k] = 0;
                s_i2s.write((uint8_t *)buf, (size_t)n * 4);
                rem -= n;
            }
        }
    }
}

/* --- recorded speech ------------------------------------------------------
 * 4-bit IMA ADPCM in, resampled straight out to the codec. The resample does
 * double duty: it converts 11.025 kHz to the 16 kHz the codec runs at, AND
 * applies the per-stage pitch. Pitch and tempo move together, which is
 * exactly how a smaller creature sounds - the pack is rendered slow on
 * purpose so the sped-up result lands near natural speech.
 *
 * Linear interpolation, not nearest-neighbour: at these ratios nearest
 * neighbour adds an audible grain to sibilants, and the interpolation costs
 * one multiply. */
static const int16_t ADPCM_STEP[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,
    80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,
    494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,
    2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,
    8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
    27086,29794,32767
};
static const int8_t ADPCM_IDX[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};

typedef struct { int32_t pred; int8_t index; } adpcm_t;

static inline int16_t adpcm_step_one(adpcm_t *st, uint8_t code)
{
    const int32_t step = ADPCM_STEP[st->index];
    int32_t diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    st->pred = (code & 8) ? st->pred - diff : st->pred + diff;
    if (st->pred >  32767) st->pred =  32767;
    if (st->pred < -32768) st->pred = -32768;
    st->index += ADPCM_IDX[code];
    if (st->index < 0)  st->index = 0;
    if (st->index > 88) st->index = 88;
    return (int16_t)st->pred;
}

static void render_clip(uint8_t pack, uint32_t off, uint32_t len, float pitch)
{
    static uint8_t  enc[512];
    static int16_t  out[AU_CHUNK * 2];
    const float amp = vol_scale();
    if (amp <= 0.0f || !len) return;

    adpcm_t st = { 0, 0 };
    /* Source samples consumed per output sample. */
    const float advance = ((float)voice_rate() / (float)AU_RATE) * pitch;
    float    frac = 0.0f;
    int16_t  prev = 0, cur = 0;
    bool     primed = false;
    uint32_t read_at = off;
    uint32_t left = len;
    int      have = 0, take = 0;      /* bytes in enc[], next byte index    */
    bool     hi_nibble = false;       /* which half of the current byte     */
    int      outn = 0;

    for (;;) {
        /* Pull source samples until the interpolator is ahead of the output. */
        while (!primed || frac >= 1.0f) {
            if (take >= have) {
                if (!left) goto flush;
                const int want = (int)(left > sizeof(enc) ? sizeof(enc) : left);
                have = voice_read(pack, read_at, enc, want);
                if (have <= 0) goto flush;
                read_at += have; left -= have;
                take = 0; hi_nibble = false;
            }
            uint8_t code;
            if (!hi_nibble) { code = enc[take] & 0x0F; hi_nibble = true; }
            else            { code = enc[take] >> 4;   hi_nibble = false; take++; }
            prev = cur;
            cur  = adpcm_step_one(&st, code);
            if (!primed) { prev = cur; primed = true; }
            else         frac -= 1.0f;
        }

        {
            const float v = (prev + (cur - prev) * frac) * amp;
            const int16_t s16 = (int16_t)(v > 32000 ? 32000 : (v < -32000 ? -32000 : v));
            out[outn * 2] = s16; out[outn * 2 + 1] = s16;
        }
        if (++outn >= AU_CHUNK) {
            s_i2s.write((uint8_t *)out, (size_t)outn * 4);
            outn = 0;
        }
        frac += advance;
    }
flush:
    if (outn) s_i2s.write((uint8_t *)out, (size_t)outn * 4);
}

static void mixer_task(void *arg)
{
    (void)arg;
    req_t r;
    for (;;) {
        if (xQueueReceive(s_q, &r, portMAX_DELAY) != pdTRUE) continue;
        if (r.kind == REQ_CLIP)        render_clip(r.pack, r.off, r.len, r.pitch);
        else if (r.kind == REQ_SPEECH) render_speech(&r.sp);
        else                           render(&r.v);
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
    /* A FOOTSTEP IS A NOISE TRANSIENT, NOT A NOTE. The first version was a
     * pitched triangle blip, which is a video-game sound however quiet you
     * make it: the ear hears a musical pitch and knows it is synthetic.
     *
     * Now it is filtered noise with a thump envelope - broadband source, a
     * resonator around 150 Hz to give it the body of a foot on a floor, and
     * an exponential decay. No pitch to latch onto, so it reads as contact
     * rather than as a beep. The two feet differ slightly; see audio_play(). */
    case SND_STEP:        v.seg[0] = { 0, 0, 55, W_NOISE, 120, ENV_THUMP, 150 };
                          break;
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
    /* LEFT FOOT, RIGHT FOOT. Two identical steps in a row is the other tell
     * that a sound is synthetic - real feet are never acoustically the same
     * twice. Alternating the body frequency and nudging the length is enough
     * for the ear to stop hearing a repeated sample. */
    if (s == SND_STEP && v.n) {
        static bool other_foot;
        other_foot = !other_foot;
        v.seg[0].res_hz = other_foot ? 138 : 163;
        v.seg[0].ms     = other_foot ? 58  : 50;
        v.seg[0].amp    = (uint8_t)(other_foot ? 120 : 104);
    }
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

        /* DECLINATION. Pitch drifts DOWN across an utterance in every human
         * language; holding it flat is one of the things that makes a
         * synthesiser sound like it is reading a list. */
        const float decl = 1.0f - 0.10f * ((float)i / (float)syllables);
        const float dir  = (i & 1) ? -1.0f : 1.0f;
        float f0 = base * decl * (1.0f + dir * spread * 0.30f);
        float f1 = base * decl * (1.0f - dir * spread * 0.30f);

        /* Irregular lengths and gaps. Equal syllables end to end is
         * chanting, not talking. */
        uint16_t ms  = (uint16_t)(syl_ms * (0.78f + (float)random(0, 45) / 100.0f));
        uint16_t gap = (uint16_t)random(18, 46);

        /* Most syllables get an unvoiced onset, but NOT all - a consonant on
         * every single one is its own kind of machine. The first one nearly
         * always does, because that is what makes the line start as a word
         * rather than fade in as a hum. */
        uint8_t cons = (i == 0) ? (random(0, 100) < 85) : (random(0, 100) < 55);

        /* A question lifts at the end, the way a real one does, and takes the
         * brightest vowel with it - and gets no trailing gap, so the rise is
         * the last thing heard. */
        if (question && i == syllables - 1) {
            f0 = base * decl; f1 = base * decl * 1.45f;
            vw = VOW_I; gap = 0; ms = (uint16_t)(ms * 1.25f);
        }
        if (i == syllables - 1 && !question) gap = 0;

        sp.seg[i] = { (uint16_t)f0, (uint16_t)f1, vw, ms, cons, gap };
    }

    req_t r = {}; r.kind = REQ_SPEECH; r.sp = sp;
    if (s_q) xQueueSend(s_q, &r, 0);
}

/* Stage pitch - a NARROW band, and deliberately so.
 *
 * The pack is rendered at the cuteness level chosen by ear (pitch x1.46 baked
 * in at render time, see tools/voicepack), and THAT is the Visitor's voice.
 * All this ladder has to do is separate the four stages from each other.
 *
 * It stays small because resampling moves pitch and TEMPO together: a wide
 * ladder would leave the Adult speaking noticeably slower than the Baby, and
 * the pace was tuned for intelligibility and must not drift. 7% total is
 * audible as growing up and costs almost nothing in pace.
 *
 * Baby is 1.00 - the recording exactly as approved - and the older stages
 * come DOWN from it. Fuller, never deep, never a realistic adult human. */
static float stage_pitch_of(uint8_t stage)
{
    switch (stage) {
        case STAGE_KID:   return 0.980f;
        case STAGE_TEEN:  return 0.955f;
        case STAGE_ADULT: return 0.930f;
        default:          return 1.000f;   /* Baby, and the egg             */
    }
}

static float stage_pitch(void) { return stage_pitch_of(pet_get()->stage); }

/* Which pack this Visitor speaks from. Gender is PRESENTATION ONLY - see
 * voice.h - and a voice is presentation, exactly like the egg colour tinting
 * the Baby. If only one pack is flashed, both genders use it: one voice is a
 * better outcome than falling back to chirps. */
static uint8_t voice_pack_for_pet(void)
{
    const uint8_t want = (pet_get()->gender == GENDER_GIRL) ? VOICE_GIRL : VOICE_BOY;
    if (voice_pack_ready(want)) return want;
    const uint8_t other = (want == VOICE_GIRL) ? VOICE_BOY : VOICE_GIRL;
    return voice_pack_ready(other) ? other : want;
}

void audio_say_as(const char *text, uint8_t stage)
{
    if (!s_ready || s_vol == VOL_MUTE || !text || !*text) return;
    uint32_t off, len;
    const uint8_t pack = voice_pack_for_pet();
    if (!voice_lookup(pack, text, &off, &len)) {
        Serial.printf("  (no clip for \"%s\" - it would chirp)\n", text);
        return;
    }
    req_t r = {};
    r.kind = REQ_CLIP; r.off = off; r.len = len;
    r.pitch = stage_pitch_of(stage); r.pack = pack;
    if (s_q) xQueueSend(s_q, &r, 0);
}

void audio_say_from(const char *text, uint8_t pack)
{
    if (!s_ready || s_vol == VOL_MUTE || !text || !*text) return;
    uint32_t off, len;
    if (!voice_lookup(pack, text, &off, &len)) {
        Serial.printf("  (pack %u has no clip for \"%s\")\n", pack, text);
        return;
    }
    req_t r = {};
    r.kind = REQ_CLIP; r.off = off; r.len = len;
    r.pitch = stage_pitch(); r.pack = pack;
    if (s_q) xQueueSend(s_q, &r, 0);
}

void audio_say(const char *text)
{
    if (!s_ready || s_vol == VOL_MUTE || !text || !*text) return;
    uint32_t off, len;
    const uint8_t pack = voice_pack_for_pet();
    if (voice_lookup(pack, text, &off, &len)) {
        req_t r = {};
        r.kind = REQ_CLIP; r.off = off; r.len = len;
        r.pitch = stage_pitch(); r.pack = pack;
        if (s_q) xQueueSend(s_q, &r, 0);
        return;
    }
    /* No recording for this line - a farewell note assembled at runtime, or
     * simply no pack flashed. Chirp it, so the Visitor is never mute. */
    const size_t n = strlen(text);
    audio_voice((uint8_t)(n <= 8 ? 1 : n <= 18 ? 2 : n <= 30 ? 3 : 4),
                text[n - 1] == '?');
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
