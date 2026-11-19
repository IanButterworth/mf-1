#include <cstring>

#include "internal.h"

namespace arpeggi {

namespace {

// One lane: its shape, rate, rhythm and gate.
struct Lane {
  int shape, rate, rhythm, gate;
};

// How a scene is set. The seeds are all chords written for MF-1.
struct Setting {
  int relation, duo;
  Lane a, b;
  int hold, ring, span, pitch;
  int sound, room, tempo;
  int swing = 0, echo = 0, echoSteps = 3;
};

void set(Scene& c, Setting s) {
  int32_t* d = c.s;
  d[AP_SCENE] = 0;
  d[AP_RELATION] = s.relation;
  d[AP_DUO] = s.duo;
  d[AP_A_SHAPE] = s.a.shape, d[AP_A_RATE] = s.a.rate, d[AP_A_RHYTHM] = s.a.rhythm, d[AP_A_GATE] = s.a.gate;
  d[AP_B_SHAPE] = s.b.shape, d[AP_B_RATE] = s.b.rate, d[AP_B_RHYTHM] = s.b.rhythm, d[AP_B_GATE] = s.b.gate;
  d[AP_HOLD_MODE] = s.hold;
  d[AP_RING_SIZE] = s.ring;
  d[AP_SPAN] = s.span;
  d[AP_B_PITCH] = s.pitch;
  d[AP_SOUND] = s.sound;
  d[AP_BRIGHT] = 50;
  d[AP_DECAY] = 50;
  d[AP_ROOM] = s.room;
  d[AP_TEMPO] = s.tempo;
  d[AP_SWING] = s.swing;
  d[AP_ECHO] = s.echo;
  d[AP_ECHO_STEPS] = s.echoSteps;
}

std::vector<Scene> build() {
  std::vector<Scene> all;
  {
    // The ring: play five keys and they loop in the order played; every key after that writes over the oldest, so
    // the loop changes a note at a time. B runs up the same five in triplets an octave higher, three against four.
    Scene c{"RING FIVE", "Ring of Five", "Each new key replaces the oldest of five", {62, 69, 65, 72, 64}};
    set(c, {AP_TOGETHER, 1, {AP_ORDER, AP_R16, AP_EVERY, 60}, {AP_UP, AP_R8T, AP_EVERY, 40}, AP_RING, 5, 1, 12,
            AP_PLUCK, 18, 112});
    all.push_back(c);
  }
  {
    // Dry and close: one line of sixteenths, the notes taken in turn from A going up and B coming down.
    Scene c{"CLOCKWORK", "Clockwork", "Up and down share one line, a note each", {60, 63, 67, 70}};
    set(c, {AP_HOCKET, 1, {AP_UP, AP_R16, AP_EVERY, 45}, {AP_DOWN, AP_R16, AP_EVERY, 30}, AP_LATCH, 4, 2, 0,
            AP_MALLET, 0, 126});
    all.push_back(c);
  }
  {
    // Spacey: slow notes held past their step, one lane closing in on the middle, the other wandering in dotted
    // eighths an octave up, in a long hall with echoes a dotted eighth apart.
    Scene c{"NEBULA", "Nebula", "Inward and wandering, in a wide hall", {53, 57, 60, 64, 67}};
    set(c, {AP_TOGETHER, 1, {AP_CONVERGE, AP_R8, AP_EVERY, 160}, {AP_WALK, AP_R8D, AP_EVERY, 120}, AP_LATCH, 4, 2,
            12, AP_PAD, 85, 76, 0, 45, 3});
    all.push_back(c);
  }
  {
    // Call and response: A bounces through the chord for a bar, then B answers with the thumb on the root.
    Scene c{"ANSWER", "Call and Answer", "A asks for a bar, B answers the next", {57, 60, 64, 69}};
    set(c, {AP_ANSWER, 1, {AP_BOUNCE, AP_R16, AP_EVERY, 55}, {AP_THUMB, AP_R16, AP_GALLOP, 50}, AP_HOLD, 4, 2, 12,
            AP_KEYS, 25, 104, 30, 15, 2});
    all.push_back(c);
  }
  {
    // A ring of four shuffled, and B singing the same line three steps behind, an octave up, as bells in a church.
    Scene c{"CANONDROPS", "Canon Drops", "B follows three steps behind, an octave up", {67, 69, 74, 76}};
    set(c, {AP_CANON, 1, {AP_SHUFFLE, AP_R8, AP_EVERY, 80}, {AP_UP, AP_R8, AP_EVERY, 60}, AP_RING, 4, 1, 12,
            AP_BELL, 70, 92, 0, 35, 4});
    all.push_back(c);
  }
  {
    // Dry polyrhythm: five strikes in every eight sixteenths against straight triplets an octave down.
    Scene c{"GEARS", "Gears", "Five in eight against triplets", {64, 67, 71, 74}};
    set(c, {AP_TOGETHER, 1, {AP_UP, AP_R16, AP_E5, 40}, {AP_DOWN, AP_R16T, AP_EVERY, 30}, AP_LATCH, 4, 2, -12,
            AP_CHIP, 0, 120});
    all.push_back(c);
  }
  {
    // Spacey: strikes fall closer and closer like a bouncing ball, and B mirrors the shape at half the speed.
    Scene c{"FALLLIGHT", "Falling Light", "A ball bounces; B mirrors it at half speed", {62, 66, 69, 76}};
    set(c, {AP_MIRROR, 1, {AP_UP, AP_R16, AP_FALL, 90}, {AP_UP, AP_R8, AP_FALL, 70}, AP_LATCH, 4, 3, 12, AP_GLASS,
            80, 88, 0, 40, 3});
    all.push_back(c);
  }
  {
    // A spiral with ratchets against a triplet bounce an octave up, swapping shapes every two bars.
    Scene c{"TRADEWINDS", "Trade Winds", "The lanes swap shapes every two bars", {48, 51, 55, 58, 60}};
    set(c, {AP_TRADE, 1, {AP_SPIRAL, AP_R16, AP_RATCHET, 45}, {AP_BOUNCE, AP_R8T, AP_EVERY, 60}, AP_LATCH, 4, 2,
            12, AP_BASS, 10, 118, 20, 20, 3});
    all.push_back(c);
  }
  for (size_t i = 0; i < all.size(); i++) all[i].s[AP_SCENE] = (int32_t)i;
  return all;
}

}  // namespace

const std::vector<Scene>& scenes() {
  static const std::vector<Scene> all = build();
  return all;
}

void packVoice(const uint8_t v[155], uint8_t out[128]) {
  for (int op = 0; op < 6; op++) {
    const uint8_t* p = v + op * 21;
    uint8_t* o = out + op * 17;
    memcpy(o, p, 11);
    o[11] = (uint8_t)((p[12] << 2) | p[11]);
    o[12] = (uint8_t)((p[20] << 3) | p[13]);
    o[13] = (uint8_t)((p[15] << 2) | p[14]);
    o[14] = p[16];
    o[15] = (uint8_t)((p[18] << 1) | p[17]);
    o[16] = p[19];
  }
  memcpy(out + 102, v + 126, 8);
  out[110] = v[134];
  out[111] = (uint8_t)((v[136] << 3) | v[135]);
  out[112] = v[137];
  out[113] = v[138];
  out[114] = v[139];
  out[115] = v[140];
  out[116] = (uint8_t)((v[143] << 4) | (v[142] << 1) | v[141]);
  out[117] = v[144];
  memcpy(out + 118, v + 145, 10);
}

}  // namespace arpeggi
