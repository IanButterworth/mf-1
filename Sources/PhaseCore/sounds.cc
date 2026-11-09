#include "internal.h"

namespace phase {

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
  std::vector<Sound> all(PH_SOUND_COUNT);
  // Every sound but the organ is on algorithm 5: three pairs, each a carrier and its modulator. The mallets are
  // struck: the level jumps at once and falls away with no sustain, and higher notes fall faster.

  // Marimba: the bar's fundamental, its overtone two octaves up as wooden bars are tuned, and a short knock of the
  // mallet. The fundamental's modulator drops away quickly, so the tone mellows after the strike.
  all[PH_MARIMBA] = Sound{"MARIMBA", 5, 5,
                          {op(100, 99, 99, 60, 40, 60, 99, 90, 0, 0, false, 3),
                           op(100, 64, 99, 70, 45, 60, 99, 55, 0, 0, true, 3),
                           op(400, 74, 99, 62, 50, 62, 99, 80, 0, 0, false, 4),
                           op(100, 38, 99, 80, 60, 60, 99, 0, 0, 0, true),
                           op(1000, 54, 99, 90, 72, 70, 99, 0, 0, 0),
                           op(300, 60, 99, 90, 72, 70, 99, 0, 0, 0, true)}};

  // Vibraphone: metal bars ring for seconds, with the same two-octave overtone and a slow tremolo from the motor.
  {
    Sound s{"VIBES", 5, 3,
            {op(100, 99, 99, 40, 32, 46, 99, 82, 0, 0, false, 2),
             op(100, 50, 99, 56, 40, 50, 99, 38, 0, 0, true, 2),
             op(400, 70, 99, 55, 40, 50, 99, 50, 0, 0, false, 3),
             op(100, 28, 99, 70, 50, 50, 99, 0, 0, 0, true),
             op(1000, 44, 99, 86, 60, 60, 99, 0, 0, 0),
             op(100, 38, 99, 86, 60, 60, 99, 0, 0, 0, true)}};
    s.op[0].ams = 2, s.op[2].ams = 2;
    s.lfoSpeed = 30, s.amd = 24, s.lfoWave = 4;
    all[PH_VIBES] = s;
  }

  // Glockenspiel: steel bars, with the inharmonic partials near 2.76 and 5.4 times the fundamental, two octaves up.
  {
    Sound s{"GLOCKEN", 5, 0,
            {op(100, 99, 99, 50, 34, 46, 99, 92, 0, 0, false, 3),
             op(100, 30, 99, 70, 50, 50, 99, 0, 0, 0, true),
             op(276, 90, 99, 55, 38, 50, 99, 85, 0, 0, false, 3),
             op(100, 0, 99, 99, 99, 99, 99, 99, 99, 0),
             op(540, 74, 99, 62, 50, 56, 99, 70, 0, 0, false, 4),
             op(700, 44, 99, 86, 60, 60, 99, 0, 0, 0, true)}};
    s.transpose = 24;
    s.roomType = 2;
    all[PH_GLOCK] = s;
  }

  // Piano: an electric piano of the period, three pairs a touch apart in tune. The fourteenth partial is the tine's
  // bright ping, gone in a moment.
  {
    Sound s{"EPIANO", 5, 6,
            {op(100, 92, 96, 30, 28, 62, 99, 78, 0, 0, false, 3),
             op(100, 60, 96, 56, 36, 70, 99, 52, 0, 0, true, 3),
             op(100, 80, 98, 34, 30, 62, 99, 72, 0, 0, false, 3),
             op(1400, 54, 99, 80, 60, 70, 99, 0, 0, 0, true, 4),
             op(100, 74, 98, 36, 30, 62, 99, 70, 0, 0, false, 3),
             op(100, 48, 99, 60, 40, 70, 99, 40, 0, 0, true, 3)}};
    s.op[2].detune = 10, s.op[4].detune = 4;
    s.roomType = 0;
    all[PH_PIANO] = s;
  }

  // Organ: a combo organ on algorithm 32, six sine drawbars that sound while the key is held and stop at once.
  // Bright pulls the upper drawbars out.
  {
    Sound s{"ORGAN", 32, 4,
            {op(100, 86, 99, 99, 99, 76, 99, 99, 99, 0),
             op(200, 78, 99, 99, 99, 76, 99, 99, 99, 0, true),
             op(300, 68, 99, 99, 99, 76, 99, 99, 99, 0, true),
             op(400, 64, 99, 99, 99, 76, 99, 99, 99, 0, true),
             op(50, 78, 99, 99, 99, 76, 99, 99, 99, 0),
             op(600, 54, 99, 99, 99, 76, 99, 99, 99, 0, true)}};
    s.op[1].detune = 8;
    s.lfoSpeed = 34, s.pmd = 3, s.pms = 2;
    all[PH_ORGAN] = s;
  }

  // Wood: a pitched wood block, a hollow knock with inharmonic partials and a burst of noise from a modulator fed
  // back on itself, all gone in a few tens of milliseconds.
  {
    Sound s{"WOODBLOCK", 5, 7,
            {op(100, 99, 99, 85, 58, 72, 99, 92, 0, 0, false, 3),
             op(170, 70, 99, 82, 62, 72, 99, 60, 0, 0, true),
             op(300, 90, 99, 85, 65, 72, 99, 80, 0, 0, false, 3),
             op(541, 44, 99, 85, 60, 72, 99, 0, 0, 0, true),
             op(600, 76, 99, 92, 70, 75, 99, 0, 0, 0),
             op(100, 80, 99, 94, 70, 75, 99, 0, 0, 0, true)}};
    s.transpose = 12;
    s.roomType = 0;
    all[PH_WOOD] = s;
  }

  // Voice: a soft sung "ooh", mostly the fundamental with a little of the second partial, swelling in and held,
  // with a slow vibrato that comes in after a moment.
  {
    Sound s{"OOH", 5, 0,
            {op(100, 96, 48, 40, 30, 56, 99, 92, 86, 0),
             op(100, 40, 45, 40, 30, 56, 99, 85, 80, 0, true),
             op(200, 70, 46, 40, 30, 56, 99, 90, 82, 0),
             op(100, 24, 45, 40, 30, 56, 99, 85, 80, 0, true),
             op(100, 86, 50, 40, 30, 56, 99, 92, 86, 0),
             op(300, 18, 45, 40, 30, 56, 99, 85, 80, 0, true)}};
    s.op[4].detune = 10;
    s.lfoSpeed = 34, s.lfoDelay = 50, s.pmd = 8, s.pms = 3;
    all[PH_VOICE] = s;
  }

  // Bell: a small bell, with a hum an octave under the strike and inharmonic partials above it, ringing long.
  {
    Sound s{"BELLS", 5, 0,
            {op(100, 94, 99, 30, 26, 38, 99, 80, 0, 0, false, 2),
             op(350, 56, 99, 45, 30, 40, 99, 44, 0, 0, true, 2),
             op(50, 70, 99, 28, 24, 36, 99, 82, 0, 0, false, 2),
             op(100, 30, 99, 50, 36, 40, 99, 30, 0, 0, true),
             op(240, 64, 99, 42, 32, 40, 99, 60, 0, 0, false, 3),
             op(140, 46, 99, 70, 50, 50, 99, 0, 0, 0, true)}};
    s.transpose = 12;
    all[PH_BELL] = s;
  }
  return all;
}

}  // namespace

const Sound& sound(int index) {
  static const std::vector<Sound> all = build();
  if (index < 0 || index >= (int)all.size()) index = 0;
  return all[index];
}

}  // namespace phase
