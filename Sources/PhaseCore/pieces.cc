#include <cstring>

#include "internal.h"

namespace phase {

namespace {

// How a piece is set: its process and how it moves, its sound and its time. The cells are all written for MF-1.
struct Setting {
  int process, motion, parts, repeats;
  int sound, gate, room;
  int tempo;
  int echo = 0, echoSteps = 3, spread = PH_UNISON;
};

void set(Piece& p, Setting s) {
  int32_t* d = p.s;
  d[PH_PIECE] = 0;
  d[PH_PROCESS] = s.process;
  d[PH_MOTION] = s.motion;
  d[PH_PARTS] = s.parts;
  d[PH_REPEATS] = s.repeats;
  d[PH_LENGTH] = (int32_t)p.cell.size();
  d[PH_DENSITY] = 100;
  d[PH_TRANSPOSE] = 0;
  d[PH_GATE] = s.gate;
  d[PH_SOUND] = s.sound;
  d[PH_BRIGHT] = 50;
  d[PH_DECAY] = 50;
  d[PH_ROOM] = s.room;
  d[PH_TEMPO] = s.tempo;
  d[PH_ECHO] = s.echo;
  d[PH_ECHO_STEPS] = s.echoSteps;
  d[PH_SPREAD] = s.spread;
}

constexpr int R = -1;

std::vector<Piece> build() {
  std::vector<Piece> all;
  {
    // D dorian, twelve even steps that climb and turn back, so every offset gives a new interlocking figure.
    Piece p{"MALLETS", "Two Mallets", "Two marimbas drift apart and lock",
            {62, 69, 65, 72, 67, 69, 62, 74, 65, 72, 67, 64}};
    set(p, {PH_PHASE, 60, 2, 4, PH_MARIMBA, 45, 22, 132});
    all.push_back(p);
  }
  {
    // Three wood blocks and five rests in twelve, so each jump moves the gaps against each other.
    Piece p{"WOOD SHIFT", "Wood Shift", "One block jumps ahead every six",
            {79, R, 72, 76, R, 79, 72, R, 79, R, R, 74}};
    set(p, {PH_SHIFT, 0, 2, 6, PH_WOOD, 30, 12, 152});
    all.push_back(p);
  }
  {
    // A pentatonic glockenspiel line with more rests than notes, for the rests to fill.
    Piece p{"GLASS", "Glass Steps", "Rests fill one by one, then empty",
            {69, R, R, 64, R, 66, R, R, 73, R, 64, R, R, 69, R, 71}};
    set(p, {PH_BUILD, 20, 2, 2, PH_GLOCK, 50, 30, 120});
    all.push_back(p);
  }
  {
    // A slow aeolian line for voices, four steps apart in canon.
    Piece p{"ROUND", "Round of Four", "Voices enter four steps apart",
            {60, R, 63, R, 67, R, 65, 63, 62, R, 58, R, 60, R, R, R}};
    set(p, {PH_CANON, 27, 4, 2, PH_VOICE, 92, 48, 76});
    all.push_back(p);
  }
  {
    // Eight notes in E flat; the chord is four of them at a time, moving one note along each stage.
    Piece p{"PULSE", "Pulse Field", "A chord pulses and swells in waves",
            {51, 58, 62, 65, 67, 70, 74, 69}};
    set(p, {PH_PULSE, 70, 4, 4, PH_PIANO, 40, 30, 112, 0, 3, PH_OPEN});
    all.push_back(p);
  }
  {
    // A spread A minor chord, broken, with a long gap after it for the notes to grow into.
    Piece p{"LONG CHORD", "Long Chord", "Short organ notes grow into a chord",
            {57, R, 64, R, 67, 71, R, 74, R, R, R, R}};
    set(p, {PH_STRETCH, 40, 2, 2, PH_ORGAN, 20, 30, 100});
    all.push_back(p);
  }
  {
    // G mixolydian with two rests, so the line the third part picks changes shape at every offset.
    Piece p{"HIDDEN", "Hidden Lines", "A third part finds a line inside",
            {67, 74, 72, R, 71, 69, 74, R, 67, 72, 71, 69}};
    set(p, {PH_RESULT, 55, 3, 4, PH_VIBES, 50, 30, 126});
    all.push_back(p);
  }
  {
    // Sparse pentatonic bells on three loops of slightly different lengths.
    Piece p{"TAPE BELLS", "Tape Bells", "Three loops slide past each other",
            {76, R, 79, 72, R, R, 74, R, 67, R, 72, R, R, 69, R, R}};
    set(p, {PH_TAPE, 20, 3, 4, PH_BELL, 70, 45, 84, 25, 3});
    all.push_back(p);
  }
  return all;
}

}  // namespace

const std::vector<Piece>& pieces() {
  static const std::vector<Piece> all = build();
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

}  // namespace phase
