/* voice - mount the spoken-line pack and look lines up. See voice.h. */

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>

#include "config.h"
#include "voice.h"

/* One file per gender; the NAME must match the partition in
 * partitions_visitor.csv. */
#define VOICE_PATH_BOY  "/voice_boy.bin"
#define VOICE_PATH_GIRL "/voice_girl.bin"
#define VOICE_PARTITION "littlefs"
#define VOICE_MAGIC 0x31505656UL          /* "VVP1", little-endian          */

typedef struct __attribute__((packed)) {
    uint32_t hash, off, len;
} vindex_t;

typedef struct {
    File      f;
    vindex_t *idx;
    uint32_t  n, data0, bytes;
    bool      ready;
} pack_t;

static pack_t   s_pack[VOICE_PACKS];
static uint32_t s_rate = 16000;
static bool     s_any;
static bool     s_force_miss;   /* diagnostic only - see voice.h */

void voice_set_force_miss(bool on) { s_force_miss = on; }
bool voice_force_miss(void)        { return s_force_miss; }

bool voice_ready(void) { return s_any; }
bool voice_pack_ready(uint8_t p) { return p < VOICE_PACKS && s_pack[p].ready; }
uint32_t voice_rate(void)  { return s_rate; }
uint32_t voice_count(void)
{
    /* Both packs carry the same lines, so one count describes the set. */
    for (uint8_t i = 0; i < VOICE_PACKS; i++)
        if (s_pack[i].ready) return s_pack[i].n;
    return 0;
}

uint32_t voice_hash(const char *text)
{
    uint32_t h = 0x811C9DC5UL;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        h ^= (uint32_t)*p;
        h *= 0x01000193UL;
    }
    return h;
}

/* Open one pack. A pack that fails to load leaves the OTHER usable - a
 * half-flashed device should lose one voice, not all speech. */
static bool pack_open(pack_t *pk, const char *path, const char *label)
{
    pk->f = LittleFS.open(path, "r");
    if (!pk->f || pk->f.size() < 16) {
        Serial.printf("VOICE: no %s (%s) - that gender will chirp\n", label, path);
        return false;
    }
    uint32_t hdr[4];
    if (pk->f.read((uint8_t *)hdr, sizeof(hdr)) != (int)sizeof(hdr) ||
        hdr[0] != VOICE_MAGIC) {
        Serial.printf("VOICE: %s header is not VVP1 - ignoring\n", label);
        pk->f.close(); return false;
    }
    pk->n = hdr[1];
    if (hdr[2]) s_rate = hdr[2];
    if (pk->n == 0 || pk->n > 4000) {
        Serial.printf("VOICE: %s implausible clip count %lu\n", label,
                      (unsigned long)pk->n);
        pk->f.close(); return false;
    }
    pk->bytes = pk->n * sizeof(vindex_t);
    /* PSRAM: the index is a few KB and internal SRAM is the scarce resource
     * on this board. */
    pk->idx = (vindex_t *)heap_caps_malloc(pk->bytes, MALLOC_CAP_SPIRAM);
    if (!pk->idx) pk->idx = (vindex_t *)malloc(pk->bytes);
    if (!pk->idx || pk->f.read((uint8_t *)pk->idx, pk->bytes) != (int)pk->bytes) {
        Serial.printf("VOICE: %s index load failed\n", label);
        if (pk->idx) { free(pk->idx); pk->idx = nullptr; }
        pk->f.close(); return false;
    }
    pk->data0 = sizeof(hdr) + pk->bytes;
    pk->ready = true;
    Serial.printf("VOICE: %s - %lu clips, %lu KB\n", label,
                  (unsigned long)pk->n,
                  (unsigned long)((pk->f.size() - pk->data0) / 1024));
    return true;
}

bool voice_begin(void)
{
    if (s_any) return true;
    /* LittleFS, NOT SPIFFS, and the partition LABEL must be passed
     * explicitly. The toolchain builds a LittleFS image, and Arduino's
     * LittleFS defaults to looking for a partition called "spiffs" while
     * partitions_visitor.csv names ours "littlefs". Both mismatches produce
     * the same misleading symptom - a clean build reporting "no filesystem"
     * over a perfectly good image - and both cost a round trip to find.
     *
     * false = do NOT format on failure. A missing or unformatted partition
     * means "no voice pack", which is a fine state to run in; it must never
     * turn into "reformat the user's flash". */
    if (!LittleFS.begin(false, "/littlefs", 10, VOICE_PARTITION)) {
        Serial.println("VOICE: no filesystem - chirp voice only");
        return false;
    }
    const bool b = pack_open(&s_pack[VOICE_BOY],  VOICE_PATH_BOY,  "boy");
    const bool g = pack_open(&s_pack[VOICE_GIRL], VOICE_PATH_GIRL, "girl");
    s_any = b || g;
    if (!s_any) Serial.println("VOICE: no packs loaded - chirp voice only");
    else Serial.printf("VOICE: ready @ %lu Hz\n", (unsigned long)s_rate);
    return s_any;
}

bool voice_lookup(uint8_t pack, const char *text, uint32_t *off, uint32_t *len)
{
    if (s_force_miss) return false;    /* diagnostic: pretend there is no clip */
    if (pack >= VOICE_PACKS || !s_pack[pack].ready || !text || !*text) return false;
    const pack_t *pk = &s_pack[pack];
    const uint32_t h = voice_hash(text);
    /* The generator sorts the index, so this is a binary search rather than a
     * walk over 258 entries on every spoken line. */
    uint32_t lo = 0, hi = pk->n;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (pk->idx[mid].hash < h)      lo = mid + 1;
        else if (pk->idx[mid].hash > h) hi = mid;
        else {
            if (off) *off = pk->data0 + pk->idx[mid].off;
            if (len) *len = pk->idx[mid].len;
            return true;
        }
    }
    return false;
}

int voice_read(uint8_t pack, uint32_t off, uint8_t *dst, int n)
{
    if (pack >= VOICE_PACKS || !s_pack[pack].ready || n <= 0) return 0;
    pack_t *pk = &s_pack[pack];
    if (!pk->f.seek(off)) return 0;
    return pk->f.read(dst, (size_t)n);
}

void voice_report(void)
{
    Serial.println();
    Serial.println("=== VOICE PACKS ============================================");
    if (!s_any) {
        Serial.println("  NONE LOADED - the Visitor uses the chirp voice.");
        Serial.println("  Build:  see tools/voicepack/README.md");
        Serial.println("  Flash:  pio run -t uploadfs   (does NOT touch NVS)");
        Serial.println("-----------------------------------------------------------");
        return;
    }
    static const char *NM[VOICE_PACKS] = { "boy (Norman)", "girl (Kristin)" };
    for (uint8_t i = 0; i < VOICE_PACKS; i++) {
        if (!s_pack[i].ready) { Serial.printf("  %-14s : NOT LOADED\n", NM[i]); continue; }
        Serial.printf("  %-14s : %lu clips, %lu KB audio, %lu B index\n", NM[i],
                      (unsigned long)s_pack[i].n,
                      (unsigned long)((s_pack[i].f.size() - s_pack[i].data0) / 1024),
                      (unsigned long)s_pack[i].bytes);
    }
    Serial.printf("  rate           : %lu Hz, 4-bit IMA ADPCM\n", (unsigned long)s_rate);
    Serial.println("  Lines are keyed by FNV-1a of their text; a line with no clip");
    Serial.println("  falls back to the chirp voice rather than going silent.");
    Serial.println("  Gender picks the pack and is PRESENTATION ONLY.");
    Serial.println("-----------------------------------------------------------");
}
