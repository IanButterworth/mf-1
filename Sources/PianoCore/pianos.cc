// The piano as MF1Core's instrument.
#include <cmath>

#include "MF1Core.h"
#include "PianoCore.h"
#include "piano.h"

namespace {

pn_piano_t g_piano;
int32_t g_set[PN_FIELDS];
bool g_ready = false;

void instPrepare(void*, double sr) {
  pn_init(&g_piano, (float)sr);
  for (int i = 0; i < PN_FIELDS; i++) piano_setting(&g_piano, i, g_set[i]);
  g_ready = true;
}
void instOn(void*, int note, int vel) { pn_note_on(&g_piano, note, vel); }
void instOff(void*, int note) { pn_note_off(&g_piano, note); }
void instSustain(void*, int down) { pn_pedal(&g_piano, down); }
void instAllOff(void*) { pn_all_off(&g_piano); }
void instSet(void*, int id, int v) {
  if (id < 0 || id >= PN_FIELDS) return;
  g_set[id] = v;
  piano_setting(&g_piano, id, v);
}
int instRender(void*, float* out) {
  pn_render(&g_piano, out);
  return g_piano.count;
}

const mf1_instrument_t kInstrument = {nullptr, instPrepare, instOn, instOff, instSustain, instAllOff, instSet, instRender};

}  // namespace

extern "C" {

void piano_install(void) {
  piano_default(0, g_set);
  mf1_set_instrument(&kInstrument);
}

void piano_apply(const int32_t s[PN_FIELDS]) {
  for (int k = 0; k < PN_FIELDS; k++) mf1_instrument_set(k, s[k]);
  int cc[24];
  piano_fx(s, cc);
  for (int k = 0; k < 24; k++)
    if (mf1_fx_get(k) != cc[k]) mf1_fx_set(k, cc[k]);
}

void piano_levels(float out[128]) {
  for (int n = 0; n < 128; n++) out[n] = g_ready ? std::sqrt(g_piano.level[n]) : 0.f;
}

int piano_oscillators(void) { return g_ready ? g_piano.count : 0; }
int piano_budget(void) { return g_piano.budget; }

}  // extern "C"
