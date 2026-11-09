#include <algorithm>
#include <cstring>

#include "MF1Core.h"
#include "internal.h"

using namespace phase;

namespace {

int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// num / den rounded half away from zero, in integers so a C port gives the same voice.
int roundDiv(int num, int den) { return num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den); }

// MF1Core's effect numbering, and the order of the chain, as in the other modes.
enum { kFilter, kReverb, kDelay, kDistortion, kChorus, kPhaser };
const int kChain[6] = {kFilter, kDistortion, kPhaser, kChorus, kDelay, kReverb};

const char* const kProcesses[PH_PROCESS_COUNT] = {"Phase", "Shift", "Build", "Canon",
                                                  "Pulse", "Stretch", "Result", "Tape"};
const char* const kSounds[PH_SOUND_COUNT] = {"Marimba", "Vibes", "Glock", "Piano", "Organ", "Wood", "Voice", "Bell"};
const char* const kSpreads[PH_SPREAD_COUNT] = {"Unison", "Close", "Open", "Wide"};

void setFx(int cc, int value) {
  if (mf1_fx_get(cc) != value) mf1_fx_set(cc, value);
}

void setEdit(int index, int value) {
  if (mf1_edit_get(index) != value) mf1_edit_set(index, value);
}

// A ratio times 100 as the DX7 writes it: coarse 0 is a half, and fine adds hundredths of the coarse ratio.
void ratioAt(uint8_t* p, int ratio) {
  int coarse = ratio < 100 ? 0 : std::min(31, ratio / 100);
  int fine = coarse == 0 ? roundDiv((ratio - 50) * 100, 50) : roundDiv((ratio - coarse * 100) * 100, coarse * 100);
  p[17] = 0, p[18] = (uint8_t)coarse, p[19] = (uint8_t)clampi(fine, 0, 99);
}

}  // namespace

int phase_piece_count(void) { return (int)pieces().size(); }

bool phase_piece(int index, PhasePiece* out) {
  if (index < 0 || index >= phase_piece_count() || !out) return false;
  const Piece& p = pieces()[index];
  memset(out, 0, sizeof(*out));
  strncpy(out->name, p.voiceName, sizeof(out->name) - 1);
  strncpy(out->title, p.title, sizeof(out->title) - 1);
  strncpy(out->blurb, p.blurb, sizeof(out->blurb) - 1);
  return true;
}

const char* phase_process_name(int p) { return kProcesses[clampi(p, 0, PH_PROCESS_COUNT - 1)]; }
const char* phase_sound_name(int s) { return kSounds[clampi(s, 0, PH_SOUND_COUNT - 1)]; }
const char* phase_spread_name(int s) { return kSpreads[clampi(s, 0, PH_SPREAD_COUNT - 1)]; }

void phase_default_piece(int piece, int32_t out[PH_FIELDS]) {
  piece = clampi(piece, 0, phase_piece_count() - 1);
  memcpy(out, pieces()[piece].s, sizeof(int32_t) * PH_FIELDS);
  out[PH_PIECE] = piece;
}

void phase_default_cell(int piece, PhaseCell* out) {
  const Piece& p = pieces()[clampi(piece, 0, phase_piece_count() - 1)];
  memset(out, -1, sizeof(out->note));
  out->length = (int8_t)std::min<size_t>(PH_MAX_STEPS, p.cell.size());
  for (int i = 0; i < out->length; i++) out->note[i] = (int8_t)p.cell[i];
}

void phase_range(int field, int* lo, int* hi) {
  int a = 0, b = 100;
  switch (field) {
    case PH_PIECE: b = phase_piece_count() - 1; break;
    case PH_PROCESS: b = PH_PROCESS_COUNT - 1; break;
    case PH_PARTS: a = 1, b = PH_MAX_PARTS; break;
    case PH_REPEATS: a = 1, b = 16; break;
    case PH_LENGTH: a = 2, b = PH_MAX_STEPS; break;
    case PH_TRANSPOSE: a = -12, b = 12; break;
    case PH_GATE: a = 5; break;
    case PH_SOUND: b = PH_SOUND_COUNT - 1; break;
    case PH_TEMPO: a = 40, b = 200; break;
    case PH_ECHO_STEPS: a = 1, b = 8; break;
    case PH_SPREAD: b = PH_SPREAD_COUNT - 1; break;
  }
  if (lo) *lo = a;
  if (hi) *hi = b;
}

