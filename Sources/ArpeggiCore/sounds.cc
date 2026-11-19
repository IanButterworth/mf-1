#include "internal.h"

namespace arpeggi {

namespace {

// rates and levels, then the ratio and level, with optional extras set after.
Op op(int ratio, int level, int r1, int r2, int r3, int r4, int l1, int l2, int l3, int l4, bool bright = false,
      int rateScale = 0) {
  Op o;
  o.ratio = ratio, o.level = level;
  o.r[0] = r1, o.r[1] = r2, o.r[2] = r3, o.r[3] = r4;
  o.l[0] = l1, o.l[1] = l2, o.l[2] = l3, o.l[3] = l4;
  o.bright = bright, o.rateScale = rateScale;
  return o;
}

std::vector<Sound> build() {
  std::vector<Sound> all(AP_SOUND_COUNT);
  // All on algorithm 5: three pairs, each a carrier and its modulator. Arpeggios are many short notes, so every
  // sound starts at once; the spacey ones keep ringing after the key lets go, the dry ones stop.

  // Pluck: a plucked string, dry. A bright pick that dies in a moment over a body that fades within a second.
  {
    Sound s{"PLUCK", 5, 5,
            {op(100, 99, 99, 50, 35, 62, 99, 72, 0, 0, false, 3),
             op(100, 72, 99, 72, 50, 62, 99, 40, 0, 0, true, 3),
             op(200, 68, 99, 60, 40, 62, 99, 58, 0, 0, false, 3),
             op(500, 50, 99, 86, 62, 62, 99, 0, 0, 0, true, 3),
             op(100, 80, 99, 52, 36, 62, 99, 70, 0, 0, false, 3),
             op(300, 54, 99, 82, 56, 62, 99, 18, 0, 0, true, 3)}};
    s.op[4].detune = 10;
    s.roomType = 0;
    all[AP_PLUCK] = s;
  }

  // Glass: a glassy bell-piano that keeps a little sustain while held and rings on in the hall, with chorus.
  {
    Sound s{"GLASS", 5, 3,
            {op(100, 95, 96, 32, 24, 40, 99, 86, 66, 0, false, 2),
             op(350, 58, 99, 42, 30, 40, 99, 56, 36, 0, true, 2),
             op(200, 72, 94, 30, 22, 40, 99, 80, 56, 0, false, 2),
             op(700, 44, 99, 58, 40, 40, 99, 26, 0, 0, true, 3),
             op(100, 84, 96, 32, 24, 40, 99, 86, 66, 0, false, 2),
             op(100, 48, 99, 38, 30, 40, 99, 60, 48, 0, true, 2)}};
    s.op[4].detune = 11;
    s.roomType = 1;
    s.chorus = 45;
    all[AP_GLASS] = s;
  }

  // Mallet: a marimba bar, dry: the fundamental, its overtone two octaves up, and a short knock of the mallet.
  {
    Sound s{"MALLET", 5, 5,
            {op(100, 99, 99, 60, 40, 60, 99, 90, 0, 0, false, 3),
             op(100, 64, 99, 70, 45, 60, 99, 55, 0, 0, true, 3),
             op(400, 74, 99, 62, 50, 62, 99, 80, 0, 0, false, 4),
             op(100, 38, 99, 80, 60, 60, 99, 0, 0, 0, true),
             op(1000, 54, 99, 90, 72, 70, 99, 0, 0, 0),
             op(300, 60, 99, 90, 72, 70, 99, 0, 0, 0, true)}};
    s.roomType = 0;
    all[AP_MALLET] = s;
  }

  // Pad: a soft string pad that swells in over a tenth of a second and fades slowly after the key, with a slow
  // vibrato and chorus. Long gates let its notes overlap into a wash.
  {
    Sound s{"PAD", 5, 5,
            {op(100, 96, 64, 30, 20, 34, 99, 92, 86, 0),
             op(100, 62, 62, 30, 20, 34, 99, 88, 80, 0, true),
             op(100, 88, 64, 30, 20, 34, 99, 92, 86, 0),
             op(200, 52, 62, 30, 20, 34, 99, 84, 76, 0, true),
             op(50, 78, 64, 30, 20, 34, 99, 92, 86, 0),
             op(100, 46, 62, 30, 20, 34, 99, 84, 76, 0, true)}};
    s.op[2].detune = 4, s.op[4].detune = 11;
    s.lfoSpeed = 30, s.pmd = 5, s.pms = 3;
    s.roomType = 1;
    s.chorus = 60;
    all[AP_PAD] = s;
  }

  // Chip: a buzzy square-ish lead, dry, like a home computer's sound chip. It holds while the key is down and
  // stops at once, so the gate is the whole envelope.
  {
    Sound s{"CHIP", 5, 7,
            {op(100, 92, 99, 99, 99, 80, 99, 99, 99, 0),
             op(200, 70, 99, 99, 99, 80, 99, 99, 99, 0, true),
             op(200, 58, 99, 99, 99, 80, 99, 99, 99, 0),
             op(200, 56, 99, 99, 99, 80, 99, 99, 99, 0, true),
             op(100, 76, 99, 99, 99, 80, 99, 99, 99, 0),
             op(100, 66, 99, 99, 99, 80, 99, 99, 99, 0, true)}};
    s.op[2].detune = 9;
    s.roomType = 0;
    all[AP_CHIP] = s;
  }

  // Bass: an octave down, a round body with a bright edge that closes like a filter as the note goes on.
  {
    Sound s{"BASS", 5, 4,
            {op(100, 99, 99, 50, 40, 70, 99, 82, 64, 0, false, 2),
             op(100, 80, 99, 66, 44, 70, 99, 52, 30, 0, true, 2),
             op(50, 82, 99, 50, 36, 70, 99, 86, 72, 0),
             op(100, 50, 99, 82, 60, 70, 99, 0, 0, 0, true),
             op(100, 0, 99, 99, 99, 99, 99, 99, 99, 0),
             op(100, 0, 99, 99, 99, 99, 99, 99, 99, 0)}};
    s.transpose = -12;
    s.roomType = 0;
    all[AP_BASS] = s;
  }

  // Bell: a small bell, with a hum an octave under the strike and inharmonic partials above it, ringing long.
  {
    Sound s{"BELL", 5, 0,
            {op(100, 94, 99, 30, 26, 38, 99, 80, 0, 0, false, 2),
             op(350, 56, 99, 45, 30, 40, 99, 44, 0, 0, true, 2),
             op(50, 70, 99, 28, 24, 36, 99, 82, 0, 0, false, 2),
             op(100, 30, 99, 50, 36, 40, 99, 30, 0, 0, true),
             op(240, 64, 99, 42, 32, 40, 99, 60, 0, 0, false, 3),
             op(140, 46, 99, 70, 50, 50, 99, 0, 0, 0, true)}};
    s.roomType = 1;
    all[AP_BELL] = s;
  }

  // Keys: an electric piano, three pairs a touch apart in tune, the fourteenth partial its tine's ping.
  {
    Sound s{"KEYS", 5, 6,
            {op(100, 92, 96, 30, 28, 62, 99, 78, 0, 0, false, 3),
             op(100, 60, 96, 56, 36, 70, 99, 52, 0, 0, true, 3),
             op(100, 80, 98, 34, 30, 62, 99, 72, 0, 0, false, 3),
             op(1400, 54, 99, 80, 60, 70, 99, 0, 0, 0, true, 4),
             op(100, 74, 98, 36, 30, 62, 99, 70, 0, 0, false, 3),
             op(100, 48, 99, 60, 40, 70, 99, 40, 0, 0, true, 3)}};
    s.op[2].detune = 10, s.op[4].detune = 4;
    s.roomType = 0;
    all[AP_KEYS] = s;
  }
  return all;
}

}  // namespace

const Sound& sound(int index) {
  static const std::vector<Sound> all = build();
  if (index < 0 || index >= (int)all.size()) index = 0;
  return all[index];
}

}  // namespace arpeggi
