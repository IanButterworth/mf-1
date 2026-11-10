// The pianos and what their settings do to the engine and the effects, in plain C so the firmware builds this file.
#include "PianoCore.h"
#include "piano.h"

typedef struct {
  const char* name;
  const char* blurb;
  int16_t v[PN_FIELDS - 1];  // every field after the piano's own number
} pn_voicing_t;

// The measured piano first; the rest are what a technician, a room or a player's taste would do to it.
//                                                    tone sus room space vol unison knock stretch damper touch
static const pn_voicing_t kPianos[] = {
    {"Concert Grand", "Steinway B, as measured", {50, 100, 22, 1, 70, 100, 100, 100, 100, 80}},
    {"Bright Grand", "hammers hardened, for pop", {68, 100, 18, 0, 70, 100, 130, 100, 100, 88}},
    {"Mellow Grand", "soft hammers, late at night", {28, 110, 26, 1, 72, 90, 70, 100, 90, 72}},
    {"Felt Piano", "a felt strip before the hammers", {5, 55, 16, 0, 78, 80, 45, 100, 170, 70}},
    {"Studio Dry", "close microphones, no room", {54, 100, 0, 0, 70, 100, 110, 100, 100, 80}},
    {"Concert Hall", "from the stalls", {44, 105, 55, 1, 66, 100, 80, 100, 100, 80}},
    {"Honky-Tonk", "a saloon upright, out of tune", {60, 70, 14, 0, 70, 250, 150, 60, 140, 92}},
    {"Old Upright", "short strings, a worn action", {56, 60, 20, 0, 72, 220, 170, 70, 130, 84}},
};
#define PN_COUNT ((int)(sizeof kPianos / sizeof kPianos[0]))

// Every value fits a byte, as the firmware stores them.
static const int kLo[PN_FIELDS] = {0, 0, 25, 0, 0, 0, 0, 0, 0, 25, 1};
static const int kHi[PN_FIELDS] = {PN_COUNT - 1, 100, 200, 100, 2, 100, 250, 200, 200, 250, 127};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

int piano_count(void) { return PN_COUNT; }
const char* piano_name(int i) { return kPianos[clampi(i, 0, PN_COUNT - 1)].name; }
const char* piano_blurb(int i) { return kPianos[clampi(i, 0, PN_COUNT - 1)].blurb; }

void piano_default(int i, int32_t out[PN_FIELDS]) {
  i = clampi(i, 0, PN_COUNT - 1);
  out[0] = i;
  for (int k = 1; k < PN_FIELDS; k++) out[k] = kPianos[i].v[k - 1];
}

void piano_range(int field, int* lo, int* hi) {
  int f = clampi(field, 0, PN_FIELDS - 1);
  *lo = kLo[f];
  *hi = kHi[f];
}

void piano_clamp(int32_t s[PN_FIELDS]) {
  for (int k = 0; k < PN_FIELDS; k++) s[k] = clampi(s[k], kLo[k], kHi[k]);
}

void piano_setting(pn_piano_t* p, int id, int v) {
  switch (id) {
    case PN_F_TONE: p->tilt = (float)(v - 50) * 0.12f; break;
    case PN_F_SUSTAIN: p->decay = 100.0f / (float)clampi(v, 1, 1000); break;
    case PN_F_VOLUME: p->gain = 0.55f * (float)v / 70.0f; break;
    case PN_F_UNISON: p->beat = (float)v / 100.0f; break;
    case PN_F_KNOCK: p->knock = (float)v / 100.0f; break;
    case PN_F_STRETCH: p->stretch = (float)v / 100.0f; break;
    case PN_F_DAMPER: p->release = (float)v / 100.0f; break;
    default: break;
  }
}

void piano_fx(const int32_t s[PN_FIELDS], int cc[24]) {
  int room = s[PN_F_ROOM];
  for (int k = 0; k < 24; k++) cc[k] = 0;
  // The room is the reverb; every other effect stays off, as a piano wants.
  cc[4] = room > 0;
  cc[5] = s[PN_F_SPACE];
  cc[6] = 25 + room * 55 / 100;
  cc[7] = room * 60 / 100;
  // The off effects keep sensible values, should a player switch one on.
  cc[2] = 107;
  cc[9] = 40;
  cc[10] = 50;
}