void phase_clamp(int32_t s[PH_FIELDS]) {
  for (int i = 0; i < PH_FIELDS; i++) {
    int lo, hi;
    phase_range(i, &lo, &hi);
    s[i] = clampi(s[i], lo, hi);
  }
}

// The sound a piece's settings ask for. Bright moves the marked operators' levels by up to 14 either way; Decay
// slows or speeds every decay and the release, so notes ring longer or stop shorter.
void phase_voice(const int32_t settings[PH_FIELDS], uint8_t v[155]) {
  int32_t s[PH_FIELDS];
  memcpy(s, settings, sizeof(s));
  phase_clamp(s);
  const Sound& snd = sound(s[PH_SOUND]);
  memset(v, 0, 155);
  const int bright = roundDiv((s[PH_BRIGHT] - 50) * 14, 50);
  const int decay = s[PH_DECAY] - 50;
  for (int k = 0; k < 6; k++) {
    const Op& o = snd.op[k];
    uint8_t* p = v + (5 - k) * 21;  // VCED stores OP6 first
    p[0] = (uint8_t)clampi(o.r[0], 1, 99);
    p[1] = (uint8_t)clampi(o.r[1] - roundDiv(decay * 16, 50), 1, 99);
    p[2] = (uint8_t)clampi(o.r[2] - roundDiv(decay * 16, 50), 1, 99);
    p[3] = (uint8_t)clampi(o.r[3] - roundDiv(decay * 12, 50), 1, 99);
    for (int i = 0; i < 4; i++) p[4 + i] = (uint8_t)clampi(o.l[i], 0, 99);
    p[8] = 39;
    p[13] = (uint8_t)clampi(o.rateScale, 0, 7);
    p[14] = (uint8_t)clampi(o.ams, 0, 3);
    p[16] = (uint8_t)clampi(o.level + (o.bright && o.level > 0 ? bright : 0), 0, 99);
    ratioAt(p, o.ratio);
    p[20] = (uint8_t)clampi(o.detune, 0, 14);
  }
  for (int k = 0; k < 4; k++) v[126 + k] = 99, v[130 + k] = 50;
  v[134] = (uint8_t)(snd.algorithm - 1);
  v[135] = (uint8_t)clampi(snd.feedback, 0, 7);
  v[136] = 1;  // oscillator key sync: every stroke starts the same
  v[137] = (uint8_t)snd.lfoSpeed;
  v[138] = (uint8_t)snd.lfoDelay;
  v[139] = (uint8_t)snd.pmd;
  v[140] = (uint8_t)snd.amd;
  v[141] = snd.lfoSync ? 1 : 0;
  v[142] = (uint8_t)snd.lfoWave;
  v[143] = (uint8_t)snd.pms;
  v[144] = (uint8_t)clampi(24 + snd.transpose, 0, 48);
  const Piece& piece = pieces()[s[PH_PIECE]];
  size_t n = strlen(piece.voiceName);
  for (int i = 0; i < 10; i++) v[145 + i] = (uint8_t)(i < (int)n ? piece.voiceName[i] : ' ');
}

