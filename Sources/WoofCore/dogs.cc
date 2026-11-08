#include <cstring>

#include "internal.h"

namespace woof {

namespace {

// How a breed is shown: its place, its mood and how the keys play it. The barks are kept dry, with no echo, and
// reverb only indoors, so they sound close.
struct Show {
  int place, distance, echo;
  int mood = 50, howl = 0, growl = 0;
  int play = WF_SINGLE, pack = WF_PAIR;
  int tempo = 120, step = 1;
};

void show(Breed& b, Show s) {
  int32_t* d = b.day;
  d[WF_DOG] = 0;
  d[WF_SIZE] = 50;
  d[WF_GRUFF] = 50;
  d[WF_LENGTH] = 50;
  d[WF_YELP] = 50;
  d[WF_MOOD] = s.mood;
  d[WF_HOWL] = s.howl;
  d[WF_GROWL] = s.growl;
  d[WF_BREATH] = 50;
  d[WF_PLACE] = s.place;
  d[WF_DISTANCE] = s.distance;
  d[WF_ECHO] = s.echo;
  d[WF_SPACE] = 50;
  d[WF_PLAY] = s.play;
  d[WF_PACK] = s.pack;
  d[WF_TEMPO] = s.tempo;
  d[WF_STEP] = s.step;
}

std::vector<Breed> build() {
  std::vector<Breed> all;
  // voice, breed, word, blurb, accent,
  //   transpose, formant, rasp, throat, mouth, mouthLevel, breath, gruff, decay, start, rise, fall
  {
    Breed b{"SPOTS", "Dalmatian", "WOOF", "Bright, eager, round", 0xD8282E,
            0, 310, 340, 80, 72, 94, 84, 6, 52, -6, 12, -4};
    b.arpMode = 4;
    show(b, {WF_ROOFTOPS, 0, 0, 60, 0, 0, WF_SINGLE, WF_LITTER, 132, 3});
    all.push_back(b);
  }
  {
    Breed b{"GOLDIE", "Labrador", "WOOF", "Big, warm and friendly", 0xD9A440,
            -5, 300, 335, 78, 70, 94, 80, 5, 48, -5, 10, -5};
    show(b, {WF_PARK, 0, 0, 65, 0, 0, WF_REPEAT, WF_PAIR, 104, 1});
    all.push_back(b);
  }
  {
    Breed b{"TINY", "Chihuahua", "YIP", "A sharp little yap", 0xC77D4E,
            12, 335, 365, 80, 70, 90, 78, 6, 68, -3, 14, -2};
    show(b, {WF_PARLOUR, 10, 0, 70, 0, 0, WF_REPEAT, WF_TRIO, 150, 3});
    all.push_back(b);
  }
  {
    Breed b{"BAYER", "Beagle", "AROO", "A baying call across the fields", 0x9A5A2C,
            0, 305, 335, 76, 70, 94, 76, 5, 47, -6, 12, -8};
    show(b, {WF_FARM, 0, 0, 55, 45, 0, WF_SINGLE, WF_PAIR, 96, 1});
    all.push_back(b);
  }
  {
    Breed b{"SNOWY", "Husky", "AWOO", "A long howl at the moon", 0x5B86B8,
            -2, 295, 330, 72, 66, 94, 70, 4, 46, -8, 14, -10};
    show(b, {WF_RIVER, 0, 0, 40, 85, 0, WF_SINGLE, WF_TRIO, 80, 0});
    all.push_back(b);
  }
  {
    Breed b{"BRUISER", "Bulldog", "RUFF", "Short, gruff and grumbling", 0x8C6A4F,
            -7, 295, 325, 86, 74, 92, 86, 7, 58, -7, 10, -6};
    show(b, {WF_PARK, 0, 0, 35, 0, 35, WF_SINGLE, WF_PAIR, 100, 1});
    all.push_back(b);
  }
  {
    Breed b{"FIFI", "Poodle", "ARF", "Prim and precise", 0xC2508A,
            5, 318, 350, 76, 70, 92, 76, 4, 58, -4, 9, -3};
    show(b, {WF_PARLOUR, 10, 0, 60, 0, 0, WF_CHORUS, WF_PAIR, 120, 1});
    all.push_back(b);
  }
  {
    Breed b{"STUMPY", "Corgi", "BOOF", "A big bark from short legs", 0xE07B2A,
            3, 312, 345, 80, 72, 94, 82, 6, 54, -5, 11, -4};
    b.arpMode = 2;
    show(b, {WF_FARM, 0, 0, 75, 0, 0, WF_CHASE, WF_TRIO, 140, 3});
    all.push_back(b);
  }
  {
    Breed b{"SAUSAGE", "Dachshund", "BORK", "A deep bark from a long dog", 0xA0442A,
            0, 300, 335, 82, 72, 94, 82, 6, 50, -5, 11, -5};
    show(b, {WF_PARK, 0, 0, 55, 0, 10, WF_REPEAT, WF_PAIR, 112, 1});
    all.push_back(b);
  }
  {
    Breed b{"GUARD", "Shepherd", "WOOF", "Sharp and strong, on guard", 0x6E4A2A,
            -5, 305, 338, 84, 74, 94, 86, 6, 56, -6, 11, -6};
    show(b, {WF_FARM, 0, 0, 45, 0, 15, WF_CHORUS, WF_TRIO, 108, 1});
    all.push_back(b);
  }
  {
    Breed b{"GIANT", "Great Dane", "BWOOF", "A booming bark from up high", 0x4A5A78,
            -12, 285, 320, 80, 70, 96, 80, 6, 44, -6, 11, -7};
    show(b, {WF_RIVER, 0, 0, 45, 0, 0, WF_SINGLE, WF_PAIR, 84, 0});
    all.push_back(b);
  }
  {
    Breed b{"SNUFFLE", "Pug", "RUFF", "A snort, then a snuffly ruff", 0xB08A5A,
            7, 325, 310, 72, 66, 90, 90, 7, 60, -3, 11, -5};
    show(b, {WF_PARLOUR, 10, 0, 60, 0, 20, WF_SINGLE, WF_PAIR, 100, 1});
    all.push_back(b);
  }
  {
    // Otto: a brown, medium-sized cockapoo. A friendly, slightly soft woof between a spaniel's and a poodle's.
    Breed b{"OTTO", "Otto", "WOOF", "A brown cockapoo, soft and curly", 0x2F7FC1,
            2, 306, 342, 74, 68, 92, 74, 4, 54, -5, 12, -4};
    show(b, {WF_PARK, 0, 0, 68, 0, 0, WF_SINGLE, WF_PAIR, 116, 1});
    all.push_back(b);
  }
  return all;
}

}  // namespace

const std::vector<Breed>& breeds() {
  static const std::vector<Breed> all = build();
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

}  // namespace woof