void phase_apply(const int32_t settings[PH_FIELDS]) {
  int32_t s[PH_FIELDS];
  memcpy(s, settings, sizeof(s));
  phase_clamp(s);
  const Sound& snd = sound(s[PH_SOUND]);

  uint8_t v[155];
  phase_voice(s, v);
  for (int i = 0; i < 145; i++) setEdit(i, v[i]);  // the name stays the piece's
  if (mf1_edit_mono()) mf1_edit_set_mono(false);
  if (mf1_env_get(0) != 0) mf1_env_set(0, 0);
  if (mf1_arp_on()) mf1_arp_enable(false);

  for (int pos = 0; pos < 6; pos++) {
    if (mf1_fx_order(pos) == kChain[pos]) continue;
    for (int other = pos + 1; other < 6; other++)
      if (mf1_fx_order(other) == kChain[pos]) mf1_fx_swap(pos, other);
  }
  // The sound is the voice's own: no filter, drive, phaser or chorus.
  setFx(0, 0);
  setFx(12, 0);
  setFx(20, 0);
  setFx(16, 0);

  // Room: the reverb's mix, with a longer tail as it grows.
  setFx(5, snd.roomType);
  setFx(6, clampi(24 + s[PH_ROOM] * 2 / 5, 0, 100));
  setFx(7, roundDiv(s[PH_ROOM] * 70, 100));
  setFx(4, s[PH_ROOM] > 0 ? 1 : 0);

  // Echo: the delay, its time a whole number of steps at the tempo as near as its 0.1 to 0.8 s reach allows.
  const double step = 15.0 / s[PH_TEMPO];
  int steps = s[PH_ECHO_STEPS];
  while (steps > 1 && steps * step > 0.8) steps--;
  while (steps < 64 && steps * step < 0.1) steps++;
  const double seconds = std::max(0.1, std::min(0.8, steps * step));
  setFx(10, clampi((int)((0.8 - seconds) / 0.7 * 100 + 0.5), 0, 100));
  setFx(9, 30);
  setFx(11, s[PH_ECHO]);
  setFx(8, s[PH_ECHO] > 0 ? 1 : 0);

  if (mf1_global_get(MF1_G_TEMPO) != s[PH_TEMPO]) mf1_global_set(MF1_G_TEMPO, s[PH_TEMPO]);
}

void phase_load(const int32_t settings[PH_FIELDS]) {
  int32_t s[PH_FIELDS];
  memcpy(s, settings, sizeof(s));
  phase_clamp(s);
  mf1_load_preset(s[PH_PIECE]);
  phase_apply(s);
}

namespace {

void emptyVoice(uint8_t v[155]) {
  memset(v, 0, 155);
  uint8_t* c = v + 5 * 21;
  for (int i = 0; i < 4; i++) c[i] = 99, c[4 + i] = 99;
  c[7] = 0, c[16] = 99, c[18] = 1, c[20] = 7;
  for (int k = 0; k < 4; k++) v[126 + k] = 99, v[130 + k] = 50;
  v[134] = 4, v[136] = 1, v[137] = 35, v[143] = 3, v[144] = 24;
  memcpy(v + 145, "INIT PHASE", 10);
}

// F0 43 0n 09 20 00, 32 voices, checksum, F7.
void bankDump(uint8_t out[4104]) {
  out[0] = 0xF0, out[1] = 0x43, out[2] = 0x00, out[3] = 0x09, out[4] = 0x20, out[5] = 0x00;
  uint8_t empty[155];
  emptyVoice(empty);
  for (int i = 0; i < 32; i++) {
    uint8_t v[155];
    if (i < phase_piece_count()) {
      int32_t d[PH_FIELDS];
      phase_default_piece(i, d);
      phase_voice(d, v);
    } else {
      memcpy(v, empty, sizeof(v));
    }
    packVoice(v, out + 6 + i * 128);
  }
  int sum = 0;
  for (int i = 0; i < 4096; i++) sum += out[6 + i];
  out[6 + 4096] = (uint8_t)((128 - (sum & 0x7f)) & 0x7f);
  out[6 + 4097] = 0xF7;
}

}  // namespace

void phase_export_bank(uint8_t out[4104]) { bankDump(out); }

void phase_install(void) {
  uint8_t dump[4104];
  bankDump(dump);
  for (int b = 0; b < 4; b++) mf1_import_factory_bank(b, dump, sizeof(dump));
}
